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

#include <limits>
#include <sstream>

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
// LLDate::fromStream checks only the format: it hands the fields to timegm,
// which normalizes a value out of range rather than rejecting it (24:00:00 is
// the next day's midnight, 2003-02-29 is 2003-03-01). Turn the case below on
// when the parser checks the ranges.
#define LL_DATE_PARSER_CHECKS_BOUNDARY 0
#if LL_DATE_PARSER_CHECKS_BOUNDARY
#define INVALID_DATE_24HOUR_BOUNDARY                "2003-04-30T24:00:00Z"
#define INVALID_DATE_LEAP                           "2003-02-29T04:00:00Z"
#define INVALID_DATE_HOUR                           "2003-04-30T24:59:59Z"
#define INVALID_DATE_MIN                            "2003-04-30T22:69:59Z"
#define INVALID_DATE_SEC                            "2003-04-30T22:59:69Z"
#define INVALID_DATE_YEAR                           "0-04-30T22:59:59Z"
#define INVALID_DATE_MONTH                          "2003-13-30T22:59:59Z"
#define INVALID_DATE_DAY                            "2003-04-35T22:59:59Z"
#endif

namespace
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
        CHECK(expected_string == date.asString());

        result = date.fromString(VALID_DATE_LEAP);
        expected_string = VALID_DATE_LEAP;
        CHECK(expected_string == date.asString());

        result = date.fromString(VALID_DATE_HOUR_BOUNDARY);
        expected_string = VALID_DATE_HOUR_BOUNDARY;
        CHECK(expected_string == date.asString());

        result = date.fromString(VALID_DATE_FRACTIONAL_SECS);
        expected_string = VALID_DATE_FRACTIONAL_SECS;
        CHECK(expected_string == date.asString());

        result = date.fromString(INVALID_DATE_MISSING_YEAR);
        UNSCOPED_INFO("INVALID_DATE_MISSING_YEAR should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_MONTH);
        UNSCOPED_INFO("INVALID_DATE_MISSING_MONTH should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_DATE);
        UNSCOPED_INFO("INVALID_DATE_MISSING_DATE should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_T);
        UNSCOPED_INFO("INVALID_DATE_MISSING_T should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_HOUR);
        UNSCOPED_INFO("INVALID_DATE_MISSING_HOUR should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_MIN);
        UNSCOPED_INFO("INVALID_DATE_MISSING_MIN should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_SEC);
        UNSCOPED_INFO("INVALID_DATE_MISSING_SEC should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MISSING_Z);
        UNSCOPED_INFO("INVALID_DATE_MISSING_Z should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_EMPTY);
        UNSCOPED_INFO("INVALID_DATE_EMPTY should have failed");
        CHECK_FALSE(result);
    }

#if LL_DATE_PARSER_CHECKS_BOUNDARY
    /* Invalid Value Handling */
    TEST_CASE("LLDate: fromString rejects out-of-range field values", "[LLDate]")
    {
        LLDate date;
        bool result;

        result = date.fromString(INVALID_DATE_24HOUR_BOUNDARY);
        UNSCOPED_INFO("INVALID_DATE_24HOUR_BOUNDARY should have failed");
        CHECK_FALSE(result);
        UNSCOPED_INFO("a failed parse leaves the date unchanged");
        CHECK(date.secondsSinceEpoch() == 0);

        result = date.fromString(INVALID_DATE_LEAP);
        UNSCOPED_INFO("INVALID_DATE_LEAP should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_HOUR);
        UNSCOPED_INFO("INVALID_DATE_HOUR should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MIN);
        UNSCOPED_INFO("INVALID_DATE_MIN should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_SEC);
        UNSCOPED_INFO("INVALID_DATE_SEC should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_YEAR);
        UNSCOPED_INFO("INVALID_DATE_YEAR should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_MONTH);
        UNSCOPED_INFO("INVALID_DATE_MONTH should have failed");
        CHECK_FALSE(result);

        result = date.fromString(INVALID_DATE_DAY);
        UNSCOPED_INFO("INVALID_DATE_DAY should have failed");
        CHECK_FALSE(result);
    }
#endif // LL_DATE_PARSER_CHECKS_BOUNDARY

    /* API checks */
    TEST_CASE("LLDate: fromStream reads a date", "[LLDate]")
    {
        LLDate date;
        std::istringstream stream(VALID_DATE);
        std::string  expected_string = VALID_DATE;
        date.fromStream(stream);
        CHECK(date.asString() == expected_string);
    }

    TEST_CASE("LLDate: the copy constructor copies the date", "[LLDate]")
    {
        LLDate date1(VALID_DATE);
        LLDate date2(date1);
        CHECK(date1.asString() == date2.asString());
    }

    TEST_CASE("LLDate: a date built from secondsSinceEpoch equals the original", "[LLDate]")
    {
        LLDate date1(VALID_DATE);
        LLDate date2(date1.secondsSinceEpoch());
        CHECK(date1.secondsSinceEpoch() == date2.secondsSinceEpoch());
        CHECK(date1.asString() == date2.asString());
    }

    TEST_CASE("LLDate: operator<< writes the date string", "[LLDate]")
    {
        LLDate date(VALID_DATE);
        std::ostringstream stream;
        stream << date;
        std::string expected_str = VALID_DATE;
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

        CHECK(date.asString() == expected_str);
        CHECK(stream.str() == out_stream.str());
    }

    // Comparison operators (now constexpr in the header) — runtime coverage.
    TEST_CASE("LLDate: comparison operators order dates by their seconds", "[LLDate]")
    {
        LLDate earlier(1000.0);
        LLDate later(2000.0);
        LLDate also_earlier(1000.0);

        CHECK(earlier < later);
        CHECK(later > earlier);
        CHECK(earlier <= later);
        CHECK(later >= earlier);
        CHECK(earlier <= also_earlier);
        CHECK(earlier >= also_earlier);
        CHECK(earlier == also_earlier);
        CHECK(earlier != later);
        CHECK_FALSE(earlier < also_earlier);
        CHECK_FALSE(earlier > also_earlier);
    }

    // isNull / notNull / setter round-trip.
    TEST_CASE("LLDate: isNull and notNull follow the seconds set", "[LLDate]")
    {
        LLDate d;
        CHECK(d.isNull());
        CHECK_FALSE(d.notNull());

        d.secondsSinceEpoch(1234.5);
        CHECK_FALSE(d.isNull());
        CHECK(d.notNull());
        CHECK(d.secondsSinceEpoch() == 1234.5);

        d.secondsSinceEpoch(0.0);
        CHECK(d.isNull());
    }

    // toLocalDateString / toHTTPDateString must not crash on the epoch value
    // (regression for the thread-safe localtime_utc/gmtime_utc rewrite).
    TEST_CASE("LLDate: the date string formatters give output for the epoch value", "[LLDate]")
    {
        LLDate d;
        CHECK(d.toLocalDateString(std::string("%Y-%m-%d")).size() > 0);
        CHECK(d.toHTTPDateString(std::string("%Y-%m-%d")).size() > 0);
        // RFC1123 wraps toHTTPDateString as "%A, %d %b %Y %H:%M:%S GMT".
        // The day-of-week token, whatever the locale names it, comes before
        // the first ", ": a strftime success indicator that doesn't pin locale.
        std::string rfc = d.asRFC1123();
        const size_t day_end = rfc.find(", ");
        CHECK(day_end != std::string::npos);
        CHECK(day_end > 0);
    }

    // A binary LLSD date is whatever eight bytes it was sent.
    TEST_CASE("LLDate: seconds no time can hold format as the epoch or not at all", "[LLDate]")
    {
        const F64 unholdable[] =
        {
            std::numeric_limits<F64>::quiet_NaN(),
            std::numeric_limits<F64>::infinity(),
            -std::numeric_limits<F64>::infinity(),
            1e300,
            -1e300,
        };
        for (F64 seconds : unholdable)
        {
            CAPTURE(seconds);
            LLDate date(seconds);
            CHECK(date.asString() == "1970-01-01T00:00:00Z");
            std::ostringstream stream;
            stream << date;
            CHECK(stream.str() == "1970-01-01T00:00:00Z");
            CHECK(date.toHTTPDateString(std::string("%Y-%m-%d")).empty());
            CHECK(date.toLocalDateString(std::string("%Y-%m-%d")).empty());
            S32 year = 0;
            CHECK_FALSE(date.split(&year, nullptr, nullptr, nullptr, nullptr, nullptr));
        }
    }

    TEST_CASE("LLDate: fields past nine digits fail, and offsets past an S32 parse without overflowing", "[LLDate]")
    {
        LLDate date;
        // past an S32, and within one but carried by timegm() into another date
        CHECK_FALSE(date.fromString("99999999999-01-01T00:00:00Z"));
        CHECK_FALSE(date.fromString("-99999999999-01-01T00:00:00Z"));
        CHECK_FALSE(date.fromString("2000-01-01T00:00:1000000000Z"));

        // an offset of a hundred million hours is worked out in full
        REQUIRE(date.fromString("2000-01-01T00:00:00+99999999"));
        CHECK(date.secondsSinceEpoch() == 946684800.0 - 99999999.0 * 3600.0);
        REQUIRE(date.fromString("2000-01-01T00:00:00-99999999:30"));
        CHECK(date.secondsSinceEpoch() == 946684800.0 + 99999999.0 * 3600.0 + 30.0 * 60.0);
    }

    TEST_CASE("LLDate: a format too long for the buffer gives an empty string", "[LLDate]")
    {
        // strftime() leaves its buffer indeterminate when the result does not fit
        LLDate d;
        CHECK(d.toHTTPDateString(std::string(200, 'x')).empty());
    }
}
