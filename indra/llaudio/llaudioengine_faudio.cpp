/**
 * @file llaudioengine_faudio.cpp
 * @brief Audio engine using FAudio.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "linden_common.h"

#include "llaudioengine_faudio.h"
#include "lllistener_faudio.h"
#include "llstring.h"
#include "llwavfile.h"
#include "llwindgen.h"

#include <FAudio.h>
#include <FAudioFX.h>
#include <FAPO.h>
#include <F3DAudio.h>

#include <array>
#include <cmath>
#include <sstream>

namespace
{
    constexpr uint16_t FORMAT_PCM         = 1;
    constexpr uint16_t FORMAT_IEEE_FLOAT  = 3;

    // Build a WAVEFORMATEX-style descriptor for raw PCM / IEEE float.
    FAudioWaveFormatEx make_format(uint16_t format_tag, uint16_t channels,
                                   uint32_t sample_rate, uint16_t bits_per_sample)
    {
        FAudioWaveFormatEx f{};
        f.wFormatTag      = format_tag;
        f.nChannels       = channels;
        f.nSamplesPerSec  = sample_rate;
        f.wBitsPerSample  = bits_per_sample;
        f.nBlockAlign     = static_cast<uint16_t>(channels * (bits_per_sample / 8));
        f.nAvgBytesPerSec = sample_rate * f.nBlockAlign;
        f.cbSize          = 0;
        return f;
    }

    bool formats_equal(const FAudioWaveFormatEx& a, const FAudioWaveFormatEx& b)
    {
        return a.wFormatTag == b.wFormatTag
            && a.nChannels == b.nChannels
            && a.nSamplesPerSec == b.nSamplesPerSec
            && a.wBitsPerSample == b.wBitsPerSample;
    }

    // FAudio stores device DeviceID and DisplayName as UTF-16 int16_t[256]
    // regardless of platform. int16_t and char16_t have identical layout
    // and aliasing rules for utf16 codeunits, so we reinterpret-cast and
    // hand off to llcommon's UTF-16 -> UTF-8 converter (handles
    // surrogate pairs and invalid sequences, substituting the Unicode
    // replacement char for the latter).
    std::string utf16_buf_to_utf8(const int16_t* buf, size_t cap)
    {
        // Manually find the null terminator within the cap; the FAudio
        // struct fields are fixed-size with C-string-style termination.
        size_t len = 0;
        while (len < cap && buf[len]) ++len;
        return utf16str_to_utf8str(reinterpret_cast<const char16_t*>(buf), len);
    }
    // FAudio's DeviceID field has turned out to be unstable across
    // FAudio_Release / FAudioCreate cycles on at least one user's
    // setup — same physical device, different DeviceID string across
    // swaps — so persisting it as the user's "selected device" id
    // doesn't round-trip reliably. The DisplayName is what the user
    // picks in the UI and what they expect to see preserved, and the
    // underlying platform name is stable across hot-swaps. Use the
    // display name as the engine's "id" so matching at the next init
    // resolves to the same physical device the user selected, even
    // if the FAudio-internal DeviceID has shuffled.
    std::string device_id(const FAudioDeviceDetails& d)
    { return utf16_buf_to_utf8(d.DisplayName, 256); }
    std::string device_name(const FAudioDeviceDetails& d)
    { return utf16_buf_to_utf8(d.DisplayName, 256); }

    // FAudioFXReverb's DspReverb_Create asserts (in_channels == 1 || 2 || 6)
    // and likewise for out_channels. Asserts compile out under NDEBUG, after
    // which the FAPO indexes a fixed 5-slot channel array with up to 8 — UB.
    // Pick a supported reverb-submix channel count for the given master
    // channel count; FAudio's default submix->master coefficient matrix
    // handles the conversion when the two differ (e.g. 6-ch reverb to 7.1
    // master upmixes the rears, 2-ch reverb to quad replicates fronts).
    // 0 means "no reverb supported for this configuration".
    uint16_t pick_reverb_channels(uint16_t master_channels)
    {
        if (master_channels == 0) return 0;
        if (master_channels == 1) return 1;
        if (master_channels >= 6) return 6;   // 5.1, 7.1, atmos -> 5.1 reverb
        return 2;                              // 2/3/4/5 -> stereo reverb
    }

    // Lookup I3DL2 preset by name. Case-insensitive; unknown -> PLAIN
    // with a warn so a typo'd AudioReverbPreset setting is visible in
    // the log instead of silently falling back. Each
    // FAUDIOFX_I3DL2_PRESET_* expands to a struct initialiser, so the
    // table holds them by value rather than via pointer-to-macro.
    FAudioFXReverbI3DL2Parameters pick_i3dl2_preset(const std::string& raw_name)
    {
        std::string name(raw_name);
        // std::toupper takes int and is UB for char values >0x7F on
        // platforms with signed char. Cast through unsigned char first.
        for (auto& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (name == "DEFAULT")         return FAUDIOFX_I3DL2_PRESET_DEFAULT;
        if (name == "GENERIC")         return FAUDIOFX_I3DL2_PRESET_GENERIC;
        if (name == "PADDEDCELL")      return FAUDIOFX_I3DL2_PRESET_PADDEDCELL;
        if (name == "ROOM")            return FAUDIOFX_I3DL2_PRESET_ROOM;
        if (name == "BATHROOM")        return FAUDIOFX_I3DL2_PRESET_BATHROOM;
        if (name == "LIVINGROOM")      return FAUDIOFX_I3DL2_PRESET_LIVINGROOM;
        if (name == "STONEROOM")       return FAUDIOFX_I3DL2_PRESET_STONEROOM;
        if (name == "AUDITORIUM")      return FAUDIOFX_I3DL2_PRESET_AUDITORIUM;
        if (name == "CONCERTHALL")     return FAUDIOFX_I3DL2_PRESET_CONCERTHALL;
        if (name == "CAVE")            return FAUDIOFX_I3DL2_PRESET_CAVE;
        if (name == "ARENA")           return FAUDIOFX_I3DL2_PRESET_ARENA;
        if (name == "HANGAR")          return FAUDIOFX_I3DL2_PRESET_HANGAR;
        if (name == "CARPETEDHALLWAY") return FAUDIOFX_I3DL2_PRESET_CARPETEDHALLWAY;
        if (name == "HALLWAY")         return FAUDIOFX_I3DL2_PRESET_HALLWAY;
        if (name == "STONECORRIDOR")   return FAUDIOFX_I3DL2_PRESET_STONECORRIDOR;
        if (name == "ALLEY")           return FAUDIOFX_I3DL2_PRESET_ALLEY;
        if (name == "FOREST")          return FAUDIOFX_I3DL2_PRESET_FOREST;
        if (name == "CITY")            return FAUDIOFX_I3DL2_PRESET_CITY;
        if (name == "MOUNTAINS")       return FAUDIOFX_I3DL2_PRESET_MOUNTAINS;
        if (name == "QUARRY")          return FAUDIOFX_I3DL2_PRESET_QUARRY;
        if (name == "PLAIN")           return FAUDIOFX_I3DL2_PRESET_PLAIN;
        if (name == "PARKINGLOT")      return FAUDIOFX_I3DL2_PRESET_PARKINGLOT;
        if (name == "SEWERPIPE")       return FAUDIOFX_I3DL2_PRESET_SEWERPIPE;
        if (name == "UNDERWATER")      return FAUDIOFX_I3DL2_PRESET_UNDERWATER;
        if (name == "SMALLROOM")       return FAUDIOFX_I3DL2_PRESET_SMALLROOM;
        LL_WARNS() << "Unknown I3DL2 reverb preset '" << raw_name
                   << "' — falling back to PLAIN. See settings_alchemy.xml "
                      "AudioReverbPreset for the valid names." << LL_ENDL;
        return FAUDIOFX_I3DL2_PRESET_PLAIN;
    }
}

//
// ─── Engine ──────────────────────────────────────────────────────────────
//

LLAudioEngine_FAudio::LLAudioEngine_FAudio(LLAudioEngineFAudioConfig config)
    : mConfig(std::move(config))
{
    // Defensive clamps — keep math sane on misconfigured settings.
    if (mConfig.audible_range     < 1.0f) mConfig.audible_range     = 1.0f;
    if (mConfig.inner_radius      < 0.0f) mConfig.inner_radius      = 0.0f;
    if (mConfig.reverb_send_scale < 0.0f) mConfig.reverb_send_scale = 0.0f;
}

LLAudioEngine_FAudio::~LLAudioEngine_FAudio()
{
    // If init() failed partway through after LLAudioEngine::init had
    // already allocated the listener, llstartup deletes us without
    // calling shutdown(). Drive shutdown ourselves in that case so
    // mListenerp + any partially-built FAudio resources are cleaned
    // up. shutdown() is idempotent — all its branches no-op on null
    // pointers — so it's also safe in the normal "shutdown was
    // called explicitly" path where mListenerp is already nullptr.
    if (mListenerp || mFAudio)
    {
        shutdown();
    }
}

std::vector<LLAudioOutputDevice> LLAudioEngine_FAudio::enumerateOutputDevices() const
{
    std::vector<LLAudioOutputDevice> devices;
    if (!mFAudio) return devices;
    uint32_t count = 0;
    if (FAudio_GetDeviceCount(mFAudio, &count) != 0) return devices;
    // Skip FAudio's index 0 — it's the platform's "default endpoint"
    // alias, exposed by the prefs UI as a synthetic "System default"
    // entry. Surfacing it again here as a separate item (FAudio names
    // it "Default Device" via the underlying platform layer) would
    // give the user two visually-distinct entries that pick the same
    // device, which is confusing. Specific hardware endpoints start at
    // index 1. The enumeration result is the index-1+ stretch returned
    // in caller order so callers can still use index 0 implicitly via
    // an empty preferred_device_id (which is exactly what the synthetic
    // "System default" entry does).
    if (count > 1) devices.reserve(count - 1);
    for (uint32_t i = 1; i < count; ++i)
    {
        FAudioDeviceDetails details{};
        if (FAudio_GetDeviceDetails(mFAudio, i, &details) != 0)
        {
            devices.emplace_back();  // placeholder; keeps caller-side
                                     // diagnostics distinguishable
            continue;
        }
        devices.push_back({ device_id(details), device_name(details) });
    }
    // Disambiguate devices that share a display name. Windows can hand
    // back duplicate strings (USB DAC + onboard line-out both reported
    // as "Speakers" on some setups) and the FAudio backend uses display
    // name as the persistence id — without disambiguation the two
    // collide on id and "pick BlackShark" might resolve to either. Walk
    // the list and append a 1-based ordinal suffix to names that occur
    // more than once. Mirroring the change into id keeps the round-trip
    // stable.
    std::map<std::string, int> name_seen;
    for (auto& d : devices)
    {
        if (d.name.empty()) continue;
        ++name_seen[d.name];
    }
    std::map<std::string, int> name_order;
    for (auto& d : devices)
    {
        if (d.name.empty()) continue;
        if (name_seen[d.name] <= 1) continue;
        const int n = ++name_order[d.name];
        const std::string suffix = " (" + std::to_string(n) + ")";
        d.name += suffix;
        d.id   += suffix;
    }
    return devices;
}

// Wind voice callbacks. Dispatch table sets these on the wind callback
// struct; FAudio invokes them on its mixer thread when buffers complete.
// The wind PCM chunks are owned by the engine (mWindChunks) rather than
// the callback — FAudio's flush-then-destroy path drops OnBufferEnd
// events on the floor, which would have leaked any callback-owned heap.
// Here we only signal completion via the atomic counter; the main thread
// drains the deque on its next pass.
static void FAUDIOCALL wind_on_pass_start(FAudioVoiceCallback*, uint32_t) {}
static void FAUDIOCALL wind_on_pass_end(FAudioVoiceCallback*) {}
static void FAUDIOCALL wind_on_stream_end(FAudioVoiceCallback*) {}
static void FAUDIOCALL wind_on_buffer_start(FAudioVoiceCallback*, void*) {}
static void FAUDIOCALL wind_on_buffer_end(FAudioVoiceCallback* cb, void*)
{
    auto* self = static_cast<LLAudioEngine_FAudio::WindVoiceCallback*>(cb);
    if (self && self->engine)
    {
        self->engine->mWindQueueDepth.fetch_sub(1, std::memory_order_relaxed);
    }
}
static void FAUDIOCALL wind_on_loop_end(FAudioVoiceCallback*, void*) {}
static void FAUDIOCALL wind_on_voice_error(FAudioVoiceCallback*, void*, uint32_t) {}

bool LLAudioEngine_FAudio::init(void* userdata, const std::string& app_title)
{
    // Parent init creates the listener via allocateListener().
    LLAudioEngine::init(userdata, app_title);
    return initFAudioDevice();
}

bool LLAudioEngine_FAudio::initFAudioDevice()
{
    uint32_t hr = FAudioCreate(&mFAudio, 0, FAUDIO_DEFAULT_PROCESSOR);
    if (hr != 0 || !mFAudio)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::initFAudioDevice() FAudioCreate failed: 0x"
                   << std::hex << hr << std::dec << LL_ENDL;
        return false;
    }

    // Resolve preferred device id -> FAudio device index. Default (0)
    // when id is empty or no match. Record both id and display name of
    // what we actually opened.
    //
    // Skip index 0 in the lookup. enumerateOutputDevices already hides
    // it (the platform "default endpoint" alias is represented by the
    // synthetic "System default" entry in the UI, with empty
    // preferred_device_id). Without this skip, a user whose preferred
    // id happens to match index 0's display name (e.g. "Speakers" on a
    // system where that's both the default endpoint label and a real
    // device's name) would resolve to the default-alias instead of the
    // specific hardware endpoint.
    uint32_t device_index = 0;
    mActiveDeviceId.clear();
    mActiveDeviceName.clear();
    if (!mConfig.preferred_device_id.empty())
    {
        uint32_t count = 0;
        FAudio_GetDeviceCount(mFAudio, &count);
        // The display-name disambiguation in enumerateOutputDevices
        // appends " (N)" suffixes to names that occur more than once,
        // mirroring the change into the persisted id. Reproduce the
        // same suffix scheme during lookup so a saved disambiguated
        // id round-trips to the matching index.
        std::map<std::string, int> name_seen;
        for (uint32_t i = 1; i < count; ++i)
        {
            FAudioDeviceDetails d{};
            if (FAudio_GetDeviceDetails(mFAudio, i, &d) != 0) continue;
            ++name_seen[device_name(d)];
        }
        std::map<std::string, int> name_order;
        for (uint32_t i = 1; i < count; ++i)
        {
            FAudioDeviceDetails details{};
            if (FAudio_GetDeviceDetails(mFAudio, i, &details) != 0) continue;
            std::string id = device_id(details);
            std::string name = device_name(details);
            if (name_seen[name] > 1)
            {
                const int n = ++name_order[name];
                const std::string suffix = " (" + std::to_string(n) + ")";
                id += suffix;
                name += suffix;
            }
            if (id == mConfig.preferred_device_id)
            {
                device_index = i;
                mActiveDeviceId = id;
                mActiveDeviceName = name;
                break;
            }
        }
        // Disambiguation-stickiness fallback. If the saved id ends in
        // a " (N)" digit suffix (it was disambiguated because the user
        // had two devices sharing a display name at save time) but no
        // exact match was found, strip the suffix and try matching the
        // base name. This handles the case where the duplicate device
        // has since been unplugged and the surviving same-named device
        // is reported un-disambiguated; without the fallback the user's
        // saved pick fails to resolve and we silently fall through to
        // system default.
        if (mActiveDeviceId.empty())
        {
            std::string base = mConfig.preferred_device_id;
            const std::size_t open = base.rfind(" (");
            if (open != std::string::npos && !base.empty() && base.back() == ')')
            {
                const std::size_t num_start = open + 2;
                const std::size_t num_end = base.size() - 1;
                if (num_end > num_start)
                {
                    bool all_digits = true;
                    for (std::size_t p = num_start; p < num_end; ++p)
                    {
                        if (!std::isdigit(static_cast<unsigned char>(base[p])))
                        {
                            all_digits = false;
                            break;
                        }
                    }
                    if (all_digits)
                    {
                        base = base.substr(0, open);
                    }
                }
            }
            if (base != mConfig.preferred_device_id)
            {
                for (uint32_t i = 1; i < count; ++i)
                {
                    FAudioDeviceDetails details{};
                    if (FAudio_GetDeviceDetails(mFAudio, i, &details) != 0) continue;
                    const std::string name = device_name(details);
                    if (name == base)
                    {
                        device_index = i;
                        mActiveDeviceId = name;  // un-suffixed; the
                                                 // saved id will heal
                                                 // on next persist.
                        mActiveDeviceName = name;
                        LL_INFOS() << "LLAudioEngine_FAudio::initFAudioDevice() "
                                      "preferred id '" << mConfig.preferred_device_id
                                   << "' not found exactly; matched base name '"
                                   << base << "' (duplicate disambiguation no "
                                      "longer needed)" << LL_ENDL;
                        break;
                    }
                }
            }
        }
        if (mActiveDeviceId.empty())
        {
            LL_INFOS() << "LLAudioEngine_FAudio::initFAudioDevice() preferred "
                          "output device id '" << mConfig.preferred_device_id
                       << "' not found; using system default." << LL_ENDL;
        }
    }
    if (mActiveDeviceId.empty())
    {
        // Resolve the system default's id + display name. Note we keep
        // mActiveDeviceId empty when we fell back to the default (no
        // explicit pick), so the UI sees "no preference active" rather
        // than the default's incidental id.
        FAudioDeviceDetails details{};
        if (FAudio_GetDeviceDetails(mFAudio, 0, &details) == 0)
        {
            mActiveDeviceName = device_name(details);
        }
    }

    hr = FAudio_CreateMasteringVoice(mFAudio, &mMasterVoice,
                                     FAUDIO_DEFAULT_CHANNELS,
                                     FAUDIO_DEFAULT_SAMPLERATE,
                                     0, device_index, nullptr);
    if (hr != 0 || !mMasterVoice)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::init() CreateMasteringVoice failed: 0x"
                   << std::hex << hr << std::dec << LL_ENDL;
        FAudio_Release(mFAudio);
        mFAudio = nullptr;
        return false;
    }

    FAudioVoiceDetails details{};
    FAudioVoice_GetVoiceDetails(mMasterVoice, &details);
    mSampleRate     = details.InputSampleRate;
    mOutputChannels = static_cast<uint16_t>(details.InputChannels);

    // Some platform layers (notably SDL3 on Windows when the requested
    // endpoint is also the OS-default and held by another role) can
    // return success from CreateMasteringVoice for a specific device
    // index but hand back a voice with zero channels / zero sample
    // rate — the voice is technically alive but produces no audio.
    // Detect that here and fall back to the system default (index 0),
    // which the same platform reliably opens.
    if (device_index != 0
        && (mOutputChannels == 0 || mSampleRate == 0))
    {
        LL_WARNS() << "LLAudioEngine_FAudio::initFAudioDevice() device index "
                   << device_index << " (id '" << mActiveDeviceId
                   << "') opened with invalid format ("
                   << mOutputChannels << "ch @ " << mSampleRate
                   << "Hz). Falling back to system default endpoint."
                   << LL_ENDL;
        FAudioVoice_DestroyVoice(mMasterVoice);
        mMasterVoice = nullptr;
        mActiveDeviceId.clear();
        mActiveDeviceName.clear();
        hr = FAudio_CreateMasteringVoice(mFAudio, &mMasterVoice,
                                         FAUDIO_DEFAULT_CHANNELS,
                                         FAUDIO_DEFAULT_SAMPLERATE,
                                         0, 0, nullptr);
        if (hr != 0 || !mMasterVoice)
        {
            LL_WARNS() << "LLAudioEngine_FAudio::initFAudioDevice() default "
                          "endpoint fallback also failed: 0x"
                       << std::hex << hr << std::dec << LL_ENDL;
            FAudio_Release(mFAudio);
            mFAudio = nullptr;
            return false;
        }
        FAudioVoice_GetVoiceDetails(mMasterVoice, &details);
        mSampleRate     = details.InputSampleRate;
        mOutputChannels = static_cast<uint16_t>(details.InputChannels);
        // Record what we actually opened — the default endpoint's
        // human-readable name for the driver-name log + UI.
        FAudioDeviceDetails ddet{};
        if (FAudio_GetDeviceDetails(mFAudio, 0, &ddet) == 0)
        {
            mActiveDeviceName = device_name(ddet);
        }
    }

    // Re-apply the cached master gain. The new mastering voice opens
    // at FAudio's default unity, but the base class's mInternalGain
    // holds whatever value the user's master-volume slider has been
    // set to (and whether mute is on). Without this re-apply, a hot-
    // swap mid-session would reset the master volume to 1.0 and stay
    // there until the user next moved the slider. The -1.0f sentinel
    // (from setDefaults() before the first setMasterGain call lands)
    // is preserved by the range check so first-init isn't disturbed.
    if (mInternalGain >= 0.f && mInternalGain <= 1.f)
    {
        FAudioVoice_SetVolume(mMasterVoice, mInternalGain, FAUDIO_COMMIT_NOW);
    }

    // F3DAudio needs the output speaker mask to know geometry.
    uint32_t channel_mask = 0;
    FAudioMasteringVoice_GetChannelMask(mMasterVoice, &channel_mask);
    if (channel_mask == 0)
    {
        // Fallback when the backend doesn't report a mask: derive a sane one
        // from the channel count.
        switch (mOutputChannels)
        {
            case 1: channel_mask = SPEAKER_MONO; break;
            case 4: channel_mask = SPEAKER_QUAD; break;
            case 6: channel_mask = SPEAKER_5POINT1; break;
            case 8: channel_mask = SPEAKER_7POINT1; break;
            case 2: default: channel_mask = SPEAKER_STEREO; break;
        }
    }
    // X3DAudio's canonical speed of sound (m/s); the macro lives in the
    // proprietary XAudio2 SDK but F3DAudio leaves it to the caller.
    constexpr float kSpeedOfSound = 343.5f;
    F3DAudioInitialize(channel_mask, kSpeedOfSound, mX3DInstance);

    // One submix per audio type — per-type secondary gain maps to a single
    // SetVolume call on the submix, and the per-source SetVolume only needs
    // to apply the source's own gain. Submixes route to the mastering voice
    // by default. Seed each submix from the base engine's stored gain so a
    // slider change made before init() is honored.
    for (S32 i = 0; i < LLAudioEngine::AUDIO_TYPE_COUNT; ++i)
    {
        hr = FAudio_CreateSubmixVoice(mFAudio, &mGroupVoices[i],
                                      mOutputChannels, mSampleRate,
                                      0, 0, nullptr, nullptr);
        if (hr != 0 || !mGroupVoices[i])
        {
            LL_WARNS() << "LLAudioEngine_FAudio::init() CreateSubmixVoice("
                       << i << ") failed: 0x" << std::hex << hr << std::dec << LL_ENDL;
            // Non-fatal: source voices will fall back to the mastering voice.
            // Those voices then need to apply secondary gain per-source.
            mGroupVoices[i] = nullptr;
            continue;
        }
        FAudioVoice_SetVolume(mGroupVoices[i], mSecondaryGain[i], FAUDIO_COMMIT_NOW);
    }

    // Reverb submix. Holds an FAudioFXReverb FAPO; source voices send a
    // wet signal here based on F3DAudio's per-emitter ReverbLevel. Output
    // routes back into the mastering voice (default null pSendList).
    // Configured with the I3DL2 PLAIN preset — a fairly open outdoor
    // ambience, the closest match for the SL world feel. Non-fatal if
    // creation fails: channels will just route to the type submix
    // without a reverb send and the world sounds normal-but-dry.
    //
    // The reverb FAPO supports only 1/2/6-channel I/O. For other master
    // channel counts (quad/3/5/7.1/atmos) we still get reverb by creating
    // the submix at the closest supported size; FAudio's default
    // submix->master coefficient matrix handles the channel-count
    // conversion when reverb output mixes back into the master.
    mReverbChannels = pick_reverb_channels(mOutputChannels);
    if (mReverbChannels == 0)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::init() reverb disabled (no "
                      "supported reverb channel count for "
                   << mOutputChannels << "-ch output)." << LL_ENDL;
    }
    else if (FAudioCreateReverb(&mReverbApo, 0) != 0 || !mReverbApo)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::init() FAudioCreateReverb "
                   "failed — reverb disabled" << LL_ENDL;
        mReverbApo = nullptr;
    }
    else
    {
        FAudioEffectDescriptor reverb_desc{};
        reverb_desc.pEffect = mReverbApo;
        reverb_desc.InitialState = 1;
        reverb_desc.OutputChannels = mReverbChannels;

        FAudioEffectChain reverb_chain{};
        reverb_chain.EffectCount = 1;
        reverb_chain.pEffectDescriptors = &reverb_desc;

        hr = FAudio_CreateSubmixVoice(mFAudio, &mReverbVoice,
                                      mReverbChannels, mSampleRate,
                                      0, 0, nullptr, &reverb_chain);
        if (hr == 0 && mReverbVoice)
        {
            FAudioFXReverbI3DL2Parameters i3dl2 = pick_i3dl2_preset(mConfig.reverb_preset);
            FAudioFXReverbParameters native{};
            ReverbConvertI3DL2ToNative(&i3dl2, &native);
            FAudioVoice_SetEffectParameters(mReverbVoice, 0,
                                            &native, sizeof(native),
                                            FAUDIO_COMMIT_NOW);
            // FAudio's effect-chain attach AddRefs the FAPO when it's
            // added to the submix and Releases on DestroyVoice (verified
            // against FAudio 24.x source — function names and line
            // numbers intentionally not cited because internal symbol
            // names shift with FAudio versions). Our reference from
            // FAudioCreateReverb is now redundant — drop it so the
            // only live count is held by the submix, otherwise the
            // FAPO outlives the submix and leaks at shutdown.
            mReverbApo->Release(mReverbApo);
            mReverbApo = nullptr;
            if (mReverbChannels != mOutputChannels)
            {
                LL_INFOS() << "LLAudioEngine_FAudio::init() reverb running at "
                           << mReverbChannels << "ch, mixing into "
                           << mOutputChannels << "ch master." << LL_ENDL;
            }
        }
        else
        {
            LL_WARNS() << "LLAudioEngine_FAudio::init() reverb submix "
                       "creation failed: 0x" << std::hex << hr << std::dec
                       << " — reverb disabled" << LL_ENDL;
            // CreateSubmixVoice failed; the FAPO is unowned. Release it
            // manually so we don't leak the heap allocation.
            mReverbApo->Release(mReverbApo);
            mReverbApo = nullptr;
            mReverbVoice = nullptr;
            mReverbChannels = 0;
        }
    }

    LL_INFOS() << "LLAudioEngine_FAudio::init() FAudio "
               << ((FAudioLinkedVersion() >> 16) & 0xFF) << "."
               << ((FAudioLinkedVersion() >>  8) & 0xFF) << "."
               <<  (FAudioLinkedVersion()        & 0xFF)
               << " — " << mOutputChannels << "ch @ " << mSampleRate << "Hz" << LL_ENDL;

    return true;
}

std::string LLAudioEngine_FAudio::getDriverName(bool verbose)
{
    if (!verbose) return "FAudio";

    std::ostringstream o;
    uint32_t v = FAudioLinkedVersion();
    o << "FAudio " << ((v >> 16) & 0xFF) << "." << ((v >> 8) & 0xFF) << "." << (v & 0xFF);

    // Prefer the name we resolved at init() if available — that's the
    // device the mastering voice is actually running on, which may
    // differ from index 0 when the user picked a non-default device.
    if (!mActiveDeviceName.empty())
    {
        o << " — " << mActiveDeviceName;
    }

    if (mOutputChannels)
    {
        o << " (" << mOutputChannels << "ch @ " << mSampleRate << "Hz)";
    }
    return o.str();
}

void LLAudioEngine_FAudio::allocateListener()
{
    mListenerp = new LLListener_FAudio(this);
}

void LLAudioEngine_FAudio::shutdown()
{
    LL_INFOS() << "LLAudioEngine_FAudio::shutdown()" << LL_ENDL;

    cleanupWind();

    // Parent shutdown releases sources/buffers/channels, which in turn
    // destroy their FAudio source voices via LLAudioChannelFAudio::cleanup().
    LLAudioEngine::shutdown();

    // Reverb submix owns the FAPO via its effect chain; DestroyVoice
    // releases the FAPO. We still null mReverbApo to avoid a stale
    // pointer in case shutdown is re-entered.
    if (mReverbVoice)
    {
        FAudioVoice_DestroyVoice(mReverbVoice);
        mReverbVoice = nullptr;
    }
    mReverbApo = nullptr;
    mReverbChannels = 0;

    for (auto*& voice : mGroupVoices)
    {
        if (voice)
        {
            FAudioVoice_DestroyVoice(voice);
            voice = nullptr;
        }
    }

    if (mMasterVoice)
    {
        FAudioVoice_DestroyVoice(mMasterVoice);
        mMasterVoice = nullptr;
    }

    if (mFAudio)
    {
        FAudio_Release(mFAudio);
        mFAudio = nullptr;
    }

    delete mListenerp;
    mListenerp = nullptr;
}

void LLAudioEngine_FAudio::releaseFAudioDevice()
{
    // Drop every channel's source voice first so the mixer thread isn't
    // racing dying voices. prepareForDeviceReset captures playback frame
    // before tearing down so the next updateBuffer resumes mid-stream.
    // mCurrentBufferp / mCurrentSourcep are preserved.
    for (size_t i = 0; i < mChannels.size(); ++i)
    {
        if (mChannels[i])
        {
            static_cast<LLAudioChannelFAudio*>(mChannels[i])->prepareForDeviceReset();
        }
    }

    if (mWindVoice)
    {
        FAudioSourceVoice_Stop(mWindVoice, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(mWindVoice);
        FAudioVoice_DestroyVoice(mWindVoice);
        mWindVoice = nullptr;
    }
    mWindQueueDepth = 0;
    mWindChunks.clear();

    if (mReverbVoice)
    {
        FAudioVoice_DestroyVoice(mReverbVoice);
        mReverbVoice = nullptr;
    }
    mReverbApo = nullptr;
    mReverbChannels = 0;

    for (auto*& voice : mGroupVoices)
    {
        if (voice)
        {
            FAudioVoice_DestroyVoice(voice);
            voice = nullptr;
        }
    }

    if (mMasterVoice)
    {
        FAudioVoice_DestroyVoice(mMasterVoice);
        mMasterVoice = nullptr;
    }

    if (mFAudio)
    {
        FAudio_Release(mFAudio);
        mFAudio = nullptr;
    }

    mOutputChannels = 0;
    mSampleRate = 0;
    mActiveDeviceId.clear();
    mActiveDeviceName.clear();
}

void LLAudioEngine_FAudio::setOutputDevice(const std::string& id)
{
    if (id == mConfig.preferred_device_id && mFAudio)
    {
        // Same device and we're already running — nothing to do.
        return;
    }

    LL_INFOS() << "LLAudioEngine_FAudio::setOutputDevice() switching to id '"
               << (id.empty() ? "<system default>" : id.c_str())
               << "' (from '"
               << (mActiveDeviceName.empty() ? "<unknown>" : mActiveDeviceName.c_str())
               << "')" << LL_ENDL;

    mConfig.preferred_device_id = id;

    // Snapshot wind state. We carry mWindGen across the swap when the
    // sample rate didn't change, so synthesis stays continuous; if it
    // did change, initWind below recreates the generator at the new
    // rate to avoid the FAudio resampler trying to stitch across a
    // format mismatch (a known cause of intermittent silent-wind on
    // hot-swap).
    const bool had_wind = mEnableWind && mWindGen != nullptr;

    releaseFAudioDevice();

    if (!initFAudioDevice())
    {
        LL_WARNS() << "LLAudioEngine_FAudio::setOutputDevice() failed to open "
                      "device id '" << id << "' — audio silent until another "
                      "device is selected." << LL_ENDL;
        return;
    }
    LL_INFOS() << "LLAudioEngine_FAudio::setOutputDevice() opened '"
               << mActiveDeviceName << "' (" << mOutputChannels << "ch @ "
               << mSampleRate << "Hz)" << LL_ENDL;

    if (had_wind)
    {
        if (initWind())
        {
            LL_INFOS() << "LLAudioEngine_FAudio::setOutputDevice() wind voice "
                          "re-attached at " << mWindBufFreq << "Hz" << LL_ENDL;
        }
        else
        {
            LL_WARNS() << "LLAudioEngine_FAudio::setOutputDevice() wind voice "
                          "re-attach failed; wind silent until re-init."
                       << LL_ENDL;
        }
    }

    // Tell any subscribed UIs (Sound prefs combo) that the active
    // device may have shifted so they can refresh their display.
    mDevicesChangedSignal();
}

FAudioSubmixVoice* LLAudioEngine_FAudio::getGroupVoice(S32 type) const
{
    if (type < 0 || type >= LLAudioEngine::AUDIO_TYPE_COUNT) return nullptr;
    return mGroupVoices[type];
}

void LLAudioEngine_FAudio::releaseBufferReferences(LLAudioBufferFAudio* buf)
{
    if (!buf) return;
    for (size_t i = 0; i < mChannels.size(); ++i)
    {
        if (!mChannels[i]) continue;
        static_cast<LLAudioChannelFAudio*>(mChannels[i])->releaseIfReferencing(buf);
    }
}

void LLAudioEngine_FAudio::setInternalGain(F32 gain)
{
    if (mMasterVoice)
    {
        FAudioVoice_SetVolume(mMasterVoice, gain, FAUDIO_COMMIT_NOW);
    }
}

void LLAudioEngine_FAudio::setAudibleRange(float meters)
{
    if (meters < 1.0f) meters = 1.0f;
    mConfig.audible_range = meters;
}

void LLAudioEngine_FAudio::setInnerRadius(float meters)
{
    if (meters < 0.0f) meters = 0.0f;
    mConfig.inner_radius = meters;
}

void LLAudioEngine_FAudio::setReverbSendScale(float scale)
{
    if (scale < 0.0f) scale = 0.0f;
    mConfig.reverb_send_scale = scale;
}

void LLAudioEngine_FAudio::setReverbPreset(const std::string& preset_name)
{
    mConfig.reverb_preset = preset_name;
    if (!mReverbVoice) return;
    FAudioFXReverbI3DL2Parameters i3dl2 = pick_i3dl2_preset(preset_name);
    FAudioFXReverbParameters native{};
    ReverbConvertI3DL2ToNative(&i3dl2, &native);
    FAudioVoice_SetEffectParameters(mReverbVoice, 0,
                                    &native, sizeof(native),
                                    FAUDIO_COMMIT_NOW);
}

void LLAudioEngine_FAudio::setSecondaryGain(S32 type, F32 gain)
{
    LLAudioEngine::setSecondaryGain(type, gain);
    if (type >= 0 && type < LLAudioEngine::AUDIO_TYPE_COUNT
        && mGroupVoices[type])
    {
        FAudioVoice_SetVolume(mGroupVoices[type], gain, FAUDIO_COMMIT_NOW);
    }
}

LLAudioBuffer* LLAudioEngine_FAudio::createBuffer()
{
    return new LLAudioBufferFAudio();
}

LLAudioChannel* LLAudioEngine_FAudio::createChannel()
{
    return new LLAudioChannelFAudio(this);
}

//
// ─── Wind ────────────────────────────────────────────────────────────────
//

bool LLAudioEngine_FAudio::initWind()
{
    if (!mFAudio || mSampleRate == 0)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::initWind() preconditions not met "
                      "(mFAudio=" << (mFAudio ? "alive" : "null")
                   << ", mSampleRate=" << mSampleRate << ")" << LL_ENDL;
        return false;
    }

    // Allocate (or re-allocate) the wind generator. We carry the
    // generator across device swaps so the filter / smoothing state
    // stays continuous, but only when the device's sample rate didn't
    // change — if it did, the wind voice would be created at the new
    // master's rate while the generator emits at the old rate, leaving
    // FAudio's resampler stitching across the mismatch on every chunk.
    // That's been the source of intermittent silent-wind cases on
    // hot-swap. Recreating mWindGen at the new rate trades the carry-
    // over smoothing state for a guaranteed format match.
    const bool fresh_gen = !mWindGen ||
                            mWindGen->getInputSamplingRate() != mSampleRate;
    if (fresh_gen)
    {
        mWindGen = std::make_unique<LLWindGen<WIND_SAMPLE_T>>(mSampleRate);
        mWindBufFreq    = mWindGen->getInputSamplingRate();
        mWindBufSamples = llceil(mWindBufFreq * WIND_BUFFER_SIZE_SEC);
        // Cold-start: voice begins silent and updateWind ramps it to
        // unity over ~300 ms, masking the synthesis ramp-up. Across a
        // sample-rate-changing hot-swap we also reset the fade because
        // the new generator's first samples are silence-from-zero.
        mWindFadeIn = 0.0f;
    }
    // else: mWindFadeIn keeps whatever value it had on the prior
    // device. A settled (mWindFadeIn==1.0) voice resumes at unity;
    // an in-flight fade resumes where it left off.

    mWindCallback.OnVoiceProcessingPassStart = &wind_on_pass_start;
    mWindCallback.OnVoiceProcessingPassEnd   = &wind_on_pass_end;
    mWindCallback.OnStreamEnd                = &wind_on_stream_end;
    mWindCallback.OnBufferStart              = &wind_on_buffer_start;
    mWindCallback.OnBufferEnd                = &wind_on_buffer_end;
    mWindCallback.OnLoopEnd                  = &wind_on_loop_end;
    mWindCallback.OnVoiceError               = &wind_on_voice_error;
    mWindCallback.engine                     = this;

    FAudioWaveFormatEx fmt = make_format(FORMAT_IEEE_FLOAT, 2, mWindBufFreq, 32);

    uint32_t hr = FAudio_CreateSourceVoice(mFAudio, &mWindVoice, &fmt,
                                         0, FAUDIO_DEFAULT_FREQ_RATIO,
                                         &mWindCallback, nullptr, nullptr);
    if (hr != 0 || !mWindVoice)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::initWind() CreateSourceVoice failed: 0x"
                   << std::hex << hr << std::dec << LL_ENDL;
        if (fresh_gen)
        {
            // We allocated mWindGen earlier in this call; tear it down
            // so the engine returns to its pre-init state. Re-entry
            // paths (device swap) leave the pre-existing generator
            // alone so a transient device failure doesn't lose the
            // synthesis state.
            mWindGen.reset();
        }
        return false;
    }

    // Set the FAudio voice volume to whatever fade state mWindFadeIn is
    // already in: 0 on a cold start (ramps to unity over ~300 ms in
    // updateWind, masking the click against the silent baseline) or
    // the prior value across a device swap (no audible ramp on resume).
    FAudioVoice_SetVolume(mWindVoice, mWindFadeIn, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_Start(mWindVoice, 0, FAUDIO_COMMIT_NOW);
    return true;
}

void LLAudioEngine_FAudio::cleanupWind()
{
    if (mWindVoice)
    {
        FAudioSourceVoice_Stop(mWindVoice, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(mWindVoice);
        FAudioVoice_DestroyVoice(mWindVoice);
        mWindVoice = nullptr;
    }
    mWindQueueDepth = 0;
    // DestroyVoice removed the source from FAudio's graph; the mixer can
    // no longer touch our chunks. Safe to clear regardless of whether
    // OnBufferEnd ever fired for them (FAudio's flush path can drop
    // pending OnBufferEnd events on the floor when the voice is destroyed
    // before the mixer drains flush_buffers).
    mWindChunks.clear();

    mWindGen.reset();
}

void LLAudioEngine_FAudio::updateWind(LLVector3 wind_vec, F32 camera_altitude)
{
    LL_PROFILE_ZONE_SCOPED;

    if (!mEnableWind || !mWindGen || !mWindVoice) return;

    // Drain consumed chunks from the front of mWindChunks. FAudio fires
    // OnBufferEnd in submission (FIFO) order, decrementing mWindQueueDepth;
    // our deque size mirrors that.
    {
        const int depth = mWindQueueDepth.load(std::memory_order_relaxed);
        while (static_cast<int>(mWindChunks.size()) > depth)
        {
            mWindChunks.pop_front();
        }
    }

    if (mWindFadeIn < 1.0f)
    {
        mWindFadeIn = std::min(1.0f, mWindFadeIn + kWindFadeInPerFrame);
        FAudioVoice_SetVolume(mWindVoice, mWindFadeIn, FAUDIO_COMMIT_NOW);
    }

    if (mWindUpdateTimer.checkExpirationAndReset(LL_WIND_UPDATE_INTERVAL))
    {
        // Linden -> DS3D flip shared with the OpenAL backend via the
        // base helper so wind direction maps to pan identically across
        // both engines.
        wind_vec = lindenToDS3DWind(wind_vec);

        F64 pitch       = 1.0 + mapWindVecToPitch(wind_vec);
        F64 center_freq = 80.0 * pow(pitch, 2.5 * (mapWindVecToGain(wind_vec) + 1.0));

        // Altitude shape: muffle the resonator + attenuate gain
        // when the camera is underwater; subtle gain lift when high
        // above water. Shape is computed once per coefficient update,
        // not per audio sample.
        const WindAltitudeShape shape = computeWindAltitudeShape(camera_altitude);

        mWindGen->mTargetFreq     = (F32)center_freq * shape.freq_mul;
        mWindGen->mTargetGain     = (F32)mapWindVecToGain(wind_vec) * mMaxWindGain * shape.gain_mul;
        mWindGen->mTargetPanGainR = (F32)mapWindVecToPan(wind_vec);
    }

    // Top up the queue so FAudio always has at least MAX_WIND_QUEUED chunks
    // pending. Chunks are owned by mWindChunks (FIFO); FAudio reads them
    // in place and signals completion via OnBufferEnd.
    int depth = mWindQueueDepth.load(std::memory_order_relaxed);
    while (depth < MAX_WIND_QUEUED)
    {
        mWindChunks.emplace_back(mWindBufSamples * 2 /*stereo*/, 0.f);
        auto& chunk = mWindChunks.back();
        mWindGen->windGenerate(chunk.data(), mWindBufSamples);

        FAudioBuffer buf{};
        buf.AudioBytes = static_cast<uint32_t>(chunk.size() * sizeof(WIND_SAMPLE_T));
        buf.pAudioData = reinterpret_cast<const uint8_t*>(chunk.data());
        buf.Flags      = 0;  // wind is continuous, never end-of-stream
        buf.LoopCount  = 0;
        buf.pContext   = nullptr;

        mWindQueueDepth.fetch_add(1, std::memory_order_relaxed);

        uint32_t hr = FAudioSourceVoice_SubmitSourceBuffer(mWindVoice, &buf, nullptr);
        if (hr != 0)
        {
            LL_WARNS() << "LLAudioEngine_FAudio::updateWind() SubmitSourceBuffer failed: 0x"
                       << std::hex << hr << std::dec << LL_ENDL;
            mWindChunks.pop_back();
            mWindQueueDepth.fetch_sub(1, std::memory_order_relaxed);
            break;
        }
        depth++;
    }
}

//
// ─── Channel ─────────────────────────────────────────────────────────────
//

static void FAUDIOCALL channel_on_pass_start(FAudioVoiceCallback*, uint32_t) {}
static void FAUDIOCALL channel_on_pass_end(FAudioVoiceCallback*) {}
static void FAUDIOCALL channel_on_stream_end(FAudioVoiceCallback* cb)
{
    auto* self = static_cast<LLAudioChannelFAudio::ChannelCallback*>(cb);
    if (self && self->owner)
    {
        self->owner->mFinished.store(true, std::memory_order_relaxed);
    }
}
static void FAUDIOCALL channel_on_buffer_start(FAudioVoiceCallback*, void*) {}
static void FAUDIOCALL channel_on_buffer_end(FAudioVoiceCallback*, void*) {}
static void FAUDIOCALL channel_on_loop_end(FAudioVoiceCallback* cb, void*)
{
    auto* self = static_cast<LLAudioChannelFAudio::ChannelCallback*>(cb);
    if (self && self->owner)
    {
        self->owner->mLoopCount.fetch_add(1, std::memory_order_relaxed);
    }
}
static void FAUDIOCALL channel_on_voice_error(FAudioVoiceCallback*, void*, uint32_t) {}

LLAudioChannelFAudio::LLAudioChannelFAudio(LLAudioEngine_FAudio* engine)
    : mEnginep(engine)
{
    mCallback.OnVoiceProcessingPassStart = &channel_on_pass_start;
    mCallback.OnVoiceProcessingPassEnd   = &channel_on_pass_end;
    mCallback.OnStreamEnd                = &channel_on_stream_end;
    mCallback.OnBufferStart              = &channel_on_buffer_start;
    mCallback.OnBufferEnd                = &channel_on_buffer_end;
    mCallback.OnLoopEnd                  = &channel_on_loop_end;
    mCallback.OnVoiceError               = &channel_on_voice_error;
    mCallback.owner                      = this;
}

LLAudioChannelFAudio::~LLAudioChannelFAudio()
{
    destroyVoice();
}

void LLAudioChannelFAudio::destroyVoice()
{
    if (mVoice)
    {
        FAudioSourceVoice_Stop(mVoice, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(mVoice);
        FAudioVoice_DestroyVoice(mVoice);
        mVoice = nullptr;
    }
    mDestVoice = nullptr;
    mRoutedThroughGroup = false;
    mStarted = false;
    mFinished = false;
    mLoopCount = 0;
    mObservedLoopCount = 0;
    mSmoothedDoppler = 1.0f;
    // A freshly-built voice's filter defaults to Frequency=1.0 (passthrough),
    // so reset our tracking to match. Otherwise the first run_f3d after a
    // voice rebuild would see a non-passthrough mLastAppliedLpf and force a
    // SetFilterParameters call before the smoother has any signal.
    mSmoothedLpf = 1.0f;
    mLastAppliedLpf = 1.0f;
}

bool LLAudioChannelFAudio::ensureVoice(const FAudioWaveFormatEx& fmt)
{
    if (!mEnginep->getFAudio())
    {
        // No live FAudio engine — nothing we can build a voice on. Caller
        // (updateBuffer) will return false and the channel stays silent.
        return false;
    }

    // Compute what the routing SHOULD be for this source. The short-circuit
    // below has to reject not only format mismatches but also routing
    // mismatches — a source whose type changed while the channel is reused
    // would otherwise keep playing through the old type's submix.
    FAudioSubmixVoice* desired_group = nullptr;
    if (mCurrentSourcep)
    {
        desired_group = mEnginep->getGroupVoice(mCurrentSourcep->getType());
    }
    FAudioVoice* desired_dest = desired_group
        ? reinterpret_cast<FAudioVoice*>(desired_group)
        : reinterpret_cast<FAudioVoice*>(mEnginep->getMasterVoice());
    const bool desired_through_group = (desired_group != nullptr);

    if (mVoice
        && formats_equal(mFormat, fmt)
        && mDestVoice == desired_dest
        && mRoutedThroughGroup == desired_through_group)
    {
        return true;
    }
    destroyVoice();

    // Two-entry send list: slot 0 is the dry mix path (per-type submix
    // when available, otherwise the mastering voice as a fallback);
    // slot 1 routes a wet send into the reverb submix when reverb is
    // available. The reverb send level is controlled per-frame from
    // F3DAudio's dsp.ReverbLevel — initialised to silence below so the
    // voice isn't audibly fed into reverb before the first apply3D.
    FAudioVoiceSends sends{};
    FAudioSendDescriptor send_list[2]{};
    int num_sends = 0;

    send_list[num_sends].Flags = 0;
    send_list[num_sends].pOutputVoice = desired_dest;
    num_sends++;

    FAudioSubmixVoice* reverb = mEnginep->getReverbVoice();
    if (reverb)
    {
        send_list[num_sends].Flags = 0;
        send_list[num_sends].pOutputVoice = reinterpret_cast<FAudioVoice*>(reverb);
        num_sends++;
    }
    sends.SendCount = num_sends;
    sends.pSends = send_list;

    // FAUDIO_VOICE_USEFILTER enables per-voice LPF for occlusion / distance
    // high-frequency rolloff. F3DAudio writes the cutoff coefficient into
    // dsp.LPFDirectCoefficient each frame; the listener applies it via
    // FAudioVoice_SetFilterParameters.
    uint32_t hr = FAudio_CreateSourceVoice(mEnginep->getFAudio(),
                                         &mVoice, &fmt,
                                         FAUDIO_VOICE_USEFILTER,
                                         FAUDIO_DEFAULT_FREQ_RATIO,
                                         &mCallback,
                                         &sends,
                                         nullptr);
    if (hr != 0 || !mVoice)
    {
        LL_WARNS() << "LLAudioChannelFAudio::ensureVoice() CreateSourceVoice failed: 0x"
                   << std::hex << hr << std::dec << LL_ENDL;
        mVoice = nullptr;
        mDestVoice = nullptr;
        mRoutedThroughGroup = false;
        return false;
    }
    mFormat = fmt;
    mDestVoice = desired_dest;
    mRoutedThroughGroup = desired_through_group;

    // Silence the reverb send until F3DAudio sets a per-frame level —
    // otherwise FAudio's default identity matrix would route the dry
    // signal into the reverb at full gain. Matrix dims are
    // src_channels x reverb_channels (the submix's input count, not the
    // master's — the reverb submix can be a different size).
    if (reverb)
    {
        const uint32_t reverb_ch = mEnginep->getReverbChannelCount();
        std::array<float, 64> zero{};
        FAudioVoice_SetOutputMatrix(mVoice,
                                    reinterpret_cast<FAudioVoice*>(reverb),
                                    fmt.nChannels, reverb_ch, zero.data(),
                                    FAUDIO_COMMIT_NOW);
    }
    return true;
}

void LLAudioChannelFAudio::play()
{
    if (!mVoice) return;
    if (!mStarted)
    {
        FAudioSourceVoice_Start(mVoice, 0, FAUDIO_COMMIT_NOW);
        mStarted = true;
    }
    if (getSource())
    {
        getSource()->setPlayedOnce(true);
    }
}

void LLAudioChannelFAudio::playSynced(LLAudioChannel* master)
{
    if (!master || !mVoice || !mCurrentSourcep)
    {
        play();
        return;
    }

    auto* master_ch = static_cast<LLAudioChannelFAudio*>(master);
    if (!master_ch->mVoice || master_ch->mFormat.nSamplesPerSec == 0
        || master_ch->mFormat.nBlockAlign == 0
        || mFormat.nSamplesPerSec == 0 || mFormat.nBlockAlign == 0)
    {
        play();
        return;
    }

    auto* slave_buf = static_cast<LLAudioBufferFAudio*>(
        mCurrentSourcep->getCurrentBuffer());
    if (!slave_buf)
    {
        play();
        return;
    }

    // Master's current position within its current loop, in frames.
    FAudioVoiceState state{};
    FAudioSourceVoice_GetState(master_ch->mVoice, &state, 0);

    auto* master_src = master_ch->mCurrentSourcep
        ? static_cast<LLAudioBufferFAudio*>(master_ch->mCurrentSourcep->getCurrentBuffer())
        : nullptr;
    if (!master_src)
    {
        play();
        return;
    }
    const uint32_t master_frames_per_loop =
        master_src->getFAudioBuffer().AudioBytes / master_ch->mFormat.nBlockAlign;
    if (master_frames_per_loop == 0)
    {
        play();
        return;
    }
    // SamplesPlayed counts consumed input frames including loop
    // iterations, so % gives the within-loop position *only if* the
    // master is actually looping. For a non-looping master whose
    // playback has run past the end of its buffer, the modulo result
    // would be a position the master has already left behind —
    // synchronising the slave to that point would mean starting it in
    // the middle of an already-over sound. Fall through to plain
    // play() in that case (the master is functionally done).
    if (!master_ch->mLooping
        && state.SamplesPlayed >= master_frames_per_loop)
    {
        play();
        return;
    }
    const uint64_t master_pos_frames = state.SamplesPlayed % master_frames_per_loop;

    // Convert via wall-clock time so different sample rates align correctly.
    const double t = static_cast<double>(master_pos_frames)
                   / master_ch->mFormat.nSamplesPerSec;
    const uint32_t slave_frames_per_loop =
        slave_buf->getFAudioBuffer().AudioBytes / mFormat.nBlockAlign;
    if (slave_frames_per_loop == 0)
    {
        play();
        return;
    }
    const uint64_t slave_target = static_cast<uint64_t>(t * mFormat.nSamplesPerSec);
    const uint32_t play_begin =
        static_cast<uint32_t>(slave_target % slave_frames_per_loop);

    // Flush whatever's queued and re-submit the same buffer starting at the
    // computed offset. PlayBegin can't be changed on a buffer already in the
    // voice's queue, so this requires the flush.
    FAudioSourceVoice_Stop(mVoice, 0, FAUDIO_COMMIT_NOW);
    FAudioSourceVoice_FlushSourceBuffers(mVoice);

    FAudioBuffer fbuf = slave_buf->getFAudioBuffer();
    fbuf.PlayBegin = play_begin;
    fbuf.LoopCount = mLooping ? FAUDIO_LOOP_INFINITE : 0;
    fbuf.Flags     = FAUDIO_END_OF_STREAM;
    fbuf.pContext  = nullptr;

    mFinished = false;
    mLoopCount = 0;
    mObservedLoopCount = 0;

    uint32_t hr = FAudioSourceVoice_SubmitSourceBuffer(mVoice, &fbuf, nullptr);
    if (hr != 0)
    {
        LL_WARNS() << "LLAudioChannelFAudio::playSynced() SubmitSourceBuffer failed: 0x"
                   << std::hex << hr << std::dec << LL_ENDL;
        play();
        return;
    }

    FAudioSourceVoice_Start(mVoice, 0, FAUDIO_COMMIT_NOW);
    mStarted = true;
    if (getSource()) getSource()->setPlayedOnce(true);
}

void LLAudioChannelFAudio::cleanup()
{
    destroyVoice();
    mCurrentBufferp = nullptr;
    mResumePending = false;
    mResumeFrames = 0;
}

bool LLAudioChannelFAudio::isPlaying()
{
    return mVoice && mStarted && !mFinished.load(std::memory_order_relaxed);
}

bool LLAudioChannelFAudio::updateBuffer()
{
    if (!mCurrentSourcep) return false;

    bool buffer_changed = LLAudioChannel::updateBuffer();

    auto* buf = static_cast<LLAudioBufferFAudio*>(mCurrentSourcep->getCurrentBuffer());
    if (!buf) return false;

    if (buffer_changed || !mVoice)
    {
        // A genuine buffer swap makes any stashed resume offset stale —
        // it was a frame count for the old buffer's length, not this
        // one. Clear before submission.
        if (buffer_changed)
        {
            mResumePending = false;
            mResumeFrames = 0;
        }

        if (!ensureVoice(buf->getFormat()))
        {
            return false;
        }

        // Submit a fresh playback of this buffer, with looping if requested.
        FAudioBuffer fbuf = buf->getFAudioBuffer();
        bool wants_loop = mCurrentSourcep->isLoop();
        mLooping = wants_loop;
        fbuf.LoopCount = wants_loop ? FAUDIO_LOOP_INFINITE : 0;
        fbuf.Flags     = FAUDIO_END_OF_STREAM;
        fbuf.pContext  = nullptr;

        // Resume from the prior playback frame if this voice is being
        // rebuilt after a device-hot-swap. PlayBegin is consumed once
        // per snapshot.
        if (mResumePending)
        {
            fbuf.PlayBegin = mResumeFrames;
            mResumePending = false;
            mResumeFrames = 0;
        }

        FAudioSourceVoice_FlushSourceBuffers(mVoice);
        mFinished = false;
        mLoopCount = 0;
        mObservedLoopCount = 0;
        uint32_t hr = FAudioSourceVoice_SubmitSourceBuffer(mVoice, &fbuf, nullptr);
        if (hr != 0)
        {
            LL_WARNS() << "LLAudioChannelFAudio::updateBuffer() SubmitSourceBuffer failed: 0x"
                       << std::hex << hr << std::dec << LL_ENDL;
            return false;
        }
        mStarted = false;

        // Kick the freshly-built voice into playback. The base
        // LLAudioEngine::idle() calls channelp->play() *before*
        // updateChannels — i.e. before this rebuild branch had a
        // chance to create the voice — so for channels that already
        // had a source assigned (the device-hot-swap case in
        // particular: prepareForDeviceReset destroyed the voice but
        // left mCurrentSourcep alive, and the source is still
        // expected to be playing) there is no other caller that
        // would start the voice after we rebuild it. Without this
        // kick the freshly-built voice sits stopped indefinitely
        // — what users see as "device swap goes silent and never
        // recovers". Skip when the channel is a sync-slave that's
        // explicitly been told to wait. play() is idempotent (gated
        // on mStarted) so the normal external play() callers from
        // LLAudioSource still work cleanly.
        if (!isWaiting())
        {
            play();
        }
    }

    if (mVoice)
    {
        // Submix already applies the per-type secondary gain; the source
        // voice only needs the source's own gain. When we fell back to
        // master routing (submix creation failed), the source voice has
        // to multiply secondary gain in itself.
        const F32 gain = mCurrentSourcep->getGain()
            * (mRoutedThroughGroup ? 1.0f : getSecondaryGain());
        FAudioVoice_SetVolume(mVoice, gain, FAUDIO_COMMIT_NOW);
    }
    return true;
}

void LLAudioChannelFAudio::update3DPosition()
{
    LL_PROFILE_ZONE_SCOPED;
    if (!mCurrentSourcep || !mVoice) return;

    LLListener_FAudio* listener = mEnginep->getFAudioListener();
    if (listener)
    {
        if (mCurrentSourcep->isForcedPriority())
        {
            // UI / preview sounds: route the source to all output channels
            // (proper 5.1/7.1 coverage via F3DAudio near-field diffusion).
            listener->applyForcedPriority(this);
        }
        else
        {
            listener->apply3D(this);
        }
    }

    if (mCurrentSourcep)
    {
        const F32 gain = mCurrentSourcep->getGain()
            * (mRoutedThroughGroup ? 1.0f : getSecondaryGain());
        FAudioVoice_SetVolume(mVoice, gain, FAUDIO_COMMIT_NOW);
    }
}

void LLAudioChannelFAudio::prepareForDeviceReset()
{
    // SamplesPlayed counts input frames consumed since voice start
    // (including loop iterations); modulo the buffer's frame count
    // gives the current loop-relative offset. Voice rate is the
    // buffer's own sample rate — FrequencyRatio adjusts playback
    // speed, not the consume rate — so frame counts translate 1:1
    // when the rebuilt voice is created at the same format.
    //
    // What we *don't* preserve across the swap:
    // - mSmoothedDoppler resets to 1.0 in destroyVoice(). The next
    //   update3DPosition pass re-converges from the listener +
    //   source motion — perceptually inaudible at the swap moment
    //   because the brief device-reinit silence covers the
    //   re-convergence ramp.
    // - mLoopCount + mObservedLoopCount reset to 0. Anyone tracking
    //   "I've heard this loop N times" via mLoopedThisFrame sees a
    //   reset; in practice nothing in the viewer's source/channel
    //   API relies on absolute loop counts across a device reset.
    // - With heavy Doppler frequency-ratio modulation, "frames
    //   consumed" diverges from wall-clock playback position over
    //   long durations; the modulo-into-buffer-frames approach is
    //   still close-enough for resume positioning on the new device.
    if (mVoice && mStarted && mFormat.nBlockAlign && mCurrentBufferp)
    {
        FAudioVoiceState state{};
        FAudioSourceVoice_GetState(mVoice, &state, 0);
        auto* buf = static_cast<LLAudioBufferFAudio*>(mCurrentBufferp);
        const uint32_t buf_frames =
            buf->getFAudioBuffer().AudioBytes / mFormat.nBlockAlign;
        if (buf_frames > 0)
        {
            mResumeFrames =
                static_cast<uint32_t>(state.SamplesPlayed % buf_frames);
            mResumePending = true;
        }
    }
    destroyVoice();
}

void LLAudioChannelFAudio::releaseIfReferencing(LLAudioBufferFAudio* buf)
{
    if (mCurrentBufferp != buf) return;
    // FAudioSourceVoice_Stop just flips voice->src.active; the mixer
    // thread may already be inside FAudio_INTERNAL_MixSource for this
    // voice and continue reading pAudioData (= mPcm.data()) past our
    // Stop call. FlushSourceBuffers only manages the queued-buffer
    // list and doesn't synchronize either. Only DestroyVoice waits for
    // the mixer (busy-loops on voice == audio->processingSource in
    // destroy_voice), so we tear the voice down completely before the
    // buffer's mPcm vector is freed. The channel's source association
    // is preserved; the next updateBuffer rebuilds the voice on
    // whatever new buffer the source has (likely null after eviction,
    // in which case it stays silent).
    destroyVoice();
    mCurrentBufferp = nullptr;
}

void LLAudioChannelFAudio::updateLoop()
{
    U32 loops_now = mLoopCount.load(std::memory_order_relaxed);
    if (loops_now != mObservedLoopCount)
    {
        mLoopedThisFrame = true;
        mObservedLoopCount = loops_now;
    }
}

//
// ─── Buffer ──────────────────────────────────────────────────────────────
//

bool LLAudioBufferFAudio::loadWAV(const std::string& filename)
{
    mPcm.clear();
    mFormat = FAudioWaveFormatEx{};
    mBuffer = FAudioBuffer{};
    mBytesPerFrame = 0;

    LLAudio::WavInfo wav;
    if (!LLAudio::parseWav(filename, wav))
    {
        return false;
    }

    uint16_t fmt_tag = FORMAT_PCM;
    if (wav.format_tag == 1) fmt_tag = FORMAT_PCM;
    else if (wav.format_tag == 3) fmt_tag = FORMAT_IEEE_FLOAT;
    else
    {
        LL_WARNS() << "LLAudioBufferFAudio::loadWAV() Unsupported WAV format 0x"
                   << std::hex << wav.format_tag << std::dec
                   << " in " << filename << LL_ENDL;
        return false;
    }

    if (wav.channels < 1 || wav.channels > 2
        || (wav.bits_per_sample != 8 && wav.bits_per_sample != 16
            && wav.bits_per_sample != 32))
    {
        LL_WARNS() << "LLAudioBufferFAudio::loadWAV() Unsupported PCM layout: "
                   << wav.channels << "ch " << wav.bits_per_sample << "-bit in "
                   << filename << LL_ENDL;
        return false;
    }

    mPcm = std::move(wav.pcm);
    mFormat = make_format(fmt_tag, wav.channels, wav.sample_rate, wav.bits_per_sample);
    mBytesPerFrame = mFormat.nBlockAlign;

    mBuffer.AudioBytes = static_cast<uint32_t>(mPcm.size());
    mBuffer.pAudioData = mPcm.data();
    mBuffer.Flags      = FAUDIO_END_OF_STREAM;
    mBuffer.LoopCount  = 0;
    mBuffer.PlayBegin  = 0;
    mBuffer.PlayLength = 0;
    mBuffer.LoopBegin  = 0;
    mBuffer.LoopLength = 0;
    mBuffer.pContext   = nullptr;

    return true;
}

U32 LLAudioBufferFAudio::getLength()
{
    if (mBytesPerFrame == 0) return 0;
    return static_cast<U32>(mPcm.size() / mBytesPerFrame);
}

LLAudioBufferFAudio::~LLAudioBufferFAudio()
{
    // The base engine may evict a buffer that's still referenced by a
    // source voice's submitted FAudioBuffer descriptor — mInUse tracking
    // has a one-frame gap. Synchronously flush any voice pointing at our
    // mPcm before the vector's storage is freed.
    if (gAudiop)
    {
        static_cast<LLAudioEngine_FAudio*>(gAudiop)->releaseBufferReferences(this);
    }
}
