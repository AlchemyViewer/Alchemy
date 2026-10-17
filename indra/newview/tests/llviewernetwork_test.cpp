/**
 * @file llviewernetwork_test.cpp
 * @author Roxie
 * @date 2009-03-9
 * @brief Test the viewernetwork functionality
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
#include "../../llxml/llcontrol.h"
#include "llfile.h"

namespace
{

// Should not collide with other test programs creating temp files.
static const char * const TEST_FILENAME("llviewernetwork_test.xml");

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
    std::string grid_label = std::string();
    if(xml_desc == "AgniGridLabel")
    {
        grid_label = "Second Life Main Grid (Agni)";
    }
    else if(xml_desc == "AditiGridLabel")
    {
        grid_label = "Second Life Beta Test Grid (Aditi)";
    }

    return grid_label;
}

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

LLControlGroup gSavedSettings("llviewernetwork");

static const char *gSampleGridFile =
    "<?xml version=\"1.0\"?>"
    "<llsd>"
    "  <map>"
    "    <key>altgrid.long.name</key>"
    "    <map>"
    "      <key>helper_uri</key><string>https://helper1/helpers/</string>"
    "      <key>label</key><string>Alternative Grid</string>"
    "      <key>login_page</key><string>altgrid/loginpage</string>"
    "      <key>login_uri</key>"
    "      <array>"
    "        <string>altgrid/myloginuri1</string>"
    "        <string>altgrid/myloginuri2</string>"
    "      </array>"
    "      <key>keyname</key><string>altgrid.long.name</string>"
    "      <key>credential_type</key><string>agent</string>"
    "      <key>grid_login_id</key><string>AltGrid</string>"
    "    </map>"
    "    <key>minimal.long.name</key>"
    "    <map>"
    "      <key>keyname</key><string>minimal.long.name</string>"
    "    </map>"
    "    <!-- Note that the values for agni and aditi below are deliberately"
    "         incorrect to test that they are not overwritten -->"
    "    <key>util.agni.lindenlab.com</key> <!-- conflict -->"
    "    <map>"
    "      <key>helper_uri</key><string>https://helper1/helpers/</string>"
    "      <key>grid_login_id</key><string>mylabel</string>"
    "      <key>label</key><string>mylabel</string>"
    "      <key>login_page</key><string>loginpage</string>"
    "      <key>login_uri</key>"
    "      <array>"
    "        <string>myloginuri</string>"
    "      </array>"
    "      <key>keyname</key><string>util.agni.lindenlab.com</string> <!-- conflict -->"
    "    </map>"
    "    <key>util.foobar.lindenlab.com</key>"
    "    <map>"
    "      <key>helper_uri</key><string>https://helper1/helpers/</string>"
    "      <key>grid_login_id</key><string>Aditi</string> <!-- conflict -->"
    "      <key>label</key><string>mylabel</string>"
    "      <key>login_page</key><string>loginpage</string>"
    "      <key>login_uri</key>"
    "      <array>"
    "        <string>myloginuri</string>"
    "      </array>"
    "      <key>update_query_url_base</key><string>https://update.secondlife.com/update</string>"
    "      <key>keyname</key><string>util.foobar.lindenlab.com</string>"
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
  struct viewerNetworkTest
    {
        viewerNetworkTest()
        {
            LLGridManager::createInstance();
            LLFile::remove(TEST_FILENAME);
            gCmdLineLoginURI.clear();
            gCmdLineGridChoice.clear();
            gCmdLineHelperURI.clear();
            gLoginPage.clear();
            gCurrentGrid.clear();
        }
        ~viewerNetworkTest()
        {
            LLFile::remove(TEST_FILENAME);
            LLGridManager::deleteSingleton();
        }
    };

    // ---------------------------------------------------------------------------------------
    // Test functions
    // ---------------------------------------------------------------------------------------
    // initialization without a grid file
    TEST_CASE_METHOD(viewerNetworkTest, "LLViewerNetwork: without a grid file Agni and Aditi are the known grids", "[LLViewerNetwork]")
    {
        LLGridManager *manager = LLGridManager::getInstance();
        // grid file doesn't exist
        manager->initialize(TEST_FILENAME);
        // validate that some of the defaults are available.
        std::map<std::string, std::string> known_grids = manager->getKnownGrids();
        REQUIRE(known_grids.size() == 2);
        CHECK(known_grids[std::string("util.agni.lindenlab.com")] ==
                      std::string("Second Life Main Grid (Agni)"));
        CHECK(known_grids[std::string("util.aditi.lindenlab.com")] ==
                      std::string("Second Life Beta Test Grid (Aditi)"));
        CHECK(LLGridManager::getInstance()->getGrid("util.agni.lindenlab.com") ==
                      std::string("util.agni.lindenlab.com"));
        CHECK(std::string("Agni") ==
                      LLGridManager::getInstance()->getGridId("util.agni.lindenlab.com"));
        UNSCOPED_INFO("update url base for Agni");
        CHECK(std::string("https://update.secondlife.com/update") ==
                      LLGridManager::getInstance()->getUpdateServiceURL());
        CHECK(LLGridManager::getInstance()->getGridLabel("util.agni.lindenlab.com") ==
                      std::string("Second Life Main Grid (Agni)"));

        std::vector<std::string> login_uris;
        LLGridManager::getInstance()->getLoginURIs(std::string("util.agni.lindenlab.com"), login_uris);
        REQUIRE(1 == login_uris.size());
        CHECK(login_uris[0] ==
                      std::string("https://login.agni.lindenlab.com/cgi-bin/login.cgi"));
        CHECK(LLGridManager::getInstance()->getHelperURI("util.agni.lindenlab.com") ==
                      std::string("https://secondlife.com/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage("util.agni.lindenlab.com") ==
                      std::string("https://viewer-splash-v2.secondlife.com/"));
        CHECK(LLGridManager::getInstance()->isSystemGrid("util.agni.lindenlab.com"));

        CHECK(LLGridManager::getInstance()->getGrid("util.aditi.lindenlab.com") ==
                      std::string("util.aditi.lindenlab.com"));
        CHECK(LLGridManager::getInstance()->getGridId("util.aditi.lindenlab.com") ==
                      std::string("Aditi"));
        CHECK(LLGridManager::getInstance()->getGridLabel("util.aditi.lindenlab.com") ==
                      std::string("Second Life Beta Test Grid (Aditi)"));

        LLGridManager::getInstance()->getLoginURIs(std::string("util.aditi.lindenlab.com"), login_uris);

        REQUIRE(1 == login_uris.size());
        CHECK(login_uris[0] ==
                      std::string("https://login.aditi.lindenlab.com/cgi-bin/login.cgi"));
        CHECK(LLGridManager::getInstance()->getHelperURI("util.aditi.lindenlab.com") ==
                      std::string("https://secondlife.aditi.lindenlab.com/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage("util.aditi.lindenlab.com") ==
                      std::string("https://viewer-splash-v2.secondlife.com/"));
        CHECK(LLGridManager::getInstance()->isSystemGrid("util.aditi.lindenlab.com"));
    }

    // initialization with a grid file
    TEST_CASE_METHOD(viewerNetworkTest, "LLViewerNetwork: a grid file adds its grids without overwriting Agni or Aditi", "[LLViewerNetwork]")
    {
        llofstream gridfile(TEST_FILENAME);
        gridfile << gSampleGridFile;
        gridfile.close();

        LLGridManager::getInstance()->initialize(TEST_FILENAME);
        std::map<std::string, std::string> known_grids = LLGridManager::getInstance()->getKnownGrids();
        UNSCOPED_INFO("adding a grid via a grid file increases known grid size");
        REQUIRE(4 == known_grids.size());

        // Verify that Agni and Aditi were not overwritten
        CHECK(known_grids[std::string("util.agni.lindenlab.com")] ==
                      std::string("Second Life Main Grid (Agni)"));
        CHECK(known_grids[std::string("util.aditi.lindenlab.com")] ==
                      std::string("Second Life Beta Test Grid (Aditi)"));
        CHECK(LLGridManager::getInstance()->getGrid("util.agni.lindenlab.com") ==
                      std::string("util.agni.lindenlab.com"));
        CHECK(LLGridManager::getInstance()->getGridId("util.agni.lindenlab.com") ==
                      std::string("Agni"));
        UNSCOPED_INFO("update url base for Agni");
        CHECK(std::string("https://update.secondlife.com/update") ==
                      LLGridManager::getInstance()->getUpdateServiceURL());
        CHECK(LLGridManager::getInstance()->getGridLabel("util.agni.lindenlab.com") ==
                      std::string("Second Life Main Grid (Agni)"));
        std::vector<std::string> login_uris;
        LLGridManager::getInstance()->getLoginURIs(std::string("util.agni.lindenlab.com"), login_uris);
        REQUIRE(1 == login_uris.size());
        CHECK(login_uris[0] ==
                      std::string("https://login.agni.lindenlab.com/cgi-bin/login.cgi"));
        CHECK(LLGridManager::getInstance()->getHelperURI("util.agni.lindenlab.com") ==
                      std::string("https://secondlife.com/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage("util.agni.lindenlab.com") ==
                      std::string("https://viewer-splash-v2.secondlife.com/"));
        CHECK(LLGridManager::getInstance()->isSystemGrid("util.agni.lindenlab.com"));

        CHECK(LLGridManager::getInstance()->getGrid("util.aditi.lindenlab.com") ==
                      std::string("util.aditi.lindenlab.com"));
        CHECK(LLGridManager::getInstance()->getGridId("util.aditi.lindenlab.com") ==
                      std::string("Aditi"));
        CHECK(LLGridManager::getInstance()->getGridLabel("util.aditi.lindenlab.com") ==
                      std::string("Second Life Beta Test Grid (Aditi)"));

        LLGridManager::getInstance()->getLoginURIs(std::string("util.aditi.lindenlab.com"), login_uris);
        REQUIRE(1 == login_uris.size());
        CHECK(login_uris[0] ==
                      std::string("https://login.aditi.lindenlab.com/cgi-bin/login.cgi"));
        CHECK(LLGridManager::getInstance()->getHelperURI("util.aditi.lindenlab.com") ==
                      std::string("https://secondlife.aditi.lindenlab.com/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage("util.aditi.lindenlab.com") ==
                      std::string("https://viewer-splash-v2.secondlife.com/"));
        CHECK(LLGridManager::getInstance()->isSystemGrid("util.aditi.lindenlab.com"));

        // Check the additional grid from the file
        UNSCOPED_INFO("alternative grid is in name<->label map");
        CHECK(known_grids["altgrid.long.name"] ==
                      std::string("Alternative Grid"));
        CHECK(LLGridManager::getInstance()->getGrid("altgrid.long.name") ==
                      std::string("altgrid.long.name"));
        CHECK(LLGridManager::getInstance()->getGridId("altgrid.long.name") ==
                      std::string("AltGrid"));
        CHECK(LLGridManager::getInstance()->getGridLabel("altgrid.long.name") ==
                      std::string("Alternative Grid"));
        std::vector<std::string> alt_login_uris;
        LLGridManager::getInstance()->getLoginURIs(std::string("altgrid.long.name"), alt_login_uris);
        REQUIRE(2 == alt_login_uris.size());
        CHECK(alt_login_uris[0] ==
                      std::string("altgrid/myloginuri1"));
        CHECK(alt_login_uris[1] ==
                      std::string("altgrid/myloginuri2"));
        CHECK(LLGridManager::getInstance()->getHelperURI("altgrid.long.name") ==
                      std::string("https://helper1/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage("altgrid.long.name") ==
                      std::string("altgrid/loginpage"));
        CHECK_FALSE(LLGridManager::getInstance()->isSystemGrid("altgrid.long.name"));

        UNSCOPED_INFO("minimal grid is in name<->label map");
        CHECK(known_grids["minimal.long.name"] ==
                      std::string("minimal.long.name"));
        CHECK(LLGridManager::getInstance()->getGrid("minimal.long.name") ==
                      std::string("minimal.long.name"));
        CHECK(LLGridManager::getInstance()->getGridId("minimal.long.name") ==
                      std::string("minimal.long.name"));
        CHECK(LLGridManager::getInstance()->getGridLabel("minimal.long.name") ==
                      std::string("minimal.long.name"));

        LLGridManager::getInstance()->getLoginURIs(std::string("minimal.long.name"), alt_login_uris);
        REQUIRE(1 == alt_login_uris.size());
        CHECK(alt_login_uris[0] ==
                      std::string("https://minimal.long.name/cgi-bin/login.cgi"));
        CHECK(LLGridManager::getInstance()->getHelperURI("minimal.long.name") ==
                      std::string("https://minimal.long.name/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage("minimal.long.name") ==
                      std::string("https://minimal.long.name/app/login/"));

    }


    // validate grid selection
    TEST_CASE_METHOD(viewerNetworkTest, "LLViewerNetwork: setGridChoice selects the grid the getters answer for", "[LLViewerNetwork]")
    {
        // The sample grid file, for a grid besides Agni and Aditi to choose.
        llofstream gridfile(TEST_FILENAME);
        gridfile << gSampleGridFile;
        gridfile.close();

        LLGridManager::getInstance()->initialize(TEST_FILENAME);

        LLGridManager::getInstance()->setGridChoice("util.agni.lindenlab.com");
        CHECK(LLGridManager::getInstance()->getGridLabel() ==
                      std::string("Second Life Main Grid (Agni)"));
        CHECK(LLGridManager::getInstance()->getGridId() ==
                      std::string("Agni"));
        CHECK(LLGridManager::getInstance()->getGrid() ==
                      std::string("util.agni.lindenlab.com"));
        CHECK(LLGridManager::getInstance()->getHelperURI() ==
                      std::string("https://secondlife.com/helpers/"));
        CHECK(LLGridManager::getInstance()->getLoginPage() ==
                      std::string("https://viewer-splash-v2.secondlife.com/"));
        UNSCOPED_INFO("update url base for Agni");
        CHECK(std::string("https://update.secondlife.com/update") ==
                      LLGridManager::getInstance()->getUpdateServiceURL());
        UNSCOPED_INFO("Agni is a production grid");
        CHECK(LLGridManager::getInstance()->isInProductionGrid());
        std::vector<std::string> uris;
        LLGridManager::getInstance()->getLoginURIs(uris);
        REQUIRE(1 == uris.size());
        CHECK(uris[0] ==
                      std::string("https://login.agni.lindenlab.com/cgi-bin/login.cgi"));

        LLGridManager::getInstance()->setGridChoice("altgrid.long.name");
        CHECK(LLGridManager::getInstance()->getGridLabel() ==
                      std::string("Alternative Grid"));
        CHECK(LLGridManager::getInstance()->getGridId() ==
                      std::string("AltGrid"));
        UNSCOPED_INFO("alternative grid is not a system grid");
        CHECK_FALSE(LLGridManager::getInstance()->isSystemGrid());
        UNSCOPED_INFO("alternative grid is not a production grid");
        CHECK_FALSE(LLGridManager::getInstance()->isInProductionGrid());
    }

}
