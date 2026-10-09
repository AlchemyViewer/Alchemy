/**
 * @file skfarplane_test.cpp
 * @brief The projection far plane follows the depth convention: infinite on reverse-Z, the old fixed plane on forward-Z
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "../test/lltut.h"

#include "../skfarplane.h"

#include "llcamera.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace tut
{
    struct skfarplane_data
    {
    };

    // A shader's source, found from this file so the test reads what the viewer compiles.
    static std::string shader_source(const std::string& rel)
    {
        const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / ".." / "app_settings" / "shaders" / rel;
        std::ifstream in(path);
        std::stringstream buf;
        buf << in.rdbuf();
        return buf.str();
    }

    // The number that follows `key` in `text`, or -1 when the key is missing.
    static F32 literal_after(const std::string& text, const std::string& key)
    {
        const size_t at = text.find(key);
        return at == std::string::npos ? -1.f : std::stof(text.substr(at + key.size()));
    }

    typedef test_group<skfarplane_data> skfarplane_group;
    typedef skfarplane_group::object    skfarplane_object;
    tut::skfarplane_group skfarplane_g("skfarplane");

    // The ceilings: forward-Z keeps the old 512 m, reverse-Z gets the raised one.
    template<> template<>
    void skfarplane_object::test<1>()
    {
        ensure_equals("forward ceiling", skDrawDistanceCeiling(false), 512.f);
        ensure_equals("reverse ceiling", skDrawDistanceCeiling(true), 2048.f);
    }

    // Reverse-Z is always infinite; forward-Z and cube snapshots keep the old fixed 1024 m plane.
    template<> template<>
    void skfarplane_object::test<2>()
    {
        ensure("reverse infinite", skIsInfinite(skProjectionFar(true, false)));
        ensure_equals("forward", skProjectionFar(false, false), 1024.f);
        ensure_equals("reverse cube snapshot", skProjectionFar(true, true), 1024.f);
        ensure_equals("forward cube snapshot", skProjectionFar(false, true), 1024.f);
        ensure("1024 is finite", !skIsInfinite(1024.f));
        ensure("infinite constant", skIsInfinite(SK_PROJECTION_INFINITE) && SK_PROJECTION_INFINITE > 0.f);
    }

    // Reach selection: terrain and water, and edge water on each convention.
    template<> template<>
    void skfarplane_object::test<7>()
    {
        ensure_equals("terrain reach under the infinite projection", skTerrainReach(SK_PROJECTION_INFINITE), 8192.f);
        ensure_equals("terrain unbounded on forward-Z", skTerrainReach(skProjectionFar(false, false)), 0.f);
        ensure_equals("terrain unbounded in cube snapshots", skTerrainReach(skProjectionFar(true, true)), 0.f);
        ensure_equals("edge water, infinite", skEdgeWaterStretch(true), 256000.f);
        ensure_equals("edge water, finite keeps 2048", skEdgeWaterStretch(false), 2048.f);
    }

    // The numbers the infinite projection relies on, against an independent double-precision reference.
    template<> template<>
    void skfarplane_object::test<8>()
    {
        // Under the infinite projection the nearest sky is SK_SKY_PIN_DEPTH_INFINITE: everything within reach
        // must store a larger depth, so the sky stays behind it.
        ensure("terrain in front of the sky", MIN_NEAR_PLANE / SK_REACH_TERRAIN > SK_SKY_PIN_DEPTH_INFINITE);
        ensure("reconstruction floor past the reach", SK_RECONSTRUCT_FAR > SK_REACH_TERRAIN);
        // The floors the shaders actually carry, read from their sources.
        const F32 floor = 1.f / SK_RECONSTRUCT_FAR;
        const std::string util = shader_source("class1/deferred/deferredUtil.glsl");
        ensure("deferredUtil read", !util.empty());
        ensure_equals("deferredUtil floor", literal_after(util, "#define SK_RECONSTRUCT_W_FLOOR "), floor);
        ensure_equals("aoUtil floor", literal_after(shader_source("class1/deferred/aoUtil.glsl"), "pos.w = max(pos.w, "), floor);
        ensure_equals("cofF floor", literal_after(shader_source("class1/deferred/cofF.glsl"), "p.z/max(p.w, "), floor);
        // Edge water must reach at least as far as terrain is drawn.
        ensure("edge water covers the terrain reach", skEdgeWaterStretch(true) >= SK_REACH_TERRAIN);
    }

    template<> template<>
    void skfarplane_object::test<11>()
    {
        ensure_equals("infinite", skFrustumFarWindowDepth(true, 0.1f, 0.f), 0.1f / SK_RECONSTRUCT_FAR);
        ensure("infinite depth unprojects to SK_RECONSTRUCT_FAR", fabsf(0.1f / skFrustumFarWindowDepth(true, 0.1f, 0.f) - SK_RECONSTRUCT_FAR) < 1.f);
        ensure_equals("finite, reverse", skFrustumFarWindowDepth(false, 0.1f, 0.f), 0.f);
        ensure_equals("finite, forward", skFrustumFarWindowDepth(false, 0.1f, 1.f), 1.f);
    }

    // Pinned-sky classification, against an independent double-precision reference.
    template<> template<>
    void skfarplane_object::test<12>()
    {
        const SKSkyDepth finite = skSkyDepth(0.1f, 1024.f);
        ensure("finite classifies nothing", finite.mThreshold < 0.f && finite.mReachDepth == 0.f);
        const SKSkyDepth sky = skSkyDepth(MIN_NEAR_PLANE, SK_PROJECTION_INFINITE);
        ensure("threshold", fabsf(sky.mThreshold - 4.08825704e-08f) < 1e-13f);
        ensure("farthest water", fabsf(skFarthestWater() - 445773.0f) < 2.f);
        ensure("farthest water is not sky", MIN_NEAR_PLANE / skFarthestWater() > sky.mThreshold);
        ensure("reach depth", fabsf(sky.mReachDepth - 1.220703e-5f) < 1e-11f);
        ensure("legacy pin distance", fabsf(sky.mLegacyDistance - 974.1294f) < 0.01f);
        ensure("pin is sky", SK_SKY_PIN_DEPTH_INFINITE <= sky.mThreshold);
        // A wide margin on both sides, since the GPU's z/w division may round.
        ensure("margin to the pin", sky.mThreshold > 5.f * SK_SKY_PIN_DEPTH_INFINITE);
        ensure("margin to the farthest water", MIN_NEAR_PLANE / skFarthestWater() > 5.f * sky.mThreshold);
        ensure("reach is not sky", MIN_NEAR_PLANE / SK_REACH_TERRAIN > sky.mThreshold);
        // The haze and lens flare shaders take the pin from the uniform, never a literal of their own.
        ensure("haze uses the pin", shader_source("class3/deferred/hazeF.glsl").find("skPinnedSkyPosition(getPositionWithDepth") != std::string::npos);
        // Water fog must not use the pin: it would place the sky under void water in front of the water.
        ensure("water haze does not use the pin", shader_source("class3/deferred/waterHazeF.glsl").find("skPinnedSkyPosition") == std::string::npos);
    }

    // Water's far terms: upstream's under a finite projection, reach, edge fade and sky threshold under an infinite one.
    template<> template<>
    void skfarplane_object::test<16>()
    {
        set_test_name("water far terms");
        const SKWaterFar finite = skWaterFar(MIN_NEAR_PLANE, 1024.f);
        ensure_equals("finite wave clamp", finite.mWaveClamp, 2560.f);
        ensure_equals("finite no edge fade", finite.mEdgeFade, 0.f);
        const SKWaterFar infinite = skWaterFar(MIN_NEAR_PLANE, SK_PROJECTION_INFINITE);
        ensure_equals("wave clamp at the farthest water", infinite.mWaveClamp, skFarthestWaterHorizontal());
        ensure("wave clamp past the edge water corner", infinite.mWaveClamp >= 1.414f * (SK_REACH_TERRAIN + MAX_FAR_CLIP));
        ensure_equals("edge fade over the stretch", infinite.mEdgeFade, skEdgeWaterStretch(true));
        const std::string water_f = shader_source("class3/environment/waterF.glsl");
        ensure("void water refracts its fog, not the haze", water_f.find("sk_water_far.z") == std::string::npos);
        const std::string haze = shader_source("class3/deferred/waterHazeF.glsl");
        ensure("sky behind water is held past the surface", haze.find("entry + sk_sky_pin.y") != std::string::npos);
        const std::string water_v = shader_source("class1/environment/waterV.glsl");
        ensure("wave clamp from the uniform", water_v.find("min(d, sk_water_far.x)") != std::string::npos);
        ensure("no literal wave clamp", water_v.find("min(d, 2560.0)") == std::string::npos);
        // The per-fragment rebuild scrolls by the offsets waterV takes from the CPU, which sets no wave direction or time.
        for (const char* scroll : { "+ bigWaveScroll;", "+ littleWaveScroll.xy;", "+ littleWaveScroll.zw;" })
        {
            ensure(std::string("waterV scrolls by ") + scroll, water_v.find(scroll) != std::string::npos);
            ensure(std::string("rebuilt waves scroll by ") + scroll, water_f.find(scroll) != std::string::npos);
        }
    }

    // SKRenderFarPlaneForce: automatic passes through, 1 and 2 force on reverse-Z only, cube snapshots never.
    template<> template<>
    void skfarplane_object::test<13>()
    {
        for (F32 far_plane : { 1024.f, SK_PROJECTION_INFINITE })
        {
            ensure_equals("auto", skForcedProjectionFar(far_plane, 0, 2048.f, true, false), far_plane);
            ensure_equals("finite", skForcedProjectionFar(far_plane, 1, 2048.f, true, false), 4096.f);
            ensure("infinite", skIsInfinite(skForcedProjectionFar(far_plane, 2, 2048.f, true, false)));
            ensure_equals("finite never under 1024", skForcedProjectionFar(far_plane, 1, 256.f, true, false), 1024.f);
            ensure_equals("unknown mode", skForcedProjectionFar(far_plane, 7, 2048.f, true, false), far_plane);
            ensure_equals("cube snapshot", skForcedProjectionFar(far_plane, 2, 2048.f, true, true), far_plane);
            ensure_equals("cube snapshot finite", skForcedProjectionFar(far_plane, 1, 2048.f, true, true), far_plane);
        }
        ensure_equals("forward ignores infinite", skForcedProjectionFar(1024.f, 2, 2048.f, false, false), 1024.f);
        ensure_equals("forward ignores finite", skForcedProjectionFar(1024.f, 1, 2048.f, false, false), 1024.f);
    }

    // Sky pins: upstream's exact values under a finite projection; under the infinite one, powers of two from the
    // pin back, with sun behind moon behind the dome behind the higher layers, and the pin ~13,400 km out.
    template<> template<>
    void skfarplane_object::test<19>()
    {
        for (U32 layer = 0; layer < 8; ++layer)
        {
            ensure_equals("finite layer is upstream's " + std::to_string(layer), skSkyLayerDepth(layer, false), 0.000005f + 0.00005f * layer);
        }
        ensure_equals("pin bits", SK_SKY_PIN_DEPTH_INFINITE, ldexpf(1.f, -27));
        ensure("pin is about 13,400 km at 0.1 m", fabsf(MIN_NEAR_PLANE / SK_SKY_PIN_DEPTH_INFINITE - 13421772.8f) < 2.f);
        ensure_equals("dome bits", skSkyLayerDepth(0, true), ldexpf(1.f, -31));
        for (U32 layer = 1; layer < 8; ++layer)
        {
            ensure("layer at or behind the pin " + std::to_string(layer), skSkyLayerDepth(layer, true) <= SK_SKY_PIN_DEPTH_INFINITE);
            ensure("layer nearer than the one below " + std::to_string(layer),
                   skSkyLayerDepth(layer, true) == 2.f * skSkyLayerDepth(layer - 1, true) || layer > SK_SKY_PIN_LAYERS);
        }
        ensure_equals("top layer at the pin", skSkyLayerDepth(SK_SKY_PIN_LAYERS, true), SK_SKY_PIN_DEPTH_INFINITE);
        // Real distances: decode back within float precision at the main view's near plane.
        const F32 moon = SK_SKY_MOON_DEPTH_INFINITE;
        const F32 sun = SK_SKY_SUN_DEPTH_INFINITE;
        ensure("moon decodes to 384,400 km at 0.1 m", fabs(0.1 / (F64)moon - 384400000.0) < 384400000.0 * 1e-6);
        ensure("sun decodes to 1 AU at 0.1 m", fabs(0.1 / (F64)sun - 149597870700.0) < 149597870700.0 * 1e-6);
        // Front to back, upstream's order: pin and layers, dome, moon, sun, stars (0).
        ensure("moon behind the dome", moon < skSkyLayerDepth(0, true));
        ensure("sun behind the moon", sun < moon);
        ensure("stars behind the sun", sun > 0.f);
        // Every sky element classifies as sky, with margin.
        const SKSkyDepth sky = skSkyDepth(MIN_NEAR_PLANE, SK_PROJECTION_INFINITE);
        ensure("pin and bodies are sky", SK_SKY_PIN_DEPTH_INFINITE * 5.f < sky.mThreshold && moon < sky.mThreshold);
        // The shaders keep upstream's literals as their finite fallback.
        ensure("sun upstream fallback", shader_source("class1/deferred/sunDiscV.glsl").find(": 0.0000005)") != std::string::npos);
        ensure("moon upstream fallback", shader_source("class1/deferred/moonV.glsl").find(": 0.0000045)") != std::string::npos);
    }

    // The draw distance consumers see: forward-Z keeps 512 m, reverse-Z reaches MAX_FAR_CLIP.
    template<> template<>
    void skfarplane_object::test<18>()
    {
        ensure_equals("forward caps", skClampDrawDistance(2048.f, false), 512.f);
        ensure_equals("forward keeps lower", skClampDrawDistance(256.f, false), 256.f);
        ensure_equals("reverse keeps 2048", skClampDrawDistance(2048.f, true), 2048.f);
        ensure_equals("reverse caps past MAX_FAR_CLIP", skClampDrawDistance(4096.f, true), MAX_FAR_CLIP);
    }

    // The projection overlay: nearer-first pairs out to 100 km on distinct bearings, each farther column
    // behind its partner and overlapping it on screen, and every column the same angular size.
    template<> template<>
    void skfarplane_object::test<14>()
    {
        const std::vector<SKFarOverlayColumn>& columns = skFarOverlayColumns();
        ensure("pairs", columns.size() >= 2 && columns.size() % 2 == 0);
        ensure_equals("reaches 100 km", columns[columns.size() - 2].mDistance, 100000.f);
        for (size_t i = 0; i < columns.size(); i += 2)
        {
            const SKFarOverlayColumn& n = columns[i];
            const SKFarOverlayColumn& f = columns[i + 1];
            const std::string at = std::to_string((S32)n.mDistance) + " m";
            ensure("nearer first at " + at, n.mNearer && !f.mNearer);
            ensure("farther behind at " + at, f.mDistance > n.mDistance);
            ensure("same bearing at " + at, f.mBearing == n.mBearing);
            // On screen the farther column's near edge sits inside the nearer column.
            const F32 f_left = (f.mOffset - f.mHalfWidth) / f.mDistance;
            const F32 n_right = (n.mOffset + n.mHalfWidth) / n.mDistance;
            const F32 n_left = (n.mOffset - n.mHalfWidth) / n.mDistance;
            ensure("overlap at " + at, f_left > n_left && f_left < n_right);
            ensure("same angle at " + at, fabsf(n.mHeight / n.mDistance - columns[0].mHeight / columns[0].mDistance) < 1e-6f);
            if (i > 0)
            {
                ensure("own bearing at " + at, n.mBearing != columns[i - 2].mBearing);
                ensure("receding at " + at, n.mDistance > columns[i - 2].mDistance);
            }
        }
    }

    template<> template<>
    void skfarplane_object::test<15>()
    {
        set_test_name("water pass visibility by camera height");
        const F32 inf = std::numeric_limits<F32>::infinity();
        ensure("finite: below 1024 m", skWaterVisibleFrom(1000.f, 20.f, 1024.f));
        ensure("finite: from 1024 m it stops, as upstream", !skWaterVisibleFrom(1024.f, 20.f, 1024.f));
        ensure("infinite: 2 km up still shows", skWaterVisibleFrom(2000.f, 20.f, inf));
        ensure("infinite: within reach above the water", skWaterVisibleFrom(20.f + SK_EDGE_WATER_STRETCH - 1.f, 20.f, inf));
        ensure("infinite: past reach above the water", !skWaterVisibleFrom(20.f + SK_EDGE_WATER_STRETCH, 20.f, inf));
    }
}
