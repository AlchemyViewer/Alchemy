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

#include <deque>
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

class LLJointStateBlender
{
protected:
    // Sorted by priority, highest first, and packed: the first mNumStates
    // slots hold something and the rest are empty. One joint is written by
    // one or two motions almost always, so nothing here walks six slots to
    // find that out.
    LLPointer<LLJointState> mJointStates[JSB_NUM_JOINT_STATES];
    S32             mPriorities[JSB_NUM_JOINT_STATES];
    bool            mAdditiveBlends[JSB_NUM_JOINT_STATES];
    S32             mNumStates;

    // Where the coarse clock's blend goes instead of the joint, for the
    // frames in between to interpolate the joint toward. Held in the forms
    // the blend writes them in and the interpolation reads them in, which is
    // the same pair of forms the joint states carry.
    LLVector4a      mCachedPosition;
    LLQuaternion2   mCachedRotation;
    LLVector4a      mCachedScale;

    // Which of those three the blend actually wrote. The rest hold the
    // joint's own value and are not interpolated toward anything.
    U32             mCachedUsage;

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
    // The blenders themselves, in the order the joints were first animated,
    // which for a skeleton driven by one animation is joint order. A deque
    // keeps addresses stable while still handing out neighbours in the same
    // block: the per-frame walk is over pointers, and one blender per heap
    // allocation made it a walk across the heap.
    std::deque<LLJointStateBlender> mBlenderStorage;
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

    // Request motion joint states to be added to pose blender joint state
    // records. `saturated_joints` carries the priority at which each joint is
    // already claimed by a motion at full weight; a state that only rotates
    // such a joint is left out, since the blend would interpolate it to
    // nothing and it would take one of the joint's six slots to do it.
    bool addMotion(LLMotion* motion, const U8* saturated_joints);

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

