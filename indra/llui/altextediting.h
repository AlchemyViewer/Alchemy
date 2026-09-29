/**
 * @file altextediting.h
 * @brief A text view's commands over whole lines: duplicate, move, delete, comment and join them.
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


#pragma once

#include "altextdocument.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

// A text view's commands over whole lines -- duplicate, move and delete
// them, comment them out and back in, join them. Each is worked out over
// a document and a selection alone, and says what it would do: the
// replacements, made one after another as one step to undo, and where the
// selection goes after. The view does it, so that each can be tested with
// nothing laid out or drawn. Where a line's indentation belongs is
// ALTextIndent's, in the same terms.
namespace ALTextEditing
{
    // One stretch and what goes in its place, measured on the text as it is
    // before any replacement of the same change is made.
    struct Replacement
    {
        ALTextRange range;
        std::string text;
    };

    // What a command does: its replacements, none over another, made as one
    // edit (ALTextView::apply); and the selection after, in the text as it
    // is then -- anchor to caret where `selects`, the caret alone
    // otherwise, which takes the anchor with it. Positions are the
    // document's to clamp, as the view clamps whatever it is handed.
    struct Change
    {
        std::vector<Replacement> replacements;
        bool                     selects = false;
        ALTextPos                anchor;
        ALTextPos                caret;
    };

    // The lines a selection covers, as commands over whole lines count
    // them: a selection ending at a line's start does not take that line.
    std::pair<S32, S32> selectedLines(const ALTextRange& selection);
    // The lines again under them, the selection going with the copy.
    Change duplicateLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret);
    // The lines past the one above or below them, the selection with
    // them; nothing where they are at the top or the bottom already.
    std::optional<Change> moveLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, S32 direction);
    // Lines first through last put under line `below` -- -1 for above the
    // first -- as vim's :m and :t have it: moved, as one replacement of the
    // lines between, nothing where `below` is among them or is the line
    // above them already; or copied. The caret at the start of the last
    // line put there.
    std::optional<Change> moveLinesTo(const ALTextDocument& doc, S32 first, S32 last, S32 below);
    Change                copyLinesTo(const ALTextDocument& doc, S32 first, S32 last, S32 below);
    // The lines gone, the last with the break before it, and the caret on
    // the line that took their place, in its column where that line has it.
    Change deleteLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret);
    // The lines that say anything commented out with the line comment, or
    // back in where they all are: the selection the lines whole, or the
    // caret where it was in its text. Nothing where no line says anything.
    std::optional<Change> toggleComment(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, const std::string& token);
    // Lines first through last joined into one, as vim's J does: each break
    // and the next line's leading blanks gone, one space in their place --
    // none before a `)`, nor where the next line is blank; or, with
    // `keep_blanks`, the break alone, as its gJ. The caret where the first
    // join is. Nothing where last is not past first.
    std::optional<Change> joinLines(const ALTextDocument& doc, S32 first, S32 last, bool keep_blanks);
}
