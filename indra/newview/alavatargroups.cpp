/**
* @file alavatargroups.cpp
 * @brief ALAvatarGroups implementation for central color control of avatar names, chat, and map blips
*
* $LicenseInfo:firstyear=2013&license=viewerlgpl$
* Copyright (C) Rye Mutt <rye@alchemyviewer.org>
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
#include "llchat.h"
#include "lluicolor.h"
#include "lluicolortable.h"
#include "lluuid.h"
#include "v4color.h"

// viewer includes
#include "llagent.h"
#include "llcallingcard.h"
#include "llinstantmessage.h" // SYSTEM_FROM
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

bool ALAvatarGroups::getIRCChatColor(const LLChat& chat, LLUIColor& color)
{
    static LLCachedControl<bool> enabled(gSavedSettings, "AlchemyChatIRCColorsEnabled", false);
    if (!enabled)
    {
        return false;
    }

    // Only override "other" speakers; self, system, objects and owner-say keep
    // their user-configured chat colors from the existing switch.
    if (chat.mSourceType == CHAT_SOURCE_AGENT
        && chat.mFromID.notNull()
        && chat.mFromID != gAgentID
        && SYSTEM_FROM != chat.mFromName
        // A stable per-avatar color would defeat @shownames, so skip it while
        // restricted from seeing this speaker's name.
        && RlvActions::canShowName(RlvActions::SNC_DEFAULT, chat.mFromID))
    {
        color = deterministicAgentColor(chat.mFromID);
        return true;
    }

    return false;
}

bool ALAvatarGroups::getIRCNameColor(const LLChat& chat, const LLUIColor& chat_color, LLUIColor& color)
{
    static LLCachedControl<bool> enabled(gSavedSettings, "AlchemyChatIRCColorsEnabled", false);
    if (!enabled)
    {
        return false;
    }

    // Keep the default name styling while restricted from seeing this speaker's
    // name, matching getIRCChatColor.
    if (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, chat.mFromID))
    {
        return false;
    }

    color = dimNameColor(chat_color.get());
    return true;
}

bool ALAvatarGroups::getIRCNameTagColor(const LLUUID& id, LLColor4& color)
{
    static LLCachedControl<bool> enabled(gSavedSettings, "AlchemyChatIRCColorsEnabled", false);
    static LLCachedControl<bool> name_tags(gSavedSettings, "AlchemyChatIRCColorNameTags", false);
    if (!enabled || !name_tags)
    {
        return false;
    }

    // Mirror the chat-color override: only other agents get a per-avatar color,
    // self keeps the user-configured name tag colors. Skip while restricted from
    // seeing this avatar's name so the color can't defeat @shownames. The name
    // dimming applied to chat names is applied here too so tags match.
    if (id.notNull() && id != gAgentID
        && RlvActions::canShowName(RlvActions::SNC_DEFAULT, id))
    {
        color = dimNameColor(deterministicAgentColor(id));
        return true;
    }

    return false;
}

LLColor4 ALAvatarGroups::deterministicAgentColor(const LLUUID& id)
{
    static LLCachedControl<F32> saturation(gSavedSettings, "AlchemyChatIRCAgentSaturation", 0.7f);
    static LLCachedControl<F32> lightness(gSavedSettings, "AlchemyChatIRCAgentLightness", 0.9f);

    LLColor4 color;
    color.setHSL(static_cast<F32>(id.getCRC32() % 360) / 360.f, saturation(), lightness());
    color.mV[VALPHA] = 1.f;

    return color;
}

LLColor4 ALAvatarGroups::dimNameColor(const LLColor4& color)
{
    static LLCachedControl<F32> scale(gSavedSettings, "AlchemyChatIRCNameLightnessScale", 0.8f);

    F32 hue = 0.f;
    F32 saturation = 0.f;
    F32 lightness = 0.f;
    color.calcHSL(&hue, &saturation, &lightness);

    LLColor4 result;
    result.setHSL(hue, saturation, lightness * scale());
    result.mV[VALPHA] = 1.f;

    return result;
}
