/**
 * @file math.cpp
 * @author Phoenix
 * @date 2005-09-26
 * @brief Tests for the llmath library.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>
#include "../test/alrandomreseed.h"

#include "llcrc.h"
#include "llrand.h"
#include "lluuid.h"

#include "../llline.h"
#include "../llmath.h"
#include "../v3math.h"
#include "../v3dmath.h"

#include <cmath>

namespace
{
    TEST_CASE("BasicLindenMath: llabs of an S32", "[BasicLindenMath]")
    {
        S32 val = 89543;
        val = llabs(val);
        CHECK(val == 89543);
        val = -500;
        val = llabs(val);
        CHECK(val == 500);
    }

    TEST_CASE("BasicLindenMath: llabs of an F32", "[BasicLindenMath]")
    {
        F32 val = -2583.4f;
        val = llabs(val);
        CHECK(val == 2583.4f);
        val = 430903.f;
        val = llabs(val);
        CHECK(val == 430903.f);
    }

    TEST_CASE("BasicLindenMath: llabs of an F64", "[BasicLindenMath]")
    {
        F64 val = 387439393.987329839;
        val = llabs(val);
        CHECK(val == 387439393.987329839);
        val = -8937843.9394878;
        val = llabs(val);
        CHECK(val == 8937843.9394878);
    }

    TEST_CASE("BasicLindenMath: lltrunc of an F32", "[BasicLindenMath]")
    {
        F32 val = 430903.9f;
        S32 val1 = lltrunc(val);
        CHECK(val1 == 430903);
        val = -2303.9f;
        val1 = lltrunc(val);
        CHECK(val1 == -2303);
    }

    TEST_CASE("BasicLindenMath: lltrunc of an F64", "[BasicLindenMath]")
    {
        F64 val = 387439393.987329839 ;
        S32 val1 = lltrunc(val);
        CHECK(val1 == 387439393);
        val = -387439393.987329839;
        val1 = lltrunc(val);
        CHECK(val1 == -387439393);
    }

    TEST_CASE("BasicLindenMath: llfloor of an F32", "[BasicLindenMath]")
    {
        F32 val = 430903.2f;
        S32 val1 = llfloor(val);
        CHECK(val1 == 430903);
        val = -430903.9f;
        val1 = llfloor(val);
        CHECK(val1 == -430904);
    }

    TEST_CASE("BasicLindenMath: llceil of an F32", "[BasicLindenMath]")
    {
        F32 val = 430903.2f;
        S32 val1 = llceil(val);
        CHECK(val1 == 430904);
        val = -430903.9f;
        val1 = llceil(val);
        CHECK(val1 == -430903);
    }

    TEST_CASE("BasicLindenMath: ll_round of an F32 to an integer", "[BasicLindenMath]")
    {
        F32 val = 430903.2f;
        S32 val1 = ll_round(val);
        CHECK(val1 == 430903);
        val = -430903.9f;
        val1 = ll_round(val);
        CHECK(val1 == -430904);
    }

    TEST_CASE("BasicLindenMath: ll_round of an F32 to the nearest multiple", "[BasicLindenMath]")
    {
        F32 val = 430905.2654f, nearest = 100.f;
        val = ll_round(val, nearest);
        CHECK(val == 430900);
        val = -430905.2654f, nearest = 10.f;
        val = ll_round(val, nearest);
        CHECK(val == -430910);
    }

    TEST_CASE("BasicLindenMath: ll_round of an F64 to the nearest multiple", "[BasicLindenMath]")
    {
        F64 val = 430905.2654, nearest = 100.0;
        val = ll_round(val, nearest);
        CHECK(val == 430900);
        val = -430905.2654, nearest = 10.0;
        val = ll_round(val, nearest);
        CHECK(val == -430910.00000);
    }

    TEST_CASE("BasicLindenMath: llsimple_angle wraps an angle into -pi to pi", "[BasicLindenMath]")
    {
        const F32   F_PI        = 3.1415926535897932384626433832795f;
        F32 angle = 3506.f;
        angle =  llsimple_angle(angle);
        CHECK(angle <=F_PI);
        CHECK(angle >= -F_PI);
        angle = -431.f;
        angle =  llsimple_angle(angle);
        CHECK(angle <=F_PI);
        CHECK(angle >= -F_PI);
    }

    // The floating-point environment this executable was linked with. The
    // build sets flush-to-zero and denormals-are-zero per thread on purpose,
    // in code; nothing may impose them on the whole process from the link
    // line. A denormal that survives a multiply here means no crtfastmath.o
    // was linked.
    TEST_CASE("BasicLindenMath: the process default keeps denormals", "[BasicLindenMath]")
    {
        volatile float denormal = 1.0e-40f;
        volatile float one = 1.0f;
        float product = denormal * one;
        UNSCOPED_INFO("denormals are not flushed by the process default");
        CHECK(product != 0.0f);
        UNSCOPED_INFO("a denormal is still a denormal");
        CHECK(product == 1.0e-40f);
    }

    // Optimised builds relax floating-point semantics to MSVC's /fp:fast
    // contract, which keeps NaN and infinity. -ffinite-math-only would fold
    // both of these guards to a constant.
    TEST_CASE("BasicLindenMath: optimised builds keep NaN and infinity", "[BasicLindenMath]")
    {
        volatile float zero = 0.0f;
        float nan = zero / zero;
        float inf = 1.0f / zero;
        CHECK(std::isnan(nan));
        CHECK(std::isinf(inf));
    }
}

namespace
{
    TEST_CASE("LLCrc: a buffer update equals updating one char at a time", "[LLCrc]")
    {
        /* Test buffer update and individual char update */
        const char TEST_BUFFER[] = "hello &#$)$&Nd0";
        LLCRC c1, c2;
        c1.update((U8*)TEST_BUFFER, sizeof(TEST_BUFFER) - 1);
        char* rh = (char*)TEST_BUFFER;
        while(*rh != '\0')
        {
            c2.update(*rh);
            ++rh;
        }
        CHECK(c1.getCRC() == c2.getCRC());
    }

    TEST_CASE("LLCrc: buffer and char updates mixed in either order agree", "[LLCrc]")
    {
        /* Test mixing of buffer and individual char update */
        const char TEST_BUFFER1[] = "Split Buffer one $^%$%#@$";
        const char TEST_BUFFER2[] = "Split Buffer two )(8723#5dsds";
        LLCRC c1, c2;
        c1.update((U8*)TEST_BUFFER1, sizeof(TEST_BUFFER1) - 1);
        char* rh = (char*)TEST_BUFFER2;
        while(*rh != '\0')
        {
            c1.update(*rh);
            ++rh;
        }

        rh = (char*)TEST_BUFFER1;
        while(*rh != '\0')
        {
            c2.update(*rh);
            ++rh;
        }
        c2.update((U8*)TEST_BUFFER2, sizeof(TEST_BUFFER2) - 1);

        CHECK(c1.getCRC() == c2.getCRC());
    }
}

namespace
{
    F32 SMALL_RADIUS = 1.0f;
    F32 MEDIUM_RADIUS = 5.0f;
    F32 LARGE_RADIUS = 10.0f;

    struct line_data
    {
        // The intersects, nearest-approach and two-planes cases seed the main
        // thread's generator.
        ~line_data() { ll_test::reseedRandomFromEntropy(); }
    };

    TEST_CASE_METHOD(line_data, "LLLine: intersects finds points within a tolerance of the line", "[LLLine]")
    {
        // this is a test for LLLine::intersects(point) which returns true
        // if the line passes within some tolerance of point

        // these tests will have some floating point error,
        // so we need to specify how much error is ok
        F32 allowable_relative_error = 0.00001f;
        S32 number_of_tests = 100;
        // A fixed seed, so a failure draws the same lines when run again.
        ll_rand_seed(296);
        for (S32 test = 0; test < number_of_tests; ++test)
        {
            // generate some random point to be on the line
            LLVector3 point_on_line( ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f);
            point_on_line.normalize();
            point_on_line *= ll_frand(LARGE_RADIUS);

            // generate some random point to "intersect"
            LLVector3 random_direction ( ll_frand(2.f) - 1.f,
                                         ll_frand(2.f) - 1.f,
                                         ll_frand(2.f) - 1.f);
            random_direction.normalize();

            LLVector3 random_offset( ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f);
            random_offset.normalize();
            random_offset *= ll_frand(SMALL_RADIUS);

            LLVector3 point = point_on_line + MEDIUM_RADIUS * random_direction
                + random_offset;

            // compute the axis of approach (a unit vector between the points)
            LLVector3 axis_of_approach = point - point_on_line;
            axis_of_approach.normalize();

            // compute the direction of the the first line (perp to axis_of_approach)
            LLVector3 first_dir( ll_frand(2.f) - 1.f,
                                 ll_frand(2.f) - 1.f,
                                 ll_frand(2.f) - 1.f);
            first_dir.normalize();
            F32 dot = first_dir * axis_of_approach;
            first_dir -= dot * axis_of_approach;    // subtract component parallel to axis
            first_dir.normalize();

            // construct the line
            LLVector3 another_point_on_line = point_on_line + ll_frand(LARGE_RADIUS) * first_dir;
            LLLine line(another_point_on_line, point_on_line);

            // test that the intersection point is within MEDIUM_RADIUS + SMALL_RADIUS
            F32 test_radius = MEDIUM_RADIUS + SMALL_RADIUS;
            test_radius += (LARGE_RADIUS * allowable_relative_error);
            UNSCOPED_INFO("line should pass near intersection point");
            REQUIRE(line.intersects(point, test_radius));

            test_radius = allowable_relative_error * (point - point_on_line).length();
            UNSCOPED_INFO("line should intersect point used to define it");
            REQUIRE(line.intersects(point_on_line, test_radius));
        }
    }

    TEST_CASE_METHOD(line_data, "LLLine: nearestApproach finds each line's point nearest the other", "[LLLine]")
    {
        // this is a test for LLLine::nearestApproach(LLLine) method
        // which computes the point on a line nearest another line

        // Two lines whose nearest points are known exactly: the x axis, and
        // a line along y through (3, 0, 5).
        LLLine x_axis(LLVector3(0.f, 0.f, 0.f), LLVector3(1.f, 0.f, 0.f));
        LLLine above(LLVector3(3.f, -1.f, 5.f), LLVector3(3.f, 1.f, 5.f));
        CHECK(dist_vec(x_axis.nearestApproach(above), LLVector3(3.f, 0.f, 0.f)) < 1e-5f);
        CHECK(dist_vec(above.nearestApproach(x_axis), LLVector3(3.f, 0.f, 5.f)) < 1e-5f);

        // Random lines, held to the nearest points worked out in double from
        // the lines as they were made. Not to the points they were built
        // through: making a line in float moves it, and where the two lines
        // are near parallel that moves their nearest points by far more than
        // nearestApproach's own error. That, not nearestApproach, was the
        // intermittent failure DEV-16600 commented this test out for.
        auto nearest_in_double = [](const LLLine& line, const LLLine& other)
        {
            const LLVector3d p1(line.getPoint()), d1(line.getDirection());
            const LLVector3d p2(other.getPoint()), d2(other.getDirection());
            const LLVector3d between = p1 - p2;
            const F64 aa = d1 * d1, ab = d1 * d2, bb = d2 * d2, a_between = d1 * between, b_between = d2 * between;
            const F64 along = (ab * b_between - bb * a_between) / (aa * bb - ab * ab);
            return LLVector3(p1 + d1 * along);
        };

        F32 allowable_relative_error = 0.001f;
        S32 number_of_tests = 100;
        // A fixed seed, so a failure draws the same lines when run again.
        ll_rand_seed(16600);
        for (S32 test = 0; test < number_of_tests; ++test)
        {
            // generate two points to be our known nearest approaches
            LLVector3 some_point( ll_frand(2.f) - 1.f,
                                  ll_frand(2.f) - 1.f,
                                  ll_frand(2.f) - 1.f);
            some_point.normalize();
            some_point *= ll_frand(LARGE_RADIUS);

            LLVector3 another_point( ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f);
            another_point.normalize();
            another_point *= ll_frand(LARGE_RADIUS);

            // compute the axis of approach (a unit vector between the points)
            LLVector3 axis_of_approach = another_point - some_point;
            axis_of_approach.normalize();

            // compute the direction of the the first line (perp to axis_of_approach)
            LLVector3 first_dir( ll_frand(2.f) - 1.f,
                                 ll_frand(2.f) - 1.f,
                                 ll_frand(2.f) - 1.f);
            F32 dot = first_dir * axis_of_approach;
            first_dir -= dot * axis_of_approach;        // subtract component parallel to axis
            first_dir.normalize();                      // normalize

            // compute the direction of the the second line
            LLVector3 second_dir( ll_frand(2.f) - 1.f,
                                  ll_frand(2.f) - 1.f,
                                  ll_frand(2.f) - 1.f);
            dot = second_dir * axis_of_approach;
            second_dir -= dot * axis_of_approach;
            second_dir.normalize();

            // make sure the lines aren't too parallel,
            dot = fabsf(first_dir * second_dir);
            if (dot > 0.99f)
            {
                // skip this test, we're not interested in testing
                // the intractible cases
                continue;
            }

            // construct the lines
            LLVector3 first_point = some_point + ll_frand(LARGE_RADIUS) * first_dir;
            LLLine first_line(first_point, some_point);

            LLVector3 second_point = another_point + ll_frand(LARGE_RADIUS) * second_dir;
            LLLine second_line(second_point, another_point);

            // compute the points of nearest approach
            LLVector3 some_computed_point = first_line.nearestApproach(second_line);
            LLVector3 another_computed_point = second_line.nearestApproach(first_line);

            // what they should be, for the lines as made
            some_point = nearest_in_double(first_line, second_line);
            another_point = nearest_in_double(second_line, first_line);

            // compute the error
            F32 first_error = (some_point - some_computed_point).length();
            F32 scale = llmax((some_point - another_point).length(), some_point.length());
            scale = llmax(scale, another_point.length());
            scale = llmax(scale, 1.f);
            F32 first_relative_error = first_error / scale;

            F32 second_error = (another_point - another_computed_point).length();
            F32 second_relative_error = second_error / scale;

            // test that the errors are small
            CAPTURE(test, scale, first_dir * second_dir);
            REQUIRE(first_relative_error <= allowable_relative_error);
            REQUIRE(second_relative_error <= allowable_relative_error);
        }
    }

    F32 ALMOST_PARALLEL = 0.99f;

    TEST_CASE_METHOD(line_data, "LLLine: getIntersectionBetweenTwoPlanes on axis and random planes", "[LLLine]")
    {
        // this is a test for LLLine::getIntersectionBetweenTwoPlanes() method

        // first some known tests
        LLLine xy_plane(LLVector3(0.f, 0.f, 2.f), LLVector3(0.f, 0.f, 3.f));
        LLLine yz_plane(LLVector3(2.f, 0.f, 0.f), LLVector3(3.f, 0.f, 0.f));
        LLLine zx_plane(LLVector3(0.f, 2.f, 0.f), LLVector3(0.f, 3.f, 0.f));

        LLLine x_line;
        LLLine y_line;
        LLLine z_line;

        bool x_success = LLLine::getIntersectionBetweenTwoPlanes(x_line, xy_plane, zx_plane);
        bool y_success = LLLine::getIntersectionBetweenTwoPlanes(y_line, yz_plane, xy_plane);
        bool z_success = LLLine::getIntersectionBetweenTwoPlanes(z_line, zx_plane, yz_plane);

        UNSCOPED_INFO("xy and zx planes should intersect");
        CHECK(x_success);
        UNSCOPED_INFO("yz and xy planes should intersect");
        CHECK(y_success);
        UNSCOPED_INFO("zx and yz planes should intersect");
        CHECK(z_success);

        // These directions come from normalize() of a cross product of unit
        // axes -- analytically exact (1, 0, 0) etc., but under -ffast-math the
        // compiler may substitute an approximate rsqrt for 1/sqrt and the unit
        // component drifts to ~0.99997. Use a tolerance instead of ==.
        constexpr F32 AXIS_EPS = 1e-4f;
        LLVector3 direction = x_line.getDirection();
        {
            INFO("x_line should be parallel to x_axis");
            CHECK(fabs(fabs(direction.mV[VX]) - 1.f) < AXIS_EPS);
            CHECK(fabs(direction.mV[VY])         < AXIS_EPS);
            CHECK(fabs(direction.mV[VZ])         < AXIS_EPS);
        }
        direction = y_line.getDirection();
        {
            INFO("y_line should be parallel to y_axis");
            CHECK(fabs(direction.mV[VX])         < AXIS_EPS);
            CHECK(fabs(fabs(direction.mV[VY]) - 1.f) < AXIS_EPS);
            CHECK(fabs(direction.mV[VZ])         < AXIS_EPS);
        }
        direction = z_line.getDirection();
        {
            INFO("z_line should be parallel to z_axis");
            CHECK(fabs(direction.mV[VX])         < AXIS_EPS);
            CHECK(fabs(direction.mV[VY])         < AXIS_EPS);
            CHECK(fabs(fabs(direction.mV[VZ]) - 1.f) < AXIS_EPS);
        }

        // next some random tests
        F32 allowable_relative_error = 0.0001f;
        S32 number_of_tests = 20;
        // A fixed seed, so a failure draws the same planes when run again.
        ll_rand_seed(469);
        for (S32 test = 0; test < number_of_tests; ++test)
        {
            // generate the known line
            LLVector3 some_point( ll_frand(2.f) - 1.f,
                                  ll_frand(2.f) - 1.f,
                                  ll_frand(2.f) - 1.f);
            some_point.normalize();
            some_point *= ll_frand(LARGE_RADIUS);
            LLVector3 another_point( ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f,
                                     ll_frand(2.f) - 1.f);
            another_point.normalize();
            another_point *= ll_frand(LARGE_RADIUS);
            LLLine known_intersection(some_point, another_point);

            // compute a plane that intersect the line
            LLVector3 point_on_plane( ll_frand(2.f) - 1.f,
                                      ll_frand(2.f) - 1.f,
                                      ll_frand(2.f) - 1.f);
            point_on_plane.normalize();
            point_on_plane *= ll_frand(LARGE_RADIUS);
            LLVector3 plane_normal = (point_on_plane - some_point) % known_intersection.getDirection();
            plane_normal.normalize();
            LLLine first_plane(point_on_plane, point_on_plane + plane_normal);

            // compute a different plane that intersect the line
            LLVector3 point_on_different_plane( ll_frand(2.f) - 1.f,
                                                ll_frand(2.f) - 1.f,
                                                ll_frand(2.f) - 1.f);
            point_on_different_plane.normalize();
            point_on_different_plane *= ll_frand(LARGE_RADIUS);
            LLVector3 different_plane_normal = (point_on_different_plane - another_point) % known_intersection.getDirection();
            different_plane_normal.normalize();
            LLLine second_plane(point_on_different_plane, point_on_different_plane + different_plane_normal);

            if (fabs(plane_normal * different_plane_normal) > ALMOST_PARALLEL)
            {
                // the two planes are approximately parallel, so we won't test this case
                continue;
            }

            LLLine measured_intersection;
            bool success = LLLine::getIntersectionBetweenTwoPlanes(
                    measured_intersection,
                    first_plane,
                    second_plane);

            UNSCOPED_INFO("plane intersection should succeed");
            REQUIRE(success);

            F32 dot = fabs(known_intersection.getDirection() * measured_intersection.getDirection());
            UNSCOPED_INFO("measured intersection should be parallel to known intersection");
            REQUIRE(dot > ALMOST_PARALLEL);

            UNSCOPED_INFO("measured intersection should pass near known point");
            REQUIRE(measured_intersection.intersects(some_point, LARGE_RADIUS * allowable_relative_error));
        }
    }
}

namespace
{
    struct uuid_data
    {
        LLUUID id;
    };

    TEST_CASE_METHOD(uuid_data, "mathmisc LLUUID: isNull by default and after setNull but not after generate", "[mathmisc_LLUUID]")
    {
        CHECK(id.isNull());
        id.generate();
        CHECK(id.notNull());
        id.setNull();
        CHECK(id.isNull());
    }

    TEST_CASE_METHOD(uuid_data, "mathmisc LLUUID: copies and assignments compare equal", "[mathmisc_LLUUID]")
    {
        id.generate();
        LLUUID a(id);
        CHECK(id == a);
        a.generate();
        CHECK(id != a);
        a = id;
        CHECK(id == a);
    }

    TEST_CASE_METHOD(uuid_data, "mathmisc LLUUID: xor with a mask twice restores the id", "[mathmisc_LLUUID]")
    {
        id.generate();
        LLUUID copy(id);
        LLUUID mask;
        mask.generate();
        copy ^= mask;
        CHECK(id != copy);
        copy ^= mask;
        CHECK(id == copy);
    }

    TEST_CASE_METHOD(uuid_data, "mathmisc LLUUID: asString round-trips through the string constructor", "[mathmisc_LLUUID]")
    {
        id.generate();
        std::string id_str = id.asString();
        LLUUID copy(id_str.c_str());
        CHECK(id == copy);
    }
}
