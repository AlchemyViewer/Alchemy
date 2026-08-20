/**
 * @file aldaycyclelandmarks_test.cpp
 * @brief Unit tests for finding landmarks in a day cycle
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

#include "../aldaycyclelandmarks.h"

#include <cmath>

namespace
{
    // File scope rather than a fixture member: TUT's test bodies reach the
    // fixture through a dependent base, so a name inherited from it is not
    // visible inside a lambda written in one.
    constexpr F32 TWO_PI = 6.28318530718f;
}

namespace tut
{
    struct daycycle_landmarks
    {
        /// The ordinary case: one rise and one set per cycle, noon halfway
        /// between them. Offsetting the phase moves every landmark with it,
        /// which is the whole point of searching rather than assuming.
        static ALDayCycleLandmarks::altitude_sampler_t sine(F32 phase = 0.f)
        {
            return [phase](F32 p) { return std::sin(TWO_PI * (p - phase)); };
        }

        /// How far apart two cycle positions are, the short way round. A
        /// landmark at 0.999 and one at 0.001 are neighbours, not opposites.
        static F32 apart(F32 a, F32 b)
        {
            const F32 d = std::fabs(a - b);
            return (d > 0.5f) ? (1.f - d) : d;
        }
    };

    typedef test_group<daycycle_landmarks> landmark_group;
    typedef landmark_group::object         landmark_object;
    tut::landmark_group lg("ALDayCycleLandmarks");

    // A plain sine puts sunrise at 0, noon at a quarter, sunset at a half and
    // midnight at three quarters.
    template<> template<>
    void landmark_object::test<1>()
    {
        const auto marks = ALDayCycleLandmarks::find(sine());

        ensure("has sunrise", marks.has_sunrise);
        ensure("has noon", marks.has_noon);
        ensure("has sunset", marks.has_sunset);
        ensure("has midnight", marks.has_midnight);

        ensure("sunrise at 0", apart(marks.sunrise, 0.f) < 0.02f);
        ensure("noon at 0.25", apart(marks.noon, 0.25f) < 0.02f);
        ensure("sunset at 0.5", apart(marks.sunset, 0.5f) < 0.02f);
        ensure("midnight at 0.75", apart(marks.midnight, 0.75f) < 0.02f);
    }

    // Every landmark moves with the cycle. This is the case that a hardcoded
    // "noon is 0.5" gets wrong, and the reason this code exists.
    template<> template<>
    void landmark_object::test<2>()
    {
        const F32 phase = 0.3f;
        const auto marks = ALDayCycleLandmarks::find(sine(phase));

        ensure("sunrise moved", apart(marks.sunrise, phase) < 0.02f);
        ensure("noon moved", apart(marks.noon, phase + 0.25f) < 0.02f);
        ensure("sunset moved", apart(marks.sunset, std::fmod(phase + 0.5f, 1.f)) < 0.02f);
        ensure("midnight moved", apart(marks.midnight, std::fmod(phase + 0.75f, 1.f)) < 0.02f);
    }

    // A crossing that falls between two samples is interpolated, so the answer
    // is better than the sample grid rather than snapped to it.
    template<> template<>
    void landmark_object::test<3>()
    {
        // Sunrise sits at 0.1, which no 16-sample grid position lands on.
        const auto marks = ALDayCycleLandmarks::find(sine(0.1f), 16);
        const F32 grid = 1.f / 16.f;

        ensure("has sunrise", marks.has_sunrise);
        ensure("beats the grid", apart(marks.sunrise, 0.1f) < grid * 0.5f);
    }

    // A sun that never sets has a brightest moment and nothing else: naming a
    // sunrise there would be inventing one.
    template<> template<>
    void landmark_object::test<4>()
    {
        const auto marks = ALDayCycleLandmarks::find(
            [](F32 p) { return 0.5f + 0.25f * std::sin(TWO_PI * p); });

        ensure("has noon", marks.has_noon);
        ensure("no sunrise", !marks.has_sunrise);
        ensure("no sunset", !marks.has_sunset);
        ensure("no midnight", !marks.has_midnight);
        ensure("noon at the peak", apart(marks.noon, 0.25f) < 0.02f);
    }

    // A sun that never rises is the same argument the other way up.
    template<> template<>
    void landmark_object::test<5>()
    {
        const auto marks = ALDayCycleLandmarks::find(
            [](F32 p) { return -0.5f + 0.25f * std::sin(TWO_PI * p); });

        ensure("has midnight", marks.has_midnight);
        ensure("no noon", !marks.has_noon);
        ensure("no sunrise", !marks.has_sunrise);
        ensure("no sunset", !marks.has_sunset);
    }

    // A cycle built from one repeated frame -- which is what the viewer's own
    // default day cycle actually is -- has no moment to single out.
    template<> template<>
    void landmark_object::test<6>()
    {
        const auto marks = ALDayCycleLandmarks::find([](F32) { return 0.5f; });

        ensure("nothing to find", !marks.any());
    }

    // Landmarks are cycle positions, so they stay inside [0, 1) even when the
    // crossing they came from sits across the seam.
    template<> template<>
    void landmark_object::test<7>()
    {
        for (S32 i = 0; i < 20; ++i)
        {
            const F32 phase = (F32)i / 20.f;
            const auto marks = ALDayCycleLandmarks::find(sine(phase));

            ensure("sunrise in range", marks.sunrise >= 0.f && marks.sunrise < 1.f);
            ensure("noon in range", marks.noon >= 0.f && marks.noon < 1.f);
            ensure("sunset in range", marks.sunset >= 0.f && marks.sunset < 1.f);
            ensure("midnight in range", marks.midnight >= 0.f && marks.midnight < 1.f);
        }
    }

    // Nothing to sample with, nothing to say.
    template<> template<>
    void landmark_object::test<8>()
    {
        ensure("no sampler", !ALDayCycleLandmarks::find(nullptr).any());
        ensure("too few samples", !ALDayCycleLandmarks::find(sine(), 1).any());
    }
}
