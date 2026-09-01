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

#include "../test/lltut.h"

namespace tut
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
    typedef test_group<alagentlocationformat_data> factory;
    typedef factory::object object;
}

namespace
{
    tut::factory tf("ALAgentLocationFormat");
}

namespace tut
{
    // A parcel with a name is what most of these show.
    template<> template<>
    void object::test<1>()
    {
        ensure_equals("NORMAL is the parcel and the region",
                      named(LLAgentUI::LOCATION_FORMAT_NORMAL),
                      std::string("Ye Olde Parcel, Hippotropolis"));

        ensure_equals("NORMAL_COORDS is the parcel and the position, no region",
                      named(LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS),
                      std::string("Ye Olde Parcel (128, 64, 22)"));

        ensure_equals("LANDMARK is the parcel alone",
                      named(LLAgentUI::LOCATION_FORMAT_LANDMARK),
                      std::string("Ye Olde Parcel"));

        ensure_equals("NO_MATURITY is everything but the rating",
                      named(LLAgentUI::LOCATION_FORMAT_NO_MATURITY),
                      std::string("Ye Olde Parcel, Hippotropolis (128, 64, 22)"));

        ensure_equals("NO_COORDS is everything but the position",
                      named(LLAgentUI::LOCATION_FORMAT_NO_COORDS),
                      std::string("Ye Olde Parcel, Hippotropolis - Adult"));

        ensure_equals("FULL is everything",
                      named(LLAgentUI::LOCATION_FORMAT_FULL),
                      std::string("Ye Olde Parcel, Hippotropolis (128, 64, 22) - Adult"));
    }

    // An unnamed parcel drops out entirely rather than leaving its comma
    // behind, and the region takes its place.
    template<> template<>
    void object::test<2>()
    {
        ensure_equals("NORMAL falls back to the region",
                      unnamed(LLAgentUI::LOCATION_FORMAT_NORMAL),
                      std::string("Hippotropolis"));

        ensure_equals("NORMAL_COORDS falls back to the region",
                      unnamed(LLAgentUI::LOCATION_FORMAT_NORMAL_COORDS),
                      std::string("Hippotropolis (128, 64, 22)"));

        ensure_equals("LANDMARK falls back to the region",
                      unnamed(LLAgentUI::LOCATION_FORMAT_LANDMARK),
                      std::string("Hippotropolis"));

        // The one place the two branches agree on the answer but reach it
        // differently: with no parcel to name, NO_MATURITY has nothing left to
        // distinguish it from NORMAL_COORDS.
        ensure_equals("NO_MATURITY falls back to the region",
                      unnamed(LLAgentUI::LOCATION_FORMAT_NO_MATURITY),
                      std::string("Hippotropolis (128, 64, 22)"));

        ensure_equals("NO_COORDS falls back to the region",
                      unnamed(LLAgentUI::LOCATION_FORMAT_NO_COORDS),
                      std::string("Hippotropolis - Adult"));

        ensure_equals("FULL falls back to the region",
                      unnamed(LLAgentUI::LOCATION_FORMAT_FULL),
                      std::string("Hippotropolis (128, 64, 22) - Adult"));
    }

    // No rating means no separator either, in both branches. A trailing " - "
    // is what forgetting this looks like.
    template<> template<>
    void object::test<3>()
    {
        ensure_equals("a named parcel with no rating has no separator",
                      named(LLAgentUI::LOCATION_FORMAT_NO_COORDS, ""),
                      std::string("Ye Olde Parcel, Hippotropolis"));

        ensure_equals("nor does FULL",
                      named(LLAgentUI::LOCATION_FORMAT_FULL, ""),
                      std::string("Ye Olde Parcel, Hippotropolis (128, 64, 22)"));

        ensure_equals("nor does an unnamed parcel",
                      unnamed(LLAgentUI::LOCATION_FORMAT_NO_COORDS, ""),
                      std::string("Hippotropolis"));

        ensure_equals("nor unnamed FULL",
                      unnamed(LLAgentUI::LOCATION_FORMAT_FULL, ""),
                      std::string("Hippotropolis (128, 64, 22)"));
    }

    // Negative and multi-digit coordinates reach the string intact. The
    // position is signed, and a region's Z runs well past three digits.
    template<> template<>
    void object::test<4>()
    {
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_FULL,
                                      "Parcel", "Region", "General", -4, 0, 4096);
        ensure_equals("negative and large coordinates survive",
                      out, std::string("Parcel, Region (-4, 0, 4096) - General"));
    }

    // The buffer is cleared, not appended to. Callers keep it between rebuilds
    // for its capacity, and a formatter that appended would grow it forever.
    template<> template<>
    void object::test<5>()
    {
        out = "left over from last time";
        ensure_equals("the previous answer is gone",
                      named(LLAgentUI::LOCATION_FORMAT_NORMAL),
                      std::string("Ye Olde Parcel, Hippotropolis"));

        // Including on the assigning branches, which do not go through the
        // formatter's sink at all.
        out = "left over from last time";
        ensure_equals("including where the name is assigned whole",
                      named(LLAgentUI::LOCATION_FORMAT_LANDMARK),
                      std::string("Ye Olde Parcel"));
    }

    // A landmark name is cut at LANDMARK_NAME_MAX_BYTES, and on a character
    // boundary. The printf this replaced used "%.100s", which counts bytes and
    // will cut a multi-byte character in half.
    template<> template<>
    void object::test<6>()
    {
        // 33 three-byte characters is 99 bytes; a 34th would straddle the
        // hundredth. A byte-counting truncation keeps one byte of it.
        std::string wide;
        for (int i = 0; i < 40; ++i)
        {
            wide += "\xe6\x97\xa5";     // U+65E5
        }
        ensure_equals("the fixture is three bytes per character",
                      wide.size(), std::size_t(120));

        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      wide, "Region", "", 0, 0, 0);

        ensure_equals("cut on the character before the limit, not across it",
                      out.size(), std::size_t(99));
        ensure_equals("and it is whole characters",
                      out, wide.substr(0, 99));

        // The region branch takes the same rule.
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      "", wide, "", 0, 0, 0);
        ensure_equals("the unnamed-parcel branch truncates too",
                      out.size(), std::size_t(99));
    }

    // A name shorter than the limit is not touched, and a name whose bytes
    // land exactly on it keeps all of them.
    template<> template<>
    void object::test<7>()
    {
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      "Short", "Region", "", 0, 0, 0);
        ensure_equals("a short name is left alone", out, std::string("Short"));

        const std::string exact(ALAgentLocationFormat::LANDMARK_NAME_MAX_BYTES, 'x');
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_LANDMARK,
                                      exact, "Region", "", 0, 0, 0);
        ensure_equals("a name exactly at the limit keeps every byte", out, exact);
    }

    // Only LANDMARK truncates. A long parcel name is shown in full everywhere
    // else, because everywhere else is a readout rather than an asset name.
    template<> template<>
    void object::test<8>()
    {
        const std::string long_name(200, 'y');
        ALAgentLocationFormat::format(out, LLAgentUI::LOCATION_FORMAT_NORMAL,
                                      long_name, "Region", "", 0, 0, 0);
        ensure_equals("NORMAL does not truncate",
                      out, long_name + ", Region");
    }
}
