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

#include "../test/lltut.h"

#include "../alwaterfield.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace tut
{
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
    }

    struct alwaterfield_data
    {
    };

    typedef test_group<alwaterfield_data> alwaterfield_group;
    typedef alwaterfield_group::object    alwaterfield_object;
    tut::alwaterfield_group alwaterfield_g("alwaterfield");

    // The cells and rings cover every point inside the rim exactly once and nothing outside it, sampled at the scale of
    // the cells, of the inner rings and of the rim, for squares from the smallest to the largest and both stretches.
    template<> template<>
    void alwaterfield_object::test<1>()
    {
        set_test_name("cells and rings tile the plane to the rim");
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
                            ensure_equals(what + " covers once inside the rim, never outside", coverage(layout, x, y),
                                          within ? 1 : 0);
                        }
                    }
                }
                for (S32 ring = 0; ring < layout.mRings; ++ring)
                {
                    for (S32 quad = 0; quad < ALWaterField::RING_QUADS; ++quad)
                    {
                        const ALWaterField::Rect r = ALWaterField::ringQuad(layout, ring, quad);
                        ensure(what + " no ring quad is empty", r.mMinX < r.mMaxX && r.mMinY < r.mMaxY);
                    }
                }
            }
        }
    }

    // The rim is the square's half-size plus the stretch, and the rings stop at the first that reaches it.
    template<> template<>
    void alwaterfield_object::test<2>()
    {
        set_test_name("rim and ring count");
        for (F64 stretch : { INFINITE_STRETCH, FINITE_STRETCH, 100.0 })
        {
            for (F32 draw_distance : { 64.f, 512.f, 2048.f })
            {
                const ALWaterField::Layout layout =
                    ALWaterField::layout(EYE_X, EYE_Y, WIDTH, draw_distance, stretch, {}, 0.0);
                ensure_equals("half-size", layout.mHalf, (layout.mCells + 0.5) * WIDTH);
                ensure_equals("rim", layout.mRim, layout.mHalf + stretch);
                ensure("the last ring reaches the rim", std::ldexp(layout.mHalf, layout.mRings) >= layout.mRim);
                ensure("the one before it does not",
                       layout.mRings == 1 || std::ldexp(layout.mHalf, layout.mRings - 1) < layout.mRim);
            }
        }
        const ALWaterField::Layout seven = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 2048.f, INFINITE_STRETCH, {}, 0.0);
        ensure_equals("2048 m takes nine cells each side of the centre", seven.mCells, 9);
        ensure_equals("a square nineteen across", seven.side(), 19);
        ensure_equals("and seven rings to the horizon", seven.mRings, 7);
    }

    // The square is centred on the camera's cell and takes in the draw distance and every loaded region, a
    // variable-sized one by all its cells, until it reaches MAX_CELLS.
    template<> template<>
    void alwaterfield_object::test<3>()
    {
        set_test_name("the square holds the draw distance and the loaded regions");
        const S32 cx = ALWaterField::cellOf(EYE_X, WIDTH);
        const S32 cy = ALWaterField::cellOf(EYE_Y, WIDTH);

        ALWaterField::Layout layout = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, {}, 0.0);
        ensure_equals("centre x", layout.mCentreX, cx);
        ensure_equals("centre y", layout.mCentreY, cy);
        ensure_equals("256 m and a cell", layout.mCells, 2);
        ensure_equals("a short draw distance keeps the smallest", ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 32.f,
                      INFINITE_STRETCH, {}, 0.0).mCells, ALWaterField::MIN_CELLS);
        ensure_equals("257 m reaches a third", ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 257.f, INFINITE_STRETCH, {},
                      0.0).mCells, 3);

        // A 512 m region whose far corner is five cells east and four north.
        const std::vector<ALWaterField::Region> regions = { { cx + 4, cy + 3, 2, 21.f }, { cx - 1, cy, 1, 20.f } };
        layout = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, regions, 0.0);
        ensure_equals("the far region's last cell", layout.mCells, 5);

        const std::vector<ALWaterField::Region> distant = { { cx - 40, cy, 1, 20.f } };
        layout = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, distant, 0.0);
        ensure_equals("held to MAX_CELLS", layout.mCells, ALWaterField::MAX_CELLS);
        ensure("the square fits the cell map", layout.side() <= ALWaterField::MAP_SIZE);
    }

    // With waves on, the patches are whole cells of the square, every cell whose nearest point is within the reach
    // along both axes and no other, and the reach ends before the square's edge, for every density, draw distance and
    // place in the centre cell.
    template<> template<>
    void alwaterfield_object::test<4>()
    {
        set_test_name("the patches and their reach stay inside the square");
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
                        ensure(what + " has a reach", layout.mReach > 0.0);
                        ensure(what + " patches inside the square",
                               layout.mPatchMinX >= layout.mCentreX - layout.mCells &&
                                   layout.mPatchMaxX <= layout.mCentreX + layout.mCells &&
                                   layout.mPatchMinY >= layout.mCentreY - layout.mCells &&
                                   layout.mPatchMaxY <= layout.mCentreY + layout.mCells);

                        // The nearest point of the square's edge to the camera.
                        const F64 sx = layout.centreX() - eye_x;
                        const F64 sy = layout.centreY() - eye_y;
                        const F64 edge = llmin(llmin(layout.mHalf - sx, layout.mHalf + sx),
                                               llmin(layout.mHalf - sy, layout.mHalf + sy));
                        ensure(what + " the reach's fade is 0 before the edge", layout.mReach < edge);

                        for (S32 c = layout.mCentreX - layout.mCells; c <= layout.mCentreX + layout.mCells; ++c)
                        {
                            const F64 lo = c * WIDTH;
                            const F64 nearest = llmax(0.0, llmax(lo - eye_x, eye_x - (lo + WIDTH)));
                            const bool patched = c >= layout.mPatchMinX && c <= layout.mPatchMaxX;
                            ensure_equals(what + " column patched by its nearest point", patched,
                                          nearest < layout.mReach);
                        }
                        for (S32 r = layout.mCentreY - layout.mCells; r <= layout.mCentreY + layout.mCells; ++r)
                        {
                            const F64 lo = r * WIDTH;
                            const F64 nearest = llmax(0.0, llmax(lo - eye_y, eye_y - (lo + WIDTH)));
                            const bool patched = r >= layout.mPatchMinY && r <= layout.mPatchMaxY;
                            ensure_equals(what + " row patched by its nearest point", patched,
                                          nearest < layout.mReach);
                        }
                    }
                }
            }
        }
    }

    // A denser tessellation reaches no less far.
    template<> template<>
    void alwaterfield_object::test<5>()
    {
        set_test_name("the reach grows with the density");
        F64 last = 0.0;
        F64 last_held = 0.0;
        for (F32 density = 1.f; density <= 512.f; density *= 1.25f)
        {
            const F64 reach = ALWaterField::tessReach(density);
            ensure("tessReach is monotone", reach >= last);
            const F64 held =
                ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, reach).mReach;
            ensure("the held reach is monotone", held >= last_held);
            last = reach;
            last_held = held;
        }
        ensure_equals("half the big layer's 25 m at density 64", ALWaterField::tessReach(64.f), 800.0);
    }

    // The coordinates the shaders work from a camera-relative position and the phases are the point's own, worked in
    // double from its global position, for cameras across a large grid and points out to the wave clamp.
    template<> template<>
    void alwaterfield_object::test<6>()
    {
        set_test_name("phases equal the direct evaluation");
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
                            ensure("layer coordinate " + std::to_string(k) + " at " + std::to_string(rx) + ", " +
                                       std::to_string(ry),
                                   wrapped(a.v[k], b.v[k]) < tolerance[k]);
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
            ensure("scroll " + std::to_string(k), wrapped(got[k], expected[k]) < 1e-6);
        }
    }

    // A fixed point keeps its coordinates as the camera moves a kilometre, the phases taken from the camera's global
    // position.
    template<> template<>
    void alwaterfield_object::test<7>()
    {
        set_test_name("a point keeps its coordinates across a region crossing");
        const F64 px = EYE_X + 140.0;
        const F64 py = EYE_Y - 60.0;
        const ALWaterField::WavePhases here = ALWaterField::wavePhases(EYE_X, EYE_Y, UPTIME, WAVE1, WAVE2);
        const ALWaterField::WavePhases there =
            ALWaterField::wavePhases(EYE_X + 1000.0, EYE_Y, UPTIME, WAVE1, WAVE2);
        const Coordinates a = shaded(here, (F32)(px - EYE_X), (F32)(py - EYE_Y));
        const Coordinates b = shaded(there, (F32)(px - EYE_X - 1000.0), (F32)(py - EYE_Y));
        for (S32 k = 0; k < 6; ++k)
        {
            ensure("layer " + std::to_string(k) + " unchanged as the camera moves", wrapped(a.v[k], b.v[k]) < 2e-3);
        }
    }

    // The cell map holds each cell's region height with its flag, a variable-sized region in every cell it covers, and
    // the agent region's height with no flag where no region is; the camera's cell is texel (n, n).
    template<> template<>
    void alwaterfield_object::test<8>()
    {
        set_test_name("the cell map");
        const S32 cx = ALWaterField::cellOf(EYE_X, WIDTH);
        const S32 cy = ALWaterField::cellOf(EYE_Y, WIDTH);
        const std::vector<ALWaterField::Region> regions = { { cx, cy, 1, 21.5f }, { cx + 1, cy - 2, 2, 30.f } };
        const ALWaterField::Layout layout =
            ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 256.f, INFINITE_STRETCH, regions, 0.0);
        std::vector<F32> texels;
        ALWaterField::fillCells(layout, regions, 19.f, texels);
        ensure_equals("one texel of two channels a cell of the map", texels.size(),
                      (size_t)ALWaterField::MAP_SIZE * ALWaterField::MAP_SIZE * 2);

        const auto at = [&](S32 dx, S32 dy)
        {
            const S32 i = layout.mCells + dx;
            const S32 j = layout.mCells + dy;
            const F32* t = texels.data() + ((size_t)j * ALWaterField::MAP_SIZE + i) * 2;
            return std::make_pair(t[0], t[1]);
        };
        ensure_equals("the camera's cell's height", at(0, 0).first, 21.5f);
        ensure_equals("the camera's cell is a region", at(0, 0).second, 1.f);
        for (S32 dx : { 1, 2 })
        {
            for (S32 dy : { -2, -1 })
            {
                ensure_equals("the 512 m region covers its four cells", at(dx, dy).first, 30.f);
                ensure_equals("each flagged", at(dx, dy).second, 1.f);
            }
        }
        ensure_equals("a cell with no region takes the agent's height", at(-1, 1).first, 19.f);
        ensure_equals("and no flag", at(-1, 1).second, 0.f);
        ensure_equals("past the 512 m region", at(3, -2).first, 19.f);
    }

    // The square's corner the GPU places the cells from, relative to an origin (the agent region's): the corner's global
    // position less the origin, near and far from the grid's origin and with the origin a region off the camera's.
    template<> template<>
    void alwaterfield_object::test<11>()
    {
        set_test_name("the cell square relative to the agent region");
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
                ensure_equals("x from the origin", (F64)square.mV[VX],
                              (F64)(layout.mCentreX - layout.mCells) * WIDTH - origin_x);
                ensure_equals("y from the origin", (F64)square.mV[VY],
                              (F64)(layout.mCentreY - layout.mCells) * WIDTH - origin_y);
                ensure_equals("a cell's width", (F64)square.mV[VZ], WIDTH);
                ensure_equals("the cells a side", (S32)square.mV[VW], layout.side());
            }
        }
    }

    // Negative global coordinates: the cell holding a point is its floor's, and the square, its patches and the camera's
    // cell work as they do anywhere.
    template<> template<>
    void alwaterfield_object::test<12>()
    {
        set_test_name("negative coordinates");
        ensure_equals("just below 0", ALWaterField::cellOf(-0.5, WIDTH), -1);
        ensure_equals("a whole cell below", ALWaterField::cellOf(-WIDTH, WIDTH), -1);
        ensure_equals("past it", ALWaterField::cellOf(-WIDTH - 0.5, WIDTH), -2);
        const F64 eye_x = -5.0 * WIDTH + 30.0;
        const F64 eye_y = -0.25;
        const ALWaterField::Layout layout =
            ALWaterField::layout(eye_x, eye_y, WIDTH, 256.f, INFINITE_STRETCH, {}, ALWaterField::tessReach(16.f));
        ensure_equals("centre x", layout.mCentreX, -5);
        ensure_equals("centre y", layout.mCentreY, -1);
        ensure("the camera lies in the centre cell", eye_x >= layout.mCentreX * WIDTH && eye_x < (layout.mCentreX + 1) * WIDTH);
        ensure("and its patches", layout.mPatchMinX <= layout.mCentreX && layout.mPatchMaxX >= layout.mCentreX &&
                                       layout.mPatchMinY <= layout.mCentreY && layout.mPatchMaxY >= layout.mCentreY);
        const F64 lo = layout.mPatchMinX * WIDTH;
        ensure("the west patch column's nearest point within the reach", eye_x - (lo + WIDTH) < layout.mReach);
        ensure("and the one past it not", eye_x - lo >= layout.mReach);
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
    template<> template<>
    void alwaterfield_object::test<13>()
    {
        set_test_name("the cell map, region by region");
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
        ensure_equals("the largest square", layout.mCells, ALWaterField::MAX_CELLS);
        std::vector<F32> texels;
        ALWaterField::fillCells(layout, regions, 19.f, texels);
        const std::vector<F32> brute = bruteCells(layout, regions, 19.f);
        ensure("as every texel asking every region", texels == brute);

        const auto at = [&](S32 i, S32 j) { return texels[((size_t)j * ALWaterField::MAP_SIZE + i) * 2]; };
        const S32 n = layout.mCells;
        ensure_equals("the first listed holds the camera's cell", at(n, n), 21.5f);
        ensure_equals("the second region of two holds its own", at(n + 1, n + 1), 99.f);
        ensure_equals("the straddling region inside the square", at(2 * n, n - 2), 30.f);
        ensure_equals("and past the square, in the map", at(2 * n + 1, n - 2), 30.f);
        ensure_equals("the west straddler's cell inside", at(0, n + 10), 12.f);

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
            ensure("trial " + std::to_string(trial), texels == bruteCells(l, random, 19.f));
        }
    }

    // Moving within its cell moves nothing of the square: the cell map's key, the square, stays the same however far
    // across the cell the camera goes.
    template<> template<>
    void alwaterfield_object::test<14>()
    {
        set_test_name("the square stays as the camera moves within its cell");
        const F64 base_x = 1000.0 * WIDTH;
        const F64 base_y = 1031.0 * WIDTH;
        const ALWaterField::Layout first = ALWaterField::layout(base_x, base_y, WIDTH, 1024.f, INFINITE_STRETCH, {}, 0.0);
        for (F64 fx : { 0.0, 0.3, 0.999 })
        {
            for (F64 fy : { 0.0, 0.61, 0.999 })
            {
                const ALWaterField::Layout l =
                    ALWaterField::layout(base_x + fx * WIDTH, base_y + fy * WIDTH, WIDTH, 1024.f, INFINITE_STRETCH, {}, 0.0);
                ensure("the same layout", l == first);
            }
        }
        const ALWaterField::Layout next = ALWaterField::layout(base_x + WIDTH, base_y, WIDTH, 1024.f, INFINITE_STRETCH, {}, 0.0);
        ensure_equals("a cell east recentres it", next.mCentreX, first.mCentreX + 1);
    }

    // A camera at no finite place, or cells of no width, lay out nothing; a camera far past any grid is held at the
    // cells' limit; a stretch past any rim takes no more than the most rings; a draw distance or reach that is not a
    // number takes none.
    template<> template<>
    void alwaterfield_object::test<17>()
    {
        set_test_name("what the layout will not take");
        const F64 nan = std::numeric_limits<F64>::quiet_NaN();
        const F64 inf = std::numeric_limits<F64>::infinity();
        ensure_equals("a NaN camera", ALWaterField::layout(nan, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0).mCells, 0);
        ensure_equals("an infinite one", ALWaterField::layout(EYE_X, -inf, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0).mCells, 0);
        ensure_equals("no width", ALWaterField::layout(EYE_X, EYE_Y, 0.0, 512.f, INFINITE_STRETCH, {}, 0.0).mCells, 0);
        ensure_equals("a negative one", ALWaterField::layout(EYE_X, EYE_Y, -WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0).mCells, 0);
        ensure("nothing to draw for it", ALWaterField::fieldDraws(ALWaterField::Layout()).mCount == 0);

        const ALWaterField::Layout distant = ALWaterField::layout(1e300, -1e300, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0);
        ensure_equals("far east, at the limit", distant.mCentreX, ALWaterField::CELL_LIMIT);
        ensure_equals("far south, at the limit", distant.mCentreY, -ALWaterField::CELL_LIMIT);
        ensure_equals("a cellOf past the limit", ALWaterField::cellOf(1e300, WIDTH), ALWaterField::CELL_LIMIT);
        ensure_equals("a cellOf of NaN", ALWaterField::cellOf(nan, WIDTH), 0);

        const ALWaterField::Layout wide = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, 1e300, {}, 0.0);
        ensure("a finite number of rings for any stretch", wide.mRings > 0 && wide.mRings <= 40);
        const ALWaterField::Layout odd = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, std::numeric_limits<F32>::quiet_NaN(),
                                                              INFINITE_STRETCH, {}, nan);
        ensure_equals("a NaN draw distance takes the fewest cells", odd.mCells, ALWaterField::MIN_CELLS);
        ensure_equals("and a NaN reach none", odd.mReach, 0.0);
    }

    // The field's draws: the cells, the rings at three triangles a quad, and ring 0's four sides at a triangle a cell
    // along the square and two more.
    template<> template<>
    void alwaterfield_object::test<18>()
    {
        set_test_name("the field's draws");
        const ALWaterField::Layout flat = ALWaterField::layout(EYE_X, EYE_Y, WIDTH, 512.f, INFINITE_STRETCH, {}, 0.0);
        const ALWaterField::FieldDraws draws = ALWaterField::fieldDraws(flat);
        ensure_equals("three", draws.mCount, 3);
        ensure_equals("the cells", draws.mDraws[0].mPart, ALWaterField::Draw::CELLS);
        ensure("as strips", draws.mDraws[0].mStrip && draws.mDraws[0].mVertices == 4);
        ensure_equals("a cell an instance", draws.mDraws[0].mInstances, flat.side() * flat.side());
        ensure_equals("the rings", draws.mDraws[1].mPart, ALWaterField::Draw::RINGS);
        ensure("three triangles a quad", !draws.mDraws[1].mStrip && draws.mDraws[1].mVertices == 9);
        ensure_equals("twelve quads a ring", draws.mDraws[1].mInstances, ALWaterField::RING_QUADS * flat.mRings);
        ensure_equals("the zippers", draws.mDraws[2].mPart, ALWaterField::Draw::ZIPPERS);
        ensure_equals("four of them", draws.mDraws[2].mInstances, 4);
        ensure_equals("a triangle a cell along the side and two", draws.mDraws[2].mVertices, 3 * (flat.side() + 2));
    }
}
