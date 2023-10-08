/**
 *
 * Copyright (c) 2012-2017, Kitty Barnett
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 * By copying, modifying or distributing this software, you acknowledge that
 * you have read and understood your obligations described above, and agree to
 * abide by those obligations.
 *
 */

#pragma once

#include "llavatarname.h"
#include "llfloater.h"
#include "llmutelist.h"

class LLFilterEditor;
class LLListContextMenu;
class LLNameListCtrl;
class LLScrollListCell;
class LLScrollListCtrl;
class LLTabContainer;

// ============================================================================
// Constants
//

extern const std::string BLOCKED_PARAM_NAME;
extern const std::string DERENDER_PARAM_NAME;
extern const std::string EXCEPTION_PARAM_NAME;
extern const std::string BLOCKED_TAB_NAME;
extern const std::string DERENDER_TAB_NAME;
extern const std::string EXCEPTION_TAB_NAME;

// ============================================================================
// LLPanelBlockBase
//

class LLPanelBlockBase
{
public:
	virtual const std::string& getFilterString() const = 0;
	virtual void               setFilterString(const std::string& strFilter) = 0;
};

// ============================================================================
// LLPanelBlockList
//

class LLPanelBlockList : public LLPanel, public LLMuteListObserver, public LLPanelBlockBase
{
public:
	LLPanelBlockList();
	~LLPanelBlockList();

	/*
	 * LLPanel overrides
	 */
public:
	BOOL postBuild() override;
	void onOpen(const LLSD& sdParam) override;

	/*
	 * Member functions
	 */
public:
	const std::string& getFilterString() const override { return m_strFilter; }
	void setFilterString(const std::string& strFilter) override;
protected:
	void refresh();
	void removePicker();
	void selectEntry(const LLSD& sdValue) { LLMute muteEntry(sdValue["id"].asUUID(), sdValue["name"].asString()); selectEntry(muteEntry); }
	void selectEntry(const LLMute& muteEntry);
	void updateButtons();

	/*
	 * Event handlers
	 */
public:
	       void onChange() override;
protected:
	       void onAddAvatar(LLUICtrl* pCtrl);
	static void onAddAvatarCallback(const uuid_vec_t& idAgents, const std::vector<LLAvatarName>& avAgents);
	static void onAddByName();
	static void onAddByNameCallback(const std::string& strBlockName);
		   void onRemoveSelection();
		   void onColumnSortChange();
	static void onIdleRefresh(LLHandle<LLPanel> hPanel);
		   void onSelectionChange();
		   void onToggleMuteFlag(const LLSD& sdValue, const LLScrollListCell* pCell);

	/*
	 * Member variables
	 */
protected:
	bool                m_fRefreshOnChange = true;
	std::string         m_strFilter;
	LLScrollListCtrl*   m_pBlockList = nullptr;
	LLButton*           m_pTrashBtn = nullptr;
	LLHandle<LLFloater> m_hPicker;
};

// ============================================================================
// LLPanelDerenderList
//

class LLPanelDerenderList : public LLPanel
{
public:
	LLPanelDerenderList();
	~LLPanelDerenderList();

	/*
	 * LLPanel overrides
	 */
public:
	BOOL postBuild() override;
	void onOpen(const LLSD& sdParam) override;

	/*
	 * Member functions
	 */
protected:
	void refresh();

	/*
	 * Event handlers
	 */
protected:
	void onColumnSortChange();
	void onSelectionChange();
	void onSelectionRemove();

	/*
	 * Member variables
	 */
protected:
	LLScrollListCtrl*           m_pDerenderList;
	boost::signals2::connection m_DerenderChangeConn;
};

// ============================================================================
// LLPanelAvatarRendering - Configure avatar complexity excpetions
//

class LLPanelAvatarRendering : public LLPanel, public LLMuteListObserver, public LLPanelBlockBase
{
	friend class LLPanelAvatarRenderingContextMenu;
public:
	LLPanelAvatarRendering();
	~LLPanelAvatarRendering();

	/*
	 * LLPanel overrides
	 */
public:
	BOOL postBuild() override;
	void onOpen(const LLSD& sdParam) override;

	/*
	 * Member functions
	 */
public:
	const std::string& getFilterString() const override { return m_strFilter; }
	void setFilterString(const std::string& strFilter) override;
protected:
	void refresh();
	void removePicker();
	void updateButtons();

	/*
	 * Event handlers
	 */
public:
	void onChange() override;
protected:
	void onAddException(LLUICtrl* pCtrl, const LLSD& sdParam);
	static void onAddExceptionCb(const uuid_vec_t& idAgents, S32 nSetting);
	bool onHasException(const LLUUID& idAgent, const LLSD& sdParamn);
	void onExceptionMenu(S32 x, S32 y);
	void onRemoveException();
	void onSetException(const LLUUID& idAgent, const LLSD& sdParamn);

	void onColumnSortChange();
	void onSelectionChange();

	/*
	 * Member variables
	 */
protected:
	bool                m_fRefreshOnChange = true;
	std::string         m_strFilter;
	LLNameListCtrl*     m_pExceptionList = nullptr;
	LLButton*           m_pTrashBtn = nullptr;
	LLListContextMenu*  m_pContextMenu = nullptr;
	LLHandle<LLFloater> m_hPicker;
};

// ============================================================================
// LLFloaterBlocked
//

class LLFloaterBlocked : public LLFloater
{
public:
	LLFloaterBlocked(const LLSD& sdKey);
	~LLFloaterBlocked();

	/*
	 * LLFloater overrides
	 */
public:
	BOOL postBuild() override;
	void onOpen(const LLSD& sdParam) override;

	/*
	 * Event handlers
	 */
protected:
	void onFilterEdit(const std::string& strFilter);
	void onTabSelect(const LLSD& sdParam);

	/*
	 * Member functions
	 */
public:
	static void showMuteAndSelect(const LLUUID& idMute);
	static void showDerenderAndSelect(const LLUUID& idEntry);
	static void showRenderExceptionAndSelect(const LLUUID& idEntry);

	/*
	 * Member variables
	 */
protected:
	LLFilterEditor* m_pFilterEditor = nullptr;
	LLTabContainer* m_pBlockedTabs = nullptr;
};

// ============================================================================
