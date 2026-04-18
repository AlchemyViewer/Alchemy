/**
 * @file audioengine_openal.cpp
 * @brief implementation of audio engine using OpenAL
 * support as a OpenAL 3D implementation
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

#include "linden_common.h"
#include "lldir.h"
#include "llfile.h"

#include "llaudioengine_openal.h"
#include "lllistener_openal.h"

#include <cstring>
#include <new>
#include <vector>


const float LLAudioEngine_OpenAL::WIND_BUFFER_SIZE_SEC = 0.05f;

LLAudioEngine_OpenAL::LLAudioEngine_OpenAL()
    :
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

    mALCDevice = alcOpenDevice(NULL);
    if (!mALCDevice)
    {
        LL_WARNS() << "LLAudioEngine_OpenAL::init() Could not open default ALC device" << LL_ENDL;
        return false;
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

namespace
{
    inline U16 read_u16_le(const U8* p) { return (U16)p[0] | ((U16)p[1] << 8); }
    inline U32 read_u32_le(const U8* p)
    {
        return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24);
    }
}

bool LLAudioBufferOpenAL::loadWAV(const std::string& filename)
{
    cleanup();

    if (filename.empty())
    {
        return false;
    }

    llifstream infile(filename, std::ios::in | std::ios::binary);
    if (!infile)
    {
        if (LLFile::isfile(filename))
        {
            LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unable to open "
                << filename << LL_ENDL;
        }
        else
        {
            // It's common for the file to not actually exist.
            LL_DEBUGS() << "LLAudioBufferOpenAL::loadWAV() File missing "
                << filename << LL_ENDL;
        }
        return false;
    }

    U8 riff[12];
    infile.read(reinterpret_cast<char*>(riff), sizeof(riff));
    if (!infile || static_cast<size_t>(infile.gcount()) != sizeof(riff)
        || memcmp(riff, "RIFF", 4) != 0
        || memcmp(riff + 8, "WAVE", 4) != 0)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Not a RIFF/WAVE file: "
            << filename << LL_ENDL;
        return false;
    }

    U16 format_tag = 0;
    U16 channels = 0;
    U32 sample_rate = 0;
    U16 bits_per_sample = 0;
    std::vector<U8> pcm_data;
    bool have_fmt = false;
    bool have_data = false;

    while (infile)
    {
        U8 chunk_hdr[8];
        infile.read(reinterpret_cast<char*>(chunk_hdr), sizeof(chunk_hdr));
        if (static_cast<size_t>(infile.gcount()) != sizeof(chunk_hdr))
        {
            break;
        }
        U32 chunk_size = read_u32_le(chunk_hdr + 4);

        if (memcmp(chunk_hdr, "fmt ", 4) == 0)
        {
            if (chunk_size < 16)
            {
                LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Malformed fmt chunk in "
                    << filename << LL_ENDL;
                return false;
            }
            std::vector<U8> fmt(chunk_size);
            infile.read(reinterpret_cast<char*>(fmt.data()), chunk_size);
            if (static_cast<U32>(infile.gcount()) != chunk_size)
            {
                LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Truncated fmt chunk in "
                    << filename << LL_ENDL;
                return false;
            }
            format_tag      = read_u16_le(fmt.data() + 0);
            channels        = read_u16_le(fmt.data() + 2);
            sample_rate     = read_u32_le(fmt.data() + 4);
            bits_per_sample = read_u16_le(fmt.data() + 14);
            have_fmt = true;
        }
        else if (memcmp(chunk_hdr, "data", 4) == 0)
        {
            pcm_data.resize(chunk_size);
            if (chunk_size > 0)
            {
                infile.read(reinterpret_cast<char*>(pcm_data.data()), chunk_size);
                if (static_cast<U32>(infile.gcount()) != chunk_size)
                {
                    LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Truncated data chunk in "
                        << filename << LL_ENDL;
                    return false;
                }
            }
            have_data = true;
            break;
        }
        else
        {
            U32 padded = chunk_size + (chunk_size & 1);
            infile.seekg(padded, std::ios::cur);
        }
    }

    if (!have_fmt || !have_data)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Missing fmt or data chunk in "
            << filename << LL_ENDL;
        return false;
    }

    if (format_tag != 1 /* WAVE_FORMAT_PCM */)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unsupported WAV format 0x"
            << std::hex << format_tag << std::dec
            << " (only PCM is supported) in " << filename << LL_ENDL;
        return false;
    }

    if (channels == 0 || sample_rate == 0)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Invalid fmt chunk ("
            << channels << "ch @ " << sample_rate << "Hz) in "
            << filename << LL_ENDL;
        return false;
    }

    ALenum al_format = AL_NONE;
    if (channels == 1 && bits_per_sample == 8)       al_format = AL_FORMAT_MONO8;
    else if (channels == 1 && bits_per_sample == 16) al_format = AL_FORMAT_MONO16;
    else if (channels == 2 && bits_per_sample == 8)  al_format = AL_FORMAT_STEREO8;
    else if (channels == 2 && bits_per_sample == 16) al_format = AL_FORMAT_STEREO16;
    else
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() Unsupported PCM layout: "
            << channels << "ch " << bits_per_sample << "-bit in "
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

    alBufferData(mALBuffer, al_format, pcm_data.data(),
                 static_cast<ALsizei>(pcm_data.size()),
                 static_cast<ALsizei>(sample_rate));
    err = alGetError();
    if (err != AL_NO_ERROR)
    {
        LL_WARNS() << "LLAudioBufferOpenAL::loadWAV() alBufferData failed: 0x"
            << std::hex << err << std::dec << " for " << filename << LL_ENDL;
        alDeleteBuffers(1, &mALBuffer);
        mALBuffer = AL_NONE;
        return false;
    }

    mBytesPerFrame = static_cast<U16>(channels * (bits_per_sample / 8));
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

