/**
 * @file llfloatersettingsdebug.h
 * @brief floater for debugging internal viewer settings
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#ifndef LLFLOATERDEBUGSETTINGS_H
#define LLFLOATERDEBUGSETTINGS_H

#include "llcontrol.h"
#include "alpropertygrid.h"
#include "llfloater.h"

class ALPropertyGrid;
class LLScrollListCtrl;
class LLSpinCtrl;
class LLTextBox;

class LLFloaterSettingsDebug final
:   public LLFloater
{
    friend class LLFloaterReg;

public:
    AL_VIEW_TYPE(LLFloaterSettingsDebug, LLFloater);

    virtual bool postBuild();
    virtual void draw();

    void updateControl(LLControlVariable* control);

    // The grid's two answers: a row committed, and a row's way back pressed.
    void onEditorCommit(const std::string& name, const std::string& text);
    void onEditorRemove(const std::string& name);

    bool matchesSearchFilter(std::string setting_name);
    bool isSettingHidden(LLControlVariable* control);

private:
    // key - selects which settings to show, one of:
    // "all", "base", "account", "skin"
    LLFloaterSettingsDebug(const LLSD& key);
    virtual ~LLFloaterSettingsDebug();

    void updateList(bool skip_selection = false);
    void onSettingSelect();
    void setSearchFilter(const std::string& filter);

    void updateDefaultColumn(LLControlVariable* control);

    LLScrollListCtrl* mSettingList;

    std::vector<std::string> mSearchTokens;

protected:
    class LLTextEditor* mComment;
    // The editor a setting gets is the editor its type asks for, which is
    // what a property grid is. This pane made one of each by hand and showed
    // the right ones; now it hands the grid a field and the grid decides.
    ALPropertyGrid*     mEditor = nullptr;
    LLTextBox*          mSettingNameText = nullptr;

    std::string mSearchFilter;
    std::string mOldText;

    // What the grid is holding. Asked every frame, because a setting changed
    // anywhere else has to show here -- and a row built again every frame is
    // a row nobody can type into, so nothing is built unless one of these
    // has moved.
    std::string mShownName;
    std::string mShownValue;
    bool        mShownWritten = false;
    bool        mShownEditable = true;
};

#endif //LLFLOATERDEBUGSETTINGS_H

