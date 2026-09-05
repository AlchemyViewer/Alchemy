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

#include <cmath>
#include <map>
#include <random>

#include "../test/lltut.h"

namespace
{
    typedef LLKeyframeMotion::InterpolationType InterpolationType;
    typedef LLKeyframeMotion::PositionCurve PositionCurve;
    typedef LLKeyframeMotion::RotationCurve RotationCurve;

    LLVector3 blend_reference(F32 u, const LLVector3& before, const LLVector3& after)
    {
        return lerp(before, after, u);
    }

    LLQuaternion blend_reference(F32 u, const LLQuaternion& before, const LLQuaternion& after)
    {
        return nlerp(u, before, after);
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
    template <typename T>
    void build(Rng& rng, LLKeyframeMotion::KeyCurve<T>& curve, MapCurve<T>& reference, InterpolationType type)
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
            curve.setKey(time, value);
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

    // Every way of sampling a curve without a cursor: far outside the keys,
    // random within them, and exactly on each one and a step either side.
    template <typename T>
    void ensure_stateless_parity(const char* what, Rng& rng, const LLKeyframeMotion::KeyCurve<T>& curve, const MapCurve<T>& reference)
    {
        for (U32 i = 0; i < 2000; ++i)
        {
            const F32 time = rng.uniform(-0.5f, DURATION + 0.5f);
            ensure_same_value(what, time, curve.getValue(time), reference.getValue(time));
        }
        for (U32 k = 0; k < curve.getNumKeys(); ++k)
        {
            const F32 time = curve.getKeyTime(k);
            const F32 above = std::nextafter(time, DURATION * 2.f);
            const F32 below = std::nextafter(time, -DURATION);
            ensure_same_value(what, time, curve.getValue(time), reference.getValue(time));
            ensure_same_value(what, above, curve.getValue(above), reference.getValue(above));
            ensure_same_value(what, below, curve.getValue(below), reference.getValue(below));
        }
    }

    // Playback: time only ever moves forward, by a step in [min_step,
    // max_step], until it passes the end and wraps to the start the way a
    // looping motion does. Every sample must match the map, and the cursor
    // must be exactly where the map's search would have landed.
    template <typename T>
    U32 ensure_playback_parity(const char* what, Rng& rng, const LLKeyframeMotion::KeyCurve<T>& curve, const MapCurve<T>& reference,
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
            ensure_same_value(what, time, curve.getValue(time, cursor), reference.getValue(time));
            tut::ensure_equals(std::string(what) + " cursor at t=" + std::to_string(time), cursor, reference.lowerBound(time));
        }
        return wraps;
    }
}

namespace tut
{
    struct llkeyframemotion_data
    {
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
        curve.setKey(2.f, LLVector3(2.f, 0.f, 0.f));
        curve.setKey(1.f, LLVector3(1.f, 0.f, 0.f));
        curve.setKey(3.f, LLVector3(3.f, 0.f, 0.f));
        curve.setKey(2.f, LLVector3(0.f, 2.f, 0.f));
        ensure_equals("a repeated time does not add a key", curve.getNumKeys(), 3u);
        ensure_equals("keys are sorted: first", curve.getKeyTime(0), 1.f);
        ensure_equals("keys are sorted: second", curve.getKeyTime(1), 2.f);
        ensure_equals("keys are sorted: third", curve.getKeyTime(2), 3.f);
        ensure("the last value at a time wins", curve.getKeyValue(1) == LLVector3(0.f, 2.f, 0.f));

        Rng rng;
        RotationCurve rotations;
        MapCurve<LLQuaternion> reference;
        build(rng, rotations, reference, LLKeyframeMotion::IT_LINEAR);
        ensure_equals("random keys: count matches the map", rotations.getNumKeys(), static_cast<U32>(reference.mKeys.size()));
        U32 k = 0;
        for (const auto& key : reference.mKeys)
        {
            ensure_equals("random keys: time matches the map", rotations.getKeyTime(k), key.first);
            ensure("random keys: value matches the map", rotations.getKeyValue(k) == key.second);
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
                ensure_same_value("exact key under any cursor", time, curve.getValue(time, cursor), reference.getValue(time));
                ensure_equals("exact key leaves the cursor on the key", cursor, k);

                cursor = hint;
                const F32 above = std::nextafter(time, DURATION * 2.f);
                ensure_same_value("just past a key under any cursor", above, curve.getValue(above, cursor), reference.getValue(above));
                ensure_equals("just past a key leaves the cursor after it", cursor, k + 1);
            }
        }
    }

    template<> template<>
    void llkeyframemotion_object::test<5>()
    {
        // No keys: the identity of the channel. One key: that key, wherever
        // the sample falls, with the cursor before or after it.
        PositionCurve no_positions;
        ensure("an empty position curve is the zero vector", no_positions.getValue(1.f) == LLVector3());
        RotationCurve no_rotations;
        ensure("an empty rotation curve is the identity", no_rotations.getValue(1.f) == LLQuaternion());

        RotationCurve one;
        const LLQuaternion key(0.5f, LLVector3(0.f, 1.f, 0.f));
        one.setKey(1.f, key);
        U32 cursor = 0;
        ensure("before the only key", one.getValue(0.f, cursor) == key);
        ensure_equals("before the only key: cursor", cursor, 0u);
        ensure("on the only key", one.getValue(1.f, cursor) == key);
        ensure_equals("on the only key: cursor", cursor, 0u);
        ensure("after the only key", one.getValue(2.f, cursor) == key);
        ensure_equals("after the only key: cursor", cursor, 1u);
        ensure("back before the only key", one.getValue(0.5f, cursor) == key);
        ensure_equals("back before the only key: cursor", cursor, 0u);
    }
}
