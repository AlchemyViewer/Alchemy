/**
 * @file skotscamera.h
 * @brief Over-the-shoulder view for mouselook
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Martin "Nexii" Pitt <martin.r.pitt@gmail.com>
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
 * $/LicenseInfo$
 */

#ifndef SK_OTSCAMERA_H
#define SK_OTSCAMERA_H

#include "llquaternion.h"
#include "v3math.h"
#include "v3dmath.h"

// The over-the-shoulder view is a way of placing the camera while in mouselook,
// not a camera mode of its own: input, tools and UI stay exactly as in mouselook,
// only the camera sits behind the shoulder and the avatar is drawn.
namespace SKOTSCamera
{
    // Whether entering mouselook should open in the shoulder view (SKOTSEnabled).
    bool isEnabled();

    // True while in mouselook with the shoulder view showing.
    bool isShoulder();

    // Asks for the next mouselook entry to open over the shoulder. Only the mouselook key asks;
    // scrolling in and scripts still enter first person.
    void requestShoulder();
    void cancelRequest(); // Drops a request mouselook did not take up

    // Picks the view on entering mouselook from the request and the last view toggled to.
    void onEnterMouselook();

    // Switches between the shoulder view and first person without leaving mouselook.
    void toggleView();

    // Mirrors the camera to the other shoulder.
    void swapShoulder();

    // Camera and focus for the shoulder view, in agent coordinates, from the avatar root and aim
    // rotation. A seat's own camera offset, when it sets one, replaces the user's shoulder offset.
    LLVector3 cameraPositionAgent(const LLVector3& root_agent, const LLQuaternion& aim_rot, const LLVector3* sit_offset);
    LLVector3 focusPositionAgent(const LLVector3& root_agent, const LLQuaternion& aim_rot, const LLVector3* sit_offset);

    // The point under the crosshair that a shot from the eye should travel to.
    LLVector3 convergenceTarget(const LLVector3& cam_origin, const LLVector3& cam_at);

    // Replaces the camera reported to the region with the eye aimed at the crosshair, so
    // scripts that fire from the camera fire from the avatar. Returns false when not in use.
    bool reportedCamera(LLVector3& pos, LLVector3& at, LLVector3& left, LLVector3& up);

    // Turns the head rotation sent to the region onto the reported aim. The body is not turned:
    // the region moves the avatar along it, so a converged body walks sideways.
    void convergeHead(const LLVector3& aim_at, LLQuaternion& head);

    // Camera animation bookkeeping: whether the last rendered frame was first person, and the
    // avatar frame at the start of an animation so a transition can follow the avatar.
    void onCameraUpdated(bool first_person);
    void onAnimationStart(const LLVector3d& root_global, const LLQuaternion& rot);
    bool animationFromFirstPerson();
    LLVector3d followAvatar(const LLVector3d& start_global, const LLVector3d& root_global, const LLQuaternion& rot);
}

#endif // SK_OTSCAMERA_H
