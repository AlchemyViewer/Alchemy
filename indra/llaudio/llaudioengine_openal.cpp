/**
 * @file audioengine_openal.cpp
 * @brief implementation of audio engine using OpenAL
 * support as a OpenAL 3D implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "linden_common.h"
#include "lldir.h"
#include "llfile.h"

#include "llaudioengine_openal.h"
#include "lllistener_openal.h"
#include "llwavfile.h"

// efx-presets.h carries the EFXEAXREVERBPROPERTIES struct + macro
// initialisers. Pulled in here rather than from the public header so
// the cpp file is the only TU that depends on the preset surface.
#include <AL/efx-presets.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <vector>


const float LLAudioEngine_OpenAL::WIND_BUFFER_SIZE_SEC = 0.05f;

LLAudioEngine_OpenAL::LLAudioEngine_OpenAL(std::string preferred_device_id)
    :
    mPreferredDevice(std::move(preferred_device_id)),
    mWindBufFreq(0),
    mWindBufSamples(0),
    mWindBufBytes(0),
    mWindSource(AL_NONE)
{
}

// virtual
LLAudioEngine_OpenAL::~LLAudioEngine_OpenAL()
{
}

// virtual
bool LLAudioEngine_OpenAL::init(void* userdata, const std::string &app_title)
{
    mWindGen = NULL;
    LLAudioEngine::init(userdata, app_title);

    // alcOpenDevice(NULL) is the system default; pass the preferred name
    // through verbatim if the user picked one. If the name no longer
    // matches any present device, alcOpenDevice returns NULL and we
    // fall back to default — covers unplugged / renamed hardware
    // without forcing the user to re-pick.
    const char* device_name = mPreferredDevice.empty() ? nullptr : mPreferredDevice.c_str();
    mALCDevice = alcOpenDevice(device_name);
    if (!mALCDevice && device_name)
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() Preferred ALC device '"
                   << mPreferredDevice << "' not available; falling back to "
                   "system default." << LL_ENDL;
        mALCDevice = alcOpenDevice(nullptr);
    }
    if (!mALCDevice)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not open default ALC device" << LL_ENDL;
        return false;
    }

    // Record what we actually opened so getActiveOutputDevice / the
    // driver-name log can show the live device name.
    if (const char* opened = alcGetString(mALCDevice, ALC_ALL_DEVICES_SPECIFIER))
    {
        mActiveDevice = opened;
    }
    else if (const char* opened_basic = alcGetString(mALCDevice, ALC_DEVICE_SPECIFIER))
    {
        mActiveDevice = opened_basic;
    }

    mALCContext = alcCreateContext(mALCDevice, NULL);
    if (!mALCContext)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not create ALC context: 0x"
                   << std::hex << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        alcCloseDevice(mALCDevice);
        mALCDevice = NULL;
        return false;
    }

    if (!alcMakeContextCurrent(mALCContext))
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not make ALC context current: 0x"
                   << std::hex << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        alcDestroyContext(mALCContext);
        alcCloseDevice(mALCDevice);
        mALCContext = NULL;
        mALCDevice = NULL;
        return false;
    }

    LL_INFOS() << "LLAudioEngine_OpenAL::init() OpenAL successfully initialized" << LL_ENDL;

    LL_INFOS() << "OpenAL version: "
        << ll_safe_string(alGetString(AL_VERSION)) << LL_ENDL;
    LL_INFOS() << "OpenAL vendor: "
        << ll_safe_string(alGetString(AL_VENDOR)) << LL_ENDL;
    LL_INFOS() << "OpenAL renderer: "
        << ll_safe_string(alGetString(AL_RENDERER)) << LL_ENDL;

    ALint major = 0;
    ALint minor = 0;
    alcGetIntegerv(mALCDevice, ALC_MAJOR_VERSION, 1, &major);
    alcGetIntegerv(mALCDevice, ALC_MINOR_VERSION, 1, &minor);
    LL_INFOS() << "ALC version: " << major << "." << minor << LL_ENDL;

    LL_INFOS() << "ALC default device: "
        << ll_safe_string(alcGetString(mALCDevice,
                           ALC_DEFAULT_DEVICE_SPECIFIER))
        << LL_ENDL;

    // Best-effort EFX init for reverb. Failure is non-fatal — the engine
    // runs without reverb and supportsReverb() reports false so the UI
    // hides the controls.
    if (initEfx())
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() EFX reverb available" << LL_ENDL;
    }

    // ALC_EXT_disconnect is OpenAL Soft's mechanism for surfacing
    // device-loss. Without it we silently keep running on a dead
    // device — with it the per-frame idle() poll catches the drop and
    // tries to reopen. FAudio's device-loss path is handled inside
    // SDL3, so this is OpenAL-only.
    mDisconnectExtAvailable = (alcIsExtensionPresent(mALCDevice,
                                                     "ALC_EXT_disconnect") == ALC_TRUE);
    if (mDisconnectExtAvailable)
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() ALC_EXT_disconnect "
                      "available — will poll for device-loss." << LL_ENDL;
        mDisconnectPollTimer.reset();
    }

    initSystemDefaultFollowing();

    return true;
}

// ALC_SOFT_system_events tells us when the OS default output device
// changes; ALC_SOFT_reopen_device lets us move onto it without tearing
// down the context. Both are needed, and both only matter while the user
// is running on the system default — a pinned device stays pinned.
void LLAudioEngine_OpenAL::initSystemDefaultFollowing()
{
    mFollowSystemDefault = false;
    mEventControlSOFT    = nullptr;
    mEventCallbackSOFT   = nullptr;

    if (alcIsExtensionPresent(mALCDevice, "ALC_SOFT_reopen_device") != ALC_TRUE ||
        alcIsExtensionPresent(mALCDevice, "ALC_SOFT_system_events") != ALC_TRUE)
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() ALC_SOFT_system_events / "
                      "ALC_SOFT_reopen_device unavailable; a restart is needed "
                      "to pick up OS default device changes." << LL_ENDL;
        return;
    }

    mEventControlSOFT = reinterpret_cast<LPALCEVENTCONTROLSOFT>(
        alcGetProcAddress(mALCDevice, "alcEventControlSOFT"));
    mEventCallbackSOFT = reinterpret_cast<LPALCEVENTCALLBACKSOFT>(
        alcGetProcAddress(mALCDevice, "alcEventCallbackSOFT"));
    if (!mEventControlSOFT || !mEventCallbackSOFT)
    {
        mEventControlSOFT  = nullptr;
        mEventCallbackSOFT = nullptr;
        LL_WARNS() << "LLAudioEngine_OpenAL::init() ALC_SOFT_system_events "
                      "advertised but its entry points did not resolve."
                   << LL_ENDL;
        return;
    }

    // Subscribe regardless of the current preference: the user can move
    // back to "default" at runtime, and re-arming then would mean
    // resolving entry points against a device we may no longer hold.
    mDefaultDeviceChanged = false;
    mEventCallbackSOFT(&LLAudioEngine_OpenAL::onDeviceEventSOFT, this);
    ALCenum events[] = { ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT };
    mEventControlSOFT(1, events, ALC_TRUE);

    mFollowSystemDefault = mPreferredDevice.empty();
    if (mFollowSystemDefault)
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() following the OS default "
                      "output device." << LL_ENDL;
    }
    else
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() output device pinned to '"
                   << mPreferredDevice << "'; OS default changes ignored."
                   << LL_ENDL;
    }
}

// static
void ALC_APIENTRY LLAudioEngine_OpenAL::onDeviceEventSOFT(ALCenum event_type, ALCenum device_type,
                                                          ALCdevice* /*device*/, ALCsizei /*length*/,
                                                          const ALCchar* /*message*/, void* user_param) noexcept
{
    // Runs on OpenAL's own event thread. Do nothing here but raise the
    // flag — idle() does the reopen on the main thread, where the rest
    // of the engine's AL state is owned.
    if (event_type != ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT) return;

    // The same event reports the default CAPTURE device changing. Reopening
    // playback for that would interrupt audio because the user picked a
    // different microphone.
    if (device_type != ALC_PLAYBACK_DEVICE_SOFT) return;

    if (auto* self = static_cast<LLAudioEngine_OpenAL*>(user_param))
    {
        self->mDefaultDeviceChanged = true;
    }
}

void LLAudioEngine_OpenAL::reopenOnDefaultDevice()
{
    if (!mFollowSystemDefault || !mALCDevice) return;

    using ReopenFn = ALCboolean (ALC_APIENTRY *)(ALCdevice*, const ALCchar*,
                                                 const ALCint*);
    auto reopen = reinterpret_cast<ReopenFn>(
        alcGetProcAddress(mALCDevice, "alcReopenDeviceSOFT"));
    if (!reopen) return;

    LL_INFOS() << "LLAudioEngine_OpenAL: OS default output device changed; "
                  "reopening (was '" << mActiveDevice << "')" << LL_ENDL;

    // Clear any pending device error so the failure branch reports ours.
    (void)alcGetError(mALCDevice);

    if (reopen(mALCDevice, nullptr, nullptr) != ALC_TRUE)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::reopenOnDefaultDevice() "
                      "alcReopenDeviceSOFT failed: 0x" << std::hex
                   << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        return;
    }

    if (const char* opened = alcGetString(mALCDevice, ALC_ALL_DEVICES_SPECIFIER))
    {
        mActiveDevice = opened;
    }
    LL_INFOS() << "LLAudioEngine_OpenAL::reopenOnDefaultDevice() now on '"
               << mActiveDevice << "'" << LL_ENDL;

    // The Sound prefs combo shows the active device, so let it refresh.
    mDevicesChangedSignal();
}

// virtual
std::string LLAudioEngine_OpenAL::getDriverName(bool verbose)
{
    std::ostringstream version;

    version <<
        "OpenAL";

    if (verbose)
    {
        version <<
            ", version " <<
            ll_safe_string(alGetString(AL_VERSION)) <<
            " / " <<
            ll_safe_string(alGetString(AL_VENDOR)) <<
            " / " <<
            ll_safe_string(alGetString(AL_RENDERER));

        if (mALCDevice)
            version <<
                ": " <<
                ll_safe_string(alcGetString(mALCDevice,
                    ALC_DEFAULT_DEVICE_SPECIFIER));
    }

    return version.str();
}

// virtual
void LLAudioEngine_OpenAL::allocateListener()
{
    mListenerp = (LLListener *) new LLListener_OpenAL();
    if(!mListenerp)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::allocateListener() Listener creation failed" << LL_ENDL;
    }
}

// virtual
void LLAudioEngine_OpenAL::shutdown()
{
    // Tear EFX down BEFORE LLAudioEngine::shutdown() so shutdownEfx can
    // walk the live channel pool and detach each source's AUX send from
    // our slot. The base shutdown() deletes the channels and nulls
    // mChannels[*], so doing this after it would leave the per-channel
    // unwind as a dead-code walk over null pointers. The defensive
    // null-check inside shutdownEfx prevents a crash if the order ever
    // regresses, but the *intent* of the walk only holds with this
    // ordering.
    shutdownEfx();

    // Unhook the OS default-device callback before the device is closed,
    // so OpenAL's event thread can't call back into a half-torn engine.
    if (mEventControlSOFT && mEventCallbackSOFT)
    {
        ALCenum events[] = { ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT };
        mEventControlSOFT(1, events, ALC_FALSE);
        mEventCallbackSOFT(nullptr, nullptr);
    }
    mFollowSystemDefault = false;
    mEventControlSOFT    = nullptr;
    mEventCallbackSOFT   = nullptr;

    LL_INFOS() << "About to LLAudioEngine::shutdown()" << LL_ENDL;
    LLAudioEngine::shutdown();

    LL_INFOS() << "About to tear down OpenAL context/device" << LL_ENDL;

    if (mALCContext)
    {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(mALCContext);
        mALCContext = NULL;
    }
    if (mALCDevice)
    {
        if (!alcCloseDevice(mALCDevice))
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::shutdown() alcCloseDevice failed: 0x"
                       << std::hex << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        }
        mALCDevice = NULL;
    }

    LL_INFOS() << "LLAudioEngine_OpenAL::shutdown() OpenAL successfully shut down" << LL_ENDL;

    delete mListenerp;
    mListenerp = NULL;
}

LLAudioBuffer *LLAudioEngine_OpenAL::createBuffer()
{
    return new LLAudioBufferOpenAL();
}

LLAudioChannel *LLAudioEngine_OpenAL::createChannel()
{
    return new LLAudioChannelOpenAL();
}

void LLAudioEngine_OpenAL::setInternalGain(F32 gain)
{
    //LL_INFOS() << "LLAudioEngine_OpenAL::setInternalGain() Gain: " << gain << LL_ENDL;
    alListenerf(AL_GAIN, gain);
}

// virtual
std::vector<LLAudioOutputDevice> LLAudioEngine_OpenAL::enumerateOutputDevices() const
{
    std::vector<LLAudioOutputDevice> devices;
    // ALC enumeration returns a NULL-separated list terminated by a
    // double-NULL. ALC_ALL_DEVICES_SPECIFIER (the ALC_ENUMERATE_ALL_EXT
    // form) lists every device the implementation can open; the older
    // ALC_DEVICE_SPECIFIER is a per-implementation summary list and is
    // our fallback if the extension isn't present. Either way the
    // returned strings double as the names we pass to alcOpenDevice —
    // OpenAL doesn't surface a separate stable id, so id == name here.
    const ALCchar* list = nullptr;
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE)
    {
        list = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
    }
    if (!list)
    {
        list = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    }
    if (!list) return devices;
    for (const ALCchar* p = list; *p; p += std::strlen(p) + 1)
    {
        LLAudioOutputDevice d;
        d.id = p;
        d.name = p;
        devices.push_back(std::move(d));
    }
    return devices;
}

// virtual
void LLAudioEngine_OpenAL::setOutputDevice(const std::string& id)
{
    if (id == mPreferredDevice && mALCDevice)
    {
        return;  // already running on this device
    }
    mPreferredDevice = id;
    // Following the OS default is only correct while the user is ON the
    // default; picking a device pins it, picking "default" resumes. Set it
    // here so the early exits below cannot leave it disagreeing with
    // mPreferredDevice.
    mFollowSystemDefault  = mPreferredDevice.empty() && mEventControlSOFT != nullptr;
    mDefaultDeviceChanged = false;

    if (!mALCDevice)
    {
        // No live device — the preference will take effect on next init.
        return;
    }

    // ALC_SOFT_reopen_device lets us swap the underlying device on an
    // existing context, keeping all sources / buffers / context state
    // intact. Without it, switching would require a full teardown +
    // recreate of every AL object, which the existing channel pool
    // isn't set up to drive. In that case persist for next launch and
    // log the limitation.
    using ReopenFn = ALCboolean (ALC_APIENTRY *)(ALCdevice*, const ALCchar*,
                                                  const ALCint*);
    auto reopen = reinterpret_cast<ReopenFn>(
        alcGetProcAddress(mALCDevice, "alcReopenDeviceSOFT"));
    if (!reopen)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::setOutputDevice() "
                      "ALC_SOFT_reopen_device unavailable; preference "
                      "saved but will only take effect on next launch."
                   << LL_ENDL;
        return;
    }

    const char* device_name = id.empty() ? nullptr : id.c_str();
    if (reopen(mALCDevice, device_name, nullptr) != ALC_TRUE)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::setOutputDevice() "
                      "alcReopenDeviceSOFT failed for '"
                   << (device_name ? device_name : "<default>")
                   << "': 0x" << std::hex << alcGetError(mALCDevice)
                   << std::dec << LL_ENDL;
        return;
    }

    if (const char* opened = alcGetString(mALCDevice, ALC_ALL_DEVICES_SPECIFIER))
    {
        mActiveDevice = opened;
    }
    LL_INFOS() << "LLAudioEngine_OpenAL::setOutputDevice() switched to '"
               << mActiveDevice << "'" << LL_ENDL;

    // Notify any UI listeners (Sound prefs combo) that the active
    // device has shifted so they can refresh.
    mDevicesChangedSignal();
}

//
// ─── Device-loss detection (ALC_EXT_disconnect) ─────────────────────────
//

// virtual
void LLAudioEngine_OpenAL::idle()
{
    // Run the base bookkeeping first — source / buffer aging, priority
    // recomputation, channel reassignment. Our disconnect poll is
    // independent of any of it and just rides on top once a second.
    LLAudioEngine::idle();

    // Raised on OpenAL's event thread by onDeviceEventSOFT.
    if (mDefaultDeviceChanged)
    {
        mDefaultDeviceChanged = false;
        reopenOnDefaultDevice();
    }

    if (!mDisconnectExtAvailable || !mALCDevice) return;

    // Polling ALC_CONNECTED on every frame would generate an ALC call
    // per frame for no benefit — once every couple seconds catches a
    // user unplugging a device well within the typical "did the audio
    // just stop?" cognitive window without flooding the driver.
    static constexpr F32 kDisconnectPollIntervalSec = 2.0f;
    if (mDisconnectPollTimer.getElapsedTimeF32() < kDisconnectPollIntervalSec) return;
    mDisconnectPollTimer.reset();

    ALCint connected = ALC_TRUE;
    alcGetIntegerv(mALCDevice, ALC_CONNECTED, 1, &connected);

    if (connected == ALC_TRUE)
    {
        // Any healthy poll clears the disconnect-debounce counter so
        // flicker doesn't accumulate toward the edge threshold.
        mConsecutiveDisconnects = 0;
        if (mDeviceDisconnected)
        {
            // Edge transition disconnected -> connected. The driver
            // recovered on its own (some implementations reconnect
            // transparently after the OS audio service restarts).
            LL_INFOS() << "LLAudioEngine_OpenAL: device reconnected." << LL_ENDL;
            mDeviceDisconnected = false;
        }
        return;
    }

    // Device is currently disconnected. Require N consecutive
    // disconnected polls (~N*kDisconnectPollIntervalSec wall-clock)
    // before treating the disconnect as real — some drivers flicker
    // the ALC_CONNECTED bit during sample-rate negotiation, exclusive-
    // mode handoff, etc. Two consecutive polls (~4 seconds) is well
    // under the human "did the audio stop" threshold and well above
    // typical flicker durations.
    static constexpr int kDisconnectEdgeThreshold = 2;
    mConsecutiveDisconnects++;
    if (mConsecutiveDisconnects < kDisconnectEdgeThreshold && !mDeviceDisconnected)
    {
        // Not yet considered lost — wait for the next poll.
        return;
    }

    if (!mDeviceDisconnected)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL: device lost — attempting "
                      "alcReopenDeviceSOFT recovery every "
                   << kDisconnectPollIntervalSec << "s until it returns."
                   << LL_ENDL;
        mDeviceDisconnected = true;
    }

    // Try alcReopenDeviceSOFT to the preferred device (or system
    // default if no preference). On success the context, sources,
    // buffers, EFX state all survive — alcReopenDeviceSOFT preserves
    // them by design. The function pointer is resolved fresh each
    // attempt: when the underlying device went away, the previously-
    // resolved pointer is still valid for the surviving ALCdevice
    // wrapper, but resolving each time is cheap and matches the
    // pattern in setOutputDevice.
    using ReopenFn = ALCboolean (ALC_APIENTRY *)(ALCdevice*, const ALCchar*,
                                                  const ALCint*);
    auto reopen = reinterpret_cast<ReopenFn>(
        alcGetProcAddress(mALCDevice, "alcReopenDeviceSOFT"));
    if (!reopen) return;

    // If the user originally picked a device that was already gone at
    // init() we still try that name first here — desirable behaviour
    // since hot-plug of the original target should restore the pick.
    // Empty mPreferredDevice means "no explicit preference, use
    // system default", and alcReopenDeviceSOFT(NULL) does exactly that.
    const char* device_name = mPreferredDevice.empty() ? nullptr
                                                       : mPreferredDevice.c_str();
    if (reopen(mALCDevice, device_name, nullptr) == ALC_TRUE)
    {
        // Verify the live device actually reports connected post-reopen
        // — alcReopenDeviceSOFT can succeed against a device that's
        // still gone (e.g., returning EOF immediately). The next poll
        // tick (~2s) will catch that and retry.
        ALCint post = ALC_TRUE;
        alcGetIntegerv(mALCDevice, ALC_CONNECTED, 1, &post);
        if (post == ALC_TRUE)
        {
            LL_INFOS() << "LLAudioEngine_OpenAL: device recovered via "
                          "alcReopenDeviceSOFT." << LL_ENDL;
            mDeviceDisconnected = false;
            if (const char* opened =
                    alcGetString(mALCDevice, ALC_ALL_DEVICES_SPECIFIER))
            {
                mActiveDevice = opened;
            }
        }
    }
}

//
// ─── EFX reverb ──────────────────────────────────────────────────────────
//

namespace
{
    // Lookup an EFX EAXReverb preset struct by I3DL2 name. Mirrors the
    // FAudio backend's pick_i3dl2_preset so the same AudioReverbPreset
    // setting drives both backends identically. DEFAULT and SMALLROOM
    // aren't in the EFX preset table — map to the closest available
    // (GENERIC and CASTLE_SMALLROOM respectively). Unknown -> PLAIN
    // with a warn.
    EFXEAXREVERBPROPERTIES pick_efx_preset(const std::string& raw_name)
    {
        std::string name(raw_name);
        // std::toupper takes int and is UB for char values >0x7F on
        // platforms with signed char. Cast through unsigned char first.
        for (auto& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        // I3DL2 names that exist verbatim in EFX.
        if (name == "GENERIC")         { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_GENERIC;         return p; }
        if (name == "PADDEDCELL")      { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_PADDEDCELL;      return p; }
        if (name == "ROOM")            { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_ROOM;            return p; }
        if (name == "BATHROOM")        { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_BATHROOM;        return p; }
        if (name == "LIVINGROOM")      { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_LIVINGROOM;      return p; }
        if (name == "STONEROOM")       { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_STONEROOM;       return p; }
        if (name == "AUDITORIUM")      { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_AUDITORIUM;      return p; }
        if (name == "CONCERTHALL")     { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_CONCERTHALL;     return p; }
        if (name == "CAVE")            { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_CAVE;            return p; }
        if (name == "ARENA")           { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_ARENA;           return p; }
        if (name == "HANGAR")          { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_HANGAR;          return p; }
        if (name == "CARPETEDHALLWAY") { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_CARPETEDHALLWAY; return p; }
        if (name == "HALLWAY")         { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_HALLWAY;         return p; }
        if (name == "STONECORRIDOR")   { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_STONECORRIDOR;   return p; }
        if (name == "ALLEY")           { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_ALLEY;           return p; }
        if (name == "FOREST")          { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_FOREST;          return p; }
        if (name == "CITY")            { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_CITY;            return p; }
        if (name == "MOUNTAINS")       { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_MOUNTAINS;       return p; }
        if (name == "QUARRY")          { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_QUARRY;          return p; }
        if (name == "PLAIN")           { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_PLAIN;           return p; }
        if (name == "PARKINGLOT")      { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_PARKINGLOT;      return p; }
        if (name == "SEWERPIPE")       { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_SEWERPIPE;       return p; }
        if (name == "UNDERWATER")      { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_UNDERWATER;      return p; }

        // FAudio I3DL2 has these; EFX uses different names — map to the
        // closest match so the user's pick still resolves to a sensible
        // sound rather than the silent fallback.
        if (name == "DEFAULT")   { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_GENERIC;        return p; }
        if (name == "SMALLROOM") { EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_CASTLE_SMALLROOM; return p; }

        LL_WARNS() << "LLAudioEngine_OpenAL: unknown reverb preset '" << raw_name
                   << "' — falling back to PLAIN. See settings_alchemy.xml "
                      "AudioReverbPreset for the valid names." << LL_ENDL;
        EFXEAXREVERBPROPERTIES p = EFX_REVERB_PRESET_PLAIN;
        return p;
    }
}

bool LLAudioEngine_OpenAL::initEfx()
{
    // ALC_EXT_EFX is the umbrella; checked on the device, not the
    // context. Without it the EAXReverb effect type isn't available.
    if (alcIsExtensionPresent(mALCDevice, "ALC_EXT_EFX") != ALC_TRUE)
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::initEfx() ALC_EXT_EFX not "
                      "available; running without reverb." << LL_ENDL;
        return false;
    }

    // Resolve the function pointers we use. alGetProcAddress returns
    // the implementation's function for the current context, so this
    // has to run after alcMakeContextCurrent.
    mAlGenEffects = reinterpret_cast<LPALGENEFFECTS>(
        alGetProcAddress("alGenEffects"));
    mAlDeleteEffects = reinterpret_cast<LPALDELETEEFFECTS>(
        alGetProcAddress("alDeleteEffects"));
    mAlEffecti = reinterpret_cast<LPALEFFECTI>(
        alGetProcAddress("alEffecti"));
    mAlEffectf = reinterpret_cast<LPALEFFECTF>(
        alGetProcAddress("alEffectf"));
    mAlEffectfv = reinterpret_cast<LPALEFFECTFV>(
        alGetProcAddress("alEffectfv"));
    mAlGenAuxiliaryEffectSlots = reinterpret_cast<LPALGENAUXILIARYEFFECTSLOTS>(
        alGetProcAddress("alGenAuxiliaryEffectSlots"));
    mAlDeleteAuxiliaryEffectSlots = reinterpret_cast<LPALDELETEAUXILIARYEFFECTSLOTS>(
        alGetProcAddress("alDeleteAuxiliaryEffectSlots"));
    mAlAuxiliaryEffectSloti = reinterpret_cast<LPALAUXILIARYEFFECTSLOTI>(
        alGetProcAddress("alAuxiliaryEffectSloti"));
    mAlAuxiliaryEffectSlotf = reinterpret_cast<LPALAUXILIARYEFFECTSLOTF>(
        alGetProcAddress("alAuxiliaryEffectSlotf"));

    if (!mAlGenEffects || !mAlDeleteEffects || !mAlEffecti || !mAlEffectf
        || !mAlEffectfv || !mAlGenAuxiliaryEffectSlots
        || !mAlDeleteAuxiliaryEffectSlots || !mAlAuxiliaryEffectSloti
        || !mAlAuxiliaryEffectSlotf)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::initEfx() failed to resolve "
                      "one or more EFX function pointers; running without "
                      "reverb." << LL_ENDL;
        return false;
    }

    alGetError();  // clear any stale error before the EFX object gens

    mAlGenAuxiliaryEffectSlots(1, &mEffectSlot);
    mAlGenEffects(1, &mEffect);

    // Prefer EAXReverb (full I3DL2 surface). Drivers that only advertise
    // ALC_EXT_EFX but don't implement EAXReverb fall back to AL_EFFECT_
    // REVERB — coarser but still functional. iDecayHFLimit / GainLF /
    // panning fields are dropped on the standard REVERB type.
    mAlEffecti(mEffect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    if (alGetError() != AL_NO_ERROR)
    {
        mAlEffecti(mEffect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        if (alGetError() != AL_NO_ERROR)
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::initEfx() neither "
                          "EAXReverb nor standard reverb supported; "
                          "running without reverb." << LL_ENDL;
            mAlDeleteEffects(1, &mEffect);
            mAlDeleteAuxiliaryEffectSlots(1, &mEffectSlot);
            mEffect = 0;
            mEffectSlot = 0;
            return false;
        }
    }

    mEfxAvailable = true;
    // Seed with PLAIN at silent slot gain so the effect chain is live
    // but inaudible from the moment init returns; llstartup pushes the
    // user's saved preset / send scale via setReverbPreset /
    // setReverbSendScale once the engine is up. The 0.0f initial gain
    // matters because anything that plays in the window between
    // initEfx() and llstartup's push would otherwise reverb at full
    // wet — nothing in the viewer's startup ordering actually plays
    // that early, but seeding silent costs nothing and is safer.
    // The engine itself stays free of gSavedSettings, matching the
    // FAudio backend's "config in, no direct llcontrol coupling" shape.
    applyReverbPreset("PLAIN");
    mAlAuxiliaryEffectSlotf(mEffectSlot, AL_EFFECTSLOT_GAIN, 0.0f);

    return true;
}

void LLAudioEngine_OpenAL::shutdownEfx()
{
    if (!mEfxAvailable) return;

    // INVARIANT: this must run BEFORE LLAudioEngine::shutdown() (which
    // deletes the channel pool and nulls mChannels[*]). The per-
    // channel walk below relies on live channels being present so the
    // AL_AUXILIARY_SEND_FILTER detach actually fires for each source —
    // otherwise it's a dead-code walk over null slots. shutdown() at
    // the top of this file orders the call correctly; keep it that
    // way. The defensive null-check on each slot prevents a crash if
    // the order regresses, but the *purpose* of the walk only holds
    // with the right ordering.
    //
    // Channels that already wired AL_AUXILIARY_SEND_FILTER to our slot
    // hold a stale slot id past this point. Walk the pool and reset
    // their latch + clear the send back to AL_EFFECTSLOT_NULL — if EFX
    // is ever re-initialized while channels are alive (no caller does
    // this today, but the design should fail gracefully if one is
    // added) the lazy-wire in play() will re-attach to the new slot.
    for (size_t i = 0; i < mChannels.size(); ++i)
    {
        if (auto* ch = static_cast<LLAudioChannelOpenAL*>(mChannels[i]))
        {
            ch->onEfxShutdown();
        }
    }

    // Detach effect from slot, then delete both. Sources auto-clean
    // their AUX sends when the source itself is destroyed (in
    // LLAudioChannelOpenAL::~LLAudioChannelOpenAL).
    mAlAuxiliaryEffectSloti(mEffectSlot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
    mAlDeleteEffects(1, &mEffect);
    mAlDeleteAuxiliaryEffectSlots(1, &mEffectSlot);
    mEffect = 0;
    mEffectSlot = 0;
    mEfxAvailable = false;
}

void LLAudioEngine_OpenAL::applyReverbPreset(const std::string& name)
{
    if (!mEfxAvailable) return;

    EFXEAXREVERBPROPERTIES p = pick_efx_preset(name);

    // EAXReverb has the full surface area. The standard REVERB effect
    // type silently ignores writes to enums it doesn't support, so
    // setting everything is safe regardless of which fallback we landed
    // on in initEfx.
    mAlEffectf (mEffect, AL_EAXREVERB_DENSITY,             p.flDensity);
    mAlEffectf (mEffect, AL_EAXREVERB_DIFFUSION,           p.flDiffusion);
    mAlEffectf (mEffect, AL_EAXREVERB_GAIN,                p.flGain);
    mAlEffectf (mEffect, AL_EAXREVERB_GAINHF,              p.flGainHF);
    mAlEffectf (mEffect, AL_EAXREVERB_GAINLF,              p.flGainLF);
    mAlEffectf (mEffect, AL_EAXREVERB_DECAY_TIME,          p.flDecayTime);
    mAlEffectf (mEffect, AL_EAXREVERB_DECAY_HFRATIO,       p.flDecayHFRatio);
    mAlEffectf (mEffect, AL_EAXREVERB_DECAY_LFRATIO,       p.flDecayLFRatio);
    mAlEffectf (mEffect, AL_EAXREVERB_REFLECTIONS_GAIN,    p.flReflectionsGain);
    mAlEffectf (mEffect, AL_EAXREVERB_REFLECTIONS_DELAY,   p.flReflectionsDelay);
    mAlEffectfv(mEffect, AL_EAXREVERB_REFLECTIONS_PAN,     p.flReflectionsPan);
    mAlEffectf (mEffect, AL_EAXREVERB_LATE_REVERB_GAIN,    p.flLateReverbGain);
    mAlEffectf (mEffect, AL_EAXREVERB_LATE_REVERB_DELAY,   p.flLateReverbDelay);
    mAlEffectfv(mEffect, AL_EAXREVERB_LATE_REVERB_PAN,     p.flLateReverbPan);
    mAlEffectf (mEffect, AL_EAXREVERB_ECHO_TIME,           p.flEchoTime);
    mAlEffectf (mEffect, AL_EAXREVERB_ECHO_DEPTH,          p.flEchoDepth);
    mAlEffectf (mEffect, AL_EAXREVERB_MODULATION_TIME,     p.flModulationTime);
    mAlEffectf (mEffect, AL_EAXREVERB_MODULATION_DEPTH,    p.flModulationDepth);
    mAlEffectf (mEffect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, p.flAirAbsorptionGainHF);
    mAlEffectf (mEffect, AL_EAXREVERB_HFREFERENCE,         p.flHFReference);
    mAlEffectf (mEffect, AL_EAXREVERB_LFREFERENCE,         p.flLFReference);
    mAlEffectf (mEffect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, p.flRoomRolloffFactor);
    mAlEffecti (mEffect, AL_EAXREVERB_DECAY_HFLIMIT,       p.iDecayHFLimit);

    // Re-binding the effect to the slot is what makes parameter
    // changes audible — slot state is sampled at attach time.
    mAlAuxiliaryEffectSloti(mEffectSlot, AL_EFFECTSLOT_EFFECT,
                            static_cast<ALint>(mEffect));
}

void LLAudioEngine_OpenAL::setReverbPreset(const std::string& preset_name)
{
    applyReverbPreset(preset_name);
}

void LLAudioEngine_OpenAL::setReverbSendScale(float scale)
{
    if (!mEfxAvailable) return;
    if (scale < 0.0f) scale = 0.0f;
    mAlAuxiliaryEffectSlotf(mEffectSlot, AL_EFFECTSLOT_GAIN, scale);
}

LLAudioChannelOpenAL::LLAudioChannelOpenAL()
    :
    mALSource(AL_NONE),
    mLastSamplePos(0)
{
    alGenSources(1, &mALSource);
}

LLAudioChannelOpenAL::~LLAudioChannelOpenAL()
{
    cleanup();
    if (mALSource != AL_NONE)
    {
        alDeleteSources(1, &mALSource);
        mALSource = AL_NONE;
    }
}

void LLAudioChannelOpenAL::cleanup()
{
    if (mALSource != AL_NONE)
    {
        alSourceStop(mALSource);
        alSourcei(mALSource, AL_BUFFER, AL_NONE);
    }

    mCurrentBufferp = NULL;
}

void LLAudioChannelOpenAL::play()
{
    if (mALSource == AL_NONE)
    {
        LL_WARNS() << "Playing without a mALSource, aborting" << LL_ENDL;
        return;
    }

    // Lazy-wire the AUX send to the engine's reverb slot on first play.
    // Deferred from the constructor because engine init order doesn't
    // guarantee initEfx has run before the channel pool spawns. EFX
    // unavailable -> getEffectSlot() returns 0 -> source AUX send
    // stays at AL_EFFECTSLOT_NULL (silently dry).
    if (!mAuxSendWired)
    {
        if (auto* eng = dynamic_cast<LLAudioEngine_OpenAL*>(gAudiop))
        {
            const ALuint slot = eng->getEffectSlot();
            if (slot)
            {
                alSource3i(mALSource, AL_AUXILIARY_SEND_FILTER,
                           static_cast<ALint>(slot), 0,
                           AL_FILTER_NULL);
            }
            mAuxSendWired = true;
        }
    }

    if(!isPlaying())
    {
        alSourcePlay(mALSource);
        getSource()->setPlayedOnce(true);
    }
}

void LLAudioChannelOpenAL::onEfxShutdown()
{
    // The engine is tearing down its effect slot; clear our reference
    // to it and reset the lazy-wire flag so a subsequent initEfx
    // re-attaches us via the next play() call. Skip the alSource3i if
    // we never wired in the first place — saves an ALC round-trip.
    if (mAuxSendWired && mALSource != AL_NONE)
    {
        alSource3i(mALSource, AL_AUXILIARY_SEND_FILTER,
                   AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
    }
    mAuxSendWired = false;
}

void LLAudioChannelOpenAL::playSynced(LLAudioChannel *channelp)
{
    if (channelp)
    {
        LLAudioChannelOpenAL *masterchannelp =
            (LLAudioChannelOpenAL*)channelp;
        if (mALSource != AL_NONE &&
            masterchannelp->mALSource != AL_NONE)
        {
            // we have channels allocated to master and slave
            ALfloat master_offset;
            alGetSourcef(masterchannelp->mALSource, AL_SEC_OFFSET,
                     &master_offset);

            LL_INFOS() << "Syncing with master at " << master_offset
                << "sec" << LL_ENDL;
            // *TODO: detect when this fails, maybe use AL_SAMPLE_
            alSourcef(mALSource, AL_SEC_OFFSET, master_offset);
        }
    }
    play();
}

bool LLAudioChannelOpenAL::isPlaying()
{
    if (mALSource != AL_NONE)
    {
        ALint state;
        alGetSourcei(mALSource, AL_SOURCE_STATE, &state);
        if(state == AL_PLAYING)
        {
            return true;
        }
    }

    return false;
}

bool LLAudioChannelOpenAL::updateBuffer()
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
        // set up the source for a different buffer.
        LLAudioBufferOpenAL *bufferp = (LLAudioBufferOpenAL *)mCurrentSourcep->getCurrentBuffer();
        if (!bufferp)
        {
            return false;
        }
        alSourcei(mALSource, AL_BUFFER, bufferp->getBuffer());
        mLastSamplePos = 0;
    }

    if (mCurrentSourcep)
    {
        alSourcef(mALSource, AL_GAIN,
              mCurrentSourcep->getGain() * getSecondaryGain());
        alSourcei(mALSource, AL_LOOPING,
              mCurrentSourcep->isLoop() ? AL_TRUE : AL_FALSE);
        alSourcef(mALSource, AL_ROLLOFF_FACTOR,
              gAudiop->mListenerp->getRolloffFactor());
    }

    return true;
}


void LLAudioChannelOpenAL::updateLoop()
{
    if (mALSource == AL_NONE)
    {
        return;
    }

    // Hack:  We keep track of whether we looped or not by seeing when the
    // sample position looks like it's going backwards.  Not reliable; may
    // yield false negatives.
    //
    ALint cur_pos;
    alGetSourcei(mALSource, AL_SAMPLE_OFFSET, &cur_pos);
    if (cur_pos < mLastSamplePos)
    {
        mLoopedThisFrame = true;
    }
    mLastSamplePos = cur_pos;
}


void LLAudioChannelOpenAL::update3DPosition()
{
    if(!mCurrentSourcep)
    {
        return;
    }
    if (mCurrentSourcep->isForcedPriority())
    {
        alSource3f(mALSource, AL_POSITION, 0.0, 0.0, 0.0);
        alSource3f(mALSource, AL_VELOCITY, 0.0, 0.0, 0.0);
        alSourcei (mALSource, AL_SOURCE_RELATIVE, AL_TRUE);
    } else {
        LLVector3 float_pos;
        float_pos.setVec(mCurrentSourcep->getPositionGlobal());
        alSourcefv(mALSource, AL_POSITION, float_pos.mV);
        alSourcefv(mALSource, AL_VELOCITY, mCurrentSourcep->getVelocity().mV);
        alSourcei (mALSource, AL_SOURCE_RELATIVE, AL_FALSE);
    }

    alSourcef(mALSource, AL_GAIN, mCurrentSourcep->getGain() * getSecondaryGain());
}

LLAudioBufferOpenAL::LLAudioBufferOpenAL() = default;

LLAudioBufferOpenAL::~LLAudioBufferOpenAL()
{
    cleanup();
}

void LLAudioBufferOpenAL::cleanup()
{
    if(mALBuffer != AL_NONE)
    {
        alGetError(); // clear error
        alDeleteBuffers(1, &mALBuffer);

        ALenum error = alGetError();
        if(AL_NO_ERROR != error)
        {
            LL_WARNS("OpenAL") << "Error: 0x" << std::hex << error << std::dec
                << " when cleaning up a buffer" << LL_ENDL;
        }
        mALBuffer = AL_NONE;
    }
    mBytesPerFrame = 0;
}

bool LLAudioBufferOpenAL::loadWAV(const std::string& filename)
{
    cleanup();

    LLAudio::WavInfo wav;
    if (!LLAudio::parseWav(filename, wav))
    {
        return false;
    }

    if (wav.format_tag != 1 /* WAVE_FORMAT_PCM */)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unsupported WAV format 0x"
            << std::hex << wav.format_tag << std::dec
            << " (only PCM is supported) in " << filename << LL_ENDL;
        return false;
    }

    ALenum al_format = AL_NONE;
    if (wav.channels == 1 && wav.bits_per_sample == 8)       al_format = AL_FORMAT_MONO8;
    else if (wav.channels == 1 && wav.bits_per_sample == 16) al_format = AL_FORMAT_MONO16;
    else if (wav.channels == 2 && wav.bits_per_sample == 8)  al_format = AL_FORMAT_STEREO8;
    else if (wav.channels == 2 && wav.bits_per_sample == 16) al_format = AL_FORMAT_STEREO16;
    else
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unsupported PCM layout: "
            << wav.channels << "ch " << wav.bits_per_sample << "-bit in "
            << filename << LL_ENDL;
        return false;
    }

    alGetError(); // clear
    alGenBuffers(1, &mALBuffer);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR || mALBuffer == AL_NONE)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() alGenBuffers failed: 0x"
            << std::hex << err << std::dec << LL_ENDL;
        mALBuffer = AL_NONE;
        return false;
    }

    alBufferData(mALBuffer, al_format, wav.pcm.data(),
                 static_cast<ALsizei>(wav.pcm.size()),
                 static_cast<ALsizei>(wav.sample_rate));
    err = alGetError();
    if (err != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() alBufferData failed: 0x"
            << std::hex << err << std::dec << " for " << filename << LL_ENDL;
        alDeleteBuffers(1, &mALBuffer);
        mALBuffer = AL_NONE;
        return false;
    }

    mBytesPerFrame = static_cast<U16>(wav.channels * (wav.bits_per_sample / 8));
    return true;
}

U32 LLAudioBufferOpenAL::getLength()
{
    if (mALBuffer == AL_NONE || mBytesPerFrame == 0)
    {
        return 0;
    }
    ALint length = 0;
    alGetBufferi(mALBuffer, AL_SIZE, &length);
    return static_cast<U32>(length) / mBytesPerFrame;
}

// ------------

bool LLAudioEngine_OpenAL::initWind()
{
    ALenum error;
    LL_INFOS() << "LLAudioEngine_OpenAL::initWind() start" << LL_ENDL;

    alGetError(); /* clear error */

    alGenSources(1,&mWindSource);

    if((error=alGetError()) != AL_NO_ERROR || mWindSource == AL_NONE)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::initWind() alGenSources failed: "
                   << error << " — wind disabled until next initWind()" << LL_ENDL;
        // Bail before allocating mWindGen / scratch buffer / pool. The
        // historical pattern just logged and continued, leaving every
        // subsequent updateWind call issuing AL_INVALID_NAME against
        // mWindSource == AL_NONE and the pool buffers leaking via
        // queue/unqueue against an invalid source.
        mWindSource = AL_NONE;
        return false;
    }

    // Synthesise at the device's actual sample rate so OpenAL Soft
    // doesn't have to resample our wind PCM on the output path —
    // eliminates a hidden resample stage. ALC_FREQUENCY is queried
    // on the device, not the context, and returns the mixer's output
    // rate (typically 48 kHz on modern WASAPI / CoreAudio / Pipewire).
    ALCint device_rate = 44100;
    if (mALCDevice)
    {
        alcGetIntegerv(mALCDevice, ALC_FREQUENCY, 1, &device_rate);
        if (device_rate <= 0) device_rate = 44100;
    }
    mWindGen = std::make_unique<LLWindGen<WIND_SAMPLE_T>>(static_cast<U32>(device_rate));

    mWindBufFreq = mWindGen->getInputSamplingRate();
    mWindBufSamples = llceil(mWindBufFreq * WIND_BUFFER_SIZE_SEC);
    mWindBufBytes = mWindBufSamples * 2 /*stereo*/ * sizeof(WIND_SAMPLE_T);

    // One scratch buffer the synthesiser writes into. Each pool
    // buffer's alBufferData call copies the contents, so a single
    // shared scratch is correct (and avoids per-pool-slot heap).
    mWindBuf.assign(mWindBufSamples * 2 /*stereo*/, WIND_SAMPLE_T{});

    // Pre-allocate the AL buffer pool once. The historical code
    // alGen/alDelete'd buffers each updateWind tick, which is both
    // wasteful (per-frame ALC pressure) and a memory-safety hazard
    // (delete failures left dangling ids). Now we own a fixed pool
    // for the source's lifetime and just rewrite contents via
    // alBufferData when recycling.
    mWindBufferPool.assign(MAX_NUM_WIND_BUFFERS, 0);
    alGetError();
    alGenBuffers(MAX_NUM_WIND_BUFFERS, mWindBufferPool.data());
    if ((error = alGetError()) != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::initWind() alGenBuffers failed: "
                   << error << LL_ENDL;
        mWindBufferPool.clear();
        mWindGen.reset();
        return false;
    }
    mWindFreeBuffers.clear();
    for (ALuint id : mWindBufferPool) mWindFreeBuffers.push_back(id);

    LL_INFOS() << "LLAudioEngine_OpenAL::initWind() done — "
               << MAX_NUM_WIND_BUFFERS << " buffers @ "
               << mWindBufFreq << " Hz, "
               << (WIND_BUFFER_SIZE_SEC * 1000.f) << " ms each" << LL_ENDL;

    return true;
}

void LLAudioEngine_OpenAL::cleanupWind()
{
    LL_INFOS() << "LLAudioEngine_OpenAL::cleanupWind()" << LL_ENDL;

    if (mWindSource != AL_NONE)
    {
        // Stop the source and detach all buffers in one go via
        // alSourcei(..., AL_BUFFER, AL_NONE) — that's the canonical
        // way to clear the queue without per-buffer unqueue. Then
        // delete the source.
        alSourceStop(mWindSource);
        alSourcei(mWindSource, AL_BUFFER, AL_NONE);
        alDeleteSources(1, &mWindSource);
        mWindSource = AL_NONE;
    }

    if (!mWindBufferPool.empty())
    {
        alDeleteBuffers(static_cast<ALsizei>(mWindBufferPool.size()),
                        mWindBufferPool.data());
        mWindBufferPool.clear();
    }
    mWindFreeBuffers.clear();

    mWindBuf.clear();

    mWindGen.reset();
}

void LLAudioEngine_OpenAL::updateWind(LLVector3 wind_vec, F32 camera_altitude)
{
    if (!mEnableWind)
        return;

    if (mWindBuf.empty() || !mWindGen)
        return;

    if (mWindUpdateTimer.checkExpirationAndReset(LL_WIND_UPDATE_INTERVAL))
    {
        // Shared Linden -> DS3D flip via the base helper. Used by
        // both OpenAL and FAudio so wind direction maps to pan
        // identically across backends.
        wind_vec = lindenToDS3DWind(wind_vec);

        F64 pitch = 1.0 + mapWindVecToPitch(wind_vec);
        F64 center_freq = 80.0 * pow(pitch, 2.5 * (mapWindVecToGain(wind_vec) + 1.0));

        // Altitude shape: muffle + attenuate underwater, subtle
        // gain lift at altitude. Same helper everyone calls so the
        // behaviour is uniform.
        const WindAltitudeShape shape = computeWindAltitudeShape(camera_altitude);

        mWindGen->mTargetFreq     = (F32)center_freq * shape.freq_mul;
        mWindGen->mTargetGain     = (F32)mapWindVecToGain(wind_vec) * mMaxWindGain * shape.gain_mul;
        mWindGen->mTargetPanGainR = (F32)mapWindVecToPan(wind_vec);

        alSourcei(mWindSource, AL_LOOPING, AL_FALSE);
        alSource3f(mWindSource, AL_POSITION, 0.0, 0.0, 0.0);
        alSource3f(mWindSource, AL_VELOCITY, 0.0, 0.0, 0.0);
        alSourcef(mWindSource, AL_ROLLOFF_FACTOR, 0.0);
        alSourcei(mWindSource, AL_SOURCE_RELATIVE, AL_TRUE);
    }

    // 1. Drain processed buffers back onto the free list. Each
    //    returns to the pool with its old contents intact — those
    //    contents will be overwritten by alBufferData before re-
    //    queueing, so what's actually in there doesn't matter.
    ALint processed = 0;
    alGetSourcei(mWindSource, AL_BUFFERS_PROCESSED, &processed);
    while (processed-- > 0)
    {
        ALuint buf = 0;
        alGetError();
        alSourceUnqueueBuffers(mWindSource, 1, &buf);
        if (alGetError() != AL_NO_ERROR || buf == 0)
        {
            break;  // mid-frame race; try again next tick
        }
        mWindFreeBuffers.push_back(buf);
    }

    // 2. Top up the queued count to kWindTargetQueueDepth from the
    //    free list. windGenerate(mWindBuf.data(), N) writes a fresh
    //    chunk of N frames each call — different output every
    //    iteration because LLWindGen advances its internal noise +
    //    filter state.
    ALint queued = 0;
    alGetSourcei(mWindSource, AL_BUFFERS_QUEUED, &queued);
    ALint in_flight = queued;  // already accounts for the just-unqueued
    while (in_flight < kWindTargetQueueDepth && !mWindFreeBuffers.empty())
    {
        const ALuint buf = mWindFreeBuffers.front();
        mWindFreeBuffers.pop_front();

        mWindGen->windGenerate(mWindBuf.data(), mWindBufSamples);

        alGetError();
        alBufferData(buf, AL_FORMAT_STEREO_FLOAT32,
                     mWindBuf.data(), mWindBufBytes, mWindBufFreq);
        if (alGetError() != AL_NO_ERROR)
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::updateWind() alBufferData failed"
                       << LL_ENDL;
            // Buffer state is suspect — put it back on the free
            // list anyway; alBufferData on a subsequent tick will
            // overwrite it cleanly.
            mWindFreeBuffers.push_back(buf);
            break;
        }

        alSourceQueueBuffers(mWindSource, 1, &buf);
        if (alGetError() != AL_NO_ERROR)
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::updateWind() alSourceQueueBuffers failed"
                       << LL_ENDL;
            mWindFreeBuffers.push_back(buf);
            break;
        }
        in_flight++;
    }

    // 3. Restart playback if the source stalled (under-run during a
    //    scheduling hiccup). Without this the wind silently stops
    //    even though the queue is healthy.
    ALint state = 0;
    alGetSourcei(mWindSource, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING && in_flight > 0)
    {
        alSourcePlay(mWindSource);
    }
}

