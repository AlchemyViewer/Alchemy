/**
 * @file alfloatersceneexplorerfilters.cpp
 * @brief Companion filters floater for the Scene Explorer
 *
 * Copyright (c) 2026, Rye Mutt <rye@alchemyviewer.org>
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "llviewerprecompiledheaders.h"

#include "alfloatersceneexplorerfilters.h"

#include "llcheckboxctrl.h"
#include "llfloaterreg.h"
#include "llradiogroup.h"
#include "llsliderctrl.h"
#include "llspinctrl.h"

#include "alfloatersceneexplorer.h"
#include "alobjectproperties.h"
#include "alsceneexplorermodel.h"
#include "llviewercontrol.h"

namespace
{
    // Checkbox name -> EFlag bit (mask semantics: all checked bits required).
    constexpr std::pair<const char*, U32> FLAG_CHECKS[] = {
        { "check_scripted",   ALObjectProperties::FLAG_SCRIPTED },
        { "check_physics",    ALObjectProperties::FLAG_PHYSICS },
        { "check_phantom",    ALObjectProperties::FLAG_PHANTOM },
        { "check_temporary",  ALObjectProperties::FLAG_TEMPORARY },
        { "check_flexible",   ALObjectProperties::FLAG_FLEXIBLE },
        { "check_light",      ALObjectProperties::FLAG_LIGHT },
        { "check_spotlight",  ALObjectProperties::FLAG_SPOTLIGHT },
        { "check_projector",  ALObjectProperties::FLAG_PROJECTOR },
        { "check_glow",       ALObjectProperties::FLAG_GLOW },
        { "check_fullbright", ALObjectProperties::FLAG_FULLBRIGHT },
        { "check_media",      ALObjectProperties::FLAG_MEDIA },
        { "check_particles",  ALObjectProperties::FLAG_PARTICLES },
        { "check_probe",      ALObjectProperties::FLAG_REFLECTION_PROBE },
        { "check_animated",   ALObjectProperties::FLAG_ANIMATED },
        { "check_pbr",        ALObjectProperties::FLAG_PBR_MATERIAL },
        { "check_alpha",      ALObjectProperties::FLAG_ALPHA },
        { "check_attachment", ALObjectProperties::FLAG_ATTACHMENT },
        { "check_for_sale",   ALObjectProperties::FLAG_FOR_SALE },
        { "check_payable",    ALObjectProperties::FLAG_PAYABLE },
    };

    constexpr std::pair<const char*, U32> TYPE_CHECKS[] = {
        { "check_type_prim",   1u << ALObjectProperties::GEOM_PRIM },
        { "check_type_mesh",   1u << ALObjectProperties::GEOM_MESH },
        { "check_type_sculpt", 1u << ALObjectProperties::GEOM_SCULPT },
    };
}

ALFloaterSceneExplorerFilters::ALFloaterSceneExplorerFilters(const LLSD& key)
:   LLFloater(key)
{
}

bool ALFloaterSceneExplorerFilters::postBuild()
{
    for (const auto& [name, bit] : FLAG_CHECKS)
    {
        getChild<LLUICtrl>(name)->setCommitCallback(
            boost::bind(&ALFloaterSceneExplorerFilters::onCommitAny, this));
    }
    for (const auto& [name, bit] : TYPE_CHECKS)
    {
        getChild<LLUICtrl>(name)->setCommitCallback(
            boost::bind(&ALFloaterSceneExplorerFilters::onCommitAny, this));
    }
    getChild<LLUICtrl>("scope_radio")->setCommitCallback(
        boost::bind(&ALFloaterSceneExplorerFilters::onCommitAny, this));
    getChild<LLUICtrl>("radius_slider")->setCommitCallback(
        boost::bind(&ALFloaterSceneExplorerFilters::onCommitAny, this));
    getChild<LLUICtrl>("min_li_spinner")->setCommitCallback(
        boost::bind(&ALFloaterSceneExplorerFilters::onCommitAny, this));
    getChild<LLUICtrl>("min_tris_spinner")->setCommitCallback(
        boost::bind(&ALFloaterSceneExplorerFilters::onCommitAny, this));
    getChild<LLUICtrl>("reset_btn")->setCommitCallback(
        boost::bind(&ALFloaterSceneExplorerFilters::onClickReset, this));

    refreshFromSettings();
    return true;
}

void ALFloaterSceneExplorerFilters::onOpen(const LLSD& key)
{
    // The quick bar may have edited the shared settings since we were last
    // shown.
    refreshFromSettings();
}

void ALFloaterSceneExplorerFilters::refreshFromSettings()
{
    const U32 flags = gSavedSettings.getU32("ALSceneExplorerFlagFilter");
    for (const auto& [name, bit] : FLAG_CHECKS)
    {
        getChild<LLUICtrl>(name)->setValue((flags & bit) != 0);
    }
    const U32 types = gSavedSettings.getU32("ALSceneExplorerTypeFilter");
    for (const auto& [name, bit] : TYPE_CHECKS)
    {
        getChild<LLUICtrl>(name)->setValue((types & bit) != 0);
    }
    getChild<LLRadioGroup>("scope_radio")->setSelectedIndex(
        (S32)gSavedSettings.getU32("ALSceneExplorerScope"));
    getChild<LLUICtrl>("radius_slider")->setValue(gSavedSettings.getF32("ALSceneExplorerRadius"));
    getChild<LLUICtrl>("min_li_spinner")->setValue(gSavedSettings.getF32("ALSceneExplorerMinLandImpact"));
    getChild<LLUICtrl>("min_tris_spinner")->setValue(
        (S32)gSavedSettings.getU32("ALSceneExplorerMinTriangles"));
}

void ALFloaterSceneExplorerFilters::onCommitAny()
{
    U32 flags = 0;
    for (const auto& [name, bit] : FLAG_CHECKS)
    {
        if (getChild<LLUICtrl>(name)->getValue().asBoolean())
            flags |= bit;
    }
    U32 types = 0;
    for (const auto& [name, bit] : TYPE_CHECKS)
    {
        if (getChild<LLUICtrl>(name)->getValue().asBoolean())
            types |= bit;
    }
    gSavedSettings.setU32("ALSceneExplorerFlagFilter", flags);
    gSavedSettings.setU32("ALSceneExplorerTypeFilter", types);
    gSavedSettings.setU32("ALSceneExplorerScope",
                          (U32)llmax(0, getChild<LLRadioGroup>("scope_radio")->getSelectedIndex()));
    gSavedSettings.setF32("ALSceneExplorerRadius",
                          (F32)getChild<LLUICtrl>("radius_slider")->getValue().asReal());
    gSavedSettings.setF32("ALSceneExplorerMinLandImpact",
                          (F32)getChild<LLUICtrl>("min_li_spinner")->getValue().asReal());
    gSavedSettings.setU32("ALSceneExplorerMinTriangles",
                          (U32)llmax(0, getChild<LLUICtrl>("min_tris_spinner")->getValue().asInteger()));

    // Settings are shared state; the explorer re-reads them into its quick
    // bar and filter object.
    if (ALFloaterSceneExplorer* explorer =
            LLFloaterReg::findTypedInstance<ALFloaterSceneExplorer>("scene_explorer"))
    {
        explorer->refreshFilters();
    }
}

void ALFloaterSceneExplorerFilters::onClickReset()
{
    gSavedSettings.setU32("ALSceneExplorerFlagFilter", 0);
    gSavedSettings.setU32("ALSceneExplorerTypeFilter", 0);
    gSavedSettings.setU32("ALSceneExplorerScope", (U32)ALSceneExplorerFilter::SCOPE_REGION);
    gSavedSettings.setF32("ALSceneExplorerMinLandImpact", 0.f);
    gSavedSettings.setU32("ALSceneExplorerMinTriangles", 0);
    refreshFromSettings();
    if (ALFloaterSceneExplorer* explorer =
            LLFloaterReg::findTypedInstance<ALFloaterSceneExplorer>("scene_explorer"))
    {
        explorer->refreshFilters();
    }
}
