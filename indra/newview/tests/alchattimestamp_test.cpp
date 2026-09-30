/**
 * @file alchattimestamp_test.cpp
 * @brief Tests for mixed-precision chat timestamps.
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

#include "../alchattimestamp.h"
#include "../test/lltut.h"

namespace tut
{
struct alchattimestamp_data
{
};
typedef test_group<alchattimestamp_data> alchattimestamp_group;
typedef alchattimestamp_group::object    alchattimestamp_object;
alchattimestamp_group                    alchattimestamp_test("alchattimestamp");

template<>
template<>
void alchattimestamp_object::test<1>()
{
    for (const std::string date : { "", "2026/09/25 ", "2026/9/5 " })
    {
        for (const std::string time : { "14:05", "14:05:09", "2:05 PM", "2:05:09 PM", "12:00 am", "12:00:00 am" })
        {
            for (const std::string separator : { "  ", " ", "\t", "" })
            {
                const std::string prefix  = "[" + date + time + "]" + separator;
                const std::string message = "Example Resident: text with a colon: and [14:05:09]";
                const std::string line    = prefix + message;
                const size_t      length  = ALChatTimestamp::prefixLength(line);
                ensure_equals("timestamp recognized: " + prefix, length, prefix.size());
                ensure_equals("message preserved", line.substr(length), message);
            }
        }
    }
}

template<>
template<>
void alchattimestamp_object::test<2>()
{
    for (const std::string timestamp : { "14:05", "2:05 PM", "2026/09/25 14:05", "2026/09/25 2:05 pm", "" })
    {
        ensure_equals("missing seconds stay missing", ALChatTimestamp::format(timestamp, true), timestamp);
        ensure_equals("minute display stays unchanged", ALChatTimestamp::format(timestamp, false), timestamp);
    }
}

template<>
template<>
void alchattimestamp_object::test<3>()
{
    for (const std::string date : { "", "2026/09/25 " })
    {
        // Italian and Japanese name the afternoon in words of their own.
        for (const std::string suffix : { "", " AM", " pm", " pomeridiane", " \xe5\x8d\x88\xe5\xbe\x8c" })
        {
            for (const std::string seconds : { ":00", ":09", ":59" })
            {
                const std::string minutes   = date + "2:05" + suffix;
                const std::string timestamp = date + "2:05" + seconds + suffix;
                ensure_equals("recorded seconds retained", ALChatTimestamp::format(timestamp, true), timestamp);
                ensure_equals("seconds hidden", ALChatTimestamp::format(timestamp, false), minutes);
                ensure_equals("mixed logs share a merge minute", ALChatTimestamp::format(timestamp, false),
                              ALChatTimestamp::format(minutes, false));
            }
        }
    }
}

template<>
template<>
void alchattimestamp_object::test<4>()
{
    for (const std::string line :
         { "", "Example Resident: hello", "text [14:05:09]", "[14:05:9] hello", "[14:05:091] hello", "[14:05:09 hello" })
    {
        ensure_equals("not a timestamp prefix: " + line, ALChatTimestamp::prefixLength(line), size_t(0));
    }
    ensure_equals("unknown time is not rewritten", ALChatTimestamp::format("unknown:time:00", false), "unknown:time:00");
    ensure_equals("overlong seconds are not rewritten", ALChatTimestamp::format("14:05:091", false), "14:05:091");
}
} // namespace tut
