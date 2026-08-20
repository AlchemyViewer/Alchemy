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

#include "../test/lltut.h"

#include "../alcolorwheelmodel.h"

#include <cmath>

namespace tut
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

    typedef test_group<wheel_data> wheel_group;
    typedef wheel_group::object    wheel_object;
    tut::wheel_group wg("ALColorWheelModel");

    // --- the basis itself ----------------------------------------------------

    // The whole design rests on {(1,1,1)/sqrt3, u, v} being orthonormal -- that
    // is what makes the decomposition lossless. A typo in the constants would
    // show up here and nowhere else obvious.
    template<> template<>
    void wheel_object::test<1>()
    {
        const LLVector3 u = ALColorWheelModel::chromaDirection(0.f);
        const LLVector3 v = ALColorWheelModel::chromaDirection(F_PI_BY_TWO);
        const LLVector3 grey(1.f, 1.f, 1.f);

        ensure_approximately_equals("u is unit", u.magVec(), 1.f, 5);
        ensure_approximately_equals("v is unit", v.magVec(), 1.f, 5);
        ensure_approximately_equals("u and v are perpendicular", u * v, 0.f, 5);
        ensure_approximately_equals("u has no achromatic part", u * grey, 0.f, 5);
        ensure_approximately_equals("v has no achromatic part", v * grey, 0.f, 5);
    }

    // Angle lands on the usual colour circle: red at 0, green at 120, blue at 240.
    template<> template<>
    void wheel_object::test<2>()
    {
        F32 hue, sat;
        ALColorWheelModel::toPolar(LLVector3(1.f, 0.f, 0.f), hue, sat);
        ensure_approximately_equals("pure red is hue 0", angleDelta(hue, deg(0.f)), 0.f, 4);

        ALColorWheelModel::toPolar(LLVector3(0.f, 1.f, 0.f), hue, sat);
        ensure_approximately_equals("pure green is 120", angleDelta(hue, deg(120.f)), 0.f, 4);

        ALColorWheelModel::toPolar(LLVector3(0.f, 0.f, 1.f), hue, sat);
        ensure_approximately_equals("pure blue is 240", angleDelta(hue, deg(240.f)), 0.f, 4);
    }

    // A neutral triplet has no chroma, and asking its hue must not produce a
    // NaN from atan2(0,0) -- the puck would jump the moment it crossed centre.
    template<> template<>
    void wheel_object::test<3>()
    {
        F32 hue, sat;
        for (F32 grey : { -0.4f, 0.f, 0.5f, 1.f })
        {
            ALColorWheelModel::toPolar(LLVector3(grey, grey, grey), hue, sat);
            ensure_approximately_equals("no saturation", sat, 0.f, 5);
            ensure("hue is finite", std::isfinite(hue));
        }
    }

    // --- round trip ----------------------------------------------------------

    // The property everything else depends on: decompose and rebuild returns
    // the same triplet. If this fails, typing a number and dragging the puck
    // disagree about the value.
    template<> template<>
    void wheel_object::test<4>()
    {
        const F32 samples[] = { -0.5f, -0.31f, -0.07f, 0.f, 0.12f, 0.29f, 0.5f };
        for (F32 r : samples)
        {
            for (F32 g : samples)
            {
                for (F32 b : samples)
                {
                    const LLVector3 in(r, g, b);
                    F32 hue, sat;
                    ALColorWheelModel::toPolar(in, hue, sat);
                    const LLVector3 out = ALColorWheelModel::toRGB(
                        ALColorWheelModel::masterOf(in), hue, sat);

                    ensure_approximately_equals("r round trips", out.mV[VX], in.mV[VX], 5);
                    ensure_approximately_equals("g round trips", out.mV[VY], in.mV[VY], 5);
                    ensure_approximately_equals("b round trips", out.mV[VZ], in.mV[VZ], 5);
                }
            }
        }
    }

    // Round trip through the stored value, which is the path the widget takes.
    template<> template<>
    void wheel_object::test<5>()
    {
        asLift();
        for (S32 i = 0; i < 24; ++i)
        {
            const F32 hue = deg((F32)i * 15.f);
            const F32 sat = mWheel.getMaxSat() * 0.6f;
            mWheel.setPolar(hue, sat);

            ensure_approximately_equals("hue survives", angleDelta(mWheel.getHue(), hue), 0.f, 4);
            ensure_approximately_equals("saturation survives", mWheel.getSat(), sat, 4);
            ensure_approximately_equals("master untouched", mWheel.getMaster(), 0.f, 5);
        }
    }

    // --- the hexagon ---------------------------------------------------------

    // getMaxSat is the inradius, so a puck on the rim is reachable at EVERY
    // hue with the master centred -- nothing gets clamped.
    template<> template<>
    void wheel_object::test<6>()
    {
        asLift();
        const F32 rim = mWheel.getMaxSat();
        for (S32 i = 0; i < 72; ++i)
        {
            const F32 hue = deg((F32)i * 5.f);
            mWheel.setPolar(hue, rim);
            ensure_approximately_equals("rim is reachable at every hue", mWheel.getSat(), rim, 3);
            ensure_approximately_equals("and the hue is the one asked for",
                                        angleDelta(mWheel.getHue(), hue), 0.f, 3);
        }
    }

    // The inradius is sqrt(1.5) * halfRange, and the tightest directions are
    // the primaries, where one channel lands exactly on its limit.
    template<> template<>
    void wheel_object::test<7>()
    {
        asLift();
        ensure_approximately_equals("inradius", mWheel.getMaxSat(), sqrtf(1.5f) * 0.5f, 4);

        mWheel.setPolar(0.f, mWheel.getMaxSat());
        ensure_approximately_equals("red sits on its limit", mWheel.getRGB().mV[VX], 0.5f, 3);

        asGain();
        ensure_approximately_equals("gain inradius", mWheel.getMaxSat(), sqrtf(1.5f) * 0.5f, 4);
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
    template<> template<>
    void wheel_object::test<8>()
    {
        asLift();
        const F32 rim = mWheel.getMaxSat();
        const F32 widest = sqrtf(2.f / 3.f) * (mWheel.getMax() - mWheel.getMin());

        for (S32 i = 0; i < 36; ++i)
        {
            const F32 hue = deg((F32)i * 10.f);
            mWheel.setPolar(hue, rim * 4.f);

            const LLVector3& rgb = mWheel.getRGB();
            for (S32 c = 0; c < 3; ++c)
            {
                ensure("channel stayed in range", rgb.mV[c] >= -0.5f - 1e-5f && rgb.mV[c] <= 0.5f + 1e-5f);
            }
            ensure("saturation did not run away", mWheel.getSat() <= widest + 1e-4f);
            ensure("but it did move out to the boundary", mWheel.getSat() > rim * 0.9f);
        }

        // Far enough past the rim in a primary direction and the triplet is
        // pinned to a cube corner, which is exactly the widest case.
        mWheel.setPolar(0.f, rim * 4.f);
        ensure_approximately_equals("corner-pinned saturation", mWheel.getSat(), widest, 4);
        ensure_approximately_equals("r on its ceiling", mWheel.getRGB().mV[VX], 0.5f, 5);
        ensure_approximately_equals("g on its floor", mWheel.getRGB().mV[VY], -0.5f, 5);
    }

    // Clamping must not silently rotate the hue -- the puck would slide around
    // the ring while the user dragged straight outward.
    template<> template<>
    void wheel_object::test<9>()
    {
        asLift();
        for (S32 i = 0; i < 12; ++i)
        {
            const F32 hue = deg((F32)i * 30.f);
            mWheel.setPolar(hue, mWheel.getMaxSat() * 3.f);
            ensure_approximately_equals("hue held through the clamp",
                                        angleDelta(mWheel.getHue(), hue), 0.f, 3);
        }
    }

    // --- master --------------------------------------------------------------

    // Moving the master keeps the chroma, which is what makes the wheel and
    // its slider feel independent.
    template<> template<>
    void wheel_object::test<10>()
    {
        asGain();
        mWheel.setPolar(deg(200.f), mWheel.getMaxSat() * 0.4f);
        const F32 hue = mWheel.getHue();
        const F32 sat = mWheel.getSat();

        mWheel.setMaster(1.2f);
        ensure_approximately_equals("master moved", mWheel.getMaster(), 1.2f, 4);
        ensure_approximately_equals("hue kept", angleDelta(mWheel.getHue(), hue), 0.f, 3);
        ensure_approximately_equals("saturation kept", mWheel.getSat(), sat, 3);
    }

    // A master pushed past its range clamps, and the triplet stays legal.
    template<> template<>
    void wheel_object::test<11>()
    {
        asGain();
        mWheel.setMaster(9.f);
        ensure("master clamped", mWheel.getMaster() <= 1.5f + 1e-5f);
        for (S32 c = 0; c < 3; ++c)
        {
            ensure("channel in range", mWheel.getRGB().mV[c] <= 1.5f + 1e-5f);
        }
    }

    // Tint wheels lock the master, because the renderer divides each tint by
    // dot(tint, LUMA) -- magnitude cancels, so a master there would be a
    // control that visibly does nothing.
    template<> template<>
    void wheel_object::test<12>()
    {
        asTint();
        mWheel.setPolar(deg(90.f), mWheel.getMaxSat() * 0.5f);
        const F32 master = mWheel.getMaster();

        mWheel.setMaster(0.9f);
        ensure_approximately_equals("master ignored", mWheel.getMaster(), master, 5);

        // ... and a fresh puck move re-centres on the configured neutral.
        mWheel.setPolar(deg(30.f), mWheel.getMaxSat() * 0.5f);
        ensure_approximately_equals("re-centred on neutral", mWheel.getMaster(), 0.5f, 4);
    }

    // --- ring / puck agreement ----------------------------------------------

    // The ring is generated from the same basis as the puck, so the colour
    // shown at an angle is the colour dragging to that angle produces. Drawing
    // a generic HSV ring instead is where these two drift apart.
    template<> template<>
    void wheel_object::test<13>()
    {
        for (S32 i = 0; i < 36; ++i)
        {
            const F32 hue = deg((F32)i * 10.f);
            const LLVector3 ring = ALColorWheelModel::ringColor(hue);

            F32 ring_hue, ring_sat;
            ALColorWheelModel::toPolar(ring, ring_hue, ring_sat);
            ensure_approximately_equals("ring hue matches the puck's",
                                        angleDelta(ring_hue, hue), 0.f, 3);
            ensure("ring is actually coloured", ring_sat > 0.f);
        }
    }

    // The ring's reference chroma is chosen to stay inside 0..1, so no channel
    // is ever clipped -- clipping would bend the hue it is advertising.
    template<> template<>
    void wheel_object::test<14>()
    {
        for (S32 i = 0; i < 72; ++i)
        {
            const LLVector3 ring = ALColorWheelModel::ringColor(deg((F32)i * 5.f));
            for (S32 c = 0; c < 3; ++c)
            {
                ensure("ring channel is strictly inside the gamut",
                       ring.mV[c] > 0.001f && ring.mV[c] < 0.999f);
            }
        }
    }

    // --- numeric entry and reset --------------------------------------------

    // Typing into one field leaves the other two alone and moves the puck.
    template<> template<>
    void wheel_object::test<15>()
    {
        asLift();
        mWheel.setChannel(0, 0.2f);
        ensure_approximately_equals("channel written", mWheel.getRGB().mV[VX], 0.2f, 5);
        ensure_approximately_equals("g untouched", mWheel.getRGB().mV[VY], 0.f, 5);
        ensure_approximately_equals("b untouched", mWheel.getRGB().mV[VZ], 0.f, 5);
        ensure("puck moved off centre", mWheel.getSat() > 0.f);
        ensure_approximately_equals("towards red", angleDelta(mWheel.getHue(), 0.f), 0.f, 3);

        mWheel.setChannel(0, 9.f);
        ensure_approximately_equals("typed value clamped", mWheel.getRGB().mV[VX], 0.5f, 5);

        mWheel.setChannel(7, 1.f);      // out of range, ignored
        ensure_approximately_equals("bad index ignored", mWheel.getRGB().mV[VX], 0.5f, 5);
    }

    // Reset returns the configured neutral for each flavour.
    template<> template<>
    void wheel_object::test<16>()
    {
        asLift();
        mWheel.setPolar(deg(45.f), mWheel.getMaxSat());
        mWheel.reset();
        ensure_approximately_equals("lift neutral is 0", mWheel.getMaster(), 0.f, 5);
        ensure_approximately_equals("and colourless", mWheel.getSat(), 0.f, 5);

        asGain();
        mWheel.reset();
        ensure_approximately_equals("gain neutral is 1", mWheel.getMaster(), 1.f, 5);

        asTint();
        mWheel.reset();
        ensure_approximately_equals("tint neutral is 0.5", mWheel.getMaster(), 0.5f, 5);
        ensure_approximately_equals("tint neutral r", mWheel.getRGB().mV[VX], 0.5f, 5);
    }

    // Re-configuring re-clamps whatever was already held, so switching a wheel
    // from lift to gain cannot leave an out-of-range value behind.
    template<> template<>
    void wheel_object::test<17>()
    {
        asLift();
        mWheel.setRGB(LLVector3(-0.4f, 0.f, 0.3f));
        asGain();
        for (S32 c = 0; c < 3; ++c)
        {
            ensure("re-clamped on configure",
                   mWheel.getRGB().mV[c] >= 0.5f - 1e-5f && mWheel.getRGB().mV[c] <= 1.5f + 1e-5f);
        }
    }

    // A reversed range is accepted rather than producing a negative width.
    template<> template<>
    void wheel_object::test<18>()
    {
        mWheel.configure(0.f, 0.5f, -0.5f, false);
        ensure_approximately_equals("min is the low one", mWheel.getMin(), -0.5f, 5);
        ensure_approximately_equals("max is the high one", mWheel.getMax(), 0.5f, 5);
        ensure("max saturation is positive", mWheel.getMaxSat() > 0.f);
    }
}
