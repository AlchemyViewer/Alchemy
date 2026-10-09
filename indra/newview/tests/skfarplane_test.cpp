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

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
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
        // The sky keeps the cleared depth, 0 under reverse-Z. Anything drawn stores near / distance, which must stay
        // a normal float out to the reconstruction floor so no geometry rounds onto the sky.
        ensure("reconstruction floor past the reach", SK_RECONSTRUCT_FAR > SK_REACH_TERRAIN);
        ensure("geometry at the floor stores a normal depth", MIN_NEAR_PLANE / SK_RECONSTRUCT_FAR >= std::numeric_limits<F32>::min());
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

    // The sky is the cleared far depth and nothing else: every pass that tells sky from geometry tests for exactly
    // that value, so no distance, projection or pin enters the test.
    template<> template<>
    void skfarplane_object::test<12>()
    {
        const std::string haze = shader_source("class3/deferred/hazeF.glsl");
        ensure("haze leaves the sky to the sky", haze.find("if (isFarDepth(depth))") != std::string::npos);
        ensure("haze takes geometry where it is", haze.find("= getPositionWithDepth(tc, depth);") != std::string::npos);
        ensure("void water fog keys on the far depth", shader_source("class3/deferred/waterHazeF.glsl").find("if (isFarDepth(depth))") != std::string::npos);
        const std::string flare = shader_source("class1/alchemy/lensFlareStateF.glsl");
        ensure("flare sky is the far plane, reversed", flare.find("return d <= 0.0 ? 1.0 : 0.0;") != std::string::npos);
        ensure("flare sky is the far plane, forward", flare.find("return d >= 1.0 ? 1.0 : 0.0;") != std::string::npos);
        // The farthest geometry: a top corner of the edge water's extent, from as high above it as the water shows.
        const F64 edge = (F64)skEdgeWaterStretch(true) + MAX_FAR_CLIP;
        const F64 farthest = sqrt(2.0 * edge * edge + (F64)SK_EDGE_WATER_STRETCH * SK_EDGE_WATER_STRETCH);
        ensure("farthest water inside the floor", farthest < SK_RECONSTRUCT_FAR);
        ensure("farthest water stores a normal depth", (F32)(MIN_NEAR_PLANE / farthest) >= std::numeric_limits<F32>::min());
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
        ensure("sky behind water is held past the surface", haze.find("entry + VOID_WATER_FOG_DEPTH") != std::string::npos);
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
