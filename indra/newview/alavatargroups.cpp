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
#include "llvoavatar.h"
#include "rlvactions.h"

namespace
{
    // One entry per kind that has colours of its own, in the order
    // colorIndex counts them: chat, name tag, map.
    const char* const COLOR_NAMES[] = {
        "UserChatColor",   "NameTagSelf",   "MapAvatarSelfColor",
        "FriendChatColor", "NameTagFriend", "MapAvatarFriendColor",
        "MutedChatColor",  "NameTagMuted",  "MapAvatarMutedColor",
        "LindenChatColor", "NameTagLinden", "MapAvatarLindenColor",
    };
    constexpr S32 COLOR_COUNT = sizeof(COLOR_NAMES) / sizeof(COLOR_NAMES[0]);

    // what each name falls back to when a skin does not define it
    const LLColor4* colorFallback(S32 index)
    {
        if (index >= 9) return &LLColor4::cyan;    // staff
        if (index >= 6) return &LLColor4::grey3;   // muted
        return &LLColor4::white;
    }

    bool ircColorsEnabled()
    {
        static LLCachedControl<bool> enabled(gSavedSettings, "AlchemyChatIRCColorsEnabled", false);
        return enabled;
    }

    // IRC-style chat's colour for an id: a hue of its own, at the shared
    // saturation and the lightness given
    LLColor4 hashedColor(const LLUUID& id, F32 lightness)
    {
        static LLCachedControl<F32> saturation(gSavedSettings, "AlchemyChatIRCAgentSaturation", 0.7f);

        LLColor4 color;
        color.setHSL(static_cast<F32>(id.getCRC32() % 360) / 360.f, saturation, lightness);
        color.mV[VALPHA] = 1.f;
        return color;
    }
}

ALAvatarGroups::EAvatarKind ALAvatarGroups::classify(const LLUUID& id)
{
    if (id == gAgentID)
    {
        return EAvatarKind::SELF;
    }
    if (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, id))
    {
        return EAvatarKind::HIDDEN;
    }
    if (LLAvatarTracker::instance().getBuddyInfo(id))
    {
        return EAvatarKind::FRIEND;
    }
    if (LLMuteList::getInstance()->isMuted(id))
    {
        return EAvatarKind::MUTED;
    }
    if (LLMuteList::getInstance()->isLinden(id))
    {
        return EAvatarKind::STAFF;
    }
    return EAvatarKind::OTHER;
}

ALAvatarGroups::EAvatarKind ALAvatarGroups::classify(const LLVOAvatar* avatar)
{
    // The same questions as above, answered from the avatar's own caches
    // rather than the friend list, the mute list and the name cache. The
    // name tag asks this every frame for every avatar in view.
    if (avatar->isSelf())
    {
        return EAvatarKind::SELF;
    }
    if (!RlvActions::canShowName(RlvActions::SNC_DEFAULT, avatar->getID()))
    {
        return EAvatarKind::HIDDEN;
    }
    if (avatar->isBuddy())
    {
        return EAvatarKind::FRIEND;
    }
    if (avatar->isInMuteList())
    {
        return EAvatarKind::MUTED;
    }
    if (avatar->isStaffUser())
    {
        return EAvatarKind::STAFF;
    }
    return EAvatarKind::OTHER;
}

S32 ALAvatarGroups::colorIndex(EAvatarKind kind, EColorType color_type)
{
    S32 base;
    switch (kind)
    {
    case EAvatarKind::SELF:   base = 0; break;
    case EAvatarKind::FRIEND: base = 3; break;
    case EAvatarKind::MUTED:  base = 6; break;
    case EAvatarKind::STAFF:  base = 9; break;
    default:
        return -1;
    }

    static LLCachedControl<bool> nearby_list_colorize(gSavedSettings, "AlchemyNearbyColorize", true);
    static LLCachedControl<bool> color_friends(gSavedSettings, "NameTagShowFriends");

    switch (color_type)
    {
    case COLOR_CHAT:
        return base + 0;
    case COLOR_NAMETAG:
        // friends are only coloured on the tag when asked for
        return (kind == EAvatarKind::FRIEND && !color_friends) ? -1 : base + 1;
    case COLOR_MINIMAP:
        return base + 2;
    case COLOR_NEARBY:
        // the nearby list takes the map colour, and never for yourself
        return (kind != EAvatarKind::SELF && nearby_list_colorize) ? base + 2 : -1;
    default:
        return -1;
    }
}

LLColor4 ALAvatarGroups::colorForKind(EAvatarKind kind, LLColor4 default_color, EColorType color_type)
{
    // Filled once. An LLUIColor is a handle into the colour table, so the
    // colours themselves still follow a skin change; what this saves is the
    // twelve name lookups behind them.
    static std::vector<LLUIColor> ui_color_cache;
    if (ui_color_cache.empty())
    {
        auto& ui_color_inst = LLUIColorTable::instance();
        ui_color_cache.reserve(COLOR_COUNT);
        for (S32 i = 0; i < COLOR_COUNT; ++i)
        {
            ui_color_cache.push_back(ui_color_inst.getColor(COLOR_NAMES[i], *colorFallback(i)));
        }
    }

    const S32 index = colorIndex(kind, color_type);
    return index < 0 ? default_color : LLColor4(ui_color_cache[index]);
}

bool ALAvatarGroups::ircColored(EAvatarKind kind)
{
    // You, staff and the muted keep the colours that say so, and a colour of
    // one's own would give away a name RLV hides.
    return kind == EAvatarKind::OTHER || kind == EAvatarKind::FRIEND;
}

bool ALAvatarGroups::ircColor(EAvatarKind kind, const LLUUID& id, EColorType color_type, LLColor4& color)
{
    if (!ircColorsEnabled() || !ircColored(kind))
    {
        return false;
    }

    static LLCachedControl<bool> name_tags(gSavedSettings, "AlchemyChatIRCColorNameTags", false);
    switch (color_type)
    {
    case COLOR_CHAT:
        color = getIRCChatColor(id);
        return true;
    case COLOR_NAMETAG:
        if (!name_tags)
        {
            return false;
        }
        color = getIRCNameColor(id);
        return true;
    default:
        return false;
    }
}

LLColor4 ALAvatarGroups::getAvatarColor(const LLUUID& id, LLColor4 default_color, EColorType color_type)
{
    const EAvatarKind kind = classify(id);
    LLColor4 color;
    return ircColor(kind, id, color_type, color) ? color : colorForKind(kind, default_color, color_type);
}

LLColor4 ALAvatarGroups::getAvatarColor(const LLVOAvatar* avatar, LLColor4 default_color, EColorType color_type)
{
    const EAvatarKind kind = classify(avatar);
    LLColor4 color;
    return ircColor(kind, avatar->getID(), color_type, color) ? color : colorForKind(kind, default_color, color_type);
}

bool ALAvatarGroups::getChatNameColor(const LLChat& chat, LLColor4& color)
{
    // the messages LLViewerChat::getChatColor gives the speaker's own colour
    if (!ircColorsEnabled()
        || chat.mMuted
        || chat.mSourceType != CHAT_SOURCE_AGENT
        || chat.mFromID.isNull()
        || chat.mFromName == SYSTEM_FROM
        || !ircColored(classify(chat.mFromID)))
    {
        return false;
    }

    color = getIRCNameColor(chat.mFromID);
    return true;
}

LLColor4 ALAvatarGroups::getIRCChatColor(const LLUUID& id)
{
    static LLCachedControl<F32> lightness(gSavedSettings, "AlchemyChatIRCAgentLightness", 0.9f);
    return hashedColor(id, lightness);
}

LLColor4 ALAvatarGroups::getIRCNameColor(const LLUUID& id)
{
    // The same hue and saturation at a lightness of its own, worked from the
    // id rather than from the text colour: a very light or dark colour loses
    // its saturation, and at the extremes its hue, on the way through RGB.
    static LLCachedControl<F32> lightness(gSavedSettings, "AlchemyChatIRCNameLightness", 0.7f);
    return hashedColor(id, lightness);
}
