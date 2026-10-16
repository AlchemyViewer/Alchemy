/**
 * @file streamingaudio_fmodstudio.h
 * @brief Definition of LLStreamingAudio_FMODSTUDIO implementation
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

#ifndef LL_STREAMINGAUDIO_FMODSTUDIO_H
#define LL_STREAMINGAUDIO_FMODSTUDIO_H

#include "stdtypes.h" // from llcommon

#include "llstreamingaudio.h"
#include "lltimer.h"
#include "llsd.h"

#include "boost/signals2.hpp"

//Stubs
class LLAudioStreamManagerFMODSTUDIO;
namespace FMOD
{
    class System;
    class Channel;
    class ChannelGroup;
    class DSP;
}

//Interfaces
class LLStreamingAudio_FMODSTUDIO final : public LLStreamingAudioInterface
{
public:
    LLStreamingAudio_FMODSTUDIO(FMOD::System *system);
    ~LLStreamingAudio_FMODSTUDIO() override;

    void start(const std::string& url) override;
    void stop() override;
    void pause(S32 pause) override;
    void update() override;
    S32 isPlaying() override;
    void setGain(F32 vol) override;
    F32 getGain() override;
    std::string getURL() override;

    bool supportsAdjustableBufferSizes() override {return true;}
    void setBufferSizes(U32 streambuffertime, U32 decodebuffertime) override;

    bool supportsMetaData() override {return true;}
    LLSD getMetadata() const override { return mMetadata; } //return NULL if not playing.
    bool supportsWaveData() override {return true;}
    bool getWaveData(float* arr, S32 count, S32 stride = 1) override;
private:
    void killDeadStreams();
    void cleanupWaveData();

    FMOD::System *mSystem;

    LLAudioStreamManagerFMODSTUDIO *mCurrentInternetStreamp;
    FMOD::DSP* mStreamDSP;
    FMOD::ChannelGroup* mStreamGroup;
    FMOD::Channel *mFMODInternetStreamChannelp;
    std::list<LLAudioStreamManagerFMODSTUDIO *> mDeadStreams;

    std::string mURL;
    F32 mGain;
    S32 mRetryCount;

    LLSD mMetadata;
};


#endif // LL_STREAMINGAUDIO_FMODSTUDIO_H
