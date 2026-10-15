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

#include <catch2/catch_test_macros.hpp>

#include "../aldaycyclelandmarks.h"

#include <cmath>

namespace
{
    // At file scope, where every case and every lambda in one sees it.
    constexpr F32 TWO_PI = 6.28318530718f;
}

namespace
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

    // A plain sine puts sunrise at 0, noon at a quarter, sunset at a half and
    // midnight at three quarters.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: a plain sine has its four landmarks a quarter apart from 0", "[ALDayCycleLandmarks]")
    {
        const auto marks = ALDayCycleLandmarks::find(sine());

        CHECK(marks.has_sunrise);
        CHECK(marks.has_noon);
        CHECK(marks.has_sunset);
        CHECK(marks.has_midnight);

        CHECK(apart(marks.sunrise, 0.f) < 0.02f);
        CHECK(apart(marks.noon, 0.25f) < 0.02f);
        CHECK(apart(marks.sunset, 0.5f) < 0.02f);
        CHECK(apart(marks.midnight, 0.75f) < 0.02f);
    }

    // Every landmark moves with the cycle. This is the case that a hardcoded
    // "noon is 0.5" gets wrong, and the reason this code exists.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: every landmark moves with the phase of the cycle", "[ALDayCycleLandmarks]")
    {
        const F32 phase = 0.3f;
        const auto marks = ALDayCycleLandmarks::find(sine(phase));

        CHECK(apart(marks.sunrise, phase) < 0.02f);
        CHECK(apart(marks.noon, phase + 0.25f) < 0.02f);
        CHECK(apart(marks.sunset, std::fmod(phase + 0.5f, 1.f)) < 0.02f);
        CHECK(apart(marks.midnight, std::fmod(phase + 0.75f, 1.f)) < 0.02f);
    }

    // A crossing that falls between two samples is interpolated, so the answer
    // is better than the sample grid rather than snapped to it.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: a crossing between two samples is found finer than the grid", "[ALDayCycleLandmarks]")
    {
        // Sunrise sits at 0.1, which no 16-sample grid position lands on.
        const auto marks = ALDayCycleLandmarks::find(sine(0.1f), 16);
        const F32 grid = 1.f / 16.f;

        CHECK(marks.has_sunrise);
        UNSCOPED_INFO("beats the grid");
        CHECK(apart(marks.sunrise, 0.1f) < grid * 0.5f);
    }

    // A sun that never sets has a brightest moment and nothing else: naming a
    // sunrise there would be inventing one.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: a sun that never sets has a noon and nothing else", "[ALDayCycleLandmarks]")
    {
        const auto marks = ALDayCycleLandmarks::find(
            [](F32 p) { return 0.5f + 0.25f * std::sin(TWO_PI * p); });

        CHECK(marks.has_noon);
        CHECK_FALSE(marks.has_sunrise);
        CHECK_FALSE(marks.has_sunset);
        CHECK_FALSE(marks.has_midnight);
        UNSCOPED_INFO("noon at the peak");
        CHECK(apart(marks.noon, 0.25f) < 0.02f);
    }

    // A sun that never rises is the same argument the other way up.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: a sun that never rises has a midnight and nothing else", "[ALDayCycleLandmarks]")
    {
        const auto marks = ALDayCycleLandmarks::find(
            [](F32 p) { return -0.5f + 0.25f * std::sin(TWO_PI * p); });

        CHECK(marks.has_midnight);
        CHECK_FALSE(marks.has_noon);
        CHECK_FALSE(marks.has_sunrise);
        CHECK_FALSE(marks.has_sunset);
    }

    // A cycle built from one repeated frame -- which is what the viewer's own
    // default day cycle actually is -- has no moment to single out.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: a cycle of one repeated frame has no landmarks", "[ALDayCycleLandmarks]")
    {
        const auto marks = ALDayCycleLandmarks::find([](F32) { return 0.5f; });

        CHECK_FALSE(marks.any());
    }

    // Landmarks are cycle positions, so they stay inside [0, 1) even when the
    // crossing they came from sits across the seam. Each is found first: an
    // absent one is left at 0, which is inside the cycle whatever happened.
    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: landmarks stay inside the cycle when a crossing is across the seam", "[ALDayCycleLandmarks]")
    {
        for (S32 i = 0; i < 20; ++i)
        {
            const F32 phase = (F32)i / 20.f;
            const auto marks = ALDayCycleLandmarks::find(sine(phase));
            CAPTURE(phase);

            REQUIRE(marks.has_sunrise);
            REQUIRE(marks.has_noon);
            REQUIRE(marks.has_sunset);
            REQUIRE(marks.has_midnight);
            REQUIRE(marks.sunrise >= 0.f);
            REQUIRE(marks.sunrise < 1.f);
            REQUIRE(marks.noon >= 0.f);
            REQUIRE(marks.noon < 1.f);
            REQUIRE(marks.sunset >= 0.f);
            REQUIRE(marks.sunset < 1.f);
            REQUIRE(marks.midnight >= 0.f);
            REQUIRE(marks.midnight < 1.f);
        }
    }

    TEST_CASE_METHOD(daycycle_landmarks, "ALDayCycleLandmarks: no sampler or too few samples finds nothing", "[ALDayCycleLandmarks]")
    {
        CHECK_FALSE(ALDayCycleLandmarks::find(nullptr).any());
        CHECK_FALSE(ALDayCycleLandmarks::find(sine(), 1).any());
    }
}
