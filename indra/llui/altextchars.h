/**
 * @file altextchars.h
 * @brief What the text widgets agree a word, a name and a matching letter are.
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

#include "llstring.h"

#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>

// The one answer the document, the view, the editor, the search and the
// vim keymap give to what a byte is: part of a word, part of a name, a
// blank. And letters compared without regard to case, by codepoint,
// since lower-casing a string can change its length and with it every
// offset, and a byte at a time folds nothing past ASCII.

// A word's byte, as a search or a caret's word motion counts one: a
// letter, a digit, an underscore, or anything beyond ASCII, which is a
// letter often enough to be treated as one.
inline bool alWordByte(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return u >= 0x80 || c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// A name's byte, as code spells one: ASCII letters, digits and the
// underscore.
inline bool alIdentifierByte(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Whether a part of a name begins at a byte of it: after an underscore, a
// dot, a colon, a dash, a slash or a blank; at a capital after a small
// letter, and at the last capital of a run before a small one; and at a
// digit after what is not one -- `Set` and `Pos` in `llSetPos`, `POSITION`
// in `PRIM_POSITION`. What fuzzy matching and subword motion both read.
inline bool alNamePartAt(std::string_view name, size_t at)
{
    const auto brk   = [](char c) { return c == '_' || c == '.' || c == ':' || c == '-' || c == '/' || c == '\\' || c == ' '; };
    const auto upper = [](char c) { return c >= 'A' && c <= 'Z'; };
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (at == 0)
    {
        return true;
    }
    const char prev = name[at - 1];
    const char c    = name[at];
    if (brk(prev))
    {
        return !brk(c);
    }
    if ((upper(c) && lower(prev)) || (digit(c) && !digit(prev)))
    {
        return true;
    }
    return upper(c) && upper(prev) && at + 1 < name.size() && lower(name[at + 1]);
}

// The end of a match of the needle at `at` in the hay, or npos. Without
// regard to case it compares codepoint by codepoint.
inline size_t alMatchAt(std::string_view hay, size_t at, std::string_view needle, bool case_insensitive)
{
    if (!case_insensitive)
    {
        if (at + needle.size() > hay.size() || hay.compare(at, needle.size(), needle) != 0)
        {
            return std::string_view::npos;
        }
        return at + needle.size();
    }
    size_t h = at;
    size_t n = 0;
    while (n < needle.size())
    {
        if (h >= hay.size())
        {
            return std::string_view::npos;
        }
        const LLCodepointAt hc = utf8str_decode_at(hay, h);
        const LLCodepointAt nc = utf8str_decode_at(needle, n);
        if (LLStringOps::toLower(hc.cp) != LLStringOps::toLower(nc.cp))
        {
            return std::string_view::npos;
        }
        h = hc.next;
        n = nc.next;
    }
    return h;
}

// Where a tab at a place reaches: the next stop past it, the stops a tab's
// width apart. In display columns, or in pixels as a line is laid out.
template <typename T>
inline T alNextTabStop(T at, T tab_width)
{
    if constexpr (std::is_floating_point_v<T>)
    {
        return tab_width > T(0) ? (std::floor(at / tab_width) + T(1)) * tab_width : at;
    }
    else
    {
        tab_width = tab_width > 0 ? tab_width : 1;
        return (at / tab_width + 1) * tab_width;
    }
}

// How wide the blanks a text begins with are drawn, in display columns,
// each tab to its next stop; and how many bytes they are.
inline S32 alBlanksWidth(std::string_view text, S32 tab_width, size_t* bytes = nullptr)
{
    S32    width = 0;
    size_t at    = 0;
    for (; at < text.size() && (text[at] == ' ' || text[at] == '\t'); ++at)
    {
        width = text[at] == '\t' ? alNextTabStop(width, tab_width) : width + 1;
    }
    if (bytes)
    {
        *bytes = at;
    }
    return width;
}

// A stretch of text with its tabs as spaces to the next stop, starting at
// a display column -- one per character, a tab reaching the next stop --
// which is moved on past it: what a text drawn somewhere a tab is not
// honoured shows, where the view's own stops are wanted.
inline std::string alExpandTabs(std::string_view text, S32& column, S32 tab_width)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c == '\t')
        {
            const S32 stop = alNextTabStop(column, tab_width);
            out.append(static_cast<size_t>(stop - column), ' ');
            column = stop;
            continue;
        }
        out.push_back(c);
        // A column a character: counted at its first byte.
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
        {
            ++column;
        }
    }
    return out;
}

// The replacement put in the case the match had, letter by letter past
// ASCII as well: all capitals where the match is, capitalised where it is,
// all small where it is; as it is where the match is none of those, or has
// no letters.
inline std::string alInCaseOf(std::string_view match, std::string_view text)
{
    bool letters = false, all_upper = true, all_lower = true, rest_lower = true, first_upper = false;
    for (size_t at = 0; at < match.size();)
    {
        const LLCodepointAt c = utf8str_decode_at(match, at);
        if (LLStringOps::isAlpha(c.cp))
        {
            const bool upper = LLStringOps::isUpper(c.cp);
            if (!letters)
            {
                first_upper = upper;
            }
            else if (upper)
            {
                rest_lower = false;
            }
            letters   = true;
            all_upper = all_upper && upper;
            all_lower = all_lower && !upper;
        }
        at = c.next;
    }
    if (!letters || (!all_upper && !all_lower && !(first_upper && rest_lower)))
    {
        return std::string(text);
    }
    std::string out;
    out.reserve(text.size());
    bool first = true;
    for (size_t at = 0; at < text.size();)
    {
        const LLCodepointAt c = utf8str_decode_at(text, at);
        const bool          up = all_upper || (!all_lower && first && LLStringOps::isAlpha(c.cp));
        utf8str_append_cp(out, up ? LLStringOps::toUpper(c.cp) : LLStringOps::toLower(c.cp));
        if (LLStringOps::isAlpha(c.cp))
        {
            first = false;
        }
        at = c.next;
    }
    return out;
}

// The text with its case changed, codepoint by codepoint: swapped for
// '~', lowered for 'u', raised for 'U' -- vim's three.
inline std::string alRecased(std::string_view text, char how)
{
    std::string out;
    out.reserve(text.size());
    for (size_t at = 0; at < text.size();)
    {
        const LLCodepointAt c = utf8str_decode_at(text, at);
        llwchar             cp = c.cp;
        if (how == 'u')
        {
            cp = LLStringOps::toLower(cp);
        }
        else if (how == 'U')
        {
            cp = LLStringOps::toUpper(cp);
        }
        else if (how == '~')
        {
            const llwchar lower = LLStringOps::toLower(cp);
            cp                  = lower != cp ? lower : LLStringOps::toUpper(cp);
        }
        utf8str_append_cp(out, cp);
        at = c.next;
    }
    return out;
}
