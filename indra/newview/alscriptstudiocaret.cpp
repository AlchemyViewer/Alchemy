/**
 * @file alscriptstudiocaret.cpp
 * @brief Script Studio's name at the caret: its definition gone to, its references and rename asked for, the inspector told.
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

#include "alscriptstudiocaret.h"

#include "alcodeeditor.h"
#include "alscriptstudioplaces.h"
#include "alscriptstudioservices.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <optional>

using ALScriptPlaces::lineOf;
using ALScriptPlaces::mapSpan;
using ALScriptPlaces::placeText;
using ALScriptPlaces::rangeOf;

ALScriptStudioCaret::ALScriptStudioCaret(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window) {}

void ALScriptStudioCaret::ask(Doc& doc, ALEditorCommand command, const ALTextRange& word)
{
    doc.caret.symbolCommand = command;
    doc.caret.symbolVersion = doc.editor->document().version();
    doc.caret.symbolAt      = word.begin;
    mWindow.askAnalyzer(doc, ALScriptAnalysis::Kind::References, word.begin);
}

void ALScriptStudioCaret::answered(Doc& doc, const ALScriptAnalysis::Result& result, const ALTextPos& at)
{
    // Of another question, or of a text that has moved on. Where it was
    // asked is the source's place, which the result's own is not where
    // the preprocessor ran and an include moved the lines.
    if (result.version != doc.caret.symbolVersion || at != doc.caret.symbolAt || doc.caret.symbolCommand == ALEditorCommand::None)
    {
        return;
    }
    const ALEditorCommand     command = doc.caret.symbolCommand;
    const ALScriptReferences& refs    = result.references;
    doc.caret.symbolCommand           = ALEditorCommand::None;
    LLStringUtil::format_map_t args;
    const std::string          name = refs.found ? refs.name : doc.editor->document().text(doc.editor->identifierAt(doc.caret.symbolAt));
    args["[NAME]"]                  = name;
    // A word of the language has no definition in the script to go to --
    // where the script has not made one of its own: its reference is where
    // it is defined.
    const ALScriptStudioWords::Vocab* known =
        command == ALEditorCommand::GoToDefinition ? ALScriptStudioWords::word(doc.language.lua, name) : nullptr;
    if (known && (!refs.found || !refs.hasDefinition))
    {
        mWindow.showReference(*known, doc.language.lua);
        return;
    }
    if (!refs.found)
    {
        // Nothing known because nothing could be read, which the syntax
        // errors in the problems explain, or nothing known of this name.
        mServices.setStatus(mServices.words(result.understood ? "NothingKnown" : "NothingKnownBroken", args), !result.understood);
        return;
    }
    // Back to the source: the declaration and each place in this script
    // or in an include, which keeps the include's identity.
    const bool         mapped        = mWindow.preprocessed(doc) && doc.expanded.valid && doc.expanded.version == result.version;
    const ALSourceMap* map           = mapped ? &doc.expanded.map : nullptr;
    bool               hasDefinition = refs.hasDefinition;
    ALScriptSpan       definition    = refs.definition;
    std::string        homePath;
    std::string        homeName;
    if (hasDefinition && map)
    {
        const S32 file = mapSpan(*map, definition);
        if (file < 0)
        {
            hasDefinition = false;
        }
        else if (file > 0)
        {
            homePath = map->files()[file].path;
            homeName = map->files()[file].name;
        }
    }
    std::vector<Doc::Place> places;
    places.reserve(refs.references.size());
    // Each file's lines found once, however many places are in it; the
    // expansion's too, for a place in an include not had.
    boost::unordered_flat_map<std::string, ALScriptPlaces::Lines, ll::string_hash, std::equal_to<>> files;
    std::optional<ALScriptPlaces::Lines>                                                            expansion;
    for (ALScriptSpan span : refs.references)
    {
        const ALScriptSpan raw = span;
        Doc::Place         place;
        if (map)
        {
            const S32 file = mapSpan(*map, span);
            if (file < 0)
            {
                continue;
            }
            if (file > 0)
            {
                place.file     = map->files()[file].path;
                place.fileName = map->files()[file].name;
            }
        }
        place.span = span;
        // The line as it was written: this script's, or the include's
        // where it is in hand; the expansion's, with its macros put in
        // place, only where it is not.
        auto lines = place.file.empty() ? files.end() : files.find(place.file);
        if (!place.file.empty() && lines == files.end())
        {
            lines = files.emplace(place.file, mWindow.sourceLines(place.file)).first;
        }
        if (place.file.empty())
        {
            placeText(place, lineOf(doc.editor->document(), span.line));
        }
        else if (lines->second.has(span.line))
        {
            placeText(place, lines->second.line(span.line));
        }
        else
        {
            if (!expansion)
            {
                expansion.emplace(doc.expanded.text);
            }
            placeText(place, expansion->line(raw.line));
            place.at = -1;
        }
        places.push_back(std::move(place));
    }
    switch (command)
    {
        case ALEditorCommand::GoToDefinition:
            if (!hasDefinition)
            {
                mServices.setStatus(mServices.words("NoDefinition", args));
                break;
            }
            mWindow.noteJump();
            if (homePath.empty())
            {
                mWindow.goTo(doc, rangeOf(definition));
            }
            else
            {
                mWindow.openIncludeAt(homePath, homeName, definition.line, definition.column, definition.endColumn - definition.column);
            }
            break;
        case ALEditorCommand::FindReferences:
        case ALEditorCommand::Rename:
            mWindow.startLookup(doc, command, refs, hasDefinition, homePath, definition, std::move(places), result.version);
            break;
        default:
            break;
    }
}

void ALScriptStudioCaret::pump(F64 now)
{
    Doc* doc = mServices.frontDoc();
    if (!doc || !doc->loaded || doc->notecard)
    {
        return;
    }
    // The caret of the view in front, which the trailer reads. What the
    // breadcrumb, the lit references and the inspector say is of the
    // source, and waits while the expansion is being read.
    const ALCodeEditor& shown  = *doc->shownText();
    const bool          source = doc->shownView() == Doc::View::Source;
    const ALTextPos     caret  = shown.caret();
    if (shown.hasFocus())
    {
        mWindow.keyboardInText();
    }
    if (caret != doc->caret.seen)
    {
        doc->caret.seen       = caret;
        doc->caret.inspectDue = source ? now + SETTLE : 0.0;
        mWindow.showPath(*doc);
        // A search's lit places, and a name's, each go once the caret has
        // left them all; vim's visual block and the places a substitution
        // asks about are vim's to put out.
        for (const ALCodeEditor::Highlight layer : { ALCodeEditor::Highlight::Search, ALCodeEditor::Highlight::References })
        {
            if (source && !doc->editor->highlights(layer).empty() && !doc->editor->highlighted(layer, caret))
            {
                doc->editor->clearHighlights(layer);
            }
        }
    }
    // Settled, and the inspector out to say it: a whole-text question at
    // every settle is the analyzers' time nobody reads with it folded.
    if (source && doc->caret.inspectDue > 0.0 && now >= doc->caret.inspectDue && mWindow.inspectorShown())
    {
        doc->caret.inspectDue = 0.0;
        const ALTextRange word    = doc->editor->identifierAtCaret();
        const U32         version = doc->editor->document().version();
        if (word.empty())
        {
            // No name here: what is wrong here, where anything is, and
            // otherwise the last name's words stay, rather than the pane
            // blanking at every space and bracket the caret passes.
            if (mWindow.showProblemsAt(*doc, caret))
            {
                doc->caret.inspectAt = ALTextPos(-1, -1);
            }
        }
        else if (word.begin != doc->caret.inspectAt || version != doc->caret.inspectVersion)
        {
            doc->caret.inspectAt      = word.begin;
            doc->caret.inspectVersion = version;
            mWindow.askAnalyzer(*doc, ALScriptAnalysis::Kind::Inspect, word.begin);
        }
    }
}
