/**
 * @file lljointstate.h
 * @brief Implementation of LLJointState class.
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

#ifndef LL_LLJOINTSTATE_H
#define LL_LLJOINTSTATE_H

//-----------------------------------------------------------------------------
// Header Files
//-----------------------------------------------------------------------------
#include "lljoint.h"
#include "llquaternion2.h"
#include "llrefcount.h"
#include "llvector4a.h"

//-----------------------------------------------------------------------------
// class LLJointState
//-----------------------------------------------------------------------------
class LLJointState : public LLRefCount
{
public:
    enum BlendPhase
    {
        INACTIVE,
        EASE_IN,
        ACTIVE,
        EASE_OUT
    };
protected:
    // Transformation members, first so that they land on their own alignment
    // rather than behind the pointer and the two words. Held in the vector
    // forms because that is what reads and writes them: one of these per joint
    // per playing motion is blended every frame, and holding three and four
    // floats meant loading and storing across that boundary every time.
    LLVector4a      mPosition;  // position relative to parent joint
    LLQuaternion2   mRotation;  // joint rotation relative to parent joint
    LLVector4a      mScale;     // scale relative to rotated frame

    // associated joint
    LLJoint *mJoint;

    // indicates which members are used
    U32     mUsage;

    // indicates weighted effect of this joint
    F32     mWeight;

    LLJoint::JointPriority  mPriority;  // how important this joint state is relative to others

    void initTransforms()
    {
        // What the scalar members defaulted to: no offset, no rotation, and a
        // scale of zero, which is only ever read by a state that carries SCALE
        // and has therefore been written.
        mPosition.clear();
        mRotation = LLQuaternion2::identity();
        mScale.clear();
    }

public:
    // Constructor
    LLJointState()
        : mJoint(NULL)
        , mUsage(0)
        , mWeight(0.f)
        , mPriority(LLJoint::USE_MOTION_PRIORITY)
    {
        initTransforms();
    }

    LLJointState(LLJoint* joint)
        : mJoint(joint)
        , mUsage(0)
        , mWeight(0.f)
        , mPriority(LLJoint::USE_MOTION_PRIORITY)
    {
        initTransforms();
    }

    // joint that this state is applied to
    LLJoint* getJoint()             { return mJoint; }
    const LLJoint* getJoint() const { return mJoint; }
    bool setJoint( LLJoint *joint ) { mJoint = joint; return mJoint != NULL; }

    // transform type (bitwise flags can be combined)
    // Note that these are set automatically when various
    // member setPos/setRot/setScale functions are called.
    enum Usage
    {
        POS     = 1,
        ROT     = 2,
        SCALE   = 4,
    };
    U32 getUsage() const            { return mUsage; }
    void setUsage( U32 usage )      { mUsage = usage; }
    F32 getWeight() const           { return mWeight; }
    void setWeight( F32 weight )    { mWeight = weight; }

    // get/set position, rotation and scale in the form they are held in, which
    // is what the blender and the keyframe sampler want
    const LLVector4a& getPositionV() const          { return mPosition; }
    void setPosition( const LLVector4a& pos )       { llassert(mUsage & POS); mPosition = pos; }

    const LLQuaternion2& getRotationQ() const       { return mRotation; }
    void setRotation( const LLQuaternion2& rot )    { llassert(mUsage & ROT); mRotation = rot; }

    const LLVector4a& getScaleV() const             { return mScale; }
    void setScale( const LLVector4a& scale )        { llassert(mUsage & SCALE); mScale = scale; }

    // and in the scalar form the motions are written against. These return by
    // value: there is no LLVector3 in here to hand back a reference to.
    LLVector3 getPosition() const               { return LLVector3(mPosition.getF32ptr()); }
    void setPosition( const LLVector3& pos )    { llassert(mUsage & POS); mPosition.load3(pos.mV); }

    LLQuaternion getRotation() const            { LLQuaternion rot; mRotation.store(rot); return rot; }
    void setRotation( const LLQuaternion& rot ) { llassert(mUsage & ROT); mRotation = rot; }

    LLVector3 getScale() const                  { return LLVector3(mScale.getF32ptr()); }
    void setScale( const LLVector3& scale )     { llassert(mUsage & SCALE); mScale.load3(scale.mV); }

    // get/set priority
    LLJoint::JointPriority getPriority() const  { return mPriority; }
    void setPriority( LLJoint::JointPriority priority ) { mPriority = priority; }

protected:
    // Destructor
    virtual ~LLJointState()
    {
    }

};

#endif // LL_LLJOINTSTATE_H

