/**
 * @file llpose.h
 * @brief Implementation of LLPose class.
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

#ifndef LL_LLPOSE_H
#define LL_LLPOSE_H

//-----------------------------------------------------------------------------
// Header Files
//-----------------------------------------------------------------------------

#include "lljointstate.h"
#include "lljoint.h"
#include "llpointer.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>


//-----------------------------------------------------------------------------
// class LLPose
//-----------------------------------------------------------------------------
class LLPose
{
    friend class LLPoseBlender;
public:
    typedef std::vector<LLPointer<LLJointState> > joint_state_list_t;
protected:
    // A few dozen states at most, added and removed at setup or by the
    // poser, and walked by the blender every frame: contiguous, with a
    // linear find. One state per joint; addJointState keeps it that way.
    joint_state_list_t          mJointStates;
    F32                         mWeight;
public:
    const joint_state_list_t& getJointStates() const { return mJointStates; }
    LLJointState* findJointState(LLJoint *joint);
    LLJointState* findJointState(std::string_view name);
public:
    // Constructor
    LLPose() : mWeight(0.f) {}
    // Destructor
    ~LLPose();
    // add a joint state in this pose
    bool addJointState(const LLPointer<LLJointState>& jointState);
    // remove a joint state from this pose
    bool removeJointState(const LLPointer<LLJointState>& jointState);
    // removes all joint states from this pose
    bool removeAllJointStates();
    // set weight for all joint states in this pose
    void setWeight(F32 weight);
    // get weight for this pose
    F32 getWeight() const;
    // returns number of joint states stored in this pose
    S32 getNumJointStates() const;
};

const S32 JSB_NUM_JOINT_STATES = 6;

class alignas(16) LLJointStateBlender
{
    LL_ALIGN_NEW
protected:
    LLPointer<LLJointState> mJointStates[JSB_NUM_JOINT_STATES];
    S32             mPriorities[JSB_NUM_JOINT_STATES];
    bool            mAdditiveBlends[JSB_NUM_JOINT_STATES];

    // Where the coarse clock's blend goes instead of the joint, for the
    // frames in between to interpolate the joint toward.
    LLVector3       mCachedPosition;
    LLQuaternion    mCachedRotation;
    LLVector3       mCachedScale;

    // on the pose blender's list of blenders to run this frame
    bool            mQueued;
public:
    LLJointStateBlender();
    ~LLJointStateBlender();
    void blendJointStates(bool apply_now = true);
    bool addJointState(const LLPointer<LLJointState>& joint_state, S32 priority, bool additive_blend);
    void interpolate(F32 u);
    void clear();
    void resetCachedJoint();

    bool isQueued() const { return mQueued; }
    void setQueued(bool queued) { mQueued = queued; }
};

class LLMotion;

class LLPoseBlender
{
protected:
    typedef std::vector<LLJointStateBlender*> blender_list_t;
    // one slot per joint number, filled the first time that joint is animated
    LLJointStateBlender* mJointStateBlenderPool[LL_CHARACTER_MAX_ANIMATED_JOINTS];
    blender_list_t mActiveBlenders;

    S32         mNextPoseSlot;
    LLPose      mBlendedPose;
public:
    // Constructor
    LLPoseBlender();
    // Destructor
    ~LLPoseBlender();

    // request motion joint states to be added to pose blender joint state records
    bool addMotion(LLMotion* motion);

    // blend all joint states and apply to skeleton
    void blendAndApply();

    // removes all joint state blenders from last time
    void clearBlenders();

    // blend all joint states and cache results
    void blendAndCache(bool reset_cached_joints);

    // interpolate all joints towards cached values
    void interpolate(F32 u);

    LLPose* getBlendedPose() { return &mBlendedPose; }
};

#endif // LL_LLPOSE_H

