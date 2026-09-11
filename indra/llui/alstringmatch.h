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

#include "llstring.h"

#include <algorithm>
#include <string_view>

// What a filter typed over a list asks of each row: whether the words are
// in there, whatever case either was written in. Nothing is copied, since
// this is asked of every row on every letter typed.
struct ALStringMatch
{
    static bool containsNoCase(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
        {
            return true;
        }
        const auto same = [](char a, char b)
        {
            return LLStringOps::toLower(a) == LLStringOps::toLower(b);
        };
        return std::search(haystack.begin(), haystack.end(),
                           needle.begin(), needle.end(), same) != haystack.end();
    }
};
