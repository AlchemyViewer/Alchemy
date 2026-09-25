/**
 * @file alchattimestamp.cpp
 * @brief Chat timestamp recognition and display precision.
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

#include "llviewerprecompiledheaders.h"

#include "alchattimestamp.h"
#include "alregex.h"

namespace
{
// Date and AM/PM are optional. Group 1 retains minutes, group 2 seconds,
// and group 3 the AM/PM suffix, including its original spacing and case.
const std::string TIME_PATTERN = R"(((?:\d{4}/\d{1,2}/\d{1,2}\s+)?\d{1,2}:\d{2})(:\d{2})?(\s[AaPp][Mm])?)";
const ALRegex     TIME(TIME_PATTERN);
const ALRegex     PREFIX("\\[" + TIME_PATTERN + "\\]\\s*");
} // namespace

size_t ALChatTimestamp::prefixLength(std::string_view line)
{
    ALRegexMatch match;
    return PREFIX.search(line, &match, 0, true, 0) ? match.end() : 0;
}

std::string ALChatTimestamp::format(std::string_view timestamp, bool show_seconds)
{
    ALRegexMatch match;
    if (!show_seconds && TIME.match(timestamp, &match) && match.matched(2))
    {
        return match.str(1) + match.str(3);
    }
    return std::string(timestamp);
}
