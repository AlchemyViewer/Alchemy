/**
 * @file llsdutil_tut.cpp
 * @author Adroit
 * @date 2007-02
 * @brief LLSD conversion routines test cases.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "m4math.h"
#include "v2math.h"
#include "v2math.h"
#include "v3color.h"
#include "v3math.h"
#include "v3dmath.h"
#include "v4coloru.h"
#include "v4math.h"
#include "llquaternion.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "stringize.h"
#include <set>
#include <boost/range.hpp>

namespace
{
    struct llsdutil_data
    {
        void test_matches(const std::string& proto_key, const LLSD& possibles,
                          const char** begin, const char** end)
        {
            std::set<std::string> succeed(begin, end);
            LLSD prototype(possibles[proto_key]);
            for (LLSD::map_const_iterator pi(possibles.beginMap()), pend(possibles.endMap());
                 pi != pend; ++pi)
            {
                std::string match(llsd_matches(prototype, pi->second));
                std::set<std::string>::const_iterator found = succeed.find(pi->first);
                if (found != succeed.end())
                {
                    // This test is supposed to succeed. Comparing to the
                    // empty string ensures that if the test fails, it will
                    // display the string received so we can tell what failed.
                    UNSCOPED_INFO("match");
                    REQUIRE(match == "");
                }
                else
                {
                    // This test is supposed to fail. If we get a false match,
                    // the string 'match' will be empty, which doesn't tell us
                    // much about which case went awry. So construct a more
                    // detailed description string.
                    UNSCOPED_INFO(proto_key + " shouldn't match " + pi->first);
                    REQUIRE_FALSE(match.empty());
                }
            }
        }
    };
    ;

    TEST_CASE_METHOD(llsdutil_data, "llsdutil: ll_sd_from_U64 round-trips a U64", "[llsdutil]")
    {
        LLSD sd;
        U64 valueIn , valueOut;
        valueIn = U64L(0xFEDCBA9876543210);
        sd = ll_sd_from_U64(valueIn);
        valueOut = ll_U64_from_sd(sd);
        UNSCOPED_INFO("U64 valueIn->sd->valueOut");
        CHECK(valueIn == valueOut);
    }

    TEST_CASE_METHOD(llsdutil_data, "llsdutil: ll_sd_from_U32 round-trips a U32", "[llsdutil]")
    {
        LLSD sd;
        U32 valueIn, valueOut;
        valueIn = 0x87654321;
        sd = ll_sd_from_U32(valueIn);
        valueOut = ll_U32_from_sd(sd);
        UNSCOPED_INFO("U32 valueIn->sd->valueOut");
        CHECK(valueIn == valueOut);
    }

    TEST_CASE_METHOD(llsdutil_data, "llsdutil: ll_sd_from_ipaddr round-trips an address", "[llsdutil]")
    {
        U32 valueIn, valueOut;
        valueIn = 0x87654321;
        LLSD sd;
        sd = ll_sd_from_ipaddr(valueIn);
        valueOut = ll_ipaddr_from_sd(sd);
        UNSCOPED_INFO("valueIn->sd->valueOut");
        CHECK(valueIn == valueOut);
    }

    TEST_CASE_METHOD(llsdutil_data, "llsdutil: llsd_matches and llsd_equals across every LLSD type", "[llsdutil]")
    {
        // for this test, construct a map of all possible LLSD types
        LLSD map;
        map.insert("empty",     LLSD());
        map.insert("Boolean",   LLSD::Boolean());
        map.insert("Integer",   LLSD::Integer(0));
        map.insert("Real",      LLSD::Real(0.0));
        map.insert("String",    LLSD::String("bah"));
        map.insert("NumString", LLSD::String("1"));
        map.insert("UUID",      LLSD::UUID());
        map.insert("Date",      LLSD::Date());
        map.insert("URI",       LLSD::URI());
        map.insert("Binary",    LLSD::Binary());
        map.insert("Map",       LLSD().with("foo", LLSD()));
        // Only an empty array can be constructed on the fly
        LLSD array;
        array.append(LLSD());
        map.insert("Array",     array);

        // These iterators are declared outside our various for loops to avoid
        // fatal MSVC warning: "I used to be broken, but I'm all better now!"
        LLSD::map_const_iterator mi, mend(map.endMap());

        /*-------------------------- llsd_matches --------------------------*/

        // empty prototype matches anything
        for (mi = map.beginMap(); mi != mend; ++mi)
        {
            UNSCOPED_INFO(std::string("empty matches ") + mi->first);
            REQUIRE(llsd_matches(LLSD(), mi->second) == "");
        }

        LLSD proto_array, data_array;
        for (int i = 0; i < 3; ++i)
        {
            proto_array.append(LLSD());
            data_array.append(LLSD());
        }

        // prototype array matches only array
        for (mi = map.beginMap(); mi != mend; ++mi)
        {
            UNSCOPED_INFO(std::string("array doesn't match ") + mi->first);
            REQUIRE_FALSE(llsd_matches(proto_array, mi->second).empty());
        }

        // data array must be at least as long as prototype array
        proto_array.append(LLSD());
        UNSCOPED_INFO("data array too short");
        CHECK(llsd_matches(proto_array, data_array) ==
                      "Array size 4 required instead of Array size 3");
        data_array.append(LLSD());
        UNSCOPED_INFO("data array just right");
        CHECK(llsd_matches(proto_array, data_array) == "");
        data_array.append(LLSD());
        UNSCOPED_INFO("data array longer");
        CHECK(llsd_matches(proto_array, data_array) == "");

        // array element matching
        data_array[0] = LLSD::String();
        UNSCOPED_INFO("undefined prototype array entry");
        CHECK(llsd_matches(proto_array, data_array) == "");
        proto_array[0] = LLSD::Binary();
        UNSCOPED_INFO("scalar prototype array entry");
        CHECK(llsd_matches(proto_array, data_array) ==
                      "[0]: Binary required instead of String");
        data_array[0] = LLSD::Binary();
        UNSCOPED_INFO("matching prototype array entry");
        CHECK(llsd_matches(proto_array, data_array) == "");

        // build a coupla maps
        LLSD proto_map, data_map;
        data_map["got"] = LLSD();
        data_map["found"] = LLSD();
        for (LLSD::map_const_iterator dmi(data_map.beginMap()), dmend(data_map.endMap());
             dmi != dmend; ++dmi)
        {
            proto_map[dmi->first] = dmi->second;
        }
        proto_map["foo"] = LLSD();
        proto_map["bar"] = LLSD();

        // prototype map matches only map
        for (mi = map.beginMap(); mi != mend; ++mi)
        {
            UNSCOPED_INFO(std::string("map doesn't match ") + mi->first);
            REQUIRE_FALSE(llsd_matches(proto_map, mi->second).empty());
        }

        // data map must contain all keys in prototype map
        std::string error(llsd_matches(proto_map, data_map));
        UNSCOPED_INFO("missing keys");
        CHECK_THAT(error, Catch::Matchers::ContainsSubstring("missing keys"));
        UNSCOPED_INFO("missing foo");
        CHECK_THAT(error, Catch::Matchers::ContainsSubstring("foo"));
        UNSCOPED_INFO("missing bar");
        CHECK_THAT(error, Catch::Matchers::ContainsSubstring("bar"));
        UNSCOPED_INFO("found found");
        CHECK_THAT(error, !Catch::Matchers::ContainsSubstring("found"));
        UNSCOPED_INFO("got got");
        CHECK_THAT(error, !Catch::Matchers::ContainsSubstring("got"));
        data_map["bar"] = LLSD();
        error = llsd_matches(proto_map, data_map);
        UNSCOPED_INFO("missing foo");
        CHECK_THAT(error, Catch::Matchers::ContainsSubstring("foo"));
        UNSCOPED_INFO("got bar");
        CHECK_THAT(error, !Catch::Matchers::ContainsSubstring("bar"));
        data_map["foo"] = LLSD();
        UNSCOPED_INFO("data map just right");
        CHECK(llsd_matches(proto_map, data_map) == "");
        data_map["extra"] = LLSD();
        UNSCOPED_INFO("data map with extra");
        CHECK(llsd_matches(proto_map, data_map) == "");

        // map element matching
        data_map["foo"] = LLSD::String();
        UNSCOPED_INFO("undefined prototype map entry");
        CHECK(llsd_matches(proto_map, data_map) == "");
        proto_map["foo"] = LLSD::Binary();
        UNSCOPED_INFO("scalar prototype map entry");
        CHECK(llsd_matches(proto_map, data_map) ==
                      "['foo']: Binary required instead of String");
        data_map["foo"] = LLSD::Binary();
        UNSCOPED_INFO("matching prototype map entry");
        CHECK(llsd_matches(proto_map, data_map) == "");

        // String
        {
            static const char* matches[] = { "String", "NumString", "Boolean", "Integer",
                                             "Real", "UUID", "Date", "URI" };
            test_matches("String", map, boost::begin(matches), boost::end(matches));
        }

        // Boolean, Integer, Real
        static const char* numerics[] = { "Boolean", "Integer", "Real" };
        for (const char **ni = boost::begin(numerics), **nend = boost::end(numerics);
             ni != nend; ++ni)
        {
            static const char* matches[] = { "Boolean", "Integer", "Real", "String", "NumString" };
            test_matches(*ni, map, boost::begin(matches), boost::end(matches));
        }

        // UUID
        {
            static const char* matches[] = { "UUID", "String", "NumString" };
            test_matches("UUID", map, boost::begin(matches), boost::end(matches));
        }

        // Date
        {
            static const char* matches[] = { "Date", "String", "NumString" };
            test_matches("Date", map, boost::begin(matches), boost::end(matches));
        }

        // URI
        {
            static const char* matches[] = { "URI", "String", "NumString" };
            test_matches("URI", map, boost::begin(matches), boost::end(matches));
        }

        // Binary
        {
            static const char* matches[] = { "Binary" };
            test_matches("Binary", map, boost::begin(matches), boost::end(matches));
        }

        /*-------------------------- llsd_equals ---------------------------*/

        // Cross-product of each LLSD type with every other
        for (LLSD::map_const_iterator lmi(map.beginMap()), lmend(map.endMap());
             lmi != lmend; ++lmi)
        {
            for (LLSD::map_const_iterator rmi(map.beginMap()), rmend(map.endMap());
                 rmi != rmend; ++rmi)
            {
                // Name this test based on the map keys naming the types of
                // interest, e.g "String::Integer".
                // We expect the values (xmi->second) to be equal if and only
                // if the type names (xmi->first) are equal.
                UNSCOPED_INFO((STRINGIZE(lmi->first << "::" << rmi->first)));
                REQUIRE(bool(lmi->first == rmi->first) ==
                       bool(llsd_equals(lmi->second, rmi->second)));
            }
        }

        // Array cases
        LLSD rarray;
        rarray.append(1.0);
        rarray.append(2);
        rarray.append("3");
        LLSD larray(rarray);
        UNSCOPED_INFO("llsd_equals(equal arrays)");
        CHECK(llsd_equals(larray, rarray));
        rarray[2] = "4";
        UNSCOPED_INFO("llsd_equals(different [2])");
        CHECK_FALSE(llsd_equals(larray, rarray));
        rarray = larray;
        rarray.append(LLSD::Date());
        UNSCOPED_INFO("llsd_equals(longer right array)");
        CHECK_FALSE(llsd_equals(larray, rarray));
        rarray = larray;
        rarray.erase(2);
        UNSCOPED_INFO("llsd_equals(shorter right array)");
        CHECK_FALSE(llsd_equals(larray, rarray));

        // Map cases
        LLSD rmap;
        rmap["San Francisco"] = 65;
        rmap["Phoenix"] = 92;
        rmap["Boston"] = 77;
        LLSD lmap(rmap);
        UNSCOPED_INFO("llsd_equals(equal maps)");
        CHECK(llsd_equals(lmap, rmap));
        rmap["Boston"] = 80;
        UNSCOPED_INFO("llsd_equals(different [\"Boston\"])");
        CHECK_FALSE(llsd_equals(lmap, rmap));
        rmap = lmap;
        rmap["Atlanta"] = 95;
        UNSCOPED_INFO("llsd_equals(superset right map)");
        CHECK_FALSE(llsd_equals(lmap, rmap));
        rmap = lmap;
        lmap["Seattle"] = 72;
        UNSCOPED_INFO("llsd_equals(superset left map)");
        CHECK_FALSE(llsd_equals(lmap, rmap));
    }

    TEST_CASE_METHOD(llsdutil_data, "llsdutil: hashing LLSD follows its type and contents", "[llsdutil]")
    {
        {
            LLSD data_s1 = LLSD::String("The quick brown aardvark jumped over the lazy lemming.");
            LLSD data_s2 = LLSD::String("The quick brown aardvark jumped over the lazy lemming.");

            UNSCOPED_INFO("hash: Identical string hashes match.");
            CHECK(boost::hash<LLSD>{}(data_s1) == boost::hash<LLSD>{}(data_s2));
        }
        {
            LLSD data_r1 = LLSD::Real(3.0f);
            LLSD data_i1 = LLSD::Integer(3);
            UNSCOPED_INFO("hash: equivalent values but different types do not match.");
            CHECK(boost::hash<LLSD>{}(data_r1) != boost::hash<LLSD>{}(data_i1));
        }
        {
            LLSD data_a1 = llsd::array("A", "B", "C");
            LLSD data_a2 = llsd::array("A", "B", "C");

            UNSCOPED_INFO("hash: identical arrays produce identical results");
            CHECK(boost::hash<LLSD>{}(data_a1) == boost::hash<LLSD>{}(data_a2));

            data_a2.append(llsd::array(1, 2));

            UNSCOPED_INFO("hash: changing the array changes the hash.");
            CHECK(boost::hash<LLSD>{}(data_a1) != boost::hash<LLSD>{}(data_a2));

            data_a1.append(llsd::array(1, 2));
            UNSCOPED_INFO("hash: identical arrays produce identical results with nested arrays");
            CHECK(boost::hash<LLSD>{}(data_a1) == boost::hash<LLSD>{}(data_a2));
        }
        {
            LLSD data_m1 = LLSDMap("key1", LLSD::Real(3.0))("key2", "value2")("key3", llsd::array(1, 2, 3));
            LLSD data_m2 = LLSDMap("key1", LLSD::Real(3.0))("key2", "value2")("key3", llsd::array(1, 2, 3));

            UNSCOPED_INFO("hash: identical maps produce identical results");
            CHECK(boost::hash<LLSD>{}(data_m1) == boost::hash<LLSD>{}(data_m2));

            LLSD data_m3 = LLSDMap("key1", LLSD::Real(5.0))("key2", "value2")("key3", llsd::array(1, 2, 3));
            UNSCOPED_INFO("hash: Different values in the map produce different hashes.");
            CHECK(boost::hash<LLSD>{}(data_m1) != boost::hash<LLSD>{}(data_m3));

            LLSD data_m4 = LLSDMap("keyA", LLSD::Real(3.0))("key2", "value2")("key3", llsd::array(1, 2, 3));
            UNSCOPED_INFO("hash: Different keys in the map produce different hashes.");
            CHECK(boost::hash<LLSD>{}(data_m1) != boost::hash<LLSD>{}(data_m4));

        }
    }

}
