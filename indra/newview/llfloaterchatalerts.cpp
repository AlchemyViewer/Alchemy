/** 
 *
 * Copyright (c) 2012, Kitty Barnett
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

#include "llappviewer.h"
#include "llaudioengine.h"
#include "llassetstorage.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "llcolorswatch.h"
#include "llfloaterchatalerts.h"
#include "lliconctrl.h"
#include "llinventoryicon.h"
#include "llinventorymodel.h"
#include "lllineeditor.h"
#include "llnotificationsutil.h"
#include "llscrolllistctrl.h"
#include "lltextparser.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"

#include <boost/algorithm/string.hpp>

// ============================================================================
// LLSoundDropTarget helper class
//

static LLDefaultChildRegistry::Register<LLSoundDropTarget> r("sound_drop_target");

LLSoundDropTarget::LLSoundDropTarget(const LLSoundDropTarget::Params& p) 
	: LLView(p)
	, m_pDropSignal(NULL)
{
}

LLSoundDropTarget::~LLSoundDropTarget()
{
	delete m_pDropSignal;
}

BOOL LLSoundDropTarget::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept, std::string& tooltip_msg)
{
	BOOL fHandled = FALSE;
	if (getParent())
	{
		switch (cargo_type)
		{
			case DAD_SOUND:
				{
					const LLViewerInventoryItem* pItem = (LLViewerInventoryItem*)cargo_data;
					if (gInventory.getItem(pItem->getUUID()))
					{
						*accept = ACCEPT_YES_COPY_SINGLE;

						if (drop)
						{
							if ( (m_pDropSignal) && (!m_pDropSignal->empty()) )
								(*m_pDropSignal)(pItem->getUUID());
							else
								getParent()->notifyParent(LLSD().with("item_id", pItem->getUUID()));;
						}
					}
					else
					{
						// It's not in the user's inventory
						*accept = ACCEPT_NO;
					}
				}
				break;
			default:
				*accept = ACCEPT_NO;
				break;
		}

		fHandled = TRUE;
	}
	return fHandled;
}

boost::signals2::connection LLSoundDropTarget::setDropCallback(const drop_signal_t::slot_type& cb) 
{ 
	if (!m_pDropSignal)
		m_pDropSignal = new drop_signal_t();
	return m_pDropSignal->connect(cb); 
}

// ============================================================================
// LLFloaterChatAlerts
//

LLFloaterChatAlerts::LLFloaterChatAlerts(const LLSD& sdKey)
	: LLFloater(sdKey)
	, m_pAlertList(NULL)
	, m_fNewEntry(false)
	, m_pKeywordEditor(NULL)
	, m_pKeywordCase(NULL)
	, m_pColorCtrl(NULL)
	, m_fSoundChanged(false)
	, m_pSoundIconCtrl(NULL)
	, m_pSoundEditor(NULL)
	, m_pSoundClearBtn(NULL)
	, m_pTriggerChat(NULL)
	, m_pTriggerIM(NULL)
	, m_pTriggerGroup(NULL)
	, m_fChatAlertsEnabled(false)
	, m_fPendingSave(false)
{
}

LLFloaterChatAlerts::~LLFloaterChatAlerts()
{
	mChatAlertsConnection.disconnect();
}

BOOL LLFloaterChatAlerts::canClose()
{
	if (isEntryDirty())
	{
		m_fPendingSave = true;
		onEntrySaveChanges(LLUUID::null, true);
		return FALSE;
	}
	return TRUE;
}

void LLFloaterChatAlerts::onOpen(const LLSD& sdKey)
{
	refresh();
}

void LLFloaterChatAlerts::onClose(bool app_quitting)
{
	LLTextParser::instance().saveToDisk();
}

BOOL LLFloaterChatAlerts::postBuild(void)
{
	m_pAlertList = findChild<LLScrollListCtrl>("alerts_list");
	m_pAlertList->setCommitOnKeyboardMovement(true);
	m_pAlertList->setCommitOnSelectionChange(true);
	m_pAlertList->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onEntrySelect, this));

	findChild<LLButton>("alerts_new_btn")->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onEntryNew, this));
	findChild<LLButton>("alerts_delete_btn")->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onEntryDelete, this));
	findChild<LLButton>("alerts_save_btn")->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onEntrySave, this));
	findChild<LLButton>("alerts_revert_btn")->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onEntryRevert, this));

	m_pKeywordEditor = findChild<LLLineEditor>("alerts_keyword");
	m_pKeywordCase = findChild<LLCheckBoxCtrl>("alerts_keyword_case");
	m_pColorCtrl = findChild<LLColorSwatchCtrl>("alerts_highlight_color");

	m_pSoundIconCtrl = findChild<LLIconCtrl>("alerts_sound_icon");
	m_pSoundEditor = findChild<LLLineEditor>("alerts_sound_name");
	m_pSoundClearBtn = findChild<LLButton>("alerts_sound_clear_btn");
	m_pSoundClearBtn->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onSoundClearItem, this));

	m_pTriggerChat = findChild<LLCheckBoxCtrl>("alerts_trigger_chat");
	m_pTriggerChat->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onToggleTriggerType, this));
	m_pTriggerIM = findChild<LLCheckBoxCtrl>("alerts_trigger_im");
	m_pTriggerIM->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onToggleTriggerType, this));
	m_pTriggerGroup = findChild<LLCheckBoxCtrl>("alerts_trigger_group");
	m_pTriggerGroup->setCommitCallback(boost::bind(&LLFloaterChatAlerts::onToggleTriggerType, this));

	m_fChatAlertsEnabled = gSavedSettings.getBOOL("ChatAlerts");
	mChatAlertsConnection = gSavedSettings.getControl("ChatAlerts")->getSignal()->connect(
			boost::bind(&LLFloaterChatAlerts::onToggleChatAlerts, this, _2));

	return TRUE;
}

S32 LLFloaterChatAlerts::notifyParent(const LLSD& sdInfo)
{
	if (sdInfo.has("item_id"))
	{
		m_fSoundChanged = true;
		m_idSoundItem = sdInfo["item_id"].asUUID();
		refreshSound();
		return 1;
	}
	return LLFloater::notifyParent(sdInfo);
}

bool LLFloaterChatAlerts::isEntryDirty() const
{
	return
		(m_pKeywordEditor->isDirty()) || (m_pKeywordCase->isDirty()) || (m_pColorCtrl->isDirty()) || (m_fSoundChanged) || 
		(m_pTriggerChat->isDirty()) || (m_pTriggerIM->isDirty()) || (m_pTriggerGroup->isDirty());
}

void LLFloaterChatAlerts::onEntryNew()
{
	if (isEntryDirty())
	{
		m_fPendingSave = true;
		onEntrySaveChanges(LLUUID::null, false);
	}
	else
	{
		m_pAlertList->deselectAllItems(true);
		refreshEntry(true);
		m_pKeywordEditor->setFocus(true);
	}
}

void LLFloaterChatAlerts::onEntryDelete()
{
	const LLUUID idEntry = m_pAlertList->getFirstSelected()->getUUID();
	if (idEntry.notNull())
	{
		LLTextParser::instance().removeHighlight(idEntry);
	}
	refreshList();
}

void LLFloaterChatAlerts::onEntrySave()
{
	LLHighlightEntry* pEntry = (m_idCurEntry.notNull()) ? LLTextParser::instance().getHighlightById(m_idCurEntry) 
	                                                    : (m_fNewEntry) ? new LLHighlightEntry() : NULL;
	if ( (pEntry) && (m_pKeywordEditor->getLength() > 0) )
	{
		if ( (m_pKeywordEditor->isDirty()) || (m_fNewEntry) )
		{
			pEntry->mPattern = m_pKeywordEditor->getText();
			boost::trim(pEntry->mPattern);
		}
		if ( (m_pKeywordCase->isDirty()) || (m_fNewEntry) )
		{
			pEntry->mCaseSensitive = m_pKeywordCase->get();
		}
		if ( (m_pColorCtrl->isDirty()) || (m_fNewEntry) )
		{
			pEntry->mColor.set(m_pColorCtrl->get());
		}

		if (m_fSoundChanged)
		{
			// Store both the item and asset UUID for now
			const LLViewerInventoryItem* pItem = gInventory.getItem(m_idSoundItem);
			pEntry->mSoundAsset = (pItem) ? pItem->getAssetUUID() : LLUUID::null;
			pEntry->mSoundItem = m_idSoundItem;
		}

		if ( (m_pTriggerChat->isDirty()) || (m_fNewEntry) )
		{
			if (m_pTriggerChat->get())
				pEntry->mCategoryMask |= LLHighlightEntry::CAT_NEARBYCHAT;
			else
				pEntry->mCategoryMask &= ~LLHighlightEntry::CAT_NEARBYCHAT;
		}
		if ( (m_pTriggerIM->isDirty()) || (m_fNewEntry) )
		{
			if (m_pTriggerIM->get())
				pEntry->mCategoryMask |= LLHighlightEntry::CAT_IM;
			else
				pEntry->mCategoryMask &= ~LLHighlightEntry::CAT_IM;
		}
		if ( (m_pTriggerGroup->isDirty()) || (m_fNewEntry) )
		{
			if (m_pTriggerGroup->get())
				pEntry->mCategoryMask |= LLHighlightEntry::CAT_GROUP;
			else
				pEntry->mCategoryMask &= ~LLHighlightEntry::CAT_GROUP;
		}

		if (m_fNewEntry)
		{
			LLTextParser::instance().addHighlight(*pEntry);

			refreshList();
			m_pAlertList->setSelectedByValue(pEntry->getId(), true);

			delete pEntry;
		}
		else
		{
			refreshEntry(false);
		}
	}
}

void LLFloaterChatAlerts::onEntrySaveChanges(const LLUUID& idNewEntry, bool fCloseFloater)
{
	// HACK: the scroll list control seems to trigger the commit callback multiple times over different mouse events
	//       so we try and get rid of duplicate notifcations by delaying showing them slightly
	if (m_fPendingSave)
	{
		m_fPendingSave = false;

		// We don't want to trigger the commit callback
		m_pAlertList->setCommitOnSelectionChange(false);
		m_pAlertList->setSelectedByValue(m_idCurEntry, true);
		m_pAlertList->setCommitOnSelectionChange(true);

		LLNotificationsUtil::add("SaveChanges", LLSD(), LLSD().with("cur_sel", m_idCurEntry).with("new_sel", idNewEntry).with("close", fCloseFloater),  
			boost::bind(&LLFloaterChatAlerts::onEntrySaveChangesCallback, this, _1, _2));
	}
}

void LLFloaterChatAlerts::onEntrySaveChangesCallback(const LLSD& notification, const LLSD& response)
{
	const LLUUID idCurSel = notification["payload"]["cur_sel"].asUUID();
	const LLUUID idNewSel = notification["payload"]["new_sel"].asUUID();

	// Only process if the current item still matches
	if (idCurSel == m_idCurEntry)
	{
		S32 idxOption = LLNotificationsUtil::getSelectedOption(notification, response);
		switch(idxOption)
		{
			case 0:   // "Yes"
				onEntrySave();
			case 1:   // "No"
				if (idNewSel.notNull())
				{
					// Select existing
					m_pAlertList->setCommitOnSelectionChange(false);
					m_pAlertList->setSelectedByValue(idNewSel, true);
					m_pAlertList->setCommitOnSelectionChange(true);
					refreshEntry(false);
				}
				else
				{
					// Create new
					m_pAlertList->deselectAllItems(true);
					refreshEntry(true);
					m_pKeywordEditor->setFocus(true);
				}

				if (notification["payload"]["close"].asBoolean())
				{
					closeFloater();
				}

				break;
			case 2:   // "Cancel"
			default:
		        LLAppViewer::instance()->abortQuit();
				break;
		}
	}
}

void LLFloaterChatAlerts::onEntryRevert()
{
	refreshEntry(false);
}

void LLFloaterChatAlerts::onEntrySelect()
{
	// Don't do anything if the user clicked on the current entry a second time
	if (m_idCurEntry == m_pAlertList->getSelectedValue().asUUID())
	{
		return;
	}

	if (isEntryDirty())
	{
		m_fPendingSave = true;
		doOnIdleOneTime(boost::bind(&LLFloaterChatAlerts::onEntrySaveChanges, this, m_pAlertList->getSelectedValue().asUUID(), false));
	}
	else
	{
		refreshEntry(false);
		m_pKeywordEditor->setFocus(true);
	}
}

void LLFloaterChatAlerts::onSoundClearItem()
{
	m_fSoundChanged = true;
	m_idSoundItem.setNull();
	refreshSound();
}

void LLFloaterChatAlerts::onToggleChatAlerts(const LLSD& sdValue)
{
	m_fChatAlertsEnabled = sdValue.asBoolean();
	refresh();
}

void LLFloaterChatAlerts::onToggleTriggerType()
{
	// Make sure at least one of the trigger checkboxes is checked at all times
	if ( (!m_pTriggerChat->get()) && (!m_pTriggerIM->get()) && (!m_pTriggerGroup->get()) )
	{
		m_pTriggerChat->set(true);
	}
}

void LLFloaterChatAlerts::refresh()
{
	m_pAlertList->setEnabled(m_fChatAlertsEnabled);
	m_pAlertList->clearRows();

	findChild<LLButton>("alerts_new_btn")->setEnabled(m_fChatAlertsEnabled);

	refreshList();
}

void LLFloaterChatAlerts::refreshList()
{
	m_pAlertList->clearRows();

	// Set-up a row we can just reuse
	LLSD sdRow; 
	LLSD& sdColumns = sdRow["columns"];
	sdColumns[0]["column"] = "alert_keyword";
	sdColumns[0]["type"] = "text";

	const LLTextParser::highlight_list_t& highlights = LLTextParser::instance().getHighlights();
	for (LLTextParser::highlight_list_t::const_iterator itHighlight = highlights.begin(); 
			itHighlight != highlights.end(); ++itHighlight)
	{
		const LLHighlightEntry& entry = *itHighlight;

		sdColumns[0]["value"] = entry.mPattern;
		sdRow["value"] = entry.getId();

		m_pAlertList->addElement(sdRow, ADD_BOTTOM);
	}
	m_pAlertList->sortByColumnIndex(0, true);
	m_pAlertList->setEnabled(m_fChatAlertsEnabled);

	refreshEntry(false);
}

void LLFloaterChatAlerts::refreshEntry(bool fNewEntry)
{
	m_fNewEntry = fNewEntry;
	m_idCurEntry = m_pAlertList->getSelectedValue().asUUID();;
	const LLHighlightEntry* pEntry = (m_idCurEntry.notNull()) ? LLTextParser::instance().getHighlightById(m_idCurEntry) : NULL;
	bool fEnable = (NULL != pEntry) || (fNewEntry);

	findChild<LLButton>("alerts_delete_btn")->setEnabled(m_idCurEntry.notNull());
	findChild<LLButton>("alerts_save_btn")->setEnabled(fEnable);
	findChild<LLButton>("alerts_revert_btn")->setEnabled(m_idCurEntry.notNull());

	findChild<LLUICtrl>("alerts_keyword_label")->setEnabled(fEnable);
	m_pKeywordEditor->setEnabled(fEnable);
	m_pKeywordEditor->setText( (pEntry) ? pEntry->mPattern : "" );
	m_pKeywordEditor->resetDirty();
	m_pKeywordCase->setEnabled(fEnable);
	m_pKeywordCase->set( (pEntry) ? pEntry->mCaseSensitive : false );
	m_pKeywordCase->resetDirty();
	findChild<LLUICtrl>("alerts_color_label")->setEnabled(fEnable);
	m_pColorCtrl->setEnabled(fEnable);
	m_pColorCtrl->set( (pEntry) ? pEntry->mColor : LLColor4::white );
	m_pColorCtrl->resetDirty();

	findChild<LLView>("sound_drop_target")->setEnabled(fEnable);
	findChild<LLUICtrl>("alerts_sound_label")->setEnabled(fEnable);
	m_fSoundChanged = false;
	m_idSoundItem = (pEntry) ? pEntry->mSoundItem : LLUUID::null;
	refreshSound();

	findChild<LLUICtrl>("alerts_trigger_label")->setEnabled(fEnable);
	m_pTriggerChat->setEnabled(fEnable);
	m_pTriggerChat->set( (pEntry) ? pEntry->mCategoryMask & LLHighlightEntry::CAT_NEARBYCHAT: fNewEntry );
	m_pTriggerChat->resetDirty();
	m_pTriggerIM->setEnabled(fEnable);
	m_pTriggerIM->set( (pEntry) ? pEntry->mCategoryMask & LLHighlightEntry::CAT_IM : fNewEntry );
	m_pTriggerIM->resetDirty();
	m_pTriggerGroup->setEnabled(fEnable);
	m_pTriggerGroup->set( (pEntry) ? pEntry->mCategoryMask & LLHighlightEntry::CAT_GROUP : fNewEntry );
	m_pTriggerGroup->resetDirty();
}

void LLFloaterChatAlerts::refreshSound()
{
	const LLViewerInventoryItem* pItem = gInventory.getItem(m_idSoundItem);
	if (pItem)
	{
		std::string strIconName = LLInventoryIcon::getIconName(pItem->getType(), pItem->getInventoryType(), pItem->getFlags());

		m_idSoundItem = pItem->getUUID();
		m_pSoundIconCtrl->setValue(strIconName);
		m_pSoundIconCtrl->setVisible(true);
		m_pSoundEditor->setText(pItem->getName());
		m_pSoundClearBtn->setVisible(true);

		if ( (gAssetStorage) && (!gAssetStorage->hasLocalAsset(pItem->getAssetUUID(), LLAssetType::AT_SOUND)) && (gAudiop) )
		{
			gAssetStorage->getAssetData(pItem->getAssetUUID(), LLAssetType::AT_SOUND, LLAudioEngine::assetCallback, NULL);
		}
	}
	else
	{
		m_pSoundIconCtrl->setVisible(false);
		m_pSoundEditor->setText( (m_idSoundItem.notNull()) ? m_idSoundItem.asString() : LLStringUtil::null );
		m_pSoundClearBtn->setVisible(false);
	}
}

// ============================================================================
