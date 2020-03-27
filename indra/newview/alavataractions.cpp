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

#include "llavatarnamecache.h"
#include "llclipboard.h"

#include "alavataractions.h"
#include "llslurl.h"
#include "llviewercontrol.h"

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