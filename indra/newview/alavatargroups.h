/**
 * @file alchatcommand.h
 * @brief ALChatCommand header for chat input commands
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
 */

#pragma once

#include "llsingleton.h"

#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

class LLColor4;
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
};
