/**
 * @file audioengine_fmodstudio.cpp
 * @brief Implementation of LLAudioEngine class abstracting the audio
 * support as a FMODSTUDIO implementation
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Linden Research, Inc.
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

#include "linden_common.h"

#include "llstreamingaudio.h"
#include "llstreamingaudio_fmodstudio.h"

#include "llaudioengine_fmodstudio.h"
#include "lllistener_fmodstudio.h"

#include "llerror.h"
#include "llmath.h"
#include "llrand.h"
#include "lldir.h"
#include "llapr.h"

#include "sound_ids.h"

#include <fmod.hpp>
#include <fmod_errors.h>

#include <cstdio>

FMOD_RESULT F_CALL windCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels);

FMOD::ChannelGroup *LLAudioEngine_FMODSTUDIO::mChannelGroups[LLAudioEngine::AUDIO_TYPE_COUNT] = {0};
float LLAudioEngine_FMODSTUDIO::sReverbSendScale = 0.0f;

namespace
{
    // Serialise an FMOD_GUID into the canonical Windows-style
    // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" form. Used as the stable
    // device id in the picker UI / settings; round-trips via simple
    // string compare against another FMOD_GUID's serialised form.
    std::string guid_to_string(const FMOD_GUID& g)
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf),
            "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            g.Data1, g.Data2, g.Data3,
            g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
            g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
        return buf;
    }

    // Lookup a FMOD I3DL2-style preset by name. Mirrors the FAudio
    // pick_i3dl2_preset / OpenAL pick_efx_preset surfaces so the shared
    // AudioReverbPreset setting drives all three backends identically.
    // FMOD has no DEFAULT / SMALLROOM presets — map to the closest
    // available (GENERIC and ROOM); CARPETTEDHALLWAY is spelt with two
    // T's in FMOD's headers (vs CARPETEDHALLWAY in I3DL2 / EFX) —
    // accept both. Unknown -> PLAIN with a warn.
    FMOD_REVERB_PROPERTIES pick_fmod_preset(const std::string& raw_name)
    {
        std::string name(raw_name);
        // std::toupper takes int and is UB for char values >0x7F on
        // platforms with signed char. Cast through unsigned char first.
        for (auto& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if (name == "GENERIC")        { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_GENERIC;        return p; }
        if (name == "PADDEDCELL")     { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_PADDEDCELL;     return p; }
        if (name == "ROOM")           { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_ROOM;           return p; }
        if (name == "BATHROOM")       { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_BATHROOM;       return p; }
        if (name == "LIVINGROOM")     { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_LIVINGROOM;     return p; }
        if (name == "STONEROOM")      { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_STONEROOM;      return p; }
        if (name == "AUDITORIUM")     { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_AUDITORIUM;     return p; }
        if (name == "CONCERTHALL")    { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_CONCERTHALL;    return p; }
        if (name == "CAVE")           { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_CAVE;           return p; }
        if (name == "ARENA")          { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_ARENA;          return p; }
        if (name == "HANGAR")         { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_HANGAR;         return p; }
        if (name == "CARPETEDHALLWAY"
         || name == "CARPETTEDHALLWAY"){ FMOD_REVERB_PROPERTIES p = FMOD_PRESET_CARPETTEDHALLWAY; return p; }
        if (name == "HALLWAY")        { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_HALLWAY;        return p; }
        if (name == "STONECORRIDOR")  { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_STONECORRIDOR;  return p; }
        if (name == "ALLEY")          { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_ALLEY;          return p; }
        if (name == "FOREST")         { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_FOREST;         return p; }
        if (name == "CITY")           { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_CITY;           return p; }
        if (name == "MOUNTAINS")      { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_MOUNTAINS;      return p; }
        if (name == "QUARRY")         { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_QUARRY;         return p; }
        if (name == "PLAIN")          { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_PLAIN;          return p; }
        if (name == "PARKINGLOT")     { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_PARKINGLOT;     return p; }
        if (name == "SEWERPIPE")      { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_SEWERPIPE;      return p; }
        if (name == "UNDERWATER")     { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_UNDERWATER;     return p; }

        // I3DL2 has these; FMOD doesn't — map to the closest match.
        if (name == "DEFAULT")        { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_GENERIC;        return p; }
        if (name == "SMALLROOM")      { FMOD_REVERB_PROPERTIES p = FMOD_PRESET_ROOM;           return p; }

        LL_WARNS() << "LLAudioEngine_FMODSTUDIO: unknown reverb preset '"
                   << raw_name << "' — falling back to PLAIN. See "
                      "settings_alchemy.xml AudioReverbPreset for the "
                      "valid names." << LL_ENDL;
        FMOD_REVERB_PROPERTIES p = FMOD_PRESET_PLAIN;
        return p;
    }
}

LLAudioEngine_FMODSTUDIO::LLAudioEngine_FMODSTUDIO(bool enable_profiler,
                                                     std::string preferred_device_id)
:   mInited(false),
    mWindDSP(nullptr),
    mSystem(nullptr),
    mEnableProfiler(enable_profiler),
    mWindDSPDesc(nullptr),
    mPreferredDeviceId(std::move(preferred_device_id))
{
}


LLAudioEngine_FMODSTUDIO::~LLAudioEngine_FMODSTUDIO()
{
    // mWindDSPDesc, mWindGen and mWindDSP get cleaned up on cleanupWind in LLAudioEngine::shutdown()
    // mSystem gets cleaned up at shutdown()
}


static inline bool Check_FMOD_Error(FMOD_RESULT result, const char *string)
{
    if (result == FMOD_OK)
        return false;
    LL_DEBUGS("FMOD") << string << " Error: " << FMOD_ErrorString(result) << LL_ENDL;
    return true;
}

bool LLAudioEngine_FMODSTUDIO::init(void* userdata, const std::string &app_title)
{
    U32 version;
    FMOD_RESULT result;

    LL_DEBUGS("AppInit") << "LLAudioEngine_FMODSTUDIO::init() initializing FMOD" << LL_ENDL;

    result = FMOD::System_Create(&mSystem);
    if (Check_FMOD_Error(result, "FMOD::System_Create"))
        return false;

    //will call LLAudioEngine_FMODSTUDIO::allocateListener, which needs a valid mSystem pointer.
    LLAudioEngine::init(userdata, app_title);

    result = mSystem->getVersion(&version);
    Check_FMOD_Error(result, "FMOD::System::getVersion");

    if (version < FMOD_VERSION)
    {
        LL_WARNS("AppInit") << "FMOD Studio version mismatch, actual: " << version
            << " expected:" << FMOD_VERSION << LL_ENDL;
    }

    // Resolve preferred device id -> FMOD driver index. Default (0)
    // when id is empty or no current driver matches — covers unplugged
    // or replaced devices without forcing a re-pick. Must run before
    // mSystem->init() — that's when FMOD locks in the driver.
    int active_driver = 0;
    if (!mPreferredDeviceId.empty())
    {
        int num_drivers = 0;
        mSystem->getNumDrivers(&num_drivers);
        bool matched = false;
        for (int i = 0; i < num_drivers; ++i)
        {
            FMOD_GUID g{};
            char name[256] = {0};
            if (mSystem->getDriverInfo(i, name, sizeof(name), &g,
                                        nullptr, nullptr, nullptr) != FMOD_OK)
                continue;
            if (guid_to_string(g) == mPreferredDeviceId)
            {
                active_driver = i;
                mActiveDeviceId = mPreferredDeviceId;
                name[sizeof(name)-1] = '\0';
                mActiveDeviceName = name;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init() preferred "
                                   "driver id '" << mPreferredDeviceId
                                << "' not present; using system default."
                                << LL_ENDL;
        }
    }
    if (active_driver != 0)
    {
        mSystem->setDriver(active_driver);
    }

    FMOD_ADVANCEDSETTINGS settings;
    memset(&settings, 0, sizeof(settings));
    settings.cbSize = sizeof(FMOD_ADVANCEDSETTINGS);
    settings.resamplerMethod = FMOD_DSP_RESAMPLER_SPLINE;

    result = mSystem->setAdvancedSettings(&settings);
    Check_FMOD_Error(result, "FMOD::System::setAdvancedSettings");

    // FMOD_INIT_THREAD_UNSAFE Disables thread safety for API calls.
    // Only use this if FMOD is being called from a single thread, and if Studio API is not being used.
    U32 fmod_flags = FMOD_INIT_NORMAL | FMOD_INIT_3D_RIGHTHANDED | FMOD_INIT_THREAD_UNSAFE;
    if (mEnableProfiler)
    {
        fmod_flags |= FMOD_INIT_PROFILE_ENABLE;
    }

#if LL_LINUX
    bool audio_ok = false;

    if (!audio_ok)
    {
        const char* env_string = getenv("LL_BAD_FMOD_PULSEAUDIO");
        if (nullptr == env_string)
        {
            LL_DEBUGS("AppInit") << "Trying PulseAudio audio output..." << LL_ENDL;
            if (mSystem->setOutput(FMOD_OUTPUTTYPE_PULSEAUDIO) == FMOD_OK &&
                (result = mSystem->init(LL_MAX_AUDIO_CHANNELS + 2, fmod_flags, const_cast<char*>(app_title.c_str()))) == FMOD_OK)
            {
                LL_DEBUGS("AppInit") << "PulseAudio output initialized OKAY" << LL_ENDL;
                audio_ok = true;
            }
            else
            {
                Check_FMOD_Error(result, "PulseAudio audio output FAILED to initialize");
            }
        }
        else
        {
            LL_DEBUGS("AppInit") << "PulseAudio audio output SKIPPED" << LL_ENDL;
        }
    }
    if (!audio_ok)
    {
        const char* env_string = getenv("LL_BAD_FMOD_ALSA");
        if (nullptr == env_string)
        {
            LL_DEBUGS("AppInit") << "Trying ALSA audio output..." << LL_ENDL;
            if (mSystem->setOutput(FMOD_OUTPUTTYPE_ALSA) == FMOD_OK &&
                (result = mSystem->init(LL_MAX_AUDIO_CHANNELS + 2, fmod_flags, 0)) == FMOD_OK)
            {
                LL_DEBUGS("AppInit") << "ALSA audio output initialized OKAY" << LL_ENDL;
                audio_ok = true;
            }
            else
            {
                Check_FMOD_Error(result, "ALSA audio output FAILED to initialize");
            }
        }
        else
        {
            LL_DEBUGS("AppInit") << "ALSA audio output SKIPPED" << LL_ENDL;
        }
    }
    if (!audio_ok)
    {
        LL_WARNS("AppInit") << "Overall audio init failure." << LL_ENDL;
        return false;
    }

    // We're interested in logging which output method we
    // ended up with, for QA purposes.
    FMOD_OUTPUTTYPE output_type;
    mSystem->getOutput(&output_type);
    switch (output_type)
    {
    case FMOD_OUTPUTTYPE_NOSOUND:
        LL_INFOS("AppInit") << "Audio output: NoSound" << LL_ENDL; break;
    case FMOD_OUTPUTTYPE_PULSEAUDIO:
        LL_INFOS("AppInit") << "Audio output: PulseAudio" << LL_ENDL; break;
    case FMOD_OUTPUTTYPE_ALSA:
        LL_INFOS("AppInit") << "Audio output: ALSA" << LL_ENDL; break;
    default:
        LL_INFOS("AppInit") << "Audio output: Unknown!" << LL_ENDL; break;
    };
#else // LL_LINUX

    // initialize the FMOD engine
    // number of channel in this case looks to be identiacal to number of max simultaneously
    // playing objects and we can set practically any number
    result = mSystem->init(LL_MAX_AUDIO_CHANNELS + 2, fmod_flags, 0);
    if (Check_FMOD_Error(result, "Error initializing FMOD Studio with default settins, retrying with other format"))
    {
        result = mSystem->setSoftwareFormat(44100, FMOD_SPEAKERMODE_STEREO, 0/*- ignore*/);
        if (Check_FMOD_Error(result, "Error setting sotware format. Can't init."))
        {
            return false;
        }
        result = mSystem->init(LL_MAX_AUDIO_CHANNELS + 2, fmod_flags, 0);
    }
    if (Check_FMOD_Error(result, "Error initializing FMOD Studio"))
    {
        // If it fails here and (result == FMOD_ERR_OUTPUT_CREATEBUFFER),
        // we can retry with other settings
        return false;
    }
#endif

    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init() FMOD Studio initialized correctly" << LL_ENDL;

    int r_numbuffers, r_samplerate, r_channels;
    unsigned int r_bufferlength;
    char r_name[512];
    int latency = 100;
    mSystem->getDSPBufferSize(&r_bufferlength, &r_numbuffers);
    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): r_bufferlength=" << r_bufferlength << " bytes" << LL_ENDL;
    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): r_numbuffers=" << r_numbuffers << LL_ENDL;

    {
        // Query whichever driver FMOD actually selected (may be 0 if we
        // didn't match the preferred id, or the index we set above).
        // Record id + name so getActiveOutputDevice() reports the live
        // device rather than always driver 0.
        int live_driver = 0;
        mSystem->getDriver(&live_driver);
        FMOD_GUID g{};
        mSystem->getDriverInfo(live_driver, r_name, 511, &g, &r_samplerate, nullptr, &r_channels);
        r_name[511] = '\0';
        mActiveDeviceName = r_name;
        mActiveDeviceId = guid_to_string(g);
    }
    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): r_name=\"" << r_name << "\"" << LL_ENDL;
    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): r_samplerate=" << r_samplerate << "Hz" << LL_ENDL;
    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): r_channels=" << r_channels << LL_ENDL;

    if (r_samplerate != 0)
        latency = (int)(1000.0f * r_bufferlength * r_numbuffers / r_samplerate);
    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): latency=" << latency << "ms" << LL_ENDL;

    mInited = true;

    // Mark reverb as available — FMOD always ships its DSP reverb, no
    // extension check needed. Preset / send-scale come from llstartup
    // via the lifted base setters after init returns.
    mReverbActive = true;
    {
        FMOD_REVERB_PROPERTIES off = FMOD_PRESET_OFF;
        // Seed instance 0 with OFF so the slot exists but contributes
        // nothing until llstartup pushes the user's saved preset.
        Check_FMOD_Error(mSystem->setReverbProperties(0, &off),
                         "FMOD::System::setReverbProperties (init seed)");
    }

    LL_INFOS("AppInit") << "LLAudioEngine_FMODSTUDIO::init(): initialization complete." << LL_ENDL;

    return true;
}

// virtual
std::vector<LLAudioOutputDevice> LLAudioEngine_FMODSTUDIO::enumerateOutputDevices() const
{
    std::vector<LLAudioOutputDevice> devices;
    if (!mSystem) return devices;
    int num_drivers = 0;
    mSystem->getNumDrivers(&num_drivers);
    devices.reserve(num_drivers);
    for (int i = 0; i < num_drivers; ++i)
    {
        FMOD_GUID g{};
        char name[256] = {0};
        if (mSystem->getDriverInfo(i, name, sizeof(name), &g,
                                    nullptr, nullptr, nullptr) != FMOD_OK)
        {
            devices.emplace_back();  // keep index alignment
            continue;
        }
        name[sizeof(name)-1] = '\0';
        devices.push_back({ guid_to_string(g), std::string(name) });
    }
    return devices;
}

// virtual
void LLAudioEngine_FMODSTUDIO::setOutputDevice(const std::string& id)
{
    if (id == mPreferredDeviceId && mInited) return;
    mPreferredDeviceId = id;

    if (!mSystem || !mInited)
    {
        // Preference saved; next init() will pick it up.
        return;
    }

    // Resolve preferred id (or empty -> driver 0) to a driver index.
    int target = 0;
    if (!id.empty())
    {
        int num_drivers = 0;
        mSystem->getNumDrivers(&num_drivers);
        bool matched = false;
        for (int i = 0; i < num_drivers; ++i)
        {
            FMOD_GUID g{};
            char name[256] = {0};
            if (mSystem->getDriverInfo(i, name, sizeof(name), &g,
                                        nullptr, nullptr, nullptr) != FMOD_OK)
                continue;
            if (guid_to_string(g) == id)
            {
                target = i;
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            LL_INFOS() << "LLAudioEngine_FMODSTUDIO::setOutputDevice() driver id '"
                       << id << "' not found among live drivers; preference "
                       "saved but no live swap performed." << LL_ENDL;
            return;
        }
    }

    int current = 0;
    mSystem->getDriver(&current);
    if (target == current) return;

    // Per FMOD docs (System::setDriver), when called after System::init the
    // current driver is shut down and the newly selected one is
    // initialized — so this is a true hot-swap, no manual teardown
    // needed. Active channels may glitch briefly while FMOD reinitialises
    // its output but the system stays alive.
    LL_INFOS() << "LLAudioEngine_FMODSTUDIO::setOutputDevice() switching to "
                  "driver " << target << " (id '"
               << (id.empty() ? "<system default>" : id.c_str()) << "')"
               << LL_ENDL;
    FMOD_RESULT result = mSystem->setDriver(target);
    if (Check_FMOD_Error(result, "FMOD::System::setDriver"))
    {
        LL_WARNS() << "LLAudioEngine_FMODSTUDIO::setOutputDevice() setDriver "
                      "failed; preference saved but live swap aborted."
                   << LL_ENDL;
        return;
    }

    // Refresh active id + display name from the live driver.
    FMOD_GUID g{};
    char name[256] = {0};
    if (mSystem->getDriverInfo(target, name, sizeof(name), &g,
                                nullptr, nullptr, nullptr) == FMOD_OK)
    {
        name[sizeof(name)-1] = '\0';
        mActiveDeviceId = guid_to_string(g);
        mActiveDeviceName = name;
    }

    // Notify any subscribed UIs that the active device shifted.
    mDevicesChangedSignal();
}

// virtual
void LLAudioEngine_FMODSTUDIO::setReverbPreset(const std::string& preset_name)
{
    if (!mSystem || !mReverbActive) return;
    FMOD_REVERB_PROPERTIES p = pick_fmod_preset(preset_name);
    // Instance 0 is the default reverb slot every channel mixes into
    // unless explicitly retargeted; updating it propagates the new
    // preset to all active channels without a per-channel reapply.
    Check_FMOD_Error(mSystem->setReverbProperties(0, &p),
                     "FMOD::System::setReverbProperties");
}

// virtual
void LLAudioEngine_FMODSTUDIO::setReverbSendScale(float scale)
{
    if (scale < 0.0f) scale = 0.0f;
    sReverbSendScale = scale;
    if (!mReverbActive) return;
    // Per-channel wet level only takes effect when set on each channel
    // — System::setReverbProperties carries the preset shape, not the
    // wet send. Iterate the live channel pool and apply; new channels
    // pick the value up in play().
    // Channels in mChannels were created by this engine's createChannel,
    // so the static_cast to the FMOD derived type is safe. setReverbWet
    // no-ops if the channel hasn't been bound to an FMOD::Channel yet —
    // the next play() will pick the value up from sReverbSendScale.
    for (size_t i = 0; i < mChannels.size(); ++i)
    {
        if (auto* ch = static_cast<LLAudioChannelFMODSTUDIO*>(mChannels[i]))
        {
            ch->setReverbWet(scale);
        }
    }
}


std::string LLAudioEngine_FMODSTUDIO::getDriverName(bool verbose)
{
    llassert_always(mSystem);
    if (verbose)
    {
        U32 version;
        if (!Check_FMOD_Error(mSystem->getVersion(&version), "FMOD::System::getVersion"))
        {
            return llformat("FMOD Studio %1x.%02x.%02x", version >> 16, version >> 8 & 0x000000FF, version & 0x000000FF);
        }
    }
    return "FMOD STUDIO";
}


// create our favourite FMOD-native streaming audio implementation
LLStreamingAudioInterface *LLAudioEngine_FMODSTUDIO::createDefaultStreamingAudioImpl() const
{
    return new LLStreamingAudio_FMODSTUDIO(mSystem);
}


void LLAudioEngine_FMODSTUDIO::allocateListener(void)
{
    mListenerp = (LLListener *) new LLListener_FMODSTUDIO(mSystem);
    if (!mListenerp)
    {
        LL_WARNS("FMOD") << "Listener creation failed" << LL_ENDL;
    }
}


void LLAudioEngine_FMODSTUDIO::shutdown()
{
    stopInternetStream();

    LL_INFOS("FMOD") << "About to LLAudioEngine::shutdown()" << LL_ENDL;
    LLAudioEngine::shutdown();

    LL_INFOS("FMOD") << "LLAudioEngine_FMODSTUDIO::shutdown() closing FMOD Studio" << LL_ENDL;
    if (mSystem)
    {
        mSystem->close();
        mSystem->release();
    }
    LL_INFOS("FMOD") << "LLAudioEngine_FMODSTUDIO::shutdown() done closing FMOD Studio" << LL_ENDL;

    delete mListenerp;
    mListenerp = nullptr;
}


LLAudioBuffer * LLAudioEngine_FMODSTUDIO::createBuffer()
{
    return new LLAudioBufferFMODSTUDIO(mSystem);
}


LLAudioChannel * LLAudioEngine_FMODSTUDIO::createChannel()
{
    return new LLAudioChannelFMODSTUDIO(mSystem);
}

bool LLAudioEngine_FMODSTUDIO::initWind()
{
    mNextWindUpdate = 0.0;

    if (!mWindDSPDesc)
    {
        mWindDSPDesc = new FMOD_DSP_DESCRIPTION();
    }

    if (!mWindDSP)
    {
        memset(mWindDSPDesc, 0, sizeof(*mWindDSPDesc)); //Set everything to zero
        strncpy(mWindDSPDesc->name, "Wind Unit", sizeof(mWindDSPDesc->name));
        mWindDSPDesc->pluginsdkversion = FMOD_PLUGIN_SDK_VERSION;
        mWindDSPDesc->read = &windCallback; // Assign callback - may be called from arbitrary threads
        if (Check_FMOD_Error(mSystem->createDSP(mWindDSPDesc, &mWindDSP), "FMOD::createDSP"))
            return false;

        mWindGen.reset();

        int frequency = 44100;

        FMOD_SPEAKERMODE mode;
        if (Check_FMOD_Error(mSystem->getSoftwareFormat(&frequency, &mode, nullptr), "FMOD::System::getSoftwareFormat"))
        {
            cleanupWind();
            return false;
        }

        mWindGen = std::make_unique<LLWindGen<MIXBUFFERFORMAT>>((U32)frequency);

        if (Check_FMOD_Error(mWindDSP->setUserData((void*)mWindGen.get()), "FMOD::DSP::setUserData"))
        {
            cleanupWind();
            return false;
        }
        if (Check_FMOD_Error(mWindDSP->setChannelFormat(FMOD_CHANNELMASK_STEREO, 2, mode), "FMOD::DSP::setChannelFormat"))
        {
            cleanupWind();
            return false;
        }
    }

    // *TODO:  Should this guard against multiple plays?
    if (Check_FMOD_Error(mSystem->playDSP(mWindDSP, nullptr, false, nullptr), "FMOD::System::playDSP"))
    {
        cleanupWind();
        return false;
    }
    return true;
}


void LLAudioEngine_FMODSTUDIO::cleanupWind()
{
    if (mWindDSP)
    {
        FMOD::ChannelGroup* master_group = nullptr;
        if (!Check_FMOD_Error(mSystem->getMasterChannelGroup(&master_group), "FMOD::System::getMasterChannelGroup")
            && master_group)
        {
            master_group->removeDSP(mWindDSP);
        }
        mWindDSP->release();
        mWindDSP = nullptr;
    }

    delete mWindDSPDesc;
    mWindDSPDesc = nullptr;

    mWindGen.reset();
}


//-----------------------------------------------------------------------
void LLAudioEngine_FMODSTUDIO::updateWind(LLVector3 wind_vec, F32 camera_height_above_water)
{
    if (!mEnableWind || !mWindGen)
    {
        return;
    }

    if (mWindUpdateTimer.checkExpirationAndReset(LL_WIND_UPDATE_INTERVAL))
    {
        // Shared Linden -> DS3D flip via the base helper.
        wind_vec = lindenToDS3DWind(wind_vec);

        F64 pitch       = 1.0 + mapWindVecToPitch(wind_vec);
        F64 center_freq = 80.0 * pow(pitch, 2.5 * (mapWindVecToGain(wind_vec) + 1.0));

        // Altitude shape applied upstream of LLWindGen — FMOD's DSP
        // callback reads mTargetFreq / mTargetGain / mTargetPanGainR
        // from the LLWindGen we set up via setUserData, so the
        // shaped values propagate naturally without touching the
        // callback itself.
        const WindAltitudeShape shape = computeWindAltitudeShape(camera_height_above_water);

        mWindGen->mTargetFreq     = (F32)center_freq * shape.freq_mul;
        mWindGen->mTargetGain     = (F32)mapWindVecToGain(wind_vec) * mMaxWindGain * shape.gain_mul;
        mWindGen->mTargetPanGainR = (F32)mapWindVecToPan(wind_vec);
    }
}

//-----------------------------------------------------------------------
void LLAudioEngine_FMODSTUDIO::setInternalGain(F32 gain)
{
    if (!mInited)
    {
        return;
    }

    gain = llclamp(gain, 0.0f, 1.0f);

    FMOD::ChannelGroup* master_group = nullptr;
    if (!Check_FMOD_Error(mSystem->getMasterChannelGroup(&master_group), "FMOD::System::getMasterChannelGroup")
        && master_group)
    {
        master_group->setVolume(gain);
    }

    LLStreamingAudioInterface *saimpl = getStreamingAudioImpl();
    if (saimpl)
    {
        // fmod likes its streaming audio channel gain re-asserted after
        // master volume change.
        saimpl->setGain(saimpl->getGain());
    }
}

//
// LLAudioChannelFMODSTUDIO implementation
//

LLAudioChannelFMODSTUDIO::LLAudioChannelFMODSTUDIO(FMOD::System *system) : LLAudioChannel(), mSystemp(system), mChannelp(nullptr), mLastSamplePos(0)
{
}


LLAudioChannelFMODSTUDIO::~LLAudioChannelFMODSTUDIO()
{
    cleanup();
}

bool LLAudioChannelFMODSTUDIO::updateBuffer()
{
    if (!mCurrentSourcep)
    {
        // This channel isn't associated with any source, nothing
        // to be updated
        return false;
    }

    if (LLAudioChannel::updateBuffer())
    {
        // Base class update returned true, which means that we need to actually
        // set up the channel for a different buffer.

        LLAudioBufferFMODSTUDIO *bufferp = (LLAudioBufferFMODSTUDIO *)mCurrentSourcep->getCurrentBuffer();

        // Grab the FMOD sample associated with the buffer
        FMOD::Sound *soundp = bufferp->getSound();
        if (!soundp)
        {
            // This is bad, there should ALWAYS be a sound associated with a legit
            // buffer.
            LL_ERRS() << "No FMOD sound!" << LL_ENDL;
            return false;
        }


        // Actually play the sound.  Start it off paused so we can do all the necessary
        // setup.
        if (!mChannelp)
        {
            FMOD_RESULT result = getSystem()->playSound(soundp, nullptr /*free channel?*/, true, &mChannelp);
            Check_FMOD_Error(result, "FMOD::System::playSound");
        }

        // Setting up channel mChannelID
    }

    // If we have a source for the channel, we need to update its gain.
    if (mCurrentSourcep)
    {
        // SJB: warnings can spam and hurt framerate, disabling
        //FMOD_RESULT result;

        mChannelp->setVolume(getSecondaryGain() * mCurrentSourcep->getGain());
        //Check_FMOD_Error(result, "FMOD::Channel::setVolume");

        mChannelp->setMode(mCurrentSourcep->isLoop() ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
        /*if(Check_FMOD_Error(result, "FMOD::Channel::setMode"))
        {
        S32 index;
        mChannelp->getIndex(&index);
        LL_WARNS() << "Channel " << index << "Source ID: " << mCurrentSourcep->getID()
        << " at " << mCurrentSourcep->getPositionGlobal() << LL_ENDL;
        }*/
    }

    return true;
}


void LLAudioChannelFMODSTUDIO::update3DPosition()
{
    if (!mChannelp)
    {
        // We're not actually a live channel (i.e., we're not playing back anything)
        return;
    }

    LLAudioBufferFMODSTUDIO  *bufferp = (LLAudioBufferFMODSTUDIO  *)mCurrentBufferp;
    if (!bufferp)
    {
        // We don't have a buffer associated with us (should really have been picked up
        // by the above if.
        return;
    }

    if (mCurrentSourcep->isForcedPriority())
    {
        // Prioritized UI and preview sounds don't need to do any positional updates.
        set3DMode(false);
    }
    else
    {
        // Localized sound.  Update the position and velocity of the sound.
        set3DMode(true);

        LLVector3 float_pos;
        float_pos.setVec(mCurrentSourcep->getPositionGlobal());
        FMOD_RESULT result = mChannelp->set3DAttributes((FMOD_VECTOR*)float_pos.mV, (FMOD_VECTOR*)mCurrentSourcep->getVelocity().mV);
        Check_FMOD_Error(result, "FMOD::Channel::set3DAttributes");
    }
}


void LLAudioChannelFMODSTUDIO::updateLoop()
{
    if (!mChannelp)
    {
        // May want to clear up the loop/sample counters.
        return;
    }

    //
    // Hack:  We keep track of whether we looped or not by seeing when the
    // sample position looks like it's going backwards.  Not reliable; may
    // yield false negatives.
    //
    U32 cur_pos;
    mChannelp->getPosition(&cur_pos, FMOD_TIMEUNIT_PCMBYTES);

    if (cur_pos < (U32)mLastSamplePos)
    {
        mLoopedThisFrame = true;
    }
    mLastSamplePos = cur_pos;
}


void LLAudioChannelFMODSTUDIO::cleanup()
{
    if (!mChannelp)
    {
        // Aborting cleanup with no channel handle.
        return;
    }

    //Cleaning up channel mChannelID
    Check_FMOD_Error(mChannelp->stop(), "FMOD::Channel::stop");

    mCurrentBufferp = nullptr;
    mChannelp = nullptr;
}


void LLAudioChannelFMODSTUDIO::play()
{
    if (!mChannelp)
    {
        LL_WARNS() << "Playing without a channel handle, aborting" << LL_ENDL;
        return;
    }

    Check_FMOD_Error(mChannelp->setPaused(false), "FMOD::Channel::pause");

    getSource()->setPlayedOnce(true);

    if (LLAudioEngine_FMODSTUDIO::mChannelGroups[getSource()->getType()])
        mChannelp->setChannelGroup(LLAudioEngine_FMODSTUDIO::mChannelGroups[getSource()->getType()]);

    // Pick up the current wet level. setReverbSendScale also iterates
    // live channels for already-running sounds, so this catches the
    // freshly-started case.
    setReverbWet(LLAudioEngine_FMODSTUDIO::sReverbSendScale);
}

void LLAudioChannelFMODSTUDIO::setReverbWet(float wet)
{
    if (!mChannelp) return;
    if (wet < 0.0f) wet = 0.0f;
    // Channel::setReverbProperties takes a wet scalar (0 = dry, 1 =
    // fully wet). Apply to instance 0 — the slot the engine
    // configures in setReverbPreset.
    Check_FMOD_Error(mChannelp->setReverbProperties(0, wet),
                     "FMOD::Channel::setReverbProperties");
}


void LLAudioChannelFMODSTUDIO::playSynced(LLAudioChannel *channelp)
{
    LLAudioChannelFMODSTUDIO *fmod_channelp = (LLAudioChannelFMODSTUDIO*)channelp;
    if (!(fmod_channelp->mChannelp && mChannelp))
    {
        // Don't have channels allocated to both the master and the slave
        return;
    }

    U32 cur_pos;
    if (Check_FMOD_Error(mChannelp->getPosition(&cur_pos, FMOD_TIMEUNIT_PCMBYTES), "Unable to retrieve current position"))
        return;

    cur_pos %= mCurrentBufferp->getLength();

    // Try to match the position of our sync master
    Check_FMOD_Error(mChannelp->setPosition(cur_pos, FMOD_TIMEUNIT_PCMBYTES), "Unable to set current position");

    // Start us playing
    play();
}


bool LLAudioChannelFMODSTUDIO::isPlaying()
{
    if (!mChannelp)
    {
        return false;
    }

    bool paused, playing;
    mChannelp->getPaused(&paused);
    mChannelp->isPlaying(&playing);
    return !paused && playing;
}


//
// LLAudioChannelFMODSTUDIO implementation
//


LLAudioBufferFMODSTUDIO::LLAudioBufferFMODSTUDIO(FMOD::System *system) : mSystemp(system), mSoundp(nullptr)
{
}


LLAudioBufferFMODSTUDIO::~LLAudioBufferFMODSTUDIO()
{
    if (mSoundp)
    {
        mSoundp->release();
        mSoundp = nullptr;
    }
}


bool LLAudioBufferFMODSTUDIO::loadWAV(const std::string& filename)
{
    // Try to open a wav file from disk.  This will eventually go away, as we don't
    // really want to block doing this.
    if (filename.empty())
    {
        // invalid filename, abort.
        return false;
    }

    if (!gDirUtilp->fileExists(filename))
    {
        // File not found, abort.
        return false;
    }

    if (mSoundp)
    {
        // If there's already something loaded in this buffer, clean it up.
        mSoundp->release();
        mSoundp = nullptr;
    }

    FMOD_MODE base_mode = FMOD_LOOP_NORMAL;
    FMOD_CREATESOUNDEXINFO exinfo;
    memset(&exinfo, 0, sizeof(exinfo));
    exinfo.cbsize = sizeof(exinfo);
    exinfo.suggestedsoundtype = FMOD_SOUND_TYPE_WAV;    //Hint to speed up loading.
    // Load up the wav file into an fmod sample (since 1.05 fmod studio expects everything in UTF-8)
    FMOD_RESULT result = getSystem()->createSound(filename.c_str(), base_mode, &exinfo, &mSoundp);

    if (result != FMOD_OK)
    {
        // We failed to load the file for some reason.
        LL_WARNS() << "Could not load data '" << filename << "': " << FMOD_ErrorString(result) << LL_ENDL;

        //
        // If we EVER want to load wav files provided by end users, we need
        // to rethink this!
        //
        // file is probably corrupt - remove it.
        LLFile::remove(filename);
        return false;
    }

    // Everything went well, return true
    return true;
}


U32 LLAudioBufferFMODSTUDIO::getLength()
{
    if (!mSoundp)
    {
        return 0;
    }

    U32 length;
    mSoundp->getLength(&length, FMOD_TIMEUNIT_PCMBYTES);
    return length;
}


void LLAudioChannelFMODSTUDIO::set3DMode(bool use3d)
{
    FMOD_MODE current_mode;
    if (mChannelp->getMode(&current_mode) != FMOD_OK)
        return;
    FMOD_MODE new_mode = current_mode;
    new_mode &= ~(use3d ? FMOD_2D : FMOD_3D);
    new_mode |= use3d ? FMOD_3D : FMOD_2D;

    if (current_mode != new_mode)
    {
        mChannelp->setMode(new_mode);
    }
}

// *NOTE:  This is almost certainly being called on the mixer thread,
// not the main thread.  May have implications for callees or audio
// engine shutdown.

FMOD_RESULT F_CALL windCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels)
{
    // inbuffer = fmod's original mixbuffer.
    // outbuffer = the buffer passed from the previous DSP unit.
    // length = length in samples at this mix time.

    LLWindGen<LLAudioEngine_FMODSTUDIO::MIXBUFFERFORMAT> *windgen = nullptr;
    FMOD::DSP *thisdsp = (FMOD::DSP *)dsp_state->instance;

    thisdsp->getUserData((void **)&windgen);

    if (windgen)
    {
        windgen->windGenerate((LLAudioEngine_FMODSTUDIO::MIXBUFFERFORMAT *)outbuffer, length);
    }

    return FMOD_OK;
}
