/**
 * @file altestmotion.h
 * @brief LLMotion stub for llcharacter tests: settable descriptors, counted
 *        callbacks, and public access to addJointState.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#pragma once

#include "llcharacter.h"
#include "llmotion.h"
#include "lljointstate.h"
#include "lluuid.h"

class ALTestMotion final : public LLMotion
{
public:
    explicit ALTestMotion(const LLUUID& id = LLUUID::generateNewID(),
                          LLJoint::JointPriority priority = LLJoint::MEDIUM_PRIORITY,
                          LLMotionBlendType blend_type = NORMAL_BLEND)
        : LLMotion(id)
        , mPriority(priority)
        , mBlendType(blend_type)
    {
    }

    static LLMotion* create(const LLUUID& id) { return new ALTestMotion(id); }
    static LLMotion* createAdditive(const LLUUID& id) { return new ALTestMotion(id, LLJoint::MEDIUM_PRIORITY, ADDITIVE_BLEND); }

    bool getLoop() override { return mLoop; }
    F32 getDuration() override { return mDuration; }
    F32 getEaseInDuration() override { return mEaseInDuration; }
    F32 getEaseOutDuration() override { return mEaseOutDuration; }
    LLJoint::JointPriority getPriority() override { return mPriority; }
    LLMotionBlendType getBlendType() override { return mBlendType; }
    F32 getMinPixelArea() override { return mMinPixelArea; }

    LLMotionInitStatus onInitialize(LLCharacter* character) override
    {
        ++mInitializeCount;
        mCharacter = character;
        return mInitStatus;
    }

    bool onActivate() override
    {
        ++mActivateCount;
        return true;
    }

    bool onUpdate(F32 time, U8*) override
    {
        ++mUpdateCount;
        mUpdateSerial = ++sUpdateSerial;
        mLastUpdateTime = time;
        if (mStartOnUpdate.notNull() && mCharacter)
        {
            // once: the way an emote restarts the sit animation
            const LLUUID id = mStartOnUpdate;
            mStartOnUpdate.setNull();
            mCharacter->startMotion(id);
        }
        return mUpdateResult;
    }

    void onDeactivate() override { ++mDeactivateCount; }

    void setStopTime(F32 time) override
    {
        ++mStopTimeCalls;
        LLMotion::setStopTime(time);
    }
    F32 sendStopTimestamp() const { return mSendStopTimestamp; }

    using LLMotion::addJointState;

    // Creates a joint state on `joint` with `usage`, adds it to the motion, and
    // returns it so the test can set its transform.
    LLPointer<LLJointState> addJoint(LLJoint* joint, U32 usage,
                                     LLJoint::JointPriority priority = LLJoint::USE_MOTION_PRIORITY)
    {
        LLPointer<LLJointState> state = new LLJointState(joint);
        state->setUsage(usage);
        state->setPriority(priority);
        addJointState(state);
        return state;
    }

    LLJoint::JointPriority mPriority;
    LLMotionBlendType mBlendType;
    bool mLoop = true;
    F32 mDuration = 0.f;
    F32 mEaseInDuration = 0.f;
    F32 mEaseOutDuration = 0.f;
    F32 mMinPixelArea = 0.f;
    LLMotionInitStatus mInitStatus = STATUS_SUCCESS;
    bool mUpdateResult = true;

    S32 mInitializeCount = 0;
    S32 mActivateCount = 0;
    S32 mUpdateCount = 0;
    S32 mDeactivateCount = 0;
    S32 mStopTimeCalls = 0;
    LLCharacter* mCharacter = nullptr;
    LLUUID mStartOnUpdate;
    F32 mLastUpdateTime = 0.f;

    // Which onUpdate this was, across every test motion: the order the
    // controller visited them in.
    static inline S32 sUpdateSerial = 0;
    S32 mUpdateSerial = 0;
};
