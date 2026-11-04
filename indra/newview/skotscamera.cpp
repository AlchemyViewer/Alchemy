/**
 * @file skotscamera.cpp
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

#include "llviewerprecompiledheaders.h"

#include "skotscamera.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llcharacter.h"
#include "llcoordframe.h"
#include "llcriticaldamp.h"
#include "llframetimer.h"
#include "llspatialpartition.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerobject.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include "llworld.h"
#include "pipeline.h"

namespace
{
    constexpr F32 CONVERGE_RANGE = 512.f;            // metres; open-sky fallback
    constexpr F32 MIN_BEYOND_EYE = 0.3f;             // nearest a target may sit past the eye
    constexpr F32 MIN_CAMERA_DISTANCE = 0.3f;        // never pulled in closer than just behind the head
    constexpr F32 COLLISION_EASE_OUT_HALF_LIFE = 0.15f;
    constexpr S32 MAX_PHANTOM_SKIPS = 16;

    bool sShoulder = false;
    bool sShoulderRequested = false;
    F32 sCollisionDistance = -1.f;                   // smoothed pull-in distance, negative when unset
    bool sFirstPersonLastFrame = false;
    bool sAnimFromFirstPerson = false;
    LLVector3d sAnimRootGlobal;
    LLQuaternion sAnimRot;

    // Bullets are never something to aim at: the shooter's own fly straight down the crosshair ray,
    // and incoming fire crosses it. SL bullets travel at 100-200 m/s as a single prim 3-14 m long
    // and under 0.2 m thick; only that shape is stepped through, so everything else still counts.
    // The shooter's own bullet just rezzed at the gun is known by shape alone, before the region
    // has sent its velocity.
    bool isProjectile(const LLViewerObject* obj)
    {
        constexpr F32 MIN_SPEED = 100.f;     // metres per second
        constexpr F32 MIN_LENGTH = 3.f;
        constexpr F32 MAX_LENGTH = 14.f;
        constexpr F32 MAX_THICKNESS = 0.2f;
        constexpr F32 MUZZLE_REACH = 3.f;    // how near the shooter a fresh bullet starts

        if (obj->isAttachment())
        {
            return false;
        }

        const LLVector3& scale = obj->getScale();
        const F32 longest = llmax(scale.mV[VX], scale.mV[VY], scale.mV[VZ]);
        const F32 shortest = llmin(scale.mV[VX], scale.mV[VY], scale.mV[VZ]);
        const F32 middle = scale.mV[VX] + scale.mV[VY] + scale.mV[VZ] - longest - shortest;
        if (longest < MIN_LENGTH || longest > MAX_LENGTH || middle > MAX_THICKNESS)
        {
            return false;
        }

        const F32 reach = longest * 0.5f + MUZZLE_REACH;
        const bool at_muzzle = obj->permYouOwner() && isAgentAvatarValid()
            && (obj->getPositionAgent() - gAgentAvatarp->getPositionAgent()).lengthSquared() <= reach * reach;
        return at_muzzle || obj->getVelocity().lengthSquared() >= MIN_SPEED * MIN_SPEED;
    }

    // First solid surface along a segment through volumes and terrain. Avatars and their
    // attachments are never tested, so a ray from inside the wearer's body is safe, and
    // phantom prims are stepped through because shots and the camera pass through them.
    // Invisible prims still stop a shot, but should not push the camera about.
    // The linkset the avatar sits on is stepped through too, so a vehicle's own hull or canopy
    // neither clamps the camera nor steals the aim.
    bool intersectSolidWorld(const LLVector3& start, const LLVector3& end, bool pick_transparent, LLVector3& hit_out)
    {
        const LLViewerObject* seat_root = nullptr;
        if (isAgentAvatarValid() && gAgentAvatarp->isSitting() && gAgentAvatarp->getParent())
        {
            seat_root = static_cast<LLViewerObject*>(gAgentAvatarp->getParent())->getRootEdit();
        }

        static const U32 partitions[] =
        {
            LLViewerRegion::PARTITION_VOLUME,
            LLViewerRegion::PARTITION_BRIDGE,
            LLViewerRegion::PARTITION_TERRAIN
        };

        LLVector3 dir = end - start;
        if (dir.normalize() <= 0.f)
        {
            return false;
        }

        LLVector4a seg_start;
        seg_start.load3(start.mV);
        LLVector4a seg_end;
        seg_end.load3(end.mV);

        for (S32 attempt = 0; attempt <= MAX_PHANTOM_SKIPS; ++attempt)
        {
            LLDrawable* drawable = nullptr;
            LLVector4a local_end = seg_end;
            LLVector4a position;

            for (LLViewerRegion* region : LLWorld::getInstance()->getRegionList())
            {
                for (U32 j : partitions)
                {
                    LLSpatialPartition* part = region->getSpatialPartition(j);
                    if (part && gPipeline.hasRenderType(part->mDrawableType))
                    {
                        LLDrawable* hit = part->lineSegmentIntersect(seg_start, local_end, pick_transparent, false, true, false, nullptr, &position);
                        if (hit)
                        {
                            drawable = hit;
                            local_end = position;
                        }
                    }
                }
            }

            if (!drawable)
            {
                return false;
            }

            const LLViewerObject* obj = drawable->getVObj();
            if (obj && (obj->flagPhantom() || (seat_root && obj->getRootEdit() == seat_root) || isProjectile(obj)))
            {
                LLVector3 next = LLVector3(local_end.getF32ptr()) + dir * 0.05f;
                seg_start.load3(next.mV);
                continue;
            }

            hit_out.set(local_end.getF32ptr());
            return true;
        }

        return false;
    }

    // Distance along a unit ray to an oriented box, or false on a miss.
    bool intersectBox(const LLVector3& origin, const LLVector3& dir, const LLVector3& center,
                      const LLQuaternion& rot, const LLVector3& half, F32& t_out)
    {
        const LLQuaternion inv = ~rot;
        const LLVector3 o = (origin - center) * inv;
        const LLVector3 d = dir * inv;

        F32 tmin = -F32_MAX;
        F32 tmax = F32_MAX;
        for (S32 a = 0; a < 3; ++a)
        {
            if (fabsf(d.mV[a]) < 1e-6f)
            {
                if (o.mV[a] < -half.mV[a] || o.mV[a] > half.mV[a])
                {
                    return false;
                }
                continue;
            }
            F32 t1 = (-half.mV[a] - o.mV[a]) / d.mV[a];
            F32 t2 = (half.mV[a] - o.mV[a]) / d.mV[a];
            if (t1 > t2)
            {
                std::swap(t1, t2);
            }
            tmin = llmax(tmin, t1);
            tmax = llmin(tmax, t2);
            if (tmin > tmax)
            {
                return false;
            }
        }
        if (tmax < 0.f)
        {
            return false;
        }
        t_out = tmin >= 0.f ? tmin : tmax;
        return true;
    }

    // Nearest avatar hitbox under the crosshair, in front of the shooter's eye. The box is the
    // one Avatar Hitboxes draws: object position, scale and rotation rather than the animated
    // body. A stand-in until the measured physics shape replaces it.
    bool intersectAvatarHitbox(const LLVector3& cam_origin, const LLVector3& cam_at, F32 eye_dist, F32 max_dist, F32& t_out)
    {
        bool found = false;
        for (LLCharacter* character : LLCharacter::sInstances)
        {
            LLVOAvatar* avatar = static_cast<LLVOAvatar*>(character);
            if (!avatar || avatar->isDead() || avatar->isControlAvatar() || avatar->isSelf())
            {
                continue;
            }

            const LLVector3 center = avatar->getPositionAgent();
            const LLVector3 to_avatar = center - cam_origin;
            if (to_avatar * cam_at < 0.f || to_avatar.lengthSquared() > max_dist * max_dist)
            {
                continue;
            }

            F32 t = 0.f;
            if (intersectBox(cam_origin, cam_at, center, avatar->getRotationRegion(), avatar->getScale() * 0.5f, t)
                && t > eye_dist && t < max_dist)
            {
                max_dist = t;
                t_out = t;
                found = true;
            }
        }
        return found;
    }

    LLVector3 eyePositionAgent()
    {
        return gAgentAvatarp->mHeadp ? gAgentAvatarp->mHeadp->getWorldPosition()
                                     : gAgentAvatarp->getPositionAgent() + LLVector3(0.f, 0.f, 0.6f);
    }

    LLVector3 shoulderOffset(const LLVector3* sit_offset)
    {
        if (sit_offset)
        {
            return LLVector3(-fabsf(sit_offset->mV[VX]), sit_offset->mV[VY], sit_offset->mV[VZ]);
        }
        static LLCachedControl<F32> distance(gSavedSettings, "SKOTSCameraDistance", 3.f);
        static LLCachedControl<F32> side(gSavedSettings, "SKOTSCameraSide", -0.5f);
        static LLCachedControl<F32> height(gSavedSettings, "SKOTSCameraHeight", 0.5f);
        return LLVector3(-(F32)distance, (F32)side, (F32)height);
    }
}

bool SKOTSCamera::isEnabled()
{
    static LLCachedControl<bool> enabled(gSavedSettings, "SKOTSEnabled", false);
    return enabled;
}

bool SKOTSCamera::isShoulder()
{
    return sShoulder && gAgentCamera.getCameraMode() == CAMERA_MODE_MOUSELOOK;
}

void SKOTSCamera::requestShoulder()
{
    // A seat without its own camera offset gets the cockpit view rather than a guessed shoulder
    // camera clipping through the hull.
    const bool seated_without_camera = isAgentAvatarValid() && gAgentAvatarp->isSitting() && !gAgentCamera.sitCameraEnabled();
    static LLCachedControl<bool> remember(gSavedSettings, "SKOTSRememberLastView", false);
    sShoulderRequested = isEnabled() && !seated_without_camera
        && !(remember && gSavedSettings.getBOOL("SKOTSLastViewFirstPerson"));
}

void SKOTSCamera::cancelRequest()
{
    sShoulderRequested = false;
}

void SKOTSCamera::onEnterMouselook()
{
    sShoulder = sShoulderRequested;
    sShoulderRequested = false;
    sCollisionDistance = -1.f;
}

void SKOTSCamera::toggleView()
{
    if (gAgentCamera.getCameraMode() != CAMERA_MODE_MOUSELOOK)
    {
        return;
    }

    sShoulder = !sShoulder;
    sCollisionDistance = -1.f;
    gSavedSettings.setBOOL("SKOTSLastViewFirstPerson", !sShoulder);
    if (isAgentAvatarValid())
    {
        gAgentAvatarp->updateAttachmentVisibility(CAMERA_MODE_MOUSELOOK);
    }
    gAgentCamera.startCameraAnimation();
}

void SKOTSCamera::swapShoulder()
{
    gSavedSettings.setF32("SKOTSCameraSide", -gSavedSettings.getF32("SKOTSCameraSide"));
    if (isShoulder())
    {
        gAgentCamera.startCameraAnimation();
    }
}

LLVector3 SKOTSCamera::cameraPositionAgent(const LLVector3& root_agent, const LLQuaternion& aim_rot, const LLVector3* sit_offset)
{
    const LLVector3 offset = shoulderOffset(sit_offset);
    LLVector3 cam = root_agent + offset * aim_rot;

    static LLCachedControl<bool> collision(gSavedSettings, "SKOTSCameraCollision", true);
    if (!collision || gAgent.getTeleportState() != LLAgent::TELEPORT_NONE)
    {
        sCollisionDistance = -1.f;
        return cam;
    }

    // Aim pitches the offset, so looking up swings the camera into the ground; pull it in
    // from a pivot at shoulder height to just in front of whatever is in the way.
    const LLVector3 pivot = root_agent + LLVector3(0.f, 0.f, offset.mV[VZ]);
    LLVector3 dir = cam - pivot;
    const F32 desired = dir.normalize();
    F32 target = desired;

    LLVector3 hit;
    if (intersectSolidWorld(pivot, cam, false, hit))
    {
        // The margin has to cover the near clip, or the plane pokes through the surface.
        const F32 margin = llmax(0.2f, LLViewerCamera::getInstance()->getNear() + 0.05f);
        target = llclamp((hit - pivot).length() - margin, MIN_CAMERA_DISTANCE, desired);
    }

    // Snap in so the camera never clips, ease out so rough surfaces do not jitter it.
    if (sCollisionDistance < 0.f || target <= sCollisionDistance)
    {
        sCollisionDistance = target;
    }
    else
    {
        sCollisionDistance = lerp(sCollisionDistance, target, LLSmoothInterpolation::getInterpolant(COLLISION_EASE_OUT_HALF_LIFE));
    }

    return pivot + dir * llmin(sCollisionDistance, desired);
}

LLVector3 SKOTSCamera::focusPositionAgent(const LLVector3& root_agent, const LLQuaternion& aim_rot, const LLVector3* sit_offset)
{
    static LLCachedControl<F32> focus_distance(gSavedSettings, "SKOTSFocusDistance", 10.f);
    const LLVector3 offset = shoulderOffset(sit_offset);
    const LLVector3 focus_local((F32)focus_distance, offset.mV[VY] * 0.3f, offset.mV[VZ] * 0.5f);
    return root_agent + focus_local * aim_rot;
}

LLVector3 SKOTSCamera::convergenceTarget(const LLVector3& cam_origin, const LLVector3& cam_at)
{
    // A shot leaves from the eye, which sits ahead of the camera along the crosshair ray; anything
    // the ray meets before the eye (a pillar beside the shoulder, a doorframe) cannot be shot at
    // and would turn the shot sideways or backwards, so the search starts at the eye.
    F32 eye_dist = MIN_BEYOND_EYE;
    if (isAgentAvatarValid())
    {
        eye_dist = llmax(eye_dist, (eyePositionAgent() - cam_origin) * cam_at);
    }

    // Solid world geometry, then avatar hitboxes; attachments and phantom surfaces never take the aim.
    F32 dist = CONVERGE_RANGE;
    LLVector3 hit;
    if (intersectSolidWorld(cam_origin + cam_at * eye_dist, cam_origin + cam_at * CONVERGE_RANGE, true, hit))
    {
        dist = (hit - cam_origin) * cam_at;
    }

    // A hitbox in front of the wall is an authoritative target: exempt from the smoothing, so the
    // lock is immediate even at point blank.
    static LLCachedControl<bool> avatar_converge(gSavedSettings, "SKOTSAvatarConverge", true);
    F32 avatar_dist = 0.f;
    const bool on_avatar = avatar_converge && intersectAvatarHitbox(cam_origin, cam_at, eye_dist, dist, avatar_dist);

    dist = llmax(dist, eye_dist + MIN_BEYOND_EYE);

    // Smooth the depth along the ray, not the point, so the target stays under the crosshair.
    // Advanced once per frame because more than one caller asks within a frame.
    static LLCachedControl<F32> half_life(gSavedSettings, "SKOTSConvergeSmoothingHalfLife", 0.06f);
    static U32 last_frame = 0;
    static F32 smoothed = CONVERGE_RANGE;
    const U32 frame = LLFrameTimer::getFrameCount();
    if (on_avatar)
    {
        last_frame = frame;
        smoothed = avatar_dist;
    }
    else if (frame != last_frame)
    {
        last_frame = frame;
        smoothed = (F32)half_life > 0.f ? lerp(smoothed, dist, LLSmoothInterpolation::getInterpolant((F32)half_life)) : dist;
    }

    return cam_origin + cam_at * llmax(smoothed, eye_dist + MIN_BEYOND_EYE);
}

bool SKOTSCamera::reportedCamera(LLVector3& pos, LLVector3& at, LLVector3& left, LLVector3& up)
{
    // Always in the shoulder view: reporting the shoulder camera would let camera-fired weapons
    // shoot from behind and beside the avatar, around cover it is behind. Seated, the region gets
    // the true camera instead: vehicle turrets aim from llGetCameraPos and llGetCameraRot, and
    // fire from the vehicle rather than the eye.
    if (!isShoulder() || !isAgentAvatarValid() || gAgentAvatarp->isSitting())
    {
        return false;
    }

    const LLVector3 eye = eyePositionAgent();
    LLVector3 dir = at;
    if (gAgent.getTeleportState() == LLAgent::TELEPORT_NONE) // no raycasts into a world being torn down
    {
        dir = convergenceTarget(pos, at) - eye;
        if (dir.normalize() <= 0.f)
        {
            dir = at;
        }
    }

    LLCoordFrame frame;
    frame.lookDir(dir);
    pos = eye;
    at = frame.getAtAxis();
    left = frame.getLeftAxis();
    up = frame.getUpAxis();
    return true;
}

void SKOTSCamera::convergeHead(const LLVector3& aim_at, LLQuaternion& head)
{
    // Built from the look direction exactly as LLAgent::getHeadRotation does in mouselook.
    LLVector3 up = aim_at % gAgent.getFrameAgent().getLeftAxis();
    up.normalize();
    const LLVector3 left = up % aim_at;
    head = LLQuaternion(aim_at, left, up);
}

void SKOTSCamera::onCameraUpdated(bool first_person)
{
    sFirstPersonLastFrame = first_person;
}

void SKOTSCamera::onAnimationStart(const LLVector3d& root_global, const LLQuaternion& rot)
{
    sAnimFromFirstPerson = sFirstPersonLastFrame;
    sAnimRootGlobal = root_global;
    sAnimRot = rot;
}

bool SKOTSCamera::animationFromFirstPerson()
{
    return sAnimFromFirstPerson;
}

LLVector3d SKOTSCamera::followAvatar(const LLVector3d& start_global, const LLVector3d& root_global, const LLQuaternion& rot)
{
    // Carry the start of the transition with the avatar, so a turn or strafe while it plays
    // does not leave the view pinned to where the avatar was.
    LLVector3 offset(start_global - sAnimRootGlobal);
    offset = offset * (~sAnimRot * rot);
    return root_global + LLVector3d(offset);
}
