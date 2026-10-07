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

#include <algorithm>
#include <cctype>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// What the vim keymap and the classes it is made of (ALVimSearch and the
// rest) read the text by, a character at a time, and the small rules they
// share: a word's classes, a line's blanks, brackets by the text alone, a
// count's bounds, the keys two typed make, a : command's name as vim reads
// it. Inside, not for anyone else -- but a host's : commands, whose names
// are read by the same rule.
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

    // --- a word's classes ---------------------------------------------------------------

    // vim's utf_class() tables past Latin-1 (mbyte.c), each in order: the
    // runs of codepoints with a class of their own -- blank 0, punctuation 1,
    // or the first codepoint of a script's block, which its characters share
    // -- and the emoji, 3, which are looked for first.
    struct ClassRun
    {
        U32 first;
        U32 last;
        S32 cls;
    };
    struct EmojiRun
    {
        U32 first;
        U32 last;
    };
    inline constexpr ClassRun CLASS_RUNS[] = {
        { 0x037e, 0x037e, 1 }, { 0x0387, 0x0387, 1 }, { 0x055a, 0x055f, 1 },
        { 0x0589, 0x0589, 1 }, { 0x05be, 0x05be, 1 }, { 0x05c0, 0x05c0, 1 },
        { 0x05c3, 0x05c3, 1 }, { 0x05f3, 0x05f4, 1 }, { 0x060c, 0x060c, 1 },
        { 0x061b, 0x061b, 1 }, { 0x061f, 0x061f, 1 }, { 0x066a, 0x066d, 1 },
        { 0x06d4, 0x06d4, 1 }, { 0x0700, 0x070d, 1 }, { 0x0964, 0x0965, 1 },
        { 0x0970, 0x0970, 1 }, { 0x0df4, 0x0df4, 1 }, { 0x0e4f, 0x0e4f, 1 },
        { 0x0e5a, 0x0e5b, 1 }, { 0x0f04, 0x0f12, 1 }, { 0x0f3a, 0x0f3d, 1 },
        { 0x0f85, 0x0f85, 1 }, { 0x104a, 0x104f, 1 }, { 0x10fb, 0x10fb, 1 },
        { 0x1361, 0x1368, 1 }, { 0x166d, 0x166e, 1 }, { 0x1680, 0x1680, 0 },
        { 0x169b, 0x169c, 1 }, { 0x16eb, 0x16ed, 1 }, { 0x1735, 0x1736, 1 },
        { 0x17d4, 0x17dc, 1 }, { 0x1800, 0x180a, 1 }, { 0x2000, 0x200b, 0 },
        { 0x200c, 0x2027, 1 }, { 0x2028, 0x2029, 0 }, { 0x202a, 0x202e, 1 },
        { 0x202f, 0x202f, 0 }, { 0x2030, 0x205e, 1 }, { 0x205f, 0x205f, 0 },
        { 0x2060, 0x206f, 1 }, { 0x2070, 0x207f, 0x2070 }, { 0x2080, 0x2094, 0x2080 },
        { 0x20a0, 0x27ff, 1 }, { 0x2800, 0x28ff, 0x2800 }, { 0x2900, 0x2998, 1 },
        { 0x29d8, 0x29db, 1 }, { 0x29fc, 0x29fd, 1 }, { 0x2e00, 0x2e7f, 1 },
        { 0x3000, 0x3000, 0 }, { 0x3001, 0x3020, 1 }, { 0x3030, 0x3030, 1 },
        { 0x303d, 0x303d, 1 }, { 0x3040, 0x309f, 0x3040 }, { 0x30a0, 0x30ff, 0x30a0 },
        { 0x3300, 0x9fff, 0x4e00 }, { 0xac00, 0xd7a3, 0xac00 }, { 0xf900, 0xfaff, 0x4e00 },
        { 0xfd3e, 0xfd3f, 1 }, { 0xfe30, 0xfe6b, 1 }, { 0xff00, 0xff0f, 1 },
        { 0xff1a, 0xff20, 1 }, { 0xff3b, 0xff40, 1 }, { 0xff5b, 0xff65, 1 },
        { 0x1d000, 0x1d24f, 1 }, { 0x1d400, 0x1d7ff, 1 }, { 0x1f000, 0x1f2ff, 1 },
        { 0x1f300, 0x1f9ff, 1 }, { 0x20000, 0x2a6df, 0x4e00 }, { 0x2a700, 0x2b73f, 0x4e00 },
        { 0x2b740, 0x2b81f, 0x4e00 }, { 0x2f800, 0x2fa1f, 0x4e00 },
    };
    inline constexpr EmojiRun EMOJI_RUNS[] = {
        { 0x203c, 0x203c }, { 0x2049, 0x2049 }, { 0x2122, 0x2122 }, { 0x2139, 0x2139 }, { 0x2194, 0x2199 },
        { 0x21a9, 0x21aa }, { 0x231a, 0x231b }, { 0x2328, 0x2328 }, { 0x23cf, 0x23cf }, { 0x23e9, 0x23f3 },
        { 0x23f8, 0x23fa }, { 0x24c2, 0x24c2 }, { 0x25aa, 0x25ab }, { 0x25b6, 0x25b6 }, { 0x25c0, 0x25c0 },
        { 0x25fb, 0x25fe }, { 0x2600, 0x2604 }, { 0x260e, 0x260e }, { 0x2611, 0x2611 }, { 0x2614, 0x2615 },
        { 0x2618, 0x2618 }, { 0x261d, 0x261d }, { 0x2620, 0x2620 }, { 0x2622, 0x2623 }, { 0x2626, 0x2626 },
        { 0x262a, 0x262a }, { 0x262e, 0x262f }, { 0x2638, 0x263a }, { 0x2640, 0x2640 }, { 0x2642, 0x2642 },
        { 0x2648, 0x2653 }, { 0x265f, 0x2660 }, { 0x2663, 0x2663 }, { 0x2665, 0x2666 }, { 0x2668, 0x2668 },
        { 0x267b, 0x267b }, { 0x267e, 0x267f }, { 0x2692, 0x2697 }, { 0x2699, 0x2699 }, { 0x269b, 0x269c },
        { 0x26a0, 0x26a1 }, { 0x26a7, 0x26a7 }, { 0x26aa, 0x26ab }, { 0x26b0, 0x26b1 }, { 0x26bd, 0x26be },
        { 0x26c4, 0x26c5 }, { 0x26c8, 0x26c8 }, { 0x26ce, 0x26cf }, { 0x26d1, 0x26d1 }, { 0x26d3, 0x26d4 },
        { 0x26e9, 0x26ea }, { 0x26f0, 0x26f5 }, { 0x26f7, 0x26fa }, { 0x26fd, 0x26fd }, { 0x2702, 0x2702 },
        { 0x2705, 0x2705 }, { 0x2708, 0x270d }, { 0x270f, 0x270f }, { 0x2712, 0x2712 }, { 0x2714, 0x2714 },
        { 0x2716, 0x2716 }, { 0x271d, 0x271d }, { 0x2721, 0x2721 }, { 0x2728, 0x2728 }, { 0x2733, 0x2734 },
        { 0x2744, 0x2744 }, { 0x2747, 0x2747 }, { 0x274c, 0x274c }, { 0x274e, 0x274e }, { 0x2753, 0x2755 },
        { 0x2757, 0x2757 }, { 0x2763, 0x2764 }, { 0x2795, 0x2797 }, { 0x27a1, 0x27a1 }, { 0x27b0, 0x27b0 },
        { 0x27bf, 0x27bf }, { 0x2934, 0x2935 }, { 0x2b05, 0x2b07 }, { 0x2b1b, 0x2b1c }, { 0x2b50, 0x2b50 },
        { 0x2b55, 0x2b55 }, { 0x3030, 0x3030 }, { 0x303d, 0x303d }, { 0x3297, 0x3297 }, { 0x3299, 0x3299 },
        { 0x1f004, 0x1f004 }, { 0x1f0cf, 0x1f0cf }, { 0x1f170, 0x1f171 }, { 0x1f17e, 0x1f17f }, { 0x1f18e, 0x1f18e },
        { 0x1f191, 0x1f19a }, { 0x1f1e6, 0x1f1ff }, { 0x1f201, 0x1f202 }, { 0x1f21a, 0x1f21a }, { 0x1f22f, 0x1f22f },
        { 0x1f232, 0x1f23a }, { 0x1f250, 0x1f251 }, { 0x1f300, 0x1f321 }, { 0x1f324, 0x1f393 }, { 0x1f396, 0x1f397 },
        { 0x1f399, 0x1f39b }, { 0x1f39e, 0x1f3f0 }, { 0x1f3f3, 0x1f3f5 }, { 0x1f3f7, 0x1f4fd }, { 0x1f4ff, 0x1f53d },
        { 0x1f549, 0x1f54e }, { 0x1f550, 0x1f567 }, { 0x1f56f, 0x1f570 }, { 0x1f573, 0x1f57a }, { 0x1f587, 0x1f587 },
        { 0x1f58a, 0x1f58d }, { 0x1f590, 0x1f590 }, { 0x1f595, 0x1f596 }, { 0x1f5a4, 0x1f5a5 }, { 0x1f5a8, 0x1f5a8 },
        { 0x1f5b1, 0x1f5b2 }, { 0x1f5bc, 0x1f5bc }, { 0x1f5c2, 0x1f5c4 }, { 0x1f5d1, 0x1f5d3 }, { 0x1f5dc, 0x1f5de },
        { 0x1f5e1, 0x1f5e1 }, { 0x1f5e3, 0x1f5e3 }, { 0x1f5e8, 0x1f5e8 }, { 0x1f5ef, 0x1f5ef }, { 0x1f5f3, 0x1f5f3 },
        { 0x1f5fa, 0x1f64f }, { 0x1f680, 0x1f6c5 }, { 0x1f6cb, 0x1f6d2 }, { 0x1f6d5, 0x1f6d7 }, { 0x1f6dc, 0x1f6e5 },
        { 0x1f6e9, 0x1f6e9 }, { 0x1f6eb, 0x1f6ec }, { 0x1f6f0, 0x1f6f0 }, { 0x1f6f3, 0x1f6fc }, { 0x1f7e0, 0x1f7eb },
        { 0x1f7f0, 0x1f7f0 }, { 0x1f90c, 0x1f93a }, { 0x1f93c, 0x1f945 }, { 0x1f947, 0x1f9ff }, { 0x1fa70, 0x1fa7c },
        { 0x1fa80, 0x1fa88 }, { 0x1fa90, 0x1fabd }, { 0x1fabf, 0x1fac5 }, { 0x1face, 0x1fadb }, { 0x1fae0, 0x1fae8 },
        { 0x1faf0, 0x1faf8 },
    };

    // vim's classes of a character, as utf_class() gives them: a blank 0,
    // punctuation 1, a word's 2 -- ASCII's letters, digits and underscore,
    // and Latin-1's as vim's default 'iskeyword' has them, U+00B5 and from
    // U+00C0 on;
    // past Latin-1, emoji 3, and the tables' -- the ideographs, the kana,
    // Hangul and the rest each a class of its own, a word apart from the
    // next -- else a word's. For a WORD, a blank and everything else.
    inline S32 codepointClass(llwchar c, bool big)
    {
        S32 cls = 2;
        if (c < 0x100)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == 0 || c == 0xa0)
            {
                return 0;
            }
            cls = (c < 0x80 ? isWordByte(static_cast<char>(c)) : c == 0xb5 || c >= 0xc0) ? 2 : 1;
        }
        else
        {
            const auto emoji = std::lower_bound(std::begin(EMOJI_RUNS), std::end(EMOJI_RUNS), c, [](const EmojiRun& r, llwchar cp) { return r.last < cp; });
            const auto run   = std::lower_bound(std::begin(CLASS_RUNS), std::end(CLASS_RUNS), c, [](const ClassRun& r, llwchar cp) { return r.last < cp; });
            if (emoji != std::end(EMOJI_RUNS) && emoji->first <= c)
            {
                cls = 3;
            }
            else if (run != std::end(CLASS_RUNS) && run->first <= c)
            {
                cls = run->cls;
            }
        }
        return big && cls != 0 ? 1 : cls;
    }

    inline bool atLineEnd(const ALTextDocument& d, const ALTextPos& p) { return p.column >= d.lineLength(p.line); }
    // The class of the character at a position (codepointClass); a line's
    // end is a blank.
    inline S32 classAt(const ALTextDocument& d, const ALTextPos& p, bool big)
    {
        const std::string& line = d.line(p.line);
        if (p.column < 0 || p.column >= static_cast<S32>(line.size()))
        {
            return 0;
        }
        return codepointClass(utf8str_decode_at(line, static_cast<size_t>(p.column)).cp, big);
    }
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
    // What a block keeps of its lines, as a register holds it, a line each:
    // what it holds of each (blockPiece), and blanks as wide as the block
    // for a line that stops short of its first column -- to the widest
    // line's end, for a block taken with $ -- as vim's yank pads one. A line
    // that reaches the first column and stops short of the last keeps what
    // it has.
    inline std::string blockText(const ALTextDocument& d, const std::vector<ALTextRange>& pieces, S32 left, S32 right, bool to_end, S32 tab_width)
    {
        S32 width = right - left + 1;
        if (to_end)
        {
            S32 widest = 0;
            for (const ALTextRange& piece : pieces)
            {
                widest = llmax(widest, d.displayColumn(d.lineEnd(piece.begin.line), tab_width));
            }
            width = widest - left + 1;
        }
        std::string text;
        for (size_t i = 0; i < pieces.size(); ++i)
        {
            if (i > 0)
            {
                text += '\n';
            }
            const ALTextRange& piece = pieces[i];
            if (piece.empty() && !padTo(d, piece.begin.line, left, tab_width).empty())
            {
                text.append(static_cast<size_t>(llmax(0, width)), ' ');
            }
            else
            {
                text += d.text(piece);
            }
        }
        return text;
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
    // what a command does with one, no more than MAX_COUNT. It is typed as
    // large as vim's may be, MAX_COUNT_TYPED, for go, whose count is a byte
    // of the text and may be any of a long one's, and reads it as typed.
    inline constexpr S32 MAX_COUNT       = 100000;
    inline constexpr S32 MAX_COUNT_TYPED = 999999999;
    inline S32 countOr(S32 count, S32 fallback = 1) { return count > 0 ? llmin(count, MAX_COUNT) : fallback; }
    // A count with one digit more typed, as vim's takes it: a tenth makes it
    // the most there is.
    inline S32 countTyped(S32 count, llwchar digit)
    {
        return count > MAX_COUNT_TYPED / 10 ? MAX_COUNT_TYPED : count * 10 + static_cast<S32>(digit - '0');
    }
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
    // Whether a : command's name is `whole` as vim reads its names: any of
    // it from the least it may be shortened to, `least`, to the whole --
    // d, de, del ... delete.
    inline bool abbreviates(std::string_view name, std::string_view least, std::string_view whole)
    {
        return name.size() >= least.size() && name.size() <= whole.size() && whole.compare(0, name.size(), name) == 0;
    }
    // :set, and its local and global forms, which are one here.
    inline bool isSetCommand(const std::string& name)
    {
        return abbreviates(name, "se", "set") || abbreviates(name, "setl", "setlocal") || abbreviates(name, "setg", "setglobal");
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
