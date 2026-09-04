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
        ALTestMotion* startFreshMotion(LLUUID& id)
        {
            id = LLUUID::generateNewID();
            ensure("registerMotion", mCharacter.registerMotion(id, ALTestMotion::create));
            ensure("startMotion", mCharacter.startMotion(id));
            LLMotion* motion = mCharacter.findMotion(id);
            ensure("started motion is findable", motion != nullptr);
            return static_cast<ALTestMotion*>(motion);
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
}
