/**
 * @file alfloaterbulkrename.h
 * @brief Renames several inventory items by one rule.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Wolfgang <wolfgang@wlf.io>
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

#include "llfloater.h"

#include "albulkrename.h"

class LLInventoryObject;
class LLButton;
class LLComboBox;
class LLLineEditor;
class LLPanel;
class LLSpinCtrl;
class LLTextBox;

class ALFloaterBulkRename final : public LLFloater
{
public:
    AL_VIEW_TYPE(ALFloaterBulkRename, LLFloater);

    ALFloaterBulkRename(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;

private:
    ~ALFloaterBulkRename() = default;

    ALBulkRename rule() const;
    // The selected objects that will be renamed, in selection order.
    std::vector<const LLInventoryObject*> targets() const;
    // The name an object's rename would give it, after the inventory's own corrections.
    static std::string newName(const ALBulkRename& rule, const LLInventoryObject* obj, int index, int total);
    void               updatePreview();
    void               onClickOK();

    uuid_vec_t mIDs;
    LLUUID     mTaskID; // Set when the items live in an object's contents.

    LLComboBox*   mModeCombo            = nullptr;
    LLComboBox*   mPositionCombo        = nullptr;
    LLComboBox*   mFormatKindCombo      = nullptr;
    LLComboBox*   mFormatPositionCombo  = nullptr;
    LLComboBox*   mDateFormatCombo      = nullptr;
    LLPanel*      mAddPanel             = nullptr;
    LLPanel*      mReplacePanel         = nullptr;
    LLPanel*      mFormatPanel          = nullptr;
    LLPanel*      mFormatPositionPanel  = nullptr;
    LLPanel*      mFormatDatePanel      = nullptr;
    LLPanel*      mFormatCounterPanel   = nullptr;
    LLPanel*      mFormatCustomPanel    = nullptr;
    LLLineEditor* mAddText              = nullptr;
    LLLineEditor* mReplaceText          = nullptr;
    LLLineEditor* mWithText             = nullptr;
    LLLineEditor* mFormatText           = nullptr;
    LLSpinCtrl*   mCounterStart         = nullptr;
    LLTextBox*    mPreviewOld           = nullptr;
    LLTextBox*    mPreviewNew           = nullptr;
    LLButton*     mOKButton             = nullptr;
};
