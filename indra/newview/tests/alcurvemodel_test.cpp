/**
 * @file alcurvemodel_test.cpp
 * @brief Unit tests for the curve editor's shape model and the tone curve bake
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
#include <limits>

namespace tut
{
    struct curve_data
    {
        ALCurveModel mCurve;

        /// applySplitToning's three luma masks as the shader computed them
        /// BEFORE the widths became settings: two-edge smoothstep with the
        /// half-width written as the literal 0.35. Kept as the promise that
        /// the default widths render every existing Look unchanged. Written
        /// out longhand on purpose: the point of the comparison is that it is
        /// an independent transcription of the shader, not a call back into
        /// the model.
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

        /// cg_ramp and the current applySplitToning masks, transcribed from
        /// class1/alchemy/colorGradeUtilF.glsl in the scale/bias form the CPU
        /// uploads, with the CPU's own derivation of scale and bias written
        /// out here rather than fetched from the model.
        static F32 shaderRamp(F32 l, F32 scale, F32 bias)
        {
            F32 t = l * scale + bias;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return t * t * (3.0f - 2.0f * t);
        }
        static void shaderSplitWeightsRamped(F32 l, F32 mid, F32 ws, F32 wh, F32& lo, F32& md, F32& hi)
        {
            hi = shaderRamp(l, 1.0f / wh, -mid / wh);
            lo = 1.0f - shaderRamp(l, 1.0f / ws, -(mid - ws) / ws);
            md = std::max(1.0f - hi - lo, 0.0f);
        }

        /// applyChannelCurves' fetch: the half-texel mapping the shader applies,
        /// then GL's CLAMP_TO_EDGE + LINEAR filter over a row of n RGBA16
        /// texels, with the blend weight snapped to the 8 sub-texel bits the
        /// hardware is allowed to use.
        static F32 shaderLutFetch(const std::vector<U16>& texels, S32 n, S32 channel, F32 x)
        {
            F32 u  = x * (1.0f - 1.0f / (F32)n) + 0.5f / (F32)n;
            F32 tc = u * (F32)n - 0.5f;
            tc = tc < 0.0f ? 0.0f : (tc > (F32)(n - 1) ? (F32)(n - 1) : tc);
            S32 i0 = (S32)std::floor(tc);
            S32 i1 = std::min(i0 + 1, n - 1);
            F32 w  = std::floor((tc - (F32)i0) * 256.0f) / 256.0f;
            F32 a  = texels[(size_t)i0 * 4 + channel] / 65535.0f;
            F32 b  = texels[(size_t)i1 * 4 + channel] / 65535.0f;
            return a + (b - a) * w;
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

        static LLSD pair(const LLSD& a, const LLSD& b)
        {
            LLSD p = LLSD::emptyArray();
            p.append(a);
            p.append(b);
            return p;
        }
    };

    typedef test_group<curve_data> curve_group;
    typedef curve_group::object    curve_object;
    tut::curve_group cg("ALCurveModel");

    // --- spline: ordering and point management -------------------------------

    // A fresh model is the identity ramp.
    template<> template<>
    void curve_object::test<1>()
    {
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
    void curve_object::test<2>()
    {
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
    void curve_object::test<3>()
    {
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
    void curve_object::test<4>()
    {
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
    void curve_object::test<5>()
    {
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
    void curve_object::test<6>()
    {
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
    void curve_object::test<7>()
    {
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
    void curve_object::test<8>()
    {
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
    void curve_object::test<9>()
    {
        mCurve.setEndpointsLocked(false);
        mCurve.setPoints(pts({ { 0.2f, 0.3f }, { 0.8f, 0.7f } }));

        ensure_approximately_equals("holds the left value", mCurve.evaluate(0.f), 0.3f, 5);
        ensure_approximately_equals("holds the right value", mCurve.evaluate(1.f), 0.7f, 5);
    }

    // Output never leaves 0..1, so a curve can never ask the caller to write an
    // out-of-gamut value.
    template<> template<>
    void curve_object::test<10>()
    {
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
    void curve_object::test<11>()
    {
        mCurve.setPoints(pts({ { 0.f, 0.1f }, { 0.5f, 0.4f }, { 1.f, 0.9f } }));

        std::vector<F32> s;
        mCurve.sample(s, 65);
        ensure_equals("count honoured", (S32)s.size(), 65);
        ensure_approximately_equals("starts at evaluate(0)", s.front(), mCurve.evaluate(0.f), 6);
        ensure_approximately_equals("ends at evaluate(1)", s.back(), mCurve.evaluate(1.f), 6);
        ensure_approximately_equals("the last sample is the value at exactly 1", s.back(), 0.9f, 6);

        mCurve.sample(s, 1);
        ensure("a single sample is not a curve", s.empty());
        mCurve.sample(s, 0);
        ensure("zero samples", s.empty());
    }

    // Unlocking the endpoints lets the first and last points move horizontally;
    // re-locking snaps them back to the full domain.
    template<> template<>
    void curve_object::test<12>()
    {
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
    void curve_object::test<13>()
    {
        mCurve.setPoints(pts({ { 0.3f, 0.3f } }));
        ensure_equals("degenerate input resets to a ramp", mCurve.getPointCount(), 2);
        ensure_approximately_equals("and it is the identity", mCurve.evaluate(0.5f), 0.5f, 5);
    }

    // The point cap: addPoint refuses at MAX_POINTS, and setPoints truncates
    // while keeping both ends, so a locked curve still spans the domain.
    template<> template<>
    void curve_object::test<14>()
    {
        for (S32 i = 1; i <= ALCurveModel::MAX_POINTS - 2; ++i)
        {
            const F32 x = (F32)i / (F32)(ALCurveModel::MAX_POINTS - 1);
            ensure("added while under the cap", mCurve.addPoint(x, x) >= 0);
        }
        ensure_equals("at the cap", mCurve.getPointCount(), ALCurveModel::MAX_POINTS);
        ensure_equals("refused past the cap", mCurve.addPoint(0.123f, 0.5f), -1);
        ensure_equals("count unchanged", mCurve.getPointCount(), ALCurveModel::MAX_POINTS);

        // A stored curve may carry more than the editor adds, and keeps every
        // point: truncating a file would render a shape the file does not
        // describe. The stored cap is a backstop for absurd input only.
        std::vector<ALCurveModel::Point> many;
        for (S32 i = 0; i < 20; ++i)
        {
            const F32 x = (F32)i / 19.f;
            many.push_back(ALCurveModel::Point{ x, x * 0.5f });
        }
        mCurve.setPoints(many);
        ensure_equals("twenty stored points survive", mCurve.getPointCount(), 20);
        ensure_approximately_equals("interior point kept", mCurve.getPoints()[15].mX, 15.f / 19.f, 5);
        ensure_equals("but no further adds", mCurve.addPoint(0.123f, 0.5f), -1);

        std::vector<ALCurveModel::Point> absurd;
        for (S32 i = 0; i < 100; ++i)
        {
            const F32 x = (F32)i / 99.f;
            absurd.push_back(ALCurveModel::Point{ x, x * 0.5f });
        }
        mCurve.setPoints(absurd);
        ensure_equals("setPoints capped at the stored limit", mCurve.getPointCount(), ALCurveModel::MAX_STORED_POINTS);
        ensure_approximately_equals("first point kept at 0", mCurve.getPoints().front().mX, 0.f, 6);
        ensure_approximately_equals("last point kept at 1", mCurve.getPoints().back().mX, 1.f, 6);
        ensure_approximately_equals("and it is the original last point", mCurve.getPoints().back().mY, 0.5f, 5);
    }

    // --- split-tone bands ----------------------------------------------------

    // With the widths left at their default, the bands are what the shader
    // computed before the widths existed, across the whole range of split
    // points the Balance slider can ask for. This is the promise that every
    // Look saved before then renders unchanged.
    template<> template<>
    void curve_object::test<15>()
    {
        for (F32 mid : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
        {
            for (S32 i = 0; i <= 64; ++i)
            {
                const F32 l = (F32)i / 64.f;
                F32 lo, md, hi;
                shaderSplitWeights(l, mid, lo, md, hi);
                const auto w = ALCurveModel::splitToneWeights(l, mid);
                ensure_approximately_equals("shadow matches the old shader",    w.mShadow,    lo, 5);
                ensure_approximately_equals("midtone matches the old shader",   w.mMidtone,   md, 5);
                ensure_approximately_equals("highlight matches the old shader", w.mHighlight, hi, 5);
            }
        }
        ensure_approximately_equals("the default width is the old constant",
                                    ALCurveModel::SPLIT_TONE_DEFAULT_WIDTH, 0.35f, 6);
    }

    // The three weights partition the luma range: they sum to one everywhere.
    // That is what makes the band graph honest -- a tone is never partly
    // untinted, only ever shared between neighbouring bands -- and it holds
    // because the two ramps meet at the split without overlapping, so the
    // midtone remainder is never clamped away.
    template<> template<>
    void curve_object::test<16>()
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
        // shader's ramp edges were ever swapped, this is what notices.
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
    void curve_object::test<17>()
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
        // uploads the ramps, so the graph must agree rather than plot a split
        // the renderer will never use.
        ensure_approximately_equals("out-of-range balance clamps",
                                    ALCurveModel::splitToneMid(3.f), 0.9f, 6);
        ensure_approximately_equals("and so does the inverse",
                                    ALCurveModel::splitToneBalance(2.f), 1.f, 6);
    }

    // With independent widths the model still agrees with the shader, which
    // now takes each ramp as the {scale, bias} pair the CPU derives.
    template<> template<>
    void curve_object::test<18>()
    {
        const std::pair<F32, F32> widths[] = { { 0.05f, 0.5f }, { 0.35f, 0.35f }, { 0.5f, 0.02f }, { 0.2f, 0.1f } };
        for (F32 mid : { 0.1f, 0.5f, 0.9f })
        {
            for (const auto& wsh : widths)
            {
                for (S32 i = 0; i <= 64; ++i)
                {
                    const F32 l = (F32)i / 64.f;
                    F32 lo, md, hi;
                    shaderSplitWeightsRamped(l, mid, wsh.first, wsh.second, lo, md, hi);
                    const auto w = ALCurveModel::splitToneWeights(l, mid, wsh.first, wsh.second);
                    ensure_approximately_equals("shadow matches the shader",    w.mShadow,    lo, 5);
                    ensure_approximately_equals("midtone matches the shader",   w.mMidtone,   md, 5);
                    ensure_approximately_equals("highlight matches the shader", w.mHighlight, hi, 5);
                }
            }
        }
    }

    // Partition of unity survives asymmetric widths: the ramps still meet at
    // the split and only at the split.
    template<> template<>
    void curve_object::test<19>()
    {
        const std::pair<F32, F32> widths[] = { { 0.05f, 0.5f }, { 0.5f, 0.02f }, { 0.2f, 0.1f } };
        for (F32 mid : { 0.1f, 0.5f, 0.9f })
        {
            for (const auto& wsh : widths)
            {
                F32 prev_hi = -1.f, prev_lo = 2.f;
                for (S32 i = 0; i <= 64; ++i)
                {
                    const F32 l = (F32)i / 64.f;
                    const auto w = ALCurveModel::splitToneWeights(l, mid, wsh.first, wsh.second);
                    ensure_approximately_equals("weights sum to one",
                                                w.mShadow + w.mMidtone + w.mHighlight, 1.f, 5);
                    ensure("shadow in range",    w.mShadow    >= 0.f && w.mShadow    <= 1.f);
                    ensure("midtone in range",   w.mMidtone   >= 0.f && w.mMidtone   <= 1.f);
                    ensure("highlight in range", w.mHighlight >= 0.f && w.mHighlight <= 1.f);
                    ensure("highlight never falls", w.mHighlight >= prev_hi - 1e-5f);
                    ensure("shadow never rises", w.mShadow <= prev_lo + 1e-5f);
                    prev_hi = w.mHighlight;
                    prev_lo = w.mShadow;
                }
                const auto at_mid = ALCurveModel::splitToneWeights(mid, mid, wsh.first, wsh.second);
                ensure_approximately_equals("midtone peaks at the split", at_mid.mMidtone, 1.f, 5);
                // The ends are fully tinted only when the ramp fits on the
                // plot. A wide ramp under a low split runs off below zero and
                // black is then only partly shadow -- as it was with the old
                // fixed width at full negative balance, and as the band graph
                // shows.
                if (mid - wsh.first >= 0.f)
                {
                    ensure_approximately_equals("black is all shadow",
                                                ALCurveModel::splitToneWeights(0.f, mid, wsh.first, wsh.second).mShadow, 1.f, 5);
                }
                if (mid + wsh.second <= 1.f)
                {
                    ensure_approximately_equals("white is all highlight",
                                                ALCurveModel::splitToneWeights(1.f, mid, wsh.first, wsh.second).mHighlight, 1.f, 5);
                }
            }
        }
    }

    // A zero width is a step at the split, finite on both sides, and still a
    // partition -- the floor is applied before the shadow ramp's left edge is
    // formed, so the ramp ends at the split rather than short of it.
    template<> template<>
    void curve_object::test<20>()
    {
        const auto ramp = ALCurveModel::splitToneRamp(0.5f, 0.f);
        ensure("scale finite", std::isfinite(ramp.mScale));
        ensure("bias finite", std::isfinite(ramp.mBias));

        const auto below = ALCurveModel::splitToneWeights(0.49f, 0.5f, 0.f, 0.f);
        const auto above = ALCurveModel::splitToneWeights(0.51f, 0.5f, 0.f, 0.f);
        ensure_approximately_equals("all shadow just below the split", below.mShadow, 1.f, 5);
        ensure_approximately_equals("all highlight just above the split", above.mHighlight, 1.f, 5);
        for (S32 i = 0; i <= 64; ++i)
        {
            const auto w = ALCurveModel::splitToneWeights((F32)i / 64.f, 0.5f, 0.f, 0.f);
            ensure_approximately_equals("still sums to one", w.mShadow + w.mMidtone + w.mHighlight, 1.f, 5);
        }
    }

    // The scale/bias pair is the two-edge form folded: l*scale + bias is
    // (l - edge0) / width wherever the width is above the floor.
    template<> template<>
    void curve_object::test<21>()
    {
        const F32 edges[]  = { 0.f, 0.15f, 0.5f, 0.9f };
        const F32 widths[] = { 0.02f, 0.1f, 0.35f, 0.5f };
        for (F32 edge0 : edges)
        {
            for (F32 width : widths)
            {
                const auto ramp = ALCurveModel::splitToneRamp(edge0, width);
                for (S32 i = 0; i <= 20; ++i)
                {
                    const F32 l = (F32)i / 20.f;
                    ensure_approximately_equals("folded form agrees",
                                                l * ramp.mScale + ramp.mBias, (l - edge0) / width, 4);
                }
            }
        }
    }

    // The two-argument overload is the four-argument call at the default.
    template<> template<>
    void curve_object::test<22>()
    {
        for (F32 mid : { 0.1f, 0.5f, 0.9f })
        {
            for (S32 i = 0; i <= 32; ++i)
            {
                const F32 l = (F32)i / 32.f;
                const auto a = ALCurveModel::splitToneWeights(l, mid);
                const auto b = ALCurveModel::splitToneWeights(l, mid, 0.35f, 0.35f);
                ensure_equals("shadow bit for bit", a.mShadow, b.mShadow);
                ensure_equals("midtone bit for bit", a.mMidtone, b.mMidtone);
                ensure_equals("highlight bit for bit", a.mHighlight, b.mHighlight);
            }
        }
    }

    // --- ALToneCurveSet ------------------------------------------------------

    // A fresh set is identity in every channel, and bakes to the identity ramp.
    template<> template<>
    void curve_object::test<23>()
    {
        ALToneCurveSet set;
        ensure("identity by default", set.isIdentity());
        for (S32 c = 0; c < ALToneCurveSet::CH_COUNT; ++c)
        {
            for (S32 i = 0; i <= 20; ++i)
            {
                const F32 x = (F32)i / 20.f;
                ensure_approximately_equals("evaluates to x", set.evaluate((ALToneCurveSet::EChannel)c, x), x, 6);
            }
        }

        std::vector<U16> texels;
        set.bake(texels, 512);
        ensure_equals("four U16 per texel", (S32)texels.size(), 512 * 4);
        for (S32 i = 0; i < 512; ++i)
        {
            const S32 expected = (S32)std::floor(65535.0 * i / 511.0 + 0.5);
            for (S32 c = 0; c < 3; ++c)
            {
                // Within one code: the spline runs in F32, and a few of the
                // ratios sit close enough to a rounding boundary for the last
                // bit to decide which side.
                ensure("identity ramp texel", std::abs((S32)texels[i * 4 + c] - expected) <= 1);
            }
            ensure_equals("alpha opaque", (S32)texels[i * 4 + 3], (S32)ALToneCurveSet::ALPHA_OPAQUE);
        }
    }

    // Points survive an LLSD round trip, in the array-of-pairs shape the
    // settings and Looks carry.
    template<> template<>
    void curve_object::test<24>()
    {
        const auto in = pts({ { 0.f, 0.05f }, { 0.25f, 0.2f }, { 0.75f, 0.8f }, { 1.f, 0.95f } });
        const LLSD sd = ALToneCurveSet::pointsToLLSD(in);
        ensure("array", sd.isArray());
        ensure_equals("one entry per point", (S32)sd.size(), 4);
        for (S32 i = 0; i < 4; ++i)
        {
            ensure("entry is an array", sd[i].isArray());
            ensure_equals("entry is a pair", (S32)sd[i].size(), 2);
            ensure("x is a real", sd[i][0].isReal());
            ensure("y is a real", sd[i][1].isReal());
        }

        std::vector<ALCurveModel::Point> out;
        ensure("parsed", ALToneCurveSet::pointsFromLLSD(sd, out));
        ensure_equals("all points back", (S32)out.size(), 4);
        for (S32 i = 0; i < 4; ++i)
        {
            ensure_approximately_equals("x survives", out[i].mX, in[i].mX, 6);
            ensure_approximately_equals("y survives", out[i].mY, in[i].mY, 6);
        }

        ALToneCurveSet set;
        ensure("set from LLSD", set.setCurveFromLLSD(ALToneCurveSet::CH_GREEN, sd));
        ensure("no longer identity", !set.isIdentity());
        const LLSD back = set.getCurveAsLLSD(ALToneCurveSet::CH_GREEN);
        ensure_equals("same count after the model", (S32)back.size(), 4);
        ensure_approximately_equals("same value after the model", (F32)back[1][1].asReal(), 0.2f, 6);
    }

    // Garbage in is identity out, and never a crash or a point at the origin:
    // a corrupt setting renders as "no curve".
    template<> template<>
    void curve_object::test<25>()
    {
        std::vector<ALCurveModel::Point> out;

        ensure("undefined refused", !ALToneCurveSet::pointsFromLLSD(LLSD(), out));
        ensure_equals("identity on refusal", (S32)out.size(), 2);
        ensure("string refused", !ALToneCurveSet::pointsFromLLSD(LLSD("[[0,0],[1,1]]"), out));
        ensure("map refused", !ALToneCurveSet::pointsFromLLSD(LLSD::emptyMap(), out));
        ensure("empty array refused", !ALToneCurveSet::pointsFromLLSD(LLSD::emptyArray(), out));

        // Malformed entries are skipped, not fatal; the survivors are used.
        LLSD mixed = LLSD::emptyArray();
        mixed.append(pair("a", "b"));
        LLSD lone = LLSD::emptyArray();
        lone.append(LLSD::Real(0.5));
        mixed.append(lone);
        LLSD map = LLSD::emptyMap();
        map["x"] = 1;
        mixed.append(map);
        mixed.append(pair(LLSD::Real(std::numeric_limits<F64>::quiet_NaN()), LLSD::Real(0.2)));
        mixed.append(pair(LLSD::Real(std::numeric_limits<F64>::infinity()), LLSD::Real(1.0)));
        mixed.append(LLSD::Real(0.3));
        mixed.append(pair(LLSD::Real(0.0), LLSD::Real(0.0)));
        mixed.append(pair(LLSD::Integer(1), LLSD::Integer(1)));   // integers are numbers too
        mixed.append(pair(LLSD::Real(0.5), LLSD::Real(0.3)));
        ensure("survivors accepted", ALToneCurveSet::pointsFromLLSD(mixed, out));
        ensure_equals("exactly the three well-formed pairs", (S32)out.size(), 3);

        // One well-formed pair is not a curve.
        LLSD one = LLSD::emptyArray();
        one.append(pair(LLSD::Real(0.5), LLSD::Real(0.5)));
        ensure("a lone pair refused", !ALToneCurveSet::pointsFromLLSD(one, out));
        ensure_equals("identity again", (S32)out.size(), 2);

        // Out of range is clamped by the model, not rejected.
        LLSD wild = LLSD::emptyArray();
        wild.append(pair(LLSD::Real(-1.0), LLSD::Real(-1.0)));
        wild.append(pair(LLSD::Real(2.0), LLSD::Real(2.0)));
        ALToneCurveSet set;
        ensure("parsed", set.setCurveFromLLSD(ALToneCurveSet::CH_RED, wild));
        for (const auto& p : set.curve(ALToneCurveSet::CH_RED).getPoints())
        {
            ensure("x clamped", p.mX >= 0.f && p.mX <= 1.f);
            ensure("y clamped", p.mY >= 0.f && p.mY <= 1.f);
        }

        // More points than the editor adds are kept as long as a file could
        // reasonably hold them; past the stored limit the list is garbage.
        LLSD many = LLSD::emptyArray();
        for (S32 i = 0; i < 40; ++i)
        {
            const F64 x = i / 39.0;
            many.append(pair(LLSD::Real(x), LLSD::Real(x * x)));
        }
        ensure("parsed", set.setCurveFromLLSD(ALToneCurveSet::CH_BLUE, many));
        const auto& kept = set.curve(ALToneCurveSet::CH_BLUE).getPoints();
        ensure_equals("all forty kept", (S32)kept.size(), 40);
        ensure_approximately_equals("first at 0", kept.front().mX, 0.f, 6);
        ensure_approximately_equals("last at 1", kept.back().mX, 1.f, 6);
        ensure_approximately_equals("shape honoured", set.curve(ALToneCurveSet::CH_BLUE).evaluate(0.5f), 0.25f, 4);

        LLSD absurd = LLSD::emptyArray();
        for (S32 i = 0; i <= ALCurveModel::MAX_STORED_POINTS; ++i)
        {
            const F64 x = (F64)i / ALCurveModel::MAX_STORED_POINTS;
            absurd.append(pair(LLSD::Real(x), LLSD::Real(x)));
        }
        ensure("past the stored limit is refused", !set.setCurveFromLLSD(ALToneCurveSet::CH_BLUE, absurd));
        ensure_equals("and renders as no curve", set.curve(ALToneCurveSet::CH_BLUE).getPointCount(), 2);
    }

    // Composite order: the channel curve first, then master on its output.
    template<> template<>
    void curve_object::test<26>()
    {
        ALToneCurveSet set;
        set.curve(ALToneCurveSet::CH_RED).setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.25f }, { 1.f, 1.f } }));
        set.curve(ALToneCurveSet::CH_MASTER).setPoints(pts({ { 0.f, 0.f }, { 0.25f, 0.75f }, { 1.f, 1.f } }));

        ensure_approximately_equals("red then master", set.evaluate(ALToneCurveSet::CH_RED, 0.5f), 0.75f, 4);
        ensure_approximately_equals("is master(red(x))",
                                    set.evaluate(ALToneCurveSet::CH_RED, 0.5f),
                                    set.curve(ALToneCurveSet::CH_MASTER).evaluate(set.curve(ALToneCurveSet::CH_RED).evaluate(0.5f)), 6);
        ensure_approximately_equals("an identity channel is just master",
                                    set.evaluate(ALToneCurveSet::CH_GREEN, 0.5f),
                                    set.curve(ALToneCurveSet::CH_MASTER).evaluate(0.5f), 6);
        ensure_approximately_equals("master alone is master",
                                    set.evaluate(ALToneCurveSet::CH_MASTER, 0.5f),
                                    set.curve(ALToneCurveSet::CH_MASTER).evaluate(0.5f), 6);

        // A master edit moves every channel.
        ALToneCurveSet master_only;
        master_only.curve(ALToneCurveSet::CH_MASTER).setPoints(pts({ { 0.f, 0.1f }, { 1.f, 0.9f } }));
        for (S32 c = ALToneCurveSet::CH_RED; c <= ALToneCurveSet::CH_BLUE; ++c)
        {
            ensure_approximately_equals("every channel follows master",
                                        master_only.evaluate((ALToneCurveSet::EChannel)c, 0.f), 0.1f, 5);
        }
    }

    // Identity detection is exact on the points, with a tolerance far below
    // anything visible, and it is per set: one non-identity channel is enough.
    template<> template<>
    void curve_object::test<27>()
    {
        ALCurveModel close_curve;
        close_curve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.5f + 0.5f * ALToneCurveSet::IDENTITY_EPS }, { 1.f, 1.f } }));
        ensure("within eps is identity", ALToneCurveSet::isIdentityCurve(close_curve));

        ALCurveModel far_curve;
        far_curve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.5f + 10.f * ALToneCurveSet::IDENTITY_EPS }, { 1.f, 1.f } }));
        ensure("beyond eps is not", !ALToneCurveSet::isIdentityCurve(far_curve));

        ALToneCurveSet set;
        ensure("all identity", set.isIdentity());
        set.curve(ALToneCurveSet::CH_BLUE).setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.6f }, { 1.f, 1.f } }));
        ensure("one channel is enough", !set.isIdentity());
    }

    // The baked row: one RGBA16 texel per sample, the curve's ends at the
    // row's ends, and a monotone curve gives a monotone row.
    template<> template<>
    void curve_object::test<28>()
    {
        ALToneCurveSet set;
        set.curve(ALToneCurveSet::CH_MASTER).setPoints(
            pts({ { 0.f, 0.f }, { 0.2f, 0.05f }, { 0.4f, 0.15f }, { 0.5f, 0.5f }, { 0.6f, 0.85f }, { 0.8f, 0.95f }, { 1.f, 1.f } }));
        set.curve(ALToneCurveSet::CH_RED).setPoints(pts({ { 0.f, 0.1f }, { 0.5f, 0.45f }, { 1.f, 0.9f } }));

        const S32 n = ALToneCurveSet::LUT_SIZE;
        std::vector<U16> texels;
        set.bake(texels, n);
        ensure_equals("size", (S32)texels.size(), n * 4);

        for (S32 c = 0; c < 3; ++c)
        {
            const ALToneCurveSet::EChannel channel = (ALToneCurveSet::EChannel)(ALToneCurveSet::CH_RED + c);
            const F32 lo = set.evaluate(channel, 0.f) * 65535.f;
            const F32 hi = set.evaluate(channel, 1.f) * 65535.f;
            ensure("first texel is the curve at 0", fabsf((F32)texels[c] - lo) <= 1.f);
            ensure("last texel is the curve at 1", fabsf((F32)texels[(n - 1) * 4 + c] - hi) <= 1.f);
            for (S32 i = 1; i < n; ++i)
            {
                ensure("monotone row", texels[i * 4 + c] >= texels[(i - 1) * 4 + c]);
            }
        }
        for (S32 i = 0; i < n; ++i)
        {
            ensure_equals("alpha opaque", (S32)texels[i * 4 + 3], (S32)ALToneCurveSet::ALPHA_OPAQUE);
        }

        std::vector<U16> tiny;
        set.bake(tiny, 1);
        ensure_equals("a size below 2 is raised to 2", (S32)tiny.size(), 2 * 4);
    }

    // What the shader will read back agrees with the analytic curve to well
    // under the display quantum: the half-texel mapping plus the hardware's
    // linear filter over LUT_SIZE samples. A pure-Python transcription of this
    // fetch measured a worst case of 2.8e-4 (0.29/1024) for this pair of
    // curves at 512 samples with 8-bit filter weights, and 0.50/1024 even with
    // the 4-bit weights old hardware is allowed; at 256 samples the 4-bit case
    // reaches 0.97/1024, which is why 512 is the size that ships.
    template<> template<>
    void curve_object::test<29>()
    {
        ALToneCurveSet set;
        set.curve(ALToneCurveSet::CH_MASTER).setPoints(
            pts({ { 0.f, 0.f }, { 0.2f, 0.05f }, { 0.4f, 0.15f }, { 0.5f, 0.5f }, { 0.6f, 0.85f }, { 0.8f, 0.95f }, { 1.f, 1.f } }));
        set.curve(ALToneCurveSet::CH_RED).setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.45f }, { 1.f, 1.f } }));

        const S32 n = ALToneCurveSet::LUT_SIZE;
        std::vector<U16> texels;
        set.bake(texels, n);

        std::vector<F32> xs;
        for (S32 i = 0; i <= 4096; ++i)
        {
            xs.push_back((F32)i / 4096.f);
        }
        for (S32 k = 0; k <= 255; ++k)
        {
            xs.push_back((F32)k / 255.f);
        }

        F32 worst = 0.f;
        for (S32 c = 0; c < 3; ++c)
        {
            const ALToneCurveSet::EChannel channel = (ALToneCurveSet::EChannel)(ALToneCurveSet::CH_RED + c);
            for (F32 x : xs)
            {
                worst = std::max(worst, fabsf(shaderLutFetch(texels, n, c, x) - set.evaluate(channel, x)));
            }
            ensure("endpoint 0 exact to the U16",
                   fabsf(shaderLutFetch(texels, n, c, 0.f) - set.evaluate(channel, 0.f)) <= 1.f / 65535.f + 1e-6f);
            ensure("endpoint 1 exact to the U16",
                   fabsf(shaderLutFetch(texels, n, c, 1.f) - set.evaluate(channel, 1.f)) <= 1.f / 65535.f + 1e-6f);
        }
        ensure("reconstruction error below 1/1024", worst < 1.f / 1024.f);
    }

    // The half-texel mapping puts x = 0 on the centre of the first texel and
    // x = 1 on the centre of the last, for any row width.
    template<> template<>
    void curve_object::test<30>()
    {
        for (S32 n : { 2, 256, 512 })
        {
            F32 scale, bias;
            ALToneCurveSet::lutScaleBias(n, scale, bias);
            const F32 u0 = 0.f * scale + bias;
            const F32 u1 = 1.f * scale + bias;
            ensure_approximately_equals("x = 0 is texel 0's centre", u0 * (F32)n - 0.5f, 0.f, 6);
            ensure_approximately_equals("x = 1 is the last texel's centre", u1 * (F32)n - 0.5f, (F32)(n - 1), 6);
        }
    }

    // The channel combo's values map onto the set, with anything unexpected
    // landing on Master rather than on a channel.
    template<> template<>
    void curve_object::test<31>()
    {
        ensure_equals("-1 is master", (S32)ALToneCurveSet::channelFromCombo(-1), (S32)ALToneCurveSet::CH_MASTER);
        ensure_equals("0 is red", (S32)ALToneCurveSet::channelFromCombo(0), (S32)ALToneCurveSet::CH_RED);
        ensure_equals("1 is green", (S32)ALToneCurveSet::channelFromCombo(1), (S32)ALToneCurveSet::CH_GREEN);
        ensure_equals("2 is blue", (S32)ALToneCurveSet::channelFromCombo(2), (S32)ALToneCurveSet::CH_BLUE);
        ensure_equals("anything else is master", (S32)ALToneCurveSet::channelFromCombo(7), (S32)ALToneCurveSet::CH_MASTER);

        const LLSD ident = ALToneCurveSet::identityLLSD();
        ensure_equals("identity is two points", (S32)ident.size(), 2);
        ensure_approximately_equals("from the origin", (F32)ident[0][0].asReal(), 0.f, 6);
        ensure_approximately_equals("to the corner", (F32)ident[1][1].asReal(), 1.f, 6);
    }

    // An add at either edge lands one gap inside the pinned end, never on it,
    // so the curve still ends where the end says and the end stays the end.
    template<> template<>
    void curve_object::test<32>()
    {
        ensure("added at the right edge", mCurve.addPoint(1.f, 0.03f) >= 0);
        ensure_equals("three points", mCurve.getPointCount(), 3);
        ensure_approximately_equals("interior, one gap inside", mCurve.getPoints()[1].mX, 1.f - ALCurveModel::MIN_POINT_GAP, 5);
        ensure_approximately_equals("the end is still the end", mCurve.getPoints().back().mX, 1.f, 6);
        ensure_approximately_equals("and still at its height", mCurve.getPoints().back().mY, 1.f, 6);
        ensure_approximately_equals("evaluate(1) is the end", mCurve.evaluate(1.f), 1.f, 6);

        ensure("added at the left edge", mCurve.addPoint(0.f, 0.9f) >= 0);
        ensure_approximately_equals("the start is still the start", mCurve.getPoints().front().mX, 0.f, 6);
        ensure_approximately_equals("and at its height", mCurve.getPoints().front().mY, 0.f, 6);
        ensure_approximately_equals("interior, one gap inside", mCurve.getPoints()[1].mX, ALCurveModel::MIN_POINT_GAP, 5);
        ensure_approximately_equals("evaluate(0) is the start", mCurve.evaluate(0.f), 0.f, 6);
    }

    // Points piled up on the top edge are pushed down, not left coincident:
    // the separation pass works from both ends.
    template<> template<>
    void curve_object::test<33>()
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 1.f, 0.2f }, { 1.f, 0.4f }, { 1.f, 0.6f }, { 1.f, 1.f } }));
        const auto& p = mCurve.getPoints();
        ensure_equals("all kept", (S32)p.size(), 5);
        for (size_t i = 1; i < p.size(); ++i)
        {
            ensure("separated", p[i].mX - p[i - 1].mX >= ALCurveModel::MIN_POINT_GAP - 1e-6f);
        }
        ensure_approximately_equals("the end is still at 1", p.back().mX, 1.f, 6);
        ensure_approximately_equals("evaluate(1) is the end", mCurve.evaluate(1.f), 1.f, 6);

        std::vector<F32> s;
        mCurve.sample(s, 1025);
        for (size_t i = 1; i < s.size(); ++i)
        {
            ensure("finite", std::isfinite(s[i]));
            ensure("monotone", s[i] >= s[i - 1] - 1e-5f);
        }
    }

    // The setting names are one list: distinct, and channel-shaped.
    template<> template<>
    void curve_object::test<34>()
    {
        for (S32 c = 0; c < ALToneCurveSet::CH_COUNT; ++c)
        {
            const std::string name = ALToneCurveSet::settingName((ALToneCurveSet::EChannel)c);
            ensure("named for the curve", name.rfind("RenderColorGradeCurve", 0) == 0);
            for (S32 d = 0; d < c; ++d)
            {
                ensure("distinct", name != ALToneCurveSet::settingName((ALToneCurveSet::EChannel)d));
            }
        }
        ensure_equals("master", std::string(ALToneCurveSet::settingName(ALToneCurveSet::CH_MASTER)),
                      std::string("RenderColorGradeCurveMaster"));
    }
}
