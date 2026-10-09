/**
 * @file alfarplane_test.cpp
 * @brief The main view's distances: the draw distance, the projection's far plane, and how far
 *        terrain and water reach.
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

#include "../alfarplane.h"

#include "llcamera.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace tut
{
    struct alfarplane_data
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

    typedef test_group<alfarplane_data> alfarplane_group;
    typedef alfarplane_group::object    alfarplane_object;
    tut::alfarplane_group alfarplane_g("alfarplane");

    // The draw distance consumers see: forward-Z keeps 512 m, reverse-Z reaches MAX_FAR_CLIP.
    template<> template<>
    void alfarplane_object::test<1>()
    {
        set_test_name("draw distance ceiling");
        ensure_equals("forward ceiling", ALFarPlane::drawDistanceCeiling(false), 512.f);
        ensure_equals("reverse ceiling", ALFarPlane::drawDistanceCeiling(true), 2048.f);
        ensure_equals("forward caps", ALFarPlane::clampDrawDistance(2048.f, false), 512.f);
        ensure_equals("forward keeps lower", ALFarPlane::clampDrawDistance(256.f, false), 256.f);
        ensure_equals("reverse keeps 2048", ALFarPlane::clampDrawDistance(2048.f, true), 2048.f);
        ensure_equals("reverse caps past MAX_FAR_CLIP", ALFarPlane::clampDrawDistance(4096.f, true), MAX_FAR_CLIP);
    }

    // Reverse-Z is infinite; forward-Z and probe captures keep the finite plane, and the debug force moves only the
    // main view under reverse-Z.
    template<> template<>
    void alfarplane_object::test<2>()
    {
        set_test_name("projection far plane");
        using namespace ALFarPlane;
        ensure("reverse infinite", isInfinite(projectionFar(true, false)));
        ensure_equals("forward", projectionFar(false, false), FINITE_PROJECTION_FAR);
        ensure_equals("reverse cube snapshot", projectionFar(true, true), FINITE_PROJECTION_FAR);
        ensure_equals("forward cube snapshot", projectionFar(false, true), FINITE_PROJECTION_FAR);
        ensure_equals("finite plane is 1024 m", FINITE_PROJECTION_FAR, 1024.f);
        ensure("1024 is finite", !isInfinite(1024.f));
        ensure("infinite constant", isInfinite(INFINITE_FAR) && INFINITE_FAR > 0.f);

        ensure("auto", isInfinite(projectionFar(true, false, FORCE_AUTOMATIC, 2048.f)));
        ensure_equals("finite", projectionFar(true, false, FORCE_FINITE, 2048.f), 4096.f);
        ensure_equals("finite never under the finite plane", projectionFar(true, false, FORCE_FINITE, 256.f), FINITE_PROJECTION_FAR);
        ensure("infinite", isInfinite(projectionFar(true, false, FORCE_INFINITE, 2048.f)));
        ensure("unknown mode", isInfinite(projectionFar(true, false, 7, 2048.f)));
        ensure_equals("cube snapshot never forced infinite", projectionFar(true, true, FORCE_INFINITE, 2048.f), FINITE_PROJECTION_FAR);
        ensure_equals("cube snapshot never forced finite", projectionFar(true, true, FORCE_FINITE, 2048.f), FINITE_PROJECTION_FAR);
        ensure_equals("forward ignores infinite", projectionFar(false, false, FORCE_INFINITE, 2048.f), FINITE_PROJECTION_FAR);
        ensure_equals("forward ignores finite", projectionFar(false, false, FORCE_FINITE, 2048.f), FINITE_PROJECTION_FAR);
    }

    // How far terrain and water reach, and from how high water is drawn.
    template<> template<>
    void alfarplane_object::test<3>()
    {
        set_test_name("terrain and water reach");
        using namespace ALFarPlane;
        ensure_equals("terrain reach under the infinite projection", terrainReach(INFINITE_FAR), 8192.f);
        ensure_equals("terrain unbounded on forward-Z", terrainReach(projectionFar(false, false)), 0.f);
        ensure_equals("terrain unbounded in cube snapshots", terrainReach(projectionFar(true, true)), 0.f);
        ensure_equals("edge water, infinite", edgeWaterStretch(true), 256000.f);
        ensure_equals("edge water, finite keeps 2048", edgeWaterStretch(false), 2048.f);
        ensure("edge water covers the terrain reach", edgeWaterStretch(true) >= TERRAIN_REACH);

        ensure("finite: below 1024 m", waterVisibleFrom(1000.f, 20.f, FINITE_PROJECTION_FAR));
        ensure("finite: from 1024 m it stops", !waterVisibleFrom(1024.f, 20.f, FINITE_PROJECTION_FAR));
        ensure("infinite: 2 km up still shows", waterVisibleFrom(2000.f, 20.f, INFINITE_FAR));
        ensure("infinite: within reach above the water", waterVisibleFrom(20.f + EDGE_WATER_STRETCH - 1.f, 20.f, INFINITE_FAR));
        ensure("infinite: past reach above the water", !waterVisibleFrom(20.f + EDGE_WATER_STRETCH, 20.f, INFINITE_FAR));
    }

    // Depth reconstruction: the far corners an infinite projection unprojects, and the floors the shaders carry.
    template<> template<>
    void alfarplane_object::test<4>()
    {
        set_test_name("depth reconstruction");
        using namespace ALFarPlane;
        ensure_equals("infinite", frustumFarWindowDepth(true, 0.1f, 0.f), 0.1f / MAX_RECONSTRUCT_DISTANCE);
        ensure("infinite depth unprojects to MAX_RECONSTRUCT_DISTANCE", fabsf(0.1f / frustumFarWindowDepth(true, 0.1f, 0.f) - MAX_RECONSTRUCT_DISTANCE) < 1.f);
        ensure_equals("finite, reverse", frustumFarWindowDepth(false, 0.1f, 0.f), 0.f);
        ensure_equals("finite, forward", frustumFarWindowDepth(false, 0.1f, 1.f), 1.f);

        // The sky keeps the cleared depth, 0 under reverse-Z. Anything drawn stores near / distance, which must stay
        // a normal float out to the reconstruction floor so no geometry rounds onto the sky.
        ensure("reconstruction floor past the reach", MAX_RECONSTRUCT_DISTANCE > TERRAIN_REACH);
        ensure("geometry at the floor stores a normal depth", MIN_NEAR_PLANE / MAX_RECONSTRUCT_DISTANCE >= std::numeric_limits<F32>::min());

        // The floors the shaders actually carry, read from their sources.
        const F32 floor = 1.f / MAX_RECONSTRUCT_DISTANCE;
        const std::string util = shader_source("class1/deferred/deferredUtil.glsl");
        ensure("deferredUtil read", !util.empty());
        ensure_equals("deferredUtil floor", literal_after(util, "#define RECONSTRUCT_W_FLOOR "), floor);
        ensure_equals("aoUtil floor", literal_after(shader_source("class1/deferred/aoUtil.glsl"), "pos.w = max(pos.w, "), floor);
        ensure_equals("cofF floor", literal_after(shader_source("class1/deferred/cofF.glsl"), "p.z/max(p.w, "), floor);
    }

    // The sky is the cleared far depth and nothing else: every pass that tells sky from geometry tests for exactly
    // that value, so no distance, projection or pin enters the test.
    template<> template<>
    void alfarplane_object::test<5>()
    {
        set_test_name("sky is the cleared depth");
        const std::string haze = shader_source("class3/deferred/hazeF.glsl");
        ensure("haze leaves the sky to the sky", haze.find("if (isFarDepth(depth))") != std::string::npos);
        ensure("haze takes geometry where it is", haze.find("= getPositionWithDepth(tc, depth);") != std::string::npos);
        ensure("void water fog keys on the far depth", shader_source("class3/deferred/waterHazeF.glsl").find("if (isFarDepth(depth))") != std::string::npos);
        const std::string flare = shader_source("class1/alchemy/lensFlareStateF.glsl");
        ensure("flare sky is the far plane, reversed", flare.find("return d <= 0.0 ? 1.0 : 0.0;") != std::string::npos);
        ensure("flare sky is the far plane, forward", flare.find("return d >= 1.0 ? 1.0 : 0.0;") != std::string::npos);
        // Opaque geometry in the haze pass takes the in-scatter blended geometry takes in its own shading.
        ensure("the haze pass's in-scatter is alpha's", haze.find("atmosFragLighting(vec3(0), additive, atten)") != std::string::npos);
        // The farthest geometry: a top corner of the edge water's extent, from as high above it as the water shows.
        const F64 edge = (F64)ALFarPlane::edgeWaterStretch(true) + MAX_FAR_CLIP;
        const F64 farthest = sqrt(2.0 * edge * edge + (F64)ALFarPlane::EDGE_WATER_STRETCH * ALFarPlane::EDGE_WATER_STRETCH);
        ensure("farthest water inside the floor", farthest < MAX_RECONSTRUCT_DISTANCE);
        ensure("farthest water stores a normal depth", (F32)(MIN_NEAR_PLANE / farthest) >= std::numeric_limits<F32>::min());
    }

    // Water's far terms: the old ones under a finite projection; under an infinite one the waves follow the surface
    // to the rim's farthest corner and the water fades over the edge water stretch.
    template<> template<>
    void alfarplane_object::test<6>()
    {
        set_test_name("water far terms");
        using namespace ALFarPlane;
        const LLVector2 lo(-1000.f, -2000.f);
        const LLVector2 hi(3000.f, 500.f);
        const WaterFar finite = waterFar(FINITE_PROJECTION_FAR, true, lo, hi);
        ensure_equals("finite wave clamp", finite.mWaveClamp, 2560.f);
        ensure_equals("finite no edge fade", finite.mEdgeFade, 0.f);
        const WaterFar infinite = waterFar(INFINITE_FAR, true, lo, hi);
        ensure("wave clamp at the farthest rim corner", fabsf(infinite.mWaveClamp - sqrtf(3000.f * 3000.f + 2000.f * 2000.f)) < 0.01f);
        ensure_equals("edge fade over the stretch", infinite.mEdgeFade, EDGE_WATER_STRETCH);
        const WaterFar rimless = waterFar(INFINITE_FAR, false, LLVector2(), LLVector2());
        ensure_equals("no rim, no fade", rimless.mEdgeFade, 0.f);
        ensure("no rim, waves past the regions", rimless.mWaveClamp >= EDGE_WATER_STRETCH);

        const std::string water_f = shader_source("class3/environment/waterF.glsl");
        const std::string haze = shader_source("class3/deferred/waterHazeF.glsl");
        ensure("sky behind water is held past the surface", haze.find("entry + VOID_WATER_FOG_DEPTH") != std::string::npos);
        const std::string water_v = shader_source("class1/environment/waterV.glsl");
        ensure("wave clamp from the uniform", water_v.find("min(d, waterFar.x)") != std::string::npos);
        ensure("no literal wave clamp", water_v.find("min(d, 2560.0)") == std::string::npos);
        // The per-fragment rebuild scrolls by the offsets waterV takes from the CPU, which sets no wave direction or time.
        for (const char* scroll : { "+ bigWaveScroll;", "+ littleWaveScroll.xy;", "+ littleWaveScroll.zw;" })
        {
            ensure(std::string("waterV scrolls by ") + scroll, water_v.find(scroll) != std::string::npos);
            ensure(std::string("rebuilt waves scroll by ") + scroll, water_f.find(scroll) != std::string::npos);
        }
    }

    // The eye-to-sky rotation: a level camera's forward, up and right land on the sky's (north, up, east) for its heading,
    // and any view's eye directions come back as toLightNorm's swizzle of the agent directions it rotated.
    template<> template<>
    void alfarplane_object::test<8>()
    {
        set_test_name("eye to sky frame");
        const auto apply = [](const std::array<F32, 9>& m, const LLVector3& v)
        {
            return LLVector3(m[0] * v.mV[0] + m[3] * v.mV[1] + m[6] * v.mV[2],
                             m[1] * v.mV[0] + m[4] * v.mV[1] + m[7] * v.mV[2],
                             m[2] * v.mV[0] + m[5] * v.mV[1] + m[8] * v.mV[2]);
        };
        const auto close_to = [](const LLVector3& a, const LLVector3& b) { return (a - b).length() < 1e-5f; };

        // A level camera facing north: eye right is east, eye up is up, eye -z is north. LLMatrix4a stores the
        // rotation's columns.
        LLMatrix4a north;
        north.setIdentity();
        north.setRows(LLVector4a(1.f, 0.f, 0.f), LLVector4a(0.f, 0.f, -1.f), LLVector4a(0.f, 1.f, 0.f));
        const std::array<F32, 9> n = ALFarPlane::eyeToSkyFrame(north);
        ensure("facing north, forward is north", close_to(apply(n, LLVector3(0.f, 0.f, -1.f)), LLVector3(1.f, 0.f, 0.f)));
        ensure("facing north, up is up", close_to(apply(n, LLVector3(0.f, 1.f, 0.f)), LLVector3(0.f, 1.f, 0.f)));
        ensure("facing north, right is east", close_to(apply(n, LLVector3(1.f, 0.f, 0.f)), LLVector3(0.f, 0.f, 1.f)));

        // Facing east: eye right is south, eye -z is east.
        LLMatrix4a east;
        east.setIdentity();
        east.setRows(LLVector4a(0.f, 0.f, -1.f), LLVector4a(-1.f, 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f));
        const std::array<F32, 9> e = ALFarPlane::eyeToSkyFrame(east);
        ensure("facing east, forward is east", close_to(apply(e, LLVector3(0.f, 0.f, -1.f)), LLVector3(0.f, 0.f, 1.f)));
        ensure("facing east, right is south", close_to(apply(e, LLVector3(1.f, 0.f, 0.f)), LLVector3(-1.f, 0.f, 0.f)));

        // A pitched, rolled, yawed view: what the pipeline rotates into eye space (as it does the sun's direction)
        // comes back as the sky frame's swizzle of the agent direction.
        LLMatrix4a view = LLMatrix4a::lookDir(LLVector4a(10.f, -20.f, 30.f), LLVector4a(0.3f, 0.8f, -0.25f), LLVector4a(0.1f, 0.f, 1.f));
        const std::array<F32, 9> v = ALFarPlane::eyeToSkyFrame(view);
        for (const LLVector3& agent : { LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 1.f, 0.f), LLVector3(0.f, 0.f, 1.f), LLVector3(0.48f, -0.6f, 0.64f) })
        {
            LLVector4a eye;
            view.rotate(LLVector4a(agent.mV[0], agent.mV[1], agent.mV[2], 0.f), eye);
            const LLVector3 sky = apply(v, LLVector3(eye.getF32ptr()));
            ensure("agent direction back as (north, up, east)", close_to(sky, LLVector3(agent.mV[1], agent.mV[2], agent.mV[0])));
        }

        // WindLight's haze takes the rotation from the Environment block, for every pass that hazes geometry, and its
        // callers hand it eye space.
        const std::string funcs = shader_source("class1/windlight/atmosphericsFuncs.glsl");
        ensure("haze position into the sky's frame", funcs.find("vec3 rel_pos = eyeToSky * inPositionEye;") != std::string::npos);
        ensure("haze light into the sky's frame", funcs.find("light_dir = eyeToSky * light_dir;") != std::string::npos);
        ensure("the rotation is in the Environment block", shader_source("class1/deferred/environmentBlock.glsl").find("mat3  eyeToSky;") != std::string::npos);
        ensure("the haze pass hands over eye space", shader_source("class3/deferred/hazeF.glsl").find("eyeToSky") == std::string::npos);
    }

    // The far plane overlay: nearer-first pairs out to 100 km on distinct bearings, each farther column behind its
    // partner and overlapping it on screen, and every column the same angular size.
    template<> template<>
    void alfarplane_object::test<7>()
    {
        set_test_name("overlay columns");
        const std::vector<ALFarPlane::OverlayColumn>& columns = ALFarPlane::overlayColumns();
        ensure("pairs", columns.size() >= 2 && columns.size() % 2 == 0);
        ensure_equals("reaches 100 km", columns[columns.size() - 2].mDistance, 100000.f);
        for (size_t i = 0; i < columns.size(); i += 2)
        {
            const ALFarPlane::OverlayColumn& n = columns[i];
            const ALFarPlane::OverlayColumn& f = columns[i + 1];
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
}
