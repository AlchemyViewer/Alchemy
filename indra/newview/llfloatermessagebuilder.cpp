/**
 * @file lleasymessagesender.cpp
 *
 * $LicenseInfo:firstyear=2015&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llfloatermessagebuilder.h"
#include "llmessagetemplate.h"
#include "llagent.h"
#include "llviewerregion.h" // getHandle
#include "llcombobox.h"
#include "llselectmgr.h" // fill in stuff about selected object
#include "llparcel.h"
#include "llviewerparcelmgr.h" // same for parcel
#include "llscrolllistctrl.h"
#include "llworld.h"
#include "lltemplatemessagebuilder.h"
#include "llfloaterreg.h"
#include "llnotificationsutil.h"
#include "lleasymessagesender.h"
#include "lltextbase.h"

////////////////////////////////
// LLNetListItem
////////////////////////////////
LLNetListItem::LLNetListItem(LLUUID id)
:	mID(id),
	mAutoName(TRUE),
	mName("No name"),
	mPreviousRegionName(""),
	mHandle(0),
	mCircuitData(nullptr)
{
}

////////////////////////////////
// LLFloaterMessageBuilder
////////////////////////////////
std::list<LLNetListItem*> LLFloaterMessageBuilder::sNetListItems;

LLFloaterMessageBuilder::LLFloaterMessageBuilder(const LLSD& key)
:	LLFloater(key),
	LLEventTimer(1.0f)
//	mNetInfoMode(NI_NET)
{
}

void LLFloaterMessageBuilder::show(const std::string& initial_text)
{
	LLFloaterReg::showInstance("message_builder", initial_text.length() > 0
							   ? LLSD().with("initial_text", initial_text) : LLSD());
}

void LLFloaterMessageBuilder::onOpen(const LLSD& key)
{
	if(key.has("initial_text"))
	{
		mInitialText = key["initial_text"].asString();
		getChild<LLTextBase>("message_edit")->setText(mInitialText);
	}
}
BOOL LLFloaterMessageBuilder::tick()
{
	refreshNetList();
	return FALSE;
}
LLNetListItem* LLFloaterMessageBuilder::findNetListItem(LLHost host)
{
	for (LLNetListItem* itemp : sNetListItems)
		if(itemp->mCircuitData && itemp->mCircuitData->getHost() == host)
			return itemp;
	return nullptr;
}
LLNetListItem* LLFloaterMessageBuilder::findNetListItem(LLUUID id)
{
	for (LLNetListItem* itemp : sNetListItems)
		if(itemp->mID == id)
			return itemp;
	return nullptr;
}
void LLFloaterMessageBuilder::refreshNetList()
{
	LLScrollListCtrl* scrollp = getChild<LLScrollListCtrl>("net_list");
	// Update circuit data of net list items
	std::vector<LLCircuitData*> circuits = gMessageSystem->getCircuit()->getCircuitDataList();
	for (LLCircuitData* cdp : circuits)
	{
		LLNetListItem* itemp = findNetListItem(cdp->getHost());
		if(!itemp)
		{
			LLUUID id; id.generate();
			itemp = new LLNetListItem(id);
			sNetListItems.push_back(itemp);
		}
		itemp->mCircuitData = cdp;
	}
	// Clear circuit data of items whose circuits are gone
	for (LLNetListItem* itemp : sNetListItems)
	{
		if (std::find(circuits.begin(), circuits.end(), itemp->mCircuitData) == circuits.end())
			itemp->mCircuitData = nullptr;
	}
	// Remove net list items that are totally useless now
	for(std::list<LLNetListItem*>::iterator iter = sNetListItems.begin(); iter != sNetListItems.end();)
	{
		if((*iter)->mCircuitData == nullptr)
			iter = sNetListItems.erase(iter);
		else
			++iter;
	}
	// Update names of net list items
	for (LLNetListItem* itemp : sNetListItems)
	{
		if (itemp->mAutoName)
		{
			if (itemp->mCircuitData)
			{
				LLViewerRegion* regionp = LLWorld::getInstance()->getRegion(itemp->mCircuitData->getHost());
				if (regionp)
				{
					std::string name = regionp->getName();
					if(name == ::LLStringUtil::null)
						name = llformat("%s (awaiting region name)",
										itemp->mCircuitData->getHost().getString().c_str());
					itemp->mName = name;
					itemp->mPreviousRegionName = name;
				}
				else
				{
					itemp->mName = itemp->mCircuitData->getHost().getString();
					if(itemp->mPreviousRegionName != LLStringUtil::null)
						itemp->mName.append(llformat(" (was %s)",
													 itemp->mPreviousRegionName.c_str()));
				}
			}
			else
			{
				// an item just for an event queue, not handled yet
				itemp->mName = "Something else";
			}
		}
	}
	// Rebuild scroll list from scratch
	LLUUID selected_id = scrollp->getFirstSelected() ? scrollp->getFirstSelected()->getUUID() : LLUUID::null;
	S32 scroll_pos = scrollp->getScrollPos();
	scrollp->clearRows();
	for (LLNetListItem* itemp : sNetListItems)
	{
		std::string text_suffix;
		bool has_live_circuit =  false;
		if (itemp->mCircuitData)
		{
			has_live_circuit = itemp->mCircuitData->isAlive();
			if (itemp->mCircuitData->getHost() == gAgent.getRegionHost())
			{
				text_suffix = " (main)";
			}
		}

		LLSD element;
		element["id"] = itemp->mID;
		LLSD& text_column = element["columns"][0];
		text_column["column"] = "text";
		text_column["value"] = itemp->mName + text_suffix;

		LLSD& state_column = element["columns"][ 1];
		state_column["column"] = "state";
		state_column["value"] = "";

		LLScrollListItem* scroll_itemp = scrollp->addElement(element);

		LLScrollListText* state = (LLScrollListText*)scroll_itemp->getColumn(1);

		if(has_live_circuit)
			state->setText(LLStringExplicit("Alive"));
		else
			state->setText(LLStringExplicit("Dead"));
	}
	if(selected_id.notNull())
		scrollp->selectByID(selected_id);
	if(scroll_pos < scrollp->getItemCount())
		scrollp->setScrollPos(scroll_pos);
}

BOOL LLFloaterMessageBuilder::postBuild()
{
	string_vec_t untrusted_names;
	string_vec_t trusted_names;
	for (const auto& msg : gMessageSystem->mMessageTemplates)
	{
		switch (msg.second->getTrust())
		{
			case MT_NOTRUST:
				untrusted_names.push_back(msg.second->mName);
				break;
			case MT_TRUST:
				trusted_names.push_back(msg.second->mName);
		}
	}
	std::sort(untrusted_names.begin(), untrusted_names.end());
	std::sort(trusted_names.begin(), trusted_names.end());
	LLComboBox* combo = getChild<LLComboBox>("untrusted_message_combo");
	combo->setCommitCallback(boost::bind(&LLFloaterMessageBuilder::onCommitPacketCombo, this, _1));
	for (const std::string& name : untrusted_names)
		combo->add(name);
	combo = getChild<LLComboBox>("trusted_message_combo");
	combo->setCommitCallback(boost::bind(&LLFloaterMessageBuilder::onCommitPacketCombo, this, _1));
	for (const std::string& name : trusted_names)
		combo->add(name);
	
	getChild<LLTextBase>("message_edit")->setText(mInitialText);
	getChild<LLUICtrl>("send_btn")->setCommitCallback(boost::bind(&LLFloaterMessageBuilder::onClickSend, this));
	
	return TRUE;
}

void LLFloaterMessageBuilder::onCommitPacketCombo(LLUICtrl* ctrl)
{
	LLViewerObject* selected_objectp = LLSelectMgr::getInstance()->getSelection()->getPrimaryObject();
	LLParcel* agent_parcelp = LLViewerParcelMgr::getInstance()->getAgentParcel();
	const std::string& message = ctrl->getValue();
	LLMessageSystem::message_template_name_map_t::iterator template_iter;
	template_iter = gMessageSystem->mMessageTemplates.find(LLMessageStringTable::getInstance()->getString(message.c_str()));
	if (template_iter == gMessageSystem->mMessageTemplates.end())
	{
		getChild<LLTextBase>("message_edit")->setText(LLStringUtil::null);
		return;
	}
	std::string text(llformat((*template_iter).second->getTrust() == MT_NOTRUST
							  ? "out %s\n\n" : "in %s\n\n", message.c_str()));
	LLMessageTemplate* temp = (*template_iter).second;
	for (const auto& block : temp->mMemberBlocks)
	{
		std::string block_name = std::string(block->mName);
		S32 num_blocks = 1;
		if(block->mType == MBT_MULTIPLE)
			num_blocks = block->mNumber;
		else if("ObjectLink" == message && "ObjectData" == block_name)
			num_blocks = 2;
		
		for(S32 i = 0; i < num_blocks; i++)
		{
			text.append(llformat("[%s]\n", block_name.c_str()));
			for (const auto& variable : block->mMemberVariables)
			{
				std::string var_name = std::string(variable->getName());
				text.append(llformat("    %s = ", var_name.c_str()));
				std::string value(LLStringUtil::null);
				S32 size = variable->getSize();
				switch(variable->getType())
				{
				case MVT_U8:
				case MVT_U16:
				case MVT_U32:
				case MVT_U64:
				case MVT_S8:
				case MVT_S16:
				case MVT_S32:
				case MVT_IP_ADDR:
				case MVT_IP_PORT:
					if("RegionHandle" == var_name || "Handle" == var_name)
						value = "$RegionHandle";
					else if("CircuitCode" == var_name || "ViewerCircuitCode" == var_name
						|| ("Code" == var_name && "CircuitCode" == block_name) )
					{
						value = "$CircuitCode";
					}
					else if(selected_objectp &&
							("ObjectLocalID" == var_name || "TaskLocalID" == var_name || ("LocalID" == var_name && ("ObjectData" == block_name || "UpdateData" == block_name || "InventoryData" == block_name))))
					{

						value = fmt::to_string(selected_objectp->getLocalID());
					}
					else if(agent_parcelp && "LocalID" == var_name
							&& ("ParcelData" == block_name || message.find("Parcel") != message.npos))
					{
						value = fmt::to_string(agent_parcelp->getLocalID());
					}
					else if("PCode" == var_name)
						value = "9";
					else if("PathCurve" == var_name)
						value = "16";
					else if("ProfileCurve" == var_name)
						value = "1";
					else if("PathScaleX" == var_name || "PathScaleY" == var_name)
						value = "100";
					else if("BypassRaycast" == var_name)
						value = "1";
					else
						value = "0";
					break;
				case MVT_F32:
				case MVT_F64:
					value = "0.0";
					break;
				case MVT_LLVector3:
				case MVT_LLVector3d:
				case MVT_LLQuaternion:
					if("Position" == var_name || "RayStart" == var_name || "RayEnd" == var_name)
						value = "$Position";
					else if("Scale" == var_name)
						value = "<0.5, 0.5, 0.5>";
					else
						value = "<0, 0, 0>";
					break;
				case MVT_LLVector4:
					value = "<0, 0, 0, 0>";
					break;
				case MVT_LLUUID:
					if("AgentID" == var_name)
						value = "$AgentID";
					else if("SessionID" == var_name)
						value = "$SessionID";
					else if("ObjectID" == var_name && selected_objectp)
						value = selected_objectp->getID().asString();
					else if("ParcelID" == var_name && agent_parcelp)
						value = agent_parcelp->getID().asString();
					else
						value = LLUUID::null.asString();
					break;
				case MVT_BOOL:
					value = "false";
					break;
				case MVT_VARIABLE:
					value = "Hello, world!";
					break;
				case MVT_FIXED:
					for(S32 si = 0; si < size; si++)
						value.append("a");
					break;
				default:
					value = LLStringUtil::null;
					break;
				}
				text.append(llformat("%s\n", value.c_str()));
			}
		}
	}
	text = text.substr(0, text.length() - 1);
	getChild<LLTextBase>("message_edit")->setText(text);
}

void LLFloaterMessageBuilder::onClickSend()
{
	LLScrollListCtrl* scrollp = getChild<LLScrollListCtrl>("net_list");
	LLScrollListItem* selected_itemp = scrollp->getFirstSelected();

	LLHost end_point;

	//if a specific circuit is selected, send it to that, otherwise send it to the current sim
	if(selected_itemp)
	{
		LLNetListItem* itemp = findNetListItem(selected_itemp->getUUID());
		LLScrollListText* textColumn = (LLScrollListText*)selected_itemp->getColumn(1);

		//why would you send data through a dead circuit?
		if(textColumn->getValue().asString() == "Dead")
		{
			LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", "No sending messages through dead circuits!"));
			return;
		}

		end_point = itemp->mCircuitData->getHost();
	}
	else
		end_point = gAgent.getRegionHost();

	mMessageSender.sendMessage(end_point, getChild<LLTextBase>("message_edit")->getText());
}

BOOL LLFloaterMessageBuilder::handleKeyHere(KEY key, MASK mask)
{
	if(key == KEY_RETURN && (mask & MASK_CONTROL))
	{
		onClickSend();
		return TRUE;
	}
	if(key == KEY_ESCAPE)
	{
		releaseFocus();
		return TRUE;
	}
	return FALSE;
}
