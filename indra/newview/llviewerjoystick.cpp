/**
 * @file llviewerjoystick.cpp
 * @brief Joystick / NDOF device functionality.
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

#include "llviewerprecompiledheaders.h"

#include "llviewerjoystick.h"

#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llviewercamera.h"
#include "llappviewer.h"
#include "llkeyboard.h"
#include "lltoolmgr.h"
#include "llselectmgr.h"
#include "llviewermenu.h"
#include "llviewerwindow.h"
#include "llwindow.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llfocusmgr.h"

#if LL_WINDOWS && !LL_MESA_HEADLESS
// Require DirectInput version 8
#define DIRECTINPUT_VERSION 0x0800

#include <dinput.h>
#endif


// ----------------------------------------------------------------------------
// Constants

#define  X_I    1
#define  Y_I    2
#define  Z_I    0
#define RX_I    4
#define RY_I    5
#define RZ_I    3

std::array<F32, 7>  LLViewerJoystick::sLastDelta = {0,0,0,0,0,0,0};
std::array<F32, 7>  LLViewerJoystick::sDelta = {0,0,0,0,0,0,0};

// These constants specify the maximum absolute value coming in from the device.
// HACK ALERT! the value of MAX_JOYSTICK_INPUT_VALUE is not arbitrary as it
// should be.  It has to be equal to 3000 because the SpaceNavigator on Windows
// refuses to respond to the DirectInput SetProperty call; it always returns
// values in the [-3000, 3000] range.
#define MAX_SPACENAVIGATOR_INPUT  3000.0f
#define MAX_JOYSTICK_INPUT_VALUE  MAX_SPACENAVIGATOR_INPUT


#if LIB_NDOF
std::ostream& operator<<(std::ostream& out, NDOF_Device* ptr)
{
    if (! ptr)
    {
        return out << "nullptr";
    }
    out << "NDOF_Device{ ";
    out << "axes [";
    const char* delim = "";
    for (short axis = 0; axis < ptr->axes_count; ++axis)
    {
        out << delim << ptr->axes[axis];
        delim = ", ";
    }
    out << "]";
    out << ", buttons [";
    delim = "";
    for (short button = 0; button < ptr->btn_count; ++button)
    {
        out << delim << ptr->buttons[button];
        delim = ", ";
    }
    out << "]";
    out << ", range " << ptr->axes_min << ':' << ptr->axes_max;
    // If we don't coerce these to unsigned, they're streamed as characters,
    // e.g. ctrl-A or nul.
    out << ", absolute " << unsigned(ptr->absolute);
    out << ", valid " << unsigned(ptr->valid);
    out << ", manufacturer '" << ptr->manufacturer << "'";
    out << ", product '" << ptr->product << "'";
    out << ", private " << ptr->private_data;
    out << " }";
    return out;
}
#endif // LIB_NDOF


#if LL_WINDOWS && !LL_MESA_HEADLESS
// this should reflect ndof and set axises, see ndofdev_win.cpp from ndof package
BOOL CALLBACK EnumObjectsCallback(const DIDEVICEOBJECTINSTANCE* inst, VOID* user_data)
{
    if (inst->dwType & DIDFT_AXIS)
    {
        LPDIRECTINPUTDEVICE8 device = *((LPDIRECTINPUTDEVICE8 *)user_data);
        DIPROPRANGE diprg;
        diprg.diph.dwSize = sizeof(DIPROPRANGE);
        diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        diprg.diph.dwHow = DIPH_BYID;
        diprg.diph.dwObj = inst->dwType; // specify the enumerated axis

        // Set the range for the axis
        diprg.lMin = (long)-MAX_JOYSTICK_INPUT_VALUE;
        diprg.lMax = (long)+MAX_JOYSTICK_INPUT_VALUE;
        HRESULT hr = device->SetProperty(DIPROP_RANGE, &diprg.diph);

        if (FAILED(hr))
        {
            return DIENUM_STOP;
        }
    }

    return DIENUM_CONTINUE;
}

BOOL CALLBACK di8_devices_callback(LPCDIDEVICEINSTANCE device_instance_ptr, LPVOID pvRef)
{
    // Note: If a single device can function as more than one DirectInput
    // device type, it is enumerated as each device type that it supports.
    // Capable of detecting devices like Oculus Rift
    if (device_instance_ptr)
    {
        std::string product_name = ll_convert_wide_to_string(std::wstring(device_instance_ptr->tszProductName));

        LLSD guid = LLViewerJoystick::getInstance()->getDeviceUUID();

        bool init_device = false;
        if (guid.isBinary())
        {
            std::vector<U8> bin_bucket = guid.asBinary();
            init_device = memcmp(&bin_bucket[0], &device_instance_ptr->guidInstance, sizeof(GUID)) == 0;
        }
        else
        {
            // It might be better to init space navigator here, but if system doesn't has one,
            // ndof will pick a random device, it is simpler to pick first device now to have an id
            init_device = true;
        }

        if (init_device)
        {
            LL_DEBUGS("Joystick") << "Found and attempting to use device: " << product_name << LL_ENDL;
            LPDIRECTINPUT8       di8_interface = *((LPDIRECTINPUT8 *)gViewerWindow->getWindow()->getDirectInput8());
            LPDIRECTINPUTDEVICE8 device = NULL;

            HRESULT status = di8_interface->CreateDevice(
                device_instance_ptr->guidInstance, // REFGUID rguid,
                &device,                           // LPDIRECTINPUTDEVICE * lplpDirectInputDevice,
                NULL                               // LPUNKNOWN pUnkOuter
                );

            if (status == DI_OK)
            {
                // prerequisite for aquire()
                LL_DEBUGS("Joystick") << "Device created" << LL_ENDL;
                status = device->SetDataFormat(&c_dfDIJoystick); // c_dfDIJoystick2
            }

            if (status == DI_OK)
            {
                // set properties
                LL_DEBUGS("Joystick") << "Format set" << LL_ENDL;
                status = device->EnumObjects(EnumObjectsCallback, &device, DIDFT_ALL);
            }

            if (status == DI_OK)
            {
                LL_DEBUGS("Joystick") << "Properties updated" << LL_ENDL;

                S32 size = sizeof(GUID);
                LLSD::Binary binary_data; //just an std::vector
                binary_data.resize(size);
                memcpy(&binary_data[0], &device_instance_ptr->guidInstance /*POD _GUID*/, size);
                LLSD data(binary_data);
                LLViewerJoystick::getInstance()->initDevice(&device, product_name, data);
                return DIENUM_STOP;
            }
        }
        else
        {
            LL_DEBUGS("Joystick") << "Found device: " << product_name << LL_ENDL;
        }
    }
    return DIENUM_CONTINUE;
}

// Windows guids
// This is GUID2 so teoretically it can be memcpy copied into LLUUID
void guid_from_string(GUID &guid, const std::string &input)
{
    CLSIDFromString(ll_convert_string_to_wide(input).c_str(), &guid);
}

std::string string_from_guid(const GUID &guid)
{
    OLECHAR* guidString; //wchat
    StringFromCLSID(guid, &guidString);

    // use guidString...

    std::string res = ll_convert_wide_to_string(std::wstring(guidString));
    // ensure memory is freed
    ::CoTaskMemFree(guidString);

    return res;
}
#elif LL_DARWIN

bool macos_devices_callback(std::string &product_name, LLSD &data, void* userdata)
{
    std::string product = data["product"].asString();

    return LLViewerJoystick::getInstance()->initDevice(nullptr, product, data);
}

#endif


// -----------------------------------------------------------------------------
void LLViewerJoystick::updateEnabled(bool autoenable)
{
    if (mDriverState == JDS_UNINITIALIZED)
    {
        gSavedSettings.setBOOL("JoystickEnabled", FALSE );
    }
    else
    {
        // autoenable if user specifically chose this device
        if (autoenable && (isLikeSpaceNavigator() || isDeviceUUIDSet()))
        {
            gSavedSettings.setBOOL("JoystickEnabled", TRUE );
        }
    }
    if (!gSavedSettings.getBOOL("JoystickEnabled"))
    {
        mOverrideCamera = FALSE;
    }
}

void LLViewerJoystick::setOverrideCamera(bool val)
{
    if (!mJoystickEnabled)
    {
        mOverrideCamera = FALSE;
    }
    else
    {
        mOverrideCamera = val;
    }

    if (mOverrideCamera)
    {
        gAgentCamera.changeCameraToDefault();
    }
}

// -----------------------------------------------------------------------------
#if LIB_NDOF
NDOF_HotPlugResult LLViewerJoystick::HotPlugAddCallback(NDOF_Device *dev)
{
    NDOF_HotPlugResult res = NDOF_DISCARD_HOTPLUGGED;
    LLViewerJoystick* joystick(LLViewerJoystick::getInstance());
    if (joystick->mDriverState == JDS_UNINITIALIZED)
    {
        LL_INFOS("Joystick") << "HotPlugAddCallback: will use device:" << LL_ENDL;
        ndof_dump(stderr, dev);
        joystick->mNdofDev = dev;
        joystick->mDriverState = JDS_INITIALIZED;
        res = NDOF_KEEP_HOTPLUGGED;
    }
    joystick->updateEnabled(true);
    return res;
}
#endif

// -----------------------------------------------------------------------------
#if LIB_NDOF
void LLViewerJoystick::HotPlugRemovalCallback(NDOF_Device *dev)
{
    LLViewerJoystick* joystick(LLViewerJoystick::getInstance());
    if (joystick->mNdofDev == dev)
    {
        LL_INFOS("Joystick") << "HotPlugRemovalCallback: joystick->mNdofDev="
                << joystick->mNdofDev << "; removed device:" << LL_ENDL;
        ndof_dump(stderr, dev);
        joystick->mDriverState = JDS_UNINITIALIZED;
    }
    joystick->updateEnabled(true);
}
#endif

// -----------------------------------------------------------------------------
LLViewerJoystick::LLViewerJoystick()
:   mDriverState(JDS_UNINITIALIZED),
    mNdofDev(NULL),
    mResetFlag(false),
    mCameraUpdated(true),
    mOverrideCamera(false),
    mJoystickRun(0)
{
    for (int i = 0; i < 6; i++)
    {
        mAxes[i] = sDelta[i] = sLastDelta[i] = 0.0f;
    }

    memset(mBtn, 0, sizeof(mBtn));

    // factor in bandwidth? bandwidth = gViewerStats->mKBitStat
    mPerfScale = 4000.f / gSysCPU.getMHz(); // hmm.  why?

    mLastDeviceUUID = LLSD::Integer(1);

    gSavedSettings.getControl("JoystickEnabled")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("JoystickAxis0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickAxis1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickAxis2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickAxis3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickAxis4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickAxis5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickAxis6")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("Cursor3D")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AutoLeveling")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("ZoomDirect")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("JoystickAvatarEnabled")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickBuildEnabled")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("JoystickFlycamEnabled")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("AvatarAxisScale0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisScale1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisScale2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisScale3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisScale4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisScale5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("BuildAxisScale0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisScale1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisScale2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisScale3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisScale4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisScale5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("FlycamAxisScale0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisScale1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisScale2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisScale3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisScale4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisScale5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisScale6")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("AvatarAxisDeadZone0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisDeadZone1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisDeadZone2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisDeadZone3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisDeadZone4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("AvatarAxisDeadZone5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("BuildAxisDeadZone0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisDeadZone1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisDeadZone2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisDeadZone3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisDeadZone4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildAxisDeadZone5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("FlycamAxisDeadZone0")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisDeadZone1")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisDeadZone2")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisDeadZone3")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisDeadZone4")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisDeadZone5")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamAxisDeadZone6")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("AvatarFeathering")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("BuildFeathering")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });
    gSavedSettings.getControl("FlycamFeathering")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    gSavedSettings.getControl("JoystickInvertPitch")->getCommitSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { refreshFromSettings(); });

    refreshFromSettings();
}

// -----------------------------------------------------------------------------
LLViewerJoystick::~LLViewerJoystick()
{
    if (mDriverState == JDS_INITIALIZED)
    {
        terminate();
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::init(bool autoenable)
{
#if LIB_NDOF
    static bool libinit = false;
    mDriverState = JDS_INITIALIZING;

    loadDeviceIdFromSettings();

    if (libinit == false)
    {
        // Note: The HotPlug callbacks are not actually getting called on Windows
        if (ndof_libinit(HotPlugAddCallback,
                         HotPlugRemovalCallback,
                         gViewerWindow->getWindow()->getDirectInput8()))
        {
            mDriverState = JDS_UNINITIALIZED;
        }
        else
        {
            // NB: ndof_libinit succeeds when there's no device
            libinit = true;

            // allocate memory once for an eventual device
            mNdofDev = ndof_create();
        }
    }

    if (libinit)
    {
        if (mNdofDev)
        {
            U32 device_type = 0;
            void* win_callback = nullptr;
            std::function<bool(std::string&, LLSD&, void*)> osx_callback;
            // di8_devices_callback callback is immediate and happens in scope of getInputDevices()
#if LL_WINDOWS && !LL_MESA_HEADLESS
            // space navigator is marked as DI8DEVCLASS_GAMECTRL in ndof lib
            device_type = DI8DEVCLASS_GAMECTRL;
            win_callback = &di8_devices_callback;
#elif LL_DARWIN
            osx_callback = macos_devices_callback;

            if (mLastDeviceUUID.isMap())
            {
                std::string manufacturer = mLastDeviceUUID["manufacturer"].asString();
                std::string product = mLastDeviceUUID["product"].asString();

                strncpy(mNdofDev->manufacturer, manufacturer.c_str(), sizeof(mNdofDev->manufacturer));
                strncpy(mNdofDev->product, product.c_str(), sizeof(mNdofDev->product));

                if (ndof_init_first(mNdofDev, nullptr))
                {
                    mDriverState = JDS_INITIALIZING;
                    // Saved device no longer exist
                    // No device found
                    LL_WARNS() << "ndof_init_first FAILED" << LL_ENDL;
                }
                else
                {
                    mDriverState = JDS_INITIALIZED;
                }
            }
#endif
            if (mDriverState != JDS_INITIALIZED)
            {
                if (!gViewerWindow->getWindow()->getInputDevices(device_type, osx_callback, win_callback, NULL))
                {
                    LL_INFOS("Joystick") << "Failed to gather input devices. Falling back to ndof's init" << LL_ENDL;
                    // Failed to gather devices, init first suitable one
                    mLastDeviceUUID = LLSD();
                    void *preffered_device = NULL;
                    initDevice(preffered_device);
                }
            }

            if (mDriverState == JDS_INITIALIZING)
            {
                LL_INFOS("Joystick") << "Found no matching joystick devices." << LL_ENDL;
                mDriverState = JDS_UNINITIALIZED;
            }
        }
        else
        {
            mDriverState = JDS_UNINITIALIZED;
        }
    }

    // Autoenable the joystick for recognized devices if nothing was connected previously
    if (!autoenable)
    {
        autoenable = gSavedSettings.getString("JoystickInitialized").empty() ? true : false;
    }
    updateEnabled(autoenable);

    if (mDriverState == JDS_INITIALIZED)
    {
        // A Joystick device is plugged in
        if (isLikeSpaceNavigator())
        {
            // It's a space navigator, we have defaults for it.
            if (gSavedSettings.getString("JoystickInitialized") != "SpaceNavigator")
            {
                // Only set the defaults if we haven't already (in case they were overridden)
                setSNDefaults();
                gSavedSettings.setString("JoystickInitialized", "SpaceNavigator");
            }
        }
        else
        {
            // It's not a Space Navigator
            gSavedSettings.setString("JoystickInitialized", "UnknownDevice");
        }
    }
    else
    {
        // No device connected, don't change any settings
    }

    LL_INFOS("Joystick") << "ndof: mDriverState=" << mDriverState << "; mNdofDev="
            << mNdofDev << "; libinit=" << libinit << LL_ENDL;
#endif
}

void LLViewerJoystick::refreshFromSettings()
{
    mJoystickEnabled = gSavedSettings.getBOOL("JoystickEnabled");

    mJoystickAxis[0] = gSavedSettings.getS32("JoystickAxis0");
    mJoystickAxis[1] = gSavedSettings.getS32("JoystickAxis1");
    mJoystickAxis[2] = gSavedSettings.getS32("JoystickAxis2");
    mJoystickAxis[3] = gSavedSettings.getS32("JoystickAxis3");
    mJoystickAxis[4] = gSavedSettings.getS32("JoystickAxis4");
    mJoystickAxis[5] = gSavedSettings.getS32("JoystickAxis5");
    mJoystickAxis[6] = gSavedSettings.getS32("JoystickAxis6");

    m3DCursor = gSavedSettings.getBOOL("Cursor3D");
    mAutoLeveling = gSavedSettings.getBOOL("AutoLeveling");
    mZoomDirect = gSavedSettings.getBOOL("ZoomDirect");

    mAvatarEnabled = gSavedSettings.getBOOL("JoystickAvatarEnabled");
    mBuildEnabled = gSavedSettings.getBOOL("JoystickBuildEnabled");
    mFlycamEnabled = gSavedSettings.getBOOL("JoystickFlycamEnabled");

    mAvatarAxisScale[0] = gSavedSettings.getF32("AvatarAxisScale0");
    mAvatarAxisScale[1] = gSavedSettings.getF32("AvatarAxisScale1");
    mAvatarAxisScale[2] = gSavedSettings.getF32("AvatarAxisScale2");
    mAvatarAxisScale[3] = gSavedSettings.getF32("AvatarAxisScale3");
    mAvatarAxisScale[4] = gSavedSettings.getF32("AvatarAxisScale4");
    mAvatarAxisScale[5] = gSavedSettings.getF32("AvatarAxisScale5");

    mBuildAxisScale[0] = gSavedSettings.getF32("BuildAxisScale0");
    mBuildAxisScale[1] = gSavedSettings.getF32("BuildAxisScale1");
    mBuildAxisScale[2] = gSavedSettings.getF32("BuildAxisScale2");
    mBuildAxisScale[3] = gSavedSettings.getF32("BuildAxisScale3");
    mBuildAxisScale[4] = gSavedSettings.getF32("BuildAxisScale4");
    mBuildAxisScale[5] = gSavedSettings.getF32("BuildAxisScale5");

    mFlycamAxisScale[0] = gSavedSettings.getF32("FlycamAxisScale0");
    mFlycamAxisScale[1] = gSavedSettings.getF32("FlycamAxisScale1");
    mFlycamAxisScale[2] = gSavedSettings.getF32("FlycamAxisScale2");
    mFlycamAxisScale[3] = gSavedSettings.getF32("FlycamAxisScale3");
    mFlycamAxisScale[4] = gSavedSettings.getF32("FlycamAxisScale4");
    mFlycamAxisScale[5] = gSavedSettings.getF32("FlycamAxisScale5");
    mFlycamAxisScale[6] = gSavedSettings.getF32("FlycamAxisScale6");

    mAvatarAxisDeadZone[0] = gSavedSettings.getF32("AvatarAxisDeadZone0");
    mAvatarAxisDeadZone[1] = gSavedSettings.getF32("AvatarAxisDeadZone1");
    mAvatarAxisDeadZone[2] = gSavedSettings.getF32("AvatarAxisDeadZone2");
    mAvatarAxisDeadZone[3] = gSavedSettings.getF32("AvatarAxisDeadZone3");
    mAvatarAxisDeadZone[4] = gSavedSettings.getF32("AvatarAxisDeadZone4");
    mAvatarAxisDeadZone[5] = gSavedSettings.getF32("AvatarAxisDeadZone5");

    mBuildAxisDeadZone[0] = gSavedSettings.getF32("BuildAxisDeadZone0");
    mBuildAxisDeadZone[1] = gSavedSettings.getF32("BuildAxisDeadZone1");
    mBuildAxisDeadZone[2] = gSavedSettings.getF32("BuildAxisDeadZone2");
    mBuildAxisDeadZone[3] = gSavedSettings.getF32("BuildAxisDeadZone3");
    mBuildAxisDeadZone[4] = gSavedSettings.getF32("BuildAxisDeadZone4");
    mBuildAxisDeadZone[5] = gSavedSettings.getF32("BuildAxisDeadZone5");

    mFlycamAxisDeadZone[0] = gSavedSettings.getF32("FlycamAxisDeadZone0");
    mFlycamAxisDeadZone[1] = gSavedSettings.getF32("FlycamAxisDeadZone1");
    mFlycamAxisDeadZone[2] = gSavedSettings.getF32("FlycamAxisDeadZone2");
    mFlycamAxisDeadZone[3] = gSavedSettings.getF32("FlycamAxisDeadZone3");
    mFlycamAxisDeadZone[4] = gSavedSettings.getF32("FlycamAxisDeadZone4");
    mFlycamAxisDeadZone[5] = gSavedSettings.getF32("FlycamAxisDeadZone5");
    mFlycamAxisDeadZone[6] = gSavedSettings.getF32("FlycamAxisDeadZone6");

    mAvatarFeathering = gSavedSettings.getF32("AvatarFeathering"); // note: max feather is 32.0
    mBuildFeathering = gSavedSettings.getF32("BuildFeathering");
    mFlycamFeathering = gSavedSettings.getF32("FlycamFeathering");

    mInvertPitch = gSavedSettings.getBOOL("JoystickInvertPitch");
}

void LLViewerJoystick::initDevice(LLSD &guid)
{
#if LIB_NDOF
    mLastDeviceUUID = guid;
    U32 device_type = 0;
    void* win_callback = nullptr;
    std::function<bool(std::string&, LLSD&, void*)> osx_callback;
    mDriverState = JDS_INITIALIZING;

#if LL_WINDOWS && !LL_MESA_HEADLESS
    // space navigator is marked as DI8DEVCLASS_GAMECTRL in ndof lib
    device_type = DI8DEVCLASS_GAMECTRL;
    win_callback = &di8_devices_callback;
#elif LL_DARWIN
    osx_callback = macos_devices_callback;
    if (mLastDeviceUUID.isMap())
    {
        std::string manufacturer = mLastDeviceUUID["manufacturer"].asString();
        std::string product = mLastDeviceUUID["product"].asString();

        strncpy(mNdofDev->manufacturer, manufacturer.c_str(), sizeof(mNdofDev->manufacturer));
        strncpy(mNdofDev->product, product.c_str(), sizeof(mNdofDev->product));

        if (ndof_init_first(mNdofDev, nullptr))
        {
            mDriverState = JDS_INITIALIZING;
            // Saved device no longer exist
            // Np other device present
            LL_WARNS() << "ndof_init_first FAILED" << LL_ENDL;
        }
        else
        {
            mDriverState = JDS_INITIALIZED;
        }
    }
#endif

    if (mDriverState != JDS_INITIALIZED)
    {
        if (!gViewerWindow->getWindow()->getInputDevices(device_type, osx_callback, win_callback, NULL))
        {
            LL_INFOS("Joystick") << "Failed to gather input devices. Falling back to ndof's init" << LL_ENDL;
            // Failed to gather devices from window, init first suitable one
            void *preffered_device = NULL;
            mLastDeviceUUID = LLSD();
            initDevice(preffered_device);
        }
    }

    if (mDriverState == JDS_INITIALIZING)
    {
        LL_INFOS("Joystick") << "Found no matching joystick devices." << LL_ENDL;
        mDriverState = JDS_UNINITIALIZED;
    }
#endif
}

bool LLViewerJoystick::initDevice(void * preffered_device /*LPDIRECTINPUTDEVICE8*/, std::string &name, LLSD &guid)
{
#if LIB_NDOF
    mLastDeviceUUID = guid;

#if LL_DARWIN
    if (guid.isMap())
    {
        std::string manufacturer = mLastDeviceUUID["manufacturer"].asString();
        std::string product = mLastDeviceUUID["product"].asString();

        strncpy(mNdofDev->manufacturer, manufacturer.c_str(), sizeof(mNdofDev->manufacturer));
        strncpy(mNdofDev->product, product.c_str(), sizeof(mNdofDev->product));
    }
    else
    {
        mNdofDev->product[0] = '\0';
        mNdofDev->manufacturer[0] = '\0';
    }
#else
    size_t dest_size = sizeof(mNdofDev->product);
    strncpy(mNdofDev->product, name.c_str(), dest_size);
    mNdofDev->product[dest_size-1] = '\0';

    mNdofDev->manufacturer[0] = '\0';
#endif

    return initDevice(preffered_device);
#else
    return false;
#endif
}

bool LLViewerJoystick::initDevice(void * preffered_device /* LPDIRECTINPUTDEVICE8* */)
{
#if LIB_NDOF
    // Different joysticks will return different ranges of raw values.
    // Since we want to handle every device in the same uniform way,
    // we initialize the mNdofDev struct and we set the range
    // of values we would like to receive.
    //
    // HACK: On Windows, libndofdev passes our range to DI with a
    // SetProperty call. This works but with one notable exception, the
    // SpaceNavigator, who doesn't seem to care about the SetProperty
    // call. In theory, we should handle this case inside libndofdev.
    // However, the range we're setting here is arbitrary anyway,
    // so let's just use the SpaceNavigator range for our purposes.
    mNdofDev->axes_min = (long)-MAX_JOYSTICK_INPUT_VALUE;
    mNdofDev->axes_max = (long)+MAX_JOYSTICK_INPUT_VALUE;

    // libndofdev could be used to return deltas.  Here we choose to
    // just have the absolute values instead.
    mNdofDev->absolute = 1;
    // init & use the first suitable NDOF device found on the USB chain
    // On windows preffered_device needs to be a pointer to LPDIRECTINPUTDEVICE8
    if (ndof_init_first(mNdofDev, preffered_device))
    {
        mDriverState = JDS_UNINITIALIZED;
        LL_WARNS() << "ndof_init_first FAILED" << LL_ENDL;
    }
    else
    {
        mDriverState = JDS_INITIALIZED;
        return true;
    }
#endif
    return false;
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::terminate()
{
#if LIB_NDOF
    if (mNdofDev != NULL)
    {
        ndof_libcleanup(); // frees alocated memory in mNdofDev
        mDriverState = JDS_UNINITIALIZED;
        mNdofDev = NULL;
        LL_INFOS("Joystick") << "Terminated connection with NDOF device." << LL_ENDL;
    }
#endif
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::updateStatus()
{
#if LIB_NDOF
    if (!mNdofDev || !mNdofDev->private_data)
    {
        return;
    }

    ndof_update(mNdofDev);

    for (int i=0; i<6; i++)
    {
        mAxes[i] = (F32) mNdofDev->axes[i] / mNdofDev->axes_max;
    }

    for (int i=0; i<16; i++)
    {
        mBtn[i] = mNdofDev->buttons[i];
    }

#endif
}

// -----------------------------------------------------------------------------
F32 LLViewerJoystick::getJoystickAxis(U32 axis) const
{
    if (axis < 6)
    {
        return mAxes[axis];
    }
    return 0.f;
}

// -----------------------------------------------------------------------------
U32 LLViewerJoystick::getJoystickButton(U32 button) const
{
    if (button < 16)
    {
        return mBtn[button];
    }
    return 0;
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::handleRun(F32 inc)
{
    // Decide whether to walk or run by applying a threshold, with slight
    // hysteresis to avoid oscillating between the two with input spikes.
    // Analog speed control would be better, but not likely any time soon.
    static LLCachedControl<F32> joy_run_threshold(gSavedSettings, "JoystickRunThreshold");
    if (inc > joy_run_threshold)
    {
        if (1 == mJoystickRun)
        {
            ++mJoystickRun;
//          gAgent.setRunning();
//          gAgent.sendWalkRun(gAgent.getRunning());
// [RLVa:KB] - Checked: 2011-05-11 (RLVa-1.3.0i) | Added: RLVa-1.3.0i
            gAgent.setTempRun();
// [/RLVa:KB]
        }
        else if (0 == mJoystickRun)
        {
            // hysteresis - respond NEXT frame
            ++mJoystickRun;
        }
    }
    else
    {
        if (mJoystickRun > 0)
        {
            --mJoystickRun;
            if (0 == mJoystickRun)
            {
//              gAgent.clearRunning();
//              gAgent.sendWalkRun(gAgent.getRunning());
// [RLVa:KB] - Checked: 2011-05-11 (RLVa-1.3.0i) | Added: RLVa-1.3.0i
                gAgent.clearTempRun();
// [/RLVa:KB]
            }
        }
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::agentJump()
{
    gAgent.moveUp(1);
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::agentSlide(F32 inc)
{
    if (inc < 0.f)
    {
        gAgent.moveLeft(1);
    }
    else if (inc > 0.f)
    {
        gAgent.moveLeft(-1);
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::agentPush(F32 inc)
{
    if (inc < 0.f)                            // forward
    {
        gAgent.moveAt(1, false);
    }
    else if (inc > 0.f)                       // backward
    {
        gAgent.moveAt(-1, false);
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::agentFly(F32 inc)
{
    if (inc < 0.f)
    {
        if (! (gAgent.getFlying() ||
               !gAgent.canFly() ||
               gAgent.upGrabbed() ||
               !ALControlCache::AutomaticFly) )
        {
            gAgent.setFlying(true);
        }
        gAgent.moveUp(1);
    }
    else if (inc > 0.f)
    {
        // crouch
        gAgent.moveUp(-1);
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::agentPitch(F32 pitch_inc)
{
    if (pitch_inc < 0)
    {
        gAgent.setControlFlags(AGENT_CONTROL_PITCH_POS);
    }
    else if (pitch_inc > 0)
    {
        gAgent.setControlFlags(AGENT_CONTROL_PITCH_NEG);
    }

    gAgent.pitch(-pitch_inc);
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::agentYaw(F32 yaw_inc)
{
    // Cannot steer some vehicles in mouselook if the script grabs the controls
    static LLCachedControl<bool> joy_mouse_yaw(gSavedSettings, "JoystickMouselookYaw");
    if (gAgentCamera.cameraMouselook() && !joy_mouse_yaw)
    {
        gAgent.rotate(-yaw_inc, gAgent.getReferenceUpVector());
    }
    else
    {
        if (yaw_inc < 0)
        {
            gAgent.setControlFlags(AGENT_CONTROL_YAW_POS);
        }
        else if (yaw_inc > 0)
        {
            gAgent.setControlFlags(AGENT_CONTROL_YAW_NEG);
        }

        gAgent.yaw(-yaw_inc);
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::resetDeltas(S32 axis[])
{
    for (U32 i = 0; i < 6; i++)
    {
        sLastDelta[i] = -mAxes[axis[i]];
        sDelta[i] = 0.f;
    }

    sLastDelta[6] = sDelta[6] = 0.f;
    mResetFlag = false;
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::moveObjects(bool reset)
{
    static bool toggle_send_to_sim = false;

    if (!gFocusMgr.getAppHasFocus() || mDriverState != JDS_INITIALIZED
        || !mJoystickEnabled || !mBuildEnabled)
    {
        return;
    }

    if (reset || mResetFlag)
    {
        resetDeltas(mJoystickAxis);
        return;
    }

    F32 cur_delta[6];
    F32 time = gFrameIntervalSeconds.value();

    // avoid making ridicously big movements if there's a big drop in fps
    if (time > .2f)
    {
        time = .2f;
    }

    // max feather is 32
    F32 feather = mBuildFeathering;
    bool is_zero = true, absolute = m3DCursor;

    for (U32 i = 0; i < 6; i++)
    {
        cur_delta[i] = -mAxes[mJoystickAxis[i]];
//      //BD - Invertable Pitch Controls
        if (!mInvertPitch && i == 4)
            cur_delta[i] = -cur_delta[i];

        F32 tmp = cur_delta[i];
        if (absolute)
        {
            cur_delta[i] = cur_delta[i] - sLastDelta[i];
        }
        sLastDelta[i] = tmp;
        is_zero = is_zero && (cur_delta[i] == 0.f);

        if (cur_delta[i] > 0)
        {
            cur_delta[i] = llmax(cur_delta[i]- mBuildAxisDeadZone[i], 0.f);
        }
        else
        {
            cur_delta[i] = llmin(cur_delta[i]+ mBuildAxisDeadZone[i], 0.f);
        }
        cur_delta[i] *= mBuildAxisScale[i];

        if (!absolute)
        {
            cur_delta[i] *= time;
        }

        sDelta[i] = sDelta[i] + (cur_delta[i]-sDelta[i])*time*feather;
    }

    U32 upd_type = UPD_NONE;
    LLVector3 v;

    if (!is_zero)
    {
        // Clear AFK state if moved beyond the deadzone
        if (gAwayTimer.getElapsedTimeF32() > LLAgent::MIN_AFK_TIME)
        {
            gAgent.clearAFK();
        }

        if (sDelta[0] || sDelta[1] || sDelta[2])
        {
            upd_type |= UPD_POSITION;
            v.setVec(sDelta[0], sDelta[1], sDelta[2]);
        }

        if (sDelta[3] || sDelta[4] || sDelta[5])
        {
            upd_type |= UPD_ROTATION;
        }

        // the selection update could fail, so we won't send
        if (LLSelectMgr::getInstance()->selectionMove(v, sDelta[3],sDelta[4],sDelta[5], upd_type))
        {
            toggle_send_to_sim = true;
        }
    }
    else if (toggle_send_to_sim)
    {
        LLSelectMgr::getInstance()->sendSelectionMove();
        toggle_send_to_sim = false;
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::moveAvatar(bool reset)
{
    if (!gFocusMgr.getAppHasFocus() || mDriverState != JDS_INITIALIZED
        || !mJoystickEnabled || !mAvatarEnabled)
    {
        return;
    }

    if (reset || mResetFlag)
    {
        resetDeltas(mJoystickAxis);
        if (reset)
        {
            // Note: moving the agent triggers agent camera mode;
            //  don't do this every time we set mResetFlag (e.g. because we gained focus)
            gAgent.moveAt(0, true);
        }
        return;
    }

    bool is_zero = true;
    static bool button_held = false;

    if (mBtn[1] == 1)
    {
        // If AutomaticFly is enabled, then button1 merely causes a
        // jump (as the up/down axis already controls flying) if on the
        // ground, or cease flight if already flying.
        // If AutomaticFly is disabled, then button1 toggles flying.
        if (ALControlCache::AutomaticFly)
        {
            if (!gAgent.getFlying())
            {
                gAgent.moveUp(1);
            }
            else if (!button_held)
            {
                button_held = true;
                gAgent.setFlying(FALSE);
            }
        }
        else if (!button_held)
        {
            button_held = true;
            gAgent.setFlying(!gAgent.getFlying());
        }

        is_zero = false;
    }
    else
    {
        button_held = false;
    }

    // time interval in seconds between this frame and the previous
    F32 time = gFrameIntervalSeconds.value();

    // avoid making ridicously big movements if there's a big drop in fps
    if (time > .2f)
    {
        time = .2f;
    }

    // note: max feather is 32.0
    F32 feather = mAvatarFeathering;

    F32 cur_delta[6];
    F32 val, dom_mov = 0.f;
    U32 dom_axis = Z_I;
#if LIB_NDOF
    bool absolute = (m3DCursor && mNdofDev->absolute);
#else
    bool absolute = false;
#endif
    // remove dead zones and determine biggest movement on the joystick
    for (U32 i = 0; i < 6; i++)
    {
        cur_delta[i] = -mAxes[mJoystickAxis[i]];

        if (!mInvertPitch && i == 4)
            cur_delta[i] = -cur_delta[i];

        if (absolute)
        {
            F32 tmp = cur_delta[i];
            cur_delta[i] = cur_delta[i] - sLastDelta[i];
            sLastDelta[i] = tmp;
        }

        if (cur_delta[i] > 0)
        {
            cur_delta[i] = llmax(cur_delta[i]- mAvatarAxisDeadZone[i], 0.f);
        }
        else
        {
            cur_delta[i] = llmin(cur_delta[i]+ mAvatarAxisDeadZone[i], 0.f);
        }

        // we don't care about Roll (RZ) and Z is calculated after the loop
        if (i != Z_I && i != RZ_I)
        {
            // find out the axis with the biggest joystick motion
            val = fabs(cur_delta[i]);
            if (val > dom_mov)
            {
                dom_axis = i;
                dom_mov = val;
            }
        }

        is_zero = is_zero && (cur_delta[i] == 0.f);
    }

    if (!is_zero)
    {
        // Clear AFK state if moved beyond the deadzone
        if (gAwayTimer.getElapsedTimeF32() > LLAgent::MIN_AFK_TIME)
        {
            gAgent.clearAFK();
        }

        setCameraNeedsUpdate(true);
    }

    // forward|backward movements overrule the real dominant movement if
    // they're bigger than its 20%. This is what you want 'cos moving forward
    // is what you do most. We also added a special (even more lenient) case
    // for RX|RY to allow walking while pitching and turning
    if (fabs(cur_delta[Z_I]) > .2f * dom_mov
        || ((dom_axis == RX_I || dom_axis == RY_I)
        && fabs(cur_delta[Z_I]) > .05f * dom_mov))
    {
        dom_axis = Z_I;
    }

    sDelta[X_I] = -cur_delta[X_I] * mAvatarAxisScale[X_I];
    sDelta[Y_I] = -cur_delta[Y_I] * mAvatarAxisScale[Y_I];
    sDelta[Z_I] = -cur_delta[Z_I] * mAvatarAxisScale[Z_I];
    cur_delta[RX_I] *= -mAvatarAxisScale[RX_I] * mPerfScale;
    cur_delta[RY_I] *= -mAvatarAxisScale[RY_I] * mPerfScale;

    if (!absolute)
    {
        cur_delta[RX_I] *= time;
        cur_delta[RY_I] *= time;
    }
    sDelta[RX_I] += (cur_delta[RX_I] - sDelta[RX_I]) * time * feather;
    sDelta[RY_I] += (cur_delta[RY_I] - sDelta[RY_I]) * time * feather;

    handleRun((F32) sqrt(sDelta[Z_I]*sDelta[Z_I] + sDelta[X_I]*sDelta[X_I]));

    // Allow forward/backward movement some priority
    if (dom_axis == Z_I)
    {
        agentPush(sDelta[Z_I]);         // forward/back

        if (fabs(sDelta[X_I])  > .1f)
        {
            agentSlide(sDelta[X_I]);    // move sideways
        }

        if (fabs(sDelta[Y_I])  > .1f)
        {
            agentFly(sDelta[Y_I]);      // up/down & crouch
        }

        // too many rotations during walking can be confusing, so apply
        // the deadzones one more time (quick & dirty), at 50%|30% power
        F32 eff_rx = .3f * mAvatarAxisDeadZone[RX_I];
        F32 eff_ry = .3f * mAvatarAxisDeadZone[RY_I];

        if (sDelta[RX_I] > 0)
        {
            eff_rx = llmax(sDelta[RX_I] - eff_rx, 0.f);
        }
        else
        {
            eff_rx = llmin(sDelta[RX_I] + eff_rx, 0.f);
        }

        if (sDelta[RY_I] > 0)
        {
            eff_ry = llmax(sDelta[RY_I] - eff_ry, 0.f);
        }
        else
        {
            eff_ry = llmin(sDelta[RY_I] + eff_ry, 0.f);
        }


        if (fabs(eff_rx) > 0.f || fabs(eff_ry) > 0.f)
        {
            if (gAgent.getFlying())
            {
                agentPitch(eff_rx);
                agentYaw(eff_ry);
            }
            else
            {
                agentPitch(eff_rx);
                agentYaw(2.f * eff_ry);
            }
        }
    }
    else
    {
        agentSlide(sDelta[X_I]);        // move sideways
        agentFly(sDelta[Y_I]);          // up/down & crouch
        agentPush(sDelta[Z_I]);         // forward/back
        agentPitch(sDelta[RX_I]);       // pitch
        agentYaw(sDelta[RY_I]);         // turn
    }
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::moveFlycam(bool reset)
{
    static LLQuaternion         sFlycamRotation;
    static LLVector3            sFlycamPosition;
    static F32                  sFlycamZoom;

    if (!gFocusMgr.getAppHasFocus() || mDriverState != JDS_INITIALIZED
        || !mJoystickEnabled || !mFlycamEnabled)
    {
        return;
    }

    bool in_build_mode = LLToolMgr::getInstance()->inBuildMode();
    if (reset || mResetFlag)
    {
        sFlycamPosition = LLViewerCamera::getInstance()->getOrigin();
        sFlycamRotation = LLViewerCamera::getInstance()->getQuaternion();
        sFlycamZoom = LLViewerCamera::getInstance()->getView();

        resetDeltas(mJoystickAxis);

        return;
    }

    F32 time = gFrameIntervalSeconds.value();

    // avoid making ridiculously big movements if there's a big drop in fps
    if (time > .2f)
    {
        time = .2f;
    }

    F32 cur_delta[7];
    F32 feather = mFlycamFeathering;
    bool absolute = m3DCursor;
    bool is_zero = true;

    for (U32 i = 0; i < 7; i++)
    {
        cur_delta[i] = -getJoystickAxis(mJoystickAxis[i]);

        if (!mInvertPitch && i == 4)
            cur_delta[i] = -cur_delta[i];

        F32 tmp = cur_delta[i];
        if (absolute)
        {
            cur_delta[i] = cur_delta[i] - sLastDelta[i];
        }
        sLastDelta[i] = tmp;

        if (cur_delta[i] > 0)
        {
            cur_delta[i] = llmax(cur_delta[i]- mFlycamAxisDeadZone[i], 0.f);
        }
        else
        {
            cur_delta[i] = llmin(cur_delta[i]+ mFlycamAxisDeadZone[i], 0.f);
        }

        // We may want to scale camera movements up or down in build mode.
        // NOTE: this needs to remain after the deadzone calculation, otherwise
        // we have issues with flycam "jumping" when the build dialog is opened/closed  -Nyx
        if (in_build_mode)
        {
            if (i == X_I || i == Y_I || i == Z_I)
            {
                static LLCachedControl<F32> build_mode_scale(gSavedSettings,"FlycamBuildModeScale", 1.0);
                cur_delta[i] *= build_mode_scale;
            }
        }

        cur_delta[i] *= mFlycamAxisScale[i];

        if (!absolute)
        {
            cur_delta[i] *= time;
        }

        sDelta[i] = sDelta[i] + (cur_delta[i]-sDelta[i])*time*feather;

        is_zero = is_zero && (cur_delta[i] == 0.f);

    }

    // Clear AFK state if moved beyond the deadzone
    if (!is_zero && gAwayTimer.getElapsedTimeF32() > LLAgent::MIN_AFK_TIME)
    {
        gAgent.clearAFK();
    }

    sFlycamPosition += LLVector3(sDelta.data()) * sFlycamRotation;

    LLMatrix3 rot_mat(sDelta[3], sDelta[4], sDelta[5]);
    sFlycamRotation = LLQuaternion(rot_mat)*sFlycamRotation;

    if (mAutoLeveling)
    {
        LLMatrix3 level(sFlycamRotation);

        LLVector3 x = LLVector3(level.mMatrix[0]);
        LLVector3 y = LLVector3(level.mMatrix[1]);
        LLVector3 z = LLVector3(level.mMatrix[2]);

        y.mV[2] = 0.f;
        y.normVec();

        level.setRows(x,y,z);
        level.orthogonalize();

        LLQuaternion quat(level);
        sFlycamRotation = nlerp(llmin(feather*time,1.f), sFlycamRotation, quat);
    }

    if (mZoomDirect)
    {
        sFlycamZoom = sLastDelta[6]* mFlycamAxisScale[6]+ mFlycamAxisDeadZone[6];
    }
    else
    {
        //BD - We need to cap zoom otherwise it internally counts higher causing
        //     the zoom level to not react until that extra has been removed first.
        sFlycamZoom = llclamp(sFlycamZoom + sDelta[6], LLViewerCamera::getInstance()->getMinView(), LLViewerCamera::getInstance()->getMaxView());
    }

    LLMatrix3 mat(sFlycamRotation);

    LLViewerCamera::getInstance()->setView(sFlycamZoom);
    LLViewerCamera::getInstance()->setOrigin(sFlycamPosition);
    LLViewerCamera::getInstance()->mXAxis = LLVector3(mat.mMatrix[0]);
    LLViewerCamera::getInstance()->mYAxis = LLVector3(mat.mMatrix[1]);
    LLViewerCamera::getInstance()->mZAxis = LLVector3(mat.mMatrix[2]);
}

// -----------------------------------------------------------------------------
bool LLViewerJoystick::toggleFlycam()
{
    if (!mJoystickEnabled || !mFlycamEnabled)
    {
        mOverrideCamera = false;
        return false;
    }

    if (!mOverrideCamera)
    {
        gAgentCamera.changeCameraToDefault();
    }

    if (gAwayTimer.getElapsedTimeF32() > LLAgent::MIN_AFK_TIME)
    {
        gAgent.clearAFK();
    }

    mOverrideCamera = !mOverrideCamera;
    if (mOverrideCamera)
    {
        moveFlycam(true);

    }
    else
    {
        // Exiting from the flycam mode: since we are going to keep the flycam POV for
        // the main camera until the avatar moves, we need to track this situation.
        setCameraNeedsUpdate(false);
        setNeedsReset(true);
    }
    return true;
}

void LLViewerJoystick::scanJoystick()
{
    if (mDriverState != JDS_INITIALIZED || !mJoystickEnabled)
    {
        return;
    }

#if LL_WINDOWS
    // On windows, the flycam is updated syncronously with a timer, so there is
    // no need to update the status of the joystick here.
    if (!mOverrideCamera)
#endif
    updateStatus();

    // App focus check Needs to happen AFTER updateStatus in case the joystick
    // is not centred when the app loses focus.
    if (!gFocusMgr.getAppHasFocus())
    {
        return;
    }

    static long toggle_flycam = 0;

    if (mBtn[0] == 1)
    {
        if (mBtn[0] != toggle_flycam)
        {
            toggle_flycam = toggleFlycam() ? 1 : 0;
        }
    }
    else
    {
        toggle_flycam = 0;
    }

    if (!mOverrideCamera && !(LLToolMgr::getInstance()->inBuildMode() && mBuildEnabled))
    {
        moveAvatar();
    }
}

// -----------------------------------------------------------------------------
bool LLViewerJoystick::isDeviceUUIDSet()
{
#if LL_WINDOWS && !LL_MESA_HEADLESS
    // for ease of comparison and to dial less with platform specific variables, we store id as LLSD binary
    return mLastDeviceUUID.isBinary();
#elif LL_DARWIN
    return mLastDeviceUUID.isMap();
#else
    return false;
#endif
}

LLSD LLViewerJoystick::getDeviceUUID()
{
    return mLastDeviceUUID;
}

std::string LLViewerJoystick::getDeviceUUIDString()
{
#if LL_WINDOWS && !LL_MESA_HEADLESS
    // Might be simpler to just convert _GUID into string everywhere, store and compare as string
    if (mLastDeviceUUID.isBinary())
    {
        S32 size = sizeof(GUID);
        LLSD::Binary data = mLastDeviceUUID.asBinary();
        GUID guid;
        memcpy(&guid, &data[0], size);
        return string_from_guid(guid);
    }
    else
    {
        return std::string();
    }
#elif LL_DARWIN
    if (mLastDeviceUUID.isMap())
    {
        std::string manufacturer = mLastDeviceUUID["manufacturer"].asString();
        std::string product = mLastDeviceUUID["product"].asString();
        return manufacturer + ":" + product;
    }
    else
    {
        return std::string();
    }
#else
    return std::string();
#endif
}

void LLViewerJoystick::saveDeviceIdToSettings()
{
#if LL_WINDOWS && !LL_MESA_HEADLESS
    // can't save as binary directly,
    // someone editing the xml will corrupt it
    // so convert to string first
    std::string device_string = getDeviceUUIDString();
    gSavedSettings.setLLSD("JoystickDeviceUUID", LLSD(device_string));
#else
    LLSD device_id = getDeviceUUID();
    gSavedSettings.setLLSD("JoystickDeviceUUID", device_id);
#endif
}

void LLViewerJoystick::loadDeviceIdFromSettings()
{
    LLSD dev_id = gSavedSettings.getLLSD("JoystickDeviceUUID");
#if LL_WINDOWS && !LL_MESA_HEADLESS
    // We can't save binary data to gSavedSettings, somebody editing the file will corrupt it,
    // so _GUID data gets converted to string (we probably can convert it to LLUUID with memcpy)
    // and here we need to convert it back to binary from string
    std::string device_string;
    if (dev_id.isString())
    {
        device_string = dev_id.asString();
    }
    if (device_string.empty())
    {
        mLastDeviceUUID = LLSD();
    }
    else
    {
        LL_DEBUGS("Joystick") << "Looking for device by id: " << device_string << LL_ENDL;
        GUID guid;
        guid_from_string(guid, device_string);
        S32 size = sizeof(GUID);
        LLSD::Binary data; //just an std::vector
        data.resize(size);
        memcpy(&data[0], &guid /*POD _GUID*/, size);
        // We store this data in LLSD since it can handle both GUID2 and long
        mLastDeviceUUID = LLSD(data);
    }
#elif LL_DARWIN
    if (!dev_id.isMap())
    {
        mLastDeviceUUID = LLSD();
    }
    else
    {
        std::string manufacturer = mLastDeviceUUID["manufacturer"].asString();
        std::string product = mLastDeviceUUID["product"].asString();
        LL_DEBUGS("Joystick") << "Looking for device by manufacturer: " << manufacturer << " and product: " << product <<  LL_ENDL;
        // We store this data in LLSD since it can handle both GUID2 and long
        mLastDeviceUUID = dev_id;
    }
#else
    mLastDeviceUUID = LLSD();
    //mLastDeviceUUID = gSavedSettings.getLLSD("JoystickDeviceUUID");
#endif
}

// -----------------------------------------------------------------------------
std::string LLViewerJoystick::getDescription()
{
    std::string res;
#if LIB_NDOF
    if (mDriverState == JDS_INITIALIZED && mNdofDev)
    {
        res = ll_safe_string(mNdofDev->product);
    }
#endif
    return res;
}

bool LLViewerJoystick::isLikeSpaceNavigator() const
{
#if LIB_NDOF
    return (isJoystickInitialized()
            && (strncmp(mNdofDev->product, "SpaceNavigator", 14) == 0
                || strncmp(mNdofDev->product, "3Dconnexion SpaceNavigator", 26) == 0
                || strncmp(mNdofDev->product, "SpaceExplorer", 13) == 0
                || strncmp(mNdofDev->product, "SpaceTraveler", 13) == 0
                || strncmp(mNdofDev->product, "SpacePilot", 10) == 0));
#else
    return false;
#endif
}

// -----------------------------------------------------------------------------
void LLViewerJoystick::setSNDefaults()
{
#if LL_DARWIN || LL_LINUX
    const float platformScale = 20.f;
    const float platformScaleAvXZ = 1.f;
    // The SpaceNavigator doesn't act as a 3D cursor on OS X / Linux.
    const bool is_3d_cursor = false;
#else
    const float platformScale = 1.f;
    const float platformScaleAvXZ = 2.f;
    const bool is_3d_cursor = true;
#endif

    //gViewerWindow->alertXml("CacheWillClear");
    LL_INFOS("Joystick") << "restoring SpaceNavigator defaults..." << LL_ENDL;

    gSavedSettings.setS32("JoystickAxis0", 1); // z (at)
    gSavedSettings.setS32("JoystickAxis1", 0); // x (slide)
    gSavedSettings.setS32("JoystickAxis2", 2); // y (up)
    gSavedSettings.setS32("JoystickAxis3", 4); // pitch
    gSavedSettings.setS32("JoystickAxis4", 3); // roll
    gSavedSettings.setS32("JoystickAxis5", 5); // yaw
    gSavedSettings.setS32("JoystickAxis6", -1);

    gSavedSettings.setBOOL("Cursor3D", is_3d_cursor);
    gSavedSettings.setBOOL("AutoLeveling", true);
    gSavedSettings.setBOOL("ZoomDirect", false);
    gSavedSettings.setBOOL("JoystickInvertPitch", true);

    gSavedSettings.setF32("AvatarAxisScale0", 1.f * platformScaleAvXZ);
    gSavedSettings.setF32("AvatarAxisScale1", 1.f * platformScaleAvXZ);
    gSavedSettings.setF32("AvatarAxisScale2", 1.f);
    gSavedSettings.setF32("AvatarAxisScale4", .1f * platformScale);
    gSavedSettings.setF32("AvatarAxisScale5", .1f * platformScale);
    gSavedSettings.setF32("AvatarAxisScale3", 0.f * platformScale);
    gSavedSettings.setF32("BuildAxisScale1", .3f * platformScale);
    gSavedSettings.setF32("BuildAxisScale2", .3f * platformScale);
    gSavedSettings.setF32("BuildAxisScale0", .3f * platformScale);
    gSavedSettings.setF32("BuildAxisScale4", .3f * platformScale);
    gSavedSettings.setF32("BuildAxisScale5", .3f * platformScale);
    gSavedSettings.setF32("BuildAxisScale3", .3f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale1", 2.f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale2", 2.f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale0", 2.1f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale4", .1f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale5", .15f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale3", 0.f * platformScale);
    gSavedSettings.setF32("FlycamAxisScale6", 0.f * platformScale);

    gSavedSettings.setF32("AvatarAxisDeadZone0", .1f);
    gSavedSettings.setF32("AvatarAxisDeadZone1", .1f);
    gSavedSettings.setF32("AvatarAxisDeadZone2", .1f);
    gSavedSettings.setF32("AvatarAxisDeadZone3", 1.f);
    gSavedSettings.setF32("AvatarAxisDeadZone4", .02f);
    gSavedSettings.setF32("AvatarAxisDeadZone5", .01f);
    gSavedSettings.setF32("BuildAxisDeadZone0", .01f);
    gSavedSettings.setF32("BuildAxisDeadZone1", .01f);
    gSavedSettings.setF32("BuildAxisDeadZone2", .01f);
    gSavedSettings.setF32("BuildAxisDeadZone3", .01f);
    gSavedSettings.setF32("BuildAxisDeadZone4", .01f);
    gSavedSettings.setF32("BuildAxisDeadZone5", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone0", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone1", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone2", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone3", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone4", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone5", .01f);
    gSavedSettings.setF32("FlycamAxisDeadZone6", 1.f);

    gSavedSettings.setF32("AvatarFeathering", 6.f);
    gSavedSettings.setF32("BuildFeathering", 12.f);
    gSavedSettings.setF32("FlycamFeathering", 5.f);
}

//BD - Xbox360 Controller Support
void LLViewerJoystick::setXboxDefaults()
{
    LL_INFOS() << "restoring Xbox Controller defaults..." << LL_ENDL;

    gSavedSettings.setS32("JoystickAxis0", 1);  // Z
    gSavedSettings.setS32("JoystickAxis1", 0);  // X
    gSavedSettings.setS32("JoystickAxis2", -1); // Y
    gSavedSettings.setS32("JoystickAxis3", 2);  // Roll
    gSavedSettings.setS32("JoystickAxis4", 4);  // Pitch
    gSavedSettings.setS32("JoystickAxis5", 3);  // Yaw
    gSavedSettings.setS32("JoystickAxis6", -1); // Zoom

    gSavedSettings.setBOOL("Cursor3D", false); // Xbox Gamepad, not 3D Mouse
    gSavedSettings.setBOOL("AutoLeveling", false);
    gSavedSettings.setBOOL("ZoomDirect", false);
    gSavedSettings.setBOOL("JoystickInvertPitch", false);

    gSavedSettings.setF32("AvatarAxisScale0", 1.f);
    gSavedSettings.setF32("AvatarAxisScale2", 1.f);
    gSavedSettings.setF32("AvatarAxisScale1", 1.f);
    gSavedSettings.setF32("AvatarAxisScale4", 1.f);
    gSavedSettings.setF32("AvatarAxisScale5", 1.f);
    gSavedSettings.setF32("AvatarAxisScale3", 1.f);
    gSavedSettings.setF32("BuildAxisScale0", 1.25f);
    gSavedSettings.setF32("BuildAxisScale2", 1.25f);
    gSavedSettings.setF32("BuildAxisScale1", 1.25f);
    gSavedSettings.setF32("BuildAxisScale4", 1.f);
    gSavedSettings.setF32("BuildAxisScale5", 1.f);
    gSavedSettings.setF32("BuildAxisScale3", 1.f);
    gSavedSettings.setF32("FlycamAxisScale0", 5.0f);
    gSavedSettings.setF32("FlycamAxisScale2", 5.0f);
    gSavedSettings.setF32("FlycamAxisScale1", 5.0f);
    gSavedSettings.setF32("FlycamAxisScale4", 2.0f);
    gSavedSettings.setF32("FlycamAxisScale5", 2.5f);
    gSavedSettings.setF32("FlycamAxisScale3", 2.0f);
    gSavedSettings.setF32("FlycamAxisScale6", 1.0f);

    gSavedSettings.setF32("AvatarAxisDeadZone0", .6f);
    gSavedSettings.setF32("AvatarAxisDeadZone2", .3f);
    gSavedSettings.setF32("AvatarAxisDeadZone1", .6f);
    gSavedSettings.setF32("AvatarAxisDeadZone3", .3f);
    gSavedSettings.setF32("AvatarAxisDeadZone4", .3f);
    gSavedSettings.setF32("AvatarAxisDeadZone5", .3f);
    gSavedSettings.setF32("BuildAxisDeadZone0", .25f);
    gSavedSettings.setF32("BuildAxisDeadZone2", .25f);
    gSavedSettings.setF32("BuildAxisDeadZone1", .25f);
    gSavedSettings.setF32("BuildAxisDeadZone3", .3f);
    gSavedSettings.setF32("BuildAxisDeadZone4", .3f);
    gSavedSettings.setF32("BuildAxisDeadZone5", .1f);
    gSavedSettings.setF32("FlycamAxisDeadZone0", .25f);
    gSavedSettings.setF32("FlycamAxisDeadZone2", .25f);
    gSavedSettings.setF32("FlycamAxisDeadZone1", .25f);
    gSavedSettings.setF32("FlycamAxisDeadZone3", .1f);
    gSavedSettings.setF32("FlycamAxisDeadZone4", .3f);
    gSavedSettings.setF32("FlycamAxisDeadZone5", .3f);
    gSavedSettings.setF32("FlycamAxisDeadZone6", .1f);

    gSavedSettings.setF32("AvatarFeathering", 20.0f);
    gSavedSettings.setF32("BuildFeathering", 3.f);
    gSavedSettings.setF32("FlycamFeathering", 1.0f);
}
