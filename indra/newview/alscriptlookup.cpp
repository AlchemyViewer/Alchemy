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

#include "alfloaterscriptstudio.h"

#include "alpanelist.h"
#include "alscriptenvelope.h"
#include "alscriptexplorerpane.h"
#include "alscriptpreprocessor.h"
#include "alscriptstudioplaces.h"
#include "llscrolllistcell.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <map>
#include <set>

using ALScriptPlaces::isIdentifier;
using ALScriptPlaces::lineOf;
using ALScriptPlaces::mapSpan;
using ALScriptPlaces::placeText;
using ALScriptPlaces::rangeOf;
using ALScriptPlaces::sourceOf;

// static
void ALFloaterScriptStudio::addPlace(Doc::Lookup& lookup, Doc::Place place)
{
    const std::string key = place.file + llformat(":%d:%d", place.span.line, place.span.column);
    if (lookup.seen.insert(key).second)
    {
        lookup.places.push_back(std::move(place));
    }
}

void ALFloaterScriptStudio::startLookup(Doc& doc, ALEditorCommand command, const ALScriptReferences& refs, bool has_definition, const std::string& home_path,
                                        const ALScriptSpan& definition, std::vector<Doc::Place> places, U32 version)
{
    // A script names are looked up from is being worked in: a preview of
    // it is held, so that following what it finds does not close it.
    holdPreview(doc);
    Doc::Lookup& lookup   = doc.lookup;
    lookup                = Doc::Lookup();
    lookup.generation     = ++mLookupGeneration;
    const std::string id_of_lookup   = doc.id;
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
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        if (each->loaded && each.get() != &doc)
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
    if (has_definition && !doc.ref.inInventory())
    {
        const LLHandle<LLFloater> handle     = getHandle();
        const std::string         id         = doc.id;
        const U32                 generation = lookup.generation;
        for (const ALScriptExplorerModel::Object& object : mExplorerPane->model().objects())
        {
            bool ours = false;
            for (const ALScriptExplorerModel::Prim& prim : object.prims)
            {
                ours = ours || prim.id == doc.ref.object;
            }
            if (!ours || !object.present)
            {
                continue;
            }
            for (const ALScriptExplorerModel::Prim& prim : object.prims)
            {
                for (const ALScriptWorkspace::Item& item : prim.items)
                {
                    const ALScriptRef ref(prim.id, item.id);
                    if (!item.script || item.lua != doc.language.lua || ref == doc.ref)
                    {
                        continue;
                    }
                    ++lookup.pending;
                    if (const size_t index = indexOf(ref); index != NONE && mDocs[index]->loaded)
                    {
                        const Doc&        other = *mDocs[index];
                        const std::string name  = other.name;
                        const LLUUID      asset = other.assetId;
                        const std::string text  = other.editor->text();
                        lookupCandidate(id, generation, ref, name, asset, text);
                        continue;
                    }
                    const std::string name = item.name;
                    ALScriptWorkspace::instance().load(ref, [handle, id, generation, ref, name](const ALScriptWorkspace::Loaded& loaded) {
                        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
                        if (!studio)
                        {
                            return;
                        }
                        studio->lookupCandidate(id, generation, ref, name, loaded.assetId, sourceOf(loaded));
                    });
                }
            }
        }
    }
    // What the held one stood for: the doc is found again, since a
    // candidate answered on the spot may have opened a tab.
    const size_t still = indexOf(id_of_lookup);
    if (still == NONE || mDocs[still]->lookup.generation != lookup_generation)
    {
        return;
    }
    Doc&         now    = *mDocs[still];
    Doc::Lookup& theirs = now.lookup;
    --theirs.pending;
    if (theirs.pending > 0)
    {
        setStatus(counted("LookingAcross", theirs.pending, { { "[NAME]", theirs.name } }));
    }
    lookupSettled(now);
}

void ALFloaterScriptStudio::lookupCandidate(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const LLUUID& asset_id,
                                            const std::string& text)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc& doc = *mDocs[index];
    if (text.empty() || text.find(doc.lookup.name) == std::string::npos)
    {
        --doc.lookup.pending;
        lookupSettled(doc);
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
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptPreprocessor::instance().run(request, [handle, id, generation, ref, name, text](const ALPreprocessor::Result& result) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->lookupExpanded(id, generation, ref, name, text, result);
        }
    });
}

void ALFloaterScriptStudio::lookupExpanded(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name,
                                           const std::string& source, const ALPreprocessor::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc&              doc  = *mDocs[index];
    const std::string self = ALScriptPreprocessor::pathOf(ref);
    const std::string home = doc.lookup.homePath.empty() ? ALScriptPreprocessor::pathOf(doc.ref) : doc.lookup.homePath;
    // The script declaring the name, in this expansion: the script
    // itself, or one of its includes; a script that has neither cannot
    // name it.
    const S32 file = self == home ? 0 : result.map.fileOf(home);
    if (file < 0)
    {
        --doc.lookup.pending;
        lookupSettled(doc);
        return;
    }
    const ALSourceMap::Loc at = result.map.toExpanded(file, doc.lookup.definition.line, doc.lookup.definition.column);
    if (!at.found())
    {
        --doc.lookup.pending;
        lookupSettled(doc);
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
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptAnalysis::instance().ask(std::move(request), [handle, id, generation, ref, name, source, map = result.map,
                                                          expanded = result.text](const ALScriptAnalysis::Result& answer) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->lookupAnswered(id, generation, ref, name, map, source, expanded, answer);
        }
    });
}

void ALFloaterScriptStudio::lookupAnswered(const std::string& id, U32 generation, const ALScriptRef& ref, const std::string& name, const ALSourceMap& map,
                                           const std::string& source, const std::string& expanded, const ALScriptAnalysis::Result& result)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc&              doc  = *mDocs[index];
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
            else if (sourceLine(place.file, span.line, line))
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
    lookupSettled(doc);
}

void ALFloaterScriptStudio::lookupSettled(Doc& doc)
{
    Doc::Lookup& lookup = doc.lookup;
    if (lookup.pending > 0 || lookup.command == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand command = lookup.command;
    lookup.command                = ALEditorCommand::None;
    // This script's places first, then each other script's, in order.
    std::stable_sort(lookup.places.begin(), lookup.places.end(), [](const Doc::Place& a, const Doc::Place& b) {
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
        mFound               = References();
        mFound.from          = doc.id;
        mFound.fromName      = doc.name;
        mFound.name          = lookup.name;
        mFound.places        = lookup.places;
        mFound.hasDefinition = lookup.hasDefinition;
        mFound.home          = lookup.homePath;
        mFound.definition    = lookup.definition;
        std::vector<ALTextRange> lit;
        for (const Doc::Place& place : mFound.places)
        {
            if (place.file.empty())
            {
                lit.push_back(rangeOf(place.span));
            }
        }
        doc.editor->setHighlights(std::move(lit));
        fillReferences();
        showBottom("references_tab");
        setStatus(counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", static_cast<S32>(lookup.places.size()), args));
    }
    else if (command == ALEditorCommand::Rename)
    {
        if (lookup.renamable)
        {
            askNewName(doc);
        }
        else
        {
            setStatus(getString("NotRenamable", args));
        }
    }
}

void ALFloaterScriptStudio::askNewName(Doc& doc)
{
    const std::string         id         = doc.id;
    const U32                 generation = doc.lookup.generation;
    const std::string         old_name   = doc.lookup.name;
    const LLHandle<LLFloater> handle     = getHandle();
    ALQuickOpen* quick = quickOpen(
        {}, getString("RenamePlaceholder"), getString("RenameTitle"),
        [handle, id, generation](const std::string& typed) {
            if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
            {
                studio->renameTo(id, generation, typed);
            }
        },
        mEditorHost, 420, 56);
    if (!quick)
    {
        return;
    }
    // The row under the field says what return will do with what is typed.
    const S32             count = static_cast<S32>(doc.lookup.places.size());
    std::set<std::string> files;
    for (const Doc::Place& place : doc.lookup.places)
    {
        files.insert(place.file);
    }
    const S32 scripts = static_cast<S32>(files.size());
    quick->onQueryChanged([handle, quick, count, scripts, old_name, id](const std::string& typed) {
        ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get());
        const size_t           index  = studio ? studio->indexOf(id) : NONE;
        if (index == NONE)
        {
            return;
        }
        const Doc& doc = *studio->mDocs[index];
        std::string name = typed;
        LLStringUtil::trim(name);
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = old_name;
        args["[NEW]"]   = name;
        args["[COUNT]"] = std::to_string(count);
        args["[FILES]"] = std::to_string(scripts);
        if (name.empty())
        {
            quick->setHint(studio->getString("RenameHint", args));
        }
        else if (!isIdentifier(name))
        {
            args["[NAME]"] = name;
            quick->setHint(studio->getString("RenameBadName", args));
        }
        else if (studio->reservedName(doc, name))
        {
            args["[NAME]"] = name;
            quick->setHint(studio->getString("RenameReserved", args));
        }
        else if (name == old_name)
        {
            quick->setHint(studio->getString("RenameSame", args));
        }
        else if (std::any_of(doc.outline.begin(), doc.outline.end(), [&name](const ALScriptOutlineEntry& entry) { return entry.name == name; }))
        {
            // Allowed, since a name in another scope may be meant; said.
            quick->setHint(studio->getString("RenameClash", args));
        }
        else
        {
            quick->setHint(scripts > 1 ? studio->getString("RenameToAcross", args) : studio->counted("RenameTo", count, args));
        }
    });
    quick->setQuery(old_name);
    quick->takeFocus();
}

bool ALFloaterScriptStudio::reservedName(const Doc& doc, const std::string& name) const
{
    // The language's own words, which a name of the script's cannot be:
    // its keywords and types, the preprocessor's words while their
    // transforms are on, and every function, event and constant the
    // definitions give.
    static const std::set<std::string> LSL{ "default", "state", "event", "jump", "return", "if", "else", "for", "do", "while", "print",
                                           "integer", "float", "string", "key", "vector", "rotation", "quaternion", "list" };
    static const std::set<std::string> LUAU{ "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "if", "in",
                                            "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while", "continue" };
    if (doc.language.lua ? LUAU.count(name) > 0 : LSL.count(name) > 0)
    {
        return true;
    }
    if (!doc.language.lua)
    {
        if ((name == "switch" || name == "case") && gSavedSettings.getBOOL("ALScriptPreprocSwitch"))
        {
            return true;
        }
        if ((name == "break" || name == "continue" || name == "inline") && gSavedSettings.getBOOL("ALScriptPreprocExtensions"))
        {
            return true;
        }
    }
    return ALScriptStudioWords::word(doc.language.lua, name) != nullptr;
}

void ALFloaterScriptStudio::renameTo(const std::string& id, U32 generation, const std::string& new_name)
{
    const size_t index = indexOf(id);
    if (index == NONE || mDocs[index]->lookup.generation != generation)
    {
        return;
    }
    Doc&              doc      = *mDocs[index];
    const Doc::Lookup& lookup  = doc.lookup;
    const std::string old_name = lookup.name;
    std::string       name     = new_name;
    LLStringUtil::trim(name);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name;
    if (!isIdentifier(name))
    {
        setStatus(getString("RenameBadName", args), true);
        return;
    }
    if (reservedName(doc, name))
    {
        setStatus(getString("RenameReserved", args), true);
        return;
    }
    if (name == old_name)
    {
        doc.editor->setFocus(true);
        return;
    }
    if (doc.editor->document().version() != lookup.version)
    {
        setStatus(getString("RenameStale", args), true);
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
            size_t at = indexOf(file);
            if (at != NONE)
            {
                stale += rename_open(*mDocs[at], file, places) ? 0 : 1;
                continue;
            }
            openFile(path, doc.language.lua);
            at = indexOf(file);
            if (at == NONE)
            {
                // Gone, or opened in another window.
                ++stale;
                continue;
            }
            // Read as it is on disk now, which the expansion may not have
            // been: each place where the old name still stands.
            Doc& other = *mDocs[at];
            for (const Doc::Place* place : places)
            {
                other.pendingEdits.push_back(Doc::PendingEdit{ place->span, old_name, name });
            }
            // Counted as a script not open is, each place asked for; the
            // ones that no longer stand are said by the edits' own report.
            renamed += static_cast<S32>(places.size());
            applyPendingEdits(other);
            ++scripts;
            ++opened;
            continue;
        }
        const size_t other_index = indexOf(ref);
        if (other_index != NONE && mDocs[other_index]->loaded)
        {
            stale += rename_open(*mDocs[other_index], file, places) ? 0 : 1;
            continue;
        }
        // Not open: opened, with the change made once its text is in,
        // where the old name still stands at each place.
        if (other_index == NONE)
        {
            openScript(ref, places.front()->fileName);
        }
        const size_t opened_index = indexOf(ref);
        if (opened_index == NONE)
        {
            ++stale;
            continue;
        }
        Doc& other = *mDocs[opened_index];
        for (const Doc::Place* place : places)
        {
            other.pendingEdits.push_back(Doc::PendingEdit{ place->span, old_name, name });
        }
        renamed += static_cast<S32>(places.size());
        ++scripts;
        ++opened;
    }
    // What was done, a clause for each thing there is to say, each count in
    // its own form.
    args["[PLACES]"]  = counted("Places", renamed);
    args["[SCRIPTS]"] = counted("Scripts", scripts);
    std::string said  = getString(scripts > 1 || opened > 0 ? "RenamedIn" : "RenamedHere", args);
    if (opened > 0)
    {
        args["[SCRIPTS]"] = counted("Scripts", opened);
        said += "; " + getString("RenamedOpened", args);
    }
    if (stale > 0)
    {
        args["[SCRIPTS]"] = counted("Scripts", stale);
        said += "; " + getString("RenamedLeft", args);
    }
    report(said + ".", stale > 0);
    activate(index);
    doc.editor->setFocus(true);
}

void ALFloaterScriptStudio::applyPendingEdits(Doc& doc)
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
        report(counted(replace ? "ReplaceMissed" : "RenameMissed", missed, args), true, &doc);
    }
}

void ALFloaterScriptStudio::fillReferences()
{
    // The row chosen and the scroll kept through a refill, which an edit
    // moving the places asks for.
    mPlacesStale        = false;
    const S32 scrolled  = mReferences->getScrollPos();
    const S32 chosen    = mReferences->getFirstSelected() ? mReferences->getFirstSelected()->getValue().asInteger() : -1;
    mReferences->deleteAllItems();
    refreshBottomTabs();
    if (mFound.places.empty())
    {
        // A list with nothing in it and nothing to say is a pane that
        // looks broken; this one is empty until it is asked a question,
        // so it says which question.
        mReferencesHead->setText(getString("NoReferences"));
        return;
    }
    // What was looked up, and where it was found, over the places: the
    // status line says it once, and the next thing said takes it away.
    std::set<std::string> files;
    for (const Doc::Place& place : mFound.places)
    {
        files.insert(place.file);
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = mFound.name;
    args["[FILES]"] = std::to_string(files.size());
    mReferencesHead->setText(counted(files.size() > 1 ? "ReferencesFoundAcross" : "ReferencesFound", static_cast<S32>(mFound.places.size()), args));

    LLStringUtil::format_map_t named;
    named["[NAME]"] = mFound.name;
    const std::string declared_tip = getString("ReferenceDeclarationTip", named);
    for (size_t i = 0; i < mFound.places.size(); ++i)
    {
        const Doc::Place& place = mFound.places[i];
        // The declaration, marked: in the script it was looked up from
        // where no include declares it, else in the include that does.
        const std::string& in_file     = place.file;
        const bool         declaration = mFound.hasDefinition && place.span.line == mFound.definition.line &&
                                 place.span.column == mFound.definition.column && in_file == mFound.home;
        LLSD row;
        row["value"]                = static_cast<S32>(i);
        row["columns"][0]["column"] = "where";
        row["columns"][0]["value"]  = place.file.empty() ? mFound.fromName : place.fileName;
        row["columns"][1]["column"] = "line";
        row["columns"][1]["value"]  = llformat("%d:%d", place.span.line + 1, place.span.column + 1);
        row["columns"][2]["column"] = "role";
        row["columns"][2]["value"]  = declaration ? getString("ReferenceDeclaration") : std::string();
        row["columns"][3]["column"] = "text";
        row["columns"][3]["value"]  = place.text;
        if (declaration)
        {
            for (S32 c = 0; c < 4; ++c)
            {
                row["columns"][c]["font"]["style"] = "BOLD";
                row["columns"][c]["tool_tip"]      = declared_tip;
            }
        }
        LLScrollListItem* item = mReferences->addElement(row);
        // The name, lit where it stands in the line.
        if (item && place.at >= 0)
        {
            if (LLScrollListCell* text = item->getColumn(3))
            {
                text->highlightText(place.at, static_cast<S32>(mFound.name.size()));
            }
        }
    }
    if (chosen >= 0)
    {
        mReferences->selectByValue(LLSD(llmin(chosen, static_cast<S32>(mFound.places.size()) - 1)));
    }
    mReferences->setScrollPos(scrolled);
}

void ALFloaterScriptStudio::slidePlaces(Doc& doc, const ALTextDocument::Edit& edit)
{
    if (mFound.places.empty())
    {
        return;
    }
    // This script's places: its own, where it was looked up from, or the
    // ones in it as an include or another of the object's scripts.
    const std::string path    = doc.file.empty() ? ALScriptPreprocessor::pathOf(doc.ref) : doc.id;
    const size_t      before  = mFound.places.size();
    const S32         first   = edit.range.normalised().begin.line;
    const S32         last    = edit.rangeAfter().normalised().end.line;
    bool              changed = false;
    const auto        mine    = [&](const std::string& file) { return file.empty() ? doc.id == mFound.from : file == path; };
    const auto        slide   = [&](ALScriptSpan& span) {
        ALTextRange       range(ALTextPos(span.line, span.column), ALTextPos(span.endLine, span.endColumn));
        const ALTextRange was = range;
        if (!edit.slide(range))
        {
            return false;
        }
        if (range != was)
        {
            span.line      = range.begin.line;
            span.column    = range.begin.column;
            span.endLine   = range.end.line;
            span.endColumn = range.end.column;
            changed        = true;
        }
        return true;
    };
    mFound.places.erase(std::remove_if(mFound.places.begin(), mFound.places.end(),
                                       [&](Doc::Place& place) {
                                           if (!mine(place.file))
                                           {
                                               return false;
                                           }
                                           // The name itself edited: no longer a place it stands.
                                           if (!slide(place.span))
                                           {
                                               return true;
                                           }
                                           // Its line's words, where the edit was on it.
                                           if (place.span.line >= first && place.span.line <= last)
                                           {
                                               placeText(place, doc.editor->document().line(place.span.line));
                                               changed = true;
                                           }
                                           return false;
                                       }),
                        mFound.places.end());
    if (mFound.hasDefinition && mine(mFound.home))
    {
        mFound.hasDefinition = slide(mFound.definition);
    }
    // Refilled once this frame is drawn, however many edits it has.
    mPlacesStale = mPlacesStale || changed || mFound.places.size() != before;
}

void ALFloaterScriptStudio::onReferenceChosen(bool to_editor)
{
    LLScrollListItem* item = mReferences->getFirstSelected();
    if (!item)
    {
        return;
    }
    const size_t index = static_cast<size_t>(item->getValue().asInteger());
    if (index >= mFound.places.size())
    {
        return;
    }
    const Doc::Place place = mFound.places[index];
    if (!to_editor && mNavigation.deferOpen(mReferences, place.file))
    {
        return;
    }
    mNavigation.noteJump(!to_editor);
    ++mHoldPanes;
    if (place.file.empty())
    {
        // The script it was looked up from, whichever tab is in front.
        const size_t from = indexOf(mFound.from);
        if (from != NONE)
        {
            if (from != mActive)
            {
                activate(from);
            }
            sourceInFront(*mDocs[from]).goTo(rangeOf(place.span));
        }
    }
    else
    {
        openIncludeAt(place.file, place.fileName, place.span.line, place.span.column, place.span.endColumn - place.span.column);
    }
    --mHoldPanes;
    revealed(mReferences, to_editor);
}
