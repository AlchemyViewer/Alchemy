/**
 * @file alscriptstudioselections.cpp
 * @brief A Script Studio window's selections compared: one held, then compared with another.
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

#include "alscriptstudioselections.h"

#include "alcodeeditor.h"
#include "aldiffview.h"
#include "alscriptstudioservices.h"

namespace
{
    // A place in a stretch's own text where the stretch starts at `at`.
    ALTextPos within(const ALTextPos& at, const ALTextPos& pos)
    {
        return pos.line == 0 ? ALTextPos(at.line, at.column + pos.column) : ALTextPos(at.line + pos.line, pos.column);
    }
}

ALScriptStudioSelections::ALScriptStudioSelections(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
}

// static
bool ALScriptStudioSelections::canHold(const Doc& doc)
{
    const ALCodeEditor* shown = doc.loaded ? doc.shownText() : nullptr;
    return shown && shown->hasSelection();
}

std::string ALScriptStudioSelections::titleOf(const Doc& doc, const ALTextRange& range) const
{
    // A selection ending at a line's start ends on the line before.
    const ALTextRange ordered = range.normalised();
    const S32         last    = ordered.end.column == 0 && ordered.end.line > ordered.begin.line ? ordered.end.line - 1 : ordered.end.line;
    LLStringUtil::format_map_t args;
    args["[NAME]"]  = doc.name;
    args["[FIRST]"] = std::to_string(mServices.shownLine(ordered.begin.line, doc.notecard));
    args["[LAST]"]  = std::to_string(mServices.shownLine(last, doc.notecard));
    return mServices.words(last == ordered.begin.line ? "SelectionLine" : "SelectionLines", args);
}

void ALScriptStudioSelections::hold(Doc& doc)
{
    if (!canHold(doc))
    {
        return;
    }
    const ALCodeEditor* shown = doc.shownText();
    mHeld                     = Held{ shown->selectedText(), titleOf(doc, shown->selection()) };
    LLStringUtil::format_map_t args;
    args["[TITLE]"] = mHeld->title;
    mServices.setStatus(mServices.words("SelectionHeld", args));
}

bool ALScriptStudioSelections::canCompare(const Doc& doc) const
{
    return mHeld && canHold(doc);
}

void ALScriptStudioSelections::compare(Doc& doc)
{
    if (!canCompare(doc))
    {
        return;
    }
    // Taken before the comparison takes the selection's place.
    ALCodeEditor* const shown = doc.shownText();
    const bool          own   = shown == doc.editor;
    const ALTextRange   range = shown->selection().normalised();
    const std::string   text  = shown->selectedText();
    const Held          held  = *mHeld;
    mWindow.compare(doc, held.text, text, held.title, titleOf(doc, range));
    if (!doc.compareView)
    {
        return;
    }
    if (!own)
    {
        // Text alone, from a view that is not the source: a keystroke has
        // no place in the script to go to, so none is sent there.
        doc.compareView->setOnEdit(nullptr);
        return;
    }
    // The right a stretch of the source, which the comparison follows as
    // its changes are taken back: where it stands now, and whether the
    // source still holds it there.
    const auto        stretch = std::make_shared<ALTextRange>(range);
    ALCodeEditor*     source  = doc.editor;
    ALDiffView* const view    = doc.compareView;
    const auto        holds   = [source, view, stretch]() { return source->document().text(*stretch) == view->rightText(); };
    const std::string id      = doc.id;
    view->setOnEdit([this, id, holds, stretch](S32 line, S32 column) -> LLView* {
        Doc* found = mServices.findDoc(id);
        if (!found || !found->loaded || !found->modifiable || found->editor->isReadOnly())
        {
            return nullptr;
        }
        return mWindow.typeInSource(*found, holds() ? within(stretch->begin, ALTextPos(line, column)) : stretch->begin);
    });
    if (doc.modifiable && !doc.editor->isReadOnly())
    {
        view->setOnTakeBack([source, holds, stretch](const ALTextRange& edit, const std::string& put) {
            if (!holds() || source->isReadOnly())
            {
                return false;
            }
            // The stretch's own places made the source's; and the stretch
            // as it will be: what was before the edit, what goes in, what
            // was after it.
            const std::string before = source->document().text(ALTextRange(stretch->begin, within(stretch->begin, edit.begin)));
            const std::string after  = source->document().text(ALTextRange(within(stretch->begin, edit.end), stretch->end));
            if (!source->replaceAll({ { ALTextRange(within(stretch->begin, edit.begin), within(stretch->begin, edit.end)), put } }))
            {
                return false;
            }
            stretch->end = alTextEnd(stretch->begin, before + put + after);
            return true;
        });
    }
}
