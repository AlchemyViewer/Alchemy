/**
 * @file llviewernetwork.cpp
 * @author James Cook, Richard Nelson
 * @brief Networking constants and globals for viewer.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#include "llviewerprecompiledheaders.h"

#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "llcorehttputil.h"
#include "llnotificationsutil.h"
#include "llsdserialize.h"
#include "llsecapi.h"
#include "lltrans.h"
#include "llxmlnode.h"

/// key used to store the grid, and the name attribute in the grid data
const std::string  GRID_VALUE = "keyname";
/// the value displayed in the grid selector menu, and other human-oriented text
const std::string  GRID_LABEL_VALUE = "label";
/// the value used on the --grid command line argument
const std::string  GRID_ID_VALUE = "grid_login_id";
/// the url for the login cgi script
const std::string  GRID_LOGIN_URI_VALUE = "login_uri";
/// url base for update queries
const std::string  GRID_UPDATE_SERVICE_URL = "update_query_url_base";
/// uri for data helpers like currency and landbuy
const std::string  GRID_HELPER_URI_VALUE = "helper_uri";
/// the splash page url
const std::string  GRID_LOGIN_PAGE_VALUE = "login_page";
/// url for the web profile site
const std::string  GRID_WEB_PROFILE_VALUE = "web_profile_url";
/// url for the web profile site
const std::string  GRID_STATUS_PAGE_URL = "grid_status";
/// url for the web profile site
const std::string  GRID_STATUS_RSS_URL = "grid_status_rss";
/// internal data on system grids
const std::string  GRID_IS_SYSTEM_GRID_VALUE = "system_grid";
/// whether this is single or double names
const std::string  GRID_LOGIN_IDENTIFIER_TYPES = "login_identifier_types";
/// the url for registering a new account for the given grid
const std::string GRID_ACCOUNT_REGISTRATION_URL = "register";
/// the url for retrieving passwords for the given grid
const std::string GRID_FORGOT_PASSWORD_URL = "password";
/// the platform string for a given grid
const std::string GRID_PLATFORM = "platform";
/// a grid's gatekeeper address
const std::string GRID_GATEKEEPER = "gatekeeper";
/// a grid's uas service address
const std::string GRID_UAS = "uas";
/// a grid's operating agent (optional)
const std::string GRID_ADMIN = "administrator";
/// internal data on whether a grid was added manually or temporarily
const std::string GRID_TEMPORARY = "temporary";

// defines slurl formats associated with various grids.
// we need to continue to support existing forms, as slurls
// are shared between viewers that may not understand newer
// forms.
/// slurl base for grid slurls
const std::string GRID_SLURL_BASE = "slurl_base";
/// slurl base for grid slapp links
const std::string GRID_APP_SLURL_BASE = "app_slurl_base";

const std::string DEFAULT_LOGIN_PAGE = "https://viewer-splash.secondlife.com/";

const std::string MAIN_GRID_LOGIN_URI = "https://login.agni.lindenlab.com/cgi-bin/login.cgi";

const std::string SL_UPDATE_QUERY_URL = "https://update.secondlife.com/update";

const std::string MAIN_GRID_SLURL_BASE = "http://maps.secondlife.com/secondlife/";
const std::string SYSTEM_GRID_APP_SLURL_BASE = "secondlife:///app";

const std::string MAIN_GRID_WEB_PROFILE_URL = "https://my.secondlife.com/";

const char* SYSTEM_GRID_SLURL_BASE = "secondlife://%s/secondlife/";
const char* DEFAULT_SLURL_BASE = "x-grid-info://%s/region/";
const char* DEFAULT_APP_SLURL_BASE = "x-grid-info://%s/app";

const std::string ALCHEMY_UPDATE_SERVICE = "https://app.alchemyviewer.org/update";

//
const std::string GRIDS_USER_FILE = "grids_user.xml";

LLGridManager::LLGridManager()
:	mLoggedIn(false)
,	mPlatform(NOPLATFORM)
{
	// by default, we use the 'grids.xml' file in the user settings directory
	// this file is an LLSD file containing multiple grid definitions.
	// This file does not contain definitions for secondlife.com grids,
	// as that would be a security issue when they are overwritten by
	// an attacker.  Don't want someone snagging a password.
	std::string grid_file = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,
														   GRIDS_USER_FILE);
	LL_DEBUGS("GridManager")<<LL_ENDL;

	initialize(grid_file);

}


//
// LLGridManager - class for managing the list of known grids, and the current
// selection
//


//
// LLGridManager::initialze - initialize the list of known grids based
// on the fixed list of linden grids (fixed for security reasons)
// and the grids.xml file
void LLGridManager::initialize(const std::string& grid_file)
{
	// default grid list.
	// Don't move to a modifiable file for security reasons,
	mGrid.clear() ;

	// set to undefined
	mGridList = LLSD();
	mGridFile = grid_file;
	// as we don't want an attacker to override our grid list
	// to point the default grid to an invalid grid
  	addSystemGrid("Second Life",
				  MAINGRID,
				  MAIN_GRID_LOGIN_URI,
				  "https://secondlife.com/helpers/",
				  DEFAULT_LOGIN_PAGE,
				  "https://secondlife.com/my/account/request.php",
				  "https://join.secondlife.com/?sourceid=AlchemyViewer",
				  ALCHEMY_UPDATE_SERVICE,
				  MAIN_GRID_WEB_PROFILE_URL,
				  "http://status.secondlifegrid.net/",
				  "https://status.secondlifegrid.net/history.atom",
				  "Linden Lab",
				  "secondlife",
				  "Agni");
	addSystemGrid("Second Life Beta",
				  "util.aditi.lindenlab.com",
				  "https://login.aditi.lindenlab.com/cgi-bin/login.cgi",
				  "https://secondlife.aditi.lindenlab.com/helpers/",
				  DEFAULT_LOGIN_PAGE,
				  "https://secondlife.com/my/account/request.php",
				  "https://join.secondlife.com/?sourceid=AlchemyViewer",
				  ALCHEMY_UPDATE_SERVICE,
				  "https://my.aditi.lindenlab.com/",
				  "http://status.secondlifegrid.net/",
				  "https://status.secondlifegrid.net/history.atom",
				  "Linden Lab",
				  "secondlife",
				  "Aditi");

	LLSD other_grids;
	llifstream llsd_xml;
	if (!grid_file.empty())
	{
		LL_INFOS("GridManager")<<"Grid configuration file '"<<grid_file<<"'"<<LL_ENDL;
		llsd_xml.open( grid_file.c_str(), std::ios::in | std::ios::binary );

		// parse through the gridfile, inserting grids into the list unless
		// they overwrite an existing grid.
		if( llsd_xml.is_open())
		{
			LLSDSerialize::fromXMLDocument( other_grids, llsd_xml );
			if(other_grids.isMap())
			{
				for(LLSD::map_iterator grid_itr = other_grids.beginMap();
					grid_itr != other_grids.endMap();
					++grid_itr)
				{
					LLSD::String key_name = grid_itr->first;
					LLSD grid = grid_itr->second;

					std::string existingGrid = getGrid(grid);
					if (mGridList.has(key_name) || !existingGrid.empty())
					{
						LL_WARNS("GridManager") << "Cannot override existing grid '" << key_name << "'; ignoring definition from '"<<grid_file<<"'" << LL_ENDL;
					}
					else if ( addGrid(grid) )
					{
						LL_INFOS("GridManager") << "added grid '"<<key_name<<"'"<<LL_ENDL;
					}
					else
					{
						LL_WARNS("GridManager") << "failed to add invalid grid '"<<key_name<<"'"<<LL_ENDL;
					}
				}
				llsd_xml.close();
			}
			else
			{
				LL_WARNS("GridManager")<<"Failed to parse grid configuration '"<<grid_file<<"'"<<LL_ENDL;
			}
		}
		else
		{
			LL_WARNS("GridManager")<<"Failed to open grid configuration '"<<grid_file<<"'"<<LL_ENDL;
		}
	}
	else
	{
		LL_DEBUGS("GridManager")<<"no grid file specified"<<LL_ENDL;
	}

	// load a grid from the command line.
	// if the actual grid name is specified from the command line,
	// set it as the 'selected' grid.
	std::string cmd_line_grid = gSavedSettings.getString("CmdLineGridChoice");
	if(!cmd_line_grid.empty())
	{
		// try to find the grid assuming the command line parameter is
		// the case-insensitive 'label' of the grid.  ie 'Agni'
		mGrid = getGrid(cmd_line_grid);
		if(mGrid.empty())
		{
			LL_WARNS("GridManager")<<"Unknown grid '"<<cmd_line_grid<<"'"<<LL_ENDL;
		}
		else
		{
			LL_INFOS("GridManager")<<"Command line specified '"<<cmd_line_grid<<"': "<<mGrid<<LL_ENDL;
		}
	}
	else
	{
		// if a grid was not passed in via the command line, grab it from the CurrentGrid setting.
		// if there's no current grid, that's ok as it'll be either set by the value passed
		// in via the login uri if that's specified, or will default to maingrid
		std::string last_grid = gSavedSettings.getString("CurrentGrid");
		if ( ! getGrid(last_grid).empty() )
		{
			LL_INFOS("GridManager")<<"Using last grid: "<<last_grid<<LL_ENDL;
			mGrid = last_grid;
		}
		else
		{
			LL_INFOS("GridManager")<<"Last grid '"<<last_grid<<"' not configured"<<LL_ENDL;
		}
	}

	if(mGrid.empty())
	{
		// no grid was specified so default to maingrid
		LL_INFOS("GridManager") << "Default grid to "<<MAINGRID<< LL_ENDL;
		mGrid = MAINGRID;
	}

	LLControlVariablePtr grid_control = gSavedSettings.getControl("CurrentGrid");
	if (grid_control.notNull())
	{
		grid_control->getSignal()->connect(boost::bind(&LLGridManager::updateIsInProductionGrid, this));
	}

	// since above only triggers on changes, trigger the callback manually to initialize state
	updateIsInProductionGrid();

	setGridChoice(mGrid);
}

LLGridManager::~LLGridManager()
{
}

//
// LLGridManager::addGrid - add a grid to the grid list, populating the needed values
// if they're not populated yet.
//

bool LLGridManager::addGrid(LLSD& grid_data)
{
	bool added = false;
	if (grid_data.isMap() && grid_data.has(GRID_VALUE))
	{
		std::string grid = utf8str_tolower(grid_data[GRID_VALUE].asString());

		if ( getGrid(grid_data[GRID_VALUE]).empty() && getGrid(grid).empty() )
		{
			std::string grid_id = grid_data.has(GRID_ID_VALUE) ? grid_data[GRID_ID_VALUE].asString() : std::string();
			if ( getGrid(grid_id).empty() )
			{
				// populate the other values if they don't exist
				if (!grid_data.has(GRID_LABEL_VALUE))
				{
					grid_data[GRID_LABEL_VALUE] = grid;
				}
				if (!grid_data.has(GRID_ID_VALUE))
				{
					grid_data[GRID_ID_VALUE] = grid;
				}

				// if the grid data doesn't include any of the URIs, then
				// generate them from the grid, which should be a dns address
				if (!grid_data.has(GRID_LOGIN_URI_VALUE))
				{
					grid_data[GRID_LOGIN_URI_VALUE] = LLSD::emptyArray();
					grid_data[GRID_LOGIN_URI_VALUE].append(std::string("https://") +
														   grid + "/cgi-bin/login.cgi");
				}
				// Populate to the default values
				if (!grid_data.has(GRID_LOGIN_PAGE_VALUE))
				{
					grid_data[GRID_LOGIN_PAGE_VALUE] = std::string("http://") + grid + "/app/login/";
				}
				if (!grid_data.has(GRID_WEB_PROFILE_VALUE))
				{
					grid_data[GRID_WEB_PROFILE_VALUE] = std::string("https://") + grid + "/";
				}

				if (!grid_data.has(GRID_LOGIN_IDENTIFIER_TYPES))
				{
					// non system grids and grids that haven't already been configured with values
					// get both types of credentials.
					grid_data[GRID_LOGIN_IDENTIFIER_TYPES] = LLSD::emptyArray();
					grid_data[GRID_LOGIN_IDENTIFIER_TYPES].append(CRED_IDENTIFIER_TYPE_AGENT);
					grid_data[GRID_LOGIN_IDENTIFIER_TYPES].append(CRED_IDENTIFIER_TYPE_ACCOUNT);
				}

				LL_DEBUGS("GridManager") <<grid<<"\n"
										 <<"  id:          "<<grid_data[GRID_ID_VALUE].asString()<<"\n"
										 <<"  label:       "<<grid_data[GRID_LABEL_VALUE].asString()<<"\n"
										 <<"  login page:  "<<grid_data[GRID_LOGIN_PAGE_VALUE].asString()<<"\n"
										 <<"  web profile: "<<grid_data[GRID_WEB_PROFILE_VALUE].asString()<<"\n";
				/* still in LL_DEBUGS */ 
				for (const LLSD& login_uris : grid_data[GRID_LOGIN_URI_VALUE].array())
				{
					LL_CONT << "  login uri:   "<<login_uris.asString()<<"\n";
				}
				LL_CONT << LL_ENDL;
				mGridList[grid] = grid_data;
				added = true;
			}
			else
			{
				if (grid_data.has(GRID_GATEKEEPER))
				{
					mGridList[grid][GRID_GATEKEEPER] = grid_data[GRID_GATEKEEPER];
				}
				LL_WARNS("GridManager")<<"duplicate grid id '"<<grid_id<<"' ignored"<<LL_ENDL;
			}
		}
		else
		{
			if (grid_data.has(GRID_GATEKEEPER))
			{
				mGridList[grid][GRID_GATEKEEPER] = grid_data[GRID_GATEKEEPER];
			}
			LL_WARNS("GridManager")<<"duplicate grid name '"<<grid<<"' ignored"<<LL_ENDL;
		}
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid definition ignored"<<LL_ENDL;
	}
	return added;
}

bool LLGridManager::removeGrid(const std::string& gridkey)
{
	//Grid must exist and not be a system addition
	if (mGridList.has(gridkey) && !isSystemGrid(gridkey))
	{
		mGridList.erase(gridkey);
		mGridListChangedSignal();
		saveGridList();
		return true;
	}
	return false;
}

//
// LLGridManager::addSystemGrid - helper for adding a system grid.
void LLGridManager::addSystemGrid(const std::string& label,
								  const std::string& name,
								  const std::string& login_uri,
								  const std::string& helper,
								  const std::string& login_page,
								  const std::string& password_url,
								  const std::string& register_url,
								  const std::string& update_url_base,
								  const std::string& web_profile_url,
								  const std::string& grid_status_url,
								  const std::string& grid_status_rss_url,
								  const std::string& administrator,
								  const std::string& platform,
								  const std::string& login_id)
{
	LLSD grid = LLSD::emptyMap();
	grid[GRID_VALUE] = name;
	grid[GRID_LABEL_VALUE] = label;
	grid[GRID_HELPER_URI_VALUE] = helper;
	grid[GRID_LOGIN_URI_VALUE] = LLSD::emptyArray();
	grid[GRID_LOGIN_URI_VALUE].append(login_uri);
	grid[GRID_LOGIN_PAGE_VALUE] = login_page;
	grid[GRID_UPDATE_SERVICE_URL] = update_url_base;
	grid[GRID_WEB_PROFILE_VALUE] = web_profile_url;
	grid[GRID_STATUS_PAGE_URL] = grid_status_url;
	grid[GRID_STATUS_RSS_URL] = grid_status_rss_url;
	grid[GRID_IS_SYSTEM_GRID_VALUE] = true;
	grid[GRID_LOGIN_IDENTIFIER_TYPES] = LLSD::emptyArray();
	grid[GRID_LOGIN_IDENTIFIER_TYPES].append(CRED_IDENTIFIER_TYPE_AGENT);
	grid[GRID_FORGOT_PASSWORD_URL] = password_url;
	grid[GRID_ACCOUNT_REGISTRATION_URL] = register_url;
	grid[GRID_PLATFORM] = platform;

	grid[GRID_APP_SLURL_BASE] = SYSTEM_GRID_APP_SLURL_BASE;
	if (login_id.empty())
	{
		grid[GRID_ID_VALUE] = name;
	}
	else
	{
		grid[GRID_ID_VALUE] = login_id;
	}

	if (name == std::string(MAINGRID))
	{
		grid[GRID_SLURL_BASE] = MAIN_GRID_SLURL_BASE;
	}
	else
	{
		grid[GRID_SLURL_BASE] = llformat(SYSTEM_GRID_SLURL_BASE, grid[GRID_ID_VALUE].asString().c_str());
	}
	if (!administrator.empty())
	{
		grid[GRID_ADMIN] = administrator;
	}

	addGrid(grid);
}

void LLGridManager::addRemoteGrid(const std::string& login_uri, const EAddGridType type)
{
	LL_DEBUGS("GridManager") << "Adding '" << login_uri << "' to grid manager." << LL_ENDL;
	if (login_uri.empty()) return;

	std::string grid = utf8str_tolower(login_uri);
	LLStringUtil::trim(grid);
	// Grid needs to be in the form of a dns address,
	// but also support localhost:9000 or localhost:9000/login
	if (grid.find_first_not_of("abcdefghijklmnopqrstuvwxyz1234567890-_.:/@% ") != std::string::npos)
	{
		LLNotificationsUtil::add("InvalidGrid", LLSD().with("GRID", grid));
		return;
	}
	
	// Trim any ending slash
	size_t slash_pos = grid.find_last_of('/');
	if (grid.length() - 1 == slash_pos)
	{
		grid.erase(slash_pos);
	}
	
	std::string slashy_slash("://");
	size_t find_scheme = grid.find(slashy_slash);
	std::string grid_value(grid);
	if (find_scheme != std::string::npos)
	{
		grid_value.erase(0, find_scheme + slashy_slash.length());
	}
	else
	{
		// default to http
		grid.insert(0, "http://");
	}

    bool hypergrid = false;
	switch (type)
	{
		case ADD_HYPERGRID:
            hypergrid = true;
            // yep, fallthru.
		case ADD_LINK:
		case ADD_MANUAL:
            LLCoros::instance().launch("LLGridManager::addRemoteGrid",
                std::bind(&LLGridManager::gridInfoResponderCoro, this, grid, hypergrid));

			break;
	}
}

void LLGridManager::gridInfoResponderCoro(const std::string uri, bool hypergrid)
{
    using namespace LLCoreHttpUtil;
    
    LLSD grid;
    LLURI grid_uri(uri);
    grid[GRID_VALUE] = grid_uri.authority();
    if (hypergrid)
        grid[GRID_TEMPORARY] = true;
    
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    auto httpAdapter = boost::make_shared<HttpCoroutineAdapter>("GridInfoRequest", httpPolicy);
    auto httpRequest = boost::make_shared<LLCore::HttpRequest>();
    auto httpOptions = boost::make_shared<LLCore::HttpOptions>();
    httpOptions->setTimeout(5);
    
    LLSD result = httpAdapter->getRawAndSuspend(httpRequest, llformat("%s/get_grid_info", grid_uri.asString().c_str()), httpOptions);
    
    LLCore::HttpStatus status = HttpCoroutineAdapter::getStatusFromLLSD(result[HttpCoroutineAdapter::HTTP_RESULTS]);
    
    if (!status)
    {
        LLSD args;
        args["GRID"] = uri;
        args["STATUS"] = status.toString();
        args["REASON"] = status.getMessage();
        LLNotificationsUtil::add("CantAddGrid", args);
        
        return;
    }
    
    // *TODO: need to write a special adapter for the weird ass gridinfo pseudo-xml format
    const LLSD::Binary &raw_results = result[HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    // is LLXMLNode::parseBuffer() const safe? iunno! today is not the day to find out, so we make a copy.
    std::string babe_pig_in_the_city(raw_results.begin(), raw_results.end());
	LLPointer<LLXMLNode> xmlnode;
	if (!LLXMLNode::parseBuffer(reinterpret_cast<U8*>(&babe_pig_in_the_city[0]),
                                babe_pig_in_the_city.size(), xmlnode, nullptr))
	{
        LLNotificationsUtil::add("MalformedGridInfo", LLSD().with("GRID", grid[GRID_VALUE]));
        return;
    }
    
	for (LLXMLNode* node = xmlnode->getFirstChild(); node != nullptr; node = node->getNextSibling())
	{
		if (node->hasName("login"))
		{
			grid[GRID_LOGIN_URI_VALUE] = LLSD::emptyArray();
			grid[GRID_LOGIN_URI_VALUE].append(node->getTextContents());
			LL_DEBUGS("GridManager") << "[\"login\"]: " << grid[GRID_LOGIN_URI_VALUE] << LL_ENDL;
		}
		else if (node->hasName("gridnick"))
		{
			grid[GRID_ID_VALUE] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"gridnick\"]: " << grid[GRID_ID_VALUE] << LL_ENDL;
		}
		else if (node->hasName("gridname"))
		{
			if (!grid.has(GRID_ID_VALUE))
			{
				grid[GRID_ID_VALUE] = node->getTextContents();
			}
			grid[GRID_LABEL_VALUE] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"gridname\"]: " << grid[GRID_LABEL_VALUE] << LL_ENDL;
		}
		else if (node->hasName("administrator"))
		{
			grid[GRID_ADMIN] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"administrator\" " << grid[GRID_ADMIN] << LL_ENDL;
		}
		else if (node->hasName("gatekeeper"))
		{
			LLURI gatekeeper(node->getTextContents());
			grid[GRID_GATEKEEPER] = gatekeeper.authority();
			LL_DEBUGS("GridManager") << "[\"gatekeeper\"]: " << grid[GRID_GATEKEEPER] << LL_ENDL;
		}
		else if (node->hasName("uas"))
		{
			grid[GRID_UAS] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"uas\"]: " << grid[GRID_UAS] << LL_ENDL;
		}
		else if (node->hasName("platform"))
		{
			grid[GRID_PLATFORM] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"platform\"]: " << grid[GRID_PLATFORM] << LL_ENDL;
		}
		else if (node->hasName("welcome"))
		{
			grid[GRID_LOGIN_PAGE_VALUE] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"welcome\"]: " << grid[GRID_LOGIN_PAGE_VALUE] << LL_ENDL;
		}
		else if (node->hasName("register"))
		{
			grid[GRID_ACCOUNT_REGISTRATION_URL] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"register\"]: " << grid[GRID_ACCOUNT_REGISTRATION_URL] << LL_ENDL;
		}
		else if (node->hasName("GridStatus"))
		{
			grid[GRID_STATUS_PAGE_URL] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"gridstatus\"]: " << grid[GRID_STATUS_PAGE_URL] << LL_ENDL;
		}
		else if (node->hasName("GridStatusRSS"))
		{
			grid[GRID_STATUS_RSS_URL] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"gridstatusrss\"]: " << grid[GRID_STATUS_RSS_URL] << LL_ENDL;
		}
		else if (node->hasName("password"))
		{
			grid[GRID_FORGOT_PASSWORD_URL] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"password\"]: " << grid[GRID_FORGOT_PASSWORD_URL] << LL_ENDL;
		}
		// Two names for the same thing...
		else if (node->hasName("economy") || node->hasName("helperuri"))
		{
			grid[GRID_HELPER_URI_VALUE] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"economy\"]: " << grid[GRID_HELPER_URI_VALUE] << LL_ENDL;
		}
		else if (node->hasName("slurl_base"))
		{
			grid[GRID_SLURL_BASE] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"slurl_base\"]: " << grid[GRID_SLURL_BASE] << LL_ENDL;
		}
		else if (node->hasName("app_slurl_base"))
		{
			grid[GRID_APP_SLURL_BASE] = node->getTextContents();
			LL_DEBUGS("GridManager") << "[\"app_slurl_base\"]: " << grid[GRID_APP_SLURL_BASE] << LL_ENDL;
		}
	}
	
	if (addGrid(grid))
	{
		if (!(grid.has(GRID_TEMPORARY) && grid[GRID_TEMPORARY].asBoolean()))
		{
			LLNotificationsUtil::add("AddGridSuccess",
									 LLSD().with("GRID", grid[GRID_LABEL_VALUE].asString()));
			setGridChoice(grid[GRID_VALUE].asString());
		}
		mGridListChangedSignal();
		saveGridList();
	}
}

void LLGridManager::saveGridList()
{
	LLSD data;
	for(LLSD::map_iterator grid_iter = mGridList.beginMap();
		grid_iter != mGridList.endMap();
	    ++grid_iter)
	{
		// We don't need to store system grids, they're hard coded!
		if (grid_iter->second.has(GRID_IS_SYSTEM_GRID_VALUE)
			 && grid_iter->second[GRID_IS_SYSTEM_GRID_VALUE].asBoolean())
			continue;
		if (grid_iter->second.has(GRID_TEMPORARY)
			&& grid_iter->second[GRID_TEMPORARY].asBoolean())
			continue;
		
		data[grid_iter->first] = grid_iter->second;
	}
	const std::string& filename = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, GRIDS_USER_FILE);
	llofstream outstream;
	outstream.open(filename.c_str());
	LLSDSerialize::toPrettyXML(data, outstream);
	outstream.close();
}

// return a list of grid name -> grid label mappings for UI purposes
std::map<std::string, std::string> LLGridManager::getKnownGrids() const
{
	std::map<std::string, std::string> result;
	for(const auto& grid_pair : mGridList.map())
	{
		// skip temp grids. since this is just for "grid label mappings for UI purposes"
		if (grid_pair.second.has(GRID_TEMPORARY) && grid_pair.second[GRID_TEMPORARY].asBoolean())
			continue;
		result[grid_pair.first] = grid_pair.second[GRID_LABEL_VALUE].asString();
	}

	return result;
}

void LLGridManager::setGridChoice(const std::string& grid, const bool only_select /* = true */)
{
	// Can't change grid once we are logged in
	if (mLoggedIn) return;

	// Set the grid choice based on a string.
	LL_DEBUGS("GridManager") << "requested " << grid << LL_ENDL;
 	std::string grid_name = getGrid(grid); // resolved either the name or the id to the name

	if(!grid_name.empty())
	{
		LL_INFOS("GridManager") << "setting " << grid_name << LL_ENDL;
		mGrid = grid_name;
		gSavedSettings.setString("CurrentGrid", grid_name);
		LLTrans::setDefaultArg("CURRENT_GRID", getGridLabel());
		LLTrans::setDefaultArg("GRID_ADMIN", getGridAdministrator());
		
		updateIsInProductionGrid();
	}
	else if (!only_select)
	{
		// the grid was not in the list of grids.
		LL_WARNS("GridManager") << "fetching grid info " << grid << LL_ENDL;
		
		addRemoteGrid(grid, ADD_LINK);
	}
	else
	{
		// the grid was not in the list of grids.
		LL_WARNS("GridManager")<<"unknown grid "<<grid<<LL_ENDL;
	}
	mGridListChangedSignal();
}

std::string LLGridManager::getGrid(const std::string& grid) const
{
	std::string grid_name;

	if (mGridList.has(grid))
	{
		// the grid was the long name, so we're good, return it
		grid_name = grid;
	}
	else
	{
		// search the grid list for a grid with a matching id
		for(const auto& grid_pair : mGridList.map())
		{
			if (grid_pair.second.has(GRID_ID_VALUE))
			{
				if (0 == (LLStringUtil::compareInsensitive(grid,
														   grid_pair.second[GRID_ID_VALUE].asString())))
				{
					// found a matching label, return this name
					grid_name = grid_pair.first;
					break;
				}
			}
			if (grid_pair.second.has(GRID_GATEKEEPER))
			{
				if (0 == (LLStringUtil::compareInsensitive(grid,
					grid_pair.second[GRID_GATEKEEPER].asString())))
				{
					// found a matching label, return this name
					grid_name = grid_pair.first;
					break;
				}
			}
		}
	}
	return grid_name;
}

std::string LLGridManager::getGridByProbing(const std::string& identifier) const
{
	std::string grid = getGridByAttribute(GRID_GATEKEEPER, identifier);
	if (!grid.empty()) return grid;
	grid = getGridByAttribute(GRID_VALUE, identifier);
	if (!grid.empty()) return grid;
	grid = getGridByAttribute(GRID_ID_VALUE, identifier);
	if (!grid.empty()) return grid;
	return grid;
}

std::string LLGridManager::getGridByAttribute(const std::string& attribute, const std::string& value) const
{
	if (attribute.empty() || value.empty()) return LLStringUtil::null;
	
	for(const auto& grid_iter : mGridList.map())
	{
		if (grid_iter.second.has(attribute)
			&& LLStringUtil::compareInsensitive(value, grid_iter.second[attribute].asString()) == 0)
		{
			return grid_iter.first;
		}
	}
	return LLStringUtil::null;
}

std::string LLGridManager::getGridLabel(const std::string& grid) const
{
	std::string grid_label;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		grid_label = mGridList[grid_name][GRID_LABEL_VALUE].asString();
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid '"<<grid<<"'"<<LL_ENDL;
	}
	LL_DEBUGS("GridManager")<<"returning "<<grid_label<<LL_ENDL;
	return grid_label;
}

std::string LLGridManager::getGridId(const std::string& grid) const
{
	std::string grid_id;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		grid_id = mGridList[grid_name][GRID_ID_VALUE].asString();
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid '"<<grid<<"'"<<LL_ENDL;
	}
	LL_DEBUGS("GridManager")<<"returning "<<grid_id<<LL_ENDL;
	return grid_id;
}

std::string LLGridManager::getGridAdministrator(const std::string& grid) const
{
	std::string admininstrator = "Linden Lab"; // gotta default to something
	std::string grid_name = getGrid(grid);
	if(!grid_name.empty())
	{
		if (mGridList[grid_name].has(GRID_ADMIN))
		{
			admininstrator = mGridList[grid_name][GRID_ADMIN].asString();
		}
		else if (mGridList[grid_name].has(GRID_LABEL_VALUE))
		{
			admininstrator = mGridList[grid_name][GRID_LABEL_VALUE].asString();
		}
	}
	return admininstrator;
}

void LLGridManager::getLoginURIs(const std::string& grid, std::vector<std::string>& uris) const
{
	uris.clear();
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
        if (mGridList[grid_name][GRID_LOGIN_URI_VALUE].isArray())
        {
		    for (const LLSD& llsd_uri : mGridList[grid_name][GRID_LOGIN_URI_VALUE].array())
		    {
			    uris.push_back(llsd_uri.asString());
		    }
        }
        else
        {
            uris.push_back(mGridList[grid_name][GRID_LOGIN_URI_VALUE].asString());
        }
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid '"<<grid<<"'"<<LL_ENDL;
	}
}

void LLGridManager::getLoginURIs(std::vector<std::string>& uris) const
{
	getLoginURIs(mGrid, uris);
}

std::string LLGridManager::getGatekeeper(const std::string& grid) const
{
	std::string url = mGridList[grid].has(GRID_GATEKEEPER)
					  ? mGridList[grid][GRID_GATEKEEPER].asString()
					  : LLStringUtil::null;
	LL_DEBUGS("GridManager") << "returning " << url << LL_ENDL;
	return url;
}

std::string LLGridManager::getUserAccountServiceURL(const std::string& grid) const
{
	std::string url = LLStringUtil::null;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		url = mGridList[grid_name].has(GRID_UAS)
			? mGridList[grid_name][GRID_UAS].asString()
			: LLStringUtil::null;
	}
	return url;
}

std::string LLGridManager::getHelperURI(const std::string& grid) const
{
	std::string helper_uri;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		helper_uri = mGridList[grid_name].has(GRID_HELPER_URI_VALUE)
				   ? mGridList[grid_name][GRID_HELPER_URI_VALUE].asString()
				   : llformat("https://%s/helpers/", grid.c_str());
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid '"<<grid<<"'"<<LL_ENDL;
	}

	LL_DEBUGS("GridManager")<<"returning "<<helper_uri<<LL_ENDL;
	return helper_uri;
}

std::string LLGridManager::getLoginPage(const std::string& grid) const
{
	std::string grid_login_page;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		grid_login_page = mGridList[grid_name][GRID_LOGIN_PAGE_VALUE].asString();
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid '"<<grid<<"'"<<LL_ENDL;
	}
	return grid_login_page;
}

std::string LLGridManager::getLoginPage() const
{
	std::string login_page = mGridList[mGrid][GRID_LOGIN_PAGE_VALUE].asString();
	LL_DEBUGS("GridManager")<<"returning "<<login_page<<LL_ENDL;
	return login_page;
}

std::string LLGridManager::getForgotPasswordURL() const
{
	std::string url = mGridList[mGrid].has(GRID_FORGOT_PASSWORD_URL)
					  ? mGridList[mGrid][GRID_FORGOT_PASSWORD_URL].asString()
					  : LLStringUtil::null;
	LL_DEBUGS("GridManager") << "returning " << url << LL_ENDL;
	return url;
}

std::string LLGridManager::getCreateAccountURL() const
{
	std::string url = mGridList[mGrid].has(GRID_ACCOUNT_REGISTRATION_URL)
					  ? mGridList[mGrid][GRID_ACCOUNT_REGISTRATION_URL].asString()
					  : LLStringUtil::null;
	LL_DEBUGS("GridManager") << "returning " << url << LL_ENDL;
	return url;
}

std::string LLGridManager::getGridStatusURL(const std::string& grid) const
{
	std::string grid_status_url;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		grid_status_url = mGridList[grid_name][GRID_STATUS_PAGE_URL].asString();
	}
	else
	{
		LL_WARNS("GridManager") << "invalid grid '" << grid << "'" << LL_ENDL;
	}
	return grid_status_url;
}

std::string LLGridManager::getGridStatusRSSURL(const std::string& grid) const
{
	std::string grid_status_rss_url;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		grid_status_rss_url = mGridList[grid_name][GRID_STATUS_RSS_URL].asString();
	}
	else
	{
		LL_WARNS("GridManager") << "invalid grid '" << grid << "'" << LL_ENDL;
	}
	return grid_status_rss_url;
}

std::string LLGridManager::getWebProfileURL(const std::string& grid)
{
	std::string web_profile_url;
	std::string grid_name = getGrid(grid);
	if (!grid_name.empty())
	{
		web_profile_url = mGridList[grid_name][GRID_WEB_PROFILE_VALUE].asString();
	}
	else
	{
		LL_WARNS("GridManager")<<"invalid grid '"<<grid<<"'"<<LL_ENDL;
	}
	return web_profile_url;
}

void LLGridManager::getLoginIdentifierTypes(LLSD& idTypes) const
{
	idTypes = mGridList[mGrid][GRID_LOGIN_IDENTIFIER_TYPES];
}

std::string LLGridManager::getGridLoginID() const
{
	return mGridList[mGrid][GRID_ID_VALUE];
}

std::string LLGridManager::getPlatformString() const
{
	std::string platform = mGridList[mGrid].has(GRID_PLATFORM)
						   ? mGridList[mGrid][GRID_PLATFORM].asString()
						   : LLStringUtil::null;
	return platform;
}

std::string LLGridManager::getUpdateServiceURL() const
{
	std::string update_url_base = gSavedSettings.getString("CmdLineUpdateService");;
	if ( !update_url_base.empty() )
	{
		LL_INFOS("UpdaterService","GridManager")
			<< "Update URL base overridden from command line: " << update_url_base
			<< LL_ENDL;
	}
	else
	{
		update_url_base = ALCHEMY_UPDATE_SERVICE;
	}
			
	return update_url_base;
}

LLSD LLGridManager::getGridInfo(const std::string& grid) const
{
	return mGridList.has(grid) ? mGridList[grid] : LLSD();
}

void LLGridManager::updateIsInProductionGrid()
{
	// *NOTE:Mani This used to compare GRID_INFO_AGNI to gGridChoice,
	// but it seems that loginURI trumps that.
	std::vector<std::string> uris;
	getLoginURIs(uris);
	if (uris.empty())
	{
		LL_DEBUGS("GridManager") << "uri is empty, Setting grid platform to NOTHING." << LL_ENDL;
		mPlatform = NOPLATFORM;
		return;
	}

	// Detect Second Life Agni. We want to match the exact uri here because we're dealing with a live economy
	for (const std::string& uri : uris)
	{
		if (MAIN_GRID_LOGIN_URI == uri)
		{
			LL_DEBUGS("GridManager") << "Setting grid platform to SLMAIN" << LL_ENDL;
			mPlatform = SLMAIN;
			return;
		}
	}
	
	// Detect Second Life Aditi et al.
	for (const std::string& uri : uris)
	{
		LLURI login_uri = LLURI(uri);
		if (login_uri.authority().find("lindenlab.com") != std::string::npos) // Any old lab domain will do
		{
			LL_DEBUGS("GridManager") << "Setting grid platform to SLBETA" << LL_ENDL;
			mPlatform = SLBETA;
			return;
		}
	}
	
	if (getPlatformString() == "OpenSim")
	{
		LL_DEBUGS("GridManager") << "Setting grid platform to OPENSIM" << LL_ENDL;
		mPlatform = OPENSIM;
		return;
	}
	if (getPlatformString() == "Halcyon")
	{
		LL_DEBUGS("GridManager") << "Setting grid platform to HALCYON" << LL_ENDL;
		mPlatform = HALCYON;
		return;
	}
	
	// Default to OpenSim
	LL_DEBUGS("GridManager")<< "Defaulting to OPENSIM" << LL_ENDL;
	mPlatform = OPENSIM;
}

bool LLGridManager::isInSecondlife() const
{
	return (isInSLMain() || isInSLBeta());
}

bool LLGridManager::isInOpenSim() const
{
	return (mPlatform == OPENSIM || mPlatform == HALCYON);
}

bool LLGridManager::isInOpenSimulator() const
{
	return mPlatform == OPENSIM;
}

bool LLGridManager::isInHalcyon() const
{
	return mPlatform == HALCYON;
}

bool LLGridManager::isInSLMain() const
{
	return mPlatform == SLMAIN;
}

bool LLGridManager::isInSLBeta() const
{
	return mPlatform == SLBETA;
}

bool LLGridManager::isSystemGrid(const std::string& grid) const
{
	std::string grid_name = getGrid(grid);

	return (   !grid_name.empty()
			&& mGridList[grid_name].has(GRID_IS_SYSTEM_GRID_VALUE)
			&& mGridList[grid_name][GRID_IS_SYSTEM_GRID_VALUE].asBoolean()
			);
}

// build a slurl for the given region within the selected grid
std::string LLGridManager::getSLURLBase(const std::string& grid) const
{
	std::string grid_base = "";
	std::string grid_name = getGrid(grid);
	if(!grid_name.empty())
	{
		if (mGridList[grid_name].has(GRID_SLURL_BASE))
		{
			grid_base = mGridList[grid_name][GRID_SLURL_BASE].asString();
		}
		else
		{
			grid_base = llformat(DEFAULT_SLURL_BASE, grid.c_str());
		}
	}
	LL_DEBUGS("GridManager")<<"returning '"<<grid_base<<"'"<<LL_ENDL;
	return grid_base;
}

// build a slurl for the given region within the selected grid
std::string LLGridManager::getAppSLURLBase(const std::string& grid) const
{
	std::string grid_base;
	std::string grid_name = getGrid(grid);
	if(!grid_name.empty())
	{
		if (mGridList[grid_name].has(GRID_APP_SLURL_BASE))
		{
			grid_base = mGridList[grid_name][GRID_APP_SLURL_BASE].asString();
		}
		else
		{
			grid_base = llformat(DEFAULT_APP_SLURL_BASE, grid.c_str());
		}
	}
	LL_DEBUGS("GridManager")<<"returning '"<<grid_base<<"'"<<LL_ENDL;
	return grid_base;
}
