/**
 * @file alaudiodevicenotifier.cpp
 * @brief System audio endpoint change notification
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

#include "alaudiodevicenotifier.h"

#if defined(_WIN32) || defined(_WIN64)
#define AL_NOTIFIER_WMM 1
#elif defined(__APPLE__)
#define AL_NOTIFIER_COREAUDIO 1
#elif defined(__linux__)
#define AL_NOTIFIER_PULSE 1
#endif

#if defined(AL_NOTIFIER_WMM)

#include <windows.h>
#include <mmdeviceapi.h>

namespace llwebrtc
{
namespace
{

class ALWasapiNotifier final : public ALAudioDeviceNotifier, private IMMNotificationClient
{
  public:
    explicit ALWasapiNotifier(Observer* observer) : mObserver(observer) {}

    ~ALWasapiNotifier() override
    {
        if (mEnumerator)
        {
            mEnumerator->UnregisterEndpointNotificationCallback(this);
            mEnumerator->Release();
        }
        if (mOwnsCom)
        {
            CoUninitialize();
        }
    }

    // Must run on the thread that will later destroy this object: CoUninitialize
    // only balances a CoInitializeEx made from the same thread.
    bool start()
    {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (initialized == RPC_E_CHANGED_MODE)
        {
            // The thread is already in a single-threaded apartment.  The
            // enumerator is happy either way, so use it without claiming
            // ownership of the initialization.
        }
        else if (FAILED(initialized))
        {
            return false;
        }
        else
        {
            mOwnsCom = true;
        }

        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                    nullptr,
                                    CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator),
                                    reinterpret_cast<void**>(&mEnumerator))))
        {
            return false;
        }

        return SUCCEEDED(mEnumerator->RegisterEndpointNotificationCallback(this));
    }

  private:
    //
    // IMMNotificationClient
    //

    // An endpoint appearing or disappearing is reported as a state change, so
    // OnDeviceAdded and OnDeviceRemoved would only duplicate this one.
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override
    {
        mObserver->OnDevicesUpdated();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    //
    // IUnknown.  The owning unique_ptr decides when this object dies, so the
    // count is kept only to satisfy COM and never reaches a delete.
    //

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&mRefCount); }
    ULONG STDMETHODCALLTYPE Release() override { return InterlockedDecrement(&mRefCount); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
    {
        if (!object)
        {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient))
        {
            *object = static_cast<IMMNotificationClient*>(this);
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    Observer*            mObserver;
    IMMDeviceEnumerator* mEnumerator = nullptr;
    LONG                 mRefCount   = 1;
    bool                 mOwnsCom    = false;
};

}  // namespace

std::unique_ptr<ALAudioDeviceNotifier> ALAudioDeviceNotifier::create(Observer* observer)
{
    if (!observer)
    {
        return nullptr;
    }

    auto notifier = std::make_unique<ALWasapiNotifier>(observer);
    if (!notifier->start())
    {
        return nullptr;
    }
    return notifier;
}

}  // namespace llwebrtc

#elif defined(AL_NOTIFIER_COREAUDIO)

#include <CoreAudio/CoreAudio.h>

namespace llwebrtc
{
namespace
{

// An endpoint arriving or leaving moves the device list; the system switching
// to a different endpoint on its own moves one of the defaults.
constexpr AudioObjectPropertySelector WATCHED_SELECTORS[] = {
    kAudioHardwarePropertyDevices,
    kAudioHardwarePropertyDefaultInputDevice,
    kAudioHardwarePropertyDefaultOutputDevice,
};

class ALCoreAudioNotifier final : public ALAudioDeviceNotifier
{
  public:
    explicit ALCoreAudioNotifier(Observer* observer) : mObserver(observer) {}

    ~ALCoreAudioNotifier() override
    {
        // Unwind only what was registered, so a partial start still cleans up.
        while (mRegistered > 0)
        {
            const AudioObjectPropertyAddress address = addressFor(WATCHED_SELECTORS[--mRegistered]);
            AudioObjectRemovePropertyListener(kAudioObjectSystemObject, &address, &onPropertyChanged, this);
        }
    }

    bool start()
    {
        for (const AudioObjectPropertySelector selector : WATCHED_SELECTORS)
        {
            const AudioObjectPropertyAddress address = addressFor(selector);
            if (AudioObjectAddPropertyListener(kAudioObjectSystemObject, &address, &onPropertyChanged, this) != noErr)
            {
                return false;
            }
            ++mRegistered;
        }
        return true;
    }

  private:
    static AudioObjectPropertyAddress addressFor(AudioObjectPropertySelector selector)
    {
        return { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    }

    static OSStatus onPropertyChanged(AudioObjectID,
                                      UInt32,
                                      const AudioObjectPropertyAddress*,
                                      void* context)
    {
        static_cast<ALCoreAudioNotifier*>(context)->mObserver->OnDevicesUpdated();
        return noErr;
    }

    Observer* mObserver;
    unsigned  mRegistered = 0;
};

}  // namespace

std::unique_ptr<ALAudioDeviceNotifier> ALAudioDeviceNotifier::create(Observer* observer)
{
    if (!observer)
    {
        return nullptr;
    }

    auto notifier = std::make_unique<ALCoreAudioNotifier>(observer);
    if (!notifier->start())
    {
        return nullptr;
    }
    return notifier;
}

}  // namespace llwebrtc

#elif defined(AL_NOTIFIER_PULSE)

#include <cstdint>
#include <dlfcn.h>

namespace llwebrtc
{
namespace
{

// The handful of libpulse declarations used here, mirrored so that the library
// can be resolved at run time.  A host without PulseAudio -- one running ALSA
// straight, say -- then goes without notifications instead of failing to load.
// PipeWire is reached the same way, through its PulseAudio compatibility layer.
struct pa_threaded_mainloop;
struct pa_mainloop_api;
struct pa_context;
struct pa_operation;
struct pa_spawn_api;

using pa_context_notify_cb_t    = void (*)(pa_context*, void*);
using pa_context_success_cb_t   = void (*)(pa_context*, int, void*);
using pa_context_subscribe_cb_t = void (*)(pa_context*, int, uint32_t, void*);

enum
{
    PA_CONTEXT_READY      = 4,
    PA_CONTEXT_FAILED     = 5,
    PA_CONTEXT_TERMINATED = 6,
};

enum
{
    PA_SUBSCRIPTION_MASK_SINK   = 0x0001u,
    PA_SUBSCRIPTION_MASK_SOURCE = 0x0002u,
    PA_SUBSCRIPTION_MASK_SERVER = 0x0080u,
    PA_SUBSCRIPTION_MASK_CARD   = 0x0200u,
};

// Sinks and sources cover endpoints coming and going, cards cover a device
// changing profile, and the server event covers either default moving.
constexpr int WATCHED_EVENTS = PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE |
                               PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_CARD;

// dlsym() returns void*, but our slots are typed function pointers. Casting
// through void** trips -Wstrict-aliasing unless the slot is may-alias.
typedef void* __attribute__((__may_alias__)) al_pulse_void_p_alias;

struct ALPulseSymbols
{
    pa_threaded_mainloop* (*mainloop_new)(void);
    void (*mainloop_free)(pa_threaded_mainloop*);
    int (*mainloop_start)(pa_threaded_mainloop*);
    void (*mainloop_stop)(pa_threaded_mainloop*);
    void (*mainloop_lock)(pa_threaded_mainloop*);
    void (*mainloop_unlock)(pa_threaded_mainloop*);
    void (*mainloop_wait)(pa_threaded_mainloop*);
    void (*mainloop_signal)(pa_threaded_mainloop*, int);
    pa_mainloop_api* (*mainloop_get_api)(pa_threaded_mainloop*);

    pa_context* (*context_new)(pa_mainloop_api*, const char*);
    void (*context_unref)(pa_context*);
    void (*context_set_state_callback)(pa_context*, pa_context_notify_cb_t, void*);
    int (*context_get_state)(const pa_context*);
    int (*context_connect)(pa_context*, const char*, int, const pa_spawn_api*);
    void (*context_disconnect)(pa_context*);
    void (*context_set_subscribe_callback)(pa_context*, pa_context_subscribe_cb_t, void*);
    pa_operation* (*context_subscribe)(pa_context*, int, pa_context_success_cb_t, void*);

    void (*operation_unref)(pa_operation*);
};

class ALPulseNotifier final : public ALAudioDeviceNotifier
{
  public:
    explicit ALPulseNotifier(Observer* observer) : mObserver(observer) {}

    ~ALPulseNotifier() override
    {
        if (mMainloop)
        {
            if (mContext)
            {
                mPulse.mainloop_lock(mMainloop);
                mPulse.context_set_subscribe_callback(mContext, nullptr, nullptr);
                mPulse.context_set_state_callback(mContext, nullptr, nullptr);
                mPulse.context_disconnect(mContext);
                mPulse.mainloop_unlock(mMainloop);
            }

            // Stop the loop thread before releasing anything it might touch.
            if (mMainloopRunning)
            {
                mPulse.mainloop_stop(mMainloop);
            }

            if (mContext)
            {
                mPulse.context_unref(mContext);
            }
            mPulse.mainloop_free(mMainloop);
        }

        if (mLibrary)
        {
            dlclose(mLibrary);
        }
    }

    bool start()
    {
        // RTLD_NODELETE keeps libpulse mapped past dlclose, so nothing it
        // registered can dangle if this object outlives its own teardown.
        mLibrary = dlopen("libpulse.so.0", RTLD_LAZY | RTLD_NODELETE);
        if (!mLibrary && !(mLibrary = dlopen("libpulse.so", RTLD_LAZY | RTLD_NODELETE)))
        {
            return false;
        }

        if (!loadSymbols())
        {
            return false;
        }

        mMainloop = mPulse.mainloop_new();
        if (!mMainloop || mPulse.mainloop_start(mMainloop) < 0)
        {
            return false;
        }
        mMainloopRunning = true;

        mPulse.mainloop_lock(mMainloop);
        const bool connected = connectLocked();
        mPulse.mainloop_unlock(mMainloop);
        return connected;
    }

  private:
    bool loadSymbols()
    {
#define AL_PULSE_LOAD(member, symbol)                                                       \
    do                                                                                      \
    {                                                                                       \
        void* address = dlsym(mLibrary, symbol);                                            \
        if (!address)                                                                       \
        {                                                                                   \
            return false;                                                                   \
        }                                                                                   \
        *reinterpret_cast<al_pulse_void_p_alias*>(&mPulse.member) = address;                   \
    } while (false)

        AL_PULSE_LOAD(mainloop_new, "pa_threaded_mainloop_new");
        AL_PULSE_LOAD(mainloop_free, "pa_threaded_mainloop_free");
        AL_PULSE_LOAD(mainloop_start, "pa_threaded_mainloop_start");
        AL_PULSE_LOAD(mainloop_stop, "pa_threaded_mainloop_stop");
        AL_PULSE_LOAD(mainloop_lock, "pa_threaded_mainloop_lock");
        AL_PULSE_LOAD(mainloop_unlock, "pa_threaded_mainloop_unlock");
        AL_PULSE_LOAD(mainloop_wait, "pa_threaded_mainloop_wait");
        AL_PULSE_LOAD(mainloop_signal, "pa_threaded_mainloop_signal");
        AL_PULSE_LOAD(mainloop_get_api, "pa_threaded_mainloop_get_api");
        AL_PULSE_LOAD(context_new, "pa_context_new");
        AL_PULSE_LOAD(context_unref, "pa_context_unref");
        AL_PULSE_LOAD(context_set_state_callback, "pa_context_set_state_callback");
        AL_PULSE_LOAD(context_get_state, "pa_context_get_state");
        AL_PULSE_LOAD(context_connect, "pa_context_connect");
        AL_PULSE_LOAD(context_disconnect, "pa_context_disconnect");
        AL_PULSE_LOAD(context_set_subscribe_callback, "pa_context_set_subscribe_callback");
        AL_PULSE_LOAD(context_subscribe, "pa_context_subscribe");
        AL_PULSE_LOAD(operation_unref, "pa_operation_unref");

#undef AL_PULSE_LOAD
        return true;
    }

    // Called with the mainloop lock held.
    bool connectLocked()
    {
        mContext = mPulse.context_new(mPulse.mainloop_get_api(mMainloop), "Alchemy Voice");
        if (!mContext)
        {
            return false;
        }

        mPulse.context_set_state_callback(mContext, &onStateChanged, this);
        if (mPulse.context_connect(mContext, nullptr, 0, nullptr) < 0)
        {
            return false;
        }

        for (;;)
        {
            const int state = mPulse.context_get_state(mContext);
            if (state == PA_CONTEXT_READY)
            {
                break;
            }
            if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            {
                return false;
            }
            mPulse.mainloop_wait(mMainloop);
        }

        mPulse.context_set_subscribe_callback(mContext, &onSubscribedEvent, this);
        if (pa_operation* subscription = mPulse.context_subscribe(mContext, WATCHED_EVENTS, nullptr, nullptr))
        {
            mPulse.operation_unref(subscription);
            return true;
        }
        return false;
    }

    // Wakes connectLocked() out of mainloop_wait() as the connection advances.
    static void onStateChanged(pa_context*, void* context)
    {
        ALPulseNotifier* self = static_cast<ALPulseNotifier*>(context);
        self->mPulse.mainloop_signal(self->mMainloop, 0);
    }

    static void onSubscribedEvent(pa_context*, int, uint32_t, void* context)
    {
        static_cast<ALPulseNotifier*>(context)->mObserver->OnDevicesUpdated();
    }

    Observer*             mObserver;
    ALPulseSymbols        mPulse{};
    void*                 mLibrary         = nullptr;
    pa_threaded_mainloop* mMainloop        = nullptr;
    pa_context*           mContext         = nullptr;
    bool                  mMainloopRunning = false;
};

}  // namespace

std::unique_ptr<ALAudioDeviceNotifier> ALAudioDeviceNotifier::create(Observer* observer)
{
    if (!observer)
    {
        return nullptr;
    }

    auto notifier = std::make_unique<ALPulseNotifier>(observer);
    if (!notifier->start())
    {
        return nullptr;
    }
    return notifier;
}

}  // namespace llwebrtc

#else

namespace llwebrtc
{

std::unique_ptr<ALAudioDeviceNotifier> ALAudioDeviceNotifier::create(Observer*)
{
    return nullptr;
}

}  // namespace llwebrtc

#endif
