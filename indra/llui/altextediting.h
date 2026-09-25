/**
 * @file altextediting.h
 * @brief A text view's line commands and indentation: what a command does to whole lines, and where a line belongs.
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

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALSyntaxGrammar;

// A text view's commands over whole lines -- indent and outdent them,
// duplicate, move and delete them, comment them out and back in -- and
// where a line's indentation belongs as it is typed: a new line's, and a
// line that closes a block brought out to the block's level. Each is worked
// out over a document and a selection alone, and says what it would do: the
// replacements, made one after another as one step to undo, and where the
// selection goes after. The view does it, so that each can be tested with
// nothing laid out or drawn.
namespace ALTextEditing
{
    struct Options
    {
        S32  tabWidth = 4;
        // Spaces to the next stop where a tab is typed.
        bool softTabs = false;
    };

    // One stretch and what goes in its place, measured on the text as the
    // replacements before it in the same change left it.
    struct Replacement
    {
        ALTextRange range;
        std::string text;
    };

    // What a command does: its replacements, in the order they are made,
    // and the selection after -- anchor to caret where `selects`, the caret
    // alone otherwise, which takes the anchor with it. Positions are the
    // document's to clamp, as the view clamps whatever it is handed.
    struct Change
    {
        std::vector<Replacement> replacements;
        bool                     selects = false;
        ALTextPos                anchor;
        ALTextPos                caret;
    };

    // The bracket a closing one at a place closes, where whoever asks can
    // match brackets; false where it cannot, or there is none.
    typedef std::function<bool(const ALTextPos& closer, ALTextPos& opener)> opener_t;

    // --- indentation ------------------------------------------------------------------

    // What a tab typed at a place puts in: a tab, or spaces to the next
    // stop where tabs are soft.
    std::string tabText(const ALTextDocument& doc, const ALTextPos& at, const Options& options);
    // The blanks a line begins with.
    std::string leadingBlanks(const ALTextDocument& doc, S32 line);
    // An indentation one level in from, or out from, another, in the kind
    // of blank it is written in.
    std::string indentUnit(const std::string& like, const Options& options);
    std::string outdented(const std::string& indent, const Options& options);
    // Where a line that closes a block belongs, by the grammar's rules:
    // level with the line that opened it -- the bracket it closes, where
    // `opener` can match one, or else the line above, if that opens a
    // block, or a level out from it.
    std::string closingIndent(const ALTextDocument& doc, S32 line, const ALSyntaxGrammar* grammar, const opener_t& opener,
                              const Options& options);
    // A line's blanks put to an indentation, only ever further out: a line
    // put further out by hand stays where it was put. Nothing where the
    // line stays.
    std::optional<Replacement> reindent(const ALTextDocument& doc, S32 line, const std::string& indent, const Options& options);

    // Return, in two steps, the second over the text the first left.
    // First: a closing word the caret's line is, finished by the Return
    // rather than by a character after it, brought out as the character
    // would have brought it; nothing with a selection, or a grammar that
    // does not indent. Then the line split: what replaces the selection --
    // the new line with the indentation of the one it leaves, a level
    // further in under what opens a block -- and, between a bracket and
    // the one that closes it, the caret on the line between them, where it
    // is not after what went in.
    std::optional<Replacement> closingBeforeReturn(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret,
                                                   const ALSyntaxGrammar* grammar, const opener_t& opener, const Options& options);
    struct Split
    {
        ALTextRange              range;
        std::string              text;
        std::optional<ALTextPos> caret;
    };
    Split splitLine(const ALTextDocument& doc, const ALTextRange& selection, const ALSyntaxGrammar* grammar, const Options& options);

    // A character just typed that finishes what closes a block, as the
    // first thing on its line: the line brought out to where it belongs.
    // A word so brought out is kept -- its line, the caret after it, and
    // the indentation it came from -- so that a character after it that
    // makes it a longer word, a name, puts the line back. `last` is what
    // the character before this one kept; `next` is what this one keeps.
    struct AutoOutdent
    {
        S32         line   = -1;
        S32         column = -1;
        std::string indent;
    };
    struct Outdent
    {
        std::optional<Replacement> replacement;
        AutoOutdent                next;
    };
    Outdent outdentAsTyped(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, llwchar typed,
                           const ALSyntaxGrammar* grammar, const opener_t& opener, const AutoOutdent& last, const Options& options);

    // --- whole lines ------------------------------------------------------------------

    // The lines a selection covers, as commands over whole lines count
    // them: a selection ending at a line's start does not take that line.
    std::pair<S32, S32> selectedLines(const ALTextRange& selection);
    // Each line a level in, where it has anything on it, or out by what a
    // level is; the caret and the anchor moved with their lines' starts.
    Change indentLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, bool in, const Options& options);
    // The lines again under them, the selection going with the copy.
    Change duplicateLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret);
    // The lines past the one above or below them, the selection with
    // them; nothing where they are at the top or the bottom already.
    std::optional<Change> moveLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, S32 direction);
    // The lines gone, the last with the break before it, and the caret on
    // the line that took their place, in its column where that line has it.
    Change deleteLines(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret);
    // The lines that say anything commented out with the line comment, or
    // back in where they all are: the selection the lines whole, or the
    // caret where it was in its text. Nothing where no line says anything.
    std::optional<Change> toggleComment(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret, const std::string& token);
}
