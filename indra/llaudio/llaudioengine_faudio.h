/**
 * @file llaudioengine_faudio.h
 * @brief Audio engine using FAudio (an open-source XAudio2 / X3DAudio
 *        reimplementation).
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

#ifndef LL_AUDIOENGINE_FAUDIO_H
#define LL_AUDIOENGINE_FAUDIO_H

#include "llaudioengine.h"
#include "lllistener_faudio.h"
#include "llwindgen.h"

#include <FAudio.h>
#include <F3DAudio.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

class LLAudioChannelFAudio;
class LLAudioBufferFAudio;

// Startup tunables read from gSavedSettings by llstartup and passed in
// once at engine construction. Defaults match the previous hardcoded
// constants — overridable for advanced tuning without touching code.
struct LLAudioEngineFAudioConfig
{
    // FAudio device id (LLAudioOutputDevice::id). Empty -> system default.
    std::string preferred_device_id;
    // F3DAudio emitter InnerRadius (metres). Below this, channels diffuse
    // to all speakers; above, clean directional pan.
    float inner_radius      = 2.0f;
    // FAudioFX I3DL2 preset name applied to the reverb submix. Case-
    // insensitive; unknown names fall back to PLAIN. Recognised:
    // DEFAULT, GENERIC, PADDEDCELL, ROOM, BATHROOM, LIVINGROOM,
    // STONEROOM, AUDITORIUM, CONCERTHALL, CAVE, ARENA, HANGAR,
    // CARPETEDHALLWAY, HALLWAY, STONECORRIDOR, ALLEY, FOREST, CITY,
    // MOUNTAINS, QUARRY, PLAIN, PARKINGLOT, SEWERPIPE, UNDERWATER,
    // SMALLROOM.
    std::string reverb_preset = "PLAIN";
    // Scalar applied to dsp.ReverbLevel before writing the per-source ->
    // reverb-submix matrix. 0 disables reverb sends entirely.
    float reverb_send_scale = 0.05f;
    // FAPOFX FXMasteringLimiter attached to the mastering voice. FAudio's
    // float-internal mix can sum past unity when many sources play at
    // once or after F3DAudio matrix scaling, producing distortion at the
    // device-side integer quantizer. The limiter pulls peaks back below
    // unity transparently for typical content.
    //
    //   limiter_enable   — chain is always attached at init; this flag
    //                      controls whether the FAPO starts enabled
    //                      (live-toggleable via FAudioVoice_Enable/
    //                      DisableEffect).
    //   limiter_release  — gain-recovery time in ms (FAPOFX range 1-20,
    //                      default 6). Lower = snappier release;
    //                      higher = smoother but slower recovery.
    //   limiter_loudness — unit-less threshold target (FAPOFX range
    //                      1-1800, default 1000). Higher leaves more
    //                      headroom before the limiter engages; lower
    //                      compresses earlier.
    bool     limiter_enable   = true;
    uint32_t limiter_release  = 6;
    uint32_t limiter_loudness = 1000;
};

class LLAudioEngine_FAudio : public LLAudioEngine
{
public:
    // Config carries the user-tunable startup parameters; defaults match
    // the previously-hardcoded constants when an empty config is used.
    explicit LLAudioEngine_FAudio(LLAudioEngineFAudioConfig config = {});
    ~LLAudioEngine_FAudio() override;

    // Accessors for the listener (run_f3d reads these per call) and
    // anyone inspecting the live tuning.
    float    getInnerRadius()            const { return mConfig.inner_radius; }
    float    getReverbSendScale()        const { return mConfig.reverb_send_scale; }
    bool     getMasterLimiterEnable()    const { return mConfig.limiter_enable; }
    uint32_t getMasterLimiterRelease()   const { return mConfig.limiter_release; }
    uint32_t getMasterLimiterLoudness()  const { return mConfig.limiter_loudness; }

    // Live setters. InnerRadius / ReverbSendScale are read each
    // F3DAudioCalculate call so changes take effect on the next frame's
    // spatial update. ReverbPreset re-applies via the FAudioFX FAPO's
    // SetEffectParameters — also instant. The MasterLimiter* setters
    // dispatch directly to the FAPO via SetEffectParameters /
    // Enable/DisableEffect — also instant.
    void setInnerRadius(float meters);
    void setReverbSendScale(float scale) override;
    void setReverbPreset(const std::string& preset_name) override;
    void setMasterLimiterEnable(bool enable);
    void setMasterLimiterRelease(uint32_t ms);
    void setMasterLimiterLoudness(uint32_t value);
    bool supportsReverb() const override { return true; }
    std::string getReverbSendScaleSettingName() const override
    {
        return "AudioFAudioReverbSendScale";
    }

    void setWindGustiness(F32 depth) override
    {
        if (mWindGen) mWindGen->setGustinessDepth(depth);
    }

    // Returns every output device FAudio reports as {id, name} pairs.
    // Index 0 in the underlying device list is FAudio's notion of the
    // system default and shows up here verbatim — UI typically prepends
    // a synthetic "System default" entry with empty id. Empty list if
    // FAudio hasn't been initialised yet.
    std::vector<LLAudioOutputDevice> enumerateOutputDevices() const override;

    std::string getActiveOutputDevice()   const override { return mActiveDeviceName; }
    std::string getActiveOutputDeviceId() const override { return mActiveDeviceId;   }
    std::string getOutputDeviceSettingName() const override { return "AudioFAudioOutputDevice"; }

    // Live device hot-swap. Tears down the FAudio voice graph
    // (channels' source voices, wind, reverb, submixes, master), then
    // rebuilds against the device matching this id. Playing channels
    // capture their playback frame before teardown and resume on the
    // new device at the same position. No-op if id matches the current
    // preference and the device is already live.
    void setOutputDevice(const std::string& id) override;

    bool init(void* userdata, const std::string& app_title) override;
    std::string getDriverName(bool verbose) override;
    LLStreamingAudioInterface* createDefaultStreamingAudioImpl() const override { return nullptr; }
    void allocateListener() override;
    void shutdown() override;

    void setInternalGain(F32 gain) override;
    void setSecondaryGain(S32 type, F32 gain) override;

    LLAudioBuffer* createBuffer() override;
    LLAudioChannel* createChannel() override;

    bool initWind() override;
    void cleanupWind() override;
    void updateWind(LLVector3 direction, F32 camera_altitude) override;

    // Accessors used by the channel and listener subclasses.
    FAudio*                getFAudio() const          { return mFAudio; }
    FAudioMasteringVoice*  getMasterVoice() const     { return mMasterVoice; }
    FAudioSubmixVoice*     getGroupVoice(S32 type) const;
    FAudioSubmixVoice*     getReverbVoice() const     { return mReverbVoice; }
    uint32_t               getReverbChannelCount() const { return mReverbChannels; }
    uint32_t               getOutputChannelCount() const { return mOutputChannels; }
    uint32_t               getOutputSampleRate() const   { return mSampleRate; }
    const F3DAUDIO_HANDLE& getX3DInstance() const      { return mX3DInstance; }
    LLListener_FAudio* getFAudioListener() const
    {
        // mListenerp is a protected base member; legal here because
        // LLAudioEngine_FAudio is a derived class.
        return static_cast<LLListener_FAudio*>(mListenerp);
    }

    // Wind queue depth is updated from the FAudio mixer thread via the
    // OnBufferEnd callback; public so the static C callback can reach it.
    // Main thread uses it as a high-water mark to drain consumed chunks
    // from mWindChunks (FIFO).
    std::atomic<int>       mWindQueueDepth{0};

    struct WindVoiceCallback : public FAudioVoiceCallback
    {
        LLAudioEngine_FAudio* engine = nullptr;
    };

    // Called by LLAudioBufferFAudio's destructor: flush any source voice
    // still referencing this buffer's PCM so FAudio isn't reading freed
    // memory after the vector is destructed. The base engine's eviction
    // path can delete a buffer that's still queued on a voice when mInUse
    // hasn't been refreshed yet within a frame.
    void releaseBufferReferences(class LLAudioBufferFAudio* buf);

private:
    // FAudio-only init / teardown. init() composes them with the parent
    // LLAudioEngine::init bookkeeping; setOutputDevice composes them
    // without disturbing the channel pool / source list above us so
    // playing channels can transparently re-attach on the new device.
    bool initFAudioDevice();
    void releaseFAudioDevice();

    using WIND_SAMPLE_T = F32;

    static constexpr float WIND_BUFFER_SIZE_SEC = 0.05f;
    static constexpr int   MAX_WIND_QUEUED      = 4;

    // Startup tunables (device id, distance / reverb knobs).
    LLAudioEngineFAudioConfig mConfig;
    // Id and display name of the device init() actually opened, for
    // getActiveOutputDevice() / getActiveOutputDeviceId().
    std::string            mActiveDeviceId;
    std::string            mActiveDeviceName;

    FAudio*                mFAudio       = nullptr;
    FAudioMasteringVoice*  mMasterVoice  = nullptr;
    FAudioSubmixVoice*     mGroupVoices[LLAudioEngine::AUDIO_TYPE_COUNT]{};
    // Single global reverb submix with an FAudioFXReverb FAPO on it. All
    // spatial source voices send a wet-level signal here in addition to
    // their dry submix; the reverb output mixes back into the master.
    // FAudioFXReverb only supports 1/2/6 channel I/O, so the submix is
    // created at the closest supported size and FAudio's default
    // submix->master matrix handles the channel-count conversion when
    // master is e.g. quad (4) or 7.1 (8). mReverbChannels records the
    // size we picked so source-side reverb-send matrices are sized to
    // match.
    FAudioSubmixVoice*     mReverbVoice  = nullptr;
    FAPO*                  mReverbApo    = nullptr;
    uint16_t               mReverbChannels = 0;

    // FXMasteringLimiter is attached to the mastering voice's effect chain
    // (index 0). The chain holds the FAPO reference after attach, so we
    // don't keep an FAPO* member — live tuning is done by calling
    // FAudioVoice_SetEffectParameters on mMasterVoice with the
    // FAPOFXMasteringLimiterParameters struct. mHasMasterLimiter tracks
    // whether the chain attach succeeded so setters short-circuit on
    // backends/configs where it didn't.
    bool                   mHasMasterLimiter = false;
    // Effect index of the limiter within the mastering voice's chain.
    // Single-effect chain so this is always 0; pulled out as a constant
    // so the setters and clean-up paths read self-documenting.
    static constexpr uint32_t kMasterLimiterEffectIndex = 0;

    uint32_t               mSampleRate     = 0;
    // Output channel count from the mastering voice. Named distinctly so
    // it doesn't shadow the base class's `mChannels` channel-pool array.
    uint16_t               mOutputChannels = 0;

    F3DAUDIO_HANDLE        mX3DInstance{};

    // Wind synthesis.
    std::unique_ptr<LLWindGen<WIND_SAMPLE_T>> mWindGen;
    FAudioSourceVoice*        mWindVoice   = nullptr;
    U32                       mWindBufFreq = 0;
    U32                       mWindBufSamples = 0;
    // FIFO of wind PCM chunks owned by the engine. Each submit pushes a
    // new chunk; OnBufferEnd decrements mWindQueueDepth (without freeing,
    // because FAudio's flush-then-destroy path can drop OnBufferEnd
    // events on the floor); the main thread drains consumed chunks from
    // the front of the deque the next time updateWind runs by comparing
    // deque size against mWindQueueDepth.
    std::deque<std::vector<WIND_SAMPLE_T>> mWindChunks;
    // Wind voice volume ramp. initWind starts the voice silent; updateWind
    // ramps up to unity over ~300 ms. Smooths the initial-startup
    // transient when the wind voice begins playing freshly-generated
    // samples against a silent baseline.
    float                  mWindFadeIn = 1.0f;
    static constexpr float kWindFadeInPerFrame = 0.05f;

    WindVoiceCallback mWindCallback{};
};

class LLAudioChannelFAudio : public LLAudioChannel
{
public:
    explicit LLAudioChannelFAudio(LLAudioEngine_FAudio* engine);
    ~LLAudioChannelFAudio() override;

    // Used by LLListener_FAudio::apply3D().
    FAudioSourceVoice* getVoice() const            { return mVoice; }
    FAudioVoice*       getDestVoice() const        { return mDestVoice; }
    bool               isRoutedThroughGroup() const { return mRoutedThroughGroup; }
    uint32_t           getSourceChannelCount() const { return mFormat.nChannels; }
    LLAudioSource*     getSource() const           { return mCurrentSourcep; }
    // True when the underlying source voice was created with
    // FAUDIO_VOICE_USEFILTER. SetFilterParameters is invalid on voices
    // without that flag, so the listener consults this before writing the
    // air-absorption LPF coefficient.
    bool               getVoiceUsesFilter() const  { return mVoiceUsesFilter; }

    // Exponentially-smoothed doppler frequency ratio. Single-writer (main
    // thread via apply3D / applyForcedPriority), single-reader (same path
    // next frame) — no atomic needed.
    float              mSmoothedDoppler = 1.0f;

    // Exponentially-smoothed F3DAudio LPF coefficient (FAudio voice filter
    // Frequency, [0,1] passthrough->cutoff). FAudio interpolates SetVolume
    // and SetOutputMatrix across the audio frame, but SetFilterParameters
    // is applied at once — so per-frame coefficient writes drive the IIR
    // biquad's poles out from under stale y[n-1]/y[n-2] state, producing
    // audible "pops" on rapid listener motion. Smoothing here is the main-
    // thread half of the fix; the listener's run_f3d skips the API call
    // entirely when the smoothed value has settled at passthrough.
    // mLastAppliedLpf tracks what was actually written so the run can
    // detect convergence to passthrough and stop calling.
    float              mSmoothedLpf    = 1.0f;
    float              mLastAppliedLpf = 1.0f;

    // Exponentially-smoothed reverb-send level (F3DAudio's dsp.ReverbLevel
    // scaled by the engine's reverb_send_scale and, for grouped channels,
    // their secondary gain). FAudio interpolates SetOutputMatrix across
    // the audio frame, but per-frame writes still produce small steps
    // when send_gain changes rapidly — fast listener motion or a source
    // crossing the reverb curve's steep section. Smoothing + skip-when-
    // settled-at-silence mirrors the LPF approach in mSmoothedLpf above.
    // mLastAppliedReverbSend tracks the most recently written value so
    // run_f3d can detect convergence and stop poking the matrix.
    float              mSmoothedReverbSend    = 0.0f;
    float              mLastAppliedReverbSend = 0.0f;

    // Per-channel fade-in gain multiplier (0..1). Only armed by
    // playSynced, which restarts the voice at an arbitrary mid-buffer
    // PlayBegin offset whose first sample is guaranteed non-zero
    // amplitude — a discontinuity FMOD's own setVolumeRamp also has to
    // ramp through. Regular play() does NOT arm a fade-in (FMOD doesn't
    // either; the FAudio-specific first-quantum dip-out is handled by
    // updateBuffer's silent pre-roll buffer rather than a gain ramp,
    // matching FMOD's perceptual behaviour on every fresh sound start).
    //
    // The per-frame gain assignments in updateBuffer / update3DPosition
    // multiply by mFadeIn; the tick at the top of updateBuffer ramps it
    // back to 1.0. At rest (mFadeIn == 1.0) the multiplier is a no-op.
    // Single-writer (main thread), no atomic needed.
    //
    // Tick placement note: the tick lives at the *top* of updateBuffer
    // rather than in update3DPosition. playSynced may be called between
    // updateBuffer and update3DPosition for the same channel — a tick
    // later in the same frame would immediately bump mFadeIn back
    // toward 1.0 and the SetVolume(target=source.gain*mFadeIn) at the
    // bottom of updateBuffer / inside update3DPosition would land on a
    // non-zero target, defeating the synced-restart ramp.
    float              mFadeIn         = 1.0f;
    float              mFadeInPerFrame = 0.34f;
    // ~50 ms ramp at 60 Hz. Single source of fade-in rate now — only
    // playSynced arms a fade — but kept as a named constant so the
    // duration is documented and easy to tweak. ~50 ms is long enough
    // to mask a mid-buffer restart's first-sample discontinuity without
    // shifting the perceived attack of the synced sound noticeably.
    static constexpr float kFadeInPerFrameSynced = 0.34f;

    // Called from LLAudioBufferFAudio's destructor when this channel's
    // current buffer is about to be freed. Flush the voice so the mixer
    // thread stops reading the buffer's PCM before the vector is gone.
    void releaseIfReferencing(class LLAudioBufferFAudio* buf);

    // Snapshot the current playback frame within mCurrentBufferp, then
    // destroy the voice. The next updateBuffer pass rebuilds the voice
    // on the new FAudio instance and resumes from the saved frame —
    // used by the engine's setOutputDevice hot-swap path so a long
    // looping ambient doesn't jump to its loop start when the user
    // picks a different output device.
    void prepareForDeviceReset();

    // Updated from the FAudio mixer thread by the static C callbacks; public
    // so those free functions can reach them via the ChannelCallback owner
    // pointer without a friend declaration shuffle.
    std::atomic<bool>  mFinished{false};
    std::atomic<U32>   mLoopCount{0};

    struct ChannelCallback : public FAudioVoiceCallback
    {
        LLAudioChannelFAudio* owner = nullptr;
    };

    // Move the current source voice into the dying-voice slot for cross-
    // frame fade-out. Used by the "safe" destruction paths (channel reuse
    // via ensureVoice's rebuild branch, cleanup() at end-of-source). The
    // dying voice's callback owner is nulled so its OnStreamEnd /
    // OnLoopEnd no longer mutate the live channel state, then it fades
    // out under updateLoop's per-frame tick before final destruction. The
    // "unsafe" paths (releaseIfReferencing, prepareForDeviceReset, the
    // destructor) still go through destroyVoice() — there's no time to
    // fade because the underlying resources are about to disappear.
    void retireVoice();

protected:
    void play() override;
    void playSynced(LLAudioChannel* channelp) override;
    void cleanup() override;
    bool isPlaying() override;

    bool updateBuffer() override;
    void update3DPosition() override;
    void updateLoop() override;

private:
    bool ensureVoice(const FAudioWaveFormatEx& fmt);
    void destroyVoice();

    LLAudioEngine_FAudio* mEnginep = nullptr;

    FAudioSourceVoice*    mVoice = nullptr;
    // Frame offset to resume from when the voice is rebuilt after the
    // engine hot-swaps its output device. Set by prepareForDeviceReset,
    // consumed and cleared by the next successful updateBuffer.
    uint32_t              mResumeFrames = 0;
    bool                  mResumePending = false;
    // The voice this source routes to — either a per-type submix or the
    // mastering voice as a fallback. SetOutputMatrix must target whatever
    // is actually in the voice's send list.
    FAudioVoice*          mDestVoice = nullptr;
    // True when mDestVoice is a per-type submix that already carries the
    // secondary gain. False when the submix wasn't created and we routed
    // straight to master — then the source voice must apply secondary gain
    // itself.
    bool                  mRoutedThroughGroup = false;
    // Records whether the source voice was built with FAUDIO_VOICE_USEFILTER.
    // Forced-priority sources (UI / preview) sit at listener distance 0 —
    // the LPF curve is at passthrough and the biquad would only contribute
    // floating-point housekeeping noise — so we omit the flag and skip
    // SetFilterParameters entirely. Tracked here so ensureVoice can rebuild
    // when a channel is reused for a source whose forced-priority state
    // differs from the previous occupant.
    bool                  mVoiceUsesFilter = false;
    FAudioWaveFormatEx    mFormat{};
    bool                  mLooping  = false;
    bool                  mStarted  = false;
    U32                   mObservedLoopCount = 0;

    // One master-quantum of silence in the source's PCM format, prepended
    // to every fresh real-buffer submit. Without it, FAudio's mixer would
    // mix the real buffer's first sample at the voice's default
    // currentVolume of 1.0 — defeating SetVolume(0) + mFadeIn since the
    // first quantum's "previous volume" hasn't been updated yet. With the
    // pre-roll, the mixer's first quantum outputs silence while
    // currentVolume ramps from 1.0 to 0; the real buffer starts on the
    // next quantum with currentVolume already at 0 and the fade-in tick
    // raising the target back toward source.gain. Sized in ensureVoice
    // when the format is known (source_rate / 100 samples * nBlockAlign);
    // costs ~1-2 KB per active channel.
    std::vector<U8>       mSilentPreroll;

    // Shared ownership of the live voice's PCM, captured from the
    // LLAudioBufferFAudio at submit time. FAudio's submitted FAudioBuffer
    // descriptor stores pAudioData as a raw pointer; if the
    // LLAudioBufferFAudio object is destroyed (engine eviction) the raw
    // pointer would dangle. The shared_ptr ref-count keeps the underlying
    // vector alive for as long as a voice queue references it, so the
    // engine can free the buffer object without pulling the rug out from
    // under the mixer — and releaseIfReferencing no longer needs the
    // synchronous-destroy-on-eviction click that was its only previous
    // option.
    std::shared_ptr<std::vector<U8>> mVoicePcm;
    // Transferred from mVoicePcm by retireVoice. The dying voice plays
    // out its grace window backed by this shared_ptr; reset when the
    // dying voice is destroyed in updateLoop, which drops the last ref
    // (or one of several, if the underlying buffer was already destroyed
    // earlier).
    std::shared_ptr<std::vector<U8>> mDyingVoicePcm;

    // Two callback structs so a dying voice can have its owner pointer
    // nulled — preventing channel_on_stream_end / channel_on_loop_end from
    // racing into the live channel's mFinished / mLoopCount during the
    // dying voice's fade-out — without disabling callbacks on the new
    // voice. FAudio binds a callback pointer at CreateSourceVoice time
    // and doesn't expose a way to swap it later, so we cycle between
    // mCallbackA and mCallbackB on each retire. mActiveCallback names
    // the slot the next ensureVoice will pass to FAudio.
    ChannelCallback       mCallbackA{};
    ChannelCallback       mCallbackB{};
    ChannelCallback*      mActiveCallback = &mCallbackA;

    // Cross-frame fade-out slot. retireVoice() moves mVoice here, sets
    // its target volume to 0 (FAudio interpolates the dry/reverb output
    // matrix internally), and starts a grace countdown. updateLoop ticks
    // the countdown each frame and destroys the voice when it reaches 0.
    // Only one slot — a second retire while one is in progress hard-
    // destroys the older fading voice so the channel doesn't grow an
    // unbounded queue of fading voices on rapid channel reuse.
    FAudioSourceVoice*    mDyingVoice = nullptr;
    int                   mDyingFramesLeft = 0;
    // ~2 frames @ 60 Hz gives FAudio ~32 ms to apply its quantum-level
    // ramp from the prior volume to 0 before we DestroyVoice. Matches
    // the perceptual length of FMOD's default ramp on Stop.
    static constexpr int  kFadeOutGraceFrames = 2;
};

class LLAudioBufferFAudio : public LLAudioBuffer
{
public:
    LLAudioBufferFAudio() = default;
    ~LLAudioBufferFAudio() override;

    bool loadWAV(const std::string& filename) override;
    U32  getLength() override;

    friend class LLAudioChannelFAudio;

    const FAudioWaveFormatEx& getFormat() const { return mFormat; }
    const FAudioBuffer&       getFAudioBuffer() const { return mBuffer; }
    U16                       getBytesPerFrame() const { return mBytesPerFrame; }

private:
    // shared_ptr so a channel can keep the PCM alive after this buffer
    // object is destroyed by the engine's eviction sweep. FAudio's
    // submitted FAudioBuffer descriptor caches pAudioData = mPcm->data()
    // (which is allocation-pointer-preserving as long as mPcm is alive);
    // channels capture this shared_ptr on submit, transfer it on retire,
    // and drop it when the dying voice is destroyed. See
    // LLAudioChannelFAudio::mVoicePcm / mDyingVoicePcm.
    std::shared_ptr<std::vector<U8>> mPcm;
    FAudioWaveFormatEx mFormat{};
    FAudioBuffer       mBuffer{};
    U16                mBytesPerFrame = 0;
};

#endif
