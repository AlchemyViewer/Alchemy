/**
 * @file aldifftokens.cpp
 * @brief A line cut into the words two lines are compared by.
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

#include "linden_common.h"

#include "aldifftokens.h"

#include "altextchars.h"

bool ALDiffTokens::blank(char c)
{
    return c == ' ' || c == '\t';
}

ALTextDiff::spans_t ALDiffTokens::words(std::string_view line)
{
    ALTextDiff::spans_t out;
    size_t              i = 0;
    while (i < line.size())
    {
        const unsigned char c   = static_cast<unsigned char>(line[i]);
        size_t              end = i + 1;
        if (alWordByte(static_cast<char>(c)))
        {
            while (end < line.size() && alWordByte(line[end]))
            {
                ++end;
            }
        }
        else if (blank(static_cast<char>(c)))
        {
            while (end < line.size() && blank(line[end]))
            {
                ++end;
            }
        }
        out.emplace_back(static_cast<S32>(i), static_cast<S32>(end));
        i = end;
    }
    return out;
}
