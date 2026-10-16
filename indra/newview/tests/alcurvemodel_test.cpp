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

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include "../alcurvemodel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
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

    // --- spline: ordering and point management -------------------------------

    TEST_CASE_METHOD(curve_data, "ALCurveModel: a fresh model is the identity ramp", "[ALCurveModel]")
    {
        CHECK(mCurve.getPointCount() == 2);
        for (S32 i = 0; i <= 10; ++i)
        {
            const F32 x = i * 0.1f;
            CAPTURE(x);
            REQUIRE_THAT(mCurve.evaluate(x), ll_test::WithinBits(x, 5));
        }
    }

    // Points added out of order come back sorted by x, and the reported index
    // is where the point actually landed.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: points added out of order come back sorted by x", "[ALCurveModel]")
    {
        const S32 idx_hi = mCurve.addPoint(0.8f, 0.9f);
        const S32 idx_lo = mCurve.addPoint(0.2f, 0.1f);

        UNSCOPED_INFO("high point went before the last");
        CHECK(idx_hi == 1);
        UNSCOPED_INFO("low point went before the high one");
        CHECK(idx_lo == 1);
        REQUIRE(mCurve.getPointCount() == 4);

        const auto& p = mCurve.getPoints();
        for (size_t i = 1; i < p.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(p[i].mX > p[i - 1].mX);
        }
        UNSCOPED_INFO("second point is the low one");
        CHECK_THAT(p[1].mX, ll_test::WithinBits(0.2f, 5));
        UNSCOPED_INFO("third point is the high one");
        CHECK_THAT(p[2].mX, ll_test::WithinBits(0.8f, 5));
    }

    // Coincident points are pushed apart rather than sharing an x, so no
    // segment can have zero width.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: coincident points are pushed apart by at least the minimum gap", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.3f }, { 0.5f, 0.7f }, { 1.f, 1.f } }));

        const auto& p = mCurve.getPoints();
        CHECK((S32)p.size() == 4);
        for (size_t i = 1; i < p.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(p[i].mX - p[i - 1].mX >= ALCurveModel::MIN_POINT_GAP - 1e-6f);
        }
        CHECK(std::isfinite(mCurve.evaluate(0.5f)));
    }

    // A drag cannot push a point past its neighbours, and locked endpoints
    // keep their x while still accepting a new height.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: a drag stops at the neighbours and locked ends keep their x", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.3f, 0.3f }, { 0.6f, 0.6f }, { 1.f, 1.f } }));

        mCurve.movePoint(1, 0.95f, 0.5f);
        const auto& p = mCurve.getPoints();
        REQUIRE(p.size() == size_t(4));
        UNSCOPED_INFO("clamped below its right neighbour");
        CHECK(p[1].mX < p[2].mX);
        UNSCOPED_INFO("clamped to exactly the gap");
        CHECK_THAT(p[1].mX, ll_test::WithinBits(0.6f - ALCurveModel::MIN_POINT_GAP, 5));
        {
            INFO("still ordered");
            CHECK(p[0].mX < p[1].mX);
            CHECK(p[2].mX < p[3].mX);
        }

        mCurve.movePoint(0, 0.4f, 0.25f);
        UNSCOPED_INFO("first point x stays pinned");
        CHECK_THAT(mCurve.getPoints()[0].mX, ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("first point y moved");
        CHECK_THAT(mCurve.getPoints()[0].mY, ll_test::WithinBits(0.25f, 5));

        mCurve.movePoint(3, 0.4f, 0.8f);
        UNSCOPED_INFO("last point x stays pinned");
        CHECK_THAT(mCurve.getPoints()[3].mX, ll_test::WithinBits(1.f, 6));
    }

    // Removal refuses to break the curve: never below two points, and never an
    // endpoint while the endpoints are locked.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: removal never leaves fewer than two points or drops a locked end", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.4f }, { 1.f, 1.f } }));

        UNSCOPED_INFO("cannot remove the first");
        CHECK_FALSE(mCurve.removePoint(0));
        UNSCOPED_INFO("cannot remove the last");
        CHECK_FALSE(mCurve.removePoint(2));
        UNSCOPED_INFO("out of range refused");
        CHECK_FALSE(mCurve.removePoint(7));
        UNSCOPED_INFO("interior removed");
        CHECK(mCurve.removePoint(1));
        CHECK(mCurve.getPointCount() == 2);
        UNSCOPED_INFO("cannot go below two");
        CHECK_FALSE(mCurve.removePoint(0));
    }

    // --- spline: shape -------------------------------------------------------

    TEST_CASE_METHOD(curve_data, "ALCurveModel: the curve passes through every control point", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.05f }, { 0.25f, 0.5f }, { 0.7f, 0.6f }, { 1.f, 0.95f } }));

        for (const auto& p : mCurve.getPoints())
        {
            CAPTURE(p.mX);
            REQUIRE_THAT(mCurve.evaluate(p.mX), ll_test::WithinBits(p.mY, 4));
        }
    }

    // Monotone data yields a monotone curve. A natural or Catmull-Rom spline
    // fails this on exactly this shape -- a long flat run into a sharp rise
    // makes it dip below the flat before climbing.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: monotone data yields a monotone curve", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.4f, 0.02f }, { 0.6f, 0.05f }, { 0.7f, 0.9f }, { 1.f, 1.f } }));

        std::vector<F32> s;
        mCurve.sample(s, 257);
        for (size_t i = 1; i < s.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(s[i] >= s[i - 1] - 1e-5f);
        }
    }

    TEST_CASE_METHOD(curve_data, "ALCurveModel: a flat run stays exactly flat with no ringing between equal points", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.3f, 0.5f }, { 0.7f, 0.5f }, { 1.f, 1.f } }));

        for (S32 i = 0; i <= 8; ++i)
        {
            const F32 x = 0.3f + i * 0.05f;
            CAPTURE(x);
            REQUIRE_THAT(mCurve.evaluate(x), ll_test::WithinBits(0.5f, 4));
        }
    }

    // Outside the point range the curve holds its end values rather than
    // extrapolating off the graph.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: outside its points the curve holds its end values", "[ALCurveModel]")
    {
        mCurve.setEndpointsLocked(false);
        mCurve.setPoints(pts({ { 0.2f, 0.3f }, { 0.8f, 0.7f } }));

        UNSCOPED_INFO("holds the left value");
        CHECK_THAT(mCurve.evaluate(0.f), ll_test::WithinBits(0.3f, 5));
        UNSCOPED_INFO("holds the right value");
        CHECK_THAT(mCurve.evaluate(1.f), ll_test::WithinBits(0.7f, 5));
    }

    // Output never leaves 0..1, so a curve can never ask the caller to write an
    // out-of-gamut value.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: output never leaves 0..1 even for a wild curve", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.05f, 0.99f }, { 0.1f, 0.01f }, { 1.f, 1.f } }));

        std::vector<F32> s;
        mCurve.sample(s, 129);
        for (F32 y : s)
        {
            REQUIRE(y >= 0.f);
            REQUIRE(y <= 1.f);
            REQUIRE(std::isfinite(y));
        }
    }

    // --- sampling ------------------------------------------------------------

    TEST_CASE_METHOD(curve_data, "ALCurveModel: sample() spans 0..1 inclusive and honours its count", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.1f }, { 0.5f, 0.4f }, { 1.f, 0.9f } }));

        std::vector<F32> s;
        mCurve.sample(s, 65);
        REQUIRE((S32)s.size() == 65);
        CHECK_THAT(s.front(), ll_test::WithinBits(mCurve.evaluate(0.f), 6));
        CHECK_THAT(s.back(), ll_test::WithinBits(mCurve.evaluate(1.f), 6));
        UNSCOPED_INFO("the last sample is the value at exactly 1");
        CHECK_THAT(s.back(), ll_test::WithinBits(0.9f, 6));

        mCurve.sample(s, 1);
        UNSCOPED_INFO("a single sample is not a curve");
        CHECK(s.empty());
        mCurve.sample(s, 0);
        CHECK(s.empty());
    }

    // Unlocking the endpoints lets the first and last points move horizontally;
    // re-locking snaps them back to the full domain.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: unlocked ends move horizontally and relocking snaps them back", "[ALCurveModel]")
    {
        mCurve.setEndpointsLocked(false);
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.5f }, { 1.f, 1.f } }));

        mCurve.movePoint(0, 0.2f, 0.1f);
        UNSCOPED_INFO("unlocked end moved");
        CHECK_THAT(mCurve.getPoints()[0].mX, ll_test::WithinBits(0.2f, 5));

        mCurve.setEndpointsLocked(true);
        UNSCOPED_INFO("relock snaps to 0");
        CHECK_THAT(mCurve.getPoints()[0].mX, ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("relock snaps to 1");
        CHECK_THAT(mCurve.getPoints().back().mX, ll_test::WithinBits(1.f, 6));
    }

    // setPoints refuses to leave the model unusable.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: setPoints resets a single point to the identity ramp", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.3f, 0.3f } }));
        UNSCOPED_INFO("degenerate input resets to a ramp");
        CHECK(mCurve.getPointCount() == 2);
        UNSCOPED_INFO("and it is the identity");
        CHECK_THAT(mCurve.evaluate(0.5f), ll_test::WithinBits(0.5f, 5));
    }

    // The point cap: addPoint refuses at MAX_POINTS, and setPoints truncates
    // while keeping both ends, so a locked curve still spans the domain.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: addPoint stops at MAX_POINTS and setPoints keeps both ends", "[ALCurveModel]")
    {
        for (S32 i = 1; i <= ALCurveModel::MAX_POINTS - 2; ++i)
        {
            const F32 x = (F32)i / (F32)(ALCurveModel::MAX_POINTS - 1);
            CAPTURE(i);
            REQUIRE(mCurve.addPoint(x, x) >= 0);
        }
        UNSCOPED_INFO("at the cap");
        CHECK(mCurve.getPointCount() == ALCurveModel::MAX_POINTS);
        UNSCOPED_INFO("refused past the cap");
        CHECK(mCurve.addPoint(0.123f, 0.5f) == -1);
        UNSCOPED_INFO("count unchanged");
        CHECK(mCurve.getPointCount() == ALCurveModel::MAX_POINTS);

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
        UNSCOPED_INFO("twenty stored points survive");
        REQUIRE(mCurve.getPointCount() == 20);
        UNSCOPED_INFO("interior point kept");
        CHECK_THAT(mCurve.getPoints()[15].mX, ll_test::WithinBits(15.f / 19.f, 5));
        UNSCOPED_INFO("but no further adds");
        CHECK(mCurve.addPoint(0.123f, 0.5f) == -1);

        std::vector<ALCurveModel::Point> absurd;
        for (S32 i = 0; i < 100; ++i)
        {
            const F32 x = (F32)i / 99.f;
            absurd.push_back(ALCurveModel::Point{ x, x * 0.5f });
        }
        mCurve.setPoints(absurd);
        UNSCOPED_INFO("setPoints capped at the stored limit");
        CHECK(mCurve.getPointCount() == ALCurveModel::MAX_STORED_POINTS);
        UNSCOPED_INFO("first point kept at 0");
        CHECK_THAT(mCurve.getPoints().front().mX, ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("last point kept at 1");
        CHECK_THAT(mCurve.getPoints().back().mX, ll_test::WithinBits(1.f, 6));
        UNSCOPED_INFO("and it is the original last point");
        CHECK_THAT(mCurve.getPoints().back().mY, ll_test::WithinBits(0.5f, 5));
    }

    // --- split-tone bands ----------------------------------------------------

    // With the widths left at their default, the bands are what the shader
    // computed before the widths existed, across the whole range of split
    // points the Balance slider can ask for. This is the promise that every
    // Look saved before then renders unchanged.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: default widths give the bands the shader computed before widths existed", "[ALCurveModel]")
    {
        for (F32 mid : { 0.1f, 0.3f, 0.5f, 0.7f, 0.9f })
        {
            for (S32 i = 0; i <= 64; ++i)
            {
                const F32 l = (F32)i / 64.f;
                F32 lo, md, hi;
                shaderSplitWeights(l, mid, lo, md, hi);
                const auto w = ALCurveModel::splitToneWeights(l, mid);
                CAPTURE(mid, l);
                REQUIRE_THAT(w.mShadow, ll_test::WithinBits(lo, 5));
                REQUIRE_THAT(w.mMidtone, ll_test::WithinBits(md, 5));
                REQUIRE_THAT(w.mHighlight, ll_test::WithinBits(hi, 5));
            }
        }
        UNSCOPED_INFO("the default width is the old constant");
        CHECK_THAT(ALCurveModel::SPLIT_TONE_DEFAULT_WIDTH, ll_test::WithinBits(0.35f, 6));
    }

    // The three weights partition the luma range: they sum to one everywhere.
    // That is what makes the band graph honest -- a tone is never partly
    // untinted, only ever shared between neighbouring bands -- and it holds
    // because the two ramps meet at the split without overlapping, so the
    // midtone remainder is never clamped away.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: the three split-tone weights sum to one everywhere", "[ALCurveModel]")
    {
        for (F32 mid : { 0.1f, 0.35f, 0.5f, 0.65f, 0.9f })
        {
            for (S32 i = 0; i <= 64; ++i)
            {
                const auto w = ALCurveModel::splitToneWeights((F32)i / 64.f, mid);
                CAPTURE(mid, i);
                REQUIRE_THAT(w.mShadow + w.mMidtone + w.mHighlight, ll_test::WithinBits(1.f, 5));
                REQUIRE(w.mShadow    >= 0.f);
                REQUIRE(w.mShadow    <= 1.f);
                REQUIRE(w.mMidtone   >= 0.f);
                REQUIRE(w.mMidtone   <= 1.f);
                REQUIRE(w.mHighlight >= 0.f);
                REQUIRE(w.mHighlight <= 1.f);
            }
        }

        const F32 mid = 0.55f;
        const auto at_mid = ALCurveModel::splitToneWeights(mid, mid);
        UNSCOPED_INFO("midtone peaks at the split");
        CHECK_THAT(at_mid.mMidtone, ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("shadow is spent there");
        CHECK_THAT(at_mid.mShadow, ll_test::WithinBits(0.f, 5));
        UNSCOPED_INFO("highlight has not started");
        CHECK_THAT(at_mid.mHighlight, ll_test::WithinBits(0.f, 5));

        const auto black = ALCurveModel::splitToneWeights(0.f, mid);
        const auto white = ALCurveModel::splitToneWeights(1.f, mid);
        UNSCOPED_INFO("black is all shadow");
        CHECK_THAT(black.mShadow, ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("white is all highlight");
        CHECK_THAT(white.mHighlight, ll_test::WithinBits(1.f, 5));

        // Monotone, so the bands cannot cross back over themselves. If the
        // shader's ramp edges were ever swapped, this is what notices.
        F32 prev_hi = -1.f, prev_lo = 2.f;
        for (S32 i = 0; i <= 64; ++i)
        {
            const auto w = ALCurveModel::splitToneWeights((F32)i / 64.f, mid);
            CAPTURE(i);
            REQUIRE(w.mHighlight >= prev_hi - 1e-5f);
            REQUIRE(w.mShadow <= prev_lo + 1e-5f);
            prev_hi = w.mHighlight;
            prev_lo = w.mShadow;
        }
    }

    // Balance and split point invert each other across the whole slider range,
    // which is what lets the graph's handle be read back into the setting
    // without the value creeping a little on every drag.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: balance and split point invert each other across the slider range", "[ALCurveModel]")
    {
        for (S32 i = -10; i <= 10; ++i)
        {
            const F32 balance = (F32)i / 10.f;
            const F32 mid = ALCurveModel::splitToneMid(balance);
            CAPTURE(balance);
            REQUIRE(mid >= 0.1f - 1e-5f);
            REQUIRE(mid <= 0.9f + 1e-5f);
            REQUIRE_THAT(ALCurveModel::splitToneBalance(mid), ll_test::WithinBits(balance, 5));
        }

        UNSCOPED_INFO("neutral balance splits at mid grey");
        CHECK_THAT(ALCurveModel::splitToneMid(0.f), ll_test::WithinBits(0.5f, 6));
        // Clamped, not wrapped: pipeline.cpp clamps the balance before it
        // uploads the ramps, so the graph must agree rather than plot a split
        // the renderer will never use.
        UNSCOPED_INFO("out-of-range balance clamps");
        CHECK_THAT(ALCurveModel::splitToneMid(3.f), ll_test::WithinBits(0.9f, 6));
        UNSCOPED_INFO("and so does the inverse");
        CHECK_THAT(ALCurveModel::splitToneBalance(2.f), ll_test::WithinBits(1.f, 6));
    }

    // With independent widths the model still agrees with the shader, which
    // now takes each ramp as the {scale, bias} pair the CPU derives.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: independent widths agree with the shader's scale and bias ramps", "[ALCurveModel]")
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
                    CAPTURE(mid, wsh.first, wsh.second, l);
                    REQUIRE_THAT(w.mShadow, ll_test::WithinBits(lo, 5));
                    REQUIRE_THAT(w.mMidtone, ll_test::WithinBits(md, 5));
                    REQUIRE_THAT(w.mHighlight, ll_test::WithinBits(hi, 5));
                }
            }
        }
    }

    // Partition of unity survives asymmetric widths: the ramps still meet at
    // the split and only at the split.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: partition of unity survives asymmetric widths", "[ALCurveModel]")
    {
        const std::pair<F32, F32> widths[] = { { 0.05f, 0.5f }, { 0.5f, 0.02f }, { 0.2f, 0.1f } };
        for (F32 mid : { 0.1f, 0.5f, 0.9f })
        {
            for (const auto& wsh : widths)
            {
                CAPTURE(mid, wsh.first, wsh.second);
                F32 prev_hi = -1.f, prev_lo = 2.f;
                for (S32 i = 0; i <= 64; ++i)
                {
                    const F32 l = (F32)i / 64.f;
                    const auto w = ALCurveModel::splitToneWeights(l, mid, wsh.first, wsh.second);
                    CAPTURE(l);
                    REQUIRE_THAT(w.mShadow + w.mMidtone + w.mHighlight, ll_test::WithinBits(1.f, 5));
                    REQUIRE(w.mShadow    >= 0.f);
                    REQUIRE(w.mShadow    <= 1.f);
                    REQUIRE(w.mMidtone   >= 0.f);
                    REQUIRE(w.mMidtone   <= 1.f);
                    REQUIRE(w.mHighlight >= 0.f);
                    REQUIRE(w.mHighlight <= 1.f);
                    REQUIRE(w.mHighlight >= prev_hi - 1e-5f);
                    REQUIRE(w.mShadow <= prev_lo + 1e-5f);
                    prev_hi = w.mHighlight;
                    prev_lo = w.mShadow;
                }
                const auto at_mid = ALCurveModel::splitToneWeights(mid, mid, wsh.first, wsh.second);
                UNSCOPED_INFO("midtone peaks at the split");
                REQUIRE_THAT(at_mid.mMidtone, ll_test::WithinBits(1.f, 5));
                // The ends are fully tinted only when the ramp fits on the
                // plot. A wide ramp under a low split runs off below zero and
                // black is then only partly shadow -- as it was with the old
                // fixed width at full negative balance, and as the band graph
                // shows.
                if (mid - wsh.first >= 0.f)
                {
                    UNSCOPED_INFO("black is all shadow");
                    REQUIRE_THAT(ALCurveModel::splitToneWeights(0.f, mid, wsh.first, wsh.second).mShadow, ll_test::WithinBits(1.f, 5));
                }
                if (mid + wsh.second <= 1.f)
                {
                    UNSCOPED_INFO("white is all highlight");
                    REQUIRE_THAT(ALCurveModel::splitToneWeights(1.f, mid, wsh.first, wsh.second).mHighlight, ll_test::WithinBits(1.f, 5));
                }
            }
        }
    }

    // A zero width is a step at the split, finite on both sides, and still a
    // partition -- the floor is applied before the shadow ramp's left edge is
    // formed, so the ramp ends at the split rather than short of it.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: a zero width is a finite step at the split and still a partition", "[ALCurveModel]")
    {
        const auto ramp = ALCurveModel::splitToneRamp(0.5f, 0.f);
        CHECK(std::isfinite(ramp.mScale));
        CHECK(std::isfinite(ramp.mBias));

        const auto below = ALCurveModel::splitToneWeights(0.49f, 0.5f, 0.f, 0.f);
        const auto above = ALCurveModel::splitToneWeights(0.51f, 0.5f, 0.f, 0.f);
        UNSCOPED_INFO("all shadow just below the split");
        CHECK_THAT(below.mShadow, ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("all highlight just above the split");
        CHECK_THAT(above.mHighlight, ll_test::WithinBits(1.f, 5));
        for (S32 i = 0; i <= 64; ++i)
        {
            const auto w = ALCurveModel::splitToneWeights((F32)i / 64.f, 0.5f, 0.f, 0.f);
            CAPTURE(i);
            REQUIRE_THAT(w.mShadow + w.mMidtone + w.mHighlight, ll_test::WithinBits(1.f, 5));
        }
    }

    // The scale/bias pair is the two-edge form folded: l*scale + bias is
    // (l - edge0) / width wherever the width is above the floor.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: the scale/bias pair is the two-edge ramp folded", "[ALCurveModel]")
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
                    CAPTURE(edge0, width, l);
                    REQUIRE_THAT(l * ramp.mScale + ramp.mBias, ll_test::WithinBits((l - edge0) / width, 4));
                }
            }
        }
    }

    TEST_CASE_METHOD(curve_data, "ALCurveModel: the two-argument overload is the four-argument call at the default", "[ALCurveModel]")
    {
        for (F32 mid : { 0.1f, 0.5f, 0.9f })
        {
            for (S32 i = 0; i <= 32; ++i)
            {
                const F32 l = (F32)i / 32.f;
                const auto a = ALCurveModel::splitToneWeights(l, mid);
                const auto b = ALCurveModel::splitToneWeights(l, mid, 0.35f, 0.35f);
                CAPTURE(mid, l);
                REQUIRE(a.mShadow == b.mShadow);
                REQUIRE(a.mMidtone == b.mMidtone);
                REQUIRE(a.mHighlight == b.mHighlight);
            }
        }
    }

    // --- ALToneCurveSet ------------------------------------------------------

    TEST_CASE_METHOD(curve_data, "ALCurveModel: a fresh set is identity in every channel and bakes to the identity ramp", "[ALCurveModel]")
    {
        ALToneCurveSet set;
        CHECK(set.isIdentity());
        for (S32 c = 0; c < ALToneCurveSet::CH_COUNT; ++c)
        {
            for (S32 i = 0; i <= 20; ++i)
            {
                const F32 x = (F32)i / 20.f;
                CAPTURE(c, x);
                REQUIRE_THAT(set.evaluate((ALToneCurveSet::EChannel)c, x), ll_test::WithinBits(x, 6));
            }
        }

        std::vector<U16> texels;
        set.bake(texels, 512);
        UNSCOPED_INFO("four U16 per texel");
        REQUIRE((S32)texels.size() == 512 * 4);
        for (S32 i = 0; i < 512; ++i)
        {
            CAPTURE(i);
            const S32 expected = (S32)std::floor(65535.0 * i / 511.0 + 0.5);
            for (S32 c = 0; c < 3; ++c)
            {
                // Within one code: the spline runs in F32, and a few of the
                // ratios sit close enough to a rounding boundary for the last
                // bit to decide which side.
                CAPTURE(c);
                REQUIRE(std::abs((S32)texels[i * 4 + c] - expected) <= 1);
            }
            REQUIRE((S32)texels[i * 4 + 3] == (S32)ALToneCurveSet::ALPHA_OPAQUE);
        }
    }

    // Points survive an LLSD round trip, in the array-of-pairs shape the
    // settings and Looks carry.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: points survive an LLSD round trip as an array of pairs", "[ALCurveModel]")
    {
        const auto in = pts({ { 0.f, 0.05f }, { 0.25f, 0.2f }, { 0.75f, 0.8f }, { 1.f, 0.95f } });
        const LLSD sd = ALToneCurveSet::pointsToLLSD(in);
        REQUIRE(sd.isArray());
        REQUIRE((S32)sd.size() == 4);
        for (S32 i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            REQUIRE(sd[i].isArray());
            REQUIRE((S32)sd[i].size() == 2);
            REQUIRE(sd[i][0].isReal());
            REQUIRE(sd[i][1].isReal());
        }

        std::vector<ALCurveModel::Point> out;
        CHECK(ALToneCurveSet::pointsFromLLSD(sd, out));
        REQUIRE((S32)out.size() == 4);
        for (S32 i = 0; i < 4; ++i)
        {
            CAPTURE(i);
            REQUIRE_THAT(out[i].mX, ll_test::WithinBits(in[i].mX, 6));
            REQUIRE_THAT(out[i].mY, ll_test::WithinBits(in[i].mY, 6));
        }

        ALToneCurveSet set;
        CHECK(set.setCurveFromLLSD(ALToneCurveSet::CH_GREEN, sd));
        UNSCOPED_INFO("no longer identity");
        CHECK_FALSE(set.isIdentity());
        const LLSD back = set.getCurveAsLLSD(ALToneCurveSet::CH_GREEN);
        UNSCOPED_INFO("same count after the model");
        REQUIRE((S32)back.size() == 4);
        UNSCOPED_INFO("same value after the model");
        CHECK_THAT((F32)back[1][1].asReal(), ll_test::WithinBits(0.2f, 6));
    }

    // Garbage in is identity out, and never a crash or a point at the origin:
    // a corrupt setting renders as "no curve".
    TEST_CASE_METHOD(curve_data, "ALCurveModel: garbage LLSD is refused or skipped and leaves an identity curve", "[ALCurveModel]")
    {
        std::vector<ALCurveModel::Point> out;

        UNSCOPED_INFO("undefined refused");
        CHECK_FALSE(ALToneCurveSet::pointsFromLLSD(LLSD(), out));
        UNSCOPED_INFO("identity on refusal");
        CHECK((S32)out.size() == 2);
        UNSCOPED_INFO("string refused");
        CHECK_FALSE(ALToneCurveSet::pointsFromLLSD(LLSD("[[0,0],[1,1]]"), out));
        UNSCOPED_INFO("map refused");
        CHECK_FALSE(ALToneCurveSet::pointsFromLLSD(LLSD::emptyMap(), out));
        UNSCOPED_INFO("empty array refused");
        CHECK_FALSE(ALToneCurveSet::pointsFromLLSD(LLSD::emptyArray(), out));

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
        UNSCOPED_INFO("survivors accepted");
        CHECK(ALToneCurveSet::pointsFromLLSD(mixed, out));
        UNSCOPED_INFO("exactly the three well-formed pairs");
        CHECK((S32)out.size() == 3);

        // One well-formed pair is not a curve.
        LLSD one = LLSD::emptyArray();
        one.append(pair(LLSD::Real(0.5), LLSD::Real(0.5)));
        UNSCOPED_INFO("a lone pair refused");
        CHECK_FALSE(ALToneCurveSet::pointsFromLLSD(one, out));
        UNSCOPED_INFO("identity again");
        CHECK((S32)out.size() == 2);

        // Out of range is clamped by the model, not rejected.
        LLSD wild = LLSD::emptyArray();
        wild.append(pair(LLSD::Real(-1.0), LLSD::Real(-1.0)));
        wild.append(pair(LLSD::Real(2.0), LLSD::Real(2.0)));
        ALToneCurveSet set;
        CHECK(set.setCurveFromLLSD(ALToneCurveSet::CH_RED, wild));
        for (const auto& p : set.curve(ALToneCurveSet::CH_RED).getPoints())
        {
            REQUIRE(p.mX >= 0.f);
            REQUIRE(p.mX <= 1.f);
            REQUIRE(p.mY >= 0.f);
            REQUIRE(p.mY <= 1.f);
        }

        // More points than the editor adds are kept as long as a file could
        // reasonably hold them; past the stored limit the list is garbage.
        LLSD many = LLSD::emptyArray();
        for (S32 i = 0; i < 40; ++i)
        {
            const F64 x = i / 39.0;
            many.append(pair(LLSD::Real(x), LLSD::Real(x * x)));
        }
        CHECK(set.setCurveFromLLSD(ALToneCurveSet::CH_BLUE, many));
        const auto& kept = set.curve(ALToneCurveSet::CH_BLUE).getPoints();
        REQUIRE((S32)kept.size() == 40);
        CHECK_THAT(kept.front().mX, ll_test::WithinBits(0.f, 6));
        CHECK_THAT(kept.back().mX, ll_test::WithinBits(1.f, 6));
        UNSCOPED_INFO("shape honoured");
        CHECK_THAT(set.curve(ALToneCurveSet::CH_BLUE).evaluate(0.5f), ll_test::WithinBits(0.25f, 4));

        LLSD absurd = LLSD::emptyArray();
        for (S32 i = 0; i <= ALCurveModel::MAX_STORED_POINTS; ++i)
        {
            const F64 x = (F64)i / ALCurveModel::MAX_STORED_POINTS;
            absurd.append(pair(LLSD::Real(x), LLSD::Real(x)));
        }
        UNSCOPED_INFO("past the stored limit is refused");
        CHECK_FALSE(set.setCurveFromLLSD(ALToneCurveSet::CH_BLUE, absurd));
        UNSCOPED_INFO("and renders as no curve");
        CHECK(set.curve(ALToneCurveSet::CH_BLUE).getPointCount() == 2);
    }

    TEST_CASE_METHOD(curve_data, "ALCurveModel: a channel's curve applies first and master on its output", "[ALCurveModel]")
    {
        ALToneCurveSet set;
        set.curve(ALToneCurveSet::CH_RED).setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.25f }, { 1.f, 1.f } }));
        set.curve(ALToneCurveSet::CH_MASTER).setPoints(pts({ { 0.f, 0.f }, { 0.25f, 0.75f }, { 1.f, 1.f } }));

        UNSCOPED_INFO("red then master");
        CHECK_THAT(set.evaluate(ALToneCurveSet::CH_RED, 0.5f), ll_test::WithinBits(0.75f, 4));
        UNSCOPED_INFO("is master(red(x))");
        CHECK_THAT(set.evaluate(ALToneCurveSet::CH_RED, 0.5f),
                                    ll_test::WithinBits(set.curve(ALToneCurveSet::CH_MASTER).evaluate(set.curve(ALToneCurveSet::CH_RED).evaluate(0.5f)), 6));
        UNSCOPED_INFO("an identity channel is just master");
        CHECK_THAT(set.evaluate(ALToneCurveSet::CH_GREEN, 0.5f),
                                    ll_test::WithinBits(set.curve(ALToneCurveSet::CH_MASTER).evaluate(0.5f), 6));
        UNSCOPED_INFO("master alone is master");
        CHECK_THAT(set.evaluate(ALToneCurveSet::CH_MASTER, 0.5f),
                                    ll_test::WithinBits(set.curve(ALToneCurveSet::CH_MASTER).evaluate(0.5f), 6));

        // A master edit moves every channel.
        ALToneCurveSet master_only;
        master_only.curve(ALToneCurveSet::CH_MASTER).setPoints(pts({ { 0.f, 0.1f }, { 1.f, 0.9f } }));
        for (S32 c = ALToneCurveSet::CH_RED; c <= ALToneCurveSet::CH_BLUE; ++c)
        {
            CAPTURE(c);
            REQUIRE_THAT(master_only.evaluate((ALToneCurveSet::EChannel)c, 0.f), ll_test::WithinBits(0.1f, 5));
        }
    }

    // Identity detection is exact on the points, with a tolerance far below
    // anything visible, and it is per set: one non-identity channel is enough.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: identity is detected within a tiny tolerance and per set", "[ALCurveModel]")
    {
        ALCurveModel close_curve;
        close_curve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.5f + 0.5f * ALToneCurveSet::IDENTITY_EPS }, { 1.f, 1.f } }));
        UNSCOPED_INFO("within eps is identity");
        CHECK(ALToneCurveSet::isIdentityCurve(close_curve));

        ALCurveModel far_curve;
        far_curve.setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.5f + 10.f * ALToneCurveSet::IDENTITY_EPS }, { 1.f, 1.f } }));
        UNSCOPED_INFO("beyond eps is not");
        CHECK_FALSE(ALToneCurveSet::isIdentityCurve(far_curve));

        ALToneCurveSet set;
        CHECK(set.isIdentity());
        set.curve(ALToneCurveSet::CH_BLUE).setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.6f }, { 1.f, 1.f } }));
        UNSCOPED_INFO("one channel is enough");
        CHECK_FALSE(set.isIdentity());
    }

    // The baked row: one RGBA16 texel per sample, the curve's ends at the
    // row's ends, and a monotone curve gives a monotone row.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: the baked row holds the curve's ends and is monotone and opaque", "[ALCurveModel]")
    {
        ALToneCurveSet set;
        set.curve(ALToneCurveSet::CH_MASTER).setPoints(
            pts({ { 0.f, 0.f }, { 0.2f, 0.05f }, { 0.4f, 0.15f }, { 0.5f, 0.5f }, { 0.6f, 0.85f }, { 0.8f, 0.95f }, { 1.f, 1.f } }));
        set.curve(ALToneCurveSet::CH_RED).setPoints(pts({ { 0.f, 0.1f }, { 0.5f, 0.45f }, { 1.f, 0.9f } }));

        const S32 n = ALToneCurveSet::LUT_SIZE;
        std::vector<U16> texels;
        set.bake(texels, n);
        REQUIRE((S32)texels.size() == n * 4);

        for (S32 c = 0; c < 3; ++c)
        {
            const ALToneCurveSet::EChannel channel = (ALToneCurveSet::EChannel)(ALToneCurveSet::CH_RED + c);
            const F32 lo = set.evaluate(channel, 0.f) * 65535.f;
            const F32 hi = set.evaluate(channel, 1.f) * 65535.f;
            CAPTURE(c);
            UNSCOPED_INFO("first texel is the curve at 0");
            REQUIRE(fabsf((F32)texels[c] - lo) <= 1.f);
            UNSCOPED_INFO("last texel is the curve at 1");
            REQUIRE(fabsf((F32)texels[(n - 1) * 4 + c] - hi) <= 1.f);
            for (S32 i = 1; i < n; ++i)
            {
                CAPTURE(i);
                REQUIRE(texels[i * 4 + c] >= texels[(i - 1) * 4 + c]);
            }
        }
        for (S32 i = 0; i < n; ++i)
        {
            CAPTURE(i);
            REQUIRE((S32)texels[i * 4 + 3] == (S32)ALToneCurveSet::ALPHA_OPAQUE);
        }

        std::vector<U16> tiny;
        set.bake(tiny, 1);
        UNSCOPED_INFO("a size below 2 is raised to 2");
        CHECK((S32)tiny.size() == 2 * 4);
    }

    // What the shader will read back agrees with the analytic curve to well
    // under the display quantum: the half-texel mapping plus the hardware's
    // linear filter over LUT_SIZE samples. A pure-Python transcription of this
    // fetch measured a worst case of 2.8e-4 (0.29/1024) for this pair of
    // curves at 512 samples with 8-bit filter weights, and 0.50/1024 even with
    // the 4-bit weights old hardware is allowed; at 256 samples the 4-bit case
    // reaches 0.97/1024, which is why 512 is the size that ships.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: the shader's LUT fetch agrees with the curve to under 1/1024", "[ALCurveModel]")
    {
        ALToneCurveSet set;
        set.curve(ALToneCurveSet::CH_MASTER).setPoints(
            pts({ { 0.f, 0.f }, { 0.2f, 0.05f }, { 0.4f, 0.15f }, { 0.5f, 0.5f }, { 0.6f, 0.85f }, { 0.8f, 0.95f }, { 1.f, 1.f } }));
        set.curve(ALToneCurveSet::CH_RED).setPoints(pts({ { 0.f, 0.f }, { 0.5f, 0.45f }, { 1.f, 1.f } }));

        const S32 n = ALToneCurveSet::LUT_SIZE;
        std::vector<U16> texels;
        set.bake(texels, n);
        REQUIRE((S32)texels.size() == n * 4);

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
            CAPTURE(c);
            UNSCOPED_INFO("endpoint 0 exact to the U16");
            REQUIRE(fabsf(shaderLutFetch(texels, n, c, 0.f) - set.evaluate(channel, 0.f)) <= 1.f / 65535.f + 1e-6f);
            UNSCOPED_INFO("endpoint 1 exact to the U16");
            REQUIRE(fabsf(shaderLutFetch(texels, n, c, 1.f) - set.evaluate(channel, 1.f)) <= 1.f / 65535.f + 1e-6f);
        }
        UNSCOPED_INFO("reconstruction error below 1/1024");
        CHECK(worst < 1.f / 1024.f);
    }

    // The half-texel mapping puts x = 0 on the centre of the first texel and
    // x = 1 on the centre of the last, for any row width.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: the half-texel mapping puts 0 and 1 on the end texels' centres", "[ALCurveModel]")
    {
        for (S32 n : { 2, 256, 512 })
        {
            F32 scale, bias;
            ALToneCurveSet::lutScaleBias(n, scale, bias);
            const F32 u0 = 0.f * scale + bias;
            const F32 u1 = 1.f * scale + bias;
            CAPTURE(n);
            REQUIRE_THAT(u0 * (F32)n - 0.5f, ll_test::WithinBits(0.f, 6));
            REQUIRE_THAT(u1 * (F32)n - 0.5f, ll_test::WithinBits((F32)(n - 1), 6));
        }
    }

    TEST_CASE_METHOD(curve_data, "ALCurveModel: combo values map onto channels and anything else onto master", "[ALCurveModel]")
    {
        CHECK((S32)ALToneCurveSet::channelFromCombo(-1) == (S32)ALToneCurveSet::CH_MASTER);
        CHECK((S32)ALToneCurveSet::channelFromCombo(0) == (S32)ALToneCurveSet::CH_RED);
        CHECK((S32)ALToneCurveSet::channelFromCombo(1) == (S32)ALToneCurveSet::CH_GREEN);
        CHECK((S32)ALToneCurveSet::channelFromCombo(2) == (S32)ALToneCurveSet::CH_BLUE);
        UNSCOPED_INFO("anything else is master");
        CHECK((S32)ALToneCurveSet::channelFromCombo(7) == (S32)ALToneCurveSet::CH_MASTER);

        const LLSD ident = ALToneCurveSet::identityLLSD();
        UNSCOPED_INFO("identity is two points");
        REQUIRE((S32)ident.size() == 2);
        UNSCOPED_INFO("from the origin");
        CHECK_THAT((F32)ident[0][0].asReal(), ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("to the corner");
        CHECK_THAT((F32)ident[1][1].asReal(), ll_test::WithinBits(1.f, 6));
    }

    // An add at either edge lands one gap inside the pinned end, never on it,
    // so the curve still ends where the end says and the end stays the end.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: an add at either edge lands one gap inside the pinned end", "[ALCurveModel]")
    {
        UNSCOPED_INFO("added at the right edge");
        CHECK(mCurve.addPoint(1.f, 0.03f) >= 0);
        REQUIRE(mCurve.getPointCount() == 3);
        UNSCOPED_INFO("interior, one gap inside");
        CHECK_THAT(mCurve.getPoints()[1].mX, ll_test::WithinBits(1.f - ALCurveModel::MIN_POINT_GAP, 5));
        UNSCOPED_INFO("the end is still the end");
        CHECK_THAT(mCurve.getPoints().back().mX, ll_test::WithinBits(1.f, 6));
        UNSCOPED_INFO("and still at its height");
        CHECK_THAT(mCurve.getPoints().back().mY, ll_test::WithinBits(1.f, 6));
        UNSCOPED_INFO("evaluate(1) is the end");
        CHECK_THAT(mCurve.evaluate(1.f), ll_test::WithinBits(1.f, 6));

        UNSCOPED_INFO("added at the left edge");
        CHECK(mCurve.addPoint(0.f, 0.9f) >= 0);
        UNSCOPED_INFO("the start is still the start");
        CHECK_THAT(mCurve.getPoints().front().mX, ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("and at its height");
        CHECK_THAT(mCurve.getPoints().front().mY, ll_test::WithinBits(0.f, 6));
        UNSCOPED_INFO("interior, one gap inside");
        CHECK_THAT(mCurve.getPoints()[1].mX, ll_test::WithinBits(ALCurveModel::MIN_POINT_GAP, 5));
        UNSCOPED_INFO("evaluate(0) is the start");
        CHECK_THAT(mCurve.evaluate(0.f), ll_test::WithinBits(0.f, 6));
    }

    // Points piled up on the top edge are pushed down, not left coincident:
    // the separation pass works from both ends.
    TEST_CASE_METHOD(curve_data, "ALCurveModel: points piled up on the top edge are pushed apart below it", "[ALCurveModel]")
    {
        mCurve.setPoints(pts({ { 0.f, 0.f }, { 1.f, 0.2f }, { 1.f, 0.4f }, { 1.f, 0.6f }, { 1.f, 1.f } }));
        const auto& p = mCurve.getPoints();
        REQUIRE((S32)p.size() == 5);
        for (size_t i = 1; i < p.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(p[i].mX - p[i - 1].mX >= ALCurveModel::MIN_POINT_GAP - 1e-6f);
        }
        UNSCOPED_INFO("the end is still at 1");
        CHECK_THAT(p.back().mX, ll_test::WithinBits(1.f, 6));
        UNSCOPED_INFO("evaluate(1) is the end");
        CHECK_THAT(mCurve.evaluate(1.f), ll_test::WithinBits(1.f, 6));

        std::vector<F32> s;
        mCurve.sample(s, 1025);
        for (size_t i = 1; i < s.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(std::isfinite(s[i]));
            REQUIRE(s[i] >= s[i - 1] - 1e-5f);
        }
    }

    TEST_CASE_METHOD(curve_data, "ALCurveModel: the setting names are distinct and named for the curve", "[ALCurveModel]")
    {
        for (S32 c = 0; c < ALToneCurveSet::CH_COUNT; ++c)
        {
            const std::string name = ALToneCurveSet::settingName((ALToneCurveSet::EChannel)c);
            CAPTURE(c);
            REQUIRE(name.rfind("RenderColorGradeCurve", 0) == 0);
            for (S32 d = 0; d < c; ++d)
            {
                CAPTURE(d);
                REQUIRE(name != ALToneCurveSet::settingName((ALToneCurveSet::EChannel)d));
            }
        }
        CHECK(std::string(ALToneCurveSet::settingName(ALToneCurveSet::CH_MASTER)) ==
                      std::string("RenderColorGradeCurveMaster"));
    }
}
