/**
 * @file alscriptlookup.cpp
 * @brief Script Studio's lookups across the object's scripts: references found, a name renamed, and the References tab.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptlookup.h"

#include "aldiskincludes.h"
#include "alincludeidentity.h"
#include "alscriptlexicon.h"
#include "alscriptnavigation.h"
#include "alscriptstudioanalysis.h"
#include "alscriptfixes.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiotabs.h"
#include "alscriptstudioviewer.h"
#include "alscriptstudiowords.h"
#include "fsyspath.h"
#include "llinventorytype.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <map>
#include <set>

using ALScriptPlaces::isIdentifier;
using ALScriptPlaces::lineOf;
using ALScriptPlaces::mapSpan;
using ALScriptPlaces::placeText;
using ALScriptPlaces::rangeOf;

namespace
{
    // What the preprocessor calls a tab's script: an item's path, or a
    // file's on disk, which is the tab's id.
    std::string pathOfScript(const ALScriptStudioDoc& doc)
    {
        return doc.file.empty() ? ALScriptPreprocessor::pathOf(doc.ref) : doc.id;
    }
    // And one of the lookup's other scripts.
    std::string pathOfScript(const ALScriptLookup::Candidate& other)
    {
        return other.path.empty() ? ALScriptPreprocessor::pathOf(other.ref) : other.path;
    }
}

ALScriptLookup::ALScriptLookup(ALScriptStudioServices& services, ALScriptStudioTabs& tabs, ALScriptStudioAnalysis& analysis, ALScriptNavigation& navigation, Window& window) : mServices(services), mTabs(tabs), mAnalysis(analysis), mNavigation(navigation), mWindow(window) {}

// static
std::vector<ALScriptLookup::Candidate> ALScriptLookup::folderCandidates(const std::vector<const LLInventoryItem*>& items, const LLUUID& own, bool lua)
{
    std::vector<Candidate> found;
    for (const LLInventoryItem* item : items)
    {
        if (!item || item->getType() != LLAssetType::AT_LSL_TEXT || item->getUUID() == own)
        {
            continue;
        }
        if ((item->getRuntime() == "luau" || item->getInventorySubType() == SST_LUA) == lua)
        {
            found.push_back({ ALScriptRef(LLUUID::null, item->getUUID()), item->getName() });
        }
    }
    return found;
}

// static
void ALScriptLookup::addPlace(Doc::Lookup& lookup, Doc::Place place)
{
    const std::string key = place.file + llformat(":%d:%d", place.span.line, place.span.column);
    if (lookup.seen.insert(key).second)
    {
        lookup.places.push_back(std::move(place));
    }
}

// static
void ALScriptLookup::sortPlaces(std::vector<Doc::Place>& places)
{
    std::stable_sort(places.begin(), places.end(), [](const Doc::Place& a, const Doc::Place& b) {
        if (a.file.empty() != b.file.empty())
        {
            return a.file.empty();
        }
        if (a.fileName != b.fileName)
        {
            return a.fileName < b.fileName;
        }
        if (a.file != b.file)
        {
            return a.file < b.file;
        }
        return a.span < b.span;
    });
}

// static
bool ALScriptLookup::reserved(bool lua, const std::string& name)
{
    // The language's own words, which a name of the script's cannot be:
    // its keywords and types, the preprocessor's words while their
    // transforms are on, and every function, event and constant the
    // definitions give.
    if (ALScriptLexicon::isKeyword(lua, name))
    {
        return true;
    }
    if (!lua)
    {
        const std::vector<std::string> words = ALScriptStudioViewer::get().preprocessorWords();
        if (std::find(words.begin(), words.end(), name) != words.end())
        {
            return true;
        }
    }
    return ALScriptStudioWords::word(lua, name) != nullptr;
}

ALScriptLookup::Doc* ALScriptLookup::lookingIn(const std::string& id, U32 generation)
{
    Doc* doc = mServices.findDoc(id);
    return doc && doc->lookup->generation == generation ? doc : nullptr;
}

void ALScriptLookup::start(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition,
                           const std::string& home_path, const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version)
{
    // A script names are looked up from is being worked in: a preview of
    // it is held, so that following what it finds does not close it.
    mNavigation.holdPreview(doc);
    Doc::Lookup& lookup   = *doc.lookup;
    lookup                = Doc::Lookup();
    lookup.generation     = ++mGeneration;
    const std::string id_of_lookup      = doc.id;
    const U32         lookup_generation = lookup.generation;
    lookup.command        = command;
    lookup.name           = refs.name;
    lookup.hasDefinition  = has_definition;
    lookup.homePath       = home_path;
    lookup.definition     = definition;
    lookup.renamable      = refs.renamable;
    lookup.version        = version;
    lookup.versions[""]   = version;
    // What every open script's text is now, so that a rename reaching
    // one knows whether it has moved on since.
    // Each by the path the preprocessor's map calls it by: an item's, or
    // a file's, which is its tab's id.
    for (Doc* each : mServices.openDocs())
    {
        if (each->loaded && each != &doc)
        {
            lookup.versions[pathOfScript(*each)] = each->editor->document().version();
        }
    }
    for (Doc::Place& place : places)
    {
        addPlace(lookup, std::move(place));
    }
    // Beyond this script: every other script of its object in the same
    // language, where the name is declared somewhere they can share --
    // in an include, or in this script, which another may include. Each
    // is read as it stands in an open tab, else as the region has it,
    // and passed over where it does not so much as mention the name.
    // One held while the fan-out is built, so that a candidate answered
    // on the spot -- a script already open -- cannot bring the count to
    // nothing and finish the lookup with the others still to be asked.
    ++lookup.pending;
    // What the tab's last lookup had still to begin is let go of.
    mLanes.erase(doc.id);
    if (has_definition)
    {
        // Held until the window has them, which may be once every prim of
        // the object has said what it holds; then each counted as waited
        // for, and begun a few at a time.
        ++lookup.pending;
        const std::weak_ptr<bool> alive = mAlive;
        mWindow.candidates(doc, [this, alive, id_of_lookup, lookup_generation](Candidates found) {
            Doc* doc = alive.lock() ? lookingIn(id_of_lookup, lookup_generation) : nullptr;
            if (!doc)
            {
                return;
            }
            // And every file on disk open here in the language, which no
            // object or folder lists: a module a script requires is one,
            // and so, often, are the scripts that require it.
            boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> listed{ doc->id };
            for (const Doc* open : mServices.openDocs())
            {
                if (open != doc && open->loaded && !open->notecard && !open->file.empty() && open->language.lua == doc->language.lua)
                {
                    found.scripts.push_back({ ALScriptRef(), open->name, open->id });
                    listed.insert(open->id);
                }
            }
            // And those on disk under the folders a script reads from, open
            // here or not: a script requiring the module may be any of them.
            for (const std::string& file : mWindow.diskCandidates(*doc))
            {
                const std::string path = ALIncludeIdentity::ofFile(file);
                if (listed.insert(path).second)
                {
                    found.scripts.push_back({ ALScriptRef(), fsyspath(fsyspath(file).filename()).string(), path, true });
                }
            }
            doc->lookup->unlisted = found.unlisted;
            doc->lookup->pending += static_cast<S32>(found.scripts.size());
            if (!found.scripts.empty())
            {
                Lane lane;
                lane.generation        = lookup_generation;
                lane.left              = std::move(found.scripts);
                mLanes[id_of_lookup] = std::move(lane);
                feed(id_of_lookup);
            }
            // The hold let go of, the doc found again: a candidate answered
            // on the spot may have opened a tab.
            if (Doc* now = lookingIn(id_of_lookup, lookup_generation))
            {
                --now->lookup->pending;
                settled(*now);
            }
        });
    }
    // What the held one stood for: the doc is found again, since a
    // candidate answered on the spot may have opened a tab.
    Doc* now = lookingIn(id_of_lookup, lookup_generation);
    if (!now)
    {
        return;
    }
    Doc::Lookup& theirs = *now->lookup;
    --theirs.pending;
    if (theirs.pending > 0)
    {
        mServices.setStatus(mServices.counted("LookingAcross", theirs.pending, { { "[NAME]", theirs.name } }));
    }
    settled(*now);
}

void ALScriptLookup::feed(const std::string& id)
{
    auto found = mLanes.find(id);
    // One begun on the spot, answered on the spot, comes back here: the
    // loop below goes on with the next.
    if (found == mLanes.end() || found->second.feeding)
    {
        return;
    }
    found->second.feeding = true;
    while (true)
    {
        found = mLanes.find(id);
        if (found == mLanes.end())
        {
            return;
        }
        Lane& lane = found->second;
        Doc*  doc  = lookingIn(id, lane.generation);
        if (!doc || (lane.next >= lane.left.size() && lane.running <= 0))
        {
            // Begun again, closed, or all done with.
            mLanes.erase(found);
            return;
        }
        if (lane.running >= AT_ONCE || lane.next >= lane.left.size())
        {
            lane.feeding = false;
            return;
        }
        const Candidate candidate = lane.left[lane.next++];
        ++lane.running;
        begin(*doc, lane.generation, candidate);
    }
}

void ALScriptLookup::begin(Doc& doc, U32 generation, const Candidate& candidate)
{
    const ALScriptRef ref = candidate.ref;
    const std::string id  = doc.id;
    // Read as it stands in an open tab, else as the region has it; a file
    // on disk only as it stands in its tab, which may have closed since.
    const Doc* other = candidate.path.empty() ? mServices.findDoc(ref) : mServices.findDoc(candidate.path);
    if (other && other->loaded)
    {
        Candidate named = candidate;
        named.name      = other->name;
        this->candidate(id, generation, named, other->assetId, other->snapshot());
        return;
    }
    if (!candidate.path.empty())
    {
        // A file under a folder a script reads from, read as it is on disk;
        // one only open here, which has closed, passed over.
        std::string file;
        std::string text;
        if (candidate.onDisk && ALIncludeIdentity::fileOf(candidate.path, file) && ALDiskIncludes::readOrdinary(file, text))
        {
            this->candidate(id, generation, candidate, LLUUID::null, std::make_shared<const std::string>(std::move(text)));
            return;
        }
        if (candidate.onDisk)
        {
            doc.lookup->unread.push_back(candidate.name);
        }
        --doc.lookup->pending;
        passed(doc);
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.loadSource(ref, [this, alive, id, generation, candidate](const LLUUID& asset, const std::optional<std::string>& source) {
        if (!alive.lock())
        {
            return;
        }
        if (source)
        {
            this->candidate(id, generation, candidate, asset, std::make_shared<const std::string>(*source));
            return;
        }
        // Not read: passed over, and said so with what was found.
        if (Doc* doc = lookingIn(id, generation))
        {
            doc->lookup->unread.push_back(candidate.name);
            --doc->lookup->pending;
            passed(*doc);
        }
    });
}

void ALScriptLookup::passed(Doc& doc)
{
    const std::string id = doc.id;
    if (auto found = mLanes.find(id); found != mLanes.end() && found->second.generation == doc.lookup->generation)
    {
        --found->second.running;
        feed(id);
    }
    // Found again: what was begun may have come back on the spot.
    if (Doc* now = mServices.findDoc(id))
    {
        settled(*now);
    }
}

void ALScriptLookup::candidate(const std::string& id, U32 generation, const Candidate& other, const LLUUID& asset_id,
                               std::shared_ptr<const std::string> text)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    if (text->empty() || text->find(doc.lookup->name) == std::string::npos)
    {
        --doc.lookup->pending;
        passed(doc);
        return;
    }
    // Expanded as the compiler would see it, its includes fetched.
    ALScriptPreprocessor::Request request;
    request.ref           = other.ref;
    request.path          = other.path;
    request.name          = other.name;
    request.assetId       = asset_id;
    request.source        = text;
    request.lua           = doc.language.lua;
    request.compileTarget = doc.language.compileTarget;
    // Expanded as the compiler would see it, not optimized: a name is
    // being looked for, and the optimizer may rename it or take it away
    // -- with shrinknames on it renames every one.
    request.optimize      = false;
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.expand(request, [this, alive, id, generation, other, text](const ALPreprocessor::Result& result) {
        if (alive.lock())
        {
            expanded(id, generation, other, text, result);
        }
    });
}

void ALScriptLookup::expanded(const std::string& id, U32 generation, const Candidate& other,
                              const std::shared_ptr<const std::string>& source, const ALPreprocessor::Result& result)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&              doc  = *found;
    const std::string self = pathOfScript(other);
    const std::string home = doc.lookup->homePath.empty() ? pathOfScript(doc) : doc.lookup->homePath;
    // The script declaring the name, in this expansion: the script
    // itself, or one of its includes; a script that has neither cannot
    // name it.
    const S32 file = self == home ? 0 : result.map.fileOf(home);
    if (file < 0)
    {
        --doc.lookup->pending;
        passed(doc);
        return;
    }
    const ALSourceMap::Loc at = result.map.toExpanded(file, doc.lookup->definition.line, doc.lookup->definition.column);
    if (!at.found())
    {
        --doc.lookup->pending;
        passed(doc);
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::References;
    // Its own script to the analyzers, by who asked and what is looked
    // through: another tab's lookup through the same script does not
    // stand in for this one's, and this one's next lookup does.
    request.id      = "lookup:" + id + ":" + (other.path.empty() ? other.ref.id() : other.path);
    request.version = generation;
    request.lua     = doc.language.lua;
    request.mono    = doc.language.compileTarget != "lsl2";
    request.text    = std::make_shared<const std::string>(result.text);
    request.line    = at.line;
    request.column  = at.column;
    // The texts kept for the answer are the ones the questions hold.
    const std::weak_ptr<bool>                alive    = mAlive;
    const std::shared_ptr<const std::string> expanded_text = request.text;
    mAnalysis.askAnalysis(std::move(request), [this, alive, id, generation, other, source, map = result.map,
                                             expanded_text](const ALScriptAnalysis::Result& answer) {
        if (alive.lock())
        {
            answered(id, generation, other, map, *source, expanded_text, answer);
        }
    });
}

void ALScriptLookup::answered(const std::string& id, U32 generation, const Candidate& other, const ALSourceMap& map,
                              const std::string& source, const std::shared_ptr<const std::string>& expanded,
                              const ALScriptAnalysis::Result& result)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&              doc  = *found;
    const std::string self = pathOfScript(other);
    const std::string own  = pathOfScript(doc);
    const std::string name = other.name;
    --doc.lookup->pending;
    if (result.references.found)
    {
        // Each file's lines found once, however many places are in it.
        boost::unordered_flat_map<std::string, ALScriptPlaces::Lines, ll::string_hash, std::equal_to<>> files;
        const ALScriptPlaces::Lines                                                     own_lines(source);
        const ALScriptPlaces::Lines                                                     expansion(expanded);
        for (ALScriptSpan span : result.references.references)
        {
            const ALScriptSpan raw  = span;
            const S32          file = mapSpan(map, span);
            if (file < 0)
            {
                continue;
            }
            // This script's own, read through the other's expansion of it
            // -- the region's copy, which is not what is typed here: its
            // own answer has them, in the text as it stands.
            if (file > 0 && map.files()[file].path == own)
            {
                continue;
            }
            Doc::Place place;
            place.span     = span;
            place.file     = file == 0 ? self : map.files()[file].path;
            place.fileName = file == 0 ? name : map.files()[file].name;
            // As the other script, or its include, was written.
            auto lines = file == 0 ? files.end() : files.find(place.file);
            if (file != 0 && lines == files.end())
            {
                lines = files.emplace(place.file, mAnalysis.sourceLines(place.file)).first;
            }
            if (file == 0)
            {
                placeText(place, own_lines.line(span.line));
            }
            else if (lines->second.has(span.line))
            {
                placeText(place, lines->second.line(span.line));
            }
            else
            {
                placeText(place, expansion.line(raw.line));
                place.at = -1;
            }
            addPlace(*doc.lookup, std::move(place));
        }
        // Reached, and what it reads as kept for a rename's clash check.
        if (doc.lookup->scripts.insert(self).second)
        {
            doc.lookup->texts.emplace_back(name, expanded);
        }
    }
    passed(doc);
}

void ALScriptLookup::settled(Doc& doc)
{
    Doc::Lookup& lookup = *doc.lookup;
    if (lookup.pending > 0 || lookup.command == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand command = lookup.command;
    lookup.command                = ALEditorCommand::None;
    // This script's places first, then each other script's, in order.
    sortPlaces(lookup.places);
    std::set<std::string> files;
    for (const Doc::Place& place : lookup.places)
    {
        files.insert(place.file);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = lookup.name;
    args["[FILES]"] = std::to_string(files.size());
    if (command == ALEditorCommand::FindReferences)
    {
        Found found;
        found.from          = doc.id;
        found.fromName      = doc.name;
        found.name          = lookup.name;
        found.places        = lookup.places;
        found.hasDefinition = lookup.hasDefinition;
        found.home          = lookup.homePath;
        found.definition    = lookup.definition;
        mWindow.showFound(doc, found);
        const S32         count  = static_cast<S32>(lookup.places.size());
        const std::string missed = passedOver(doc);
        mServices.setStatus(mServices.counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", count, args) +
                            (missed.empty() ? std::string() : " " + missed));
    }
    else if (command == ALEditorCommand::Rename)
    {
        if (!lookup.renamable)
        {
            mServices.setStatus(mServices.words("NotRenamable", args));
            return;
        }
        // This script as the analyzers read it, its includes in it, first
        // among what the clash check reads.
        const bool expanded = doc.expanded.valid && doc.expanded.version == lookup.version && doc.expanded.text;
        lookup.texts.insert(lookup.texts.begin(), { doc.name, expanded ? doc.expanded.text : doc.snapshot() });
        const std::string         id         = doc.id;
        const U32                 generation = lookup.generation;
        const std::string         old_name   = lookup.name;
        const S32                 count      = static_cast<S32>(lookup.places.size());
        const S32                 scripts    = static_cast<S32>(files.size());
        const std::weak_ptr<bool> alive      = mAlive;
        mWindow.askNewName(
            doc,
            [this, alive, id, old_name, count, scripts](const std::string& typed) {
                const Doc* asking = alive.lock() ? mServices.findDoc(id) : nullptr;
                return asking ? renameHint(*asking, old_name, count, scripts, typed) : std::string();
            },
            [this, alive, id, generation](const std::string& typed) {
                if (alive.lock())
                {
                    renameTo(id, generation, typed);
                }
            },
            [this, alive, id, generation](const std::string& typed) {
                if (alive.lock())
                {
                    previewRename(id, generation, typed);
                }
            });
    }
}

std::string ALScriptLookup::renameHint(const Doc& doc, const std::string& old_name, S32 count, S32 scripts, const std::string& typed, bool prompting) const
{
    // The row under the field says what return will do with what is typed.
    std::string name = typed;
    LLStringUtil::trim(name);
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = old_name;
    args["[NEW]"]   = name;
    args["[COUNT]"] = std::to_string(count);
    args["[FILES]"] = std::to_string(scripts);
    if (name.empty())
    {
        return mServices.words("RenameHint", args);
    }
    if (const std::string said = refused(doc, name); !said.empty())
    {
        return said;
    }
    if (name == old_name)
    {
        return mServices.words("RenameSame", args);
    }
    // What it cannot reach, after what it will do; and under the prompt,
    // that Shift-Return shows it first.
    std::string beyond = unreached(doc);
    beyond             = beyond.empty() ? beyond : " " + beyond;
    if (const std::string missed = passedOver(doc); !missed.empty())
    {
        beyond += " " + missed;
    }
    if (prompting)
    {
        beyond += " " + mServices.words("RenamePreviewHint");
    }
    if (const std::string in = clashIn(doc, name); !in.empty())
    {
        // Allowed, since a name in another scope may be meant; said.
        args["[SCRIPT]"] = in;
        return mServices.words("RenameClash", args) + beyond;
    }
    return (scripts > 1 ? mServices.words("RenameToAcross", args) : mServices.counted("RenameTo", count, args)) + beyond;
}

std::string ALScriptLookup::refused(const Doc& doc, const std::string& name) const
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name;
    if (!isIdentifier(name))
    {
        return mServices.words("RenameBadName", args);
    }
    if (reserved(doc.language.lua, name))
    {
        return mServices.words("RenameReserved", args);
    }
    return std::string();
}

// static
bool ALScriptLookup::mentions(std::string_view text, std::string_view name, bool lua)
{
    // Past the strings and the comments, as the lexers pass over them.
    const auto name_byte = ALScriptLexicon::isNameByte;
    for (size_t i = 0; i < text.size();)
    {
        const char c = text[i];
        if (const ALScriptLexicon::Stretch run = ALScriptLexicon::stretchAt(text, i, lua); run.kind != ALScriptLexicon::Kind::Code)
        {
            i = run.end;
            continue;
        }
        if (name_byte(c))
        {
            size_t end = i;
            while (end < text.size() && name_byte(text[end]))
            {
                ++end;
            }
            if (text.substr(i, end - i) == name)
            {
                return true;
            }
            i = end;
            continue;
        }
        ++i;
    }
    return false;
}

std::string ALScriptLookup::clashIn(const Doc& doc, const std::string& name) const
{
    // Declared in this script, by what the outline says of it; else named
    // anywhere in what the rename changes -- a local, a parameter, an
    // include's global -- which may be in another scope, and may not.
    if (std::any_of(doc.outline.begin(), doc.outline.end(), [&name](const ALScriptOutlineEntry& entry) { return entry.name == name; }))
    {
        return doc.name;
    }
    for (const auto& [script, text] : doc.lookup->texts)
    {
        if (text && mentions(*text, name, doc.language.lua))
        {
            return script;
        }
    }
    return std::string();
}

std::string ALScriptLookup::unreached(const Doc& doc) const
{
    // An include is changed wherever it is; the scripts that include it
    // and were not read -- other objects', other folders' -- are not, and
    // will not compile against it until they are.
    std::vector<std::string> includes;
    // A file on disk renamed in its own tab is one such include.
    if (!doc.file.empty() && std::any_of(doc.lookup->places.begin(), doc.lookup->places.end(), [](const Doc::Place& place) { return place.file.empty(); }))
    {
        includes.push_back(doc.name);
    }
    for (const Doc::Place& place : doc.lookup->places)
    {
        if (!place.file.empty() && !doc.lookup->scripts.contains(place.file) &&
            std::find(includes.begin(), includes.end(), place.fileName) == includes.end())
        {
            includes.push_back(place.fileName);
        }
    }
    if (includes.empty())
    {
        return std::string();
    }
    std::string names;
    for (const std::string& include : includes)
    {
        names += (names.empty() ? "" : ", ") + include;
    }
    return mServices.words("RenameUnreached", { { "[NAMES]", names } });
}

std::string ALScriptLookup::passedOver(const Doc& doc) const
{
    std::string said;
    if (doc.lookup->unlisted > 0)
    {
        said = mServices.counted("LookupUnlisted", doc.lookup->unlisted);
    }
    if (!doc.lookup->unread.empty())
    {
        std::string names;
        for (const std::string& name : doc.lookup->unread)
        {
            names += (names.empty() ? "" : ", ") + name;
        }
        said += (said.empty() ? "" : " ") +
                mServices.counted("LookupUnread", static_cast<S32>(doc.lookup->unread.size()), { { "[NAMES]", names } });
    }
    return said;
}

void ALScriptLookup::previewRename(const std::string& id, U32 generation, const std::string& new_name)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&        doc  = *found;
    std::string name = new_name;
    LLStringUtil::trim(name);
    if (const std::string said = refused(doc, name); !said.empty())
    {
        mServices.setStatus(said, true);
        return;
    }
    if (name == doc.lookup->name)
    {
        doc.editor->setFocus(true);
        return;
    }
    const Doc::Lookup& lookup = *doc.lookup;
    Found              shown;
    shown.from          = doc.id;
    shown.fromName      = doc.name;
    shown.name          = lookup.name;
    shown.places        = lookup.places;
    shown.hasDefinition = lookup.hasDefinition;
    shown.home          = lookup.homePath;
    shown.definition    = lookup.definition;
    // What it will do, what may clash, and what it cannot reach, over the
    // places.
    std::set<std::string> files;
    for (const Doc::Place& place : lookup.places)
    {
        files.insert(place.file);
    }
    std::string said = renameHint(doc, lookup.name, static_cast<S32>(lookup.places.size()), static_cast<S32>(files.size()), name, false);
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.previewRename(
        doc, shown, name, said,
        [this, alive, id, generation, name](const std::vector<size_t>& kept) {
            if (alive.lock())
            {
                renameTo(id, generation, name, &kept);
            }
        },
        [this, alive, id, generation, name](const std::string& file, const std::vector<size_t>& kept) {
            if (alive.lock())
            {
                showRenameChanges(id, generation, name, file, kept);
            }
        });
}

bool ALScriptLookup::showRenameChanges(const std::string& id, U32 generation, const std::string& new_name, const std::string& file,
                                       const std::vector<size_t>& kept)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return false;
    }
    Doc&               doc    = *found;
    const Doc::Lookup& lookup = *doc.lookup;
    std::string        name   = new_name;
    LLStringUtil::trim(name);
    // The tab the file is in, as the rename reaches it on the spot, and
    // the version its places were read at.
    Doc*        target  = nullptr;
    std::string called  = doc.name;
    U32         version = lookup.version;
    if (file.empty())
    {
        target = &doc;
    }
    else
    {
        ALScriptRef ref;
        target             = ALScriptPreprocessor::refOf(file, ref) ? mServices.findDoc(ref) : mServices.findDoc(file);
        const auto read_at = lookup.versions.find(file);
        target             = read_at != lookup.versions.end() ? target : nullptr;
        version            = read_at != lookup.versions.end() ? read_at->second : 0;
        const auto named   = std::find_if(lookup.places.begin(), lookup.places.end(), [&file](const Doc::Place& place) { return place.file == file; });
        called             = named != lookup.places.end() ? named->fileName : file;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]   = name;
    args["[SCRIPT]"] = called;
    if (!target || !target->loaded || target->editor->document().version() != version)
    {
        mServices.setStatus(mServices.words("RenameChangesNotHere", args), true);
        return false;
    }
    // Each place kept in the file where the old name still stands, as the
    // rename makes them there.
    const ALTextDocument& text = target->editor->document();
    ALScriptFix           all;
    for (size_t index : kept)
    {
        const Doc::Place* place = index < lookup.places.size() ? &lookup.places[index] : nullptr;
        if (place && place->file == file && place->span.line < text.lineCount() && text.text(rangeOf(place->span)) == lookup.name)
        {
            all.edits.emplace_back(place->span, name);
        }
    }
    const std::string                now   = target->editor->wholeText();
    const std::optional<std::string> after = ALScriptFixes::apply(now, all);
    if (!after)
    {
        return false;
    }
    mTabs.activate(*target);
    mWindow.compare(*target, now, *after, mServices.words("CompareNow"), mServices.words("RenameAfter", args));
    return true;
}

void ALScriptLookup::renameTo(const std::string& id, U32 generation, const std::string& new_name, const std::vector<size_t>* kept)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&               doc      = *found;
    const Doc::Lookup& lookup   = *doc.lookup;
    const std::string  old_name = lookup.name;
    std::string        name     = new_name;
    LLStringUtil::trim(name);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name;
    if (const std::string said = refused(doc, name); !said.empty())
    {
        mServices.setStatus(said, true);
        return;
    }
    if (name == old_name)
    {
        doc.editor->setFocus(true);
        return;
    }
    if (doc.editor->document().version() != lookup.version)
    {
        mServices.setStatus(mServices.words("RenameStale", args), true);
        return;
    }
    // Each script's places together: this one's put in as one step;
    // another's the same where it is open and unchanged since it was
    // read, else opened with the change waiting for its text.
    // Every place, or those kept where the rename was previewed.
    std::map<std::string, std::vector<const Doc::Place*>> by_file;
    for (size_t i = 0; i < lookup.places.size(); ++i)
    {
        if (!kept || std::find(kept->begin(), kept->end(), i) != kept->end())
        {
            by_file[lookup.places[i].file].push_back(&lookup.places[i]);
        }
    }
    S32 renamed = 0;
    S32 scripts = 0;
    S32 opened  = 0;
    S32 stale   = 0;
    // Another script or file open here, unchanged since it was read: each
    // place where the old name still stands, since an open include may
    // not read as the expansion had it. False where it has moved on, or
    // may not be changed.
    const auto rename_open = [&](Doc& other, const std::string& file, const std::vector<const Doc::Place*>& places) {
        const auto version = lookup.versions.find(file);
        if (version == lookup.versions.end() || version->second != other.editor->document().version() || !other.modifiable)
        {
            return false;
        }
        const ALTextDocument&                            text = other.editor->document();
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (const Doc::Place* place : places)
        {
            const ALTextRange range = rangeOf(place->span);
            if (place->span.line < text.lineCount() && text.text(range) == old_name)
            {
                edits.emplace_back(range, name);
            }
        }
        const S32 count = static_cast<S32>(edits.size());
        if (count > 0 && other.editor->replaceAll(std::move(edits)))
        {
            other.editor->undoJournal().label("rename");
            renamed += count;
            ++scripts;
        }
        return true;
    };
    for (const auto& [file, places] : by_file)
    {
        if (file.empty())
        {
            std::vector<std::pair<ALTextRange, std::string>> edits;
            edits.reserve(places.size());
            for (const Doc::Place* place : places)
            {
                edits.emplace_back(rangeOf(place->span), name);
            }
            if (doc.editor->replaceAll(std::move(edits)))
            {
                doc.editor->undoJournal().label("rename");
                renamed += static_cast<S32>(places.size());
                ++scripts;
            }
            continue;
        }
        ALScriptRef ref;
        std::string path;
        if (!ALScriptPreprocessor::refOf(file, ref))
        {
            // A file on disk -- one the preprocessor read from a folder
            // it may read -- changed in its tab, as any script is, and
            // written only when that tab is saved; opened where it is not
            // open, with the change waiting unsaved, as a script is.
            if (!ALScriptPreprocessor::fileOf(file, path))
            {
                ++stale;
                continue;
            }
            if (Doc* open = mServices.findDoc(file))
            {
                stale += rename_open(*open, file, places) ? 0 : 1;
                continue;
            }
            mTabs.openFileTab(path, doc.language.lua);
            Doc* other = mServices.findDoc(file);
            if (!other)
            {
                // Gone, or opened in another window.
                ++stale;
                continue;
            }
            // Read as it is on disk now, which the expansion may not have
            // been: each place where the old name still stands.
            for (const Doc::Place* place : places)
            {
                other->pendingEdits.push_back(Doc::PendingEdit{ place->span, old_name, name });
            }
            // Counted as a script not open is, each place asked for; the
            // ones that no longer stand are said by the edits' own report.
            renamed += static_cast<S32>(places.size());
            applyPendingEdits(*other);
            ++scripts;
            ++opened;
            continue;
        }
        Doc* open = mServices.findDoc(ref);
        if (open && open->loaded)
        {
            stale += rename_open(*open, file, places) ? 0 : 1;
            continue;
        }
        // Not open: opened, with the change made once its text is in,
        // where the old name still stands at each place.
        if (!open)
        {
            mServices.openScript(ref, places.front()->fileName);
        }
        Doc* other = mServices.findDoc(ref);
        if (!other)
        {
            ++stale;
            continue;
        }
        for (const Doc::Place* place : places)
        {
            other->pendingEdits.push_back(Doc::PendingEdit{ place->span, old_name, name });
        }
        renamed += static_cast<S32>(places.size());
        ++scripts;
        ++opened;
    }
    // What was done, a clause for each thing there is to say, each count in
    // its own form.
    args["[PLACES]"]  = mServices.counted("Places", renamed);
    args["[SCRIPTS]"] = mServices.counted("Scripts", scripts);
    std::string said  = mServices.words(scripts > 1 || opened > 0 ? "RenamedIn" : "RenamedHere", args);
    if (opened > 0)
    {
        args["[SCRIPTS]"] = mServices.counted("Scripts", opened);
        said = mServices.clauses(said, mServices.words("RenamedOpened", args));
    }
    if (stale > 0)
    {
        args["[SCRIPTS]"] = mServices.counted("Scripts", stale);
        said = mServices.clauses(said, mServices.words("RenamedLeft", args));
    }
    // What it could not reach, or look through, said with what it did.
    std::string beyond = unreached(doc);
    if (const std::string missed = passedOver(doc); !missed.empty())
    {
        beyond = mServices.sentences(beyond, missed);
    }
    mServices.report(mServices.sentences(mServices.sentence(said), beyond), stale > 0 || !passedOver(doc).empty());
    mTabs.activate(doc);
    doc.editor->setFocus(true);
}

void ALScriptLookup::applyPendingEdits(Doc& doc)
{
    if (doc.pendingEdits.empty() || !doc.loaded)
    {
        return;
    }
    std::vector<Doc::PendingEdit> edits;
    edits.swap(doc.pendingEdits);
    const ALTextDocument&                            text = doc.editor->document();
    std::vector<std::pair<ALTextRange, std::string>> changes;
    S32                                              missed = 0;
    for (const Doc::PendingEdit& edit : edits)
    {
        const ALTextRange range = rangeOf(edit.span);
        if (edit.span.line < text.lineCount() && text.text(range) == edit.was)
        {
            changes.emplace_back(range, edit.now);
        }
        else
        {
            ++missed;
        }
    }
    const bool replace = !edits.empty() && edits.front().replace;
    if (!changes.empty() && !doc.modifiable)
    {
        // Nothing may be changed here: every place was missed.
        missed += static_cast<S32>(changes.size());
        changes.clear();
    }
    if (!changes.empty())
    {
        doc.editor->setReadOnly(false);
        if (doc.editor->replaceAll(std::move(changes)))
        {
            doc.editor->undoJournal().label(replace ? "replace" : "rename");
        }
    }
    if (missed > 0)
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        mServices.report(mServices.counted(replace ? "ReplaceMissed" : "RenameMissed", missed, args), true, &doc);
    }
}
