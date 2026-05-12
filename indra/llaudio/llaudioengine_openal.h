/**
 * @file audioengine_openal.cpp
 * @brief implementation of audio engine using OpenAL
 * support as a OpenAL 3D implementation
 *
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


#ifndef LL_AUDIOENGINE_OPENAL_H
#define LL_AUDIOENGINE_OPENAL_H

#include "llaudioengine.h"
#include "lllistener_openal.h"
#include "llwindgen.h"

#include <string>
#include <vector>

class LLAudioEngine_OpenAL : public LLAudioEngine
{
    public:
        // preferred_device_id is an ALC device name (which doubles as the
        // stable id in OpenAL — there's no separate GUID surface). Empty
        // string means use the system default (alcOpenDevice(NULL)).
        explicit LLAudioEngine_OpenAL(std::string preferred_device_id = std::string());
        virtual ~LLAudioEngine_OpenAL();

        virtual bool init(void *user_data, const std::string &app_title);
        virtual std::string getDriverName(bool verbose);
        virtual LLStreamingAudioInterface* createDefaultStreamingAudioImpl() const { return nullptr; }
        virtual void allocateListener();

        virtual void shutdown();

        void setInternalGain(F32 gain);

        // Device selection (override base API). In OpenAL the device's
        // name doubles as its stable id — there's no separate GUID
        // surface — so getActiveOutputDevice() == getActiveOutputDeviceId().
        std::vector<LLAudioOutputDevice> enumerateOutputDevices() const override;
        std::string getActiveOutputDevice()   const override { return mActiveDevice; }
        std::string getActiveOutputDeviceId() const override { return mActiveDevice; }
        std::string getOutputDeviceSettingName() const override { return "AudioOpenALOutputDevice"; }
        // Hot-swap via the ALC_SOFT_reopen_device extension if available;
        // otherwise persists and warns that a restart is needed.
        void setOutputDevice(const std::string& id) override;

        LLAudioBuffer* createBuffer();
        LLAudioChannel* createChannel();

        /*virtual*/ bool initWind();
        /*virtual*/ void cleanupWind();
        /*virtual*/ void updateWind(LLVector3 direction, F32 camera_altitude);

    private:
        std::string mPreferredDevice;
        std::string mActiveDevice;

        typedef F32 WIND_SAMPLE_T;
        LLWindGen<WIND_SAMPLE_T> *mWindGen;
        F32 *mWindBuf;
        U32 mWindBufFreq;
        U32 mWindBufSamples;
        U32 mWindBufBytes;
        ALuint mWindSource;
        int mNumEmptyWindALBuffers;

        ALCdevice*  mALCDevice  = nullptr;
        ALCcontext* mALCContext = nullptr;

        std::vector<ALuint> mWindRecycleBuffers;
        std::vector<ALuint> mWindQueueBuffers;

        static const int MAX_NUM_WIND_BUFFERS = 80;
        static const float WIND_BUFFER_SIZE_SEC; // 1/20th sec
};

class LLAudioChannelOpenAL : public LLAudioChannel
{
    public:
        LLAudioChannelOpenAL();
        virtual ~LLAudioChannelOpenAL();
    protected:
        /*virtual*/ void play();
        /*virtual*/ void playSynced(LLAudioChannel *channelp);
        /*virtual*/ void cleanup();
        /*virtual*/ bool isPlaying();

        /*virtual*/ bool updateBuffer();
        /*virtual*/ void update3DPosition();
        /*virtual*/ void updateLoop();

        ALuint mALSource;
            ALint mLastSamplePos;
};

class LLAudioBufferOpenAL : public LLAudioBuffer{
    public:
        LLAudioBufferOpenAL();
        virtual ~LLAudioBufferOpenAL();

        bool loadWAV(const std::string& filename);
        U32 getLength();

        friend class LLAudioChannelOpenAL;
    protected:
        void cleanup();
        ALuint getBuffer() {return mALBuffer;}

        ALuint mALBuffer       = AL_NONE;
        U16    mBytesPerFrame  = 0; // channels * bits_per_sample / 8
};

#endif
