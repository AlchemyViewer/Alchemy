/**
 * @file alcurvemodel_test.cpp
 * @brief Unit tests for the curve editor's shape model
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../alcurvemodel.h"

#include <algorithm>
#include <cmath>

namespace tut
{
    struct curve_data
    {
        ALCurveModel mCurve;

        /// cg_sCurve transcribed straight from
        /// class1/alchemy/colorGradeUtilF.glsl, with the uCurveInvRange that
        /// pipeline.cpp uploads folded in. Written out longhand on purpose:
        /// the point of the comparison is that it is an independent
        /// transcription of the shader, not a call back into the model.
        static F32 shaderCurve(F32 x, F32 toe, F32 shoulder, F32 strength)
        {
            F32 inv_range = 1.0f / std::max(shoulder - toe, 1e-4f);
            F32 t = (x - toe) * inv_range;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            F32 s = t * t * (3.0f - 2.0f * t);
            F32 k = strength < 0.0f ? 0.0f : (strength > 1.0f ? 1.0f : strength);
            return x * (1.0f - k) + s * k;      // mix(x, s, k)
        }

        /// applySplitToning's three luma masks, transcribed the same way and
        /// for the same reason: an independent copy of the shader, so a change
        /// to either side shows up as a disagreement rather than as two
        /// matching edits.
        static void shaderSplitWeights(F32 l, F32 mid, F32& lo, F32& md, F32& hi)
        {
            auto ss = [](F32 e0, F32 e1, F32 x) {
                F32 t = (x - e0) / (e1 - e0);
                t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                return t * t * (3.0f - 2.0f * t);
            };
            hi = ss(mid, mid + 0.35f, l);
            lo = 1.0f - ss(mid - 0.35f, mid, l);
            md = std::max(1.0f - hi - lo, 0.0f);
        }

        static std::vector<ALCurveModel::Point> pts(std::initializer_list<std::pair<F32, F32>> in)
        {
            std::vector<ALCurveModel::Point> out;
            for (const auto& p : in)
            {
                out.push_back(ALCurveModel::Point{ p.first, p.second });
            }
            return out;
        }
    };

    typedef test_group<curve_data> curve_group;
    typedef curve_group::object    curve_object;
    tut::curve_group cg("ALCurveModel");

    // --- smoothstep: agreement with the shader -------------------------------

    // Zero strength is the identity, whatever the toe and shoulder say.
    template<> template<>
    void curve_object::test<1>()
    {
        mCurve.setSmoothstep(0.2f, 0.8f, 0.f);
        for (S32 i = 0; i <= 10; ++i)
        {
            const F32 x = i * 0.1f;
            ensure_approximately_equals("identity at strength 0", mCurve.evaluate(x), x, 6);
        }
    }

    // Full strength is a pure smoothstep between toe and shoulder.
    template<> template<>
    void curve_object::test<2>()
    {
        mCurve.setSmoothstep(0.25f, 0.75f, 1.f);
        ensure_approximately_equals("flat below the toe", mCurve.evaluate(0.1f), 0.f, 6);
        ensure_approximately_equals("flat above the shoulder", mCurve.evaluate(0.9f), 1.f, 6);
        ensure_approximately_equals("midpoint", mCurve.evaluate(0.5f), 0.5f, 6);
    }

    // The model and an independent transcription of the shader agree across a
    // sweep of parameters, including the degenerate shoulder <= toe.
    template<> template<>
    void curve_object::test<3>()
    {
        const F32 toes[]      = { 0.f, 0.1f, 0.35f, 0.6f };
        const F32 shoulders[] = { 0.05f, 0.4f, 0.85f, 1.f };
        const F32 strengths[] = { 0.f, 0.25f, 0.7f, 1.f };

        for (F32 toe : toes)
        {
            for (F32 shoulder : shoulders)
            {
                for (F32 strength : strengths)
                {
                    for (S32 i = 0; i <= 20; ++i)
                    {
                        const F32 x = i * 0.05f;
                        ensure_approximately_equals(
                            "model matches the shader",
                            ALCurveModel::smoothstep(x, toe, shoulder, strength),
                            shaderCurve(x, toe, shoulder, strength), 6);
                    }
                }
            }
        }
    }

    // A shoulder at or below the toe must not divide by zero; it degenerates
    // to a step at the toe, which is what the shader's guarded reciprocal does.
    template<> template<>
    void curve_object::test<4>()
    {
        const F32 y_below = ALCurveModel::smoothstep(0.3f, 0.5f, 0.5f, 1.f);
        const F32 y_above = ALCurveModel::smoothstep(0.7f, 0.5f, 0.5f, 1.f);
        ensure("finite below", std::isfinite(y_below));
        ensure("finite above", std::isfinite(y_above));
        ensure_approximately_equals("black below the toe", y_below, 0.f, 5);
        ensure_approximately_equals("white above the toe", y_above, 1.f, 5);
    }

    // Strength outside 0..1 is clamped, matching pipeline.cpp's llclamp on the
    // uniform it uploads.
    template<> template<>
    void curve_object::test<5>()
    {
        ensure_approximately_equals("over-strength clamps to 1",
                                    ALCurveModel::smoothstep(0.3f, 0.f, 1.f, 4.f),
                                    ALCurveModel::smoothstep(0.3f, 0.f, 1.f, 1.f), 6);
        ensure_approximately_equals("negative strength clamps to 0",
                                    ALCurveModel::smoothstep(0.3f, 0.f, 1.f, -2.f),
                                    0.3f, 6);
    }

    // --- spline: ordering and point management -------------------------------

    // A fresh model is the identity ramp under either kind.
    template<> template<>
    void curve_object::test<6>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        ensure_equals("two points by default", mCurve.getPointCount(), 2);
        for (S32 i = 0; i <= 10; ++i)
        {
            const F32 x = i * 0.1f;
            ensure_approximately_equals("identity ramp", mCurve.evaluate(x), x, 5);
        }
    }

    // Points added out of order come back sorted by x, and the reported index
    // is where the point actually landed.
    template<> template<>
    void curve_object::test<7>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        const S32 idx_hi = mCurve.addPoint(0.8f, 0.9f);
        const S32 idx_lo = mCurve.addPoint(0.2f, 0.1f);

        ensure_equals("high point went before the last", idx_hi, 1);
        ensure_equals("low point went before the high one", idx_lo, 1);
        ensure_equals("four points", mCurve.getPointCount(), 4);

        const auto& p = mCurve.getPoints();
        for (size_t i = 1; i < p.size(); ++i)
        {
            ensure("x is ascending", p[i].mX > p[i - 1].mX);
        }
        ensure_approximately_equals("second point is the low one", p[1].mX, 0.2f, 5);
        ensure_approximately_equals("third point is the high one", p[2].mX, 0.8f, 5);
    }

    // Coincident points are pushed apart rather than sharing an x, so no
    // segment can have zero width.
    template<> template<>
    void curve_object::test<8>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.3f }, { 0.5f, 0.7f }, { 1.f, 1.f } }));

        const auto& p = mCurve.getPoints();
        ensure_equals("kept all four", (S32)p.size(), 4);
        for (size_t i = 1; i < p.size(); ++i)
        {
            ensure("gap is at least MIN_POINT_GAP",
                   p[i].mX - p[i - 1].mX >= ALCurveModel::MIN_POINT_GAP - 1e-6f);
        }
        ensure("evaluation is finite", std::isfinite(mCurve.evaluate(0.5f)));
    }

    // A drag cannot push a point past its neighbours, and locked endpoints
    // keep their x while still accepting a new height.
    template<> template<>
    void curve_object::test<9>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.3f, 0.3f }, { 0.6f, 0.6f }, { 1.f, 1.f } }));

        mCurve.movePoint(1, 0.95f, 0.5f);
        const auto& p = mCurve.getPoints();
        ensure("clamped below its right neighbour", p[1].mX < p[2].mX);
        ensure_approximately_equals("clamped to exactly the gap",
                                    p[1].mX, 0.6f - ALCurveModel::MIN_POINT_GAP, 5);
        ensure("still ordered", p[0].mX < p[1].mX && p[2].mX < p[3].mX);

        mCurve.movePoint(0, 0.4f, 0.25f);
        ensure_approximately_equals("first point x stays pinned", mCurve.getPoints()[0].mX, 0.f, 6);
        ensure_approximately_equals("first point y moved", mCurve.getPoints()[0].mY, 0.25f, 5);

        mCurve.movePoint(3, 0.4f, 0.8f);
        ensure_approximately_equals("last point x stays pinned", mCurve.getPoints()[3].mX, 1.f, 6);
    }

    // Removal refuses to break the curve: never below two points, and never an
    // endpoint while the endpoints are locked.
    template<> template<>
    void curve_object::test<10>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.4f }, { 1.f, 1.f } }));

        ensure("cannot remove the first", !mCurve.removePoint(0));
        ensure("cannot remove the last", !mCurve.removePoint(2));
        ensure("out of range refused", !mCurve.removePoint(7));
        ensure("interior removed", mCurve.removePoint(1));
        ensure_equals("two left", mCurve.getPointCount(), 2);
        ensure("cannot go below two", !mCurve.removePoint(0));
    }

    // --- spline: shape -------------------------------------------------------

    // The curve passes through every control point.
    template<> template<>
    void curve_object::test<11>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.05f }, { 0.25f, 0.5f }, { 0.7f, 0.6f }, { 1.f, 0.95f } }));

        for (const auto& p : mCurve.getPoints())
        {
            ensure_approximately_equals("interpolates its points",
                                        mCurve.evaluate(p.mX), p.mY, 4);
        }
    }

    // Monotone data yields a monotone curve. A natural or Catmull-Rom spline
    // fails this on exactly this shape -- a long flat run into a sharp rise
    // makes it dip below the flat before climbing.
    template<> template<>
    void curve_object::test<12>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.4f, 0.02f }, { 0.6f, 0.05f }, { 0.7f, 0.9f }, { 1.f, 1.f } }));

        std::vector<F32> s;
        mCurve.sample(s, 257);
        for (size_t i = 1; i < s.size(); ++i)
        {
            ensure("never decreases", s[i] >= s[i - 1] - 1e-5f);
        }
    }

    // A flat run stays exactly flat -- no ringing between equal-valued points.
    template<> template<>
    void curve_object::test<13>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.3f, 0.5f }, { 0.7f, 0.5f }, { 1.f, 1.f } }));

        for (S32 i = 0; i <= 8; ++i)
        {
            const F32 x = 0.3f + i * 0.05f;
            ensure_approximately_equals("flat between equal points", mCurve.evaluate(x), 0.5f, 4);
        }
    }

    // Outside the point range the curve holds its end values rather than
    // extrapolating off the graph.
    template<> template<>
    void curve_object::test<14>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setEndpointsLocked(false);
        mCurve.setPoints(pts({ { 0.2f, 0.3f }, { 0.8f, 0.7f } }));

        ensure_approximately_equals("holds the left value", mCurve.evaluate(0.f), 0.3f, 5);
        ensure_approximately_equals("holds the right value", mCurve.evaluate(1.f), 0.7f, 5);
    }

    // Output never leaves 0..1, so a curve can never ask the caller to write an
    // out-of-gamut value.
    template<> template<>
    void curve_object::test<15>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.05f, 0.99f }, { 0.1f, 0.01f }, { 1.f, 1.f } }));

        std::vector<F32> s;
        mCurve.sample(s, 129);
        for (F32 y : s)
        {
            ensure("in range", y >= 0.f && y <= 1.f);
            ensure("finite", std::isfinite(y));
        }
    }

    // --- sampling ------------------------------------------------------------

    // sample() spans 0..1 inclusive and honours its count.
    template<> template<>
    void curve_object::test<16>()
    {
        mCurve.setSmoothstep(0.1f, 0.9f, 1.f);

        std::vector<F32> s;
        mCurve.sample(s, 65);
        ensure_equals("count honoured", (S32)s.size(), 65);
        ensure_approximately_equals("starts at evaluate(0)", s.front(), mCurve.evaluate(0.f), 6);
        ensure_approximately_equals("ends at evaluate(1)", s.back(), mCurve.evaluate(1.f), 6);

        mCurve.sample(s, 1);
        ensure("a single sample is not a curve", s.empty());
        mCurve.sample(s, 0);
        ensure("zero samples", s.empty());
    }

    // Unlocking the endpoints lets the first and last points move horizontally;
    // re-locking snaps them back to the full domain.
    template<> template<>
    void curve_object::test<17>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setEndpointsLocked(false);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.5f }, { 1.f, 1.f } }));

        mCurve.movePoint(0, 0.2f, 0.1f);
        ensure_approximately_equals("unlocked end moved", mCurve.getPoints()[0].mX, 0.2f, 5);

        mCurve.setEndpointsLocked(true);
        ensure_approximately_equals("relock snaps to 0", mCurve.getPoints()[0].mX, 0.f, 6);
        ensure_approximately_equals("relock snaps to 1", mCurve.getPoints().back().mX, 1.f, 6);
    }

    // setPoints refuses to leave the model unusable.
    template<> template<>
    void curve_object::test<18>()
    {
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.3f, 0.3f } }));
        ensure_equals("degenerate input resets to a ramp", mCurve.getPointCount(), 2);
        ensure_approximately_equals("and it is the identity", mCurve.evaluate(0.5f), 0.5f, 5);
    }

    // Switching kinds does not disturb the other kind's state, so a widget can
    // offer both without a round trip losing anything.
    template<> template<>
    void curve_object::test<19>()
    {
        mCurve.setSmoothstep(0.2f, 0.7f, 0.6f);
        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.8f }, { 1.f, 1.f } }));

        mCurve.setKind(ALCurveModel::KIND_SMOOTHSTEP);
        ensure_approximately_equals("toe survived", mCurve.getToe(), 0.2f, 6);
        ensure_approximately_equals("shoulder survived", mCurve.getShoulder(), 0.7f, 6);
        ensure_approximately_equals("strength survived", mCurve.getStrength(), 0.6f, 6);

        mCurve.setKind(ALCurveModel::KIND_SPLINE);
        ensure_equals("points survived", mCurve.getPointCount(), 3);
        ensure_approximately_equals("and their values did",
                                    mCurve.evaluate(0.5f), 0.8f, 4);
    }

    // --- split-tone bands ----------------------------------------------------

    // Agreement with the shader, across the whole range of split points the
    // Balance slider can ask for.
    template<> template<>
    void curve_object::test<20>()
    {
        for (F32 mid : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
        {
            for (S32 i = 0; i <= 64; ++i)
            {
                const F32 l = (F32)i / 64.f;
                F32 lo, md, hi;
                shaderSplitWeights(l, mid, lo, md, hi);
                const auto w = ALCurveModel::splitToneWeights(l, mid);
                ensure_approximately_equals("shadow matches the shader",    w.mShadow,    lo, 5);
                ensure_approximately_equals("midtone matches the shader",   w.mMidtone,   md, 5);
                ensure_approximately_equals("highlight matches the shader", w.mHighlight, hi, 5);
            }
        }
    }

    // The three weights partition the luma range: they sum to one everywhere.
    // That is what makes the band graph honest -- a tone is never partly
    // untinted, only ever shared between neighbouring bands -- and it holds
    // because the two ramps meet at the split without overlapping, so the
    // midtone remainder is never clamped away.
    template<> template<>
    void curve_object::test<21>()
    {
        for (F32 mid : { 0.1f, 0.35f, 0.5f, 0.65f, 0.9f })
        {
            for (S32 i = 0; i <= 64; ++i)
            {
                const auto w = ALCurveModel::splitToneWeights((F32)i / 64.f, mid);
                ensure_approximately_equals("weights sum to one",
                                            w.mShadow + w.mMidtone + w.mHighlight, 1.f, 5);
                ensure("shadow in range",    w.mShadow    >= 0.f && w.mShadow    <= 1.f);
                ensure("midtone in range",   w.mMidtone   >= 0.f && w.mMidtone   <= 1.f);
                ensure("highlight in range", w.mHighlight >= 0.f && w.mHighlight <= 1.f);
            }
        }

        const F32 mid = 0.55f;
        const auto at_mid = ALCurveModel::splitToneWeights(mid, mid);
        ensure_approximately_equals("midtone peaks at the split", at_mid.mMidtone, 1.f, 5);
        ensure_approximately_equals("shadow is spent there", at_mid.mShadow, 0.f, 5);
        ensure_approximately_equals("highlight has not started", at_mid.mHighlight, 0.f, 5);

        const auto black = ALCurveModel::splitToneWeights(0.f, mid);
        const auto white = ALCurveModel::splitToneWeights(1.f, mid);
        ensure_approximately_equals("black is all shadow", black.mShadow, 1.f, 5);
        ensure_approximately_equals("white is all highlight", white.mHighlight, 1.f, 5);

        // Monotone, so the bands cannot cross back over themselves. If the
        // shader's smoothstep edges were ever swapped, this is what notices.
        F32 prev_hi = -1.f, prev_lo = 2.f;
        for (S32 i = 0; i <= 64; ++i)
        {
            const auto w = ALCurveModel::splitToneWeights((F32)i / 64.f, mid);
            ensure("highlight never falls", w.mHighlight >= prev_hi - 1e-5f);
            ensure("shadow never rises", w.mShadow <= prev_lo + 1e-5f);
            prev_hi = w.mHighlight;
            prev_lo = w.mShadow;
        }
    }

    // Balance and split point invert each other across the whole slider range,
    // which is what lets the graph's handle be read back into the setting
    // without the value creeping a little on every drag.
    template<> template<>
    void curve_object::test<22>()
    {
        for (S32 i = -10; i <= 10; ++i)
        {
            const F32 balance = (F32)i / 10.f;
            const F32 mid = ALCurveModel::splitToneMid(balance);
            ensure("split point stays in range", mid >= 0.1f - 1e-5f && mid <= 0.9f + 1e-5f);
            ensure_approximately_equals("balance round-trips",
                                        ALCurveModel::splitToneBalance(mid), balance, 5);
        }

        ensure_approximately_equals("neutral balance splits at mid grey",
                                    ALCurveModel::splitToneMid(0.f), 0.5f, 6);
        // Clamped, not wrapped: pipeline.cpp clamps the balance before it
        // uploads the split point, so the graph must agree rather than plot a
        // split the renderer will never use.
        ensure_approximately_equals("out-of-range balance clamps",
                                    ALCurveModel::splitToneMid(3.f), 0.9f, 6);
        ensure_approximately_equals("and so does the inverse",
                                    ALCurveModel::splitToneBalance(2.f), 1.f, 6);
    }
}
