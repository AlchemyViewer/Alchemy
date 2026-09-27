/**
 * @file alfuzzymatch_test.cpp
 * @brief How well a few letters typed answer a name.
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

#include "../alfuzzymatch.h"

#include "../test/lltut.h"

namespace tut
{
    struct alfuzzymatch_data
    {
        typedef ALFuzzyMatch::Tier Tier;

        static Tier tier(const char* name, const char* typed, Tier worst = Tier::Scattered)
        {
            const Tier bare     = ALFuzzyMatch::match(name, typed, worst).tier;
            const Tier prepared = ALFuzzyMatch::match(ALFuzzyMatch::prepare(name), typed, worst).tier;
            ensure("prepared or not, the same answer", bare == prepared);
            return bare;
        }
    };
    typedef test_group<alfuzzymatch_data> alfuzzymatch_group;
    typedef alfuzzymatch_group::object    alfuzzymatch_object;
    alfuzzymatch_group                    alfuzzymatch_instance("alfuzzymatch");

    template<> template<>
    void alfuzzymatch_object::test<1>()
    {
        set_test_name("each kind of answer, best first, prepared or not");
        ensure("the start as typed", tier("llSay", "llSa") == Tier::Prefix);
        ensure("the start in either case", tier("llSay", "LLSA") == Tier::PrefixAnyCase);
        ensure("a run from a part's start", tier("llSay", "say") == Tier::PartRun);
        ensure("a run from a part's start, whatever its case", tier("llSetPos", "setpos") == Tier::PartRun);
        ensure("the letters of its parts, runs after them", tier("llSetPos", "sp") == Tier::Parts && tier("llSetPos", "setp") == Tier::PartRun &&
                                                                 tier("llSetPos", "spos") == Tier::Parts);
        ensure("a file's words", tier("floater_buy.xml", "flbuy") == Tier::Parts && tier("floater_buy.xml", "buy") == Tier::PartRun);
        ensure("a run anywhere", tier("floater_buy.xml", "oater") == Tier::Run);
        ensure("letters in order", tier("floater_buy.xml", "fby") == Tier::Scattered);
        ensure("nothing out of order", tier("floater_buy.xml", "yubx") == Tier::None);
        ensure("nothing longer than the name", tier("btn", "button") == Tier::None);
        ensure("nothing typed is its start", tier("anything", "") == Tier::Prefix);
    }

    template<> template<>
    void alfuzzymatch_object::test<2>()
    {
        set_test_name("no worse than asked: a lesser kind of answer is none");
        ensure("parts taken", tier("llSetPos", "sp", Tier::Parts) == Tier::Parts);
        ensure("a run anywhere refused", tier("floater_buy.xml", "oater", Tier::Parts) == Tier::None);
        ensure("scattered refused", tier("floater_buy.xml", "fby", Tier::Run) == Tier::None);
        ensure("where a run begins", ALFuzzyMatch::match("floater_buy.xml", "oater").at == 2);
    }

    template<> template<>
    void alfuzzymatch_object::test<3>()
    {
        set_test_name("where a part begins: after a break, at a capital after a small letter, at the last of a run of capitals before a small one, at a digit");
        const char* name = "get_HTTPRequest2go-now";
        std::string starts;
        for (size_t k = 0; k < strlen(name); ++k)
        {
            if (ALFuzzyMatch::partAt(name, k))
            {
                starts += name[k];
            }
        }
        ensure_equals("g, H, R, 2, n: a letter after a digit begins none", starts, std::string("gHR2n"));
        ensure("a letter beyond ASCII lowered as it is", ALFuzzyMatch::lower('\xC3') == '\xC3');
    }
}
