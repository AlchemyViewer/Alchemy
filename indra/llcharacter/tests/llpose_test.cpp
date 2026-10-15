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

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

namespace
{
    const F32 TOLERANCE = 1e-5f;

    void require_vec3_near(const char* what, const LLVector3& actual, const LLVector3& expected)
    {
        INFO(what);
        REQUIRE_THAT(actual.mV[VX], Catch::Matchers::WithinAbs(expected.mV[VX], TOLERANCE));
        REQUIRE_THAT(actual.mV[VY], Catch::Matchers::WithinAbs(expected.mV[VY], TOLERANCE));
        REQUIRE_THAT(actual.mV[VZ], Catch::Matchers::WithinAbs(expected.mV[VZ], TOLERANCE));
    }

    // q and -q are the same rotation, so compare through the absolute dot product.
    void require_same_rotation(const char* what, const LLQuaternion& actual, const LLQuaternion& expected)
    {
        INFO(what);
        REQUIRE_THAT(fabsf(dot(actual, expected)), Catch::Matchers::WithinAbs(1.f, TOLERANCE));
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

namespace
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
    // LLJointState
    //-------------------------------------------------------------------------
    struct lljointstate_data : public joints
    {
    };

    TEST_CASE_METHOD(lljointstate_data, "LLJointState: a fresh state has no usage or weight and default channels", "[LLJointState]")
    {
        // What a fresh state holds. The blender seeds a channel from the joint
        // when nothing contributes to it, so these only ever reach a joint
        // through a state that carries the matching usage, but a state that
        // came back with something else in it would be a silent wrong pose.
        LLPointer<LLJointState> state = new LLJointState(&mA);
        CHECK(state->getUsage() == 0u);
        CHECK(state->getWeight() == 0.f);
        require_vec3_near("position starts at the origin", state->getPosition(), LLVector3::zero);
        require_same_rotation("rotation starts at identity", state->getRotation(), LLQuaternion::DEFAULT);
        require_vec3_near("scale starts at zero", state->getScale(), LLVector3::zero);
    }

    TEST_CASE_METHOD(lljointstate_data, "LLJointState: values set as scalars read back through both views", "[LLJointState]")
    {
        // The two views of a channel are the same channel: written either
        // way, read either way.
        LLPointer<LLJointState> state = new LLJointState(&mA);
        state->setUsage(LLJointState::POS | LLJointState::ROT | LLJointState::SCALE);

        const LLVector3 pos(1.5f, -2.25f, 3.f);
        const LLVector3 scale(2.f, 4.f, 8.f);

        state->setPosition(pos);
        state->setRotation(ROT_A);
        state->setScale(scale);

        require_vec3_near("position read back", state->getPosition(), pos);
        require_same_rotation("rotation read back", state->getRotation(), ROT_A);
        require_vec3_near("scale read back", state->getScale(), scale);

        require_vec3_near("position through the vector view",
                          LLVector3(state->getPositionV().getF32ptr()), pos);
        require_vec3_near("scale through the vector view",
                          LLVector3(state->getScaleV().getF32ptr()), scale);
        LLQuaternion from_vector;
        state->getRotationQ().store(from_vector);
        require_same_rotation("rotation through the vector view", from_vector, ROT_A);
    }

    TEST_CASE_METHOD(lljointstate_data, "LLJointState: values set through the vector view read back as scalars", "[LLJointState]")
    {
        // and written through the vector view, read through the scalar one
        LLPointer<LLJointState> state = new LLJointState(&mA);
        state->setUsage(LLJointState::POS | LLJointState::ROT | LLJointState::SCALE);

        LLVector4a pos;
        pos.set(-4.f, 5.f, 6.f, 0.f);
        LLVector4a scale;
        scale.set(0.5f, 0.25f, 0.125f, 0.f);

        state->setPosition(pos);
        state->setRotation(LLQuaternion2(ROT_B));
        state->setScale(scale);

        require_vec3_near("position set as a vector", state->getPosition(), LLVector3(-4.f, 5.f, 6.f));
        require_same_rotation("rotation set as a quaternion2", state->getRotation(), ROT_B);
        require_vec3_near("scale set as a vector", state->getScale(), LLVector3(0.5f, 0.25f, 0.125f));
    }

    //-------------------------------------------------------------------------
    // LLPose
    //-------------------------------------------------------------------------
    struct llpose_data : public joints
    {
        LLPose mPose;
    };

    //-------------------------------------------------------------------------
    // LLJointStateBlender
    //-------------------------------------------------------------------------
    struct lljointstateblender_data : public joints
    {
        LLJointStateBlender mBlender;
    };

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: addJointState refuses a state with no joint", "[LLJointStateBlender]")
    {
        LLPointer<LLJointState> orphan = make_state(nullptr, LLJointState::ROT);
        CHECK_FALSE(mBlender.addJointState(orphan, LLJoint::MEDIUM_PRIORITY, false));

        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        CHECK(mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false));
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a lone full-weight state is applied to its joint", "[LLJointStateBlender]")
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::POS | LLJointState::ROT | LLJointState::SCALE);
        sa->setPosition(LLVector3(1.f, 2.f, 3.f));
        sa->setRotation(ROT_A);
        sa->setScale(LLVector3(2.f, 2.f, 2.f));
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);

        mBlender.blendJointStates();

        require_vec3_near("single state position", mA.getPosition(), LLVector3(1.f, 2.f, 3.f));
        require_same_rotation("single state rotation", mA.getRotation(), ROT_A);
        require_vec3_near("single state scale", mA.getScale(), LLVector3(2.f, 2.f, 2.f));
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a zero-weight state leaves the joint alone", "[LLJointStateBlender]")
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::POS | LLJointState::ROT, 0.f);
        sa->setPosition(LLVector3(1.f, 2.f, 3.f));
        sa->setRotation(ROT_A);
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);

        mBlender.blendJointStates();

        require_vec3_near("zero-weight state leaves position", mA.getPosition(), LLVector3::zero);
        require_same_rotation("zero-weight state leaves rotation", mA.getRotation(), LLQuaternion::DEFAULT);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: higher priority wins and equal priority keeps the first", "[LLJointStateBlender]")
    {
        // Higher priority wins at full weight regardless of insertion order.
        LLPointer<LLJointState> low = make_state(&mA, LLJointState::ROT);
        low->setRotation(ROT_A);
        LLPointer<LLJointState> high = make_state(&mA, LLJointState::ROT);
        high->setRotation(ROT_B);

        mBlender.addJointState(low, LLJoint::LOW_PRIORITY, false);
        mBlender.addJointState(high, LLJoint::HIGH_PRIORITY, false);
        mBlender.blendJointStates();
        require_same_rotation("high after low", mA.getRotation(), ROT_B);

        mA.setRotation(LLQuaternion::DEFAULT);
        mBlender.addJointState(high, LLJoint::HIGH_PRIORITY, false);
        mBlender.addJointState(low, LLJoint::LOW_PRIORITY, false);
        mBlender.blendJointStates();
        require_same_rotation("low after high", mA.getRotation(), ROT_B);

        // At equal priority the state added first stays in front.
        mA.setRotation(LLQuaternion::DEFAULT);
        mBlender.addJointState(low, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.addJointState(high, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        require_same_rotation("equal priority keeps the first", mA.getRotation(), ROT_A);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: two half-weight positions meet in the middle", "[LLJointStateBlender]")
    {
        // Two half-weight positions at the same priority meet in the middle.
        LLPointer<LLJointState> first = make_state(&mA, LLJointState::POS, 0.5f);
        first->setPosition(LLVector3(2.f, 0.f, 0.f));
        LLPointer<LLJointState> second = make_state(&mA, LLJointState::POS, 0.5f);
        second->setPosition(LLVector3(0.f, 4.f, 0.f));

        mBlender.addJointState(first, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.addJointState(second, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        require_vec3_near("half and half", mA.getPosition(), LLVector3(1.f, 2.f, 0.f));
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: an additive state adds position and scale and composes rotation", "[LLJointStateBlender]")
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

        require_vec3_near("additive position sums", mA.getPosition(), LLVector3(1.f, 1.f, 0.f));
        require_same_rotation("additive rotation composes", mA.getRotation(), ROT_B * ROT_A);
        require_vec3_near("additive scale sums", mA.getScale(), LLVector3(1.5f, 1.f, 1.f));
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: blending or clearing empties the queued states", "[LLJointStateBlender]")
    {
        // blendJointStates() consumes its slots: a second call with nothing added is a no-op.
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        sa->setRotation(ROT_A);
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        require_same_rotation("first blend applies", mA.getRotation(), ROT_A);

        mA.setRotation(ROT_C);
        mBlender.blendJointStates();
        require_same_rotation("empty blend leaves the joint alone", mA.getRotation(), ROT_C);

        // clear() drops queued states without applying them.
        mBlender.addJointState(sa, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.clear();
        mBlender.blendJointStates();
        require_same_rotation("cleared blend leaves the joint alone", mA.getRotation(), ROT_C);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a full blender refuses lower priority and takes higher", "[LLJointStateBlender]")
    {
        std::vector<LLPointer<LLJointState>> states;
        for (S32 i = 0; i < JSB_NUM_JOINT_STATES; ++i)
        {
            states.push_back(make_state(&mA, LLJointState::ROT));
            REQUIRE(mBlender.addJointState(states.back(), LLJoint::MEDIUM_PRIORITY, false));
        }

        LLPointer<LLJointState> lower = make_state(&mA, LLJointState::ROT);
        CHECK_FALSE(mBlender.addJointState(lower, LLJoint::LOW_PRIORITY, false));

        LLPointer<LLJointState> higher = make_state(&mA, LLJointState::ROT);
        higher->setRotation(ROT_B);
        CHECK(mBlender.addJointState(higher, LLJoint::HIGH_PRIORITY, false));

        mBlender.blendJointStates();
        require_same_rotation("the higher priority lands in front", mA.getRotation(), ROT_B);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a rotation-only blend does not dirty the position", "[LLJointStateBlender]")
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
        UNSCOPED_INFO("joint starts clean");
        CHECK(mA.mDirtyFlags == 0);

        LLPointer<LLJointState> rot_only = make_state(&mA, LLJointState::ROT);
        rot_only->setRotation(ROT_A);
        mBlender.addJointState(rot_only, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        require_vec3_near("rotation-only blend leaves scale alone", mA.getScale(), scale);
        require_same_rotation("rotation-only blend still applies rotation", mA.getRotation(), ROT_A);
        CHECK((mA.mDirtyFlags & LLJoint::POSITION_DIRTY) == 0);
        CHECK((mA.mDirtyFlags & LLJoint::ROTATION_DIRTY) != 0);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a scale-only state still writes the scale", "[LLJointStateBlender]")
    {
        // A state that does carry SCALE still writes, including when it is the
        // only channel in use.
        const LLVector3 scale(5.f, 6.f, 7.f);
        LLPointer<LLJointState> scale_only = make_state(&mA, LLJointState::SCALE);
        scale_only->setScale(scale);
        mBlender.addJointState(scale_only, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();

        require_vec3_near("scale-only blend applies scale", mA.getScale(), scale);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a non-finite scale never reaches the joint", "[LLJointStateBlender]")
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

        CHECK(mA.getScale().isFinite());
        require_vec3_near("non-finite scale is reset", mA.getScale(), LLVector3(1.f, 1.f, 1.f));
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: holding a pose stops dirtying the joint", "[LLJointStateBlender]")
    {
        // The payoff of the scale skip and the rotation compare together: a
        // motion holding a pose blends the same values every frame, and must
        // stop dirtying the joint once the first frame has been applied.
        // Either guard alone leaves the other write dirtying it.
        LLPointer<LLJointState> held = make_state(&mA, LLJointState::ROT);
        held->setRotation(ROT_A);

        mBlender.addJointState(held, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        require_same_rotation("first blend applies the pose", mA.getRotation(), ROT_A);

        mA.updateWorldMatrix();
        UNSCOPED_INFO("joint is clean once the pose has been applied");
        CHECK(mA.mDirtyFlags == 0);

        mBlender.addJointState(held, LLJoint::MEDIUM_PRIORITY, false);
        mBlender.blendJointStates();
        UNSCOPED_INFO("holding the pose does not dirty the joint again");
        CHECK(mA.mDirtyFlags == 0);
    }

    TEST_CASE_METHOD(lljointstateblender_data, "LLJointStateBlender: a full blender refuses equal priority and blending frees the slots", "[LLJointStateBlender]")
    {
        // There are six slots. Filling them refuses anything of lower or
        // equal priority, and applying the blend hands them all back.
        std::vector<LLPointer<LLJointState> > states;
        for (S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
        {
            states.push_back(make_state(&mA, LLJointState::ROT));
            states.back()->setRotation(ROT_B);
            REQUIRE(mBlender.addJointState(states.back(), LLJoint::HIGH_PRIORITY, false));
        }

        LLPointer<LLJointState> equal = make_state(&mA, LLJointState::ROT);
        CHECK_FALSE(mBlender.addJointState(equal, LLJoint::HIGH_PRIORITY, false));

        LLPointer<LLJointState> higher = make_state(&mA, LLJointState::ROT);
        higher->setRotation(ROT_A);
        UNSCOPED_INFO("a full blender takes a higher priority");
        CHECK(mBlender.addJointState(higher, LLJoint::HIGHEST_PRIORITY, false));

        mBlender.blendJointStates();
        require_same_rotation("the highest priority won", mA.getRotation(), ROT_A);

        // Applying releases every slot, so the next frame starts empty rather
        // than inheriting six states that are already spoken for.
        LLPointer<LLJointState> next = make_state(&mA, LLJointState::ROT);
        next->setRotation(ROT_C);
        UNSCOPED_INFO("the slots came back");
        CHECK(mBlender.addJointState(next, LLJoint::LOW_PRIORITY, false));
        mBlender.blendJointStates();
        require_same_rotation("the next frame blends only its own state", mA.getRotation(), ROT_C);
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: an empty pose has no states and no weight", "[LLPose]")
    {
        CHECK(mPose.getNumJointStates() == 0);
        CHECK(mPose.getJointStates().empty());
        CHECK(mPose.findJointState(&mA) == nullptr);
        CHECK(mPose.findJointState(std::string("a")) == nullptr);
        CHECK(mPose.getWeight() == 0.f);
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: an added state is counted and found by joint and by name", "[LLPose]")
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        CHECK(mPose.addJointState(sa));
        CHECK(mPose.getNumJointStates() == 1);
        CHECK(mPose.findJointState(&mA) == sa.get());
        CHECK(mPose.findJointState(std::string("a")) == sa.get());
        CHECK(mPose.findJointState(&mB) == nullptr);
        REQUIRE(mPose.getJointStates().size() == (size_t)1);
        CHECK(mPose.getJointStates()[0].get() == sa.get());
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: states on one joint collapse to the first added", "[LLPose]")
    {
        // Two states on the same joint collapse to one; the first added wins.
        LLPointer<LLJointState> first = make_state(&mA, LLJointState::ROT);
        LLPointer<LLJointState> second = make_state(&mA, LLJointState::POS);
        mPose.addJointState(first);
        mPose.addJointState(second);
        CHECK(mPose.getNumJointStates() == 1);
        CHECK(mPose.findJointState(&mA) == first.get());
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: removeJointState and removeAllJointStates drop states", "[LLPose]")
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT);
        LLPointer<LLJointState> sb = make_state(&mB, LLJointState::ROT);
        mPose.addJointState(sa);
        mPose.addJointState(sb);
        CHECK(mPose.getNumJointStates() == 2);

        mPose.removeJointState(sa);
        CHECK(mPose.getNumJointStates() == 1);
        CHECK(mPose.findJointState(&mA) == nullptr);
        CHECK(mPose.findJointState(&mB) == sb.get());

        mPose.addJointState(sa);
        CHECK(mPose.getNumJointStates() == 2);

        mPose.removeAllJointStates();
        CHECK(mPose.getNumJointStates() == 0);
        CHECK(mPose.getJointStates().empty());
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: iterating the states visits each once", "[LLPose]")
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
        CHECK(seen.size() == (size_t)3);
        CHECK(std::count(seen.begin(), seen.end(), sa.get()) == 1);
        CHECK(std::count(seen.begin(), seen.end(), sb.get()) == 1);
        CHECK(std::count(seen.begin(), seen.end(), sc.get()) == 1);
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: setWeight reaches every state including one added later", "[LLPose]")
    {
        LLPointer<LLJointState> sa = make_state(&mA, LLJointState::ROT, 0.f);
        LLPointer<LLJointState> sb = make_state(&mB, LLJointState::ROT, 0.f);
        mPose.addJointState(sa);
        mPose.addJointState(sb);

        mPose.setWeight(0.5f);
        CHECK(mPose.getWeight() == 0.5f);
        CHECK(sa->getWeight() == 0.5f);
        CHECK(sb->getWeight() == 0.5f);

        // A state added after setWeight is brought up to the pose's weight.
        // Every state carrying the pose weight is what lets setWeight skip
        // the walk when the weight has not changed; left at its own weight,
        // a late state would sit there until the next change.
        LLPointer<LLJointState> sc = make_state(&mC, LLJointState::ROT, 0.f);
        mPose.addJointState(sc);
        UNSCOPED_INFO("late state receives the pose weight");
        CHECK(sc->getWeight() == 0.5f);

        mPose.setWeight(1.f);
        CHECK(sa->getWeight() == 1.f);
        CHECK(sb->getWeight() == 1.f);
        CHECK(sc->getWeight() == 1.f);
    }

    TEST_CASE_METHOD(llpose_data, "LLPose: a state found on a joint can be removed and replaced", "[LLPose]")
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
        UNSCOPED_INFO("two states on one joint are one entry");
        CHECK(mPose.getNumJointStates() == 2);
        CHECK(mPose.findJointState(&mA) == first.get());
        CHECK(mPose.findJointState("a") == first.get());
        CHECK(mPose.findJointState("b") == other.get());
        CHECK(mPose.findJointState("nobody") == nullptr);

        mPose.removeJointState(mPose.findJointState(&mA));
        CHECK(mPose.getNumJointStates() == 1);
        CHECK(mPose.findJointState(&mA) == nullptr);
        mPose.removeJointState(second);
        UNSCOPED_INFO("removing an absent state changes nothing");
        CHECK(mPose.getNumJointStates() == 1);

        mPose.addJointState(second);
        UNSCOPED_INFO("the joint can be re-added with a different state");
        CHECK(mPose.findJointState(&mA) == second.get());
    }

    //-------------------------------------------------------------------------
    // LLPoseBlender
    //-------------------------------------------------------------------------
    struct llposeblender_data : public joints
    {
        LLPoseBlender mBlender;

        // No joint already claimed by a motion at full weight, which is what
        // every test below means unless it says otherwise.
        U8 mNothingSaturated[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};

        bool addMotion(LLMotion* motion) { return mBlender.addMotion(motion, mNothingSaturated); }

        static void arm(ALTestMotion& motion) { motion.getPose()->setWeight(1.f); }
    };

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: a motion's joint states are applied to their joints", "[LLPoseBlender]")
    {
        ALTestMotion motion;
        LLPointer<LLJointState> sa = motion.addJoint(&mA, LLJointState::ROT);
        sa->setRotation(ROT_A);
        LLPointer<LLJointState> sb = motion.addJoint(&mB, LLJointState::POS);
        sb->setPosition(LLVector3(3.f, 2.f, 1.f));
        arm(motion);

        CHECK(addMotion(&motion));
        mBlender.blendAndApply();

        require_same_rotation("motion rotation applied", mA.getRotation(), ROT_A);
        require_vec3_near("motion position applied", mB.getPosition(), LLVector3(3.f, 2.f, 1.f));
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: the higher priority motion wins in either order", "[LLPoseBlender]")
    {
        ALTestMotion low(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        low.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(low);

        ALTestMotion high(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY);
        high.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(high);

        addMotion(&low);
        addMotion(&high);
        mBlender.blendAndApply();
        require_same_rotation("higher motion priority wins, low first", mA.getRotation(), ROT_B);

        mA.setRotation(LLQuaternion::DEFAULT);
        addMotion(&high);
        addMotion(&low);
        mBlender.blendAndApply();
        require_same_rotation("higher motion priority wins, high first", mA.getRotation(), ROT_B);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: a joint state's own priority overrides its motion's", "[LLPoseBlender]")
    {
        // A joint state's own priority overrides its motion's.
        ALTestMotion low(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        low.addJoint(&mA, LLJointState::ROT, LLJoint::HIGHEST_PRIORITY)->setRotation(ROT_A);
        arm(low);

        ALTestMotion medium(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY);
        medium.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(medium);

        addMotion(&medium);
        addMotion(&low);
        mBlender.blendAndApply();
        require_same_rotation("joint state priority beats motion priority", mA.getRotation(), ROT_A);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: an additive motion composes ahead of the base and is masked behind", "[LLPoseBlender]")
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

        addMotion(&additive);
        addMotion(&base);
        mBlender.blendAndApply();
        require_same_rotation("additive ahead of the base composes onto it", mA.getRotation(), ROT_B * ROT_A);

        mA.setRotation(LLQuaternion::DEFAULT);
        addMotion(&base);
        addMotion(&additive);
        mBlender.blendAndApply();
        require_same_rotation("additive behind a saturated base is masked", mA.getRotation(), ROT_A);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: blendAndApply and clearBlenders empty the queued motions", "[LLPoseBlender]")
    {
        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        addMotion(&motion);
        mBlender.blendAndApply();
        require_same_rotation("first apply", mA.getRotation(), ROT_A);

        // The active list is consumed by blendAndApply().
        mA.setRotation(ROT_C);
        mBlender.blendAndApply();
        require_same_rotation("second apply without addMotion is a no-op", mA.getRotation(), ROT_C);

        // clearBlenders() discards queued states.
        addMotion(&motion);
        mBlender.clearBlenders();
        mBlender.blendAndApply();
        require_same_rotation("cleared queue is a no-op", mA.getRotation(), ROT_C);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: a joint with no number is in the motion's pose but not its signature", "[LLPoseBlender]")
    {
        // A joint that never received a joint number is still kept in the
        // pose: LLMotion::addJointState inserts the state before it checks the
        // number's range. It is left out of the motion's joint signature,
        // which has no slot for it.
        LLJoint unnumbered;
        unnumbered.setup("unnumbered");
        UNSCOPED_INFO("default joint number");
        CHECK(unnumbered.getJointNum() == -1);

        ALTestMotion motion;
        motion.addJoint(&unnumbered, LLJointState::ROT);
        CHECK(motion.getPose()->getNumJointStates() == 1);
        const U8* signature = &motion.mJointSignature[0][0];
        CHECK(std::count_if(signature, signature + sizeof(motion.mJointSignature), [](U8 b) { return b != 0; }) == 0);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: a joint with no number is skipped and the rest still blend", "[LLPoseBlender]")
    {
        // The pool is indexed by joint number. A joint that never received one
        // -- LLJoint::init leaves it at -1 -- is in the motion's pose, since
        // LLMotion::addJointState inserts before it checks the range, but has
        // nowhere to go in the pool. It is left alone; the rest of the motion
        // is not.
        LLJoint unnumbered;
        unnumbered.setup("unnumbered");
        UNSCOPED_INFO("a fresh joint has no number");
        CHECK(unnumbered.getJointNum() == -1);
        const LLQuaternion before = unnumbered.getRotation();

        ALTestMotion motion;
        motion.addJoint(&unnumbered, LLJointState::ROT)->setRotation(ROT_B);
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        addMotion(&motion);
        mBlender.blendAndApply();
        require_same_rotation("the unnumbered joint is not blended", unnumbered.getRotation(), before);
        require_same_rotation("the numbered joint in the same motion is", mA.getRotation(), ROT_A);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: the first and the last pool slots both blend", "[LLPoseBlender]")
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

        addMotion(&motion);
        mBlender.blendAndApply();
        require_same_rotation("slot zero blends", mA.getRotation(), ROT_A);
        require_same_rotation("the last slot blends", last.getRotation(), ROT_C);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: blendAndCache holds the blend and interpolate moves toward it", "[LLPoseBlender]")
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

        addMotion(&motion);
        mBlender.blendAndCache(true);
        require_same_rotation("caching leaves the joint's rotation alone", mA.getRotation(), LLQuaternion::DEFAULT);
        require_vec3_near("caching leaves the joint's position alone", mA.getPosition(), LLVector3::zero);

        mBlender.interpolate(0.5f);
        require_vec3_near("half way toward the cached position", mA.getPosition(), LLVector3(1.f, 2.f, 3.f));

        mBlender.interpolate(1.f);
        require_vec3_near("at the cached position", mA.getPosition(), LLVector3(2.f, 4.f, 6.f));
        require_same_rotation("at the cached rotation", mA.getRotation(), ROT_A);

        // and once the blenders are cleared, interpolating moves nothing
        mBlender.clearBlenders();
        mA.setPosition(LLVector3::zero);
        mBlender.interpolate(1.f);
        require_vec3_near("a cleared blender no longer drives the joint", mA.getPosition(), LLVector3::zero);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: interpolating a rotation-only cache does not dirty the position", "[LLPoseBlender]")
    {
        // Interpolating toward the cache moves only the channels the blend
        // wrote. A joint on the coarse clock is interpolated every frame, so
        // a rotation-only blend that also wrote back an unchanged position
        // would rebuild the joint's subtree every frame for nothing.
        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        addMotion(&motion);
        mBlender.blendAndCache(true);
        mA.updateWorldMatrixChildren();
        UNSCOPED_INFO("the joint starts clean");
        CHECK(mA.mDirtyFlags == 0);

        mBlender.interpolate(0.5f);
        UNSCOPED_INFO("the rotation moved, so the matrix is dirty");
        CHECK((mA.mDirtyFlags & LLJoint::MATRIX_DIRTY) != 0);
        UNSCOPED_INFO("an unchanged position and scale dirty nothing of their own");
        CHECK((mA.mDirtyFlags & LLJoint::POSITION_DIRTY) == 0);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: zero-weight motions do not take a joint's slots", "[LLPoseBlender]")
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
            UNSCOPED_INFO("a silent motion weighs nothing");
            REQUIRE(silent.back()->getPose()->getWeight() == 0.f);
            addMotion(silent.back().get());
        }

        ALTestMotion playing(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        playing.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(playing);
        addMotion(&playing);

        mBlender.blendAndApply();
        require_same_rotation("the motion with a weight still reaches the joint", mA.getRotation(), ROT_A);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: leaving out a saturated rotation gives the same pose", "[LLPoseBlender]")
    {
        // The differential the skip rests on. A motion at full weight claims
        // the whole of a joint's rotation, so a lower priority rotation
        // underneath it is interpolated away to nothing whether it is blended
        // in or left out -- and left out it does not take one of the joint's
        // six slots to arrive at the same answer.
        ALTestMotion owner(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY);
        owner.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);

        ALTestMotion underneath(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY);
        underneath.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(underneath);

        U8 saturated[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};
        saturated[mA.getJointNum()] = 0xff >> (7 - LLJoint::HIGH_PRIORITY);

        // left in
        arm(owner);
        mA.setRotation(LLQuaternion::DEFAULT);
        addMotion(&owner);
        addMotion(&underneath);
        mBlender.blendAndApply();
        const LLQuaternion blended_in = mA.getRotation();

        // left out
        mA.setRotation(LLQuaternion::DEFAULT);
        addMotion(&owner);
        mBlender.addMotion(&underneath, saturated);
        mBlender.blendAndApply();
        const LLQuaternion left_out = mA.getRotation();

        require_same_rotation("blending it in gives the owner's rotation", blended_in, ROT_A);
        require_same_rotation("and so does leaving it out", left_out, blended_in);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: only rotation is left out under a saturated joint", "[LLPoseBlender]")
    {
        // Only a rotation is left out. A state that also moves or scales the
        // joint is summed on its own account, whatever is claimed above it.
        U8 saturated[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};
        saturated[mA.getJointNum()] = 0xff >> (7 - LLJoint::HIGHEST_PRIORITY);

        const LLVector3 offset(0.f, 0.f, 0.75f);

        ALTestMotion mover(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        LLPointer<LLJointState> state = mover.addJoint(&mA, LLJointState::ROT | LLJointState::POS);
        state->setRotation(ROT_B);
        state->setPosition(offset);
        arm(mover);

        mBlender.addMotion(&mover, saturated);
        mBlender.blendAndApply();

        require_vec3_near("the position still reaches the joint", mA.getPosition(), offset);
        require_same_rotation("and so does the rotation that came with it", mA.getRotation(), ROT_B);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: a half-faded owner does not mask what is underneath", "[LLPoseBlender]")
    {
        // Which is why the mask is built from motions at full weight and no
        // others. An owner only part way in has not claimed the joint, the one
        // underneath is still visible through it, and leaving it out would be
        // a pose nobody asked for.
        ALTestMotion owner(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY);
        owner.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        owner.getPose()->setWeight(0.5f);

        ALTestMotion underneath(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY);
        underneath.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_B);
        arm(underneath);

        mA.setRotation(LLQuaternion::DEFAULT);
        addMotion(&owner);
        addMotion(&underneath);
        mBlender.blendAndApply();

        const LLQuaternion got = mA.getRotation();
        CHECK(fabsf(dot(got, ROT_A)) < 0.999f);
        CHECK(fabsf(dot(got, ROT_B)) < 0.999f);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: left-out rotations leave a slot for a motion that needs one", "[LLPoseBlender]")
    {
        // What leaving them out buys, beyond the work: a joint has six slots
        // and a state that would be interpolated away to nothing was taking
        // one. Six claimed rotations and then something that actually has
        // somewhere to put the joint -- with them left in, the six fill the
        // slots and it is turned away.
        U8 saturated[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};
        saturated[mA.getJointNum()] = 0xff >> (7 - LLJoint::HIGH_PRIORITY);

        std::vector<std::unique_ptr<ALTestMotion> > claimed;
        for (S32 i = 0; i < JSB_NUM_JOINT_STATES; i++)
        {
            claimed.push_back(std::make_unique<ALTestMotion>(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY));
            claimed.back()->addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
            arm(*claimed.back());
            mBlender.addMotion(claimed.back().get(), saturated);
        }

        const LLVector3 offset(0.f, 0.f, 1.25f);
        ALTestMotion mover(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        LLPointer<LLJointState> state = mover.addJoint(&mA, LLJointState::ROT | LLJointState::POS);
        state->setRotation(ROT_B);
        state->setPosition(offset);
        arm(mover);
        mBlender.addMotion(&mover, saturated);

        mBlender.blendAndApply();
        require_vec3_near("the slots were there for the motion that needed one",
                          mA.getPosition(), offset);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: interpolate leaves channels nothing blended where they are", "[LLPoseBlender]")
    {
        // A rotation-only blend caches a rotation and nothing else. What sits
        // in the other two is the joint's own value, put there when the cache
        // was seeded, so leading the joint back toward it undoes whatever has
        // moved the joint since -- the constraints and the foot solver write
        // joints straight, in the frames between quanta that this is filling
        // in -- a little further with every frame.
        const LLVector3 moved(0.f, 0.f, 0.5f);
        const LLVector3 rescaled(2.f, 2.f, 2.f);

        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        arm(motion);

        addMotion(&motion);
        mBlender.blendAndCache(true);

        mA.setPosition(moved);
        mA.setScale(rescaled);

        mBlender.interpolate(0.5f);
        require_vec3_near("a position nothing blended is left where it is", mA.getPosition(), moved);
        require_vec3_near("and so is a scale", mA.getScale(), rescaled);
        UNSCOPED_INFO("the channel that was blended still moves");
        CHECK(mA.getRotation() != LLQuaternion::DEFAULT);
    }

    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: interpolate follows the arc between rotations", "[LLPoseBlender]")
    {
        // The frames between quanta follow the arc, like the blend they are
        // standing in for. A quantum is up to a quarter of a second, which is
        // as far apart as two poses of one joint ever get, and the chord cuts
        // most where they are furthest apart. Nine tenths of a right angle
        // short of a half turn, a quarter of the way along, is two degrees.
        const LLQuaternion far_apart(2.6f, LLVector3::y_axis);

        ALTestMotion motion;
        motion.addJoint(&mA, LLJointState::ROT)->setRotation(far_apart);
        arm(motion);

        addMotion(&motion);
        mBlender.blendAndCache(true);

        mBlender.interpolate(0.25f);
        require_same_rotation("a quarter of the way along the arc, not across it",
                              mA.getRotation(), slerp(0.25f, LLQuaternion::DEFAULT, far_apart));
    }


    TEST_CASE_METHOD(llposeblender_data, "LLPoseBlender: the pelvis fix still gets a slot under a landing's seven states", "[LLPoseBlender]")
    {
        // Landing from a run puts seven states on the pelvis at once: the two
        // adjust servos, both live for the frame the fly one hands over to the
        // walk one, the rotation the jump has on it, the animation coming in
        // over the one going out, and the pelvis fix underneath them all. The
        // fix is a full weight state at rest, and what it is there for is to
        // take up the weight the animations have not claimed -- through the
        // saturating sum, a pose the animations only claim an eighth of comes
        // out an eighth as deep.
        //
        // It is also the lowest priority and the last one offered a slot, so
        // it is the one refused when they run out. That put the whole of a
        // landing crouch on the pelvis for one frame.
        const LLVector3 crouch(0.f, 0.f, -0.3f);

        // The additive pass runs first, so its states are offered slots first.
        ALTestMotion fly(LLUUID::generateNewID(), LLJoint::HIGHER_PRIORITY, LLMotion::ADDITIVE_BLEND);
        fly.addJoint(&mA, LLJointState::POS | LLJointState::ROT);
        arm(fly);

        ALTestMotion walk(LLUUID::generateNewID(), LLJoint::HIGH_PRIORITY, LLMotion::ADDITIVE_BLEND);
        walk.addJoint(&mA, LLJointState::POS);
        arm(walk);

        ALTestMotion noise(LLUUID::generateNewID(), LLJoint::MEDIUM_PRIORITY, LLMotion::ADDITIVE_BLEND);
        noise.addJoint(&mA, LLJointState::ROT);
        arm(noise);

        addMotion(&fly);
        addMotion(&walk);
        addMotion(&noise);

        // then the normal one, newest motion first
        ALTestMotion jump(LLUUID::generateNewID(), LLJoint::HIGHER_PRIORITY);
        jump.addJoint(&mA, LLJointState::ROT)->setRotation(ROT_A);
        jump.getPose()->setWeight(0.96f);

        ALTestMotion run(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        run.addJoint(&mA, LLJointState::POS | LLJointState::ROT);
        run.getPose()->setWeight(0.02f);

        ALTestMotion land(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        land.addJoint(&mA, LLJointState::POS | LLJointState::ROT)->setPosition(crouch);
        land.getPose()->setWeight(0.13f);

        // the fix, oldest of them, so it is offered a slot last of all
        ALTestMotion fix(LLUUID::generateNewID(), LLJoint::LOW_PRIORITY);
        fix.addJoint(&mA, LLJointState::POS);
        arm(fix);

        addMotion(&run);
        addMotion(&land);
        addMotion(&jump);
        addMotion(&fix);

        mBlender.blendAndApply();

        // The animations between them claim 0.15 of the joint, so the crouch
        // arrives about a seventh as deep. Refuse the fix a slot and it
        // arrives at seven eighths of it.
        UNSCOPED_INFO("the pelvis fix took up the weight the animations did not claim");
        CHECK(fabsf(mA.getPosition().mV[VZ]) < 0.5f * fabsf(crouch.mV[VZ]));
    }

}
