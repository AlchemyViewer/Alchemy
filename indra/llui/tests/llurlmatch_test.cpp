/**
 * @file llurlmatch_test.cpp
 * @author Martin Reddy
 * @brief Unit tests for LLUrlMatch
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

#include "../llurlmatch.h"
#include "../llrender/lluiimage.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // A match is given a style, and a style's parameters start from the
    // default font, so the fonts are loaded first.
    struct LLUrlMatchData
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
    };

}

namespace
{
    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: a match is empty until its values are set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the empty() method
        //
        LLUrlMatch match;
        CHECK(match.empty());

        match.setValues(0, 1, "http://secondlife.com", "", "Second Life", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK_FALSE(match.empty());
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getStart is 0 by default and then the start set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getStart() method
        //
        LLUrlMatch match;
        CHECK(match.getStart() == 0);

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getStart() == 10);
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getEnd is 0 by default and then the end set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getEnd() method
        //
        LLUrlMatch match;
        CHECK(match.getEnd() == 0);

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getEnd() == 20);
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getUrl is empty by default and then the url last set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getUrl() method
        //
        LLUrlMatch match;
        CHECK(match.getUrl() == "");

        match.setValues(10, 20, "http://slurl.com/", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getUrl() == "http://slurl.com/");

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getUrl() == "");
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getLabel is empty by default and then the label last set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getLabel() method
        //
        LLUrlMatch match;
        CHECK(match.getLabel() == "");

        match.setValues(10, 20, "", "Label", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getLabel() == "Label");

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getLabel() == "");
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getTooltip is empty by default and then the tooltip last set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getTooltip() method
        //
        LLUrlMatch match;
        CHECK(match.getTooltip() == "");

        match.setValues(10, 20, "", "", "", "Info", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getTooltip() == "Info");

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getTooltip() == "");
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getIcon is empty by default and then the icon last set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getIcon() method
        //
        LLUrlMatch match;
        CHECK(match.getIcon() == "");

        match.setValues(10, 20, "", "", "", "", "Icon", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getIcon() == "Icon");

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getIcon() == "");
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getMenuName is empty by default and then the menu last set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getMenuName() method
        //
        LLUrlMatch match;
        CHECK(match.getMenuName().empty());

        match.setValues(10, 20, "", "", "", "", "Icon", LLStyle::Params(), "xui_file.xml", "", LLUUID::null);
        CHECK(match.getMenuName() == "xui_file.xml");

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getMenuName().empty());
    }

    TEST_CASE_METHOD(LLUrlMatchData, "LLUrlMatch: getLocation is empty by default and then the location last set", "[LLUrlMatch]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        //
        // test the getLocation() method
        //
        LLUrlMatch match;
        CHECK(match.getLocation().empty());

        match.setValues(10, 20, "", "", "", "", "Icon", LLStyle::Params(), "xui_file.xml", "Paris", LLUUID::null);
        CHECK(match.getLocation() == "Paris");

        match.setValues(10, 20, "", "", "", "", "", LLStyle::Params(), "", "", LLUUID::null);
        CHECK(match.getLocation().empty());
    }
}
