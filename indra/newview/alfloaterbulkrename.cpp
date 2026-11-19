/**
 * @file alfloaterbulkrename.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alfloaterbulkrename.h"

// library
#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llsdutil.h"
#include "llspinctrl.h"
#include "lltextbox.h"

// newview
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llnotificationsutil.h"
#include "llpreview.h"
#include "llviewerinventory.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"

namespace
{
constexpr size_t CONFIRM_RENAME_ABOVE = 15;
}

// static
void ALFloaterBulkRename::show(const LLSD& key)
{
    static LLCachedControl<U32> max_items(gSavedSettings, "AlchemyBulkRenameMaxItems", 100);
    const size_t count = key["ids"].size();
    if (count > max_items())
    {
        LLSD args;
        args["MAX"]   = static_cast<LLSD::Integer>(max_items());
        args["COUNT"] = static_cast<LLSD::Integer>(count);
        LLNotificationsUtil::add("BulkRenameTooMany", args);
        return;
    }
    LLFloaterReg::showInstance("bulk_rename", key);
}

ALFloaterBulkRename::ALFloaterBulkRename(const LLSD& key)
:   LLFloater(key)
{
}

bool ALFloaterBulkRename::postBuild()
{
    mModeCombo           = getChild<LLComboBox>("mode_combo");
    mPositionCombo       = getChild<LLComboBox>("position_combo");
    mFormatKindCombo     = getChild<LLComboBox>("format_kind_combo");
    mFormatPositionCombo = getChild<LLComboBox>("format_position_combo");
    mDateFormatCombo     = getChild<LLComboBox>("date_format_combo");
    mAddPanel            = getChild<LLPanel>("add_panel");
    mReplacePanel        = getChild<LLPanel>("replace_panel");
    mFormatPanel         = getChild<LLPanel>("format_panel");
    mFormatPositionPanel = getChild<LLPanel>("format_position_panel");
    mFormatDatePanel     = getChild<LLPanel>("format_date_panel");
    mFormatCounterPanel  = getChild<LLPanel>("format_counter_panel");
    mFormatCustomPanel   = getChild<LLPanel>("format_custom_panel");
    mAddText             = getChild<LLLineEditor>("add_text");
    mReplaceText         = getChild<LLLineEditor>("replace_text");
    mWithText            = getChild<LLLineEditor>("with_text");
    mFormatText          = getChild<LLLineEditor>("format_text");
    mCounterStart        = getChild<LLSpinCtrl>("counter_start");
    mPreviewOld          = getChild<LLTextBox>("preview_old");
    mPreviewNew          = getChild<LLTextBox>("preview_new");
    mOKButton            = getChild<LLButton>("btn_ok");

    mModeCombo->setValue("add");
    mPositionCombo->setValue("after");
    mFormatKindCombo->setValue("date_current");
    mFormatPositionCombo->setValue("after");
    mDateFormatCombo->setValue("%Y-%m-%d");

    auto refresh = [this](LLUICtrl*, const LLSD&) { updatePreview(); };
    mModeCombo->setCommitCallback(refresh);
    mPositionCombo->setCommitCallback(refresh);
    mFormatKindCombo->setCommitCallback(refresh);
    mFormatPositionCombo->setCommitCallback(refresh);
    mDateFormatCombo->setCommitCallback(refresh);
    mCounterStart->setCommitCallback(refresh);
    for (LLLineEditor* editor : { mAddText, mReplaceText, mWithText, mFormatText })
    {
        editor->setKeystrokeCallback([this](LLLineEditor*, void*) { updatePreview(); }, nullptr);
    }

    mOKButton->setClickedCallback([this](LLUICtrl*, const LLSD&) { onClickOK(); });
    getChild<LLButton>("btn_cancel")->setClickedCallback([this](LLUICtrl*, const LLSD&) { closeFloater(); });

    return LLFloater::postBuild();
}

void ALFloaterBulkRename::onOpen(const LLSD& key)
{
    mIDs.clear();
    mTaskID = key["task_id"].asUUID();
    for (const LLSD& id : llsd::inArray(key["ids"]))
    {
        mIDs.push_back(id.asUUID());
    }
    updatePreview();
}

std::vector<const LLInventoryObject*> ALFloaterBulkRename::targets() const
{
    std::vector<const LLInventoryObject*> result;
    if (mTaskID.notNull())
    {
        LLViewerObject* object = gObjectList.findObject(mTaskID);
        if (!object)
        {
            return result;
        }
        for (const LLUUID& id : mIDs)
        {
            const LLInventoryObject* obj = object->getInventoryObject(id);
            // The root "Contents" folder is a category, and cannot be renamed.
            if (obj && obj->getType() != LLAssetType::AT_CATEGORY && get_is_task_item_renameable(object, id))
            {
                result.push_back(obj);
            }
        }
        return result;
    }

    for (const LLUUID& id : mIDs)
    {
        const LLInventoryObject* obj = gInventory.getObject(id);
        if (!obj || obj->getIsLinkType())
        {
            continue;
        }
        const bool renameable = gInventory.getCategory(id) ? get_is_category_renameable(&gInventory, id)
                                                            : get_is_item_renameable(&gInventory, id);
        if (renameable)
        {
            result.push_back(obj);
        }
    }
    return result;
}

ALBulkRename ALFloaterBulkRename::rule() const
{
    ALBulkRename rule;
    const std::string mode = mModeCombo->getValue().asString();
    if (mode == "replace")
    {
        rule.mode = ALBulkRename::Mode::Replace;
        rule.text = mReplaceText->getText();
        rule.with = mWithText->getText();
    }
    else if (mode == "format")
    {
        rule.mode     = ALBulkRename::Mode::Format;
        rule.position = mFormatPositionCombo->getValue().asString() == "before" ? ALBulkRename::Position::Before
                                                                                : ALBulkRename::Position::After;
        rule.text     = mFormatText->getText();
        rule.counter_start = mCounterStart->getValue().asInteger();
        if (mFormatKindCombo->getValue().asString() == "count")
        {
            rule.format_kind   = ALBulkRename::FormatKind::Count;
        }
        else if (mFormatKindCombo->getValue().asString() == "index")
        {
            rule.format_kind   = ALBulkRename::FormatKind::Index;
        }
        else if (mFormatKindCombo->getValue().asString() == "date_current")
        {
            rule.format_kind = ALBulkRename::FormatKind::CurrentDate;
            rule.date_format = mDateFormatCombo->getValue().asString();
        }
        else if (mFormatKindCombo->getValue().asString() == "date_acquired")
        {
            rule.format_kind = ALBulkRename::FormatKind::AcquiredDate;
            rule.date_format = mDateFormatCombo->getValue().asString();
        }
        else if (mFormatKindCombo->getValue().asString() == "custom")
        {
            rule.format_kind = ALBulkRename::FormatKind::Custom;
        }
    }
    else
    {
        rule.mode     = ALBulkRename::Mode::Add;
        rule.position = mPositionCombo->getValue().asString() == "before" ? ALBulkRename::Position::Before
                                                                          : ALBulkRename::Position::After;
        rule.text     = mAddText->getText();
    }
    return rule;
}

std::string ALFloaterBulkRename::newName(const ALBulkRename& rule, const LLInventoryObject* obj, int index, int total)
{
    std::string new_name = rule.apply(obj->getName(), index, total, obj->getCreationDate());
    LLInventoryObject::correctInventoryName(new_name);
    return new_name;
}

void ALFloaterBulkRename::updatePreview()
{
    const ALBulkRename current = rule();
    mAddPanel->setVisible(current.mode == ALBulkRename::Mode::Add);
    mReplacePanel->setVisible(current.mode == ALBulkRename::Mode::Replace);
    mFormatPanel->setVisible(current.mode == ALBulkRename::Mode::Format);
    mFormatPositionPanel->setVisible(current.mode == ALBulkRename::Mode::Format &&
                                    current.format_kind != ALBulkRename::FormatKind::Custom);
    mFormatDatePanel->setVisible(current.mode == ALBulkRename::Mode::Format &&
                                (
                                    current.format_kind == ALBulkRename::FormatKind::CurrentDate
                                    ||
                                    current.format_kind == ALBulkRename::FormatKind::AcquiredDate
                                ));
    mFormatCounterPanel->setVisible(current.mode == ALBulkRename::Mode::Format &&
                                    (
                                        current.format_kind == ALBulkRename::FormatKind::Count
                                        ||
                                        current.format_kind == ALBulkRename::FormatKind::Index
                                        ||
                                        current.format_kind == ALBulkRename::FormatKind::Custom
                                    ));
    mFormatCustomPanel->setVisible(current.mode == ALBulkRename::Mode::Format &&
                                    current.format_kind == ALBulkRename::FormatKind::Custom);

    const std::vector<const LLInventoryObject*> objects = targets();
    const bool can_ok = !objects.empty() &&
                        (current.mode == ALBulkRename::Mode::Format || !current.text.empty());
    mOKButton->setEnabled(can_ok);

    const LLInventoryObject* first = objects.empty() ? nullptr : objects.front();
    mPreviewOld->setText(first ? first->getName() : LLStringUtil::null);
    mPreviewNew->setText(first ? newName(current, first, 0, static_cast<int>(objects.size())) : LLStringUtil::null);
}

void ALFloaterBulkRename::onClickOK()
{
    const ALBulkRename                          current = rule();
    const std::vector<const LLInventoryObject*> objects = targets();
    const int                                   total   = static_cast<int>(objects.size());

    // Name everything before renaming anything: a rename can replace the
    // objects these pointers refer to.
    std::vector<std::pair<LLUUID, std::string>> renames;
    for (int index = 0; index < total; ++index)
    {
        const LLInventoryObject* obj      = objects[index];
        std::string              new_name = newName(current, obj, index, total);
        if (!new_name.empty() && new_name != obj->getName())
        {
            renames.emplace_back(obj->getUUID(), std::move(new_name));
        }
    }

    if (renames.size() > CONFIRM_RENAME_ABOVE)
    {
        LLSD args;
        args["COUNT"] = static_cast<LLSD::Integer>(renames.size());
        const LLHandle<LLFloater> handle = getHandle();
        LLNotificationsUtil::add("BulkRenameConfirm", args, LLSD(),
                                 [handle, renames](const LLSD& notification, const LLSD& response) {
                                     ALFloaterBulkRename* floater = ALViewType::as<ALFloaterBulkRename>(handle.get());
                                     if (floater && LLNotificationsUtil::getSelectedOption(notification, response) == 0)
                                     {
                                         floater->applyRenames(renames);
                                     }
                                 });
        return;
    }
    applyRenames(renames);
}

void ALFloaterBulkRename::applyRenames(const std::vector<std::pair<LLUUID, std::string>>& renames)
{
    if (mTaskID.notNull())
    {
        if (LLViewerObject* object = gObjectList.findObject(mTaskID))
        {
            for (const auto& [id, new_name] : renames)
            {
                rename_task_item(object, id, new_name);
            }
        }
        closeFloater();
        return;
    }

    for (const auto& [id, new_name] : renames)
    {
        if (gInventory.getCategory(id))
        {
            rename_category(&gInventory, id, new_name);
        }
        else
        {
            LLPreview::dirty(id);
            LLSD updates;
            updates["name"] = new_name;
            update_inventory_item(id, updates, nullptr);
        }
    }
    closeFloater();
}
