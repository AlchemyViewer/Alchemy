/**
 * @file llviewerfoldertype.h
 * @brief Declaration of LLAssetType.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLVIEWERFOLDERTYPE_H
#define LL_LLVIEWERFOLDERTYPE_H

#include <string>
#include "llfoldertype.h"
#include "lldictionary.h"
#include "llsingleton.h"

// This class is similar to llfoldertype, but contains methods
// only used by the viewer.  This also handles ensembles.
class LLViewerFolderType : public LLFolderType
{
public:
    static const std::string&   lookupXUIName(EType folder_type); // name used by the UI
    static LLFolderType::EType  lookupTypeFromXUIName(const std::string& name);

    static const std::string&   lookupIconName(EType folder_type, bool is_open = false); // folder icon name
    static bool                 lookupIsQuietType(EType folder_type); // folder doesn't require UI update when changes have occured
    static bool                 lookupIsHiddenIfEmpty(EType folder_type); // folder is not displayed if empty
    static const std::string&   lookupNewCategoryName(EType folder_type); // default name when creating new category
    static LLFolderType::EType  lookupTypeFromNewCategoryName(const std::string& name); // default name when creating new category

    static U64                  lookupValidFolderTypes(const std::string& item_name); // which folders allow an item of this type?

protected:
    LLViewerFolderType() {}
    ~LLViewerFolderType() {}
};

struct ViewerFolderEntry : public LLDictionaryEntry
{
    // Constructor for non-ensembles
    ViewerFolderEntry(const std::string &new_category_name, // default name when creating a new category of this type
                      const std::string &icon_name_open,    // name of the folder icon
                      const std::string &icon_name_closed,
                      bool is_quiet,                        // folder doesn't need a UI update when changed
                      bool hide_if_empty,                   // folder not shown if empty
                      const std::string &dictionary_name = {} // no reverse lookup needed on non-ensembles, so in most cases just leave this blank
        )
        :
        LLDictionaryEntry(dictionary_name),
        mNewCategoryName(new_category_name),
        mIconNameOpen(icon_name_open),
        mIconNameClosed(icon_name_closed),
        mIsQuiet(is_quiet),
        mHideIfEmpty(hide_if_empty)
    {
        mAllowedNames.clear();
    }

    // Constructor for ensembles
    ViewerFolderEntry(const std::string &xui_name,          // name of the xui menu item
                      const std::string &new_category_name, // default name when creating a new category of this type
                      const std::string &icon_name,         // name of the folder icon
                      const std::string allowed_names       // allowed item typenames for this folder type
        )
        :
        LLDictionaryEntry(xui_name),
        /* Just use default icons until we actually support ensembles
        mIconNameOpen(icon_name),
        mIconNameClosed(icon_name),
        */
        mIconNameOpen("Inv_FolderOpen"), mIconNameClosed("Inv_FolderClosed"),
        mNewCategoryName(new_category_name),
        mIsQuiet(false),
        mHideIfEmpty(false)
    {
        const std::string delims (",");
        LLStringUtilBase<char>::getTokens(allowed_names, mAllowedNames, delims);
    }

    bool getIsAllowedName(const std::string &name) const
    {
        if (mAllowedNames.empty())
            return false;
        for (name_vec_t::const_iterator iter = mAllowedNames.begin();
             iter != mAllowedNames.end();
             iter++)
        {
            if (name == (*iter))
                return true;
        }
        return false;
    }
    const std::string mIconNameOpen;
    const std::string mIconNameClosed;
    const std::string mNewCategoryName;
    typedef std::vector<std::string> name_vec_t;
    name_vec_t mAllowedNames;
    bool mIsQuiet;
    bool mHideIfEmpty;
};

class LLViewerFolderDictionary : public LLSimpleton<LLViewerFolderDictionary>,
                                 public LLDictionary<LLFolderType::EType, ViewerFolderEntry>
{
public:
    LLViewerFolderDictionary();
protected:
    bool initEnsemblesFromFile(); // Reads in ensemble information from foldertypes.xml
};

#endif // LL_LLVIEWERFOLDERTYPE_H
