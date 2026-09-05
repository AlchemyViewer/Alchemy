/**
 * @file llpose.cpp
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

//-----------------------------------------------------------------------------
// Header Files
//-----------------------------------------------------------------------------
#include "linden_common.h"

#include "llpose.h"

#include "llmotion.h"
#include "llmath.h"
#include "llstl.h"

//-----------------------------------------------------------------------------
// Static
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// LLPose
//-----------------------------------------------------------------------------
LLPose::~LLPose()
{
}

//-----------------------------------------------------------------------------
// addJointState()
//-----------------------------------------------------------------------------
bool LLPose::addJointState(const LLPointer<LLJointState>& jointState)
{
    // One state per joint, first one in wins. The physics motion relies on
    // this: its six sub-motions share two joints, and each blender slot
    // they would otherwise take is one the animation cannot have.
    if (!findJointState(jointState->getJoint()))
    {
        // Every state in a pose carries the pose's weight -- setWeight can
        // then skip the walk when nothing changed -- so one that arrives
        // late is brought up to it rather than sitting at zero until the
        // next change.
        jointState->setWeight(mWeight);
        mJointStates.push_back(jointState);
    }
    return true;
}

//-----------------------------------------------------------------------------
// removeJointState()
//-----------------------------------------------------------------------------
bool LLPose::removeJointState(const LLPointer<LLJointState>& jointState)
{
    LLJoint* joint = jointState->getJoint();
    for (joint_state_list_t::iterator iter = mJointStates.begin(); iter != mJointStates.end(); ++iter)
    {
        if ((*iter)->getJoint() == joint)
        {
            mJointStates.erase(iter);
            break;
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
// removeAllJointStates()
//-----------------------------------------------------------------------------
bool LLPose::removeAllJointStates()
{
    mJointStates.clear();
    return true;
}

//-----------------------------------------------------------------------------
// findJointState()
//-----------------------------------------------------------------------------
LLJointState* LLPose::findJointState(LLJoint *joint)
{
    for (const LLPointer<LLJointState>& state : mJointStates)
    {
        if (state->getJoint() == joint)
        {
            return state;
        }
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// findJointState()
//-----------------------------------------------------------------------------
LLJointState* LLPose::findJointState(std::string_view name)
{
    for (const LLPointer<LLJointState>& state : mJointStates)
    {
        if (state->getJoint()->getName() == name)
        {
            return state;
        }
    }
    return NULL;
}

//-----------------------------------------------------------------------------
// setWeight()
//-----------------------------------------------------------------------------
void LLPose::setWeight(F32 weight)
{
    // Every state already carries mWeight, so an unchanged weight has
    // nothing to write. The controller sets this once per motion per frame,
    // and for a motion that is simply playing it is the same 1.0 each time.
    if (weight == mWeight)
    {
        return;
    }
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    for (const LLPointer<LLJointState>& state : mJointStates)
    {
        state->setWeight(weight);
    }
    mWeight = weight;
}

//-----------------------------------------------------------------------------
// getWeight()
//-----------------------------------------------------------------------------
F32 LLPose::getWeight() const
{
    return mWeight;
}

//-----------------------------------------------------------------------------
// getNumJointStates()
//-----------------------------------------------------------------------------
S32 LLPose::getNumJointStates() const
{
    return (S32)mJointStates.size();
}

//-----------------------------------------------------------------------------
// LLJointStateBlender
//-----------------------------------------------------------------------------

LLJointStateBlender::LLJointStateBlender()
    : mCachedScale(1.f, 1.f, 1.f),
      mQueued(false)
{
    for(S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
    {
        mJointStates[i] = NULL;
        mPriorities[i] = S32_MIN;
        mAdditiveBlends[i] = false;
    }
}

LLJointStateBlender::~LLJointStateBlender()
{

}

//-----------------------------------------------------------------------------
// addJointState()
//-----------------------------------------------------------------------------
bool LLJointStateBlender::addJointState(const LLPointer<LLJointState>& joint_state, S32 priority, bool additive_blend)
{
    llassert(joint_state);

    if (!joint_state->getJoint())
        // this joint state doesn't point to an actual joint, so we don't care about applying it
        return false;

    for(S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
    {
        if (mJointStates[i].isNull())
        {
            mJointStates[i] = joint_state;
            mPriorities[i] = priority;
            mAdditiveBlends[i] = additive_blend;
            return true;
        }
        else if (priority > mPriorities[i])
        {
            // we're at a higher priority than the current joint state in this slot
            // so shift everyone over
            // previous joint states (newer motions) with same priority should stay in place
            for (S32 j = JSB_NUM_JOINT_STATES - 1; j > i; j--)
            {
                mJointStates[j] = mJointStates[j - 1];
                mPriorities[j] = mPriorities[j - 1];
                mAdditiveBlends[j] = mAdditiveBlends[j - 1];
            }
            // now store ourselves in this slot
            mJointStates[i] = joint_state;
            mPriorities[i] = priority;
            mAdditiveBlends[i] = additive_blend;
            return true;
        }
    }

    return false;
}

//-----------------------------------------------------------------------------
// blendJointStates()
//-----------------------------------------------------------------------------
void LLJointStateBlender::blendJointStates(bool apply_now)
{
    // we need at least one joint to blend
    // if there is one, it will be in slot zero according to insertion logic
    // instead of resetting joint state to default, just leave it unchanged from last frame
    if (mJointStates[0].isNull())
    {
        return;
    }

    LLJoint* target_joint = mJointStates[0]->getJoint();

    const S32 POS_WEIGHT = 0;
    const S32 ROT_WEIGHT = 1;
    const S32 SCALE_WEIGHT = 2;

    F32             sum_weights[3];
    U32             sum_usage = 0;

    // Every channel below is seeded from the joint, so a channel no joint
    // state contributes to ends up holding the value it started with. Writing
    // that back is a no-op that still dirties the joint and, through touch(),
    // its entire subtree. Track which channels actually received a
    // contribution so the untouched ones can be left alone.
    U32             contributed_usage = 0;

    // Seeded from wherever the result is going: the joint, or the cache the
    // coarse clock is interpolating the joint toward.
    LLVector3       blended_pos = apply_now ? target_joint->getPosition() : mCachedPosition;
    LLQuaternion    blended_rot = apply_now ? target_joint->getRotation() : mCachedRotation;
    LLVector3       blended_scale = apply_now ? target_joint->getScale() : mCachedScale;

    LLVector3       added_pos;
    LLQuaternion    added_rot;
    LLVector3       added_scale;

    //S32               joint_state_index;

    sum_weights[POS_WEIGHT] = 0.f;
    sum_weights[ROT_WEIGHT] = 0.f;
    sum_weights[SCALE_WEIGHT] = 0.f;

    for(S32 joint_state_index = 0;
        joint_state_index < JSB_NUM_JOINT_STATES && mJointStates[joint_state_index].notNull();
        joint_state_index++)
    {
        LLJointState* jsp = mJointStates[joint_state_index];
        U32 current_usage = jsp->getUsage();
        F32 current_weight = jsp->getWeight();

        if (current_weight == 0.f)
        {
            continue;
        }

        contributed_usage |= current_usage;

        if (mAdditiveBlends[joint_state_index])
        {
            if(current_usage & LLJointState::POS)
            {
                F32 new_weight_sum = llmin(1.f, current_weight + sum_weights[POS_WEIGHT]);

                // add in pos for this jointstate modulated by weight
                added_pos += jsp->getPosition() * (new_weight_sum - sum_weights[POS_WEIGHT]);
            }

            if(current_usage & LLJointState::SCALE)
            {
                F32 new_weight_sum = llmin(1.f, current_weight + sum_weights[SCALE_WEIGHT]);

                // add in scale for this jointstate modulated by weight
                added_scale += jsp->getScale() * (new_weight_sum - sum_weights[SCALE_WEIGHT]);
            }

            if (current_usage & LLJointState::ROT)
            {
                F32 new_weight_sum = llmin(1.f, current_weight + sum_weights[ROT_WEIGHT]);

                // add in rotation for this jointstate modulated by weight
                added_rot = nlerp((new_weight_sum - sum_weights[ROT_WEIGHT]), added_rot, jsp->getRotation()) * added_rot;
            }
        }
        else
        {
            // blend two jointstates together

            // blend position
            if(current_usage & LLJointState::POS)
            {
                if(sum_usage & LLJointState::POS)
                {
                    F32 new_weight_sum = llmin(1.f, current_weight + sum_weights[POS_WEIGHT]);

                    // blend positions from both
                    blended_pos = lerp(jsp->getPosition(), blended_pos, sum_weights[POS_WEIGHT] / new_weight_sum);
                    sum_weights[POS_WEIGHT] = new_weight_sum;
                }
                else
                {
                    // copy position from current
                    blended_pos = jsp->getPosition();
                    sum_weights[POS_WEIGHT] = current_weight;
                }
            }

            // now do scale
            if(current_usage & LLJointState::SCALE)
            {
                if(sum_usage & LLJointState::SCALE)
                {
                    F32 new_weight_sum = llmin(1.f, current_weight + sum_weights[SCALE_WEIGHT]);

                    // blend scales from both
                    blended_scale = lerp(jsp->getScale(), blended_scale, sum_weights[SCALE_WEIGHT] / new_weight_sum);
                    sum_weights[SCALE_WEIGHT] = new_weight_sum;
                }
                else
                {
                    // copy scale from current
                    blended_scale = jsp->getScale();
                    sum_weights[SCALE_WEIGHT] = current_weight;
                }
            }

            // rotation
            if (current_usage & LLJointState::ROT)
            {
                if(sum_usage & LLJointState::ROT)
                {
                    F32 new_weight_sum = llmin(1.f, current_weight + sum_weights[ROT_WEIGHT]);

                    // blend rotations from both
                    blended_rot = nlerp(sum_weights[ROT_WEIGHT] / new_weight_sum, jsp->getRotation(), blended_rot);
                    sum_weights[ROT_WEIGHT] = new_weight_sum;
                }
                else
                {
                    // copy rotation from current
                    blended_rot = jsp->getRotation();
                    sum_weights[ROT_WEIGHT] = current_weight;
                }
            }

            // update resulting usage mask
            sum_usage = sum_usage | current_usage;
        }
    }

    if (!added_scale.isFinite())
    {
        added_scale.clearVec();
    }

    if (!blended_scale.isFinite())
    {
        blended_scale.setVec(1,1,1);
    }

    if (apply_now)
    {
        // apply transforms
        // SL-315
        target_joint->setPosition(blended_pos + added_pos);
        // blended_scale was seeded from the joint, which LLXform::setScale keeps
        // finite, and only a joint state carrying SCALE can move it off that
        // value. So the reset above is unreachable without a scale contribution,
        // and skipping the write here cannot strand a non-finite scale.
        if (contributed_usage & LLJointState::SCALE)
        {
            target_joint->setScale(blended_scale + added_scale);
        }
        target_joint->setRotation(added_rot * blended_rot);

        // now clear joint states
        for(S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
        {
            mJointStates[i] = NULL;
        }
    }
    else
    {
        // The cache is not a joint: nothing below it to dirty, so every
        // channel is simply stored.
        mCachedPosition = blended_pos + added_pos;
        mCachedScale = blended_scale + added_scale;
        mCachedRotation = added_rot * blended_rot;
    }
}

//-----------------------------------------------------------------------------
// interpolate()
//-----------------------------------------------------------------------------
void LLJointStateBlender::interpolate(F32 u)
{
    // only interpolate if we have a joint state
    if (!mJointStates[0])
    {
        return;
    }
    LLJoint* target_joint = mJointStates[0]->getJoint();

    if (!target_joint)
    {
        return;
    }

    // SL-315
    target_joint->setPosition(lerp(target_joint->getPosition(), mCachedPosition, u));
    target_joint->setScale(lerp(target_joint->getScale(), mCachedScale, u));
    target_joint->setRotation(nlerp(u, target_joint->getRotation(), mCachedRotation));
}

//-----------------------------------------------------------------------------
// clear()
//-----------------------------------------------------------------------------
void LLJointStateBlender::clear()
{
    // now clear joint states
    for(S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
    {
        mJointStates[i] = NULL;
    }
}

//-----------------------------------------------------------------------------
// resetCachedJoint()
//-----------------------------------------------------------------------------
void LLJointStateBlender::resetCachedJoint()
{
    if (!mJointStates[0])
    {
        return;
    }
    LLJoint* source_joint = mJointStates[0]->getJoint();
    // SL-315
    mCachedPosition = source_joint->getPosition();
    mCachedScale = source_joint->getScale();
    mCachedRotation = source_joint->getRotation();
}

//-----------------------------------------------------------------------------
// LLPoseBlender
//-----------------------------------------------------------------------------

LLPoseBlender::LLPoseBlender()
    : mJointStateBlenderPool{},
      mNextPoseSlot(0)
{
}

LLPoseBlender::~LLPoseBlender()
{
    for (LLJointStateBlender* blender : mJointStateBlenderPool)
    {
        delete blender;
    }
}

//-----------------------------------------------------------------------------
// addMotion()
//-----------------------------------------------------------------------------
bool LLPoseBlender::addMotion(LLMotion* motion)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    LLPose* pose = motion->getPose();

    // Neither changes across the motion's joint states, and both are
    // virtual calls.
    const S32 motion_priority = motion->getPriority();
    const bool additive_blend = (motion->getBlendType() == LLMotion::ADDITIVE_BLEND);

    for (const LLPointer<LLJointState>& state : pose->getJointStates())
    {
        LLJointState* jsp = state;
        LLJoint *jointp = jsp->getJoint();
        if (!jointp)
        {
            continue;
        }

        // A joint that never received a number is in the pose -- LLMotion
        // inserts before it checks the range -- but never made the joint
        // signature, so nothing scheduled an update for it. It has no slot
        // here either.
        const S32 joint_num = jointp->getJointNum();
        if (joint_num < 0 || joint_num >= (S32)LL_CHARACTER_MAX_ANIMATED_JOINTS)
        {
            continue;
        }

        LLJointStateBlender* joint_blender = mJointStateBlenderPool[joint_num];
        if (!joint_blender)
        {
            // this is the first time we are animating this joint
            // so create new jointblender and add it to our pool
            joint_blender = new LLJointStateBlender();
            mJointStateBlenderPool[joint_num] = joint_blender;
        }

        const S32 priority = (jsp->getPriority() == LLJoint::USE_MOTION_PRIORITY) ? motion_priority : jsp->getPriority();
        joint_blender->addJointState(jsp, priority, additive_blend);

        // add it to our list of active blenders
        if (!joint_blender->isQueued())
        {
            joint_blender->setQueued(true);
            mActiveBlenders.push_back(joint_blender);
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
// blendAndApply()
//-----------------------------------------------------------------------------
void LLPoseBlender::blendAndApply()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    LL_PROFILE_ZONE_NUM(mActiveBlenders.size());
    // Order does not matter: each blender owns one joint and reads only
    // that joint's local transform.
    for (LLJointStateBlender* jsbp : mActiveBlenders)
    {
        jsbp->blendJointStates();
        jsbp->setQueued(false);
    }

    // we're done now so there are no more active blenders for this frame
    mActiveBlenders.clear();
}

//-----------------------------------------------------------------------------
// blendAndCache()
//-----------------------------------------------------------------------------
void LLPoseBlender::blendAndCache(bool reset_cached_joints)
{
    for (LLJointStateBlender* jsbp : mActiveBlenders)
    {
        if (reset_cached_joints)
        {
            jsbp->resetCachedJoint();
        }
        jsbp->blendJointStates(false);
    }
}

//-----------------------------------------------------------------------------
// interpolate()
//-----------------------------------------------------------------------------
void LLPoseBlender::interpolate(F32 u)
{
    for (LLJointStateBlender* jsbp : mActiveBlenders)
    {
        jsbp->interpolate(u);
    }
}

//-----------------------------------------------------------------------------
// clearBlenders()
//-----------------------------------------------------------------------------
void LLPoseBlender::clearBlenders()
{
    for (LLJointStateBlender* jsbp : mActiveBlenders)
    {
        jsbp->clear();
        jsbp->setQueued(false);
    }

    mActiveBlenders.clear();
}

