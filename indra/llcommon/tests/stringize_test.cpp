/**
 * @file   stringize_test.cpp
 * @author Nat Goodspeed
 * @date   2008-09-12
 * @brief  Test of stringize.h
 *
 * $LicenseInfo:firstyear=2008&license=viewerlgpl$
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

/*==========================================================================*|
#if LL_WINDOWS
#pragma warning (disable : 4675) // "resolved by ADL" -- just as I want!
#endif
|*==========================================================================*/

// STL headers
#include <iomanip>

// Precompiled header
#include "linden_common.h"

// associated header
#include "../stringize.h"

// std headers
// external library headers
// other Linden headers
#include "../llsd.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct stringize_data
    {
        stringize_data():
            c('c'),
            s(17),
            i(34),
            l(68),
            f(3.14159265358979f),
            d(3.14159265358979),
            // Including a space differentiates this from
            // boost::lexical_cast<std::string>, which doesn't handle embedded
            // spaces so well.
            abc("abc def")
        {
            llsd["i"]   = i;
            llsd["d"]   = d;
            llsd["abc"] = abc;
            def = L"def ghi";

        }

        char        c;
        short       s;
        int         i;
        long        l;
        float       f;
        double      d;
        std::string abc;
        std::wstring def;
        LLSD        llsd;
    };

    TEST_CASE_METHOD(stringize_data, "stringize_h: stringize renders each type and LLSD notation", "[stringize_h]")
    {
        CHECK(stringize(c) == "c");
        CHECK(stringize(s) == "17");
        CHECK(stringize(i) == "34");
        CHECK(stringize(l) == "68");
        CHECK(stringize(f) == "3.14159");
        CHECK(stringize(d) == "3.14159");
        CHECK(stringize(abc) == "abc def");
        //ensure_equals(stringize(def),  "def ghi"); //Will generate LL_WARNS() due to narrowing.
        // LLSDNotationFormatter emits Reals at full round-trip precision
        CHECK(stringize(llsd) == "{'abc':'abc def','d':r3.14159265358979,'i':i34}");
    }

    TEST_CASE_METHOD(stringize_data, "stringize_h: STRINGIZE streams an expression with manipulators", "[stringize_h]")
    {
        CHECK(STRINGIZE("c is " << c) == "c is c");
        CHECK(STRINGIZE(std::setprecision(4) << d) == "3.142");
    }

    TEST_CASE_METHOD(stringize_data, "stringize_h: wstringize renders each type on Windows", "[stringize_h]")
    {
        //Tests rely on validity of wstring_to_utf8str()
#if LL_WINDOWS // Windows wstring is a 2byte UTF16 type
        CHECK(ll_convert<std::string>(wstringize(c)) == ll_convert<std::string>(std::wstring(L"c")));
        CHECK(ll_convert<std::string>(wstringize(s)) == ll_convert<std::string>(std::wstring(L"17")));
        CHECK(ll_convert<std::string>(wstringize(i)) == ll_convert<std::string>(std::wstring(L"34")));
        CHECK(ll_convert<std::string>(wstringize(l)) == ll_convert<std::string>(std::wstring(L"68")));
        CHECK(ll_convert<std::string>(wstringize(f)) == ll_convert<std::string>(std::wstring(L"3.14159")));
        CHECK(ll_convert<std::string>(wstringize(d)) == ll_convert<std::string>(std::wstring(L"3.14159")));
        CHECK(ll_convert<std::string>(wstringize(abc)) == ll_convert<std::string>(std::wstring(L"abc def")));
        CHECK(ll_convert<std::string>(wstringize(abc)) == ll_convert<std::string>(wstringize(abc.c_str())));
        CHECK(ll_convert<std::string>(wstringize(def)) == ll_convert<std::string>(std::wstring(L"def ghi")));
#else
        // ensure_equals(ll_convert<std::string>(wstringize(c)),    wstring_to_utf8str(U"c"));
        // ensure_equals(ll_convert<std::string>(wstringize(s)),    wstring_to_utf8str(U"17"));
        // ensure_equals(ll_convert<std::string>(wstringize(i)),    wstring_to_utf8str(U"34"));
        // ensure_equals(ll_convert<std::string>(wstringize(l)),    wstring_to_utf8str(U"68"));
        // ensure_equals(ll_convert<std::string>(wstringize(f)),    wstring_to_utf8str(U"3.14159"));
        // ensure_equals(ll_convert<std::string>(wstringize(d)),    wstring_to_utf8str(U"3.14159"));
        // ensure_equals(ll_convert<std::string>(wstringize(abc)),  wstring_to_utf8str(U"abc def"));
        // ensure_equals(ll_convert<std::string>(wstringize(abc)),  wstring_to_utf8str(wstringize(abc.c_str())));
        // ensure_equals(ll_convert<std::string>(wstringize(def)),  wstring_to_utf8str(U"def ghi"));
#endif
 //       ensure_equals(wstring_to_utf8str(wstringize(llsd)), wstring_to_utf8str(L"{'abc':'abc def','d':r3.14159,'i':i34}"));
    }
} // anonymous namespace
