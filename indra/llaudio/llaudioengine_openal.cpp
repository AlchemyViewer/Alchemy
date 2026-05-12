/**
 * @file audioengine_openal.cpp
 * @brief implementation of audio engine using OpenAL
 * support as a OpenAL 3D implementation
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

#include "linden_common.h"
#include "lldir.h"
#include "llfile.h"

#include "llaudioengine_openal.h"
#include "lllistener_openal.h"
#include "llwavfile.h"

#include <cstring>
#include <new>
#include <vector>


const float LLAudioEngine_OpenAL::WIND_BUFFER_SIZE_SEC = 0.05f;

LLAudioEngine_OpenAL::LLAudioEngine_OpenAL(std::string preferred_device_id)
    :
    mPreferredDevice(std::move(preferred_device_id)),
    mWindGen(NULL),
    mWindBuf(NULL),
    mWindBufFreq(0),
    mWindBufSamples(0),
    mWindBufBytes(0),
    mWindSource(AL_NONE),
    mNumEmptyWindALBuffers(MAX_NUM_WIND_BUFFERS)
{
}

// virtual
LLAudioEngine_OpenAL::~LLAudioEngine_OpenAL()
{
}

// virtual
bool LLAudioEngine_OpenAL::init(void* userdata, const std::string &app_title)
{
    mWindGen = NULL;
    LLAudioEngine::init(userdata, app_title);

    // alcOpenDevice(NULL) is the system default; pass the preferred name
    // through verbatim if the user picked one. If the name no longer
    // matches any present device, alcOpenDevice returns NULL and we
    // fall back to default — covers unplugged / renamed hardware
    // without forcing the user to re-pick.
    const char* device_name = mPreferredDevice.empty() ? nullptr : mPreferredDevice.c_str();
    mALCDevice = alcOpenDevice(device_name);
    if (!mALCDevice && device_name)
    {
        LL_INFOS() << "LLAudioEngine_OpenAL::init() Preferred ALC device '"
                   << mPreferredDevice << "' not available; falling back to "
                   "system default." << LL_ENDL;
        mALCDevice = alcOpenDevice(nullptr);
    }
    if (!mALCDevice)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not open default ALC device" << LL_ENDL;
        return false;
    }

    // Record what we actually opened so getActiveOutputDevice / the
    // driver-name log can show the live device name.
    if (const char* opened = alcGetString(mALCDevice, ALC_ALL_DEVICES_SPECIFIER))
    {
        mActiveDevice = opened;
    }
    else if (const char* opened_basic = alcGetString(mALCDevice, ALC_DEVICE_SPECIFIER))
    {
        mActiveDevice = opened_basic;
    }

    mALCContext = alcCreateContext(mALCDevice, NULL);
    if (!mALCContext)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not create ALC context: 0x"
                   << std::hex << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        alcCloseDevice(mALCDevice);
        mALCDevice = NULL;
        return false;
    }

    if (!alcMakeContextCurrent(mALCContext))
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not make ALC context current: 0x"
                   << std::hex << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        alcDestroyContext(mALCContext);
        alcCloseDevice(mALCDevice);
        mALCContext = NULL;
        mALCDevice = NULL;
        return false;
    }

    LL_INFOS() << "LLAudioEngine_OpenAL::init() OpenAL successfully initialized" << LL_ENDL;

    LL_INFOS() << "OpenAL version: "
        << ll_safe_string(alGetString(AL_VERSION)) << LL_ENDL;
    LL_INFOS() << "OpenAL vendor: "
        << ll_safe_string(alGetString(AL_VENDOR)) << LL_ENDL;
    LL_INFOS() << "OpenAL renderer: "
        << ll_safe_string(alGetString(AL_RENDERER)) << LL_ENDL;

    ALint major = 0;
    ALint minor = 0;
    alcGetIntegerv(mALCDevice, ALC_MAJOR_VERSION, 1, &major);
    alcGetIntegerv(mALCDevice, ALC_MINOR_VERSION, 1, &minor);
    LL_INFOS() << "ALC version: " << major << "." << minor << LL_ENDL;

    LL_INFOS() << "ALC default device: "
        << ll_safe_string(alcGetString(mALCDevice,
                           ALC_DEFAULT_DEVICE_SPECIFIER))
        << LL_ENDL;

    return true;
}

// virtual
std::string LLAudioEngine_OpenAL::getDriverName(bool verbose)
{
    std::ostringstream version;

    version <<
        "OpenAL";

    if (verbose)
    {
        version <<
            ", version " <<
            ll_safe_string(alGetString(AL_VERSION)) <<
            " / " <<
            ll_safe_string(alGetString(AL_VENDOR)) <<
            " / " <<
            ll_safe_string(alGetString(AL_RENDERER));

        if (mALCDevice)
            version <<
                ": " <<
                ll_safe_string(alcGetString(mALCDevice,
                    ALC_DEFAULT_DEVICE_SPECIFIER));
    }

    return version.str();
}

// virtual
void LLAudioEngine_OpenAL::allocateListener()
{
    mListenerp = (LLListener *) new LLListener_OpenAL();
    if(!mListenerp)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::allocateListener() Listener creation failed" << LL_ENDL;
    }
}

// virtual
void LLAudioEngine_OpenAL::shutdown()
{
    LL_INFOS() << "About to LLAudioEngine::shutdown()" << LL_ENDL;
    LLAudioEngine::shutdown();

    LL_INFOS() << "About to tear down OpenAL context/device" << LL_ENDL;

    if (mALCContext)
    {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(mALCContext);
        mALCContext = NULL;
    }
    if (mALCDevice)
    {
        if (!alcCloseDevice(mALCDevice))
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::shutdown() alcCloseDevice failed: 0x"
                       << std::hex << alcGetError(mALCDevice) << std::dec << LL_ENDL;
        }
        mALCDevice = NULL;
    }

    LL_INFOS() << "LLAudioEngine_OpenAL::shutdown() OpenAL successfully shut down" << LL_ENDL;

    delete mListenerp;
    mListenerp = NULL;
}

LLAudioBuffer *LLAudioEngine_OpenAL::createBuffer()
{
    return new LLAudioBufferOpenAL();
}

LLAudioChannel *LLAudioEngine_OpenAL::createChannel()
{
    return new LLAudioChannelOpenAL();
}

void LLAudioEngine_OpenAL::setInternalGain(F32 gain)
{
    //LL_INFOS() << "LLAudioEngine_OpenAL::setInternalGain() Gain: " << gain << LL_ENDL;
    alListenerf(AL_GAIN, gain);
}

// virtual
std::vector<LLAudioOutputDevice> LLAudioEngine_OpenAL::enumerateOutputDevices() const
{
    std::vector<LLAudioOutputDevice> devices;
    // ALC enumeration returns a NULL-separated list terminated by a
    // double-NULL. ALC_ALL_DEVICES_SPECIFIER (the ALC_ENUMERATE_ALL_EXT
    // form) lists every device the implementation can open; the older
    // ALC_DEVICE_SPECIFIER is a per-implementation summary list and is
    // our fallback if the extension isn't present. Either way the
    // returned strings double as the names we pass to alcOpenDevice —
    // OpenAL doesn't surface a separate stable id, so id == name here.
    const ALCchar* list = nullptr;
    if (alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT") == ALC_TRUE)
    {
        list = alcGetString(nullptr, ALC_ALL_DEVICES_SPECIFIER);
    }
    if (!list)
    {
        list = alcGetString(nullptr, ALC_DEVICE_SPECIFIER);
    }
    if (!list) return devices;
    for (const ALCchar* p = list; *p; p += std::strlen(p) + 1)
    {
        LLAudioOutputDevice d;
        d.id = p;
        d.name = p;
        devices.push_back(std::move(d));
    }
    return devices;
}

// virtual
void LLAudioEngine_OpenAL::setOutputDevice(const std::string& id)
{
    if (id == mPreferredDevice && mALCDevice)
    {
        return;  // already running on this device
    }
    mPreferredDevice = id;

    if (!mALCDevice)
    {
        // No live device — the preference will take effect on next init.
        return;
    }

    // ALC_SOFT_reopen_device lets us swap the underlying device on an
    // existing context, keeping all sources / buffers / context state
    // intact. Without it, switching would require a full teardown +
    // recreate of every AL object, which the existing channel pool
    // isn't set up to drive. In that case persist for next launch and
    // log the limitation.
    using ReopenFn = ALCboolean (ALC_APIENTRY *)(ALCdevice*, const ALCchar*,
                                                  const ALCint*);
    auto reopen = reinterpret_cast<ReopenFn>(
        alcGetProcAddress(mALCDevice, "alcReopenDeviceSOFT"));
    if (!reopen)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::setOutputDevice() "
                      "ALC_SOFT_reopen_device unavailable; preference "
                      "saved but will only take effect on next launch."
                   << LL_ENDL;
        return;
    }

    const char* device_name = id.empty() ? nullptr : id.c_str();
    if (reopen(mALCDevice, device_name, nullptr) != ALC_TRUE)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::setOutputDevice() "
                      "alcReopenDeviceSOFT failed for '"
                   << (device_name ? device_name : "<default>")
                   << "': 0x" << std::hex << alcGetError(mALCDevice)
                   << std::dec << LL_ENDL;
        return;
    }

    if (const char* opened = alcGetString(mALCDevice, ALC_ALL_DEVICES_SPECIFIER))
    {
        mActiveDevice = opened;
    }
    LL_INFOS() << "LLAudioEngine_OpenAL::setOutputDevice() switched to '"
               << mActiveDevice << "'" << LL_ENDL;
}

LLAudioChannelOpenAL::LLAudioChannelOpenAL()
    :
    mALSource(AL_NONE),
    mLastSamplePos(0)
{
    alGenSources(1, &mALSource);
}

LLAudioChannelOpenAL::~LLAudioChannelOpenAL()
{
    cleanup();
    if (mALSource != AL_NONE)
    {
        alDeleteSources(1, &mALSource);
        mALSource = AL_NONE;
    }
}

void LLAudioChannelOpenAL::cleanup()
{
    if (mALSource != AL_NONE)
    {
        alSourceStop(mALSource);
        alSourcei(mALSource, AL_BUFFER, AL_NONE);
    }

    mCurrentBufferp = NULL;
}

void LLAudioChannelOpenAL::play()
{
    if (mALSource == AL_NONE)
    {
        LL_WARNS() << "Playing without a mALSource, aborting" << LL_ENDL;
        return;
    }

    if(!isPlaying())
    {
        alSourcePlay(mALSource);
        getSource()->setPlayedOnce(true);
    }
}

void LLAudioChannelOpenAL::playSynced(LLAudioChannel *channelp)
{
    if (channelp)
    {
        LLAudioChannelOpenAL *masterchannelp =
            (LLAudioChannelOpenAL*)channelp;
        if (mALSource != AL_NONE &&
            masterchannelp->mALSource != AL_NONE)
        {
            // we have channels allocated to master and slave
            ALfloat master_offset;
            alGetSourcef(masterchannelp->mALSource, AL_SEC_OFFSET,
                     &master_offset);

            LL_INFOS() << "Syncing with master at " << master_offset
                << "sec" << LL_ENDL;
            // *TODO: detect when this fails, maybe use AL_SAMPLE_
            alSourcef(mALSource, AL_SEC_OFFSET, master_offset);
        }
    }
    play();
}

bool LLAudioChannelOpenAL::isPlaying()
{
    if (mALSource != AL_NONE)
    {
        ALint state;
        alGetSourcei(mALSource, AL_SOURCE_STATE, &state);
        if(state == AL_PLAYING)
        {
            return true;
        }
    }

    return false;
}

bool LLAudioChannelOpenAL::updateBuffer()
{
    if (!mCurrentSourcep)
    {
        // This channel isn't associated with any source, nothing
        // to be updated
        return false;
    }

    if (LLAudioChannel::updateBuffer())
    {
        // Base class update returned true, which means that we need to actually
        // set up the source for a different buffer.
        LLAudioBufferOpenAL *bufferp = (LLAudioBufferOpenAL *)mCurrentSourcep->getCurrentBuffer();
        if (!bufferp)
        {
            return false;
        }
        alSourcei(mALSource, AL_BUFFER, bufferp->getBuffer());
        mLastSamplePos = 0;
    }

    if (mCurrentSourcep)
    {
        alSourcef(mALSource, AL_GAIN,
              mCurrentSourcep->getGain() * getSecondaryGain());
        alSourcei(mALSource, AL_LOOPING,
              mCurrentSourcep->isLoop() ? AL_TRUE : AL_FALSE);
        alSourcef(mALSource, AL_ROLLOFF_FACTOR,
              gAudiop->mListenerp->getRolloffFactor());
    }

    return true;
}


void LLAudioChannelOpenAL::updateLoop()
{
    if (mALSource == AL_NONE)
    {
        return;
    }

    // Hack:  We keep track of whether we looped or not by seeing when the
    // sample position looks like it's going backwards.  Not reliable; may
    // yield false negatives.
    //
    ALint cur_pos;
    alGetSourcei(mALSource, AL_SAMPLE_OFFSET, &cur_pos);
    if (cur_pos < mLastSamplePos)
    {
        mLoopedThisFrame = true;
    }
    mLastSamplePos = cur_pos;
}


void LLAudioChannelOpenAL::update3DPosition()
{
    if(!mCurrentSourcep)
    {
        return;
    }
    if (mCurrentSourcep->isForcedPriority())
    {
        alSource3f(mALSource, AL_POSITION, 0.0, 0.0, 0.0);
        alSource3f(mALSource, AL_VELOCITY, 0.0, 0.0, 0.0);
        alSourcei (mALSource, AL_SOURCE_RELATIVE, AL_TRUE);
    } else {
        LLVector3 float_pos;
        float_pos.setVec(mCurrentSourcep->getPositionGlobal());
        alSourcefv(mALSource, AL_POSITION, float_pos.mV);
        alSourcefv(mALSource, AL_VELOCITY, mCurrentSourcep->getVelocity().mV);
        alSourcei (mALSource, AL_SOURCE_RELATIVE, AL_FALSE);
    }

    alSourcef(mALSource, AL_GAIN, mCurrentSourcep->getGain() * getSecondaryGain());
}

LLAudioBufferOpenAL::LLAudioBufferOpenAL() = default;

LLAudioBufferOpenAL::~LLAudioBufferOpenAL()
{
    cleanup();
}

void LLAudioBufferOpenAL::cleanup()
{
    if(mALBuffer != AL_NONE)
    {
        alGetError(); // clear error
        alDeleteBuffers(1, &mALBuffer);

        ALenum error = alGetError();
        if(AL_NO_ERROR != error)
        {
            LL_WARNS("OpenAL") << "Error: 0x" << std::hex << error << std::dec
                << " when cleaning up a buffer" << LL_ENDL;
        }
        mALBuffer = AL_NONE;
    }
    mBytesPerFrame = 0;
}

bool LLAudioBufferOpenAL::loadWAV(const std::string& filename)
{
    cleanup();

    LLAudio::WavInfo wav;
    if (!LLAudio::parseWav(filename, wav))
    {
        return false;
    }

    if (wav.format_tag != 1 /* WAVE_FORMAT_PCM */)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unsupported WAV format 0x"
            << std::hex << wav.format_tag << std::dec
            << " (only PCM is supported) in " << filename << LL_ENDL;
        return false;
    }

    ALenum al_format = AL_NONE;
    if (wav.channels == 1 && wav.bits_per_sample == 8)       al_format = AL_FORMAT_MONO8;
    else if (wav.channels == 1 && wav.bits_per_sample == 16) al_format = AL_FORMAT_MONO16;
    else if (wav.channels == 2 && wav.bits_per_sample == 8)  al_format = AL_FORMAT_STEREO8;
    else if (wav.channels == 2 && wav.bits_per_sample == 16) al_format = AL_FORMAT_STEREO16;
    else
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unsupported PCM layout: "
            << wav.channels << "ch " << wav.bits_per_sample << "-bit in "
            << filename << LL_ENDL;
        return false;
    }

    alGetError(); // clear
    alGenBuffers(1, &mALBuffer);
    ALenum err = alGetError();
    if (err != AL_NO_ERROR || mALBuffer == AL_NONE)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() alGenBuffers failed: 0x"
            << std::hex << err << std::dec << LL_ENDL;
        mALBuffer = AL_NONE;
        return false;
    }

    alBufferData(mALBuffer, al_format, wav.pcm.data(),
                 static_cast<ALsizei>(wav.pcm.size()),
                 static_cast<ALsizei>(wav.sample_rate));
    err = alGetError();
    if (err != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() alBufferData failed: 0x"
            << std::hex << err << std::dec << " for " << filename << LL_ENDL;
        alDeleteBuffers(1, &mALBuffer);
        mALBuffer = AL_NONE;
        return false;
    }

    mBytesPerFrame = static_cast<U16>(wav.channels * (wav.bits_per_sample / 8));
    return true;
}

U32 LLAudioBufferOpenAL::getLength()
{
    if (mALBuffer == AL_NONE || mBytesPerFrame == 0)
    {
        return 0;
    }
    ALint length = 0;
    alGetBufferi(mALBuffer, AL_SIZE, &length);
    return static_cast<U32>(length) / mBytesPerFrame;
}

// ------------

bool LLAudioEngine_OpenAL::initWind()
{
    ALenum error;
    LL_INFOS() << "LLAudioEngine_OpenAL::initWind() start" << LL_ENDL;

    mNumEmptyWindALBuffers = MAX_NUM_WIND_BUFFERS;

    alGetError(); /* clear error */

    alGenSources(1,&mWindSource);

    if((error=alGetError()) != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::initWind() Error creating wind sources: "<<error<<LL_ENDL;
    }

    mWindGen = new LLWindGen<WIND_SAMPLE_T>;

    mWindBufFreq = mWindGen->getInputSamplingRate();
    mWindBufSamples = llceil(mWindBufFreq * WIND_BUFFER_SIZE_SEC);
    mWindBufBytes = mWindBufSamples * 2 /*stereo*/ * sizeof(WIND_SAMPLE_T);

    mWindBuf = new(std::nothrow) WIND_SAMPLE_T [mWindBufSamples * 2 /*stereo*/];

    if(mWindBuf == NULL)
    {
        LL_ERRS() << "LLAudioEngine_OpenAL::initWind() Error creating wind memory buffer" << LL_ENDL;
        return false;
    }

    LL_INFOS() << "LLAudioEngine_OpenAL::initWind() done" << LL_ENDL;

    return true;
}

void LLAudioEngine_OpenAL::cleanupWind()
{
    LL_INFOS() << "LLAudioEngine_OpenAL::cleanupWind()" << LL_ENDL;

    if (mWindSource != AL_NONE)
    {
        // detach and delete all outstanding buffers on the wind source
        alSourceStop(mWindSource);
        ALint processed;
        alGetSourcei(mWindSource, AL_BUFFERS_PROCESSED, &processed);
        while (processed--)
        {
            ALuint buffer = AL_NONE;
            alSourceUnqueueBuffers(mWindSource, 1, &buffer);
            alDeleteBuffers(1, &buffer);
        }

        // delete the wind source itself
        alDeleteSources(1, &mWindSource);

        mWindSource = AL_NONE;
    }

    delete[] mWindBuf;
    mWindBuf = NULL;

    delete mWindGen;
    mWindGen = NULL;
}

void LLAudioEngine_OpenAL::updateWind(LLVector3 wind_vec, F32 camera_altitude)
{
    if (!mEnableWind)
        return;

    if(!mWindBuf)
        return;

    if (mWindUpdateTimer.checkExpirationAndReset(LL_WIND_UPDATE_INTERVAL))
    {
        // wind comes in as Linden coordinate (+X = forward, +Y = left, +Z = up)
        // need to convert this to the conventional orientation DS3D and OpenAL use
        // where +X = right, +Y = up, +Z = backwards

        wind_vec.setVec(-wind_vec.mV[1], wind_vec.mV[2], -wind_vec.mV[0]);

        F64 pitch = 1.0 + mapWindVecToPitch(wind_vec);
        F64 center_freq = 80.0 * pow(pitch, 2.5 * (mapWindVecToGain(wind_vec) + 1.0));

        mWindGen->mTargetFreq = (F32)center_freq;
        mWindGen->mTargetGain = (F32)mapWindVecToGain(wind_vec) * mMaxWindGain;
        mWindGen->mTargetPanGainR = (F32)mapWindVecToPan(wind_vec);

        alSourcei(mWindSource, AL_LOOPING, AL_FALSE);
        alSource3f(mWindSource, AL_POSITION, 0.0, 0.0, 0.0);
        alSource3f(mWindSource, AL_VELOCITY, 0.0, 0.0, 0.0);
        alSourcef(mWindSource, AL_ROLLOFF_FACTOR, 0.0);
        alSourcei(mWindSource, AL_SOURCE_RELATIVE, AL_TRUE);
    }

    // ok lets make a wind buffer now

    ALint processed = 0, queued = 0;
    alGetSourcei(mWindSource, AL_BUFFERS_PROCESSED, &processed);
    alGetSourcei(mWindSource, AL_BUFFERS_QUEUED, &queued);
    ALint unprocessed = queued - processed;

    // ensure that there are always at least 3x as many filled buffers
    // queued as we managed to empty since last time.
    mNumEmptyWindALBuffers = llmin(mNumEmptyWindALBuffers + processed * 3 - unprocessed, MAX_NUM_WIND_BUFFERS-unprocessed);
    mNumEmptyWindALBuffers = llmax(mNumEmptyWindALBuffers, 0);

    ALenum error;

    // Recycle the processed buffers back into fresh allocations. Keeping a
    // single member vector avoids per-frame heap churn.
    if (processed > 0)
    {
        mWindRecycleBuffers.resize(processed);
        alGetError(); // clear error
        alSourceUnqueueBuffers(mWindSource, processed, mWindRecycleBuffers.data());
        error = alGetError();
        if (error != AL_NO_ERROR)
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::updateWind() error unqueuing buffers" << LL_ENDL;
        }
        else
        {
            alDeleteBuffers(processed, mWindRecycleBuffers.data());
        }
    }

    if (mNumEmptyWindALBuffers <= 0)
    {
        // Nothing to queue this frame.
        ALint playing = 0;
        alGetSourcei(mWindSource, AL_SOURCE_STATE, &playing);
        if (playing != AL_PLAYING && unprocessed > 0)
        {
            alSourcePlay(mWindSource);
        }
        return;
    }

    unprocessed += mNumEmptyWindALBuffers;
    mWindQueueBuffers.resize(mNumEmptyWindALBuffers);
    alGetError(); // clear error
    alGenBuffers(mNumEmptyWindALBuffers, mWindQueueBuffers.data());
    if ((error = alGetError()) != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::updateWind() Error creating wind buffer: " << error << LL_ENDL;
    }

    //fill the buffers with generated wind.
    int errors = 0;
    for (int i = 0; i < mNumEmptyWindALBuffers; i++)
    {
        alBufferData(mWindQueueBuffers[i],
                    AL_FORMAT_STEREO_FLOAT32,
                    mWindGen->windGenerate(mWindBuf, mWindBufSamples),
                    mWindBufBytes,
                    mWindBufFreq);
        error = alGetError();
        if (error != AL_NO_ERROR)
        {
            LL_WARNS() << "LLAudioEngine_OpenAL::updateWind() error filling wind buffer" << LL_ENDL;
            errors++;
        }
    }

    alSourceQueueBuffers(mWindSource, mNumEmptyWindALBuffers, mWindQueueBuffers.data());
    error = alGetError();
    if (error != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::updateWind() error queuing buffers" << LL_ENDL;
    }

    mNumEmptyWindALBuffers = errors;

    //restart playing if not playing
    ALint playing = 0;
    alGetSourcei(mWindSource, AL_SOURCE_STATE, &playing);
    if (playing != AL_PLAYING)
    {
        alSourcePlay(mWindSource);

        LL_DEBUGS() << "Wind had stopped - probably ran out of buffers - restarting: " << (unprocessed+mNumEmptyWindALBuffers) << " now queued." << LL_ENDL;
    }
}

