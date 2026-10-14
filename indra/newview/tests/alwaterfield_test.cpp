/**
 * @file alwaterfield_test.cpp
 * @brief The water's layout tiles the plane to the rim, holds the loaded regions and the wave reach, and its wave
 *        phases give a point the same coordinates wherever the camera and the agent's region are.
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

#include "../alwaterfield.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
        constexpr F64 WIDTH = 256.0;
        constexpr F64 INFINITE_STRETCH = 256000.0;
        constexpr F64 FINITE_STRETCH = 2048.0;

        // A camera somewhere on a large grid, not on a cell edge.
        constexpr F64 EYE_X = 1000.0 * WIDTH + 97.3;
        constexpr F64 EYE_Y = 1031.0 * WIDTH + 201.9;

        bool inside(const ALWaterField::Rect& r, F64 x, F64 y)
        {
            return x >= r.mMinX && x < r.mMaxX && y >= r.mMinY && y < r.mMaxY;
        }

        // How many of the layout's quads, cells and rings together, hold a point given from the square's centre.
        S32 coverage(const ALWaterField::Layout& layout, F64 x, F64 y)
        {
            S32 count = 0;
            for (S32 j = 0; j < layout.side(); ++j)
            {
                for (S32 i = 0; i < layout.side(); ++i)
                {
                    count += inside(ALWaterField::cellRect(layout, i, j), x, y);
                }
            }
            for (S32 ring = 0; ring < layout.mRings; ++ring)
            {
                for (S32 quad = 0; quad < ALWaterField::RING_QUADS; ++quad)
                {
                    count += inside(ALWaterField::ringQuad(layout, ring, quad), x, y);
                }
            }
            return count;
        }

        // The difference of two wave coordinates on the circle the wrapping maps make of them.
        F64 wrapped(F64 a, F64 b)
        {
            const F64 d = a - b;
            return std::fabs(d - std::round(d));
        }

        F64 frac(F64 x)
        {
            return x - std::floor(x);
        }

        const LLVector2 WAVE1(0.53f, -0.31f);
        const LLVector2 WAVE2(-1.27f, 0.88f);
        constexpr F64 UPTIME = 123456.789;

        // A point's wave coordinates as the shaders work them: its position relative to the camera in float, the
        // phases added. Big xy, then little xy and zw.
        struct Coordinates
        {
            F64 v[6];
        };

        Coordinates shaded(const ALWaterField::WavePhases& phases, F32 rx, F32 ry)
        {
            const F32 s = 6.f * (cosf(0.08f * rx + phases.mSweep.mV[0]) + sinf(0.02f * ry + phases.mSweep.mV[1]));
            const F32 x = rx + s;
            return { { x * 0.04f + phases.mBig.mV[0], ry * 0.04f + phases.mBig.mV[1],
                       x * 0.45f + phases.mLittle.mV[0], ry * 0.9f + phases.mLittle.mV[1],
                       x * 0.1f + phases.mLittle.mV[2], ry * 0.2f + phases.mLittle.mV[3] } };
        }

        // The same coordinates worked directly from the point's global position in double, with the scrolls the water
        // pool gives the layers: the direction times half the uptime times the layer's rate.
        Coordinates direct(F64 px, F64 py)
        {
            const F64 t = UPTIME * 0.5;
            const F64 x = px + 6.0 * (std::cos(0.08 * px) + std::sin(0.02 * py));
            return { { frac(x * 0.04 + WAVE1.mV[0] * t * 0.055), frac(py * 0.04 + WAVE1.mV[1] * t * 0.055),
                       frac(x * 0.45 + WAVE2.mV[0] * t * 0.13), frac(py * 0.9 + WAVE2.mV[1] * t * 0.13),
                       frac(x * 0.1 + WAVE1.mV[0] * t * 0.1), frac(py * 0.2 + WAVE1.mV[1] * t * 0.1) } };
        }

    // The cells and rings cover every point inside the rim exactly once and nothing outside it, sampled at the scale of
    // the cells, of the inner rings and of the rim, for squares from the smallest to the largest and both stretches.
    TEST_CASE("alwaterfield: cells and rings tile the plane to the rim", "[alwaterfield]")
    {
        for (F64 stretch : { INFINITE_STRETCH, FINITE_STRETCH })
        {
            for (F32 draw_distance : { 64.f, 256.f, 1024.f, 2048.f, 3584.f })
            {
                const ALWaterField::Layout layout =
                    ALWaterField::layout(EYE_X, EYE_Y, WIDTH, draw_distance, stretch, {}, 0.0);
                const std::string what = "n " + std::to_string(layout.mCells) + " stretch " + std::to_string(stretch);
                for (F64 extent : { layout.mHalf * 1.2, layout.mHalf * 5.0, layout.mRim * 1.1 })
                {
                    constexpr S32 STEPS = 61;
                    for (S32 j = 0; j < STEPS; ++j)
                    {
                        for (S32 i = 0; i < STEPS; ++i)
                        {
                            // Off the quads' edges: an irrational fraction of a step.
                            const F64 x = -extent + (i + 0.4142135) * 2.0 * extent / STEPS;
                            const F64 y = -extent + (j + 0.7320508) * 2.0 * extent / STEPS;
                            const bool within = std::fabs(x) < layout.mRim && std::fabs(y) < layout.mRim;
                            UNSCOPED_INFO(what + " covers once inside the rim, never outside");
                            REQUIRE(coverage(layout, x, y) == (within ? 1 : 0));
                        }
                    }
                }
                for (S32 ring = 0; ring < layout.mRings; ++ring)
                {
                    for (S32 quad = 0; quad < ALWaterField::RING_QUADS; ++quad)
                    {
                        const ALWaterField::Rect r = ALWaterField::ringQuad(layout, ring, quad);
                        {
                            INFO(what + " no ring quad is empty");
                            REQUIRE(r.mMinX < r.mMaxX);
                            REQUIRE(r.mMinY < r.mMaxY);
                        }
                    }
                }
            }
        }
    }

    // The rim is the square's half-size plus the stretch, and the rings stop at the first that reaches it.
    TEST_CASE("alwaterfield: rim and ring count", "[alwaterfield]")
    {
        for (F64 stretch : { INFINITE_STRETCH, FINITE_STRETCH, 100.0 })
        {
            for (F32 draw_distance : { 64.f, 512.f, 2048.f })
            {
                const ALWaterField::Layout layout =
                    ALWaterField::layout(EYE_X, EYE_Y, WIDTH, draw_distance, stretch, {}, 0.0);
                UNSCOPED_INFO("half-size");
                REQUIRE(layout.mHalf == (layout.mCells + 0.5) * WIDTH);
                UNSCOPED_INFO("rim");
                REQUIRE(layout.mRim == layout.mHalf + stretch);
                UNSCOPED_INFO("the last ring reaches the rim");
                REQUIRE(std::ldexp(layout.mHalf, layout.mRings) >= layout.mRim);
                UNSCOPED_INFO("the one before it does not");
                REQUIRE((layout.mRings == 1 || std::ldexp(layout.mHalf, layout.mRings - 1) < layout.mRim));
            }
        }
        const ALWaterField::Layout seven = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 2048.f, INFINITE_STRETCH, {}, 0.0);
        UNSCOPED_INFO("2048 m takes nine cells each side of the centre");
        CHECK(seven.mCells == 9);
        UNSCOPED_INFO("a square nineteen across");
        CHECK(seven.side() == 19);
        UNSCOPED_INFO("and seven rings to the horizon");
        CHECK(seven.mRings == 7);
    }

    // The square is centred on the camera's cell and takes in the draw distance and every loaded region, a
    // variable-sized one by all its cells, until it reaches MAX_CELLS.
    TEST_CASE("alwaterfield: the square holds the draw distance and the loaded regions", "[alwaterfield]")
    {
        const S32 cx = ALWaterField::cellOf(EYE_X, WIDTH);
        const S32 cy = ALWaterField::cellOf(EYE_Y, WIDTH);

        ALWaterField::Layout layout = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, {}, 0.0);
        UNSCOPED_INFO("centre x");
        CHECK(layout.mCentreX == cx);
        UNSCOPED_INFO("centre y");
        CHECK(layout.mCentreY == cy);
        UNSCOPED_INFO("256 m and a cell");
        CHECK(layout.mCells == 2);
        UNSCOPED_INFO("a short draw distance keeps the smallest");
        CHECK(ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 32.f,
              INFINITE_STRETCH, {}, 0.0).mCells == ALWaterField::MIN_CELLS);
        UNSCOPED_INFO("257 m reaches a third");
        CHECK(ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 257.f, INFINITE_STRETCH, {}, 0.0).mCells == 3);

        // A 512 m region whose far corner is five cells east and four north.
        const std::vector<ALWaterField::Region> regions = { { cx + 4, cy + 3, 2, 21.f }, { cx - 1, cy, 1, 20.f } };
        layout = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, regions, 0.0);
        UNSCOPED_INFO("the far region's last cell");
        CHECK(layout.mCells == 5);

        const std::vector<ALWaterField::Region> distant = { { cx - 40, cy, 1, 20.f } };
        layout = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, distant, 0.0);
        UNSCOPED_INFO("held to MAX_CELLS");
        CHECK(layout.mCells == ALWaterField::MAX_CELLS);
        UNSCOPED_INFO("the square fits the cell map");
        CHECK(layout.side() <= ALWaterField::MAP_SIZE);
    }

    // With waves on, the patches are whole cells of the square, every cell whose nearest point is within the reach
    // along both axes and no other, and the reach ends before the square's edge, for every density, draw distance and
    // place in the centre cell.
    TEST_CASE("alwaterfield: the patches and their reach stay inside the square", "[alwaterfield]")
    {
        const F64 base_x = 1000.0 * WIDTH;
        const F64 base_y = 1031.0 * WIDTH;
        for (F32 density : { 8.f, 32.f, 64.f, 128.f, 256.f, 400.f })
        {
            for (F32 draw_distance : { 64.f, 256.f, 512.f, 1024.f, 2048.f })
            {
                for (F64 fx : { 0.0, 0.25, 0.999 })
                {
                    for (F64 fy : { 0.0, 0.5, 0.999 })
                    {
                        const F64 eye_x = base_x + fx * WIDTH;
                        const F64 eye_y = base_y + fy * WIDTH;
                        const ALWaterField::Layout layout = ALWaterField::layout(
                            eye_x, eye_y, WIDTH, draw_distance, INFINITE_STRETCH, {}, ALWaterField::tessReach(density));
                        const std::string what = "density " + std::to_string(density) + " draw " +
                                                 std::to_string(draw_distance);
                        UNSCOPED_INFO(what + " has a reach");
                        REQUIRE(layout.mReach > 0.0);
                        {
                            INFO(what + " patches inside the square");
                            REQUIRE(layout.mPatchMinX >= layout.mCentreX - layout.mCells);
                            REQUIRE(layout.mPatchMaxX <= layout.mCentreX + layout.mCells);
                            REQUIRE(layout.mPatchMinY >= layout.mCentreY - layout.mCells);
                            REQUIRE(layout.mPatchMaxY <= layout.mCentreY + layout.mCells);
                        }

                        // The nearest point of the square's edge to the camera.
                        const F64 sx = layout.centreX() - eye_x;
                        const F64 sy = layout.centreY() - eye_y;
                        const F64 edge = llmin(llmin(layout.mHalf - sx, layout.mHalf + sx),
                                               llmin(layout.mHalf - sy, layout.mHalf + sy));
                        UNSCOPED_INFO(what + " the reach's fade is 0 before the edge");
                        REQUIRE(layout.mReach < edge);

                        for (S32 c = layout.mCentreX - layout.mCells; c <= layout.mCentreX + layout.mCells; ++c)
                        {
                            const F64 lo = c * WIDTH;
                            const F64 nearest = llmax(0.0, llmax(lo - eye_x, eye_x - (lo + WIDTH)));
                            const bool patched = c >= layout.mPatchMinX && c <= layout.mPatchMaxX;
                            UNSCOPED_INFO(what + " column patched by its nearest point");
                            REQUIRE(patched == (nearest < layout.mReach));
                        }
                        for (S32 r = layout.mCentreY - layout.mCells; r <= layout.mCentreY + layout.mCells; ++r)
                        {
                            const F64 lo = r * WIDTH;
                            const F64 nearest = llmax(0.0, llmax(lo - eye_y, eye_y - (lo + WIDTH)));
                            const bool patched = r >= layout.mPatchMinY && r <= layout.mPatchMaxY;
                            UNSCOPED_INFO(what + " row patched by its nearest point");
                            REQUIRE(patched == (nearest < layout.mReach));
                        }
                    }
                }
            }
        }
    }

    // A denser tessellation reaches no less far.
    TEST_CASE("alwaterfield: the reach grows with the density", "[alwaterfield]")
    {
        F64 last = 0.0;
        F64 last_held = 0.0;
        for (F32 density = 1.f; density <= 512.f; density *= 1.25f)
        {
            const F64 reach = ALWaterField::tessReach(density);
            UNSCOPED_INFO("tessReach is monotone");
            REQUIRE(reach >= last);
            const F64 held =
                ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, reach).mReach;
            UNSCOPED_INFO("the held reach is monotone");
            REQUIRE(held >= last_held);
            last = reach;
            last_held = held;
        }
        UNSCOPED_INFO("half the big layer's 25 m at density 64");
        CHECK(ALWaterField::tessReach(64.f) == 800.0);
    }

    // The coordinates the shaders work from a camera-relative position and the phases are the point's own, worked in
    // double from its global position, for cameras across a large grid and points out to the wave clamp.
    TEST_CASE("alwaterfield: phases equal the direct evaluation", "[alwaterfield]")
    {
        const F64 tolerance[6] = { 1e-4, 1e-4, 2e-3, 2e-3, 5e-4, 5e-4 };
        for (F64 eye_x : { 97.3, EYE_X, 4000.0 * WIDTH + 31.7 })
        {
            for (F64 eye_y : { 201.9, EYE_Y, 3900.0 * WIDTH + 3.1 })
            {
                const ALWaterField::WavePhases phases =
                    ALWaterField::wavePhases(eye_x, eye_y, UPTIME, WAVE1, WAVE2);
                for (F64 rx : { -2560.0, -731.25, 0.0, 17.5, 2049.0 })
                {
                    for (F64 ry : { -1999.0, -3.5, 0.0, 640.0, 2560.0 })
                    {
                        const Coordinates a = shaded(phases, (F32)rx, (F32)ry);
                        const Coordinates b = direct(eye_x + rx, eye_y + ry);
                        for (S32 k = 0; k < 6; ++k)
                        {
                            UNSCOPED_INFO("layer coordinate " + std::to_string(k) + " at " + std::to_string(rx) + ", " +
                                          std::to_string(ry));
                            REQUIRE(wrapped(a.v[k], b.v[k]) < tolerance[k]);
                        }
                    }
                }
            }
        }

        // The scrolls alone, a layer's direction times half the uptime times its rate, worked here in double.
        const ALWaterField::WaveScrolls scrolls = ALWaterField::waveScrolls(UPTIME, WAVE1, WAVE2);
        const F64 t = UPTIME * 0.5;
        const F64 expected[6] = { frac(WAVE1.mV[0] * t * 0.055), frac(WAVE1.mV[1] * t * 0.055),
                                  frac(WAVE2.mV[0] * t * 0.13),  frac(WAVE2.mV[1] * t * 0.13),
                                  frac(WAVE1.mV[0] * t * 0.1),   frac(WAVE1.mV[1] * t * 0.1) };
        const F32 got[6] = { scrolls.mBig.mV[0], scrolls.mBig.mV[1], scrolls.mLittle.mV[0], scrolls.mLittle.mV[1],
                             scrolls.mLittle.mV[2], scrolls.mLittle.mV[3] };
        for (S32 k = 0; k < 6; ++k)
        {
            UNSCOPED_INFO("scroll " + std::to_string(k));
            REQUIRE(wrapped(got[k], expected[k]) < 1e-6);
        }
    }

    // A fixed point keeps its coordinates as the camera moves a kilometre, the phases taken from the camera's global
    // position.
    TEST_CASE("alwaterfield: a point keeps its coordinates across a region crossing", "[alwaterfield]")
    {
        const F64 px = EYE_X + 140.0;
        const F64 py = EYE_Y - 60.0;
        const ALWaterField::WavePhases here = ALWaterField::wavePhases(EYE_X, EYE_Y, UPTIME, WAVE1, WAVE2);
        const ALWaterField::WavePhases there =
            ALWaterField::wavePhases(EYE_X + 1000.0, EYE_Y, UPTIME, WAVE1, WAVE2);
        const Coordinates a = shaded(here, (F32)(px - EYE_X), (F32)(py - EYE_Y));
        const Coordinates b = shaded(there, (F32)(px - EYE_X - 1000.0), (F32)(py - EYE_Y));
        for (S32 k = 0; k < 6; ++k)
        {
            UNSCOPED_INFO("layer " + std::to_string(k) + " unchanged as the camera moves");
            REQUIRE(wrapped(a.v[k], b.v[k]) < 2e-3);
        }
    }

    // The cell map holds each cell's region height with its flag, a variable-sized region in every cell it covers, and
    // the agent region's height with no flag where no region is; the camera's cell is texel (n, n).
    TEST_CASE("alwaterfield: the cell map", "[alwaterfield]")
    {
        const S32 cx = ALWaterField::cellOf(EYE_X, WIDTH);
        const S32 cy = ALWaterField::cellOf(EYE_Y, WIDTH);
        const std::vector<ALWaterField::Region> regions = { { cx, cy, 1, 21.5f }, { cx + 1, cy - 2, 2, 30.f } };
        const ALWaterField::Layout layout =
            ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, regions, 0.0);
        std::vector<F32> texels;
        ALWaterField::fillCells(layout, regions, 19.f, texels);
        UNSCOPED_INFO("one texel of two channels a cell of the map");
        REQUIRE(texels.size() == (size_t)ALWaterField::MAP_SIZE * ALWaterField::MAP_SIZE * 2);

        const auto at = [&](S32 dx, S32 dy)
        {
            const S32 i = layout.mCells + dx;
            const S32 j = layout.mCells + dy;
            const F32* t = texels.data() + ((size_t)j * ALWaterField::MAP_SIZE + i) * 2;
            return std::make_pair(t[0], t[1]);
        };
        UNSCOPED_INFO("the camera's cell's height");
        CHECK(at(0, 0).first == 21.5f);
        UNSCOPED_INFO("the camera's cell is a region");
        CHECK(at(0, 0).second == 1.f);
        for (S32 dx : { 1, 2 })
        {
            for (S32 dy : { -2, -1 })
            {
                UNSCOPED_INFO("the 512 m region covers its four cells");
                REQUIRE(at(dx, dy).first == 30.f);
                UNSCOPED_INFO("each flagged");
                REQUIRE(at(dx, dy).second == 1.f);
            }
        }
        UNSCOPED_INFO("a cell with no region takes the agent's height");
        CHECK(at(-1, 1).first == 19.f);
        UNSCOPED_INFO("and no flag");
        CHECK(at(-1, 1).second == 0.f);
        UNSCOPED_INFO("past the 512 m region");
        CHECK(at(3, -2).first == 19.f);
    }

    namespace
    {
        // The tiled layers' scales, as waterSurface.glsl's water_wave_normals scales a point for each lattice.
        constexpr F64 LAYER_SCALE[2][2] = { { ALWaterField::BIG_SCALE, ALWaterField::BIG_SCALE },
                                            { ALWaterField::LITTLE_ZW_SCALE[0], ALWaterField::LITTLE_ZW_SCALE[1] } };

        // A point's skewed lattice coordinates on a layer, in double from its global position.
        void latticeCoords(S32 layer, F64 x, F64 y, F64& qx, F64& qy)
        {
            const F64 px = x * LAYER_SCALE[layer][0];
            const F64 py = y * LAYER_SCALE[layer][1];
            qx = ALWaterField::HEX_GRID * (px + ALWaterField::HEX_SKEW_XY * py);
            qy = ALWaterField::HEX_GRID * (ALWaterField::HEX_SKEW_YY * py);
        }

        // The cell id water_hex_cells works for a point `r` from the camera, in float as the shader does: the camera's
        // place from the CPU, and the skewed offset of the point added to its fraction.
        std::pair<U32, U32> shaderCell(const ALWaterField::LatticePlaces& places, S32 layer, F32 rx, F32 ry)
        {
            const F32 px = rx * (F32)LAYER_SCALE[layer][0] * (F32)ALWaterField::HEX_GRID;
            const F32 py = ry * (F32)LAYER_SCALE[layer][1] * (F32)ALWaterField::HEX_GRID;
            const F32 qx = places.mFrac.mV[2 * layer] + (px - 0.57735027f * py);
            const F32 qy = places.mFrac.mV[2 * layer + 1] + 1.15470054f * py;
            return { (U32)places.mBase[2 * layer] + (U32)(S32)std::floor(qx),
                     (U32)places.mBase[2 * layer + 1] + (U32)(S32)std::floor(qy) };
        }

        // The cell holding a skewed coordinate, its floor's low 32 bits; false within `margin` of a cell's edge, where
        // float rounding may land either side.
        bool trueCell(F64 q, F64 margin, U32& cell)
        {
            const F64 whole = std::floor(q);
            cell = (U32)(U64)(S64)whole;
            return q - whole > margin && whole + 1.0 - q > margin;
        }
    }

    // A point of the sea keeps its lattice cell however far the camera goes: the cell the shader works from the
    // camera's place and the point's offset is the cell its global position is in, from a camera beside it and from one
    // 10 km off, at the grid's far corner, and where the 32-bit ids wrap. A camera's place split in float misses at the
    // far corner.
    TEST_CASE("alwaterfield: the tiled layers' lattice", "[alwaterfield]")
    {
        // Far corner: a million regions out. Wrap: the big layer's skewed y crosses 2^32 among the points.
        const F64 far_corner = 1048576.0 * WIDTH;
        const F64 wrap_y = 4294967296.0 / (ALWaterField::BIG_SCALE * ALWaterField::HEX_SKEW_YY * ALWaterField::HEX_GRID);
        const F64 eyes[3][2] = { { EYE_X, EYE_Y }, { far_corner + 97.3, far_corner + 201.9 }, { EYE_X, wrap_y + 3.1 } };
        S32 checked = 0;
        S32 wrapped = 0;
        for (const auto& eye : eyes)
        {
            const ALWaterField::LatticePlaces near_places = ALWaterField::latticePlaces(eye[0], eye[1]);
            const ALWaterField::LatticePlaces far_places = ALWaterField::latticePlaces(eye[0] + 7071.0, eye[1] - 7071.0);
            for (S32 layer = 0; layer < 2; ++layer)
            {
                for (S32 i = -20; i <= 20; ++i)
                {
                    for (S32 j = -20; j <= 20; ++j)
                    {
                        const F64 x = eye[0] + 4.9 * i;
                        const F64 y = eye[1] + 4.7 * j;
                        F64 qx = 0.0;
                        F64 qy = 0.0;
                        latticeCoords(layer, x, y, qx, qy);
                        U32 cx = 0;
                        U32 cy = 0;
                        if (!trueCell(qx, 1e-3, cx) || !trueCell(qy, 1e-3, cy))
                        {
                            continue;
                        }
                        const std::pair<U32, U32> from_near = shaderCell(near_places, layer, (F32)(x - eye[0]), (F32)(y - eye[1]));
                        const std::pair<U32, U32> from_far =
                            shaderCell(far_places, layer, (F32)(x - eye[0] - 7071.0), (F32)(y - eye[1] + 7071.0));
                        UNSCOPED_INFO("the cell beside the camera");
                        REQUIRE(from_near.first == cx);
                        UNSCOPED_INFO("the cell beside the camera, y");
                        REQUIRE(from_near.second == cy);
                        UNSCOPED_INFO("the cell from 10 km off");
                        REQUIRE(from_far.first == cx);
                        UNSCOPED_INFO("the cell from 10 km off, y");
                        REQUIRE(from_far.second == cy);
                        ++checked;
                        wrapped += cy < 16 ? 1 : 0;
                    }
                }
            }
        }
        UNSCOPED_INFO("most points are away from a cell's edge");
        CHECK(checked > 5000);
        UNSCOPED_INFO("the wrap's points include ids past it");
        CHECK(wrapped > 0);

        const ALWaterField::LatticePlaces places = ALWaterField::latticePlaces(-EYE_X, EYE_Y);
        for (F32 f : { places.mFrac.mV[0], places.mFrac.mV[1], places.mFrac.mV[2], places.mFrac.mV[3] })
        {
            {
                INFO("a fraction in [0, 1)");
                REQUIRE(f >= 0.f);
                REQUIRE(f < 1.f);
            }
        }
    }

    // A normal map's mean decoded slope: the first two channels over every pixel, and nothing without three.
    TEST_CASE("alwaterfield: a normal map's mean", "[alwaterfield]")
    {
        const U8 rgba[4 * 4] = { 255, 0, 255, 9, 0, 0, 255, 9, 255, 255, 255, 9, 255, 255, 255, 9 };
        const LLVector2 mean = ALWaterField::meanNormal(rgba, 2, 2, 4);
        UNSCOPED_INFO("x: three of four at 1");
        CHECK_THAT(mean.mV[VX], ll_test::WithinBits(0.5f, 20));
        UNSCOPED_INFO("y: two of four at 1");
        CHECK_THAT(mean.mV[VY], ll_test::WithinBits(0.f, 20));
        const U8 rgb[3] = { 128, 64, 255 };
        const LLVector2 one = ALWaterField::meanNormal(rgb, 1, 1, 3);
        UNSCOPED_INFO("one pixel, x");
        CHECK_THAT(one.mV[VX], ll_test::WithinBits(128.f / 255.f * 2.f - 1.f, 20));
        UNSCOPED_INFO("one pixel, y");
        CHECK_THAT(one.mV[VY], ll_test::WithinBits(64.f / 255.f * 2.f - 1.f, 20));
        UNSCOPED_INFO("two channels have no mean");
        CHECK(ALWaterField::meanNormal(rgb, 1, 1, 2) == LLVector2::zero);
        UNSCOPED_INFO("no pixels have no mean");
        REQUIRE(ALWaterField::meanNormal(nullptr, 4, 4, 3) == LLVector2::zero);
    }

    // The square's corner the GPU places the cells from, relative to an origin (the agent region's): the corner's global
    // position less the origin, near and far from the grid's origin and with the origin a region off the camera's.
    TEST_CASE("alwaterfield: the cell square relative to the agent region", "[alwaterfield]")
    {
        const F64 eyes[3][2] = { { EYE_X, EYE_Y }, { 97.3, 201.9 }, { 1048576.0 * WIDTH + 97.3, 1048000.0 * WIDTH + 3.1 } };
        for (const auto& eye : eyes)
        {
            const ALWaterField::Layout layout = ALWaterField::layout(eye[0], eye[1], WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0);
            for (F64 off : { 0.0, WIDTH, -3.0 * WIDTH })
            {
                // The agent region's south-west corner: the camera's cell's, moved `off`.
                const F64 origin_x = layout.mCentreX * WIDTH + off;
                const F64 origin_y = layout.mCentreY * WIDTH - off;
                const LLVector4 square = ALWaterField::cellSquare(layout, origin_x, origin_y);
                UNSCOPED_INFO("x from the origin");
                REQUIRE((F64)square.mV[VX] == (F64)(layout.mCentreX - layout.mCells) * WIDTH - origin_x);
                UNSCOPED_INFO("y from the origin");
                REQUIRE((F64)square.mV[VY] == (F64)(layout.mCentreY - layout.mCells) * WIDTH - origin_y);
                UNSCOPED_INFO("a cell's width");
                REQUIRE((F64)square.mV[VZ] == WIDTH);
                UNSCOPED_INFO("the cells a side");
                REQUIRE((S32)square.mV[VW] == layout.side());
            }
        }
    }

    // Negative global coordinates: the cell holding a point is its floor's, and the square, its patches and the camera's
    // cell work as they do anywhere.
    TEST_CASE("alwaterfield: negative coordinates", "[alwaterfield]")
    {
        UNSCOPED_INFO("just below 0");
        CHECK(ALWaterField::cellOf(-0.5, WIDTH) == -1);
        UNSCOPED_INFO("a whole cell below");
        CHECK(ALWaterField::cellOf(-WIDTH, WIDTH) == -1);
        UNSCOPED_INFO("past it");
        CHECK(ALWaterField::cellOf(-WIDTH - 0.5, WIDTH) == -2);
        const F64 eye_x = -5.0 * WIDTH + 30.0;
        const F64 eye_y = -0.25;
        const ALWaterField::Layout layout =
            ALWaterField::layout(eye_x, eye_y, WIDTH, 256.f, INFINITE_STRETCH, {}, ALWaterField::tessReach(16.f));
        UNSCOPED_INFO("centre x");
        CHECK(layout.mCentreX == -5);
        UNSCOPED_INFO("centre y");
        CHECK(layout.mCentreY == -1);
        {
            INFO("the camera lies in the centre cell");
            CHECK(eye_x >= layout.mCentreX * WIDTH);
            CHECK(eye_x < (layout.mCentreX + 1) * WIDTH);
        }
        {
            INFO("and its patches");
            CHECK(layout.mPatchMinX <= layout.mCentreX);
            CHECK(layout.mPatchMaxX >= layout.mCentreX);
            CHECK(layout.mPatchMinY <= layout.mCentreY);
            CHECK(layout.mPatchMaxY >= layout.mCentreY);
        }
        const F64 lo = layout.mPatchMinX * WIDTH;
        UNSCOPED_INFO("the west patch column's nearest point within the reach");
        CHECK(eye_x - (lo + WIDTH) < layout.mReach);
        UNSCOPED_INFO("and the one past it not");
        CHECK(eye_x - lo >= layout.mReach);
    }

    namespace
    {
        // The cell map as one region at a time would fill it: every texel of the map, the first region listed that
        // holds its cell, or the default.
        std::vector<F32> bruteCells(const ALWaterField::Layout& layout, const std::vector<ALWaterField::Region>& regions,
                                    F32 default_height)
        {
            std::vector<F32> texels((size_t)ALWaterField::MAP_SIZE * ALWaterField::MAP_SIZE * 2);
            for (S32 j = 0; j < ALWaterField::MAP_SIZE; ++j)
            {
                for (S32 i = 0; i < ALWaterField::MAP_SIZE; ++i)
                {
                    const S32 cx = layout.mCentreX - layout.mCells + i;
                    const S32 cy = layout.mCentreY - layout.mCells + j;
                    F32* t = texels.data() + ((size_t)j * ALWaterField::MAP_SIZE + i) * 2;
                    t[0] = default_height;
                    t[1] = 0.f;
                    for (const ALWaterField::Region& region : regions)
                    {
                        if (cx >= region.mCellX && cx < region.mCellX + region.mCells && cy >= region.mCellY &&
                            cy < region.mCellY + region.mCells)
                        {
                            t[0] = region.mWaterHeight;
                            t[1] = 1.f;
                            break;
                        }
                    }
                }
            }
            return texels;
        }
    }

    // The cell map, region by region: the same as asking every texel of every region; at the largest square; with a
    // region straddling the square's edge, one past the map, two claiming a cell, the first listed holding it; and the
    // texels past the square filled the same way.
    TEST_CASE("alwaterfield: the cell map, region by region", "[alwaterfield]")
    {
        const S32 cx = ALWaterField::cellOf(EYE_X, WIDTH);
        const S32 cy = ALWaterField::cellOf(EYE_Y, WIDTH);
        const std::vector<ALWaterField::Region> regions = {
            { cx, cy, 1, 21.5f },
            { cx + 14, cy - 2, 4, 30.f },  // straddles the east edge of the largest square, and past the map
            { cx - 16, cy + 10, 2, 12.f }, // straddles the west edge
            { cx, cy, 2, 99.f },           // claims the camera's cell after the first
            { cx - 60, cy - 60, 1, 7.f },  // far off the map
        };
        const ALWaterField::Layout layout =
            ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, regions, 0.0);
        UNSCOPED_INFO("the largest square");
        CHECK(layout.mCells == ALWaterField::MAX_CELLS);
        std::vector<F32> texels;
        ALWaterField::fillCells(layout, regions, 19.f, texels);
        const std::vector<F32> brute = bruteCells(layout, regions, 19.f);
        UNSCOPED_INFO("as every texel asking every region");
        CHECK(texels == brute);

        const auto at = [&](S32 i, S32 j) { return texels[((size_t)j * ALWaterField::MAP_SIZE + i) * 2]; };
        const S32 n = layout.mCells;
        UNSCOPED_INFO("the first listed holds the camera's cell");
        CHECK(at(n, n) == 21.5f);
        UNSCOPED_INFO("the second region of two holds its own");
        CHECK(at(n + 1, n + 1) == 99.f);
        UNSCOPED_INFO("the straddling region inside the square");
        CHECK(at(2 * n, n - 2) == 30.f);
        UNSCOPED_INFO("and past the square, in the map");
        CHECK(at(2 * n + 1, n - 2) == 30.f);
        UNSCOPED_INFO("the west straddler's cell inside");
        CHECK(at(0, n + 10) == 12.f);

        // Random squares and regions, against the same.
        U32 seed = 12345u;
        const auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        for (S32 trial = 0; trial < 200; ++trial)
        {
            std::vector<ALWaterField::Region> random;
            const S32 count = (S32)(next() % 12);
            for (S32 r = 0; r < count; ++r)
            {
                random.push_back({ cx + (S32)(next() % 50) - 25, cy + (S32)(next() % 50) - 25, 1 + (S32)(next() % 4),
                                   (F32)(next() % 100) });
            }
            const ALWaterField::Layout l = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, (F32)(next() % 4000), INFINITE_STRETCH, random, 0.0);
            ALWaterField::fillCells(l, random, 19.f, texels);
            UNSCOPED_INFO("trial " + std::to_string(trial));
            REQUIRE(texels == bruteCells(l, random, 19.f));
        }
    }

    // Moving within its cell moves nothing of the square: the cell map's key, the square, stays the same however far
    // across the cell the camera goes.
    TEST_CASE("alwaterfield: the square stays as the camera moves within its cell", "[alwaterfield]")
    {
        const F64 base_x = 1000.0 * WIDTH;
        const F64 base_y = 1031.0 * WIDTH;
        const ALWaterField::Layout first = ALWaterField::layout(base_x, base_y, WIDTH, 1024.f, INFINITE_STRETCH, {}, 0.0);
        for (F64 fx : { 0.0, 0.3, 0.999 })
        {
            for (F64 fy : { 0.0, 0.61, 0.999 })
            {
                const ALWaterField::Layout l =
                    ALWaterField::layout(base_x + fx * WIDTH, base_y + fy * WIDTH, WIDTH, 1024.f, INFINITE_STRETCH, {}, 0.0);
                UNSCOPED_INFO("the same layout");
                REQUIRE(l == first);
            }
        }
        const ALWaterField::Layout next = ALWaterField::layout(base_x + WIDTH, base_y, WIDTH, 1024.f, INFINITE_STRETCH, {}, 0.0);
        UNSCOPED_INFO("a cell east recentres it");
        CHECK(next.mCentreX == first.mCentreX + 1);
    }

    // The patches' rectangle in the cell map's cells, its end past the last; empty without waves. And the order the
    // patches run in: along the axis the camera looks along most, from the end behind it.
    TEST_CASE("alwaterfield: the patch rectangle and order", "[alwaterfield]")
    {
        const ALWaterField::Layout flat = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0);
        UNSCOPED_INFO("no waves, no rectangle");
        REQUIRE(ALWaterField::patchRect(flat).empty());
        const ALWaterField::Layout waved =
            ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, ALWaterField::tessReach(64.f));
        const ALWaterField::PatchRect rect = ALWaterField::patchRect(waved);
        const S32 west = waved.mCentreX - waved.mCells;
        const S32 south = waved.mCentreY - waved.mCells;
        UNSCOPED_INFO("min x");
        CHECK(rect.mMinX == waved.mPatchMinX - west);
        UNSCOPED_INFO("min y");
        CHECK(rect.mMinY == waved.mPatchMinY - south);
        UNSCOPED_INFO("end x, past the last");
        CHECK(rect.mEndX == waved.mPatchMaxX - west + 1);
        UNSCOPED_INFO("end y, past the last");
        CHECK(rect.mEndY == waved.mPatchMaxY - south + 1);
        {
            INFO("a cell of the square all round it");
            CHECK(rect.mMinX >= 1);
            CHECK(rect.mMinY >= 1);
            CHECK(rect.mEndX <= waved.side() - 1);
            CHECK(rect.mEndY <= waved.side() - 1);
        }
        {
            INFO("the camera's cell among them");
            CHECK(rect.mMinX <= waved.mCells);
            CHECK(rect.mEndX > waved.mCells);
            CHECK(rect.mMinY <= waved.mCells);
            CHECK(rect.mEndY > waved.mCells);
        }

        const S32 east[4] = { 1, 0, 0, 0 };
        const S32 north_west[4] = { 0, 0, 1, 0 };
        const S32 south_east[4] = { 0, 1, 0, 0 };
        const S32 west_south[4] = { 1, 1, 1, 0 };
        const auto same = [](const ALWaterField::PatchOrder& order, const S32 (&expected)[4])
        {
            return order.mOrder[0] == expected[0] && order.mOrder[1] == expected[1] && order.mOrder[2] == expected[2] &&
                   order.mOrder[3] == expected[3];
        };
        UNSCOPED_INFO("looking east: x slow, from the west, y from the south");
        CHECK(same(ALWaterField::patchOrder(1.f, 0.2f), east));
        UNSCOPED_INFO("looking north, a little west: y slow, from the south, x from the east");
        CHECK(same(ALWaterField::patchOrder(-0.3f, 0.9f), north_west));
        UNSCOPED_INFO("looking south, a little east: y slow, from the north");
        CHECK(same(ALWaterField::patchOrder(0.2f, -0.9f), south_east));
        UNSCOPED_INFO("looking west, a little south: x slow, from the east, y from the north");
        CHECK(same(ALWaterField::patchOrder(-0.9f, -0.3f), west_south));
    }

    // The reach across the plane of a camera above it: every point of the plane within the reach of the camera itself,
    // and no other, lies within it of the point below; none at or past the reach's height.
    TEST_CASE("alwaterfield: the reach across the plane from a height", "[alwaterfield]")
    {
        UNSCOPED_INFO("on the plane, the whole reach");
        CHECK(ALWaterField::reachAcross(800.0, 0.0) == 800.0);
        UNSCOPED_INFO("at the reach's height, none");
        CHECK(ALWaterField::reachAcross(800.0, 800.0) == 0.0);
        UNSCOPED_INFO("above it, none");
        CHECK(ALWaterField::reachAcross(800.0, 3000.0) == 0.0);
        UNSCOPED_INFO("below the plane as above it");
        CHECK(ALWaterField::reachAcross(800.0, -600.0) == ALWaterField::reachAcross(800.0, 600.0));
        UNSCOPED_INFO("no reach, none");
        CHECK(ALWaterField::reachAcross(0.0, 0.0) == 0.0);
        for (F64 h : { 0.0, 50.0, 500.0, 799.0 })
        {
            const F64 across = ALWaterField::reachAcross(800.0, h);
            UNSCOPED_INFO("a point that far across is the reach from the camera");
            REQUIRE(std::fabs(std::sqrt(across * across + h * h) - 800.0) < 1e-9);
        }
        // A camera 2 km up lays out no patches; at 300 m, fewer than at the water.
        const ALWaterField::Layout high = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {},
                                                               ALWaterField::reachAcross(ALWaterField::tessReach(64.f), 2000.0));
        UNSCOPED_INFO("none from 2 km up");
        REQUIRE(ALWaterField::patchRect(high).empty());
        const ALWaterField::Layout low = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {},
                                                              ALWaterField::reachAcross(ALWaterField::tessReach(64.f), 0.0));
        const ALWaterField::Layout mid = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {},
                                                              ALWaterField::reachAcross(ALWaterField::tessReach(64.f), 700.0));
        {
            INFO("fewer from 700 m");
            CHECK(mid.mReach < low.mReach);
            CHECK(mid.mPatchMaxX - mid.mPatchMinX < low.mPatchMaxX - low.mPatchMinX);
        }
    }

    // A camera at no finite place, or cells of no width, lay out nothing; a camera far past any grid is held at the
    // cells' limit; a stretch past any rim takes no more than the most rings; a draw distance or reach that is not a
    // number takes none.
    TEST_CASE("alwaterfield: what the layout will not take", "[alwaterfield]")
    {
        const F64 nan = std::numeric_limits<F64>::quiet_NaN();
        const F64 inf = std::numeric_limits<F64>::infinity();
        UNSCOPED_INFO("a NaN camera");
        CHECK(ALWaterField::layout(nan, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0).mCells == 0);
        UNSCOPED_INFO("an infinite one");
        CHECK(ALWaterField::layout(EYE_X, -inf, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0).mCells == 0);
        UNSCOPED_INFO("no width");
        CHECK(ALWaterField::layout(EYE_X, EYE_Y, 0.0, 512.f, INFINITE_STRETCH, {}, 0.0).mCells == 0);
        UNSCOPED_INFO("a negative one");
        CHECK(ALWaterField::layout(EYE_X, EYE_Y, -WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0).mCells == 0);
        UNSCOPED_INFO("nothing to draw for it");
        CHECK(ALWaterField::fieldDraws(ALWaterField::Layout(), ALWaterField::PatchRect()).mCount == 0);

        const ALWaterField::Layout distant = ALWaterField::layout(1e300, -1e300, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0);
        UNSCOPED_INFO("far east, at the limit");
        CHECK(distant.mCentreX == ALWaterField::CELL_LIMIT);
        UNSCOPED_INFO("far south, at the limit");
        CHECK(distant.mCentreY == -ALWaterField::CELL_LIMIT);
        UNSCOPED_INFO("a cellOf past the limit");
        CHECK(ALWaterField::cellOf(1e300, WIDTH) == ALWaterField::CELL_LIMIT);
        UNSCOPED_INFO("a cellOf of NaN");
        CHECK(ALWaterField::cellOf(nan, WIDTH) == 0);

        const ALWaterField::Layout wide = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, 1e300, {}, 0.0);
        {
            INFO("a finite number of rings for any stretch");
            CHECK(wide.mRings > 0);
            CHECK(wide.mRings <= 40);
        }
        const ALWaterField::Layout odd = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, std::numeric_limits<F32>::quiet_NaN(),
                                                              INFINITE_STRETCH, {}, nan);
        UNSCOPED_INFO("a NaN draw distance takes the fewest cells");
        CHECK(odd.mCells == ALWaterField::MIN_CELLS);
        UNSCOPED_INFO("and a NaN reach none");
        CHECK(odd.mReach == 0.0);
    }

    // The field's draws: the cells, the rings at three triangles a quad, ring 0's four sides at a triangle a cell along
    // the square and two more, and the fringe, only with patches, a cell along each side of their rectangle.
    TEST_CASE("alwaterfield: the field's draws", "[alwaterfield]")
    {
        const ALWaterField::Layout flat = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0);
        const ALWaterField::FieldDraws draws = ALWaterField::fieldDraws(flat, ALWaterField::PatchRect());
        UNSCOPED_INFO("three without patches");
        CHECK(draws.mCount == 3);
        UNSCOPED_INFO("the cells");
        CHECK(draws.mDraws[0].mPart == ALWaterField::Draw::CELLS);
        {
            INFO("as strips");
            CHECK(draws.mDraws[0].mStrip);
            CHECK(draws.mDraws[0].mVertices == 4);
        }
        UNSCOPED_INFO("a cell an instance");
        CHECK(draws.mDraws[0].mInstances == flat.side() * flat.side());
        UNSCOPED_INFO("the rings");
        CHECK(draws.mDraws[1].mPart == ALWaterField::Draw::RINGS);
        {
            INFO("three triangles a quad");
            CHECK(!draws.mDraws[1].mStrip);
            CHECK(draws.mDraws[1].mVertices == 9);
        }
        UNSCOPED_INFO("twelve quads a ring");
        CHECK(draws.mDraws[1].mInstances == ALWaterField::RING_QUADS * flat.mRings);
        UNSCOPED_INFO("the zippers");
        CHECK(draws.mDraws[2].mPart == ALWaterField::Draw::ZIPPERS);
        UNSCOPED_INFO("four of them");
        CHECK(draws.mDraws[2].mInstances == 4);
        UNSCOPED_INFO("a triangle a cell along the side and two");
        CHECK(draws.mDraws[2].mVertices == 3 * (flat.side() + 2));

        const ALWaterField::Layout waved =
            ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, ALWaterField::tessReach(64.f));
        const ALWaterField::PatchRect rect = ALWaterField::patchRect(waved);
        const ALWaterField::FieldDraws fringed = ALWaterField::fieldDraws(waved, rect);
        UNSCOPED_INFO("four with patches");
        CHECK(fringed.mCount == 4);
        UNSCOPED_INFO("the fringe");
        CHECK(fringed.mDraws[3].mPart == ALWaterField::Draw::FRINGE);
        UNSCOPED_INFO("a triangle a patch along its edge and one");
        CHECK(fringed.mDraws[3].mVertices == 3 * (ALWaterField::PATCHES_PER_CELL + 1));
        UNSCOPED_INFO("a cell along each side");
        CHECK(fringed.mDraws[3].mInstances == 2 * ((rect.mEndX - rect.mMinX) + (rect.mEndY - rect.mMinY)));
    }
}
