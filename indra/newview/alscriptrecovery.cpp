/**
 * @file alscriptrecovery.cpp
 * @brief Script Studio's unsaved work, kept on disk until it is saved, so that a crash or a lost object does not take it.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alscriptrecovery.h"

#include "fsyspath.h"
#include "llfile.h"
#include "llsdserialize.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

#if LL_WINDOWS
#include <io.h>
#else
#include <unistd.h>
#endif

namespace
{
    const char* const EXTENSION = ".llsd";
    // What a file is written as first, beside where it goes.
    const char* const HALF_WRITTEN = ".tmp";

    const char* stateName(ALScriptRecoveryEntry::State state)
    {
        switch (state)
        {
            case ALScriptRecoveryEntry::State::Kept:      return "kept";
            case ALScriptRecoveryEntry::State::Discarded: return "discarded";
            default:                                      return "unsaved";
        }
    }

    ALScriptRecoveryEntry::State stateFrom(const std::string& name)
    {
        return name == "kept" ? ALScriptRecoveryEntry::State::Kept
               : name == "discarded" ? ALScriptRecoveryEntry::State::Discarded
                                     : ALScriptRecoveryEntry::State::Unsaved;
    }

    std::string withSeparator(std::string directory)
    {
        if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
        {
            directory += '/';
        }
        return directory;
    }

    // A file's name, split at its dots: the key's hash, the session, and,
    // for a discarded one, when; the extension gone.
    std::vector<std::string> partsOf(const std::string& name)
    {
        std::vector<std::string> parts;
        if (name.size() <= strlen(EXTENSION) || name.compare(name.size() - strlen(EXTENSION), std::string::npos, EXTENSION) != 0)
        {
            return parts;
        }
        const std::string stem = name.substr(0, name.size() - strlen(EXTENSION));
        size_t            at   = 0;
        while (at <= stem.size())
        {
            const size_t dot = stem.find('.', at);
            parts.push_back(stem.substr(at, dot == std::string::npos ? std::string::npos : dot - at));
            if (dot == std::string::npos)
            {
                break;
            }
            at = dot + 1;
        }
        return parts;
    }

    // The regular files of a folder, by their names; none where there is no
    // such folder.
    std::vector<std::string> namesIn(const std::string& folder)
    {
        std::vector<std::string> names;
        std::error_code          ec;
        for (std::filesystem::directory_iterator it(fsyspath(folder), ec), end; !ec && it != end; it.increment(ec))
        {
            std::error_code kind;
            if (it->is_regular_file(kind))
            {
                names.push_back(fsyspath(it->path().filename()).string());
            }
        }
        return names;
    }
}

LLSD ALScriptRecoveryEntry::asLLSD() const
{
    LLSD sd;
    sd["version"]        = 1;
    sd["key"]            = key;
    sd["session"]        = session;
    sd["state"]          = stateName(state);
    sd["when"]           = when;
    sd["object"]         = object;
    sd["item"]           = item;
    sd["file"]           = file;
    sd["name"]           = name;
    sd["object_name"]    = objectName;
    sd["region"]         = region;
    sd["lua"]            = lua;
    sd["notecard"]       = notecard;
    sd["wrapped"]        = wrapped;
    sd["compile_target"] = compileTarget;
    sd["base_asset"]     = baseAsset;
    sd["text"]           = text;
    if (embedded.isArray() && embedded.size() > 0)
    {
        sd["embedded"] = embedded;
    }
    if (history.isMap())
    {
        sd["history"] = history;
    }
    if (caretLine >= 0)
    {
        sd["caret"] = LLSD::emptyArray().with(0, caretLine).with(1, caretColumn);
    }
    return sd;
}

// static
bool ALScriptRecoveryEntry::fromLLSD(const LLSD& sd, ALScriptRecoveryEntry& out)
{
    if (!sd.isMap() || !sd.has("key") || !sd.has("text"))
    {
        return false;
    }
    out.key           = sd["key"].asString();
    out.session       = sd["session"].asString();
    out.state         = stateFrom(sd["state"].asString());
    out.when          = sd["when"].asDate();
    out.object        = sd["object"].asUUID();
    out.item          = sd["item"].asUUID();
    out.file          = sd["file"].asString();
    out.name          = sd["name"].asString();
    out.objectName    = sd["object_name"].asString();
    out.region        = sd["region"].asString();
    out.lua           = sd["lua"].asBoolean();
    out.notecard      = sd["notecard"].asBoolean();
    out.wrapped       = sd["wrapped"].asBoolean();
    out.compileTarget = sd["compile_target"].asString();
    out.baseAsset     = sd["base_asset"].asUUID();
    out.text          = sd["text"].asString();
    out.embedded      = sd.has("embedded") ? sd["embedded"] : LLSD::emptyArray();
    out.history       = sd["history"];
    out.caretLine     = sd.has("caret") ? sd["caret"][0].asInteger() : -1;
    out.caretColumn   = sd.has("caret") ? sd["caret"][1].asInteger() : -1;
    return !out.key.empty();
}

// static
F64 ALScriptRecoveryRetry::delayAfter(S32 failures)
{
    return FIRST * std::pow(3.0, static_cast<F64>(llmax(failures, 1) - 1));
}

ALScriptRecoveryStore::ALScriptRecoveryStore(std::string directory, std::string session)
:   mDirectory(withSeparator(std::move(directory))),
    mDiscarded(mDirectory + "discarded/"),
    mSession(std::move(session))
{
}

// static
std::string ALScriptRecoveryStore::keyOf(const LLUUID& object, const LLUUID& item, const std::string& file)
{
    if (!file.empty())
    {
        return "disk:" + file;
    }
    return object.isNull() ? "item:" + item.asString() : "task:" + object.asString() + ":" + item.asString();
}

std::string ALScriptRecoveryStore::fileOf(const std::string& key) const
{
    // By a hash of the key, which may be a path of any length and any
    // letters.
    return LLUUID::generateNewID(key).asString();
}

std::string ALScriptRecoveryStore::pathOf(const std::string& key, const std::string& session) const
{
    return mDirectory + fileOf(key) + "." + session + EXTENSION;
}

// static
bool ALScriptRecoveryStore::writeWhole(const std::string& path, const LLSD& sd)
{
    // As notation: a person can still read it, and it is a fraction of
    // what XML makes of a history's many small edits.
    std::ostringstream text;
    LLSDSerialize::serialize(sd, text, LLSDSerialize::LLSD_NOTATION, LLSDFormatter::OPTIONS_NONE);
    const std::string written = text.str();
    // Beside it first, forced out to the disk, then put in its place: a
    // crash, or the power going, leaves the last whole text or this one.
    const std::string beside = path + HALF_WRITTEN;
    LLFILE*           file   = LLFile::fopen(beside, LLFILE_MODE("wb"));
    if (!file)
    {
        return false;
    }
    const bool whole = fwrite(written.data(), 1, written.size(), file) == written.size() && fflush(file) == 0;
#if LL_WINDOWS
    _commit(_fileno(file));
#else
    fsync(fileno(file));
#endif
    fclose(file);
    if (!whole || LLFile::rename(beside, path) != 0)
    {
        LLFile::remove(beside, ENOENT);
        return false;
    }
    return true;
}

// static
bool ALScriptRecoveryStore::readEntry(const std::string& path, ALScriptRecoveryEntry& out)
{
    std::error_code   ec;
    const std::string text = LLFile::getContents(fsyspath(path), ec);
    if (ec || text.empty())
    {
        return false;
    }
    std::istringstream in(text);
    LLSD               sd;
    if (!LLSDSerialize::deserialize(sd, in, static_cast<llssize>(text.size())) || !ALScriptRecoveryEntry::fromLLSD(sd, out))
    {
        return false;
    }
    out.path = path;
    return true;
}

bool ALScriptRecoveryStore::write(ALScriptRecoveryEntry entry)
{
    if (entry.key.empty())
    {
        return false;
    }
    LLFile::mkdir(mDirectory);
    entry.session = mSession;
    entry.when    = LLDate::now();
    return writeWhole(pathOf(entry.key, mSession), entry.asLLSD());
}

void ALScriptRecoveryStore::forget(const std::string& key)
{
    LLFile::remove(pathOf(key, mSession), ENOENT);
}

bool ALScriptRecoveryStore::setAside(ALScriptRecoveryEntry entry)
{
    if (entry.key.empty())
    {
        return false;
    }
    if (entry.session.empty())
    {
        entry.session = mSession;
    }
    entry.state = ALScriptRecoveryEntry::State::Discarded;
    entry.when  = LLDate::now();
    LLFile::mkdir(mDirectory);
    LLFile::mkdir(mDiscarded);
    // Named by when to the millisecond, which prune reads without opening
    // it, and which two set aside in a moment do not share.
    const std::string target = mDiscarded + fileOf(entry.key) + "." + entry.session + "." +
                               std::to_string(static_cast<S64>(entry.when.secondsSinceEpoch() * 1000.0)) + EXTENSION;
    return writeWhole(target, entry.asLLSD());
}

bool ALScriptRecoveryStore::discard(const ALScriptRecoveryEntry& entry)
{
    if (entry.state == ALScriptRecoveryEntry::State::Discarded)
    {
        return true;
    }
    if (!setAside(entry))
    {
        return false;
    }
    remove(entry);
    return true;
}

void ALScriptRecoveryStore::remove(const ALScriptRecoveryEntry& entry)
{
    if (!entry.path.empty())
    {
        LLFile::remove(entry.path, ENOENT);
    }
}

bool ALScriptRecoveryStore::letGo(const Parting& parting)
{
    bool let_go = false;
    bool safe   = true;
    if (parting.carrying)
    {
        // Never put in. A text carried with no file of its own is set aside
        // as it came; an entry on disk is still what it was.
        if (parting.tookUp && parting.tookUp->path.empty())
        {
            let_go = safe = setAside(*parting.tookUp);
        }
        if (!parting.tookUp || let_go)
        {
            forget(parting.key);
        }
    }
    else if (parting.unsaved)
    {
        let_go = safe = setAside(*parting.unsaved);
        if (safe)
        {
            forget(parting.key);
        }
    }
    else
    {
        // Saved, never changed, or never loaded: nothing of its own to keep.
        forget(parting.key);
        let_go = parting.settled;
    }
    if (let_go && parting.tookUp)
    {
        remove(*parting.tookUp);
    }
    return safe;
}

void ALScriptRecoveryStore::listIn(const std::string& folder, std::vector<ALScriptRecoveryEntry>& out) const
{
    for (const std::string& name : namesIn(folder))
    {
        if (partsOf(name).size() < 2)
        {
            continue;
        }
        ALScriptRecoveryEntry entry;
        if (readEntry(folder + name, entry))
        {
            out.push_back(std::move(entry));
        }
    }
}

std::vector<ALScriptRecoveryEntry> ALScriptRecoveryStore::list() const
{
    std::vector<ALScriptRecoveryEntry> entries;
    listIn(mDirectory, entries);
    listIn(mDiscarded, entries);
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ALScriptRecoveryEntry& a, const ALScriptRecoveryEntry& b) { return a.when.secondsSinceEpoch() > b.when.secondsSinceEpoch(); });
    return entries;
}

std::vector<ALScriptRecoveryEntry> ALScriptRecoveryStore::left() const
{
    // The discarded are not read: they are in a folder of their own.
    std::vector<ALScriptRecoveryEntry> all;
    listIn(mDirectory, all);
    std::vector<ALScriptRecoveryEntry> entries;
    for (ALScriptRecoveryEntry& entry : all)
    {
        if (entry.state != ALScriptRecoveryEntry::State::Discarded && entry.session != mSession)
        {
            entries.push_back(std::move(entry));
        }
    }
    std::stable_sort(entries.begin(), entries.end(),
                     [](const ALScriptRecoveryEntry& a, const ALScriptRecoveryEntry& b) { return a.when.secondsSinceEpoch() > b.when.secondsSinceEpoch(); });
    return entries;
}

std::optional<ALScriptRecoveryEntry> ALScriptRecoveryStore::leftFor(const std::string& key) const
{
    // By the files' names first: only this key's are read.
    const std::string                    hash = fileOf(key);
    std::optional<ALScriptRecoveryEntry> newest;
    for (const std::string& name : namesIn(mDirectory))
    {
        const std::vector<std::string> parts = partsOf(name);
        if (parts.size() != 2 || parts[0] != hash || parts[1] == mSession)
        {
            continue;
        }
        ALScriptRecoveryEntry entry;
        if (readEntry(mDirectory + name, entry) && entry.key == key && entry.state != ALScriptRecoveryEntry::State::Discarded &&
            (!newest || entry.when.secondsSinceEpoch() > newest->when.secondsSinceEpoch()))
        {
            newest = std::move(entry);
        }
    }
    return newest;
}

bool ALScriptRecoveryStore::hasOffers() const
{
    for (const std::string& name : namesIn(mDirectory))
    {
        const std::vector<std::string> parts = partsOf(name);
        if (parts.size() == 2 && parts[1] != mSession)
        {
            return true;
        }
    }
    for (const std::string& name : namesIn(mDiscarded))
    {
        if (partsOf(name).size() >= 2)
        {
            return true;
        }
    }
    return false;
}

void ALScriptRecoveryStore::prune(F64 max_age_seconds, const LLDate& now)
{
    for (const std::string& name : namesIn(mDiscarded))
    {
        // When, from the name; read from the file only where the name does
        // not say it.
        const std::vector<std::string> parts = partsOf(name);
        if (parts.size() < 2)
        {
            continue;
        }
        F64  when  = 0.0;
        bool known = false;
        if (parts.size() == 3)
        {
            char*       end    = nullptr;
            const S64   millis = std::strtoll(parts[2].c_str(), &end, 10);
            known              = end && *end == '\0' && !parts[2].empty();
            when               = static_cast<F64>(millis) / 1000.0;
        }
        if (!known)
        {
            ALScriptRecoveryEntry entry;
            if (!readEntry(mDiscarded + name, entry))
            {
                continue;
            }
            when = entry.when.secondsSinceEpoch();
        }
        if (now.secondsSinceEpoch() - when > max_age_seconds)
        {
            LLFile::remove(mDiscarded + name, ENOENT);
        }
    }
    // A write cut short -- a crash between writing and putting in place --
    // leaves its half beside the entry, which nothing reads. This session
    // has written nothing yet where it prunes as it starts, and writes
    // whole in one call where it does not.
    for (const std::string& folder : { mDirectory, mDiscarded })
    {
        for (const std::string& name : namesIn(folder))
        {
            const size_t tail = strlen(HALF_WRITTEN);
            if (name.size() > tail && name.compare(name.size() - tail, tail, HALF_WRITTEN) == 0 && name.find(mSession) == std::string::npos)
            {
                LLFile::remove(folder + name, ENOENT);
            }
        }
    }
}
