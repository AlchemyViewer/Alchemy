/**
 * @file alstarfield_test.cpp
 * @brief The star field: the same field from the same count in every session, laid out in its tiers, and the
 *        buffer it is drawn from, binned into cells a view culls.
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

#include "altestmatchers.h"

#include "../alstarfieldgenerate.h"
#include "../alstarfield.h"
#include "alstarfield_draws.h"

#include "llmatrix4a.h"
#include "llrand.h"
#include "llstrider.h"
#include "llvector4a.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    // LLSettingsSky's DOME_RADIUS.
    constexpr F32 DOME_RADIUS = 15000.f;

    // Each tier's distance and intensity range, in dome radii and intensity bytes.
    struct Tier
    {
        F32 mDistance;
        U8  mFloor;
        U8  mCeil;
    };
    constexpr Tier TIERS[] = { { 1.0f, 140, 255 }, { 1.6f, 46, 153 }, { 3.0f, 13, 56 } };

    bool same(const ALStarField::Star& a, const ALStarField::Star& b)
    {
        return a.mPosition == b.mPosition && a.mColour == b.mColour;
    }

    // A shader's source, read from the viewer's shader tree so the test reads what the viewer compiles.
    std::string shader_source(const std::string& rel)
    {
        const std::filesystem::path path = std::filesystem::path(AL_TEST_SHADER_DIR) / rel;
        std::ifstream in(path);
        std::stringstream buf;
        buf << in.rdbuf();
        return buf.str();
    }
}

TEST_CASE("alstarfield: a count lays out the same field every time", "[alstarfield]")
{
    const std::vector<ALStarField::Star> first = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);

    // The global generator moving on between the two, as the meteors and everything else move it, changes nothing.
    for (S32 i = 0; i < 1000; ++i)
    {
        (void)ll_frand();
    }
    const std::vector<ALStarField::Star> second = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);

    REQUIRE(first.size() == second.size());
    size_t differ = 0;
    for (size_t s = 0; s < first.size(); ++s)
    {
        differ += !same(first[s], second[s]);
    }
    CHECK(differ == 0);

    // Another count between, and back, also changes nothing.
    (void)ALStarField::generate(ALStarField::DEFAULT_COUNT / 3, DOME_RADIUS);
    const std::vector<ALStarField::Star> third = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    REQUIRE(third.size() == first.size());
    differ = 0;
    for (size_t s = 0; s < first.size(); ++s)
    {
        differ += !same(first[s], third[s]);
    }
    CHECK(differ == 0);
}

TEST_CASE("alstarfield: the default field is the one the fixed seed laid out before", "[alstarfield]")
{
    // The first stars of the default field, as LLVOWLSky::initStars laid them out from the fixed seed: a change of
    // seed, draw order or tier would move every image of the night sky the sky's A/B captures compare.
    struct Pinned
    {
        size_t    mIndex;
        LLVector3 mPosition;
        LLColor4U mColour;
    };
    const Pinned PINNED[] = {
        { 0, LLVector3(11800.555f, -7005.780f, 6055.242f), LLColor4U(255, 235, 218, 140) },
        { 4999, LLVector3(-8317.633f, 7518.630f, 9964.296f), LLColor4U(255, 204, 163, 246) },
        { 5000, LLVector3(6414.705f, 9700.752f, 20993.975f), LLColor4U(255, 223, 196, 46) },
        { 35000, LLVector3(-9913.384f, 43411.914f, 6490.825f), LLColor4U(255, 210, 174, 13) },
        { 154999, LLVector3(21522.605f, -29961.686f, 25769.652f), LLColor4U(255, 213, 179, 14) },
    };

    const std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    for (const Pinned& pinned : PINNED)
    {
        CAPTURE(pinned.mIndex);
        REQUIRE(pinned.mIndex < stars.size());
        const ALStarField::Star& star = stars[pinned.mIndex];
        for (S32 c = 0; c < 3; ++c)
        {
            CAPTURE(c, star.mPosition.mV[c], pinned.mPosition.mV[c]);
            CHECK(fabsf(star.mPosition.mV[c] - pinned.mPosition.mV[c]) < 0.05f);
        }
        for (S32 c = 0; c < 4; ++c)
        {
            CAPTURE(c, (S32)star.mColour.mV[c], (S32)pinned.mColour.mV[c]);
            CHECK(abs((S32)star.mColour.mV[c] - (S32)pinned.mColour.mV[c]) <= 1);
        }
    }
}

TEST_CASE("alstarfield: a band comparison that rounds the other way moves no other star", "[alstarfield]")
{
    // A platform whose expf rounds the band's density to the other side of a draw takes the other branch of that one
    // comparison. Each run here forces one comparison's outcome, in each tier: the density at that candidate 0, which
    // rejects it, or 1, which keeps it. One of the two takes the branch the field did not, and that star alone moves.
    // Every run, the field's among them, goes through the one call below, so that each rounds every other comparison
    // as the field does. Candidates of the bright tier, the mid tier and the dust: a tenth of the default count draws
    // about 450, 3800 and 24000 candidates for the band.
    constexpr U32 COUNT = ALStarField::DEFAULT_COUNT / 10;
    constexpr U32 NONE  = ~0u;
    const U32 FLIPS[] = { 100u, 2000u, 20000u };
    struct Run
    {
        U32 mFlip;
        F32 mForced;
    };
    std::vector<Run> runs = { { NONE, 0.f } };
    for (const U32 flip : FLIPS)
    {
        runs.push_back({ flip, 0.f });
        runs.push_back({ flip, 1.f });
    }
    std::vector<std::vector<ALStarField::Star>> fields;
    std::vector<U32> candidates;
    for (const Run& run : runs)
    {
        U32 calls = 0;
        fields.push_back(ALStarField::detail::generate(
            COUNT, DOME_RADIUS, [&calls, run](const LLVector3& direction)
            { return calls++ == run.mFlip ? run.mForced : ALStarField::milkyWayDensity(direction); }));
        candidates.push_back(calls);
    }
    const std::vector<ALStarField::Star>& field = fields[0];

    {
        // The band's own density is the field's, but where the two, compiled apart, round one of its comparisons the
        // other way, which moves that star alone.
        const std::vector<ALStarField::Star> generated = ALStarField::generate(COUNT, DOME_RADIUS);
        REQUIRE(generated.size() == field.size());
        size_t differ = 0;
        for (size_t s = 0; s < field.size(); ++s)
        {
            differ += !same(field[s], generated[s]);
        }
        CAPTURE(differ);
        CHECK(differ * 1000 <= field.size());
    }

    for (size_t f = 0; f < LL_ARRAY_SIZE(FLIPS); ++f)
    {
        size_t moved[2] = {};
        for (S32 forced = 0; forced < 2; ++forced)
        {
            const std::vector<ALStarField::Star>& other = fields[1 + 2 * f + forced];
            REQUIRE(other.size() == field.size());
            for (size_t s = 0; s < field.size(); ++s)
            {
                moved[forced] += !same(field[s], other[s]);
            }
        }
        CAPTURE(FLIPS[f], candidates[1 + 2 * f], moved[0], moved[1]);
        REQUIRE(candidates[1 + 2 * f] > FLIPS[f]);
        CHECK(moved[0] + moved[1] == 1);
    }
}

TEST_CASE("alstarfield: every tier scales with the count and the count is held to the most", "[alstarfield]")
{
    CHECK(ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS).size() == ALStarField::DEFAULT_COUNT);
    CHECK(ALStarField::generate(0, DOME_RADIUS).empty());
    // Each tier rounds its share, so the most may come to a star more or less than MAX_COUNT.
    const size_t most = ALStarField::generate(ALStarField::MAX_COUNT, DOME_RADIUS).size();
    CHECK(most + 1 >= ALStarField::MAX_COUNT);
    CHECK(most <= ALStarField::MAX_COUNT + 1);
    CHECK(ALStarField::generate(ALStarField::MAX_COUNT * 4, DOME_RADIUS).size() == most);

    // A tenth of the default: a tenth of each tier, each on its own sphere.
    const std::vector<ALStarField::Star> tenth = ALStarField::generate(ALStarField::DEFAULT_COUNT / 10, DOME_RADIUS);
    std::vector<U32> per_tier(LL_ARRAY_SIZE(TIERS), 0);
    for (const ALStarField::Star& star : tenth)
    {
        const F32 radii = star.mPosition.length() / DOME_RADIUS;
        bool found = false;
        for (size_t t = 0; t < LL_ARRAY_SIZE(TIERS); ++t)
        {
            if (fabsf(radii - TIERS[t].mDistance) < 1e-3f)
            {
                ++per_tier[t];
                found = true;
            }
        }
        CAPTURE(radii);
        REQUIRE(found);
    }
    for (size_t t = 0; t < LL_ARRAY_SIZE(TIERS); ++t)
    {
        CAPTURE(t);
        CHECK(per_tier[t] == ALStarField::TIER_COUNT[t] / 10);
    }
}

TEST_CASE("alstarfield: every star lies on the upper hemisphere in its tier's intensity range", "[alstarfield]")
{
    const std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    size_t first = 0;
    for (size_t t = 0; t < LL_ARRAY_SIZE(TIERS); ++t)
    {
        CAPTURE(t);
        const size_t end = first + ALStarField::TIER_COUNT[t];
        REQUIRE(end <= stars.size());
        U8 least = 255;
        U8 most  = 0;
        for (size_t s = first; s < end; ++s)
        {
            const ALStarField::Star& star = stars[s];
            CAPTURE(s);
            REQUIRE(star.mPosition.mV[VZ] >= 0.f);
            REQUIRE(fabsf(star.mPosition.length() / DOME_RADIUS - TIERS[t].mDistance) < 1e-3f);
            least = llmin(least, star.mColour.mV[VALPHA]);
            most  = llmax(most, star.mColour.mV[VALPHA]);
        }
        CAPTURE((S32)least, (S32)most);
        CHECK(least >= TIERS[t].mFloor);
        CHECK(most <= TIERS[t].mCeil);
        // The power law reaches near both ends of a tier's range.
        CHECK(least <= TIERS[t].mFloor + 1);
        CHECK(most >= TIERS[t].mCeil - 4);
        first = end;
    }
}

TEST_CASE("alstarfield: a star is spiked exactly where starsV.glsl gives it spikes", "[alstarfield]")
{
    // starsV.glsl's gate, smoothstep(0.90, 1.0, intensity), on the intensity the vertex stage unpacks.
    const std::string vertex = shader_source("class1/deferred/starsV.glsl");
    REQUIRE(vertex.find("smoothstep(0.90, 1.0, intensity)") != std::string::npos);
    for (S32 byte = 0; byte < 256; ++byte)
    {
        const F32 intensity = (F32)byte / 255.f;
        const F32 t = llclamp((intensity - 0.90f) / (1.0f - 0.90f), 0.f, 1.f);
        const F32 spike = t * t * (3.f - 2.f * t);
        ALStarField::Star star{ LLVector3(0.f, 0.f, 1.f), LLColor4U(255, 255, 255, (U8)byte) };
        CAPTURE(byte, spike);
        CHECK(ALStarField::spiked(star) == (spike > 0.f));
    }
}

TEST_CASE("alstarfield: the layout puts the spiked stars first and keeps every star", "[alstarfield]")
{
    const std::vector<ALStarField::Star> field = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    std::vector<ALStarField::Star> stars = field;
    const ALStarField::Layout layout = ALStarField::layOut(stars);

    REQUIRE(stars.size() == field.size());
    CHECK((size_t)layout.mQuads + layout.mPoints == field.size());
    // A few hundred of the bright tier.
    CHECK(layout.mQuads > 100);
    CHECK(layout.mQuads < 1000);
    CHECK(layout.firstPoint() == layout.mQuads * ALStarField::QUAD_VERTICES);
    CHECK(layout.vertices() == layout.firstPoint() + layout.mPoints);

    // Each part holds its kind: the quads in the field's own order, the points cell by cell, in the field's order
    // within a cell.
    std::vector<ALStarField::Star> quads;
    std::vector<ALStarField::Star> points;
    for (const ALStarField::Star& star : field)
    {
        (ALStarField::spiked(star) ? quads : points).push_back(star);
    }
    std::stable_sort(points.begin(), points.end(), [](const ALStarField::Star& a, const ALStarField::Star& b)
                     { return ALStarField::cellOf(a.mPosition) < ALStarField::cellOf(b.mPosition); });
    REQUIRE(quads.size() == layout.mQuads);
    size_t differ = 0;
    for (size_t s = 0; s < stars.size(); ++s)
    {
        const ALStarField::Star& expected = s < quads.size() ? quads[s] : points[s - quads.size()];
        differ += !same(stars[s], expected);
    }
    CHECK(differ == 0);
}

TEST_CASE("alstarfield: a star's vertex is its centre and its colour's bytes and a quad has six of them", "[alstarfield]")
{
    std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT / 50, DOME_RADIUS);
    const ALStarField::Layout layout = ALStarField::layOut(stars);
    REQUIRE(layout.mQuads > 0);
    REQUIRE(layout.mPoints > 0);

    std::vector<LLVector4a> vertices(layout.vertices() + 1);
    // Past the last vertex: nothing may be written there.
    std::memset(&vertices.back(), 0xA5, sizeof(LLVector4a));
    LLStrider<LLVector4a> strider = vertices.data();
    ALStarField::fillVertices(stars, layout, strider);

    const auto holds = [](const LLVector4a& vertex, const ALStarField::Star& star)
    {
        const F32* xyz = vertex.getF32ptr();
        U8 colour[4];
        std::memcpy(colour, xyz + 3, sizeof(colour));
        return xyz[0] == star.mPosition.mV[VX] && xyz[1] == star.mPosition.mV[VY] && xyz[2] == star.mPosition.mV[VZ]
            && std::memcmp(colour, star.mColour.mV, sizeof(colour)) == 0;
    };
    size_t wrong = 0;
    for (U32 v = 0; v < layout.vertices(); ++v)
    {
        const size_t star = v < layout.firstPoint() ? v / ALStarField::QUAD_VERTICES
                                                    : v - layout.firstPoint() + layout.mQuads;
        wrong += !holds(vertices[v], stars[star]);
    }
    CHECK(wrong == 0);
    U8 untouched[sizeof(LLVector4a)];
    std::memset(untouched, 0xA5, sizeof(untouched));
    CHECK(std::memcmp(&vertices.back(), untouched, sizeof(untouched)) == 0);
}

namespace
{
    // A camera: its bearing clockwise from north and pitch above the horizon, its roll, its vertical field of view and
    // aspect, all in degrees but the aspect; and how far the stars have turned about z.
    struct View
    {
        F32 mBearing;
        F32 mPitch;
        F32 mRoll;
        F32 mFov;
        F32 mAspect;
        F32 mRotation;
    };

    // The stars' frame to clip space for @a view, built as LLRender builds it for the star draw: the view from a camera
    // in the agent's frame, then the stars taken to the camera and turned (LLDrawPoolWLSky::renderStarsDeferred).
    LLMatrix4a starMVP(const View& view)
    {
        const LLVector3 camera(118.f, -43.f, 27.f);
        LLMatrix4a mv;
        mv.setIdentity();
        const auto apply = [&mv](const LLMatrix4a& m) { mv.setMul(m, mv); };
        apply(LLMatrix4a::rotation(view.mRoll * DEG_TO_RAD, LLVector4a(0.f, 0.f, 1.f)));
        apply(LLMatrix4a::rotation(-view.mPitch * DEG_TO_RAD, LLVector4a(1.f, 0.f, 0.f)));
        apply(LLMatrix4a::rotation(-90.f * DEG_TO_RAD, LLVector4a(1.f, 0.f, 0.f)));
        apply(LLMatrix4a::rotation(view.mBearing * DEG_TO_RAD, LLVector4a(0.f, 0.f, 1.f)));
        apply(LLMatrix4a::translation(-camera.mV[VX], -camera.mV[VY], -camera.mV[VZ]));
        apply(LLMatrix4a::translation(camera.mV[VX], camera.mV[VY], camera.mV[VZ]));
        apply(LLMatrix4a::rotation(view.mRotation * DEG_TO_RAD, LLVector4a(0.f, 0.f, 1.f)));

        constexpr F32 NEAR_CLIP = 0.5f;
        constexpr F32 FAR_CLIP  = 1024.f;
        const F32 tan_half = tanf(view.mFov * 0.5f * DEG_TO_RAD);
        LLMatrix4a proj;
        proj.clear();
        F32* p = proj.getF32ptr();
        p[0]  = 1.f / (tan_half * view.mAspect);
        p[5]  = 1.f / tan_half;
        p[10] = (FAR_CLIP + NEAR_CLIP) / (NEAR_CLIP - FAR_CLIP);
        p[11] = -1.f;
        p[14] = 2.f * FAR_CLIP * NEAR_CLIP / (NEAR_CLIP - FAR_CLIP);

        LLMatrix4a mvp;
        mvp.setMul(mv, proj);
        return mvp;
    }

    // Whether @a position lands within the clip square widened by @a margin_x and @a margin_y.
    bool inClip(const LLMatrix4a& mvp, const LLVector3& position, F32 margin_x, F32 margin_y)
    {
        LLVector4a clip;
        mvp.transform4(LLVector4a(position.mV[VX], position.mV[VY], position.mV[VZ], 1.f), clip);
        const F32 w = clip[3];
        return w > 0.f && fabsf(clip[0]) <= (1.f + margin_x) * w && fabsf(clip[1]) <= (1.f + margin_y) * w;
    }

    // Views about the sky: level, up, at the zenith, below the horizon, rolled, wide and narrow, and many at random.
    std::vector<View> views()
    {
        std::vector<View> out = {
            { 30.f, 35.f, 0.f, 60.f, 16.f / 9.f, 17.f },  { 200.f, -3.f, 0.f, 60.f, 16.f / 9.f, 245.f },
            { 75.f, 89.f, 0.f, 100.f, 1.5f, 3.f },        { 300.f, -40.f, 0.f, 60.f, 16.f / 9.f, 90.f },
            { 10.f, 20.f, 33.f, 45.f, 1.f, 300.f },       { 160.f, 60.f, -70.f, 120.f, 2.4f, 12.f },
            { 0.f, 0.f, 0.f, 10.f, 16.f / 9.f, 0.f },
        };
        std::mt19937 draw(7);
        std::uniform_real_distribution<F32> unit(0.f, 1.f);
        for (S32 i = 0; i < 40; ++i)
        {
            out.push_back({ 360.f * unit(draw), -30.f + 120.f * unit(draw), -45.f + 90.f * unit(draw),
                            15.f + 100.f * unit(draw), 1.f + 1.5f * unit(draw), 360.f * unit(draw) });
        }
        return out;
    }
}

TEST_CASE("alstarfield: the cells lay the points out in order with each star inside its cell's cone", "[alstarfield]")
{
    std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    const ALStarField::Layout layout = ALStarField::layOut(stars);
    REQUIRE(layout.mCells.size() == ALStarField::CELLS);

    U32 next = layout.firstPoint();
    U32 empty = 0;
    size_t wrong_cell = 0;
    size_t outside_cone = 0;
    for (U32 c = 0; c < ALStarField::CELLS; ++c)
    {
        const ALStarField::Cell& cell = layout.mCells[c];
        CAPTURE(c);
        REQUIRE(cell.mFirst == next);
        next += cell.mCount;
        empty += cell.mCount == 0;
        CHECK(fabsf(cell.mAxis.length() - 1.f) < 1e-4f);
        CHECK(cell.mSinRadius >= 0.f);
        CHECK(cell.mSinRadius < 0.5f);
        const F32 cos_radius = sqrtf(1.f - cell.mSinRadius * cell.mSinRadius);
        for (U32 v = cell.mFirst; v < cell.mFirst + cell.mCount; ++v)
        {
            const ALStarField::Star& star = stars[v - layout.firstPoint() + layout.mQuads];
            REQUIRE(!ALStarField::spiked(star));
            wrong_cell += ALStarField::cellOf(star.mPosition) != c;
            const LLVector3 direction = star.mPosition / star.mPosition.length();
            outside_cone += cell.mAxis * direction < cos_radius - 1e-5f;
        }
    }
    CHECK(next == layout.vertices());
    CHECK(wrong_cell == 0);
    CHECK(outside_cone == 0);
    // The default field leaves no cell of the sky empty.
    CHECK(empty == 0);
}

TEST_CASE("alstarfield: the cull keeps every cell with a point in view and leaves most of the sky out", "[alstarfield]")
{
    std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    const ALStarField::Layout layout = ALStarField::layOut(stars);
    constexpr U32 WIDTH  = 1920;
    constexpr U32 HEIGHT = 1080;
    const F32 margin_x = 2.f * ALStarField::POINT_REACH / (F32)WIDTH;
    const F32 margin_y = 2.f * ALStarField::POINT_REACH / (F32)HEIGHT;

    ALStarField::Draws draws;
    for (const View& view : views())
    {
        CAPTURE(view.mBearing, view.mPitch, view.mRoll, view.mFov, view.mAspect, view.mRotation);
        const LLMatrix4a mvp = starMVP(view);
        const ALStarField::Frustum frustum = ALStarField::pointFrustum(mvp, WIDTH, HEIGHT);

        size_t lost = 0;
        size_t in_view = 0;
        for (const ALStarField::Cell& cell : layout.mCells)
        {
            const bool kept = ALStarField::inView(frustum, cell);
            for (U32 v = cell.mFirst; v < cell.mFirst + cell.mCount; ++v)
            {
                const ALStarField::Star& star = stars[v - layout.firstPoint() + layout.mQuads];
                if (inClip(mvp, star.mPosition, margin_x, margin_y))
                {
                    ++in_view;
                    lost += !kept;
                }
            }
        }
        CAPTURE(in_view);
        CHECK(lost == 0);

        // The draws are the cells kept, in order, the runs that meet merged.
        ALStarField::pointDraws(layout, frustum, draws);
        REQUIRE(draws.mFirst.size() == draws.mCount.size());
        U32 drawn = 0;
        for (size_t r = 0; r < draws.mFirst.size(); ++r)
        {
            CHECK(draws.mCount[r] > 0);
            if (r > 0)
            {
                CHECK(draws.mFirst[r] > draws.mFirst[r - 1] + draws.mCount[r - 1]);
            }
            drawn += (U32)draws.mCount[r];
        }
        U32 kept_points = 0;
        for (const ALStarField::Cell& cell : layout.mCells)
        {
            kept_points += ALStarField::inView(frustum, cell) ? cell.mCount : 0;
        }
        CHECK(drawn == kept_points);
        CHECK(drawn >= in_view);
    }

    // Looking 35 degrees up through 60 degrees at 16:9, the view draws under a third of the field, and the cells it
    // draws are mostly in view.
    const View level = { 30.f, 35.f, 0.f, 60.f, 16.f / 9.f, 17.f };
    const LLMatrix4a mvp = starMVP(level);
    ALStarField::pointDraws(layout, ALStarField::pointFrustum(mvp, WIDTH, HEIGHT), draws);
    size_t in_view = 0;
    for (size_t p = layout.mQuads; p < stars.size(); ++p)
    {
        in_view += inClip(mvp, stars[p].mPosition, margin_x, margin_y);
    }
    const U32 drawn = al_star_test::drawnPoints(draws);
    CAPTURE(drawn, in_view, draws.mFirst.size());
    CHECK(drawn * 3 < layout.mPoints);
    CHECK(drawn < in_view * 3 / 2);
}
