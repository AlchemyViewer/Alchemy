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
#include "../test/lltut.h"
#include "../llslurl.h"
#include "llxmlnode.h"
#include "llcontrol.h"
#include "llnotificationsutil.h"
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
	static void setDefaultArg(const std::string& name, std::string value);
};

std::string LLTrans::getString(const std::string_view xml_desc, const LLStringUtil::format_map_t& args, bool def_string)
{
	return std::string();
}

void LLTrans::setDefaultArg(const std::string& name, std::string value)
{
}

// [RLVa:KB] - Checked: 2010-11-12 (RLVa-1.2.2a) | Added: RLVa-1.2.2a
// Stub implementation to get the test to compile properly
#include "../rlvhandler.h"

const std::string& RlvStrings::getString(const std::string_view strStringName)
{
	static const std::string strMissing = "(Missing RLVa string)";
	return strMissing;
}

bool RlvUtil::isNearbyRegion(const std::string& strRegion)
{
	return false;
}

RlvHandler::RlvHandler() : m_pGCTimer(NULL)
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

bool LLXMLNode::parseBuffer(U8* buffer, U32 length, LLXMLNodePtr& node, LLXMLNode* defaults) { return false; }
std::string LLXMLNode::getTextContents() const { return {}; }
LLXMLNodePtr LLXMLNode::getFirstChild() const { return {}; }
LLXMLNodePtr LLXMLNode::getNextSibling() const { return {}; }

LLControlGroup::LLControlGroup(const std::string& name)
: LLInstanceTracker<LLControlGroup, std::string>(name) {}
LLControlGroup::~LLControlGroup() {}
LLControlVariable* LLControlGroup::declareString(const std::string& name,
                                   const std::string& initial_val,
                                   const std::string& comment,
                                   LLControlVariable::ePersist persist) {return NULL;}
void LLControlGroup::setString(const std::string_view name, const std::string& val){}
LLNotificationPtr LLNotificationsUtil::add(const std::string& name, const LLSD& substitutions) { return NULL; }

std::string gCmdLineLoginURI;
std::string gCmdLineGridChoice;
std::string gCmdLineHelperURI;
std::string gLoginPage;
std::string gCurrentGrid;
std::string LLControlGroup::getString(const std::string_view name)
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

LLSD LLControlGroup::getLLSD(const std::string_view name)
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

LLPointer<LLControlVariable> LLControlGroup::getControl(const std::string_view name)
{
	ctrl_name_table_t::iterator iter = mNameTable.find(name);
	return iter == mNameTable.end() ? LLPointer<LLControlVariable>() : iter->second;
}

LLControlGroup gSavedSettings("test");
const char *gSampleGridFile =
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
	"    <key>my.stupidgrid.com:8002</key>"
	"    <map>"
	"      <key>helper_uri</key><string>https://my.stupidgrid.com/helpers/</string>"
	"      <key>label</key><string>My Stupid Grid</string>"
	"      <key>login_page</key><string>my.stupidgrid.com/loginpage</string>"
	"      <key>login_uri</key>"
	"      <array>"
	"        <string>my.stupidgrid.com:8002/</string>"
	"      </array>"
	"      <key>keyname</key><string>my.stupidgrid.com:8002</string>"
	"      <key>credential_type</key><string>agent</string>"
	"      <key>grid_login_id</key><string>My Stupid Grid</string>"
	"    </map>"
	"  </map>"
	"</llsd>"
	;

// -------------------------------------------------------------------------------------------
// TUT
// -------------------------------------------------------------------------------------------
namespace tut
{
	// Test wrapper declaration : wrapping nothing for the moment
	struct slurlTest
	{
		slurlTest()
		{
			LLGridManager::getInstance()->initialize(std::string(""));
		}
		~slurlTest()
		{
		}
	};

	// Tut templating thingamagic: test group, object and test instance
	typedef test_group<slurlTest> slurlTestFactory;
	typedef slurlTestFactory::object slurlTestObject;
	tut::slurlTestFactory tut_test("LLSlurl");

	// ---------------------------------------------------------------------------------------
	// Test functions
	// ---------------------------------------------------------------------------------------
	// construction from slurl string
	template<> template<>
	void slurlTestObject::test<1>()
	{
		llofstream gridfile(TEST_FILENAME);
		gridfile << gSampleGridFile;
		gridfile.close();

		LLGridManager::getInstance()->initialize(TEST_FILENAME);

		LLGridManager::getInstance()->setGridChoice("util.agni.lindenlab.com");

		LLSLURL slurl = LLSLURL("");
		ensure_equals("null slurl", (int)slurl.getType(), LLSLURL::LAST_LOCATION);

		slurl = LLSLURL("http://slurl.com/secondlife/myregion");
		ensure_equals("slurl.com slurl, region only - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("slurl.com slurl, region only", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/myregion/128/128/0");

		slurl = LLSLURL("http://maps.secondlife.com/secondlife/myregion/1/2/3");
		ensure_equals("maps.secondlife.com slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("maps.secondlife.com slurl, region + coords", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/myregion/1/2/3");

		slurl = LLSLURL("secondlife://myregion");
		ensure_equals("secondlife: slurl, region only - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("secondlife: slurl, region only", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/myregion/128/128/0");

		slurl = LLSLURL("secondlife://myregion/1/2/3");
		ensure_equals("secondlife: slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("secondlife slurl, region + coords", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/myregion/1/2/3");

		slurl = LLSLURL("/myregion");
		ensure_equals("/region slurl, region- type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("/region slurl, region ", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/myregion/128/128/0");

		slurl = LLSLURL("/myregion/1/2/3");
		ensure_equals("/: slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("/ slurl, region + coords", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/myregion/1/2/3");

		slurl = LLSLURL("my region/1/2/3");
		ensure_equals(" slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals(" slurl, region + coords", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/my%20region/1/2/3");

		LLGridManager::getInstance()->setGridChoice("my.grid.com");
		slurl = LLSLURL("x-grid-info://my.grid.com/region/my%20region/1/2/3");
		ensure_equals("grid slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("grid slurl, region + coords", slurl.getSLURLString(),
					  "x-grid-info://my.grid.com/region/my%20region/1/2/3");

		slurl = LLSLURL("x-grid-info://my.grid.com/region/my region");
		ensure_equals("grid slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("grid slurl, region + coords", slurl.getSLURLString(),
					  "x-grid-info://my.grid.com/region/my%20region/128/128/0");

		LLGridManager::getInstance()->setGridChoice("foo.bar.com");
		slurl = LLSLURL("/myregion/1/2/3");
		ensure_equals("/: slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("/ slurl, region + coords", slurl.getSLURLString(),
					  "x-grid-info://foo.bar.com/region/myregion/1/2/3");

		slurl = LLSLURL("myregion/1/2/3");
		ensure_equals(": slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals(" slurl, region + coords", slurl.getSLURLString(),
					  "x-grid-info://foo.bar.com/region/myregion/1/2/3");

		slurl = LLSLURL(LLSLURL::SIM_LOCATION_HOME);
		ensure_equals("home", slurl.getType(), LLSLURL::HOME_LOCATION);

		slurl = LLSLURL(LLSLURL::SIM_LOCATION_LAST);
		ensure_equals("last", slurl.getType(), LLSLURL::LAST_LOCATION);

		slurl = LLSLURL("secondlife:///app/foo/bar?12345");
		ensure_equals("app", slurl.getType(), LLSLURL::APP);
		ensure_equals("appcmd", slurl.getAppCmd(), "foo");
		ensure_equals("apppath", slurl.getAppPath().size(), 1);
		ensure_equals("apppath2", slurl.getAppPath()[0].asString(), "bar");
		ensure_equals("appquery", slurl.getAppQuery(), "12345");
		ensure_equals("grid1", slurl.getGrid(), "FooBar");

		slurl = LLSLURL("secondlife://Aditi/app/foo/bar?12345");
		ensure_equals("app", slurl.getType(), LLSLURL::APP);
		ensure_equals("appcmd", slurl.getAppCmd(), "foo");
		ensure_equals("apppath", slurl.getAppPath().size(), 1);
		ensure_equals("apppath2", slurl.getAppPath()[0].asString(), "bar");
		ensure_equals("appquery", slurl.getAppQuery(), "12345");
		ensure_equals("grid2",  slurl.getGrid(), "Aditi");

		LLGridManager::getInstance()->setGridChoice("foo.bar.com");
		slurl = LLSLURL("secondlife:///secondlife/myregion/1/2/3");
		ensure_equals("/: slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("location", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("region" , "myregion", slurl.getRegion());
		ensure_equals("grid3", slurl.getGrid(), "util.agni.lindenlab.com");

		slurl = LLSLURL("secondlife://Aditi/secondlife/myregion/1/2/3");
		ensure_equals("/: slurl, region + coords - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("location", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("region" , "myregion", slurl.getRegion());
		ensure_equals("grid4", slurl.getGrid(), "Aditi" );

		LLGridManager::getInstance()->setGridChoice("my.grid.com");
		slurl = LLSLURL("x-grid-info://my.grid.com/app/foo/bar?12345");
		ensure_equals("app", slurl.getType(), LLSLURL::APP);
		ensure_equals("appcmd", slurl.getAppCmd(), "foo");
		ensure_equals("apppath", slurl.getAppPath().size(), 1);
		ensure_equals("apppath2", slurl.getAppPath()[0].asString(), "bar");
		ensure_equals("appquery", slurl.getAppQuery(), "12345");

	}

	// construction from grid/region/vector combos
	template<> template<>
	void slurlTestObject::test<2>()
	{
		llofstream gridfile(TEST_FILENAME);
		gridfile << gSampleGridFile;
		gridfile.close();

		LLGridManager::getInstance()->initialize(TEST_FILENAME);

		LLSLURL slurl = LLSLURL("my.grid.com", "my region");
		ensure_equals("grid/region - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals("grid/region", slurl.getSLURLString(),
					  "x-grid-info://my.grid.com/region/my%20region/128/128/0");

		slurl = LLSLURL("my.grid.com", "my region", LLVector3(1,2,3));
		ensure_equals("grid/region/vector - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals(" grid/region/vector", slurl.getSLURLString(),
					  "x-grid-info://my.grid.com/region/my%20region/1/2/3");

		LLGridManager::getInstance()->setGridChoice("util.agni.lindenlab.com");
		slurl = LLSLURL("my region", LLVector3(1,2,3));
		ensure_equals("default grid/region/vector - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals(" default grid/region/vector", slurl.getSLURLString(),
					  "http://maps.secondlife.com/secondlife/my%20region/1/2/3");

		LLGridManager::getInstance()->setGridChoice("MyGrid");
		slurl = LLSLURL("my region", LLVector3(1,2,3));
		ensure_equals("default grid/region/vector - type", slurl.getType(), LLSLURL::LOCATION);
		ensure_equals(" default grid/region/vector", slurl.getSLURLString(),
					  "x-grid-info://my.grid.com/region/my%20region/1/2/3");

	}

	// x-grid-location-info
	template<> template<>
	void slurlTestObject::test<3>()
	{
		llofstream gridfile(TEST_FILENAME);
		gridfile << gSampleGridFile;
		gridfile.close();

		LLGridManager::getInstance()->initialize(TEST_FILENAME);

		LLGridManager::getInstance()->setGridChoice("my.grid.com");
		LLSLURL slurl = LLSLURL("x-grid-info://my.grid.com/app/foo/bar?12345");
		ensure_equals("app", slurl.getType(), LLSLURL::APP);
		ensure_equals("appcmd", slurl.getAppCmd(), "foo");
		ensure_equals("apppath", slurl.getAppPath().size(), 1);
		ensure_equals("apppath2", slurl.getAppPath()[0].asString(), "bar");
		ensure_equals("appquery", slurl.getAppQuery(), "12345");
		ensure_equals("grid1", slurl.getGrid(), "my.grid.com");
		slurl = LLSLURL("x-grid-info://lincoln.lindenlab.com/app/agent/0e346d8b-4433-4d66-a6b0-fd37083abc4c/about");
		ensure_equals("app", slurl.getType(), LLSLURL::APP);
		ensure_equals("appcmd", slurl.getAppCmd(), "agent");
		ensure_equals("apppath", slurl.getAppPath().size(), 2);
		ensure_equals("apppath2", slurl.getAppPath()[0].asString(), "0e346d8b-4433-4d66-a6b0-fd37083abc4c");
		ensure_equals("apppath3", slurl.getAppPath()[1].asString(), "about");
		ensure_equals("grid1", slurl.getGrid(), "lincoln.lindenlab.com");
		LLGridManager::getInstance()->setGridChoice("my.stupidgrid.com:8002");
		slurl = LLSLURL("x-grid-info://my.stupidgrid.com:8002/app/foo/bar/baz?12345");
		ensure_equals("app", slurl.getType(), LLSLURL::APP);
		ensure_equals("appcmd", slurl.getAppCmd(), "foo");
		ensure_equals("apppath", slurl.getAppPath().size(), 2);
		ensure_equals("apppath2", slurl.getAppPath()[0].asString(), "bar");
		ensure_equals("apppath3", slurl.getAppPath()[1].asString(), "baz");
		ensure_equals("appquery", slurl.getAppQuery(), "12345");
		ensure_equals("grid1", slurl.getGrid(), "my.stupidgrid.com:8002");
		slurl = LLSLURL("x-grid-info://my.stupidgrid.com:8002/region/my%20region/1/2/3");
		ensure_equals("login string", slurl.getLoginString(), "uri:my region&amp;1&amp;2&amp;3");
		ensure_equals("location string", slurl.getLocationString(), "my region/1/2/3");
		ensure_equals("grid", slurl.getGrid(), "my.stupidgrid.com:8002");
		ensure_equals("region", slurl.getRegion(), "my region");
		ensure_equals("position", slurl.getPosition(), LLVector3(1, 2, 3));

	}
}
