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

#include <memory>
#include <string>
#include <vector>

#include "../test/lltut.h"

namespace
{
    // q and -q are the same rotation, so compare through the absolute dot product.
    void ensure_quat_equals(const char* msg, const LLQuaternion& actual, const LLQuaternion& expected)
    {
        tut::ensure_approximately_equals_range(msg, fabsf(dot(actual, expected)), 1.f, 1e-5f);
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

namespace tut
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
            ensure("the parameter takes its info", param->setInfo(mParamInfos.back().get()));
            mCharacter.addVisualParam(param);
            return param;
        }

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

    template<> template<>
    void llcharacter_object::test<11>()
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
        ensure("registerMotion", mCharacter.registerMotion(started_id, ALTestMotion::create));
        middle->mStartOnUpdate = started_id;

        mCharacter.updateMotions(LLCharacter::NORMAL_UPDATE);
        ensure("the started motion is playing", mCharacter.isMotionActive(started_id));
        ensure_equals("the motion ahead of the starter was updated once", newest->mUpdateCount, 2);
        ensure_equals("the starter was updated once", middle->mUpdateCount, 2);
        ensure_equals("the motion behind the starter was updated once", oldest->mUpdateCount, 2);
        ALTestMotion* started = static_cast<ALTestMotion*>(mCharacter.findMotion(started_id));
        ensure_equals("the new motion had only its activation update", started->mUpdateCount, 1);

        const LLMotionController::motion_list_t& normals = activeMotions(LLMotion::NORMAL_BLEND);
        ensure_equals("four motions listed", normals.size(), 4u);
        ensure("newest first, the started one ahead of them all",
               normals[0] == started && normals[1] == newest && normals[2] == middle && normals[3] == oldest);
    }

    template<> template<>
    void llcharacter_object::test<12>()
    {
        // The values motions hand one another are slots on the character: set
        // by whoever owns the value, empty when nobody does, and the hand pose
        // is dropped with the motions on a flush.
        F32 speed = 3.f;
        ensure("an unset channel reads empty", mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED) == nullptr);
        mCharacter.setAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED, &speed);
        ensure("a set channel reads back what was set", mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED) == &speed);
        ensure("the other channels are untouched",
               mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_HAND_POSE) == nullptr
               && mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_LOOK_AT_POINT) == nullptr);
        mCharacter.removeAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED);
        ensure("a removed channel reads empty", mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_WALK_SPEED) == nullptr);

        S32 pose = 1;
        mCharacter.setAnimationData(LLCharacter::ANIM_CHANNEL_HAND_POSE, &pose);
        mCharacter.flushAllMotions();
        ensure("a flush drops the hand pose", mCharacter.getAnimationData(LLCharacter::ANIM_CHANNEL_HAND_POSE) == nullptr);
    }

    template<> template<>
    void llcharacter_object::test<13>()
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
        ensure("the motion is still easing in", easing_in < 0.5f);

        mCharacter.stopMotion(id);
        runFrame();

        ensure("the last update does not raise the weight",
               motion->getPose()->getWeight() <= easing_in);
        ensure("the motion is still around for its last update",
               activeMotions(LLMotion::NORMAL_BLEND).size() == 1u);

        runFrame();
        ensure("and is gone the frame after", activeMotions(LLMotion::NORMAL_BLEND).empty());
    }

    template<> template<>
    void llcharacter_object::test<14>()
    {
        // The mask that lets a motion's joints go unread is built from motions
        // at full weight and no others. One still easing in has claimed
        // nothing, so what is under it is still blended and still shows.
        LLJoint* joint = mCharacter.getJoint("mPelvis");
        ensure("the joint resolves", joint != nullptr);

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

        ensure("the one easing in has not reached full weight",
               over->getPose()->getWeight() < 0.5f);
        ensure("so the one underneath is what the joint shows",
               fabsf(dot(joint->getRotation(), under_rot)) > 0.99f);

        // and once it is all the way in, it owns the joint
        over->mEaseInDuration = 0.f;
        runFrame();

        ensure_equals("it is at full weight now", over->getPose()->getWeight(), 1.f);
        ensure("and the joint is what it asked for",
               fabsf(dot(joint->getRotation(), over_rot)) > 0.99f);
    }

    template<> template<>
    void llcharacter_object::test<15>()
    {
        // The sweep applies a parameter whose weight has moved and leaves the
        // rest alone, and says whether it did anything.
        ALTestVisualParam* first = addParam(1, "First");
        ALTestVisualParam* second = addParam(2, "Second");

        // A parameter arrives at its default weight, which is the weight it
        // was last applied at, so there is nothing to do for either of them.
        ensure("a sweep with nothing to do says so", !mCharacter.updateVisualParams());
        ensure_equals("and applies nothing", first->mApplied, 0);
        ensure_equals("to either of them", second->mApplied, 0);

        first->setWeight(0.5f);
        ensure("a sweep after a weight moves says so", mCharacter.updateVisualParams());
        ensure_equals("the parameter that moved is applied", first->mApplied, 1);
        ensure_equals("and the one that did not is not", second->mApplied, 0);

        ensure("and once it has settled again there is nothing to do",
               !mCharacter.updateVisualParams());
        ensure_equals("with nothing applied a second time", first->mApplied, 1);

        // Both of them at once.
        first->setWeight(0.25f);
        second->setWeight(0.75f);
        ensure("a sweep with two to do says so", mCharacter.updateVisualParams());
        ensure_equals("and applies the first", first->mApplied, 2);
        ensure_equals("and the second", second->mApplied, 1);
    }

    template<> template<>
    void llcharacter_object::test<16>()
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
        ensure("an animating parameter is not swept", !mCharacter.updateVisualParams());
        ensure_equals("and not applied", animating->mApplied, settled);

        animating->setAnimating(false);
        ensure("once it stops animating the sweep takes it", mCharacter.updateVisualParams());

        // The female parameter settled at its default under a male sex, so
        // moving its weight changes nothing the sweep can see.
        const S32 female_settled = female->mApplied;
        female->setWeight(0.75f);
        ensure("a parameter for the other sex has nothing to apply",
               !mCharacter.updateVisualParams());
        ensure_equals("and is not applied", female->mApplied, female_settled);
    }

    template<> template<>
    void llcharacter_object::test<17>()
    {
        // Parameters are found by name whatever case the name is asked in,
        // and the sweep walks them in the order of their ids however they
        // arrived, because that is the order they used to be applied in.
        addParam(30, "Third");
        addParam(10, "First");
        addParam(20, "Second");

        ensure_equals("every parameter is counted", mCharacter.getVisualParamCount(), 3);

        std::vector<S32> ids;
        for (LLVisualParam* param = mCharacter.getFirstVisualParam();
             param;
             param = mCharacter.getNextVisualParam())
        {
            ids.push_back(param->getID());
        }
        ensure_equals("all three are walked", ids.size(), 3u);
        ensure_equals("in id order, not the order they arrived", ids[0], 10);
        ensure_equals("second", ids[1], 20);
        ensure_equals("third", ids[2], 30);

        LLVisualParam* by_name = mCharacter.getVisualParam("Second");
        ensure("a parameter is found by its name", by_name != nullptr);
        ensure_equals("and it is the right one", by_name->getID(), 20);

        ensure_equals("the name is matched without regard to case",
                      mCharacter.getVisualParam("sEcOnD"), by_name);
        ensure_equals("in either direction",
                      mCharacter.getVisualParam("SECOND"), by_name);
        ensure("a name nobody has is not found",
               mCharacter.getVisualParam("NotAParameter") == nullptr);

        // And by id, which is the other way in.
        ensure_equals("a parameter is found by its id",
                      mCharacter.getVisualParam(20), by_name);

        ensure("setting a weight by name finds it",
               mCharacter.setVisualParamWeight("SeCoNd", 0.25f));
        ensure_approximately_equals("and sets it",
                                    mCharacter.getVisualParamWeight("second"), 0.25f, 16);
        ensure("setting one nobody has does not",
               !mCharacter.setVisualParamWeight("NotAParameter", 0.25f));
    }

    template<> template<>
    void llcharacter_object::test<18>()
    {
        // A parameter added under an id that is already taken replaces the one
        // there, in the list the sweep walks as well as in the map.
        ALTestVisualParam* first = addParam(7, "First");
        ALTestVisualParam* second = addParam(7, "Second");

        ensure_equals("the second one took the first one's place",
                      mCharacter.getVisualParamCount(), 1);
        ensure_equals("and it is the one that is found",
                      mCharacter.getVisualParam(7), (LLVisualParam*)second);

        S32 walked = 0;
        for (LLVisualParam* param = mCharacter.getFirstVisualParam();
             param;
             param = mCharacter.getNextVisualParam())
        {
            ensure_equals("the list holds the one that is found",
                          param, (LLVisualParam*)second);
            walked++;
        }
        ensure_equals("once", walked, 1);

        // The one it replaced is nobody's now, so the character will not free
        // it and this test has to.
        delete first;
    }
}
