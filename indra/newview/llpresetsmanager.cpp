/**
 * @file llpresetsmanager.cpp
 * @brief Implementation for the LLPresetsManager class.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

#include "llpresetsmanager.h"

#include "lldiriterator.h"
#include "llfloater.h"
#include "llsdserialize.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llfloaterpreference.h"
#include "llfloaterreg.h"
#include "llfeaturemanager.h"
#include "llagentcamera.h"
#include "llfile.h"

LLPresetsManager::LLPresetsManager()
{
    copyDefaultLooks();

    // Connect preset signals
    startWatching(PRESETS_GRAPHIC);
    startWatching(PRESETS_CAMERA);
    startWatching(PRESETS_LOOKS);
}

LLPresetsManager::~LLPresetsManager()
{
    for (auto& signal : mGraphicsChangedSignals)
    {
        signal.disconnect();
    }
    mGraphicsChangedSignals.clear();


    for (auto& signal : mCameraChangedSignals)
    {
        signal.disconnect();
    }
    mCameraChangedSignals.clear();

    for (auto& signal : mLooksChangedSignals)
    {
        signal.disconnect();
    }
    mLooksChangedSignals.clear();
}

void LLPresetsManager::triggerChangeCameraSignal()
{
    mPresetListChangeCameraSignal();
}

void LLPresetsManager::triggerChangeSignal()
{
    mPresetListChangeSignal();
}

void LLPresetsManager::triggerChangeLooksSignal()
{
    mPresetListChangeLooksSignal();
}

void LLPresetsManager::createMissingDefault(const std::string& subdirectory)
{

    if (PRESETS_CAMERA == subdirectory)
    {
        createCameraDefaultPresets();
        return;
    }

    std::string default_file = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, PRESETS_DIR,
        subdirectory, PRESETS_DEFAULT + ".xml");
    if (!gDirUtilp->fileExists(default_file))
    {
        LL_INFOS() << "No default preset found -- creating one at " << default_file << LL_ENDL;

        // Write current settings as the default
        savePreset(subdirectory, PRESETS_DEFAULT, true);
    }
    else
    {
        LL_DEBUGS() << "default preset exists; no-op" << LL_ENDL;
    }
}

void LLPresetsManager::createCameraDefaultPresets()
{
    bool is_default_created = createDefaultCameraPreset(PRESETS_REAR_VIEW);
    is_default_created |= createDefaultCameraPreset(PRESETS_FRONT_VIEW);
    is_default_created |= createDefaultCameraPreset(PRESETS_SIDE_VIEW);

    if (is_default_created)
    {
        triggerChangeCameraSignal();
    }
}

void LLPresetsManager::copyDefaultLooks()
{
    // Seed each bundled Look once ever, and remember which ones.
    //
    // This used to skip the whole step as soon as the user's directory held any
    // .xml at all. That kept the property that matters -- a Look you delete
    // stays deleted -- but it also meant a Look bundled in a later release
    // could never reach anybody who had ever saved one of their own, which is
    // to say anybody who uses the feature.
    //
    // Recording the names instead keeps both. Nothing is ever copied over a
    // file that already exists, so an edited starter Look is safe too.
    const std::string user_dir = getPresetsDir(PRESETS_LOOKS);
    const std::string app_dir  = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, PRESETS_LOOKS);
    const std::string record   = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, SEEDED_LOOKS_FILE);

    // Deliberately not inside the looks directory: anything named *.xml in
    // there is enumerated as a Look.
    LLSD seeded;
    const bool have_record = LLFile::isfile(record);
    if (have_record)
    {
        llifstream in(record);
        if (in.is_open())
        {
            LLSDSerialize::fromXML(seeded, in);
        }
    }
    if (!seeded.isMap())
    {
        seeded = LLSD::emptyMap();
    }

    // Upgrading with Looks already present means this user has been through the
    // old first-run seeding. Adopt every bundled name as already done rather
    // than copying: whatever they deleted back then stays deleted, and only
    // names bundled after this point will ever seed for them.
    bool adopt_only = false;
    if (!have_record)
    {
        LLDirIterator user_iter(user_dir, "*.xml");
        std::string   existing;
        adopt_only = user_iter.next(existing);
    }

    bool        changed = false;
    std::string file;
    LLDirIterator app_iter(app_dir, "*.xml");
    while (app_iter.next(file))
    {
        if (seeded.has(file))
        {
            continue;
        }

        if (!adopt_only && !LLFile::isfile(gDirUtilp->add(user_dir, file)))
        {
            LL_INFOS("Presets") << "Seeding bundled Look '" << file << "'" << LL_ENDL;
            if (!LLFile::copy(gDirUtilp->add(app_dir, file), gDirUtilp->add(user_dir, file)))
            {
                // Recording a copy that did not happen would burn the name
                // forever -- seeding is once-per-name by design, so there
                // would be no second chance. Leave it unrecorded and the next
                // startup simply tries again.
                LL_WARNS("Presets") << "Could not seed bundled Look '" << file
                                    << "'; leaving it for the next run" << LL_ENDL;
                continue;
            }
        }

        seeded.insert(file, true);
        changed = true;
    }

    if (changed)
    {
        llofstream out(record.c_str());
        if (out.is_open())
        {
            LLPointer<LLSDFormatter> formatter = new LLSDXMLFormatter();
            formatter->format(seeded, out, LLSDFormatter::OPTIONS_PRETTY);
            out.close();
        }
        else
        {
            // Not fatal, but say so: without the record the next startup will
            // re-adopt rather than re-seed, so a newly bundled Look is missed
            // rather than duplicated.
            LL_WARNS("Presets") << "Could not write the seeded-Looks record at " << record << LL_ENDL;
        }
    }
}

void LLPresetsManager::startWatching(const std::string& subdirectory)
{
    if (PRESETS_CAMERA == subdirectory)
    {
        std::vector<std::string> name_list;
        getCameraControlNames(name_list);

        for (std::vector<std::string>::iterator it = name_list.begin(); it != name_list.end(); ++it)
        {
            std::string ctrl_name = *it;
            if (gSavedSettings.controlExists(ctrl_name))
            {
                LLPointer<LLControlVariable> cntrl_ptr = gSavedSettings.getControl(ctrl_name);
                if (cntrl_ptr.isNull())
                {
                    LL_WARNS("Init") << "Unable to set signal on global setting '" << ctrl_name
                        << "'" << LL_ENDL;
                }
                else
                {
                    mCameraChangedSignals.push_back(cntrl_ptr->getCommitSignal()->connect(boost::bind(&LLPresetsManager::cameraSettingChanged, this)));
                }
            }
        }
    }
    else if (PRESETS_GRAPHIC == subdirectory)
    {
        std::vector<std::string> name_list;
        getGraphicsControlNames(name_list);

        for (std::vector<std::string>::iterator it = name_list.begin(); it != name_list.end(); ++it)
        {
            std::string ctrl_name = *it;
            if (gSavedSettings.controlExists(ctrl_name))
            {
                LLPointer<LLControlVariable> cntrl_ptr = gSavedSettings.getControl(ctrl_name);
                if (cntrl_ptr.isNull())
                {
                    LL_WARNS("Init") << "Unable to set signal on global setting '" << ctrl_name
                        << "'" << LL_ENDL;
                }
                else
                {
                    mGraphicsChangedSignals.push_back(cntrl_ptr->getCommitSignal()->connect(boost::bind(&LLPresetsManager::graphicsSettingChanged, this)));
                }
            }
            else if (gSavedPerAccountSettings.controlExists(ctrl_name))
            {
                LLPointer<LLControlVariable> cntrl_ptr = gSavedPerAccountSettings.getControl(ctrl_name);
                if (cntrl_ptr.isNull())
                {
                    LL_WARNS("Init") << "Unable to set signal on global setting '" << ctrl_name
                        << "'" << LL_ENDL;
                }
                else
                {
                    mGraphicsChangedSignals.push_back(cntrl_ptr->getCommitSignal()->connect(boost::bind(&LLPresetsManager::graphicsSettingChanged, this)));
                }
            }
        }
    }
    else if (PRESETS_LOOKS == subdirectory)
    {
        std::vector<std::string> name_list;
        getLooksControlNames(name_list);

        for (const std::string& ctrl_name : name_list)
        {
            if (gSavedSettings.controlExists(ctrl_name))
            {
                LLPointer<LLControlVariable> cntrl_ptr = gSavedSettings.getControl(ctrl_name);
                if (cntrl_ptr.isNull())
                {
                    // Cannot happen while controlExists and getControl agree;
                    // guarded anyway, exactly as the camera and graphics
                    // branches above guard it.
                    LL_WARNS("Presets") << "Unable to set signal on Looks control '" << ctrl_name << "'" << LL_ENDL;
                }
                else
                {
                    mLooksChangedSignals.push_back(cntrl_ptr->getCommitSignal()->connect(boost::bind(&LLPresetsManager::looksSettingChanged, this)));
                }
            }
            else
            {
                // Loud on purpose: a renamed setting must not silently fall
                // out of the Looks whitelist.
                LL_WARNS("Presets") << "Looks control does not exist: '" << ctrl_name << "'" << LL_ENDL;
            }
        }
    }
}

std::string LLPresetsManager::getPresetsDir(const std::string& subdirectory)
{
    std::string presets_path = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, PRESETS_DIR);

    LLFile::mkdir(presets_path);

    presets_path = gDirUtilp->add(presets_path, subdirectory);
    if (!gDirUtilp->fileExists(presets_path))
        LLFile::mkdir(presets_path);

    return presets_path;
}

void LLPresetsManager::loadPresetNamesFromDir(const std::string& subdirectory, preset_name_list_t& presets, EDefaultOptions default_option)
{
    bool IS_CAMERA = (PRESETS_CAMERA == subdirectory);
    bool IS_GRAPHIC = (PRESETS_GRAPHIC == subdirectory);
    bool IS_LOOKS = (PRESETS_LOOKS == subdirectory);

    std::string dir = LLPresetsManager::getInstance()->getPresetsDir(subdirectory);
    LL_INFOS("AppInit") << "Loading list of preset names from " << dir << LL_ENDL;

    mPresetNames.clear();

    LLDirIterator dir_iter(dir, "*.xml");
    bool found = true;
    while (found)
    {
        std::string file;
        found = dir_iter.next(file);

        if (found)
        {
            std::string path = gDirUtilp->add(dir, file);
            std::string name = LLURI::unescape(gDirUtilp->getBaseFileName(path, /*strip_exten = */ true));
            LL_DEBUGS() << "  Found preset '" << name << "'" << LL_ENDL;

            if (IS_CAMERA)
            {
                if (isTemplateCameraPreset(name))
                {
                    continue;
                }
                if ((default_option == DEFAULT_HIDE) || (default_option == DEFAULT_BOTTOM))
                {
                    if (isDefaultCameraPreset(name))
                    {
                        continue;
                    }
                }
                mPresetNames.push_back(name);
            }
            if (IS_GRAPHIC)
            {
                if (PRESETS_DEFAULT != name)
                {
                    mPresetNames.push_back(name);
                }
                else
                {
                    switch (default_option)
                    {
                    case DEFAULT_SHOW:
                        mPresetNames.push_back(LLTrans::getString(PRESETS_DEFAULT));
                        break;

                    case DEFAULT_TOP:
                        mPresetNames.push_front(LLTrans::getString(PRESETS_DEFAULT));
                        break;

                    case DEFAULT_HIDE:
                    default:
                        break;
                    }
                }
            }
            if (IS_LOOKS)
            {
                mPresetNames.push_back(name);
            }
        }
    }

    if (IS_LOOKS)
    {
        mPresetNames.sort(LLStringUtil::precedesDict);
    }

    if (IS_CAMERA)
    {
        mPresetNames.sort(LLStringUtil::precedesDict);
        if (default_option == DEFAULT_BOTTOM)
        {
            mPresetNames.push_back(PRESETS_FRONT_VIEW);
            mPresetNames.push_back(PRESETS_REAR_VIEW);
            mPresetNames.push_back(PRESETS_SIDE_VIEW);
        }
    }

    presets = mPresetNames;
}

void LLPresetsManager::graphicsSettingChanged()
{
    static LLCachedControl<std::string> graphic_preset_active(gSavedSettings, "PresetGraphicActive", "");
    if (!graphic_preset_active().empty() && !mIgnoreChangedSignal)
    {
        gSavedSettings.setString("PresetGraphicActive", "");

        triggerChangeSignal();
    }
}

void LLPresetsManager::looksSettingChanged()
{
    static LLCachedControl<std::string> looks_preset_active(gSavedSettings, "PresetLooksActive", "");
    if (!looks_preset_active().empty() && !mIgnoreChangedSignal)
    {
        gSavedSettings.setString("PresetLooksActive", "");

        triggerChangeLooksSignal();
    }
}

void LLPresetsManager::cameraSettingChanged()
{
    static LLCachedControl<std::string> preset_camera_active(gSavedSettings, "PresetCameraActive", "");
    if (!preset_camera_active().empty() && !mIgnoreChangedSignal)
    {
        gSavedSettings.setString("PresetCameraActive", "");

        triggerChangeCameraSignal();
    }
}

void LLPresetsManager::getGraphicsControlNames(std::vector<std::string>& names)
{
    const std::vector<std::string> camera_controls = {
        // From panel_preferences_graphics.xml
        "RenderAnisotropicLevel",
        "RenderAvatarLODFactor",
        "RenderAvatarMaxComplexity",
        "RenderAvatarMaxNonImpostors",
        "RenderAvatarPhysicsLODFactor",
        "RenderDeferredSSAO",
        "RenderDepthOfField",
        "RenderFSAASamples",
        "RenderFSAAType",
        "RenderFarClip",
        "RenderFlexTimeFactor",
        "RenderGlowResolutionPow",
        "RenderLocalLightCount",
        "RenderMaxPartCount",
        "RenderQualityPerformance",
        "RenderShadowDetail",
        "RenderTerrainLODFactor",
        "RenderExposure",
        "AlchemyRenderTonemapType",
        "RenderTransparentWater",
        "RenderTreeLODFactor",
        "RenderVolumeLODFactor",
        "RenderScreenSpaceReflections",
        "RenderReflectionProbeDetail",
        "RenderReflectionProbeLevel"
        "RenderCASSharpness",
    };
    names = camera_controls;
}

void LLPresetsManager::getCameraControlNames(std::vector<std::string>& names)
{
    const std::vector<std::string> camera_controls = {
        // From panel_preferences_move.xml
        "CameraAngle",
        "CameraOffsetScale",
        // From llagentcamera.cpp
        "CameraOffsetBuild",
        "TrackFocusObject",
        "CameraOffsetRearView",
        "FocusOffsetRearView",
        "AvatarSitRotation",
        "ZoomTime",
        "CameraPositionSmoothing"
    };
    names = camera_controls;
}

void LLPresetsManager::getLooksControlNames(std::vector<std::string>& names)
{
    // The single source of truth for what a Look carries. Aesthetic-only by
    // design: scene/performance/quality keys belong to graphics presets, and
    // session-scoped (Persist=0) or buffer-shape keys would create phantom
    // dirty state. Any new aesthetic setting exposed in panel_lightbox_look
    // or panel_lightbox_lens MUST be added here; Scene-tab keys must not be.
    // Applying a Look writes only keys on this list, so a shared Look file
    // cannot smuggle unrelated settings.
    //
    // Static: the list never changes, and callers include per-save and
    // per-apply paths, so build the ~110 strings once rather than every call.
    static const std::vector<std::string> looks_controls = {
        // Exposure & tone
        "RenderExposure",
        "AlchemyRenderTonemapType",
        "RenderTonemapMix",
        "RenderTonemapACESWhite",
        "RenderTonemapReinhardWhite",
        "RenderTonemapFilmicWhite",
        "RenderTonemapAgxContrast",
        "RenderTonemapAgxWhite",
        "RenderDynamicExposureEnabled",
        "RenderDynamicExposureCoefficient",
        "RenderDynamicExposureSpeedError",
        "RenderDynamicExposureSpeedTarget",
        "RenderUseExposureSkySettings",
        // Color LUT
        "RenderColorGrade",
        "RenderColorGradeLUT",
        "RenderColorGradeLUTStrength",
        // Basic grade, white balance, curves
        "RenderColorGradeBrightness",
        "RenderColorGradeContrast",
        "RenderColorGradeSaturation",
        "RenderColorGradeVibrance",
        "RenderColorGradeHighlights",
        "RenderColorGradeShadows",
        "RenderColorGradeHueShift",
        "RenderColorGradeBlackPoint",
        "RenderColorGradeWhitePoint",
        "RenderColorGradeWhiteBalanceCCT",
        "RenderColorGradeWhiteBalanceDuv",
        "RenderColorGradeLift",
        "RenderColorGradeGamma",
        "RenderColorGradeGain",
        "RenderColorGradeCurveToe",
        "RenderColorGradeCurveShoulder",
        "RenderColorGradeCurveStrength",
        // Split toning
        "RenderSplitToneAmount",
        "RenderSplitToneBalance",
        "RenderSplitToneShadowTint",
        "RenderSplitToneHighlightTint",
        "RenderSplitToneMidtoneTint",
        "RenderSplitToneMidtoneAmount",
        // HDR bloom aesthetics (not the structural mip/resolution knobs)
        "RenderBloomStrength",
        "RenderBloomThreshold",
        "RenderBloomKnee",
        "RenderBloomScatter",
        "RenderBloomAlphaGlowBoost",
        "RenderBloomHalation",
        "RenderBloomHalationStrength",
        "RenderBloomHalationTint",
        // Legacy glow (minus RenderGlowResolutionPow, owned by graphics presets)
        "RenderGlow",
        "RenderGlowStrength",
        "RenderGlowWidth",
        "RenderGlowIterations",
        "RenderGlowLumWeights",
        "RenderGlowMaxExtractAlpha",
        "RenderGlowMinLuminance",
        "RenderGlowHDR",
        "RenderGlowWarmthAmount",
        "RenderGlowWarmthWeights",
        "RenderGlowNoise",
        // Lens flare
        "RenderLensFlareStrength",
        "RenderLensFlareStreakLength",
        "RenderLensFlareStreakFalloff",
        "RenderLensFlareStreakThickness",
        "RenderLensFlareStreakIntensity",
        "RenderLensFlareStreakTint",
        "RenderLensFlareChromaticSpread",
        "RenderLensFlareGlow",
        "RenderLensFlareGlowRadius",
        "RenderLensFlareGlowFalloff",
        "RenderLensFlareGhost",
        "RenderLensFlareGhostCount",
        "RenderLensFlareGhostSpacing",
        "RenderLensFlareHalo",
        "RenderLensFlareHaloRadius",
        "RenderLensFlareHaloWidth",
        "RenderLensFlareStarburst",
        "RenderLensFlareStarburstSpikes",
        "RenderLensFlareStarburstSharpness",
        "RenderLensFlareStarburstLength",
        "RenderLensFlareOcclusionRadius",
        "RenderLensFlareOcclusionTaps",
        // Chromatic aberration
        "RenderChromaticAberrationStrength",
        "RenderChromaticAberrationFalloff",
        "RenderChromaticAberrationAngle",
        "RenderChromaticAberrationAnisotropy",
        "RenderChromaticAberrationOffsetRX",
        "RenderChromaticAberrationOffsetRY",
        "RenderChromaticAberrationOffsetBX",
        "RenderChromaticAberrationOffsetBY",
        // Vignette
        "RenderVignetteAmount",
        "RenderVignetteCenter",
        "RenderVignetteColor",
        "RenderVignetteCorrectAspect",
        "RenderVignetteFeather",
        "RenderVignetteMidColor",
        "RenderVignetteMidPoint",
        "RenderVignetteRadius",
        "RenderVignetteShape",
        "RenderVignetteSoft",
        // Film grain
        "RenderFilmGrainAmount",
        "RenderFilmGrainAnimated",
        "RenderFilmGrainRange",
        "RenderFilmGrainSize",
        "RenderFilmGrainStyle",
        "RenderFilmGrainTint",
        // Sharpen
        "RenderCASSharpness",
    };
    names = looks_controls;
}

bool LLPresetsManager::savePreset(const std::string& subdirectory, std::string name, bool createDefault)
{
    bool IS_CAMERA = (PRESETS_CAMERA == subdirectory);
    bool IS_GRAPHIC = (PRESETS_GRAPHIC == subdirectory);
    bool IS_LOOKS = (PRESETS_LOOKS == subdirectory);

    if (LLTrans::getString(PRESETS_DEFAULT) == name)
    {
        name = PRESETS_DEFAULT;
    }
    if (!createDefault && name == PRESETS_DEFAULT)
    {
        LL_WARNS() << "Should not overwrite default" << LL_ENDL;
        return false;
    }

    if (isTemplateCameraPreset(name))
    {
        LL_WARNS() << "Should not overwrite template presets" << LL_ENDL;
        return false;
    }

    bool saved = false;
    std::vector<std::string> name_list;

    if (IS_GRAPHIC)
    {
        name_list.clear();
        getGraphicsControlNames(name_list);

        if (!createDefault)
        {
            gSavedSettings.setString("PresetGraphicActive", name);
            name_list.push_back("PresetGraphicActive");
        }
    }
    else if (IS_CAMERA)
    {
        name_list.clear();
        getCameraControlNames(name_list);
        name_list.push_back("PresetCameraActive");
    }
    else if (IS_LOOKS)
    {
        // The active-name tracking settings are deliberately not embedded in
        // the file: Looks are shared as files, and the filtered apply sets
        // them explicitly.
        name_list.clear();
        getLooksControlNames(name_list);
    }
    else
    {
        LL_ERRS() << "Invalid presets directory '" << subdirectory << "'" << LL_ENDL;
    }

    // make an empty llsd
    LLSD paramsData(LLSD::emptyMap());

    // Create a default graphics preset from hw recommended settings
    if (IS_GRAPHIC && createDefault && name == PRESETS_DEFAULT)
    {
        paramsData = LLFeatureManager::getInstance()->getRecommendedSettingsMap();
        if (gSavedSettings.getU32("RenderAvatarMaxComplexity") == 0)
        {
            mIgnoreChangedSignal = true;
            // use the recommended setting as an initial one (MAINT-6435)
            gSavedSettings.setU32("RenderAvatarMaxComplexity", paramsData["RenderAvatarMaxComplexity"]["Value"].asInteger());
            mIgnoreChangedSignal = false;
        }

        // Add dynamic controls to default preset
        for (std::vector<std::string>::iterator it = name_list.begin(); it != name_list.end(); ++it)
        {
            std::string ctrl_name = *it;

            LLControlVariable* ctrl = gSavedSettings.getControl(ctrl_name).get();
            if (ctrl)
            {
                std::string comment = ctrl->getComment();
                std::string type = LLControlGroup::typeEnumToString(ctrl->type());
                LLSD value = ctrl->getValue();

                paramsData[ctrl_name]["Comment"] = comment;
                paramsData[ctrl_name]["Persist"] = 1;
                paramsData[ctrl_name]["Type"] = type;
                paramsData[ctrl_name]["Value"] = value;
            }
        }
    }
    else
    {
        ECameraPreset new_camera_preset = (ECameraPreset)gSavedSettings.getU32("CameraPresetType");
        if (IS_CAMERA)
        {
            if (isDefaultCameraPreset(name))
            {
                if (PRESETS_REAR_VIEW == name)
                {
                    new_camera_preset = CAMERA_PRESET_REAR_VIEW;
                }
                else if (PRESETS_SIDE_VIEW == name)
                {
                    new_camera_preset = CAMERA_PRESET_GROUP_VIEW;
                }
                else if (PRESETS_FRONT_VIEW == name)
                {
                    new_camera_preset = CAMERA_PRESET_FRONT_VIEW;
                }
            }
            else
            {
                new_camera_preset = CAMERA_PRESET_CUSTOM;
            }
        }
        for (std::vector<std::string>::iterator it = name_list.begin(); it != name_list.end(); ++it)
        {
            std::string ctrl_name = *it;

            LLControlVariable* ctrl = gSavedSettings.getControl(ctrl_name).get();
            if (ctrl)
            {
                std::string comment = ctrl->getComment();
                std::string type = LLControlGroup::typeEnumToString(ctrl->type());
                LLSD value = ctrl->getValue();

                paramsData[ctrl_name]["Comment"] = comment;
                paramsData[ctrl_name]["Persist"] = 1;
                paramsData[ctrl_name]["Type"] = type;
                paramsData[ctrl_name]["Value"] = value;
            }
        }
        if (IS_CAMERA)
        {
            gSavedSettings.setU32("CameraPresetType", new_camera_preset);
        }
    }

    std::string pathName(getPresetsDir(subdirectory) + gDirUtilp->getDirDelimiter() + LLURI::escape(name) + ".xml");

 // If the active preset name is the only thing in the list, don't save the list
    if (paramsData.size() > 1)
    {
        // write to file
        llofstream presetsXML(pathName.c_str());
        if (presetsXML.is_open())
        {
            LLPointer<LLSDFormatter> formatter = new LLSDXMLFormatter();
            formatter->format(paramsData, presetsXML, LLSDFormatter::OPTIONS_PRETTY);
            presetsXML.close();
            saved = true;

            LL_DEBUGS() << "saved preset '" << name << "'; " << paramsData.size() << " parameters" << LL_ENDL;

            if (IS_GRAPHIC)
            {
                gSavedSettings.setString("PresetGraphicActive", name);
                // signal interested parties
                triggerChangeSignal();
            }

            if (IS_CAMERA)
            {
                gSavedSettings.setString("PresetCameraActive", name);
                // signal interested parties
                triggerChangeCameraSignal();
            }

            if (IS_LOOKS)
            {
                gSavedSettings.setString("PresetLooksActive", name);
                gSavedSettings.setString("PresetLooksLastApplied", name);
                // signal interested parties
                triggerChangeLooksSignal();
            }
        }
        else
        {
            LL_WARNS("Presets") << "Cannot open for output preset file " << pathName << LL_ENDL;
        }
    }
    else
    {
        LL_INFOS() << "No settings available to be saved" << LL_ENDL;
    }

    return saved;
}

bool LLPresetsManager::setPresetNamesInComboBox(const std::string& subdirectory, LLComboBox* combo, EDefaultOptions default_option)
{
    bool sts = true;

    combo->clearRows();
    combo->setEnabled(true);

    std::list<std::string> preset_names;
    loadPresetNamesFromDir(subdirectory, preset_names, default_option);

    if (preset_names.begin() != preset_names.end())
    {
        for (std::list<std::string>::const_iterator it = preset_names.begin(); it != preset_names.end(); ++it)
        {
            const std::string& name = *it;
            combo->add(name, name);
        }
    }
    else
    {
        combo->setLabel(LLTrans::getString("preset_combo_label"));
        combo->setEnabled(PRESETS_CAMERA != subdirectory);
        sts = false;
    }

    return sts;
}

void LLPresetsManager::loadPreset(const std::string& subdirectory, std::string name)
{
    if (LLTrans::getString(PRESETS_DEFAULT) == name)
    {
        name = PRESETS_DEFAULT;
    }

    std::string full_path(getPresetsDir(subdirectory) + gDirUtilp->getDirDelimiter() + LLURI::escape(name) + ".xml");

    LL_DEBUGS() << "attempting to load preset '"<<name<<"' from '"<<full_path<<"'" << LL_ENDL;

    bool appearance_camera_movement = gSavedSettings.getBOOL("AppearanceCameraMovement");
    bool edit_camera_movement = gSavedSettings.getBOOL("EditCameraMovement");

    mIgnoreChangedSignal = true;
    if(gSavedSettings.loadFromFile(full_path, false, true) > 0)
    {
        mIgnoreChangedSignal = false;
        if(PRESETS_GRAPHIC == subdirectory)
        {
            gSavedSettings.setString("PresetGraphicActive", name);

            LLFloaterPreference* instance = LLFloaterReg::findTypedInstance<LLFloaterPreference>("preferences");
            if (instance)
            {
                instance->refreshEnabledGraphics();
            }
            triggerChangeSignal();
        }
        if(PRESETS_CAMERA == subdirectory)
        {
            gSavedSettings.setString("PresetCameraActive", name);
            triggerChangeCameraSignal();

            //SL-20277 old preset files may contain settings that should be ignored when loading camera presets
            if (appearance_camera_movement != (bool)gSavedSettings.getBOOL("AppearanceCameraMovement"))
            {
                gSavedSettings.setBOOL("AppearanceCameraMovement", appearance_camera_movement);
            }
            if (edit_camera_movement != (bool)gSavedSettings.getBOOL("EditCameraMovement"))
            {
                gSavedSettings.setBOOL("EditCameraMovement", edit_camera_movement);
            }
        }
    }
    else
    {
        mIgnoreChangedSignal = false;
        LL_WARNS("Presets") << "failed to load preset '"<<name<<"' from '"<<full_path<<"'" << LL_ENDL;
    }
}

bool LLPresetsManager::loadLooksPreset(std::string name)
{
    std::string full_path(getPresetsDir(PRESETS_LOOKS) + gDirUtilp->getDirDelimiter() + LLURI::escape(name) + ".xml");

    llifstream preset_file(full_path.c_str());
    if (!preset_file.is_open())
    {
        LL_WARNS("Presets") << "Cannot open Look '" << name << "' at '" << full_path << "'" << LL_ENDL;
        return false;
    }

    LLSD params;
    LLSDSerialize::fromXML(params, preset_file);
    preset_file.close();
    if (!params.isMap() || params.size() == 0)
    {
        LL_WARNS("Presets") << "Look '" << name << "' is not a valid preset file" << LL_ENDL;
        return false;
    }

    // Whitelist-filtered apply: only recognized aesthetic keys are ever
    // written, so a shared Look file cannot alter unrelated settings.
    std::vector<std::string> allowed;
    getLooksControlNames(allowed);

    S32 applied = 0;
    mIgnoreChangedSignal = true;
    for (const std::string& ctrl_name : allowed)
    {
        if (!params.has(ctrl_name))
        {
            continue;
        }
        const LLSD& entry = params[ctrl_name];
        if (!entry.isMap() || !entry.has("Value"))
        {
            continue;
        }
        LLControlVariable* ctrl = gSavedSettings.getControl(ctrl_name).get();
        if (ctrl)
        {
            ctrl->set(entry["Value"]);
            ++applied;
        }
    }
    mIgnoreChangedSignal = false;

    if (applied == 0)
    {
        LL_WARNS("Presets") << "Look '" << name << "' contained no applicable settings" << LL_ENDL;
        return false;
    }

    LL_DEBUGS("Presets") << "Applied Look '" << name << "'; " << applied << " settings" << LL_ENDL;
    gSavedSettings.setString("PresetLooksActive", name);
    gSavedSettings.setString("PresetLooksLastApplied", name);
    triggerChangeLooksSignal();

    return true;
}

bool LLPresetsManager::deletePreset(const std::string& subdirectory, std::string name)
{
    if (LLTrans::getString(PRESETS_DEFAULT) == name)
    {
        name = PRESETS_DEFAULT;
    }

    bool sts = true;

    if (PRESETS_DEFAULT == name)
    {
        // This code should never execute
        LL_WARNS("Presets") << "You are not allowed to delete the default preset." << LL_ENDL;
        sts = false;
    }

    if (gDirUtilp->deleteFilesInDir(getPresetsDir(subdirectory), LLURI::escape(name) + ".xml") < 1)
    {
        LL_WARNS("Presets") << "Error removing preset " << name << " from disk" << LL_ENDL;
        sts = false;
    }

    // If you delete the preset that is currently marked as loaded then also indicate that no preset is loaded.
    if(PRESETS_GRAPHIC == subdirectory)
    {
        if (gSavedSettings.getString("PresetGraphicActive") == name)
        {
            gSavedSettings.setString("PresetGraphicActive", "");
        }
        // signal interested parties
        triggerChangeSignal();
    }

    if(PRESETS_CAMERA == subdirectory)
    {
        if (gSavedSettings.getString("PresetCameraActive") == name)
        {
            gSavedSettings.setString("PresetCameraActive", "");
        }
        // signal interested parties
        triggerChangeCameraSignal();
    }

    if(PRESETS_LOOKS == subdirectory)
    {
        if (gSavedSettings.getString("PresetLooksActive") == name)
        {
            gSavedSettings.setString("PresetLooksActive", "");
        }
        if (gSavedSettings.getString("PresetLooksLastApplied") == name)
        {
            gSavedSettings.setString("PresetLooksLastApplied", "");
        }
        // signal interested parties
        triggerChangeLooksSignal();
    }

    return sts;
}

bool LLPresetsManager::isDefaultCameraPreset(std::string preset_name)
{
    return (preset_name == PRESETS_REAR_VIEW || preset_name == PRESETS_SIDE_VIEW || preset_name == PRESETS_FRONT_VIEW);
}

bool LLPresetsManager::isTemplateCameraPreset(std::string preset_name)
{
    return (preset_name == PRESETS_REAR || preset_name == PRESETS_SIDE || preset_name == PRESETS_FRONT);
}

void LLPresetsManager::resetCameraPreset(std::string preset_name)
{
    if (isDefaultCameraPreset(preset_name))
    {
        createDefaultCameraPreset(preset_name, true);

        if (gSavedSettings.getString("PresetCameraActive") == preset_name)
        {
            loadPreset(PRESETS_CAMERA, preset_name);
        }
    }
}

bool LLPresetsManager::createDefaultCameraPreset(std::string preset_name, bool force_reset)
{
    std::string preset_file = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, PRESETS_DIR,
        PRESETS_CAMERA, LLURI::escape(preset_name) + ".xml");
    if (!gDirUtilp->fileExists(preset_file) || force_reset)
    {
        std::string template_name = preset_name.substr(0, preset_name.size() - PRESETS_VIEW_SUFFIX.size());
        std::string default_template_file = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, PRESETS_CAMERA, template_name + ".xml");
        return LLFile::copy(default_template_file, preset_file);
    }
    return false;
}

boost::signals2::connection LLPresetsManager::setPresetListChangeCameraCallback(const preset_list_signal_t::slot_type& cb)
{
    return mPresetListChangeCameraSignal.connect(cb);
}

boost::signals2::connection LLPresetsManager::setPresetListChangeCallback(const preset_list_signal_t::slot_type& cb)
{
    return mPresetListChangeSignal.connect(cb);
}

boost::signals2::connection LLPresetsManager::setPresetListChangeLooksCallback(const preset_list_signal_t::slot_type& cb)
{
    return mPresetListChangeLooksSignal.connect(cb);
}
