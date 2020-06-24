/**
 * @file alpanelquicksettings.cpp
 * @brief Base panel for quick settings popdown and floater
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2013-2014, Alchemy Viewer Project.
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
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "alpanelquicksettings.h"

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llslider.h"
#include "llspinctrl.h"

#include "llagent.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"

static LLPanelInjector<ALPanelQuickSettings> t_quick_settings("quick_settings");

ALPanelQuickSettings::ALPanelQuickSettings()
	: LLPanel(),
	mRegionSettingsCheckBox(nullptr),
	mHoverSlider(nullptr),
	mHoverSpinner(nullptr)
{
}

ALPanelQuickSettings::~ALPanelQuickSettings()
{
	if (mRegionChangedSlot.connected())
	{
		mRegionChangedSlot.disconnect();
	}
}

// virtual
BOOL ALPanelQuickSettings::postBuild()
{
	refresh();

	// Hover height
	mHoverSlider = getChild<LLSlider>("hover_slider_bar");
	mHoverSlider->setMinValue(MIN_HOVER_Z);
	mHoverSlider->setMaxValue(MAX_HOVER_Z);
	mHoverSlider->setMouseUpCallback(boost::bind(&ALPanelQuickSettings::onHoverSliderFinalCommit, this));
	mHoverSlider->setCommitCallback(boost::bind(&ALPanelQuickSettings::onHoverSliderMoved, this, _2));

	mHoverSpinner = getChild<LLSpinCtrl>("hover_spinner");
	mHoverSpinner->setMinValue(MIN_HOVER_Z);
	mHoverSpinner->setMaxValue(MAX_HOVER_Z);

	// Initialize slider from pref setting.
	syncFromPreferenceSetting();

	// Update slider on future pref changes.
	gSavedPerAccountSettings.getControl("AvatarHoverOffsetZ")->getCommitSignal()->connect(boost::bind(&ALPanelQuickSettings::syncFromPreferenceSetting, this));

	updateEditHoverEnabled();

	if (!mRegionChangedSlot.connected())
	{
		mRegionChangedSlot = gAgent.addRegionChangedCallback(boost::bind(&ALPanelQuickSettings::onRegionChanged, this));
	}
	// Set up based on initial region.
	onRegionChanged();

	return LLPanel::postBuild();
}

// virtual
void ALPanelQuickSettings::refresh()
{
    LLPanel::refresh();
}

void ALPanelQuickSettings::syncFromPreferenceSetting()
{
	F32 value = gSavedPerAccountSettings.getF32("AvatarHoverOffsetZ");
	mHoverSlider->setValue(value, FALSE);
	mHoverSpinner->setValue(value);

	if (isAgentAvatarValid())
	{
		LLVector3 offset(0.0, 0.0, llclamp(value, MIN_HOVER_Z, MAX_HOVER_Z));
		LL_INFOS("Avatar") << "setting hover from preference setting " << offset[2] << LL_ENDL;
		gAgentAvatarp->setHoverOffset(offset);
	}
}

void ALPanelQuickSettings::onHoverSliderMoved(const LLSD& val)
{
	if (isAgentAvatarValid())
	{
		auto value = static_cast<F32>(val.asReal());
		LLVector3 offset(0.0, 0.0, llclamp(value, MIN_HOVER_Z, MAX_HOVER_Z));
		LL_INFOS("Avatar") << "setting hover from slider moved" << offset[2] << LL_ENDL;
		gAgentAvatarp->setHoverOffset(offset, false);
	}
}

// Do send-to-the-server work when slider drag completes, or new
// value entered as text.
void ALPanelQuickSettings::onHoverSliderFinalCommit()
{
	F32 value = mHoverSlider->getValueF32();
	gSavedPerAccountSettings.setF32("AvatarHoverOffsetZ", value);
	if (isAgentAvatarValid())
	{
		LLVector3 offset(0.0, 0.0, llclamp(value, MIN_HOVER_Z, MAX_HOVER_Z));
		LL_INFOS("Avatar") << "setting hover from slider final commit " << offset[2] << LL_ENDL;
		gAgentAvatarp->setHoverOffset(offset, true); // will send update this time.
	}
}

void ALPanelQuickSettings::onRegionChanged()
{
	LLViewerRegion *region = gAgent.getRegion();
	if (region && region->simulatorFeaturesReceived())
	{
		updateEditHoverEnabled();
	}
	else if (region)
	{
		region->setSimulatorFeaturesReceivedCallback(boost::bind(&ALPanelQuickSettings::onSimulatorFeaturesReceived, this, _1));
	}
}

void ALPanelQuickSettings::onSimulatorFeaturesReceived(const LLUUID &region_id)
{
	LLViewerRegion *region = gAgent.getRegion();
	if (region && (region->getRegionID() == region_id))
	{
		updateEditHoverEnabled();
	}
}

void ALPanelQuickSettings::updateEditHoverEnabled()
{
	bool enabled = gAgent.getRegion() && gAgent.getRegion()->avatarHoverHeightEnabled();
	mHoverSlider->setEnabled(enabled);
	mHoverSpinner->setEnabled(enabled);
	if (enabled)
	{
		syncFromPreferenceSetting();
	}
}
