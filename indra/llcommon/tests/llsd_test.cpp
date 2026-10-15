/**
 * @file llsd_test.cpp
 * @date   February 2006
 * @brief LLSD unit tests
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2006-2011, Linden Research, Inc.
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

#define LLSD_DEBUG_INFO
#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>
#include "llsd.h"

#include "llsdtraits.h"
#include "llstring.h"

using std::fpclassify;

namespace
{
    namespace
    {
        // LLSD's live count is back where it started once what the body made
        // is gone. Checked here rather than in a destructor, which is
        // noexcept: a failed REQUIRE there would end the process instead of
        // failing the test. A release for download keeps no counts
        // (llsd::COUNTS_IMPLS), and there the body runs unchecked.
        template <typename BODY>
        void require_cleaned_up(BODY&& body)
        {
            [[maybe_unused]] const U32 outstanding = llsd::outstandingCount();
            body();
            if constexpr (llsd::COUNTS_IMPLS)
            {
                REQUIRE(llsd::outstandingCount() == outstanding);
            }
        }

        // ...and the body allocated exactly as many LLSD values as expected.
        template <typename BODY>
        void require_allocations([[maybe_unused]] const std::string& what, [[maybe_unused]] U32 expectedAllocations, BODY&& body)
        {
            [[maybe_unused]] const U32 outstanding = llsd::outstandingCount();
            [[maybe_unused]] const U32 allocations = llsd::allocationCount();
            body();
            if constexpr (llsd::COUNTS_IMPLS)
            {
                INFO(what);
                REQUIRE(llsd::allocationCount() - allocations ==
                    expectedAllocations);
                REQUIRE(llsd::outstandingCount() == outstanding);
            }
        }
    } // anonymous namespace

    // Free functions rather than members of a fixture: the tests call them
    // inside the bodies they hand the checks above, and MSVC does not find
    // a member of the fixture by its bare name in a lambda.
    template<class T>
    static void require_type_and_value(const char* what, const LLSD& actual,
        T expectedValue)
    {
        LLSDTraits<T> traits;

        INFO(what);
        REQUIRE(traits.checkType(actual));
        REQUIRE(traits.get(actual) == expectedValue);
    }

    template<class T>
    static void require_type_and_ref_value(const char* what, const LLSD& actual,
        const T& expectedValue)
    {
        LLSDTraits<const T&> traits;

        INFO(what);
        REQUIRE(traits.checkType(actual));
        REQUIRE(traits.get(actual) == expectedValue);
    }

    // template<> template<>
    // void SDTestObject::test<1>()
    //  // construction and test of undefined
    // {
    //  SDCleanupCheck check;

    //  LLSD u;
    //  ensure("is undefined", u.isUndefined());
    // }

    TEST_CASE("LLSD(new): setting and fetching scalar types", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            LLSD v;

            v = true;       require_type_and_value("set true", v, true);
            v = false;      require_type_and_value("set false", v, false);
            v = true;       require_type_and_value("set true again", v, true);

            v = 42;         require_type_and_value("set to 42", v, 42);
            v = 0;          require_type_and_value("set to zero", v, 0);
            v = -12345;     require_type_and_value("set to neg", v, -12345);
            v = 2000000000; require_type_and_value("set to big", v, 2000000000);

            v = 3.14159265359;
                            require_type_and_value("set to pi", v, 3.14159265359);
                            REQUIRE(v.asReal() !=
                                (float)3.14159265359);
            v = 6.7e256;    require_type_and_value("set to big", v, 6.7e256);

            LLUUID nullUUID;
            LLUUID newUUID;
            newUUID.generate();

            v = nullUUID;   require_type_and_value("set to null UUID", v, nullUUID);
            v = newUUID;    require_type_and_value("set to new UUID", v, newUUID);
            v = nullUUID;   require_type_and_value("set to null again", v, nullUUID);

            // strings must be tested with two types of string objects
            std::string s = "now is the time";
            const char* cs = "for all good zorks";

            v = s;          require_type_and_value("set to std::string", v, s);
            v = cs;         require_type_and_value("set to const char*", v, cs);

            LLDate epoch;
            LLDate aDay("2001-10-22T10:11:12.00Z");

            v = epoch;      require_type_and_value("set to epoch", v, epoch);
            v = aDay;       require_type_and_value("set to a day", v, aDay);

            LLURI path("http://slurl.com/secondlife/Ambleside/57/104/26/");

            v = path;       require_type_and_value("set to a uri", v, path);

            const char source[] = "once in a blue moon";
            std::vector<U8> data;
            copy(&source[0], &source[sizeof(source)], back_inserter(data));

            v = data;       require_type_and_ref_value("set to data", v, data);

            v.clear();
            REQUIRE(v.type() == LLSD::TypeUndefined);
        });
    }

    TEST_CASE("LLSD(new): construction and initialization from scalar values", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            LLSD b1(true);  require_type_and_value("construct boolean", b1, true);
            LLSD b2 = true; require_type_and_value("initialize  boolean", b2, true);
            LLSD i1(42);    require_type_and_value("construct int", i1, 42);
            LLSD i2 =42;    require_type_and_value("initialize  int", i2, 42);
            LLSD d1(1.2);   require_type_and_value("construct double", d1, 1.2);
            LLSD d2 = 1.2;  require_type_and_value("initialize double", d2, 1.2);

            LLUUID newUUID;
            newUUID.generate();
            LLSD u1(newUUID);
                            require_type_and_value("construct UUID", u1, newUUID);
            LLSD u2 = newUUID;
                            require_type_and_value("initialize UUID", u2, newUUID);

            LLSD ss1(std::string("abc"));
                            require_type_and_value("construct std::string", ss1, "abc");
            LLSD ss2 = std::string("abc");
                            require_type_and_value("initialize std::string",ss2, "abc");
            LLSD sl1(std::string("def"));
                            require_type_and_value("construct std::string", sl1, "def");
            LLSD sl2 = std::string("def");
                            require_type_and_value("initialize std::string", sl2, "def");
            LLSD sc1("ghi");
                            require_type_and_value("construct const char*", sc1, "ghi");
            LLSD sc2 = "ghi";
                            require_type_and_value("initialize const char*",sc2, "ghi");

            LLDate aDay("2001-10-22T10:11:12.00Z");
            LLSD t1(aDay);  require_type_and_value("construct LLDate", t1, aDay);
            LLSD t2 = aDay; require_type_and_value("initialize LLDate", t2, aDay);

            LLURI path("http://slurl.com/secondlife/Ambleside/57/104/26/");
            LLSD p1(path);  require_type_and_value("construct LLURI", p1, path);
            LLSD p2 = path; require_type_and_value("initialize LLURI", p2, path);

            const char source[] = "once in a blue moon";
            std::vector<U8> data;
            copy(&source[0], &source[sizeof(source)], back_inserter(data));
            LLSD x1(data);  require_type_and_ref_value("construct vector<U8>", x1, data);
            LLSD x2 = data; require_type_and_ref_value("initialize vector<U8>", x2, data);
        });
    }

    static void require_conversions(const char* what, const LLSD& v,
        LLSD::Boolean eBoolean, LLSD::Integer eInteger,
        LLSD::Real eReal, const LLSD::String& eString)
    {
        INFO(what);
        REQUIRE(v.asBoolean() == eBoolean);
        REQUIRE(v.asInteger() == eInteger);
        if (eReal == eReal)
        {
            REQUIRE(v.asReal() == eReal);
            REQUIRE(v.asString() == eString);
        }
        else
        {
            int left  = fpclassify(v.asReal());
            int right = fpclassify(eReal);

            REQUIRE(left == right);
            // ensure_equals(s+" to string", v.asString(), eString);
            // I've commented this check out, since there doesn't
            // seem to be uniform string representation for NaN on
            // all platforms. For example, on my Ubuntu 8.10 laptop
            // with libc 2.11.1, sqrt(-1.0) will return '-nan', not
            // 'nan'.
        }
    }

    TEST_CASE("LLSD(new): conversion of undefined and scalars to the basic scalar types", "[LLSD_new]")
        // conversion between undefined and basic scalar types:
        //  boolean, integer, real and string
    {
        require_cleaned_up([&]
        {
            LLSD v;         require_conversions("untitled", v, false, 0, 0.0, "");

            v = false;      require_conversions("false", v, false, 0, 0.0, "");
            v = true;       require_conversions("true", v, true, 1, 1.0, "true");

            v = 0;          require_conversions("zero", v, false, 0, 0.0, "0");
            v = 1;          require_conversions("one", v, true, 1, 1.0, "1");
            v = -33;        require_conversions("neg33", v, true, -33, -33.0, "-33");

            v = 0.0;        require_conversions("0.0", v, false, 0, 0.0, "0");
            v = 0.5;        require_conversions("point5", v, true, 0, 0.5, "0.5");
            v = 0.9;        require_conversions("point9", v, true, 0, 0.9, "0.9");
            v = -3.9;       require_conversions("neg3dot9", v, true, -3, -3.9, "-3.9");
            // Get rid of NaN test. First, some libraries don't reliably return
            // NaN for sqrt(-1.0) -- meaning that I don't even know how to
            // portably, reliably produce a NaN value. Second, we observe failures
            // on different platforms in the asString() test. But LLSD's
            // ImplReal::asString() does not itself recognize NaN! It merely
            // passes the value through to llformat(), which passes it through to
            // the library vsnprintf(). That is, even when we do produce NaN,
            // we're not testing any LLSD code: we're testing the local library's
            // vsnprintf() function, which (empirically) produces idiosyncratic
            // results. This is just not a good test case.
    //      v = sqrt(-1.0); require_conversions("NaN", v, false, 0, sqrt(-1.0), "nan");

            v = "";         require_conversions("empty", v, false, 0, 0.0, "");
            v = "0";        require_conversions("digit0", v, true, 0, 0.0, "0");
            v = "10";       require_conversions("digit10", v, true, 10, 10.0, "10");
            v = "-2.345";   require_conversions("decdigits", v,
                                true, -2, -2.345, "-2.345");
            v = "apple";    require_conversions("apple", v, true, 0, 0.0, "apple");
            v = "33bob";    require_conversions("digialpha", v, true, 0, 0.0, "33bob");
            v = " ";        require_conversions("space", v, true, 0, 0.0, " ");
            v = "\n";       require_conversions("newline", v, true, 0, 0.0, "\n");
        });
    }

    template<class T>
    static void require_round_trip(const std::string& what, const LLSD& actual,
        const char* sExpected, T vExpected)
    {
        INFO(what);
        std::string str = actual.asString();

        if (sExpected) {
            REQUIRE(str == sExpected);
        }

        LLSD u(str);
        LLSDTraits<T> traits;

        REQUIRE(traits.get(u) == vExpected);
    }


    TEST_CASE("LLSD(new): UUID and Date and URI convert to and from String", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            LLSD v;

            LLUUID nullUUID;
            LLUUID someUUID;
            someUUID.generate();

            v = nullUUID;   require_round_trip("null uuid", v,
                                "00000000-0000-0000-0000-000000000000", nullUUID);
            v = someUUID;   require_round_trip("random uuid", v, 0, someUUID);

            LLDate epoch;
            LLDate beta("2003-04-30T04:00:00Z");
            LLDate oneOh("2003-06-23T04:00:00Z");

            v = epoch;      require_round_trip("epoch date", v, 0, epoch);
            v = beta;       require_round_trip("beta date", v,
                                "2003-04-30T04:00:00Z", beta);
            v = oneOh;      require_round_trip("1.0 date", v,
                                "2003-06-23T04:00:00Z", oneOh);

            LLURI empty;
            LLURI path("http://slurl.com/secondlife/Ambleside/57/104/26/");
            LLURI mail("mailto:zero.linden@secondlife.com");

            v = empty;      require_round_trip("empty URI", v, 0, empty);
            v = path;       require_round_trip("path URI", v,
                                "http://slurl.com/secondlife/Ambleside/57/104/26/",
                                path);
            v = mail;       require_round_trip("mail URI", v,
                                "mailto:zero.linden@secondlife.com", mail);
        });
    }

    TEST_CASE("LLSD(new): copies made by construction or assignment change independently", "[LLSD_new]")
        // copy construction and assignment
        // checking for shared values after constr. or assignment
        // checking in both the same type and change of type case
    {
        require_cleaned_up([&]
        {
            {
                LLSD v = 42;

                LLSD w0(v);
                require_type_and_value("int constr.", w0, 42);

                LLSD w1(v);
                w1 = 13;
                require_type_and_value("int constr. change case 1", w1, 13);
                require_type_and_value("int constr. change case 2", v, 42);

                LLSD w2(v);
                v = 7;
                require_type_and_value("int constr. change case 3", w2, 42);
                require_type_and_value("int constr. change case 4", v, 7);
            }

            {
                LLSD v = 42;

                LLSD w1(v);
                w1 = "bob";
                require_type_and_value("string constr. change case 1", w1, "bob");
                require_type_and_value("string constr. change case 2", v, 42);

                LLSD w2(v);
                v = "amy";
                require_type_and_value("string constr. change case 3", w2, 42);
                require_type_and_value("string constr. change case 4", v, "amy");
            }

            {
                LLSD v = 42;

                LLSD w0;
                w0 = v;
                require_type_and_value("int assign", w0, 42);

                LLSD w1;
                w1 = v;
                w1 = 13;
                require_type_and_value("int assign change case 1", w1, 13);
                require_type_and_value("int assign change case 2", v, 42);

                LLSD w2;
                w2 = v;
                v = 7;
                require_type_and_value("int assign change case 3", w2, 42);
                require_type_and_value("int assign change case 4", v, 7);
            }

            {
                LLSD v = 42;

                LLSD w1;
                w1 = v;
                w1 = "bob";
                require_type_and_value("string assign change case 1", w1, "bob");
                require_type_and_value("string assign change case 2", v, 42);

                LLSD w2;
                w2 = v;
                v = "amy";
                require_type_and_value("string assign change case 3", w2, 42);
                require_type_and_value("string assign change case 4", v, "amy");
            }
        });
    }


    TEST_CASE("LLSD(new): assigning and casting LLSD to scalar types converts", "[LLSD_new]")
        // Test assignment and casting to various scalar types.  These
        // assignments should invoke the right conversion without it being
        // mentioned explicitly.  The few exceptions are marked SAD.
    {
        require_cleaned_up([&]
        {
            LLSD v("  42.375");

            bool b = false;
            b = v;              UNSCOPED_INFO("assign to bool"); REQUIRE(b);
            b = (bool)v;        UNSCOPED_INFO("cast to bool"); REQUIRE(b);

            int i = 99;
            i = v;              UNSCOPED_INFO("assign to int"); REQUIRE(i == 42);
            i = (int)v;         UNSCOPED_INFO("cast to int"); REQUIRE(i == 42);

            double d = 3.14159;
            d = v;              UNSCOPED_INFO("assign to double"); REQUIRE(d == 42.375);
            d = (double)v;      UNSCOPED_INFO("cast to double"); REQUIRE(d == 42.375);

            std::string s = "yo";
    // SAD  s = v;              ensure_equals("assign to string", s, "  42.375");
            s = (std::string)v; UNSCOPED_INFO("cast to string"); REQUIRE(s == "  42.375");

            std::string uuidStr = "b1e50c2b-b627-4d23-8a86-a65d97b6319b";
            v = uuidStr;
            LLUUID u;
            u = v;
                        UNSCOPED_INFO("assign to LLUUID");
                        REQUIRE(u == LLUUID(uuidStr));
    // SAD  u = (LLUUID)v;
    //                  ensure_equals("cast to LLUUID", u, LLUUID(uuidStr));

            std::string dateStr = "2005-10-24T15:00:00Z";
            v = dateStr;
            LLDate date;
            date = v;
                        UNSCOPED_INFO("assign to LLDate");
                        REQUIRE(date.asString() == dateStr);
    // SAD  date = (LLDate)v;
    //                  ensure_equals("cast to LLDate", date.asString(), dateStr);

            std::string uriStr = "http://secondlife.com";
            v = uriStr;
            LLURI uri;
            uri = v;
                        UNSCOPED_INFO("assign to LLURI");
                        REQUIRE(uri.asString() == uriStr);
    // SAD  uri = (LLURI)v;
    //                  ensure_equals("cast to LLURI", uri.asString(), uriStr);
        });
    }

    TEST_CASE("LLSD(new): constructing scalar types from LLSD converts", "[LLSD_new]")
        // Test construction of various scalar types from LLSD.
        // Test both construction and initialization forms.
        // These should invoke the right conversion without it being
        // mentioned explicitly.  The few exceptions are marked SAD.
    {
        require_cleaned_up([&]
        {
            LLSD v("  42.375");

            bool b1(v);     UNSCOPED_INFO("construct bool"); REQUIRE(b1);
            bool b2 = v;    UNSCOPED_INFO("initialize bool"); REQUIRE(b2);

            int i1(v);      UNSCOPED_INFO("construct int"); REQUIRE(i1 == 42);
            int i2 = v;     UNSCOPED_INFO("initialize int"); REQUIRE(i2 == 42);

            double d1(v);   UNSCOPED_INFO("construct double"); REQUIRE(d1 == 42.375);
            double d2 = v;  UNSCOPED_INFO("initialize double"); REQUIRE(d2 == 42.375);

            std::string s1(v);
            std::string s2 = v;
                            UNSCOPED_INFO("construct string");
                            REQUIRE(s1 == "  42.375");
                            UNSCOPED_INFO("initialize string");
                            REQUIRE(s2 == "  42.375");

            std::string t1(v);
            std::string t2 = v.asString();      // SAD
                            UNSCOPED_INFO("construct std::string");
                            REQUIRE(t1 == "  42.375");
                            UNSCOPED_INFO("initialize std::string");
                            REQUIRE(t2 == "  42.375");

            std::string uuidStr = "b1e50c2b-b627-4d23-8a86-a65d97b6319b";
            v = uuidStr;
            LLUUID uuid1(v.asUUID());       // SAD
            LLUUID uuid2 = v;
                    UNSCOPED_INFO("construct LLUUID");
                    REQUIRE(uuid1 == LLUUID(uuidStr));
                    UNSCOPED_INFO("initialize LLUUID");
                    REQUIRE(uuid2 == LLUUID(uuidStr));

            std::string dateStr = "2005-10-24T15:00:00Z";
            v = dateStr;
            LLDate date1(v.asDate());       // SAD
            LLDate date2 = v;
                    UNSCOPED_INFO("construct LLDate");
                    REQUIRE(date1.asString() == dateStr);
                    UNSCOPED_INFO("initialize LLDate");
                    REQUIRE(date2.asString() == dateStr);

            std::string uriStr = "http://secondlife.com";
            v = uriStr;
            LLURI uri1(v.asURI());          // SAD
            LLURI uri2 = v;
                    UNSCOPED_INFO("construct LLURI");
                    REQUIRE(uri1.asString() == uriStr);
                    UNSCOPED_INFO("initialize LLURI");
                    REQUIRE(uri2.asString() == uriStr);
        });
    }


    TEST_CASE("LLSD(new): the string 0 is true as a bool but 0 as an int", "[LLSD_new]")
        // test to make sure v is interpreted as a bool in a various
        // scenarios.
    {
        require_cleaned_up([&]
        {
            LLSD v = "0";
            // magic value that is interpreted as boolean true, but integer false!

            UNSCOPED_INFO("trinary operator bool");
            REQUIRE((v ? true : false) == true);
            UNSCOPED_INFO("convert to int, then bool");
            REQUIRE(((int)v ? true : false) == false);

            bool took_if = false;
            if(v)
            {
                took_if = true;
            }
            UNSCOPED_INFO("bool did not convert to a bool in if statement.");
            REQUIRE(took_if);

            if(!v)
            {
                FAIL("bool did not convert to a bool in negated if statement.");
            }
        });
    }

    TEST_CASE("LLSD(new): map has/get/insert/erase and copy", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            LLSD v;
            UNSCOPED_INFO("undefined has no members");
            REQUIRE_FALSE(v.has("amy"));
            UNSCOPED_INFO("undefined get() is undefined");
            REQUIRE(v.get("bob").isUndefined());

            v = LLSD::emptyMap();
            REQUIRE(v.isMap());
            UNSCOPED_INFO("empty map has no members");
            REQUIRE_FALSE(v.has("cam"));
            UNSCOPED_INFO("empty map get() is undefined");
            REQUIRE(v.get("don").isUndefined());

            v.clear();
            v.insert("eli", 43);
            UNSCOPED_INFO("insert converts to map");
            REQUIRE(v.isMap());
            REQUIRE(v.has("eli"));
            require_type_and_value("inserted value", v.get("eli"), 43);

            v.insert("fra", false);
            UNSCOPED_INFO("first key still present");
            REQUIRE(v.has("eli"));
            REQUIRE(v.has("fra"));
            require_type_and_value("first value", v.get("eli"), 43);
            require_type_and_value("second value", v.get("fra"), false);

            v.erase("eli");
            REQUIRE_FALSE(v.has("eli"));
            UNSCOPED_INFO("second key still present");
            REQUIRE(v.has("fra"));
            REQUIRE(v.get("eli").isUndefined());
            require_type_and_value("second value still there", v.get("fra"), false);

            v.erase("fra");
            REQUIRE_FALSE(v.has("fra"));
            REQUIRE(v.get("fra").isUndefined());

            v["gil"] = (std::string)"good morning";
            REQUIRE(v.has("gil"));
            require_type_and_value("third key value", v.get("gil"), "good morning");

            const LLSD& cv = v; // FIX ME IF POSSIBLE
            REQUIRE(cv["ham"].isUndefined());
            UNSCOPED_INFO("const operator[] does not insert");
            REQUIRE_FALSE(v.has("ham"));

            LLSD w = 43;
            const LLSD& cw = w; // FIX ME IF POSSIBLE
            int i = cw["ian"];
            require_type_and_value("other missing value", i, 0);
            UNSCOPED_INFO("const operator[] does not insert");
            REQUIRE_FALSE(w.has("ian"));
            UNSCOPED_INFO("const operator[] does not convert");
            REQUIRE(w.isInteger());

            LLSD x;
            x = v;
            REQUIRE(x.isMap());
            require_type_and_value("copy map value gil", x.get("gil"), "good morning");
        });
    }


    TEST_CASE("LLSD(new): array append/insert/set/erase and copy", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            LLSD v;
            UNSCOPED_INFO("undefined has no size");
            REQUIRE(v.size() == 0);
            UNSCOPED_INFO("undefined get() is undefined");
            REQUIRE(v.get(0).isUndefined());

            v = LLSD::emptyArray();
            REQUIRE(v.isArray());
            UNSCOPED_INFO("empty array has no size");
            REQUIRE(v.size() == 0);
            UNSCOPED_INFO("empty array get() is undefined");
            REQUIRE(v.get(0).isUndefined());

            v.clear();
            v.append(88);
            v.append("noodle");
            v.append(true);
            REQUIRE(v.size() == 3);
            REQUIRE(v.isArray());
            require_type_and_value("append 0", v[0], 88);
            require_type_and_value("append 1", v[1], "noodle");
            require_type_and_value("append 2", v[2], true);

            v.insert(0, 77);
            v.insert(2, "soba");
            v.insert(4, false);
            REQUIRE(v.size() == 6);
            require_type_and_value("post insert 0", v[0], 77);
            require_type_and_value("post insert 1", v[1], 88);
            require_type_and_value("post insert 2", v[2], "soba");
            require_type_and_value("post insert 3", v[3], "noodle");
            require_type_and_value("post insert 4", v[4], false);
            require_type_and_value("post insert 5", v[5], true);

            LLSD beyond;
            beyond.insert(2, "far");
            UNSCOPED_INFO("beyond-end insert size");
            REQUIRE(beyond.size() == 3);
            UNSCOPED_INFO("beyond-end insert pad 0");
            REQUIRE(beyond[0].isUndefined());
            UNSCOPED_INFO("beyond-end insert pad 1");
            REQUIRE(beyond[1].isUndefined());
            require_type_and_value("beyond-end insert value", beyond[2], "far");

            require_type_and_value("get 1", v.get(1), 88);
            v.set(1, "hot");
            require_type_and_value("set 1", v.get(1), "hot");

            v.erase(3);
            REQUIRE(v.size() == 5);
            require_type_and_value("post erase 0", v[0], 77);
            require_type_and_value("post erase 1", v[1], "hot");
            require_type_and_value("post erase 2", v[2], "soba");
            require_type_and_value("post erase 3", v[3], false);
            require_type_and_value("post erase 4", v[4], true);

            v.append(34);
            REQUIRE(v.size() == 6);
            require_type_and_value("post append 5", v[5], 34);

            LLSD w;
            w = v;
            REQUIRE(w.isArray());
            REQUIRE(w.size() == 6);
            require_type_and_value("copy array 0", w[0], 77);
            require_type_and_value("copy array 1", w[1], "hot");
            require_type_and_value("copy array 2", w[2], "soba");
            require_type_and_value("copy array 3", w[3], false);
            require_type_and_value("copy array 4", w[4], true);
            require_type_and_value("copy array 5", w[5], 34);
        });
    }


    TEST_CASE("LLSD(new): a copy shares no values with its original", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            LLSD a = 99;
            LLSD b = a;
            a = 34;
            require_type_and_value("top level original changed",    a, 34);
            require_type_and_value("top level copy unaltered",      b, 99);
            b = a;
            b = 66;
            require_type_and_value("top level original unaltered",  a, 34);
            require_type_and_value("top level copy changed",        b, 66);

            a[0] = "uno";
            a[1] = 99;
            a[2] = 1.414;
            b = a;
            a[1] = 34;
            require_type_and_value("array member original changed", a[1], 34);
            require_type_and_value("array member copy unaltered",   b[1], 99);
            b = a;
            b[1] = 66;
            require_type_and_value("array member original unaltered", a[1], 34);
            require_type_and_value("array member copy changed",     b[1], 66);

            a["alpha"] = "uno";
            a["beta"] = 99;
            a["gamma"] = 1.414;
            b = a;
            a["beta"] = 34;
            require_type_and_value("map member original changed",   a["beta"], 34);
            require_type_and_value("map member copy unaltered",     b["beta"], 99);
            b = a;
            b["beta"] = 66;
            require_type_and_value("map member original unaltered", a["beta"], 34);
            require_type_and_value("map member copy changed",       b["beta"], 66);
        });
    }

    TEST_CASE("LLSD(new): copies share their implementation and scalars never allocate", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            require_allocations("copy construct undefined", 0, []
            {
                LLSD v;
                LLSD w = v;
            });

            require_allocations("assign undefined", 0, []
            {
                LLSD v;
                LLSD w;
                w = v;
            });

            // scalar values are stored inline and never allocate
            require_allocations("assign integer value", 0, []
            {
                LLSD v = 45;
                v = 33;
                v = 0;
            });

            require_allocations("copy construct integer", 0, []
            {
                LLSD v = 45;
                LLSD w = v;
            });

            require_allocations("assign integer", 0, []
            {
                LLSD v = 45;
                LLSD w;
                w = v;
            });

            require_allocations("avoids extra clone", 1, []
            {
                LLSD v = 45;
                LLSD w = v;
                w = "nice day";
            });

            // map + array + two strings; integers, reals and undefs are inline
            require_allocations("shared values test for threaded work", 4, []
            {
                //U32 start_llsd_count = LLSD::outstandingCount();

                LLSD m = LLSD::emptyMap();

                m["one"] = 1;
                m["two"] = 2;
                m["one_copy"] = m["one"];           // 1 (m; the integers are inline)

                m["undef_one"] = LLSD();
                m["undef_two"] = LLSD();
                m["undef_one_copy"] = m["undef_one"];

                {   // Ensure first_array gets freed to avoid counting it
                    LLSD first_array = LLSD::emptyArray();
                    first_array.append(1.0f);
                    first_array.append(2.0f);
                    first_array.append(3.0f);           // 2 (the reals are inline)

                    m["array"] = first_array;
                    m["array_clone"] = first_array;
                    m["array_copy"] = m["array"];       // 2 (shared, no clones)
                }

                m["string_one"] = "string one value";
                m["string_two"] = "string two value";
                m["string_one_copy"] = m["string_one"];     // 4

                //U32 llsd_object_count = LLSD::outstandingCount();
                //std::cout << "Using " << (llsd_object_count - start_llsd_count) << " LLSD objects" << std::endl;

                //m.dumpStats();
            });
        });
    }

    TEST_CASE("LLSD(new): assigning a null char pointer makes a string without crashing", "[LLSD_new]")
    {
        LLSD v;
        v = (const char*)NULL;
        CHECK(v.isString());
    }

    TEST_CASE("LLSD(new): asXMLRPCValue serializes each type", "[LLSD_new]")
    {
        require_cleaned_up([&]
        {
            REQUIRE(LLSD().asXMLRPCValue() ==
                "<value><nil/></value>");
            REQUIRE(LLSD(true).asXMLRPCValue() ==
                "<value><boolean>1</boolean></value>");
            REQUIRE(LLSD(3).asXMLRPCValue() ==
                "<value><int>3</int></value>");
            REQUIRE(LLSD(1.5).asXMLRPCValue() ==
                "<value><double>1.5</double></value>");
            REQUIRE(LLSD("a<b").asXMLRPCValue() ==
                "<value><string>a&lt;b</string></value>");

            LLSD array = LLSD::emptyArray();
            array.append(1);
            array.append("two");
            REQUIRE(array.asXMLRPCValue() ==
                "<value><array><data>"
                "<value><int>1</int></value>"
                "<value><string>two</string></value>"
                "</data></array></value>");

            LLSD map = LLSD::emptyMap();
            map["a"] = 1;
            REQUIRE(map.asXMLRPCValue() ==
                "<value><struct>"
                "<member><name>a</name><value><int>1</int></value></member>"
                "</struct></value>");
        });
    }

    /* TO DO:
        conversion of undefined to UUID, Date, URI and Binary
        conversion of undefined to map and array
        test map operations
        test array operations
        test array extension

        test copying and assign maps and arrays (clone)
        test iteration over map
        test iteration over array
        test iteration over scalar

        test empty map and empty array are indeed shared
        test serializations
    */
}

