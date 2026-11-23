/**
 * @file algltfmaterialretry_test.cpp
 * @brief Unit tests for when a glTF material whose fetch failed is fetched again, and when it is given up
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

#include "../algltfmaterialretry.h"

namespace
{
    // Each failure schedules its retry from the time of that failure, however late the retry before it ran.
    TEST_CASE("ALGLTFMaterialRetry: retries are due 5 then 30 then 120 seconds after each failure", "[ALGLTFMaterialRetry]")
    {
        ALGLTFMaterialRetry retry;

        const std::optional<F64> first = retry.recordFailure(100.0);
        REQUIRE(first.has_value());
        CHECK(*first == 105.0);
        CHECK(retry.dueTime() == 105.0);

        const std::optional<F64> second = retry.recordFailure(110.0);
        REQUIRE(second.has_value());
        CHECK(*second == 140.0);
        CHECK(retry.dueTime() == 140.0);

        const std::optional<F64> third = retry.recordFailure(200.0);
        REQUIRE(third.has_value());
        CHECK(*third == 320.0);
        CHECK(retry.dueTime() == 320.0);
    }

    TEST_CASE("ALGLTFMaterialRetry: a failure of the third retry gives the material up", "[ALGLTFMaterialRetry]")
    {
        ALGLTFMaterialRetry retry;
        CHECK_FALSE(retry.isPending());
        CHECK_FALSE(retry.isGivenUp());

        // the first fetch and two retries fail: the third retry is still to come
        for (F64 now : { 0.0, 10.0, 50.0 })
        {
            INFO("a failure at " << now);
            CHECK(retry.recordFailure(now).has_value());
            CHECK(retry.isPending());
            CHECK_FALSE(retry.isGivenUp());
        }

        CHECK_FALSE(retry.recordFailure(200.0).has_value());
        CHECK_FALSE(retry.isPending());
        CHECK(retry.isGivenUp());
        CHECK_FALSE(retry.isDue(1000000.0));

        // a material given up stays given up, however often it fails again
        CHECK_FALSE(retry.recordFailure(300.0).has_value());
        CHECK_FALSE(retry.isPending());
        CHECK(retry.isGivenUp());
    }

    TEST_CASE("ALGLTFMaterialRetry: a material given up early schedules no retry", "[ALGLTFMaterialRetry]")
    {
        ALGLTFMaterialRetry retry;
        REQUIRE(retry.recordFailure(0.0).has_value());

        retry.giveUp();
        CHECK_FALSE(retry.isPending());
        CHECK(retry.isGivenUp());
        CHECK_FALSE(retry.isDue(1000000.0));
        CHECK_FALSE(retry.recordFailure(10.0).has_value());
    }

    TEST_CASE("ALGLTFMaterialRetry: a retry is due from its time on and not before", "[ALGLTFMaterialRetry]")
    {
        ALGLTFMaterialRetry retry;
        // nothing has failed: nothing is due, whatever the time
        CHECK_FALSE(retry.isDue(0.0));
        CHECK_FALSE(retry.isDue(1000000.0));

        REQUIRE(retry.recordFailure(100.0).has_value());
        CHECK_FALSE(retry.isDue(100.0));
        CHECK_FALSE(retry.isDue(104.5));
        CHECK(retry.isDue(105.0));
        CHECK(retry.isDue(1000.0));
    }

    TEST_CASE("ALGLTFMaterialRetry: a success forgets every failure", "[ALGLTFMaterialRetry]")
    {
        ALGLTFMaterialRetry retry;
        REQUIRE(retry.recordFailure(0.0).has_value());
        REQUIRE(retry.recordFailure(10.0).has_value());

        retry.reset();
        CHECK_FALSE(retry.isPending());
        CHECK_FALSE(retry.isGivenUp());
        CHECK_FALSE(retry.isDue(1000000.0));

        // the next failure starts the schedule over, at its first wait
        const std::optional<F64> due = retry.recordFailure(500.0);
        REQUIRE(due.has_value());
        CHECK(*due == 505.0);

        // a material given up is forgiven by a success too
        CHECK(retry.recordFailure(600.0).has_value());
        CHECK(retry.recordFailure(700.0).has_value());
        CHECK_FALSE(retry.recordFailure(800.0).has_value());
        REQUIRE(retry.isGivenUp());
        retry.reset();
        CHECK_FALSE(retry.isGivenUp());
        const std::optional<F64> after_given_up = retry.recordFailure(900.0);
        REQUIRE(after_given_up.has_value());
        CHECK(*after_given_up == 905.0);
    }
}
