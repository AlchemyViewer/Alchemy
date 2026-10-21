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

#include <catch2/catch_test_macros.hpp>

#include "altestmatchers.h"

#include "../alfarplane.h"
#include "../alwaterfield.h"
#include "../alwaterheightbake.h"

#include "llcamera.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace
{
    // A shader's source, read from the viewer's shader tree so the test reads what the viewer compiles.
    static std::string shader_source(const std::string& rel)
    {
        const std::filesystem::path path = std::filesystem::path(AL_TEST_SHADER_DIR) / rel;
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

    // The draw distance consumers see: forward-Z keeps 512 m, reverse-Z reaches MAX_FAR_CLIP.
    TEST_CASE("alfarplane: draw distance caps at 512 m forward-Z and MAX_FAR_CLIP reversed", "[alfarplane]")
    {
        UNSCOPED_INFO("forward ceiling");
        CHECK(ALFarPlane::drawDistanceCeiling(false) == 512.f);
        UNSCOPED_INFO("reverse ceiling");
        CHECK(ALFarPlane::drawDistanceCeiling(true) == 2048.f);
        UNSCOPED_INFO("forward caps");
        CHECK(ALFarPlane::clampDrawDistance(2048.f, false) == 512.f);
        UNSCOPED_INFO("forward keeps lower");
        CHECK(ALFarPlane::clampDrawDistance(256.f, false) == 256.f);
        UNSCOPED_INFO("reverse keeps 2048");
        CHECK(ALFarPlane::clampDrawDistance(2048.f, true) == 2048.f);
        UNSCOPED_INFO("reverse caps past MAX_FAR_CLIP");
        CHECK(ALFarPlane::clampDrawDistance(4096.f, true) == MAX_FAR_CLIP);
    }

    // Reverse-Z is infinite; forward-Z and probe captures keep the finite plane, and the debug force moves only the
    // main view under reverse-Z.
    TEST_CASE("alfarplane: only the reverse-Z main view gets an infinite or forced far plane", "[alfarplane]")
    {
        using namespace ALFarPlane;
        UNSCOPED_INFO("reverse infinite");
        CHECK(isInfinite(projectionFar(true, false)));
        UNSCOPED_INFO("forward");
        CHECK(projectionFar(false, false) == FINITE_PROJECTION_FAR);
        UNSCOPED_INFO("reverse cube snapshot");
        CHECK(projectionFar(true, true) == FINITE_PROJECTION_FAR);
        UNSCOPED_INFO("forward cube snapshot");
        CHECK(projectionFar(false, true) == FINITE_PROJECTION_FAR);
        CHECK(FINITE_PROJECTION_FAR == 1024.f);
        CHECK_FALSE(isInfinite(1024.f));
        CHECK(isInfinite(INFINITE_FAR));
        CHECK(INFINITE_FAR > 0.f);

        CHECK(isInfinite(projectionFar(true, false, FORCE_AUTOMATIC, 2048.f)));
        CHECK(projectionFar(true, false, FORCE_FINITE, 2048.f) == 4096.f);
        UNSCOPED_INFO("finite never under the finite plane");
        CHECK(projectionFar(true, false, FORCE_FINITE, 256.f) == FINITE_PROJECTION_FAR);
        CHECK(isInfinite(projectionFar(true, false, FORCE_INFINITE, 2048.f)));
        UNSCOPED_INFO("unknown mode");
        CHECK(isInfinite(projectionFar(true, false, 7, 2048.f)));
        UNSCOPED_INFO("cube snapshot never forced infinite");
        CHECK(projectionFar(true, true, FORCE_INFINITE, 2048.f) == FINITE_PROJECTION_FAR);
        UNSCOPED_INFO("cube snapshot never forced finite");
        CHECK(projectionFar(true, true, FORCE_FINITE, 2048.f) == FINITE_PROJECTION_FAR);
        UNSCOPED_INFO("forward ignores infinite");
        CHECK(projectionFar(false, false, FORCE_INFINITE, 2048.f) == FINITE_PROJECTION_FAR);
        UNSCOPED_INFO("forward ignores finite");
        CHECK(projectionFar(false, false, FORCE_FINITE, 2048.f) == FINITE_PROJECTION_FAR);
    }

    // How far terrain and water reach, and from how high water is drawn.
    TEST_CASE("alfarplane: how far terrain and water reach and from how high water shows", "[alfarplane]")
    {
        using namespace ALFarPlane;
        CHECK(terrainReach(INFINITE_FAR) == 8192.f);
        UNSCOPED_INFO("terrain unbounded on forward-Z");
        CHECK(terrainReach(projectionFar(false, false)) == 0.f);
        UNSCOPED_INFO("terrain unbounded in cube snapshots");
        CHECK(terrainReach(projectionFar(true, true)) == 0.f);
        UNSCOPED_INFO("edge water, infinite");
        CHECK(edgeWaterStretch(true) == 256000.f);
        UNSCOPED_INFO("edge water, finite keeps 2048");
        CHECK(edgeWaterStretch(false) == 2048.f);
        CHECK(edgeWaterStretch(true) >= TERRAIN_REACH);

        UNSCOPED_INFO("finite: below 1024 m");
        CHECK(waterVisibleFrom(1000.f, 20.f, FINITE_PROJECTION_FAR));
        UNSCOPED_INFO("finite: from 1024 m it stops");
        CHECK_FALSE(waterVisibleFrom(1024.f, 20.f, FINITE_PROJECTION_FAR));
        UNSCOPED_INFO("finite: a farther plane shows it from higher");
        CHECK(waterVisibleFrom(2000.f, 20.f, 4096.f));
        UNSCOPED_INFO("finite: up to that plane");
        CHECK_FALSE(waterVisibleFrom(4096.f, 20.f, 4096.f));
        UNSCOPED_INFO("infinite: 2 km up still shows");
        CHECK(waterVisibleFrom(2000.f, 20.f, INFINITE_FAR));
        UNSCOPED_INFO("infinite: up to the ground's band above the water");
        CHECK(waterVisibleFrom(20.f + GROUND_FADE_END - 1.f, 20.f, INFINITE_FAR));
        UNSCOPED_INFO("infinite: from the band's top above the water it stops");
        CHECK_FALSE(waterVisibleFrom(20.f + GROUND_FADE_END, 20.f, INFINITE_FAR));
        UNSCOPED_INFO("infinite: a skybox's height");
        CHECK_FALSE(waterVisibleFrom(4000.f, 20.f, INFINITE_FAR));
        UNSCOPED_INFO("infinite: measured from the water, not from zero");
        CHECK(waterVisibleFrom(2500.f, 1000.f, INFINITE_FAR));
        UNSCOPED_INFO("finite: a farther plane stops at the band's top too");
        CHECK_FALSE(waterVisibleFrom(20.f + GROUND_FADE_END, 20.f, 4096.f));
    }

    // The band over which the world below leaves the view as the camera climbs: the water fades into the sky across
    // it, and from its top neither water nor terrain is drawn.
    TEST_CASE("alfarplane: the water fades into the sky over the ground's band and the ground goes at its top", "[alfarplane]")
    {
        using namespace ALFarPlane;
        UNSCOPED_INFO("the band starts at LL's finite far plane");
        CHECK(GROUND_FADE_START == FINITE_PROJECTION_FAR);
        CHECK(GROUND_FADE_START < GROUND_FADE_END);

        UNSCOPED_INFO("no fade on the water or below it");
        CHECK(groundFade(0.f) == 0.f);
        CHECK(groundFade(-500.f) == 0.f);
        UNSCOPED_INFO("no fade up to the band");
        CHECK(groundFade(GROUND_FADE_START) == 0.f);
        UNSCOPED_INFO("halfway at the band's middle");
        CHECK(groundFade(0.5f * (GROUND_FADE_START + GROUND_FADE_END)) == 0.5f);
        UNSCOPED_INFO("whole at the band's top");
        CHECK(groundFade(GROUND_FADE_END) == 1.f);
        CHECK(groundFade(4096.f) == 1.f);

        // Eased: no step at either end, and it only climbs.
        F32 last = 0.f;
        for (F32 h = GROUND_FADE_START; h <= GROUND_FADE_END; h += 16.f)
        {
            const F32 fade = groundFade(h);
            INFO("height " << h);
            CHECK(fade >= last);
            CHECK(fade - last < 0.025f);
            last = fade;
        }
        UNSCOPED_INFO("a metre into the band barely fades");
        CHECK(groundFade(GROUND_FADE_START + 1.f) < 1e-5f);
        UNSCOPED_INFO("a metre short of its top has all but faded");
        CHECK(groundFade(GROUND_FADE_END - 1.f) > 1.f - 1e-5f);

        UNSCOPED_INFO("drawn below the band's top");
        CHECK(groundDrawnFrom(-10.f));
        CHECK(groundDrawnFrom(GROUND_FADE_START));
        CHECK(groundDrawnFrom(GROUND_FADE_END - 1.f));
        UNSCOPED_INFO("not from its top up");
        CHECK_FALSE(groundDrawnFrom(GROUND_FADE_END));
        CHECK_FALSE(groundDrawnFrom(4000.f));
    }

    // Depth reconstruction: the far corners an infinite projection unprojects, and the floors the shaders carry.
    TEST_CASE("alfarplane: the far window depth and the reconstruction floor in the shaders", "[alfarplane]")
    {
        using namespace ALFarPlane;
        UNSCOPED_INFO("infinite");
        CHECK(frustumFarWindowDepth(true, 0.1f, 0.f) == 0.1f / MAX_RECONSTRUCT_DISTANCE);
        UNSCOPED_INFO("infinite depth unprojects to MAX_RECONSTRUCT_DISTANCE");
        CHECK(fabsf(0.1f / frustumFarWindowDepth(true, 0.1f, 0.f) - MAX_RECONSTRUCT_DISTANCE) < 1.f);
        UNSCOPED_INFO("finite, reverse");
        CHECK(frustumFarWindowDepth(false, 0.1f, 0.f) == 0.f);
        UNSCOPED_INFO("finite, forward");
        CHECK(frustumFarWindowDepth(false, 0.1f, 1.f) == 1.f);

        // The sky keeps the cleared depth, 0 under reverse-Z. Anything drawn stores near / distance, which must stay
        // a normal float out to the reconstruction floor so no geometry rounds onto the sky.
        CHECK(MAX_RECONSTRUCT_DISTANCE > TERRAIN_REACH);
        UNSCOPED_INFO("geometry at the floor stores a normal depth");
        CHECK(MIN_NEAR_PLANE / MAX_RECONSTRUCT_DISTANCE >= std::numeric_limits<F32>::min());

        // The floors the shaders actually carry, read from their sources.
        const F32 floor = 1.f / MAX_RECONSTRUCT_DISTANCE;
        const std::string util = shader_source("class1/deferred/deferredUtil.glsl");
        UNSCOPED_INFO("deferredUtil read");
        REQUIRE_FALSE(util.empty());
        UNSCOPED_INFO("deferredUtil floor");
        CHECK(literal_after(util, "#define RECONSTRUCT_W_FLOOR ") == floor);
        CHECK(literal_after(shader_source("class1/deferred/aoUtil.glsl"), "pos.w = max(pos.w, ") == floor);
        CHECK(literal_after(shader_source("class1/deferred/dofCoFUtilF.glsl"), "*max(p.w, ") == floor);
    }

    // The sky is the cleared far depth and nothing else: every pass that tells sky from geometry tests for exactly
    // that value, so no distance, projection or pin enters the test.
    TEST_CASE("alfarplane: every pass tells sky from geometry by the cleared depth alone", "[alfarplane]")
    {
        const std::string haze = shader_source("class3/deferred/hazeF.glsl");
        const std::string haze_v = shader_source("class1/deferred/hazeV.glsl");
        UNSCOPED_INFO("haze drawn on the far plane, reversed");
        REQUIRE(haze_v.find("gl_Position = vec4(position.xy, 0.0, 1.0);") != std::string::npos);
        UNSCOPED_INFO("haze drawn on the far plane, forward");
        REQUIRE(haze_v.find("gl_Position = vec4(position.xy, 1.0, 1.0);") != std::string::npos);
        // The lighting passes draw there too: the light map's passes, the lights and the soften pass's lit program in
        // front of it, the soften pass's sky program on it.
        for (const char* path : { "class2/deferred/softenLightV.glsl", "class3/deferred/multiPointLightV.glsl",
                                  "class2/deferred/sunLightV.glsl", "class1/deferred/blurLightV.glsl" })
        {
            const std::string v = shader_source(path);
            UNSCOPED_INFO(path << " drawn on the far plane, reversed");
            REQUIRE(v.find("gl_Position = vec4(position.xy, 0.0, 1.0);") != std::string::npos);
            UNSCOPED_INFO(path << " drawn on the far plane, forward");
            REQUIRE(v.find("gl_Position = vec4(position.xy, 1.0, 1.0);") != std::string::npos);
        }
        const std::string soften = shader_source("class3/deferred/softenLightF.glsl");
        UNSCOPED_INFO("the sky's soften program lights nothing");
        REQUIRE(soften.find("writeColor(skyColor(getGBuffer(tc)));") != std::string::npos);
        UNSCOPED_INFO("haze takes geometry where it is");
        REQUIRE(haze.find("= getPositionWithDepth(tc, depth);") != std::string::npos);
        UNSCOPED_INFO("void water fog keys on the far depth");
        REQUIRE(shader_source("class3/deferred/waterHazeF.glsl").find("if (isFarDepth(depth))") != std::string::npos);
        const std::string flare = shader_source("class1/alchemy/lensFlareStateF.glsl");
        UNSCOPED_INFO("flare sky is the far plane, reversed");
        REQUIRE(flare.find("return d <= 0.0 ? 1.0 : 0.0;") != std::string::npos);
        UNSCOPED_INFO("flare sky is the far plane, forward");
        REQUIRE(flare.find("return d >= 1.0 ? 1.0 : 0.0;") != std::string::npos);
        // Opaque geometry in the haze pass takes the in-scatter blended geometry takes in its own shading, and the
        // water surface alone converges on the sky behind it.
        UNSCOPED_INFO("the haze pass's in-scatter is alpha's");
        REQUIRE(haze.find("atmosFragLighting(vec3(0), additive, atten)") != std::string::npos);
        UNSCOPED_INFO("water converges on the sky behind");
        REQUIRE(haze.find("#ifdef HAZE_ON_WATER") != std::string::npos);
        UNSCOPED_INFO("water converges on the sky behind");
        REQUIRE(haze.find("additive = mix(additive, sky * (vec3(1.0) - atten), vec3(1.0) - atten);") != std::string::npos);
        UNSCOPED_INFO("the sky behind is sampled for water");
        REQUIRE(haze.find("texture(skyBehindMap, tc)") != std::string::npos);
        // The farthest geometry: a top corner of the edge water's extent, from as high above it as the water shows.
        const F64 edge = (F64)ALFarPlane::edgeWaterStretch(true) + MAX_FAR_CLIP;
        const F64 farthest = sqrt(2.0 * edge * edge + (F64)ALFarPlane::EDGE_WATER_STRETCH * ALFarPlane::EDGE_WATER_STRETCH);
        CHECK(farthest < MAX_RECONSTRUCT_DISTANCE);
        UNSCOPED_INFO("farthest water stores a normal depth");
        CHECK((F32)(MIN_NEAR_PLANE / farthest) >= std::numeric_limits<F32>::min());
    }

    // Water's far terms: the old ones under a finite projection; under an infinite one the waves follow the surface
    // to the rim's farthest corner and the water fades over the edge water stretch.
    TEST_CASE("alfarplane: water's wave clamp and edge fade under each projection", "[alfarplane]")
    {
        using namespace ALFarPlane;
        const LLVector2 lo(-1000.f, -2000.f);
        const LLVector2 hi(3000.f, 500.f);
        const WaterFar finite = waterFar(FINITE_PROJECTION_FAR, true, lo, hi);
        CHECK(finite.mWaveClamp == 2560.f);
        CHECK(finite.mEdgeFade == 0.f);
        const WaterFar infinite = waterFar(INFINITE_FAR, true, lo, hi);
        UNSCOPED_INFO("wave clamp at the farthest rim corner");
        CHECK(fabsf(infinite.mWaveClamp - sqrtf(3000.f * 3000.f + 2000.f * 2000.f)) < 0.01f);
        CHECK(infinite.mEdgeFade == EDGE_WATER_STRETCH);
        const WaterFar rimless = waterFar(INFINITE_FAR, false, LLVector2(), LLVector2());
        CHECK(rimless.mEdgeFade == 0.f);
        UNSCOPED_INFO("no rim, waves past the regions");
        CHECK(rimless.mWaveClamp >= EDGE_WATER_STRETCH);

        const std::string haze = shader_source("class3/deferred/waterHazeF.glsl");
        UNSCOPED_INFO("sky behind water is held past the surface");
        REQUIRE(haze.find("entry + VOID_WATER_FOG_DEPTH") != std::string::npos);
        const std::string surface = shader_source("class1/environment/waterSurface.glsl");
        UNSCOPED_INFO("wave clamp from the uniform");
        REQUIRE(surface.find("min(d, waterFar.x)") != std::string::npos);
        UNSCOPED_INFO("no literal wave clamp");
        REQUIRE(surface.find("2560.0") == std::string::npos);
        // The waves follow the phases the CPU takes, the camera and the scroll folded in, which sets no wave direction
        // or time; both water programs work them per fragment through the one function.
        for (const char* phase : { "+ waterBigPhase;", "+ waterLittlePhase.xy;", "+ waterLittlePhase.zw;" })
        {
            UNSCOPED_INFO("waves take " << phase);
            REQUIRE(surface.find(phase) != std::string::npos);
        }
        for (const char* path : { "class3/environment/waterF.glsl", "class3/environment/underWaterF.glsl" })
        {
            UNSCOPED_INFO(path << " works the waves per fragment");
            REQUIRE(shader_source(path).find("water_wave_normals(bumpMap, bumpMap2, view.xy, wave1, wave2, wave3);") != std::string::npos);
        }
        UNSCOPED_INFO("the wave reads work the coordinates");
        REQUIRE(surface.find("water_wave_coords(r, big_uv, little_uv);") != std::string::npos);
        // The CPU places the camera on the tiled layers' lattices; the shader adds the rest of the way to a point, on
        // the same lattice.
        UNSCOPED_INFO("the lattice's density");
        CHECK((F64)literal_after(surface, "#define WATER_HEX_GRID ") == ALWaterField::HEX_GRID);
        const std::string lattice = shader_source("class1/deferred/hexLatticeUtil.glsl");
        UNSCOPED_INFO("the lattice's skew");
        CHECK_THAT(literal_after(lattice, "mat2(1.0, 0.0, "), ll_test::WithinBits((F32)ALWaterField::HEX_SKEW_XY, 16));
        UNSCOPED_INFO("and its stretch");
        CHECK_THAT(literal_after(lattice, "mat2(1.0, 0.0, -0.57735027, "), ll_test::WithinBits((F32)ALWaterField::HEX_SKEW_YY, 16));
        // The waves' patches a cell side, which the draw counts, and the heights' metres, which the bake's bound uses.
        UNSCOPED_INFO("the patches a cell side");
        CHECK((S32)literal_after(surface, "#define WATER_PATCHES ") == ALWaterField::PATCHES_PER_CELL);
        UNSCOPED_INFO("the big layer's metres");
        CHECK(literal_after(surface, "#define WATER_BIG_METRES ") == ALWaterHeightBake::BIG_METRES);
        UNSCOPED_INFO("the mid layer's metres");
        CHECK(literal_after(surface, "#define WATER_MID_METRES ") == ALWaterHeightBake::MID_METRES);
    }

    // The far plane overlay: nearer-first pairs out to 100 km on distinct bearings, each farther column behind its
    // partner and overlapping it on screen, and every column the same angular size.
    TEST_CASE("alfarplane: overlay columns pair up out to 100 km at one angular size", "[alfarplane]")
    {
        const std::vector<ALFarPlane::OverlayColumn>& columns = ALFarPlane::overlayColumns();
        REQUIRE(columns.size() >= 2);
        REQUIRE(columns.size() % 2 == 0);
        REQUIRE(columns[columns.size() - 2].mDistance == 100000.f);
        for (size_t i = 0; i < columns.size(); i += 2)
        {
            const ALFarPlane::OverlayColumn& n = columns[i];
            const ALFarPlane::OverlayColumn& f = columns[i + 1];
            CAPTURE(n.mDistance);
            REQUIRE(n.mNearer);
            REQUIRE_FALSE(f.mNearer);
            REQUIRE(f.mDistance > n.mDistance);
            REQUIRE(f.mBearing == n.mBearing);
            // On screen the farther column's near edge sits inside the nearer column.
            const F32 f_left = (f.mOffset - f.mHalfWidth) / f.mDistance;
            const F32 n_right = (n.mOffset + n.mHalfWidth) / n.mDistance;
            const F32 n_left = (n.mOffset - n.mHalfWidth) / n.mDistance;
            {
                INFO("the farther column overlaps the nearer on screen");
                REQUIRE(f_left > n_left);
                REQUIRE(f_left < n_right);
            }
            UNSCOPED_INFO("every column the same angular size");
            REQUIRE(fabsf(n.mHeight / n.mDistance - columns[0].mHeight / columns[0].mDistance) < 1e-6f);
            if (i > 0)
            {
                REQUIRE(n.mBearing != columns[i - 2].mBearing);
                REQUIRE(n.mDistance > columns[i - 2].mDistance);
            }
        }
    }

    // The eye-to-sky rotation: a level camera's forward, up and right land on the sky's (north, up, east) for its heading,
    // and any view's eye directions come back as toLightNorm's swizzle of the agent directions it rotated.
    TEST_CASE("alfarplane: eyeToSkyFrame turns eye directions into the sky's frame", "[alfarplane]")
    {
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
        UNSCOPED_INFO("facing north, forward is north");
        CHECK(close_to(apply(n, LLVector3(0.f, 0.f, -1.f)), LLVector3(1.f, 0.f, 0.f)));
        UNSCOPED_INFO("facing north, up is up");
        CHECK(close_to(apply(n, LLVector3(0.f, 1.f, 0.f)), LLVector3(0.f, 1.f, 0.f)));
        UNSCOPED_INFO("facing north, right is east");
        CHECK(close_to(apply(n, LLVector3(1.f, 0.f, 0.f)), LLVector3(0.f, 0.f, 1.f)));

        // Facing east: eye right is south, eye -z is east.
        LLMatrix4a east;
        east.setIdentity();
        east.setRows(LLVector4a(0.f, 0.f, -1.f), LLVector4a(-1.f, 0.f, 0.f), LLVector4a(0.f, 1.f, 0.f));
        const std::array<F32, 9> e = ALFarPlane::eyeToSkyFrame(east);
        UNSCOPED_INFO("facing east, forward is east");
        CHECK(close_to(apply(e, LLVector3(0.f, 0.f, -1.f)), LLVector3(0.f, 0.f, 1.f)));
        UNSCOPED_INFO("facing east, right is south");
        CHECK(close_to(apply(e, LLVector3(1.f, 0.f, 0.f)), LLVector3(-1.f, 0.f, 0.f)));

        // A pitched, rolled, yawed view: what the pipeline rotates into eye space (as it does the sun's direction)
        // comes back as the sky frame's swizzle of the agent direction.
        LLMatrix4a view = LLMatrix4a::lookDir(LLVector4a(10.f, -20.f, 30.f), LLVector4a(0.3f, 0.8f, -0.25f), LLVector4a(0.1f, 0.f, 1.f));
        const std::array<F32, 9> v = ALFarPlane::eyeToSkyFrame(view);
        for (const LLVector3& agent : { LLVector3(1.f, 0.f, 0.f), LLVector3(0.f, 1.f, 0.f), LLVector3(0.f, 0.f, 1.f), LLVector3(0.48f, -0.6f, 0.64f) })
        {
            LLVector4a eye;
            view.rotate(LLVector4a(agent.mV[0], agent.mV[1], agent.mV[2], 0.f), eye);
            const LLVector3 sky = apply(v, LLVector3(eye.getF32ptr()));
            UNSCOPED_INFO("agent direction back as (north, up, east)");
            REQUIRE(close_to(sky, LLVector3(agent.mV[1], agent.mV[2], agent.mV[0])));
        }

        // WindLight's haze takes the rotation from the Environment block, for every pass that hazes geometry, and its
        // callers hand it eye space.
        const std::string funcs = shader_source("class1/windlight/atmosphericsFuncs.glsl");
        UNSCOPED_INFO("haze position into the sky's frame");
        REQUIRE(funcs.find("vec3 rel_pos = eyeToSky * inPositionEye;") != std::string::npos);
        UNSCOPED_INFO("haze light into the sky's frame");
        REQUIRE(funcs.find("light_dir = eyeToSky * light_dir;") != std::string::npos);
        // Only rising rays leave the haze, as the dome's do; a falling ray keeps its whole length.
        UNSCOPED_INFO("altitude limit on rising rays alone");
        REQUIRE(funcs.find("float h = max(rel_pos.y, 0.0) / max(max_y, 1.0);") != std::string::npos);
        UNSCOPED_INFO("no limit on falling rays");
        REQUIRE(funcs.find("abs(rel_pos.y)") == std::string::npos);
        UNSCOPED_INFO("the rotation is in the Environment block");
        REQUIRE(shader_source("class1/deferred/environmentBlock.glsl").find("mat3  eyeToSky;") != std::string::npos);
        // Read, and calling the haze with the position it unprojected, so the turn's absence below is not the
        // absence of the file.
        const std::string haze = shader_source("class3/deferred/hazeF.glsl");
        UNSCOPED_INFO("the haze pass hands over eye space");
        REQUIRE(haze.find("calcAtmosphericVarsLinear(pos.xyz,") != std::string::npos);
        UNSCOPED_INFO("and does not turn it itself");
        REQUIRE(haze.find("eyeToSky") == std::string::npos);
    }
}
