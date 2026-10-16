/**
 * @file llurlentry_test.cpp
 * @author Martin Reddy
 * @brief Unit tests for LLUrlEntry objects
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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
#include "../llurlentry.h"
#include "../lluictrl.h"
#include <catch2/catch_test_macros.hpp>
#include "../lluicolortable.h"
#include "../llrender/lluiimage.h"
#include "../llmessage/llexperiencecache.h"

namespace
{
}

namespace
{
        void require_url(const std::string &testname, LLUrlEntryBase &entry,
                       const char *text, const std::string &expected)
        {
            std::string url = "";
            ALRegexMatch result;
            const U32 group = entry.getUrlGroup();
            bool found = entry.getPattern().search(text, &result) && result.matched(group);
            if (found)
            {
                url = entry.getUrl(result.str(group));
            }
            INFO(testname);
            REQUIRE(url == expected);
        }

        void dummyCallback(const std::string &url, const std::string &label, const std::string& icon)
        {
        }

        void require_label(const std::string &testname, LLUrlEntryBase &entry,
                       const char *text, const std::string &expected)
        {
            std::string label = "";
            ALRegexMatch result;
            const U32 group = entry.getUrlGroup();
            bool found = entry.getPattern().search(text, &result) && result.matched(group);
            if (found)
            {
                std::string url = result.str(group);
                label = entry.getLabel(url, boost::bind(dummyCallback, _1, _2, _3));
            }
            INFO(testname);
            REQUIRE(label == expected);
        }

        void require_location(const std::string &testname, LLUrlEntryBase &entry,
                          const char *text, const std::string &expected)
        {
            std::string location = "";
            ALRegexMatch result;
            const U32 group = entry.getUrlGroup();
            bool found = entry.getPattern().search(text, &result) && result.matched(group);
            if (found)
            {
                std::string url = result.str(group);
                location = entry.getLocation(url);
            }
            INFO(testname);
            REQUIRE(location == expected);
        }

    TEST_CASE("LLUrlEntry: LLUrlEntryHTTP finds http and https urls in text", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryHTTP - standard http Urls
        //
        LLUrlEntryHTTP url;

        require_url("no valid url", url,
                  "htp://slurl.com/",
                  "");

        require_url("simple http (1)", url,
                  "http://slurl.com/",
                  "http://slurl.com/");

        require_url("simple http (2)", url,
                  "http://slurl.com",
                  "http://slurl.com");

        require_url("simple http (3)", url,
                  "http://slurl.com/about.php",
                  "http://slurl.com/about.php");

        require_url("simple https", url,
                  "https://slurl.com/about.php",
                  "https://slurl.com/about.php");

        require_url("http in text (1)", url,
                  "XX http://slurl.com/ XX",
                  "http://slurl.com/");

        require_url("http in text (2)", url,
                  "XX http://slurl.com/about.php XX",
                  "http://slurl.com/about.php");

        require_url("https in text", url,
                  "XX https://slurl.com/about.php XX",
                  "https://slurl.com/about.php");

        require_url("two http urls", url,
                  "XX http://slurl.com/about.php http://secondlife.com/ XX",
                  "http://slurl.com/about.php");

        require_url("http url with port and username", url,
                  "XX http://nobody@slurl.com:80/about.php http://secondlife.com/ XX",
                  "http://nobody@slurl.com:80/about.php");

        require_url("http url with port, username, and query string", url,
                  "XX http://nobody@slurl.com:80/about.php?title=hi%20there http://secondlife.com/ XX",
                  "http://nobody@slurl.com:80/about.php?title=hi%20there");

        // note: terminating commas will be removed by LLUrlRegistry:findUrl()
        require_url("http url with commas in middle and terminating", url,
                  "XX http://slurl.com/?title=Hi,There, XX",
                  "http://slurl.com/?title=Hi,There,");

        // note: terminating periods will be removed by LLUrlRegistry:findUrl()
        require_url("http url with periods in middle and terminating", url,
                  "XX http://slurl.com/index.php. XX",
                  "http://slurl.com/index.php.");

        // DEV-19842: Closing parenthesis ")" breaks urls
        require_url("http url with brackets (1)", url,
                  "XX http://en.wikipedia.org/wiki/JIRA_(software) XX",
                  "http://en.wikipedia.org/wiki/JIRA_(software)");

        // DEV-19842: Closing parenthesis ")" breaks urls
        require_url("http url with brackets (2)", url,
                  "XX http://jira.secondlife.com/secure/attachment/17990/eggy+avs+in+1.21.0+(93713)+public+nightly.jpg XX",
                  "http://jira.secondlife.com/secure/attachment/17990/eggy+avs+in+1.21.0+(93713)+public+nightly.jpg");

        // DEV-10353: URLs in chat log terminated incorrectly when newline in chat
        require_url("http url with newlines", url,
                  "XX\nhttp://www.secondlife.com/\nXX",
                  "http://www.secondlife.com/");

        require_url("http url without tld shouldn't be decorated (1)", url,
                  "http://test",
                  "");

        require_url("http url without tld shouldn't be decorated (2)", url,
                  "http://test .com",
                  "");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryHTTPLabel finds the url in a labelled wiki-style link", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryHTTPLabel - wiki-style http Urls with labels
        //
        LLUrlEntryHTTPLabel url;

        require_url("invalid wiki url [1]", url,
                  "[http://www.example.org]",
                  "");

        require_url("invalid wiki url [2]", url,
                  "[http://www.example.org",
                  "");

        require_url("invalid wiki url [3]", url,
                  "[http://www.example.org Label",
                  "");

        require_url("example.org with label (spaces)", url,
                  "[http://www.example.org  Text]",
                  "http://www.example.org");

        require_url("example.org with label (tabs)", url,
                  "[http://www.example.org\t Text]",
                  "http://www.example.org");

        require_url("SL http URL with label", url,
                  "[http://www.secondlife.com/ Second Life]",
                  "http://www.secondlife.com/");

        require_url("SL https URL with label", url,
                  "XXX [https://www.secondlife.com/ Second Life] YYY",
                  "https://www.secondlife.com/");

        require_url("SL http URL with label", url,
                  "[http://www.secondlife.com/?test=Hi%20There Second Life]",
                  "http://www.secondlife.com/?test=Hi%20There");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntrySLURL finds slurl.com location urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntrySLURL - second life URLs
        //
        LLUrlEntrySLURL url;

        require_url("no valid slurl [1]", url,
                  "htp://slurl.com/secondlife/Ahern/50/50/50/",
                  "");

        require_url("no valid slurl [2]", url,
                  "http://slurl.com/secondlife/",
                  "");

        require_url("no valid slurl [3]", url,
                  "hhtp://slurl.com/secondlife/Ahern/50/FOO/50/",
                  "");

        require_url("Ahern (50,50,50) [1]", url,
                  "http://slurl.com/secondlife/Ahern/50/50/50/",
                  "http://slurl.com/secondlife/Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [2]", url,
                  "XXX http://slurl.com/secondlife/Ahern/50/50/50/ XXX",
                  "http://slurl.com/secondlife/Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [3]", url,
                  "XXX http://slurl.com/secondlife/Ahern/50/50/50 XXX",
                  "http://slurl.com/secondlife/Ahern/50/50/50");

        require_url("Ahern (50,50,50) multicase", url,
                  "XXX http://SLUrl.com/SecondLife/Ahern/50/50/50/ XXX",
                  "http://SLUrl.com/SecondLife/Ahern/50/50/50/");

        require_url("Ahern (50,50) [1]", url,
                  "XXX http://slurl.com/secondlife/Ahern/50/50/ XXX",
                  "http://slurl.com/secondlife/Ahern/50/50/");

        require_url("Ahern (50,50) [2]", url,
                  "XXX http://slurl.com/secondlife/Ahern/50/50 XXX",
                  "http://slurl.com/secondlife/Ahern/50/50");

        require_url("Ahern (50)", url,
                  "XXX http://slurl.com/secondlife/Ahern/50 XXX",
                  "http://slurl.com/secondlife/Ahern/50");

        require_url("Ahern", url,
                  "XXX http://slurl.com/secondlife/Ahern/ XXX",
                  "http://slurl.com/secondlife/Ahern/");

        require_url("Ahern SLURL with title", url,
                  "XXX http://slurl.com/secondlife/Ahern/50/50/50/?title=YOUR%20TITLE%20HERE! XXX",
                  "http://slurl.com/secondlife/Ahern/50/50/50/?title=YOUR%20TITLE%20HERE!");

        require_url("Ahern SLURL with msg", url,
                  "XXX http://slurl.com/secondlife/Ahern/50/50/50/?msg=Your%20text%20here. XXX",
                  "http://slurl.com/secondlife/Ahern/50/50/50/?msg=Your%20text%20here.");

        // DEV-21577: In-world SLURLs containing "(" or ")" are not treated as a hyperlink in chat
        require_url("SLURL with brackets", url,
                  "XXX http://slurl.com/secondlife/Burning%20Life%20(Hyper)/27/210/30 XXX",
                  "http://slurl.com/secondlife/Burning%20Life%20(Hyper)/27/210/30");

        // DEV-35459: SLURLs and teleport Links not parsed properly
        require_url("SLURL with quote", url,
                  "XXX http://slurl.com/secondlife/A'ksha%20Oasis/41/166/701 XXX",
                  "http://slurl.com/secondlife/A%27ksha%20Oasis/41/166/701");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryAgent finds agent app urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryAgent - secondlife://app/agent Urls
        //
        LLUrlEntryAgent url;

        require_url("Invalid Agent Url", url,
                  "secondlife:///app/agent/0e346d8b-4433-4d66-XXXX-fd37083abc4c/about",
                  "");

        require_url("Agent Url ", url,
                  "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about",
                  "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("Agent Url in text", url,
                  "XXX secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about XXX",
                  "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("Agent Url multicase", url,
                  "XXX secondlife:///App/AGENT/0E346D8B-4433-4d66-a6b0-fd37083abc4c/About XXX",
                  "secondlife:///App/AGENT/0E346D8B-4433-4d66-a6b0-fd37083abc4c/About");

        require_url("Agent Url alternate command", url,
                  "XXX secondlife:///App/AGENT/0E346D8B-4433-4d66-a6b0-fd37083abc4c/foobar",
                  "secondlife:///App/AGENT/0E346D8B-4433-4d66-a6b0-fd37083abc4c/foobar");

        require_url("Standalone Agent Url ", url,
                  "x-grid-location-info://lincoln.lindenlab.com/app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about",
                  "x-grid-location-info://lincoln.lindenlab.com/app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("Standalone Agent Url Multicase with Text", url,
                  "M x-grid-location-info://lincoln.lindenlab.com/app/AGENT/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about M",
                  "x-grid-location-info://lincoln.lindenlab.com/app/AGENT/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryGroup finds group app urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryGroup - secondlife://app/group Urls
        //
        LLUrlEntryGroup url;

        require_url("Invalid Group Url", url,
                  "secondlife:///app/group/00005ff3-4044-c79f-XXXX-fb28ae0df991/about",
                  "");

        require_url("Group Url ", url,
                  "secondlife:///app/group/00005ff3-4044-c79f-9de8-fb28ae0df991/about",
                  "secondlife:///app/group/00005ff3-4044-c79f-9de8-fb28ae0df991/about");

        require_url("Group Url ", url,
                  "secondlife:///app/group/00005ff3-4044-c79f-9de8-fb28ae0df991/inspect",
                  "secondlife:///app/group/00005ff3-4044-c79f-9de8-fb28ae0df991/inspect");

        require_url("Group Url in text", url,
                  "XXX secondlife:///app/group/00005ff3-4044-c79f-9de8-fb28ae0df991/about XXX",
                  "secondlife:///app/group/00005ff3-4044-c79f-9de8-fb28ae0df991/about");

        require_url("Group Url multicase", url,
                  "XXX secondlife:///APP/Group/00005FF3-4044-c79f-9de8-fb28ae0df991/About XXX",
                  "secondlife:///APP/Group/00005FF3-4044-c79f-9de8-fb28ae0df991/About");

        require_url("Standalone Group Url ", url,
                  "x-grid-location-info://lincoln.lindenlab.com/app/group/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about",
                  "x-grid-location-info://lincoln.lindenlab.com/app/group/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("Standalone Group Url Multicase with Text", url,
                  "M x-grid-location-info://lincoln.lindenlab.com/app/GROUP/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about M",
                  "x-grid-location-info://lincoln.lindenlab.com/app/GROUP/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

    }

    TEST_CASE("LLUrlEntry: LLUrlEntryPlace finds secondlife location urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryPlace - secondlife://<location> URLs
        //
        LLUrlEntryPlace url;

        require_url("no valid slurl [1]", url,
                  "secondlife://Ahern/FOO/50/",
                  "");

        require_url("Ahern (50,50,50) [1]", url,
                  "secondlife://Ahern/50/50/50/",
                  "secondlife://Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [2]", url,
                  "XXX secondlife://Ahern/50/50/50/ XXX",
                  "secondlife://Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [3]", url,
                  "XXX secondlife://Ahern/50/50/50 XXX",
                  "secondlife://Ahern/50/50/50");

        require_url("Ahern (50,50,50) multicase", url,
                  "XXX SecondLife://Ahern/50/50/50/ XXX",
                  "SecondLife://Ahern/50/50/50/");

        require_url("Ahern (50,50) [1]", url,
                  "XXX secondlife://Ahern/50/50/ XXX",
                  "secondlife://Ahern/50/50/");

        require_url("Ahern (50,50) [2]", url,
                  "XXX secondlife://Ahern/50/50 XXX",
                  "secondlife://Ahern/50/50");

        // DEV-21577: In-world SLURLs containing "(" or ")" are not treated as a hyperlink in chat
        require_url("SLURL with brackets", url,
                  "XXX secondlife://Burning%20Life%20(Hyper)/27/210/30 XXX",
                  "secondlife://Burning%20Life%20(Hyper)/27/210/30");

        // DEV-35459: SLURLs and teleport Links not parsed properly
        require_url("SLURL with quote", url,
                  "XXX secondlife://A'ksha%20Oasis/41/166/701 XXX",
                      "secondlife://A%27ksha%20Oasis/41/166/701");

        require_url("Standalone All Hands (50,50) [2] with text", url,
                  "XXX x-grid-location-info://lincoln.lindenlab.com/region/All%20Hands/50/50/50 XXX",
                  "x-grid-location-info://lincoln.lindenlab.com/region/All%20Hands/50/50/50");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryParcel finds parcel app urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryParcel - secondlife://app/parcel Urls
        //
        LLUrlEntryParcel url;

        require_url("Invalid Parcel Url", url,
                  "secondlife:///app/parcel/0000060e-4b39-e00b-XXXX-d98b1934e3a8/about",
                  "");

        require_url("Parcel Url ", url,
                  "secondlife:///app/parcel/0000060e-4b39-e00b-d0c3-d98b1934e3a8/about",
                  "secondlife:///app/parcel/0000060e-4b39-e00b-d0c3-d98b1934e3a8/about");

        require_url("Parcel Url in text", url,
                  "XXX secondlife:///app/parcel/0000060e-4b39-e00b-d0c3-d98b1934e3a8/about XXX",
                  "secondlife:///app/parcel/0000060e-4b39-e00b-d0c3-d98b1934e3a8/about");

        require_url("Parcel Url multicase", url,
                  "XXX secondlife:///APP/Parcel/0000060e-4b39-e00b-d0c3-d98b1934e3a8/About XXX",
                  "secondlife:///APP/Parcel/0000060e-4b39-e00b-d0c3-d98b1934e3a8/About");
    }
    TEST_CASE("LLUrlEntry: LLUrlEntryTeleport finds teleport app urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryTeleport - secondlife://app/teleport URLs
        //
        LLUrlEntryTeleport url;

        require_url("no valid teleport [1]", url,
                  "http://slurl.com/secondlife/Ahern/50/50/50/",
                  "");

        require_url("no valid teleport [2]", url,
                  "secondlife:///app/teleport/",
                  "");

        require_url("no valid teleport [3]", url,
                  "second-life:///app/teleport/Ahern/50/50/50/",
                  "");

        require_url("no valid teleport [4]", url,
                  "hhtp://slurl.com/secondlife/Ahern/50/FOO/50/",
                  "");

        require_url("Ahern (50,50,50) [1]", url,
                  "secondlife:///app/teleport/Ahern/50/50/50/",
                  "secondlife:///app/teleport/Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [2]", url,
                  "XXX secondlife:///app/teleport/Ahern/50/50/50/ XXX",
                  "secondlife:///app/teleport/Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [3]", url,
                  "XXX secondlife:///app/teleport/Ahern/50/50/50 XXX",
                  "secondlife:///app/teleport/Ahern/50/50/50");

        require_url("Ahern (50,50,50) multicase", url,
                  "XXX SecondLife:///App/Teleport/Ahern/50/50/50/ XXX",
                  "SecondLife:///App/Teleport/Ahern/50/50/50/");

        require_url("Ahern (50,50) [1]", url,
                  "XXX secondlife:///app/teleport/Ahern/50/50/ XXX",
                  "secondlife:///app/teleport/Ahern/50/50/");

        require_url("Ahern (50,50) [2]", url,
                  "XXX secondlife:///app/teleport/Ahern/50/50 XXX",
                  "secondlife:///app/teleport/Ahern/50/50");

        require_url("Ahern (50)", url,
                  "XXX secondlife:///app/teleport/Ahern/50 XXX",
                  "secondlife:///app/teleport/Ahern/50");

        require_url("Ahern", url,
                  "XXX secondlife:///app/teleport/Ahern/ XXX",
                  "secondlife:///app/teleport/Ahern/");

        require_url("Ahern teleport with title", url,
                  "XXX secondlife:///app/teleport/Ahern/50/50/50/?title=YOUR%20TITLE%20HERE! XXX",
                  "secondlife:///app/teleport/Ahern/50/50/50/?title=YOUR%20TITLE%20HERE!");

        require_url("Ahern teleport with msg", url,
                  "XXX secondlife:///app/teleport/Ahern/50/50/50/?msg=Your%20text%20here. XXX",
                  "secondlife:///app/teleport/Ahern/50/50/50/?msg=Your%20text%20here.");

        // DEV-21577: In-world SLURLs containing "(" or ")" are not treated as a hyperlink in chat
        require_url("Teleport with brackets", url,
                  "XXX secondlife:///app/teleport/Burning%20Life%20(Hyper)/27/210/30 XXX",
                  "secondlife:///app/teleport/Burning%20Life%20(Hyper)/27/210/30");

        // DEV-35459: SLURLs and teleport Links not parsed properly
        require_url("Teleport url with quote", url,
                  "XXX secondlife:///app/teleport/A'ksha%20Oasis/41/166/701 XXX",
                  "secondlife:///app/teleport/A%27ksha%20Oasis/41/166/701");

        require_url("Standalone All Hands", url,
                  "XXX x-grid-location-info://lincoln.lindenlab.com/app/teleport/All%20Hands/50/50/50 XXX",
                  "x-grid-location-info://lincoln.lindenlab.com/app/teleport/All%20Hands/50/50/50");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntrySL finds general secondlife urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntrySL - general secondlife:// URLs
        //
        LLUrlEntrySL url;

        require_url("no valid slapp [1]", url,
                  "http:///app/",
                  "");

        require_url("valid slapp [1]", url,
                  "secondlife:///app/",
                  "secondlife:///app/");

        require_url("valid slapp [2]", url,
                  "secondlife:///app/teleport/Ahern/50/50/50/",
                  "secondlife:///app/teleport/Ahern/50/50/50/");

        require_url("valid slapp [3]", url,
                  "secondlife:///app/foo",
                  "secondlife:///app/foo");

        require_url("valid slapp [4]", url,
                  "secondlife:///APP/foo?title=Hi%20There",
                  "secondlife:///APP/foo?title=Hi%20There");

        require_url("valid slapp [5]", url,
                  "secondlife://host/app/",
                  "secondlife://host/app/");

        require_url("valid slapp [6]", url,
                  "secondlife://host:8080/foo/bar",
                  "secondlife://host:8080/foo/bar");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntrySLLabel finds the url in a labelled secondlife link", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntrySLLabel - general secondlife:// URLs with labels
        //
        LLUrlEntrySLLabel url;

        require_url("invalid wiki url [1]", url,
                  "[secondlife:///app/]",
                  "");

        require_url("invalid wiki url [2]", url,
                  "[secondlife:///app/",
                  "");

        require_url("invalid wiki url [3]", url,
                  "[secondlife:///app/ Label",
                  "");

        require_url("agent slurl with label (spaces)", url,
                  "[secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about  Text]",
                  "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("agent slurl with label (tabs)", url,
                  "[secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about\t Text]",
                  "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("agent slurl with label", url,
                  "[secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about FirstName LastName]",
                  "secondlife:///app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");

        require_url("teleport slurl with label", url,
                  "XXX [secondlife:///app/teleport/Ahern/50/50/50/ Teleport to Ahern] YYY",
                  "secondlife:///app/teleport/Ahern/50/50/50/");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryNoLink finds the text inside nolink tags", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryNoLink - turn off hyperlinking
        //
        LLUrlEntryNoLink url;

        require_url("<nolink> [1]", url,
                  "<nolink>google.com</nolink>",
                  "google.com");

        require_url("<nolink> [2]", url,
                  "<nolink>google.com",
                  "");

        require_url("<nolink> [3]", url,
                  "google.com</nolink>",
                  "");

        require_url("<nolink> [4]", url,
                  "<nolink>Hello World</nolink>",
                  "Hello World");

        require_url("<nolink> [5]", url,
                  "<nolink>My Object</nolink>",
                  "My Object");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryRegion finds region urls with their labels and locations", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryRegion - secondlife:///app/region/<location> URLs
        //
        LLUrlEntryRegion url;

        // Regex tests.
        require_url("no valid region", url,
                  "secondlife:///app/region/",
                  "");

        require_url("invalid coords", url,
                  "secondlife:///app/region/Korea2/a/b/c",
                  "secondlife:///app/region/Korea2/"); // don't count invalid coords

        require_url("Ahern (50,50,50) [1]", url,
                  "secondlife:///app/region/Ahern/50/50/50/",
                  "secondlife:///app/region/Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [2]", url,
                  "XXX secondlife:///app/region/Ahern/50/50/50/ XXX",
                  "secondlife:///app/region/Ahern/50/50/50/");

        require_url("Ahern (50,50,50) [3]", url,
                  "XXX secondlife:///app/region/Ahern/50/50/50 XXX",
                  "secondlife:///app/region/Ahern/50/50/50");

        require_url("Ahern (50,50,50) multicase", url,
                  "XXX SecondLife:///App/Region/Ahern/50/50/50/ XXX",
                  "SecondLife:///App/Region/Ahern/50/50/50/");

        require_url("Ahern (50,50) [1]", url,
                  "XXX secondlife:///app/region/Ahern/50/50/ XXX",
                  "secondlife:///app/region/Ahern/50/50/");

        require_url("Ahern (50,50) [2]", url,
                  "XXX secondlife:///app/region/Ahern/50/50 XXX",
                  "secondlife:///app/region/Ahern/50/50");

        // DEV-21577: In-world SLURLs containing "(" or ")" are not treated as a hyperlink in chat
        require_url("Region with brackets", url,
                  "XXX secondlife:///app/region/Burning%20Life%20(Hyper)/27/210/30 XXX",
                  "secondlife:///app/region/Burning%20Life%20(Hyper)/27/210/30");

        // Rendering tests.
        require_label("Render /app/region/Ahern/50/50/50/", url,
            "secondlife:///app/region/Ahern/50/50/50/",
            "Ahern (50,50,50)");

        require_label("Render /app/region/Ahern/50/50/50", url,
            "secondlife:///app/region/Ahern/50/50/50",
            "Ahern (50,50,50)");

        require_label("Render /app/region/Ahern/50/50/", url,
            "secondlife:///app/region/Ahern/50/50/",
            "Ahern (50,50)");

        require_label("Render /app/region/Ahern/50/50", url,
            "secondlife:///app/region/Ahern/50/50",
            "Ahern (50,50)");

        require_label("Render /app/region/Ahern/50/", url,
            "secondlife:///app/region/Ahern/50/",
            "Ahern (50)");

        require_label("Render /app/region/Ahern/50", url,
            "secondlife:///app/region/Ahern/50",
            "Ahern (50)");

        require_label("Render /app/region/Ahern/", url,
            "secondlife:///app/region/Ahern/",
            "Ahern");

        require_label("Render /app/region/Ahern/ within context", url,
            "XXX secondlife:///app/region/Ahern/ XXX",
            "Ahern");

        require_label("Render /app/region/Ahern", url,
            "secondlife:///app/region/Ahern",
            "Ahern");

        require_label("Render /app/region/Ahern within context", url,
            "XXX secondlife:///app/region/Ahern XXX",
            "Ahern");

        require_label("Render /app/region/Product%20Engine/", url,
            "secondlife:///app/region/Product%20Engine/",
            "Product Engine");

        require_label("Render /app/region/Product%20Engine", url,
            "secondlife:///app/region/Product%20Engine",
            "Product Engine");

        // Location parsing texts.
        require_location("Location /app/region/Ahern/50/50/50/", url,
            "secondlife:///app/region/Ahern/50/50/50/",
            "Ahern");

        require_location("Location /app/region/Product%20Engine", url,
            "secondlife:///app/region/Product%20Engine",
            "Product Engine");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryEmail finds e-mail addresses as mailto urls", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryemail - general emails
        //
        LLUrlEntryEmail url;

        // Regex tests.
        require_url("match e-mail addresses", url,
                  "test@lindenlab.com",
                  "mailto:test@lindenlab.com");

        require_url("match e-mail addresses with mailto: prefix", url,
                  "mailto:test@lindenlab.com",
                  "mailto:test@lindenlab.com");

        require_url("match e-mail addresses with different domains", url,
                  "test@foo.org.us",
                  "mailto:test@foo.org.us");

        require_url("match e-mail addresses with different domains", url,
                  "test@foo.bar",
                  "mailto:test@foo.bar");

        require_url("don't match incorrect e-mail addresses", url,
                  "test @foo.com",
                  "");

        require_url("don't match incorrect e-mail addresses", url,
                  "test@ foo.com",
                  "");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntrySecondlifeURL finds Linden domain urls that have a protocol", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntrySecondlifeURL - http://*.secondlife.com/* and http://*lindenlab.com/* urls
        //
        LLUrlEntrySecondlifeURL url;

        require_url("match urls with protocol", url,
                  "this url should match http://lindenlab.com/products/second-life",
                  "http://lindenlab.com/products/second-life");

        require_url("match urls with protocol", url,
                  "search something https://marketplace.secondlife.com/products/search on marketplace and test the https",
                  "https://marketplace.secondlife.com/products/search");

        require_url("match HTTPS urls with port", url,
                  "let's specify some port https://secondlife.com:888/status",
                  "https://secondlife.com:888/status");

        require_url("don't match HTTP urls with port", url,
                  "let's specify some port for HTTP http://secondlife.com:888/status",
                  "");

        require_url("don't match urls w/o protocol", url,
                  "looks like an url something www.marketplace.secondlife.com/products but no https prefix",
                  "");

        require_url("but with a protocol www is fine", url,
                  "so let's add a protocol https://www.marketplace.secondlife.com:8888/products",
                  "https://www.marketplace.secondlife.com:8888/products");

        require_url("don't match urls w/o protocol", url,
                  "and even no www something secondlife.com/status",
                  "");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntrySimpleSecondlifeURL finds a bare secondlife or lindenlab host", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntrySimpleSecondlifeURL - http://*.secondlife.com and http://*lindenlab.com urls
        //

        LLUrlEntrySimpleSecondlifeURL url;

        require_url("match urls with a protocol", url,
                  "this url should match http://lindenlab.com",
                  "http://lindenlab.com");

        require_url("match urls with a protocol", url,
                  "search something https://marketplace.secondlife.com on marketplace and test the https",
                  "https://marketplace.secondlife.com");

        require_url("don't match urls w/o protocol", url,
                  "looks like an url something www.marketplace.secondlife.com but no https prefix",
                  "");

        require_url("but with a protocol www is fine", url,
                  "so let's add a protocol http://www.marketplace.secondlife.com",
                  "http://www.marketplace.secondlife.com");

        require_url("don't match urls w/o protocol", url,
                  "and even no www something lindenlab.com",
                  "");

        // Only a host that ends where the text or a space does: anything
        // longer is LLUrlEntrySecondlifeURL's or LLUrlEntryHTTP's.
        require_url("a host at the end of the text", url,
                  "go to http://secondlife.com",
                  "http://secondlife.com");

        require_url("a host at the end of a line", url,
                  "go to https://www.secondlife.com\nand look",
                  "https://www.secondlife.com");

        require_url("a host with a tab after it", url,
                  "go to http://secondlifegrid.net\tnow",
                  "http://secondlifegrid.net");

        require_url("not a host with a path", url,
                  "see http://secondlife.com/foo here",
                  "");

        require_url("not a host with a port", url,
                  "at https://secondlife.com:888 now",
                  "");

        require_url("not a host that goes on", url,
                  "at https://secondlife.com.example.net x",
                  "");

        require_url("not a host with a full stop after it", url,
                  "visit http://secondlife.com.",
                  "");
    }

    TEST_CASE("LLUrlEntry: LLUrlEntryIPv6 finds bracketed IPv6 urls with a protocol", "[LLUrlEntry]")
    {
        //
        // test LLUrlEntryIPv6
        //
        LLUrlEntryIPv6 url;

        // Regex tests.
        require_url("match urls with a protocol", url,
            "this url should match http://[::1]",
            "http://[::1]");

        require_url("match urls with a protocol and query", url,
            "this url should match http://[::1]/file.mp3",
            "http://[::1]/file.mp3");

        require_url("match urls with a protocol", url,
            "this url should match http://[2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d]",
            "http://[2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d]");

        require_url("match urls with port", url,
            "let's specify some port http://[2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d]:8080",
            "http://[2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d]:8080");

        require_url("don't match urls w/o protocol", url,
            "looks like an url something [2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d] but no https prefix",
            "");

        require_url("don't match incorrect urls", url,
            "http://[ 2001:0db8:11a3:09d7:1f34:8a2e:07a0:765d ]",
            "");
    }
}
