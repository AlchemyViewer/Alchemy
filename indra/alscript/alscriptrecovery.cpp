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
#include "lltimer.h"
#include "threadpool.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
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

std::string ALScriptRecoveryEntry::whenSaid() const
{
    const time_t moment = static_cast<time_t>(when.secondsSinceEpoch());
    struct tm    local;
#if LL_WINDOWS
    localtime_s(&local, &moment);
#else
    localtime_r(&moment, &local);
#endif
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

namespace
{
    // What a listing reads of an entry, and the rest.
    LLSD metaOf(const ALScriptRecoveryEntry& e, S32 version);
    LLSD bodyOf(const ALScriptRecoveryEntry& entry);
    bool metaFrom(const LLSD& sd, ALScriptRecoveryEntry& out);
    void bodyFrom(const LLSD& sd, ALScriptRecoveryEntry& out);
}

LLSD ALScriptRecoveryEntry::asLLSD() const
{
    LLSD       sd   = metaOf(*this, 1);
    const LLSD body = bodyOf(*this);
    for (LLSD::map_const_iterator it = body.beginMap(); it != body.endMap(); ++it)
    {
        sd[it->first] = it->second;
    }
    return sd;
}

LLSD ALScriptRecoveryEntry::historyOf() const
{
    if (history.isMap() || historyWritten.empty())
    {
        return history;
    }
    std::istringstream in(historyWritten);
    LLSD               read;
    return LLSDSerialize::fromNotation(read, in, static_cast<llssize>(historyWritten.size())) > 0 ? read : LLSD();
}

std::string ALScriptRecoveryEntry::written() const
{
    std::ostringstream meta;
    LLSDSerialize::toNotation(metaOf(*this, 2), meta);
    std::ostringstream body;
    LLSDSerialize::toNotation(bodyOf(*this), body);
    std::string out = meta.str() + "\n" + body.str();
    if (!historyWritten.empty())
    {
        // Into the body's map as it stands written, before its closing
        // brace: the body always has its text before it.
        const size_t close = out.rfind('}');
        if (close != std::string::npos)
        {
            out.insert(close, ",'history':" + historyWritten);
        }
    }
    return out;
}

namespace
{
    LLSD metaOf(const ALScriptRecoveryEntry& e, S32 version)
    {
        LLSD sd;
        sd["version"]        = version;
        sd["key"]            = e.key;
        sd["session"]        = e.session;
        sd["state"]          = stateName(e.state);
        sd["when"]           = e.when;
        sd["object"]         = e.object;
        sd["item"]           = e.item;
        sd["file"]           = e.file;
        sd["name"]           = e.name;
        sd["object_name"]    = e.objectName;
        sd["region"]         = e.region;
        sd["lua"]            = e.lua;
        sd["notecard"]       = e.notecard;
        sd["wrapped"]        = e.wrapped;
        sd["compile_target"] = e.compileTarget;
        sd["base_asset"]     = e.baseAsset;
        return sd;
    }

    LLSD bodyOf(const ALScriptRecoveryEntry& e)
    {
        LLSD sd;
        sd["text"] = e.text;
        if (e.embedded.isArray() && e.embedded.size() > 0)
        {
            sd["embedded"] = e.embedded;
        }
        if (e.historyWritten.empty() && e.history.isMap())
        {
            sd["history"] = e.history;
        }
        if (e.caretLine >= 0)
        {
            sd["caret"] = LLSD::emptyArray().with(0, e.caretLine).with(1, e.caretColumn);
        }
        if (e.pickedTarget)
        {
            sd["picked_target"] = *e.pickedTarget;
        }
        if (e.pickedExperience)
        {
            sd["picked_experience"] = *e.pickedExperience;
        }
        return sd;
    }

    bool metaFrom(const LLSD& sd, ALScriptRecoveryEntry& out)
    {
        if (!sd.isMap() || !sd.has("key"))
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
        return !out.key.empty();
    }

    void bodyFrom(const LLSD& sd, ALScriptRecoveryEntry& out)
    {
        out.text             = sd["text"].asString();
        out.embedded         = sd.has("embedded") ? sd["embedded"] : LLSD::emptyArray();
        out.history          = sd["history"];
        out.caretLine        = sd.has("caret") ? sd["caret"][0].asInteger() : -1;
        out.caretColumn      = sd.has("caret") ? sd["caret"][1].asInteger() : -1;
        out.pickedTarget     = sd.has("picked_target") ? std::optional<std::string>(sd["picked_target"].asString()) : std::nullopt;
        out.pickedExperience = sd.has("picked_experience") ? std::optional<LLUUID>(sd["picked_experience"].asUUID()) : std::nullopt;
        out.whole            = true;
    }
}

// static
bool ALScriptRecoveryEntry::fromLLSD(const LLSD& sd, ALScriptRecoveryEntry& out)
{
    if (!sd.isMap() || !sd.has("text") || !metaFrom(sd, out))
    {
        return false;
    }
    bodyFrom(sd, out);
    return true;
}

// static
F64 ALScriptRecoveryRetry::delayAfter(S32 failures)
{
    return FIRST * std::pow(3.0, static_cast<F64>(llmax(failures, 1) - 1));
}

// What writeSoon has waiting for the writer: the entries, by key, each
// with where it goes -- handed over whole, what it holds of LLSD made on
// the thread that asked and held by nothing else, since an LLSD's count of
// who holds it is no thread's but one's -- whether one is being written
// now, and whether the writer has been asked to write them.
struct ALScriptRecoveryStore::Writer
{
    struct Waiting
    {
        std::string           path;
        ALScriptRecoveryEntry entry;
        bool                  durable = false;
    };
    std::mutex                     mutex;
    std::condition_variable        changed;
    std::map<std::string, Waiting> waiting;
    bool                           busy   = false;
    bool                           posted = false;
    std::vector<std::string>       failed;

    // Everything waiting written, what comes meanwhile too.
    void drain()
    {
        std::unique_lock<std::mutex> lock(mutex);
        while (!waiting.empty())
        {
            auto one = waiting.extract(waiting.begin());
            busy     = true;
            lock.unlock();
            const bool        written = writeWhole(one.mapped().path, one.mapped().entry.written(), one.mapped().durable);
            const std::string key     = one.key();
            // Let go of here, the only thread that holds it.
            one = {};
            lock.lock();
            busy = false;
            if (!written)
            {
                failed.push_back(key);
            }
            changed.notify_all();
        }
        posted = false;
        changed.notify_all();
    }
};

struct ALScriptRecoveryWriter::Pool
{
    // Only as wide as one: two stores' writes, or two of one store's, one
    // after the other.
    LL::ThreadPool pool{ "ScriptRecovery", 1, 1024 * 1024, /*auto_shutdown*/ false, /*fixed_width*/ true };
};

ALScriptRecoveryWriter::ALScriptRecoveryWriter() : mPool(std::make_unique<Pool>())
{
    mPool->pool.start();
}

ALScriptRecoveryWriter::~ALScriptRecoveryWriter() = default;

void ALScriptRecoveryWriter::cleanupSingleton()
{
    // What is posted is written before it stops.
    mPool->pool.close();
}

bool ALScriptRecoveryWriter::post(std::function<void()> work)
{
    return mPool->pool.getQueue().post(std::move(work));
}

ALScriptRecoveryStore::ALScriptRecoveryStore(std::string directory, std::string session)
:   mDirectory(withSeparator(std::move(directory))),
    mDiscarded(mDirectory + "discarded/"),
    mSession(std::move(session))
{
}

ALScriptRecoveryStore::~ALScriptRecoveryStore()
{
    flush();
}

void ALScriptRecoveryStore::writeSoon(ALScriptRecoveryEntry entry, bool durable)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    if (entry.key.empty())
    {
        return;
    }
    makeFolders(false);
    changed();
    entry.session = mSession;
    entry.when    = LLDate::now();
    // Handed over whole, and written out there: the thread holds the only
    // hold on it.
    Writer::Waiting written;
    written.path          = pathOf(entry.key, mSession);
    const std::string key = entry.key;
    written.entry         = std::move(entry);
    written.durable       = durable;
    if (!mWriter)
    {
        mWriter = std::make_unique<Writer>();
    }
    {
        const std::lock_guard<std::mutex> lock(mWriter->mutex);
        // A durable write still waiting stays durable when a newer one
        // takes its place.
        if (const auto was = mWriter->waiting.find(key); was != mWriter->waiting.end())
        {
            written.durable = written.durable || was->second.durable;
        }
        mWriter->waiting[key] = std::move(written);
        if (mWriter->posted)
        {
            // Taken up by what was asked already.
            return;
        }
        mWriter->posted = true;
    }
    Writer* writer = mWriter.get();
    if (!ALScriptRecoveryWriter::instance().post([writer] { writer->drain(); }))
    {
        // Stopped: written here.
        writer->drain();
    }
}

void ALScriptRecoveryStore::flush() const
{
    if (!mWriter)
    {
        return;
    }
    std::unique_lock<std::mutex> lock(mWriter->mutex);
    mWriter->changed.wait(lock, [this] { return mWriter->waiting.empty() && !mWriter->busy && !mWriter->posted; });
}

std::vector<std::string> ALScriptRecoveryStore::takeFailures()
{
    std::vector<std::string> out;
    if (mWriter)
    {
        const std::lock_guard<std::mutex> lock(mWriter->mutex);
        out.swap(mWriter->failed);
    }
    return out;
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
bool ALScriptRecoveryStore::writeWhole(const std::string& path, const std::string& written, bool durable)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    // Beside it first, forced out to the disk where it is to be durable,
    // then put in its place: a crash leaves the last whole text or this
    // one, and so does the power going, for one forced out.
    const std::string beside = path + HALF_WRITTEN;
    LLFILE*           file   = LLFile::fopen(beside, LLFILE_MODE("wb"));
    if (!file)
    {
        return false;
    }
    const bool whole = fwrite(written.data(), 1, written.size(), file) == written.size() && fflush(file) == 0;
    if (durable)
    {
#if LL_WINDOWS
        _commit(_fileno(file));
#else
        fsync(fileno(file));
#endif
    }
    fclose(file);
    if (!whole || LLFile::rename(beside, path) != 0)
    {
        LLFile::remove(beside, ENOENT);
        return false;
    }
    return true;
}

// static
bool ALScriptRecoveryStore::readEntry(const std::string& path, ALScriptRecoveryEntry& out, bool whole)
{
    llifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        return false;
    }
    // Its first line: what a listing reads -- or, for an entry written
    // whole as one document, with a header, as they once were, the start
    // of all of it.
    std::string first;
    std::getline(file, first);
    if (first.empty())
    {
        return false;
    }
    if (first[0] == '<')
    {
        std::stringstream all;
        all << first << '\n' << file.rdbuf();
        const std::string text = all.str();
        std::istringstream in(text);
        LLSD               sd;
        if (!LLSDSerialize::deserialize(sd, in, static_cast<llssize>(text.size())) || !ALScriptRecoveryEntry::fromLLSD(sd, out))
        {
            return false;
        }
        out.path = path;
        return true;
    }
    std::istringstream meta(first);
    LLSD               sd;
    if (LLSDSerialize::fromNotation(sd, meta, static_cast<llssize>(first.size())) <= 0 || !metaFrom(sd, out))
    {
        return false;
    }
    out.path  = path;
    out.whole = false;
    if (!whole)
    {
        return true;
    }
    std::stringstream rest;
    rest << file.rdbuf();
    const std::string body_text = rest.str();
    std::istringstream body(body_text);
    LLSD               body_sd;
    if (LLSDSerialize::fromNotation(body_sd, body, static_cast<llssize>(body_text.size())) <= 0 || !body_sd.has("text"))
    {
        return false;
    }
    bodyFrom(body_sd, out);
    return true;
}

bool ALScriptRecoveryStore::load(ALScriptRecoveryEntry& entry) const
{
    if (entry.whole)
    {
        return true;
    }
    flush();
    // Read again whole; what the listing said of it stands, but for what
    // only the rest says.
    ALScriptRecoveryEntry read;
    if (entry.path.empty() || !readEntry(entry.path, read, true) || read.key != entry.key)
    {
        return false;
    }
    entry.text        = std::move(read.text);
    entry.embedded    = read.embedded;
    entry.history     = read.history;
    entry.caretLine        = read.caretLine;
    entry.caretColumn      = read.caretColumn;
    entry.pickedTarget     = std::move(read.pickedTarget);
    entry.pickedExperience = read.pickedExperience;
    entry.whole            = true;
    return true;
}

bool ALScriptRecoveryStore::write(ALScriptRecoveryEntry entry)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    flush();
    if (entry.key.empty())
    {
        return false;
    }
    makeFolders(false);
    changed();
    entry.session = mSession;
    entry.when    = LLDate::now();
    return writeWhole(pathOf(entry.key, mSession), entry.written());
}

void ALScriptRecoveryStore::forget(const std::string& key)
{
    flush();
    changed();
    LLFile::remove(pathOf(key, mSession), ENOENT);
}

bool ALScriptRecoveryStore::setAside(ALScriptRecoveryEntry entry)
{
    return !setAsideAt(std::move(entry), /*keep_when*/ false).empty();
}

std::string ALScriptRecoveryStore::setAsideAt(ALScriptRecoveryEntry entry, bool keep_when)
{
    flush();
    // Written again whole among the discarded: what a listing read of it
    // is not all of it.
    if (entry.key.empty() || !load(entry))
    {
        return std::string();
    }
    if (entry.session.empty())
    {
        entry.session = mSession;
    }
    const LLDate now = LLDate::now();
    entry.state      = ALScriptRecoveryEntry::State::Discarded;
    if (!keep_when)
    {
        entry.when = now;
    }
    makeFolders(true);
    changed();
    // Named by when it was set aside, to the millisecond, which prune
    // reads without opening it, and which two set aside in a moment do not
    // share.
    const std::string target = mDiscarded + fileOf(entry.key) + "." + entry.session + "." +
                               std::to_string(static_cast<S64>(now.secondsSinceEpoch() * 1000.0)) + EXTENSION;
    if (!writeWhole(target, entry.written()))
    {
        return std::string();
    }
    capDiscarded();
    return target;
}

void ALScriptRecoveryStore::makeFolders(bool discarded)
{
    if (!mMadeDirectory)
    {
        LLFile::mkdir(mDirectory);
        mMadeDirectory = true;
    }
    if (discarded && !mMadeDiscarded)
    {
        LLFile::mkdir(mDiscarded);
        mMadeDiscarded = true;
    }
}

void ALScriptRecoveryStore::capDiscarded()
{
    // The newest kept, by when their names say they were set aside; one
    // whose name does not say goes as if it were the oldest.
    struct One
    {
        std::string name;
        S64         when  = 0;
        uintmax_t   bytes = 0;
    };
    std::vector<One> all;
    for (const std::string& name : namesIn(mDiscarded))
    {
        const std::vector<std::string> parts = partsOf(name);
        if (parts.size() < 2)
        {
            continue;
        }
        One one;
        one.name = name;
        if (parts.size() == 3)
        {
            one.when = std::strtoll(parts[2].c_str(), nullptr, 10);
        }
        std::error_code ec;
        one.bytes = std::filesystem::file_size(fsyspath(mDiscarded + name), ec);
        all.push_back(std::move(one));
    }
    std::sort(all.begin(), all.end(), [](const One& a, const One& b) { return a.when > b.when; });
    uintmax_t held = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        held += all[i].bytes;
        if (i >= mMaxDiscarded || (i > 0 && held > mMaxDiscardedBytes))
        {
            LLFile::remove(mDiscarded + all[i].name, ENOENT);
        }
    }
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
    flush();
    changed();
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
        if (readEntry(folder + name, entry, /*whole*/ false))
        {
            out.push_back(std::move(entry));
        }
    }
}

std::vector<ALScriptRecoveryEntry> ALScriptRecoveryStore::list() const
{
    flush();
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
        if (parts.size() < 2 || parts.size() > 3 || parts[0] != hash || parts[1] == mSession)
        {
            continue;
        }
        ALScriptRecoveryEntry entry;
        if (readEntry(mDirectory + name, entry, /*whole*/ false) && entry.key == key && entry.state != ALScriptRecoveryEntry::State::Discarded &&
            (!newest || entry.when.secondsSinceEpoch() > newest->when.secondsSinceEpoch()))
        {
            newest = std::move(entry);
        }
    }
    return newest;
}

std::optional<ALScriptRecoveryEntry> ALScriptRecoveryStore::reclaim(const std::string& key)
{
    flush();
    if (key.empty())
    {
        return std::nullopt;
    }
    const std::string     own = pathOf(key, mSession);
    ALScriptRecoveryEntry entry;
    if (!readEntry(own, entry, /*whole*/ true) || entry.key != key || entry.state == ALScriptRecoveryEntry::State::Discarded)
    {
        return std::nullopt;
    }
    const std::string aside = setAsideAt(entry, /*keep_when*/ true);
    if (aside.empty())
    {
        return std::nullopt;
    }
    LLFile::remove(own, ENOENT);
    entry.state = ALScriptRecoveryEntry::State::Discarded;
    entry.path  = aside;
    return entry;
}

bool ALScriptRecoveryStore::hasOffers() const
{
    // Another session -- another viewer on this account -- may write
    // meanwhile: asked again after a few seconds whatever this one does.
    const F64 now = LLTimer::getTotalSeconds();
    if (mOffersKnown && now < mOffersUntil)
    {
        return mOffers;
    }
    mOffersKnown = true;
    mOffersUntil = now + 5.0;
    mOffers      = false;
    for (const std::string& name : namesIn(mDirectory))
    {
        const std::vector<std::string> parts = partsOf(name);
        if ((parts.size() == 2 || parts.size() == 3) && parts[1] != mSession)
        {
            mOffers = true;
            return mOffers;
        }
    }
    for (const std::string& name : namesIn(mDiscarded))
    {
        if (partsOf(name).size() >= 2)
        {
            mOffers = true;
            return mOffers;
        }
    }
    return mOffers;
}

void ALScriptRecoveryStore::markOffered(const std::vector<ALScriptRecoveryEntry>& entries, const LLDate& now)
{
    flush();
    changed();
    const std::string when = std::to_string(static_cast<S64>(now.secondsSinceEpoch() * 1000.0));
    for (const ALScriptRecoveryEntry& entry : entries)
    {
        // Another session's, not yet marked: renamed to say when.
        const size_t                   slash = entry.path.find_last_of("/\\");
        const std::string              file  = slash == std::string::npos ? entry.path : entry.path.substr(slash + 1);
        const std::vector<std::string> parts = partsOf(file);
        if (entry.session == mSession || entry.state == ALScriptRecoveryEntry::State::Discarded || parts.size() != 2 ||
            entry.path.compare(0, mDirectory.size(), mDirectory) != 0)
        {
            continue;
        }
        LLFile::rename(entry.path, mDirectory + parts[0] + "." + parts[1] + "." + when + EXTENSION);
    }
}

void ALScriptRecoveryStore::prune(F64 max_age_seconds, const LLDate& now)
{
    changed();
    // What was offered and left, as old since it was offered.
    for (const std::string& name : namesIn(mDirectory))
    {
        const std::vector<std::string> parts = partsOf(name);
        if (parts.size() != 3)
        {
            continue;
        }
        char*     end    = nullptr;
        const S64 millis = std::strtoll(parts[2].c_str(), &end, 10);
        if (end && *end == '\0' && !parts[2].empty() && now.secondsSinceEpoch() - static_cast<F64>(millis) / 1000.0 > max_age_seconds)
        {
            LLFile::remove(mDirectory + name, ENOENT);
        }
    }
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
            if (!readEntry(mDiscarded + name, entry, /*whole*/ false))
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
    capDiscarded();
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
