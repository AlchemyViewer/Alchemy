/**
 * @file alfloaterscriptpreferences.h
 * @brief Script Studio's preferences: the editors' colours by theme or by swatch against a live preview, their font, the editor's ways, and the keys.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#pragma once

#include "alscripttheme.h"
#include "llfloater.h"

#include <map>
#include <string>
#include <vector>

class ALCodeEditor;
class ALFontField;
class LLColorSwatchCtrl;
class LLComboBox;
class LLPanel;
class LLScrollListCtrl;
class LLTabContainer;
class LLTextBox;

// One window for everything about the studio a person sets once:
// Colours, with a theme to pick and every colour to change by hand
// against a preview in either language, and the font in the viewer's
// own font field; Editor, with the ways of saving and preprocessing;
// and Keys, the keyboard map. What is changed is in force at once in
// every open studio.
class ALFloaterScriptPreferences final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALFloaterScriptPreferences, LLFloater);

    ALFloaterScriptPreferences(const LLSD& key);
    ~ALFloaterScriptPreferences() override;

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;

private:
    // Everything changes as it is changed, so that the editors show it;
    // OK keeps it, and Cancel -- or the close box -- puts back every
    // colour and setting as it was when the window opened.
    void remember();
    void revert();
    void onOK();
    void onCancel();

    // The theme list read again, the chosen one shown.
    void fillThemes();
    void onTheme();
    void onSaveTheme();
    void onRestoreSkin();
    // The swatches made once, and set from the table again.
    void buildSwatches();
    void refreshSwatches();
    void onSwatch(const std::string& name, LLColorSwatchCtrl* swatch);
    // The preview's text and words in the language chosen, and the font.
    void refreshPreview();
    // The font field as the settings have it.
    void refreshFont();
    void onFontPart(const std::string& part, const std::string& value);
    void onIncludeFolder();
    void refreshIncludeFolder();
    // Where an include is looked for: the places in the setting's order,
    // each ticked on or off, moved up and down.
    void refreshIncludeOrder();
    void storeIncludeOrder();
    void moveIncludePlace(S32 by);

    LLTabContainer*                                        mTabs        = nullptr;
    LLComboBox*                                            mThemes      = nullptr;
    LLComboBox*                                            mPreviewLang = nullptr;
    LLScrollListCtrl*                                      mOrder       = nullptr;
    ALFontField*                                           mFont        = nullptr;
    LLPanel*                                               mSwatches    = nullptr;
    ALCodeEditor*                                          mPreview     = nullptr;
    LLTextBox*                                             mFolder      = nullptr;
    std::vector<ALScriptTheme>                             mAvailable;
    std::vector<std::pair<std::string, LLColorSwatchCtrl*>> mSwatchList;
    // While the swatches are being set from the table, their commits
    // are the table's own and not a person's.
    bool                                                   mSettingSwatches = false;
    // What was in force when the window opened: each colour, and
    // whether it was the skin's; each setting's value.
    struct WasColor
    {
        LLColor4 color;
        bool     skin = true;
    };
    std::map<std::string, WasColor>                        mWasColors;
    LLSD                                                   mWasSettings;
    bool                                                   mKept = false;
};
