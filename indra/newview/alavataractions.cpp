/** 
 * @file alavataractions.cpp
 * @brief Friend-related actions (add, remove, offer teleport, etc)
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Rye Mutt <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */


#include "llviewerprecompiledheaders.h"

#include "alavataractions.h"

#include "llavatarnamecache.h"
#include "llclipboard.h"
#include "llnotifications.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "roles_constants.h"

#include "llagent.h"
#include "llfloaterregioninfo.h"
#include "llslurl.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llworld.h"

 // Flags for kick message
const U32 KICK_FLAGS_DEFAULT = 0x0;
const U32 KICK_FLAGS_FREEZE = 1 << 0;
const U32 KICK_FLAGS_UNFREEZE = 1 << 1;

// static
void ALAvatarActions::copyData(const LLUUID& id, ECopyDataType type)
{
	if (id.isNull())
		return;

	uuid_vec_t ids;
	ids.push_back(id);
	copyData(ids, type);
}

// static
void ALAvatarActions::copyData(const uuid_vec_t& ids, ECopyDataType type)
{
	if (!ids.empty())
	{
		std::string data_string;
		static LLCachedControl<std::string> seperator(gSavedSettings, "AlchemyCopySeperator", ", ");
		for (const LLUUID& id : ids)
		{
			if (id.isNull())
				continue;

			if (!data_string.empty())
				data_string.append(seperator);

			switch (type)
			{
			case E_DATA_USER_NAME:
			{
				LLAvatarName av_name;
				LLAvatarNameCache::get(id, &av_name);
				data_string.append(av_name.getUserName());
				break;
			}
			case E_DATA_ACCOUNT_NAME:
			{
				LLAvatarName av_name;
				LLAvatarNameCache::get(id, &av_name);
				data_string.append(av_name.getAccountName());
				break;
			}
			case E_DATA_DISPLAY_NAME:
			{
				LLAvatarName av_name;
				LLAvatarNameCache::get(id, &av_name);
				data_string.append(av_name.getDisplayName(true));
				break;
			}
			case E_DATA_SLURL:
				data_string.append(LLSLURL("agent", id, "about").getSLURLString());
				break;
			case E_DATA_UUID:
				data_string.append(id.asString());
				break;
			default:
				break;
			}
		}

		if (!data_string.empty())
		{
			LLWString wdata_str = utf8str_to_wstring(data_string);
			LLClipboard::instance().copyToClipboard(wdata_str, 0, wdata_str.length());
		}
	}
}

// static
void ALAvatarActions::copyData(const LLUUID& id, const LLSD& userdata)
{
	const std::string item_name = userdata.asString();
	if (item_name == "user_name")
	{
		copyData(id, E_DATA_USER_NAME);
	}
	else if (item_name == "account_name")
	{
		copyData(id, E_DATA_ACCOUNT_NAME);
	}
	else if (item_name == "display_name")
	{
		copyData(id, E_DATA_DISPLAY_NAME);
	}
	else if (item_name == "id")
	{
		copyData(id, E_DATA_UUID);
	}
	else if (item_name == "slurl")
	{
		copyData(id, E_DATA_SLURL);
	}
}

// static
void ALAvatarActions::copyData(const uuid_vec_t& ids, const LLSD& userdata)
{
	const std::string item_name = userdata.asString();
	if (item_name == "user_name")
	{
		copyData(ids, E_DATA_USER_NAME);
	}
	else if (item_name == "account_name")
	{
		copyData(ids, E_DATA_ACCOUNT_NAME);
	}
	else if (item_name == "display_name")
	{
		copyData(ids, E_DATA_DISPLAY_NAME);
	}
	else if (item_name == "id")
	{
		copyData(ids, E_DATA_UUID);
	}
	else if (item_name == "slurl")
	{
		copyData(ids, E_DATA_SLURL);
	}
}

// static
bool ALAvatarActions::canTeleportTo(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return false;

	LLWorld::pos_map_t positions;
	LLWorld::getInstance()->getAvatars(&positions);
	auto iter = positions.find(avatar_id);
	if (iter != positions.cend())
	{
		const auto& id = iter->first;
		const auto& pos = iter->second;
		if (id != gAgent.getID() && !pos.isNull())
		{
			return true;
		}
	}
	return false;
}

// static
void ALAvatarActions::teleportTo(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return;

	LLWorld::pos_map_t positions;
	LLWorld::getInstance()->getAvatars(&positions);
	auto iter = positions.find(avatar_id);
	if (iter != positions.cend())
	{
		const auto& id = iter->first;
		const auto& pos = iter->second;
		if (id != gAgentID && !pos.isNull())
		{
			gAgent.teleportViaLocation(pos);
		}
	}
}

// static 
bool ALAvatarActions::canFreezeEject(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return false;

	uuid_vec_t ids = { avatar_id };
	return canFreezeEject(ids);
}

// static 
bool ALAvatarActions::canFreezeEject(const uuid_vec_t& ids)
{
	if (ids.empty())
		return false;

	if (std::find(ids.cbegin(), ids.cend(), gAgentID) != ids.cend())
		return false;

	// Gods can always freeze and eject
	if (gAgent.isGodlike())
		return true;

	LLWorld::region_gpos_map_t idRegions;
	LLWorld::getInstance()->getAvatars(&idRegions);

	auto ret = false;

	for (const auto& id : ids)
	{
		if (id.isNull())
			continue;
		auto it = idRegions.find(id);
		if (it != idRegions.cend())
		{
			const LLViewerRegion* region = it->second.first;
			const LLVector3d& pos_global = it->second.second;
			if (region)
			{
				// Estate owners / managers can freeze
				// Parcel owners can also freeze
				LLParcelSelectionHandle selection = LLViewerParcelMgr::getInstance()->selectParcelAt(pos_global);
				const LLParcel* parcel = selection->getParcel();
				auto local_pos = region->getPosRegionFromGlobal(pos_global);

				if ((region->getOwner() == gAgent.getID() || region->isEstateManager() || region->isOwnedSelf(local_pos))
					|| (region->isOwnedGroup(local_pos) && parcel && LLViewerParcelMgr::getInstance()->isParcelOwnedByAgent(parcel, GP_LAND_ADMIN)))
				{
					ret = true;
				}
				// We hit a false so bail out early in case of multi-select
				if (!ret)
				{
					break;
				}
				continue;
			}
		}
		ret = false;
		break;
	}
	return ret;
}

// static
void ALAvatarActions::parcelFreeze(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return;

	uuid_vec_t ids = { avatar_id };
	return parcelFreeze(ids);
}

// static
void ALAvatarActions::parcelFreeze(const uuid_vec_t& ids)
{
	if (ids.empty())
		return;

	LLSD payload;
	payload["avatar_ids"] = LLSDArray();
	std::string avatars;
	for (auto id : ids)
	{
		if (id.notNull())
		{
			payload["avatar_ids"].append(id);

			if (!avatars.empty())
				avatars += "\n";

			avatars += LLSLURL("agent", id, "about").getSLURLString();
		}
	}

	LLSD args;
	args["AVATAR_NAMES"] = avatars;

	LLNotificationsUtil::add((payload["avatar_ids"].size() == 1) ? "FreezeAvatarFullname" : "FreezeAvatarMultiple", args, payload, handleParcelFreeze);
}

// static
void ALAvatarActions::parcelEject(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return;

	uuid_vec_t ids = { avatar_id };
	return parcelEject(ids);
}

// static
void ALAvatarActions::parcelEject(const uuid_vec_t& ids)
{
	if (ids.empty())
		return;

	LLWorld::pos_map_t avatar_positions;
	LLWorld::getInstance()->getAvatars(&avatar_positions);

	LLSD payload;
	payload["avatar_ids"] = LLSD::emptyArray();
	std::string avatars;
	bool ban_enabled = false;
	bool ban_killed = false;
	for (auto id : ids)
	{
		if (id.notNull())
		{
			payload["avatar_ids"].append(id);

			if (!ban_killed)
			{
				const auto& pos_it = avatar_positions.find(id);
				if (pos_it != avatar_positions.cend())
				{
					const auto& pos = pos_it->second;
					LLParcel* parcel = LLViewerParcelMgr::getInstance()->selectParcelAt(pos)->getParcel();
					if (parcel)
					{
						ban_enabled = LLViewerParcelMgr::getInstance()->isParcelOwnedByAgent(parcel, GP_LAND_MANAGE_BANNED);
						if (!ban_enabled)
						{
							ban_killed = true;
						}
					}
				}
			}

			if (!avatars.empty())
				avatars += "\n";

			avatars += LLSLURL("agent", id, "about").getSLURLString();
		}
	}
	payload["ban_enabled"] = ban_enabled;

	LLSD args;
	args["AVATAR_NAMES"] = avatars;
	std::string notification;
	bool is_single = (payload["avatar_ids"].size() == 1);
	if (ban_enabled)
	{
		notification = is_single ? "EjectAvatarFullname" : "EjectAvatarMultiple";
	}
	else
	{
		notification = is_single ? "EjectAvatarFullnameNoBan" : "EjectAvatarMultipleNoBan";
	}
	LLNotificationsUtil::add(notification, args, payload, handleParcelEject);
}

// static
bool ALAvatarActions::canManageAvatarsEstate(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return false;

	uuid_vec_t ids = { avatar_id };
	return canManageAvatarsEstate(ids);
}

// static
bool ALAvatarActions::canManageAvatarsEstate(const uuid_vec_t& ids)
{
	if (ids.empty())
		return false;

	if (std::find(ids.cbegin(), ids.cend(), gAgentID) != ids.cend())
		return false;

	// Gods can always estate manage
	if (gAgent.isGodlike())
		return true;

	LLWorld::region_gpos_map_t idRegions;
	LLWorld::getInstance()->getAvatars(&idRegions);

	auto ret = false;

	for (const auto& id : ids)
	{
		if (id.isNull())
			continue;
		auto it = idRegions.find(id);
		if (it != idRegions.cend())
		{
			auto region = it->second.first;
			if (region)
			{
				const auto& owner = region->getOwner();
				// Can't manage the estate owner!
				if (owner == id)
				{
					ret = false;
					break;
				}
				ret = (owner == gAgentID || region->canManageEstate());
				continue;
			}
		}
		ret = false;
		break;
	}
	return ret;
}

// static
void ALAvatarActions::estateTeleportHome(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return;

	uuid_vec_t ids = { avatar_id };
	estateTeleportHome(ids);
}

// static
void ALAvatarActions::estateTeleportHome(const uuid_vec_t& ids)
{
	if (ids.empty())
		return;

	LLSD payload;
	payload["avatar_ids"] = LLSD::emptyArray();
	std::string avatars;
	for (auto id : ids)
	{
		if (id.notNull())
		{
			payload["avatar_ids"].append(id);

			if (!avatars.empty())
				avatars += "\n";

			avatars += LLSLURL("agent", id, "about").getSLURLString();
		}
	}

	LLSD args;
	args["AVATAR_NAMES"] = avatars;

	LLNotificationsUtil::add((payload["avatar_ids"].size() == 1) ? "EstateTeleportHomeSingle" : "EstateTeleportHomeMulti", args, payload, handleEstateTeleportHome);
}

// static
void ALAvatarActions::estateKick(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return;

	uuid_vec_t ids = { avatar_id };
	estateKick(ids);
}

// static
void ALAvatarActions::estateKick(const uuid_vec_t& ids)
{
	if (ids.empty())
		return;

	LLSD payload;
	payload["avatar_ids"] = LLSD::emptyArray();
	std::string avatars;
	for (auto id : ids)
	{
		if (id.notNull())
		{
			payload["avatar_ids"].append(id);

			if (!avatars.empty())
				avatars += "\n";

			avatars += LLSLURL("agent", id, "about").getSLURLString();
		}
	}

	LLSD args;
	args["AVATAR_NAMES"] = avatars;

	LLNotificationsUtil::add((payload["avatar_ids"].size() == 1) ? "EstateKickSingle" : "EstateKickMultiple", args, payload, handleEstateKick);
}

// static
void ALAvatarActions::estateBan(const LLUUID& avatar_id)
{
	if (avatar_id.isNull())
		return;

	uuid_vec_t ids = { avatar_id };
	estateBan(ids);
}

// static
void ALAvatarActions::estateBan(const uuid_vec_t& ids)
{
	if (ids.empty())
		return;

	auto region = gAgent.getRegion();
	if (!region)
		return;

	LLSD payload;
	payload["avatar_ids"] = LLSD::emptyArray();
	std::string avatars;
	for (auto id : ids)
	{
		if (id.notNull())
		{
			payload["avatar_ids"].append(id);

			if (!avatars.empty())
				avatars += "\n";

			avatars += LLSLURL("agent", id, "about").getSLURLString();
		}
	}

	LLSD args;
	args["AVATAR_NAMES"] = avatars;
	std::string owner = LLSLURL("agent", region->getOwner(), "inspect").getSLURLString();
	if (gAgent.isGodlike())
	{
		LLStringUtil::format_map_t owner_args;
		owner_args["[OWNER]"] = owner;
		args["ALL_ESTATES"] = LLTrans::getString("RegionInfoAllEstatesOwnedBy", owner_args);
	}
	else if (region->getOwner() == gAgent.getID())
	{
		args["ALL_ESTATES"] = LLTrans::getString("RegionInfoAllEstatesYouOwn");
	}
	else if (region->isEstateManager())
	{
		LLStringUtil::format_map_t owner_args;
		owner_args["[OWNER]"] = owner.c_str();
		args["ALL_ESTATES"] = LLTrans::getString("RegionInfoAllEstatesYouManage", owner_args);
	}

	bool single_user = (payload["avatar_ids"].size() == 1);
	LLNotificationsUtil::add(single_user ? "EstateBanSingle" : "EstateBanMultiple", args, payload, handleEstateKick);
}

// static
void ALAvatarActions::godFreeze(const LLUUID& id)
{
	LLSD args;
	args["AVATAR_NAME"] = LLSLURL("agent", id, "about").getSLURLString();

	LLSD payload;
	payload["type"] = static_cast<LLSD::Integer>(KICK_FLAGS_FREEZE);
	payload["avatar_id"] = id;
	LLNotifications::instance().add("FreezeUser", args, payload, handleGodKick);
}
// static
void ALAvatarActions::godUnfreeze(const LLUUID& id)
{
	LLSD args;
	args["AVATAR_NAME"] = LLSLURL("agent", id, "about").getSLURLString();

	LLSD payload;
	payload["type"] = static_cast<LLSD::Integer>(KICK_FLAGS_UNFREEZE);
	payload["avatar_id"] = id;
	LLNotifications::instance().add("UnFreezeUser", args, payload, handleGodKick);
}

// static
void ALAvatarActions::godKick(const LLUUID& id)
{
	LLSD args;
	args["AVATAR_NAME"] = LLSLURL("agent", id, "about").getSLURLString();

	LLSD payload;
	payload["type"] = static_cast<LLSD::Integer>(KICK_FLAGS_DEFAULT);
	payload["avatar_id"] = id;
	LLNotifications::instance().add("KickUser", args, payload, handleGodKick);
}

// static
bool ALAvatarActions::handleParcelFreeze(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (0 == option || 1 == option)
	{
		U32 flags = 0x0;
		if (1 == option)
		{
			// unfreeze
			flags |= 0x1;
		}
		const auto& avatar_ids = notification["payload"]["avatar_ids"];
		for (LLSD::array_const_iterator it = avatar_ids.beginArray(), it_end = avatar_ids.endArray(); it != it_end; ++it)
		{
			const auto& id = it->asUUID();
			LLMessageSystem* msg = gMessageSystem;

			msg->newMessage("FreezeUser");
			msg->nextBlock("AgentData");
			msg->addUUID("AgentID", gAgent.getID());
			msg->addUUID("SessionID", gAgent.getSessionID());
			msg->nextBlock("Data");
			msg->addUUID("TargetID", id);
			msg->addU32("Flags", flags);
			gAgent.sendReliableMessage();
		}
	}
	return false;
}

// static
bool ALAvatarActions::handleParcelEject(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	if (2 == option)
	{
		return false;
	}

	bool ban_enabled = notification["payload"]["ban_enabled"].asBoolean();
	const auto& avatar_ids = notification["payload"]["avatar_ids"];
	for (LLSD::array_const_iterator it = avatar_ids.beginArray(), it_end = avatar_ids.endArray(); it != it_end; ++it)
	{
		const auto& id = it->asUUID();
		U32 flags = (1 == option && ban_enabled) ? 0x1 : 0x0;
		LLMessageSystem* msg = gMessageSystem;
		msg->newMessage("EjectUser");
		msg->nextBlock("AgentData");
		msg->addUUID("AgentID", gAgent.getID());
		msg->addUUID("SessionID", gAgent.getSessionID());
		msg->nextBlock("Data");
		msg->addUUID("TargetID", id);
		msg->addU32("Flags", flags);
		gAgent.sendReliableMessage();
	}
	return false;
}

// static
bool ALAvatarActions::handleEstateTeleportHome(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		LLWorld::region_gpos_map_t idRegions;
		LLWorld::getInstance()->getAvatars(&idRegions);
		const auto& avatar_ids = notification["payload"]["avatar_ids"];
		for (LLSD::array_const_iterator it = avatar_ids.beginArray(), it_end = avatar_ids.endArray(); it != it_end; ++it)
		{
			const auto& id = it->asUUID();
			LLViewerRegion* regionp = nullptr;
			auto idreg_it = idRegions.find(id);
			if (idreg_it != idRegions.cend())
			{
				regionp = idreg_it->second.first;
			}

			if (!regionp)
				continue;

			LLMessageSystem* msg = gMessageSystem;
			msg->newMessageFast(_PREHASH_EstateOwnerMessage);
			msg->nextBlockFast(_PREHASH_AgentData);
			msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
			msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
			msg->addUUIDFast(_PREHASH_TransactionID, LLUUID::null); //not used
			msg->nextBlockFast(_PREHASH_MethodData);
			msg->addStringFast(_PREHASH_Method, "teleporthomeuser");
			msg->addUUIDFast(_PREHASH_Invoice, LLUUID::null);
			msg->nextBlockFast(_PREHASH_ParamList);
			msg->addStringFast(_PREHASH_Parameter, gAgent.getID().asString().c_str());
			msg->nextBlockFast(_PREHASH_ParamList);
			msg->addStringFast(_PREHASH_Parameter, id.asString().c_str());
			msg->sendReliable(regionp->getHost());
		}
	}
	return false;
}

// static
bool ALAvatarActions::handleEstateKick(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		LLWorld::region_gpos_map_t idRegions;
		LLWorld::getInstance()->getAvatars(&idRegions);
		const auto& avatar_ids = notification["payload"]["avatar_ids"];
		for (LLSD::array_const_iterator it = avatar_ids.beginArray(), it_end = avatar_ids.endArray(); it != it_end; ++it)
		{
			const auto& id = it->asUUID();
			LLViewerRegion* regionp = nullptr;
			auto idreg_it = idRegions.find(id);
			if (idreg_it != idRegions.cend())
			{
				regionp = idreg_it->second.first;
			}

			if (!regionp)
				continue;

			LLMessageSystem* msg = gMessageSystem;
			msg->newMessageFast(_PREHASH_EstateOwnerMessage);
			msg->nextBlockFast(_PREHASH_AgentData);
			msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
			msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
			msg->addUUIDFast(_PREHASH_TransactionID, LLUUID::null); //not used
			msg->nextBlockFast(_PREHASH_MethodData);
			msg->addStringFast(_PREHASH_Method, "kickestate");
			msg->addUUIDFast(_PREHASH_Invoice, LLUUID::null);
			msg->nextBlockFast(_PREHASH_ParamList);
			msg->addStringFast(_PREHASH_Parameter, id.asString().c_str());
			msg->sendReliable(regionp->getHost());
		}
	}
	return false;
}

// static
bool ALAvatarActions::handleEstateBan(const LLSD& notification, const LLSD& response)
{
	LLViewerRegion* region = gAgent.getRegion();
	if (!region)
		return false;

	S32 option = LLNotification::getSelectedOption(notification, response);

	if (0 == option || 1 == option)
	{
		const auto& avatar_ids = notification["payload"]["avatar_ids"];
		for (LLSD::array_const_iterator it = avatar_ids.beginArray(), it_end = avatar_ids.endArray(); it != it_end; ++it)
		{
			const auto& id = it->asUUID();
			if (region->getOwner() == id)
			{
				// Can't ban the owner!
				continue;
			}

			U32 flags = ESTATE_ACCESS_BANNED_AGENT_ADD | ESTATE_ACCESS_ALLOWED_AGENT_REMOVE;

			if (it + 1 != it_end)
			{
				flags |= ESTATE_ACCESS_NO_REPLY;
			}

			if (option == 1)
			{
				if (region->getOwner() == gAgent.getID() || gAgent.isGodlike())
				{
					flags |= ESTATE_ACCESS_APPLY_TO_ALL_ESTATES;
				}
				else if (region->isEstateManager())
				{
					flags |= ESTATE_ACCESS_APPLY_TO_MANAGED_ESTATES;
				}
			}

			LLFloaterRegionInfo::nextInvoice();
			LLPanelEstateAccess::sendEstateAccessDelta(flags, id);
		}
	}
	return false;
}

// static
bool ALAvatarActions::handleGodKick(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotification::getSelectedOption(notification, response);

	if (option == 0)
	{
		LLUUID avatar_id = notification["payload"]["avatar_id"].asUUID();
		U32 type = static_cast<U32>(notification["payload"]["type"].asInteger());
		LLMessageSystem* msg = gMessageSystem;

		msg->newMessageFast(_PREHASH_GodKickUser);
		msg->nextBlockFast(_PREHASH_UserInfo);
		msg->addUUIDFast(_PREHASH_GodID, gAgent.getID());
		msg->addUUIDFast(_PREHASH_GodSessionID, gAgent.getSessionID());
		msg->addUUIDFast(_PREHASH_AgentID, avatar_id);
		msg->addU32Fast(_PREHASH_KickFlags, type);
		msg->addStringFast(_PREHASH_Reason, response["message"].asString());
		gAgent.sendReliableMessage();
	}
	return false;
}