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

    // Where text put in at a place ends: past its last line break, or along
    // the place's own line (alTextEnd).
    ALTextPos endOf(const ALTextPos& at, const std::string& text);
    // A place in a text moved by replacements of it, in order and none over
    // another, as a mark is (ALTextDocument::Edit::placed): pushed along by
    // text put in right at it, unless not `pushed`.
    ALTextPos placedThrough(const std::vector<Replacement>& replacements, const ALTextPos& pos, bool pushed = true);

    // --- several selections at once ---------------------------------------------

    // A command done at several selections at once is worked out in groups:
    // a selection on its own, or those that share the lines a command over
    // whole lines does once. A group says what it replaces, over the text as
    // it stands, in order and none over another; and where each selection it
    // is for is after -- by its index among the selections the command was
    // given, anchor to caret -- in the text as the group's own replacements
    // leave it, as a Change says of its one selection.
    struct Group
    {
        std::vector<Replacement>                    replacements;
        std::vector<std::pair<size_t, ALTextRange>> placed;
    };
    // Groups made one change of the text: every replacement in order, and
    // each selection where its group leaves it in the text as all of them
    // do, the groups before it having moved it along. A group whose
    // replacements land over one's before it is left out, and its selections
    // with it: those, and any no group placed, are nothing here, for
    // whoever makes the change to slide along with the text.
    struct Combined
    {
        std::vector<Replacement>                replacements;
        std::vector<std::optional<ALTextRange>> selections;
    };
    Combined combine(std::vector<Group> groups, size_t count);

    // Selections, in the order they begin, gathered into runs over the same
    // lines -- or over lines next to each other too, where `touching` --
    // each run's lines, first to last, and the selections in it.
    struct LineRun
    {
        S32                 first = 0;
        S32                 last  = 0;
        std::vector<size_t> selections;
    };
    std::vector<LineRun> lineRuns(const std::vector<ALTextRange>& selections, bool touching);

    // The commands over whole lines, at several selections in the order
    // they begin: each run of them over the same lines done once, each
    // selection going as the one selection goes. Moving lines takes runs on
    // lines next to each other as one, as does deleting them, and moves
    // nothing where any run is at the top or the bottom already; joining
    // puts every caret of a run where its first join is. Comments go in, or
    // out, the same way for all of them: out where every line any of them
    // reaches that says anything is commented.
    std::vector<Group> duplicateLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections);
    std::vector<Group> moveLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections, S32 direction);
    std::vector<Group> deleteLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections);
    std::vector<Group> toggleComment(const ALTextDocument& doc, const std::vector<ALTextRange>& selections, const std::string& token);
    std::vector<Group> joinLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections);
}
