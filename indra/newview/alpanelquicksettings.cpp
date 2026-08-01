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

#include <set>

#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llslider.h"
#include "llspinctrl.h"

#include "llagent.h"
#include "llenvironment.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventorysettings.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "rlvhandler.h"

namespace
{
    // Sentinel LLSD value (not a UUID) stored in each preset combo for the
    // "no local override, use the region's environment" entry.
    const std::string REGION_DEFAULT_VALUE = "__region_default__";

    // Filters inventory for non-marketplace settings (sky/water/day-cycle) items,
    // deduplicated by asset id.
    class ALQuickSettingsCollector : public LLInventoryCollectFunctor
    {
    public:
        ALQuickSettingsCollector()
        {
            mMarketplaceFolderUUID = gInventory.getMarketplaceListingsUUID();
        }

        bool operator()(LLInventoryCategory* cat, LLInventoryItem* item) override
        {
            if (item && item->getType() == LLAssetType::AT_SETTINGS &&
                !gInventory.isObjectDescendentOf(item->getUUID(), mMarketplaceFolderUUID) &&
                mSeen.find(item->getAssetUUID()) == mSeen.end())
            {
                mSeen.insert(item->getAssetUUID());
                return true;
            }
            return false;
        }

    private:
        LLUUID mMarketplaceFolderUUID;
        std::set<LLUUID> mSeen;
    };
}

static LLPanelInjector<ALPanelQuickSettings> t_quick_settings("quick_settings");

ALPanelQuickSettings::ALPanelQuickSettings()
    : LLPanel(),
    mHoverSlider(nullptr),
    mHoverSpinner(nullptr),
    mSkyPresetCombo(nullptr),
    mWaterPresetCombo(nullptr),
    mDayPresetCombo(nullptr)
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
bool ALPanelQuickSettings::postBuild()
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

    // Environment quick-select
    mSkyPresetCombo = getChild<LLComboBox>("sky_preset_combo");
    mSkyPresetCombo->setCommitCallback(boost::bind(&ALPanelQuickSettings::onSelectSkyPreset, this));

    mWaterPresetCombo = getChild<LLComboBox>("water_preset_combo");
    mWaterPresetCombo->setCommitCallback(boost::bind(&ALPanelQuickSettings::onSelectWaterPreset, this));

    mDayPresetCombo = getChild<LLComboBox>("day_preset_combo");
    mDayPresetCombo->setCommitCallback(boost::bind(&ALPanelQuickSettings::onSelectDayPreset, this));

    getChild<LLButton>("reset_environment_btn")->setCommitCallback(boost::bind(&ALPanelQuickSettings::onResetEnvironment, this));

    loadEnvironmentPresets();

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
    mHoverSlider->setValue(value, false);
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

void ALPanelQuickSettings::loadEnvironmentPresets()
{
    LLInventoryModel::cat_array_t cats;
    LLInventoryModel::item_array_t items;
    ALQuickSettingsCollector collector;
    gInventory.collectDescendentsIf(LLUUID::null, cats, items, LLInventoryModel::EXCLUDE_TRASH, collector);

    std::multimap<std::string, LLUUID> sky_map;
    std::multimap<std::string, LLUUID> water_map;
    std::multimap<std::string, LLUUID> day_map;

    for (const auto& item : items)
    {
        switch (LLSettingsType::fromInventoryFlags(item->getFlags()))
        {
        case LLSettingsType::ST_SKY:
            sky_map.emplace(item->getName(), item->getAssetUUID());
            break;
        case LLSettingsType::ST_WATER:
            water_map.emplace(item->getName(), item->getAssetUUID());
            break;
        case LLSettingsType::ST_DAYCYCLE:
            day_map.emplace(item->getName(), item->getAssetUUID());
            break;
        default:
            break;
        }
    }

    auto fill_combo = [](LLComboBox* combo, const std::multimap<std::string, LLUUID>& preset_map)
    {
        combo->removeall();
        combo->add("Region Default", LLSD(REGION_DEFAULT_VALUE));
        combo->addSeparator();
        for (const auto& [name, asset_id] : preset_map)
        {
            if (!name.empty())
            {
                combo->add(name, LLSD(asset_id));
            }
        }
        combo->setCurrentByIndex(0);
    };

    fill_combo(mSkyPresetCombo, sky_map);
    fill_combo(mWaterPresetCombo, water_map);
    fill_combo(mDayPresetCombo, day_map);
}

void ALPanelQuickSettings::onSelectSkyPreset()
{
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SETENV))
    {
        return;
    }
    LLSD value = mSkyPresetCombo->getSelectedValue();
    if (value.isUUID() && value.asUUID().notNull())
    {
        LLEnvironment::instance().setEnvironment(LLEnvironment::ENV_LOCAL, value.asUUID());
        LLEnvironment::instance().setSelectedEnvironment(LLEnvironment::ENV_LOCAL);
    }
}

void ALPanelQuickSettings::onSelectWaterPreset()
{
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SETENV))
    {
        return;
    }
    LLSD value = mWaterPresetCombo->getSelectedValue();
    if (value.isUUID() && value.asUUID().notNull())
    {
        LLEnvironment::instance().setEnvironment(LLEnvironment::ENV_LOCAL, value.asUUID());
        LLEnvironment::instance().setSelectedEnvironment(LLEnvironment::ENV_LOCAL);
    }
}

void ALPanelQuickSettings::onSelectDayPreset()
{
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SETENV))
    {
        return;
    }
    LLSD value = mDayPresetCombo->getSelectedValue();
    if (value.isUUID() && value.asUUID().notNull())
    {
        LLEnvironment::instance().setEnvironment(LLEnvironment::ENV_LOCAL, value.asUUID());
        LLEnvironment::instance().setSelectedEnvironment(LLEnvironment::ENV_LOCAL);
    }
}

void ALPanelQuickSettings::onResetEnvironment()
{
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SETENV))
    {
        return;
    }
    mSkyPresetCombo->setValue(LLSD(REGION_DEFAULT_VALUE));
    mWaterPresetCombo->setValue(LLSD(REGION_DEFAULT_VALUE));
    mDayPresetCombo->setValue(LLSD(REGION_DEFAULT_VALUE));
    LLEnvironment::instance().setSharedEnvironment();
}
