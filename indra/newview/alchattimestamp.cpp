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
// The date is optional. Group 1 retains minutes and group 2 seconds.
const std::string CLOCK_PATTERN = R"(((?:\d{4}/\d{1,2}/\d{1,2}\s+)?\d{1,2}:\d{2})(:\d{2})?)";
// A log line's timestamp, whose AM/PM group 3 retains with its original
// spacing and case.
const ALRegex     PREFIX("\\[" + CLOCK_PATTERN + R"((\s[AaPp][Mm])?\]\s*)");
// A displayed timestamp names the half of the day in the viewer's language,
// and not every language spells it AM or PM, so group 3 retains whatever
// follows the clock after a blank.
const ALRegex     TIME(CLOCK_PATTERN + R"((\s.*)?)");
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
