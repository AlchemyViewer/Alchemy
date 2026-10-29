/**
 * @file alskylayers_test.cpp
 * @brief What the sky's composite takes of its layers: the layers each kind of view draws, the sun's and moon's
 *        quads, where a ray meets one, the sun bloom's square, and the meteors' streaks on the screen.
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

#include <catch2/catch_test_macros.hpp>

#include "../alfarplane.h"
#include "../alskylayers.h"
#include "../alstarfield.h"

#include "llmath.h"
#include "llmatrix4a.h"
#include "llquaternion.h"
#include "llvector4a.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace
{
    // ALSky's HEAVENLY_BODY_DIST, and the sun's disk radius.
    constexpr F32 BODY_DISTANCE   = 1004.f;
    constexpr F32 SUN_DISK_RADIUS = 0.5f;

    LLQuaternion turnTo(const LLVector3& dir)
    {
        LLQuaternion rotation;
        rotation.shortestArc(LLVector3::x_axis, dir);
        return rotation;
    }

    LLVector3 dome(const LLVector3& agent)
    {
        return ALFarPlane::skyFrame(agent);
    }

    // The texture coordinates, unclipped, where @a dir, a direction from the camera in the dome's frame, meets the
    // plane of @a quad (ALSkyLayers::rayQuad's), as deferred/skyBodiesF.glsl's skyQuadCoord works them out; false, and
    // the coordinates unset, where it runs parallel to the plane or away from it.
    bool quadCoord(const std::array<LLVector3, 3>& quad, const LLVector3& dir, F32& u, F32& v)
    {
        const LLVector3 normal = quad[1] % quad[2];
        const F32 facing = dir * normal;
        // Parallel to within rounding, measured against the normal's own length, which the quad's axes set: a ray
        // along the plane leaves its dot product with the normal a few ulps of that length off zero, one way or the
        // other.
        if (fabsf(facing) <= 1e-5f * normal.length() * dir.length())
        {
            return false;
        }
        const F32 t = (quad[0] * normal) / facing;
        if (t <= 0.f)
        {
            return false;
        }
        const LLVector3 offset = dir * t - quad[0];
        u = (offset * quad[1]) * 0.5f + 0.5f;
        v = (offset * quad[2]) * 0.5f + 0.5f;
        return true;
    }
}

TEST_CASE("alskylayers: a body's quad is centred along it and square to it and grows toward the horizon", "[alskylayers]")
{
    // Below 81.9 degrees, where the zenith's own axes take over.
    for (const F32 elevation : { 0.f, 3.f, 30.f, 60.f, 80.f })
    {
        CAPTURE(elevation);
        const F32 el = elevation * DEG_TO_RAD;
        const LLVector3 dir(cosf(el) * 0.6f, cosf(el) * 0.8f, sinf(el));
        const ALSkyLayers::Corners c = ALSkyLayers::bodyCorners(turnTo(dir), BODY_DISTANCE, 1.f, SUN_DISK_RADIUS);

        const LLVector3 centre = (c[0] + c[3]) * 0.5f;
        CHECK(dist_vec(centre, (c[1] + c[2]) * 0.5f) < 1e-3f);
        CHECK(dist_vec(centre, dir * BODY_DISTANCE) < 1e-2f);

        const LLVector3 right = (c[2] - c[0]) * 0.5f;
        const LLVector3 up    = (c[0] - c[1]) * 0.5f;
        CHECK(fabsf(right * up) < 1e-2f);
        CHECK(fabsf(right * dir) < 1e-2f);
        CHECK(fabsf(up * dir) < 1e-2f);
        // Up is up the sky, right along the horizon.
        CHECK(up.mV[VZ] > 0.f);
        CHECK(fabsf(right.mV[VZ]) < 1e-3f);

        // A tenth of the distance times the disk radius, wider by 30% and taller by 20% at the horizon.
        const F32 toward = 1.f - dir.mV[VZ];
        CHECK(fabsf(right.length() - BODY_DISTANCE * 0.1f * SUN_DISK_RADIUS * (1.f + toward * 0.3f)) < 1e-2f);
        CHECK(fabsf(up.length() - BODY_DISTANCE * 0.1f * SUN_DISK_RADIUS * (1.f + toward * 0.2f)) < 1e-2f);
    }
}

TEST_CASE("alskylayers: a body near the zenith takes its own axes", "[alskylayers]")
{
    const LLVector3 dir(0.1f, 0.f, 1.f);
    const ALSkyLayers::Corners c = ALSkyLayers::bodyCorners(turnTo(dir / dir.length()), BODY_DISTANCE, 1.f, SUN_DISK_RADIUS);
    const LLVector3 right = (c[2] - c[0]) * 0.5f;
    const LLVector3 up    = (c[0] - c[1]) * 0.5f;
    CHECK(right.length() > 10.f);
    CHECK(up.length() > 10.f);
    CHECK(fabsf(right * up) < 1e-2f);
}

TEST_CASE("alskylayers: a ray meets a quad at the texture coordinates of the point it crosses", "[alskylayers]")
{
    const LLVector3 dir = LLVector3(0.3f, -0.5f, 0.6f) / LLVector3(0.3f, -0.5f, 0.6f).length();
    const ALSkyLayers::Corners c = ALSkyLayers::bodyCorners(turnTo(dir), BODY_DISTANCE, 2.f, SUN_DISK_RADIUS);

    for (const LLVector3& offset : { LLVector3::zero, LLVector3(0.f, 0.f, -ALSkyLayers::SUN_DROP) })
    {
        CAPTURE(offset);
        const std::array<LLVector3, 3> quad = ALSkyLayers::rayQuad(c, offset);

        // The corners take (0, 1), (0, 0), (1, 1) and (1, 0); the centre the middle.
        const F32 expected[5][2] = { { 0.f, 1.f }, { 0.f, 0.f }, { 1.f, 1.f }, { 1.f, 0.f }, { 0.5f, 0.5f } };
        for (S32 i = 0; i < 5; ++i)
        {
            CAPTURE(i);
            const LLVector3 point = (i < 4 ? c[i] : (c[0] + c[3]) * 0.5f) + offset;
            F32 u = -1.f;
            F32 v = -1.f;
            REQUIRE(quadCoord(quad, dome(point / point.length()), u, v));
            CHECK(fabsf(u - expected[i][0]) < 1e-4f);
            CHECK(fabsf(v - expected[i][1]) < 1e-4f);
        }

        // A ray away from the quad, or along its plane, misses it.
        F32 u = 0.f;
        F32 v = 0.f;
        CHECK_FALSE(quadCoord(quad, dome(-dir), u, v));
        const LLVector3 along = (c[2] - c[0]) / (c[2] - c[0]).length();
        CHECK_FALSE(quadCoord(quad, dome(along), u, v));
    }

    // The dome's frame is the agent's (east, north, up) turned to (north, up, east).
    CHECK(dome(LLVector3(1.f, 0.f, 0.f)) == LLVector3(0.f, 0.f, 1.f));
    CHECK(dome(LLVector3(0.f, 1.f, 0.f)) == LLVector3(1.f, 0.f, 0.f));
    CHECK(dome(LLVector3(0.f, 0.f, 1.f)) == LLVector3(0.f, 1.f, 0.f));
}

TEST_CASE("alskylayers: the bloom is a square four times the sun disc's half-width every way", "[alskylayers]")
{
    for (const F32 elevation : { 3.f, 45.f, 89.f })
    {
        CAPTURE(elevation);
        const F32 el = elevation * DEG_TO_RAD;
        const LLVector3 dir(cosf(el) * 0.8f, cosf(el) * -0.6f, sinf(el));
        const ALSkyLayers::Corners sun = ALSkyLayers::bodyCorners(turnTo(dir), BODY_DISTANCE, 1.5f, SUN_DISK_RADIUS);
        const ALSkyLayers::Corners bloom = ALSkyLayers::bloomCorners(sun);

        CHECK(dist_vec((bloom[0] + bloom[3]) * 0.5f, (sun[0] + sun[3]) * 0.5f) < 1e-2f);
        const LLVector3 sun_right   = (sun[2] - sun[0]) * 0.5f;
        const LLVector3 sun_up      = (sun[0] - sun[1]) * 0.5f;
        const LLVector3 bloom_right = (bloom[2] - bloom[0]) * 0.5f;
        const LLVector3 bloom_up    = (bloom[0] - bloom[1]) * 0.5f;
        CHECK(fabsf(bloom_right.length() - 4.f * sun_right.length()) < 1e-2f);
        CHECK(fabsf(bloom_up.length() - 4.f * sun_right.length()) < 1e-2f);
        CHECK(dist_vec(bloom_right / bloom_right.length(), sun_right / sun_right.length()) < 1e-4f);
        CHECK(dist_vec(bloom_up / bloom_up.length(), sun_up / sun_up.length()) < 1e-4f);
    }
}

TEST_CASE("alskylayers: the moon fades as the square of its height near the horizon", "[alskylayers]")
{
    CHECK(ALSkyLayers::moonFade(0.f) == 1.f);
    CHECK(ALSkyLayers::moonFade(-0.3f) == 1.f);
    CHECK(fabsf(ALSkyLayers::moonFade(0.25f) - 0.25f) < 1e-6f);
    CHECK(ALSkyLayers::moonFade(0.5f) == 1.f);
    CHECK(ALSkyLayers::moonFade(0.9f) == 1.f);
}

TEST_CASE("alskylayers: a meteor's streak is packed as its quad on the screen", "[alskylayers]")
{
    // A camera at the origin looking down -z, 90 degrees across on a 200 by 100 viewport.
    constexpr F32 WIDTH  = 200.f;
    constexpr F32 HEIGHT = 100.f;
    LLMatrix4a mvp;
    mvp.clear();
    F32* p = mvp.getF32ptr();
    p[0]  = 0.5f;
    p[5]  = 1.f;
    p[10] = -1.f;
    p[11] = -1.f;
    p[14] = -1.f;

    // Clip space of a point, and its normalized device coordinates.
    const auto ndc = [&](const LLVector3& v)
    {
        LLVector4a clip;
        mvp.transform4(LLVector4a(v.mV[VX], v.mV[VY], v.mV[VZ], 1.f), clip);
        return LLVector3(clip[VX] / clip[VW], clip[VY] / clip[VW], clip[VW]);
    };

    ALSkyLayers::Meteor ahead;
    ahead.mMidpoint = LLVector3(10.f, 5.f, -100.f);
    ahead.mHalf     = LLVector3(8.f, 2.f, 0.f);
    ahead.mColour   = LLColor4U(255, 128, 0, 64);
    ahead.mWidth    = 2.5f;

    ALSkyLayers::Meteor behind = ahead;
    behind.mMidpoint.mV[VZ] = 100.f;

    ALSkyLayers::Meteor aside = ahead;
    aside.mMidpoint = LLVector3(500.f, 0.f, -100.f);

    ALSkyLayers::Meteor end_on = ahead;
    end_on.mHalf = ahead.mMidpoint * 0.1f;

    // One end on whose ends lie apart by what rounding moves a point, a few millionths of a pixel on the screen.
    ALSkyLayers::Meteor rounded = end_on;
    rounded.mHalf.mV[VX] += 1e-5f;

    std::vector<LLVector4> out;
    const U32 count = ALSkyLayers::packMeteors({ behind, ahead, aside, end_on, rounded }, mvp, WIDTH, HEIGHT, 1.5f, out);
    REQUIRE(count == 1);
    REQUIRE(out.size() == ALSkyLayers::METEOR_VECTORS);

    // The head is (1, 0) in the streak's frame and the tail (-1, 0); a point the width across from the midpoint is
    // (0, width), the width 1.5 pixels to the unit.
    const LLVector3 mid  = ndc(ahead.mMidpoint);
    const LLVector3 head = ndc(ahead.mMidpoint + ahead.mHalf);
    const LLVector3 tail = ndc(ahead.mMidpoint - ahead.mHalf);
    CHECK(fabsf(out[0].mV[0] - mid.mV[0]) < 1e-6f);
    CHECK(fabsf(out[0].mV[1] - mid.mV[1]) < 1e-6f);
    const auto coord = [&](F32 x, F32 y)
    {
        const F32 dx = x - out[0].mV[0];
        const F32 dy = y - out[0].mV[1];
        return LLVector2(out[0].mV[2] * dx + out[0].mV[3] * dy, out[1].mV[0] * dx + out[1].mV[1] * dy);
    };
    CHECK(dist_vec(coord(head.mV[0], head.mV[1]), LLVector2(1.f, 0.f)) < 1e-4f);
    CHECK(dist_vec(coord(tail.mV[0], tail.mV[1]), LLVector2(-1.f, 0.f)) < 1e-3f);
    LLVector2 across(-(head.mV[1] - mid.mV[1]), head.mV[0] - mid.mV[0]);
    across.normVec();
    const LLVector2 side(mid.mV[0] + across.mV[0] * 1.5f * 2.f / WIDTH * ahead.mWidth,
                         mid.mV[1] + across.mV[1] * 1.5f * 2.f / HEIGHT * ahead.mWidth);
    CHECK(dist_vec(coord(side.mV[0], side.mV[1]), LLVector2(0.f, ahead.mWidth)) < 1e-3f);

    // The width it reaches, and the one its profile divides by, from the eight bits it was packed in.
    CHECK(out[1].mV[2] == ahead.mWidth);
    CHECK(fabsf(out[1].mV[3] - 64.f / 255.f * 4.f) < 1e-6f);

    // The colour, linear.
    CHECK(fabsf(out[2].mV[0] - 1.f) < 1e-6f);
    CHECK(fabsf(out[2].mV[1] - powf(128.f / 255.f, 2.2f)) < 1e-6f);
    CHECK(out[2].mV[2] == 0.f);
}

namespace
{
    // The integer defines of @a file in the viewer's shader tree whose names begin with @a prefix: each
    // `#define NAME value` line, read as the viewer compiles it.
    std::map<std::string, S64> shaderDefines(const std::string& file, const std::string& prefix)
    {
        std::ifstream in(std::filesystem::path(AL_TEST_SHADER_DIR) / file);
        CAPTURE(file);
        REQUIRE(in.good());
        std::map<std::string, S64> out;
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream words(line);
            std::string directive;
            std::string name;
            S64 value = 0;
            if (words >> directive >> name >> value && directive == "#define" && name.rfind(prefix, 0) == 0)
            {
                out[name] = value;
            }
        }
        return out;
    }
}

TEST_CASE("alskylayers: the composite's meteor array is as long as packMeteors packs", "[alskylayers]")
{
    const std::map<std::string, S64> defines = shaderDefines("class1/deferred/skyMeteorsF.glsl", "SKY_");
    const std::map<std::string, S64> expected = { { "SKY_MAX_METEORS", ALSkyLayers::MAX_METEORS },
                                                  { "SKY_METEOR_VECTORS", ALSkyLayers::METEOR_VECTORS } };
    CHECK(defines.size() == expected.size());
    for (const auto& [name, value] : expected)
    {
        CAPTURE(name);
        REQUIRE(defines.count(name) == 1);
        CHECK(defines.at(name) == value);
    }
}

TEST_CASE("alskylayers: no more meteors are packed than the composite holds", "[alskylayers]")
{
    LLMatrix4a mvp;
    mvp.setIdentity();
    ALSkyLayers::Meteor meteor;
    meteor.mMidpoint = LLVector3(0.f, 0.f, 0.5f);
    meteor.mHalf     = LLVector3(0.1f, 0.f, 0.f);
    meteor.mColour   = LLColor4U(255, 255, 255, 64);
    std::vector<LLVector4> out;
    const std::vector<ALSkyLayers::Meteor> meteors(ALSkyLayers::MAX_METEORS + 5, meteor);
    CHECK(ALSkyLayers::packMeteors(meteors, mvp, 100.f, 100.f, 1.5f, out) == ALSkyLayers::MAX_METEORS);
    CHECK(out.size() == ALSkyLayers::MAX_METEORS * ALSkyLayers::METEOR_VECTORS);
}

TEST_CASE("alskylayers: each kind of view draws the layers the raster sky drew in it", "[alskylayers]")
{
    using L = ALSkyLayers::Layer;

    // A night with everything up and drawn.
    ALSkyLayers::Frame night;
    night.mSun       = true;
    night.mBloom     = true;
    night.mMoon      = true;
    night.mStarAlpha = 0.5f;
    night.mMeteors   = true;
    night.mAurora    = 0.6f;
    night.mClouds    = true;

    // The main view draws every layer.
    CHECK(ALSkyLayers::layersFor(night) ==
          (L::STARS | L::BLOOM | L::SUN | L::MOON | L::METEORS | L::AURORA | L::CLOUDS));

    // A probe's radiance face: no stars, sun, bloom, meteors or aurora.
    ALSkyLayers::Frame probe = night;
    probe.mProbe = true;
    CHECK(ALSkyLayers::layersFor(probe) == (L::MOON | L::CLOUDS));

    // Its irradiance pass leaves the clouds out too.
    probe.mRadiance = false;
    CHECK(ALSkyLayers::layersFor(probe) == L::MOON);

    // A mirror keeps the sun and its bloom.
    ALSkyLayers::Frame mirror = night;
    mirror.mProbe  = true;
    mirror.mMirror = true;
    CHECK(ALSkyLayers::layersFor(mirror) == (L::BLOOM | L::SUN | L::MOON | L::CLOUDS));

    // An HDRI stands alone, in any view.
    ALSkyLayers::Frame hdri = night;
    hdri.mHdri = true;
    CHECK(ALSkyLayers::layersFor(hdri) == L::HDRI);
    probe.mHdri = true;
    CHECK(ALSkyLayers::layersFor(probe) == L::HDRI);

    // Daylight: below a thousandth of the stars' brightness the night's layers go.
    ALSkyLayers::Frame day = night;
    day.mStarAlpha = 0.0009f;
    CHECK(ALSkyLayers::layersFor(day) == (L::BLOOM | L::SUN | L::MOON | L::CLOUDS));

    // The aurora washed out by the moon, no meteors flying, the bloom off, nothing up.
    ALSkyLayers::Frame quiet = night;
    quiet.mAurora  = 0.001f;
    quiet.mMeteors = false;
    quiet.mBloom   = false;
    CHECK(ALSkyLayers::layersFor(quiet) == (L::STARS | L::SUN | L::MOON | L::CLOUDS));
    quiet.mSun    = false;
    quiet.mMoon   = false;
    quiet.mClouds = false;
    CHECK(ALSkyLayers::layersFor(quiet) == L::STARS);

    // The bloom goes with the sun.
    ALSkyLayers::Frame sunless = night;
    sunless.mSun = false;
    CHECK((ALSkyLayers::layersFor(sunless) & (L::SUN | L::BLOOM)) == 0);
}

TEST_CASE("alskylayers: a view draws the stars only where one can show and the night's layers with them", "[alskylayers]")
{
    // A view clears the stars' attachment, draws the stars into it and reads them back only where its layers have
    // them, so where they do some star must add what the display shows (ALStarField::faintestVisible), whatever the
    // moon and on either path; and the meteors and the aurora, which are the night's too, come only with them.
    using L = ALSkyLayers::Layer;
    const F32 ALPHAS[] = { 0.f, 0.5f * ALSkyLayers::NIGHT_STAR_ALPHA, 0.999f * ALSkyLayers::NIGHT_STAR_ALPHA,
                           ALSkyLayers::NIGHT_STAR_ALPHA, 1.001f * ALSkyLayers::NIGHT_STAR_ALPHA,
                           2.f * ALSkyLayers::NIGHT_STAR_ALPHA, 0.01f, 0.1f, 0.5f, 1.f, 2.f };
    U32 drawn = 0;
    for (S32 kind = 0; kind < 4; ++kind)
    {
        for (const F32 alpha : ALPHAS)
        {
            ALSkyLayers::Frame frame;
            frame.mProbe     = kind >= 2;
            frame.mMirror    = kind == 3;
            frame.mHdri      = kind == 1;
            frame.mSun       = true;
            frame.mMoon      = true;
            frame.mStarAlpha = alpha;
            frame.mMeteors   = true;
            frame.mAurora    = 0.6f;
            frame.mClouds    = true;
            const S32 layers = ALSkyLayers::layersFor(frame);
            CAPTURE(kind, alpha, layers);
            if ((layers & L::STARS) == 0)
            {
                CHECK((layers & (L::METEORS | L::AURORA)) == 0);
                continue;
            }
            ++drawn;
            for (const F32 moon_power : { 0.f, 0.5f, 1.f })
            {
                for (const bool emissive : { false, true })
                {
                    CAPTURE(moon_power, emissive);
                    CHECK(ALStarField::faintestVisible(alpha, moon_power, emissive) <= 255);
                }
            }
        }
    }
    // The main view draws the stars from the threshold up, and no other kind of view draws them.
    CHECK(drawn == 8);
}

TEST_CASE("alskylayers: the composite's layer bits are ALSkyLayers::Layer", "[alskylayers]")
{
    using L = ALSkyLayers::Layer;
    const std::map<std::string, S64> defines = shaderDefines("class1/deferred/skyCompositeF.glsl", "SKY_LAYER_");
    const std::map<std::string, S64> expected = {
        { "SKY_LAYER_STARS", L::STARS },     { "SKY_LAYER_BLOOM", L::BLOOM },   { "SKY_LAYER_SUN", L::SUN },
        { "SKY_LAYER_MOON", L::MOON },       { "SKY_LAYER_METEORS", L::METEORS }, { "SKY_LAYER_AURORA", L::AURORA },
        { "SKY_LAYER_CLOUDS", L::CLOUDS },   { "SKY_LAYER_HDRI", L::HDRI } };
    CHECK(defines.size() == expected.size());
    for (const auto& [name, value] : expected)
    {
        CAPTURE(name);
        REQUIRE(defines.count(name) == 1);
        CHECK(defines.at(name) == value);
    }
}
