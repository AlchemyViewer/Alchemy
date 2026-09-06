/**
 * @file llkeyframemotion_test.cpp
 * @brief Keyframe curves against the map they replaced.
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

#include "llkeyframemotion.h"

#include "lldatapacker.h"
#include "llquantize.h"

#include "altestcharacter.h"

#include <cmath>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../test/lltut.h"

namespace
{
    typedef LLKeyframeMotion::InterpolationType InterpolationType;
    typedef LLKeyframeMotion::PositionCurve PositionCurve;
    typedef LLKeyframeMotion::RotationCurve RotationCurve;

    // The curve holds LLVector4a and LLQuaternion2; the reference below is
    // built and read in the scalar types, so that what it is compared against
    // is a second implementation rather than the same one seen twice. These
    // four are the whole of the boundary between them.
    void set_key(PositionCurve& curve, F32 time, const LLVector3& value)
    {
        LLVector4a stored;
        stored.load3(value.mV);
        curve.setKey(time, stored);
    }

    void set_key(RotationCurve& curve, F32 time, const LLQuaternion& value)
    {
        curve.setKey(time, LLQuaternion2(value));
    }

    LLVector3 key_value(const PositionCurve& curve, U32 index)
    {
        return LLVector3(curve.getKeyValue(index).getF32ptr());
    }

    LLQuaternion key_value(const RotationCurve& curve, U32 index)
    {
        LLQuaternion value;
        curve.getKeyValue(index).store(value);
        return value;
    }

    LLVector3 sample(const PositionCurve& curve, F32 time)
    {
        return LLVector3(curve.getValue(time).getF32ptr());
    }

    LLVector3 sample(const PositionCurve& curve, F32 time, U32& cursor)
    {
        return LLVector3(curve.getValue(time, cursor).getF32ptr());
    }

    LLQuaternion sample(const RotationCurve& curve, F32 time)
    {
        LLQuaternion value;
        curve.getValue(time).store(value);
        return value;
    }

    LLQuaternion sample(const RotationCurve& curve, F32 time, U32& cursor)
    {
        LLQuaternion value;
        curve.getValue(time, cursor).store(value);
        return value;
    }

    LLVector3 blend_reference(F32 u, const LLVector3& before, const LLVector3& after)
    {
        return lerp(before, after, u);
    }

    // The curve interpolates rotations with a normalized lerp for every pair,
    // including the ones more than a half turn apart, where the free nlerp()
    // hands over to slerp. Written out here rather than called, so that what
    // the curve is compared against is a second implementation and not the
    // same one.
    LLQuaternion blend_reference(F32 u, const LLQuaternion& before, const LLQuaternion& after)
    {
        LLQuaternion near_after = after;
        if (dot(before, after) < 0.f)
        {
            for (S32 i = 0; i < 4; ++i)
            {
                near_after.mQ[i] = -near_after.mQ[i];
            }
        }

        LLQuaternion result;
        for (S32 i = 0; i < 4; ++i)
        {
            result.mQ[i] = before.mQ[i] + (near_after.mQ[i] - before.mQ[i]) * u;
        }
        result.normalize();
        return result;
    }

    // The curve as it was: a map from time to value, searched from the root
    // for every sample. The array curve has to agree with it to the bit.
    template <typename T>
    struct MapCurve
    {
        std::map<F32, T> mKeys;
        InterpolationType mInterpolationType = LLKeyframeMotion::IT_LINEAR;

        U32 lowerBound(F32 time) const
        {
            return static_cast<U32>(std::distance(mKeys.begin(), mKeys.lower_bound(time)));
        }

        T getValue(F32 time) const
        {
            if (mKeys.empty())
            {
                return T();
            }
            auto right = mKeys.lower_bound(time);
            if (right == mKeys.end())
            {
                --right;
                return right->second;
            }
            if (right == mKeys.begin() || right->first == time)
            {
                return right->second;
            }
            auto left = right;
            --left;
            if (mInterpolationType == LLKeyframeMotion::IT_STEP)
            {
                return left->second;
            }
            const F32 index_before = left->first;
            const F32 index_after = right->first;
            const F32 u = (time - index_before) / (index_after - index_before);
            return blend_reference(u, left->second, right->second);
        }
    };

    struct Rng
    {
        std::mt19937 mEngine{ 0x5EED };

        F32 uniform(F32 lo, F32 hi)
        {
            return std::uniform_real_distribution<F32>(lo, hi)(mEngine);
        }

        LLVector3 vector()
        {
            return LLVector3(uniform(-2.f, 2.f), uniform(-2.f, 2.f), uniform(-2.f, 2.f));
        }

        LLQuaternion rotation()
        {
            LLVector3 axis(uniform(-1.f, 1.f), uniform(-1.f, 1.f), uniform(-1.f, 1.f));
            if (axis.magVecSquared() < 0.01f)
            {
                axis.setVec(0.f, 0.f, 1.f);
            }
            return LLQuaternion(uniform(-F_PI, F_PI), axis);
        }
    };

    template <typename T> T random_value(Rng& rng);
    template <> LLVector3 random_value<LLVector3>(Rng& rng) { return rng.vector(); }
    template <> LLQuaternion random_value<LLQuaternion>(Rng& rng) { return rng.rotation(); }

    constexpr F32 DURATION = 4.f;
    constexpr U32 KEY_COUNT = 40;

    // The same keys into both curves. The times are random, so the spacing is
    // uneven; a few are repeated, so the last value at a time has to win; and
    // one sits on the origin.
    template <typename CurveT, typename T>
    void build(Rng& rng, CurveT& curve, MapCurve<T>& reference, InterpolationType type)
    {
        std::vector<F32> times;
        for (U32 i = 0; i < KEY_COUNT; ++i)
        {
            times.push_back(rng.uniform(0.f, DURATION));
        }
        times.push_back(times[3]);
        times.push_back(times[17]);
        times.push_back(0.f);
        for (F32 time : times)
        {
            const T value = random_value<T>(rng);
            set_key(curve, time, value);
            reference.mKeys[time] = value;
        }
        curve.mInterpolationType = type;
        reference.mInterpolationType = type;
    }

    template <typename T>
    void ensure_same_value(const char* what, F32 time, const T& got, const T& want)
    {
        tut::ensure(std::string(what) + " at t=" + std::to_string(time), got == want);
    }

    // Rotations are normalized on the way out of the blend, and the curve does
    // that with a reciprocal square root refined by one Newton step where this
    // reference divides by a real one. They agree to about a part in ten
    // million, which is not the same bits. Everything else here is compared
    // exactly, this one to a tolerance well inside what a rotation can carry.
    void ensure_same_value(const char* what, F32 time, const LLQuaternion& got, const LLQuaternion& want)
    {
        for (S32 i = 0; i < 4; ++i)
        {
            tut::ensure_approximately_equals_range(
                (std::string(what) + " at t=" + std::to_string(time)).c_str(),
                got.mQ[i], want.mQ[i], 1e-6f);
        }
    }

    // Every way of sampling a curve without a cursor: far outside the keys,
    // random within them, and exactly on each one and a step either side.
    template <typename CurveT, typename T>
    void ensure_stateless_parity(const char* what, Rng& rng, const CurveT& curve, const MapCurve<T>& reference)
    {
        for (U32 i = 0; i < 2000; ++i)
        {
            const F32 time = rng.uniform(-0.5f, DURATION + 0.5f);
            ensure_same_value(what, time, sample(curve, time), reference.getValue(time));
        }
        for (U32 k = 0; k < curve.getNumKeys(); ++k)
        {
            const F32 time = curve.getKeyTime(k);
            const F32 above = std::nextafter(time, DURATION * 2.f);
            const F32 below = std::nextafter(time, -DURATION);
            ensure_same_value(what, time, sample(curve, time), reference.getValue(time));
            ensure_same_value(what, above, sample(curve, above), reference.getValue(above));
            ensure_same_value(what, below, sample(curve, below), reference.getValue(below));
        }
    }

    // Playback: time only ever moves forward, by a step in [min_step,
    // max_step], until it passes the end and wraps to the start the way a
    // looping motion does. Every sample must match the map, and the cursor
    // must be exactly where the map's search would have landed.
    template <typename CurveT, typename T>
    U32 ensure_playback_parity(const char* what, Rng& rng, const CurveT& curve, const MapCurve<T>& reference,
                               F32 min_step, F32 max_step, U32 samples)
    {
        U32 cursor = 0;
        U32 wraps = 0;
        F32 time = 0.f;
        for (U32 i = 0; i < samples; ++i)
        {
            time += rng.uniform(min_step, max_step);
            if (time >= DURATION)
            {
                time -= DURATION;
                ++wraps;
            }
            ensure_same_value(what, time, sample(curve, time, cursor), reference.getValue(time));
            tut::ensure_equals(std::string(what) + " cursor at t=" + std::to_string(time), cursor, reference.lowerBound(time));
        }
        return wraps;
    }
}

namespace
{
    // One joint of an animation asset: a name, a priority, and the keys of
    // its two curves. The times are packed as fractions of the duration and
    // the rotations as a packed vector, both the way the format wants them,
    // so what comes back is quantized and only approximately what went in.
    struct AssetJoint
    {
        std::string mName;
        S32 mPriority = LLJoint::HIGH_PRIORITY;
        std::vector<std::pair<F32, LLQuaternion> > mRotations;
        std::vector<std::pair<F32, LLVector3> > mPositions;
    };

    struct AssetAnimation
    {
        S32 mBasePriority = LLJoint::MEDIUM_PRIORITY;
        F32 mDuration = 2.f;
        F32 mLoopInPoint = 0.f;
        F32 mLoopOutPoint = 2.f;
        S32 mLoop = 1;
        F32 mEaseIn = 0.3f;
        F32 mEaseOut = 0.4f;
        U32 mHandPose = LLHandMotion::HAND_POSE_RELAXED;
        std::vector<AssetJoint> mJoints;
        S32 mNumConstraints = 0;
        // written in place of the real joint count when set, for a header
        // that lies about how much follows it
        S32 mClaimedJoints = -1;
    };

    void pack_animation(LLDataPackerBinaryBuffer& dp, const AssetAnimation& anim)
    {
        dp.packU16(1, "version");
        dp.packU16(0, "sub_version");
        dp.packS32(anim.mBasePriority, "base_priority");
        dp.packF32(anim.mDuration, "duration");
        dp.packString("", "emote_name");
        dp.packF32(anim.mLoopInPoint, "loop_in_point");
        dp.packF32(anim.mLoopOutPoint, "loop_out_point");
        dp.packS32(anim.mLoop, "loop");
        dp.packF32(anim.mEaseIn, "ease_in_duration");
        dp.packF32(anim.mEaseOut, "ease_out_duration");
        dp.packU32(anim.mHandPose, "hand_pose");
        dp.packU32(anim.mClaimedJoints >= 0 ? (U32)anim.mClaimedJoints : (U32)anim.mJoints.size(), "num_joints");

        for (const AssetJoint& joint : anim.mJoints)
        {
            dp.packString(joint.mName, "joint_name");
            dp.packS32(joint.mPriority, "joint_priority");

            dp.packS32((S32)joint.mRotations.size(), "num_rot_keys");
            for (const auto& key : joint.mRotations)
            {
                dp.packU16(F32_to_U16(key.first, 0.f, anim.mDuration), "time");
                LLVector3 packed = key.second.packToVector3();
                packed.quantize16(-1.f, 1.f, -1.f, 1.f);
                dp.packU16(F32_to_U16(packed.mV[VX], -1.f, 1.f), "rot_angle_x");
                dp.packU16(F32_to_U16(packed.mV[VY], -1.f, 1.f), "rot_angle_y");
                dp.packU16(F32_to_U16(packed.mV[VZ], -1.f, 1.f), "rot_angle_z");
            }

            dp.packS32((S32)joint.mPositions.size(), "num_pos_keys");
            for (const auto& key : joint.mPositions)
            {
                dp.packU16(F32_to_U16(key.first, 0.f, anim.mDuration), "time");
                dp.packU16(F32_to_U16(key.second.mV[VX], -LL_MAX_PELVIS_OFFSET, LL_MAX_PELVIS_OFFSET), "pos_x");
                dp.packU16(F32_to_U16(key.second.mV[VY], -LL_MAX_PELVIS_OFFSET, LL_MAX_PELVIS_OFFSET), "pos_y");
                dp.packU16(F32_to_U16(key.second.mV[VZ], -LL_MAX_PELVIS_OFFSET, LL_MAX_PELVIS_OFFSET), "pos_z");
            }
        }

        dp.packS32(anim.mNumConstraints, "num_constraints");
    }

    AssetAnimation two_joint_animation()
    {
        AssetAnimation anim;

        AssetJoint pelvis;
        pelvis.mName = "mPelvis";
        pelvis.mPriority = LLJoint::HIGH_PRIORITY;
        pelvis.mRotations.push_back({ 0.f, LLQuaternion(0.4f, LLVector3::z_axis) });
        pelvis.mRotations.push_back({ 1.f, LLQuaternion(-0.6f, LLVector3::y_axis) });
        pelvis.mRotations.push_back({ 2.f, LLQuaternion(0.2f, LLVector3::x_axis) });
        pelvis.mPositions.push_back({ 0.f, LLVector3(0.f, 0.f, 0.25f) });
        pelvis.mPositions.push_back({ 2.f, LLVector3(0.1f, -0.2f, 0.3f) });
        anim.mJoints.push_back(pelvis);

        AssetJoint torso;
        torso.mName = "mTorso";
        torso.mPriority = LLJoint::MEDIUM_PRIORITY;
        torso.mRotations.push_back({ 0.5f, LLQuaternion(0.9f, LLVector3::x_axis) });
        anim.mJoints.push_back(torso);

        return anim;
    }
}

namespace tut
{
    struct llkeyframemotion_data
    {
        ALTestCharacter mCharacter;

        // The keyframe data a successful deserialize produces is owned by the
        // global cache, not by the motion, so it outlives the test unless the
        // cache is emptied.
        ~llkeyframemotion_data() { LLKeyframeDataCache::clear(); }

        // Reads `anim` into a motion the way an arriving asset would.
        // Returns whether it was accepted.
        bool load(LLKeyframeMotion& motion, const AssetAnimation& anim, U8* buffer, S32 size)
        {
            LLDataPackerBinaryBuffer dp(buffer, size);
            pack_animation(dp, anim);
            LLDataPackerBinaryBuffer reader(buffer, dp.getCurrentSize());
            motion.setCharacter(&mCharacter);
            return motion.deserialize(reader, motion.getID());
        }

        // Runs the motion's own sampling at `time` and hands back the joint
        // state it wrote, which is the only way from outside to see what the
        // curves are holding.
        LLJointState* sampleAt(LLKeyframeMotion& motion, F32 time, const char* joint_name)
        {
            U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};
            motion.onUpdate(time, mask);
            return motion.getPose()->findJointState(std::string(joint_name));
        }
    };
    typedef test_group<llkeyframemotion_data> llkeyframemotion_test;
    typedef llkeyframemotion_test::object llkeyframemotion_object;
    tut::llkeyframemotion_test llkeyframemotion_testcase("LLKeyframeMotion");

    template<> template<>
    void llkeyframemotion_object::test<1>()
    {
        // Keys arrive in whatever order the asset holds them, and a time can
        // repeat. The curve keeps them sorted and unique, the last value at a
        // time winning, which is what the map did.
        PositionCurve curve;
        set_key(curve, 2.f, LLVector3(2.f, 0.f, 0.f));
        set_key(curve, 1.f, LLVector3(1.f, 0.f, 0.f));
        set_key(curve, 3.f, LLVector3(3.f, 0.f, 0.f));
        set_key(curve, 2.f, LLVector3(0.f, 2.f, 0.f));
        ensure_equals("a repeated time does not add a key", curve.getNumKeys(), 3u);
        ensure_equals("keys are sorted: first", curve.getKeyTime(0), 1.f);
        ensure_equals("keys are sorted: second", curve.getKeyTime(1), 2.f);
        ensure_equals("keys are sorted: third", curve.getKeyTime(2), 3.f);
        ensure("the last value at a time wins", key_value(curve, 1) == LLVector3(0.f, 2.f, 0.f));

        Rng rng;
        RotationCurve rotations;
        MapCurve<LLQuaternion> reference;
        build(rng, rotations, reference, LLKeyframeMotion::IT_LINEAR);
        ensure_equals("random keys: count matches the map", rotations.getNumKeys(), static_cast<U32>(reference.mKeys.size()));
        U32 k = 0;
        for (const auto& key : reference.mKeys)
        {
            ensure_equals("random keys: time matches the map", rotations.getKeyTime(k), key.first);
            ensure("random keys: value matches the map", key_value(rotations, k) == key.second);
            ++k;
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<2>()
    {
        // Sampled anywhere, without a cursor, the array curve returns what
        // the map curve returned: clamped outside the keys, the key itself on
        // one, the blend between neighbours otherwise. Both blends, both
        // value types.
        Rng rng;
        {
            PositionCurve curve;
            MapCurve<LLVector3> reference;
            build(rng, curve, reference, LLKeyframeMotion::IT_LINEAR);
            ensure_stateless_parity("linear position", rng, curve, reference);
        }
        {
            PositionCurve curve;
            MapCurve<LLVector3> reference;
            build(rng, curve, reference, LLKeyframeMotion::IT_STEP);
            ensure_stateless_parity("stepped position", rng, curve, reference);
        }
        {
            RotationCurve curve;
            MapCurve<LLQuaternion> reference;
            build(rng, curve, reference, LLKeyframeMotion::IT_LINEAR);
            ensure_stateless_parity("linear rotation", rng, curve, reference);
        }
        {
            RotationCurve curve;
            MapCurve<LLQuaternion> reference;
            build(rng, curve, reference, LLKeyframeMotion::IT_STEP);
            ensure_stateless_parity("stepped rotation", rng, curve, reference);
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<3>()
    {
        // Played forward with a cursor, fine steps land on the last answer or
        // the one after it, coarse steps skip past both and fall back to a
        // search, and a loop wrap sends time back to the start under a
        // cursor pointing at the end. The map does not care; the values and
        // the cursor have to agree with it throughout.
        Rng rng;
        RotationCurve curve;
        MapCurve<LLQuaternion> reference;
        build(rng, curve, reference, LLKeyframeMotion::IT_LINEAR);

        const U32 fine_wraps = ensure_playback_parity("fine playback", rng, curve, reference, 0.f, 0.03f, 1200);
        ensure("fine playback wrapped", fine_wraps >= 3);

        const U32 coarse_wraps = ensure_playback_parity("coarse playback", rng, curve, reference, 0.3f, 1.f, 200);
        ensure("coarse playback wrapped", coarse_wraps >= 20);

        PositionCurve stepped;
        MapCurve<LLVector3> stepped_reference;
        build(rng, stepped, stepped_reference, LLKeyframeMotion::IT_STEP);
        ensure_playback_parity("stepped playback", rng, stepped, stepped_reference, 0.f, 0.05f, 800);
    }

    template<> template<>
    void llkeyframemotion_object::test<4>()
    {
        // A sample exactly on a key returns that key, whatever the cursor
        // claims: behind it, on it, past it, at the end, or past the end of
        // a curve that has since shrunk. A stepped curve makes the wrong
        // answer visible, since the key before holds a different value.
        Rng rng;
        PositionCurve curve;
        MapCurve<LLVector3> reference;
        build(rng, curve, reference, LLKeyframeMotion::IT_STEP);

        const U32 count = curve.getNumKeys();
        for (U32 k = 0; k < count; ++k)
        {
            const F32 time = curve.getKeyTime(k);
            const U32 hints[] = { 0u, k > 0 ? k - 1 : 0u, k, k + 1, count, count + 7 };
            for (U32 hint : hints)
            {
                U32 cursor = hint;
                ensure_same_value("exact key under any cursor", time, sample(curve, time, cursor), reference.getValue(time));
                ensure_equals("exact key leaves the cursor on the key", cursor, k);

                cursor = hint;
                const F32 above = std::nextafter(time, DURATION * 2.f);
                ensure_same_value("just past a key under any cursor", above, sample(curve, above, cursor), reference.getValue(above));
                ensure_equals("just past a key leaves the cursor after it", cursor, k + 1);
            }
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<5>()
    {
        // No keys: the identity of the channel. One key: that key, wherever
        // the sample falls, with the cursor before or after it.
        // The vector types do not zero themselves, so a curve with no keys
        // has to say what it answers with rather than handing back whatever
        // was in the memory.
        PositionCurve no_positions;
        ensure("an empty position curve is the zero vector", sample(no_positions, 1.f) == LLVector3());
        RotationCurve no_rotations;
        ensure("an empty rotation curve is the identity", sample(no_rotations, 1.f) == LLQuaternion());

        RotationCurve one;
        const LLQuaternion key(0.5f, LLVector3(0.f, 1.f, 0.f));
        set_key(one, 1.f, key);
        U32 cursor = 0;
        ensure("before the only key", sample(one, 0.f, cursor) == key);
        ensure_equals("before the only key: cursor", cursor, 0u);
        ensure("on the only key", sample(one, 1.f, cursor) == key);
        ensure_equals("on the only key: cursor", cursor, 0u);
        ensure("after the only key", sample(one, 2.f, cursor) == key);
        ensure_equals("after the only key: cursor", cursor, 1u);
        ensure("back before the only key", sample(one, 0.5f, cursor) == key);
        ensure_equals("back before the only key: cursor", cursor, 0u);
    }

    template<> template<>
    void llkeyframemotion_object::test<6>()
    {
        // An asset read in gives one joint motion per joint in the file, in
        // file order, each holding the keys that followed its name. The
        // motions live in one block, which is what every playing instance
        // walks every frame.
        LLKeyframeMotion motion(LLUUID::generateNewID());
        std::vector<U8> buffer(4096);
        const AssetAnimation anim = two_joint_animation();
        ensure("the animation is accepted", load(motion, anim, buffer.data(), (S32)buffer.size()));

        ensure_equals("both joints came through", motion.getNumJointMotions(), 2);
        ensure_approximately_equals("duration", motion.getDuration(), anim.mDuration, 16);
        ensure("the animation loops", motion.getLoop());
        ensure_approximately_equals("ease in", motion.getEaseInDuration(), anim.mEaseIn, 16);
        ensure_approximately_equals("ease out", motion.getEaseOutDuration(), anim.mEaseOut, 16);
        ensure_equals("base priority", (S32)motion.getPriority(), anim.mBasePriority);

        LLPose* pose = motion.getPose();
        ensure_equals("a joint state per joint motion", pose->getNumJointStates(), 2);
        LLJointState* pelvis_state = pose->findJointState(std::string("mPelvis"));
        ensure("the pelvis is in the pose", pelvis_state != nullptr);
        ensure_equals("the pelvis carries both channels",
                      pelvis_state->getUsage(), (U32)(LLJointState::ROT | LLJointState::POS));
        LLJointState* torso_state = pose->findJointState(std::string("mTorso"));
        ensure("the torso is in the pose", torso_state != nullptr);
        ensure_equals("the torso carries rotation only", torso_state->getUsage(), (U32)LLJointState::ROT);
        ensure_equals("the torso keeps its own priority",
                      (S32)torso_state->getPriority(), anim.mJoints[1].mPriority);

        // and the keys hold what the file held. The format quantizes both
        // channels to sixteen bits, positions over five metres either way and
        // rotations over a packed vector, so this is close rather than equal.
        for (const auto& key : anim.mJoints[0].mPositions)
        {
            LLJointState* sampled = sampleAt(motion, key.first, "mPelvis");
            ensure("the pelvis is sampled", sampled != nullptr);
            const LLVector3 got = sampled->getPosition();
            for (S32 i = 0; i < 3; ++i)
            {
                ensure_approximately_equals_range("position key survived the parse",
                                                  got.mV[i], key.second.mV[i], 1e-3f);
            }
        }

        for (const auto& key : anim.mJoints[0].mRotations)
        {
            LLJointState* sampled = sampleAt(motion, key.first, "mPelvis");
            ensure("the pelvis is sampled", sampled != nullptr);
            ensure_approximately_equals_range("rotation key survived the parse",
                                              fabsf(dot(sampled->getRotation(), key.second)), 1.f, 1e-3f);
        }

        LLJointState* torso_sample = sampleAt(motion, anim.mJoints[1].mRotations[0].first, "mTorso");
        ensure("the torso is sampled", torso_sample != nullptr);
        ensure_approximately_equals_range("the torso's one key survived the parse",
                                          fabsf(dot(torso_sample->getRotation(), anim.mJoints[1].mRotations[0].second)),
                                          1.f, 1e-3f);
    }

    template<> template<>
    void llkeyframemotion_object::test<7>()
    {
        // What was read can be written back and read again, and the second
        // reading agrees with the first joint for joint and key for key. The
        // format quantizes on the way out, so this holds only because the
        // first reading was already quantized.
        LLKeyframeMotion first(LLUUID::generateNewID());
        std::vector<U8> buffer(4096);
        ensure("the animation is accepted", load(first, two_joint_animation(), buffer.data(), (S32)buffer.size()));

        std::vector<U8> written(first.getFileSize());
        LLDataPackerBinaryBuffer writer(written.data(), (S32)written.size());
        ensure("it serializes", first.serialize(writer));

        LLKeyframeMotion second(LLUUID::generateNewID());
        second.setCharacter(&mCharacter);
        LLDataPackerBinaryBuffer reader(written.data(), writer.getCurrentSize());
        ensure("what was written reads back", second.deserialize(reader, second.getID()));

        ensure_equals("same joint count", second.getNumJointMotions(), first.getNumJointMotions());
        ensure_approximately_equals("same duration", second.getDuration(), first.getDuration(), 16);
        ensure_equals("same base priority", (S32)second.getPriority(), (S32)first.getPriority());

        LLPose* first_pose = first.getPose();
        LLPose* second_pose = second.getPose();
        ensure_equals("same joint state count",
                      second_pose->getNumJointStates(), first_pose->getNumJointStates());
        for (const char* name : { "mPelvis", "mTorso" })
        {
            LLJointState* a = first_pose->findJointState(std::string(name));
            LLJointState* b = second_pose->findJointState(std::string(name));
            ensure("joint survived the round trip", a != nullptr && b != nullptr);
            ensure_equals("same usage", b->getUsage(), a->getUsage());
            ensure_equals("same priority", (S32)b->getPriority(), (S32)a->getPriority());
        }

        // The keys themselves, sampled the way playback samples them. Without
        // this the round trip could write a joint's name and another joint's
        // curve and nothing here would notice.
        for (F32 time = 0.f; time < 2.f; time += 0.25f)
        {
            for (const char* name : { "mPelvis", "mTorso" })
            {
                LLJointState* a = sampleAt(first, time, name);
                LLJointState* b = sampleAt(second, time, name);
                ensure("joint sampled on both sides", a != nullptr && b != nullptr);

                const LLQuaternion rot_a = a->getRotation();
                const LLQuaternion rot_b = b->getRotation();
                ensure_approximately_equals_range("same rotation after the round trip",
                                                  fabsf(dot(rot_a, rot_b)), 1.f, 1e-4f);

                if (a->getUsage() & LLJointState::POS)
                {
                    const LLVector3 pos_a = a->getPosition();
                    const LLVector3 pos_b = b->getPosition();
                    for (S32 i = 0; i < 3; ++i)
                    {
                        ensure_approximately_equals_range("same position after the round trip",
                                                          pos_b.mV[i], pos_a.mV[i], 1e-4f);
                    }
                }
            }
        }
    }
}
