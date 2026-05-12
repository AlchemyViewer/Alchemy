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
    uint32_t               getOutputChannelCount() const { return mChannels; }
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
    std::atomic<int>       mWindQueueDepth{0};

    struct WindVoiceCallback : public FAudioVoiceCallback
    {
        LLAudioEngine_FAudio* engine = nullptr;
    };

private:
    using WIND_SAMPLE_T = F32;

    static constexpr float WIND_BUFFER_SIZE_SEC = 0.05f;
    static constexpr int   MAX_WIND_QUEUED      = 4;

    FAudio*                mFAudio       = nullptr;
    FAudioMasteringVoice*  mMasterVoice  = nullptr;
    FAudioSubmixVoice*     mGroupVoices[LLAudioEngine::AUDIO_TYPE_COUNT]{};

    uint32_t               mSampleRate = 0;
    uint16_t               mChannels   = 0;

    F3DAUDIO_HANDLE        mX3DInstance{};

    // Wind synthesis.
    LLWindGen<WIND_SAMPLE_T>* mWindGen     = nullptr;
    FAudioSourceVoice*        mWindVoice   = nullptr;
    U32                       mWindBufFreq = 0;
    U32                       mWindBufSamples = 0;

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
    uint32_t           getSourceChannelCount() const { return mFormat.nChannels; }
    LLAudioSource*     getSource() const           { return mCurrentSourcep; }

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
    ~LLAudioBufferFAudio() override = default;

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
