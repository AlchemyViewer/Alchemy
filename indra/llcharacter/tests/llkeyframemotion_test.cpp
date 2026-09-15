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
#include "llbvhconsts.h"

#include <cmath>
#include <limits>
#include <map>
#include <optional>
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

    // The curve interpolates rotations along the arc between the two keys,
    // over the shorter way round, dropping to the chord where the two are
    // close enough that nothing can tell them apart. Written out here rather
    // than called, so that what the curve is compared against is a second
    // implementation and not the same one seen twice.
    LLQuaternion blend_reference(F32 u, const LLQuaternion& before, const LLQuaternion& after)
    {
        F32 cos_half_angle = dot(before, after);

        LLQuaternion near_after = after;
        if (cos_half_angle < 0.f)
        {
            for (S32 i = 0; i < 4; ++i)
            {
                near_after.mQ[i] = -near_after.mQ[i];
            }
            cos_half_angle = -cos_half_angle;
        }

        // The same place the curve gives up on the arc.
        constexpr F32 SLERP_WORTH_IT = 0.9f;

        F32 from = 1.f - u;
        F32 to = u;
        if (cos_half_angle < SLERP_WORTH_IT)
        {
            const F32 half_angle = acosf(llmin(cos_half_angle, 1.f));
            const F32 sin_half_angle = sinf(half_angle);
            from = sinf((1.f - u) * half_angle) / sin_half_angle;
            to = sinf(u * half_angle) / sin_half_angle;
        }

        LLQuaternion result;
        for (S32 i = 0; i < 4; ++i)
        {
            result.mQ[i] = before.mQ[i] * from + near_after.mQ[i] * to;
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
        // written in place of the real key counts when set, for a joint that
        // lies about how many keys follow it. A negative count is one of the
        // things worth writing, so this cannot be a negative sentinel.
        std::optional<S32> mClaimedRotationKeys;
        std::optional<S32> mClaimedPositionKeys;
    };

    // A constraint as the file carries it: a chain length, the volume it is
    // anchored to, and the four ease times.
    struct AssetConstraint
    {
        S32 mChainLength = 1;
        U8 mConstraintType = CONSTRAINT_TYPE_POINT;
        std::string mSourceVolume;
        std::string mTargetVolume = "GROUND";
        F32 mEaseInStart = 0.f;
        F32 mEaseInStop = 0.f;
        F32 mEaseOutStart = 100.f;
        F32 mEaseOutStop = 100.f;
    };

    struct AssetAnimation
    {
        U16 mVersion = 1;
        U16 mSubVersion = 0;
        std::string mEmoteName;
        S32 mBasePriority = LLJoint::MEDIUM_PRIORITY;
        F32 mDuration = 2.f;
        F32 mLoopInPoint = 0.f;
        F32 mLoopOutPoint = 2.f;
        S32 mLoop = 1;
        F32 mEaseIn = 0.3f;
        F32 mEaseOut = 0.4f;
        U32 mHandPose = LLHandMotion::HAND_POSE_RELAXED;
        std::vector<AssetJoint> mJoints;
        std::vector<AssetConstraint> mConstraints;
        // written in place of the real counts when set, for a header that
        // lies about how much follows it
        std::optional<S32> mClaimedJoints;
        std::optional<S32> mClaimedConstraints;
    };

    void pack_animation(LLDataPackerBinaryBuffer& dp, const AssetAnimation& anim)
    {
        dp.packU16(anim.mVersion, "version");
        dp.packU16(anim.mSubVersion, "sub_version");
        dp.packS32(anim.mBasePriority, "base_priority");
        dp.packF32(anim.mDuration, "duration");
        dp.packString(anim.mEmoteName, "emote_name");
        dp.packF32(anim.mLoopInPoint, "loop_in_point");
        dp.packF32(anim.mLoopOutPoint, "loop_out_point");
        dp.packS32(anim.mLoop, "loop");
        dp.packF32(anim.mEaseIn, "ease_in_duration");
        dp.packF32(anim.mEaseOut, "ease_out_duration");
        dp.packU32(anim.mHandPose, "hand_pose");
        dp.packU32((U32)anim.mClaimedJoints.value_or((S32)anim.mJoints.size()), "num_joints");

        for (const AssetJoint& joint : anim.mJoints)
        {
            dp.packString(joint.mName, "joint_name");
            dp.packS32(joint.mPriority, "joint_priority");

            dp.packS32(joint.mClaimedRotationKeys.value_or((S32)joint.mRotations.size()), "num_rot_keys");
            for (const auto& key : joint.mRotations)
            {
                dp.packU16(F32_to_U16(key.first, 0.f, anim.mDuration), "time");
                LLVector3 packed = key.second.packToVector3();
                packed.quantize16(-1.f, 1.f, -1.f, 1.f);
                dp.packU16(F32_to_U16(packed.mV[VX], -1.f, 1.f), "rot_angle_x");
                dp.packU16(F32_to_U16(packed.mV[VY], -1.f, 1.f), "rot_angle_y");
                dp.packU16(F32_to_U16(packed.mV[VZ], -1.f, 1.f), "rot_angle_z");
            }

            dp.packS32(joint.mClaimedPositionKeys.value_or((S32)joint.mPositions.size()), "num_pos_keys");
            for (const auto& key : joint.mPositions)
            {
                dp.packU16(F32_to_U16(key.first, 0.f, anim.mDuration), "time");
                dp.packU16(F32_to_U16(key.second.mV[VX], -LL_MAX_PELVIS_OFFSET, LL_MAX_PELVIS_OFFSET), "pos_x");
                dp.packU16(F32_to_U16(key.second.mV[VY], -LL_MAX_PELVIS_OFFSET, LL_MAX_PELVIS_OFFSET), "pos_y");
                dp.packU16(F32_to_U16(key.second.mV[VZ], -LL_MAX_PELVIS_OFFSET, LL_MAX_PELVIS_OFFSET), "pos_z");
            }
        }

        dp.packS32(anim.mClaimedConstraints.value_or((S32)anim.mConstraints.size()), "num_constraints");
        for (const AssetConstraint& constraint : anim.mConstraints)
        {
            U8 volume_name[16];

            dp.packU8((U8)constraint.mChainLength, "chain_length");
            dp.packU8(constraint.mConstraintType, "constraint_type");

            memset(volume_name, 0, sizeof(volume_name));
            memcpy(volume_name, constraint.mSourceVolume.c_str(),
                   llmin(constraint.mSourceVolume.size(), sizeof(volume_name) - 1));
            dp.packBinaryDataFixed(volume_name, 16, "source_volume");
            dp.packVector3(LLVector3(0.f, 0.f, 0.f), "source_offset");

            memset(volume_name, 0, sizeof(volume_name));
            memcpy(volume_name, constraint.mTargetVolume.c_str(),
                   llmin(constraint.mTargetVolume.size(), sizeof(volume_name) - 1));
            dp.packBinaryDataFixed(volume_name, 16, "target_volume");
            dp.packVector3(LLVector3(0.f, 0.f, 0.f), "target_offset");
            dp.packVector3(LLVector3(0.f, 0.f, 0.f), "target_dir");

            dp.packF32(constraint.mEaseInStart, "ease_in_start");
            dp.packF32(constraint.mEaseInStop, "ease_in_stop");
            dp.packF32(constraint.mEaseOutStart, "ease_out_start");
            dp.packF32(constraint.mEaseOutStop, "ease_out_stop");
        }
    }

    // The three joints a constraint anchored to the test character's collision
    // volume walks up through, in the order the chain wants them.
    AssetAnimation constrained_animation(S32 chain_length)
    {
        AssetAnimation anim;
        // Every joint above the collision volume, so that a chain as long as
        // the arrays would otherwise resolve and the length check is what
        // refuses it.
        for (const char* name : { "mHead", "mNeck", "mChest", "mTorso", "mPelvis" })
        {
            AssetJoint joint;
            joint.mName = name;
            joint.mPriority = LLJoint::MEDIUM_PRIORITY;
            joint.mRotations.push_back({ 0.f, LLQuaternion(0.3f, LLVector3::z_axis) });
            joint.mRotations.push_back({ 2.f, LLQuaternion(0.6f, LLVector3::y_axis) });
            anim.mJoints.push_back(joint);
        }

        AssetConstraint constraint;
        constraint.mChainLength = chain_length;
        constraint.mSourceVolume = ALTestCharacter::COLLISION_VOLUME_NAME;
        anim.mConstraints.push_back(constraint);

        return anim;
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
    void llkeyframemotion_object::test<8>()
    {
        // The solver holds a constraint chain in arrays of MAX_CHAIN_LENGTH
        // and indexes them from zero to the chain length inclusive, so a chain
        // as long as those arrays reads and writes past the end of five of
        // them, three on the stack. The length arrives in a byte from the
        // asset, and was only checked against the number of joints.
        std::vector<U8> buffer(4096);

        for (S32 chain_length = 0; chain_length < MAX_CHAIN_LENGTH; ++chain_length)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure("a chain the arrays can hold is accepted",
                   load(motion, constrained_animation(chain_length), buffer.data(), (S32)buffer.size()));
        }

        {
            // One longer, which every joint above the volume can still supply,
            // so the length is the only thing left to refuse it.
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure("a chain as long as the arrays is refused",
                   !load(motion, constrained_animation(MAX_CHAIN_LENGTH), buffer.data(), (S32)buffer.size()));
        }

        {
            // and one past anything the skeleton could supply, which the
            // joint count check has always refused
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure("a chain longer than the skeleton is refused",
                   !load(motion, constrained_animation(200), buffer.data(), (S32)buffer.size()));
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<9>()
    {
        // The solver writes the chain's joints with the motion's own rotations
        // to measure against, and has to put them back. It gives up part way
        // through whenever a higher priority motion already owns a joint in
        // the chain -- which is what the mask says -- and used to leave every
        // joint it had already written holding a pose nothing asked for.
        LLKeyframeMotion motion(LLUUID::generateNewID());
        std::vector<U8> buffer(4096);
        ensure("the constrained animation loads",
               load(motion, constrained_animation(1), buffer.data(), (S32)buffer.size()));

        // The chain runs up from the collision volume: its parent and then its
        // grandparent, which for the test character is mHead and mNeck.
        LLJoint* first_in_chain = mCharacter.getJoint("mHead");
        LLJoint* second_in_chain = mCharacter.getJoint("mNeck");
        ensure("the chain joints resolve", first_in_chain != nullptr && second_in_chain != nullptr);

        const LLQuaternion marker(1.1f, LLVector3::x_axis);
        first_in_chain->setRotation(marker);

        // A mask that lets the first joint through and stops the solver on the
        // second. The threshold is the motion's own priority.
        U8 mask[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};
        mask[second_in_chain->getJointNum()] = 0xff;

        motion.onUpdate(1.f, mask);

        const LLQuaternion after = first_in_chain->getRotation();
        ensure_approximately_equals_range("the joint the solver wrote is put back",
                                          fabsf(dot(after, marker)), 1.f, 1e-5f);
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

    template<> template<>
    void llkeyframemotion_object::test<10>()
    {
        // A joint already turned by a motion at full weight, at a priority
        // this one cannot reach, is not sampled: the blend would interpolate
        // what came out of those curves away to nothing. The mask says which
        // joints those are.
        LLKeyframeMotion motion(LLUUID::generateNewID());
        std::vector<U8> buffer(4096);
        const AssetAnimation anim = two_joint_animation();
        ensure("the animation loads", load(motion, anim, buffer.data(), (S32)buffer.size()));

        U8 nothing_claimed[LL_CHARACTER_MAX_ANIMATED_JOINTS] = {};
        U8 all_claimed[LL_CHARACTER_MAX_ANIMATED_JOINTS];
        memset(all_claimed, 0xff, sizeof(all_claimed));

        LLPose* pose = motion.getPose();
        LLJointState* torso = pose->findJointState(std::string("mTorso"));
        LLJointState* pelvis = pose->findJointState(std::string("mPelvis"));
        ensure("both joints are in the pose", torso != nullptr && pelvis != nullptr);
        ensure_equals("the torso only rotates", torso->getUsage(), (U32)LLJointState::ROT);

        const LLQuaternion marker(2.4f, LLVector3(0.f, 0.f, 1.f));

        // claimed: the curves are not read and the state keeps what it held
        torso->setRotation(marker);
        motion.onUpdate(1.5f, all_claimed);
        ensure_approximately_equals_range("a claimed rotation is left alone",
                                          fabsf(dot(torso->getRotation(), marker)), 1.f, 1e-5f);

        // and unclaimed, the same sample writes it
        motion.onUpdate(1.5f, nothing_claimed);
        ensure("an unclaimed rotation is sampled",
               fabsf(dot(torso->getRotation(), marker)) < 0.999f);

        // The pelvis carries a position as well, which is summed on its own
        // account however much of the rotation is spoken for, so it is sampled
        // whatever the mask says.
        pelvis->setRotation(marker);
        motion.onUpdate(0.5f, all_claimed);
        ensure("a joint that also moves is sampled whatever is claimed",
               fabsf(dot(pelvis->getRotation(), marker)) < 0.999f);
    }

    template<> template<>
    void llkeyframemotion_object::test<11>()
    {
        // The header, field by field, with one thing wrong at a time. Every
        // one of these arrives in an animation another avatar can play at you.
        std::vector<U8> buffer(8192);
        const F32 nan = std::numeric_limits<F32>::quiet_NaN();
        const F32 infinity = std::numeric_limits<F32>::infinity();

        auto refuses = [&](const char* what, const AssetAnimation& anim)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure(what, !load(motion, anim, buffer.data(), (S32)buffer.size()));
        };
        auto accepts = [&](const char* what, const AssetAnimation& anim)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure(what, load(motion, anim, buffer.data(), (S32)buffer.size()));
        };

        {
            AssetAnimation anim = two_joint_animation();
            anim.mDuration = MAX_ANIM_DURATION + 1.f;
            refuses("a duration past the limit is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mDuration = nan;
            refuses("a duration that is not a number is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mDuration = infinity;
            refuses("an infinite duration is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mLoopInPoint = nan;
            refuses("a loop in point that is not a number is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mLoopOutPoint = infinity;
            refuses("an infinite loop out point is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mEaseIn = nan;
            refuses("an ease in that is not a number is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mEaseOut = infinity;
            refuses("an infinite ease out is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mHandPose = LLHandMotion::NUM_HAND_POSES + 1;
            refuses("a hand pose off the end of the list is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mBasePriority = LLJoint::USE_MOTION_PRIORITY - 1;
            refuses("a base priority below the lowest is refused", anim);
        }
        {
            // clamped rather than refused, which is what it has always done
            AssetAnimation anim = two_joint_animation();
            anim.mBasePriority = LLJoint::ADDITIVE_PRIORITY;
            accepts("a base priority at the additive one is taken and clamped", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints.clear();
            anim.mClaimedJoints = 0;
            refuses("an animation with no joints is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mClaimedJoints = LL_CHARACTER_MAX_ANIMATED_JOINTS + 1;
            refuses("more joints than a skeleton has is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mClaimedJoints = 5;
            refuses("a joint count larger than the body is refused", anim);
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<12>()
    {
        // The joints, the same way.
        std::vector<U8> buffer(8192);

        auto refuses = [&](const char* what, const AssetAnimation& anim)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure(what, !load(motion, anim, buffer.data(), (S32)buffer.size()));
        };

        for (const char* special : { "mRoot", "mScreen" })
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mName = special;
            refuses("an animation naming a special joint is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mPriority = LLJoint::USE_MOTION_PRIORITY - 1;
            refuses("a joint priority below the lowest is refused", anim);
        }
        {
            // The priority becomes a shift of 0xff by seven less than it, so
            // anything above seven runs off the end of the word.
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mPriority = LL_CHARACTER_MAX_PRIORITY + 1;
            refuses("a joint priority above the highest is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mPriority = 0x7fffffff;
            refuses("and so is one as large as it will go", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mClaimedRotationKeys = -1000;
            refuses("a negative rotation key count is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mClaimedPositionKeys = -1;
            refuses("a negative position key count is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[0].mClaimedRotationKeys = 100000;
            refuses("more rotation keys than the body holds is refused", anim);
        }
        {
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[1].mClaimedPositionKeys = 0x7ffffff;
            refuses("more position keys than the body holds is refused", anim);
        }
        {
            // Not refused: an unknown joint is dropped and the rest is kept,
            // because content outlives skeletons.
            AssetAnimation anim = two_joint_animation();
            anim.mJoints[1].mName = "mNotAJointAtAll";
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure("an unknown joint does not sink the animation",
                   load(motion, anim, buffer.data(), (S32)buffer.size()));
            ensure_equals("and its joint motion is still there", motion.getNumJointMotions(), 2);
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<13>()
    {
        // Constraints. The chain length is the one that used to run off the
        // end of five fixed arrays.
        std::vector<U8> buffer(8192);

        auto refuses = [&](const char* what, const AssetAnimation& anim)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure(what, !load(motion, anim, buffer.data(), (S32)buffer.size()));
        };
        auto accepts = [&](const char* what, const AssetAnimation& anim)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure(what, load(motion, anim, buffer.data(), (S32)buffer.size()));
        };

        {
            AssetAnimation anim = constrained_animation(1);
            anim.mConstraints[0].mConstraintType = NUM_CONSTRAINT_TYPES;
            refuses("a constraint type off the end of the list is refused", anim);
        }
        {
            AssetAnimation anim = constrained_animation(1);
            anim.mConstraints[0].mConstraintType = 200;
            refuses("and so is one nowhere near the list", anim);
        }
        {
            AssetAnimation anim = constrained_animation(1);
            anim.mConstraints[0].mSourceVolume = "mNotAVolume";
            refuses("a constraint anchored to nothing is refused", anim);
        }
        {
            AssetAnimation anim = constrained_animation(1);
            anim.mClaimedConstraints = 500;
            accepts("too many constraints are ignored rather than refused", anim);
        }
        {
            AssetAnimation anim = constrained_animation(1);
            anim.mClaimedConstraints = -1000;
            accepts("and so is a negative count", anim);
        }
        {
            AssetAnimation anim = constrained_animation(1);
            anim.mClaimedConstraints = 3;
            refuses("a constraint count larger than the body is refused", anim);
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<14>()
    {
        // Cut anywhere and it is refused rather than half read. Every prefix
        // of a well formed animation, so the cut lands in each field in turn,
        // including inside every key of every curve.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = two_joint_animation();

        LLDataPackerBinaryBuffer writer(buffer.data(), (S32)buffer.size());
        pack_animation(writer, anim);
        const S32 whole = writer.getCurrentSize();
        ensure("the animation is worth cutting up", whole > 40);

        for (S32 length = 0; length < whole; ++length)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            motion.setCharacter(&mCharacter);
            LLDataPackerBinaryBuffer reader(buffer.data(), length);
            ensure("an animation cut short is refused", !motion.deserialize(reader, motion.getID()));
        }

        LLKeyframeMotion motion(LLUUID::generateNewID());
        motion.setCharacter(&mCharacter);
        LLDataPackerBinaryBuffer reader(buffer.data(), whole);
        ensure("and the whole of it is not", motion.deserialize(reader, motion.getID()));
    }

    template<> template<>
    void llkeyframemotion_object::test<15>()
    {
        // The same for a constrained animation, whose tail is the part with
        // the volume names and the four ease times in it.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = constrained_animation(2);

        LLDataPackerBinaryBuffer writer(buffer.data(), (S32)buffer.size());
        pack_animation(writer, anim);
        const S32 whole = writer.getCurrentSize();

        for (S32 length = 0; length < whole; ++length)
        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            motion.setCharacter(&mCharacter);
            LLDataPackerBinaryBuffer reader(buffer.data(), length);
            ensure("a constrained animation cut short is refused",
                   !motion.deserialize(reader, motion.getID()));
        }

        LLKeyframeMotion motion(LLUUID::generateNewID());
        motion.setCharacter(&mCharacter);
        LLDataPackerBinaryBuffer reader(buffer.data(), whole);
        ensure("and the whole of it is not", motion.deserialize(reader, motion.getID()));
    }

    template<> template<>
    void llkeyframemotion_object::test<16>()
    {
        // What an animation was read into is kept so that playing it again
        // costs nothing, and it belongs to the cache and to every motion
        // playing it at once rather than to whichever of them was last.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = two_joint_animation();

        ensure_equals("the cache starts empty", LLKeyframeDataCache::size(), 0u);

        LLKeyframeMotion motion(LLUUID::generateNewID());
        ensure("the animation is read", load(motion, anim, buffer.data(), (S32)buffer.size()));

        ensure_equals("and kept", LLKeyframeDataCache::size(), 1u);
        LLKeyframeMotion::JointMotionList* held = LLKeyframeDataCache::getKeyframeData(motion.getID());
        ensure("under the id it was read for", held != nullptr);
        ensure_equals("with the joints it was read with", held->getNumJointMotions(), 2u);

        // Counted rather than owned: the cache has one hold on it and the
        // motion reading it has the other.
        ensure_equals("two things are holding it", held->getNumRefs(), 2);

        {
            LLKeyframeMotion second(motion.getID());
            ensure_equals("a second motion reading it makes three",
                          second.onInitialize(&mCharacter), LLMotion::STATUS_SUCCESS);
            ensure_equals("holds", held->getNumRefs(), 3);
        }
        ensure_equals("and letting go puts it back", held->getNumRefs(), 2);
    }

    template<> template<>
    void llkeyframemotion_object::test<17>()
    {
        // Emptying the cache does not stop an animation somebody is playing.
        // This is what the cache could not do before: it held the animation
        // itself, so letting go of one took it out from under the motion.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = two_joint_animation();

        LLKeyframeMotion motion(LLUUID::generateNewID());
        ensure("the animation is read", load(motion, anim, buffer.data(), (S32)buffer.size()));
        const F32 duration = motion.getDuration();
        ensure("it has a duration to lose", duration > 0.f);

        LLKeyframeDataCache::clear();
        ensure_equals("the cache lets go of it", LLKeyframeDataCache::size(), 0u);
        ensure("and no longer hands it out",
               LLKeyframeDataCache::getKeyframeData(motion.getID()) == nullptr);

        ensure_approximately_equals("the motion still has it", motion.getDuration(), duration, 16);
        ensure_equals("whole", motion.getNumJointMotions(), 2);

        // And it still plays, which reads the curves the cache used to own.
        LLJointState* state = sampleAt(motion, 0.5f, "mPelvis");
        ensure("a motion whose cache entry is gone still animates", state != nullptr);
    }

    template<> template<>
    void llkeyframemotion_object::test<18>()
    {
        // Removing one by name is the same: it goes from the cache and stays
        // with whoever is playing it.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = two_joint_animation();

        LLKeyframeMotion motion(LLUUID::generateNewID());
        ensure("the animation is read", load(motion, anim, buffer.data(), (S32)buffer.size()));

        LLKeyframeDataCache::removeKeyframeData(motion.getID());
        ensure_equals("the cache lets go of it", LLKeyframeDataCache::size(), 0u);
        ensure_equals("the motion does not", motion.getNumJointMotions(), 2);

        // Removing one nobody has is not an error.
        LLKeyframeDataCache::removeKeyframeData(LLUUID::generateNewID());
        ensure_equals("and the cache is no worse for it", LLKeyframeDataCache::size(), 0u);
    }

    template<> template<>
    void llkeyframemotion_object::test<19>()
    {
        // The purge drops what no motion is holding and keeps what one is,
        // which is the whole of how the cache is allowed to end a session
        // smaller than it would otherwise be.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = two_joint_animation();

        LLKeyframeMotion kept(LLUUID::generateNewID());
        ensure("the first animation is read", load(kept, anim, buffer.data(), (S32)buffer.size()));

        LLUUID dropped_id;
        {
            LLKeyframeMotion dropped(LLUUID::generateNewID());
            dropped_id = dropped.getID();
            ensure("the second is read", load(dropped, anim, buffer.data(), (S32)buffer.size()));
            ensure_equals("both are in the cache", LLKeyframeDataCache::size(), 2u);

            LLKeyframeDataCache::purge();
            ensure_equals("a purge keeps what is being played", LLKeyframeDataCache::size(), 2u);
        }

        // The second motion is gone, so nothing can play what it was reading.
        LLKeyframeDataCache::purge();
        ensure_equals("a purge drops what nobody is playing", LLKeyframeDataCache::size(), 1u);
        ensure("the one that went is the one nobody had",
               LLKeyframeDataCache::getKeyframeData(dropped_id) == nullptr);
        ensure("and the one that stayed is the one somebody had",
               LLKeyframeDataCache::getKeyframeData(kept.getID()) != nullptr);
        ensure_equals("still whole", kept.getNumJointMotions(), 2);
    }

    template<> template<>
    void llkeyframemotion_object::test<20>()
    {
        // Two motions reading the same animation share the one copy of it,
        // and it outlives either of them going.
        std::vector<U8> buffer(8192);
        const AssetAnimation anim = two_joint_animation();

        const LLUUID id = LLUUID::generateNewID();
        LLKeyframeMotion::JointMotionList* first_list = nullptr;

        LLKeyframeMotion second(id);
        {
            LLKeyframeMotion first(id);
            ensure("the animation is read", load(first, anim, buffer.data(), (S32)buffer.size()));
            first_list = LLKeyframeDataCache::getKeyframeData(id);
            ensure("and kept", first_list != nullptr);

            // The second finds it in the cache rather than reading it again,
            // which is the path every repeat play of an animation takes.
            ensure_equals("the second motion is initialized from the cache",
                          second.onInitialize(&mCharacter), LLMotion::STATUS_SUCCESS);
            ensure_equals("with the joints the first one read",
                          second.getNumJointMotions(), 2);
        }

        LLKeyframeDataCache::purge();
        ensure_equals("one motion going does not take the animation with it",
                      LLKeyframeDataCache::size(), 1u);
        ensure_equals("and it is the same one", LLKeyframeDataCache::getKeyframeData(id), first_list);
    }

    template<> template<>
    void llkeyframemotion_object::test<21>()
    {
        // A looping animation whose keys stop before its loop does. Between
        // the last key and the loop out point the curve used to hold the last
        // key and then arrive at the loop's first pose in one frame; it leads
        // back to it now.
        PositionCurve curve;
        set_key(curve, 0.f, LLVector3(0.f, 0.f, 0.f));
        set_key(curve, 1.f, LLVector3(10.f, 0.f, 0.f));

        // Without a seam the tail holds the last key, which is what a motion
        // that does not loop wants.
        ensure_approximately_equals_range("no seam: the tail holds the last key",
                                          sample(curve, 1.5f).mV[VX], 10.f, 1e-4f);

        // The loop runs to two, a second past the last key, and starts from
        // the pose at zero.
        curve.setLoopSeam(true, 0.f, 2.f);

        ensure_approximately_equals_range("the last key is still the last key",
                                          sample(curve, 1.f).mV[VX], 10.f, 1e-4f);
        ensure_approximately_equals_range("half way along the tail is half way back",
                                          sample(curve, 1.5f).mV[VX], 5.f, 1e-4f);
        ensure_approximately_equals_range("and the end of the tail is the loop's first pose",
                                          sample(curve, 2.f).mV[VX], 0.f, 1e-4f);
        ensure_approximately_equals_range("past the loop out point it stays there",
                                          sample(curve, 3.f).mV[VX], 0.f, 1e-4f);

        // Inside the keys nothing changed.
        ensure_approximately_equals_range("the keyed range is untouched",
                                          sample(curve, 0.5f).mV[VX], 5.f, 1e-4f);

        // Told it does not loop, the tail goes away again.
        curve.setLoopSeam(false, 0.f, 2.f);
        ensure_approximately_equals_range("without a loop the tail holds again",
                                          sample(curve, 1.5f).mV[VX], 10.f, 1e-4f);
    }

    template<> template<>
    void llkeyframemotion_object::test<22>()
    {
        // A loop that ends on its last key has no tail to lead anywhere, and
        // one that ends before its last key never reaches it.
        PositionCurve curve;
        set_key(curve, 0.f, LLVector3(0.f, 0.f, 0.f));
        set_key(curve, 1.f, LLVector3(10.f, 0.f, 0.f));

        curve.setLoopSeam(true, 0.f, 1.f);
        ensure_approximately_equals_range("a loop ending on the last key holds past it",
                                          sample(curve, 1.5f).mV[VX], 10.f, 1e-4f);

        curve.setLoopSeam(true, 0.f, 0.5f);
        ensure_approximately_equals_range("a loop ending before the last key holds too",
                                          sample(curve, 1.5f).mV[VX], 10.f, 1e-4f);

        // A curve with one key has nowhere to lead from.
        PositionCurve single;
        set_key(single, 0.f, LLVector3(4.f, 0.f, 0.f));
        single.setLoopSeam(true, 0.f, 2.f);
        ensure_approximately_equals_range("one key is the whole animation",
                                          sample(single, 1.f).mV[VX], 4.f, 1e-4f);

        // An empty curve is asked nothing and says nothing.
        PositionCurve empty;
        empty.setLoopSeam(true, 0.f, 2.f);
        ensure_equals("an empty curve has no keys", empty.getNumKeys(), 0u);
    }

    template<> template<>
    void llkeyframemotion_object::test<23>()
    {
        // The loop's first pose is read from the keys, not from a tail left
        // over from the last time the seam was set up.
        PositionCurve curve;
        set_key(curve, 0.f, LLVector3(0.f, 0.f, 0.f));
        set_key(curve, 1.f, LLVector3(10.f, 0.f, 0.f));

        curve.setLoopSeam(true, 0.f, 2.f);
        ensure_approximately_equals_range("the tail leads back to zero",
                                          sample(curve, 1.5f).mV[VX], 5.f, 1e-4f);

        // Setting it up again against a later loop in point reads that pose,
        // and reads it from the keys rather than through the tail it already
        // has.
        curve.setLoopSeam(true, 0.5f, 2.f);
        ensure_approximately_equals_range("a later loop in point is read from the keys",
                                          sample(curve, 1.5f).mV[VX], 7.5f, 1e-4f);
        curve.setLoopSeam(true, 0.5f, 2.f);
        ensure_approximately_equals_range("and reads the same the second time",
                                          sample(curve, 1.5f).mV[VX], 7.5f, 1e-4f);

        // A rotation curve leads back the same way.
        RotationCurve rotations;
        set_key(rotations, 0.f, LLQuaternion(0.f, LLVector3::z_axis));
        set_key(rotations, 1.f, LLQuaternion(F_PI_BY_TWO, LLVector3::z_axis));
        rotations.setLoopSeam(true, 0.f, 2.f);

        const LLQuaternion at_end = sample(rotations, 2.f);
        // q and -q are the same rotation, so compare through the dot product.
        ensure_approximately_equals_range("the rotation tail ends on the loop's first pose",
                                          fabsf(dot(at_end, LLQuaternion(0.f, LLVector3::z_axis))),
                                          1.f, 1e-4f);
    }

    template<> template<>
    void llkeyframemotion_object::test<24>()
    {
        // Sampling asks the character for the hand pose the animation carries.
        // The request has to outlive the animation: the motion can go, and the
        // cache can let the animation go after it, before the hand motion gets
        // to read what was asked for.
        std::vector<U8> buffer(8192);
        AssetAnimation anim = two_joint_animation();
        anim.mHandPose = LLHandMotion::HAND_POSE_FIST;

        {
            LLKeyframeMotion motion(LLUUID::generateNewID());
            ensure("the animation is read", load(motion, anim, buffer.data(), (S32)buffer.size()));
            ensure("nothing is asked for before it is sampled", !mCharacter.hasHandPoseRequest());
            sampleAt(motion, 0.f, "mPelvis");
            ensure("sampling asks for a hand pose", mCharacter.hasHandPoseRequest());
            ensure_equals("the one the animation carries",
                          mCharacter.getHandPoseRequest(), (S32)LLHandMotion::HAND_POSE_FIST);
            ensure_equals("at the animation's highest joint priority",
                          mCharacter.getHandPoseRequestPriority(), (S32)LLJoint::HIGH_PRIORITY);
        }
        LLKeyframeDataCache::purge();
        ensure_equals("the animation is gone", LLKeyframeDataCache::size(), 0u);
        ensure("the request is not", mCharacter.hasHandPoseRequest());
        ensure_equals("and still reads the pose",
                      mCharacter.getHandPoseRequest(), (S32)LLHandMotion::HAND_POSE_FIST);

        // A motion asking at a lower priority does not take the request away.
        anim.mHandPose = LLHandMotion::HAND_POSE_POINT;
        anim.mJoints[0].mPriority = LLJoint::MEDIUM_PRIORITY;
        LLKeyframeMotion lower(LLUUID::generateNewID());
        ensure("the lower priority animation is read", load(lower, anim, buffer.data(), (S32)buffer.size()));
        sampleAt(lower, 0.f, "mPelvis");
        ensure_equals("a lower priority motion leaves the request standing",
                      mCharacter.getHandPoseRequest(), (S32)LLHandMotion::HAND_POSE_FIST);

        // One asking at the same priority, later, does.
        anim.mHandPose = LLHandMotion::HAND_POSE_PEACE_R;
        anim.mJoints[0].mPriority = LLJoint::HIGH_PRIORITY;
        LLKeyframeMotion equal(LLUUID::generateNewID());
        ensure("the equal priority animation is read", load(equal, anim, buffer.data(), (S32)buffer.size()));
        sampleAt(equal, 0.f, "mPelvis");
        ensure_equals("an equal priority motion asking later takes the request",
                      mCharacter.getHandPoseRequest(), (S32)LLHandMotion::HAND_POSE_PEACE_R);
    }
}
