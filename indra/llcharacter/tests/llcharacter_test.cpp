/**
 * @file llcharacter_test.cpp
 * @brief Unit tests for LLCharacter and its LLMotionController, driven through
 *        the ALTestCharacter and ALTestMotion stubs.
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

#include "linden_common.h"

#include "llcharacter.h"
#include "lljoint.h"
#include "lljointstate.h"
#include "llquaternion.h"
#include "llframetimer.h"
#include "lltimer.h"

#include "altestcharacter.h"
#include "altestmotion.h"

#include "../test/lltut.h"

namespace
{
    // q and -q are the same rotation, so compare through the absolute dot product.
    void ensure_quat_equals(const char* msg, const LLQuaternion& actual, const LLQuaternion& expected)
    {
        tut::ensure_approximately_equals_range(msg, fabsf(dot(actual, expected)), 1.f, 1e-5f);
    }
}

namespace tut
{
    struct llcharacter_data
    {
        ALTestCharacter mCharacter;

        // Registers a fresh motion id with the shared registry and starts it,
        // returning the instance the controller is actually running.
        ALTestMotion* startFreshMotion(LLUUID& id, LLMotionConstructor create = ALTestMotion::create)
        {
            id = LLUUID::generateNewID();
            ensure("registerMotion", mCharacter.registerMotion(id, create));
            ensure("startMotion", mCharacter.startMotion(id));
            LLMotion* motion = mCharacter.findMotion(id);
            ensure("started motion is findable", motion != nullptr);
            return static_cast<ALTestMotion*>(motion);
        }

        // Gives a motion a rotation on a joint of its own, so the joint
        // signature never masks it and every update reaches its onUpdate.
        void animateJoint(ALTestMotion* motion, const char* joint_name)
        {
            LLJoint* joint = mCharacter.getJoint(joint_name);
            ensure("joint resolves", joint != nullptr);
            motion->addJoint(joint, LLJointState::ROT);
        }

        // One frame of a running viewer: the frame clock moves before the
        // motions are updated against it.
        void runFrame()
        {
            ms_sleep(2);
            LLFrameTimer::updateFrameTime();
            mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        }

        const LLMotionController::motion_list_t& activeMotions(LLMotion::LLMotionBlendType blend_type)
        {
            return mCharacter.getMotionController().getActiveMotions(blend_type);
        }
    };
    typedef test_group<llcharacter_data> llcharacter_test;
    typedef llcharacter_test::object llcharacter_object;
    tut::llcharacter_test llcharacter_testcase("LLCharacter");

    template<> template<>
    void llcharacter_object::test<1>()
    {
        LLJoint* pelvis = mCharacter.getJoint("mPelvis");
        ensure("pelvis resolves", pelvis != nullptr);
        ensure_equals("pelvis joint number", pelvis->getJointNum(), 0);
        ensure("pelvis hangs off the root", pelvis->getParent() == mCharacter.getRootJoint());

        LLJoint* head = mCharacter.getJoint("mHead");
        ensure("head resolves", head != nullptr);
        ensure_equals("head joint number", head->getJointNum(), 4);
        ensure_equals("head parent", head->getParent()->getName(), std::string("mNeck"));

        ensure("character joint by index", mCharacter.getCharacterJoint(2) == mCharacter.getJoint("mChest"));
        ensure("unknown joint is null", mCharacter.getJoint("mNope") == nullptr);
    }

    template<> template<>
    void llcharacter_object::test<2>()
    {
        LLUUID id = LLUUID::generateNewID();
        ensure("registerMotion accepts a new id", mCharacter.registerMotion(id, ALTestMotion::create));
        ensure("registerMotion refuses a duplicate", !mCharacter.registerMotion(id, ALTestMotion::create));

        LLMotion* motion = mCharacter.createMotion(id);
        ensure("createMotion builds an instance", motion != nullptr);
        ensure("findMotion returns the same instance", mCharacter.findMotion(id) == motion);
        ensure("createMotion again returns the same instance", mCharacter.createMotion(id) == motion);
        ensure_equals("onInitialize ran once", static_cast<ALTestMotion*>(motion)->mInitializeCount, 1);
        ensure("created motion is not active", !mCharacter.isMotionActive(id));
    }

    template<> template<>
    void llcharacter_object::test<3>()
    {
        LLUUID id;
        ALTestMotion* motion = startFreshMotion(id);
        ensure("motion is active", mCharacter.isMotionActive(id));
        ensure_equals("onActivate ran once", motion->mActivateCount, 1);
        ensure_equals("activation primes with one onUpdate", motion->mUpdateCount, 1);

        const LLQuaternion rot(0.7f, LLVector3::y_axis);
        LLJoint* pelvis = mCharacter.getJoint("mPelvis");
        motion->addJoint(pelvis, LLJointState::ROT)->setRotation(rot);

        mCharacter.updateMotions(LLCharacter::HIDDEN_UPDATE);
        ensure_equals("hidden update does not run onUpdate", motion->mUpdateCount, 1);

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        ensure_equals("normal update runs onUpdate", motion->mUpdateCount, 2);
        ensure_quat_equals("normal update blends the pose into the skeleton", pelvis->getRotation(), rot);

        ensure("stopMotion", mCharacter.stopMotion(id, true));
        ensure("stopped motion is inactive", !mCharacter.isMotionActive(id));
        ensure_equals("onDeactivate ran once", motion->mDeactivateCount, 1);

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        ensure_equals("inactive motion is not updated", motion->mUpdateCount, 2);
    }

    template<> template<>
    void llcharacter_object::test<4>()
    {
        // A motion that reports completion from onUpdate is stopped by the controller.
        LLUUID id;
        ALTestMotion* motion = startFreshMotion(id);
        motion->mUpdateResult = false;

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        ensure("self-completing motion is marked stopped", motion->isStopped());
    }

    template<> template<>
    void llcharacter_object::test<5>()
    {
        // The playing motions are kept newest first in a list per blend type,
        // and an update walks the whole additive list before the normal one:
        // the order the blender's masking depends on. Stopping a motion takes
        // it out of its own list and leaves the other alone.
        LLUUID normal_first_id, additive_first_id, normal_second_id, additive_second_id;
        ALTestMotion* normal_first = startFreshMotion(normal_first_id);
        ALTestMotion* additive_first = startFreshMotion(additive_first_id, ALTestMotion::createAdditive);
        ALTestMotion* normal_second = startFreshMotion(normal_second_id);
        ALTestMotion* additive_second = startFreshMotion(additive_second_id, ALTestMotion::createAdditive);
        animateJoint(normal_first, "mPelvis");
        animateJoint(additive_first, "mTorso");
        animateJoint(normal_second, "mChest");
        animateJoint(additive_second, "mNeck");

        const LLMotionController::motion_list_t& normals = activeMotions(LLMotion::NORMAL_BLEND);
        const LLMotionController::motion_list_t& additives = activeMotions(LLMotion::ADDITIVE_BLEND);
        ensure_equals("two normal motions", normals.size(), 2u);
        ensure("normal list is newest first", normals[0] == normal_second && normals[1] == normal_first);
        ensure_equals("two additive motions", additives.size(), 2u);
        ensure("additive list is newest first", additives[0] == additive_second && additives[1] == additive_first);

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        ensure("newest additive motion is updated first", additive_second->mUpdateSerial < additive_first->mUpdateSerial);
        ensure("the additive pass finishes before the normal pass begins", additive_first->mUpdateSerial < normal_second->mUpdateSerial);
        ensure("newest normal motion is updated first", normal_second->mUpdateSerial < normal_first->mUpdateSerial);

        ensure("stopMotion", mCharacter.stopMotion(additive_first_id, true));
        ensure_equals("stopping an additive motion shortens the additive list", additives.size(), 1u);
        ensure("the other additive motion stays", additives[0] == additive_second);
        ensure_equals("and leaves the normal list alone", normals.size(), 2u);
    }

    template<> template<>
    void llcharacter_object::test<6>()
    {
        // A motion that finishes easing out takes itself off the list while
        // the list is being walked. The older motion behind it moves into its
        // slot and has to be updated that frame all the same, exactly once.
        LLUUID oldest_id, ending_id, newest_id;
        ALTestMotion* oldest = startFreshMotion(oldest_id);
        ALTestMotion* ending = startFreshMotion(ending_id);
        ALTestMotion* newest = startFreshMotion(newest_id);
        animateJoint(oldest, "mPelvis");
        animateJoint(ending, "mTorso");
        animateJoint(newest, "mChest");
        ensure("stopMotion", mCharacter.stopMotion(ending_id, false));

        S32 frames = 0;
        while (mCharacter.isMotionActive(ending_id) && frames < 20)
        {
            runFrame();
            ++frames;
        }
        ensure("the ending motion left the list", !mCharacter.isMotionActive(ending_id));
        ensure_equals("the ending motion was deactivated once", ending->mDeactivateCount, 1);
        ensure_equals("the motion behind it was updated every frame", oldest->mUpdateCount, 1 + frames);
        ensure_equals("the motion ahead of it was updated every frame", newest->mUpdateCount, 1 + frames);

        const LLMotionController::motion_list_t& normals = activeMotions(LLMotion::NORMAL_BLEND);
        ensure_equals("two motions remain", normals.size(), 2u);
        ensure("remaining motions are still newest first", normals[0] == newest && normals[1] == oldest);
    }

    template<> template<>
    void llcharacter_object::test<7>()
    {
        // The LOD test compares every motion against the character's pixel
        // area, which is the same for all of them and a virtual call. It is
        // asked once per pass, however many motions are playing.
        LLUUID id;
        animateJoint(startFreshMotion(id), "mPelvis");
        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);

        mCharacter.mPixelAreaQueries = 0;
        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        const S32 queries_with_one_motion = mCharacter.mPixelAreaQueries;

        animateJoint(startFreshMotion(id), "mTorso");
        animateJoint(startFreshMotion(id), "mChest");
        animateJoint(startFreshMotion(id, ALTestMotion::createAdditive), "mNeck");
        mCharacter.mPixelAreaQueries = 0;
        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        ensure_equals("pixel area is not asked once per motion", mCharacter.mPixelAreaQueries, queries_with_one_motion);
    }

    template<> template<>
    void llcharacter_object::test<8>()
    {
        // A flush restarts every playing motion, whichever list it was on.
        LLUUID normal_id, additive_id;
        startFreshMotion(normal_id);
        startFreshMotion(additive_id, ALTestMotion::createAdditive);

        mCharacter.flushAllMotions();
        ensure("normal motion restarted", mCharacter.isMotionActive(normal_id));
        ensure("additive motion restarted", mCharacter.isMotionActive(additive_id));
        ensure_equals("one normal motion", activeMotions(LLMotion::NORMAL_BLEND).size(), 1u);
        ensure_equals("one additive motion", activeMotions(LLMotion::ADDITIVE_BLEND).size(), 1u);
    }

    template<> template<>
    void llcharacter_object::test<9>()
    {
        // Hidden, the controller drops every stopped motion in one pass, and
        // a motion leaving the list must not hide the stopped one behind it.
        LLUUID oldest_id, middle_id, newest_id;
        startFreshMotion(oldest_id);
        startFreshMotion(middle_id);
        startFreshMotion(newest_id);
        ensure("stop middle", mCharacter.stopMotion(middle_id, false));
        ensure("stop oldest", mCharacter.stopMotion(oldest_id, false));

        mCharacter.updateMotions(LLCharacter::HIDDEN_UPDATE);
        ensure("the stopped middle motion left the list", !mCharacter.isMotionActive(middle_id));
        ensure("the stopped motion behind it left the list too", !mCharacter.isMotionActive(oldest_id));
        ensure("the running motion stayed", mCharacter.isMotionActive(newest_id));
        ensure_equals("one motion remains", activeMotions(LLMotion::NORMAL_BLEND).size(), 1u);
    }

    template<> template<>
    void llcharacter_object::test<10>()
    {
        // Moving onto the coarse clock snaps every playing motion's timestamps
        // to the quantum. A running motion has no stop time to snap, and
        // stopping it to snap one would give it a stop time it never had; a
        // motion that never ends has no send-stop time to snap either. A
        // motion that is stopped keeps its stop, on the quantum.
        LLUUID running_id, stopped_id;
        ALTestMotion* running = startFreshMotion(running_id);
        ALTestMotion* stopped = startFreshMotion(stopped_id);
        runFrame();
        ensure("stopMotion", mCharacter.stopMotion(stopped_id, false));
        stopped->mStopTimeCalls = 0;
        running->mStopTimeCalls = 0;

        mCharacter.getMotionController().setTimeStep(0.25f);
        ensure("the running motion is still running", !running->isStopped());
        ensure_equals("its stop time was never set", running->mStopTimeCalls, 0);
        ensure_equals("and it still reads as never stopped", running->getStopTime(), 0.f);
        ensure("a motion that never ends keeps its open send-stop time", running->sendStopTimestamp() == F32_MAX);
        ensure("the stopped motion is still stopped", stopped->isStopped());
        ensure_equals("its stop time sits on the quantum", fmodf(stopped->getStopTime(), 0.25f), 0.f);
    }
}
