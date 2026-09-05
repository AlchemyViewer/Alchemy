/**
 * @file llmotioncontroller.cpp
 * @brief Implementation of LLMotionController class.
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

#include "llmotioncontroller.h"
#include "llfasttimer.h"
#include "llkeyframemotion.h"
#include "llmath.h"
#include "lltimer.h"
#include "llanimationstates.h"
#include "llstl.h"

// This is why LL_CHARACTER_MAX_ANIMATED_JOINTS needs to be a multiple of 4.
const S32 NUM_JOINT_SIGNATURE_STRIDES = LL_CHARACTER_MAX_ANIMATED_JOINTS / 4;
const U32 MAX_MOTION_INSTANCES = 32;

//-----------------------------------------------------------------------------
// Constants and statics
//-----------------------------------------------------------------------------
F32 LLMotionController::sCurrentTimeFactor = 1.f;
LLMotionRegistry LLMotionController::sRegistry;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// LLMotionRegistry class
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// LLMotionRegistry()
// Class Constructor
//-----------------------------------------------------------------------------
LLMotionRegistry::LLMotionRegistry()
{

}


//-----------------------------------------------------------------------------
// ~LLMotionRegistry()
// Class Destructor
//-----------------------------------------------------------------------------
LLMotionRegistry::~LLMotionRegistry()
{
    mMotionTable.clear();
}


//-----------------------------------------------------------------------------
// addMotion()
//-----------------------------------------------------------------------------
bool LLMotionRegistry::registerMotion( const LLUUID& id, LLMotionConstructor constructor )
{
    //  LL_INFOS() << "Registering motion: " << name << LL_ENDL;
    if (!is_in_map(mMotionTable, id))
    {
        mMotionTable[id] = constructor;
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
// markBad()
//-----------------------------------------------------------------------------
void LLMotionRegistry::markBad( const LLUUID& id )
{
    mMotionTable[id] = LLMotionConstructor(NULL);
}

//-----------------------------------------------------------------------------
// createMotion()
//-----------------------------------------------------------------------------
LLMotion *LLMotionRegistry::createMotion( const LLUUID &id )
{
    LLMotionConstructor constructor = get_if_there(mMotionTable, id, LLMotionConstructor(NULL));
    LLMotion* motion = NULL;

    if ( constructor == NULL )
    {
        // *FIX: need to replace with a better default scheme. RN
        motion = LLKeyframeMotion::create(id);
    }
    else
    {
        motion = constructor(id);
    }

    return motion;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// LLMotionController class
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

namespace
{
    // The index to visit after the motion at index, which may have taken
    // itself off the list on the way past; then the next one is already in
    // its slot.
    size_t next_motion(const LLMotionController::motion_list_t& motions, size_t index, const LLMotion* visited)
    {
        return (index < motions.size() && motions[index] == visited) ? index + 1 : index;
    }
}

//-----------------------------------------------------------------------------
// LLMotionController()
// Class Constructor
//-----------------------------------------------------------------------------
LLMotionController::LLMotionController()
    : mTimeFactor(sCurrentTimeFactor),
      mCharacter(NULL),
      mContinuousTime(0.f),
      mAnimTime(0.f),
      mPrevTimerElapsed(0.f),
      mLastTime(0.0f),
      mHasRunOnce(false),
      mPaused(false),
      mPausedFrame(0),
      mTimeStep(0.f),
      mTimeStepCount(0),
      mLastInterp(0.f),
      mIsSelf(false),
      mLastCountAfterPurge(0)
{
}


//-----------------------------------------------------------------------------
// ~LLMotionController()
// Class Destructor
//-----------------------------------------------------------------------------
LLMotionController::~LLMotionController()
{
    deleteAllMotions();
}

void LLMotionController::incMotionCounts(S32& num_motions, S32& num_loading_motions, S32& num_loaded_motions, S32& num_active_motions, S32& num_deprecated_motions)
{
    num_motions += static_cast<S32>(mAllMotions.size());
    num_loading_motions += static_cast<S32>(mLoadingMotions.size());
    num_loaded_motions += static_cast<S32>(mLoadedMotions.size());
    num_active_motions += static_cast<S32>(getNumActiveMotions());
    num_deprecated_motions += static_cast<S32>(mDeprecatedMotions.size());
}

//-----------------------------------------------------------------------------
// deleteAllMotions()
//-----------------------------------------------------------------------------
void LLMotionController::deleteAllMotions()
{
    mLoadingMotions.clear();
    mLoadedMotions.clear();
    mActiveMotions[LLMotion::NORMAL_BLEND].clear();
    mActiveMotions[LLMotion::ADDITIVE_BLEND].clear();

    for_each(mAllMotions.begin(), mAllMotions.end(), DeletePairedPointer());
    mAllMotions.clear();

    // stinson 05/12/20014 : Ownership of the LLMotion pointers is transferred from
    // mAllMotions to mDeprecatedMotions in method
    // LLMotionController::deprecateMotionInstance().  Thus, we should also clean
    // up the mDeprecatedMotions list as well.
    for_each(mDeprecatedMotions.begin(), mDeprecatedMotions.end(), DeletePointer());
    mDeprecatedMotions.clear();
}

//-----------------------------------------------------------------------------
// purgeExcessMotion()
//-----------------------------------------------------------------------------
void LLMotionController::purgeExcessMotions()
{
    if (mLoadedMotions.size() > MAX_MOTION_INSTANCES)
    {
        // clean up deprecated motions
        for (motion_set_t::iterator deprecated_motion_it = mDeprecatedMotions.begin();
             deprecated_motion_it != mDeprecatedMotions.end(); )
        {
            motion_set_t::iterator cur_iter = deprecated_motion_it++;
            LLMotion* cur_motionp = *cur_iter;
            if (!isMotionActive(cur_motionp))
            {
                // Motion is deprecated so we know it's not cannonical,
                //  we can safely remove the instance
                removeMotionInstance(cur_motionp); // modifies mDeprecatedMotions
                mDeprecatedMotions.erase(cur_iter);
            }
        }
    }

    std::set<LLUUID> motions_to_kill;
    if (mLoadedMotions.size() > MAX_MOTION_INSTANCES)
    {
        // too many motions active this frame, kill all blenders
        mPoseBlender.clearBlenders();
        for (LLMotion* cur_motionp : mLoadedMotions)
        {
            // motion isn't playing, delete it
            if (!isMotionActive(cur_motionp))
            {
                motions_to_kill.insert(cur_motionp->getID());
            }
        }
    }

    // clean up all inactive, loaded motions
    for (const LLUUID& motion_id : motions_to_kill)
    {
        // look up the motion again by ID to get canonical instance
        // and kill it only if that one is inactive
        LLMotion* motionp = findMotion(motion_id);
        if (motionp && !isMotionActive(motionp))
        {
            removeMotion(motion_id);
        }
    }

    U32 loaded_count = static_cast<U32>(mLoadedMotions.size());
    if (loaded_count > (2 * MAX_MOTION_INSTANCES) && loaded_count > mLastCountAfterPurge)
    {
        LL_WARNS_ONCE("Animation") << loaded_count << " Loaded Motions. Amount of motions is over limit." << LL_ENDL;
    }
    mLastCountAfterPurge = loaded_count;
}

//-----------------------------------------------------------------------------
// deactivateStoppedMotions()
//-----------------------------------------------------------------------------
void LLMotionController::deactivateStoppedMotions()
{
    // Since we're hidden, deactivate any stopped motions.
    for (motion_list_t& motions : mActiveMotions)
    {
        LLMotion* motionp = nullptr;
        for (size_t i = 0; i < motions.size(); i = next_motion(motions, i, motionp))
        {
            motionp = motions[i];
            if (motionp->isStopped())
            {
                deactivateMotionInstance(motionp);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// computeQuantumStep()
//-----------------------------------------------------------------------------
LLMotionController::QuantumStep LLMotionController::computeQuantumStep(F32 continuous_time, F32 time_step, S32 last_count)
{
    // One quantum ahead of real time, on purpose: the pose computed for the
    // boundary this lands on is what the frames until then interpolate
    // toward, so by the time real time reaches it the pose has arrived.
    // That is the +1. It was never the defect -- the defect was the caller
    // writing the snapped time back into its accumulator, which turned the
    // lookahead into a full quantum of advance every frame.
    const F32 quanta = continuous_time / time_step;
    const F32 whole = llmax(0.f, floorf(quanta));

    QuantumStep step;
    step.count = (S32)whole + 1;
    step.interp = llclamp(quanta - whole, 0.f, 1.f);
    step.advanced = (step.count != last_count);
    return step;
}

//-----------------------------------------------------------------------------
// quantumInterpolant()
//-----------------------------------------------------------------------------
F32 LLMotionController::quantumInterpolant(F32 interp, F32 last_interp)
{
    // The blender lerps from wherever the pose is now toward the quantum's
    // target, so the fraction it needs is of the distance still to go, not
    // of the whole quantum. Handed interp - last_interp instead, it closed
    // on the target geometrically -- (1-du)^n of the way still to go after
    // n frames -- and the boundary snapped the remainder.
    if (last_interp >= 1.f)
    {
        return 1.f;
    }
    return llclamp((interp - last_interp) / (1.f - last_interp), 0.f, 1.f);
}

//-----------------------------------------------------------------------------
// setTimeStep()
//-----------------------------------------------------------------------------
void LLMotionController::setTimeStep(F32 step)
{
    if (step == mTimeStep)
    {
        return;
    }

    // The quantum count is in units of the old step, and the blender is
    // part way toward a target computed on the old grid. Finish that move,
    // drop the cache and let the next update start the new grid from
    // scratch, whichever direction the change is.
    mPoseBlender.interpolate(1.f);
    clearBlenders();
    mTimeStepCount = 0;
    mLastInterp = 0.f;

    const bool entering = (mTimeStep == 0.f);
    mTimeStep = step;

    if (step != 0.f && entering)
    {
        // make sure timestamps conform to new quantum -- once, on the way
        // in. Doing it on every change walked them backwards a fraction of
        // a step each time the quantum moved a rung.
        for (motion_list_t& motions : mActiveMotions)
        {
            for (LLMotion* motionp : motions)
            {
                motionp->mActivationTimestamp = (F32)llfloor(motionp->mActivationTimestamp / step) * step;
                // setStopTime stops a motion, and the keyframe motion's
                // override aligns whatever time it is handed to the loop, so
                // a running motion is left alone rather than given a stop
                // time it never had. A motion that never stops itself has no
                // send-stop time to snap either.
                if (motionp->isStopped())
                {
                    motionp->mStopTimestamp = (F32)llfloor(motionp->mStopTimestamp / step) * step;
                }
                if (motionp->mSendStopTimestamp != F32_MAX)
                {
                    motionp->mSendStopTimestamp = (F32)llfloor(motionp->mSendStopTimestamp / step) * step;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// quantizeTimeStep()
//-----------------------------------------------------------------------------
F32 LLMotionController::quantizeTimeStep(F32 requested_step)
{
    // The request is a smooth function of screen size and crowd size, so
    // left alone it changes a little every frame, and every change costs a
    // pose recompute and a cache. Sixteenths are exact in binary, which
    // keeps the clock's accumulation exact at every rung.
    constexpr F32 RUNG = 1.f / 16.f;
    constexpr F32 MAX_STEP = 0.25f;
    const F32 capped = llmin(requested_step, MAX_STEP);
    return llmax(0.f, floorf(capped / RUNG)) * RUNG;
}

//-----------------------------------------------------------------------------
// setTimeFactor()
//-----------------------------------------------------------------------------
void LLMotionController::setTimeFactor(F32 time_factor)
{
    mTimeFactor = time_factor;
}

//-----------------------------------------------------------------------------
// setCharacter()
//-----------------------------------------------------------------------------
void LLMotionController::setCharacter(LLCharacter *character)
{
    mCharacter = character;
}


//-----------------------------------------------------------------------------
// registerMotion()
//-----------------------------------------------------------------------------
bool LLMotionController::registerMotion( const LLUUID& id, LLMotionConstructor constructor )
{
    return sRegistry.registerMotion(id, constructor);
}

//-----------------------------------------------------------------------------
// removeMotion()
//-----------------------------------------------------------------------------
void LLMotionController::removeMotion( const LLUUID& id)
{
    LLMotion* motionp = findMotion(id);
    mAllMotions.erase(id);
    removeMotionInstance(motionp);
}

//-----------------------------------------------------------------------------
// removeActiveMotion()
//-----------------------------------------------------------------------------
void LLMotionController::removeActiveMotion(LLMotion* motion)
{
    std::erase(mActiveMotions[motion->getBlendType()], motion);
}

// removes instance of a motion from all runtime structures, but does
// not erase entry by ID, as this could be a duplicate instance
// use removeMotion(id) to remove all references to a given motion by id.
void LLMotionController::removeMotionInstance(LLMotion* motionp)
{
    if (motionp)
    {
        llassert(findMotion(motionp->getID()) != motionp);
        if (motionp->isActive())
            motionp->deactivate();
        mLoadingMotions.erase(motionp);
        mLoadedMotions.erase(motionp);
        removeActiveMotion(motionp);
        delete motionp;
    }
}

//-----------------------------------------------------------------------------
// purgeMotionInstances()
//-----------------------------------------------------------------------------
void LLMotionController::purgeMotionInstances(const LLUUID& id)
{
    removeMotion(id);
    for (motion_set_t::iterator iter = mDeprecatedMotions.begin();
         iter != mDeprecatedMotions.end(); )
    {
        motion_set_t::iterator cur_iter = iter++;
        LLMotion* motionp = *cur_iter;
        if (motionp->getID() == id)
        {
            // The same complete excision deactivateMotionInstance() applies to
            // deprecated motions; removeMotionInstance() deactivates if needed.
            removeMotionInstance(motionp); // does not touch mDeprecatedMotions
            mDeprecatedMotions.erase(cur_iter);
        }
    }
}

//-----------------------------------------------------------------------------
// createMotion()
//-----------------------------------------------------------------------------
LLMotion* LLMotionController::createMotion( const LLUUID &id )
{
    // do we have an instance of this motion for this character?
    LLMotion *motion = findMotion(id);

    // if not, we need to create one
    if (!motion)
    {
        // look up constructor and create it
        motion = sRegistry.createMotion(id);
        if (!motion)
        {
            return NULL;
        }

        // look up name for default motions
        const char* motion_name = gAnimLibrary.animStateToString(id);
        if (motion_name)
        {
            motion->setName(motion_name);
        }

        // initialize the new instance
        LLMotion::LLMotionInitStatus stat = motion->onInitialize(mCharacter);
        switch(stat)
        {
        case LLMotion::STATUS_FAILURE:
            LL_INFOS() << "Motion " << id << " init failed." << LL_ENDL;
            sRegistry.markBad(id);
            delete motion;
            return NULL;
        case LLMotion::STATUS_HOLD:
            mLoadingMotions.insert(motion);
            break;
        case LLMotion::STATUS_SUCCESS:
            // add motion to our list
            mLoadedMotions.insert(motion);
            break;
        default:
            LL_ERRS() << "Invalid initialization status" << LL_ENDL;
            break;
        }

        mAllMotions[id] = motion;
    }
    return motion;
}

//-----------------------------------------------------------------------------
// startMotion()
//-----------------------------------------------------------------------------
bool LLMotionController::startMotion(const LLUUID &id, F32 start_offset)
{
    // do we have an instance of this motion for this character?
    LLMotion *motion = findMotion(id);

    // motion that is stopping will be allowed to stop but
    // replaced by a new instance of that motion
    if (motion
        && !mPaused
        && motion->canDeprecate()
        && motion->getFadeWeight() > 0.01f // not LOD-ed out
        && (motion->isBlending() || motion->getStopTime() != 0.f))
    {
        deprecateMotionInstance(motion);
        // force creation of new instance
        motion = NULL;
    }

    // create new motion instance
    if (!motion)
    {
        motion = createMotion(id);
    }

    if (!motion)
    {
        return false;
    }
    //if the motion is already active and allows deprecation, then let it keep playing
    else if (motion->canDeprecate() && isMotionActive(motion))
    {
        return true;
    }

//  LL_INFOS() << "Starting motion " << name << LL_ENDL;
    return activateMotionInstance(motion, mAnimTime - start_offset);
}


//-----------------------------------------------------------------------------
// stopMotionLocally()
//-----------------------------------------------------------------------------
bool LLMotionController::stopMotionLocally(const LLUUID &id, bool stop_immediate)
{
    // if already inactive, return false
    LLMotion *motion = findMotion(id);
    // SL-1290: always stop immediate if paused
    return stopMotionInstance(motion, stop_immediate||mPaused);
}

bool LLMotionController::stopMotionInstance(LLMotion* motion, bool stop_immediate)
{
    if (!motion)
    {
        return false;
    }


    // If on active list, stop it
    if (isMotionActive(motion) && !motion->isStopped())
    {
        motion->setStopTime(mAnimTime);
        if (stop_immediate)
        {
            deactivateMotionInstance(motion);
        }
        return true;
    }
    else if (isMotionLoading(motion))
    {
        motion->setStopped(true);
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
// updateRegularMotions()
//-----------------------------------------------------------------------------
void LLMotionController::updateRegularMotions()
{
    updateMotionsByType(LLMotion::NORMAL_BLEND);
}

//-----------------------------------------------------------------------------
// updateAdditiveMotions()
//-----------------------------------------------------------------------------
void LLMotionController::updateAdditiveMotions()
{
    updateMotionsByType(LLMotion::ADDITIVE_BLEND);
}

//-----------------------------------------------------------------------------
// resetJointSignatures()
//-----------------------------------------------------------------------------
void LLMotionController::resetJointSignatures()
{
    memset(&mJointSignature[0][0], 0, sizeof(U8) * LL_CHARACTER_MAX_ANIMATED_JOINTS);
    memset(&mJointSignature[1][0], 0, sizeof(U8) * LL_CHARACTER_MAX_ANIMATED_JOINTS);
}

//-----------------------------------------------------------------------------
// updateIdleMotion()
// minimal updates for active motions
//-----------------------------------------------------------------------------
void LLMotionController::updateIdleMotion(LLMotion* motionp)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    if (motionp->isStopped() && mAnimTime > motionp->getStopTime() + motionp->getEaseOutDuration())
    {
        deactivateMotionInstance(motionp);
    }
    else if (motionp->isStopped() && mAnimTime > motionp->getStopTime())
    {
        // is this the first iteration in the ease out phase?
        if (mLastTime <= motionp->getStopTime())
        {
            // store residual weight for this motion
            motionp->mResidualWeight = motionp->getPose()->getWeight();
        }
    }
    else if (mAnimTime > motionp->mSendStopTimestamp)
    {
        // notify character of timed stop event on first iteration past sendstoptimestamp
        // this will only be called when an animation stops itself (runs out of time)
        if (mLastTime <= motionp->mSendStopTimestamp)
        {
            mCharacter->requestStopMotion( motionp );
            stopMotionInstance(motionp, false);
        }
    }
    else if (mAnimTime >= motionp->mActivationTimestamp)
    {
        if (mLastTime < motionp->mActivationTimestamp)
        {
            motionp->mResidualWeight = motionp->getPose()->getWeight();
        }
    }
}

//-----------------------------------------------------------------------------
// updateIdleActiveMotions()
// Call this instead of updateMotionsByType for hidden avatars
//-----------------------------------------------------------------------------
void LLMotionController::updateIdleActiveMotions()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    for (motion_list_t& motions : mActiveMotions)
    {
        LLMotion* motionp = nullptr;
        for (size_t i = 0; i < motions.size(); i = next_motion(motions, i, motionp))
        {
            motionp = motions[i];
            updateIdleMotion(motionp);
        }
    }
}

//-----------------------------------------------------------------------------
// updateMotionsByType()
//-----------------------------------------------------------------------------
void LLMotionController::updateMotionsByType(LLMotion::LLMotionBlendType anim_type)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    motion_list_t& motions = mActiveMotions[anim_type];
    LL_PROFILE_ZONE_NUM(motions.size());
    bool update_result = true;
    U8 last_joint_signature[LL_CHARACTER_MAX_ANIMATED_JOINTS];
    S32 motions_blended = 0;
    S32 joint_states_blended = 0;

    memset(&last_joint_signature, 0, sizeof(U8) * LL_CHARACTER_MAX_ANIMATED_JOINTS);

    // the same for every motion this frame, and a virtual call
    const F32 pixel_area = mCharacter->getPixelArea();

    // newest motion first; a motion may take itself off the list on the way past
    LLMotion* motionp = nullptr;
    for (size_t i = 0; i < motions.size(); i = next_motion(motions, i, motionp))
    {
        motionp = motions[i];

        bool update_motion = false;

        if (motionp->getPose()->getWeight() < 1.f)
        {
            update_motion = true;
        }
        else
        {
            for (S32 i = 0; i < NUM_JOINT_SIGNATURE_STRIDES; i++)
            {
                // The joint signatures live in U8[] arrays; treating four-byte
                // strides as U32 directly is strict-aliasing UB. memcpy in/out
                // of U32 locals lets GCC fold each direction to a single load
                // or store while staying inside the rules.
                const S32 offset = i * 4;

                // Pass 0: merge motionp's pass-0 signature into our pass-0
                // signature; if any new bits land, mark for update.
                U32 cur0;
                std::memcpy(&cur0, &mJointSignature[0][offset], sizeof(U32));
                U32 test0;
                std::memcpy(&test0, &motionp->mJointSignature[0][offset], sizeof(U32));
                if ((cur0 | test0) > cur0)
                {
                    cur0 |= test0;
                    std::memcpy(&mJointSignature[0][offset], &cur0, sizeof(U32));
                    update_motion = true;
                }

                // Snapshot pass 1 of our previous signature into
                // last_joint_signature, then do the same merge as pass 0.
                U32 last1;
                std::memcpy(&last1, &mJointSignature[1][offset], sizeof(U32));
                std::memcpy(&last_joint_signature[offset], &last1, sizeof(U32));

                U32 cur1 = last1;
                U32 test1;
                std::memcpy(&test1, &motionp->mJointSignature[1][offset], sizeof(U32));
                if ((cur1 | test1) > cur1)
                {
                    cur1 |= test1;
                    std::memcpy(&mJointSignature[1][offset], &cur1, sizeof(U32));
                    update_motion = true;
                }
            }
        }

        if (!update_motion)
        {
            updateIdleMotion(motionp);
            continue;
        }

        LLPose *posep = motionp->getPose();

        // only filter by LOD after running every animation at least once (to prime the avatar state)
        if (mHasRunOnce && motionp->getMinPixelArea() > pixel_area)
        {
            motionp->fadeOut();

            //should we notify the simulator that this motion should be stopped (check even if skipped by LOD logic)
            if (mAnimTime > motionp->mSendStopTimestamp)
            {
                // notify character of timed stop event on first iteration past sendstoptimestamp
                // this will only be called when an animation stops itself (runs out of time)
                if (mLastTime <= motionp->mSendStopTimestamp)
                {
                    mCharacter->requestStopMotion( motionp );
                    stopMotionInstance(motionp, false);
                }
            }

            if (motionp->getFadeWeight() < 0.01f)
            {
                if (motionp->isStopped() && mAnimTime > motionp->getStopTime() + motionp->getEaseOutDuration())
                {
                    posep->setWeight(0.f);
                    deactivateMotionInstance(motionp);
                }
                continue;
            }
        }
        else
        {
            motionp->fadeIn();
        }

        //**********************
        // MOTION INACTIVE
        //**********************
        if (motionp->isStopped() && mAnimTime > motionp->getStopTime() + motionp->getEaseOutDuration())
        {
            // this motion has gone on too long, deactivate it
            // did we have a chance to stop it?
            if (mLastTime <= motionp->getStopTime())
            {
                // if not, let's stop it this time through and deactivate it the next

                posep->setWeight(motionp->getFadeWeight());
                motionp->onUpdate(motionp->getStopTime() - motionp->mActivationTimestamp, last_joint_signature);
            }
            else
            {
                posep->setWeight(0.f);
                deactivateMotionInstance(motionp);
                continue;
            }
        }

        //**********************
        // MOTION EASE OUT
        //**********************
        else if (motionp->isStopped() && mAnimTime > motionp->getStopTime())
        {
            // is this the first iteration in the ease out phase?
            if (mLastTime <= motionp->getStopTime())
            {
                // store residual weight for this motion
                motionp->mResidualWeight = motionp->getPose()->getWeight();
            }

            if (motionp->getEaseOutDuration() == 0.f)
            {
                posep->setWeight(0.f);
            }
            else
            {
                posep->setWeight(motionp->getFadeWeight() * motionp->mResidualWeight * cubic_step(1.f - ((mAnimTime - motionp->getStopTime()) / motionp->getEaseOutDuration())));
            }

            // perform motion update
            update_result = motionp->onUpdate(mAnimTime - motionp->mActivationTimestamp, last_joint_signature);
        }

        //**********************
        // MOTION ACTIVE
        //**********************
        else if (mAnimTime > motionp->mActivationTimestamp + motionp->getEaseInDuration())
        {
            posep->setWeight(motionp->getFadeWeight());

            //should we notify the simulator that this motion should be stopped?
            if (mAnimTime > motionp->mSendStopTimestamp)
            {
                // notify character of timed stop event on first iteration past sendstoptimestamp
                // this will only be called when an animation stops itself (runs out of time)
                if (mLastTime <= motionp->mSendStopTimestamp)
                {
                    mCharacter->requestStopMotion( motionp );
                    stopMotionInstance(motionp, false);
                }
            }

            // perform motion update
            {
                update_result = motionp->onUpdate(mAnimTime - motionp->mActivationTimestamp, last_joint_signature);
            }
        }

        //**********************
        // MOTION EASE IN
        //**********************
        else if (mAnimTime >= motionp->mActivationTimestamp)
        {
            if (mLastTime < motionp->mActivationTimestamp)
            {
                motionp->mResidualWeight = motionp->getPose()->getWeight();
            }
            if (motionp->getEaseInDuration() == 0.f)
            {
                posep->setWeight(motionp->getFadeWeight());
            }
            else
            {
                // perform motion update
                posep->setWeight(motionp->getFadeWeight() * motionp->mResidualWeight + (1.f - motionp->mResidualWeight) * cubic_step((mAnimTime - motionp->mActivationTimestamp) / motionp->getEaseInDuration()));
            }
            // perform motion update
            update_result = motionp->onUpdate(mAnimTime - motionp->mActivationTimestamp, last_joint_signature);
        }
        else
        {
            posep->setWeight(0.f);
            update_result = motionp->onUpdate(0.f, last_joint_signature);
        }

        // allow motions to deactivate themselves
        if (!update_result)
        {
            if (!motionp->isStopped() || motionp->getStopTime() > mAnimTime)
            {
                // animation has stopped itself due to internal logic
                // propagate this to the network
                // as not all viewers are guaranteed to have access to the same logic
                mCharacter->requestStopMotion( motionp );
                stopMotionInstance(motionp, false);
            }

        }

        // even if onupdate returns false, add this motion in to the blend one last time
        mPoseBlender.addMotion(motionp);
        ++motions_blended;
        joint_states_blended += posep->getNumJointStates();
    }
    LL_PROFILE_ZONE_NUM(motions_blended);
    LL_PROFILE_ZONE_NUM(joint_states_blended);
}

//-----------------------------------------------------------------------------
// updateLoadingMotions()
//-----------------------------------------------------------------------------
void LLMotionController::updateLoadingMotions()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    // query pending motions for completion
    for (motion_set_t::iterator iter = mLoadingMotions.begin();
         iter != mLoadingMotions.end(); )
    {
        motion_set_t::iterator curiter = iter++;
        LLMotion* motionp = *curiter;
        if( !motionp)
        {
            continue; // maybe shouldn't happen but i've seen it -MG
        }
        LLMotion::LLMotionInitStatus status = motionp->onInitialize(mCharacter);
        if (status == LLMotion::STATUS_SUCCESS)
        {
            mLoadingMotions.erase(curiter);
            // add motion to our loaded motion list
            mLoadedMotions.insert(motionp);
            // this motion should be playing
            if (!motionp->isStopped())
            {
                activateMotionInstance(motionp, mAnimTime);
            }
        }
        else if (status == LLMotion::STATUS_FAILURE)
        {
            LL_INFOS() << "Motion " << motionp->getID() << " init failed." << LL_ENDL;
            sRegistry.markBad(motionp->getID());
            mLoadingMotions.erase(curiter);
            motion_set_t::iterator found_it = mDeprecatedMotions.find(motionp);
            if (found_it != mDeprecatedMotions.end())
            {
                mDeprecatedMotions.erase(found_it);
            }
            mAllMotions.erase(motionp->getID());
            delete motionp;
        }
    }
}

//-----------------------------------------------------------------------------
// call updateMotion() or updateMotionsMinimal() every frame
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// updateMotion()
//-----------------------------------------------------------------------------
void LLMotionController::updateMotions(bool force_update)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    // SL-763: "Distant animated objects run at super fast speed"
    // The use_quantum optimization or possibly the associated code in setTimeStamp()
    // does not work as implemented.
    // Currently setting mTimeStep to nonzero is disabled elsewhere.
    bool use_quantum = (mTimeStep != 0.f);

    // Always update mPrevTimerElapsed
    F32 cur_time = mTimer.getElapsedTimeF32();
    F32 delta_time = cur_time - mPrevTimerElapsed;
    mPrevTimerElapsed = cur_time;
    mLastTime = mAnimTime;

    // Always cap the number of loaded motions
    purgeExcessMotions();

    // Update timing info for this time step.
    if (!mPaused)
    {
        // The continuous clock is the only accumulator, and it moves on
        // every frame, including the ones that leave below. The quantized
        // clock is derived from it and never fed back: written back, its
        // one-quantum lookahead became a full quantum of advance every frame
        // whatever the frame time, which is SL-763.
        mContinuousTime += delta_time * mTimeFactor;
        if (use_quantum)
        {
            const QuantumStep step = computeQuantumStep(mContinuousTime, mTimeStep, mTimeStepCount);
            if (!step.advanced)
            {
                // still in the same quantum: move the pose toward its target
                mPoseBlender.interpolate(quantumInterpolant(step.interp, mLastInterp));
                mLastInterp = step.interp;

                updateLoadingMotions();

                return;
            }

            // is calculating a new keyframe pose, make sure the last one gets applied
            mPoseBlender.interpolate(1.f);
            clearBlenders();

            mTimeStepCount = step.count;
            mAnimTime = (F32)step.count * mTimeStep;
            mLastInterp = 0.f;
        }
        else
        {
            mAnimTime = mContinuousTime;
        }
    }

    updateLoadingMotions();

    resetJointSignatures();

    if (mPaused && !force_update)
    {
        updateIdleActiveMotions();
    }
    else
    {
        // update additive motions
        updateAdditiveMotions();

        resetJointSignatures();

        // update all regular motions
        updateRegularMotions();

        if (use_quantum)
        {
            mPoseBlender.blendAndCache(true);
        }
        else
        {
            mPoseBlender.blendAndApply();
        }
    }

    mHasRunOnce = true;
//  LL_INFOS() << "Motion controller time " << motionTimer.getElapsedTimeF32() << LL_ENDL;
}

//-----------------------------------------------------------------------------
// updateMotionsMinimal()
// minimal update (e.g. while hidden)
//-----------------------------------------------------------------------------
void LLMotionController::updateMotionsMinimal()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    // Always update mPrevTimerElapsed
    mPrevTimerElapsed = mTimer.getElapsedTimeF32();

    purgeExcessMotions();
    updateLoadingMotions();
    resetJointSignatures();

    deactivateStoppedMotions();

    mHasRunOnce = true;
}

//-----------------------------------------------------------------------------
// activateMotionInstance()
//-----------------------------------------------------------------------------
bool LLMotionController::activateMotionInstance(LLMotion *motion, F32 time)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    // It's not clear why the getWeight() line seems to be crashing this, but
    // hopefully this fixes it.
    if (motion == NULL || motion->getPose() == NULL)
    {
        return false;
    }

    if (mLoadingMotions.find(motion) != mLoadingMotions.end())
    {
        // we want to start this motion, but we can't yet, so flag it as started
        motion->setStopped(false);
        // report pending animations as activated
        return true;
    }

    motion->mResidualWeight = motion->getPose()->getWeight();

    // set stop time based on given duration and ease out time
    if (motion->getDuration() != 0.f && !motion->getLoop())
    {
        F32 ease_out_time;
        F32 motion_duration;

        // should we stop at the end of motion duration, or a bit earlier
        // to allow it to ease out while moving?
        ease_out_time = motion->getEaseOutDuration();

        // is the clock running when the motion is easing in?
        // if not (POSTURE_EASE) then we need to wait that much longer before triggering the stop
        motion_duration = llmax(motion->getDuration() - ease_out_time, 0.f);
        motion->mSendStopTimestamp = time + motion_duration;
    }
    else
    {
        motion->mSendStopTimestamp = F32_MAX;
    }

    if (motion->isActive())
    {
        removeActiveMotion(motion);
    }
    motion_list_t& motions = mActiveMotions[motion->getBlendType()];
    motions.insert(motions.begin(), motion);

    motion->activate(time);
    motion->onUpdate(0.f, mJointSignature[1]);

    if (mAnimTime >= motion->mSendStopTimestamp)
    {
        motion->setStopTime(motion->mSendStopTimestamp);
        if (motion->mResidualWeight == 0.0f)
        {
            // bit of a hack; if newly activating a motion while easing out, weight should = 1
            motion->mResidualWeight = 1.f;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
// deactivateMotionInstance()
//-----------------------------------------------------------------------------
bool LLMotionController::deactivateMotionInstance(LLMotion *motion)
{
    motion->deactivate();

    motion_set_t::iterator found_it = mDeprecatedMotions.find(motion);
    if (found_it != mDeprecatedMotions.end())
    {
        // deprecated motions need to be completely excised
        removeMotionInstance(motion);
        mDeprecatedMotions.erase(found_it);
    }
    else
    {
        // for motions that we are keeping, simply remove from active queue
        removeActiveMotion(motion);
    }

    return true;
}

void LLMotionController::deprecateMotionInstance(LLMotion* motion)
{
    mDeprecatedMotions.insert(motion);

    //fade out deprecated motion
    stopMotionInstance(motion, false);
    //no longer canonical
    mAllMotions.erase(motion->getID());
}

//-----------------------------------------------------------------------------
// isMotionActive()
//-----------------------------------------------------------------------------
bool LLMotionController::isMotionActive(LLMotion *motion)
{
    return (motion && motion->isActive());
}

//-----------------------------------------------------------------------------
// isMotionLoading()
//-----------------------------------------------------------------------------
bool LLMotionController::isMotionLoading(LLMotion* motion)
{
    return (mLoadingMotions.find(motion) != mLoadingMotions.end());
}


//-----------------------------------------------------------------------------
// findMotion()
//-----------------------------------------------------------------------------
LLMotion* LLMotionController::findMotion(const LLUUID& id) const
{
    motion_map_t::const_iterator iter = mAllMotions.find(id);
    if(iter == mAllMotions.end())
    {
        return NULL;
    }
    else
    {
        return iter->second;
    }
}

//-----------------------------------------------------------------------------
// dumpMotions()
//-----------------------------------------------------------------------------
void LLMotionController::dumpMotions()
{
    LL_INFOS() << "=====================================" << LL_ENDL;
    for (motion_map_t::value_type& motion_pair : mAllMotions)
    {
        LLUUID id = motion_pair.first;
        std::string state_string;
        LLMotion *motion = motion_pair.second;
        if (mLoadingMotions.find(motion) != mLoadingMotions.end())
            state_string += std::string("l");
        if (mLoadedMotions.find(motion) != mLoadedMotions.end())
            state_string += std::string("L");
        const motion_list_t& active = mActiveMotions[motion->getBlendType()];
        if (std::find(active.begin(), active.end(), motion) != active.end())
            state_string += std::string("A");
        if (mDeprecatedMotions.find(motion) != mDeprecatedMotions.end())
            state_string += std::string("D");
        LL_INFOS() << gAnimLibrary.animationName(id) << " " << state_string << LL_ENDL;

    }
}

//-----------------------------------------------------------------------------
// deactivateAllMotions()
//-----------------------------------------------------------------------------
void LLMotionController::deactivateAllMotions()
{
    for (motion_map_t::value_type& motion_pair : mAllMotions)
    {
        LLMotion* motionp = motion_pair.second;
        deactivateMotionInstance(motionp);
    }
}


//-----------------------------------------------------------------------------
// flushAllMotions()
//-----------------------------------------------------------------------------
void LLMotionController::flushAllMotions()
{
    std::vector<std::pair<LLUUID,F32> > active_motions;
    active_motions.reserve(getNumActiveMotions());
    for (motion_list_t& motions : mActiveMotions)
    {
        for (LLMotion* motionp : motions)
        {
            F32 dtime = mAnimTime - motionp->mActivationTimestamp;
            active_motions.push_back(std::make_pair(motionp->getID(),dtime));
            motionp->deactivate(); // don't call deactivateMotionInstance() because we are going to reactivate it
        }
        motions.clear();
    }

    // delete all motion instances
    deleteAllMotions();

    // kill current hand pose that was previously called out by
    // keyframe motion
    mCharacter->removeAnimationData("Hand Pose");

    // restart motions
    for (std::vector<std::pair<LLUUID,F32> >::value_type& motion_pair : active_motions)
    {
        startMotion(motion_pair.first, motion_pair.second);
    }
}

//-----------------------------------------------------------------------------
// pause()
//-----------------------------------------------------------------------------
void LLMotionController::pauseAllMotions()
{
    if (!mPaused)
    {
        //LL_INFOS() << "Pausing animations..." << LL_ENDL;
        mPaused = true;
        mPausedFrame = LLFrameTimer::getFrameCount();
    }

}

//-----------------------------------------------------------------------------
// unpause()
//-----------------------------------------------------------------------------
void LLMotionController::unpauseAllMotions()
{
    if (mPaused)
    {
        //LL_INFOS() << "Unpausing animations..." << LL_ENDL;
        mPaused = false;
    }
}
// End
