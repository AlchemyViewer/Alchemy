/**
 * @file alcolorwheelmodel_test.cpp
 * @brief Unit tests for the colour wheel's RGB <-> master/hue/saturation maths
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

#include "linden_common.h"

#include <catch2/catch_test_macros.hpp>
#include "altestmatchers.h"

#include "../alcolorwheelmodel.h"

#include <cmath>

namespace
{
    struct wheel_data
    {
        ALColorWheelModel mWheel;

        /// Lift: centred on 0, per-channel -0.5 to +0.5.
        void asLift()  { mWheel.configure(0.f, -0.5f, 0.5f, false); }
        /// Gain / gamma: centred on 1, per-channel 0.5 to 1.5.
        void asGain()  { mWheel.configure(1.f, 0.5f, 1.5f, false); }
        /// A split-tone tint: centred on 0.5 over 0..1, master locked because
        /// the renderer normalises tint magnitude away.
        void asTint()  { mWheel.configure(0.5f, 0.f, 1.f, true); }

        static F32 deg(F32 d) { return d * F_PI / 180.f; }

        /// Smallest absolute difference between two angles, allowing for wrap.
        static F32 angleDelta(F32 a, F32 b)
        {
            F32 d = fmodf(fabsf(a - b), F_TWO_PI);
            return (d > F_PI) ? F_TWO_PI - d : d;
        }
    };

    // --- the basis itself ----------------------------------------------------

    // The whole design rests on {(1,1,1)/sqrt3, u, v} being orthonormal -- that
    // is what makes the decomposition lossless. A typo in the constants would
    // show up here and nowhere else obvious.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: the chroma basis is orthonormal and has no grey in it", "[ALColorWheelModel]")
    {
        const LLVector3 u = ALColorWheelModel::chromaDirection(0.f);
        const LLVector3 v = ALColorWheelModel::chromaDirection(F_PI_BY_TWO);
        const LLVector3 grey(1.f, 1.f, 1.f);

        CHECK_THAT(u.magVec(), ll_test::WithinBits(1.f, 5));
        CHECK_THAT(v.magVec(), ll_test::WithinBits(1.f, 5));
        UNSCOPED_INFO("u and v are perpendicular");
        CHECK_THAT(u * v, ll_test::WithinBits(0.f, 5));
        UNSCOPED_INFO("u has no achromatic part");
        CHECK_THAT(u * grey, ll_test::WithinBits(0.f, 5));
        UNSCOPED_INFO("v has no achromatic part");
        CHECK_THAT(v * grey, ll_test::WithinBits(0.f, 5));
    }

    // Angle lands on the usual colour circle: red at 0, green at 120, blue at 240.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: toPolar puts the primaries at their usual hues", "[ALColorWheelModel]")
    {
        F32 hue, sat;
        ALColorWheelModel::toPolar(LLVector3(1.f, 0.f, 0.f), hue, sat);
        UNSCOPED_INFO("pure red is hue 0");
        CHECK_THAT(angleDelta(hue, deg(0.f)), ll_test::WithinBits(0.f, 4));

        ALColorWheelModel::toPolar(LLVector3(0.f, 1.f, 0.f), hue, sat);
        UNSCOPED_INFO("pure green is 120");
        CHECK_THAT(angleDelta(hue, deg(120.f)), ll_test::WithinBits(0.f, 4));

        ALColorWheelModel::toPolar(LLVector3(0.f, 0.f, 1.f), hue, sat);
        UNSCOPED_INFO("pure blue is 240");
        CHECK_THAT(angleDelta(hue, deg(240.f)), ll_test::WithinBits(0.f, 4));
    }

    // A neutral triplet has no chroma, and asking its hue must not produce a
    // NaN from atan2(0,0) -- the puck would jump the moment it crossed centre.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: a grey has no saturation and a finite hue", "[ALColorWheelModel]")
    {
        F32 hue, sat;
        for (F32 grey : { -0.4f, 0.f, 0.5f, 1.f })
        {
            CAPTURE(grey);
            ALColorWheelModel::toPolar(LLVector3(grey, grey, grey), hue, sat);
            REQUIRE_THAT(sat, ll_test::WithinBits(0.f, 5));
            REQUIRE(std::isfinite(hue));
        }
    }

    // --- round trip ----------------------------------------------------------

    // The property everything else depends on: decompose and rebuild returns
    // the same triplet. If this fails, typing a number and dragging the puck
    // disagree about the value.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: decomposing a triplet and rebuilding it returns the same triplet", "[ALColorWheelModel]")
    {
        const F32 samples[] = { -0.5f, -0.31f, -0.07f, 0.f, 0.12f, 0.29f, 0.5f };
        for (F32 r : samples)
        {
            for (F32 g : samples)
            {
                for (F32 b : samples)
                {
                    CAPTURE(r, g, b);
                    const LLVector3 in(r, g, b);
                    F32 hue, sat;
                    ALColorWheelModel::toPolar(in, hue, sat);
                    const LLVector3 out = ALColorWheelModel::toRGB(
                        ALColorWheelModel::masterOf(in), hue, sat);

                    REQUIRE_THAT(out.mV[VX], ll_test::WithinBits(in.mV[VX], 5));
                    REQUIRE_THAT(out.mV[VY], ll_test::WithinBits(in.mV[VY], 5));
                    REQUIRE_THAT(out.mV[VZ], ll_test::WithinBits(in.mV[VZ], 5));
                }
            }
        }
    }

    // Round trip through the stored value, which is the path the widget takes.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: setPolar round trips hue and saturation through the stored value", "[ALColorWheelModel]")
    {
        asLift();
        for (S32 i = 0; i < 24; ++i)
        {
            CAPTURE(i);
            const F32 hue = deg((F32)i * 15.f);
            const F32 sat = mWheel.getMaxSat() * 0.6f;
            mWheel.setPolar(hue, sat);

            REQUIRE_THAT(angleDelta(mWheel.getHue(), hue), ll_test::WithinBits(0.f, 4));
            REQUIRE_THAT(mWheel.getSat(), ll_test::WithinBits(sat, 4));
            UNSCOPED_INFO("master untouched");
            REQUIRE_THAT(mWheel.getMaster(), ll_test::WithinBits(0.f, 5));
        }
    }

    // --- the hexagon ---------------------------------------------------------

    // getMaxSat is the inradius, so a puck on the rim is reachable at EVERY
    // hue with the master centred -- nothing gets clamped.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: a puck on the rim is reachable at every hue with the master centred", "[ALColorWheelModel]")
    {
        asLift();
        const F32 rim = mWheel.getMaxSat();
        for (S32 i = 0; i < 72; ++i)
        {
            CAPTURE(i);
            const F32 hue = deg((F32)i * 5.f);
            mWheel.setPolar(hue, rim);
            REQUIRE_THAT(mWheel.getSat(), ll_test::WithinBits(rim, 3));
            REQUIRE_THAT(angleDelta(mWheel.getHue(), hue), ll_test::WithinBits(0.f, 3));
        }
    }

    // The inradius is sqrt(1.5) * halfRange, and the tightest directions are
    // the primaries, where one channel lands exactly on its limit.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: getMaxSat is the inradius and red on the rim sits on its limit", "[ALColorWheelModel]")
    {
        asLift();
        CHECK_THAT(mWheel.getMaxSat(), ll_test::WithinBits(sqrtf(1.5f) * 0.5f, 4));

        mWheel.setPolar(0.f, mWheel.getMaxSat());
        UNSCOPED_INFO("red sits on its limit");
        CHECK_THAT(mWheel.getRGB().mV[VX], ll_test::WithinBits(0.5f, 3));

        asGain();
        UNSCOPED_INFO("gain inradius");
        CHECK_THAT(mWheel.getMaxSat(), ll_test::WithinBits(sqrtf(1.5f) * 0.5f, 4));
    }

    // Past the rim the channel clamp bites: the value stays legal, and the
    // puck reports where the value actually is rather than where the pointer
    // went.
    //
    // The ceiling here is NOT getMaxSat(). Three different radii are in play
    // and it is worth being explicit about which is which, for a range of
    // width w:
    //
    //   w * 1/sqrt(6)  = 0.408w  inradius with the master pinned to centre
    //                            -- what getMaxSat() returns, so the rim is
    //                            reachable at every hue
    //   w * 1/sqrt(8)  = 0.354w  ... times 2/sqrt(3): the circumradius of that
    //                            same hexagon, at 30, 90, 150 degrees
    //   w * sqrt(2/3)  = 0.816w  the widest deviation ANY legal triplet has
    //
    // Clamping moves the mean, so the result leaves the master-pinned hexagon
    // and lands on the projection of the whole cube -- the third figure. A
    // bound of the second is too tight and fails here.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: past the rim the channel clamp keeps the value legal", "[ALColorWheelModel]")
    {
        asLift();
        const F32 rim = mWheel.getMaxSat();
        const F32 widest = sqrtf(2.f / 3.f) * (mWheel.getMax() - mWheel.getMin());

        for (S32 i = 0; i < 36; ++i)
        {
            CAPTURE(i);
            const F32 hue = deg((F32)i * 10.f);
            mWheel.setPolar(hue, rim * 4.f);

            const LLVector3& rgb = mWheel.getRGB();
            for (S32 c = 0; c < 3; ++c)
            {
                CAPTURE(c);
                REQUIRE(rgb.mV[c] >= -0.5f - 1e-5f);
                REQUIRE(rgb.mV[c] <= 0.5f + 1e-5f);
            }
            UNSCOPED_INFO("saturation did not run away");
            REQUIRE(mWheel.getSat() <= widest + 1e-4f);
            UNSCOPED_INFO("but it did move out to the boundary");
            REQUIRE(mWheel.getSat() > rim * 0.9f);
        }

        // Far enough past the rim in a primary direction and the triplet is
        // pinned to a cube corner, which is exactly the widest case.
        mWheel.setPolar(0.f, rim * 4.f);
        UNSCOPED_INFO("corner-pinned saturation");
        CHECK_THAT(mWheel.getSat(), ll_test::WithinBits(widest, 4));
        UNSCOPED_INFO("r on its ceiling");
        CHECK_THAT(mWheel.getRGB().mV[VX], ll_test::WithinBits(0.5f, 5));
        UNSCOPED_INFO("g on its floor");
        CHECK_THAT(mWheel.getRGB().mV[VY], ll_test::WithinBits(-0.5f, 5));
    }

    // Clamping must not silently rotate the hue -- the puck would slide around
    // the ring while the user dragged straight outward.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: clamping past the rim does not rotate the hue", "[ALColorWheelModel]")
    {
        asLift();
        for (S32 i = 0; i < 12; ++i)
        {
            CAPTURE(i);
            const F32 hue = deg((F32)i * 30.f);
            mWheel.setPolar(hue, mWheel.getMaxSat() * 3.f);
            REQUIRE_THAT(angleDelta(mWheel.getHue(), hue), ll_test::WithinBits(0.f, 3));
        }
    }

    // --- master --------------------------------------------------------------

    // Moving the master keeps the chroma, which is what makes the wheel and
    // its slider feel independent.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: moving the master keeps the hue and saturation", "[ALColorWheelModel]")
    {
        asGain();
        mWheel.setPolar(deg(200.f), mWheel.getMaxSat() * 0.4f);
        const F32 hue = mWheel.getHue();
        const F32 sat = mWheel.getSat();

        mWheel.setMaster(1.2f);
        CHECK_THAT(mWheel.getMaster(), ll_test::WithinBits(1.2f, 4));
        CHECK_THAT(angleDelta(mWheel.getHue(), hue), ll_test::WithinBits(0.f, 3));
        CHECK_THAT(mWheel.getSat(), ll_test::WithinBits(sat, 3));
    }

    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: a master pushed past its range clamps and the triplet stays legal", "[ALColorWheelModel]")
    {
        asGain();
        mWheel.setMaster(9.f);
        CHECK(mWheel.getMaster() <= 1.5f + 1e-5f);
        for (S32 c = 0; c < 3; ++c)
        {
            CAPTURE(c);
            REQUIRE(mWheel.getRGB().mV[c] <= 1.5f + 1e-5f);
        }
    }

    // Tint wheels lock the master, because the renderer divides each tint by
    // dot(tint, LUMA) -- magnitude cancels, so a master there would be a
    // control that visibly does nothing.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: a tint wheel ignores the master and re-centres it on a puck move", "[ALColorWheelModel]")
    {
        asTint();
        mWheel.setPolar(deg(90.f), mWheel.getMaxSat() * 0.5f);
        const F32 master = mWheel.getMaster();

        mWheel.setMaster(0.9f);
        UNSCOPED_INFO("master ignored");
        CHECK_THAT(mWheel.getMaster(), ll_test::WithinBits(master, 5));

        // ... and a fresh puck move re-centres on the configured neutral.
        mWheel.setPolar(deg(30.f), mWheel.getMaxSat() * 0.5f);
        UNSCOPED_INFO("re-centred on neutral");
        CHECK_THAT(mWheel.getMaster(), ll_test::WithinBits(0.5f, 4));
    }

    // --- ring / puck agreement ----------------------------------------------

    // The ring is generated from the same basis as the puck, so the colour
    // shown at an angle is the colour dragging to that angle produces. Drawing
    // a generic HSV ring instead is where these two drift apart.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: the ring's colour at a hue is coloured and has that hue", "[ALColorWheelModel]")
    {
        for (S32 i = 0; i < 36; ++i)
        {
            CAPTURE(i);
            const F32 hue = deg((F32)i * 10.f);
            const LLVector3 ring = ALColorWheelModel::ringColor(hue);

            F32 ring_hue, ring_sat;
            ALColorWheelModel::toPolar(ring, ring_hue, ring_sat);
            REQUIRE_THAT(angleDelta(ring_hue, hue), ll_test::WithinBits(0.f, 3));
            REQUIRE(ring_sat > 0.f);
        }
    }

    // The ring's reference chroma is chosen to stay inside 0..1, so no channel
    // is ever clipped -- clipping would bend the hue it is advertising.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: every ring channel stays strictly inside 0..1", "[ALColorWheelModel]")
    {
        for (S32 i = 0; i < 72; ++i)
        {
            const LLVector3 ring = ALColorWheelModel::ringColor(deg((F32)i * 5.f));
            for (S32 c = 0; c < 3; ++c)
            {
                CAPTURE(i, c);
                REQUIRE(ring.mV[c] > 0.001f);
                REQUIRE(ring.mV[c] < 0.999f);
            }
        }
    }

    // --- numeric entry and reset --------------------------------------------

    // Typing into one field leaves the other two alone and moves the puck.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: setChannel writes one channel alone and moves the puck", "[ALColorWheelModel]")
    {
        asLift();
        mWheel.setChannel(0, 0.2f);
        CHECK_THAT(mWheel.getRGB().mV[VX], ll_test::WithinBits(0.2f, 5));
        UNSCOPED_INFO("g untouched");
        CHECK_THAT(mWheel.getRGB().mV[VY], ll_test::WithinBits(0.f, 5));
        UNSCOPED_INFO("b untouched");
        CHECK_THAT(mWheel.getRGB().mV[VZ], ll_test::WithinBits(0.f, 5));
        UNSCOPED_INFO("puck moved off centre");
        CHECK(mWheel.getSat() > 0.f);
        UNSCOPED_INFO("towards red");
        CHECK_THAT(angleDelta(mWheel.getHue(), 0.f), ll_test::WithinBits(0.f, 3));

        mWheel.setChannel(0, 9.f);
        UNSCOPED_INFO("typed value clamped");
        CHECK_THAT(mWheel.getRGB().mV[VX], ll_test::WithinBits(0.5f, 5));

        mWheel.setChannel(7, 1.f);      // out of range, ignored
        UNSCOPED_INFO("bad index ignored");
        CHECK_THAT(mWheel.getRGB().mV[VX], ll_test::WithinBits(0.5f, 5));
    }

    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: reset returns the configured neutral for each flavour", "[ALColorWheelModel]")
    {
        asLift();
        mWheel.setPolar(deg(45.f), mWheel.getMaxSat());
        mWheel.reset();
        UNSCOPED_INFO("lift neutral is 0");
        CHECK_THAT(mWheel.getMaster(), ll_test::WithinBits(0.f, 5));
        UNSCOPED_INFO("and colourless");
        CHECK_THAT(mWheel.getSat(), ll_test::WithinBits(0.f, 5));

        asGain();
        mWheel.reset();
        UNSCOPED_INFO("gain neutral is 1");
        CHECK_THAT(mWheel.getMaster(), ll_test::WithinBits(1.f, 5));

        asTint();
        mWheel.reset();
        UNSCOPED_INFO("tint neutral is 0.5");
        CHECK_THAT(mWheel.getMaster(), ll_test::WithinBits(0.5f, 5));
        UNSCOPED_INFO("tint neutral r");
        CHECK_THAT(mWheel.getRGB().mV[VX], ll_test::WithinBits(0.5f, 5));
    }

    // Re-configuring re-clamps whatever was already held, so switching a wheel
    // from lift to gain cannot leave an out-of-range value behind.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: configure re-clamps the value already held", "[ALColorWheelModel]")
    {
        asLift();
        mWheel.setRGB(LLVector3(-0.4f, 0.f, 0.3f));
        asGain();
        for (S32 c = 0; c < 3; ++c)
        {
            CAPTURE(c);
            REQUIRE(mWheel.getRGB().mV[c] >= 0.5f - 1e-5f);
            REQUIRE(mWheel.getRGB().mV[c] <= 1.5f + 1e-5f);
        }
    }

    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: a reversed range is accepted rather than producing a negative width", "[ALColorWheelModel]")
    {
        mWheel.configure(0.f, 0.5f, -0.5f, false);
        UNSCOPED_INFO("min is the low one");
        CHECK_THAT(mWheel.getMin(), ll_test::WithinBits(-0.5f, 5));
        UNSCOPED_INFO("max is the high one");
        CHECK_THAT(mWheel.getMax(), ll_test::WithinBits(0.5f, 5));
        CHECK(mWheel.getMaxSat() > 0.f);
    }

    // Exactly zero, not approximately: the vectorscope bins chroma with floorf,
    // so an epsilon of either sign puts one grey in a different cell from the
    // next. Compared bit for bit because that is the property callers rely on,
    // and because an epsilon is exactly what a tolerance would hide.
    TEST_CASE_METHOD(wheel_data, "ALColorWheelModel: toChroma gives a grey exactly zero chroma", "[ALColorWheelModel]")
    {
        for (S32 level = 0; level < 256; ++level)
        {
            CAPTURE(level);
            const F32 grey = (F32)level / 255.f;
            F32 u = 1.f, v = 1.f;
            ALColorWheelModel::toChroma(LLVector3(grey, grey, grey), u, v);
            REQUIRE(u == 0.f);
            REQUIRE(v == 0.f);
        }

        // Outside 0..1 as well: wheels carry lift and gain, which sit either
        // side of the unit range.
        for (F32 grey : { -0.4f, 1.5f, 37.f })
        {
            CAPTURE(grey);
            F32 u = 1.f, v = 1.f;
            ALColorWheelModel::toChroma(LLVector3(grey, grey, grey), u, v);
            REQUIRE(u == 0.f);
            REQUIRE(v == 0.f);
        }
    }
}
