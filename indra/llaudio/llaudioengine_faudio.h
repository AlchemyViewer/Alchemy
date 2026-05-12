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
#include <vector>

class LLAudioChannelFAudio;
class LLAudioBufferFAudio;

class LLAudioEngine_FAudio : public LLAudioEngine
{
public:
    LLAudioEngine_FAudio();
    ~LLAudioEngine_FAudio() override;

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
    using WIND_SAMPLE_T = F32;

    static constexpr float WIND_BUFFER_SIZE_SEC = 0.05f;
    static constexpr int   MAX_WIND_QUEUED      = 4;

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

    uint32_t               mSampleRate     = 0;
    // Output channel count from the mastering voice. Named distinctly so
    // it doesn't shadow the base class's `mChannels` channel-pool array.
    uint16_t               mOutputChannels = 0;

    F3DAUDIO_HANDLE        mX3DInstance{};

    // Wind synthesis.
    LLWindGen<WIND_SAMPLE_T>* mWindGen     = nullptr;
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

    // Exponentially-smoothed doppler frequency ratio. Single-writer (main
    // thread via apply3D / applyForcedPriority), single-reader (same path
    // next frame) — no atomic needed.
    float              mSmoothedDoppler = 1.0f;

    // Called from LLAudioBufferFAudio's destructor when this channel's
    // current buffer is about to be freed. Flush the voice so the mixer
    // thread stops reading the buffer's PCM before the vector is gone.
    void releaseIfReferencing(class LLAudioBufferFAudio* buf);

    // Updated from the FAudio mixer thread by the static C callbacks; public
    // so those free functions can reach them via the ChannelCallback owner
    // pointer without a friend declaration shuffle.
    std::atomic<bool>  mFinished{false};
    std::atomic<U32>   mLoopCount{0};

    struct ChannelCallback : public FAudioVoiceCallback
    {
        LLAudioChannelFAudio* owner = nullptr;
    };

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
    // The voice this source routes to — either a per-type submix or the
    // mastering voice as a fallback. SetOutputMatrix must target whatever
    // is actually in the voice's send list.
    FAudioVoice*          mDestVoice = nullptr;
    // True when mDestVoice is a per-type submix that already carries the
    // secondary gain. False when the submix wasn't created and we routed
    // straight to master — then the source voice must apply secondary gain
    // itself.
    bool                  mRoutedThroughGroup = false;
    FAudioWaveFormatEx    mFormat{};
    bool                  mLooping  = false;
    bool                  mStarted  = false;
    U32                   mObservedLoopCount = 0;

    ChannelCallback       mCallback{};
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
    std::vector<U8>    mPcm;
    FAudioWaveFormatEx mFormat{};
    FAudioBuffer       mBuffer{};
    U16                mBytesPerFrame = 0;
};

#endif
