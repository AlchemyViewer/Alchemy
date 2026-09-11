/**
 * @file llkeyframemotion.h
 * @brief Implementation of LLKeframeMotion class.
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

#ifndef LL_LLKEYFRAMEMOTION_H
#define LL_LLKEYFRAMEMOTION_H

//-----------------------------------------------------------------------------
// Header files
//-----------------------------------------------------------------------------

#include <string>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include "llassetstorage.h"
#include "llbboxlocal.h"
#include "llhandmotion.h"
#include "lljointstate.h"
#include "llmotion.h"
#include "llpointer.h"
#include "llquaternion.h"
#include "llrefcount.h"
#include "v3dmath.h"
#include "v3math.h"
#include "llbvhconsts.h"

class LLKeyframeDataCache;
class LLDataPacker;

// Below this the controller fades the motion out and stops updating it, and
// the joints keep the pose they were left in -- so this is the size at which
// an avatar stops moving and stands still instead. Six pixels on a side is
// small enough that there is nothing left to see standing still.
//
// It was briefly 500, the same as head turning, on the reasoning that the one
// motion writing most of the skeleton should not outlast the cheap ones. That
// is twenty-two pixels on a side, which is an avatar you can make out in a
// crowd, and freezing it is the most visible thing this subsystem can do. The
// cost belongs somewhere the eye cannot find it.
constexpr F32 MIN_REQUIRED_PIXEL_AREA_KEYFRAME = 40.f;
#define MAX_CHAIN_LENGTH (4)

const S32 KEYFRAME_MOTION_VERSION = 1;
const S32 KEYFRAME_MOTION_SUBVERSION = 0;

//-----------------------------------------------------------------------------
// class LLKeyframeMotion
//-----------------------------------------------------------------------------
class LLKeyframeMotion :
    public LLMotion
{
    friend class LLKeyframeDataCache;
public:
    // Constructor
    LLKeyframeMotion(const LLUUID &id);

    // Destructor
    virtual ~LLKeyframeMotion();

private:
    // private helper functions to wrap some asserts
    LLPointer<LLJointState>& getJointState(U32 index);
    LLJoint* getJoint(U32 index );

public:
    //-------------------------------------------------------------------------
    // functions to support MotionController and MotionRegistry
    //-------------------------------------------------------------------------

    // static constructor
    // all subclasses must implement such a function and register it
    static LLMotion *create(const LLUUID& id);

public:
    //-------------------------------------------------------------------------
    // animation callbacks to be implemented by subclasses
    //-------------------------------------------------------------------------

    // motions must specify whether or not they loop
    virtual bool getLoop() {
        if (mJointMotionList) return mJointMotionList->mLoop;
        else return false;
    }

    // motions must report their total duration
    virtual F32 getDuration() {
        if (mJointMotionList) return mJointMotionList->mDuration;
        else return 0.f;
    }

    // motions must report their "ease in" duration
    virtual F32 getEaseInDuration() {
        if (mJointMotionList) return mJointMotionList->mEaseInDuration;
        else return 0.f;
    }

    // motions must report their "ease out" duration.
    virtual F32 getEaseOutDuration() {
        if (mJointMotionList) return mJointMotionList->mEaseOutDuration;
        else return 0.f;
    }

    // motions must report their priority
    virtual LLJoint::JointPriority getPriority() {
        if (mJointMotionList) return mJointMotionList->mBasePriority;
        else return LLJoint::LOW_PRIORITY;
    }

    virtual S32 getNumJointMotions()
    {
        if (mJointMotionList)
        {
            return mJointMotionList->getNumJointMotions();
        }
        return 0;
    }

    virtual LLMotionBlendType getBlendType() { return NORMAL_BLEND; }

    // called to determine when a motion should be activated/deactivated based on avatar pixel coverage
    virtual F32 getMinPixelArea() { return MIN_REQUIRED_PIXEL_AREA_KEYFRAME; }

    // run-time (post constructor) initialization,
    // called after parameters have been set
    // must return true to indicate success and be available for activation
    virtual LLMotionInitStatus onInitialize(LLCharacter *character);

    // called when a motion is activated
    // must return true to indicate success, or else
    // it will be deactivated
    virtual bool onActivate();

    // called per time step
    // must return true while it is active, and
    // must return false when the motion is completed.
    virtual bool onUpdate(F32 time, U8* joint_mask);

    // called when a motion is deactivated
    virtual void onDeactivate();

    virtual void setStopTime(F32 time);

    static void onLoadComplete(const LLUUID& asset_uuid,
                               LLAssetType::EType type,
                               void* user_data, S32 status, LLExtStat ext_status);

public:
    U32     getFileSize();
    bool    serialize(LLDataPacker& dp) const;
    bool    deserialize(LLDataPacker& dp, const LLUUID& asset_id, bool allow_invalid_joints = true);
    bool    isLoaded() { return mJointMotionList.notNull(); }
    bool    dumpToFile(const std::string& name);


    // setters for modifying a keyframe animation
    void setLoop(bool loop);

    F32 getLoopIn() {
        return (mJointMotionList) ? mJointMotionList->mLoopInPoint : 0.f;
    }

    F32 getLoopOut() {
        return (mJointMotionList) ? mJointMotionList->mLoopOutPoint : 0.f;
    }

    void setLoopIn(F32 in_point);
    // Recomputes every curve's loop tail from the current loop settings.
    void setupLoopSeams();

    void setLoopOut(F32 out_point);

    void setHandPose(LLHandMotion::eHandPose pose) {
        if (mJointMotionList) mJointMotionList->mHandPose = pose;
    }

    LLHandMotion::eHandPose getHandPose() {
        return (mJointMotionList) ? mJointMotionList->mHandPose : LLHandMotion::HAND_POSE_RELAXED;
    }

    void setPriority(S32 priority);

    void setEmote(const LLUUID& emote_id);

    void setEaseIn(F32 ease_in);

    void setEaseOut(F32 ease_in);

    F32 getLastUpdateTime() { return mLastLoopedTime; }

    const LLBBoxLocal& getPelvisBBox();

    static void flushKeyframeCache();

protected:
    //-------------------------------------------------------------------------
    // JointConstraintSharedData
    //-------------------------------------------------------------------------
    class JointConstraintSharedData
    {
    public:
        JointConstraintSharedData() :
            mChainLength(0),
            mEaseInStartTime(0.f),
            mEaseInStopTime(0.f),
            mEaseOutStartTime(0.f),
            mEaseOutStopTime(0.f),
            mUseTargetOffset(false),
            mConstraintType(CONSTRAINT_TYPE_POINT),
            mConstraintTargetType(CONSTRAINT_TARGET_TYPE_BODY),
            mSourceConstraintVolume(0),
            mTargetConstraintVolume(0),
            mJointStateIndices(NULL)
        { };
        ~JointConstraintSharedData() { delete [] mJointStateIndices; }

        S32                     mSourceConstraintVolume;
        LLVector3               mSourceConstraintOffset;
        S32                     mTargetConstraintVolume;
        LLVector3               mTargetConstraintOffset;
        LLVector3               mTargetConstraintDir;
        S32                     mChainLength;
        S32*                    mJointStateIndices;
        F32                     mEaseInStartTime;
        F32                     mEaseInStopTime;
        F32                     mEaseOutStartTime;
        F32                     mEaseOutStopTime;
        bool                    mUseTargetOffset;
        EConstraintType         mConstraintType;
        EConstraintTargetType   mConstraintTargetType;
    };

    //-----------------------------------------------------------------------------
    // JointConstraint()
    //-----------------------------------------------------------------------------
    class JointConstraint
    {
    public:
        JointConstraint(JointConstraintSharedData* shared_data);
        ~JointConstraint();

        JointConstraintSharedData*  mSharedData;
        F32                         mWeight;
        F32                         mTotalLength;
        LLVector3                   mPositions[MAX_CHAIN_LENGTH];
        F32                         mJointLengths[MAX_CHAIN_LENGTH];
        F32                         mJointLengthFractions[MAX_CHAIN_LENGTH];
        bool                        mActive;
        LLVector3d                  mGroundPos;
        LLVector3                   mGroundNorm;
        LLJoint*                    mSourceVolume;
        LLJoint*                    mTargetVolume;
        F32                         mFixupDistanceRMS;
    };

    void applyKeyframes(F32 time, const U8* joint_mask);

    void applyConstraints(F32 time, U8* joint_mask);

    void activateConstraint(JointConstraint* constraintp);

    void initializeConstraint(JointConstraint* constraint);

    void deactivateConstraint(JointConstraint *constraintp);

    void applyConstraint(JointConstraint* constraintp, F32 time, U8* joint_mask);

    bool    setupPose();

public:
    enum AssetStatus { ASSET_LOADED, ASSET_FETCHED, ASSET_NEEDS_FETCH, ASSET_FETCH_FAILED, ASSET_UNDEFINED };

    enum InterpolationType { IT_STEP, IT_LINEAR, IT_SPLINE };

    //-------------------------------------------------------------------------
    // KeyCurve
    //-------------------------------------------------------------------------
    // One channel of one joint: its keys sorted by time and unique in it, the
    // times in an array of their own so a search reads nothing else. A curve
    // is shared by every avatar playing the animation, so the cursor that
    // makes a run of nearby samples cheap belongs to the caller.
    template <typename T>
    class KeyCurve
    {
    public:
        // Adds a key, or replaces the one already at this time.
        void setKey(F32 time, const T& value);

        U32 getNumKeys() const { return static_cast<U32>(mTimes.size()); }
        F32 getKeyTime(U32 index) const { return mTimes[index]; }
        const T& getKeyValue(U32 index) const { return mValues[index]; }

        // The nearest key outside the keyed range, the key itself on one,
        // otherwise the two neighbours blended.
        T getValue(F32 time) const;
        // The same, remembering where the sample landed so that the next one
        // nearby is placed in a compare or two. Any cursor is safe to pass,
        // including a stale one.
        T getValue(F32 time, U32& cursor) const;

        // Where a looping animation goes when its keys run out before its loop
        // does: the stretch from the last key to the loop out point leads back
        // to the pose the loop starts from, rather than holding still and then
        // arriving there in one frame. Called with the loop off, or with a
        // loop that ends on a key, it takes the tail away again.
        void setLoopSeam(bool looping, F32 loop_in_time, F32 loop_out_time);

        InterpolationType   mInterpolationType = IT_LINEAR;

    private:
        // The first key at or after the time, or the key count when none is.
        U32 findKey(F32 time, U32 hint) const;

        std::vector<F32>    mTimes;
        std::vector<T>      mValues;

        T                   mLoopInValue {};
        F32                 mLoopOutTime = 0.f;
        bool                mLoopSeam = false;
    };

    // Held in the vector forms, which is what a joint state takes: a sample
    // goes from the curve into a joint state once per channel per joint per
    // playing motion per frame, and through the scalar types that was a store
    // on the way out of the blend and a load on the way into the state.
    typedef KeyCurve<LLVector4a>    ScaleCurve;
    typedef KeyCurve<LLQuaternion2> RotationCurve;
    typedef KeyCurve<LLVector4a>    PositionCurve;

    //-------------------------------------------------------------------------
    // KeyCursors
    //-------------------------------------------------------------------------
    // Where a joint's three channels were last sampled, kept by the motion
    // instance playing them.
    struct KeyCursors
    {
        U32 mScale = 0;
        U32 mRotation = 0;
        U32 mPosition = 0;
    };

    //-------------------------------------------------------------------------
    // JointMotion
    //-------------------------------------------------------------------------
    class JointMotion
    {
    public:
        PositionCurve   mPositionCurve;
        RotationCurve   mRotationCurve;
        ScaleCurve      mScaleCurve;
        std::string     mJointName;
        U32             mUsage;
        LLJoint::JointPriority  mPriority;

        void update(LLJointState* joint_state, F32 time, KeyCursors& cursors);
    };

    //-------------------------------------------------------------------------
    // JointMotionList
    //-------------------------------------------------------------------------
    // Held by the cache and by every motion playing it, so it goes when the
    // last of them lets go rather than when the first of them does.
    class JointMotionList : public LLRefCount
    {
    public:
        // The joints an animation writes, in one block. Every playing
        // instance walks all of them every frame, and one heap allocation
        // each turned that walk into a chase across the heap.
        std::vector<JointMotion> mJointMotionArray;
        F32                     mDuration;
        bool                    mLoop;
        F32                     mLoopInPoint;
        F32                     mLoopOutPoint;
        F32                     mEaseInDuration;
        F32                     mEaseOutDuration;
        LLJoint::JointPriority  mBasePriority;
        LLHandMotion::eHandPose mHandPose;
        LLJoint::JointPriority  mMaxPriority;
        typedef std::list<JointConstraintSharedData*> constraint_list_t;
        constraint_list_t       mConstraints;
        LLBBoxLocal             mPelvisBBox;
        // mEmoteName is a facial motion, but it's necessary to appear here so that it's cached.
        std::string             mEmoteName;
        LLUUID                  mEmoteID;

    public:
        JointMotionList();
        ~JointMotionList();
        U32 dumpDiagInfo();
        JointMotion* getJointMotion(U32 index) { llassert(index < mJointMotionArray.size()); return &mJointMotionArray[index]; }
        U32 getNumJointMotions() const { return static_cast<U32>(mJointMotionArray.size()); }
    };

protected:
    LLPointer<JointMotionList>      mJointMotionList;
    std::vector<LLPointer<LLJointState> > mJointStates;
    std::vector<KeyCursors>         mKeyCursors;
    LLJoint*                        mPelvisp;
    LLCharacter*                    mCharacter;
    typedef std::list<JointConstraint*> constraint_list_t;
    constraint_list_t               mConstraints;
    U32                             mLastSkeletonSerialNum;
    F32                             mLastUpdateTime;
    F32                             mLastLoopedTime;
    AssetStatus                     mAssetStatus;

public:
    void setCharacter(LLCharacter* character) { mCharacter = character; }
};

// Every animation any character has played is kept here so that playing it
// again costs nothing. Nothing is owned outright: an entry goes when the cache
// and every motion holding it have all let go.
class LLKeyframeDataCache
{
public:
    LLKeyframeDataCache() = delete;

    typedef boost::unordered_flat_map<LLUUID, LLPointer<LLKeyframeMotion::JointMotionList>> keyframe_data_map_t;
    static keyframe_data_map_t sKeyframeDataMap;

    static void addKeyframeData(const LLUUID& id, LLKeyframeMotion::JointMotionList*);
    static LLKeyframeMotion::JointMotionList* getKeyframeData(const LLUUID& id);

    static void removeKeyframeData(const LLUUID& id);

    // Drops every entry no motion is still holding. What is left is what some
    // character can still play without fetching it again.
    static void purge();

    static size_t size() { return sKeyframeDataMap.size(); }

    //print out diagnostic info
    static void dumpDiagInfo();
    static void clear();
};

#endif // LL_LLKEYFRAMEMOTION_H


