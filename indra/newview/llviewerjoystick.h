/**
 * @file llviewerjoystick.h
 * @brief Viewer joystick / NDOF device functionality.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLVIEWERJOYSTICK_H
#define LL_LLVIEWERJOYSTICK_H

#include "stdtypes.h"

#if LIB_NDOF
#if LL_DARWIN
#define TARGET_OS_MAC 1
#endif
#include "ndofdev_external.h"
#else
#define NDOF_Device void
#define NDOF_HotPlugResult S32
#endif

typedef enum e_joystick_driver_state
{
    JDS_UNINITIALIZED,
    JDS_INITIALIZED,
    JDS_INITIALIZING
} EJoystickDriverState;

class LLViewerJoystick final : public LLSingleton<LLViewerJoystick>
{
    LLSINGLETON(LLViewerJoystick);
    virtual ~LLViewerJoystick();
    LOG_CLASS(LLViewerJoystick);

public:
    void init(bool autoenable);
    void initDevice(LLSD &guid);
    bool initDevice(void * preffered_device /*LPDIRECTINPUTDEVICE8*/);
    bool initDevice(void * preffered_device /*LPDIRECTINPUTDEVICE8*/, std::string &name, LLSD &guid);
    void terminate();

    void updateStatus();
    void scanJoystick();
    void moveObjects(bool reset = false);
    void moveAvatar(bool reset = false);
    void moveFlycam(bool reset = false);
    F32 getJoystickAxis(U32 axis) const;
    U32 getJoystickButton(U32 button) const;
    bool isJoystickInitialized() const {return (mDriverState==JDS_INITIALIZED);}
    bool isLikeSpaceNavigator() const;
    void setNeedsReset(bool reset = true) { mResetFlag = reset; }
    void setCameraNeedsUpdate(bool b)     { mCameraUpdated = b; }
    bool getCameraNeedsUpdate() const     { return mCameraUpdated; }
    bool getOverrideCamera() { return mOverrideCamera; }
    void setOverrideCamera(bool val);
    bool toggleFlycam();
    void setSNDefaults();
    void setXboxDefaults();
    bool isDeviceUUIDSet();
    LLSD getDeviceUUID(); //unconverted, OS dependent value wrapped into LLSD, for comparison/search
    std::string getDeviceUUIDString(); // converted readable value for settings
    std::string getDescription();
    void saveDeviceIdToSettings();

protected:
    void updateEnabled(bool autoenable);
    void handleRun(F32 inc);
    void agentSlide(F32 inc);
    void agentPush(F32 inc);
    void agentFly(F32 inc);
    void agentPitch(F32 pitch_inc);
    void agentYaw(F32 yaw_inc);
    void agentJump();
    void resetDeltas(S32 axis[]);
    void loadDeviceIdFromSettings();
    void refreshFromSettings();
#if LIB_NDOF
    static NDOF_HotPlugResult HotPlugAddCallback(NDOF_Device *dev);
    static void HotPlugRemovalCallback(NDOF_Device *dev);
#endif

private:
    F32                     mAxes[6];
    long                    mBtn[16];
    EJoystickDriverState    mDriverState;
    NDOF_Device             *mNdofDev;
    bool                    mResetFlag;
    F32                     mPerfScale;
    bool                    mCameraUpdated;
    bool                    mOverrideCamera;
    U32                     mJoystickRun;

    // Windows: _GUID as U8 binary map
    // MacOS: long as an U8 binary map
    // Else: integer 1 for no device/ndof's default device
    LLSD                    mLastDeviceUUID;

    // SETTINGS

    // [1 0 2 4  3  5]
    // [Z X Y RZ RX RY]
    // Device prefs
    bool mJoystickEnabled;
    LLSD mJoystickId;
    S32 mJoystickAxis[7];
    bool m3DCursor;
    bool mAutoLeveling;
    bool mZoomDirect;
    bool mInvertPitch;

    // Modes prefs
    bool mAvatarEnabled;
    bool mBuildEnabled;
    bool mFlycamEnabled;
    F32 mAvatarAxisScale[6];
    F32 mBuildAxisScale[6];
    F32 mFlycamAxisScale[7];
    F32 mAvatarAxisDeadZone[6];
    F32 mBuildAxisDeadZone[6];
    F32 mFlycamAxisDeadZone[7];
    F32 mAvatarFeathering;
    F32 mBuildFeathering;
    F32 mFlycamFeathering;

    static std::array<F32, 7> sLastDelta;
    static std::array<F32, 7> sDelta;
};

#endif
