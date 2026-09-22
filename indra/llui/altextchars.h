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

#include <string>
#include <string_view>

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
