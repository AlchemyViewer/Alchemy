/**
 * @file audioengine_fmodstudio.h
 * @brief Definition of LLAudioEngine class abstracting the audio
 * support as a FMODSTUDIO implementation
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

#ifndef LL_AUDIOENGINE_FMODSTUDIO_H
#define LL_AUDIOENGINE_FMODSTUDIO_H

#include "llaudioengine.h"
#include "llwindgen.h"

//Stubs
class LLAudioStreamManagerFMODSTUDIO;
namespace FMOD
{
    class System;
    class Channel;
    class ChannelGroup;
    class Sound;
    class DSP;
}
typedef struct FMOD_DSP_DESCRIPTION FMOD_DSP_DESCRIPTION;

//Interfaces
class LLAudioEngine_FMODSTUDIO : public LLAudioEngine
{
public:
    // preferred_device_id is a FMOD driver GUID serialised as
    // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" — see guid_to_string in
    // the cpp. Empty string uses driver 0 (FMOD's system default).
    LLAudioEngine_FMODSTUDIO(bool enable_profiler,
                              std::string preferred_device_id = std::string());
    virtual ~LLAudioEngine_FMODSTUDIO();

    // initialization/startup/shutdown
    virtual bool init(void *user_data, const std::string &app_title);
    virtual std::string getDriverName(bool verbose);
    virtual LLStreamingAudioInterface* createDefaultStreamingAudioImpl() const;
    virtual void allocateListener();

    virtual void shutdown();

    // Device selection (override base API). The id is the FMOD_GUID
    // string format; the name is the driver display name from
    // getDriverInfo. setOutputDevice persists the preference but
    // applies on next launch — runtime setDriver() in FMOD requires
    // a full System reinit which the existing channel pool isn't
    // structured to drive.
    std::vector<LLAudioOutputDevice> enumerateOutputDevices() const override;
    std::string getActiveOutputDevice()   const override { return mActiveDeviceName; }
    std::string getActiveOutputDeviceId() const override { return mActiveDeviceId; }
    std::string getOutputDeviceSettingName() const override { return "AudioFMODOutputDevice"; }
    void setOutputDevice(const std::string& id) override;

    /*virtual*/ bool initWind();
    /*virtual*/ void cleanupWind();

    /*virtual*/void updateWind(LLVector3 direction, F32 camera_height_above_water);

    typedef F32 MIXBUFFERFORMAT;

    FMOD::System *getSystem()               const {return mSystem;}
protected:
    /*virtual*/ LLAudioBuffer *createBuffer(); // Get a free buffer, or flush an existing one if you have to.
    /*virtual*/ LLAudioChannel *createChannel(); // Create a new audio channel.

    /*virtual*/ void setInternalGain(F32 gain);

    bool mInited;

    LLWindGen<MIXBUFFERFORMAT> *mWindGen;

    FMOD_DSP_DESCRIPTION *mWindDSPDesc;
    FMOD::DSP *mWindDSP;
    FMOD::System *mSystem;
    bool mEnableProfiler;

    std::string mPreferredDeviceId;
    std::string mActiveDeviceId;
    std::string mActiveDeviceName;

public:
    static FMOD::ChannelGroup *mChannelGroups[LLAudioEngine::AUDIO_TYPE_COUNT];
};


class LLAudioChannelFMODSTUDIO : public LLAudioChannel
{
public:
    LLAudioChannelFMODSTUDIO(FMOD::System *audioengine);
    virtual ~LLAudioChannelFMODSTUDIO();

protected:
    /*virtual*/ void play();
    /*virtual*/ void playSynced(LLAudioChannel *channelp);
    /*virtual*/ void cleanup();
    /*virtual*/ bool isPlaying();

    /*virtual*/ bool updateBuffer();
    /*virtual*/ void update3DPosition();
    /*virtual*/ void updateLoop();

    void set3DMode(bool use3d);
protected:
    FMOD::System *getSystem()   const {return mSystemp;}
    FMOD::System *mSystemp;
    FMOD::Channel *mChannelp;
    S32 mLastSamplePos;
};


class LLAudioBufferFMODSTUDIO : public LLAudioBuffer
{
public:
    LLAudioBufferFMODSTUDIO(FMOD::System *audioengine);
    virtual ~LLAudioBufferFMODSTUDIO();

    /*virtual*/ bool loadWAV(const std::string& filename);
    /*virtual*/ U32 getLength();
    friend class LLAudioChannelFMODSTUDIO;
protected:
    FMOD::System *getSystem()   const {return mSystemp;}
    FMOD::System *mSystemp;
    FMOD::Sound *getSound()     const{ return mSoundp; }
    FMOD::Sound *mSoundp;
};


#endif // LL_AUDIOENGINE_FMODSTUDIO_H
