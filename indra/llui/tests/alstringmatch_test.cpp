/**
 * @file alstringmatch_test.cpp
 * @brief Whether words are in, begin or are a string, whatever case either is in.
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

#include "alstringmatch.h"

#include "../test/lltut.h"

namespace tut
{
    struct alstringmatch_data
    {
    };
    typedef test_group<alstringmatch_data> alstringmatch_group;
    typedef alstringmatch_group::object    alstringmatch_object;
    alstringmatch_group                    alstringmatch_test_group("alstringmatch");

    template<> template<>
    void alstringmatch_object::test<1>()
    {
        set_test_name("in, begins, is: whatever case either is written in; nothing is in everything and begins it, and is only nothing");
        ensure("in", ALStringMatch::containsNoCase("floater_about.xml", "ABOUT"));
        ensure("not in", !ALStringMatch::containsNoCase("floater_about.xml", "abut"));
        ensure("nothing is in anything", ALStringMatch::containsNoCase("x", ""));

        ensure("begins", ALStringMatch::startsWithNoCase("llSay", "LLs"));
        ensure("the whole of it", ALStringMatch::startsWithNoCase("llSay", "LLSAY"));
        ensure("not further in", !ALStringMatch::startsWithNoCase("llSay", "say"));
        ensure("nor longer than it", !ALStringMatch::startsWithNoCase("ll", "llSay"));
        ensure("nothing begins anything", ALStringMatch::startsWithNoCase("llSay", ""));

        ensure("is", ALStringMatch::equalsNoCase("FOLLOWS_LEFT", "follows_left"));
        ensure("not a shorter", !ALStringMatch::equalsNoCase("follows_left", "follows_lef"));
        ensure("nor a longer", !ALStringMatch::equalsNoCase("follows_lef", "follows_left"));
        ensure("nor another of the same length", !ALStringMatch::equalsNoCase("follows_left", "follows_rght"));
        ensure("nothing is nothing", ALStringMatch::equalsNoCase("", ""));
    }
}
