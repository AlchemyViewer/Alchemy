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

#include "llassetstorage.h"
#include "llbboxlocal.h"
#include "llhandmotion.h"
#include "lljointstate.h"
#include "llmotion.h"
#include "llquaternion.h"
#include "v3dmath.h"
#include "v3math.h"
#include "llbvhconsts.h"

#include <absl/container/flat_hash_map.h>

class LLKeyframeDataCache;
class LLDataPacker;

#define MIN_REQUIRED_PIXEL_AREA_KEYFRAME (40.f)
#define MAX_CHAIN_LENGTH (4)

const S32 KEYFRAME_MOTION_VERSION = 1;
const S32 KEYFRAME_MOTION_SUBVERSION = 0;

//-----------------------------------------------------------------------------
// class LLKeyframeMotion
//-----------------------------------------------------------------------------

namespace LLKeyframeMotionLerp
{
	template<typename T>
	inline T lerp(F32 t, const T& before, const T& after)
	{
		return ::lerp(before, after, t);
	}

	template<>
	inline LLQuaternion lerp(F32 t, const LLQuaternion& before, const LLQuaternion& after)
	{
		return nlerp(t, before, after);
	}
}

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
	virtual BOOL getLoop() { 
		if (mJointMotionList) return mJointMotionList->mLoop; 
		else return FALSE;
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
	// must return TRUE to indicate success, or else
	// it will be deactivated
	virtual BOOL onActivate();

	// called per time step
	// must return TRUE while it is active, and
	// must return FALSE when the motion is completed.
	virtual BOOL onUpdate(F32 time, U8* joint_mask);

	// called when a motion is deactivated
	virtual void onDeactivate();

	virtual void setStopTime(F32 time);

	static void onLoadComplete(const LLUUID& asset_uuid,
							   LLAssetType::EType type,
							   void* user_data, S32 status, LLExtStat ext_status);

public:
	U32		getFileSize();
	BOOL	serialize(LLDataPacker& dp) const;
	BOOL	deserialize(LLDataPacker& dp, const LLUUID& asset_id);
	BOOL	isLoaded() { return mJointMotionList != NULL; }
    void	dumpToFile(const std::string& name);


	// setters for modifying a keyframe animation
	void setLoop(BOOL loop);

	F32 getLoopIn() {
		return (mJointMotionList) ? mJointMotionList->mLoopInPoint : 0.f;
	}

	F32 getLoopOut() {
		return (mJointMotionList) ? mJointMotionList->mLoopOutPoint : 0.f;
	}
	
	void setLoopIn(F32 in_point);

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
			mUseTargetOffset(FALSE),
			mConstraintType(CONSTRAINT_TYPE_POINT),
			mConstraintTargetType(CONSTRAINT_TARGET_TYPE_BODY),
			mSourceConstraintVolume(0),
			mTargetConstraintVolume(0),
			mJointStateIndices(NULL)
		{ };
		~JointConstraintSharedData() { delete [] mJointStateIndices; }

		S32						mSourceConstraintVolume;
		LLVector3				mSourceConstraintOffset;
		S32						mTargetConstraintVolume;
		LLVector3				mTargetConstraintOffset;
		LLVector3				mTargetConstraintDir;
		S32						mChainLength;
		S32*					mJointStateIndices;
		F32						mEaseInStartTime;
		F32						mEaseInStopTime;
		F32						mEaseOutStartTime;
		F32						mEaseOutStopTime;
		BOOL					mUseTargetOffset;
		EConstraintType			mConstraintType;
		EConstraintTargetType	mConstraintTargetType;
	};

	//-----------------------------------------------------------------------------
	// JointConstraint()
	//-----------------------------------------------------------------------------
	class JointConstraint
	{
	public:
		JointConstraint(JointConstraintSharedData* shared_data);
		~JointConstraint() = default;

		JointConstraintSharedData*	mSharedData;
		F32							mWeight;
		F32							mTotalLength;
		LLVector3					mPositions[MAX_CHAIN_LENGTH];
		F32							mJointLengths[MAX_CHAIN_LENGTH];
		F32							mJointLengthFractions[MAX_CHAIN_LENGTH];
		BOOL						mActive;
		LLVector3d					mGroundPos;
		LLVector3					mGroundNorm;
		LLJoint*					mSourceVolume;
		LLJoint*					mTargetVolume;
		F32							mFixupDistanceRMS;
	};

	void applyKeyframes(F32 time);

	void applyConstraints(F32 time, U8* joint_mask);

	void activateConstraint(JointConstraint* constraintp);

	void initializeConstraint(JointConstraint* constraint);

	void deactivateConstraint(JointConstraint *constraintp);

	void applyConstraint(JointConstraint* constraintp, F32 time, U8* joint_mask);

	BOOL	setupPose();

public:
	enum AssetStatus { ASSET_LOADED, ASSET_FETCHED, ASSET_NEEDS_FETCH, ASSET_FETCH_FAILED, ASSET_UNDEFINED };

	enum InterpolationType { IT_STEP, IT_LINEAR, IT_SPLINE };

	template<typename T>
	struct Curve
	{
		struct Key
		{
			Key() = default;
			Key(F32 time, const T& value) { mTime = time; mValue = value; }
			F32			mTime = 0;
			T			mValue;
		};

		T interp(F32 u, Key& before, Key& after)
		{
			switch (mInterpolationType)
			{
			case IT_STEP:
				return before.mValue;
			default:
			case IT_LINEAR:
			case IT_SPLINE:
				return LLKeyframeMotionLerp::lerp(u, before.mValue, after.mValue);
			}
		}

		T getValue(F32 time, F32 duration)
		{
			if (mKeys.empty())
			{
				return T();
			}

			T value;
			typename key_map_t::iterator right = std::lower_bound(mKeys.begin(), mKeys.end(), time, [](const auto& a, const auto& b) { return a.first < b; });
			if (right == mKeys.end())
			{
				// Past last key
				--right;
				value = right->second.mValue;
			}
			else if (right == mKeys.begin() || right->first == time)
			{
				// Before first key or exactly on a key
				value = right->second.mValue;
			}
			else
			{
				// Between two keys
				typename key_map_t::iterator left = right; --left;
				F32 index_before = left->first;
				F32 index_after = right->first;
				Key& pos_before = left->second;
				Key& pos_after = right->second;
				if (right == mKeys.end())
				{
					pos_after = mLoopInKey;
					index_after = duration;
				}

				F32 u = (time - index_before) / (index_after - index_before);
				value = interp(u, pos_before, pos_after);
			}
			return value;
		}

		InterpolationType	mInterpolationType = LLKeyframeMotion::IT_LINEAR;
		S32					mNumKeys = 0;
		typedef std::vector< std::pair<F32, Key> > key_map_t;
		key_map_t 			mKeys;
		Key					mLoopInKey;
		Key					mLoopOutKey;
	};

	typedef Curve<LLVector3> ScaleCurve;
	typedef ScaleCurve::Key ScaleKey;
	typedef Curve<LLQuaternion> RotationCurve;
	typedef RotationCurve::Key RotationKey;
	typedef Curve<LLVector3> PositionCurve;
	typedef PositionCurve::Key PositionKey;

	//-------------------------------------------------------------------------
	// JointMotion
	//-------------------------------------------------------------------------
	class JointMotion
	{
	public:
		PositionCurve	mPositionCurve;
		RotationCurve	mRotationCurve;
		ScaleCurve		mScaleCurve;
		std::string		mJointName;
		U32				mUsage;
		LLJoint::JointPriority	mPriority;

		void update(LLJointState* joint_state, F32 time, F32 duration);
	};
	
	//-------------------------------------------------------------------------
	// JointMotionList
	//-------------------------------------------------------------------------
	class JointMotionList
	{
	public:
		std::vector<JointMotion*> mJointMotionArray;
		F32						mDuration;
		BOOL					mLoop;
		F32						mLoopInPoint;
		F32						mLoopOutPoint;
		F32						mEaseInDuration;
		F32						mEaseOutDuration;
		LLJoint::JointPriority	mBasePriority;
		LLHandMotion::eHandPose mHandPose;
		LLJoint::JointPriority  mMaxPriority;
		typedef std::list<JointConstraintSharedData*> constraint_list_t;
		constraint_list_t		mConstraints;
		LLBBoxLocal				mPelvisBBox;
		// mEmoteName is a facial motion, but it's necessary to appear here so that it's cached.
		// TODO: LLKeyframeDataCache::getKeyframeData should probably return a class containing 
		// JointMotionList and mEmoteName, see LLKeyframeMotion::onInitialize.
		std::string				mEmoteName; 
	public:
		JointMotionList();
		~JointMotionList();
		U32 dumpDiagInfo();
		JointMotion* getJointMotion(U32 index) const { llassert(index < mJointMotionArray.size()); return mJointMotionArray[index]; }
		U32 getNumJointMotions() const { return mJointMotionArray.size(); }
	};

protected:
	JointMotionList*				mJointMotionList;
	std::vector<LLPointer<LLJointState> > mJointStates;
	LLJoint*						mPelvisp;
	LLCharacter*					mCharacter;
	typedef std::list<JointConstraint*>	constraint_list_t;
	constraint_list_t				mConstraints;
	U32								mLastSkeletonSerialNum;
	F32								mLastUpdateTime;
	F32								mLastLoopedTime;
	AssetStatus						mAssetStatus;
};

class LLKeyframeDataCache
{
public:
	// *FIX: implement this as an actual singleton member of LLKeyframeMotion
	LLKeyframeDataCache() = default;
	~LLKeyframeDataCache();

	typedef absl::flat_hash_map<LLUUID, class LLKeyframeMotion::JointMotionList*> keyframe_data_map_t; 
	static keyframe_data_map_t sKeyframeDataMap;

	static void addKeyframeData(const LLUUID& id, LLKeyframeMotion::JointMotionList*);
	static LLKeyframeMotion::JointMotionList* getKeyframeData(const LLUUID& id);

	static void removeKeyframeData(const LLUUID& id);

	//print out diagnostic info
	static void dumpDiagInfo();
	static void clear();
};

#endif // LL_LLKEYFRAMEMOTION_H


