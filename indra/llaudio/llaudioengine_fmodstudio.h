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

#include <memory>

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

    // Reverb via FMOD's built-in DSP reverb on instance 0. setReverb
    // Properties on the system configures the I3DL2-style preset (FMOD
    // preset names mirror the I3DL2 set, with CARPETTEDHALLWAY spelt
    // with two T's); per-channel wet level is set in play() from the
    // cached static sReverbSendScale. supportsReverb returns true once
    // init() succeeds — reverb is always available on FMOD, no
    // extension check needed.
    bool supportsReverb() const override { return mReverbActive; }
    void setReverbPreset(const std::string& preset_name) override;
    void setReverbSendScale(float scale) override;

    void setWindGustiness(F32 depth) override
    {
        if (mWindGen) mWindGen->setGustinessDepth(depth);
    }

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

    std::unique_ptr<LLWindGen<MIXBUFFERFORMAT>> mWindGen;

    FMOD_DSP_DESCRIPTION *mWindDSPDesc;
    FMOD::DSP *mWindDSP;
    FMOD::System *mSystem;
    bool mEnableProfiler;

    std::string mPreferredDeviceId;
    std::string mActiveDeviceId;
    std::string mActiveDeviceName;

    bool mReverbActive = false;

public:
    static FMOD::ChannelGroup *mChannelGroups[LLAudioEngine::AUDIO_TYPE_COUNT];
    // Cached wet-send level applied to every new channel in play().
    // Static so the channel class can reach it without an engine back-
    // pointer (matches the existing mChannelGroups[] pattern). Updated
    // by setReverbSendScale, which also iterates live channels.
    static float sReverbSendScale;
};


class LLAudioChannelFMODSTUDIO : public LLAudioChannel
{
public:
    LLAudioChannelFMODSTUDIO(FMOD::System *audioengine);
    virtual ~LLAudioChannelFMODSTUDIO();

    // Apply a wet-send level for the engine's instance-0 reverb slot.
    // Called by LLAudioEngine_FMODSTUDIO::setReverbSendScale to push a
    // live slider change to all active channels, and by play() on
    // first start so a freshly-created channel matches the current
    // scale. No-op when the channel hasn't been bound to an FMOD::
    // Channel yet (sReverbSendScale will be picked up in play()).
    void setReverbWet(float wet);

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
