/**
 * @file alterrainsurfacemaps_test.cpp
 * @brief The terrain map layout: the apron offset, the row order, channel
 *        interleaving, which neighbour a grid coordinate beyond the region
 *        resolves to, and which texels a change marks dirty.
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

#include "../alterrainsurfacemaps.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    struct alterrainsurfacemaps_data
    {
        // A small region: 8 grids plus the buffer column, like 256 plus one.
        static constexpr S32 GPE = 9;
        static constexpr S32 APRON = ALTerrainSurfaceMaps::APRON;
        static constexpr S32 SIZE = GPE + 2 * APRON;

        // Texel index for a grid coordinate, the way the map is laid out.
        static size_t texel(S32 gx, S32 gy, U32 channels = 1)
        {
            return (static_cast<size_t>(gy + APRON) * SIZE + (gx + APRON)) * channels;
        }

        static void allSet(bool value, bool (&out)[8])
        {
            for (bool& b : out) b = value;
        }
    };

    // fill() visits every grid coordinate from -APRON to gpe + APRON - 1 on
    // both axes, once, rows along y, and lands each at (gx + APRON, gy + APRON).
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: fill() lays the grid out APRON in with rows along y", "[ALTerrainSurfaceMaps]")
    {
        std::vector<F32> out;
        // How many times each coordinate on the map is visited, and how many
        // visits are off it.
        std::vector<S32> visits(static_cast<size_t>(SIZE) * SIZE, 0);
        S32              off_map = 0;
        ALTerrainSurfaceMaps::fill(out, GPE, 1, [&visits, &off_map](S32 gx, S32 gy, F32* t) {
            t[0] = (F32)(gx * 100 + gy);
            if (gx < -APRON || gx >= GPE + APRON || gy < -APRON || gy >= GPE + APRON)
            {
                ++off_map;
                return;
            }
            ++visits[texel(gx, gy)];
        });
        REQUIRE(out.size() == static_cast<size_t>(SIZE) * SIZE);
        {
            INFO("every coordinate visited once, and none off the map");
            CHECK(off_map == 0);
            CHECK(std::count_if(visits.begin(), visits.end(), [](S32 n) { return n != 1; }) == 0);
        }
        UNSCOPED_INFO("first texel is the south-west apron corner");
        CHECK(out[texel(-APRON, -APRON)] == (F32)(-APRON * 100 - APRON));
        UNSCOPED_INFO("grid origin sits APRON in");
        CHECK(out[texel(0, 0)] == 0.f);
        UNSCOPED_INFO("buffer column");
        CHECK(out[texel(GPE - 1, 3)] == (F32)((GPE - 1) * 100 + 3));
        UNSCOPED_INFO("last texel is the north-east apron corner");
        CHECK(out[texel(GPE + APRON - 1, GPE + APRON - 1)] == (F32)((GPE + APRON - 1) * 100 + GPE + APRON - 1));
        UNSCOPED_INFO("row stride is the map width");
        CHECK(out[texel(0, 1)] - out[texel(0, 0)] == 1.f);
        UNSCOPED_INFO("column stride is one texel");
        CHECK(out[texel(1, 0)] - out[texel(0, 0)] == 100.f);
    }

    // Two channels interleave, and a second fill of a buffer already the
    // right size keeps the channel the sampler does not write.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: two channels interleave and a refill keeps the channel it does not write", "[ALTerrainSurfaceMaps]")
    {
        std::vector<F32> out;
        ALTerrainSurfaceMaps::fill(out, GPE, 2, [](S32 gx, S32 gy, F32* t) { t[0] = (F32)gx; t[1] = (F32)gy; });
        REQUIRE(out.size() == static_cast<size_t>(SIZE) * SIZE * 2);
        UNSCOPED_INFO("channel 0");
        CHECK(out[texel(3, 5, 2)] == 3.f);
        UNSCOPED_INFO("channel 1");
        CHECK(out[texel(3, 5, 2) + 1] == 5.f);

        ALTerrainSurfaceMaps::fill(out, GPE, 2, [](S32 gx, S32, F32* t) { t[0] = (F32)(gx + 1000); });
        UNSCOPED_INFO("channel 0 rewritten");
        CHECK(out[texel(3, 5, 2)] == 1003.f);
        UNSCOPED_INFO("channel 1 kept");
        CHECK(out[texel(3, 5, 2) + 1] == 5.f);
    }

    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: resolve moves nothing inside the region", "[ALTerrainSurfaceMaps]")
    {
        bool has[8];
        allSet(true, has);
        S32 gx = 4, gy = GPE - 1;
        UNSCOPED_INFO("interior is this surface");
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == MIDDLE);
        CHECK(gx == 4);
        UNSCOPED_INFO("gy unchanged, the buffer row is ours");
        CHECK(gy == GPE - 1);
    }

    // One axis out, neighbour present: the neighbour's grid 0 is our last
    // column, so our -1 is their gpe - 2 and our gpe is their 1.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: one axis out resolves to that neighbour's grid", "[ALTerrainSurfaceMaps]")
    {
        bool has[8];
        allSet(true, has);
        S32 gx = -1, gy = 3;
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == WEST);
        UNSCOPED_INFO("west gx");
        CHECK(gx == GPE - 2);
        UNSCOPED_INFO("west gy");
        CHECK(gy == 3);

        gx = -APRON; gy = 3;
        ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has);
        UNSCOPED_INFO("west, two out");
        CHECK(gx == GPE - 3);

        gx = GPE; gy = 3;
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == EAST);
        UNSCOPED_INFO("east gx");
        CHECK(gx == 1);

        gx = GPE + 1; gy = 3;
        ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has);
        UNSCOPED_INFO("east, two out");
        CHECK(gx == 2);

        gx = 3; gy = -1;
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == SOUTH);
        UNSCOPED_INFO("south gy");
        CHECK(gy == GPE - 2);

        gx = 3; gy = GPE + 1;
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == NORTH);
        UNSCOPED_INFO("north gy");
        CHECK(gy == 2);
    }

    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: both axes out resolve to the diagonal neighbour when it is there", "[ALTerrainSurfaceMaps]")
    {
        bool has[8];
        allSet(true, has);
        S32 gx = -1, gy = -APRON;
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == SOUTHWEST);
        UNSCOPED_INFO("south-west gx");
        CHECK(gx == GPE - 2);
        UNSCOPED_INFO("south-west gy");
        CHECK(gy == GPE - 3);

        gx = GPE + 1; gy = GPE;
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == NORTHEAST);
        UNSCOPED_INFO("north-east gx");
        CHECK(gx == 2);
        UNSCOPED_INFO("north-east gy");
        CHECK(gy == 1);
    }

    // A missing neighbour clamps the axis that needed it and keeps the rest of
    // the walk: no west means gx clamps to 0 and y still resolves north.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: a missing neighbour clamps its axis and the other still resolves", "[ALTerrainSurfaceMaps]")
    {
        bool has[8];
        allSet(true, has);
        has[WEST] = false;
        S32 gx = -1, gy = 3;
        UNSCOPED_INFO("no west: this surface");
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == MIDDLE);
        UNSCOPED_INFO("no west: gx clamped");
        CHECK(gx == 0);

        gx = -1; gy = GPE;
        UNSCOPED_INFO("no west, north present: north");
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == NORTH);
        UNSCOPED_INFO("north with gx clamped");
        CHECK(gx == 0);
        UNSCOPED_INFO("north gy");
        CHECK(gy == 1);

        allSet(true, has);
        has[NORTHWEST] = false;
        gx = -1; gy = GPE;
        UNSCOPED_INFO("west present, north-west missing: west");
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == WEST);
        UNSCOPED_INFO("west gx");
        CHECK(gx == GPE - 2);
        UNSCOPED_INFO("west gy clamped to its last row");
        CHECK(gy == GPE - 1);

        allSet(false, has);
        gx = GPE + 1; gy = -1;
        UNSCOPED_INFO("no neighbours: this surface");
        CHECK(ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) == MIDDLE);
        UNSCOPED_INFO("gx clamped to the buffer column");
        CHECK(gx == GPE - 1);
        UNSCOPED_INFO("gy clamped to 0");
        CHECK(gy == 0);
    }

    // The smooth surface passes through every sample: at an integer grid point the
    // value is the sample, whatever the neighbours do.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: smoothHeight passes through every sample", "[ALTerrainSurfaceMaps]")
    {
        auto field = [](S32 gx, S32 gy) { return (F32)(gx * gx - 3 * gy + (gx * gy) % 5); };
        for (S32 gy = 0; gy < 6; ++gy)
        {
            for (S32 gx = 0; gx < 6; ++gx)
            {
                CAPTURE(gx, gy);
                const F32 h = ALTerrainSurfaceMaps::smoothHeight((F32)gx, (F32)gy, field, nullptr, nullptr);
                REQUIRE(h == field(gx, gy));
            }
        }
    }

    // A step never grows a lip. A cliff -- level, then a thirty-metre drop in
    // one cell -- keeps the level ground level right up to the edge and the
    // drop inside the cell's range; a Catmull-Rom would put metres of bump
    // above the crease. And a monotone run stays monotone, so a ramp is a
    // ramp with no ripple ahead of or behind the step.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: smoothHeight grows no lip at a step and stays monotone down it", "[ALTerrainSurfaceMaps]")
    {
        auto cliff = [](S32 gx, S32) { return gx <= 2 ? 30.f : 0.f; };
        for (F32 x = 0.f; x < 2.f; x += 0.125f)
        {
            CAPTURE(x);
            F32 dzdx = 1.f, dzdy = 1.f;
            UNSCOPED_INFO("level up to the edge");
            REQUIRE_THAT(ALTerrainSurfaceMaps::smoothHeight(x, 3.f, cliff, &dzdx, &dzdy), ll_test::WithinBits(30.f, 12));
            // The tangent at the crease sample is zeroed, not averaged with the drop: the
            // level run has no slope at all, so it does not lean toward the edge.
            UNSCOPED_INFO("no slope on the level run");
            REQUIRE_THAT(dzdx, ll_test::WithinBits(0.f, 12));
        }
        F32 previous = 30.f;
        for (F32 x = 2.f; x <= 3.f; x += 0.0625f)
        {
            CAPTURE(x);
            const F32 h = ALTerrainSurfaceMaps::smoothHeight(x, 3.f, cliff, nullptr, nullptr);
            {
                INFO("inside the cell's range");
                REQUIRE(h <= 30.f);
                REQUIRE(h >= 0.f);
            }
            UNSCOPED_INFO("monotone down the face");
            REQUIRE(h <= previous);
            previous = h;
        }
        UNSCOPED_INFO("mid-cell is the midpoint of the step");
        CHECK_THAT(ALTerrainSurfaceMaps::smoothHeight(2.5f, 3.f, cliff, nullptr, nullptr), ll_test::WithinBits(15.f, 12));
        for (F32 x = 3.f; x < 5.f; x += 0.125f)
        {
            CAPTURE(x);
            UNSCOPED_INFO("level past the foot");
            REQUIRE_THAT(ALTerrainSurfaceMaps::smoothHeight(x, 3.f, cliff, nullptr, nullptr), ll_test::WithinBits(0.f, 12));
        }

        // Off the axis too: a corner of the plateau, evaluated across the
        // cell that holds it, stays between its four corner heights.
        auto corner = [](S32 gx, S32 gy) { return (gx <= 2 && gy <= 2) ? 30.f : 0.f; };
        for (F32 y = 2.f; y <= 3.f; y += 0.25f)
        {
            for (F32 x = 2.f; x <= 3.f; x += 0.25f)
            {
                CAPTURE(x, y);
                const F32 h = ALTerrainSurfaceMaps::smoothHeight(x, y, corner, nullptr, nullptr);
                INFO("plateau corner within its cell's range");
                REQUIRE(h >= 0.f);
                REQUIRE(h <= 30.f);
            }
        }
    }

    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: a field symmetric in x and y gives a surface symmetric in x and y", "[ALTerrainSurfaceMaps]")
    {
        auto field = [](S32 gx, S32 gy) { return (F32)(gx * gx + gy * gy) * 0.1f + (F32)((gx + gy) % 3); };
        for (F32 v = 0.f; v < 4.f; v += 0.37f)
        {
            CAPTURE(v);
            const F32 a = ALTerrainSurfaceMaps::smoothHeight(1.2f, v, field, nullptr, nullptr);
            const F32 b = ALTerrainSurfaceMaps::smoothHeight(v, 1.2f, field, nullptr, nullptr);
            REQUIRE_THAT(a, ll_test::WithinBits(b, 5));
        }
    }

    // The analytic gradient is the derivative of the value.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: the analytic gradient matches a central difference of the value", "[ALTerrainSurfaceMaps]")
    {
        auto field = [](S32 gx, S32 gy) { return sinf(0.7f * gx) * 2.f + cosf(0.4f * gy) + 0.1f * gx * gy; };
        const F32 eps = 1e-2f;
        for (F32 y = 0.3f; y < 4.f; y += 1.1f)
        {
            for (F32 x = 0.6f; x < 4.f; x += 0.9f)
            {
                CAPTURE(x, y);
                F32 dzdx, dzdy;
                ALTerrainSurfaceMaps::smoothHeight(x, y, field, &dzdx, &dzdy);
                const F32 fdx = (ALTerrainSurfaceMaps::smoothHeight(x + eps, y, field, nullptr, nullptr)
                               - ALTerrainSurfaceMaps::smoothHeight(x - eps, y, field, nullptr, nullptr)) / (2.f * eps);
                const F32 fdy = (ALTerrainSurfaceMaps::smoothHeight(x, y + eps, field, nullptr, nullptr)
                               - ALTerrainSurfaceMaps::smoothHeight(x, y - eps, field, nullptr, nullptr)) / (2.f * eps);
                REQUIRE_THAT(dzdx, ll_test::WithinBits(fdx, 8));
                REQUIRE_THAT(dzdy, ll_test::WithinBits(fdy, 8));
            }
        }
    }

    // fillRect rewrites the texels of its rectangle, cut to the map, and leaves every other texel as it was.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: fillRect rewrites its rectangle and nothing else", "[ALTerrainSurfaceMaps]")
    {
        std::vector<F32> out;
        ALTerrainSurfaceMaps::fill(out, GPE, 2, [](S32, S32, F32* t) { t[0] = -1.f; t[1] = -2.f; });

        // Reaches past the map's west and north edges.
        const ALTerrainSurfaceMaps::Rect rect{ -APRON - 3, 4, 1, GPE + APRON + 5 };
        S32 visits = 0;
        ALTerrainSurfaceMaps::fillRect(out, GPE, 2, rect, [&visits](S32 gx, S32 gy, F32* t) {
            ++visits;
            t[0] = (F32)(gx * 100 + gy);
        });
        UNSCOPED_INFO("the rectangle less what lies off the map: x -APRON..1, y 4..GPE+APRON-1");
        CHECK(visits == (1 + APRON + 1) * (GPE + APRON - 4));
        for (S32 gy = -APRON; gy < GPE + APRON; ++gy)
        {
            for (S32 gx = -APRON; gx < GPE + APRON; ++gx)
            {
                CAPTURE(gx, gy);
                const bool inside = gx <= 1 && gy >= 4;
                CHECK(out[texel(gx, gy, 2)] == (inside ? (F32)(gx * 100 + gy) : -1.f));
                UNSCOPED_INFO("the channel the sampler leaves alone");
                CHECK(out[texel(gx, gy, 2) + 1] == -2.f);
            }
        }

        S32 none = 0;
        ALTerrainSurfaceMaps::fillRect(out, GPE, 2, ALTerrainSurfaceMaps::Rect{ GPE + APRON, 0, GPE + APRON + 4, 3 },
                                       [&none](S32, S32, F32*) { ++none; });
        UNSCOPED_INFO("a rectangle wholly off the map visits nothing");
        CHECK(none == 0);
    }

    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: merge and intersect, with the empty rectangle", "[ALTerrainSurfaceMaps]")
    {
        using Rect = ALTerrainSurfaceMaps::Rect;
        const Rect a{ 0, 1, 4, 5 };
        const Rect b{ 3, -2, 7, 2 };
        const Rect m = ALTerrainSurfaceMaps::merge(a, b);
        CHECK((m.x0 == 0 && m.y0 == -2 && m.x1 == 7 && m.y1 == 5));
        const Rect i = ALTerrainSurfaceMaps::intersect(a, b);
        CHECK((i.x0 == 3 && i.y0 == 1 && i.x1 == 4 && i.y1 == 2));
        UNSCOPED_INFO("the default rectangle is empty");
        CHECK(Rect().empty());
        UNSCOPED_INFO("merging the empty rectangle changes nothing, either side");
        const Rect ma = ALTerrainSurfaceMaps::merge(Rect(), a);
        const Rect am = ALTerrainSurfaceMaps::merge(a, Rect());
        CHECK((ma.x0 == a.x0 && ma.y0 == a.y0 && ma.x1 == a.x1 && ma.y1 == a.y1));
        CHECK((am.x0 == a.x0 && am.y0 == a.y0 && am.x1 == a.x1 && am.y1 == a.y1));
        UNSCOPED_INFO("disjoint rectangles intersect to nothing");
        CHECK(ALTerrainSurfaceMaps::intersect(a, Rect{ 5, 0, 6, 9 }).empty());
        const Rect w = ALTerrainSurfaceMaps::wholeMap(GPE);
        CHECK((w.x0 == -APRON && w.y0 == -APRON && w.x1 == GPE + APRON - 1 && w.y1 == GPE + APRON - 1));
    }

    // toNeighbor undoes resolve: wherever a neighbour's map reads one of our grid points, that point moved into the
    // neighbour's grid is exactly the texel that reads it, and a point no texel of the neighbour reads lands on
    // nothing of its map.
    TEST_CASE_METHOD(alterrainsurfacemaps_data, "ALTerrainSurfaceMaps: toNeighbor lands each of our grid points on the neighbour texel that reads it", "[ALTerrainSurfaceMaps]")
    {
        bool has[8];
        allSet(true, has);
        for (U32 dir = 0; dir < 8; ++dir)
        {
            // Which of the neighbour's texels read each of our grid points (by our coordinate), from its resolve.
            std::vector<S32> readers(static_cast<size_t>(SIZE) * SIZE, 0);
            for (S32 hy = -APRON; hy < GPE + APRON; ++hy)
            {
                for (S32 hx = -APRON; hx < GPE + APRON; ++hx)
                {
                    S32 gx = hx, gy = hy;
                    if (ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has) != gDirOpposite[dir])
                    {
                        continue;
                    }
                    CAPTURE(dir, hx, hy, gx, gy);
                    REQUIRE((gx >= -APRON && gx < GPE + APRON && gy >= -APRON && gy < GPE + APRON));
                    ++readers[texel(gx, gy)];
                    const ALTerrainSurfaceMaps::Rect t =
                        ALTerrainSurfaceMaps::toNeighbor(ALTerrainSurfaceMaps::Rect{ gx, gy, gx, gy }, dir, GPE);
                    CHECK((t.x0 == hx && t.y0 == hy && t.x1 == hx && t.y1 == hy));
                }
            }
            // Our grid points no texel of the neighbour reads: inside the region and over two grids from the shared
            // border, they land off the neighbour's map.
            for (S32 gy = 0; gy < GPE; ++gy)
            {
                for (S32 gx = 0; gx < GPE; ++gx)
                {
                    if (readers[texel(gx, gy)] != 0)
                    {
                        continue;
                    }
                    const ALTerrainSurfaceMaps::Rect t =
                        ALTerrainSurfaceMaps::toNeighbor(ALTerrainSurfaceMaps::Rect{ gx, gy, gx, gy }, dir, GPE);
                    if (!t.empty())
                    {
                        // On the neighbour's map but read through us by none of its texels: a point of a border we
                        // share with it, whose texel reads the same sample from the neighbour's own grid or from
                        // the surface beside us both.
                        CAPTURE(dir, gx, gy, t.x0, t.y0);
                        const bool on_x = gDirAxes[dir][0] != 0 && gx == (gDirAxes[dir][0] > 0 ? GPE - 1 : 0);
                        const bool on_y = gDirAxes[dir][1] != 0 && gy == (gDirAxes[dir][1] > 0 ? GPE - 1 : 0);
                        CHECK((on_x || on_y));
                    }
                }
            }
        }
    }
}
