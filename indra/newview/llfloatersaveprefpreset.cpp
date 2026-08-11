/**
 * @file llfloatersaveprefpreset.cpp
 * @brief Floater to save a graphics preset
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

#include "llfloatersaveprefpreset.h"

#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterpreference.h"
#include "llfloaterreg.h"
#include "llnotificationsutil.h"
#include "llpresetsmanager.h"
#include "lltrans.h"

#include <algorithm>

namespace
{
    void savePresetNamed(const std::string& subdirectory, const std::string& name)
    {
        if (!LLPresetsManager::getInstance()->savePreset(subdirectory, name))
        {
            LLSD args;
            args["NAME"] = name;
            LLNotificationsUtil::add("PresetNotSaved", args);
        }
    }

    bool presetExists(const std::string& subdirectory, const std::string& name)
    {
        std::list<std::string> names;
        LLPresetsManager::getInstance()->loadPresetNamesFromDir(subdirectory, names, DEFAULT_HIDE);

        // Case-insensitively, because the comparison stands in for the
        // filesystem that will do the overwrite, and on Windows and macOS that
        // filesystem does not care about case: saving "sunset" over
        // "Sunset.xml" replaces it. Matching exactly here would let that pair
        // slip past the confirmation. On Linux this warns for a name that
        // would actually coexist, which errs the survivable way round.
        std::string wanted(name);
        LLStringUtil::toLower(wanted);
        return std::any_of(names.begin(), names.end(),
            [&wanted](std::string existing)
            {
                LLStringUtil::toLower(existing);
                return existing == wanted;
            });
    }
}

LLFloaterSavePrefPreset::LLFloaterSavePrefPreset(const LLSD &key)
    : LLFloater(key)
{
}

// virtual
bool LLFloaterSavePrefPreset::postBuild()
{
    LLFloaterPreference* preferences = LLFloaterReg::getTypedInstance<LLFloaterPreference>("preferences");
    if (preferences)
    {
        preferences->addDependentFloater(this);
    }

    getChild<LLComboBox>("preset_combo")->setTextEntryCallback(boost::bind(&LLFloaterSavePrefPreset::onPresetNameEdited, this));
    getChild<LLComboBox>("preset_combo")->setCommitCallback(boost::bind(&LLFloaterSavePrefPreset::onPresetNameEdited, this));
    getChild<LLButton>("save")->setCommitCallback(boost::bind(&LLFloaterSavePrefPreset::onBtnSave, this));

    getChild<LLButton>("cancel")->setCommitCallback(boost::bind(&LLFloaterSavePrefPreset::onBtnCancel, this));

    LLPresetsManager::instance().setPresetListChangeCallback(boost::bind(&LLFloaterSavePrefPreset::onPresetsListChange, this));
    LLPresetsManager::instance().setPresetListChangeLooksCallback(boost::bind(&LLFloaterSavePrefPreset::onPresetsListChange, this));

    mSaveButton = getChild<LLButton>("save");
    mPresetCombo = getChild<LLComboBox>("preset_combo");

    return true;
}

void LLFloaterSavePrefPreset::onPresetNameEdited()
{
    // Disable saving a preset having empty name.
    std::string name = mPresetCombo->getSimple();

    mSaveButton->setEnabled(!name.empty());
}

void LLFloaterSavePrefPreset::onOpen(const LLSD& key)
{
    mSubdirectory = key.asString();

    std::string title_type = std::string("title_") + mSubdirectory;
    if (hasString(title_type))
    {
        setTitle(getString(title_type));
    }

    if (PRESETS_LOOKS == mSubdirectory)
    {
        // A clean name field for Looks: prefilled names invite silent
        // overwrites because the combo's text entry autocompletes typed names
        // to existing items. Overwriting deliberately is what the Save button
        // on the Lightbox bar is for.
        mPresetCombo->removeall();
        mPresetCombo->clear();
        mPresetCombo->setEnabled(true);
    }
    else
    {
        EDefaultOptions option = DEFAULT_HIDE;
        LLPresetsManager::getInstance()->setPresetNamesInComboBox(mSubdirectory, mPresetCombo, option);
    }

    onPresetNameEdited();
}

void LLFloaterSavePrefPreset::onBtnSave()
{
    std::string name = mPresetCombo->getSimple();

    std::string upper_name(name);
    LLStringUtil::toUpper(upper_name);

    if ((name == LLTrans::getString(PRESETS_DEFAULT)) || (upper_name == PRESETS_DEFAULT_UPPER))
    {
        LLNotificationsUtil::add("DefaultPresetNotSaved");
        closeFloater();
        return;
    }

    // Ask before replacing a Look. Every other kind of preset reaches this
    // floater with the existing names already in the combo, so a collision is
    // visible before the click is made; Looks deliberately start from an empty
    // field (see onOpen), which is the thing that made a silent overwrite
    // possible. Refusing outright the way the camera presets do is not an
    // option here either -- the Lightbox's own Save button only ever
    // overwrites the *active* Look, so this is the only route to replacing any
    // other one.
    if (PRESETS_LOOKS == mSubdirectory && presetExists(mSubdirectory, name))
    {
        LLSD args;
        args["NAME"] = name;

        // Everything the save needs is captured by value, and the floater
        // itself by handle: the answer arrives whenever the user gets round to
        // it, and this floater may well be gone by then.
        LLHandle<LLFloater> handle = getHandle();
        const std::string   subdirectory = mSubdirectory;
        LLNotificationsUtil::add("LightBoxLookOverwrite", args, LLSD(),
            [handle, subdirectory, name](const LLSD& notification, const LLSD& response)
            {
                if (LLNotificationsUtil::getSelectedOption(notification, response) != 0)
                {
                    // Cancelled -- leave the floater up so the name can be changed.
                    return;
                }

                savePresetNamed(subdirectory, name);
                if (LLFloater* self = handle.get())
                {
                    self->closeFloater();
                }
            });
        return;
    }

    savePresetNamed(mSubdirectory, name);
    closeFloater();
}

void LLFloaterSavePrefPreset::onPresetsListChange()
{
    if (PRESETS_LOOKS == mSubdirectory)
    {
        // Looks keep a clean name field; don't repopulate over typed text.
        return;
    }
    EDefaultOptions option = DEFAULT_HIDE;
    LLPresetsManager::getInstance()->setPresetNamesInComboBox(mSubdirectory, mPresetCombo, option);
}

void LLFloaterSavePrefPreset::onBtnCancel()
{
    closeFloater();
}
