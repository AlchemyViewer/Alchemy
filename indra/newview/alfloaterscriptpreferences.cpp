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

#include "alpanelist.h"
#include "alsurface.h"

#include "alcodeeditor.h"
#include "alscriptstudio.h"
#include "alscriptanalysis.h"
#include "alscriptstudiosnippetnotecard.h"
#include "alscriptstudiovimrc.h"
#include "alscriptstudiowords.h"
#include "alfontfield.h"
#include "alpanelscriptaliases.h"
#include "alpanelscriptkeymap.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llcolorswatch.h"
#include "llcombobox.h"
#include "lldirpicker.h"
#include "llnotificationsutil.h"
#include "llscrollcontainer.h"
#include "llsdutil.h"
#include "llscrolllistctrl.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "alscriptsnippets.h"
#include "lllineeditor.h"
#include "llviewermenufile.h"
#include "lltexteditor.h"
#include "llfloaterreg.h"
#include "llinventorymodel.h"
#include "llviewerinventory.h"
#include "alscriptpreprocessor.h"
#include "alscriptworkspace.h"
#include "llagent.h"
#include "llfloaterperms.h"
#include "llnotecard.h"
#include "llviewerassettype.h"

#include <algorithm>
#include <functional>
#include <sstream>
#include <string_view>

namespace
{
    // The settings the window changes, remembered on opening and put
    // back on Cancel.
    const char* const SETTINGS[] = {
        "ALScriptStudioTheme",       "ALScriptStudioFontFamily",   "ALScriptStudioFontSize",
        "ALScriptStudioFontStyle",   "ALScriptStudioKeymap",       "ALScriptStudioRestoreTabs",
        "ALScriptStudioPreflight",   "ALScriptPreprocEnabled",     "ALScriptPreprocSwitch",
        "ALScriptPreprocLazyLists",  "ALScriptPreprocCompress",    "ALScriptPreprocOptimizer",
        "ALScriptPreprocOptimizerShrinkNames", "ALScriptPreprocOptimizerAddStrings",
        "ALScriptPreprocOptimizerInlining",     "ALScriptPreprocExtensions", "ALScriptPreprocLineComments",
        "ALScriptUploadHeader",      "ALScriptUploadHeaderCreator",
        "ALScriptPreprocDiskIncludes", "ALScriptPreprocDiskIncludeFolder", "ALScriptPreprocIncludeOrder", "ALScriptPreprocWorldIncludes",
        "ALScriptSLuaAliases",
        "ALScriptStudioTabWidth",    "ALScriptStudioInsertSpaces", "ALScriptStudioDetectIndentation", "ALScriptStudioReindentOnPaste",
        "ALScriptLintLevels",        "ALScriptLuauMode",           "ALScriptLuauSolver",
        "ALScriptStudioAutoComplete", "ALScriptStudioCompleteAfter", "ALScriptStudioAcceptOnEnter", "ALScriptFragmentCompletion",
        "ALScriptStudioAutoClose",
        "ALScriptStudioCaretStyle",  "ALScriptStudioCaretBlink",   "ALScriptStudioHoverCards",  "ALScriptStudioHoverDelay",
        "ALScriptStudioVimClipboard",
        "ALScriptFormatBlankLines",  "ALScriptFormatSpacing",      "ALScriptFormatOnSave",      "ALScriptTrimOnSave",        "ALScriptFixOnSave",
        "ALScriptTemplateLSL",       "ALScriptTemplateSLua",       "ALScriptPreprocDefines",    "ExternalEditor",
        "ALScriptConvertLLTimers",   "ALScriptConvertDetectedTable", "ALScriptConvertSLuaCalls", "ALScriptConvertIdioms",
        "ALScriptConvertHandlerFields", "ALScriptConvertTypes",    "ALScriptConvertComments",   "ALScriptConvertKeepComments",
    };

    // What each preset of Convert to SLua sets: SLua's own ways, or as
    // close to LSL as SLua lets it be. Handlers, types and comments are
    // the scripter's either way.
    const std::pair<const char*, bool> CONVERT_MODERN[] = {
        { "ALScriptConvertLLTimers", true }, { "ALScriptConvertDetectedTable", true }, { "ALScriptConvertSLuaCalls", true }, { "ALScriptConvertIdioms", true },
    };

    // The places an include is looked for, as the setting spells them.
    const char* const PLACES[] = { "inventory", "object", "disk" };

    // How long after the last keystroke an edited snippet is written.
    const F32 SNIPPET_SETTLE = 0.5f;

    // The notecard of the agent's own that a drop carries, a link as what
    // it links to -- the notecard's asset is what changes as it is edited;
    // null where it carries anything else. The kind first: a drag of a
    // person or a group carries an id, not an item.
    const LLViewerInventoryItem* droppedNotecard(EDragAndDropType cargo_type, void* cargo_data)
    {
        if (cargo_type != DAD_NOTECARD || !cargo_data)
        {
            return nullptr;
        }
        const LLInventoryItem*       item = static_cast<LLInventoryItem*>(cargo_data);
        const LLViewerInventoryItem* held = gInventory.getItem(item->getLinkedUUID());
        return held && held->getType() == LLAssetType::AT_NOTECARD ? held : nullptr;
    }

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
    mFolders      = getChild<ALPaneList>("include_folders");
    // With none, it says so where they would be.
    mFolders->setEmpty(getString("NoFolder"), LLStringUtil::null);
    mOrder        = getChild<LLScrollListCtrl>("include_order");
    mOrderTip     = mOrder->getToolTip();
    mTemplateLSL  = getChild<ALCodeEditor>("template_lsl");
    mTemplateSLua = getChild<ALCodeEditor>("template_slua");
    mDefines      = getChild<LLTextEditor>("preproc_defines");
    mSnippetLang   = getChild<LLComboBox>("snippet_language");
    mSnippetList   = getChild<LLScrollListCtrl>("snippet_list");
    mSnippetName   = getChild<LLLineEditor>("snippet_name");
    mSnippetPrefix = getChild<LLLineEditor>("snippet_prefix");
    mSnippetDetail = getChild<LLLineEditor>("snippet_detail");
    mSnippetBody   = getChild<ALCodeEditor>("snippet_body");

    mThemes->setCommitCallback([this](LLUICtrl*, const LLSD&) { onTheme(); });
    getChild<LLButton>("save_theme")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSaveTheme(); });
    getChild<LLButton>("restore_skin")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRestoreSkin(); });
    mPreviewLang->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshPreview(); });
    getChild<LLButton>("add_folder")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onAddIncludeFolder(); });
    getChild<LLButton>("remove_folder")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onRemoveIncludeFolder(); });
    getChild<LLButton>("external_browse")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onBrowseExternalEditor(); });
    mVimrcNotecard = getChild<LLLineEditor>("vimrc_notecard");
    getChild<LLButton>("vimrc_edit")->setCommitCallback([](LLUICtrl*, const LLSD&) { ALScriptStudio::openVimrc(); });
    getChild<LLButton>("vimrc_clear")->setCommitCallback(
        [](LLUICtrl*, const LLSD&) { ALScriptStudioVimrc::instance().useNotecard(LLUUID::null); });
    mVimrcChanged = ALScriptStudioVimrc::instance().onChanged([this]() { refreshVimrc(); });
    refreshVimrc();
    mSnippetNotecard = getChild<LLLineEditor>("snippet_notecard");
    getChild<LLButton>("snippet_notecard_clear")->setCommitCallback(
        [](LLUICtrl*, const LLSD&) { ALScriptStudioSnippetNotecard::instance().useNotecard(LLUUID::null); });
    mSnippetNotecardChanged = ALScriptStudioSnippetNotecard::instance().onChanged([this]() {
        refreshSnippetNotecard();
        fillSnippets(false);
    });
    refreshSnippetNotecard();
    getChild<LLButton>("scripting_settings")->setCommitCallback([](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("scripting_settings"); });
    getChild<LLButton>("snippet_xml")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        flushSnippets();
        ALScriptStudio::editSnippets(snippetLua());
    });
    getChild<LLButton>("snippet_new")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSnippetNew(); });
    getChild<LLButton>("snippet_copy")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSnippetCopy(); });
    getChild<LLButton>("snippet_delete")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSnippetDelete(); });
    getChild<LLButton>("snippet_to_inventory")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onSnippetsToInventory(); });
    mSnippetLang->setCommitCallback([this](LLUICtrl*, const LLSD&) { fillSnippets(true); });
    getChild<LLComboBox>("convert_preset")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onConvertPreset(); });
    for (const auto& [setting, modern] : CONVERT_MODERN)
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mConvertConnections.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) { refreshConvertPreset(); }));
        }
    }
    refreshConvertPreset();
    mSnippetList->setCommitCallback([this](LLUICtrl*, const LLSD&) { showSnippet(); });
    for (LLLineEditor* field : { mSnippetName, mSnippetPrefix, mSnippetDetail })
    {
        field->setKeystrokeCallback([this](LLLineEditor*, void*) { onSnippetEdited(); }, nullptr);
    }
    mSnippetBodyChanged = mSnippetBody->onTextChanged([this]() { onSnippetEdited(); });
    mDefines->setCommitCallback([this](LLUICtrl*, const LLSD&) { storeDefines(); });
    mTemplateLSLChanged  = mTemplateLSL->onTextChanged([this]() { storeTemplate(false); });
    mTemplateSLuaChanged = mTemplateSLua->onTextChanged([this]() { storeTemplate(true); });
    ALScriptStudioWords::teach(*mTemplateLSL, false);
    ALScriptStudioWords::teach(*mTemplateSLua, true);
    mOrder->setCommitCallback([this](LLUICtrl*, const LLSD&) { storeIncludeOrder(); });
    getChild<LLButton>("order_up")->setCommitCallback([this](LLUICtrl*, const LLSD&) { moveIncludePlace(-1); });
    getChild<LLButton>("order_down")->setCommitCallback([this](LLUICtrl*, const LLSD&) { moveIncludePlace(1); });
    // The editors open, and the ones here, take the typing settings as
    // they change -- by the settings' own signals, so a Cancel reaches
    // them as a click does.
    for (const char* setting : { "ALScriptStudioTabWidth", "ALScriptStudioInsertSpaces", "ALScriptStudioDetectIndentation", "ALScriptStudioReindentOnPaste",
                                 "ALScriptStudioAutoComplete", "ALScriptStudioCompleteAfter", "ALScriptStudioAcceptOnEnter", "ALScriptFragmentCompletion",
        "ALScriptStudioAutoClose",
                                 "ALScriptStudioCaretStyle", "ALScriptStudioCaretBlink", "ALScriptStudioHoverCards", "ALScriptStudioHoverDelay" })
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mEnableWatches.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) {
                ALScriptStudio::refreshAll();
                for (ALCodeEditor* editor : { mPreview, mTemplateLSL, mTemplateSLua, mSnippetBody })
                {
                    ALScriptStudio::applyTypingOptions(*editor);
                }
            }));
        }
    }
    for (ALCodeEditor* editor : { mPreview, mTemplateLSL, mTemplateSLua, mSnippetBody })
    {
        ALScriptStudio::applyTypingOptions(*editor);
    }
    mLintsLSL  = getChild<LLScrollListCtrl>("lints_lsl");
    mLintsLuau = getChild<LLScrollListCtrl>("lints_luau");
    mLintsLSL->setCommitCallback([this](LLUICtrl*, const LLSD&) { storeLints(false); });
    mLintsLuau->setCommitCallback([this](LLUICtrl*, const LLSD&) { storeLints(true); });
    getChild<LLButton>("lints_defaults")->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        mStoringLints = true;
        ALScriptLints::reset();
        mStoringLints = false;
        fillLints();
    });
    getChild<LLButton>("ok")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onOK(); });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&) { onCancel(); });

    // The font as the settings have it; what the field says goes back.
    refreshFont();
    mFont->onPartCommit([this](const std::string& part, const std::string& value) { onFontPart(part, value); });

    // The world's places greyed in the order while includes are not taken
    // from it, and back as they were once they are.
    if (LLControlVariable* control = gSavedSettings.getControl("ALScriptPreprocWorldIncludes"))
    {
        mEnableWatches.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) { refreshIncludeOrder(); }));
    }
    for (const char* setting : { "ALScriptPreprocOptimizer", "ALScriptUploadHeader", "ALScriptPreprocDiskIncludes", "ALScriptStudioAutoComplete", "ALScriptStudioHoverCards",
                                 "ALScriptPreprocDiskIncludeFolder", "ALScriptTemplateLSL", "ALScriptTemplateSLua", "ALScriptPreprocDefines" })
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mEnableWatches.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD&, const LLSD&) { refreshEnabled(); }));
        }
    }
    refreshEnabled();
    // A lint turned off or made an error from a problem's menu while this
    // window is open is the menu's doing, not this window's: the list shows
    // it, and Cancel does not take it back.
    if (LLControlVariable* control = gSavedSettings.getControl("ALScriptLintLevels"))
    {
        mEnableWatches.emplace_back(control->getSignal()->connect([this](LLControlVariable*, const LLSD& value, const LLSD&) {
            if (!mShowing || mStoringLints)
            {
                return;
            }
            mWasSettings["ALScriptLintLevels"] = value;
            fillLints();
        }));
    }

    buildSwatches();
    fillThemes();
    refreshSwatches();
    refreshPreview();
    mFolders->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshEnabled(); });
    refreshIncludeFolder();
    refreshIncludeOrder();
    refreshTemplates();
    fillSnippets(true);
    fillLints();
    return true;
}

void ALFloaterScriptPreferences::fillLints()
{
    fillLints(false);
    fillLints(true);
}

void ALFloaterScriptPreferences::fillLints(bool lua)
{
    LLScrollListCtrl* list     = lua ? mLintsLuau : mLintsLSL;
    const S32         scrolled = list->getScrollPos();
    const S32         chosen   = list->getFirstSelectedIndex();
    list->deleteAllItems();
    for (const ALScriptLints::Lint& lint : ALScriptLints::all())
    {
        if (lint.lua != lua)
        {
            continue;
        }
        const ALScriptLints::Level level       = ALScriptLints::level(lua, lint.id);
        const std::string          description = getString((lua ? "LintLuau" : "LintLSL") + lint.id);
        std::string                tip         = description;
        if (lua)
        {
            // The name a .luaurc gives it, for whoever keeps one.
            LLStringUtil::format_map_t args;
            args["[DESCRIPTION]"] = description;
            args["[NAME]"]        = lint.id;
            tip                   = getString("LintLuauName", args);
        }
        LLSD row;
        row["value"]                  = lint.id;
        row["columns"][0]["column"]   = "on";
        row["columns"][0]["type"]     = "checkbox";
        row["columns"][0]["value"]    = level != ALScriptLints::Level::Off;
        row["columns"][1]["column"]   = "error";
        row["columns"][1]["type"]     = "checkbox";
        row["columns"][1]["value"]    = level == ALScriptLints::Level::Error;
        row["columns"][2]["column"]   = "lint";
        row["columns"][2]["value"]    = description;
        row["columns"][2]["tool_tip"] = tip;
        list->addElement(row);
    }
    if (chosen >= 0)
    {
        list->selectNthItem(chosen);
    }
    list->setScrollPos(scrolled);
}

void ALFloaterScriptPreferences::storeLints(bool lua)
{
    LLScrollListCtrl* list = lua ? mLintsLuau : mLintsLSL;
    std::vector<std::pair<std::string, ALScriptLints::Level>> levels;
    for (LLScrollListItem* item : list->getAllData())
    {
        const LLScrollListCell* on    = item->getColumn(0);
        const LLScrollListCell* error = item->getColumn(1);
        const std::string       id    = item->getValue().asString();
        // An error is on; one turned off is no error. Which box was just
        // ticked decides where the two disagree.
        const bool was_error = ALScriptLints::level(lua, id) == ALScriptLints::Level::Error;
        bool       is_on     = on && on->getValue().asBoolean();
        bool       is_error  = error && error->getValue().asBoolean();
        if (is_error && !is_on)
        {
            if (!was_error)
            {
                is_on = true;   // Error just ticked: on with it.
            }
            else
            {
                is_error = false;   // On just unticked: off, and no error.
            }
        }
        levels.emplace_back(id, !is_on ? ALScriptLints::Level::Off : is_error ? ALScriptLints::Level::Error : ALScriptLints::Level::Warning);
    }
    mStoringLints = true;
    ALScriptLints::setLevels(lua, levels);
    mStoringLints = false;
    fillLints(lua);
}

void ALFloaterScriptPreferences::refreshEnabled()
{
    const bool optimize = gSavedSettings.getBOOL("ALScriptPreprocOptimizer");
    for (const char* name : { "preproc_shrink", "preproc_addstrings", "preproc_inline" })
    {
        getChildView(name)->setEnabled(optimize);
    }
    getChildView("preproc_upload_creator")->setEnabled(gSavedSettings.getBOOL("ALScriptUploadHeader"));
    const bool disk = gSavedSettings.getBOOL("ALScriptPreprocDiskIncludes");
    for (const char* name : { "include_folders", "add_folder" })
    {
        getChildView(name)->setEnabled(disk);
    }
    getChildView("remove_folder")->setEnabled(disk && mFolders->getFirstSelected() != nullptr);
    const bool complete = gSavedSettings.getBOOL("ALScriptStudioAutoComplete");
    for (const char* name : { "complete_after_label", "complete_after" })
    {
        getChildView(name)->setEnabled(complete);
    }
    const bool hover = gSavedSettings.getBOOL("ALScriptStudioHoverCards");
    for (const char* name : { "hover_delay_label", "hover_delay" })
    {
        getChildView(name)->setEnabled(hover);
    }
    // What the settings hold, where something other than these fields
    // changed them: the menu adding a folder, a Cancel.
    refreshIncludeFolder();
    refreshTemplates();
}

void ALFloaterScriptPreferences::onOpen(const LLSD& key)
{
    if (key.isMap() && key.has("tab") && !key["tab"].asString().empty())
    {
        mTabs->selectTabByName(key["tab"].asString() + "_tab");
    }
    if (mShowing)
    {
        // Asked for again while open -- for a tab, from the problems --
        // with what Cancel goes back to still what it was when it opened.
        fillLints();
        return;
    }
    mShowing = true;
    remember();
    mCancelled = false;
    mLeaving   = false;
    fillThemes();
    refreshSwatches();
    refreshFont();
    refreshPreview();
    refreshIncludeFolder();
    refreshIncludeOrder();
    fillSnippets(true);
    fillLints();
    if (ALPanelScriptKeymap* keys = findChild<ALPanelScriptKeymap>("keys_tab"))
    {
        keys->refresh();
    }
    // An inventory folder renamed, or a folder on disk gone, since the
    // aliases were last shown, which no setting says.
    if (ALPanelScriptAliases* aliases = findChild<ALPanelScriptAliases>("aliases_panel"))
    {
        aliases->refresh();
    }
}

void ALFloaterScriptPreferences::onClose(bool app_quitting)
{
    mShowing = false;
    // A snippet typed and not yet written goes with a Cancel, and is
    // written by any other way out.
    if (mCancelled)
    {
        mSnippetsUnsaved = false;
    }
    else
    {
        flushSnippets();
    }
    if (app_quitting)
    {
        // The viewer writes the colours itself on the way out.
        return;
    }
    // Cancelled, or Discard answered: as it was. OK, or Keep answered:
    // what was changed stays, as the window showed it changing.
    if (mCancelled)
    {
        revert();
    }
    mCancelled = false;
    LLUIColorTable::instance().saveUserSettings();
}

// --- keeping and reverting -------------------------------------------------------------

void ALFloaterScriptPreferences::remember()
{
    const LLUIColorTable& table = LLUIColorTable::instance();
    mWasColors.clear();
    for (const std::string& name : ALScriptTheme::names())
    {
        // One the table has no colour for is the editor's to mix, and
        // going back takes away whatever was set for it since.
        mWasColors[name] = WasColor{ table.getColor(name).get(), !table.colorExists(name) || table.isDefault(name) };
    }
    for (bool lua : { false, true })
    {
        mWasSnippets[lua ? 1 : 0] = ALScriptSnippets::fileText(lua, mWasSnippetsFile[lua ? 1 : 0]);
    }
    mWasSettings = LLSD::emptyMap();
    for (const char* setting : SETTINGS)
    {
        if (LLControlVariable* control = gSavedSettings.getControl(setting))
        {
            mWasSettings[setting] = control->getValue();
        }
    }
    // The account's, as the notecards are.
    mWasVimrc           = ALScriptStudioVimrc::instance().notecard().asString();
    mWasSnippetNotecard = ALScriptStudioSnippetNotecard::instance().notecard().asString();
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
    if (ALScriptStudioVimrc::instance().notecard().asString() != mWasVimrc)
    {
        ALScriptStudioVimrc::instance().useNotecard(LLUUID(mWasVimrc));
    }
    if (ALScriptStudioSnippetNotecard::instance().notecard().asString() != mWasSnippetNotecard)
    {
        ALScriptStudioSnippetNotecard::instance().useNotecard(LLUUID(mWasSnippetNotecard));
    }
    // The snippets as they were, where they were changed here.
    for (bool lua : { false, true })
    {
        bool              exists = false;
        const std::string now    = ALScriptSnippets::fileText(lua, exists);
        if (exists != mWasSnippetsFile[lua ? 1 : 0] || now != mWasSnippets[lua ? 1 : 0])
        {
            ALScriptSnippets::restoreFileText(lua, mWasSnippets[lua ? 1 : 0], mWasSnippetsFile[lua ? 1 : 0]);
        }
    }
    ALScriptStudio::refreshAll();
}

// static
void ALFloaterScriptPreferences::keepChanged(std::initializer_list<const char*> settings)
{
    // Not open, nothing remembered to go back to.
    ALFloaterScriptPreferences* prefs = LLFloaterReg::findTypedInstance<ALFloaterScriptPreferences>("script_studio_prefs");
    if (!prefs || !prefs->mShowing)
    {
        return;
    }
    for (const char* setting : settings)
    {
        const LLControlVariable* control = gSavedSettings.getControl(setting);
        if (control && prefs->mWasSettings.has(setting))
        {
            prefs->mWasSettings[setting] = control->getValue();
        }
    }
}

bool ALFloaterScriptPreferences::changed() const
{
    if (mSnippetsUnsaved || ALScriptStudioVimrc::instance().notecard().asString() != mWasVimrc ||
        ALScriptStudioSnippetNotecard::instance().notecard().asString() != mWasSnippetNotecard)
    {
        return true;
    }
    for (const char* setting : SETTINGS)
    {
        const LLControlVariable* control = gSavedSettings.getControl(setting);
        if (control && mWasSettings.has(setting) && !llsd_equals(control->getValue(), mWasSettings[setting]))
        {
            return true;
        }
    }
    const LLUIColorTable& table = LLUIColorTable::instance();
    for (const auto& [name, was] : mWasColors)
    {
        const bool skin = !table.colorExists(name) || table.isDefault(name);
        if (skin != was.skin || (!skin && table.getColor(name).get() != was.color))
        {
            return true;
        }
    }
    for (bool lua : { false, true })
    {
        bool              exists = false;
        const std::string now    = ALScriptSnippets::fileText(lua, exists);
        if (exists != mWasSnippetsFile[lua ? 1 : 0] || now != mWasSnippets[lua ? 1 : 0])
        {
            return true;
        }
    }
    return false;
}

bool ALFloaterScriptPreferences::canClose()
{
    // OK and Cancel have said what becomes of the changes, the viewer
    // going keeps them, and with none there is nothing to ask.
    if (mLeaving || isQuitRequested() || !mShowing || !changed())
    {
        return true;
    }
    // Every change here shows as it is made, so a close that quietly put
    // them back would surprise as much as one that quietly kept them:
    // asked.
    const LLHandle<LLFloater> handle = getHandle();
    LLNotificationsUtil::add("ScriptStudioPrefsChanged", LLSD(), LLSD(), [handle](const LLSD& notification, const LLSD& response) {
        ALFloaterScriptPreferences* prefs  = ALViewType::as<ALFloaterScriptPreferences>(handle.get());
        const S32                   option = LLNotificationsUtil::getSelectedOption(notification, response);
        if (!prefs || !prefs->mShowing)
        {
            return;
        }
        if (option == 0)
        {
            prefs->onOK();
        }
        else if (option == 1)
        {
            prefs->onCancel();
        }
    });
    return false;
}

void ALFloaterScriptPreferences::onOK()
{
    mCancelled = false;
    mLeaving   = true;
    closeFloater();
}

void ALFloaterScriptPreferences::onCancel()
{
    mCancelled = true;
    mLeaving   = true;
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
    // The chosen theme's name where it is the person's own, to save over;
    // a shipped one's is not theirs to take.
    const std::string chosen = ALScriptTheme::chosen();
    const bool        own    = std::any_of(mAvailable.begin(), mAvailable.end(), [&](const ALScriptTheme& theme) { return theme.own && theme.name == chosen; });
    LLSD              args;
    args["NAME"] = own ? chosen : getString("ThemeNewName");
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
        // A theme is chosen by its name, and a shipped one already has it:
        // two of one name would be one that could never be chosen.
        for (const ALScriptTheme& theme : self->mAvailable)
        {
            if (!theme.own && theme.name == name)
            {
                LLSD taken;
                taken["NAME"] = name;
                LLNotificationsUtil::add("ScriptStudioThemeNameShipped", taken);
                return;
            }
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
        // Cut with an ellipsis where the column is narrower than the words,
        // which the tip then says whole.
        lp.use_ellipses  = true;
        lp.tool_tip      = ALScriptTheme::labelOf(name);
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
        // A colour the table has none for, as the editor mixes it.
        LLColor4 shown = table.getColor(name).get();
        if (!table.colorExists(name))
        {
            for (U8 i = 0; i < static_cast<U8>(ALCodeEditor::Paint::COUNT); ++i)
            {
                const ALCodeEditor::Paint which = static_cast<ALCodeEditor::Paint>(i);
                if (name == mPreview->colorPrefix() + ALCodeEditor::paintName(which))
                {
                    shown = mPreview->paint(which);
                }
            }
        }
        swatch->setOriginal(shown);
        swatch->set(shown, true, true);
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
    ALScriptStudioWords::teach(*mPreview, lua);
    mPreview->setText(getString(lua ? "PreviewSLua" : "PreviewLSL"));
    mPreview->setFont(ALScriptStudio::editorFont());
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
    mPreview->setFont(ALScriptStudio::editorFont());
    ALScriptStudio::refreshAll();
}

// --- the editor tab -------------------------------------------------------------------

void ALFloaterScriptPreferences::refreshIncludeFolder()
{
    const S32 chosen = mFolders->getFirstSelectedIndex();
    mFolders->deleteAllItems();
    for (const std::string& folder : ALScriptPreprocessor::includeFolders())
    {
        LLSD row;
        row["value"]                  = folder;
        row["columns"][0]["column"]   = "folder";
        row["columns"][0]["value"]    = folder;
        row["columns"][0]["tool_tip"] = folder;
        mFolders->addElement(row);
    }
    if (chosen >= 0 && mFolders->getItemCount() > 0)
    {
        mFolders->selectNthItem(llmin(chosen, mFolders->getItemCount() - 1));
    }
}

void ALFloaterScriptPreferences::onRemoveIncludeFolder()
{
    LLScrollListItem* item = mFolders->getFirstSelected();
    if (!item)
    {
        return;
    }
    std::vector<std::string> folders = ALScriptPreprocessor::includeFolders();
    folders.erase(std::remove(folders.begin(), folders.end(), item->getValue().asString()), folders.end());
    ALScriptPreprocessor::setIncludeFolders(folders);
}

void ALFloaterScriptPreferences::refreshVimrc()
{
    const ALScriptStudioVimrc& vimrc    = ALScriptStudioVimrc::instance();
    const bool                 notecard = vimrc.notecard().notNull();
    if (notecard)
    {
        const std::string name = vimrc.notecardName();
        mVimrcNotecard->setText(name.empty() ? getString("VimrcUnknownNotecard") : name);
    }
    else
    {
        // Empty: the box's label asks for one.
        mVimrcNotecard->setText(LLStringUtil::null);
    }
    getChildView("vimrc_clear")->setEnabled(notecard);
    LLStringUtil::format_map_t args;
    args["[PATH]"] = ALScriptStudioVimrc::filePath();
    LLTextBox* where = getChild<LLTextBox>("vimrc_where");
    where->setText(!vimrc.error().empty() ? vimrc.error() : getString(notecard ? "VimrcFromNotecard" : "VimrcFromFile", args));
}

void ALFloaterScriptPreferences::refreshSnippetNotecard()
{
    const ALScriptStudioSnippetNotecard& followed = ALScriptStudioSnippetNotecard::instance();
    const bool                           notecard = followed.notecard().notNull();
    const std::string                    name     = followed.notecardName();
    // Empty with none: the box's label asks for one.
    mSnippetNotecard->setText(!notecard ? std::string() : name.empty() ? getString("SnippetNotecardUnknown") : name);
    mSnippetNotecard->setToolTip(!followed.error().empty() ? followed.error() : getString("SnippetNotecardTip"));
    getChildView("snippet_notecard_clear")->setEnabled(notecard);
}

bool ALFloaterScriptPreferences::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data,
                                                   EAcceptance* accept, std::string& tooltip_msg)
{
    // Which box it is over, and what a notecard dropped there does; each
    // takes a notecard of the agent's own alone.
    const auto over = [this, x, y](LLView* view) {
        S32 local_x = 0;
        S32 local_y = 0;
        return view && view->isInVisibleChain() && localPointToOtherView(x, y, &local_x, &local_y, view) && view->pointInView(local_x, local_y);
    };
    const auto take = [&](const char* only, const std::function<void(const LLViewerInventoryItem&)>& use) {
        const LLViewerInventoryItem* held = droppedNotecard(cargo_type, cargo_data);
        if (!held)
        {
            *accept     = ACCEPT_NO;
            tooltip_msg = getString(only);
            return true;
        }
        *accept = ACCEPT_YES_SINGLE;
        if (drop)
        {
            use(*held);
        }
        return true;
    };
    if (over(mVimrcNotecard))
    {
        return take("VimrcOnlyNotecards", [](const LLViewerInventoryItem& held) { ALScriptStudioVimrc::instance().useNotecard(held.getUUID()); });
    }
    if (over(mSnippetNotecard))
    {
        // Followed: its snippets offered as it stands.
        return take("SnippetsFollowOnlyNotecards",
                    [](const LLViewerInventoryItem& held) { ALScriptStudioSnippetNotecard::instance().useNotecard(held.getUUID()); });
    }
    if (over(mSnippetList))
    {
        // Its snippets come in beside the scripter's own.
        return take("SnippetsOnlyNotecards", [this](const LLViewerInventoryItem& held) { addSnippetsFrom(held); });
    }
    return LLFloater::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data, accept, tooltip_msg);
}

void ALFloaterScriptPreferences::onBrowseExternalEditor()
{
    const LLHandle<LLFloater> handle = getHandle();
    LLFilePickerReplyThread::startPicker(
        [handle](const std::vector<std::string>& files, LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter) {
            if (files.empty() || !handle.get())
            {
                return;
            }
            // The program, then the file it is to open, each quoted as a
            // path with spaces in it needs.
            gSavedSettings.setString("ExternalEditor", "\"" + files.front() + "\" \"%s\"");
        },
        LLFilePicker::FFLOAD_EXE, false);
}

void ALFloaterScriptPreferences::refreshTemplates()
{
    mSettingTemplates = true;
    for (bool lua : { false, true })
    {
        ALCodeEditor*     editor = lua ? mTemplateSLua : mTemplateLSL;
        const std::string text   = gSavedSettings.getString(lua ? "ALScriptTemplateSLua" : "ALScriptTemplateLSL");
        if (editor->wholeText() != text)
        {
            editor->setText(text);
        }
    }
    const std::string defines = gSavedSettings.getString("ALScriptPreprocDefines");
    if (!mDefines->hasFocus() && mDefines->getText() != defines)
    {
        mDefines->setText(defines);
    }
    mSettingTemplates = false;
}

void ALFloaterScriptPreferences::storeTemplate(bool lua)
{
    if (mSettingTemplates)
    {
        return;
    }
    gSavedSettings.setString(lua ? "ALScriptTemplateSLua" : "ALScriptTemplateLSL", (lua ? mTemplateSLua : mTemplateLSL)->text());
}

void ALFloaterScriptPreferences::storeDefines()
{
    gSavedSettings.setString("ALScriptPreprocDefines", mDefines->getText());
}

void ALFloaterScriptPreferences::refreshIncludeOrder()
{
    // The places the setting names, in its order and ticked; after them
    // those it leaves out, unticked.
    std::vector<std::string> named;
    std::istringstream       words(gSavedSettings.getString("ALScriptPreprocIncludeOrder"));
    std::string              word;
    while (words >> word)
    {
        const bool known = std::find(std::begin(PLACES), std::end(PLACES), word) != std::end(PLACES);
        if (known && std::find(named.begin(), named.end(), word) == named.end())
        {
            named.push_back(word);
        }
    }
    const S32 chosen = mOrder->getFirstSelectedIndex();
    mOrder->deleteAllItems();
    // The object and the inventory while includes are not taken from them:
    // greyed, their tick and their place kept for when they are, and not
    // to be changed until then.
    const bool world = gSavedSettings.getBOOL("ALScriptPreprocWorldIncludes");
    const auto add   = [this, world](const std::string& place, bool on) {
        LLSD row;
        row["value"]                = place;
        row["columns"][0]["column"] = "on";
        row["columns"][0]["type"]   = "checkbox";
        row["columns"][0]["value"]  = on;
        row["columns"][1]["column"] = "place";
        row["columns"][1]["value"]  = getString(place == "inventory" ? "PlaceInventory" : place == "object" ? "PlaceObject" : "PlaceDisk");
        LLScrollListItem* item      = mOrder->addElement(row);
        const bool        live      = world || place == "disk";
        if (item && !live)
        {
            item->setEnabled(false);
            if (LLScrollListCell* tick = item->getColumn(0))
            {
                tick->setEnabled(false);
            }
        }
    };
    mOrder->setToolTip(world ? mOrderTip : getString("OrderWorldOff"));
    for (const std::string& place : named)
    {
        add(place, true);
    }
    for (const char* place : PLACES)
    {
        if (std::find(named.begin(), named.end(), place) == named.end())
        {
            add(place, false);
        }
    }
    if (chosen >= 0 && chosen < mOrder->getItemCount() && mOrder->getAllData()[chosen]->getEnabled())
    {
        mOrder->selectNthItem(chosen);
    }
}

void ALFloaterScriptPreferences::storeIncludeOrder()
{
    std::string order;
    for (LLScrollListItem* item : mOrder->getAllData())
    {
        const LLScrollListCell* on = item->getColumn(0);
        if (on && on->getValue().asBoolean())
        {
            order += (order.empty() ? "" : " ") + item->getValue().asString();
        }
    }
    gSavedSettings.setString("ALScriptPreprocIncludeOrder", order);
}

void ALFloaterScriptPreferences::moveIncludePlace(S32 by)
{
    const S32 at = mOrder->getFirstSelectedIndex();
    const S32 to = at + by;
    if (at < 0 || to < 0 || to >= mOrder->getItemCount())
    {
        return;
    }
    if (by < 0)
    {
        mOrder->swapWithPrevious(at);
    }
    else
    {
        mOrder->swapWithNext(at);
    }
    mOrder->selectNthItem(to);
    storeIncludeOrder();
}

void ALFloaterScriptPreferences::onAddIncludeFolder()
{
    const std::vector<std::string> now = ALScriptPreprocessor::includeFolders();
    (new LLDirPickerThread(
         [](const std::vector<std::string>& folders, std::string) {
             if (folders.empty())
             {
                 return;
             }
             // One more to look in, after those there are.
             std::vector<std::string> list = ALScriptPreprocessor::includeFolders();
             if (std::find(list.begin(), list.end(), folders.front()) == list.end())
             {
                 list.push_back(folders.front());
             }
             ALScriptPreprocessor::setIncludeFolders(list);
             gSavedSettings.setBOOL("ALScriptPreprocDiskIncludes", true);
         },
         now.empty() ? std::string() : now.back()))
        ->getFile();
}

// --- snippets ------------------------------------------------------------------------

bool ALFloaterScriptPreferences::snippetLua() const
{
    return mSnippetLang->getValue().asString() == "slua";
}

S32 ALFloaterScriptPreferences::chosenOwnSnippet() const
{
    // A row's value says whose it is and where, as the list compares
    // values by their words: "own:2", "followed:0", "builtin:5".
    const LLScrollListItem* item  = mSnippetList->getFirstSelected();
    const std::string       value = item ? item->getValue().asString() : std::string();
    if (value.compare(0, 4, "own:") != 0)
    {
        return -1;
    }
    const S32 index = atoi(value.c_str() + 4);
    return index >= 0 && index < static_cast<S32>(mOwnSnippets.size()) ? index : -1;
}

void ALFloaterScriptPreferences::flushSnippets()
{
    if (!mSnippetsUnsaved)
    {
        return;
    }
    mSnippetsUnsaved = false;
    // The language they were read for, which the chooser may have moved
    // off by now.
    ALScriptSnippets::saveOwn(mSnippetsLua, mOwnSnippets);
}

void ALFloaterScriptPreferences::fillSnippets(bool reread)
{
    // What is waiting written before the file is read again.
    flushSnippets();
    const bool lua = snippetLua();
    if (reread || lua != mSnippetsLua)
    {
        mSnippetsLua = lua;
        mOwnSnippets = ALScriptSnippets::own(lua);
    }
    const LLScrollListItem* first    = mSnippetList->getFirstSelected();
    const LLSD              chosen   = first ? first->getValue() : LLSD();
    const S32               scrolled = mSnippetList->getScrollPos();
    // One of the notecard's is found again by its name: the notecard may
    // have changed under it, and its place with it.
    std::string chosen_followed;
    if (first && chosen.asString().compare(0, 9, "followed:") == 0 && first->getColumn(0))
    {
        chosen_followed = first->getColumn(0)->getValue().asString();
    }
    mSnippetList->deleteAllItems();
    const LLUIColor& theirs = ALSurface::quiet();
    LLStringUtil::format_map_t args;
    args["[NAME]"]               = ALScriptStudioSnippetNotecard::instance().notecardName();
    const std::string followed   = getString("SnippetFollowed", args);
    const auto add = [this, &theirs, &followed](const ALScriptSnippets::Snippet& one, const char* whose, S32 index) {
        const bool own = std::string_view(whose) == "own";
        LLSD row;
        row["value"]                  = std::string(whose) + ":" + std::to_string(index);
        row["columns"][0]["column"]   = "name";
        row["columns"][0]["value"]    = one.name.empty() ? getString("SnippetUnnamed") : one.name;
        row["columns"][0]["tool_tip"] = own ? one.detail : one.followed ? followed : getString("SnippetBuiltin");
        row["columns"][1]["column"]   = "prefix";
        row["columns"][1]["value"]    = one.prefix;
        if (!own)
        {
            // The viewer's and the notecard's, quieter than the scripter's
            // own.
            row["columns"][0]["color"] = theirs.get().getValue();
            row["columns"][1]["color"] = theirs.get().getValue();
        }
        mSnippetList->addElement(row);
    };
    for (size_t i = 0; i < mOwnSnippets.size(); ++i)
    {
        add(mOwnSnippets[i], "own", static_cast<S32>(i));
    }
    const std::vector<ALScriptSnippets::Snippet>& notecard = ALScriptSnippets::followed(lua);
    for (size_t i = 0; i < notecard.size(); ++i)
    {
        add(notecard[i], "followed", static_cast<S32>(i));
    }
    // Why the notecard's are not there, or not as it stands, where they
    // would be: a row that cannot be chosen.
    if (const std::string& error = ALScriptStudioSnippetNotecard::instance().error(); !error.empty())
    {
        LLSD row;
        row["value"]                  = "followed:error";
        row["enabled"]                = false;
        row["columns"][0]["column"]   = "name";
        row["columns"][0]["value"]    = error;
        row["columns"][0]["tool_tip"] = error;
        row["columns"][0]["color"]    = LLUIColorTable::instance().getColor("ScriptErrorColor").get().getValue();
        mSnippetList->addElement(row);
    }
    S32 builtin = 0;
    for (const ALScriptSnippets::Snippet& one : ALScriptSnippets::all(lua))
    {
        if (one.builtin)
        {
            add(one, "builtin", builtin++);
        }
    }
    if (!chosen_followed.empty())
    {
        for (const LLScrollListItem* row : mSnippetList->getAllData())
        {
            if (row->getValue().asString().compare(0, 9, "followed:") == 0 && row->getColumn(0) &&
                row->getColumn(0)->getValue().asString() == chosen_followed)
            {
                mSnippetList->selectByValue(row->getValue());
                break;
            }
        }
    }
    else if (chosen.isString())
    {
        mSnippetList->selectByValue(chosen);
    }
    mSnippetList->setScrollPos(scrolled);
    mSnippetBody->setSyntax(lua ? "slua" : "lsl");
    ALScriptStudioWords::teach(*mSnippetBody, lua);
    showSnippet();
}

void ALFloaterScriptPreferences::showSnippet()
{
    const LLScrollListItem*          item  = mSnippetList->getFirstSelected();
    const S32                        own   = chosenOwnSnippet();
    const ALScriptSnippets::Snippet* shown = nullptr;
    if (own >= 0)
    {
        shown = &mOwnSnippets[static_cast<size_t>(own)];
    }
    else if (item)
    {
        // The followed notecard's, or the viewer's, by its place among them.
        const std::string                             value    = item->getValue().asString();
        const std::vector<ALScriptSnippets::Snippet>& notecard = ALScriptSnippets::followed(snippetLua());
        if (value.compare(0, 9, "followed:") == 0)
        {
            const S32 index = atoi(value.c_str() + 9);
            shown           = index >= 0 && index < static_cast<S32>(notecard.size()) ? &notecard[static_cast<size_t>(index)] : nullptr;
        }
        else if (value.compare(0, 8, "builtin:") == 0)
        {
            S32 builtin = 0;
            for (const ALScriptSnippets::Snippet& one : ALScriptSnippets::all(snippetLua()))
            {
                if (one.builtin && builtin++ == atoi(value.c_str() + 8))
                {
                    shown = &one;
                    break;
                }
            }
        }
    }
    mSettingSnippet = true;
    mSnippetName->setText(shown ? shown->name : std::string());
    mSnippetPrefix->setText(shown ? shown->prefix : std::string());
    mSnippetDetail->setText(shown ? shown->detail : std::string());
    const std::string body = shown ? shown->body : std::string();
    if (mSnippetBody->text() != body)
    {
        mSnippetBody->setText(body);
    }
    mSettingSnippet = false;
    // The scripter's own are edited here; the viewer's and the notecard's
    // are read, and copied.
    const bool editable = own >= 0;
    mSnippetName->setEnabled(editable);
    mSnippetPrefix->setEnabled(editable);
    mSnippetDetail->setEnabled(editable);
    mSnippetBody->setReadOnly(!editable);
    getChildView("snippet_copy")->setEnabled(shown != nullptr);
    getChildView("snippet_delete")->setEnabled(editable);
}

void ALFloaterScriptPreferences::onSnippetEdited()
{
    const S32 own = chosenOwnSnippet();
    if (mSettingSnippet || own < 0)
    {
        return;
    }
    ALScriptSnippets::Snippet& one = mOwnSnippets[static_cast<size_t>(own)];
    one.name   = mSnippetName->getText();
    one.prefix = mSnippetPrefix->getText();
    one.detail = mSnippetDetail->getText();
    one.body   = mSnippetBody->text();
    // The row says what the fields do at once, and the file holds it --
    // whatever has a name and a body -- for the studio to offer once the
    // typing stops.
    if (LLScrollListItem* item = mSnippetList->getFirstSelected())
    {
        if (LLScrollListCell* name = item->getColumn(0))
        {
            name->setValue(one.name.empty() ? getString("SnippetUnnamed") : one.name);
        }
        if (LLScrollListCell* prefix = item->getColumn(1))
        {
            prefix->setValue(one.prefix);
        }
    }
    mSnippetsUnsaved = true;
    const U32                 edit   = ++mSnippetEdits;
    const LLHandle<LLFloater> handle = getHandle();
    doAfterInterval([handle, edit]() {
        ALFloaterScriptPreferences* self = ALViewType::as<ALFloaterScriptPreferences>(handle.get());
        if (self && self->mSnippetEdits == edit)
        {
            self->flushSnippets();
        }
    }, SNIPPET_SETTLE);
}

void ALFloaterScriptPreferences::onSnippetNew()
{
    ALScriptSnippets::Snippet one;
    one.name = getString("SnippetNewName");
    mOwnSnippets.push_back(one);
    mSnippetList->deselectAllItems();
    fillSnippets(false);
    mSnippetList->selectByValue(LLSD("own:" + std::to_string(mOwnSnippets.size() - 1)));
    mSnippetList->scrollToShowSelected();
    showSnippet();
    mSnippetName->setFocus(true);
    mSnippetName->selectAll();
}

void ALFloaterScriptPreferences::onSnippetCopy()
{
    const std::string body = mSnippetBody->text();
    if (body.empty() && mSnippetName->getText().empty())
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = mSnippetName->getText();
    ALScriptSnippets::Snippet one;
    one.name   = getString("SnippetCopyName", args);
    one.prefix = mSnippetPrefix->getText();
    one.detail = mSnippetDetail->getText();
    one.body   = body;
    mOwnSnippets.push_back(one);
    mSnippetsUnsaved = true;
    flushSnippets();
    mSnippetList->deselectAllItems();
    fillSnippets(false);
    mSnippetList->selectByValue(LLSD("own:" + std::to_string(mOwnSnippets.size() - 1)));
    mSnippetList->scrollToShowSelected();
    showSnippet();
}

void ALFloaterScriptPreferences::onSnippetDelete()
{
    const S32 own = chosenOwnSnippet();
    if (own < 0)
    {
        return;
    }
    mOwnSnippets.erase(mOwnSnippets.begin() + own);
    mSnippetsUnsaved = true;
    flushSnippets();
    mSnippetList->deselectAllItems();
    fillSnippets(false);
}

void ALFloaterScriptPreferences::onSnippetsToInventory()
{
    flushSnippets();
    const std::vector<ALScriptSnippets::Snippet> lsl  = ALScriptSnippets::own(false);
    const std::vector<ALScriptSnippets::Snippet> slua = ALScriptSnippets::own(true);
    if (lsl.empty() && slua.empty())
    {
        LLNotificationsUtil::add("ScriptStudioSnippetsNone");
        return;
    }
    // Refused before an item is made, rather than an empty notecard left
    // where the upload would refuse it.
    const std::string text = ALScriptSnippets::notecardText(lsl, slua);
    if (text.size() > static_cast<size_t>(LLNotecard::MAX_SIZE))
    {
        LLSD args;
        args["SIZE"]  = static_cast<S32>(text.size());
        args["LIMIT"] = static_cast<S32>(LLNotecard::MAX_SIZE);
        LLNotificationsUtil::add("ScriptStudioSnippetsTooLarge", args);
        return;
    }
    // A new notecard of the account's default permissions, as every other
    // way of making one gives, and the snippets saved in it once it is.
    const std::string name     = getString("SnippetsNotecardName");
    const std::string not_made = getString("SnippetsNotecardNotMade");
    const S32         count    = static_cast<S32>(lsl.size() + slua.size());
    const auto        failed = [name](const std::string& why) {
        LLSD args;
        args["NAME"]   = name;
        args["REASON"] = why;
        LLNotificationsUtil::add("ScriptStudioSnippetsNotSaved", args);
    };
    LLPointer<LLBoostFuncInventoryCallback> made = new LLBoostFuncInventoryCallback(create_notecard_cb);
    made->addOnFireFunc([text, name, not_made, count, failed](const LLUUID& item_id) {
        if (item_id.isNull())
        {
            failed(not_made);
            return;
        }
        std::string error;
        const bool  sent = ALScriptWorkspace::getInstance()->saveNotecard(
            ALScriptRef(LLUUID::null, item_id), text, {},
            [name, count, failed](const ALScriptCompileResult& result) {
                if (!result.success)
                {
                    failed(result.error);
                    return;
                }
                LLSD args;
                args["NAME"]  = name;
                args["COUNT"] = count;
                LLNotificationsUtil::add("ScriptStudioSnippetsSaved", args);
            },
            error);
        if (!sent)
        {
            failed(error);
        }
    });
    std::string desc;
    LLViewerAssetType::generateDescriptionFor(LLAssetType::AT_NOTECARD, desc);
    create_inventory_item(gAgent.getID(), gAgent.getSessionID(), gInventory.findCategoryUUIDForType(LLFolderType::FT_NOTECARD),
                          LLTransactionID::tnull, name, desc, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, NO_INV_SUBTYPE,
                          LLFloaterPerms::getNextOwnerPerms("Notecards"), made);
}

void ALFloaterScriptPreferences::refreshConvertPreset()
{
    bool all = true, none = true;
    for (const auto& [setting, modern] : CONVERT_MODERN)
    {
        const bool on = gSavedSettings.getBOOL(setting);
        all           = all && on == modern;
        none          = none && on != modern;
    }
    getChild<LLComboBox>("convert_preset")->selectByValue(all ? "modern" : none ? "close" : "custom");
}

void ALFloaterScriptPreferences::onConvertPreset()
{
    const std::string preset = getChild<LLComboBox>("convert_preset")->getValue().asString();
    if (preset == "custom")
    {
        return;
    }
    for (const auto& [setting, modern] : CONVERT_MODERN)
    {
        gSavedSettings.setBOOL(setting, preset == "modern" ? modern : !modern);
    }
    refreshConvertPreset();
}

void ALFloaterScriptPreferences::addSnippetsFrom(const LLViewerInventoryItem& notecard)
{
    const LLHandle<LLFloater> handle = getHandle();
    const std::string         name   = notecard.getName();
    ALScriptWorkspace::getInstance()->load(ALScriptRef(LLUUID::null, notecard.getUUID()), [handle, name](const ALScriptLoaded& loaded) {
        // Brought in only while the window is open: its Cancel takes the
        // snippets back with everything else the window changed.
        ALFloaterScriptPreferences* self = ALViewType::as<ALFloaterScriptPreferences>(handle.get());
        if (!self)
        {
            return;
        }
        LLSD args;
        args["NAME"] = name;
        if (!loaded.error.empty())
        {
            args["REASON"] = loaded.error;
            LLNotificationsUtil::add("ScriptStudioSnippetsNotRead", args);
            return;
        }
        std::vector<ALScriptSnippets::Snippet> lsl;
        std::vector<ALScriptSnippets::Snippet> slua;
        if (!ALScriptSnippets::readNotecard(loaded.text, self->snippetLua(), lsl, slua))
        {
            LLNotificationsUtil::add("ScriptStudioSnippetsNotSnippets", args);
            return;
        }
        self->flushSnippets();
        ALScriptSnippets::Merged all;
        for (bool lua : { false, true })
        {
            const std::vector<ALScriptSnippets::Snippet>& incoming = lua ? slua : lsl;
            if (incoming.empty())
            {
                continue;
            }
            std::vector<ALScriptSnippets::Snippet> own    = ALScriptSnippets::own(lua);
            const ALScriptSnippets::Merged         merged = ALScriptSnippets::merge(own, incoming);
            if (merged.added + merged.renamed > 0)
            {
                ALScriptSnippets::saveOwn(lua, own);
            }
            all.added += merged.added;
            all.renamed += merged.renamed;
            all.skipped += merged.skipped;
        }
        self->fillSnippets(true);
        args["ADDED"]   = static_cast<S32>(all.added);
        args["RENAMED"] = static_cast<S32>(all.renamed);
        args["SKIPPED"] = static_cast<S32>(all.skipped);
        LLNotificationsUtil::add("ScriptStudioSnippetsAdded", args);
    });
}
