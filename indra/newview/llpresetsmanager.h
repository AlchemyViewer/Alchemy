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

    /// Where a preset of that name lives: its subdirectory, and its name
    /// escaped as a file name.
    static std::string getPresetFilePath(const std::string& subdirectory, const std::string& name);
    /// How long a preset's name may be once escaped as a file name, for it
    /// to be written (through "<name>.xml.tmp") in that subdirectory: on
    /// Windows, what MAX_PATH leaves after the folder's path; elsewhere, what
    /// a file name holds. 0 where the folder's path leaves nothing.
    static size_t escapedPresetNameLimit(const std::string& subdirectory);
    /// A preset file read as the settings map it holds. False, with a
    /// warning, where there is no such file or it is not a map.
    bool readPresetFile(const std::string& subdirectory, const std::string& name, LLSD& params);
    /// A settings map written as a preset file, in the shape every preset
    /// file has: `name -> {Comment, Persist, Type, Value}`, indented.
    bool writePresetFile(const std::string& subdirectory, const std::string& name, const LLSD& params);
    /// Whether a preset of that name exists, ignoring case: the comparison
    /// stands in for the file system that would overwrite it, and on
    /// Windows and macOS that file system does not care about case.
    bool presetExists(const std::string& subdirectory, const std::string& name);
    /// Whether no preset may be saved under that name: the default preset's,
    /// in any case and as translated -- the rule the Save As dialog refuses
    /// by, and the one a Look from outside is held to.
    static bool isReservedPresetName(const std::string& name);

    /// What a Look's map says about one setting: nothing; a value that is
    /// refused (ALLookFormat::sanitizeLookValue, by the setting's own type,
    /// or no such setting, or an entry without a Value); or a value as it
    /// is to be written, in `clean`. `ctrl`, where asked for, is the
    /// setting itself, null where there is none.
    enum class LookEntry
    {
        Absent,
        Refused,
        Taken
    };
    LookEntry judgeLookEntry(const LLSD& settings, const std::string& ctrl_name, LLSD& clean, LLControlVariable** ctrl = nullptr);

    /// What was taken from a Look as it was applied or saved: written,
    /// present but malformed and so skipped, and not a Look's setting at
    /// all and so ignored.
    struct LookSettingCounts
    {
        S32 accepted = 0;
        S32 rejected = 0;
        S32 unknown  = 0;
    };
    // Looks apply only whitelisted keys from the file (never a raw
    // loadFromFile), so shared Look files cannot carry unrelated settings;
    // and every value is judged first (ALLookFormat::sanitizeLookValue),
    // so a file cannot put a NaN, a wrong type or a path into one either.
    bool loadLooksPreset(std::string name);
    /// The whitelisted settings as they stand, in the shape a Look file has.
    LLSD captureLookSettings();
    /// A Look from outside -- `name -> {Type, Value}`, as a notecard carries
    /// one -- written to the Looks folder under `name`, normalised to the
    /// shape a saved Look has, with the values that pass judgment and the
    /// setting's own default in place of each that does not, so that the
    /// file is the full snapshot a saved Look is.
    /// Nothing is applied, and the active Look is not changed; the list
    /// signal fires. False where the name is reserved, nothing passed, or
    /// the file could not be written.
    bool saveSharedLook(const std::string& name, const LLSD& shared_settings, LookSettingCounts& counts);
    /// Whether the Look saved under `name` holds exactly the values a Look
    /// from outside does, setting for setting, as each would be written.
    bool lookHoldsSameSettings(const std::string& name, const LLSD& shared_settings);
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

    /// A setting's entry as a preset file holds it: its Comment, Persist,
    /// Type and this value. Non-const because LLControlVariable::type() is.
    static LLSD describeSetting(LLControlVariable& ctrl, const LLSD& value);
    /// Every named setting's entry, with its live value, put into `params`.
    void addLiveSettings(const std::vector<std::string>& names, LLSD& params);
    /// The whitelisted settings a Look's map carries written, each judged
    /// first, and the rest reset where the map is a full snapshot. The
    /// body of loadLooksPreset, for a Look that is already a map.
    LookSettingCounts applyLookSettings(const LLSD& params);

    std::vector<boost::signals2::connection> mGraphicsChangedSignals;
    std::vector<boost::signals2::connection> mCameraChangedSignals;
    std::vector<boost::signals2::connection> mLooksChangedSignals;

    bool mIgnoreChangedSignal = false;
};

#endif // LL_PRESETSMANAGER_H
