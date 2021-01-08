/**
 *
 * Copyright (c) 2011-2016, Kitty Barnett
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

#include "llagent.h"
#include "llcheckboxctrl.h"
#include "lldiriterator.h"
#include "llfloaterreg.h"
#include "llfloaterperms.h"
#include "llfolderview.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llscrolllistctrl.h"
#include "llviewerassettype.h"
#include "llviewerinventory.h"
#include "llviewerregion.h"
#include "llviewerassetupload.h"

#include "llfloaterassetrecovery.h"

// ============================================================================
// LLFloaterAssetRecovery
//

LLFloaterAssetRecovery::LLFloaterAssetRecovery(const LLSD& sdKey)
	: LLFloater(sdKey)
{
}

void LLFloaterAssetRecovery::onOpen(const LLSD& sdKey)
{
	LLScrollListCtrl* pListCtrl = findChild<LLScrollListCtrl>("item_list");

	LLSD sdBhvrRow; LLSD& sdBhvrColumns = sdBhvrRow["columns"];
	sdBhvrColumns[0] = LLSD().with("column", "item_check").with("type", "checkbox");
	sdBhvrColumns[1] = LLSD().with("column", "item_name").with("type", "text");
	sdBhvrColumns[2] = LLSD().with("column", "item_type").with("type", "text");

	pListCtrl->clearRows();
	for (LLSD::array_const_iterator itFile = sdKey["files"].beginArray(), endFile = sdKey["files"].endArray(); 
			itFile != endFile;  ++itFile)
	{
		const LLSD& sdFile = *itFile;

		sdBhvrRow["value"] = sdFile;
		sdBhvrColumns[0]["value"] = true;
		sdBhvrColumns[1]["value"] = sdFile["name"];
		sdBhvrColumns[2]["value"] = sdFile["type"];

		pListCtrl->addElement(sdBhvrRow, ADD_BOTTOM);
	}
}

BOOL LLFloaterAssetRecovery::postBuild()
{
	findChild<LLUICtrl>("recover_btn")->setCommitCallback(boost::bind(&LLFloaterAssetRecovery::onBtnRecover, this));
	findChild<LLUICtrl>("cancel_btn")->setCommitCallback(boost::bind(&LLFloaterAssetRecovery::onBtnCancel, this));

	return TRUE;
}

void LLFloaterAssetRecovery::onBtnCancel()
{
	LLScrollListCtrl* pListCtrl = findChild<LLScrollListCtrl>("item_list");

	// Delete all listed files
	std::vector<LLScrollListItem*> items = pListCtrl->getAllData();
	for (std::vector<LLScrollListItem*>::const_iterator itItem = items.begin(); itItem != items.end(); ++itItem)
	{
		LLFile::remove((*itItem)->getValue()["path"].asString());
	}

	closeFloater();
}

void LLFloaterAssetRecovery::onBtnRecover()
{
	LLScrollListCtrl* pListCtrl = findChild<LLScrollListCtrl>("item_list");

	// Recover all selected, delete any unselected
	std::vector<LLScrollListItem*> items = pListCtrl->getAllData(); LLSD sdFiles;
	for (std::vector<LLScrollListItem*>::const_iterator itItem = items.begin(); itItem != items.end(); ++itItem)
	{
		LLScrollListCheck* pCheckColumn = dynamic_cast<LLScrollListCheck*>((*itItem)->getColumn(0));
		if (!pCheckColumn)
			continue;

		const LLSD sdFile = (*itItem)->getValue();
		if (pCheckColumn->getCheckBox()->getValue().asBoolean())
			sdFiles.append(sdFile);
		else
			LLFile::remove(sdFile["path"]);
	}

	if (!sdFiles.emptyArray())
		new LLAssetRecoverQueue(sdFiles);

	closeFloater();
}

// ============================================================================
// LLCreateRecoverAssetCallback
//

class LLCreateRecoverAssetCallback : public LLInventoryCallback
{
public:
	LLCreateRecoverAssetCallback(LLAssetRecoverQueue* pRecoverQueue)
		: LLInventoryCallback(), mRecoverQueue(pRecoverQueue)
	{
	}

	void fire(const LLUUID& idItem)
	{
		mRecoverQueue->onCreateItem(idItem);
	}

protected:
	LLAssetRecoverQueue* mRecoverQueue;
};

// ============================================================================
// Helper functions
//

// static
static bool removeEmbeddedMarkers(const std::string& strFilename)
{
	std::ifstream inNotecardFile(strFilename.c_str(), std::ios::in | std::ios::binary);
	if (!inNotecardFile.is_open())
		return false;

	std::string strText((std::istreambuf_iterator<char>(inNotecardFile)), std::istreambuf_iterator<char>());
	inNotecardFile.close();

	std::string::size_type idxText = strText.find((char)'\xF4', 0), lenText = strText.length();
	while ( (std::string::npos != idxText) && (idxText + 4 <= lenText) )
	{
		// In UTF-8 we're looking for F4808080-F48FBFBF
		char chByte2 = strText[idxText + 1];
		char chByte3 = strText[idxText + 2];
		char chByte4 = strText[idxText + 3];
		if ( ((chByte2 >= '\x80') && (chByte2 <= '\x8F')) &&
			 ((chByte3 >= '\x80') && (chByte3 <= '\xBF')) &&
			 ((chByte4 >= '\x80') && (chByte4 <= '\xBF')) )
		{
			// We're being lazy and replacing embedded markers with spaces since we don't want to adjust the notecard length field
			strText.replace(idxText, 4, 4, ' ');
			continue;
		}
		idxText = strText.find('\xF4', idxText + 1);
	}

	std::ofstream outNotecardFile(strFilename.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
	if (!outNotecardFile.is_open())
		return false;

	outNotecardFile.write(strText.c_str(), strText.length());
	outNotecardFile.close();

	return true;
}

// ============================================================================
// LLAssetRecoverQueue
//

static void findRecoverFiles(LLSD& sdFiles, const std::string& strPath, const std::string& strMask, const std::string& strType)
{
	LLDirIterator itFiles(strPath, strMask); std::string strFilename;
	while (itFiles.next(strFilename))
	{
		// Build a friendly name for the file
		std::string strName = gDirUtilp->getBaseFileName(strFilename, true);
		std::string::size_type offset = strName.find_last_of("-");
		if ( (std::string::npos != offset) && (offset != 0) && (offset == strName.length() - 9))
			strName.erase(strName.length() - 9);

		LLStringUtil::trim(strName);
		if (0 == strName.length())
			strName = llformat("(Unknown %s)", strType.c_str());

		sdFiles.append(LLSD().with("path", strPath + strFilename).with("name", strName).with("type", strType));
	}
}

// static
void LLAssetRecoverQueue::recoverIfNeeded()
{
	const std::string strTempPath = LLFile::tmpdir();
	LLSD sdData, &sdFiles = sdData["files"];

	findRecoverFiles(sdFiles, strTempPath, "*.lslbackup", "script");
	findRecoverFiles(sdFiles, strTempPath, "*.ncbackup", "notecard");

	if (sdFiles.size())
	{
		LLFloaterReg::showInstance("asset_recovery", sdData);
	}
}

LLAssetRecoverQueue::LLAssetRecoverQueue(const LLSD& sdFiles)
{
	for (LLSD::array_const_iterator itFile = sdFiles.beginArray(), endFile = sdFiles.endArray(); itFile != endFile;  ++itFile)
	{
		const LLSD& sdFile = *itFile;
		if (LLFile::isfile(sdFile["path"]))
		{
			LLAssetRecoverItem recoveryItem;
			recoveryItem.strPath = sdFile["path"].asString();
			recoveryItem.strName = sdFile["name"].asString();

			// Figure out the asset type
			if ("script" == sdFile["type"].asString())
				recoveryItem.eAssetType = LLAssetType::AT_LSL_TEXT;
			else if ("notecard" == sdFile["type"].asString())
				recoveryItem.eAssetType = LLAssetType::AT_NOTECARD;

			// Generate description
			LLViewerAssetType::generateDescriptionFor(recoveryItem.eAssetType, recoveryItem.strDescription);

			// Per asset type handling
			switch (recoveryItem.eAssetType)
			{
				case LLAssetType::AT_LSL_TEXT:
					recoveryItem.eInvType = LLInventoryType::IT_LSL;
					recoveryItem.nNextOwnerPerm = LLFloaterPerms::getNextOwnerPerms("Scripts");
					break;
				case LLAssetType::AT_NOTECARD:
					recoveryItem.eInvType = LLInventoryType::IT_NOTECARD;
					recoveryItem.nNextOwnerPerm = LLFloaterPerms::getNextOwnerPerms("Notecards");
					removeEmbeddedMarkers(recoveryItem.strPath);
					break;
				default:
					break;
			}

			if (recoveryItem.eAssetType == LLAssetType::AT_LSL_TEXT || recoveryItem.eAssetType == LLAssetType::AT_NOTECARD)
				m_RecoveryQueue.push_back(recoveryItem);
		}
	}

	if (!m_RecoveryQueue.empty())
		recoverNext();
	else
		delete this;
}

bool LLAssetRecoverQueue::recoverNext()
{
	/**
	 * Steps:
	 *  (1) create a script/notecard inventory item under "Lost and Found"
	 *  (2) once we have the item's UUID we can upload it
	 *  (3) once the asset is uploaded we move on to the next item
	 */
	const LLUUID idFNF = gInventory.findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);

	// If the associated UUID is non-null then this file is already being processed
	auto itItem = m_RecoveryQueue.cbegin();
	while ( (m_RecoveryQueue.cend() != itItem) && (itItem->idItem.notNull()) )
		++itItem;

	// Empty queue - pop-up inventory floater
	if (m_RecoveryQueue.cend() == itItem)
	{
		LLInventoryPanel* pInvPanel = LLInventoryPanel::getActiveInventoryPanel(TRUE);
		if (pInvPanel)
		{
			LLFolderViewFolder* pFVF = dynamic_cast<LLFolderViewFolder*>(pInvPanel->getItemByID(idFNF));
			if (pFVF)
			{
				pFVF->setOpenArrangeRecursively(TRUE, LLFolderViewFolder::RECURSE_UP);
				pInvPanel->setSelection(idFNF, TRUE);
			}
		}

		delete this;
		return false;
	}

	// Otherwise start the recovery cycle
	create_inventory_item(gAgent.getID(), gAgent.getSessionID(), idFNF, LLTransactionID::tnull,
	                      itItem->strName, itItem->strDescription, itItem->eAssetType, itItem->eInvType,
	                      NO_INV_SUBTYPE, itItem->nNextOwnerPerm, new LLCreateRecoverAssetCallback(this));
	return true;
}

void LLAssetRecoverQueue::onCreateItem(const LLUUID& idItem)
{
	const LLViewerInventoryItem* pItem = gInventory.getItem(idItem);
	if (!pItem)
	{
		// CATZ-TODO: error handling (can't call onUploadError or we'll create an endless loop)
		return;
	}

	// Viewer will localize 'New Script' so we have to undo that
	std::string strItemName = pItem->getName();
	LLViewerInventoryItem::lookupSystemName(strItemName);

	auto itItem = m_RecoveryQueue.begin();
	while (m_RecoveryQueue.end() != itItem)
	{
		if (itItem->strName == strItemName)
			break;
		++itItem;
	}

	if (m_RecoveryQueue.end() != itItem)
	{
		itItem->idItem = idItem;

		std::string strCapsUrl, strBuffer;

		std::ifstream inNotecardFile(itItem->strPath.c_str(), std::ios::in | std::ios::binary);
		if (inNotecardFile.is_open())
		{
			strBuffer.assign((std::istreambuf_iterator<char>(inNotecardFile)), std::istreambuf_iterator<char>());
			inNotecardFile.close();
		}

		LLResourceUploadInfo::ptr_t uploadInfo;
		switch (pItem->getType())
		{
			case LLAssetType::AT_LSL_TEXT:
				strCapsUrl = gAgent.getRegion()->getCapability("UpdateScriptAgent");
				uploadInfo = LLResourceUploadInfo::ptr_t(new LLScriptAssetUpload(idItem, strBuffer, boost::bind(&LLAssetRecoverQueue::onSavedAsset, this, _1, _4)));
				break;
			case LLAssetType::AT_NOTECARD:
				strCapsUrl = gAgent.getRegion()->getCapability("UpdateNotecardAgentInventory");
				uploadInfo = LLResourceUploadInfo::ptr_t(new LLBufferedAssetUploadInfo(itItem->idItem, LLAssetType::AT_NOTECARD, strBuffer, boost::bind(&LLAssetRecoverQueue::onSavedAsset, this, _1, _4)));
				break;
		}

		if ( (!strCapsUrl.empty()) && (uploadInfo) )
		{
			uploadInfo->setUploadErrorCb(boost::bind(&LLAssetRecoverQueue::onUploadError, this, _1));
			LLViewerAssetUpload::EnqueueInventoryUpload(strCapsUrl, uploadInfo);
			return;
		}
	}

	// CATZ-TODO: error handling (if we can't find the current item)
}

void LLAssetRecoverQueue::onSavedAsset(const LLUUID& idItem, const LLSD& sdResponse)
{
	const LLViewerInventoryItem* pItem = gInventory.getItem(idItem);
	if (pItem)
	{
		auto itItem = std::find_if(m_RecoveryQueue.begin(), m_RecoveryQueue.end(), [&idItem](const LLAssetRecoverItem& item)->bool { return item.idItem == idItem; });
		if (m_RecoveryQueue.end() != itItem)
		{
			LLFile::remove(itItem->strPath);
			m_RecoveryQueue.erase(itItem);
		}
	}
	recoverNext();
}

void LLAssetRecoverQueue::onUploadError(const LLUUID& idItem)
{
	// Skip over the file when there's an error, we can try again on the next relog
	auto itItem = std::find_if(m_RecoveryQueue.begin(), m_RecoveryQueue.end(), [&idItem](const LLAssetRecoverItem& item)->bool { return item.idItem == idItem; });
	if (m_RecoveryQueue.end() != itItem)
	{
		LLViewerInventoryItem* pItem = gInventory.getItem(itItem->idItem);
		if (pItem)
			gInventory.changeItemParent(pItem, gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH), FALSE);
		m_RecoveryQueue.erase(itItem);
	}
	recoverNext();
}

// ============================================================================
