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

#include "alfuzzymatch.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alfuzzymatch_data
    {
        typedef ALFuzzyMatch::Tier Tier;

        static Tier tier(const char* name, const char* typed, Tier worst = Tier::Scattered)
        {
            const Tier bare     = ALFuzzyMatch::match(name, typed, worst).tier;
            const Tier prepared = ALFuzzyMatch::match(ALFuzzyMatch::prepare(name), typed, worst).tier;
            UNSCOPED_INFO("prepared or not, the same answer");
            REQUIRE(bare == prepared);
            return bare;
        }
    };

    TEST_CASE_METHOD(alfuzzymatch_data, "alfuzzymatch: each tier from prefix down to none for bare and prepared names", "[alfuzzymatch]")
    {
        UNSCOPED_INFO("the start as typed");
        CHECK(tier("llSay", "llSa") == Tier::Prefix);
        UNSCOPED_INFO("the start in either case");
        CHECK(tier("llSay", "LLSA") == Tier::PrefixAnyCase);
        UNSCOPED_INFO("a run from a part's start");
        CHECK(tier("llSay", "say") == Tier::PartRun);
        UNSCOPED_INFO("a run from a part's start, whatever its case");
        CHECK(tier("llSetPos", "setpos") == Tier::PartRun);
        {
            INFO("the letters of its parts, runs after them");
            CHECK(tier("llSetPos", "sp") == Tier::Parts);
            CHECK(tier("llSetPos", "setp") == Tier::PartRun);
            CHECK(tier("llSetPos", "spos") == Tier::Parts);
        }
        {
            INFO("a file's words");
            CHECK(tier("floater_buy.xml", "flbuy") == Tier::Parts);
            CHECK(tier("floater_buy.xml", "buy") == Tier::PartRun);
        }
        UNSCOPED_INFO("a run anywhere");
        CHECK(tier("floater_buy.xml", "oater") == Tier::Run);
        UNSCOPED_INFO("letters in order");
        CHECK(tier("floater_buy.xml", "fby") == Tier::Scattered);
        UNSCOPED_INFO("nothing out of order");
        CHECK(tier("floater_buy.xml", "yubx") == Tier::None);
        UNSCOPED_INFO("nothing longer than the name");
        CHECK(tier("btn", "button") == Tier::None);
        UNSCOPED_INFO("nothing typed is its start");
        CHECK(tier("anything", "") == Tier::Prefix);
    }

    TEST_CASE_METHOD(alfuzzymatch_data, "alfuzzymatch: a match worse than the tier asked for is none", "[alfuzzymatch]")
    {
        UNSCOPED_INFO("parts taken");
        CHECK(tier("llSetPos", "sp", Tier::Parts) == Tier::Parts);
        UNSCOPED_INFO("a run anywhere refused");
        CHECK(tier("floater_buy.xml", "oater", Tier::Parts) == Tier::None);
        UNSCOPED_INFO("scattered refused");
        CHECK(tier("floater_buy.xml", "fby", Tier::Run) == Tier::None);
        UNSCOPED_INFO("where a run begins");
        CHECK(ALFuzzyMatch::match("floater_buy.xml", "oater").at == 2);
    }

    TEST_CASE_METHOD(alfuzzymatch_data, "alfuzzymatch: a part begins after a break or at a case change or a digit", "[alfuzzymatch]")
    {
        const char* name = "get_HTTPRequest2go-now";
        std::string starts;
        for (size_t k = 0; k < strlen(name); ++k)
        {
            if (ALFuzzyMatch::partAt(name, k))
            {
                starts += name[k];
            }
        }
        UNSCOPED_INFO("g, H, R, 2, n: a letter after a digit begins none");
        CHECK(starts == std::string("gHR2n"));
        UNSCOPED_INFO("a letter beyond ASCII lowered as it is");
        CHECK(ALFuzzyMatch::lower('\xC3') == '\xC3');
    }
}
