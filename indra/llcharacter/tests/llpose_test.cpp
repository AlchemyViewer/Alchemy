/**
 * @file llpose_test.cpp
 * @brief Unit tests for LLPose, LLJointStateBlender and LLPoseBlender.
 *
 * Pins the blending semantics later optimisation passes must preserve: the
 * per-joint dedup in LLPose::addJointState, priority ordering in the blender
 * slots, normal versus additive weighting, and the clear-after-apply protocol.
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

#include "llpose.h"
#include "lljoint.h"
#include "lljointstate.h"
#include "llquaternion.h"
#include "v3math.h"

#include "altestmotion.h"

#include "../test/lltut.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    const F32 TOLERANCE = 1e-5f;

    void ensure_vec3_equals(const char* msg, const LLVector3& actual, const LLVector3& expected)
    {
        tut::ensure_approximately_equals_range(msg, actual.mV[VX], expected.mV[VX], TOLERANCE);
        tut::ensure_approximately_equals_range(msg, actual.mV[VY], expected.mV[VY], TOLERANCE);
        tut::ensure_approximately_equals_range(msg, actual.mV[VZ], expected.mV[VZ], TOLERANCE);
    }

    // q and -q are the same rotation, so compare through the absolute dot product.
    void ensure_quat_equals(const char* msg, const LLQuaternion& actual, const LLQuaternion& expected)
    {
        tut::ensure_approximately_equals_range(msg, fabsf(dot(actual, expected)), 1.f, TOLERANCE);
    }

    LLPointer<LLJointState> make_state(LLJoint* joint, U32 usage, F32 weight = 1.f,
                                       LLJoint::JointPriority priority = LLJoint::USE_MOTION_PRIORITY)
    {
        LLPointer<LLJointState> state = new LLJointState(joint);
        state->setUsage(usage);
        state->setWeight(weight);
        state->setPriority(priority);
        return state;
    }

    const LLQuaternion ROT_A(0.5f, LLVector3::x_axis);
    const LLQuaternion ROT_B(1.2f, LLVector3::y_axis);
    const LLQuaternion ROT_C(-0.8f, LLVector3::z_axis);
}

namespace tut
{
    struct joints
    {
        LLJoint mA;
        LLJoint mB;
        LLJoint mC;

        joints()
        {
            mA.setup("a");
            mA.setJointNum(0);
            mB.setup("b");
            mB.setJointNum(1);
            mC.setup("c");
            mC.setJointNum(2);
        }
    };

    //-------------------------------------------------------------------------
    // LLPose
    //-------------------------------------------------------------------------
    struct llpose_data : public joints
    {
        LLPose mPose;
    };
    typedef test_group<llpose_data> llpose_test;
    typedef llpose_test::object llpose_object;
    tut::llpose_test llpose_testcase("LLPose");

    template<> template<>
    void llpose_object::test<1>()
    {
        ensure_equals("empty pose has no states", mPose.getNumJointStates(), 0);
        ensure("empty pose has nothing to iterate", mPose.getJointStates().empty());
        ensure("empty pose finds nothing by joint", mPose.findJointState(&mA) == nullptr);
        ensure("empty pose finds nothing by name", mPose.findJointState(std::string("a")) == nullptr);
        ensure_equals("empty pose weight", mPose.getWeight(), 0.f);
    }

    template<> template<>
    void llpose_object::test<2>()
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        ensure("addJointState returns true", mPose.addJointState(sa));
        ensure_equals("one state", mPose.getNumJointStates(), 1);
        ensure("find by joint", mPose.findJointState(&mA) == sa.get());
        ensure("find by name", mPose.findJointState(std::string("a")) == sa.get());
        ensure("other joint absent", mPose.findJointState(&mB) == nullptr);
        ensure_equals("one state to iterate", mPose.getJointStates().size(), (size_t)1);
        ensure("the state to iterate is the one added", mPose.getJointStates()[0].get() == sa.get());
    }

    template<> template<>
    void llpose_object::test<3>()
    {
        // Two states on the same joint collapse to one; the first added wins.
        LLPointer<LLJointState> first = make_state(&mA, LLJointState::ROT);
        LLPointer<LLJointState> second = make_state(&mA, LLJointState::POS);
        mPose.addJointState(first);
        mPose.addJointState(second);
        ensure_equals("same joint dedups", mPose.getNumJointStates(), 1);
        ensure("first state kept", mPose.findJointState(&mA) == first.get());
    }

    template<> template<>
    void llpose_object::test<4>()
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        LLPointer<LLJointState> sb = make_state(&mB, LLJointState::ROT);
        mPose.addJointState(sa);
        mPose.addJointState(sb);
        ensure_equals("two states", mPose.getNumJointStates(), 2);

        mPose.removeJointState(sa);
        ensure_equals("one left after remove", mPose.getNumJointStates(), 1);
        ensure("removed state gone", mPose.findJointState(&mA) == nullptr);
        ensure("other state stays", mPose.findJointState(&mB) == sb.get());

        mPose.addJointState(sa);
        ensure_equals("re-add after remove", mPose.getNumJointStates(), 2);

        mPose.removeAllJointStates();
        ensure_equals("removeAll empties", mPose.getNumJointStates(), 0);
        ensure("nothing to iterate", mPose.getJointStates().empty());
    }

    template<> template<>
    void llpose_object::test<5>()
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        LLPointer<LLJointState> sb = make_state(&mB, LLJointState::ROT);
        LLPointer<LLJointState> sc = make_state(&mC, LLJointState::ROT);
        mPose.addJointState(sa);
        mPose.addJointState(sb);
        mPose.addJointState(sc);

        std::vector<LLJointState*> seen;
        for (const LLPointer<LLJointState>& state : mPose.getJointStates())
        {
            seen.push_back(state.get());
        }
        ensure_equals("iteration visits every state", seen.size(), (size_t)3);
        ensure("iteration visits a", std::count(seen.begin(), seen.end(), sa.get()) == 1);
        ensure("iteration visits b", std::count(seen.begin(), seen.end(), sb.get()) == 1);
        ensure("iteration visits c", std::count(seen.begin(), seen.end(), sc.get()) == 1);
    }

    template<> template<>
    void llpose_object::test<6>()
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT, 0.f);
        LLPointer<LLJointState> sb = make_state(&mB, LLJointState::ROT, 0.f);
        mPose.addJointState(sa);
        mPose.addJointState(sb);

        mPose.setWeight(0.5f);
        ensure_equals("pose weight stored", mPose.getWeight(), 0.5f);
        ensure_equals("weight reaches a", sa->getWeight(), 0.5f);
        ensure_equals("weight reaches b", sb->getWeight(), 0.5f);

        // A state added after setWeight is brought up to the pose's weight.
        // Every state carrying the pose weight is what lets setWeight skip
        // the walk when the weight has not changed; left at its own weight,
        // a late state would sit there until the next change.
        LLPointer<LLJointState> sc = make_state(&mC, LLJointState::ROT, 0.f);
        mPose.addJointState(sc);
        ensure_equals("late state receives the pose weight", sc->getWeight(), 0.5f);

        mPose.setWeight(1.f);
        ensure_equals("second setWeight reaches a", sa->getWeight(), 1.f);
        ensure_equals("second setWeight reaches b", sb->getWeight(), 1.f);
        ensure_equals("second setWeight reaches c", sc->getWeight(), 1.f);
    }

    template<> template<>
    void llpose_object::test<7>()
    {
        // The poser's runtime path: find the state on a joint, remove it, add
        // another for the same joint. Dedup is by joint, so two states on
        // one joint are still one entry; find by name still reaches it; and
        // removing a state that is not there is harmless.
        LLPointer<LLJointState> first = make_state(&mA, LLJointState::ROT);
        LLPointer<LLJointState> second = make_state(&mA, LLJointState::POS);
        LLPointer<LLJointState> other = make_state(&mB, LLJointState::ROT);
        mPose.addJointState(first);
        mPose.addJointState(second);
        mPose.addJointState(other);
        ensure_equals("two states on one joint are one entry", mPose.getNumJointStates(), 2);
        ensure("found by joint", mPose.findJointState(&mA) == first.get());
        ensure("found by name", mPose.findJointState("a") == first.get());
        ensure("the other joint is found by name too", mPose.findJointState("b") == other.get());
        ensure("an unknown name finds nothing", mPose.findJointState("nobody") == nullptr);

        mPose.removeJointState(mPose.findJointState(&mA));
        ensure_equals("removed by the state found", mPose.getNumJointStates(), 1);
        ensure("the joint is gone", mPose.findJointState(&mA) == nullptr);
        mPose.removeJointState(second);
        ensure_equals("removing an absent state changes nothing", mPose.getNumJointStates(), 1);

        mPose.addJointState(second);
        ensure("the joint can be re-added with a different state", mPose.findJointState(&mA) == second.get());
    }

    //-------------------------------------------------------------------------
    // LLJointStateBlender
    //-------------------------------------------------------------------------
    struct lljointstateblender_data : public joints
    {
        LLJointStateBlender mBlender;
    };
    typedef test_group<lljointstateblender_data> lljointstateblender_test;
    typedef lljointstateblender_test::object lljointstateblender_object;
    tut::lljointstateblender_test lljointstateblender_testcase("LLJointStateBlender");

    template<> template<>
    void lljointstateblender_object::test<1>()
    {
        LLPointer<LLJointState> orphan = make_state(nullptr, LLJointState::ROT);
        ensure("state without a joint is refused", !mBlender.addJointState(orphan, LLJoint::MEDIUM_PRIORITY, false));

        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        ensure("state with a joint is accepted", mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false));
    }

    template<> template<>
    void lljointstateblender_object::test<2>()
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::POS | LLJointState::ROT | LLJointState::SCALE);
        sa->setPosition(LLVector3(1.f, 2.f, 3.f));
        sa->setRotation(ROT_A);
        sa->setScale(LLVector3(2.f, 2.f, 2.f));
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);

        mBlender.blendJointStates();

        ensure_vec3_equals("single state position", mA.getPosition(), LLVector3(1.f, 2.f, 3.f));
        ensure_quat_equals("single state rotation", mA.getRotation(), ROT_A);
        ensure_vec3_equals("single state scale", mA.getScale(), LLVector3(2.f, 2.f, 2.f));
    }

    template<> template<>
    void lljointstateblender_object::test<3>()
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::POS | LLJointState::ROT, 0.f);
        sa->setPosition(LLVector3(1.f, 2.f, 3.f));
        sa->setRotation(ROT_A);
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);

        mBlender.blendJointStates();

        ensure_vec3_equals("zero-weight state leaves position", mA.getPosition(), LLVector3::zero);
        ensure_quat_equals("zero-weight state leaves rotation", mA.getRotation(), LLQuaternion::DEFAULT);
    }

    template<> template<>
    void lljointstateblender_object::test<4>()
    {
        // Higher priority wins at full weight regardless of insertion order.
        LLPointer<LLJointState> low = make_state(&mA, LLJointState::ROT);
        low->setRotation(ROT_A);
        LLPointer<LLJointState> high = make_state(&mA, LLJointState::ROT);
        high->setRotation(ROT_B);

        mBlender.addJointState(low, LLJoint::LOW_PRIORITY, false);
        mBlender.addJointState(high, LLJoint::HIGH_PRIORITY, false);
        mBlender.blendJointStates();
        ensure_quat_equals("high after low", mA.getRotation(), ROT_B);

        mA.setRotation(LLQuaternion::DEFAULT);
        mBlender.addJointState(high, LLJoint::HIGH_PRIORITY, false);
        mBlender.addJointState(low, LLJoint::LOW_PRIORITY, false);
        mBlender.blendJointStates();
        ensure_quat_equals("low after high", mA.getRotation(), ROT_B);

        // At equal priority the state added first stays in front.
        mA.setRotation(LLQuaternion::DEFAULT);
        mBlender.addJointState(low, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.addJointState(high, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        ensure_quat_equals("equal priority keeps the first", mA.getRotation(), ROT_A);
    }

    template<> template<>
    void lljointstateblender_object::test<5>()
    {
        // Two half-weight positions at the same priority meet in the middle.
        LLPointer<LLJointState> first = make_state(&mA, LLJointState::POS, 0.5f);
        first->setPosition(LLVector3(2.f, 0.f, 0.f));
        LLPointer<LLJointState> second = make_state(&mA, LLJointState::POS, 0.5f);
        second->setPosition(LLVector3(0.f, 4.f, 0.f));

        mBlender.addJointState(first, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.addJointState(second, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        ensure_vec3_equals("half and half", mA.getPosition(), LLVector3(1.f, 2.f, 0.f));
    }

    template<> template<>
    void lljointstateblender_object::test<6>()
    {
        LLPointer<LLJointState> base = make_state(&mA, LLJointState::POS | LLJointState::ROT | LLJointState::SCALE);
        base->setPosition(LLVector3(0.f, 1.f, 0.f));
        base->setRotation(ROT_A);
        base->setScale(LLVector3(1.f, 1.f, 1.f));

        LLPointer<LLJointState> add = make_state(&mA, LLJointState::POS | LLJointState::ROT | LLJointState::SCALE);
        add->setPosition(LLVector3(1.f, 0.f, 0.f));
        add->setRotation(ROT_B);
        add->setScale(LLVector3(0.5f, 0.f, 0.f));

        mBlender.addJointState(base, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.addJointState(add, LLJoint::ADDITIVE_PRIORITY, true);
        mBlender.blendJointStates();

        ensure_vec3_equals("additive position sums", mA.getPosition(), LLVector3(1.f, 1.f, 0.f));
        ensure_quat_equals("additive rotation composes", mA.getRotation(), ROT_B * ROT_A);
        ensure_vec3_equals("additive scale sums", mA.getScale(), LLVector3(1.5f, 1.f, 1.f));
    }

    template<> template<>
    void lljointstateblender_object::test<7>()
    {
        // blendJointStates() consumes its slots: a second call with nothing added is a no-op.
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        sa->setRotation(ROT_A);
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        ensure_quat_equals("first blend applies", mA.getRotation(), ROT_A);

        mA.setRotation(ROT_C);
        mBlender.blendJointStates();
        ensure_quat_equals("empty blend leaves the joint alone", mA.getRotation(), ROT_C);

        // clear() drops queued states without applying them.
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.clear();
        mBlender.blendJointStates();
        ensure_quat_equals("cleared blend leaves the joint alone", mA.getRotation(), ROT_C);
    }

    template<> template<>
    void lljointstateblender_object::test<8>()
    {
        std::vector<LLPointer<LLJointState>> states;
        for (S32 i = 0; i < JSB_NUM_JOINT_STATES; ++i)
        {
            states.push_back(make_state(&mA, LLJointState::ROT));
            ensure("slot accepted", mBlender.addJointState(states.back(), LLJoint::MEDIUM_PRIORITY, false));
        }

        LLPointer<LLJointState> lower = make_state(&mA, LLJointState::ROT);
        ensure("full blender refuses a lower priority", !mBlender.addJointState(lower, LLJoint::LOW_PRIORITY, false));

        LLPointer<LLJointState> higher = make_state(&mA, LLJointState::ROT);
        higher->setRotation(ROT_B);
        ensure("full blender takes a higher priority", mBlender.addJointState(higher, LLJoint::HIGH_PRIORITY, false));

        mBlender.blendJointStates();
        ensure_quat_equals("the higher priority lands in front", mA.getRotation(), ROT_B);
    }

    template<> template<>
    void lljointstateblender_object::test<9>()
    {
        // A channel no joint state contributes to holds the value read from
        // the joint, so writing it back is a no-op that still dirties the
        // joint and, through touch(), its whole subtree. Scale is observable
        // on its own: setScale dirties with ALL_DIRTY, so it is the only one
        // of the three writes that can raise POSITION_DIRTY while the position
        // itself is unchanged.
        const LLVector3 scale(2.f, 3.f, 4.f);
        mA.setScale(scale);
        mA.updateWorldMatrix();
        ensure("joint starts clean", mA.mDirtyFlags == 0);

        LLPointer<LLJointState> rot_only = make_state(&mA, LLJointState::ROT);
        rot_only->setRotation(ROT_A);
        mBlender.addJointState(rot_only, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        ensure_vec3_equals("rotation-only blend leaves scale alone", mA.getScale(), scale);
        ensure_quat_equals("rotation-only blend still applies rotation", mA.getRotation(), ROT_A);
        ensure("rotation-only blend does not dirty position",
               (mA.mDirtyFlags & LLJoint::POSITION_DIRTY) == 0);
        ensure("rotation-only blend does dirty rotation",
               (mA.mDirtyFlags & LLJoint::ROTATION_DIRTY) != 0);
    }

    template<> template<>
    void lljointstateblender_object::test<10>()
    {
        // A state that does carry SCALE still writes, including when it is the
        // only channel in use.
        const LLVector3 scale(5.f, 6.f, 7.f);
        LLPointer<LLJointState> scale_only = make_state(&mA, LLJointState::SCALE);
        scale_only->setScale(scale);
        mBlender.addJointState(scale_only, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        ensure_vec3_equals("scale-only blend applies scale", mA.getScale(), scale);
    }

    template<> template<>
    void lljointstateblender_object::test<11>()
    {
        // A non-finite scale contribution must never reach the joint. Two
        // layers enforce this, the reset in blendJointStates and the one in
        // LLXform::setScale, and the second is why a joint can be assumed to
        // hold a finite scale in the first place.
        const F32 inf = std::numeric_limits<F32>::infinity();
        LLPointer<LLJointState> bad_scale = make_state(&mA, LLJointState::SCALE);
        bad_scale->setScale(LLVector3(inf, inf, inf));
        mBlender.addJointState(bad_scale, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        ensure("joint scale stays finite", mA.getScale().isFinite());
        ensure_vec3_equals("non-finite scale is reset", mA.getScale(), LLVector3(1.f, 1.f, 1.f));
    }

    template<> template<>
    void lljointstateblender_object::test<12>()
    {
        // The payoff of the scale skip and the rotation compare together: a
        // motion holding a pose blends the same values every frame, and must
        // stop dirtying the joint once the first frame has been applied.
        // Either guard alone leaves the other write dirtying it.
        LLPointer<LLJointState> held = make_state(&mA, LLJointState::ROT);
        held->setRotation(ROT_A);

        mBlender.addJointState(held, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        ensure_quat_equals("first blend applies the pose", mA.getRotation(), ROT_A);

        mA.updateWorldMatrix();
        ensure("joint is clean once the pose has been applied", mA.mDirtyFlags == 0);

        mBlender.addJointState(held, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        ensure("holding the pose does not dirty the joint again", mA.mDirtyFlags == 0);
    }

    template<> template<>
    void lljointstateblender_object::test<13>()
    {
        // There are six slots. Filling them refuses anything of lower or
        // equal priority, and applying the blend hands them all back.
        std::vector<LLPointer<LLJointState> > states;
        for (S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
        {
            states.push_back(make_state(&mA, LLJointState::ROT));
            states.back()->setRotation(ROT_B);
            ensure("a free slot takes the state",
                   mBlender.addJointState(states.back(), LLJoint::HIGH_PRIORITY, false));
        }

        LLPointer<LLJointState> equal = make_state(&mA, LLJointState::ROT);
        ensure("a full blender refuses an equal priority",
               !mBlender.addJointState(equal, LLJoint::HIGH_PRIORITY, false));

        LLPointer<LLJointState> higher = make_state(&mA, LLJointState::ROT);
        higher->setRotation(ROT_A);
        ensure("a full blender takes a higher priority",
               mBlender.addJointState(higher, LLJoint::HIGHEST_PRIORITY, false));

        mBlender.blendJointStates();
        ensure_quat_equals("the highest priority won", mA.getRotation(), ROT_A);

        // Applying releases every slot, so the next frame starts empty rather
        // than inheriting six states that are already spoken for.
        LLPointer<LLJointState> next = make_state(&mA, LLJointState::ROT);
        next->setRotation(ROT_C);
        ensure("the slots came back", mBlender.addJointState(next, LLJoint::LOW_PRIORITY, false));
        mBlender.blendJointStates();
        ensure_quat_equals("the next frame blends only its own state", mA.getRotation(), ROT_C);
    }

    //-------------------------------------------------------------------------
    // LLPoseBlender
    //-------------------------------------------------------------------------
    struct llposeblender_data : public joints
    {
        LLPoseBlender mBlender;

        static void arm(ALTestMotion& motion) { motion.getPose()->setWeight(1.f); }
    };
    typedef test_group<llposeblender_data> llposeblender_test;
    typedef llposeblender_test::object llposeblender_object;
    tut::llposeblender_test llposeblender_testcase("LLPoseBlender");

    template<> template<>
    void llposeblender_object::test<1>()
    {
        ALTestMotion motion;
        LLPointer<LLJointState> sa = motion.addJoint(&mA, LLJointState::ROT);
        sa->setRotation(ROT_A);
        LLPointer<LLJointState> sb = motion.addJoint(&mB, LLJointState::POS);
        sb->setPosition(LLVector3(3.f, 2.f, 1.f));
        arm(motion);

        ensure("addMotion returns true", mBlender.addMotion(&motion));
        mBlender.blendAndApply();

        ensure_quat_equals("motion rotation applied", mA.getRotation(), ROT_A);
        ensure_vec3_equals("motion position applied", mB.getPosition(), LLVector3(3.f, 2.f, 1.f));
    }

    template<> template<>
    void llposeblender_object::test<2>()
    {
        ALTestMotion low(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        low.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(low);

        ALTestMotion high(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY);
        high.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(high);

        mBlender.addMotion(&low);
        mBlender.addMotion(&high);
        mBlender.blendAndApply();
        ensure_quat_equals("higher motion priority wins, low first", mA.getRotation(), ROT_B);

        mA.setRotation(LLQuaternion::DEFAULT);
        mBlender.addMotion(&high);
        mBlender.addMotion(&low);
        mBlender.blendAndApply();
        ensure_quat_equals("higher motion priority wins, high first", mA.getRotation(), ROT_B);
    }

    template<> template<>
    void llposeblender_object::test<3>()
    {
        // A joint state's own priority overrides its motion's.
        ALTestMotion low(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        low.addJoint(&mA, LLJointState::ROT, LLJoint::HIGHEST_PRIORITY)->setRotation(ROT_A);
        arm(low);

        ALTestMotion medium(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY);
        medium.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(medium);

        mBlender.addMotion(&medium);
        mBlender.addMotion(&low);
        mBlender.blendAndApply();
        ensure_quat_equals("joint state priority beats motion priority", mA.getRotation(), ROT_A);
    }

    template<> template<>
    void llposeblender_object::test<7>()
    {
        // The pool is indexed by joint number. A joint that never received one
        // -- LLJoint::init leaves it at -1 -- is in the motion's pose, since
        // LLMotion::addJointState inserts before it checks the range, but has
        // nowhere to go in the pool. It is left alone; the rest of the motion
        // is not.
        LLJoint unnumbered;
        unnumbered.setup("unnumbered");
        ensure_equals("a fresh joint has no number", unnumbered.getJointNum(), -1);
        const LLQuaternion before = unnumbered.getRotation();

        ALTestMotion motion;
        motion.addJoint(&unnumbered, LLJointState::ROT)->setRotation(ROT_B);
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        mBlender.addMotion(&motion);
        mBlender.blendAndApply();
        ensure_quat_equals("the unnumbered joint is not blended", unnumbered.getRotation(), before);
        ensure_quat_equals("the numbered joint in the same motion is", mA.getRotation(), ROT_A);
    }

    template<> template<>
    void llposeblender_object::test<8>()
    {
        // Both ends of the pool: the first slot and the highest number a real
        // joint can carry. setJointNum refuses the top two, which belong to
        // the synthetic hand and face joints.
        LLJoint last;
        last.setup("last");
        last.setJointNum(LL_CHARACTER_MAX_ANIMATED_JOINTS - 3);

        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        motion.addJoint(&last, LLJointState::ROT)->setRotation(ROT_C);
        arm(motion);

        mBlender.addMotion(&motion);
        mBlender.blendAndApply();
        ensure_quat_equals("slot zero blends", mA.getRotation(), ROT_A);
        ensure_quat_equals("the last slot blends", last.getRotation(), ROT_C);
    }

    template<> template<>
    void llposeblender_object::test<9>()
    {
        // The coarse clock's path: blend into the cache without touching the
        // joint, then interpolate the joint toward it. The cache used to be a
        // whole LLJoint per blender so the blend could be aimed at it through
        // the joint interface; it is three vectors now, and has to hold the
        // same blend.
        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT | LLJointState::POS)->setRotation(ROT_A);
        motion.getPose()->findJointState(&mA)->setPosition(LLVector3(2.f, 4.f, 6.f));
        arm(motion);

        mBlender.addMotion(&motion);
        mBlender.blendAndCache(true);
        ensure_quat_equals("caching leaves the joint's rotation alone", mA.getRotation(), LLQuaternion::DEFAULT);
        ensure_vec3_equals("caching leaves the joint's position alone", mA.getPosition(), LLVector3::zero);

        mBlender.interpolate(0.5f);
        ensure_vec3_equals("half way toward the cached position", mA.getPosition(), LLVector3(1.f, 2.f, 3.f));

        mBlender.interpolate(1.f);
        ensure_vec3_equals("at the cached position", mA.getPosition(), LLVector3(2.f, 4.f, 6.f));
        ensure_quat_equals("at the cached rotation", mA.getRotation(), ROT_A);

        // and once the blenders are cleared, interpolating moves nothing
        mBlender.clearBlenders();
        mA.setPosition(LLVector3::zero);
        mBlender.interpolate(1.f);
        ensure_vec3_equals("a cleared blender no longer drives the joint", mA.getPosition(), LLVector3::zero);
    }

    template<> template<>
    void llposeblender_object::test<10>()
    {
        // Interpolating toward the cache writes every channel of the joint,
        // and the channels the blend never touched arrive unchanged. A write
        // that changes nothing must not dirty the joint, or every joint on
        // the coarse clock is rebuilt every frame for a rotation-only blend.
        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        mBlender.addMotion(&motion);
        mBlender.blendAndCache(true);
        mA.updateWorldMatrixChildren();
        ensure("the joint starts clean", mA.mDirtyFlags == 0);

        mBlender.interpolate(0.5f);
        ensure("the rotation moved, so the matrix is dirty", (mA.mDirtyFlags & LLJoint::MATRIX_DIRTY) != 0);
        ensure("an unchanged position and scale dirty nothing of their own",
               (mA.mDirtyFlags & LLJoint::POSITION_DIRTY) == 0);
    }

    template<> template<>
    void llposeblender_object::test<4>()
    {
        // The controller feeds additive motions to the blender before normal
        // ones. At equal priority the earlier state stays in front, and an
        // additive state only contributes while the states ahead of it have
        // not already saturated the weight.
        ALTestMotion base(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY, LLMotion::NORMAL_BLEND);
        base.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(base);

        ALTestMotion additive(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY, LLMotion::ADDITIVE_BLEND);
        additive.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(additive);

        mBlender.addMotion(&additive);
        mBlender.addMotion(&base);
        mBlender.blendAndApply();
        ensure_quat_equals("additive ahead of the base composes onto it", mA.getRotation(), ROT_B * ROT_A);

        mA.setRotation(LLQuaternion::DEFAULT);
        mBlender.addMotion(&base);
        mBlender.addMotion(&additive);
        mBlender.blendAndApply();
        ensure_quat_equals("additive behind a saturated base is masked", mA.getRotation(), ROT_A);
    }

    template<> template<>
    void llposeblender_object::test<5>()
    {
        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        mBlender.addMotion(&motion);
        mBlender.blendAndApply();
        ensure_quat_equals("first apply", mA.getRotation(), ROT_A);

        // The active list is consumed by blendAndApply().
        mA.setRotation(ROT_C);
        mBlender.blendAndApply();
        ensure_quat_equals("second apply without addMotion is a no-op", mA.getRotation(), ROT_C);

        // clearBlenders() discards queued states.
        mBlender.addMotion(&motion);
        mBlender.clearBlenders();
        mBlender.blendAndApply();
        ensure_quat_equals("cleared queue is a no-op", mA.getRotation(), ROT_C);
    }

    template<> template<>
    void llposeblender_object::test<6>()
    {
        // A joint that never received a joint number is kept in the pose but
        // left out of the motion's joint signature.
        LLJoint unnumbered;
        unnumbered.setup("unnumbered");
        ensure_equals("default joint number", unnumbered.getJointNum(), -1);

        ALTestMotion motion;
        motion.addJoint(&unnumbered, LLJointState::ROT);
        ensure_equals("unnumbered joint stays in the pose", motion.getPose()->getNumJointStates(), 1);
    }

    template<> template<>
    void llposeblender_object::test<11>()
    {
        // A joint has six blend slots and they go by priority. A motion at
        // zero weight contributes nothing whatever slot it lands in, so
        // letting it take one costs the joint a motion that had something to
        // say. Six of them easing in or out over a transition is enough to
        // shut out the animation underneath for a frame.
        std::vector<std::unique_ptr<ALTestMotion> > silent;
        for (S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
        {
            silent.push_back(std::make_unique<ALTestMotion>(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY));
            silent.back()->addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
            // left at the weight a pose starts and ends its life on
            ensure_equals("a silent motion weighs nothing", silent.back()->getPose()->getWeight(), 0.f);
            mBlender.addMotion(silent.back().get());
        }

        ALTestMotion playing(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        playing.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(playing);
        mBlender.addMotion(&playing);

        mBlender.blendAndApply();
        ensure_quat_equals("the motion with a weight still reaches the joint", mA.getRotation(), ROT_A);
    }
}
