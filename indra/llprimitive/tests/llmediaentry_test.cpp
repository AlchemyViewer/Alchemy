/**
 * @file llmediaentry_test.cpp
 * @brief llmediaentry unit tests
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include <boost/lexical_cast.hpp>

#include "llstring.h"
#include "llsdutil.h"
#include "llsdserialize.h"

#include "../llmediaentry.h"
#include "indra_constants.h"

#define DEFAULT_MEDIA_ENTRY "<llsd>\n\
 <map>\n\
 <key>alt_image_enable</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_loop</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_play</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_scale</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_zoom</key>\n\
 <boolean>0</boolean>\n\
 <key>controls</key>\n\
 <integer>0</integer>\n\
 <key>current_url</key>\n\
 <string />\n\
 <key>first_click_interact</key>\n\
 <boolean>0</boolean>\n\
 <key>height_pixels</key>\n\
 <integer>0</integer>\n\
 <key>home_url</key>\n\
 <string />\n\
 <key>perms_control</key>\n\
 <integer>7</integer>\n\
 <key>perms_interact</key>\n\
 <integer>7</integer>\n\
 <key>whitelist_enable</key>\n\
 <boolean>0</boolean>\n\
 <key>width_pixels</key>\n\
 <integer>0</integer>\n\
 </map>\n\
 </llsd>"

#define EMPTY_MEDIA_ENTRY "<llsd>\n\
 <map>\n\
 <key>alt_image_enable</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_loop</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_play</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_scale</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_zoom</key>\n\
 <boolean>0</boolean>\n\
 <key>controls</key>\n\
 <integer>0</integer>\n\
 <key>current_url</key>\n\
 <string />\n\
 <key>first_click_interact</key>\n\
 <boolean>0</boolean>\n\
 <key>height_pixels</key>\n\
 <integer>0</integer>\n\
 <key>home_url</key>\n\
 <string />\n\
 <key>perms_control</key>\n\
 <integer>0</integer>\n\
 <key>perms_interact</key>\n\
 <integer>0</integer>\n\
 <key>whitelist_enable</key>\n\
 <boolean>0</boolean>\n\
 <key>width_pixels</key>\n\
 <integer>0</integer>\n\
 </map>\n\
 </llsd>"

#define PARTIAL_MEDIA_ENTRY(CURRENT_URL) "<llsd>\n\
 <map>\n\
 <key>alt_image_enable</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_loop</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_play</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_scale</key>\n\
 <boolean>0</boolean>\n\
 <key>auto_zoom</key>\n\
 <boolean>0</boolean>\n\
 <key>controls</key>\n\
 <integer>0</integer>\n\
 <key>current_url</key>\n\
 <string>" CURRENT_URL "</string>\n\
 <key>first_click_interact</key>\n\
 <boolean>0</boolean>\n\
 <key>height_pixels</key>\n\
 <integer>0</integer>\n\
 <key>home_url</key>\n\
 <string />\n\
 <key>perms_control</key>\n\
 <integer>0</integer>\n\
 <key>perms_interact</key>\n\
 <integer>0</integer>\n\
 <key>whitelist_enable</key>\n\
 <boolean>0</boolean>\n\
 <key>width_pixels</key>\n\
 <integer>0</integer>\n\
 </map>\n\
 </llsd>"

namespace
{
    // this is fixture data that gets created before each test and destroyed
    // after each test.  this is where we put all of the setup/takedown code
    // and data needed for each test.
    struct MediaEntry_test
    {
        MediaEntry_test() {
            emptyMediaEntryStr = EMPTY_MEDIA_ENTRY;
            std::istringstream e(EMPTY_MEDIA_ENTRY);
            LLSDSerialize::fromXML(emptyMediaEntryLLSD, e);
            defaultMediaEntryStr = DEFAULT_MEDIA_ENTRY;
            std::istringstream d(DEFAULT_MEDIA_ENTRY);
            LLSDSerialize::fromXML(defaultMediaEntryLLSD, d);
        }
        std::string emptyMediaEntryStr;
        LLSD emptyMediaEntryLLSD;
        std::string defaultMediaEntryStr;
        LLSD defaultMediaEntryLLSD;
    };

}


namespace
{
    void require_llsd_equals(const LLSD& expected, const LLSD& actual)
    {
        if (!llsd_equals(expected, actual))
        {
            FAIL("actual: " << ll_pretty_print_sd(actual) << "\n  expected: " << ll_pretty_print_sd(expected));
        }
    }

    void set_whitelist(LLMediaEntry &entry, const char *str)
    {
        std::vector<std::string> tokens;
        LLStringUtil::getTokens(std::string(str), tokens, ",");
        entry.setWhiteList(tokens);
    }

    void require_whitelist_check(int num, bool enable, const char *whitelist, const char *candidate_url, bool expected_pass)
    {
        CAPTURE(num, enable, whitelist, candidate_url);
        LLMediaEntry entry;
        entry.setWhiteListEnable(enable);
        set_whitelist(entry, whitelist);
        bool passed_whitelist = entry.checkCandidateUrl(candidate_url);
        REQUIRE(expected_pass == passed_whitelist);
    }

    void require_whitelist_check(int num, const char *whitelist, const char *candidate_url, bool expected_pass)
    {
        require_whitelist_check(num, true, whitelist, candidate_url, expected_pass);
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: a new entry has the default LLSD", "[LLMediaEntry]")
    {
        LLMediaEntry entry;
        require_llsd_equals(defaultMediaEntryLLSD, entry.asLLSD());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: fromLLSD of an empty LLSD gives an empty entry", "[LLMediaEntry]")
    {
        LLMediaEntry entry;
        LLSD sd;
        entry.fromLLSD(sd);
        require_llsd_equals(emptyMediaEntryLLSD, entry.asLLSD());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: fromLLSD of a partial LLSD leaves the other keys empty", "[LLMediaEntry]")
    {
        LLMediaEntry entry;
        LLSD sd;
        sd[LLMediaEntry::CURRENT_URL_KEY] = "http://www.example.com";
        entry.fromLLSD(sd);
        LLSD golden;
        std::istringstream p(PARTIAL_MEDIA_ENTRY("http://www.example.com"));
        LLSDSerialize::fromXML(golden,p);
        require_llsd_equals(golden, entry.asLLSD());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: asLLSD replaces what the LLSD already held", "[LLMediaEntry]")
    {
        LLMediaEntry entry;
        LLSD sd;
        // Put some cruft in the LLSD
        sd[LLMediaEntry::CURRENT_URL_KEY] = "http://www.example.com";
        LLSD whitelist;
        whitelist.append("*.example.com");
        sd[LLMediaEntry::WHITELIST_KEY] = whitelist;
        entry.asLLSD(sd);
        require_llsd_equals(defaultMediaEntryLLSD, sd);
    }


    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: fromLLSD of asLLSD output drops a whitelist the source lacks", "[LLMediaEntry]")
    {
        LLMediaEntry entry1, entry2;
        // Add a whitelist to entry2
        std::vector<std::string> whitelist;
        whitelist.push_back("*.example.com");
        entry2.setWhiteList(whitelist);
        // Render entry1 (which has no whitelist) as an LLSD
        LLSD sd;
        entry1.asLLSD(sd);
        // "read" that LLSD into entry 2
        entry2.fromLLSD(sd);
        require_llsd_equals(defaultMediaEntryLLSD, entry2.asLLSD());
    }

    // limit tests
    const char *URL_OK = "http://www.example.com";
    const char *URL_TOO_BIG = "http://www.example.com.qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq";

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: setCurrentURL accepts a short URL and rejects one over the limit", "[LLMediaEntry]")
    {
        LLMediaEntry entry;
        U32 status = entry.setCurrentURL(URL_OK);
        CHECK(status == LSL_STATUS_OK);
        status = entry.setCurrentURL(URL_TOO_BIG);
        CHECK(status == LSL_STATUS_BOUNDS_ERROR);
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: setHomeURL accepts a short URL and rejects one over the limit", "[LLMediaEntry]")
    {
        LLMediaEntry entry;
        U32 status = entry.setHomeURL(URL_OK);
        CHECK(status == LSL_STATUS_OK);
        status = entry.setHomeURL(URL_TOO_BIG);
        CHECK(status == LSL_STATUS_BOUNDS_ERROR);
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: setWhiteList accepts a valid list", "[LLMediaEntry]")
    {
        // Test a valid list
        LLMediaEntry entry;
        std::vector<std::string> whitelist;
        whitelist.push_back(std::string(URL_OK));
        S32 status = entry.setWhiteList(whitelist);
        CHECK(status == LSL_STATUS_OK);
        CHECK(whitelist == entry.getWhiteList());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: setWhiteList rejects a list with an oversized URL and stays empty", "[LLMediaEntry]")
    {
        // Test an invalid list
        LLMediaEntry entry;
        std::vector<std::string> whitelist, empty;
        whitelist.push_back(std::string(URL_OK));
        whitelist.push_back(std::string(URL_TOO_BIG));
        S32 status = entry.setWhiteList(whitelist);
        CHECK(status == LSL_STATUS_BOUNDS_ERROR);
        CHECK(empty == entry.getWhiteList());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: setWhiteList rejects too many entries and stays empty", "[LLMediaEntry]")
    {
        // Test an invalid list
        LLMediaEntry entry;
        std::vector<std::string> whitelist, empty;
        for (int i=0; i < LLMediaEntry::MAX_WHITELIST_SIZE+1; i++) {
            whitelist.push_back("Q");
        }
        S32 status = entry.setWhiteList(whitelist);
        CHECK(status == LSL_STATUS_BOUNDS_ERROR);
        CHECK(empty == entry.getWhiteList());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: both setWhiteList overloads agree on a valid list", "[LLMediaEntry]")
    {
        // Test a valid list
        std::vector<std::string> whitelist, empty;
        LLSD whitelist_llsd;
        whitelist.push_back(std::string(URL_OK));
        whitelist_llsd.append(std::string(URL_OK));
        LLMediaEntry entry1, entry2;
        CHECK(entry1.setWhiteList(whitelist) == LSL_STATUS_OK);
        CHECK(entry2.setWhiteList(whitelist_llsd) == LSL_STATUS_OK);
        CHECK(entry1.getWhiteList() == entry2.getWhiteList());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: both setWhiteList overloads reject an oversized URL", "[LLMediaEntry]")
    {
        // Test an invalid list
        std::vector<std::string> whitelist, empty;
        LLSD whitelist_llsd;
        whitelist.push_back(std::string(URL_OK));
        whitelist.push_back(std::string(URL_TOO_BIG));
        whitelist_llsd.append(std::string(URL_OK));
        whitelist_llsd.append(std::string(URL_TOO_BIG));
        LLMediaEntry entry1, entry2;
        CHECK(entry1.setWhiteList(whitelist) == LSL_STATUS_BOUNDS_ERROR);
        CHECK(entry2.setWhiteList(whitelist_llsd) == LSL_STATUS_BOUNDS_ERROR);
        CHECK(empty == entry1.getWhiteList());
        CHECK(empty == entry2.getWhiteList());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: both setWhiteList overloads reject too many entries", "[LLMediaEntry]")
    {
        // Test an invalid list, too many
        std::vector<std::string> whitelist, empty;
        LLSD whitelist_llsd;
        for (int i=0; i < LLMediaEntry::MAX_WHITELIST_SIZE+1; i++) {
            whitelist.push_back("Q");
            whitelist_llsd.append("Q");
        }
        LLMediaEntry entry1, entry2;
        CHECK(entry1.setWhiteList(whitelist) == LSL_STATUS_BOUNDS_ERROR);
        CHECK(entry2.setWhiteList(whitelist_llsd) == LSL_STATUS_BOUNDS_ERROR);
        CHECK(empty == entry1.getWhiteList());
        CHECK(empty == entry2.getWhiteList());
    }

    TEST_CASE_METHOD(MediaEntry_test, "LLMediaEntry: checkCandidateUrl matches URLs against whitelist patterns", "[LLMediaEntry]")
    {
        // Whitelist check tests
        int n=0;

        // Check the "empty whitelist" case
        require_whitelist_check(++n, "", "http://www.example.com", true);

        // Check the "missing scheme" case
        require_whitelist_check(++n, "www.example.com", "http://www.example.com", true);

        // Check the "exactly the same" case
        require_whitelist_check(++n, "http://example.com", "http://example.com", true);

        // Check the enable flag
        require_whitelist_check(++n, false, "www.example.com", "http://www.secondlife.com", true);
        require_whitelist_check(++n, true, "www.example.com", "http://www.secondlife.com", false);

        // Check permutations of trailing slash:
        require_whitelist_check(++n, "http://www.example.com", "http://www.example.com/", true);
        require_whitelist_check(++n, "http://www.example.com/", "http://www.example.com/", true);
        require_whitelist_check(++n, "http://www.example.com/", "http://www.example.com", false);
        require_whitelist_check(++n, "http://www.example.com", "http://www.example.com/foobar", true);
        require_whitelist_check(++n, "http://www.example.com/", "http://www.example.com/foobar", false);


        // More cases...
        require_whitelist_check(++n, "http://example.com", "http://example.com/wiki", true);
        require_whitelist_check(++n, "www.example.com", "http://www.example.com/help", true);
        require_whitelist_check(++n, "http://www.example.com", "http://wwwexample.com", false);
        require_whitelist_check(++n, "http://www.example.com", "http://www.example.com/wiki", true);
        require_whitelist_check(++n, "example.com", "http://wwwexample.com", false);
        require_whitelist_check(++n, "http://www.example.com/", "http://www.amazon.com/wiki", false);
        require_whitelist_check(++n, "www.example.com", "http://www.amazon.com", false);

        // regexp cases
        require_whitelist_check(++n, "*.example.com", "http://www.example.com", true);
        require_whitelist_check(++n, "*.example.com", "http://www.amazon.com", false);
        require_whitelist_check(++n, "*.example.com", "http://www.example.com/foo/bar", true);
        require_whitelist_check(++n, "*.example.com", "http:/example.com/foo/bar", false);
        require_whitelist_check(++n, "*example.com", "http://example.com/foo/bar", true);
        require_whitelist_check(++n, "*example.com", "http://my.virus.com/foo/bar?example.com", false);
        require_whitelist_check(++n, "example.com", "http://my.virus.com/foo/bar?example.com", false);
        require_whitelist_check(++n, "*example.com", "http://my.virus.com/foo/bar?*example.com", false);
        require_whitelist_check(++n, "http://*example.com", "http://www.example.com", true);
        require_whitelist_check(++n, "http://*.example.com", "http://www.example.com", true);
        require_whitelist_check(++n, "http://*.e$?^.com", "http://www.e$?^.com", true);
        require_whitelist_check(++n, "*.example.com/foo/bar", "http://www.example.com/", false);
        require_whitelist_check(++n, "*.example.com/foo/bar", "http://example.com/foo/bar", false);
        require_whitelist_check(++n, "http://*.example.com/foo/bar", "http://www.example.com", false);
        require_whitelist_check(++n, "http://*.example.com", "https://www.example.com", false);
        require_whitelist_check(++n, "http*://*.example.com", "rtsp://www.example.com", false);
        require_whitelist_check(++n, "http*://*.example.com", "https://www.example.com", true);
        require_whitelist_check(++n, "example.com", "http://www.example.com", false);
        require_whitelist_check(++n, "www.example.com", "http://www.example.com:80", false);
        require_whitelist_check(++n, "www.example.com", "http://www.example.com", true);
        require_whitelist_check(++n, "www.example.com/", "http://www.example.com", false);
        require_whitelist_check(++n, "www.example.com/foo/bar/*", "http://www.example.com/foo/bar/baz", true);

        // Path only
        require_whitelist_check(++n, "/foo/*/baz", "http://www.example.com/foo/bar/baz", true);
        require_whitelist_check(++n, "/foo/*/baz", "http://www.example.com/foo/bar/", false);
    }

}

