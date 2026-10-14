/**
 * @file alwhitebalancesolver_test.cpp
 * @brief Unit tests for the white-balance forward map and its inverse
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

#include "../alwhitebalancesolver.h"

#include <cmath>

namespace
{
    struct wb_data
    {
        /// How far apart two gains are, as the solver measures it: RMS of the
        /// log ratios of the two free channels. Green is pinned to 1 on both
        /// sides by construction, so it carries no information.
        static F32 gainDistance(const LLVector3& a, const LLVector3& b)
        {
            const F32 dr = std::log(std::max(a.mV[VX], 1e-6f)) - std::log(std::max(b.mV[VX], 1e-6f));
            const F32 db = std::log(std::max(a.mV[VZ], 1e-6f)) - std::log(std::max(b.mV[VZ], 1e-6f));
            return std::sqrt((dr * dr + db * db) * 0.5f);
        }
    };

    // Neutral in, neutral out. The shader's identity fast path tests the gain
    // against exactly vec3(1), so anything else here would leave the default
    // settings paying for a colour transform that does nothing.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: neutral in gives a gain of one", "[ALWhiteBalanceSolver]")
    {
        const LLVector3 g = ALWhiteBalanceSolver::gain(0.f, 0.f);
        CHECK(g.mV[VX] == 1.f);
        CHECK(g.mV[VY] == 1.f);
        CHECK(g.mV[VZ] == 1.f);
    }

    // Green is pinned across the whole range, which is what makes temperature
    // a colour control rather than an exposure one.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: the green gain stays pinned at one across the whole range", "[ALWhiteBalanceSolver]")
    {
        for (S32 i = -5; i <= 5; ++i)
        {
            for (S32 j = -2; j <= 2; ++j)
            {
                const LLVector3 g = ALWhiteBalanceSolver::gain(i * 1000.f, j * 0.5f);
                CAPTURE(i, j);
                REQUIRE_THAT(g.mV[VY], ll_test::WithinBits(1.f, 6));
            }
        }
    }

    // The direction of the temperature control. A negative offset asks for a
    // warmer scene, which the renderer delivers by pushing red up relative to
    // blue -- if this ever inverts, every tooltip in the Basic panel is wrong.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: a negative temperature offset warms and a positive one cools", "[ALWhiteBalanceSolver]")
    {
        const LLVector3 warm = ALWhiteBalanceSolver::gain(-2500.f, 0.f);
        const LLVector3 cool = ALWhiteBalanceSolver::gain( 2500.f, 0.f);
        UNSCOPED_INFO("warm lifts red above blue");
        CHECK(warm.mV[VX] > warm.mV[VZ]);
        UNSCOPED_INFO("cool lifts blue above red");
        CHECK(cool.mV[VZ] > cool.mV[VX]);
    }

    // The round trip the eyedropper depends on: every usable pair is recovered
    // from the gain it produces.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: solve recovers every usable pair from the gain it produces", "[ALWhiteBalanceSolver]")
    {
        S32 tested = 0;
        for (S32 i = -4; i <= 4; ++i)
        {
            for (S32 j = -4; j <= 4; ++j)
            {
                const F32 cct = i * 1200.f;
                const F32 duv = j * 0.25f;
                CAPTURE(cct, duv);

                // Out of gamut is out of scope: down there the blue gain is
                // negative, so every candidate looks alike to the solver and
                // no inverse exists to test. The out-of-gamut case below
                // covers that region.
                if (!ALWhiteBalanceSolver::isUsable(cct, duv))
                {
                    continue;
                }
                ++tested;

                const LLVector3 want = ALWhiteBalanceSolver::gain(cct, duv);
                const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solve(want);

                // Compared on the gain, not on the parameters. The map is not
                // equally sensitive everywhere -- a thousand Kelvin at the
                // warm end moves the gain far less than at the cool end -- so
                // a tolerance in Kelvin would be either slack where it matters
                // or unachievable where it does not. What has to round-trip is
                // the colour the renderer will actually apply.
                const LLVector3 back = ALWhiteBalanceSolver::gain(got.mCCTOffset, got.mDuv);
                UNSCOPED_INFO("gain round-trips");
                REQUIRE(gainDistance(want, back) < 1e-3f);
                UNSCOPED_INFO("and the solver knows it did");
                REQUIRE(got.mResidual < 1e-3f);
            }
        }
        // Guards against the skip above quietly emptying the sweep.
        UNSCOPED_INFO("the sweep covered most of the range");
        CHECK(tested > 60);
    }

    // A solution never escapes the sliders' range, whatever it is asked for.
    // Out here the answer is a best effort against the edge of the box, and
    // the residual is how the caller finds out.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: a solution stays in the sliders' range whatever it is asked for", "[ALWhiteBalanceSolver]")
    {
        // Both free channels pulled well below green: "make it much greener
        // than any light source is". Temperature trades red against blue and
        // tint moves them together but only so far, so this lies off the
        // reachable surface entirely -- unlike, say, (6, 1, 0.05), which looks
        // extreme and turns out to be very nearly on it.
        const LLVector3 absurd(0.3f, 1.f, 0.3f);
        const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solve(absurd);

        {
            INFO("cct stays in range");
            CHECK(got.mCCTOffset >= ALWhiteBalanceSolver::CCT_MIN);
            CHECK(got.mCCTOffset <= ALWhiteBalanceSolver::CCT_MAX);
        }
        {
            INFO("duv stays in range");
            CHECK(got.mDuv >= ALWhiteBalanceSolver::DUV_MIN);
            CHECK(got.mDuv <= ALWhiteBalanceSolver::DUV_MAX);
        }
        UNSCOPED_INFO("and it reports that it could not get there");
        CHECK(got.mResidual > 0.1f);
    }

    // The gain that neutralises a colour, and the sign of what it does.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: neutralisingGain pins green and scales red and blue to meet it", "[ALWhiteBalanceSolver]")
    {
        const LLVector3 g = ALWhiteBalanceSolver::neutralisingGain(LLColor3(0.4f, 0.5f, 0.8f));
        CHECK_THAT(g.mV[VY], ll_test::WithinBits(1.f, 6));
        UNSCOPED_INFO("red is lifted to meet green");
        CHECK_THAT(g.mV[VX], ll_test::WithinBits(1.25f, 5));
        UNSCOPED_INFO("blue is pulled down to it");
        CHECK_THAT(g.mV[VZ], ll_test::WithinBits(0.625f, 5));

        // Applying it does what it says.
        const LLColor3 sample(0.4f, 0.5f, 0.8f);
        UNSCOPED_INFO("corrected red equals green");
        CHECK_THAT(sample.mV[0] * g.mV[VX], ll_test::WithinBits(sample.mV[1], 5));
        UNSCOPED_INFO("corrected blue equals green");
        CHECK_THAT(sample.mV[2] * g.mV[VZ], ll_test::WithinBits(sample.mV[1], 5));

        // Scale-invariant: the eyedropper cares about the colour of a sample,
        // never how brightly it was lit.
        const LLVector3 dim = ALWhiteBalanceSolver::neutralisingGain(LLColor3(0.04f, 0.05f, 0.08f));
        UNSCOPED_INFO("brightness does not change the answer");
        CHECK(gainDistance(g, dim) < 1e-4f);
    }

    // The whole eyedropper, end to end: take a neutral surface, light it the
    // way some (cct, duv) pair would, and the solver should recover the pair
    // that undoes it.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: solveForColor recovers the light that tinted a grey surface", "[ALWhiteBalanceSolver]")
    {
        for (S32 i = -3; i <= 3; ++i)
        {
            const F32 cct = i * 1500.f;
            CAPTURE(cct);

            // A grey surface seen through the inverse of the correction: this
            // is what the scene buffer holds when the light is that colour.
            const LLVector3 correction = ALWhiteBalanceSolver::gain(cct, 0.f);
            const LLColor3 lit(0.5f / correction.mV[VX], 0.5f, 0.5f / correction.mV[VZ]);

            const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solveForColor(lit);
            const LLVector3 back = ALWhiteBalanceSolver::gain(got.mCCTOffset, got.mDuv);

            UNSCOPED_INFO("recovers the light's own correction");
            REQUIRE(gainDistance(correction, back) < 1e-3f);

            // And the sample really does come out neutral.
            const LLColor3 fixed(lit.mV[0] * back.mV[VX], lit.mV[1], lit.mV[2] * back.mV[VZ]);
            UNSCOPED_INFO("neutralised red");
            REQUIRE_THAT(fixed.mV[0], ll_test::WithinBits(fixed.mV[1], 4));
            UNSCOPED_INFO("neutralised blue");
            REQUIRE_THAT(fixed.mV[2], ll_test::WithinBits(fixed.mV[1], 4));
        }
    }

    // An already-neutral sample asks for nothing, so clicking a grey wall in
    // a correctly balanced scene must not nudge the sliders off zero.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: an already-neutral sample asks for no correction", "[ALWhiteBalanceSolver]")
    {
        const ALWhiteBalanceSolver::Result got =
            ALWhiteBalanceSolver::solveForColor(LLColor3(0.5f, 0.5f, 0.5f));
        CHECK(std::fabs(got.mCCTOffset) < 1.f);
        CHECK(std::fabs(got.mDuv) < 1e-3f);
        UNSCOPED_INFO("exactly reachable");
        CHECK(got.mResidual < 1e-4f);
    }

    // The warm end of the Temperature slider leaves the sRGB gamut: below
    // roughly 1900K the locus is outside it and the XYZ-to-sRGB matrix returns
    // a negative blue gain, which multiplied into a frame flips the channel's
    // sign. That is the renderer's existing behaviour and this fixes none of
    // it; what is pinned here is that the solver stays out of the region, so
    // the eyedropper can never hand a user a balance that does that.
    TEST_CASE_METHOD(wb_data, "ALWhiteBalanceSolver: the solver stays out of the out-of-gamut warm end", "[ALWhiteBalanceSolver]")
    {
        UNSCOPED_INFO("neutral is usable");
        CHECK(ALWhiteBalanceSolver::isUsable(0.f, 0.f));
        UNSCOPED_INFO("the cool end is usable");
        CHECK(ALWhiteBalanceSolver::isUsable(5000.f, 0.f));
        UNSCOPED_INFO("the warm extreme is not");
        CHECK_FALSE(ALWhiteBalanceSolver::isUsable(-5000.f, 0.f));
        // Tint moves the boundary: pushing green costs gamut at the warm end.
        UNSCOPED_INFO("-4000 is usable at neutral tint");
        CHECK(ALWhiteBalanceSolver::isUsable(-4000.f, 0.f));
        UNSCOPED_INFO("but not at full green tint");
        CHECK_FALSE(ALWhiteBalanceSolver::isUsable(-4000.f, 1.f));

        // Ask for something only the out-of-gamut region could match, and the
        // answer must still be a balance that works.
        const LLVector3 want = ALWhiteBalanceSolver::gain(-5000.f, 0.f);
        const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solve(want);
        UNSCOPED_INFO("the solution is usable");
        CHECK(ALWhiteBalanceSolver::isUsable(got.mCCTOffset, got.mDuv));
        const LLVector3 back = ALWhiteBalanceSolver::gain(got.mCCTOffset, got.mDuv);
        UNSCOPED_INFO("its red gain is positive");
        CHECK(back.mV[VX] > 0.f);
        UNSCOPED_INFO("and so is its blue");
        CHECK(back.mV[VZ] > 0.f);
    }
}
