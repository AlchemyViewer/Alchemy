/**
 * @file llfloaterdeleteprefpreset.cpp
 * @brief Floater to delete a graphics / camera preset
 *
 * $LicenseInfo:firstyear=2014&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2014, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "llfloaterdeleteprefpreset.h"

#include "alfloaterlightbox.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterpreference.h"
#include "llnotificationsutil.h"
#include "llpresetsmanager.h"
#include "llviewercontrol.h"
#include "llfloaterreg.h"

LLFloaterDeletePrefPreset::LLFloaterDeletePrefPreset(const LLSD &key)
:   LLFloater(key)
{
}

// virtual
bool LLFloaterDeletePrefPreset::postBuild()
{
    LLFloaterPreference* preferences = LLFloaterReg::findTypedInstance<LLFloaterPreference>("preferences");
    if (preferences)
    {
        preferences->addDependentFloater(this);
    }
    getChild<LLButton>("delete")->setCommitCallback(boost::bind(&LLFloaterDeletePrefPreset::onBtnDelete, this));
    getChild<LLButton>("cancel")->setCommitCallback(boost::bind(&LLFloaterDeletePrefPreset::onBtnCancel, this));
    LLPresetsManager::instance().setPresetListChangeCallback(boost::bind(&LLFloaterDeletePrefPreset::onPresetsListChange, this));
    LLPresetsManager::instance().setPresetListChangeLooksCallback(boost::bind(&LLFloaterDeletePrefPreset::onPresetsListChange, this));

    return true;
}

void LLFloaterDeletePrefPreset::onOpen(const LLSD& key)
{
    mSubdirectory = key.asString();
    std::string title_type = std::string("title_") + mSubdirectory;
    if (hasString(title_type))
    {
        std::string floater_title = getString(title_type);
        setTitle(floater_title);
    }
    else
    {
        LL_WARNS() << title_type << " not found" << LL_ENDL;
        setTitle(title_type);
    }

    fillPresetCombo();
}

void LLFloaterDeletePrefPreset::fillPresetCombo()
{
    LLComboBox* combo  = getChild<LLComboBox>("preset_combo");
    const bool  action = LLPresetsManager::getInstance()->setPresetNamesInComboBox(mSubdirectory, combo, DEFAULT_HIDE);
    getChild<LLButton>("delete")->setEnabled(action);
}

namespace
{
    // The preset deleted, and for a Look that was in use, Neutral put on in
    // its place: the settings would otherwise stay the deleted Look's, under
    // a bar that names some other one.
    void deletePresetNamed(const std::string& subdirectory, const std::string& name)
    {
        // Asked before the delete, which forgets it.
        const bool look_in_use = PRESETS_LOOKS == subdirectory &&
                                 (gSavedSettings.getString("PresetLooksActive") == name || gSavedSettings.getString("PresetLooksLastApplied") == name);
        if (!LLPresetsManager::getInstance()->deletePreset(subdirectory, name))
        {
            LLSD args;
            args["NAME"] = name;
            LLNotificationsUtil::add("PresetNotDeleted", args);
        }
        else if (look_in_use)
        {
            ALFloaterLightBox::putOnNeutralForDeletedLook(name);
        }
    }
} // namespace

void LLFloaterDeletePrefPreset::onBtnDelete()
{
    LLComboBox* combo = getChild<LLComboBox>("preset_combo");
    std::string name = combo->getSimple();

    // The answers to what is asked below arrive whenever they do, and this
    // floater may be gone by then.
    const LLHandle<LLFloater> handle       = getHandle();
    const std::string         subdirectory = mSubdirectory;

    if (PRESETS_LOOKS == mSubdirectory && LLPresetsManager::isProtectedLookName(name))
    {
        // Neutral is what deleting the Look in use falls back to, so it is
        // not deleted -- but one saved over before it was protected would
        // otherwise be kept as it is for good. Delete offers it back as it
        // came with the viewer.
        LLNotificationsUtil::add("LightBoxLookRestoreNeutral", LLSD(), LLSD(), [handle](const LLSD& notification, const LLSD& response) {
            if (LLNotificationsUtil::getSelectedOption(notification, response) != 0)
            {
                return;
            }
            ALFloaterLightBox::restoreNeutral();
            if (LLFloater* self = handle.get())
            {
                self->closeFloater();
            }
        });
        return;
    }

    if (PRESETS_LOOKS == mSubdirectory && gSavedSettings.getString("PresetLooksActive").empty() &&
        gSavedSettings.getString("PresetLooksLastApplied") == name)
    {
        // The Look in use, with changes on top of it that nothing has saved:
        // Neutral goes on in their place, so ask first.
        LLSD args;
        args["NAME"] = name;
        LLNotificationsUtil::add("LightBoxLookDeleteModified", args, LLSD(),
                                 [handle, subdirectory, name](const LLSD& notification, const LLSD& response) {
                                     if (LLNotificationsUtil::getSelectedOption(notification, response) != 0)
                                     {
                                         return;
                                     }
                                     deletePresetNamed(subdirectory, name);
                                     if (LLFloater* self = handle.get())
                                     {
                                         self->closeFloater();
                                     }
                                 });
        return;
    }

    deletePresetNamed(mSubdirectory, name);
    closeFloater();
}

void LLFloaterDeletePrefPreset::onPresetsListChange()
{
    fillPresetCombo();
}

void LLFloaterDeletePrefPreset::onBtnCancel()
{
    closeFloater();
}
