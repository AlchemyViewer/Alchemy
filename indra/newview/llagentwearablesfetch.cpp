/** 
 * @file llagentwearablesfetch.cpp
 * @brief LLAgentWearblesFetch class implementation
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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
#include "llagentwearablesfetch.h"

#include "llagent.h"
#include "llagentwearables.h"
#include "llappearancemgr.h"
#include "llinventoryfunctions.h"
#include "llstartup.h"
#include "llvoavatarself.h"


void order_my_outfits_cb()
{
		if (!LLApp::isRunning())
		{
			LL_WARNS() << "called during shutdown, skipping" << LL_ENDL;
			return;
		}
		
		const LLUUID& my_outfits_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_MY_OUTFITS);
		if (my_outfits_id.isNull()) return;

		LLInventoryModel::cat_array_t* cats;
		LLInventoryModel::item_array_t* items;
		gInventory.getDirectDescendentsOf(my_outfits_id, cats, items);
		if (!cats) return;

		//My Outfits should at least contain saved initial outfit and one another outfit
		if (cats->size() < 2)
		{
			LL_WARNS() << "My Outfits category was not populated properly" << LL_ENDL;
			return;
		}

		LL_INFOS() << "Starting updating My Outfits with wearables ordering information" << LL_ENDL;

		for (LLInventoryModel::cat_array_t::iterator outfit_iter = cats->begin();
			outfit_iter != cats->end(); ++outfit_iter)
		{
			const LLUUID& cat_id = (*outfit_iter)->getUUID();
			if (cat_id.isNull()) continue;

			// saved initial outfit already contains wearables ordering information
			if (cat_id == LLAppearanceMgr::getInstance()->getBaseOutfitUUID()) continue;

		LLAppearanceMgr::getInstance()->updateClothingOrderingInfo(cat_id);
	}

	LL_INFOS() << "Finished updating My Outfits with wearables ordering information" << LL_ENDL;
}

LLInitialWearablesFetch::LLInitialWearablesFetch(const LLUUID& cof_id) :
	LLInventoryFetchDescendentsObserver(cof_id)
{
	if (isAgentAvatarValid())
	{
		gAgentAvatarp->startPhase("initial_wearables_fetch");
		gAgentAvatarp->outputRezTiming("Initial wearables fetch started");
	}
}

LLInitialWearablesFetch::~LLInitialWearablesFetch()
{
}

// virtual
void LLInitialWearablesFetch::done()
{
	// Delay processing the actual results of this so it's not handled within
	// gInventory.notifyObservers.  The results will be handled in the next
	// idle tick instead.
	gInventory.removeObserver(this);
	doOnIdleOneTime(boost::bind(&LLInitialWearablesFetch::processContents,this));
	if (isAgentAvatarValid())
	{
		gAgentAvatarp->stopPhase("initial_wearables_fetch");
		gAgentAvatarp->outputRezTiming("Initial wearables fetch done");
	}
}

void LLInitialWearablesFetch::add(InitialWearableData &data)

{
	mAgentInitialWearables.push_back(data);
}

void LLInitialWearablesFetch::processContents()
{
	if(!gAgentAvatarp) //no need to process wearables if the agent avatar is deleted.
	{
		delete this;
		return ;
	}

	// Fetch the wearable items from the Current Outfit Folder
	LLInventoryModel::cat_array_t cat_array;
	LLInventoryModel::item_array_t wearable_array;
	LLFindWearables is_wearable;
	llassert_always(!mComplete.empty());
	gInventory.collectDescendentsIf(mComplete.front(), cat_array, wearable_array, 
									LLInventoryModel::EXCLUDE_TRASH, is_wearable);

	LLAppearanceMgr::instance().setAttachmentInvLinkEnable(true);
	if (!wearable_array.empty())
	{
		gAgentWearables.notifyLoadingStarted();
		LLAppearanceMgr::instance().updateAppearanceFromCOF();
	}
	else
	{
		// if we're constructing the COF from the wearables message, we don't have a proper outfit link
		LLAppearanceMgr::instance().setOutfitDirty(true);
		processWearablesMessage();
	}
	delete this;
}

class LLFetchAndLinkObserver: public LLInventoryFetchItemsObserver
{
public:
	LLFetchAndLinkObserver(uuid_vec_t& ids):
		LLInventoryFetchItemsObserver(ids)
	{
	}
	~LLFetchAndLinkObserver()
	{
	}

	void done() override
	{
		gInventory.removeObserver(this);

		// Link to all fetched items in COF.
		LLPointer<LLInventoryCallback> link_waiter = new LLUpdateAppearanceOnDestroy;
		LLInventoryObject::const_object_list_t item_array;
		for (auto& id : mIDs)
        {
			LLConstPointer<LLInventoryObject> item = gInventory.getItem(id);
			if (!item)
			{
				LL_WARNS() << "fetch failed for item " << id << "!" << LL_ENDL;
				continue;
			}

			item_array.push_back(item);
		}
		link_inventory_array(LLAppearanceMgr::instance().getCOF(), item_array, link_waiter);
	}
};

void LLInitialWearablesFetch::processWearablesMessage()
{
	if (!mAgentInitialWearables.empty()) // We have an empty current outfit folder, use the message data instead.
	{
        (void) LLAppearanceMgr::instance().getCOF();
		uuid_vec_t ids;
		for (const auto& wearable_data : mAgentInitialWearables)
        {
			// Populate the current outfit folder with links to the wearables passed in the message
            if (wearable_data.mAssetID.notNull())
			{
				ids.push_back(wearable_data.mItemID);
			}
			else
			{
				LL_INFOS() << "Invalid wearable, type " << wearable_data.mType << " itemID "
				<< wearable_data.mItemID << " assetID " << wearable_data.mAssetID << LL_ENDL;
			}
		}

		// Add all current attachments to the requested items as well.
		if (isAgentAvatarValid())
		{
			for (LLVOAvatar::attachment_map_t::const_iterator iter = gAgentAvatarp->mAttachmentPoints.begin(); 
				 iter != gAgentAvatarp->mAttachmentPoints.end(); ++iter)
			{
				LLViewerJointAttachment* attachment = iter->second;
				if (!attachment) continue;
				for (LLViewerObject* attached_object : attachment->mAttachedObjects)
                {
                    if (!attached_object) continue;
					const LLUUID& item_id = attached_object->getAttachmentItemID();
					if (item_id.isNull()) continue;
					ids.push_back(item_id);
				}
			}
		}

		// Need to fetch the inventory items for ids, then create links to them after they arrive.
		LLFetchAndLinkObserver *fetcher = new LLFetchAndLinkObserver(ids);
		fetcher->startFetch();
		// If no items to be fetched, done will never be triggered.
		// TODO: Change LLInventoryFetchItemsObserver::fetchItems to trigger done() on this condition.
		if (fetcher->isFinished())
		{
			fetcher->done();
		}
		else
		{
			gInventory.addObserver(fetcher);
		}
	}
	else
	{
		LL_WARNS("Wearables") << "No current outfit folder items found and no initial wearables fallback message received." << LL_ENDL;
	}
}
