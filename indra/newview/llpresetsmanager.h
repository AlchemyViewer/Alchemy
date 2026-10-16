/**
 * @file llpresetsmanager.h
 * @brief Implementation for the LLPresetsManager class.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_PRESETSMANAGER_H
#define LL_PRESETSMANAGER_H

#include "llcombobox.h"

#include <list>
#include <map>

static const std::string PRESETS_DEFAULT = "Default";
static const std::string PRESETS_DEFAULT_UPPER = "DEFAULT";
static const std::string PRESETS_DIR = "presets";
static const std::string PRESETS_GRAPHIC = "graphic";
static const std::string PRESETS_CAMERA = "camera";
static const std::string PRESETS_LOOKS = "looks";
/// Which bundled Looks have already been copied into the user's directory, so a
/// Look added in a later release still reaches an existing user while one they
/// deleted stays deleted. Lives in the user settings root and NOT in the looks
/// directory, because anything named *.xml in there is enumerated as a Look.
static const std::string SEEDED_LOOKS_FILE = "looks_seeded.xml";
static const std::string PRESETS_REAR = "Rear";
static const std::string PRESETS_FRONT = "Front";
static const std::string PRESETS_SIDE = "Side";
static const std::string PRESETS_VIEW_SUFFIX = " View";
static const std::string PRESETS_REAR_VIEW = PRESETS_REAR + PRESETS_VIEW_SUFFIX;
static const std::string PRESETS_FRONT_VIEW = PRESETS_FRONT + PRESETS_VIEW_SUFFIX;
static const std::string PRESETS_SIDE_VIEW = PRESETS_SIDE + PRESETS_VIEW_SUFFIX;

enum EDefaultOptions
{
    DEFAULT_SHOW,
    DEFAULT_TOP,
    DEFAULT_BOTTOM,
    DEFAULT_HIDE                // Do not display "Default" in a list
};

class LLPresetsManager final : public LLSingleton<LLPresetsManager>
{
    LLSINGLETON(LLPresetsManager);
    ~LLPresetsManager();

public:

    typedef std::list<std::string> preset_name_list_t;
    typedef boost::signals2::signal<void()> preset_list_signal_t;

    void createMissingDefault(const std::string& subdirectory);
    void startWatching(const std::string& subdirectory);
    void triggerChangeCameraSignal();
    void triggerChangeSignal();
    void triggerChangeLooksSignal();
    static std::string getPresetsDir(const std::string& subdirectory);
    bool setPresetNamesInComboBox(const std::string& subdirectory, LLComboBox* combo, EDefaultOptions default_option);
    void loadPresetNamesFromDir(const std::string& subdirectory, preset_name_list_t& presets, EDefaultOptions default_option);
    bool savePreset(const std::string& subdirectory, std::string name, bool createDefault = false);
    void loadPreset(const std::string& subdirectory, std::string name);
    // Looks apply only whitelisted keys from the file (never a raw
    // loadFromFile), so shared Look files cannot carry unrelated settings.
    bool loadLooksPreset(std::string name);
    // The single source of truth for what a Look carries. Public because the
    // Lightbox's undo stack watches exactly this list: a setting worth saving
    // into a Look is a setting worth undoing, and sharing the list means a new
    // grading control joins both at once rather than one and not the other.
    void getLooksControlNames(std::vector<std::string>& names);
    bool deletePreset(const std::string& subdirectory, std::string name);

    void createCameraDefaultPresets();

    bool isTemplateCameraPreset(std::string preset_name);
    bool isDefaultCameraPreset(std::string preset_name);
    void resetCameraPreset(std::string preset_name);
    bool createDefaultCameraPreset(std::string preset_name, bool force_reset = false);

    void setIgnoreChangeSignal(bool val)
    {
        mIgnoreChangedSignal = val;
    }

    // Emitted when a preset gets loaded, deleted, or saved.
    boost::signals2::connection setPresetListChangeCameraCallback(const preset_list_signal_t::slot_type& cb);
    boost::signals2::connection setPresetListChangeCallback(const preset_list_signal_t::slot_type& cb);
    boost::signals2::connection setPresetListChangeLooksCallback(const preset_list_signal_t::slot_type& cb);

    // Emitted when a preset gets loaded or saved.
    preset_name_list_t mPresetNames;

    preset_list_signal_t mPresetListChangeCameraSignal;
    preset_list_signal_t mPresetListChangeSignal;
    preset_list_signal_t mPresetListChangeLooksSignal;

  private:
    LOG_CLASS(LLPresetsManager);

    void getGraphicsControlNames(std::vector<std::string>& names);
    void getCameraControlNames(std::vector<std::string>& names);
    void graphicsSettingChanged();
    void cameraSettingChanged();
    void looksSettingChanged();
    void copyDefaultLooks();

    std::vector<boost::signals2::connection> mGraphicsChangedSignals;
    std::vector<boost::signals2::connection> mCameraChangedSignals;
    std::vector<boost::signals2::connection> mLooksChangedSignals;

    bool mIgnoreChangedSignal = false;
};

#endif // LL_PRESETSMANAGER_H
