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

#include "alscriptsnippets.h"
#include "alscripttheme.h"
#include "llfloater.h"

#include <map>
#include <string>
#include <vector>

class ALPaneList;
class ALCodeEditor;
class ALFontField;
class LLTextEditor;
class LLLineEditor;
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
    // The close box, Escape, or anything else that closes the window but
    // OK and Cancel: where something was changed, asked first whether to
    // keep it, put it back, or stay.
    bool canClose() override;
    // A notecard dropped on the vimrc's box.
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept,
                           std::string& tooltip_msg) override;

private:
    // Everything changes as it is changed, so that the editors show it;
    // OK keeps it, and Cancel puts back every colour and setting as it
    // was when the window opened.
    void remember();
    void revert();
    // Whether anything differs from what was in force then.
    bool changed() const;
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
    // The folders an include is read from: a list to add to and take from.
    void onAddIncludeFolder();
    void onRemoveIncludeFolder();
    void refreshIncludeFolder();
    // The external editor's program, chosen; the command made from it.
    void onBrowseExternalEditor();
    // What new scripts start with and the macros every script has, from
    // the settings, and back to them as they are edited.
    void refreshTemplates();
    void storeTemplate(bool lua);
    void storeDefines();
    // The snippets of the language chosen: the scripter's own, which the
    // fields edit and each change writes back, then the viewer's, which
    // are copied to be changed.
    void fillSnippets(bool reread);
    // The scripter's own written back, where an edit is waiting to be.
    void flushSnippets();
    void showSnippet();
    void onSnippetEdited();
    void onSnippetNew();
    void onSnippetCopy();
    void onSnippetDelete();
    bool snippetLua() const;
    // The chosen row: one of the scripter's own, by index, or not.
    S32  chosenOwnSnippet() const;
    // Where an include is looked for: the places in the setting's order,
    // each ticked on or off, moved up and down.
    void refreshIncludeOrder();
    // What depends on another setting goes grey while that one is off:
    // the optimizer's own options, and the folder an include is read from.
    void refreshEnabled();
    // The lints, each ticked as the settings have it, and a list's ticks
    // back to them.
    void fillLints();
    void fillLints(bool lua);
    void storeLints(bool lua);
    void storeIncludeOrder();
    void moveIncludePlace(S32 by);
    // The vimrc's box: the notecard's name, or none, and where the vimrc
    // is read from or why it could not be.
    void refreshVimrc();

    LLTabContainer*                                        mTabs        = nullptr;
    LLComboBox*                                            mThemes      = nullptr;
    LLComboBox*                                            mPreviewLang = nullptr;
    LLScrollListCtrl*                                      mOrder       = nullptr;
    // The list's own tip, which says more where the world's places are off.
    std::string                                            mOrderTip;
    ALFontField*                                           mFont        = nullptr;
    LLPanel*                                               mSwatches    = nullptr;
    ALCodeEditor*                                          mPreview     = nullptr;
    ALPaneList*                                            mFolders     = nullptr;
    ALCodeEditor*                                          mTemplateLSL  = nullptr;
    ALCodeEditor*                                          mTemplateSLua = nullptr;
    LLTextEditor*                                          mDefines      = nullptr;
    LLComboBox*                                            mSnippetLang   = nullptr;
    LLScrollListCtrl*                                      mSnippetList   = nullptr;
    LLLineEditor*                                          mSnippetName   = nullptr;
    LLLineEditor*                                          mSnippetPrefix = nullptr;
    LLLineEditor*                                          mSnippetDetail = nullptr;
    ALCodeEditor*                                          mSnippetBody   = nullptr;
    std::vector<ALScriptSnippets::Snippet>                 mOwnSnippets;
    bool                                                   mSnippetsLua     = false;
    bool                                                   mSettingSnippet  = false;
    // Edits not yet written: a keystroke marks them, and they are written
    // a moment after the typing stops -- or before anything reads the file
    // again, or as the window goes -- rather than the file once a key.
    bool                                                   mSnippetsUnsaved = false;
    U32                                                    mSnippetEdits    = 0;
    boost::signals2::scoped_connection                     mSnippetBodyChanged;
    // Each language's file as it was when the window opened, for Cancel.
    std::string                                            mWasSnippets[2];
    bool                                                   mWasSnippetsFile[2] = { false, false };
    // While the templates are being set from the settings, their edits
    // are the settings' own.
    bool                                                   mSettingTemplates = false;
    boost::signals2::scoped_connection                     mTemplateLSLChanged;
    boost::signals2::scoped_connection                     mTemplateSLuaChanged;
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
    // Cancel pressed, or Discard answered: what was changed goes back as
    // the window closes. And the way out chosen -- OK, Cancel, an answer
    // to the question a close with changes asks -- so that the close goes
    // through without asking.
    bool                                                   mCancelled = false;
    bool                                                   mLeaving   = false;
    // The lints being stored from here, so that a change of them made
    // elsewhere -- a problem's menu -- is told from one of this window's.
    bool                                                   mStoringLints = false;
    // Open, with what Cancel goes back to remembered.
    bool                                                   mShowing = false;
    LLLineEditor*                                          mVimrcNotecard = nullptr;
    // The notecard that was the vimrc when the window opened, for Cancel.
    std::string                                            mWasVimrc;
    boost::signals2::scoped_connection                     mVimrcChanged;
    LLScrollListCtrl*                                      mLintsLSL  = nullptr;
    LLScrollListCtrl*                                      mLintsLuau = nullptr;
    // The settings the enabling follows, by their own signals, so that it
    // follows a revert as well as a click.
    std::vector<boost::signals2::scoped_connection>        mEnableWatches;
};
