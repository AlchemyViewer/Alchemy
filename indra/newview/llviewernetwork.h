/** 
 * @file llviewernetwork.h
 * @author James Cook
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

#ifndef LL_LLVIEWERNETWORK_H
#define LL_LLVIEWERNETWORK_H

// @TODO this really should be private, but is used in llslurl
#define MAINGRID "util.agni.lindenlab.com"

/**
 * @brief A singleton class to manage the grids available to the viewer.
 *
 * This class maintains several properties for each known grid, and provides
 * interfaces for obtaining each of these properties given a specified
 * grid.  Grids are specified by either of two identifiers, each of which
 * must be unique among all known grids:
 * - grid name : DNS name for the grid
 * - grid id   : a short form (conventionally a single word) 
 *
 * This class maintains the currently selected grid, and provides short
 * form accessors for each of the properties of the selected grid.
 **/
class LLGridManager final : public LLSingleton<LLGridManager>
{
	/// Instantiate the grid manager, load default grids, selects the default grid
	LLSINGLETON(LLGridManager);
	~LLGridManager();

  public:
	/* ================================================================
	 * @name Initialization and Configuration
	 * @{
	 */
	/// add grids from an external grids file
	void initialize(const std::string& grid_file);
	
	//@}
	
	/* ================================================================
	 * @name Grid Identifiers 
	 * @{
	 * The id is a short form (typically one word) grid name,
	 * It should be used in URL path elements or parameters
	 *
	 * Each grid also has a "label", intented to be a user friendly
	 * descriptive form (it is used in the login panel grid menu, for example).
	 */
	/// Return the name of a grid, given either its name or its id
	std::string getGrid(const std::string& grid) const;
	
	/// Returns the grid value by probing attributes
	std::string getGridByProbing(const std::string& identifier) const;
	
	/// Return the grid value by attribute
	std::string getGridByAttribute(const std::string& attribute, const std::string& value) const;

	/// Get the id (short form selector) for a given grid
	std::string getGridId(const std::string& grid) const;

	/// Get the id (short form selector) for the selected grid
	std::string getGridId() const { return getGridId(mGrid); }

	/// Get the user-friendly long form descriptor for a given grid
	std::string getGridLabel(const std::string& grid) const;
	
	/// Get the user-friendly long form descriptor for the selected grid
	std::string getGridLabel() const { return getGridLabel(mGrid); }
	
	/// Get the grid administrator for a given grid
	std::string getGridAdministrator(const std::string& grid) const;
	
	/// Get the grid administrator for the selected grid
	std::string getGridAdministrator() const {return getGridAdministrator(mGrid); }
	
	/// Returns gridInfo for a given grid as an LLSD map
	LLSD getGridInfo(const std::string& grid) const;

	/// Retrieve a map of grid-name -> label
	std::map<std::string, std::string> getKnownGrids() const;

	//@}

	/* ================================================================
	 * @name Login related properties
	 * @{
	 */

	/**
	 * Get the login uris for the specified grid.
	 * The login uri for a grid is the target of the authentication request.
	 * A grid may have multple login uris, so they are returned as a vector.
	 */
	void getLoginURIs(const std::string& grid, std::vector<std::string>& uris) const;
	
	/// Get the login uris for the selected grid
	void getLoginURIs(std::vector<std::string>& uris) const;
	
	/// Get the hypergrid gatekeeper uri for the specified grid
	std::string getGatekeeper(const std::string& grid) const;
	
	/// Get the uas service for the specified grid if available
	std::string getUserAccountServiceURL(const std::string& grid) const;
	
	/// Get the URI for webdev help functions for the specified grid
	std::string getHelperURI(const std::string& grid) const;

	/// Get the URI for webdev help functions for the selected grid
	std::string getHelperURI() const { return getHelperURI(mGrid); }

	/// Get the url of the splash page to be displayed prior to login
	std::string getLoginPage(const std::string& grid_name) const;

	/// Get the URI for the login splash page for the selected grid
	std::string getLoginPage() const;
	
	/// Get the url for recovering a user's password for the selected grid
	std::string getForgotPasswordURL() const;
	
	/// Get the url for creating an account for the selected grid
	std::string getCreateAccountURL() const;

	/// Get the url for the grid status page
	std::string getGridStatusURL(const std::string& grid) const;
	std::string getGridStatusURL() const { return getGridStatusURL(mGrid); };

	/// Get the url for the grid status rss feed
	std::string getGridStatusRSSURL(const std::string& grid) const;
	std::string getGridStatusRSSURL() const { return getGridStatusRSSURL(mGrid); };

	/// Get the id to be used as a short name in url path components or parameters
	std::string getGridLoginID() const;
	
	/// Get the platform string for the selected grid
	std::string getPlatformString() const;

	/// Get an array of the login types supported by the grid
	void getLoginIdentifierTypes(LLSD& idTypes) const;
	/**< the types are "agent" and "avatar";
	 * one means single-name (someone Resident) accounts and other first/last name accounts
	 * I am not sure which is which
	 */

	//@}
	/* ================================================================
	 * @name Update Related Properties
	 * @{
	 */
	/// Get the update service URL base (host and path) for the selected grid
	std::string getUpdateServiceURL() const;
	
	//@}

	/* ================================================================
	 * @name URL Construction Properties
	 * @{
	 */

	/// Return the slurl prefix (everything up to but not including the region) for a given grid
	std::string getSLURLBase(const std::string& grid) const;

	/// Return the slurl prefix (everything up to but not including the region) for the selected grid
	std::string getSLURLBase() const { return getSLURLBase(mGrid); }
	
	/// Return the application URL prefix for the given grid
	std::string getAppSLURLBase(const std::string& grid) const;

	/// Return the application URL prefix for the selected grid
	std::string getAppSLURLBase() const { return getAppSLURLBase(mGrid); }

	/// Return the url of the resident profile web site for the given grid
	std::string getWebProfileURL(const std::string& grid);

	/// Return the url of the resident profile web site for the selected grid
	std::string getWebProfileURL() { return getWebProfileURL(mGrid); }


	//@}
	
	typedef enum e_grid_platform {
		NOPLATFORM = 0,
		SLMAIN,
		SLBETA,
		OPENSIM,
		HALCYON
	} EGridPlatform;
	
	typedef enum e_add_grid {
		ADD_MANUAL = 0,
		ADD_HYPERGRID,
		ADD_LINK
	} EAddGridType;

	/* ================================================================
	 * @name Selecting the current grid
	 * @{
	 * At initialization, the current grid is set by the first of:
	 * -# The value supplied by the --grid command line option (setting CmdLineGridChoice);
	 *    Note that a default for this may be set at build time.
	 * -# The grid used most recently (setting CurrentGrid)
	 * -# The main grid (Agni)
	 */

	/// Select a given grid as the current grid.
	void setGridChoice(const std::string& grid, const bool only_select = true, const bool for_login = false);

	/// Returns the name of the currently selected grid 
	std::string getGrid() const { return mGrid; }

	//@}

	/// Is the given grid one of the hard-coded default grids (Agni or Aditi)
	bool isSystemGrid(const std::string& grid) const;

	/// Is the selected grid one of the hard-coded default grids (Agni or Aditi)
	bool isSystemGrid() const { return isSystemGrid(mGrid); }
	
	/// Is the selected grid Second Life?
	bool isInSecondlife() const;
	
	/// Is the selected grid OpenSim or OpenSim-derived?
	bool isInOpenSim() const;
	
	/// Is the selected grid OpenSimulator?
	bool isInOpenSimulator() const;
	
	/// Is the selected grid Halcyon?
	bool isInHalcyon() const;
	
	/// Is the selected grid agni?
	bool isInSLMain() const;
	
	/// Is the selected grid aditi?
	bool isInSLBeta() const;
	
	/* ===============================================================
	 * @name User grid management functions
	 * @{
	 */
	
	/// Add a grid by fetching its gridInfo
	void addRemoteGrid(const std::string& login_uri, const EAddGridType type);
	
	/// Remove a grid from the grid list by key
	bool removeGrid(const std::string& gridkey);
	///< @returns true if successfully removed
	
	//@}

	/// Sets login lock so grid cannot be changed once we are logged in
	void setLoggedIn(bool logged_in) { mLoggedIn = logged_in; }
	
protected:

    void gridInfoResponderCoro(const std::string uri, bool hypergrid);
	
private:
	
	/// Add a grid to the list of grids
	bool addGrid(LLSD& grid_info);
	///< @returns true if successfully added
	
	/// Save grids list to file
	void saveGridList();
	
	void updateIsInProductionGrid();

	// helper function for adding the hard coded grids
	void addSystemGrid(const std::string& label, 
					   const std::string& name, 
					   const std::string& login, 
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
					   const std::string& login_id = "");

	bool mLoggedIn;
	std::string mGrid;
	std::string mGridFile;
	LLSD mGridList;
	EGridPlatform mPlatform;
	
	
	/* ===============================================================
	 * @name Grid list signal updates
	 * @{
	 */
	
private:
	typedef boost::signals2::signal<void()> grid_list_changed_signal_t;
	grid_list_changed_signal_t mGridListChangedSignal;
	
public:
	/// Add grid list change callback
	boost::signals2::connection addGridListChangedCallback(const grid_list_changed_signal_t::slot_type& cb)
		{ return mGridListChangedSignal.connect(cb); }
	
	//@}
};

const S32 MAC_ADDRESS_BYTES = 6;

#endif
