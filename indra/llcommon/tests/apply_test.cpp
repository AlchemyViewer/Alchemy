/**
 * @file   apply_test.cpp
 * @author Nat Goodspeed
 * @date   2022-12-19
 * @brief  Test for apply.
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Copyright (c) 2022, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "apply.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "llsd.h"
#include "llsdutil.h"
#include <array>
#include <string>
#include <vector>

// to print a std::vector<std::string> in a failed check
#include "StringVec.h"
#include <catch2/catch_test_macros.hpp>

/*****************************************************************************
*   Tests
*****************************************************************************/
namespace
{
    namespace statics
    {
        /*------------------------------ data ------------------------------*/
        // Although we're using types from the LLSD namespace, we're not
        // constructing LLSD values, but rather instances of the C++ types
        // supported by LLSD.
        static LLSD::Boolean b{true};
        static LLSD::Integer i{17};
        static LLSD::Real    f{3.14};
        static LLSD::String  s{ "hello" };
        static LLSD::UUID    uu{ "baadf00d-dead-beef-baad-feedb0ef" };
        static LLSD::Date    dt{ "2022-12-19" };
        static LLSD::URI     uri{ "http://secondlife.com" };
        static LLSD::Binary  bin{ 0x01, 0x02, 0x03, 0x04, 0x05 };

        static std::vector<LLSD::String> quick
        {
            "The", "quick", "brown", "fox", "etc."
        };

        static std::array<int, 5> fibs
        {
            0, 1, 1, 2, 3
        };

        // ensure that apply() actually reaches the target method --
        // lack of ensure_equals() failure could be due to no-op apply()
        static bool called{ false };
        // capture calls from collect()
        static std::vector<std::string> collected;

        /*------------------------- test functions -------------------------*/
        static void various(LLSD::Boolean b, LLSD::Integer i, LLSD::Real f, const LLSD::String& s,
                     const LLSD::UUID& uu, const LLSD::Date& dt,
                     const LLSD::URI& uri, const LLSD::Binary& bin)
        {
            called = true;
            REQUIRE(b == statics::b);
            REQUIRE(i == statics::i);
            REQUIRE(f == statics::f);
            REQUIRE(s == statics::s);
            REQUIRE(uu == statics::uu);
            REQUIRE(dt == statics::dt);
            REQUIRE(uri.asString() == statics::uri.asString());
            REQUIRE(bin == statics::bin);
        }

        static void strings(std::string s0, std::string s1, std::string s2, std::string s3, std::string s4)
        {
            called = true;
            REQUIRE(s0 == statics::quick[0]);
            REQUIRE(s1 == statics::quick[1]);
            REQUIRE(s2 == statics::quick[2]);
            REQUIRE(s3 == statics::quick[3]);
            REQUIRE(s4 == statics::quick[4]);
        }

        static void ints(int i0, int i1, int i2, int i3, int i4)
        {
            called = true;
            REQUIRE(i0 == statics::fibs[0]);
            REQUIRE(i1 == statics::fibs[1]);
            REQUIRE(i2 == statics::fibs[2]);
            REQUIRE(i3 == statics::fibs[3]);
            REQUIRE(i4 == statics::fibs[4]);
        }

        static void sdfunc(const LLSD& sd)
        {
            called = true;
            REQUIRE(sd.asInteger() == statics::i);
        }

        static void intfunc(int i)
        {
            called = true;
            REQUIRE(i == statics::i);
        }

        static void voidfunc()
        {
            called = true;
        }

        // recursion tail
        static void collect()
        {
            called = true;
        }

        // collect(arbitrary)
        template <typename... ARGS>
        static void collect(const std::string& first, ARGS&&... rest)
        {
            statics::collected.push_back(first);
            collect(std::forward<ARGS>(rest)...);
        }
    } // namespace statics

    struct apply_data
    {
        apply_data()
        {
            // reset called before each test
            statics::called = false;
            statics::collected.clear();
        }
    };

    TEST_CASE_METHOD(apply_data, "apply: apply(tuple) passes each element as an argument", "[apply]")
    {
        LL::apply(statics::various,
                  std::make_tuple(statics::b, statics::i, statics::f, statics::s,
                                  statics::uu, statics::dt, statics::uri, statics::bin));
        CHECK(statics::called);
    }

    TEST_CASE_METHOD(apply_data, "apply: apply(array) passes each element as an argument", "[apply]")
    {
        LL::apply(statics::ints, statics::fibs);
        CHECK(statics::called);
    }

    TEST_CASE_METHOD(apply_data, "apply: apply(vector) passes each element as an argument", "[apply]")
    {
        LL::apply(statics::strings, statics::quick);
        CHECK(statics::called);
    }

    // The various apply(LLSD) tests exercise only the success cases because
    // the failure cases trigger assert() fail, which is hard to catch.
    TEST_CASE_METHOD(apply_data, "apply: apply(LLSD()) calls a function of no arguments", "[apply]")
    {
        LL::apply(statics::voidfunc, LLSD());
        CHECK(statics::called);
    }

    TEST_CASE_METHOD(apply_data, "apply: apply with an LLSD scalar converts it for an int parameter", "[apply]")
    {
        LL::apply(statics::intfunc, LLSD(statics::i));
        CHECK(statics::called);
    }

    TEST_CASE_METHOD(apply_data, "apply: apply with an LLSD scalar passes it to an LLSD parameter", "[apply]")
    {
        // This test verifies that LLSDParam<LLSD> doesn't send the compiler
        // into infinite recursion when the target is itself LLSD.
        LL::apply(statics::sdfunc, LLSD(statics::i));
        CHECK(statics::called);
    }

    TEST_CASE_METHOD(apply_data, "apply: apply(LLSD array) converts each element to its parameter", "[apply]")
    {
        LL::apply(statics::various,
                  llsd::array(statics::b, statics::i, statics::f, statics::s,
                              statics::uu, statics::dt, statics::uri, statics::bin));
        CHECK(statics::called);
    }

    TEST_CASE_METHOD(apply_data, "apply: VAPPLY() expands an array into a variadic call", "[apply]")
    {
        // Make a std::array<std::string> from statics::quick. We can't call a
        // variadic function with a data structure of dynamic length.
        std::array<std::string, 5> strray;
        for (size_t i = 0; i < strray.size(); ++i)
            strray[i] = statics::quick[i];
        // This doesn't work: the compiler doesn't know which overload of
        // collect() to pass to LL::apply().
        // LL::apply(statics::collect, strray);
        // That's what VAPPLY() is for.
        VAPPLY(statics::collect, strray);
        CHECK(statics::called);
        CHECK(statics::collected == statics::quick);
    }
} // anonymous namespace
