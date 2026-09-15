/**
 * @file alterrainsurfacemaps_test.cpp
 * @brief The terrain map layout: the apron offset, the row order, channel
 *        interleaving, and which neighbour a grid coordinate beyond the
 *        region resolves to.
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

#include "../test/lltut.h"

#include "../alterrainsurfacemaps.h"

#include <cmath>
#include <vector>

namespace tut
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

    typedef test_group<alterrainsurfacemaps_data> alterrainsurfacemaps_test;
    typedef alterrainsurfacemaps_test::object     alterrainsurfacemaps_object;
    tut::alterrainsurfacemaps_test alterrainsurfacemaps_testcase("ALTerrainSurfaceMaps");

    // fill() visits every grid coordinate from -APRON to gpe + APRON - 1 on
    // both axes, once, rows along y, and lands each at (gx + APRON, gy + APRON).
    template<> template<>
    void alterrainsurfacemaps_object::test<1>()
    {
        std::vector<F32> out;
        ALTerrainSurfaceMaps::fill(out, GPE, 1, [](S32 gx, S32 gy, F32* t) { t[0] = (F32)(gx * 100 + gy); });
        ensure_equals("size", out.size(), static_cast<size_t>(SIZE) * SIZE);
        ensure_equals("first texel is the south-west apron corner", out[texel(-APRON, -APRON)], (F32)(-APRON * 100 - APRON));
        ensure_equals("grid origin sits APRON in", out[texel(0, 0)], 0.f);
        ensure_equals("buffer column", out[texel(GPE - 1, 3)], (F32)((GPE - 1) * 100 + 3));
        ensure_equals("last texel is the north-east apron corner",
                      out[texel(GPE + APRON - 1, GPE + APRON - 1)], (F32)((GPE + APRON - 1) * 100 + GPE + APRON - 1));
        ensure_equals("row stride is the map width", out[texel(0, 1)] - out[texel(0, 0)], 1.f);
        ensure_equals("column stride is one texel", out[texel(1, 0)] - out[texel(0, 0)], 100.f);
    }

    // Two channels interleave, and a second fill of a buffer already the
    // right size keeps the channel the sampler does not write.
    template<> template<>
    void alterrainsurfacemaps_object::test<2>()
    {
        std::vector<F32> out;
        ALTerrainSurfaceMaps::fill(out, GPE, 2, [](S32 gx, S32 gy, F32* t) { t[0] = (F32)gx; t[1] = (F32)gy; });
        ensure_equals("size", out.size(), static_cast<size_t>(SIZE) * SIZE * 2);
        ensure_equals("channel 0", out[texel(3, 5, 2)], 3.f);
        ensure_equals("channel 1", out[texel(3, 5, 2) + 1], 5.f);

        ALTerrainSurfaceMaps::fill(out, GPE, 2, [](S32 gx, S32, F32* t) { t[0] = (F32)(gx + 1000); });
        ensure_equals("channel 0 rewritten", out[texel(3, 5, 2)], 1003.f);
        ensure_equals("channel 1 kept", out[texel(3, 5, 2) + 1], 5.f);
    }

    // Inside the region nothing moves.
    template<> template<>
    void alterrainsurfacemaps_object::test<3>()
    {
        bool has[8];
        allSet(true, has);
        S32 gx = 4, gy = GPE - 1;
        ensure_equals("interior is this surface", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), MIDDLE);
        ensure_equals("gx unchanged", gx, 4);
        ensure_equals("gy unchanged, the buffer row is ours", gy, GPE - 1);
    }

    // One axis out, neighbour present: the neighbour's grid 0 is our last
    // column, so our -1 is their gpe - 2 and our gpe is their 1.
    template<> template<>
    void alterrainsurfacemaps_object::test<4>()
    {
        bool has[8];
        allSet(true, has);
        S32 gx = -1, gy = 3;
        ensure_equals("west", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), WEST);
        ensure_equals("west gx", gx, GPE - 2);
        ensure_equals("west gy", gy, 3);

        gx = -APRON; gy = 3;
        ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has);
        ensure_equals("west, two out", gx, GPE - 3);

        gx = GPE; gy = 3;
        ensure_equals("east", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), EAST);
        ensure_equals("east gx", gx, 1);

        gx = GPE + 1; gy = 3;
        ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has);
        ensure_equals("east, two out", gx, 2);

        gx = 3; gy = -1;
        ensure_equals("south", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), SOUTH);
        ensure_equals("south gy", gy, GPE - 2);

        gx = 3; gy = GPE + 1;
        ensure_equals("north", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), NORTH);
        ensure_equals("north gy", gy, 2);
    }

    // Both axes out: the diagonal neighbour when it is there.
    template<> template<>
    void alterrainsurfacemaps_object::test<5>()
    {
        bool has[8];
        allSet(true, has);
        S32 gx = -1, gy = -APRON;
        ensure_equals("south-west", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), SOUTHWEST);
        ensure_equals("south-west gx", gx, GPE - 2);
        ensure_equals("south-west gy", gy, GPE - 3);

        gx = GPE + 1; gy = GPE;
        ensure_equals("north-east", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), NORTHEAST);
        ensure_equals("north-east gx", gx, 2);
        ensure_equals("north-east gy", gy, 1);
    }

    // A missing neighbour clamps the axis that needed it and keeps the rest of
    // the walk: no west means gx clamps to 0 and y still resolves north.
    template<> template<>
    void alterrainsurfacemaps_object::test<6>()
    {
        bool has[8];
        allSet(true, has);
        has[WEST] = false;
        S32 gx = -1, gy = 3;
        ensure_equals("no west: this surface", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), MIDDLE);
        ensure_equals("no west: gx clamped", gx, 0);

        gx = -1; gy = GPE;
        ensure_equals("no west, north present: north", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), NORTH);
        ensure_equals("north with gx clamped", gx, 0);
        ensure_equals("north gy", gy, 1);

        allSet(true, has);
        has[NORTHWEST] = false;
        gx = -1; gy = GPE;
        ensure_equals("west present, north-west missing: west", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), WEST);
        ensure_equals("west gx", gx, GPE - 2);
        ensure_equals("west gy clamped to its last row", gy, GPE - 1);

        allSet(false, has);
        gx = GPE + 1; gy = -1;
        ensure_equals("no neighbours: this surface", ALTerrainSurfaceMaps::resolve(gx, gy, GPE, has), MIDDLE);
        ensure_equals("gx clamped to the buffer column", gx, GPE - 1);
        ensure_equals("gy clamped to 0", gy, 0);
    }

    // The smooth surface passes through every sample: at an integer grid point the
    // value is the sample, whatever the neighbours do.
    template<> template<>
    void alterrainsurfacemaps_object::test<7>()
    {
        auto field = [](S32 gx, S32 gy) { return (F32)(gx * gx - 3 * gy + (gx * gy) % 5); };
        for (S32 gy = 0; gy < 6; ++gy)
        {
            for (S32 gx = 0; gx < 6; ++gx)
            {
                const F32 h = ALTerrainSurfaceMaps::smoothHeight((F32)gx, (F32)gy, field, nullptr, nullptr);
                ensure_equals("value at a sample is the sample", h, field(gx, gy));
            }
        }
    }

    // A step never grows a lip. A cliff -- level, then a thirty-metre drop in
    // one cell -- keeps the level ground level right up to the edge and the
    // drop inside the cell's range; a Catmull-Rom would put metres of bump
    // above the crease. And a monotone run stays monotone, so a ramp is a
    // ramp with no ripple ahead of or behind the step.
    template<> template<>
    void alterrainsurfacemaps_object::test<8>()
    {
        auto cliff = [](S32 gx, S32) { return gx <= 2 ? 30.f : 0.f; };
        for (F32 x = 0.f; x < 2.f; x += 0.125f)
        {
            F32 dzdx = 1.f, dzdy = 1.f;
            ensure_approximately_equals("level up to the edge", ALTerrainSurfaceMaps::smoothHeight(x, 3.f, cliff, &dzdx, &dzdy), 30.f, 12);
            // The tangent at the crease sample is zeroed, not averaged with the drop: the
            // level run has no slope at all, so it does not lean toward the edge.
            ensure_approximately_equals("no slope on the level run", dzdx, 0.f, 12);
        }
        F32 previous = 30.f;
        for (F32 x = 2.f; x <= 3.f; x += 0.0625f)
        {
            const F32 h = ALTerrainSurfaceMaps::smoothHeight(x, 3.f, cliff, nullptr, nullptr);
            ensure("inside the cell's range", h <= 30.f && h >= 0.f);
            ensure("monotone down the face", h <= previous);
            previous = h;
        }
        ensure_approximately_equals("mid-cell is the midpoint of the step", ALTerrainSurfaceMaps::smoothHeight(2.5f, 3.f, cliff, nullptr, nullptr), 15.f, 12);
        for (F32 x = 3.f; x < 5.f; x += 0.125f)
        {
            ensure_approximately_equals("level past the foot", ALTerrainSurfaceMaps::smoothHeight(x, 3.f, cliff, nullptr, nullptr), 0.f, 12);
        }

        // Off the axis too: a corner of the plateau, evaluated across the
        // cell that holds it, stays between its four corner heights.
        auto corner = [](S32 gx, S32 gy) { return (gx <= 2 && gy <= 2) ? 30.f : 0.f; };
        for (F32 y = 2.f; y <= 3.f; y += 0.25f)
        {
            for (F32 x = 2.f; x <= 3.f; x += 0.25f)
            {
                const F32 h = ALTerrainSurfaceMaps::smoothHeight(x, y, corner, nullptr, nullptr);
                ensure("plateau corner within its cell's range", h >= 0.f && h <= 30.f);
            }
        }
    }

    // A field symmetric in x and y gives a surface symmetric in x and y.
    template<> template<>
    void alterrainsurfacemaps_object::test<9>()
    {
        auto field = [](S32 gx, S32 gy) { return (F32)(gx * gx + gy * gy) * 0.1f + (F32)((gx + gy) % 3); };
        for (F32 v = 0.f; v < 4.f; v += 0.37f)
        {
            const F32 a = ALTerrainSurfaceMaps::smoothHeight(1.2f, v, field, nullptr, nullptr);
            const F32 b = ALTerrainSurfaceMaps::smoothHeight(v, 1.2f, field, nullptr, nullptr);
            ensure_approximately_equals("h(x, y) == h(y, x)", a, b, 5);
        }
    }

    // The analytic gradient is the derivative of the value.
    template<> template<>
    void alterrainsurfacemaps_object::test<10>()
    {
        auto field = [](S32 gx, S32 gy) { return sinf(0.7f * gx) * 2.f + cosf(0.4f * gy) + 0.1f * gx * gy; };
        const F32 eps = 1e-2f;
        for (F32 y = 0.3f; y < 4.f; y += 1.1f)
        {
            for (F32 x = 0.6f; x < 4.f; x += 0.9f)
            {
                F32 dzdx, dzdy;
                ALTerrainSurfaceMaps::smoothHeight(x, y, field, &dzdx, &dzdy);
                const F32 fdx = (ALTerrainSurfaceMaps::smoothHeight(x + eps, y, field, nullptr, nullptr)
                               - ALTerrainSurfaceMaps::smoothHeight(x - eps, y, field, nullptr, nullptr)) / (2.f * eps);
                const F32 fdy = (ALTerrainSurfaceMaps::smoothHeight(x, y + eps, field, nullptr, nullptr)
                               - ALTerrainSurfaceMaps::smoothHeight(x, y - eps, field, nullptr, nullptr)) / (2.f * eps);
                ensure_approximately_equals("dz/dx", dzdx, fdx, 8);
                ensure_approximately_equals("dz/dy", dzdy, fdy, 8);
            }
        }
    }
}
