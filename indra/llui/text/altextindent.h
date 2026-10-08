/**
 * @file altextindent.h
 * @brief Where a line's indentation belongs, as it is typed and as whole lines are shifted.
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

#include "altextediting.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class ALSyntaxGrammar;

// Where a line's indentation belongs as it is typed -- a tab's text, a
// new line's indentation, a line that closes a block brought out to the
// block's level -- and whole lines shifted a level or their blanks made
// again of tabs or spaces. In ALTextEditing's terms: each says what it
// would do, and the view does it.
namespace ALTextIndent
{
    using ALTextEditing::Change;
    using ALTextEditing::Replacement;

    struct Options
    {
        S32  tabWidth = 4;
        // Spaces to the next stop where a tab is typed.
        bool softTabs = false;
        friend bool operator==(const Options& a, const Options& b) { return a.tabWidth == b.tabWidth && a.softTabs == b.softTabs; }
    };

    // The bracket a closing one at a place closes, where whoever asks can
    // match brackets; false where it cannot, or there is none.
    typedef std::function<bool(const ALTextPos& closer, ALTextPos& opener)> opener_t;

    // --- the text's own ------------------------------------------------------------

    // How a text is indented, as its lines say: by tabs, a tab `tab_width`
    // wide since tabs do not say, or by spaces, as many a level as the
    // lines step in by most often. Nothing where it does not say -- no line
    // indented, or as many by tabs as by spaces. The first so many lines
    // are enough, and a block comment's lines that begin with a star, a
    // space in, are not indentation.
    std::optional<Options> detect(const ALTextDocument& doc, S32 tab_width, S32 lines = 10000);

    // --- as it is typed ------------------------------------------------------------

    // What a tab typed at a place puts in: a tab, or spaces to the next
    // stop where tabs are soft.
    std::string tabText(const ALTextDocument& doc, const ALTextPos& at, const Options& options);
    // Where Backspace at a place takes the text back to, where it takes
    // more than a character: in a line's leading blanks, spaces back to
    // the stop before, a level's worth -- never past a tab, nor past what
    // is not a space. Nothing where it takes one character, as ever.
    std::optional<ALTextPos> backspaceFrom(const ALTextDocument& doc, const ALTextPos& at, const Options& options);
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
    // further in under what opens a block, or under a head that sends
    // only the next line in (`if (x)`), and back out to that head past its
    // one statement; in a comment that goes on (`in_comment`, as the
    // caret's token says), what its lines begin with -- and, between a
    // bracket and the one that closes it, or under a block the language
    // closes with a word that is not closed below (SLua's `end`), the
    // closer on a line of its own and the caret on the line between them,
    // where it is not after what went in.
    std::optional<Replacement> closingBeforeReturn(const ALTextDocument& doc, const ALTextPos& anchor, const ALTextPos& caret,
                                                   const ALSyntaxGrammar* grammar, const opener_t& opener, const Options& options);
    struct Split
    {
        ALTextRange              range;
        std::string              text;
        std::optional<ALTextPos> caret;
    };
    Split splitLine(const ALTextDocument& doc, const ALTextRange& selection, const ALSyntaxGrammar* grammar, const Options& options,
                    bool in_comment = false);

    // A character just typed that finishes what closes a block, as the
    // first thing on its line: the line brought out to where it belongs;
    // or the brace a head's block goes on with, under a head that sent
    // the line in for one statement, level with the head; or a comment's
    // end typed on a line Return began for it, its blank taken away.
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

    // --- pasted lines ---------------------------------------------------------------

    // Lines pasted into a line's indentation, and where they go: the
    // first of them with anything on it to `base` columns in -- the
    // indentation of the line they went into, or where the line above
    // says a line goes when there is nothing else on it, a level out for
    // one that closes a block -- and each after it as far in from there as
    // it was from the first. The first may have been copied from where a
    // line's text begins, without its indentation: its width is then the
    // shallowest of the rest (`ref`), where none of them is at none.
    struct PastePlan
    {
        S32              base = 0;
        S32              ref  = 0;
        // Each pasted line's width, or -1 for a blank one.
        std::vector<S32> widths;
        // What followed the paste on its line goes on after it, at `base`.
        bool             restoreRest = false;
    };
    // Worked out before the paste goes in, over the selection it replaces;
    // nothing for a single line, a grammar that does not indent, or a paste
    // after text on its line.
    std::optional<PastePlan> planPaste(const ALTextDocument& doc, const ALTextRange& selection, std::string_view pasted, const ALSyntaxGrammar* grammar,
                                       const Options& options);
    // Then, over the text with the paste in at `at`: the lines' blanks
    // made so, in the blank the text is indented by, the caret moved with
    // its line. Nothing where every line is where it goes.
    std::optional<Change> reindentPasted(const ALTextDocument& doc, const ALTextPos& at, const PastePlan& plan, const ALTextPos& caret,
                                         const Options& options);

    // --- whole lines ------------------------------------------------------------------

    // Each line a level in, where it has anything on it, or out by what a
    // level is, at the selections in the order they begin: each run of
    // them over the same lines a level in or out once, and each selection's
    // anchor and caret moved with their lines' starts. Nothing for a run
    // with nothing to shift.
    std::vector<ALTextEditing::Group> indentLines(const ALTextDocument& doc, const std::vector<ALTextRange>& selections, bool in,
                                                  const Options& options);
    // Lines first through last so many levels in, those that are not
    // empty -- a tab a level, or a tab's width of spaces where tabs are
    // soft -- or out, each level a tab's width of their blanks as they are
    // drawn, taken from the front: vim's > and <. The replacements alone,
    // each at a line's start.
    Change shiftLines(const ALTextDocument& doc, S32 first, S32 last, S32 levels, bool in, const Options& options);
    // The leading blanks of lines first through last made again of spaces,
    // or of tabs as far as they go, a tab `tab_width` wide: vim's :retab,
    // Convert Indentation. Measured with tabs `measured_width` wide where
    // that is given -- :retab 4 over tabs that were 2 -- else `tab_width`.
    // Nothing where no line changes.
    std::optional<Change> convertIndentation(const ALTextDocument& doc, S32 first, S32 last, bool to_spaces, S32 tab_width,
                                             S32 measured_width = 0);
}
