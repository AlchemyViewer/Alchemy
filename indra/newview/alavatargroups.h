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

class LLColor4;
class LLUUID;
class LLVOAvatar;

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
    // the same answer for an avatar that is in the scene, from what it
    // already caches about being a friend, muted or staff
    LLColor4 getAvatarColor(const LLVOAvatar* avatar, LLColor4 default_color, EColorType color_type);
    std::string getAvatarColorName(const LLUUID& id, std::string_view color_name, EColorType color_type);

private:
    enum class EAvatarKind
    {
        SELF,
        HIDDEN,     // a name RLV will not let us show
        FRIEND,
        MUTED,
        STAFF,      // Linden, Mole or ProductEngine
        OTHER
    };
    static EAvatarKind classify(const LLUUID& id);
    static EAvatarKind classify(const LLVOAvatar* avatar);

    // index into the colour table for a kind and use, or -1 to leave the
    // caller's colour alone
    static S32 colorIndex(EAvatarKind kind, EColorType color_type);
    LLColor4 colorForKind(EAvatarKind kind, LLColor4 default_color, EColorType color_type);
};
