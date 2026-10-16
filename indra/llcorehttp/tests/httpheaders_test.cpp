/**
 * @file httpheaders_test.cpp
 * @brief unit tests for the LLCore::HttpHeaders class
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2013, Linden Research, Inc.
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

#include "httpheaders.h"

#include <iostream>


using namespace LLCore;
using namespace LLCoreInt;


namespace
{
TEST_CASE("HttpHeaders Tests: a new HttpHeaders is empty", "[HttpHeaders_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpHeaders::ptr_t headers = HttpHeaders::ptr_t(new HttpHeaders());
    CHECK(headers->size() == 0);

    // release the implicit reference, causing the object to be released
    headers.reset();
}

TEST_CASE("HttpHeaders Tests: append keeps names and values in order", "[HttpHeaders_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpHeaders::ptr_t headers = HttpHeaders::ptr_t(new HttpHeaders());

    {
        // Append a few strings
        std::string str1n("Pragma");
        std::string str1v("");
        headers->append(str1n, str1v);
        std::string str2n("Accept");
        std::string str2v("application/json");
        headers->append(str2n, str2v);

        REQUIRE(headers->size() == 2);
        HttpHeaders::container_t & c(headers->getContainerTESTONLY());

        CHECK(c[0].first == str1n);
        CHECK(c[0].second == str1v);
        CHECK(c[1].first == str2n);
        CHECK(c[1].second == str2v);
    }

    // release the implicit reference, causing the object to be released
    headers.reset();
}

TEST_CASE("HttpHeaders Tests: find matches whole names case-sensitively", "[HttpHeaders_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpHeaders::ptr_t headers = HttpHeaders::ptr_t(new HttpHeaders());

    {
        // Append a few strings
        std::string str1n("Uno");
        std::string str1v("1");
        headers->append(str1n, str1v);
        std::string str2n("doS");
        std::string str2v("2-2-2-2");
        headers->append(str2n, str2v);
        std::string str3n("TRES");
        std::string str3v("trois gymnopedie");
        headers->append(str3n, str3v);

        CHECK(headers->size() == 3);

        const std::string * result(NULL);

        // Find a header
        result = headers->find("TRES");
        REQUIRE(result != NULL);
        CHECK((result != NULL && str3v == *result));

        // appends above are raw and find is case sensitive
        result = headers->find("TReS");
        UNSCOPED_INFO("the case differs");
        CHECK(result == NULL);

        result = headers->find("TRE");
        UNSCOPED_INFO("a prefix of the name");
        CHECK(result == NULL);

        result = headers->find("TRESS");
        UNSCOPED_INFO("the name and more");
        CHECK(result == NULL);
    }

    // release the implicit reference, causing the object to be released
    headers.reset();
}

TEST_CASE("HttpHeaders Tests: appendNormal splits and cleans raw header lines", "[HttpHeaders_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpHeaders::ptr_t headers = HttpHeaders::ptr_t(new HttpHeaders());

    {
        static char line1[] = " AcCePT : image/yourfacehere";
        static char line1v[] = "image/yourfacehere";
        headers->appendNormal(line1, sizeof(line1) - 1);

        CHECK(headers->size() == 1);

        const std::string * result(NULL);

        // Find a header
        result = headers->find("accept");
        REQUIRE(result != NULL);
        CHECK((result != NULL && *result == line1v));

        // Left-clean on value
        static char line2[] = " next : \t\tlinejunk \t";
        headers->appendNormal(line2, sizeof(line2) - 1);
        CHECK(headers->size() == 2);
        result = headers->find("next");
        REQUIRE(result != NULL);
        UNSCOPED_INFO("the value is trimmed on the left only");
        CHECK((result != NULL &&
               *result == "linejunk \t"));

        // First value unmolested
        result = headers->find("accept");
        REQUIRE(result != NULL);
        CHECK((result != NULL && *result == line1v));

        // Colons in value are okay
        static char line3[] = "FancY-PANTs::plop:-neuf-=vleem=";
        static char line3v[] = ":plop:-neuf-=vleem=";
        headers->appendNormal(line3, sizeof(line3) - 1);
        CHECK(headers->size() == 3);
        result = headers->find("fancy-pants");
        REQUIRE(result != NULL);
        UNSCOPED_INFO("colons in the value are kept");
        CHECK((result != NULL && *result == line3v));

        // Zero-length value
        static char line4[] = "all-talk-no-walk:";
        headers->appendNormal(line4, sizeof(line4) - 1);
        CHECK(headers->size() == 4);
        result = headers->find("all-talk-no-walk");
        REQUIRE(result != NULL);
        CHECK((result != NULL && result->size() == 0));

        // Zero-length name
        static char line5[] = ":all-talk-no-walk";
        static char line5v[] = "all-talk-no-walk";
        headers->appendNormal(line5, sizeof(line5) - 1);
        CHECK(headers->size() == 5);
        result = headers->find("");
        REQUIRE(result != NULL);
        CHECK((result != NULL && *result == line5v));

        // Lone colon is still something
        headers->clear();
        static char line6[] = "  :";
        headers->appendNormal(line6, sizeof(line6) - 1);
        CHECK(headers->size() == 1);
        result = headers->find("");
        REQUIRE(result != NULL);
        CHECK((result != NULL && result->size() == 0));

        // Line without colons is taken as-is and unstripped in name
        static char line7[] = " \toskdgioasdghaosdghoowg28342908tg8902hg0hwedfhqew890v7qh0wdebv78q0wdevbhq>?M>BNM<ZV>?NZ? \t";
        headers->appendNormal(line7, sizeof(line7) - 1);
        CHECK(headers->size() == 2);
        result = headers->find(line7);
        REQUIRE(result != NULL);
        CHECK((result != NULL && result->size() == 0));

        // Normaling interface heeds the byte count, doesn't look for NUL-terminator
        static char line8[] = "binary:ignorestuffontheendofthis";
        headers->appendNormal(line8, 13);
        CHECK(headers->size() == 3);
        result = headers->find("binary");
        REQUIRE(result != NULL);
        CHECK((result != NULL &&
               *result == "ignore"));

    }

    // release the implicit reference, causing the object to be released
    headers.reset();
}

// Verify forward iterator finds everything as expected
TEST_CASE("HttpHeaders Tests: iterators visit every entry in order", "[HttpHeaders_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpHeaders::ptr_t headers = HttpHeaders::ptr_t(new HttpHeaders());

    HttpHeaders::iterator end(headers->end()), begin(headers->begin());
    CHECK(end == begin);
    HttpHeaders::const_iterator cend(headers->end()), cbegin(headers->begin());
    CHECK(cend == cbegin);

    CHECK(headers->end() == headers->begin());

    {
        static char line1[] = " AcCePT : image/yourfacehere";
        static char line1v[] = "image/yourfacehere";
        headers->appendNormal(line1, sizeof(line1) - 1);

        static char line2[] = " next : \t\tlinejunk \t";
        static char line2v[] = "linejunk \t";
        headers->appendNormal(line2, sizeof(line2) - 1);

        static char line3[] = "FancY-PANTs::plop:-neuf-=vleem=";
        static char line3v[] = ":plop:-neuf-=vleem=";
        headers->appendNormal(line3, sizeof(line3) - 1);

        static char line4[] = "all-talk-no-walk:";
        static char line4v[] = "";
        headers->appendNormal(line4, sizeof(line4) - 1);

        static char line5[] = ":all-talk-no-walk";
        static char line5v[] = "all-talk-no-walk";
        headers->appendNormal(line5, sizeof(line5) - 1);

        static char line6[] = "  :";
        static char line6v[] = "";
        headers->appendNormal(line6, sizeof(line6) - 1);

        REQUIRE(headers->size() == 6);

        static char * values[] = {
            line1v,
            line2v,
            line3v,
            line4v,
            line5v,
            line6v
        };

        int i(0);
        HttpHeaders::const_iterator cend(headers->end());
        for (HttpHeaders::const_iterator it(headers->begin());
             cend != it;
             ++it, ++i)
        {
            CAPTURE(i);
            REQUIRE((*it).second == values[i]);
        }

        // Rewind, do non-consts
        i = 0;
        HttpHeaders::iterator end(headers->end());
        for (HttpHeaders::iterator it(headers->begin());
             end != it;
             ++it, ++i)
        {
            CAPTURE(i);
            REQUIRE((*it).second == values[i]);
        }
    }

    // release the implicit reference, causing the object to be released
    headers.reset();
}

// Reverse iterators find everything as expected
TEST_CASE("HttpHeaders Tests: reverse iterators visit every entry in reverse order", "[HttpHeaders_Tests]")
{
    // create a new ref counted object with an implicit reference
    HttpHeaders::ptr_t headers = HttpHeaders::ptr_t(new HttpHeaders());

    HttpHeaders::reverse_iterator rend(headers->rend()), rbegin(headers->rbegin());
    CHECK(rend == rbegin);
    HttpHeaders::const_reverse_iterator crend(headers->rend()), crbegin(headers->rbegin());
    CHECK(crend == crbegin);

    {
        static char line1[] = " AcCePT : image/yourfacehere";
        static char line1v[] = "image/yourfacehere";
        headers->appendNormal(line1, sizeof(line1) - 1);

        static char line2[] = " next : \t\tlinejunk \t";
        static char line2v[] = "linejunk \t";
        headers->appendNormal(line2, sizeof(line2) - 1);

        static char line3[] = "FancY-PANTs::plop:-neuf-=vleem=";
        static char line3v[] = ":plop:-neuf-=vleem=";
        headers->appendNormal(line3, sizeof(line3) - 1);

        static char line4[] = "all-talk-no-walk:";
        static char line4v[] = "";
        headers->appendNormal(line4, sizeof(line4) - 1);

        static char line5[] = ":all-talk-no-walk";
        static char line5v[] = "all-talk-no-walk";
        headers->appendNormal(line5, sizeof(line5) - 1);

        static char line6[] = "  :";
        static char line6v[] = "";
        headers->appendNormal(line6, sizeof(line6) - 1);

        REQUIRE(headers->size() == 6);

        static char * values[] = {
            line6v,
            line5v,
            line4v,
            line3v,
            line2v,
            line1v
        };

        int i(0);
        HttpHeaders::const_reverse_iterator cend(headers->rend());
        for (HttpHeaders::const_reverse_iterator it(headers->rbegin());
             cend != it;
             ++it, ++i)
        {
            CAPTURE(i);
            REQUIRE((*it).second == values[i]);
        }

        // Rewind, do non-consts
        i = 0;
        HttpHeaders::reverse_iterator end(headers->rend());
        for (HttpHeaders::reverse_iterator it(headers->rbegin());
             end != it;
             ++it, ++i)
        {
            CAPTURE(i);
            REQUIRE((*it).second == values[i]);
        }
    }

    // release the implicit reference, causing the object to be released
    headers.reset();
}

}  // anonymous namespace
