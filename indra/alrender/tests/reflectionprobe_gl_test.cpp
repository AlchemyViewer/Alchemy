/**
 * @file reflectionprobe_gl_test.cpp
 * @brief class3/deferred/reflectionProbeF.glsl's probe sampling, one pass over the probes that taps each as it finds
 *        it, against the probes sampled as they were before: a list of the probes that hold the point, then a pass
 *        over it for each tap. Over a scene of automatic and manual spheres and boxes, neighbour lists and the default
 *        probe (reflectionprobe_gl_scene.h), every entry point, to a rounding at unit magnitude, and with the
 *        screen-space reflections and a hero probe over them.
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

#include "reflectionprobe_gl_scene.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cmath>
#include <set>
#include <vector>

namespace
{
    using reflectionprobe_gl::ProbeScene;

    constexpr S32 SIZE = 256;

    // Draws the one pass and the list through each entry point and checks the taps agree.
    void compareEntryPoints(ProbeScene& scene)
    {
        const char* apis[] = { "sampleReflectionProbes", "sampleReflectionProbesLegacy", "sampleReflectionProbesWater",
                               "sampleReflectionProbesEither" };
        for (S32 api = 0; api < 4; ++api)
        {
            CAPTURE(apis[api]);
            scene.draw(scene.mFused, api);
            const std::vector<F32> fused = scene.read();
            scene.draw(scene.mListed, api);
            const std::vector<F32> listed = scene.read();
            CHECK((U32)glGetError() == (U32)GL_NO_ERROR);

            // Each tap as the list's to a rounding at unit magnitude. Where a manual sphere's taps are mixed with the
            // automatic probes', the compiler rounds the pass, which weighs each probe once for all its taps, apart
            // from the list's passes, which weigh it once for each, by up to that: 1.2e-7 on the NVIDIA driver this
            // was written on. Wherever a box shuts the automatic probes out, or one kind of probe alone holds the
            // point, the two agree to the bit. And taken where the probes vary: every tap the entry point writes, the
            // ambient and the gloss always and the legacy environment from the legacy entry points, many values over
            // the pixels, where a pass that tapped nothing would leave one and the tap's untouched starting value.
            // Counted by their bits, which order every value, a NaN's too.
            const size_t texels = size_t(SIZE) * SIZE * 4;
            S32 differ = 0;
            std::set<U32> values[3];
            for (size_t i = 0; i < fused.size(); i += 4)
            {
                values[i / texels].insert(std::bit_cast<U32>(fused[i]));
                bool close = true;
                for (size_t c = 0; c < 3; ++c)
                {
                    close = close &&
                            std::fabs(fused[i + c] - listed[i + c]) <= 4e-7f * llmax(1.f, std::fabs(listed[i + c]));
                }
                if (!close && ++differ <= 8)
                {
                    const size_t pixel = (i / 4) % (size_t(SIZE) * SIZE);
                    UNSCOPED_INFO("tap " << i / (size_t(SIZE) * SIZE * 4) << " at " << pixel % SIZE << ", "
                                         << pixel / SIZE << ": " << fused[i] << ", " << fused[i + 1] << ", "
                                         << fused[i + 2] << " against " << listed[i] << ", " << listed[i + 1] << ", "
                                         << listed[i + 2]);
                }
            }
            CHECK(differ == 0);
            const bool legacy = api == 1 || api == 3;
            for (size_t tap = 0; tap < (legacy ? 3 : 2); ++tap)
            {
                CAPTURE(tap);
                CHECK(values[tap].size() > (size_t)SIZE);
            }
        }
    }
}

TEST_CASE_METHOD(ProbeScene, "reflectionprobe_gl: one pass over the probes taps them as their list did",
                 "[reflectionprobe_gl]")
{
    // Every probe level's search, the viewer's list length and one the scenes fill, and the classic sky's taps.
    const S32 level = GENERATE(3, 0);
    const S32 count = GENERATE(32, 3);
    const bool classic = GENERATE(false, true);
    CAPTURE(level, count, classic);
    setUp(SIZE, SIZE);
    REQUIRE(build(mFused, "test/probeFusedF.glsl", level, count, classic));
    REQUIRE(build(mListed, "test/probeListedF.glsl", level, count, classic));
    layProbes(mFused);
    compareEntryPoints(*this);
}

TEST_CASE_METHOD(ProbeScene,
                 "reflectionprobe_gl: the screen-space reflections and the hero probe take the taps they took",
                 "[reflectionprobe_gl]")
{
    // The march over the scene, opaque and transparent, and a mirror's hero probe of either shape over what the
    // probes gave, as the soften pass and the forward shaders take them where both are on.
    mHeroShape = GENERATE(0, 1);
    const bool classic = GENERATE(false, true);
    CAPTURE(mHeroShape, classic);
    setUp(SIZE, SIZE);
    REQUIRE(build(mFused, "test/probeFusedF.glsl", 3, 32, classic, true));
    REQUIRE(build(mListed, "test/probeListedF.glsl", 3, 32, classic, true));
    layProbes(mFused);
    compareEntryPoints(*this);

    // And they reflect something: the gloss and the legacy environment, which the tails mix over, differ from the
    // probes' alone on many pixels through either kind of surface's entry point, by more than the two builds' rounding
    // of the probes.
    for (S32 api : { 0, 1 })
    {
        CAPTURE(api);
        REQUIRE(build(mListed, "test/probeFusedF.glsl", 3, 32, classic, false));
        draw(mFused, api);
        const std::vector<F32> tails = read();
        draw(mListed, api);
        const std::vector<F32> probes = read();
        const size_t texels = size_t(SIZE) * SIZE * 4;
        for (size_t tap = 1; tap < (api == 1 ? 3u : 2u); ++tap)
        {
            CAPTURE(tap);
            S32 reflected = 0;
            for (size_t i = tap * texels; i < (tap + 1) * texels; i += 4)
            {
                bool moved = false;
                for (size_t c = 0; c < 3; ++c)
                {
                    moved = moved ||
                            std::fabs(tails[i + c] - probes[i + c]) > 4e-7f * llmax(1.f, std::fabs(probes[i + c]));
                }
                reflected += moved;
            }
            CHECK(reflected > SIZE);
        }
    }
}
