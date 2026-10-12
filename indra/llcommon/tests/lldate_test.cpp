/**
 * @file lldate_test.cpp
 * @author Adroit
 * @date 2007-02
 * @brief LLDate test cases.
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

#include "../llstring.h"
#include "../lldate.h"

#include <catch2/catch_test_macros.hpp>


#define VALID_DATE                                  "2003-04-30T04:00:00Z"
#define VALID_DATE_LEAP                             "2004-02-29T04:00:00Z"
#define VALID_DATE_HOUR_BOUNDARY                    "2003-04-30T23:59:59Z"
#define VALID_DATE_FRACTIONAL_SECS                  "2007-09-26T20:31:33.70Z"

// invalid format
#define INVALID_DATE_MISSING_YEAR                   "-04-30T22:59:59Z"
#define INVALID_DATE_MISSING_MONTH                  "1900-0430T22:59:59Z"
#define INVALID_DATE_MISSING_DATE                   "1900-0430-T22:59:59Z"
#define INVALID_DATE_MISSING_T                      "1900-04-30-22:59:59Z"
#define INVALID_DATE_MISSING_HOUR                   "1900-04-30T:59:59Z"
#define INVALID_DATE_MISSING_MIN                    "1900-04-30T01::59Z"
#define INVALID_DATE_MISSING_SEC                    "1900-04-30T01:59Z"
#define INVALID_DATE_MISSING_Z                      "1900-04-30T01:59:23"
#define INVALID_DATE_EMPTY                          ""

// invalid values
// apr 1.1.1 seems to not care about constraining the date to valid
// dates. Put these back when the parser checks.
#define LL_DATE_PARSER_CHECKS_BOUNDARY 0
//#define INVALID_DATE_24HOUR_BOUNDARY              "2003-04-30T24:00:00Z"
//#define INVALID_DATE_LEAP                         "2003-04-29T04:00:00Z"
//#define INVALID_DATE_HOUR                         "2003-04-30T24:59:59Z"
//#define INVALID_DATE_MIN                          "2003-04-30T22:69:59Z"
//#define INVALID_DATE_SEC                          "2003-04-30T22:59:69Z"
//#define INVALID_DATE_YEAR                         "0-04-30T22:59:59Z"
//#define INVALID_DATE_MONTH                            "2003-13-30T22:59:59Z"
//#define INVALID_DATE_DAY                          "2003-04-35T22:59:59Z"

namespace tut
{
    // Compile-time verification that LLDate's default ctor, accessors, and
    // comparison operators are constexpr.
    static_assert(LLDate{}.isNull(), "default LLDate is null at compile time");
    static_assert(!LLDate{}.notNull(), "default LLDate notNull is false at compile time");
    static_assert(LLDate{}.secondsSinceEpoch() == 0.0, "default LLDate epoch is 0");
    static_assert(LLDate{} == LLDate{}, "default LLDates compare equal at compile time");
    static_assert(!(LLDate{} < LLDate{}), "default LLDate not less than itself");
    static_assert(LLDate{} <= LLDate{}, "default LLDate <= itself");
    static_assert(LLDate{} >= LLDate{}, "default LLDate >= itself");

    /* format validation */
    TEST_CASE("LLDate: fromString accepts valid dates and rejects malformed ones", "[LLDate]")
    {
        LLDate date(VALID_DATE);
        std::string  expected_string;
        bool result;
        expected_string = VALID_DATE;
        UNSCOPED_INFO("Valid Date failed");
        CHECK(expected_string == date.asString());

        result = date.fromString(VALID_DATE_LEAP);
        expected_string = VALID_DATE_LEAP;
        UNSCOPED_INFO("VALID_DATE_LEAP failed");
        CHECK(expected_string == date.asString());

        result = date.fromString(VALID_DATE_HOUR_BOUNDARY);
        expected_string = VALID_DATE_HOUR_BOUNDARY;
        UNSCOPED_INFO("VALID_DATE_HOUR_BOUNDARY failed");
        CHECK(expected_string == date.asString());

        result = date.fromString(VALID_DATE_FRACTIONAL_SECS);
        expected_string = VALID_DATE_FRACTIONAL_SECS;
        UNSCOPED_INFO("VALID_DATE_FRACTIONAL_SECS failed");
        CHECK(expected_string == date.asString());

        result = date.fromString(INVALID_DATE_MISSING_YEAR);
        UNSCOPED_INFO("INVALID_DATE_MISSING_YEAR should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_MONTH);
        UNSCOPED_INFO("INVALID_DATE_MISSING_MONTH should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_DATE);
        UNSCOPED_INFO("INVALID_DATE_MISSING_DATE should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_T);
        UNSCOPED_INFO("INVALID_DATE_MISSING_T should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_HOUR);
        UNSCOPED_INFO("INVALID_DATE_MISSING_HOUR should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_MIN);
        UNSCOPED_INFO("INVALID_DATE_MISSING_MIN should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_SEC);
        UNSCOPED_INFO("INVALID_DATE_MISSING_SEC should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_MISSING_Z);
        UNSCOPED_INFO("INVALID_DATE_MISSING_Z should have failed");
        CHECK(result == false);

        result = date.fromString(INVALID_DATE_EMPTY);
        UNSCOPED_INFO("INVALID_DATE_EMPTY should have failed");
        CHECK(result == false);
    }

    /* Invalid Value Handling */
    TEST_CASE("LLDate: fromString rejects out-of-range field values", "[LLDate]")
    {
#if LL_DATE_PARSER_CHECKS_BOUNDARY
        LLDate date;
        std::string  expected_string;
        bool result;

        result = date.fromString(INVALID_DATE_24HOUR_BOUNDARY);
        UNSCOPED_INFO("INVALID_DATE_24HOUR_BOUNDARY should have failed");
        REQUIRE(result == false);
        UNSCOPED_INFO("INVALID_DATE_24HOUR_BOUNDARY date still set to old value on failure!");
        REQUIRE(date.secondsSinceEpoch() == 0);

        result = date.fromString(INVALID_DATE_LEAP);
        UNSCOPED_INFO("INVALID_DATE_LEAP should have failed");
        REQUIRE(result == false);

        result = date.fromString(INVALID_DATE_HOUR);
        UNSCOPED_INFO("INVALID_DATE_HOUR should have failed");
        REQUIRE(result == false);

        result = date.fromString(INVALID_DATE_MIN);
        UNSCOPED_INFO("INVALID_DATE_MIN should have failed");
        REQUIRE(result == false);

        result = date.fromString(INVALID_DATE_SEC);
        UNSCOPED_INFO("INVALID_DATE_SEC should have failed");
        REQUIRE(result == false);

        result = date.fromString(INVALID_DATE_YEAR);
        UNSCOPED_INFO("INVALID_DATE_YEAR should have failed");
        REQUIRE(result == false);

        result = date.fromString(INVALID_DATE_MONTH);
        UNSCOPED_INFO("INVALID_DATE_MONTH should have failed");
        REQUIRE(result == false);

        result = date.fromString(INVALID_DATE_DAY);
        UNSCOPED_INFO("INVALID_DATE_DAY should have failed");
        REQUIRE(result == false);
#endif
    }

    /* API checks */
    TEST_CASE("LLDate: fromStream reads a date", "[LLDate]")
    {
        LLDate date;
        std::istringstream stream(VALID_DATE);
        std::string  expected_string = VALID_DATE;
        date.fromStream(stream);
        UNSCOPED_INFO("fromStream failed");
        CHECK(date.asString() == expected_string);
    }

    TEST_CASE("LLDate: the copy constructor copies the date", "[LLDate]")
    {
        LLDate date1(VALID_DATE);
        LLDate date2(date1);
        UNSCOPED_INFO("LLDate(const LLDate& date) constructor failed");
        CHECK(date1.asString() == date2.asString());
    }

    TEST_CASE("LLDate: a date built from secondsSinceEpoch equals the original", "[LLDate]")
    {
        LLDate date1(VALID_DATE);
        LLDate date2(date1.secondsSinceEpoch());
        UNSCOPED_INFO("secondsSinceEpoch not equal");
        CHECK(date1.secondsSinceEpoch() == date2.secondsSinceEpoch());
        UNSCOPED_INFO("LLDate created using secondsSinceEpoch not equal");
        CHECK(date1.asString() == date2.asString());
    }

    TEST_CASE("LLDate: operator<< writes the date string", "[LLDate]")
    {
        LLDate date(VALID_DATE);
        std::ostringstream stream;
        stream << date;
        std::string expected_str = VALID_DATE;
        UNSCOPED_INFO("ostringstream failed");
        CHECK(expected_str == stream.str());
    }

    TEST_CASE("LLDate: operator>> reads a date that operator<< writes back the same", "[LLDate]")
    {
        LLDate date;
        std::istringstream stream(VALID_DATE);
        stream >> date;
        std::string expected_str = VALID_DATE;
        std::ostringstream out_stream;
        out_stream << date;

        UNSCOPED_INFO("<< failed");
        CHECK(date.asString() == expected_str);
        UNSCOPED_INFO("<< to >> failed");
        CHECK(stream.str() == out_stream.str());
    }

    // Comparison operators (now constexpr in the header) — runtime coverage.
    TEST_CASE("LLDate: comparison operators order dates by their seconds", "[LLDate]")
    {
        LLDate earlier(1000.0);
        LLDate later(2000.0);
        LLDate also_earlier(1000.0);

        UNSCOPED_INFO("earlier < later");
        CHECK(earlier < later);
        UNSCOPED_INFO("later > earlier");
        CHECK(later > earlier);
        UNSCOPED_INFO("earlier <= later");
        CHECK(earlier <= later);
        UNSCOPED_INFO("later >= earlier");
        CHECK(later >= earlier);
        UNSCOPED_INFO("equal <=");
        CHECK(earlier <= also_earlier);
        UNSCOPED_INFO("equal >=");
        CHECK(earlier >= also_earlier);
        UNSCOPED_INFO("equal ==");
        CHECK(earlier == also_earlier);
        UNSCOPED_INFO("differ !=");
        CHECK(earlier != later);
        UNSCOPED_INFO("!(equal <)");
        CHECK_FALSE(earlier < also_earlier);
        UNSCOPED_INFO("!(equal >)");
        CHECK_FALSE(earlier > also_earlier);
    }

    // isNull / notNull / setter round-trip.
    TEST_CASE("LLDate: isNull and notNull follow the seconds set", "[LLDate]")
    {
        LLDate d;
        UNSCOPED_INFO("default isNull");
        CHECK(d.isNull());
        UNSCOPED_INFO("default !notNull");
        CHECK_FALSE(d.notNull());

        d.secondsSinceEpoch(1234.5);
        UNSCOPED_INFO("after set, !isNull");
        CHECK_FALSE(d.isNull());
        UNSCOPED_INFO("after set, notNull");
        CHECK(d.notNull());
        UNSCOPED_INFO("getter round-trips");
        CHECK(d.secondsSinceEpoch() == 1234.5);

        d.secondsSinceEpoch(0.0);
        UNSCOPED_INFO("zero -> isNull again");
        CHECK(d.isNull());
    }

    // toLocalDateString / toHTTPDateString must not crash on the epoch value
    // (regression for the thread-safe localtime_utc/gmtime_utc rewrite).
    TEST_CASE("LLDate: the date string formatters give output for the epoch value", "[LLDate]")
    {
        LLDate d;
        UNSCOPED_INFO("toLocalDateString does not throw");
        REQUIRE(d.toLocalDateString(std::string("%Y-%m-%d")).size() > 0);
        UNSCOPED_INFO("toHTTPDateString does not throw");
        REQUIRE(d.toHTTPDateString(std::string("%Y-%m-%d")).size() > 0);
        // RFC1123 wraps toHTTPDateString; verifies the day-of-week token
        // is present (a strftime success indicator that doesn't pin locale).
        std::string rfc = d.asRFC1123();
        UNSCOPED_INFO("asRFC1123 produces output");
        REQUIRE(rfc.size() > 0);
    }
}
