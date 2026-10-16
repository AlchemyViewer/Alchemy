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
#include "llatomic.h"

#include <deque>
#include <memory>

// alext.h pulls in the ALC_EXT_EFX function-pointer typedefs (LPALGEN
// EFFECTS, LPALAUXILIARYEFFECTSLOTI, etc.) we resolve via alGetProcAddress.
// Including here keeps the engine's effect-slot / effect handles in the
// public header so the channel class can refer to them.
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

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

        // Per-frame hook. Overridden to add a throttled
        // ALC_EXT_disconnect poll on top of the base bookkeeping —
        // detect a USB-unplug / driver-crash / OS audio-service reset
        // and try alcReopenDeviceSOFT to recover automatically.
        void idle() override;

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

        void setWindGustiness(F32 depth) override
        {
            if (mWindGen) mWindGen->setGustinessDepth(depth);
        }

        // Reverb via ALC_EXT_EFX. Only active when EFX is available on the
        // current device; supportsReverb() reflects that — so the prefs UI
        // hides the controls if the user's OpenAL implementation doesn't
        // ship EFX. The preset name set mirrors FAudio's I3DL2 list (case-
        // insensitive); unknown -> EFX_REVERB_PRESET_PLAIN with a warn.
        bool supportsReverb() const override { return mEfxAvailable; }
        void setReverbPreset(const std::string& preset_name) override;
        void setReverbSendScale(float scale) override;
        std::string getReverbSendScaleSettingName() const override
        {
            return "AudioOpenALReverbSendScale";
        }

        // Channel uses this to wire its AUX send to the engine's effect
        // slot when EFX is active. 0 / AL_EFFECTSLOT_NULL when EFX is
        // disabled, in which case channels skip the AUX send entirely.
        ALuint getEffectSlot() const { return mEffectSlot; }

        LLAudioBuffer* createBuffer();
        LLAudioChannel* createChannel();

        /*virtual*/ bool initWind();
        /*virtual*/ void cleanupWind();
        /*virtual*/ void updateWind(LLVector3 direction, F32 camera_altitude);

    private:
        std::string mPreferredDevice;
        std::string mActiveDevice;

        typedef F32 WIND_SAMPLE_T;
        std::unique_ptr<LLWindGen<WIND_SAMPLE_T>> mWindGen;
        // Single scratch buffer that windGenerate writes into each
        // refill — interleaved L/R, stereo float. Sized by initWind
        // from the per-chunk frame count.
        std::vector<WIND_SAMPLE_T> mWindBuf;
        U32 mWindBufFreq;
        U32 mWindBufSamples;
        U32 mWindBufBytes;
        ALuint mWindSource;

        ALCdevice*  mALCDevice  = nullptr;
        ALCcontext* mALCContext = nullptr;

        // ALC_EXT_disconnect: ALC_CONNECTED query lets us detect a
        // device-loss event (USB unplug, driver crash, OS audio-service
        // restart). Cached at init; false means we don't poll. Recovery
        // is via alcReopenDeviceSOFT to the same preferred device,
        // falling back to system default if the preferred is gone.
        // mDisconnectPollTimer throttles the per-frame poll to once
        // every ~2 seconds; mDeviceDisconnected gates log spam so we
        // log only on the disconnect edge + the reconnect edge.
        // mConsecutiveDisconnects debounces flickering drivers — log
        // the edge only after N consecutive disconnected polls so a
        // bouncy ALC_CONNECTED bit doesn't produce log-spam pairs.
        bool         mDisconnectExtAvailable = false;
        bool         mDeviceDisconnected     = false;
        int          mConsecutiveDisconnects = 0;
        LLFrameTimer mDisconnectPollTimer;

        // ALC_SOFT_system_events + ALC_SOFT_reopen_device let us follow
        // the OS default output device while the viewer is running. Only
        // armed when the user hasn't pinned a specific device — if
        // mPreferredDevice names one, following the OS default would drag
        // them off their choice. mDefaultDeviceChanged is written from
        // OpenAL's own event thread and consumed in idle().
        bool                   mFollowSystemDefault  = false;
        LPALCEVENTCONTROLSOFT  mEventControlSOFT     = nullptr;
        LPALCEVENTCALLBACKSOFT mEventCallbackSOFT    = nullptr;
        LLAtomicBool           mDefaultDeviceChanged{false};

        void initSystemDefaultFollowing();
        static void ALC_APIENTRY onDeviceEventSOFT(ALCenum event_type, ALCenum device_type,
                                                   ALCdevice* device, ALCsizei length,
                                                   const ALCchar* message, void* user_param) noexcept;
        void reopenOnDefaultDevice();

        // Pre-allocated buffer pool. Replaces the historical per-frame
        // alGenBuffers / alDeleteBuffers churn that ran every update
        // tick. At initWind we gen MAX_NUM_WIND_BUFFERS buffer ids
        // once; mWindFreeBuffers is the free-list of currently-
        // unqueued ids. The update tick pulls from the free list,
        // rewrites the buffer contents via alBufferData (re-using
        // the same id), and re-queues. Cleanup deletes the pool
        // once.
        std::vector<ALuint> mWindBufferPool;
        std::deque<ALuint>  mWindFreeBuffers;

        // Pool size. 16 buffers @ 50 ms each = ~800 ms total — plenty
        // of headroom even on bursty schedulers without the 4-second
        // queue the historical MAX=80 reserved. kWindTargetQueueDepth
        // is the desired in-flight (queued) count; we top up to this
        // depth from the free list each tick.
        static constexpr int MAX_NUM_WIND_BUFFERS    = 16;
        static constexpr int kWindTargetQueueDepth   = 8;
        static const float WIND_BUFFER_SIZE_SEC; // 1/20th sec

        // ─── EFX reverb state ───────────────────────────────────────
        // mEfxAvailable is set in init() once we've checked ALC_EXT_EFX
        // and successfully resolved the function pointers + created the
        // effect / slot. False means OpenAL is running but without
        // reverb — supportsReverb() returns false and the prefs UI
        // hides the reverb controls.
        bool   mEfxAvailable = false;
        ALuint mEffectSlot   = 0;   // AL_AUXILIARY_EFFECT_SLOT object
        ALuint mEffect       = 0;   // AL_EFFECT_EAXREVERB object

        // EFX function pointers resolved at init via alGetProcAddress.
        // Stored on the engine so the channel class can reach the SLOT
        // attachment helper without re-resolving on every play. nullptr
        // when EFX isn't available.
        LPALGENEFFECTS                mAlGenEffects                = nullptr;
        LPALDELETEEFFECTS             mAlDeleteEffects             = nullptr;
        LPALEFFECTI                   mAlEffecti                   = nullptr;
        LPALEFFECTF                   mAlEffectf                   = nullptr;
        LPALEFFECTFV                  mAlEffectfv                  = nullptr;
        LPALGENAUXILIARYEFFECTSLOTS   mAlGenAuxiliaryEffectSlots   = nullptr;
        LPALDELETEAUXILIARYEFFECTSLOTS mAlDeleteAuxiliaryEffectSlots = nullptr;
        LPALAUXILIARYEFFECTSLOTI      mAlAuxiliaryEffectSloti      = nullptr;
        LPALAUXILIARYEFFECTSLOTF      mAlAuxiliaryEffectSlotf      = nullptr;

        bool initEfx();          // resolve fns + gen effect/slot, attach
        void shutdownEfx();      // detach + delete + null fn pointers
        void applyReverbPreset(const std::string& name);  // configure mEffect
};

class LLAudioChannelOpenAL : public LLAudioChannel
{
    public:
        LLAudioChannelOpenAL();
        virtual ~LLAudioChannelOpenAL();

        // Engine calls this from shutdownEfx() so we drop our AUX send
        // before the slot is destroyed and reset mAuxSendWired — a
        // subsequent initEfx() would otherwise leave this channel
        // pointing at a stale slot id. Idempotent / safe to call when
        // the channel never wired its send.
        void onEfxShutdown();

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
        // True once the source's AL_AUXILIARY_SEND_FILTER has been
        // wired to the engine's reverb effect slot. play() runs the
        // attach lazily on first use so the engine has had a chance to
        // finish initEfx() before we ask it for its slot id. Stays
        // false when EFX isn't available — the source's AUX send sits
        // at AL_EFFECTSLOT_NULL and contributes nothing to reverb.
        bool   mAuxSendWired = false;
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
