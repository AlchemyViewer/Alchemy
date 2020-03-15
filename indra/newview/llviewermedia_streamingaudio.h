/** 
 * @file llviewermedia_streamingaudio.h
 * @author Tofu Linden
 * @brief Definition of LLStreamingAudio_MediaPlugins implementation - an implementation of the streaming audio interface which is implemented as a client of the media plugins API.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#ifndef LL_VIEWERMEDIA_STREAMINGAUDIO_H
#define LL_VIEWERMEDIA_STREAMINGAUDIO_H


#include "stdtypes.h" // from llcommon

#include "llstreamingaudio.h"

class LLPluginClassMedia;

class LLStreamingAudio_MediaPlugins final : public LLStreamingAudioInterface
{
 public:
	LLStreamingAudio_MediaPlugins();
	/*virtual*/ ~LLStreamingAudio_MediaPlugins();

	/*virtual*/ void start(const std::string& url) override;
	/*virtual*/ void stop() override;
	/*virtual*/ void pause(int pause) override;
	/*virtual*/ void update() override;
	/*virtual*/ int isPlaying() override;
	/*virtual*/ void setGain(F32 vol) override;
	/*virtual*/ F32 getGain() override;
	/*virtual*/ std::string getURL() override;

	bool supportsAdjustableBufferSizes() override {return false;}
	void setBufferSizes(U32 streambuffertime, U32 decodebuffertime) override {};

	virtual bool supportsMetaData() override { return false; }
	virtual const LLSD *getMetaData() override { return nullptr; }
	virtual bool hasNewMetaData() override { return false; }
	virtual bool supportsWaveData() override { return false; }
	virtual bool getWaveData(float* arr, S32 count, S32 stride = 1) override { return false; }

private:
	LLPluginClassMedia* initializeMedia(const std::string& media_type);

	LLPluginClassMedia *mMediaPlugin;

	std::string mURL;
	F32 mGain;
};


#endif //LL_VIEWERMEDIA_STREAMINGAUDIO_H
