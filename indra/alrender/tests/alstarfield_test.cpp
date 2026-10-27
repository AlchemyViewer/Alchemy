/**
 * @file alstarfield_test.cpp
 * @brief The star field: the same field from the same count in every session, laid out in its tiers, and the
 *        buffer it is drawn from, binned into cells a view culls, brightest first so a frame draws only the stars it
 *        can see.
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
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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
    // The first stars of the default field, as ALSky has them laid out from the fixed seed: a change of
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
    // starsV.glsl's gate, smoothstep(STAR_SPIKE_START, 1.0, intensity), on the intensity the vertex stage unpacks.
    const std::string vertex = shader_source("class1/deferred/starsV.glsl");
    REQUIRE(vertex.find("smoothstep(STAR_SPIKE_START, 1.0, intensity)") != std::string::npos);
    for (S32 byte = 0; byte < 256; ++byte)
    {
        const F32 intensity = (F32)byte / 255.f;
        const F32 t = llclamp((intensity - ALStarField::SPIKE_START) / (1.0f - ALStarField::SPIKE_START), 0.f, 1.f);
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

    // Each part holds its kind: the quads brightest first, the points cell by cell and brightest first within a cell,
    // stars as bright in the field's own order.
    std::vector<ALStarField::Star> quads;
    std::vector<ALStarField::Star> points;
    for (const ALStarField::Star& star : field)
    {
        (ALStarField::spiked(star) ? quads : points).push_back(star);
    }
    const auto brighter = [](const ALStarField::Star& a, const ALStarField::Star& b)
    { return a.mColour.mV[VALPHA] > b.mColour.mV[VALPHA]; };
    std::stable_sort(quads.begin(), quads.end(), brighter);
    std::stable_sort(points.begin(), points.end(), [&brighter](const ALStarField::Star& a, const ALStarField::Star& b)
    {
        const U32 cell_a = ALStarField::cellOf(a.mPosition);
        const U32 cell_b = ALStarField::cellOf(b.mPosition);
        return cell_a != cell_b ? cell_a < cell_b : brighter(a, b);
    });
    REQUIRE(quads.size() == layout.mQuads);
    size_t differ = 0;
    for (size_t s = 0; s < stars.size(); ++s)
    {
        const ALStarField::Star& expected = s < quads.size() ? quads[s] : points[s - quads.size()];
        differ += !same(stars[s], expected);
    }
    CHECK(differ == 0);
}

namespace
{
    // The layout by comparison sorts, stable, from the same field: the spiked stars first and brightest first, then the
    // points by cell and brightest first within each, stars as bright in the field's own order; each cell's run and
    // cone as layOut works them.
    ALStarField::Layout referenceLayOut(std::vector<ALStarField::Star>& stars)
    {
        using namespace ALStarField;
        const auto points = std::stable_partition(stars.begin(), stars.end(), spiked);

        Layout layout;
        layout.mQuads  = (U32)(points - stars.begin());
        layout.mPoints = (U32)(stars.end() - points);

        const auto brighter = [](const Star& a, const Star& b) { return a.mColour.mV[VALPHA] > b.mColour.mV[VALPHA]; };
        std::stable_sort(stars.begin(), points, brighter);
        std::stable_sort(points, stars.end(), [&brighter](const Star& a, const Star& b)
        {
            const U32 cell_a = cellOf(a.mPosition);
            const U32 cell_b = cellOf(b.mPosition);
            return cell_a != cell_b ? cell_a < cell_b : brighter(a, b);
        });

        layout.mCells.resize(CELLS);
        U32 first = 0;
        for (U32 c = 0; c < CELLS; ++c)
        {
            Cell& run = layout.mCells[c];
            run.mFirst = layout.firstPoint() + first;
            while (first + run.mCount < layout.mPoints && cellOf(points[first + run.mCount].mPosition) == c)
            {
                ++run.mCount;
            }
            LLVector3 sum;
            for (U32 p = first; p < first + run.mCount; ++p)
            {
                sum += points[p].mPosition / points[p].mPosition.length();
            }
            if (run.mCount == 0 || sum.normalize() == 0.f)
            {
                run.mAxis.set(0.f, 0.f, 1.f);
                run.mSinRadius = run.mCount == 0 ? 0.f : 1.f;
            }
            else
            {
                run.mAxis = sum;
                F32 least_cos = 1.f;
                for (U32 p = first; p < first + run.mCount; ++p)
                {
                    least_cos = llmin(least_cos, run.mAxis * (points[p].mPosition / points[p].mPosition.length()));
                }
                run.mSinRadius = least_cos > 0.f ? sqrtf(llmax(0.f, 1.f - least_cos * least_cos)) : 1.f;
            }
            first += run.mCount;
        }
        for (const Star& star : stars)
        {
            layout.mIntensities.push_back(star.mColour.mV[VALPHA]);
        }
        return layout;
    }
}

TEST_CASE("alstarfield: the layout is the stable sort's at every count", "[alstarfield]")
{
    for (const U32 count : { 0u, 1u, 7u, 2000u, ALStarField::DEFAULT_COUNT / 10, ALStarField::DEFAULT_COUNT,
                             ALStarField::MAX_COUNT / 2, ALStarField::MAX_COUNT })
    {
        CAPTURE(count);
        std::vector<ALStarField::Star> stars = ALStarField::generate(count, DOME_RADIUS);
        std::vector<ALStarField::Star> expected_stars = stars;
        const ALStarField::Layout layout = ALStarField::layOut(stars);
        const ALStarField::Layout expected = referenceLayOut(expected_stars);

        CHECK(layout.mQuads == expected.mQuads);
        CHECK(layout.mPoints == expected.mPoints);
        REQUIRE(stars.size() == expected_stars.size());
        size_t moved = 0;
        for (size_t s = 0; s < stars.size(); ++s)
        {
            moved += !same(stars[s], expected_stars[s]);
        }
        CHECK(moved == 0);
        CHECK(layout.mIntensities == expected.mIntensities);
        // The runs exactly; the cones, summed over the same stars in the same order, to the rounding another
        // translation unit's float maths may differ by: the radius by its square, one less the least cosine's square,
        // since a cell of one or two stars takes the square root of a rounding.
        REQUIRE(layout.mCells.size() == expected.mCells.size());
        size_t runs_differ = 0;
        size_t cones_differ = 0;
        for (size_t c = 0; c < layout.mCells.size(); ++c)
        {
            const ALStarField::Cell& a = layout.mCells[c];
            const ALStarField::Cell& b = expected.mCells[c];
            runs_differ += a.mFirst != b.mFirst || a.mCount != b.mCount;
            cones_differ += (a.mAxis - b.mAxis).length() > 1e-5f
                         || fabsf(a.mSinRadius * a.mSinRadius - b.mSinRadius * b.mSinRadius) > 1e-5f;
        }
        CHECK(runs_differ == 0);
        CHECK(cones_differ == 0);
    }
}

TEST_CASE("alstarfield: a star's vertex is its centre and its colour's bytes and a quad has six of them", "[alstarfield]")
{
    std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT / 50, DOME_RADIUS);
    const ALStarField::Layout layout = ALStarField::layOut(stars);
    REQUIRE(layout.mQuads > 0);
    REQUIRE(layout.mPoints > 0);

    std::vector<LLVector4a> vertices(layout.vertices() + 1);
    // Past the last vertex: nothing may be written there.
    std::memset(vertices.back().getF32ptr(), 0xA5, sizeof(LLVector4a));
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
    CHECK(std::memcmp(vertices.back().getF32ptr(), untouched, sizeof(untouched)) == 0);
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
    // in the agent's frame, then the stars taken to the camera and turned (ALSky::renderStars).
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
        ALStarField::pointDraws(layout, frustum, 0, draws);
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
    ALStarField::pointDraws(layout, ALStarField::pointFrustum(mvp, WIDTH, HEIGHT), 0, draws);
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

namespace
{
    // What a star adds to the brightest pixel it reaches, worked as starsV.glsl and starsF.glsl work it: its colour
    // (sRGB bytes) and intensity byte, the cosine of its zenith angle, its scintillation noises (each 0..1), the moon's
    // power and the cosine of the star's angle from it, the star brightness and the path; times the shape at its centre.
    F32 shaderPeak(const LLColor4U& colour, F32 cos_zenith, F32 twinkle_base, const LLVector3& chroma_noise,
                   F32 moon_power, F32 moon_cos, F32 star_alpha, bool emissive)
    {
        const F32 intensity = (F32)colour.mV[VALPHA] / 255.f;

        using namespace ALStarField;

        const F32 base_px  = FAINT_PX * (1.f - intensity) + BRIGHT_PX * intensity;
        const F32 t        = llclamp((intensity - SPIKE_START) / (1.f - SPIKE_START), 0.f, 1.f);
        const F32 spike    = t * t * (3.f - 2.f * t);
        const F32 ideal_px = base_px + spike * SPIKE_PX;
        const F32 quad_px  = llmax(ideal_px, MIN_PX);
        const F32 subpixel_fade = (ideal_px * ideal_px) / (quad_px * quad_px);

        const F32 cz       = llclamp(cos_zenith, 0.02f, 1.0f);
        const F32 elev_deg = asinf(cz) * RAD_TO_DEG;
        const F32 airmass  = 1.0f / (cz + 0.50572f * powf(llmax(6.07995f + elev_deg, 1.0f), -1.6364f));
        const F32 k_ext[3] = { 0.05f, 0.13f, 0.30f };

        const F32 twinkle_amount = llclamp(0.18f + 0.14f * (airmass - 1.0f), 0.0f, TWINKLE_MAX) * intensity;
        const F32 twinkle        = 1.0f + (twinkle_base - 0.5f) * 2.0f * twinkle_amount;
        const F32 chroma_amt     = CHROMA * twinkle_amount;

        const F32 sky_wash     = moon_power * (1.0f - intensity) * MOON_WASH;
        const F32 direct_glare = powf(llmax(moon_cos, 0.f), 10.0f) * moon_power * 0.85f;
        const F32 moon_fade    = llclamp(sky_wash + direct_glare, 0.0f, MOON_FADE_MAX);

        const F32 gain  = emissive ? 1.0f + intensity * intensity * BLOOM_BOOST : 1.0f + intensity;
        const F32 shape = 1.0f + HALO + 2.0f * spike * SPIKE;

        F32 peak = 0.f;
        for (S32 c = 0; c < 3; ++c)
        {
            const F32 linear = powf((F32)colour.mV[c] / 255.f, 2.2f);
            const F32 jitter = 1.0f + (chroma_noise.mV[c] - 0.5f) * 2.0f * chroma_amt;
            F32 star = linear * jitter * expf(-airmass * k_ext[c]) * twinkle;
            star *= intensity * star_alpha * (1.0f - moon_fade) * subpixel_fade;
            peak = llmax(peak, star * gain * shape);
        }
        return peak;
    }
}

TEST_CASE("alstarfield: the quads and every cell's points run brightest first", "[alstarfield]")
{
    std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    const ALStarField::Layout layout = ALStarField::layOut(stars);
    REQUIRE(layout.mIntensities.size() == stars.size());

    size_t mismatched = 0;
    for (size_t s = 0; s < stars.size(); ++s)
    {
        mismatched += layout.mIntensities[s] != stars[s].mColour.mV[VALPHA];
    }
    CHECK(mismatched == 0);

    const auto descending = [&layout](size_t first, size_t count)
    {
        for (size_t s = first + 1; s < first + count; ++s)
        {
            if (layout.mIntensities[s] > layout.mIntensities[s - 1])
            {
                return false;
            }
        }
        return true;
    };
    CHECK(descending(0, layout.mQuads));
    size_t unsorted = 0;
    for (const ALStarField::Cell& cell : layout.mCells)
    {
        unsorted += !descending(cell.mFirst - layout.firstPoint() + layout.mQuads, cell.mCount);
    }
    CHECK(unsorted == 0);
}

namespace
{
    // The value of the shader constant `const float <name> = <value>;` in @a source, or NaN where it declares none.
    F32 shaderConstant(const std::string& source, const std::string& name)
    {
        const std::string declaration = "const float " + name;
        for (size_t at = source.find(declaration); at != std::string::npos; at = source.find(declaration, at + 1))
        {
            size_t next = at + declaration.size();
            if (next < source.size() && (std::isalnum((unsigned char)source[next]) || source[next] == '_'))
            {
                continue; // a longer name
            }
            next = source.find_first_not_of(" \t", next);
            if (next == std::string::npos || source[next] != '=')
            {
                continue;
            }
            return std::strtof(source.c_str() + next + 1, nullptr);
        }
        return std::numeric_limits<F32>::quiet_NaN();
    }
}

TEST_CASE("alstarfield: the brightness cull's bound is starsV.glsl's and starsF.glsl's", "[alstarfield]")
{
    // The terms ALStarField::brightest bounds: if a shader changes one, the bound has to follow.
    const std::string vertex   = shader_source("class1/deferred/starsV.glsl");
    const std::string fragment = shader_source("class1/deferred/starsF.glsl");

    // Each bound the shaders and brightest share, by name.
    struct Shared
    {
        const std::string* mSource;
        const char*        mName;
        F32                mValue;
    };
    const Shared SHARED[] = {
        { &vertex, "STAR_FAINT_PX", ALStarField::FAINT_PX },
        { &vertex, "STAR_BRIGHT_PX", ALStarField::BRIGHT_PX },
        { &vertex, "STAR_SPIKE_START", ALStarField::SPIKE_START },
        { &vertex, "STAR_SPIKE_PX", ALStarField::SPIKE_PX },
        { &vertex, "STAR_MIN_PX", ALStarField::MIN_PX },
        { &vertex, "STAR_TWINKLE_MAX", ALStarField::TWINKLE_MAX },
        { &vertex, "STAR_CHROMA", ALStarField::CHROMA },
        { &vertex, "STAR_MOON_WASH", ALStarField::MOON_WASH },
        { &vertex, "STAR_MOON_FADE_MAX", ALStarField::MOON_FADE_MAX },
        { &vertex, "STAR_BLOOM_BOOST", ALStarField::BLOOM_BOOST },
        { &fragment, "STAR_HALO", ALStarField::HALO },
        { &fragment, "STAR_SPIKE", ALStarField::SPIKE },
    };
    for (const Shared& shared : SHARED)
    {
        const F32 value = shaderConstant(*shared.mSource, shared.mName);
        CAPTURE(shared.mName, value, shared.mValue);
        CHECK(value == shared.mValue);
    }

    // And the shaders' terms use them as brightest does.
    for (const char* term : { "mix(STAR_FAINT_PX, STAR_BRIGHT_PX, intensity)",
                              "smoothstep(STAR_SPIKE_START, 1.0, intensity) * STAR_SPIKE_PX",
                              "max(ideal_px, STAR_MIN_PX)", "(ideal_px * ideal_px) / (quad_pixels * quad_pixels)",
                              "clamp(0.18 + 0.14 * (airmass - 1.0), 0.0, STAR_TWINKLE_MAX) * intensity",
                              "1.0 + (twinkle_base - 0.5) * 2.0 * twinkle_amount", "STAR_CHROMA * twinkle_amount",
                              "1.0 + (chroma_noise - 0.5) * 2.0 * chroma_amt",
                              "moon_power * (1.0 - intensity) * STAR_MOON_WASH",
                              "clamp(sky_wash + direct_glare, 0.0, STAR_MOON_FADE_MAX)",
                              "1.0 + intensity * intensity * STAR_BLOOM_BOOST", "1.0 + intensity;",
                              "exp(-airmass * k_ext)", "star_color *= custom_alpha",
                              "vary_spike = smoothstep(STAR_SPIKE_START, 1.0, intensity)" })
    {
        CAPTURE(term);
        CHECK(vertex.find(term) != std::string::npos);
    }
    for (const char* term : { "exp(-r2 * 9.0)", "exp(-r2 * 1.8) * STAR_HALO", "* vary_spike * STAR_SPIKE;",
                              "exp(-abs(corner.y) * 32.0) * ax" })
    {
        CAPTURE(term);
        CHECK(fragment.find(term) != std::string::npos);
    }
}

TEST_CASE("alstarfield: the brightness cull leaves out only stars that cannot add half a step", "[alstarfield]")
{
    const LLColor4U colours[] = { LLColor4U(255, 255, 255), LLColor4U(255, 0, 0), LLColor4U(180, 205, 255),
                                  LLColor4U(0, 0, 255) };
    const F32 zeniths[]  = { 0.02f, 0.1f, 0.35f, 0.7f, 1.0f };
    const F32 noises[]   = { 0.f, 0.5f, 1.f };
    const F32 powers[]   = { 0.f, 0.25f, 0.6f, 1.f };
    const F32 alphas[]   = { 0.001f, 0.004f, 0.01f, 0.03f, 0.08f, 0.2f, 0.5f, 1.f, 2.f };

    size_t loud = 0;
    for (const bool emissive : { false, true })
    {
        for (const F32 alpha : alphas)
        {
            for (const F32 power : powers)
            {
                const U32 faintest = ALStarField::faintestVisible(alpha, power, emissive);
                CAPTURE(emissive, alpha, power, faintest);
                REQUIRE(faintest <= 256);
                // The peak of every star the cull leaves out, and of the faintest it keeps.
                F32 left_out = 0.f;
                F32 kept     = 0.f;
                for (U32 byte = faintest >= 8 ? faintest - 8 : 0; byte <= llmin(faintest, 255u); ++byte)
                {
                    for (LLColor4U colour : colours)
                    {
                        colour.mV[VALPHA] = (U8)byte;
                        for (const F32 zenith : zeniths)
                        {
                            for (const F32 twinkle : noises)
                            {
                                for (const F32 chroma : noises)
                                {
                                    const F32 peak = shaderPeak(colour, zenith, twinkle, LLVector3(chroma, chroma, chroma),
                                                                power, 0.f, alpha, emissive);
                                    F32& most = byte < faintest ? left_out : kept;
                                    most = llmax(most, peak);
                                }
                            }
                        }
                    }
                }
                CAPTURE(left_out * 255.f, kept * 255.f);
                CHECK(left_out < ALStarField::LEFT_OUT);
                loud += left_out >= ALStarField::LEFT_OUT;
                // And the bound is near: the faintest star kept can come within a third of it, so the cull leaves
                // out nearly all it could.
                if (faintest <= 255)
                {
                    CHECK(kept >= 0.7f * ALStarField::LEFT_OUT);
                }
            }
        }
    }
    CHECK(loud == 0);

    // The default night (star brightness 250 of 500) keeps even the faintest dust; dusk leaves most of it out.
    CHECK(ALStarField::faintestVisible(0.5f, 0.f, true) <= 13);
    CHECK(ALStarField::faintestVisible(0.5f, 0.f, false) <= 13);
    CHECK(ALStarField::faintestVisible(0.03f, 0.f, true) > 13);
    CHECK(ALStarField::faintestVisible(0.f, 0.f, true) == 256);
}

TEST_CASE("alstarfield: a view draws each cell in view down to the faintest star kept", "[alstarfield]")
{
    std::vector<ALStarField::Star> stars = ALStarField::generate(ALStarField::DEFAULT_COUNT, DOME_RADIUS);
    const ALStarField::Layout layout = ALStarField::layOut(stars);
    const LLMatrix4a mvp = starMVP({ 30.f, 35.f, 0.f, 60.f, 16.f / 9.f, 17.f });
    const ALStarField::Frustum frustum = ALStarField::pointFrustum(mvp, 1920, 1080);

    ALStarField::Draws all;
    ALStarField::pointDraws(layout, frustum, 0, all);
    const U32 all_points = al_star_test::drawnPoints(all);
    for (const U32 faintest : { 0u, 14u, 30u, 100u, 230u, 256u })
    {
        CAPTURE(faintest);
        ALStarField::Draws draws;
        ALStarField::pointDraws(layout, frustum, faintest, draws);
        U32 expected = 0;
        for (const ALStarField::Cell& cell : layout.mCells)
        {
            if (!ALStarField::inView(frustum, cell))
            {
                continue;
            }
            for (U32 v = cell.mFirst; v < cell.mFirst + cell.mCount; ++v)
            {
                expected += layout.mIntensities[v - layout.firstPoint() + layout.mQuads] >= faintest;
            }
        }
        CHECK(al_star_test::drawnPoints(draws) == expected);
        // Each run starts a cell's points and draws only stars at the faintest or brighter.
        size_t too_faint = 0;
        for (size_t r = 0; r < draws.mFirst.size(); ++r)
        {
            for (S32 v = draws.mFirst[r]; v < draws.mFirst[r] + draws.mCount[r]; ++v)
            {
                too_faint += layout.mIntensities[(size_t)v - layout.firstPoint() + layout.mQuads] < faintest;
            }
        }
        CHECK(too_faint == 0);
        CHECK(al_star_test::drawnPoints(draws) <= all_points);

        U32 quads = 0;
        for (U32 q = 0; q < layout.mQuads; ++q)
        {
            quads += layout.mIntensities[q] >= faintest;
        }
        CHECK(ALStarField::quadsDrawn(layout, faintest) == quads);
    }
    CHECK(all_points > 0);

    // At dusk, a view draws a fraction of what the night shows; by a star brightness of 0.1 (the default night is 0.5)
    // every star in view can add half a step, the moon's wash or not.
    for (const bool emissive : { false, true })
    {
        CAPTURE(emissive);
        const auto drawn = [&](F32 alpha, F32 moon_power)
        {
            ALStarField::Draws dusk;
            ALStarField::pointDraws(layout, frustum, ALStarField::faintestVisible(alpha, moon_power, emissive), dusk);
            return al_star_test::drawnPoints(dusk);
        };
        CHECK(drawn(0.003f, 0.f) * 10 < all_points);
        CHECK(drawn(0.01f, 0.f) * 5 < all_points * 2);
        CHECK(drawn(0.03f, 0.f) * 5 < all_points * 3);
        CHECK(drawn(0.03f, 0.8f) < drawn(0.03f, 0.f));
        CHECK(drawn(0.1f, 0.8f) == all_points);
    }
}
