/**
 * @file alfloaterscriptpreferences.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterscriptpreferences.h"

#include "alcodeeditor.h"
#include "alfloaterscriptstudio.h"
#include "alfontfield.h"
#include "alscriptkeymap.h"
#include "llbutton.h"
#include "llcolorswatch.h"
#include "llcombobox.h"
#include "lldirpicker.h"
#include "llnotificationsutil.h"
#include "llscrollcontainer.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"

namespace
{
    // The settings the window changes, remembered on opening and put
    // back on Cancel.
    const char* const SETTINGS[] = {
        "ALScriptStudioTheme",       "ALScriptStudioFontFamily",   "ALScriptStudioFontSize",
        "ALScriptStudioFontStyle",   "ALScriptStudioKeymap",       "ALScriptStudioEnabled",
        "ALScriptStudioPreflight",   "ALScriptPreprocEnabled",     "ALScriptPreprocSwitch",
        "ALScriptPreprocLazyLists",  "ALScriptPreprocCompress",    "ALScriptPreprocOptimizer",
        "ALScriptPreprocOptimizerShrinkNames", "ALScriptPreprocOptimizerAddStrings",
        "ALScriptPreprocOptimizerInlining",     "ALScriptPreprocExtensions",
        "ALScriptPreprocDiskIncludes", "ALScriptPreprocDiskIncludeFolder", "ALScriptPreprocIncludeOrder",
    };

    const S32 SWATCH_ROW    = 24;
    const S32 SWATCH_WIDTH  = 48;
    const S32 SWATCH_HEIGHT = 20;
    const S32 HEADING_ROW   = 26;
    const S32 SWATCH_PAD    = 6;
}

ALFloaterScriptPreferences::ALFloaterScriptPreferences(const LLSD& key) : LLFloater(key) {}

ALFloaterScriptPreferences::~ALFloaterScriptPreferences() = default;

bool ALFloaterScriptPreferences::postBuild()
{
    mTabs        = getChild<LLTabContainer>("tabs");
    mThemes      = getChild<LLComboBox>("theme");
    mPreviewLang = getChild<LLComboBox>("preview_language");
    mFont        = getChild<ALFontField>("font");
    mSwatches    = getChild<LLPanel>("swatches");
    mPreview     = getChild<ALCodeEditor>("preview");
    mFolder      = getChild<LLTextBox>("include_folder");

    mThemes->setCommitCallback([this](LLUICtrl*, const LLSD&) { onTheme(); });
    getChild<LLButton>("save_theme")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveTheme(); });
    getChild<LLButton>("restore_skin")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRestoreSkin(); });
    mPreviewLang->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshPreview(); });
    getChild<LLButton>("choose_folder")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onIncludeFolder(); });
    getChild<LLButton>("ok")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onOK(); });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onCancel(); });

    // The font as the settings have it; what the field says goes back.
    refreshFont();
    mFont->onPartCommit([this](const std::string& part, const std::string& value) { onFontPart(part, value); });

    buildSwatches();
    fillThemes();
    refreshSwatches();
    refreshPreview();
    refreshIncludeFolder();
    return true;
}

void ALFloaterScriptPreferences::onOpen(const LLSD& key)
{
    if (key.isMap() && key.has("tab") && !key["tab"].asString().empty())
    {
        mTabs->selectTabByName(key["tab"].asString() + "_tab");
    }
    remember();
    mKept = false;
    fillThemes();
    refreshSwatches();
    refreshFont();
    refreshPreview();
    refreshIncludeFolder();
    if (ALPanelScriptKeymap* keys = findChild<ALPanelScriptKeymap>("keys_tab"))
    {
        keys->refresh();
    }
}

void ALFloaterScriptPreferences::onClose(bool app_quitting)
{
    if (app_quitting)
    {
        // The viewer writes the colours itself on the way out.
        return;
    }
    // Closed without OK: as it was.
    if (!mKept)
    {
        revert();
    }
    LLUIColorTable::instance().saveUserSettings();
}

// --- keeping and reverting -------------------------------------------------------------

void ALFloaterScriptPreferences::remember()
{
    const LLUIColorTable& table = LLUIColorTable::instance();
    mWasColors.clear();
    for (const std::string& name : ALScriptTheme::names())
    {
        mWasColors[name] = WasColor{ table.getColor(name).get(), table.isDefault(name) };
    }
    mWasSettings = LLSD::emptyMap();
    for (const char* setting : SETTINGS)
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mWasSettings[setting] = control->getValue();
        }
    }
}

void ALFloaterScriptPreferences::revert()
{
    LLUIColorTable& table = LLUIColorTable::instance();
    for (const auto& [name, was] : mWasColors)
    {
        if (was.skin)
        {
            table.resetToDefault(name);
        }
        else
        {
            table.setColor(name, was.color);
        }
    }
    for (const char* setting : SETTINGS)
    {
        LLControlVariable* control = gSavedSettings.getControl(setting);
        if (control && mWasSettings.has(setting))
        {
            control->setValue(mWasSettings[setting]);
        }
    }
    ALFloaterScriptStudio::refreshAll();
}

void ALFloaterScriptPreferences::onOK()
{
    mKept = true;
    closeFloater();
}

void ALFloaterScriptPreferences::onCancel()
{
    mKept = false;
    closeFloater();
}

// --- themes ----------------------------------------------------------------------

void ALFloaterScriptPreferences::fillThemes()
{
    mAvailable = ALScriptTheme::available();
    mThemes->removeall();
    mThemes->add(getString("ThemeCustom"), LLSD(""));
    for (const ALScriptTheme& theme : mAvailable)
    {
        mThemes->add(theme.own ? theme.name + " " + getString("ThemeOwn") : theme.name, LLSD(theme.name));
    }
    const std::string chosen = ALScriptTheme::chosen();
    if (!mThemes->setSelectedByValue(LLSD(chosen), true))
    {
        mThemes->setSelectedByValue(LLSD(""), true);
    }
}

void ALFloaterScriptPreferences::onTheme()
{
    const std::string name = mThemes->getValue().asString();
    if (name.empty())
    {
        // Custom: the colours as they stand, remembered as nobody's.
        ALScriptTheme::setChosen(LLStringUtil::null);
        return;
    }
    for (const ALScriptTheme& theme : mAvailable)
    {
        if (theme.name == name)
        {
            theme.apply();
            refreshSwatches();
            return;
        }
    }
}

void ALFloaterScriptPreferences::onSaveTheme()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLSD                      args;
    args["NAME"] = ALScriptTheme::chosen().empty() ? getString("ThemeNewName") : ALScriptTheme::chosen();
    LLNotificationsUtil::add("ScriptStudioSaveTheme", args, LLSD(), [handle](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptPreferences* self = ALViewType::as<ALFloaterScriptPreferences>(handle.get());
        if (!self || LLNotificationsUtil::getSelectedOption(notification, response) != 0)
        {
            return;
        }
        std::string name = response["name"].asString();
        LLStringUtil::trim(name);
        if (name.empty())
        {
            return;
        }
        ALScriptTheme theme = ALScriptTheme::capture(name);
        std::string   path;
        if (!theme.save(path))
        {
            LLSD args;
            args["PATH"] = path;
            LLNotificationsUtil::add("ScriptStudioThemeNotSaved", args);
            return;
        }
        ALScriptTheme::setChosen(name);
        self->fillThemes();
    });
}

void ALFloaterScriptPreferences::onRestoreSkin()
{
    ALScriptTheme::restoreSkin();
    fillThemes();
    refreshSwatches();
}

// --- swatches ----------------------------------------------------------------------

void ALFloaterScriptPreferences::buildSwatches()
{
    const std::vector<std::string>& names = ALScriptTheme::names();
    const S32                       width = mSwatches->getRect().getWidth();
    // Two headings, each above its colours; the rows counted first so
    // that the panel is tall enough for the scroll container to know.
    S32 rows = 0, headings = 0;
    bool editor = true;
    for (const std::string& name : names)
    {
        if (rows == 0 || editor != ALScriptTheme::isEditorColor(name))
        {
            ++headings;
            editor = ALScriptTheme::isEditorColor(name);
        }
        ++rows;
    }
    const S32 height = headings * HEADING_ROW + rows * SWATCH_ROW + SWATCH_PAD;
    mSwatches->reshape(width, height);
    S32  y       = height - SWATCH_PAD;
    bool first   = true;
    editor       = true;
    auto heading = [&](const std::string& text) {
        LLTextBox::Params p(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        p.name       = "heading_" + text;
        p.rect       = LLRect(SWATCH_PAD, y, width - SWATCH_PAD, y - HEADING_ROW + 4);
        p.initial_value = text;
        p.font          = LLFontGL::getFontSansSerifBold();
        mSwatches->addChild(LLUICtrlFactory::create<LLTextBox>(p));
        y -= HEADING_ROW;
    };
    for (const std::string& name : names)
    {
        const bool own = ALScriptTheme::isEditorColor(name);
        if (first || own != editor)
        {
            heading(getString(own ? "HeadingEditor" : "HeadingSyntax"));
            editor = own;
            first  = false;
        }
        LLColorSwatchCtrl::Params sp(LLUICtrlFactory::getDefaultParams<LLColorSwatchCtrl>());
        sp.name                  = name;
        sp.rect                  = LLRect(SWATCH_PAD, y, SWATCH_PAD + SWATCH_WIDTH, y - SWATCH_HEIGHT);
        sp.can_apply_immediately = true;
        sp.label_height          = 0;
        sp.tool_tip              = name;
        LLColorSwatchCtrl* swatch = LLUICtrlFactory::create<LLColorSwatchCtrl>(sp);
        swatch->setCommitCallback([this, name, swatch](LLUICtrl*, const LLSD&) { onSwatch(name, swatch); });
        mSwatches->addChild(swatch);
        LLTextBox::Params lp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        lp.name          = "label_" + name;
        lp.rect          = LLRect(SWATCH_PAD + SWATCH_WIDTH + 8, y - 2, width - SWATCH_PAD, y - SWATCH_HEIGHT + 2);
        lp.initial_value = ALScriptTheme::labelOf(name);
        mSwatches->addChild(LLUICtrlFactory::create<LLTextBox>(lp));
        mSwatchList.emplace_back(name, swatch);
        y -= SWATCH_ROW;
    }
}

void ALFloaterScriptPreferences::refreshSwatches()
{
    mSettingSwatches = true;
    const LLUIColorTable& table = LLUIColorTable::instance();
    for (auto& [name, swatch] : mSwatchList)
    {
        swatch->setOriginal(table.getColor(name).get());
        swatch->set(table.getColor(name).get(), true, true);
    }
    mSettingSwatches = false;
}

void ALFloaterScriptPreferences::onSwatch(const std::string& name, LLColorSwatchCtrl* swatch)
{
    if (mSettingSwatches)
    {
        return;
    }
    LLUIColorTable::instance().setColor(name, LLColor4(swatch->getValue()));
    // A colour changed by hand is nobody's theme any more.
    if (!ALScriptTheme::chosen().empty())
    {
        ALScriptTheme::setChosen(LLStringUtil::null);
        mThemes->setSelectedByValue(LLSD(""), true);
    }
}

// --- the preview and the font --------------------------------------------------------

void ALFloaterScriptPreferences::refreshPreview()
{
    const bool lua = mPreviewLang->getValue().asString() == "slua";
    mPreview->setSyntax(lua ? "slua" : "lsl");
    ALFloaterScriptStudio::teachWords(*mPreview, lua);
    mPreview->setText(getString(lua ? "PreviewSLua" : "PreviewLSL"));
    mPreview->setFont(ALFloaterScriptStudio::editorFont());
}

void ALFloaterScriptPreferences::refreshFont()
{
    const std::string family = gSavedSettings.getString("ALScriptStudioFontFamily");
    const std::string size   = gSavedSettings.getString("ALScriptStudioFontSize");
    mFont->setValue(family.empty() ? std::string("Monospace") : family);
    mFont->setSize(size.empty() ? std::string("Monospace") : size);
    mFont->setStyle(gSavedSettings.getString("ALScriptStudioFontStyle"));
}

void ALFloaterScriptPreferences::onFontPart(const std::string& part, const std::string& value)
{
    if (part.empty())
    {
        gSavedSettings.setString("ALScriptStudioFontFamily", value == "Monospace" ? std::string() : value);
    }
    else if (part == "size")
    {
        gSavedSettings.setString("ALScriptStudioFontSize", value == "Monospace" ? std::string() : value);
    }
    else if (part == "style")
    {
        gSavedSettings.setString("ALScriptStudioFontStyle", value);
    }
    mPreview->setFont(ALFloaterScriptStudio::editorFont());
    ALFloaterScriptStudio::refreshAll();
}

// --- the editor tab -------------------------------------------------------------------

void ALFloaterScriptPreferences::refreshIncludeFolder()
{
    const std::string folder = gSavedSettings.getString("ALScriptPreprocDiskIncludeFolder");
    mFolder->setText(folder.empty() ? getString("NoFolder") : folder);
}

void ALFloaterScriptPreferences::onIncludeFolder()
{
    const LLHandle<LLFloater> handle = getHandle();
    (new LLDirPickerThread(
         [handle](const std::vector<std::string>& folders, std::string) {
             ALFloaterScriptPreferences* self = ALViewType::as<ALFloaterScriptPreferences>(handle.get());
             if (folders.empty() || !self)
             {
                 return;
             }
             gSavedSettings.setString("ALScriptPreprocDiskIncludeFolder", folders.front());
             gSavedSettings.setBOOL("ALScriptPreprocDiskIncludes", true);
             self->refreshIncludeFolder();
         },
         gSavedSettings.getString("ALScriptPreprocDiskIncludeFolder")))
        ->getFile();
}
