/**
 * @file alstringmatch.h
 * @brief Whether one string is in another, without regard to case.
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

#include "altextchars.h"
#include "llstring.h"

#include <algorithm>
#include <string_view>

// What a filter typed over a list asks of each row: whether the words are
// in there, whatever case either was written in -- or begin it, or are it.
// Letters compared as the find bar compares them (alMatchAt), codepoint by
// codepoint, so that a letter past ASCII answers its other case here as it
// does there. Nothing is copied, since this is asked of every row on every
// letter typed.
struct ALStringMatch
{
    static bool containsNoCase(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
        {
            return true;
        }
        // At each character of it: an ASCII byte is one of its own.
        for (size_t at = 0; at < haystack.size();)
        {
            if (alMatchAt(haystack, at, needle, true) != std::string_view::npos)
            {
                return true;
            }
            at = static_cast<unsigned char>(haystack[at]) < 0x80 ? at + 1 : utf8str_decode_at(haystack, at).next;
        }
        return false;
    }

    // Whether the text begins with the words, as a completion's prefix
    // does, whatever case either was written in.
    static bool startsWithNoCase(std::string_view text, std::string_view prefix)
    {
        return alMatchAt(text, 0, prefix, true) != std::string_view::npos;
    }

    // Whether two words are the same word, as a flag's name read back is.
    static bool equalsNoCase(std::string_view a, std::string_view b)
    {
        return alMatchAt(a, 0, b, true) == a.size();
    }
};
