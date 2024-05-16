/**
* @file alavatarcolormgr.cpp
* @brief ALChatCommand implementation for chat input commands
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Copyright (C) 2013 Drake Arconis
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
**/

#include "llviewerprecompiledheaders.h"

#include "alavatargroups.h"

// system includes

// lib includes
#include "llavatarname.h"
#include "llavatarnamecache.h"
#include "lluicolor.h"
#include "lluicolortable.h"
#include "lluuid.h"
#include "v4color.h"

// viewer includes
#include "llagent.h"
#include "llcallingcard.h"
#include "llmutelist.h"
#include "llviewercontrol.h"
#include "rlvactions.h"

LLColor4 ALAvatarGroups::getAvatarColor(const LLUUID& id, LLColor4 color, EColorType color_type)
{
    enum {
        USER_CHAT_COLOR, USER_NAME_TAG_COLOR, USER_MAP_COLOR,
        FRIEND_CHAT_COLOR, FRIEND_NAME_TAG_COLOR, FRIEND_MAP_COLOR,
        MUTED_CHAT_COLOR, MUTED_NAME_TAG_COLOR, MUTED_MAP_COLOR,
        LINDEN_CHAT_COLOR, LINDEN_NAME_TAG_COLOR, LINDEN_MAP_COLOR
    };
    std::vector<LLUIColor> ui_color_cache;
    if (ui_color_cache.empty())
    {
        auto& ui_color_inst = LLUIColorTable::instance();
        ui_color_cache.push_back(ui_color_inst.getColor("UserChatColor", LLColor4::white));
        ui_color_cache.push_back(ui_color_inst.getColor("NameTagSelf", LLColor4::white));
        ui_color_cache.push_back(ui_color_inst.getColor("MapAvatarSelfColor", LLColor4::white));

        ui_color_cache.push_back(ui_color_inst.getColor("FriendChatColor", LLColor4::white));
        ui_color_cache.push_back(ui_color_inst.getColor("NameTagFriend", LLColor4::white));
        ui_color_cache.push_back(ui_color_inst.getColor("MapAvatarFriendColor", LLColor4::white));

        ui_color_cache.push_back(ui_color_inst.getColor("MutedChatColor", LLColor4::grey3));
        ui_color_cache.push_back(ui_color_inst.getColor("NameTagMuted", LLColor4::grey3));
        ui_color_cache.push_back(ui_color_inst.getColor("MapAvatarMutedColor", LLColor4::grey3));

        ui_color_cache.push_back(ui_color_inst.getColor("LindenChatColor", LLColor4::cyan));
        ui_color_cache.push_back(ui_color_inst.getColor("NameTagLinden", LLColor4::cyan));
        ui_color_cache.push_back(ui_color_inst.getColor("MapAvatarLindenColor", LLColor4::cyan));
    }

    static LLCachedControl<bool> nearby_list_colorize(gSavedSettings, "AlchemyNearbyColorize", true);
    static LLCachedControl<bool> color_friends(gSavedSettings, "NameTagShowFriends");

    bool rlv_shownames = !RlvActions::canShowName(RlvActions::SNC_DEFAULT, id);

    if (id == gAgentID)
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            color = ui_color_cache[USER_CHAT_COLOR];
            break;
        case COLOR_NAMETAG:
            color = ui_color_cache[USER_NAME_TAG_COLOR];
            break;
        case COLOR_MINIMAP:
            color = ui_color_cache[USER_MAP_COLOR];
            break;
        case COLOR_NEARBY:
        default:
            break;
        }
    }
    else if (rlv_shownames)
    {
        // Don't bother with the rest if we're rlv_shownames restricted.
    }
    else if (LLAvatarTracker::instance().getBuddyInfo(id))
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            color = ui_color_cache[FRIEND_CHAT_COLOR];
            break;
        case COLOR_NAMETAG:
        {
            if (color_friends)
            {
                color = ui_color_cache[FRIEND_NAME_TAG_COLOR];
            }
            break;
        }
        case COLOR_MINIMAP:
            color = ui_color_cache[FRIEND_MAP_COLOR];
            break;
        case COLOR_NEARBY:
        {
            if (nearby_list_colorize)
            {
                color = ui_color_cache[FRIEND_MAP_COLOR];
            }
            break;
        }
        default:
            break;
        }
    }
    else if (LLMuteList::getInstance()->isMuted(id))
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            color = ui_color_cache[MUTED_MAP_COLOR];
            break;
        case COLOR_NAMETAG:
            color = ui_color_cache[MUTED_NAME_TAG_COLOR];
            break;
        case COLOR_MINIMAP:
            color = ui_color_cache[MUTED_MAP_COLOR];
            break;
        case COLOR_NEARBY:
        {
            if (nearby_list_colorize)
            {
                color = ui_color_cache[MUTED_MAP_COLOR];
            }
            break;
        }
        default:
            break;
        }
    }
    else if (LLMuteList::getInstance()->isLinden(id))
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            color = ui_color_cache[LINDEN_CHAT_COLOR];
            break;
        case COLOR_NAMETAG:
            color = ui_color_cache[LINDEN_NAME_TAG_COLOR];
            break;
        case COLOR_MINIMAP:
            color = ui_color_cache[LINDEN_MAP_COLOR];
            break;
        case COLOR_NEARBY:
        {
            if (nearby_list_colorize)
            {
                color = ui_color_cache[LINDEN_MAP_COLOR];
            }
            break;
        }
        default:
            break;
        }
    }

    return color;
}

std::string ALAvatarGroups::getAvatarColorName(const LLUUID& id, std::string_view color_name, EColorType color_type)
{
    static LLCachedControl<bool> nearby_list_colorize(gSavedSettings, "AlchemyNearbyColorize", true);
    static LLCachedControl<bool> color_friends(gSavedSettings, "NameTagShowFriends");
    bool rlv_shownames = !RlvActions::canShowName(RlvActions::SNC_DEFAULT, id);

    std::string out_color_name;
    if (id == gAgentID)
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            out_color_name = "UserChatColor";
            break;
        case COLOR_NAMETAG:
            out_color_name = "NameTagSelf";
            break;
        case COLOR_MINIMAP:
            out_color_name = "MapAvatarSelfColor";
            break;
        case COLOR_NEARBY:
        default:
            break;
        }
    }
    else if (rlv_shownames)
    {
        // Don't bother with the rest if we're rlv_shownames restricted.
    }
    else if (LLAvatarTracker::instance().getBuddyInfo(id))
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            out_color_name = "FriendChatColor";
            break;
        case COLOR_NAMETAG:
        {
            if (color_friends)
            {
                out_color_name = "NameTagFriend";
            }
            break;
        }
        case COLOR_MINIMAP:
            out_color_name = "MapAvatarFriendColor";
            break;
        case COLOR_NEARBY:
        {
            if (nearby_list_colorize)
            {
                out_color_name = "MapAvatarFriendColor";
            }
            break;
        }
        default:
            break;
        }
    }
    else if (LLMuteList::getInstance()->isMuted(id))
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            out_color_name = "MutedChatColor";
            break;
        case COLOR_NAMETAG:
            out_color_name = "NameTagMuted";
            break;
        case COLOR_MINIMAP:
            out_color_name = "MapAvatarMutedColor";
            break;
        case COLOR_NEARBY:
        {
            if (nearby_list_colorize)
            {
                out_color_name = "MapAvatarMutedColor";
            }
            break;
        }
        default:
            break;
        }
    }
    else if (LLMuteList::getInstance()->isLinden(id))
    {
        switch (color_type)
        {
        case COLOR_CHAT:
            out_color_name = "LindenChatColor";
            break;
        case COLOR_NAMETAG:
            out_color_name = "NameTagLinden";
            break;
        case COLOR_MINIMAP:
            out_color_name = "MapAvatarLindenColor";
            break;
        case COLOR_NEARBY:
        {
            if (nearby_list_colorize)
            {
                out_color_name = "MapAvatarLindenColor";
            }
            break;
        }
        default:
            break;
        }
    }
    else
    {
        out_color_name = color_name;
    }

    return out_color_name;
}
