/**
 * @file lluicolortable.h
 * @brief brief LLUIColorTable class header file
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLUICOLORTABLE_H_
#define LL_LLUICOLORTABLE_H_

#include <span>
#include <utility>

#include <boost/unordered_map.hpp>

#include "alcolorsheet.h"
#include "llsingleton.h"
#include "lluicolor.h"
#include "llxmlnode.h"

#include "v4color.h"

class LLUIColorTable : public LLSingleton<LLUIColorTable>
{
    LLSINGLETON_EMPTY_CTOR(LLUIColorTable);
    LOG_CLASS(LLUIColorTable);

    // Node based on purpose: getColor() hands out pointers into these, so an
    // entry, once made, keeps its address and is never erased.
    typedef boost::unordered_map<std::string, LLUIColor, ll::string_hash, std::equal_to<>>  string_color_map_t;

public:
    // A parsed colors.xml and the name diagnostics know it by.
    typedef std::pair<LLXMLNodePtr, std::string> document_t;

    // reset all colors to default magenta color
    void clear();

    // color lookup
    LLUIColor getColor(std::string_view name, const LLColor4& default_color = LLColor4::magenta) const;

    // if the color is in the table, it's value is changed, otherwise it is added
    void setColor(std::string_view name, const LLColor4& color);

    // returns true if color_name exists in the table
    bool colorExists(std::string_view color_name) const;

    bool isDefault(std::string_view color_name) const;

    void resetToDefault(std::string_view color_name);

    // The skins' colors.xml in override order -- default, current, and the
    // user's copies of each -- merged by name, then the user's own on top.
    bool loadFromSettings();

    // The same from documents already parsed: what loadFromSettings does
    // once it has found the files. A loaded name the documents no longer
    // declare goes magenta; a user colour set since the last load stays.
    bool load(std::span<const document_t> skin_documents, const LLXMLNodePtr& user_document, std::string_view user_filename);

    // saves colors specified by the user to the users skin directory
    void saveUserSettings(const bool scrub = false) const;

    // What saveUserSettings writes: every user colour that differs from the
    // loaded one, or with scrub only the palette entries.
    LLXMLNodePtr userSettingsDocument(bool scrub = false) const;

    const auto& getLoadedColors() const { return mLoadedColors; }
    const auto& getUserColors() const { return mUserSetColors; }

    // Where each loaded colour was declared, and what the last load had to
    // say about the files.
    const ALColorSheet& getLoadedSheet() const { return mLoadedSheet; }
    const ALColorSheet& getUserSheet() const { return mUserSheet; }

private:
    void install(const ALColorSheet& sheet, string_color_map_t& table, bool drop_missing);
    void clearTable(string_color_map_t& table);
    void setColor(std::string_view name, const LLColor4& color, string_color_map_t& table);

    string_color_map_t mLoadedColors;
    string_color_map_t mUserSetColors;
    ALColorSheet       mLoadedSheet;
    ALColorSheet       mUserSheet;
};

#endif // LL_LLUICOLORTABLE_H
