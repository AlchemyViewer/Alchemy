/** 
 * @file alavataractions.h
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

#ifndef AL_ALAVATARACTIONS_H
#define AL_ALAVATARACTIONS_H

#include "llsd.h"
#include "lluuid.h"

#include <string>
#include <vector>

class LLAvatarName;
class LLInventoryPanel;
class LLFloater;
class LLView;

/**
 * Friend-related actions (add, remove, offer teleport, etc)
 */
class ALAvatarActions
{
public:
	/**
     * Copy the selected avatar's name, slurl, or UUID to clipboard
	 */
	enum ECopyDataType : U32
	{
		E_DATA_USER_NAME = 0,
		E_DATA_ACCOUNT_NAME,
		E_DATA_DISPLAY_NAME,
		E_DATA_SLURL,
		E_DATA_UUID
	};

	static void copyData(const LLUUID& id, ECopyDataType type);
	static void copyData(const uuid_vec_t& ids, ECopyDataType type);
	static void copyData(const LLUUID& id, const LLSD& userdata);
	static void copyData(const uuid_vec_t& id, const LLSD& userdata);

	static bool canTeleportTo(const LLUUID& avatar_id);
	static void teleportTo(const LLUUID& avatar_id);

	static bool canFreezeEject(const LLUUID& avatar_id);
	static bool canFreezeEject(const uuid_vec_t& ids);
	static void parcelFreeze(const LLUUID& avatar_id);
	static void parcelFreeze(const uuid_vec_t& ids);
	static void parcelEject(const LLUUID& avatar_id);
	static void parcelEject(const uuid_vec_t& ids);

	static bool canManageAvatarsEstate(const LLUUID& avatar_id);
	static bool canManageAvatarsEstate(const uuid_vec_t& ids);
	static void estateTeleportHome(const LLUUID& avatar_id);
	static void estateTeleportHome(const uuid_vec_t& ids);
	static void estateKick(const LLUUID& avatar_id);
	static void estateKick(const uuid_vec_t& ids);
	static void estateBan(const LLUUID& avatar_id);
	static void estateBan(const uuid_vec_t& ids);

	/**
	 * Kick avatar off grid
	 */	
	static void godKick(const LLUUID& id);

	/**
	 * God Freeze avatar
	 */	
	static void godFreeze(const LLUUID& id);

	/**
	 * God Unfreeze avatar
	 */	
	static void godUnfreeze(const LLUUID& id);

private:
	static bool handleParcelFreeze(const LLSD& notification, const LLSD& response);
	static bool handleParcelEject(const LLSD& notification, const LLSD& response);
	static bool handleEstateTeleportHome(const LLSD& notification, const LLSD& response);
	static bool handleEstateKick(const LLSD& notification, const LLSD& response);
	static bool handleEstateBan(const LLSD& notification, const LLSD& response);
	static bool handleGodKick(const LLSD& notification, const LLSD& response);
};

#endif // AL_ALAVATARACTIONS_H
