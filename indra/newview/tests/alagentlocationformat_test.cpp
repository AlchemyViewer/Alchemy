/**
 * @file alagentlocationformat_test.cpp
 * @brief Tests for the twelve shapes a location readout can take
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

#include "../alagentlocationformat.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alagentlocationformat_data
    {
        // Every case names its own inputs; this is only somewhere to put the
        // answer, and reusing it across calls is also what the callers do.
        std::string out;

        std::string named(LLAgentUI::ELocationFormat format,
                          std::string_view sim_access = "Adult")
        {
            ALAgentLocationFormat::format(out, format, "Ye Olde Parcel",
                                          "Hippotropolis", sim_access, 128, 64, 22);
            return out;
        }

        std::string unnamed(LLAgentUI::ELocationFormat format,
                            std::string_view sim_access = "Adult")
        {
            ALAgentLocationFormat::format(out, format, "",
                                          "Hippotropolis", sim_access, 128, 64, 22);
            return out;
        }
    };
}

namespace
{
    // A parcel with a name is what most of these show.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: what each format shows of a named parcel", "[ALAgentLocationFormat]")
    {
        UNSCOPED_INFO("NORMAL is the parcel and the region");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_NORMAL) ==
                      std::string("Ye Olde Parcel, Hippotropolis"));

        UNSCOPED_INFO("NORMAL_COORDS is the parcel and the position, no region");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS) ==
                      std::string("Ye Olde Parcel (128, 64, 22)"));

        UNSCOPED_INFO("LANDMARK is the parcel alone");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_LANDMARK) ==
                      std::string("Ye Olde Parcel"));

        UNSCOPED_INFO("NO_MATURITY is everything but the rating");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_NO_MATURITY) ==
                      std::string("Ye Olde Parcel, Hippotropolis (128, 64, 22)"));

        UNSCOPED_INFO("NO_COORDS is everything but the position");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_NO_COORDS) ==
                      std::string("Ye Olde Parcel, Hippotropolis - Adult"));

        UNSCOPED_INFO("FULL is everything");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_FULL) ==
                      std::string("Ye Olde Parcel, Hippotropolis (128, 64, 22) - Adult"));
    }

    // An unnamed parcel drops out entirely rather than leaving its comma
    // behind, and the region takes its place.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: an unnamed parcel drops out and the region takes its place", "[ALAgentLocationFormat]")
    {
        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_NORMAL) ==
                      std::string("Hippotropolis"));

        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS) ==
                      std::string("Hippotropolis (128, 64, 22)"));

        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_LANDMARK) ==
                      std::string("Hippotropolis"));

        // The one place the two branches agree on the answer but reach it
        // differently: with no parcel to name, NO_MATURITY has nothing left to
        // distinguish it from NORMAL_COORDS.
        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_NO_MATURITY) ==
                      std::string("Hippotropolis (128, 64, 22)"));

        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_NO_COORDS) ==
                      std::string("Hippotropolis - Adult"));

        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_FULL) ==
                      std::string("Hippotropolis (128, 64, 22) - Adult"));
    }

    // No rating means no separator either, in both branches. A trailing " - "
    // is what forgetting this looks like.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: no rating leaves no separator for named or unnamed parcels", "[ALAgentLocationFormat]")
    {
        CHECK(named(LLAgentUI::LOCATION_FORMAT_NO_COORDS, "") ==
                      std::string("Ye Olde Parcel, Hippotropolis"));

        CHECK(named(LLAgentUI::LOCATION_FORMAT_FULL, "") ==
                      std::string("Ye Olde Parcel, Hippotropolis (128, 64, 22)"));

        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_NO_COORDS, "") ==
                      std::string("Hippotropolis"));

        CHECK(unnamed(LLAgentUI::LOCATION_FORMAT_FULL, "") ==
                      std::string("Hippotropolis (128, 64, 22)"));
    }

    // Negative and multi-digit coordinates reach the string intact. The
    // position is signed, and a region's Z runs well past three digits.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: negative and multi-digit coordinates reach the string intact", "[ALAgentLocationFormat]")
    {
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_FULL,
                                      "Parcel", "Region", "General", -4, 0, 4096);
        CHECK(out == std::string("Parcel, Region (-4, 0, 4096) - General"));
    }

    // The buffer is cleared, not appended to. Callers keep it between rebuilds
    // for its capacity, and a formatter that appended would grow it forever.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: the buffer is cleared rather than appended to", "[ALAgentLocationFormat]")
    {
        out = "left over from last time";
        CHECK(named(LLAgentUI::LOCATION_FORMAT_NORMAL) ==
                      std::string("Ye Olde Parcel, Hippotropolis"));

        // Including on the assigning branches, which do not go through the
        // formatter's sink at all.
        out = "left over from last time";
        UNSCOPED_INFO("including where the name is assigned whole");
        CHECK(named(LLAgentUI::LOCATION_FORMAT_LANDMARK) ==
                      std::string("Ye Olde Parcel"));
    }

    // A landmark name is cut at LANDMARK_NAME_MAX_BYTES, and on a character
    // boundary. The printf this replaced used "%.100s", which counts bytes and
    // will cut a multi-byte character in half.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: LANDMARK cuts a long name on a character boundary", "[ALAgentLocationFormat]")
    {
        // 33 three-byte characters is 99 bytes; a 34th would straddle the
        // hundredth. A byte-counting truncation keeps one byte of it.
        std::string wide;
        for (int i = 0; i < 40; ++i)
        {
            wide += "\xe6\x97\xa5";     // U+65E5
        }
        UNSCOPED_INFO("the fixture is three bytes per character");
        REQUIRE(wide.size() == std::size_t(120));

        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      wide, "Region", "", 0, 0, 0);

        UNSCOPED_INFO("cut on the character before the limit, not across it");
        REQUIRE(out.size() == std::size_t(99));
        UNSCOPED_INFO("and it is whole characters");
        CHECK(out == wide.substr(0, 99));

        // The region branch takes the same rule.
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      "", wide, "", 0, 0, 0);
        UNSCOPED_INFO("the unnamed-parcel branch truncates too");
        REQUIRE(out.size() == std::size_t(99));
    }

    // A name shorter than the limit is not touched, and a name whose bytes
    // land exactly on it keeps all of them.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: a name at or under the byte limit is kept whole", "[ALAgentLocationFormat]")
    {
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      "Short", "Region", "", 0, 0, 0);
        CHECK(out == std::string("Short"));

        const std::string exact(ALAgentLocationFormat::LANDMARK_NAME_MAX_BYTES, 'x');
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      exact, "Region", "", 0, 0, 0);
        UNSCOPED_INFO("a name exactly at the limit keeps every byte");
        CHECK(out == exact);
    }

    // Only LANDMARK truncates. A long parcel name is shown in full everywhere
    // else, because everywhere else is a readout rather than an asset name.
    TEST_CASE_METHOD(alagentlocationformat_data, "ALAgentLocationFormat: only LANDMARK truncates a long parcel name", "[ALAgentLocationFormat]")
    {
        const std::string long_name(200, 'y');
        const auto shown = [&](LLAgentUI::ELocationFormat format) {
            ALAgentLocationFormat::format(out, format, long_name, "Region", "General", 0, 0, 0);
            return out;
        };
        UNSCOPED_INFO("NORMAL does not truncate");
        CHECK(shown(LLAgentUI::LOCATION_FORMAT_NORMAL) == long_name + ", Region");
        UNSCOPED_INFO("NORMAL_COORDS does not truncate");
        CHECK(shown(LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS) == long_name + " (0, 0, 0)");
        UNSCOPED_INFO("NO_MATURITY does not truncate");
        CHECK(shown(LLAgentUI::LOCATION_FORMAT_NO_MATURITY) == long_name + ", Region (0, 0, 0)");
        UNSCOPED_INFO("NO_COORDS does not truncate");
        CHECK(shown(LLAgentUI::LOCATION_FORMAT_NO_COORDS) == long_name + ", Region - General");
        UNSCOPED_INFO("FULL does not truncate");
        CHECK(shown(LLAgentUI::LOCATION_FORMAT_FULL) == long_name + ", Region (0, 0, 0) - General");
        UNSCOPED_INFO("LANDMARK does");
        CHECK(shown(LLAgentUI::LOCATION_FORMAT_LANDMARK) == std::string(ALAgentLocationFormat::LANDMARK_NAME_MAX_BYTES, 'y'));
    }
}
