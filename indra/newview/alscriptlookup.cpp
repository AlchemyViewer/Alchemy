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

#include "alscriptstudioplaces.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiowords.h"

#include <algorithm>
#include <map>
#include <set>

using ALScriptPlaces::isIdentifier;
using ALScriptPlaces::lineOf;
using ALScriptPlaces::mapSpan;
using ALScriptPlaces::placeText;
using ALScriptPlaces::rangeOf;

ALScriptLookup::ALScriptLookup(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window) {}

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
    static const std::set<std::string> LSL{ "default", "state", "event", "jump", "return", "if", "else", "for", "do", "while", "print",
                                           "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" };
    static const std::set<std::string> LUAU{ "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
                                            "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "continue" };
    if (lua ? LUAU.count(name) > 0 : LSL.count(name) > 0)
    {
        return true;
    }
    if (!lua && ALScriptStudioWords::sources().preprocessorWords)
    {
        const std::vector<std::string> words = ALScriptStudioWords::sources().preprocessorWords();
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
    return doc && doc->lookup.generation == generation ? doc : nullptr;
}

void ALScriptLookup::start(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition,
                           const std::string& home_path, const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version)
{
    // A script names are looked up from is being worked in: a preview of
    // it is held, so that following what it finds does not close it.
    mWindow.holdPreview(doc);
    Doc::Lookup& lookup   = doc.lookup;
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
            lookup.versions[each->file.empty() ? ALScriptPreprocessor::pathOf(each->ref) : each->id] = each->editor->document().version();
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
    if (has_definition)
    {
        const std::weak_ptr<bool> alive      = mAlive;
        const std::string         id         = doc.id;
        const U32                 generation = lookup.generation;
        for (const Candidate& candidate : mWindow.candidates(doc))
        {
            const ALScriptRef ref = candidate.ref;
            ++lookup.pending;
            if (const Doc* other = mServices.findDoc(ref); other && other->loaded)
            {
                const std::string name  = other->name;
                const LLUUID      asset = other->assetId;
                const std::string text  = other->editor->text();
                this->candidate(id, generation, ref, name, asset, text);
                continue;
            }
            const std::string name = candidate.name;
            mWindow.loadSource(ref, [this, alive, id, generation, ref, name](const LLUUID& asset, const std::string& source) {
                if (alive.lock())
                {
                    this->candidate(id, generation, ref, name, asset, source);
                }
            });
        }
    }
    // What the held one stood for: the doc is found again, since a
    // candidate answered on the spot may have opened a tab.
    Doc* now = lookingIn(id_of_lookup, lookup_generation);
    if (!now)
    {
        return;
    }
    Doc::Lookup& theirs = now->lookup;
    --theirs.pending;
    if (theirs.pending > 0)
    {
        mServices.setStatus(mServices.counted("LookingAcross", theirs.pending, { { "[NAME]", theirs.name } }));
    }
    settled(*now);
}

void ALScriptLookup::candidate(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                               const LLUUID& asset_id, const std::string& text)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc& doc = *found;
    if (text.empty() || text.find(doc.lookup.name) == std::string::npos)
    {
        --doc.lookup.pending;
        settled(doc);
        return;
    }
    // Expanded as the compiler would see it, its includes fetched.
    ALScriptPreprocessor::Request request;
    request.ref           = ref;
    request.name          = name;
    request.assetId       = asset_id;
    request.source        = text;
    request.lua           = doc.language.lua;
    request.compileTarget = doc.language.compileTarget;
    // Expanded as the compiler would see it, not optimized: a name is
    // being looked for, and the optimizer may rename it or take it away
    // -- with shrinknames on it renames every one.
    request.optimize      = false;
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.expand(request, [this, alive, id, generation, ref, name, text](const ALPreprocessor::Result& result) {
        if (alive.lock())
        {
            expanded(id, generation, ref, name, text, result);
        }
    });
}

void ALScriptLookup::expanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                              const std::string& source, const ALPreprocessor::Result& result)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&              doc  = *found;
    const std::string self = ALScriptPreprocessor::pathOf(ref);
    const std::string home = doc.lookup.homePath.empty() ? ALScriptPreprocessor::pathOf(doc.ref) : doc.lookup.homePath;
    // The script declaring the name, in this expansion: the script
    // itself, or one of its includes; a script that has neither cannot
    // name it.
    const S32 file = self == home ? 0 : result.map.fileOf(home);
    if (file < 0)
    {
        --doc.lookup.pending;
        settled(doc);
        return;
    }
    const ALSourceMap::Loc at = result.map.toExpanded(file, doc.lookup.definition.line, doc.lookup.definition.column);
    if (!at.found())
    {
        --doc.lookup.pending;
        settled(doc);
        return;
    }
    ALScriptAnalysis::Request request;
    request.kind    = ALScriptAnalysis::Kind::References;
    request.id      = "lookup:" + ref.id();
    request.version = generation;
    request.lua     = doc.language.lua;
    request.mono    = doc.language.compileTarget != "lsl2";
    request.text    = result.text;
    request.line    = at.line;
    request.column  = at.column;
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.askAnalysis(std::move(request), [this, alive, id, generation, ref, name, source, map = result.map,
                                             expanded = result.text](const ALScriptAnalysis::Result& answer) {
        if (alive.lock())
        {
            answered(id, generation, ref, name, map, source, expanded, answer);
        }
    });
}

void ALScriptLookup::answered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                              const ALSourceMap& map, const std::string& source, const std::string& expanded,
                              const ALScriptAnalysis::Result& result)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&              doc  = *found;
    const std::string self = ALScriptPreprocessor::pathOf(ref);
    const std::string own  = ALScriptPreprocessor::pathOf(doc.ref);
    --doc.lookup.pending;
    if (result.references.found)
    {
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
            std::string line;
            if (file == 0)
            {
                placeText(place, lineOf(source, span.line));
            }
            else if (mWindow.sourceLine(place.file, span.line, line))
            {
                placeText(place, line);
            }
            else
            {
                placeText(place, lineOf(expanded, raw.line));
                place.at = -1;
            }
            addPlace(doc.lookup, std::move(place));
        }
    }
    settled(doc);
}

void ALScriptLookup::settled(Doc& doc)
{
    Doc::Lookup& lookup = doc.lookup;
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
        const S32 count = static_cast<S32>(lookup.places.size());
        mServices.setStatus(mServices.counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", count, args));
    }
    else if (command == ALEditorCommand::Rename)
    {
        if (!lookup.renamable)
        {
            mServices.setStatus(mServices.words("NotRenamable", args));
            return;
        }
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
            });
    }
}

std::string ALScriptLookup::renameHint(const Doc& doc, const std::string& old_name, S32 count, S32 scripts, const std::string& typed) const
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
    if (!isIdentifier(name))
    {
        args["[NAME]"] = name;
        return mServices.words("RenameBadName", args);
    }
    if (reserved(doc.language.lua, name))
    {
        args["[NAME]"] = name;
        return mServices.words("RenameReserved", args);
    }
    if (name == old_name)
    {
        return mServices.words("RenameSame", args);
    }
    if (std::any_of(doc.outline.begin(), doc.outline.end(), [&name](const ALScriptOutlineEntry& entry) { return entry.name == name; }))
    {
        // Allowed, since a name in another scope may be meant; said.
        return mServices.words("RenameClash", args);
    }
    return scripts > 1 ? mServices.words("RenameToAcross", args) : mServices.counted("RenameTo", count, args);
}

void ALScriptLookup::renameTo(const std::string& id, U32 generation, const std::string& new_name)
{
    Doc* found = lookingIn(id, generation);
    if (!found)
    {
        return;
    }
    Doc&               doc      = *found;
    const Doc::Lookup& lookup   = doc.lookup;
    const std::string  old_name = lookup.name;
    std::string        name     = new_name;
    LLStringUtil::trim(name);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name;
    if (!isIdentifier(name))
    {
        mServices.setStatus(mServices.words("RenameBadName", args), true);
        return;
    }
    if (reserved(doc.language.lua, name))
    {
        mServices.setStatus(mServices.words("RenameReserved", args), true);
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
    std::map<std::string, std::vector<const Doc::Place*>> by_file;
    for (const Doc::Place& place : lookup.places)
    {
        by_file[place.file].push_back(&place);
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
            mWindow.openFileTab(path, doc.language.lua);
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
        said += "; " + mServices.words("RenamedOpened", args);
    }
    if (stale > 0)
    {
        args["[SCRIPTS]"] = mServices.counted("Scripts", stale);
        said += "; " + mServices.words("RenamedLeft", args);
    }
    mServices.report(said + ".", stale > 0);
    mWindow.activate(doc);
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
