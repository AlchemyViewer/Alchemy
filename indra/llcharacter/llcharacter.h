/**
 * @file llcharacter.h
 * @brief Implementation of LLCharacter class.
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

#ifndef LL_LLCHARACTER_H
#define LL_LLCHARACTER_H

//-----------------------------------------------------------------------------
// Header Files
//-----------------------------------------------------------------------------
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <boost/unordered_map.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "lljoint.h"
#include "llmotioncontroller.h"
#include "llstring.h"
#include "llvisualparam.h"
#include "llstringtable.h"
#include "llpointer.h"
#include "llrefcount.h"

class LLPolyMesh;

class LLPauseRequestHandle : public LLThreadSafeRefCount
{
public:
    LLPauseRequestHandle() {};
};

typedef LLPointer<LLPauseRequestHandle> LLAnimPauseRequest;

//-----------------------------------------------------------------------------
// class LLCharacter
//-----------------------------------------------------------------------------
class LLCharacter
{
public:
    // Constructor
    LLCharacter();

    // Destructor
    virtual ~LLCharacter();

    //-------------------------------------------------------------------------
    // LLCharacter Interface
    // These functions must be implemented by subclasses.
    //-------------------------------------------------------------------------

    // get the prefix to be used to lookup motion data files
    // from the viewer data directory
    virtual const char *getAnimationPrefix() = 0;

    // get the root joint of the character
    virtual LLJoint *getRootJoint() = 0;

    // get the specified joint
    // default implementation does recursive search,
    // subclasses may optimize/cache results.
    virtual LLJoint* getJoint(std::string_view name);

    // get the position of the character
    virtual LLVector3 getCharacterPosition() = 0;

    // get the rotation of the character
    virtual LLQuaternion getCharacterRotation() = 0;

    // get the velocity of the character
    virtual LLVector3 getCharacterVelocity() = 0;

    // get the angular velocity of the character
    virtual LLVector3 getCharacterAngularVelocity() = 0;

    // get the height & normal of the ground under a point
    virtual void getGround(const LLVector3 &inPos, LLVector3 &outPos, LLVector3 &outNorm) = 0;

    // skeleton joint accessor to support joint subclasses
    virtual LLJoint *getCharacterJoint( U32 i ) = 0;

    // get the physics time dilation for the simulator
    virtual F32 getTimeDilation() = 0;

    // gets current pixel area of this character
    virtual F32 getPixelArea() const = 0;

    // gets the head mesh of the character
    virtual LLPolyMesh* getHeadMesh() = 0;

    // gets the upper body mesh of the character
    virtual LLPolyMesh* getUpperBodyMesh() = 0;

    // gets global coordinates from agent local coordinates
    virtual LLVector3d  getPosGlobalFromAgent(const LLVector3 &position) = 0;

    // gets agent local coordinates from global coordinates
    virtual LLVector3   getPosAgentFromGlobal(const LLVector3d &position) = 0;

    // updates all visual parameters for this character, and says whether any
    // of them had anything to apply
    virtual bool updateVisualParams();

    virtual void addDebugText( const std::string& text ) = 0;

    virtual std::string getDebugName() const { return getID().asString(); }

    virtual const LLUUID&   getID() const = 0;
    //-------------------------------------------------------------------------
    // End Interface
    //-------------------------------------------------------------------------
    // registers a motion with the character
    // returns true if successfull
    bool registerMotion( const LLUUID& id, LLMotionConstructor create );

    void removeMotion( const LLUUID& id );

    // removes ALL instances of a motion -- the canonical one AND any deprecated
    // duplicates still easing out -- for when the motion's backing keyframe
    // data is about to be destroyed (see LLMotionController::purgeMotionInstances)
    void purgeMotionInstances( const LLUUID& id );

    // returns an instance of a registered motion, creating one if necessary
    LLMotion* createMotion( const LLUUID &id );

    // returns an existing instance of a registered motion
    LLMotion* findMotion( const LLUUID &id );

    // start a motion
    // returns true if successful, false if an error occurred
    virtual bool startMotion( const LLUUID& id, F32 start_offset = 0.f);

    // stop a motion
    virtual bool stopMotion( const LLUUID& id, bool stop_immediate = false );

    // is this motion active?
    bool isMotionActive( const LLUUID& id );

    // Event handler for motion deactivation.
    // Called when a motion has completely stopped and has been deactivated.
    // Subclasses may optionally override this.
    // The default implementation does nothing.
    virtual void requestStopMotion( LLMotion* motion );

    // periodic update function, steps the motion controller
    enum e_update_t { NORMAL_UPDATE, HIDDEN_UPDATE, FORCE_UPDATE };
    void updateMotions(e_update_t update_type);

    LLAnimPauseRequest requestPause();
    bool areAnimationsPaused() const { return mMotionController.isPaused(); }
    void setAnimTimeFactor(F32 factor) { mMotionController.setTimeFactor(factor); }
    void setTimeStep(F32 time_step) { mMotionController.setTimeStep(time_step); }

    LLMotionController& getMotionController() { return mMotionController; }

    // Releases all motion instances which should result in
    // no cached references to character joint data.  This is
    // useful if a character wants to rebuild it's skeleton.
    virtual void flushAllMotions();

    // Flush only wipes active animations.
    virtual void deactivateAllMotions();

    // dumps information for debugging
    virtual void dumpCharacter( LLJoint *joint = NULL );

    virtual F32 getPreferredPelvisHeight() { return mPreferredPelvisHeight; }

    virtual LLVector3 getVolumePos(S32 joint_index, LLVector3& volume_offset) { return LLVector3::zero; }

    virtual LLJoint* findCollisionVolume(S32 volume_id) { return NULL; }

    virtual S32 getCollisionVolumeID(std::string &name) { return -1; }

    // The values motions hand one another through the character. Each is a
    // pointer into whoever owns it, set while that owner is active; a channel
    // nobody is filling reads null. They were looked up by name, which hashed
    // the string for every motion that asked, every frame.
    enum EAnimationChannel
    {
        ANIM_CHANNEL_LOOK_AT_POINT,         // LLVector3, agent space
        ANIM_CHANNEL_POINT_AT_POINT,        // LLVector3, agent space
        ANIM_CHANNEL_WALK_SPEED,            // F32
        NUM_ANIM_CHANNELS
    };
    void  setAnimationData(EAnimationChannel channel, void* data) { mAnimationChannels[channel] = data; }
    void* getAnimationData(EAnimationChannel channel) const { return mAnimationChannels[channel]; }
    void  removeAnimationData(EAnimationChannel channel) { mAnimationChannels[channel] = nullptr; }

    // The hand pose asked for since the hand motion last looked, and the
    // priority of the motion asking. Unlike the channels above this is a copy,
    // not a pointer: a keyframe motion asks with the pose its animation
    // carries, and the animation cache can free that animation before the hand
    // motion gets to read the request.
    void requestHandPose(S32 pose, S32 priority)   // LLHandMotion::eHandPose, LLJoint::JointPriority
    {
        mHandPoseRequest = pose;
        mHandPoseRequestPriority = priority;
        mHandPoseRequested = true;
    }
    bool hasHandPoseRequest() const { return mHandPoseRequested; }
    S32  getHandPoseRequest() const { return mHandPoseRequest; }
    S32  getHandPoseRequestPriority() const { return mHandPoseRequestPriority; }
    void clearHandPoseRequest() { mHandPoseRequested = false; }

    void addVisualParam(LLVisualParam *param);
    void addSharedVisualParam(LLVisualParam *param);

    virtual bool setVisualParamWeight(const LLVisualParam *which_param, F32 weight);
    virtual bool setVisualParamWeight(const char* param_name, F32 weight);
    virtual bool setVisualParamWeight(S32 index, F32 weight);
    virtual bool setVisualParamWeight(S32 index, S32 type, F32 weight);

    // get visual param weight by param or name
    F32 getVisualParamWeight(LLVisualParam *distortion);
    F32 getVisualParamWeight(const char* param_name);
    F32 getVisualParamWeight(S32 index);

    // set all morph weights to defaults
    void clearVisualParamWeights();

    // visual parameter accessors
    LLVisualParam*  getFirstVisualParam()
    {
        mCurVisualParam = 0;
        return getNextVisualParam();
    }
    LLVisualParam*  getNextVisualParam()
    {
        if (mCurVisualParam >= mVisualParams.size())
            return 0;
        return mVisualParams[mCurVisualParam++];
    }

    S32 getVisualParamCountInGroup(const EVisualParamGroup group) const
    {
        S32 rtn = 0;
        for (const LLVisualParam* param : mVisualParams)
        {
            if (param->getGroup() == group)
            {
                ++rtn;
            }
        }
        return rtn;
    }

    LLVisualParam*  getVisualParam(S32 id) const
    {
        visual_param_index_map_t::const_iterator iter = mVisualParamIndexMap.find(id);
        return (iter == mVisualParamIndexMap.end()) ? 0 : iter->second;
    }
    S32 getVisualParamID(LLVisualParam *id)
    {
        for (visual_param_index_map_t::value_type& index_pair : mVisualParamIndexMap)
        {
            if (index_pair.second == id)
                return index_pair.first;
        }
        return 0;
    }
    S32             getVisualParamCount() const { return (S32)mVisualParamIndexMap.size(); }
    LLVisualParam*  getVisualParam(const char *name);

    void animateTweakableVisualParams(F32 delta)
    {
        for (LLVisualParam* param : mVisualParams)
        {
            if (param->isTweakable())
            {
                param->animate(delta);
            }
        }
    }

    void applyAllVisualParams(ESex avatar_sex)
    {
        for (LLVisualParam* param : mVisualParams)
        {
            param->apply(avatar_sex);
        }
    }

    ESex getSex() const         { return mSex; }
    void setSex( ESex sex )     { mSex = sex; }

    U32             getAppearanceSerialNum() const      { return mAppearanceSerialNum; }
    void            setAppearanceSerialNum( U32 num )   { mAppearanceSerialNum = num; }

    U32             getSkeletonSerialNum() const        { return mSkeletonSerialNum; }
    void            setSkeletonSerialNum( U32 num ) { mSkeletonSerialNum = num; }

    static std::list< LLCharacter* > sInstances;
    static bool sAllowInstancesChange ; //debug use

    virtual void    setHoverOffset(const LLVector3& hover_offset, bool send_update=true) { mHoverOffset = hover_offset; }
    const LLVector3& getHoverOffset() const { return mHoverOffset; }

protected:
    LLMotionController  mMotionController;

    void*               mAnimationChannels[NUM_ANIM_CHANNELS] = {};
    S32                 mHandPoseRequest = 0;
    S32                 mHandPoseRequestPriority = 0;
    bool                mHandPoseRequested = false;

    F32                 mPreferredPelvisHeight;
    ESex                mSex;
    U32                 mAppearanceSerialNum;
    U32                 mSkeletonSerialNum;
    LLAnimPauseRequest  mPauseRequest;

private:
    // The names come from avatar_lad.xml and are matched without regard to
    // case, so the map hashes and compares them that way rather than making a
    // lowercased copy of every name it is asked about.
    struct visual_param_name_hash
    {
        using is_transparent = void;
        size_t operator()(std::string_view name) const
        {
            U64 hash = 14695981039346656037ULL;
            for (char c : name)
            {
                hash ^= (U64)(U8)LLStringOps::toLower(c);
                hash *= 1099511628211ULL;
            }
            return (size_t)hash;
        }
    };
    struct visual_param_name_equal
    {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const
        {
            return a.size() == b.size()
                && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
                   { return LLStringOps::toLower(x) == LLStringOps::toLower(y); });
        }
    };

    // visual parameter stuff
    typedef std::map<S32, LLVisualParam *>      visual_param_index_map_t;
    typedef boost::unordered_flat_map<std::string, LLVisualParam*,
                                      visual_param_name_hash,
                                      visual_param_name_equal> visual_param_name_map_t;

    // The same parameters as the index map, in the same order, for the sweeps
    // that read every one of them every frame.
    std::vector<LLVisualParam*>                 mVisualParams;
    size_t                                      mCurVisualParam = 0;
    visual_param_index_map_t                    mVisualParamIndexMap;
    visual_param_name_map_t                     mVisualParamNameMap;

    LLVector3 mHoverOffset;
};

#endif // LL_LLCHARACTER_H

