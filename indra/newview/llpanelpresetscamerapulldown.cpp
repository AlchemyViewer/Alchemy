/**
 * @file llpanelpresetscamerapulldown.cpp
 * @brief A panel showing a quick way to pick camera presets
 *
 * $LicenseInfo:firstyear=2017&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2017, Linden Research, Inc.
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

#include "llpanelpresetscamerapulldown.h"

#include "llviewercontrol.h"
#include "llstatusbar.h"

#include "llagentcamera.h"
#include "llbutton.h"
#include "lltabcontainer.h"
#include "llfloatercamera.h"
#include "llfloaterreg.h"
#include "llfloaterpreference.h"
#include "llpresetsmanager.h"
#include "llsliderctrl.h"
#include "llscrolllistctrl.h"
#include "llspinctrl.h"
#include "lltrans.h"

///----------------------------------------------------------------------------
/// Class LLPanelPresetsCameraPulldown
///----------------------------------------------------------------------------

// Default constructor
LLPanelPresetsCameraPulldown::LLPanelPresetsCameraPulldown()
{
    mCommitCallbackRegistrar.add("Presets.toggleCameraFloater", boost::bind(&LLPanelPresetsCameraPulldown::onViewButtonClick, this, _2));
    mCommitCallbackRegistrar.add("CameraPresets.ChangeView", boost::bind(&LLFloaterCamera::onClickCameraItem, _2));
    mCommitCallbackRegistrar.add("CameraPresets.RowClick", boost::bind(&LLPanelPresetsCameraPulldown::onRowClick, this, _2));
    mCommitCallbackRegistrar.add("CameraPresets.Save", boost::bind(&LLFloaterCamera::onSavePreset));
    mCommitCallbackRegistrar.add("CommitSettings", boost::bind(&LLPanelPresetsCameraPulldown::onCommitSettings, this));


    buildFromFile( "panel_presets_camera_pulldown.xml");
}

bool LLPanelPresetsCameraPulldown::postBuild()
{
    LLPresetsManager* presetsMgr = LLPresetsManager::getInstance();
    if (presetsMgr)
    {
        // Make sure there is a default preference file
        presetsMgr->createMissingDefault(PRESETS_CAMERA);

        presetsMgr->setPresetListChangeCameraCallback(boost::bind(&LLPanelPresetsCameraPulldown::populatePanel, this));
    }

    populatePanel();

    return LLPanelPulldown::postBuild();
}

void LLPanelPresetsCameraPulldown::populatePanel()
{
    LLPresetsManager::getInstance()->loadPresetNamesFromDir(PRESETS_CAMERA, mPresetNames, DEFAULT_BOTTOM);

    LLScrollListCtrl* scroll = getChild<LLScrollListCtrl>("preset_camera_list");

    if (scroll && mPresetNames.begin() != mPresetNames.end())
    {
        scroll->clearRows();

        std::string active_preset = gSavedSettings.getString("PresetCameraActive");
        if (active_preset == PRESETS_DEFAULT)
        {
            active_preset = LLTrans::getString(PRESETS_DEFAULT);
        }

        for (std::list<std::string>::const_iterator it = mPresetNames.begin(); it != mPresetNames.end(); ++it)
        {
            const std::string& name = *it;
            LL_DEBUGS() << "adding '" << name << "'" << LL_ENDL;

            LLSD row;
            row["columns"][0]["column"] = "preset_name";
            row["columns"][0]["value"] = name;

            bool is_selected_preset = false;
            if (name == active_preset)
            {
                row["columns"][1]["column"] = "icon";
                row["columns"][1]["type"] = "icon";
                row["columns"][1]["value"] = "Check_Mark";

                is_selected_preset = true;
            }

            LLScrollListItem* new_item = scroll->addElement(row);
            new_item->setSelected(is_selected_preset);
        }
    }
}

void LLPanelPresetsCameraPulldown::onRowClick(const LLSD& user_data)
{
    LLScrollListCtrl* scroll = getChild<LLScrollListCtrl>("preset_camera_list");

    if (scroll)
    {
        LLScrollListItem* item = scroll->getFirstSelected();
        if (item)
        {
            std::string name = item->getColumn(1)->getValue().asString();

            LL_DEBUGS() << "selected '" << name << "'" << LL_ENDL;
            LLFloaterCamera::switchToPreset(name);

            // Scroll grabbed focus, drop it to prevent selection of parent menu
            setFocus(false);

            setVisible(false);
        }
        else
        {
            LL_DEBUGS() << "none selected" << LL_ENDL;
        }
    }
    else
    {
        LL_DEBUGS() << "no scroll" << LL_ENDL;
    }
}

void LLPanelPresetsCameraPulldown::onViewButtonClick(const LLSD& user_data)
{
    // close the minicontrol, we're bringing up the big one
    setVisible(false);

    LLFloaterReg::toggleInstanceOrBringToFront("camera");
}

void LLPanelPresetsCameraPulldown::updateCameraControl(const LLVector3& vector)
{
    getChild<LLSpinCtrl>("camera_x")->setValue(vector[VX]);
    getChild<LLSpinCtrl>("camera_y")->setValue(vector[VY]);
    getChild<LLSpinCtrl>("camera_z")->setValue(vector[VZ]);
}

void LLPanelPresetsCameraPulldown::updateFocusControl(const LLVector3d& vector3d)
{
    getChild<LLSpinCtrl>("focus_x")->setValue(vector3d[VX]);
    getChild<LLSpinCtrl>("focus_y")->setValue(vector3d[VY]);
    getChild<LLSpinCtrl>("focus_z")->setValue(vector3d[VZ]);
}

void LLPanelPresetsCameraPulldown::draw()
{
    updateCameraControl(gAgentCamera.getCameraOffsetInitial());
    updateFocusControl(gAgentCamera.getFocusOffsetInitial());

    LLPanel::draw();
}

void LLPanelPresetsCameraPulldown::onCommitSettings()
{
    LLVector3  vector;
    LLVector3d vector3d;

    vector.mV[VX] = (F32)getChild<LLUICtrl>("camera_x")->getValue().asReal();
    vector.mV[VY] = (F32)getChild<LLUICtrl>("camera_y")->getValue().asReal();
    vector.mV[VZ] = (F32)getChild<LLUICtrl>("camera_z")->getValue().asReal();
    gSavedSettings.setVector3("CameraOffsetRearView", vector);

    vector3d.mdV[VX] = (F32)getChild<LLUICtrl>("focus_x")->getValue().asReal();
    vector3d.mdV[VY] = (F32)getChild<LLUICtrl>("focus_y")->getValue().asReal();
    vector3d.mdV[VZ] = (F32)getChild<LLUICtrl>("focus_z")->getValue().asReal();
    gSavedSettings.setVector3d("FocusOffsetRearView", vector3d);
}
