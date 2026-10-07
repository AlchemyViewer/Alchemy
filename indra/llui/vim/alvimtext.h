/**
 * @file alvimtext.h
 * @brief Vim's own small helpers over the text, shared by the keymap's files.
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

#include "albracketindex.h"
#include "altextchars.h"
#include "altextdocument.h"
#include "llstring.h"

#include <cctype>
#include <string>

// What the vim keymap and the classes it is made of (ALVimSearch and the
// rest) read the text by, a character at a time, and the small rules they
// share: a word's classes, a line's blanks, brackets by the text alone, a
// count's bounds, the keys two typed make. Inside, not for anyone else.
namespace ALVimText
{
    // --- the text, a character at a time ------------------------------------------------

    // The byte at a position, or 0 at a line's end.
    inline char at(const ALTextDocument& d, const ALTextPos& p)
    {
        const std::string& line = d.line(p.line);
        return p.column >= 0 && p.column < static_cast<S32>(line.size()) ? line[p.column] : '\0';
    }

    inline bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\0'; }
    inline bool isWordByte(char c) { return alWordByte(c); }

    // vim's classes: blank, word, and everything else -- or, for a WORD,
    // blank and everything else.
    inline S32 classOf(char c, bool big)
    {
        if (isSpace(c))
        {
            return 0;
        }
        return big || isWordByte(c) ? 1 : 2;
    }

    inline bool atLineEnd(const ALTextDocument& d, const ALTextPos& p) { return p.column >= d.lineLength(p.line); }
    inline bool lineBlank(const ALTextDocument& d, S32 line)
    {
        for (const char c : d.line(line))
        {
            if (!isSpace(c))
            {
                return false;
            }
        }
        return true;
    }
    inline S32 firstNonBlankColumn(const ALTextDocument& d, S32 line)
    {
        const std::string& text = d.line(line);
        S32                c    = 0;
        while (c < static_cast<S32>(text.size()) && isSpace(text[c]))
        {
            ++c;
        }
        return c;
    }
    inline std::string indentOf(const ALTextDocument& d, S32 line) { return d.line(line).substr(0, firstNonBlankColumn(d, line)); }

    // One cluster on, across a line's end onto the next line's start;
    // false at the end of the text.
    inline bool stepOn(const ALTextDocument& d, ALTextPos& p)
    {
        if (atLineEnd(d, p))
        {
            if (p.line + 1 >= d.lineCount())
            {
                return false;
            }
            p = ALTextPos(p.line + 1, 0);
            return true;
        }
        p = d.nextCluster(p);
        return true;
    }
    // One cluster back, across a line's start onto the previous line's
    // end; false at the start of the text.
    inline bool stepBack(const ALTextDocument& d, ALTextPos& p)
    {
        if (p.column <= 0)
        {
            if (p.line <= 0)
            {
                return false;
            }
            p = d.lineEnd(p.line - 1);
            return true;
        }
        p = d.prevCluster(p);
        return true;
    }

    // The last character's position on a line, which normal mode's
    // caret never goes past; the start of an empty line.
    inline ALTextPos lastCharOf(const ALTextDocument& d, S32 line)
    {
        const ALTextPos end = d.lineEnd(line);
        return end.column > 0 ? d.prevCluster(end) : end;
    }

    // --- a visual block, by the columns a reader counts ---------------------------------

    // A block's columns from its two corners, as displayColumn counts them:
    // the first the one further left begins at, and the last the one further
    // right covers -- a tab to its stop, a line's end one column -- so that
    // the character at either corner is in it whole.
    inline void blockColumns(const ALTextDocument& d, const ALTextPos& a, const ALTextPos& b, S32 tab_width, S32& left, S32& right)
    {
        const auto past = [&d, tab_width](const ALTextPos& p) {
            return atLineEnd(d, p) ? d.displayColumn(p, tab_width) + 1 : d.displayColumn(d.nextCluster(p), tab_width);
        };
        left  = llmin(d.displayColumn(a, tab_width), d.displayColumn(b, tab_width));
        right = llmax(past(a), past(b)) - 1;
    }
    // What a block holds of a line: from the character its first column
    // falls in to past the one its last column falls in -- or to the
    // line's end, for a block taken with $ -- wherever the bytes before
    // them on the line put them, so that a character is taken whole or not
    // at all; empty, at the line's end, on a line that stops short of the
    // block.
    inline ALTextRange blockPiece(const ALTextDocument& d, S32 line, S32 left, S32 right, bool to_end, S32 tab_width)
    {
        const ALTextPos begin = d.posAtDisplayColumn(line, left, tab_width);
        if (to_end)
        {
            return ALTextRange(begin, d.lineEnd(line));
        }
        const ALTextPos last = d.posAtDisplayColumn(line, right, tab_width);
        return ALTextRange(begin, atLineEnd(d, last) ? last : d.nextCluster(last));
    }
    // The blanks that take a line out to a column, as the reader counts
    // them; none where it reaches that far already.
    inline std::string padTo(const ALTextDocument& d, S32 line, S32 column, S32 tab_width)
    {
        const S32 width = d.displayColumn(d.lineEnd(line), tab_width);
        return width < column ? std::string(static_cast<size_t>(column - width), ' ') : std::string();
    }

    // A bracket as % finds one on a line: the round, square and curly
    // ones, as the editor pairs them (ALBracketIndex::bracketOf).
    inline bool isBracket(char c)
    {
        char partner = '\0';
        bool opens   = false;
        return ALBracketIndex::bracketOf(c, partner, opens);
    }
    // Which a bracket pairs with, and whether it opens: the editor's, and
    // the angle brackets the a< and i< objects pair too.
    inline bool bracketOf(char c, char& partner, bool& opens)
    {
        if (c == '<' || c == '>')
        {
            partner = c == '<' ? '>' : '<';
            opens   = c == '<';
            return true;
        }
        return ALBracketIndex::bracketOf(c, partner, opens);
    }
    inline char partnerOf(char c)
    {
        char partner = '\0';
        bool opens   = false;
        return bracketOf(c, partner, opens) ? partner : '\0';
    }

    // The bracket that matches the one at a position, nesting counted.
    inline bool matchBracket(const ALTextDocument& d, ALTextPos from, ALTextPos& match)
    {
        const char c       = at(d, from);
        char       partner = '\0';
        bool       forward = false;
        if (!bracketOf(c, partner, forward))
        {
            return false;
        }
        S32        depth   = 0;
        ALTextPos  p       = from;
        do
        {
            const char here = at(d, p);
            if (here == c)
            {
                ++depth;
            }
            else if (here == partner && --depth == 0)
            {
                match = p;
                return true;
            }
        } while (forward ? stepOn(d, p) : stepBack(d, p));
        return false;
    }

    inline std::string utf8Of(llwchar ch) { return utf8str_from_cp(ch); }

    // The case of a stretch changed: swapped, lowered or raised, by
    // codepoint.
    inline std::string recased(const std::string& text, llwchar how)
    {
        return alRecased(text, static_cast<char>(how));
    }

    inline bool isDigit(llwchar ch) { return ch >= '0' && ch <= '9'; }

    // A count typed before a command: the digits as a number, at least one;
    // no more than this.
    inline constexpr S32 MAX_COUNT = 100000;
    inline S32 countOr(S32 count, S32 fallback = 1) { return count > 0 ? count : fallback; }
    // What two keys typed together make -- a motion, an operator, keys
    // waiting for another -- as command() and motion() take them, beside
    // the characters: past the last codepoint, so that no key typed and no
    // register played can be one.
    enum Composite : llwchar
    {
        // gg, ge, gE, gj, gk, g_, g0, g^, gm, g$, gM and go: motions.
        GO_TOP = 0x110000,
        WORD_END_BACK,
        BIG_WORD_END_BACK,
        DISPLAY_DOWN,
        DISPLAY_UP,
        LAST_NON_BLANK,
        DISPLAY_START,
        DISPLAY_FIRST,
        DISPLAY_MIDDLE,
        DISPLAY_END,
        LINE_MIDDLE,
        GO_BYTE,
        // gr pending, and gc: an operator.
        PENDING_GR,
        COMMENT_OPERATOR,
        // Surround's: ys, an operator; the character a ys stretch waits
        // for; ds and cs waiting for the pair to take away or change, and
        // cs for the pair it becomes.
        SURROUND_OPERATOR,
        SURROUND_WITH,
        DELETE_SURROUND,
        CHANGE_SURROUND,
        CHANGE_SURROUND_TO,
        // Control-W, waiting for the window command after it.
        WINDOW_PREFIX,
    };
    inline std::string shownKey(llwchar key)
    {
        switch (key)
        {
            case GO_TOP:             return "gg";
            case WORD_END_BACK:      return "ge";
            case BIG_WORD_END_BACK:  return "gE";
            case DISPLAY_DOWN:       return "gj";
            case DISPLAY_UP:         return "gk";
            case LAST_NON_BLANK:     return "g_";
            case DISPLAY_START:      return "g0";
            case DISPLAY_FIRST:      return "g^";
            case DISPLAY_MIDDLE:     return "gm";
            case DISPLAY_END:        return "g$";
            case LINE_MIDDLE:        return "gM";
            case GO_BYTE:            return "go";
            case PENDING_GR:         return "gr";
            case COMMENT_OPERATOR:   return "gc";
            case SURROUND_OPERATOR:
            case SURROUND_WITH:      return "ys";
            case DELETE_SURROUND:    return "ds";
            case CHANGE_SURROUND:
            case CHANGE_SURROUND_TO: return "cs";
            case WINDOW_PREFIX:      return "^W";
            default:                 return utf8Of(key);
        }
    }
    // What a surround character puts round a stretch, as surround.vim has
    // it: an opening bracket the pair with a space inside each, a closing
    // one -- or b, r, B, a -- the pair alone, and any other mark itself on
    // both sides. A letter, a digit or a blank is none.
    inline bool surroundPair(llwchar ch, std::string& open, std::string& close)
    {
        switch (ch)
        {
            case '(': open = "( "; close = " )"; return true;
            case ')':
            case 'b': open = "("; close = ")"; return true;
            case '[': open = "[ "; close = " ]"; return true;
            case ']':
            case 'r': open = "["; close = "]"; return true;
            case '{': open = "{ "; close = " }"; return true;
            case '}':
            case 'B': open = "{"; close = "}"; return true;
            case '<':
            case '>':
            case 'a': open = "<"; close = ">"; return true;
            default:  break;
        }
        if (ch < 0x21 || ch == 0x7F || (ch < 0x80 && std::isalnum(static_cast<int>(ch))))
        {
            return false;
        }
        open = close = utf8Of(ch);
        return true;
    }
    // A letter of a : command's name.
    inline bool isNameChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    // :set, and its local and global forms, which are one here.
    inline bool isSetCommand(const std::string& name)
    {
        return name == "set" || name == "se" || name == "setl" || name == "setlocal" || name == "setg" || name == "setglobal";
    }
    // An operator's count and its motion's together -- 3d2w is six words --
    // held to the same most, which their product would otherwise pass far
    // enough to wrap round.
    inline S32 countTimes(S32 a, S32 b) { return static_cast<S32>(llmin<S64>(static_cast<S64>(a) * static_cast<S64>(b), MAX_COUNT)); }
    // The most text a count may make of text there already -- a register
    // put, what an insert typed -- which the count alone does not bound:
    // a hundred thousand of a register a hundred kilobytes long is ten
    // gigabytes. A megabyte is more than any script.
    inline constexpr size_t MAX_COUNT_TEXT = 1024 * 1024;
    // vim's 'report' as it is by default: a change of more lines than this
    // -- lines deleted, yanked or shifted -- or of more substitutions is
    // said, and a smaller one is not.
    inline constexpr S32 REPORT_THRESHOLD = 2;
}
