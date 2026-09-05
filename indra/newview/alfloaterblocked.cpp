/**
 *
 * Copyright (c) 2012-2017, Kitty Barnett
 * Copyright (c) 2023-2026, Rye Mutt <rye@alchemyviewer.org>
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterblocked.h"

#include "alassetblocklist.h"
#include "llavatarname.h"
#include "llavatarnamecache.h"
#include "llcallbacklist.h"
#include "alderenderlist.h"
#include "llfiltereditor.h"
#include "llfloateravatarpicker.h"
#include "llfloaterreg.h"
#include "lllistcontextmenu.h"
#include "llmenugl.h"
#include "llnamelistctrl.h"
#include "llnotificationsutil.h"
#include "llpanelblockedlist.h"
#include "llscrolllistctrl.h"
#include "lltabcontainer.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"

#include <boost/algorithm/string.hpp>

// ============================================================================
// Constants
//

const std::string BLOCKED_PARAM_NAME  = "blocked_to_select";
const std::string DERENDER_PARAM_NAME = "derender_to_select";
const std::string ASSET_PARAM_NAME = "asset_to_select";
const std::string EXCEPTION_PARAM_NAME = "exception_to_select";
const std::string BLOCKED_TAB_NAME = "mute_tab";
const std::string DERENDER_TAB_NAME = "derender_tab";
const std::string ASSET_TAB_NAME = "asset_tab";
const std::string EXCEPTION_TAB_NAME = "avatar_rendering_tab";

// ============================================================================
// ALPanelBlockList
//

static LLPanelInjector<ALPanelBlockList> t_panel_blocked_list("panel_block_list");

ALPanelBlockList::ALPanelBlockList()
    : LLPanel()
{
    mCommitCallbackRegistrar.add("Block.AddAvatar", boost::bind(&ALPanelBlockList::onAddAvatar, this, _1));
    mCommitCallbackRegistrar.add("Block.AddByName", boost::bind(&ALPanelBlockList::onAddByName));
    mCommitCallbackRegistrar.add("Block.Remove", boost::bind(&ALPanelBlockList::onRemoveSelection, this));
}

ALPanelBlockList::~ALPanelBlockList()
{
    LLMuteList::getInstance()->removeObserver(this);
}

// virtual
bool ALPanelBlockList::postBuild()
{
    setVisibleCallback(boost::bind(&ALPanelBlockList::removePicker, this));

    m_pBlockList = findChild<LLScrollListCtrl>("block_list");
    m_pBlockList->setCommitOnDelete(true);
    m_pBlockList->setCommitOnSelectionChange(true);
    m_pBlockList->setCommitCallback(boost::bind(&ALPanelBlockList::onSelectionChange, this));

    // Restore last sort order
    if (U32 nSortValue = gSavedSettings.getU32("BlockMuteSortOrder"))
        m_pBlockList->sortByColumnIndex(nSortValue >> 4, nSortValue & 0xF);
    m_pBlockList->setSortChangedCallback(boost::bind(&ALPanelBlockList::onColumnSortChange, this));

    m_pTrashBtn = findChild<LLButton>("block_trash_btn");

    LLMuteList::getInstance()->addObserver(this);

    return LLPanel::postBuild();
}

// virtual
void ALPanelBlockList::onOpen(const LLSD& sdParam)
{
    refresh();

    if ( (sdParam.has(BLOCKED_PARAM_NAME)) && (sdParam[BLOCKED_PARAM_NAME].asUUID().notNull()) )
    {
        LLMute muteEntry(sdParam[BLOCKED_PARAM_NAME].asUUID());
        selectEntry(muteEntry);
    }
}

void ALPanelBlockList::refresh()
{
    const LLSD& sdSel = m_pBlockList->getSelectedValue();

    LLSD sdGenericRow; LLSD& sdGenericColumns = sdGenericRow["columns"];
    sdGenericColumns[0]["column"] = "item_name"; sdGenericColumns[0]["type"] = "text";
    sdGenericColumns[1]["column"] = "item_type"; sdGenericColumns[1]["type"] = "text";

    LLSD sdAgentRow(sdGenericRow); LLSD& sdAgentColumns = sdAgentRow["columns"];
    sdAgentColumns[2]["column"] = "item_text"; sdAgentColumns[2]["type"] = "checkbox";
    sdAgentColumns[3]["column"] = "item_voice"; sdAgentColumns[3]["type"] = "checkbox";
    sdAgentColumns[4]["column"] = "item_particles"; sdAgentColumns[4]["type"] = "checkbox";
    sdAgentColumns[5]["column"] = "item_sounds"; sdAgentColumns[5]["type"] = "checkbox";

    m_pBlockList->deleteAllItems();
    const std::vector<LLMute> muteEntries = LLMuteList::getInstance()->getMutes();
    for (const LLMute& muteEntry : muteEntries)
    {
        if ( (!m_strFilter.empty()) && (!boost::icontains(muteEntry.mName, m_strFilter)) )
            continue;

        switch (muteEntry.mType)
        {
            case LLMute::AGENT:
                {
                    sdAgentRow["value"] = LLSD().with("id", muteEntry.mID).with("name", muteEntry.mName);
                    sdAgentColumns[0]["value"] = muteEntry.mName;
                    sdAgentColumns[1]["value"] = muteEntry.getDisplayType();
                    sdAgentColumns[2]["value"] = (muteEntry.mFlags & LLMute::flagTextChat) == 0;
                    sdAgentColumns[3]["value"] = (muteEntry.mFlags & LLMute::flagVoiceChat) == 0;
                    sdAgentColumns[4]["value"] = (muteEntry.mFlags & LLMute::flagParticles) == 0;
                    sdAgentColumns[5]["value"] = (muteEntry.mFlags & LLMute::flagObjectSounds) == 0;
                    m_pBlockList->addElement(sdAgentRow, boost::bind(&ALPanelBlockList::onToggleMuteFlag, this, _1, _2), ADD_BOTTOM);
                }
                break;
            default:
                {
                    sdGenericRow["value"] = LLSD().with("id", muteEntry.mID).with("name", muteEntry.mName);
                    sdGenericColumns[0]["value"] = muteEntry.mName;
                    sdGenericColumns[1]["value"] = muteEntry.getDisplayType();
                    m_pBlockList->addElement(sdGenericRow, ADD_BOTTOM);
                }
                break;
        }
    }
    selectEntry(sdSel);
}

void ALPanelBlockList::removePicker()
{
    if (m_hPicker.get())
    {
        m_hPicker.get()->closeFloater();
    }
}

void ALPanelBlockList::selectEntry(const LLMute& muteEntry)
{
    const std::vector<LLScrollListItem*> muteItems = m_pBlockList->getAllData();
    for (auto itItem = muteItems.begin(); itItem != muteItems.end(); ++itItem)
    {
        const LLSD& sdValue = (*itItem)->getValue();
        if ( (muteEntry.mID == sdValue["id"].asUUID()) && ((muteEntry.mName.empty()) ||(muteEntry.mName == sdValue["name"].asString())) )
        {
            S32 idxItem = S32(itItem - muteItems.begin());
            if (idxItem != m_pBlockList->getFirstSelectedIndex())
            {
                m_pBlockList->deselectAllItems();
                m_pBlockList->selectNthItem(idxItem);
                break;
            }
        }
    }
}

void ALPanelBlockList::setFilterString(const std::string& strFilter)
{
    m_strFilter = strFilter;
    refresh();
}

void ALPanelBlockList::updateButtons()
{
    bool fHasSelection = (nullptr != m_pBlockList->getFirstSelected());
    m_pTrashBtn->setEnabled(fHasSelection);
}

void ALPanelBlockList::onChange()
{
    if (m_fRefreshOnChange)
    {
        refresh();
    }
}

void ALPanelBlockList::onAddAvatar(LLUICtrl* pCtrl)
{
    if (!m_hPicker.isDead())
    {
        m_hPicker.get()->setVisibleAndFrontmost(true);
        return;
    }

    if (LLFloater* pRootFloater = gFloaterView->getParentFloater(pCtrl))
    {
        LLFloaterAvatarPicker* pPicker = LLFloaterAvatarPicker::show(
            boost::bind(&ALPanelBlockList::onAddAvatarCallback, _1, _2),
            false /*allow_multiple*/, true /*close_on_select*/, false /*skip_agent*/, pRootFloater->getName(), pCtrl);
        pRootFloater->addDependentFloater(pPicker);
        m_hPicker = pPicker->getHandle();
    }
}

// static
void ALPanelBlockList::onAddAvatarCallback(const uuid_vec_t& idAgents, const std::vector<LLAvatarName>& avAgents)
{
    if ( (idAgents.empty()) || (avAgents.empty()) )
    {
        return;
    }

    LLMute mute(idAgents[0], avAgents[0].getLegacyName(), LLMute::AGENT);
    if (LLMuteList::getInstance()->add(mute))
    {
        ALFloaterBlocked::showMuteAndSelect(mute.mID);
    }
}

// static
void ALPanelBlockList::onAddByName()
{
    LLFloaterGetBlockedObjectName::show(&ALPanelBlockList::onAddByNameCallback);
}

// static
void ALPanelBlockList::onAddByNameCallback(const std::string& strBlockName)
{
    if (strBlockName.empty())
    {
        return;
    }

    LLMute mute(LLUUID::null, strBlockName, LLMute::BY_NAME);
    if (!LLMuteList::getInstance()->add(mute))
    {
        LLNotificationsUtil::add("MuteByNameFailed");
    }
}

void ALPanelBlockList::onRemoveSelection()
{
    m_fRefreshOnChange = false;

    std::vector<LLScrollListItem*> selItems = m_pBlockList->getAllSelected();
    for (std::vector<LLScrollListItem*>::iterator itItem = selItems.begin(); itItem != selItems.end(); ++itItem)
    {
        LLScrollListItem* pSelItem = *itItem; const LLSD sdValue = pSelItem->getValue();

        LLMute selMute(sdValue["id"].asUUID(), sdValue["name"].asString());
        if (LLMuteList::getInstance()->remove(selMute))
        {
            m_pBlockList->deleteSingleItem(pSelItem);
        }
    }

    m_fRefreshOnChange = true;
}

void ALPanelBlockList::onColumnSortChange()
{
    U32 nSortValue = 0;

    S32 idxColumn = m_pBlockList->getSortColumnIndex();
    if (-1 != idxColumn)
    {
        nSortValue = idxColumn << 4 | ((m_pBlockList->getSortAscending()) ? 1 : 0);
    }

    gSavedSettings.setU32("BlockMuteSortOrder", nSortValue);
}

void ALPanelBlockList::onIdleRefresh(LLHandle<LLPanel> hPanel)
{
    if (!hPanel.isDead())
    {
        ((ALPanelBlockList*)hPanel.get())->refresh();
    }
}

void ALPanelBlockList::onSelectionChange()
{
    updateButtons();
}

void ALPanelBlockList::onToggleMuteFlag(const LLSD& sdValue, const LLScrollListCell* pCell)
{
    if (!pCell)
        return;

    LLMute muteEntry(sdValue["id"].asUUID(), sdValue["name"].asString(), LLMute::AGENT); U32 muteFlag = 0;

    const std::string& strColumnName = pCell->getColumnName();
    if ("item_text" == strColumnName)
        muteFlag = LLMute::flagTextChat;
    else if ("item_voice" == strColumnName)
        muteFlag = LLMute::flagVoiceChat;
    else if ("item_particles" == strColumnName)
        muteFlag = LLMute::flagParticles;
    else if ("item_sounds" == strColumnName)
        muteFlag = LLMute::flagObjectSounds;

    if (muteFlag)
    {
        m_fRefreshOnChange = false;
        if (pCell->getValue().asBoolean())
            LLMuteList::getInstance()->add(muteEntry, muteFlag);
        else
            LLMuteList::getInstance()->remove(muteEntry, muteFlag);
        m_fRefreshOnChange = true;

        selectEntry(muteEntry);

        // Refreshing now will invalidate an iterator upstream so do it on the next idle tick
        doOnIdleOneTime(boost::bind(&ALPanelBlockList::onIdleRefresh, getHandle()));
    }
}

// ============================================================================
// ALPanelDerenderList
//

static LLPanelInjector<ALPanelDerenderList> t_panel_derender_list("panel_derender_list");

ALPanelDerenderList::ALPanelDerenderList()
    : LLPanel()
    , m_pDerenderList(nullptr)
{
}

ALPanelDerenderList::~ALPanelDerenderList()
{
    m_DerenderChangeConn.disconnect();
}

bool ALPanelDerenderList::postBuild()
{
    m_pDerenderList = findChild<LLScrollListCtrl>("derender_list");
    m_pDerenderList->setCommitCallback(boost::bind(&ALPanelDerenderList::onSelectionChange, this));
    m_pDerenderList->setCommitOnDelete(true);
    m_pDerenderList->setCommitOnSelectionChange(true);

    // Restore last sort order
    U32 nSortValue = gSavedSettings.getU32("BlockDerenderSortOrder");
    if (nSortValue)
    {
        m_pDerenderList->sortByColumnIndex(nSortValue >> 4, nSortValue & 0xF);
    }
    m_pDerenderList->setSortChangedCallback(boost::bind(&ALPanelDerenderList::onColumnSortChange, this));

    m_DerenderChangeConn = ALDerenderList::setChangeCallback(boost::bind(&ALPanelDerenderList::refresh, this));
    findChild<LLUICtrl>("derender_trash_btn")->setCommitCallback(boost::bind(&ALPanelDerenderList::onSelectionRemove, this));

    return LLPanel::postBuild();
}

void ALPanelDerenderList::onOpen(const LLSD& sdParam)
{
    refresh();

    if ( (sdParam.has(DERENDER_PARAM_NAME)) && (sdParam[DERENDER_PARAM_NAME].asUUID().notNull()) )
    {
        m_pDerenderList->selectByID(sdParam[DERENDER_PARAM_NAME].asUUID());
    }
}

void ALPanelDerenderList::onColumnSortChange()
{
    U32 nSortValue = 0;

    S32 idxColumn = m_pDerenderList->getSortColumnIndex();
    if (-1 != idxColumn)
    {
        nSortValue = idxColumn << 4 | ((m_pDerenderList->getSortAscending()) ? 1 : 0);
    }

    gSavedSettings.setU32("BlockDerenderSortOrder", nSortValue);
}

void ALPanelDerenderList::onSelectionChange()
{
    bool hasSelected = (nullptr != m_pDerenderList->getFirstSelected());
    getChildView("derender_trash_btn")->setEnabled(hasSelected);
}

void ALPanelDerenderList::onSelectionRemove()
{
    std::vector<LLScrollListItem*> selItems = m_pDerenderList->getAllSelected(); uuid_vec_t idsObject;
//  std::for_each(selItems.begin(), selItems.end(), [&idsObject](const LLScrollListItem* i) { idsObject.push_back(i->getValue().asUUID()); });
    for (std::vector<LLScrollListItem*>::iterator itItem = selItems.begin(); itItem != selItems.end(); ++itItem)
    {
        idsObject.push_back((*itItem)->getValue().asUUID());
    }

    ALDerenderList::instance().removeObjects(ALDerenderEntry::TYPE_OBJECT, idsObject);
}

void ALPanelDerenderList::refresh()
{
    m_pDerenderList->clearRows();
    if (ALDerenderList::instanceExists())
    {
        LLSD sdRow; LLSD& sdColumns = sdRow["columns"];
        sdColumns[0]["column"] = "object_name";   sdColumns[0]["type"] = "text";
        sdColumns[1]["column"] = "location";      sdColumns[1]["type"] = "text";
        sdColumns[2]["column"] = "derender_type"; sdColumns[2]["type"] = "text";

        for (const auto& pEntry : ALDerenderList::instance().getEntries())
        {
            sdRow["value"] = pEntry->getID();
            sdColumns[0]["value"] = pEntry->getName();
            if (ALDerenderEntry::TYPE_OBJECT == pEntry->getType())
            {
                auto pObjEntry = static_cast<ALDerenderObject*>(pEntry.get());
                sdColumns[1]["value"] = llformat("%s <%f, %f, %f>", pObjEntry->strRegionName.c_str(), ll_round(pObjEntry->posRegion.mV[VX]), ll_round(pObjEntry->posRegion.mV[VY]), ll_round(pObjEntry->posRegion.mV[VZ]));
            }
            sdColumns[2]["value"] = (pEntry->isPersistent()) ? LLTrans::getString("Permanent") : LLTrans::getString("Temporary");

            m_pDerenderList->addElement(sdRow, ADD_BOTTOM);
        }
    }
}

// ============================================================================
// ALPanelAssetBlocklist
//

static LLPanelInjector<ALPanelAssetBlocklist> t_panel_asset_list("panel_asset_block_list");

ALPanelAssetBlocklist::ALPanelAssetBlocklist()
    : LLPanel()
{
}

ALPanelAssetBlocklist::~ALPanelAssetBlocklist()
{
    mAssetBlocklistChangeConn.disconnect();
}

bool ALPanelAssetBlocklist::postBuild()
{
    mAssetBlocklist = findChild<LLScrollListCtrl>("asset_list");
    mAssetBlocklist->setCommitCallback(boost::bind(&ALPanelAssetBlocklist::onSelectionChange, this));
    mAssetBlocklist->setCommitOnDelete(true);
    mAssetBlocklist->setCommitOnSelectionChange(true);

    // Restore last sort order
    U32 nSortValue = gSavedSettings.getU32("BlockAssetSortOrder");
    if (nSortValue)
    {
        mAssetBlocklist->sortByColumnIndex(nSortValue >> 4, nSortValue & 0xF);
    }
    mAssetBlocklist->setSortChangedCallback(boost::bind(&ALPanelAssetBlocklist::onColumnSortChange, this));

    mAssetBlocklistChangeConn = ALAssetBlocklist::instance().setChangeCallback(boost::bind(&ALPanelAssetBlocklist::refresh, this));
    findChild<LLUICtrl>("asset_trash_btn")->setCommitCallback(boost::bind(&ALPanelAssetBlocklist::onSelectionRemove, this));

    return LLPanel::postBuild();
}

void ALPanelAssetBlocklist::onOpen(const LLSD& sdParam)
{
    refresh();

    if ((sdParam.has(ASSET_PARAM_NAME)) && (sdParam[ASSET_PARAM_NAME].asUUID().notNull()))
    {
        mAssetBlocklist->selectByID(sdParam[ASSET_PARAM_NAME].asUUID());
    }
}

void ALPanelAssetBlocklist::onColumnSortChange()
{
    U32 nSortValue = 0;

    S32 idxColumn = mAssetBlocklist->getSortColumnIndex();
    if (-1 != idxColumn)
    {
        nSortValue = idxColumn << 4 | ((mAssetBlocklist->getSortAscending()) ? 1 : 0);
    }

    gSavedSettings.setU32("BlockAssetSortOrder", nSortValue);
}

void ALPanelAssetBlocklist::onSelectionChange()
{
    bool hasSelected = (nullptr != mAssetBlocklist->getFirstSelected());
    getChildView("asset_trash_btn")->setEnabled(hasSelected);
}

void ALPanelAssetBlocklist::onSelectionRemove()
{
    uuid_vec_t ids;
    std::vector<LLScrollListItem*> selItems = mAssetBlocklist->getAllSelected();
    for (LLScrollListItem* itemp : selItems)
    {
        ids.push_back(itemp->getValue().asUUID());
    }

    ALAssetBlocklist::instance().removeEntries(ids);
}

void ALPanelAssetBlocklist::refresh()
{
    mAssetBlocklist->clearRows();
    if (ALAssetBlocklist::instanceExists())
    {
        LLSD sdRow; LLSD& sdColumns = sdRow["columns"];
        sdColumns[0]["column"] = "name";  sdColumns[0]["type"] = "text";
        sdColumns[1]["column"] = "location"; sdColumns[1]["type"] = "text";
        sdColumns[2]["column"] = "asset_type";    sdColumns[2]["type"] = "text";
        sdColumns[3]["column"] = "date";  sdColumns[3]["type"] = "text";
        sdColumns[4]["column"] = "persist";  sdColumns[4]["type"] = "text";

        for (const auto& [key, data] : ALAssetBlocklist::instance().getEntries())
        {
            sdRow["value"] = key;

            std::string owner_name = "Unknown";
            LLAvatarName avName;
            if (LLAvatarNameCache::get(data.mOwnerID, &avName))
            {
                owner_name = avName.getUserName();
            }
            sdColumns[0]["value"] = owner_name;
            sdColumns[1]["value"] = data.mLocation;
            sdColumns[2]["value"] = LLTrans::getString(LLAssetType::lookupHumanReadable(data.mAssetType));

            std::string timeStr = getString("blockedDate");
            LLSD substitution;
            substitution["datetime"] = (S32)data.mDate.secondsSinceEpoch();
            LLStringUtil::format(timeStr, substitution);

            sdColumns[3]["value"] = timeStr;
            sdColumns[4]["value"] = data.mPersist ? LLTrans::getString("Permanent") : LLTrans::getString("Temporary");

            mAssetBlocklist->addElement(sdRow, ADD_BOTTOM);
        }
    }
}

// ============================================================================
// ALPanelAvatarRendering - Menu helper class
//

class ALPanelAvatarRenderingContextMenu : public LLListContextMenu
{
public:
    ALPanelAvatarRenderingContextMenu(ALPanelAvatarRendering* pAvRenderingPanel)
        : m_pAvRenderingPanel(pAvRenderingPanel)
    {
    }

protected:
    LLContextMenu* createMenu() override
    {
        LLUICtrl::CommitCallbackRegistry::ScopedRegistrar registrar;
        registrar.add("Settings.SetRendering", boost::bind(&ALPanelAvatarRendering::onSetException, m_pAvRenderingPanel, mUUIDs.front(), _2));

        LLUICtrl::EnableCallbackRegistry::ScopedRegistrar enable_registrar;
        enable_registrar.add("Settings.IsSelected", boost::bind(&ALPanelAvatarRendering::onHasException, m_pAvRenderingPanel, mUUIDs.front(), _2));

        LLContextMenu* pMenu = createFromFile("menu_avatar_rendering_settings.xml");
        return pMenu;
    }

protected:
    ALPanelAvatarRendering* m_pAvRenderingPanel;
};

// ============================================================================
// ALPanelAvatarRendering - Helper functions
//

static std::string createTimestamp(S32 datetime)
{
    std::string timeStr = "[" + LLTrans::getString("TimeMonth") + "]/[" + LLTrans::getString("TimeDay") + "]/[" + LLTrans::getString("TimeYear") + "]";
    LLStringUtil::format(timeStr, LLSD().with("datetime", datetime));
    return timeStr;
}

static LLVOAvatar* findAvatar(const LLUUID& idAgent)
{
    LLViewerObject* pAvObj = gObjectList.findObject(idAgent);
    while ( (pAvObj) && (pAvObj->isAttachment()) )
        pAvObj = (LLViewerObject*)pAvObj->getParent();

    if ((pAvObj) && (pAvObj->isAvatar()) )
        return (LLVOAvatar*)pAvObj;
    return nullptr;
}

static void setAvatarRenderSetting(const LLUUID& idAgent, LLVOAvatar::VisualMuteSettings eSetting)
{
    if (LLVOAvatar* pAvatar = findAvatar(idAgent))
        pAvatar->setVisualMuteSettings(LLVOAvatar::VisualMuteSettings(eSetting));
    else
        LLRenderMuteList::getInstance()->saveVisualMuteSetting(idAgent, eSetting);
}

// ============================================================================
// ALPanelAvatarRendering - Configure avatar complexity excpetions
//

static LLPanelInjector<ALPanelAvatarRendering> t_panel_block_avatar_rendering("panel_block_avatar_rendering");

ALPanelAvatarRendering::ALPanelAvatarRendering()
    : LLPanel()
{
    mCommitCallbackRegistrar.add("Rendering.AddException", boost::bind(&ALPanelAvatarRendering::onAddException, this, _1, _2));
    mCommitCallbackRegistrar.add("Rendering.RemoveException", boost::bind(&ALPanelAvatarRendering::onRemoveException, this));
}

ALPanelAvatarRendering::~ALPanelAvatarRendering()
{
    delete m_pContextMenu;
    LLRenderMuteList::getInstance()->removeObserver(this);
}

// virtual
bool ALPanelAvatarRendering::postBuild()
{
    setVisibleCallback(boost::bind(&ALPanelAvatarRendering::removePicker, this));

    m_pExceptionList = findChild<LLNameListCtrl>("exception_list");
    m_pExceptionList->setCommitOnDelete(true);
    m_pExceptionList->setCommitOnSelectionChange(true);
    m_pExceptionList->setCommitCallback(boost::bind(&ALPanelAvatarRendering::onSelectionChange, this));
    m_pExceptionList->setRightMouseDownCallback(boost::bind(&ALPanelAvatarRendering::onExceptionMenu, this, _2, _3));

    // Restore last sort order
    if (U32 nSortValue = gSavedSettings.getU32("BlockRenderingSortOrder"))
        m_pExceptionList->sortByColumnIndex(nSortValue >> 4, nSortValue & 0xF);
    m_pExceptionList->setSortChangedCallback(boost::bind(&ALPanelAvatarRendering::onColumnSortChange, this));

    m_pTrashBtn = findChild<LLButton>("rendering_exception_trash_btn");

    LLRenderMuteList::getInstance()->addObserver(this);
    m_pContextMenu = new ALPanelAvatarRenderingContextMenu(this);

    return LLPanel::postBuild();
}

// virtual
void ALPanelAvatarRendering::onOpen(const LLSD& sdParam)
{
    refresh();

    if ((sdParam.has(EXCEPTION_PARAM_NAME)) && (sdParam[EXCEPTION_PARAM_NAME].asUUID().notNull()))
    {
        m_pExceptionList->selectByID(sdParam[EXCEPTION_PARAM_NAME].asUUID());
    }
}

void ALPanelAvatarRendering::refresh()
{
    const LLUUID idSel = m_pExceptionList->getSelectedValue().asUUID();

    m_pExceptionList->deleteAllItems();

    LLAvatarName avName; LLNameListCtrl::NameItem paramItem;
    for (const auto& kvEntry : LLRenderMuteList::getInstance()->sVisuallyMuteSettingsMap)
    {
        if ( (!LLAvatarNameCache::get(kvEntry.first, &avName)) || ((!m_strFilter.empty()) && (!boost::icontains(avName.getCompleteName(), m_strFilter))) )
            continue;

        paramItem.value = kvEntry.first;
        paramItem.columns.add().value(avName.getCompleteName()).column("name");
        paramItem.columns.add().value(getString(kvEntry.second == 1 ? "av_never_render" : "av_always_render")).column("setting");
        paramItem.columns.add().value(createTimestamp(LLRenderMuteList::getInstance()->getVisualMuteDate(kvEntry.first))).column("timestamp");
        m_pExceptionList->addNameItemRow(paramItem);
    }

    if (idSel.notNull())
    {
        m_pExceptionList->selectByID(idSel);
    }
}

void ALPanelAvatarRendering::removePicker()
{
    if (m_hPicker.get())
    {
        m_hPicker.get()->closeFloater();
    }
}

void ALPanelAvatarRendering::setFilterString(const std::string& strFilter)
{
    m_strFilter = strFilter;
    refresh();
}

void ALPanelAvatarRendering::updateButtons()
{
    bool fHasSelection = (nullptr != m_pExceptionList->getFirstSelected());
    m_pTrashBtn->setEnabled(fHasSelection);
}

void ALPanelAvatarRendering::onChange()
{
    if (m_fRefreshOnChange)
    {
        refresh();
    }
}

void ALPanelAvatarRendering::onAddException(LLUICtrl* pCtrl, const LLSD& sdParam)
{
    const std::string strParam = sdParam.asString();

    LLVOAvatar::VisualMuteSettings eSetting = (LLVOAvatar::VisualMuteSettings)0;
    if ("never" == strParam)
        eSetting = LLVOAvatar::AV_DO_NOT_RENDER;
    else if ("always" == strParam)
        eSetting = LLVOAvatar::AV_ALWAYS_RENDER;

    if (LLFloater* pRootFloater = gFloaterView->getParentFloater(pCtrl))
    {
        removePicker();

        LLFloaterAvatarPicker* pPicker = LLFloaterAvatarPicker::show(
            boost::bind(&ALPanelAvatarRendering::onAddExceptionCb, _1, eSetting),
            false /*allow_multiple*/, true /*close_on_select*/, false /*skip_agent*/, pRootFloater->getName(), pCtrl);
        pRootFloater->addDependentFloater(pPicker);
        m_hPicker = pPicker->getHandle();
    }
}

void ALPanelAvatarRendering::onAddExceptionCb(const uuid_vec_t& idAgents, S32 nSetting)
{
    if (idAgents.empty())
        return;

    for (const LLUUID& idAgent : idAgents)
    {
        setAvatarRenderSetting(idAgent, (LLVOAvatar::VisualMuteSettings)nSetting);
    }

    ALFloaterBlocked::showRenderExceptionAndSelect(idAgents[0]);
}

bool ALPanelAvatarRendering::onHasException(const LLUUID& idAgent, const LLSD& sdParamn)
{
    const std::string strParam = sdParamn.asString();

    LLVOAvatar::VisualMuteSettings eSetting = (LLVOAvatar::VisualMuteSettings)LLRenderMuteList::getInstance()->getSavedVisualMuteSetting(idAgent);
    if ("default" == strParam)
        return (eSetting == LLVOAvatar::AV_RENDER_NORMALLY);
    else if ("non_default" == strParam)
        return (eSetting != LLVOAvatar::AV_RENDER_NORMALLY);
    else if ("never" == strParam)
        return (eSetting == LLVOAvatar::AV_DO_NOT_RENDER);
    else if ("always" == strParam)
        return (eSetting == LLVOAvatar::AV_ALWAYS_RENDER);
    return false;
}

void ALPanelAvatarRendering::onExceptionMenu(S32 x, S32 y)
{
    m_pExceptionList->selectItemAt(x, y, MASK_NONE);

    uuid_vec_t selected_uuids;
    if (m_pExceptionList->getCurrentID().notNull())
    {
        selected_uuids.push_back(m_pExceptionList->getCurrentID());
        m_pContextMenu->show(m_pExceptionList, selected_uuids, x, y);
    }
}

void ALPanelAvatarRendering::onRemoveException()
{
    m_fRefreshOnChange = false;

    const std::vector<LLScrollListItem*> selItems = m_pExceptionList->getAllSelected();
    for (auto* pSelItem : selItems)
    {
        const LLSD sdValue = pSelItem->getValue();
        if (sdValue.isUUID())
        {
            setAvatarRenderSetting(sdValue.asUUID(), LLVOAvatar::AV_RENDER_NORMALLY);
            m_pExceptionList->deleteSingleItem(pSelItem);
        }
    }

    m_fRefreshOnChange = true;
}

void ALPanelAvatarRendering::onSetException(const LLUUID& idAgent, const LLSD& sdParamn)
{
    const std::string strParam = sdParamn.asString();

    LLVOAvatar::VisualMuteSettings nSetting = (LLVOAvatar::VisualMuteSettings)0;
    if ("default" == strParam)
        nSetting = LLVOAvatar::AV_RENDER_NORMALLY;
    else if ("never" == strParam)
        nSetting = LLVOAvatar::AV_DO_NOT_RENDER;
    else if ("always" == strParam)
        nSetting = LLVOAvatar::AV_ALWAYS_RENDER;

    setAvatarRenderSetting(idAgent, nSetting);
}

void ALPanelAvatarRendering::onColumnSortChange()
{
    U32 nSortValue = 0;

    S32 idxColumn = m_pExceptionList->getSortColumnIndex();
    if (-1 != idxColumn)
    {
        nSortValue = idxColumn << 4 | ((m_pExceptionList->getSortAscending()) ? 1 : 0);
    }

    gSavedSettings.setU32("BlockRenderingSortOrder", nSortValue);
}

void ALPanelAvatarRendering::onSelectionChange()
{
    updateButtons();
}

// ============================================================================
// ALFloaterBlocked
//

ALFloaterBlocked::ALFloaterBlocked(const LLSD& sdKey)
    : LLFloater(sdKey)
{
}

ALFloaterBlocked::~ALFloaterBlocked()
{
}

bool ALFloaterBlocked::postBuild()
{
    m_pFilterEditor = findChild<LLFilterEditor>("blocked_filter");
    m_pFilterEditor->setCommitCallback(boost::bind(&ALFloaterBlocked::onFilterEdit, this, _2));

    m_pBlockedTabs = findChild<LLTabContainer>("blocked_tabs");
    m_pBlockedTabs->setCommitCallback(boost::bind(&ALFloaterBlocked::onTabSelect, this, _2));

    return LLPanel::postBuild();
}

void ALFloaterBlocked::onOpen(const LLSD& sdParam)
{
    if (sdParam.has(BLOCKED_PARAM_NAME))
        m_pBlockedTabs->selectTabByName(BLOCKED_TAB_NAME);
    else if (sdParam.has(DERENDER_PARAM_NAME))
        m_pBlockedTabs->selectTabByName(DERENDER_TAB_NAME);
    else if (sdParam.has(ASSET_PARAM_NAME))
        m_pBlockedTabs->selectTabByName(ASSET_TAB_NAME);
    if (sdParam.has(EXCEPTION_PARAM_NAME))
        m_pBlockedTabs->selectTabByName(EXCEPTION_TAB_NAME);
    else if ( (sdParam.isString()) && (m_pBlockedTabs->hasChild(sdParam.asString())) )
        m_pBlockedTabs->selectTabByName(sdParam.asString());
    else
        m_pBlockedTabs->getCurrentPanel()->onOpen(sdParam);
    mKey.clear();
}

void ALFloaterBlocked::onFilterEdit(const std::string& strFilter)
{
    if (ALPanelBlockBase* pCurPanel = ALPanelBlockBase::fromView(m_pBlockedTabs->getCurrentPanel()))
    {
        pCurPanel->setFilterString(strFilter);
    }
}

void ALFloaterBlocked::onTabSelect(const LLSD& sdParam)
{
    LLPanel* pActivePanel = m_pBlockedTabs->getPanelByName(sdParam.asString());
    if (pActivePanel)
    {
        if (ALPanelBlockBase* pActivePanelBase = ALPanelBlockBase::fromView(pActivePanel))
            m_pFilterEditor->setText(pActivePanelBase->getFilterString());
        pActivePanel->onOpen(mKey);
    }
}

void ALFloaterBlocked::showMuteAndSelect(const LLUUID& idMute)
{
    LLFloaterReg::showInstance("blocked", LLSD().with(BLOCKED_PARAM_NAME, idMute));
}

void ALFloaterBlocked::showDerenderAndSelect(const LLUUID& idEntry)
{
    LLFloaterReg::showInstance("blocked", LLSD().with(DERENDER_PARAM_NAME, idEntry));
}

void ALFloaterBlocked::showAssetAndSelect(const LLUUID& idEntry)
{
    LLFloaterReg::showInstance("blocked", LLSD().with(ASSET_PARAM_NAME, idEntry));
}

void ALFloaterBlocked::showRenderExceptionAndSelect(const LLUUID& idEntry)
{
    LLFloaterReg::showInstance("blocked", LLSD().with(EXCEPTION_PARAM_NAME, idEntry));
}

// ============================================================================
