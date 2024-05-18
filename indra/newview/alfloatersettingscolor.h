/**
 * @file alfloatersettingscolor.h
 * @brief floater for debugging internal viewer colors
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2024, Rye Mutt<rye@alchemyviewer.org>.
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

#ifndef ALFLOATERCOLORSETTINGS_H
#define ALFLOATERCOLORSETTINGS_H

#include "llcontrol.h"
#include "llfloater.h"

class LLColorSwatchCtrl;
class LLScrollListCtrl;
class LLSpinCtrl;
class LLTextBox;

class ALFloaterSettingsColor final
:   public LLFloater
{
    friend class LLFloaterReg;

public:

    virtual BOOL postBuild();
    virtual void draw();

    void updateControl(const std::string& color_name);

    void onCommitSettings();
    void onClickDefault();

    bool matchesSearchFilter(std::string setting_name);
    bool isSettingHidden(const std::string& color_name);

private:
    // key - selects which settings to show, one of:
    // "all", "base", "account", "skin"
    ALFloaterSettingsColor(const LLSD& key);
    virtual ~ALFloaterSettingsColor();

    void updateList(bool skip_selection = false);
    void onSettingSelect();
    void setSearchFilter(const std::string& filter);

    void updateDefaultColumn(const std::string& color_name);
    void hideUIControls();

    LLScrollListCtrl* mSettingList;

protected:
    LLUICtrl*           mDefaultButton = nullptr;
    LLTextBox*          mSettingNameText = nullptr;

    LLSpinCtrl* mAlphaSpinner = nullptr;
    LLColorSwatchCtrl* mColorSwatch = nullptr;

    std::string mSearchFilter;
};

#endif //ALFLOATERCOLORSETTINGS_H

