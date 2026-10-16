/**
 * @file llsecapi_test.cpp
 * @author Roxie
 * @date 2009-02-10
 * @brief Test the sec api functionality
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2014, Linden Research, Inc.
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
#include "../llviewerprecompiledheaders.h"
#include "../llviewernetwork.h"
#include <catch2/catch_test_macros.hpp>
#include "../llslurl.h"
#include "../../llxml/llcontrol.h"
#include "llsdserialize.h"

namespace
{

// Should not collide with other test programs creating temp files.
static const char * const TEST_FILENAME("llslurl_test.xml");

}

//
// Stub implementation for LLTrans
//
class LLTrans
{
public:
    static std::string getString(std::string_view xml_desc, const LLStringUtil::format_map_t& args, bool def_string = false);
};

std::string LLTrans::getString(std::string_view xml_desc, const LLStringUtil::format_map_t& args, bool def_string)
{
    return std::string();
}

// [RLVa:KB] - Checked: 2010-11-12 (RLVa-1.2.2a) | Added: RLVa-1.2.2a
// Stub implementation to get the test to compile properly
#include "../rlvhandler.h"

const std::string& RlvStrings::getString(std::string_view strStringName)
{
    static const std::string strMissing = "(Missing RLVa string)";
    return strMissing;
}

bool RlvUtil::isNearbyRegion(const std::string& strRegion)
{
    return false;
}

RlvHandler::RlvHandler() : m_pGCTimer(nullptr)
{
    // Array auto-initialization to 0 is non-standard? (Compiler warning in VC-8.0)
    memset(m_Behaviours, 0, sizeof(S16) * RLV_BHVR_COUNT);
}

RlvHandler::~RlvHandler()
{
}

void RlvHandler::changed(const LLUUID& idGroup, LLGroupChange change)
{}

bool RlvHandler::handleEvent(LLPointer<LLOldEvents::LLEvent> event, const LLSD& sdUserdata)
{
    return true;
}

RlvHandler gRlvHandler;
// [/RLVa:KB]

//----------------------------------------------------------------------------
// Mock objects for the dependencies of the code we're testing

LLControlGroup::LLControlGroup(const std::string& name)
: LLInstanceTracker<LLControlGroup, std::string>(name) {}
LLControlGroup::~LLControlGroup() {}
LLControlVariable* LLControlGroup::declareString(const std::string& name,
                                   const std::string& initial_val,
                                   const std::string& comment,
                                   LLControlVariable::ePersist persist) {return NULL;}
void LLControlGroup::setString(std::string_view name, const std::string& val){}

static std::string gCmdLineLoginURI;
static std::string gCmdLineGridChoice;
static std::string gCmdLineHelperURI;
static std::string gLoginPage;
static std::string gCurrentGrid;
std::string LLControlGroup::getString(std::string_view name)
{
    if (name == "CmdLineGridChoice")
        return gCmdLineGridChoice;
    else if (name == "CmdLineHelperURI")
        return gCmdLineHelperURI;
    else if (name == "LoginPage")
        return gLoginPage;
    else if (name == "CurrentGrid")
        return gCurrentGrid;
    return "";
}

LLSD LLControlGroup::getLLSD(std::string_view name)
{
    if (name == "CmdLineLoginURI")
    {
        if(!gCmdLineLoginURI.empty())
        {
            return LLSD(gCmdLineLoginURI);
        }
    }
    return LLSD();
}

LLPointer<LLControlVariable> LLControlGroup::getControl(std::string_view name)
{
    ctrl_name_table_t::iterator iter = mNameTable.find(name.data());
    return iter == mNameTable.end() ? LLPointer<LLControlVariable>() : iter->second;
}

LLControlGroup gSavedSettings("llslurl");
static const char *gSampleGridFile =
    "<?xml version=\"1.0\"?>"
    "<llsd>"
    "  <map>"
    "    <key>foo.bar.com</key>"
    "    <map>"
    "      <key>helper_uri</key><string>https://foobar/helpers/</string>"
    "      <key>label</key><string>Foobar Grid</string>"
    "      <key>login_page</key><string>foobar/loginpage</string>"
    "      <key>login_uri</key>"
    "      <array>"
    "        <string>foobar/loginuri</string>"
    "      </array>"
    "      <key>keyname</key><string>foo.bar.com</string>"
    "      <key>credential_type</key><string>agent</string>"
    "      <key>grid_login_id</key><string>FooBar</string>"
    "    </map>"
    "    <key>my.grid.com</key>"
    "    <map>"
    "      <key>helper_uri</key><string>https://mygrid/helpers/</string>"
    "      <key>label</key><string>My Grid</string>"
    "      <key>login_page</key><string>mygrid/loginpage</string>"
    "      <key>login_uri</key>"
    "      <array>"
    "        <string>mygrid/loginuri</string>"
    "      </array>"
    "      <key>keyname</key><string>my.grid.com</string>"
    "      <key>credential_type</key><string>agent</string>"
    "      <key>grid_login_id</key><string>MyGrid</string>"
    "    </map>"
    "  </map>"
    "</llsd>"
    ;

// -------------------------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------------------------
namespace
{
    // Test wrapper declaration : wrapping nothing for the moment
    struct slurlTest
    {
        slurlTest()
        {
            LLGridManager::createInstance();
            LLGridManager::getInstance()->initialize(std::string(""));
        }
        ~slurlTest()
        {
            LLGridManager::deleteSingleton();
        }
    };

    // ---------------------------------------------------------------------------------------
    // Test functions
    // ---------------------------------------------------------------------------------------
    TEST_CASE_METHOD(slurlTest, "LLSlurl: construction from each form of SLURL string", "[LLSlurl]")
    {
        llofstream gridfile(TEST_FILENAME);
        gridfile << gSampleGridFile;
        gridfile.close();

        LLGridManager::getInstance()->initialize(TEST_FILENAME);

        LLGridManager::getInstance()->setGridChoice("util.agni.lindenlab.com");

        LLSLURL slurl = LLSLURL("");
        UNSCOPED_INFO("null slurl");
        CHECK((int)slurl.getType() == LLSLURL::LAST_LOCATION);

        slurl = LLSLURL("https://slurl.com/secondlife/myregion");
        {
            INFO("slurl.com slurl, region only");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/myregion/128/128/0");
        }

        slurl = LLSLURL("https://maps.secondlife.com/secondlife/myregion/1/2/3");
        {
            INFO("maps.secondlife.com slurl, region + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/myregion/1/2/3");
        }

        slurl = LLSLURL("secondlife://");
        UNSCOPED_INFO("secondlife: slurl, empty");
        CHECK(slurl.getType() == LLSLURL::EMPTY);

        slurl = LLSLURL("secondlife:///");
        UNSCOPED_INFO("secondlife: slurl, root");
        CHECK(slurl.getType() == LLSLURL::EMPTY);

        slurl = LLSLURL("secondlife://myregion");
        {
            INFO("secondlife: slurl, region only");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/myregion/128/128/0");
        }

        slurl = LLSLURL("secondlife://myregion/1/2/3");
        {
            INFO("secondlife: slurl, region + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/myregion/1/2/3");
        }

        slurl = LLSLURL("/myregion");
        {
            INFO("/region slurl, region only");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/myregion/128/128/0");
        }

        slurl = LLSLURL("/myregion/1/2/3");
        {
            INFO("/region slurl, region + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/myregion/1/2/3");
        }

        slurl = LLSLURL("my region/1/2/3");
        {
            INFO("bare slurl, region with a space + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/my%20region/1/2/3");
        }

        LLGridManager::getInstance()->setGridChoice("my.grid.com");
        slurl = LLSLURL("https://my.grid.com/region/my%20region/1/2/3");
        {
            INFO("grid slurl, region + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://my.grid.com/region/my%20region/1/2/3");
        }

        slurl = LLSLURL("https://my.grid.com/region/my region");
        {
            INFO("grid slurl, region only with an unescaped space");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://my.grid.com/region/my%20region/128/128/0");
        }

        LLGridManager::getInstance()->setGridChoice("foo.bar.com");
        slurl = LLSLURL("/myregion/1/2/3");
        {
            INFO("/region slurl on foo.bar.com, region + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://foo.bar.com/region/myregion/1/2/3");
        }

        slurl = LLSLURL("myregion/1/2/3");
        {
            INFO("bare slurl on foo.bar.com, region + coords");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://foo.bar.com/region/myregion/1/2/3");
        }

        slurl = LLSLURL(LLSLURL::SIM_LOCATION_HOME);
        CHECK(slurl.getType() == LLSLURL::HOME_LOCATION);

        slurl = LLSLURL(LLSLURL::SIM_LOCATION_LAST);
        CHECK(slurl.getType() == LLSLURL::LAST_LOCATION);

        slurl = LLSLURL("secondlife:///app/foo/bar?12345");
        {
            INFO("app slurl naming no grid");
            CHECK(slurl.getType() == LLSLURL::APP);
            CHECK(slurl.getAppCmd() == "foo");
            REQUIRE(slurl.getAppPath().size() == 1);
            CHECK(slurl.getAppPath()[0].asString() == "bar");
            CHECK(slurl.getAppQuery() == "12345");
            CHECK(slurl.getGrid() == "FooBar");
        }

        slurl = LLSLURL("secondlife://Aditi/app/foo/bar?12345");
        {
            INFO("app slurl naming Aditi");
            CHECK(slurl.getType() == LLSLURL::APP);
            CHECK(slurl.getAppCmd() == "foo");
            REQUIRE(slurl.getAppPath().size() == 1);
            CHECK(slurl.getAppPath()[0].asString() == "bar");
            CHECK(slurl.getAppQuery() == "12345");
            CHECK(slurl.getGrid() == "Aditi");
        }

        LLGridManager::getInstance()->setGridChoice("foo.bar.com");
        slurl = LLSLURL("secondlife:///secondlife/myregion/1/2/3");
        {
            INFO("secondlife:///secondlife/ slurl naming no grid");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK("myregion" == slurl.getRegion());
            CHECK(slurl.getPosition() == LLVector3(1, 2, 3));
            CHECK(slurl.getGrid() == "util.agni.lindenlab.com");
        }

        slurl = LLSLURL("secondlife://Aditi/secondlife/myregion/1/2/3");
        {
            INFO("secondlife://Aditi/secondlife/ slurl");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK("myregion" == slurl.getRegion());
            CHECK(slurl.getPosition() == LLVector3(1, 2, 3));
            CHECK(slurl.getGrid() == "Aditi");
        }

        LLGridManager::getInstance()->setGridChoice("my.grid.com");
        slurl = LLSLURL("https://my.grid.com/app/foo/bar?12345");
        {
            INFO("app slurl on my.grid.com");
            CHECK(slurl.getType() == LLSLURL::APP);
            CHECK(slurl.getAppCmd() == "foo");
            REQUIRE(slurl.getAppPath().size() == 1);
            CHECK(slurl.getAppPath()[0].asString() == "bar");
            CHECK(slurl.getAppQuery() == "12345");
        }

    }

    TEST_CASE_METHOD(slurlTest, "LLSlurl: construction from grid/region/vector combos", "[LLSlurl]")
    {
        llofstream gridfile(TEST_FILENAME);
        gridfile << gSampleGridFile;
        gridfile.close();

        LLGridManager::getInstance()->initialize(TEST_FILENAME);

        LLSLURL slurl = LLSLURL("my.grid.com", "my region");
        {
            INFO("grid/region");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://my.grid.com/region/my%20region/128/128/0");
        }

        slurl = LLSLURL("my.grid.com", "my region", LLVector3(1,2,3));
        {
            INFO("grid/region/vector");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://my.grid.com/region/my%20region/1/2/3");
        }

        LLGridManager::getInstance()->setGridChoice("util.agni.lindenlab.com");
        slurl = LLSLURL("my region", LLVector3(1,2,3));
        {
            INFO("region/vector on the agni grid");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://maps.secondlife.com/secondlife/my%20region/1/2/3");
        }

        LLGridManager::getInstance()->setGridChoice("MyGrid");
        slurl = LLSLURL("my region", LLVector3(1,2,3));
        {
            INFO("region/vector on the grid chosen by its login id");
            CHECK(slurl.getType() == LLSLURL::LOCATION);
            CHECK(slurl.getSLURLString() ==
                          "https://my.grid.com/region/my%20region/1/2/3");
        }

    }

    TEST_CASE_METHOD(slurlTest, "LLSlurl: the accessors give the login and location strings and the parts", "[LLSlurl]")
    {
        llofstream gridfile(TEST_FILENAME);
        gridfile << gSampleGridFile;
        gridfile.close();

        LLGridManager::getInstance()->initialize(TEST_FILENAME);

        LLGridManager::getInstance()->setGridChoice("my.grid.com");
        LLSLURL slurl = LLSLURL("https://my.grid.com/region/my%20region/1/2/3");
        CHECK(slurl.getLoginString() == "uri:my region&amp;1&amp;2&amp;3");
        CHECK(slurl.getLocationString() == "my region/1/2/3");
        CHECK(slurl.getGrid() == "my.grid.com");
        CHECK(slurl.getRegion() == "my region");
        CHECK(slurl.getPosition() == LLVector3(1, 2, 3));

    }
}
