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
}

//
// ─── Engine ──────────────────────────────────────────────────────────────
//

LLAudioEngine_FAudio::LLAudioEngine_FAudio() = default;

LLAudioEngine_FAudio::~LLAudioEngine_FAudio() = default;

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

    uint32_t hr = FAudioCreate(&mFAudio, 0, FAUDIO_DEFAULT_PROCESSOR);
    if (hr != 0 || !mFAudio)
    {
        LL_WARNS() << "LLAudioEngine_FAudio::init() FAudioCreate failed: 0x"
                   << std::hex << hr << std::dec << LL_ENDL;
        return false;
    }

    hr = FAudio_CreateMasteringVoice(mFAudio, &mMasterVoice,
                                     FAUDIO_DEFAULT_CHANNELS,
                                     FAUDIO_DEFAULT_SAMPLERATE,
                                     0, 0, nullptr);
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
        LL_INFOS() << "LLAudioEngine_FAudio::init() reverb disabled (no "
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
            FAudioFXReverbI3DL2Parameters i3dl2 = FAUDIOFX_I3DL2_PRESET_PLAIN;
            FAudioFXReverbParameters native{};
            ReverbConvertI3DL2ToNative(&i3dl2, &native);
            FAudioVoice_SetEffectParameters(mReverbVoice, 0,
                                            &native, sizeof(native),
                                            FAUDIO_COMMIT_NOW);
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

    // Pull the underlying device's display name. FAudio stores it as a UTF-16
    // (int16_t[256]) string regardless of platform. On Linux/macOS the SDL
    // backend fills it with the device name as reported by SDL; on Windows
    // it's the wchar_t name from WASAPI. Either way, on the audio-device
    // names the viewer cares about, the bytes are well within ASCII, so a
    // lossy ASCII downcast is good enough for a log line.
    if (mFAudio)
    {
        FAudioDeviceDetails details{};
        if (FAudio_GetDeviceDetails(mFAudio, 0, &details) == 0)
        {
            std::string name;
            name.reserve(64);
            for (int i = 0; i < 256 && details.DisplayName[i]; ++i)
            {
                int16_t ch = details.DisplayName[i];
                name.push_back((ch > 0 && ch < 0x80) ? static_cast<char>(ch) : '?');
            }
            if (!name.empty()) o << " — " << name;
        }
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
    if (!mFAudio) return false;

    mWindGen = new LLWindGen<WIND_SAMPLE_T>;
    mWindBufFreq    = mWindGen->getInputSamplingRate();
    mWindBufSamples = llceil(mWindBufFreq * WIND_BUFFER_SIZE_SEC);

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
        delete mWindGen;
        mWindGen = nullptr;
        return false;
    }

    // Start silent; updateWind ramps the voice up to unity over ~300 ms,
    // masking the click that would otherwise occur as wind generation
    // ramps from zero against a silent baseline.
    FAudioVoice_SetVolume(mWindVoice, 0.0f, FAUDIO_COMMIT_NOW);
    mWindFadeIn = 0.0f;
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

    delete mWindGen;
    mWindGen = nullptr;
}

void LLAudioEngine_FAudio::updateWind(LLVector3 wind_vec, F32 /*camera_altitude*/)
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
        // Linden (+X fwd, +Y left, +Z up) -> DS3D convention (+X right, +Y up, +Z back)
        // matches the OpenAL backend so wind direction maps to pan identically.
        wind_vec.setVec(-wind_vec.mV[1], wind_vec.mV[2], -wind_vec.mV[0]);

        F64 pitch       = 1.0 + mapWindVecToPitch(wind_vec);
        F64 center_freq = 80.0 * pow(pitch, 2.5 * (mapWindVecToGain(wind_vec) + 1.0));

        mWindGen->mTargetFreq     = (F32)center_freq;
        mWindGen->mTargetGain     = (F32)mapWindVecToGain(wind_vec) * mMaxWindGain;
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

void LLAudioChannelFAudio::releaseIfReferencing(LLAudioBufferFAudio* buf)
{
    if (mCurrentBufferp != buf) return;
    if (mVoice)
    {
        // Stop then flush so the mixer thread releases its pAudioData
        // reference synchronously before the destructor body returns.
        FAudioSourceVoice_Stop(mVoice, 0, FAUDIO_COMMIT_NOW);
        FAudioSourceVoice_FlushSourceBuffers(mVoice);
    }
    mCurrentBufferp = nullptr;
    mStarted = false;
    mFinished = true;
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
