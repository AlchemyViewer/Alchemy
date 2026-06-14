/**
 * @file alavatargroups.h
 * @brief ALAvatarGroups header for central color control of avatar names, chat, and map blips
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
 */

#pragma once

#include "llsingleton.h"

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

class LLChat;
class LLColor4;
class LLUIColor;
class LLUUID;

class ALAvatarGroups final : public LLSingleton < ALAvatarGroups >
{
    LLSINGLETON_EMPTY_CTOR(ALAvatarGroups);
    ~ALAvatarGroups() = default;

public:
    typedef enum e_custom_colors
    {
        E_FIRST_COLOR = 0,
        E_SECOND_COLOR,
        E_THIRD_COLOR,
        E_FOURTH_COLOR
    } EAvatarColors;

    void addOrUpdateCustomColor(const LLUUID& id, EAvatarColors color);
    void clearCustomColor(const LLUUID& id);

    typedef enum e_color_type
    {
        COLOR_CHAT,
        COLOR_NAMETAG,
        COLOR_NEARBY,
        COLOR_MINIMAP
    } EColorType;

    LLColor4 getAvatarColor(const LLUUID& id, LLColor4 default_color, EColorType color_type);
    std::string getAvatarColorName(const LLUUID& id, std::string_view color_name, EColorType color_type);

    // IRC-style chat coloring: when enabled, gives every other speaker a stable
    // deterministic color and dims the displayed name relative to its text color.
    // Other message categories (self, system, objects, owner-say) keep their
    // user-configured chat colors.
    bool getIRCChatColor(const LLChat& chat, LLUIColor& color);
    bool getIRCNameColor(const LLChat& chat, LLUIColor& color);
    bool getIRCNameTagColor(const LLUUID& id, LLColor4& color);

private:
    LLColor4 deterministicAgentColor(const LLUUID& id);
    LLColor4 nameColor(const LLUUID& id);
};
