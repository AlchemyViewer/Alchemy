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

#include "llkeyframewalkmotion.h"

#include "altestcharacter.h"
#include "altestmotion.h"

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "altestmatchers.h"

namespace
{
    // q and -q are the same rotation, so compare through the absolute dot product.
    void require_same_rotation(const char* what, const LLQuaternion& actual, const LLQuaternion& expected)
    {
        INFO(what);
        REQUIRE_THAT(fabsf(dot(actual, expected)), Catch::Matchers::WithinAbs(1.f, 1e-5f));
    }
}

namespace
{

    // The info holds the name, the id and the range, and every one of its
    // fields is protected, so the way to set them is to be a subclass.
    class ALTestVisualParamInfo : public LLVisualParamInfo
    {
    public:
        ALTestVisualParamInfo(S32 id, const std::string& name, ESex sex = SEX_BOTH)
        {
            mID = id;
            mName = name;
            mSex = sex;
            mMinWeight = 0.f;
            mMaxWeight = 1.f;
            mDefaultWeight = 0.f;
        }
    };

    // Counts what the sweep asked of it, and stamps the last weight the way a
    // real apply does -- without that stamp every parameter stays changed and
    // the sweep applies all of them forever.
    class ALTestVisualParam : public LLVisualParam
    {
    public:
        // LLVisualParam::setInfo is declared but commented out of the library,
        // with a note that every subclass writes its own, so this one does.
        bool setInfo(LLVisualParamInfo* info)
        {
            if (!info || info->getID() < 0)
            {
                return false;
            }
            mID = info->getID();
            mInfo = info;
            setWeight(getDefaultWeight());
            return true;
        }

        void apply(ESex avatar_sex) override
        {
            ++mApplied;
            mAppliedSex = avatar_sex;
            setLastWeight((getSex() & avatar_sex) ? getWeight() : getDefaultWeight());
        }

        S32 getWearableType() const override { return 0; }

        S32  mApplied = 0;
        ESex mAppliedSex = SEX_BOTH;
    };
}

namespace
{
    struct llcharacter_data
    {
        ALTestCharacter mCharacter;

        // The character deletes every parameter it was given; the infos are
        // not owned by anything, so the fixture holds them.
        std::vector<std::unique_ptr<LLVisualParamInfo>> mParamInfos;

        ALTestVisualParam* addParam(S32 id, const std::string& name, ESex sex = SEX_BOTH)
        {
            mParamInfos.push_back(std::make_unique<ALTestVisualParamInfo>(id, name, sex));
            ALTestVisualParam* param = new ALTestVisualParam();
            REQUIRE(param->setInfo(mParamInfos.back().get()));
            mCharacter.addVisualParam(param);
            return param;
        }

        // Registers a fresh motion id with the shared registry and starts it,
        // returning the instance the controller is actually running.
        ALTestMotion* startFreshMotion(LLUUID& id, LLMotionConstructor create = ALTestMotion::create)
        {
            id = LLUUID::generateNewID();
            REQUIRE(mCharacter.registerMotion(id, create));
            REQUIRE(mCharacter.startMotion(id));
            LLMotion* motion = mCharacter.findMotion(id);
            REQUIRE(motion != nullptr);
            return static_cast<ALTestMotion*>(motion);
        }

        // Gives a motion a rotation on a joint of its own, so the joint
        // signature never masks it and every update reaches its onUpdate.
        void animateJoint(ALTestMotion* motion, const char* joint_name)
        {
            LLJoint* joint = mCharacter.getJoint(joint_name);
            REQUIRE(joint != nullptr);
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

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: joints resolve by name and index with their numbers and parents", "[LLCharacter]")
    {
        LLJoint* pelvis = mCharacter.getJoint("mPelvis");
        REQUIRE(pelvis != nullptr);
        CHECK(pelvis->getJointNum() == 0);
        CHECK(pelvis->getParent() == mCharacter.getRootJoint());

        LLJoint* head = mCharacter.getJoint("mHead");
        REQUIRE(head != nullptr);
        CHECK(head->getJointNum() == 4);
        LLJoint* neck = head->getParent();
        REQUIRE(neck != nullptr);
        CHECK(neck->getName() == std::string("mNeck"));

        CHECK(mCharacter.getCharacterJoint(2) == mCharacter.getJoint("mChest"));
        CHECK(mCharacter.getJoint("mNope") == nullptr);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: registerMotion refuses duplicates and createMotion builds one instance", "[LLCharacter]")
    {
        LLUUID id = LLUUID::generateNewID();
        CHECK(mCharacter.registerMotion(id, ALTestMotion::create));
        CHECK_FALSE(mCharacter.registerMotion(id, ALTestMotion::create));

        LLMotion* motion = mCharacter.createMotion(id);
        REQUIRE(motion != nullptr);
        CHECK(mCharacter.findMotion(id) == motion);
        CHECK(mCharacter.createMotion(id) == motion);
        CHECK(static_cast<ALTestMotion*>(motion)->mInitializeCount == 1);
        CHECK_FALSE(mCharacter.isMotionActive(id));
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a started motion updates on normal updates until stopped", "[LLCharacter]")
    {
        LLUUID id;
        ALTestMotion* motion = startFreshMotion(id);
        CHECK(mCharacter.isMotionActive(id));
        CHECK(motion->mActivateCount == 1);
        UNSCOPED_INFO("activation primes with one onUpdate");
        CHECK(motion->mUpdateCount == 1);

        const LLQuaternion rot(0.7f, LLVector3::y_axis);
        LLJoint* pelvis = mCharacter.getJoint("mPelvis");
        motion->addJoint(pelvis, LLJointState::ROT)->setRotation(rot);

        mCharacter.updateMotions(LLCharacter::HIDDEN_UPDATE);
        UNSCOPED_INFO("hidden update does not run onUpdate");
        CHECK(motion->mUpdateCount == 1);

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        UNSCOPED_INFO("normal update runs onUpdate");
        CHECK(motion->mUpdateCount == 2);
        require_same_rotation("normal update blends the pose into the skeleton", pelvis->getRotation(), rot);

        CHECK(mCharacter.stopMotion(id, true));
        CHECK_FALSE(mCharacter.isMotionActive(id));
        CHECK(motion->mDeactivateCount == 1);

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        UNSCOPED_INFO("inactive motion is not updated");
        CHECK(motion->mUpdateCount == 2);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a motion whose onUpdate returns false is stopped", "[LLCharacter]")
    {
        // A motion that reports completion from onUpdate is stopped by the controller.
        LLUUID id;
        ALTestMotion* motion = startFreshMotion(id);
        motion->mUpdateResult = false;

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        CHECK(motion->isStopped());
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: motions are listed newest first and additive ones update first", "[LLCharacter]")
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
        REQUIRE(normals.size() == 2u);
        CHECK(normals[0] == normal_second);
        CHECK(normals[1] == normal_first);
        REQUIRE(additives.size() == 2u);
        CHECK(additives[0] == additive_second);
        CHECK(additives[1] == additive_first);

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        UNSCOPED_INFO("newest additive motion is updated first");
        CHECK(additive_second->mUpdateSerial < additive_first->mUpdateSerial);
        UNSCOPED_INFO("the additive pass finishes before the normal pass begins");
        CHECK(additive_first->mUpdateSerial < normal_second->mUpdateSerial);
        UNSCOPED_INFO("newest normal motion is updated first");
        CHECK(normal_second->mUpdateSerial < normal_first->mUpdateSerial);

        CHECK(mCharacter.stopMotion(additive_first_id, true));
        UNSCOPED_INFO("stopping an additive motion shortens the additive list");
        REQUIRE(additives.size() == 1u);
        UNSCOPED_INFO("the other additive motion stays");
        CHECK(additives[0] == additive_second);
        UNSCOPED_INFO("and leaves the normal list alone");
        CHECK(normals.size() == 2u);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a motion leaving the list mid-update skips no other motion", "[LLCharacter]")
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
        CHECK(mCharacter.stopMotion(ending_id, false));

        S32 frames = 0;
        while (mCharacter.isMotionActive(ending_id) && frames < 20)
        {
            runFrame();
            ++frames;
        }
        CHECK_FALSE(mCharacter.isMotionActive(ending_id));
        CHECK(ending->mDeactivateCount == 1);
        UNSCOPED_INFO("the motion behind it was updated every frame");
        CHECK(oldest->mUpdateCount == 1 + frames);
        UNSCOPED_INFO("the motion ahead of it was updated every frame");
        CHECK(newest->mUpdateCount == 1 + frames);

        const LLMotionController::motion_list_t& normals = activeMotions(LLMotion::NORMAL_BLEND);
        REQUIRE(normals.size() == 2u);
        {
            INFO("remaining motions are still newest first");
            CHECK(normals[0] == newest);
            CHECK(normals[1] == oldest);
        }
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: the pixel area is asked once per pass and not per motion", "[LLCharacter]")
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
        CHECK(mCharacter.mPixelAreaQueries == queries_with_one_motion);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: flushAllMotions restarts normal and additive motions", "[LLCharacter]")
    {
        // A flush restarts every playing motion, whichever list it was on.
        LLUUID normal_id, additive_id;
        startFreshMotion(normal_id);
        startFreshMotion(additive_id, ALTestMotion::createAdditive);

        mCharacter.flushAllMotions();
        CHECK(mCharacter.isMotionActive(normal_id));
        CHECK(mCharacter.isMotionActive(additive_id));
        CHECK(activeMotions(LLMotion::NORMAL_BLEND).size() == 1u);
        CHECK(activeMotions(LLMotion::ADDITIVE_BLEND).size() == 1u);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a hidden update drops every stopped motion in one pass", "[LLCharacter]")
    {
        // Hidden, the controller drops every stopped motion in one pass, and
        // a motion leaving the list must not hide the stopped one behind it.
        LLUUID oldest_id, middle_id, newest_id;
        startFreshMotion(oldest_id);
        startFreshMotion(middle_id);
        startFreshMotion(newest_id);
        CHECK(mCharacter.stopMotion(middle_id, false));
        CHECK(mCharacter.stopMotion(oldest_id, false));

        mCharacter.updateMotions(LLCharacter::HIDDEN_UPDATE);
        CHECK_FALSE(mCharacter.isMotionActive(middle_id));
        UNSCOPED_INFO("the stopped motion behind it left the list too");
        CHECK_FALSE(mCharacter.isMotionActive(oldest_id));
        CHECK(mCharacter.isMotionActive(newest_id));
        CHECK(activeMotions(LLMotion::NORMAL_BLEND).size() == 1u);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: setTimeStep snaps only a stopped motion's stop time", "[LLCharacter]")
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
        CHECK(mCharacter.stopMotion(stopped_id, false));
        stopped->mStopTimeCalls = 0;
        running->mStopTimeCalls = 0;

        mCharacter.getMotionController().setTimeStep(0.25f);
        CHECK_FALSE(running->isStopped());
        UNSCOPED_INFO("its stop time was never set");
        CHECK(running->mStopTimeCalls == 0);
        UNSCOPED_INFO("and it still reads as never stopped");
        CHECK(running->getStopTime() == 0.0);
        UNSCOPED_INFO("a motion that never ends keeps its open send-stop time");
        CHECK(running->sendStopTimestamp() == F32_MAX);
        CHECK(stopped->isStopped());
        UNSCOPED_INFO("its stop time sits on the quantum");
        CHECK(fmod(stopped->getStopTime(), 0.25) == 0.0);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a motion started during an update waits for the next frame", "[LLCharacter]")
    {
        // A motion's update can start another motion -- an emote's does when
        // it finds the sit animation is for the wrong sex -- and the new one
        // goes in at the front of the very list being walked. Everything
        // already on the list is still updated exactly once that frame, and
        // the new one waits for the next. The motions ease in slowly so that
        // a second visit would reach their update rather than be idled away
        // by the joint signature.
        LLUUID oldest_id, middle_id, newest_id;
        ALTestMotion* oldest = startFreshMotion(oldest_id);
        ALTestMotion* middle = startFreshMotion(middle_id);
        ALTestMotion* newest = startFreshMotion(newest_id);
        animateJoint(oldest, "mPelvis");
        animateJoint(middle, "mTorso");
        animateJoint(newest, "mChest");
        oldest->mEaseInDuration = 10.f;
        middle->mEaseInDuration = 10.f;
        newest->mEaseInDuration = 10.f;

        const LLUUID started_id = LLUUID::generateNewID();
        CHECK(mCharacter.registerMotion(started_id, ALTestMotion::create));
        middle->mStartOnUpdate = started_id;

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        CHECK(mCharacter.isMotionActive(started_id));
        UNSCOPED_INFO("the motion ahead of the starter was updated once");
        CHECK(newest->mUpdateCount == 2);
        UNSCOPED_INFO("the starter was updated once");
        CHECK(middle->mUpdateCount == 2);
        UNSCOPED_INFO("the motion behind the starter was updated once");
        CHECK(oldest->mUpdateCount == 2);
        ALTestMotion* started = static_cast<ALTestMotion*>(mCharacter.findMotion(started_id));
        REQUIRE(started != nullptr);
        UNSCOPED_INFO("the new motion had only its activation update");
        CHECK(started->mUpdateCount == 1);

        const LLMotionController::motion_list_t& normals = activeMotions(LLMotion::NORMAL_BLEND);
        REQUIRE(normals.size() == 4u);
        {
            INFO("newest first, the started one ahead of them all");
            CHECK(normals[0] == started);
            CHECK(normals[1] == newest);
            CHECK(normals[2] == middle);
            CHECK(normals[3] == oldest);
        }
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: animation data is a slot and the hand pose request a copy", "[LLCharacter]")
    {
        // The values motions hand one another are slots on the character: set
        // by whoever owns the value, empty when nobody does.
        F32 speed = 3.f;
        CHECK(mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED) == nullptr);
        mCharacter.setAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED, &speed);
        CHECK(mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED) == &speed);
        {
            INFO("the other channels are untouched");
            CHECK(mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_POINT_AT_POINT) == nullptr);
            CHECK(mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_LOOK_AT_POINT) == nullptr);
        }
        mCharacter.removeAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED);
        CHECK(mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED) == nullptr);

        // The hand pose is not a slot but a copy, so the request stands after
        // whatever it was read from has changed or gone, and it is dropped
        // with the motions on a flush.
        UNSCOPED_INFO("no hand pose is asked for to begin with");
        CHECK_FALSE(mCharacter.hasHandPoseRequest());
        {
            S32 pose = 1;
            S32 priority = 2;
            mCharacter.requestHandPose(pose, priority);
            pose = 5;
            priority = 4;
        }
        UNSCOPED_INFO("a request stands");
        CHECK(mCharacter.hasHandPoseRequest());
        UNSCOPED_INFO("with the pose asked for");
        CHECK(mCharacter.getHandPoseRequest() == 1);
        UNSCOPED_INFO("at the priority asked at");
        CHECK(mCharacter.getHandPoseRequestPriority() == 2);
        mCharacter.clearHandPoseRequest();
        CHECK_FALSE(mCharacter.hasHandPoseRequest());

        mCharacter.requestHandPose(1, 2);
        mCharacter.flushAllMotions();
        CHECK_FALSE(mCharacter.hasHandPoseRequest());
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a motion stopped while easing in never rises in weight", "[LLCharacter]")
    {
        // A motion stopped before it finished easing in gets one more update,
        // to hand it the stop time it never saw, and then deactivates. It
        // must not be shown at a weight it never reached: it is a fraction of
        // the way in, its ease out is over, and the frame in between used to
        // bring it up to full.
        LLUUID id;
        ALTestMotion* motion = startFreshMotion(id);
        animateJoint(motion, "mPelvis");
        motion->mEaseInDuration = 10.f;
        motion->mEaseOutDuration = 0.f;

        runFrame();
        runFrame();
        const F32 easing_in = motion->getPose()->getWeight();
        UNSCOPED_INFO("the motion is still easing in");
        CHECK(easing_in < 0.5f);

        mCharacter.stopMotion(id);
        runFrame();

        UNSCOPED_INFO("the last update does not raise the weight");
        CHECK(motion->getPose()->getWeight() <= easing_in);
        UNSCOPED_INFO("the motion is still around for its last update");
        CHECK(activeMotions(LLMotion::NORMAL_BLEND).size() == 1u);

        runFrame();
        UNSCOPED_INFO("and is gone the frame after");
        CHECK(activeMotions(LLMotion::NORMAL_BLEND).empty());
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a motion still easing in does not mask the one under it", "[LLCharacter]")
    {
        // The mask that lets a motion's joints go unread is built from motions
        // at full weight and no others. One still easing in has claimed
        // nothing, so what is under it is still blended and still shows.
        LLJoint* joint = mCharacter.getJoint("mPelvis");
        REQUIRE(joint != nullptr);

        const LLQuaternion under_rot(0.9f, LLVector3::x_axis);
        const LLQuaternion over_rot(-1.4f, LLVector3::y_axis);

        LLUUID under_id;
        ALTestMotion* under = startFreshMotion(under_id);
        under->mPriority = LLJoint::MEDIUM_PRIORITY;
        under->addJoint(joint, LLJointState::ROT)->setRotation(under_rot);

        LLUUID over_id;
        ALTestMotion* over = startFreshMotion(over_id);
        over->mPriority = LLJoint::HIGH_PRIORITY;
        over->mEaseInDuration = 100.f;
        over->addJoint(joint, LLJointState::ROT)->setRotation(over_rot);

        runFrame();
        runFrame();

        UNSCOPED_INFO("the one easing in has not reached full weight");
        CHECK(over->getPose()->getWeight() < 0.5f);
        UNSCOPED_INFO("so the one underneath is what the joint shows");
        CHECK(fabsf(dot(joint->getRotation(), under_rot)) > 0.99f);

        // and once it is all the way in, it owns the joint
        over->mEaseInDuration = 0.f;
        runFrame();

        UNSCOPED_INFO("it is at full weight now");
        CHECK(over->getPose()->getWeight() == 1.f);
        UNSCOPED_INFO("and the joint is what it asked for");
        CHECK(fabsf(dot(joint->getRotation(), over_rot)) > 0.99f);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: updateVisualParams applies only parameters whose weight moved", "[LLCharacter]")
    {
        // The sweep applies a parameter whose weight has moved and leaves the
        // rest alone, and says whether it did anything.
        ALTestVisualParam* first = addParam(1, "First");
        ALTestVisualParam* second = addParam(2, "Second");

        // A parameter arrives at its default weight, which is the weight it
        // was last applied at, so there is nothing to do for either of them.
        UNSCOPED_INFO("a sweep with nothing to do says so");
        CHECK_FALSE(mCharacter.updateVisualParams());
        UNSCOPED_INFO("and applies nothing");
        CHECK(first->mApplied == 0);
        CHECK(second->mApplied == 0);

        first->setWeight(0.5f);
        UNSCOPED_INFO("a sweep after a weight moves says so");
        CHECK(mCharacter.updateVisualParams());
        UNSCOPED_INFO("the parameter that moved is applied");
        CHECK(first->mApplied == 1);
        UNSCOPED_INFO("and the one that did not is not");
        CHECK(second->mApplied == 0);

        UNSCOPED_INFO("and once it has settled again there is nothing to do");
        CHECK_FALSE(mCharacter.updateVisualParams());
        UNSCOPED_INFO("with nothing applied a second time");
        CHECK(first->mApplied == 1);

        // Both of them at once.
        first->setWeight(0.25f);
        second->setWeight(0.75f);
        UNSCOPED_INFO("a sweep with two to do says so");
        CHECK(mCharacter.updateVisualParams());
        CHECK(first->mApplied == 2);
        CHECK(second->mApplied == 1);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: updateVisualParams skips animating and other-sex parameters", "[LLCharacter]")
    {
        // A parameter that is animating is left to the animation, and one for
        // the other sex is applied at its default rather than its weight.
        ALTestVisualParam* animating = addParam(1, "Animating");
        ALTestVisualParam* female = addParam(2, "Female", SEX_FEMALE);

        mCharacter.setSex(SEX_MALE);
        mCharacter.updateVisualParams();
        const S32 settled = animating->mApplied;

        animating->setAnimating(true);
        animating->setWeight(0.5f);
        UNSCOPED_INFO("an animating parameter is not swept");
        CHECK_FALSE(mCharacter.updateVisualParams());
        CHECK(animating->mApplied == settled);

        animating->setAnimating(false);
        UNSCOPED_INFO("once it stops animating the sweep takes it");
        CHECK(mCharacter.updateVisualParams());

        // The female parameter settled at its default under a male sex, so
        // moving its weight changes nothing the sweep can see.
        const S32 female_settled = female->mApplied;
        female->setWeight(0.75f);
        UNSCOPED_INFO("a parameter for the other sex has nothing to apply");
        CHECK_FALSE(mCharacter.updateVisualParams());
        CHECK(female->mApplied == female_settled);
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: visual parameters are found by name in any case and walked by id", "[LLCharacter]")
    {
        // Parameters are found by name whatever case the name is asked in,
        // and the sweep walks them in the order of their ids however they
        // arrived, because that is the order they used to be applied in.
        addParam(30, "Third");
        addParam(10, "First");
        addParam(20, "Second");

        CHECK(mCharacter.getVisualParamCount() == 3);

        std::vector<S32> ids;
        for (LLVisualParam* param = mCharacter.getFirstVisualParam();
             param;
             param = mCharacter.getNextVisualParam())
        {
            ids.push_back(param->getID());
        }
        REQUIRE(ids.size() == 3u);
        UNSCOPED_INFO("in id order, not the order they arrived");
        CHECK(ids[0] == 10);
        CHECK(ids[1] == 20);
        CHECK(ids[2] == 30);

        LLVisualParam* by_name = mCharacter.getVisualParam("Second");
        REQUIRE(by_name != nullptr);
        CHECK(by_name->getID() == 20);

        CHECK(mCharacter.getVisualParam("sEcOnD") == by_name);
        CHECK(mCharacter.getVisualParam("SECOND") == by_name);
        CHECK(mCharacter.getVisualParam("NotAParameter") == nullptr);

        // And by id, which is the other way in.
        CHECK(mCharacter.getVisualParam(20) == by_name);

        UNSCOPED_INFO("setting a weight by name finds it");
        CHECK(mCharacter.setVisualParamWeight("SeCoNd", 0.25f));
        CHECK_THAT(mCharacter.getVisualParamWeight("second"), ll_test::WithinBits(0.25f, 16));
        CHECK_FALSE(mCharacter.setVisualParamWeight("NotAParameter", 0.25f));
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: a parameter added under a taken id replaces the old one", "[LLCharacter]")
    {
        // A parameter added under an id that is already taken replaces the one
        // there, in the list the sweep walks as well as in the map.
        ALTestVisualParam* first = addParam(7, "First");
        ALTestVisualParam* second = addParam(7, "Second");

        UNSCOPED_INFO("the second one took the first one's place");
        CHECK(mCharacter.getVisualParamCount() == 1);
        CHECK(mCharacter.getVisualParam(7) == (LLVisualParam*)second);

        S32 walked = 0;
        for (LLVisualParam* param = mCharacter.getFirstVisualParam();
             param;
             param = mCharacter.getNextVisualParam())
        {
            REQUIRE(param == (LLVisualParam*)second);
            walked++;
        }
        CHECK(walked == 1);

        // The one it replaced is nobody's now, so the character will not free
        // it and this test has to.
        delete first;
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: setVisualParamWeight writes this character's own copy by id", "[LLCharacter]")
    {
        // Setting a weight through a parameter looks the parameter up by its
        // id and writes whatever this character has under that id, which is
        // not always the parameter it was handed: a wearable holds its own
        // copy of a parameter with the same id, and it is the avatar's copy
        // that has to move. That is why the lookup is there, and why skipping
        // it is only right where the caller already holds this character's own.
        ALTestVisualParam* mine = addParam(5, "Mine");

        ALTestVisualParamInfo foreign_info(5, "Mine");
        ALTestVisualParam foreign;
        REQUIRE(foreign.setInfo(&foreign_info));

        UNSCOPED_INFO("a foreign parameter with a known id is taken");
        CHECK(mCharacter.setVisualParamWeight(&foreign, 0.5f));
        UNSCOPED_INFO("and this character's own is what moved");
        CHECK_THAT(mine->getWeight(), ll_test::WithinBits(0.5f, 16));
        UNSCOPED_INFO("not the one that was handed over");
        CHECK_THAT(foreign.getWeight(), ll_test::WithinBits(0.f, 16));

        // Handed this character's own, the lookup finds that same one, which
        // is what lets a caller who already has it write to it directly.
        CHECK(mCharacter.getVisualParam(5) == (LLVisualParam*)mine);

        ALTestVisualParamInfo unknown_info(6, "Unknown");
        ALTestVisualParam unknown;
        REQUIRE(unknown.setInfo(&unknown_info));
        UNSCOPED_INFO("a parameter with an id this character has not got is refused");
        CHECK_FALSE(mCharacter.setVisualParamWeight(&unknown, 0.5f));
    }

    TEST_CASE_METHOD(llcharacter_data, "LLCharacter: speedMultiplier pins at the maximum instead of changing sign", "[LLCharacter]")
    {
        // The walk servo asks how much faster the animation has to play for
        // the planted foot to keep up with the ground, and the answer changes
        // sign the moment the foot is measured going forward faster than the
        // avatar -- which is every footfall, since it is measured over one
        // frame.
        const F32 min_multiplier = 0.1f;
        const F32 max_multiplier = 1.5f;
        auto multiplier = [&](F32 speed, F32 foot_speed)
        {
            return LLWalkAdjustMotion::speedMultiplier(speed, foot_speed, min_multiplier, max_multiplier);
        };

        // A planted foot is going nowhere, so the foot speed is the avatar's
        // and the animation plays as authored.
        UNSCOPED_INFO("a planted foot plays the animation as it is");
        CHECK_THAT(multiplier(1.5f, 1.5f), ll_test::WithinBits(1.f, 16));

        // A foot that is not keeping up asks for more.
        UNSCOPED_INFO("a foot falling behind asks for more");
        CHECK_THAT(multiplier(1.5f, 1.0f), ll_test::WithinBits(1.5f, 16));
        UNSCOPED_INFO("but no more than it is allowed");
        CHECK_THAT(multiplier(1.5f, 0.1f), ll_test::WithinBits(max_multiplier, 16));

        // A foot going forward exactly as fast as the avatar cannot say how
        // much faster to play, and the answer is as fast as allowed.
        UNSCOPED_INFO("a foot going with the avatar asks for all of it");
        CHECK_THAT(multiplier(1.5f, 0.f), ll_test::WithinBits(max_multiplier, 16));

        // And one going forward faster than the avatar is the case that used
        // to come out as the slowest multiplier there is: the animation slowed,
        // which made the foot slip further, which kept the answer negative.
        UNSCOPED_INFO("and one going faster than the avatar does too");
        CHECK_THAT(multiplier(1.5f, -0.2f), ll_test::WithinBits(max_multiplier, 16));
        UNSCOPED_INFO("however much faster");
        CHECK_THAT(multiplier(1.5f, -50.f), ll_test::WithinBits(max_multiplier, 16));

        UNSCOPED_INFO("a foot going forward faster is never read as standing still");
        CHECK(multiplier(1.5f, -0.2f) > min_multiplier);

        // Running asks for far more than it is allowed, so it sits at the top
        // of the range and never goes near the sign change at all. That is why
        // it was the walk that stuck.
        UNSCOPED_INFO("running is pinned at the ceiling");
        CHECK_THAT(multiplier(5.f, 0.5f), ll_test::WithinBits(max_multiplier, 16));
    }
}
