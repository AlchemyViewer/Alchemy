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

#include "../test/lltut.h"

#include "../alwhitebalancesolver.h"

#include <cmath>

namespace tut
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

    typedef test_group<wb_data> wb_group;
    typedef wb_group::object    wb_object;
    tut::wb_group wbg("ALWhiteBalanceSolver");

    // Neutral in, neutral out. The shader's identity fast path tests the gain
    // against exactly vec3(1), so anything else here would leave the default
    // settings paying for a colour transform that does nothing.
    template<> template<>
    void wb_object::test<1>()
    {
        const LLVector3 g = ALWhiteBalanceSolver::gain(0.f, 0.f);
        ensure_approximately_equals("neutral red",   g.mV[VX], 1.f, 6);
        ensure_approximately_equals("neutral green", g.mV[VY], 1.f, 6);
        ensure_approximately_equals("neutral blue",  g.mV[VZ], 1.f, 6);
    }

    // Green is pinned across the whole range, which is what makes temperature
    // a colour control rather than an exposure one.
    template<> template<>
    void wb_object::test<2>()
    {
        for (S32 i = -5; i <= 5; ++i)
        {
            for (S32 j = -2; j <= 2; ++j)
            {
                const LLVector3 g = ALWhiteBalanceSolver::gain(i * 1000.f, j * 0.5f);
                ensure_approximately_equals("green stays pinned", g.mV[VY], 1.f, 6);
            }
        }
    }

    // The direction of the temperature control. A negative offset asks for a
    // warmer scene, which the renderer delivers by pushing red up relative to
    // blue -- if this ever inverts, every tooltip in the Basic panel is wrong.
    template<> template<>
    void wb_object::test<3>()
    {
        const LLVector3 warm = ALWhiteBalanceSolver::gain(-2500.f, 0.f);
        const LLVector3 cool = ALWhiteBalanceSolver::gain( 2500.f, 0.f);
        ensure("warm lifts red above blue", warm.mV[VX] > warm.mV[VZ]);
        ensure("cool lifts blue above red", cool.mV[VZ] > cool.mV[VX]);
    }

    // The round trip the eyedropper depends on: every usable pair is recovered
    // from the gain it produces.
    template<> template<>
    void wb_object::test<4>()
    {
        S32 tested = 0;
        for (S32 i = -4; i <= 4; ++i)
        {
            for (S32 j = -4; j <= 4; ++j)
            {
                const F32 cct = i * 1200.f;
                const F32 duv = j * 0.25f;

                // Out of gamut is out of scope: down there the blue gain is
                // negative, so every candidate looks alike to the solver and
                // no inverse exists to test. Test <9> covers that region.
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
                ensure("gain round-trips", gainDistance(want, back) < 1e-3f);
                ensure("and the solver knows it did", got.mResidual < 1e-3f);
            }
        }
        // Guards against the skip above quietly emptying the sweep.
        ensure("the sweep covered most of the range", tested > 60);
    }

    // A solution never escapes the sliders' range, whatever it is asked for.
    // Out here the answer is a best effort against the edge of the box, and
    // the residual is how the caller finds out.
    template<> template<>
    void wb_object::test<5>()
    {
        // Both free channels pulled well below green: "make it much greener
        // than any light source is". Temperature trades red against blue and
        // tint moves them together but only so far, so this lies off the
        // reachable surface entirely -- unlike, say, (6, 1, 0.05), which looks
        // extreme and turns out to be very nearly on it.
        const LLVector3 absurd(0.3f, 1.f, 0.3f);
        const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solve(absurd);

        ensure("cct stays in range",
               got.mCCTOffset >= ALWhiteBalanceSolver::CCT_MIN &&
               got.mCCTOffset <= ALWhiteBalanceSolver::CCT_MAX);
        ensure("duv stays in range",
               got.mDuv >= ALWhiteBalanceSolver::DUV_MIN &&
               got.mDuv <= ALWhiteBalanceSolver::DUV_MAX);
        ensure("and it reports that it could not get there", got.mResidual > 0.1f);
    }

    // The gain that neutralises a colour, and the sign of what it does.
    template<> template<>
    void wb_object::test<6>()
    {
        const LLVector3 g = ALWhiteBalanceSolver::neutralisingGain(LLColor3(0.4f, 0.5f, 0.8f));
        ensure_approximately_equals("green pinned", g.mV[VY], 1.f, 6);
        ensure_approximately_equals("red is lifted to meet green", g.mV[VX], 1.25f, 5);
        ensure_approximately_equals("blue is pulled down to it",   g.mV[VZ], 0.625f, 5);

        // Applying it does what it says.
        const LLColor3 sample(0.4f, 0.5f, 0.8f);
        ensure_approximately_equals("corrected red equals green",  sample.mV[0] * g.mV[VX], sample.mV[1], 5);
        ensure_approximately_equals("corrected blue equals green", sample.mV[2] * g.mV[VZ], sample.mV[1], 5);

        // Scale-invariant: the eyedropper cares about the colour of a sample,
        // never how brightly it was lit.
        const LLVector3 dim = ALWhiteBalanceSolver::neutralisingGain(LLColor3(0.04f, 0.05f, 0.08f));
        ensure("brightness does not change the answer", gainDistance(g, dim) < 1e-4f);
    }

    // The whole eyedropper, end to end: take a neutral surface, light it the
    // way some (cct, duv) pair would, and the solver should recover the pair
    // that undoes it.
    template<> template<>
    void wb_object::test<7>()
    {
        for (S32 i = -3; i <= 3; ++i)
        {
            const F32 cct = i * 1500.f;

            // A grey surface seen through the inverse of the correction: this
            // is what the scene buffer holds when the light is that colour.
            const LLVector3 correction = ALWhiteBalanceSolver::gain(cct, 0.f);
            const LLColor3 lit(0.5f / correction.mV[VX], 0.5f, 0.5f / correction.mV[VZ]);

            const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solveForColor(lit);
            const LLVector3 back = ALWhiteBalanceSolver::gain(got.mCCTOffset, got.mDuv);

            ensure("recovers the light's own correction",
                   gainDistance(correction, back) < 1e-3f);

            // And the sample really does come out neutral.
            const LLColor3 fixed(lit.mV[0] * back.mV[VX], lit.mV[1], lit.mV[2] * back.mV[VZ]);
            ensure_approximately_equals("neutralised red",  fixed.mV[0], fixed.mV[1], 4);
            ensure_approximately_equals("neutralised blue", fixed.mV[2], fixed.mV[1], 4);
        }
    }

    // An already-neutral sample asks for nothing, so clicking a grey wall in
    // a correctly balanced scene must not nudge the sliders off zero.
    template<> template<>
    void wb_object::test<8>()
    {
        const ALWhiteBalanceSolver::Result got =
            ALWhiteBalanceSolver::solveForColor(LLColor3(0.5f, 0.5f, 0.5f));
        ensure("cct stays put", std::fabs(got.mCCTOffset) < 1.f);
        ensure("duv stays put", std::fabs(got.mDuv) < 1e-3f);
        ensure("exactly reachable", got.mResidual < 1e-4f);
    }

    // The warm end of the Temperature slider leaves the sRGB gamut: below
    // roughly 1900K the locus is outside it and the XYZ-to-sRGB matrix returns
    // a negative blue gain, which multiplied into a frame flips the channel's
    // sign. That is the renderer's existing behaviour and this fixes none of
    // it; what is pinned here is that the solver stays out of the region, so
    // the eyedropper can never hand a user a balance that does that.
    template<> template<>
    void wb_object::test<9>()
    {
        ensure("neutral is usable", ALWhiteBalanceSolver::isUsable(0.f, 0.f));
        ensure("the cool end is usable", ALWhiteBalanceSolver::isUsable(5000.f, 0.f));
        ensure("the warm extreme is not", !ALWhiteBalanceSolver::isUsable(-5000.f, 0.f));
        // Tint moves the boundary: pushing green costs gamut at the warm end.
        ensure("-4000 is usable at neutral tint", ALWhiteBalanceSolver::isUsable(-4000.f, 0.f));
        ensure("but not at full green tint", !ALWhiteBalanceSolver::isUsable(-4000.f, 1.f));

        // Ask for something only the out-of-gamut region could match, and the
        // answer must still be a balance that works.
        const LLVector3 want = ALWhiteBalanceSolver::gain(-5000.f, 0.f);
        const ALWhiteBalanceSolver::Result got = ALWhiteBalanceSolver::solve(want);
        ensure("the solution is usable", ALWhiteBalanceSolver::isUsable(got.mCCTOffset, got.mDuv));
        const LLVector3 back = ALWhiteBalanceSolver::gain(got.mCCTOffset, got.mDuv);
        ensure("its red gain is positive", back.mV[VX] > 0.f);
        ensure("and so is its blue", back.mV[VZ] > 0.f);
    }
}
