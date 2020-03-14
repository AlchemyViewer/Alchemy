/** 
 * @file streamingaudio_fmodstudio.h
 * @brief Definition of LLStreamingAudio_FMODSTUDIO implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#ifndef LL_STREAMINGAUDIO_FMODSTUDIO_H
#define LL_STREAMINGAUDIO_FMODSTUDIO_H

#include "stdtypes.h" // from llcommon

#include "llstreamingaudio.h"
#include "lltimer.h"

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
	/*virtual*/ ~LLStreamingAudio_FMODSTUDIO();

	/*virtual*/ void start(const std::string& url) override;
	/*virtual*/ void stop() override;
	/*virtual*/ void pause(S32 pause) override;
	/*virtual*/ void update() override;
	/*virtual*/ S32 isPlaying() override;
	/*virtual*/ void setGain(F32 vol) override;
	/*virtual*/ F32 getGain() override;
	/*virtual*/ std::string getURL() override;

	/*virtual*/ bool supportsAdjustableBufferSizes() override {return true;}
	/*virtual*/ void setBufferSizes(U32 streambuffertime, U32 decodebuffertime) override;

	/*virtual*/ bool supportsMetaData() override {return true;}
	/*virtual*/ const LLSD *getMetaData() override { return mMetaData; }	//return NULL if not playing.
	/*virtual*/ bool hasNewMetaData() override;
	/*virtual*/ bool supportsWaveData() override {return true;}
	/*virtual*/ bool getWaveData(float* arr, S32 count, S32 stride = 1) override;
private:
	bool releaseDeadStreams();
	void cleanupWaveData();

	FMOD::System *mSystem;

	LLAudioStreamManagerFMODSTUDIO *mCurrentInternetStreamp;
	FMOD::DSP* mStreamDSP;
	FMOD::ChannelGroup* mStreamGroup;
	FMOD::Channel *mFMODInternetStreamChannelp;
	std::list<LLAudioStreamManagerFMODSTUDIO *> mDeadStreams;

	std::string mURL;
	std::string mPendingURL;
	F32 mGain;

	LLSD *mMetaData;
	bool mNewMetadata;
};


#endif // LL_STREAMINGAUDIO_FMODSTUDIO_H
