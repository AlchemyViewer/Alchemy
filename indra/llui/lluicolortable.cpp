/**
 * @file lluicolortable.cpp
 * @brief brief LLUIColorTable class implementation file
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

#include "linden_common.h"

#include "lluicolortable.h"

#include "lldir.h"
#include "llui.h"

void LLUIColorTable::clear()
{
    clearTable(mLoadedColors);
    clearTable(mUserSetColors);
    mLoadedSheet.clear();
    mUserSheet.clear();
}

LLUIColor LLUIColorTable::getColor(std::string_view name, const LLColor4& default_color) const
{
    string_color_map_t::const_iterator iter = mUserSetColors.find(name);

    if(iter != mUserSetColors.end())
    {
        return LLUIColor(&iter->second);
    }

    iter = mLoadedColors.find(name);

    if(iter != mLoadedColors.end())
    {
        return LLUIColor(&iter->second);
    }

    return  LLUIColor(default_color);
}

// update user color, loaded colors are parsed on initialization
void LLUIColorTable::setColor(std::string_view name, const LLColor4& color)
{
    auto it = mUserSetColors.find(name);
    if(it != mUserSetColors.end())
    {
        it->second = color;
    }
    else
    {
        string_color_map_t::iterator base_iter = mLoadedColors.find(name);
        if (base_iter != mLoadedColors.end())
        {
            // The node itself moves to the user table, so every handle
            // taken on the loaded colour now reads the user's; a fresh node
            // keeps the loaded value for isDefault and resetToDefault.
            LLColor4 original_color = base_iter->second.get();
            auto color_handle = mLoadedColors.extract(base_iter);
            auto new_color_pair = mUserSetColors.insert(std::move(color_handle));
            new_color_pair.position->second = color;
            mLoadedColors.emplace(name, LLUIColor(original_color));
        }
        else
        {
            mUserSetColors.insert(it, std::make_pair(name, color));
        }
    }
}

bool LLUIColorTable::isDefault(std::string_view name) const
{
    string_color_map_t::const_iterator base_iter = mLoadedColors.find(name);
    string_color_map_t::const_iterator user_iter = mUserSetColors.find(name);
    if (base_iter != mLoadedColors.end())
    {
        if(user_iter != mUserSetColors.end())
            return user_iter->second == base_iter->second;

        return true;
    }
    else if (user_iter != mUserSetColors.end()) // user only color ???
    {
        return true;
    }

    return false;
}

void LLUIColorTable::resetToDefault(std::string_view name)
{
    string_color_map_t::iterator iter = mUserSetColors.find(name);

    if (iter != mUserSetColors.end())
    {
        auto default_iter = mLoadedColors.find(name);

        if (default_iter != mLoadedColors.end())
        {
            iter->second = default_iter->second.get();
        }
    }
}

bool LLUIColorTable::loadFromSettings()
{
    // ALL_SKINS: the default skin's colors.xml, the current skin's, and the
    // user's copies of each, in that order, so each overrides the last.
    std::vector<document_t> skin_documents;
    for (const std::string& colors_path :
                  gDirUtilp->findSkinnedFilenames(LLDir::SKINBASE, "colors.xml", LLDir::ALL_SKINS))
    {
        LLXMLNodePtr root;
        if (!LLXMLNode::parseFile(colors_path, root, nullptr))
        {
            LL_WARNS("UIColorTable") << "Unable to parse color file " << colors_path << LL_ENDL;
            continue;
        }
        skin_documents.emplace_back(std::move(root), colors_path);
    }

    const std::string user_filename = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "colors.xml");
    LLXMLNodePtr user_root;
    if (gDirUtilp->fileExists(user_filename) && !LLXMLNode::parseFile(user_filename, user_root, nullptr))
    {
        LL_WARNS("UIColorTable") << "Unable to parse color file " << user_filename << LL_ENDL;
    }

    return load(skin_documents, user_root, user_filename);
}

bool LLUIColorTable::load(std::span<const document_t> skin_documents, const LLXMLNodePtr& user_document, std::string_view user_filename)
{
    bool result = false;

    mLoadedSheet.clear();
    for (const document_t& document : skin_documents)
    {
        if (mLoadedSheet.read(document.first, document.second))
        {
            result = true;
        }
        else
        {
            LL_WARNS("UIColorTable") << document.second << " is not a valid color definition file" << LL_ENDL;
        }
    }
    mLoadedSheet.resolve();
    install(mLoadedSheet, mLoadedColors, true);

    mUserSheet.clear();
    if (user_document.notNull() && !mUserSheet.read(user_document, user_filename))
    {
        LL_WARNS("UIColorTable") << user_filename << " is not a valid color definition file" << LL_ENDL;
    }
    mUserSheet.resolve(&mLoadedSheet);
    install(mUserSheet, mUserSetColors, false);

    for (const ALColorSheet* sheet : { &mLoadedSheet, &mUserSheet })
    {
        for (const ALColorSheet::Diagnostic& diagnostic : sheet->diagnostics())
        {
            LL_WARNS("UIColorTable") << diagnostic.message << LL_ENDL;
        }
    }

    return result;
}

// Every resolved colour into its node, or a new one. With drop_missing, a
// name the sheet did not resolve is painted magenta rather than left at
// what an earlier load gave it: a handle to it stays valid and shows the
// hole.
void LLUIColorTable::install(const ALColorSheet& sheet, string_color_map_t& table, bool drop_missing)
{
    if (drop_missing)
    {
        for (auto& [name, color] : table)
        {
            if (!sheet.find(name))
            {
                color = LLColor4::magenta;
            }
        }
    }
    sheet.forEachResolved([&](std::string_view name, const LLColor4& color)
    {
        setColor(name, color, table);
    });
}

LLXMLNodePtr LLUIColorTable::userSettingsDocument(bool scrub) const
{
    LLXMLNodePtr output_node = new LLXMLNode("colors", false);

    for (const auto& color_pair : mUserSetColors)
    {
        // Compare user color value with the default value, skip if equal
        string_color_map_t::const_iterator itd = mLoadedColors.find(color_pair.first);
        if(itd != mLoadedColors.end() && itd->second == color_pair.second)
            continue;

        if (!scrub || color_pair.first.find("ColorPaletteEntry") != std::string::npos)
        {
            LLXMLNodePtr color_node = output_node->createChild("color", false);
            color_node->createChild("name", true)->setStringValue(color_pair.first);
            color_node->createChild("value", true)->setFloatValue(4, color_pair.second.get().mV);
        }
    }

    return output_node;
}

void LLUIColorTable::saveUserSettings(const bool scrub /* = false */) const
{
    LLXMLNodePtr output_node = userSettingsDocument(scrub);

    const std::string& filename = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "colors.xml");
    LLFILE *fp = LLFile::fopen(filename, LLFILE_MODE("w"));

    if(fp != NULL)
    {
        LLXMLNode::writeHeaderToFile(fp);
        output_node->writeToFile(fp);

        fclose(fp);
    }
}

bool LLUIColorTable::colorExists(std::string_view color_name) const
{
    return ((mLoadedColors.find(color_name) != mLoadedColors.end())
         || (mUserSetColors.find(color_name) != mUserSetColors.end()));
}

void LLUIColorTable::clearTable(string_color_map_t& table)
{
    for(string_color_map_t::iterator it = table.begin();
        it != table.end();
        ++it)
    {
        it->second = LLColor4::magenta;
    }
}

// this method inserts a color into the table if it does not exist
// if the color already exists it changes the color
void LLUIColorTable::setColor(std::string_view name, const LLColor4& color, string_color_map_t& table)
{
    string_color_map_t::iterator it = table.find(name);
    if(it != table.end())
    {
        it->second = color;
    }
    else
    {
        table.insert(it, string_color_map_t::value_type(name, color));
    }
}

// EOF
