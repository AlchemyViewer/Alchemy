/**
 * @file alplaneset_test.cpp
 * @author Rye
 * @brief The plane set's box test against the camera's plane-at-a-time loop
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye
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

// LLCamera keeps its planes both ways: one at a time for a caller that
// hands in planes of its own, and as a set for its own. The loop is the
// reference; the set must give the same answer for every box, including
// the ones that touch a plane, since the answer decides what is drawn.

#include "linden_common.h"

#include "../test/lltut.h"
#include "../llmath.h"
#include "../llsimdmath.h"
#include "../llvector4a.h"
#include "../llcamera.h"
#include "../alplaneset.h"
#include "../v3math.h"

#include <random>
#include <string>

namespace tut
{
namespace
{
    // A camera at the origin looking down +X, with a frustum from the
    // eight corners the viewer would hand it: the near quad then the far,
    // each bottom-left, bottom-right, top-right, top-left in window terms,
    // window right being -Y and window up +Z.
    void frustum_corners(LLVector3* frust, F32 near_x, F32 far_x, F32 half_y, F32 half_z)
    {
        const F32 ny = half_y * near_x, nz = half_z * near_x;
        const F32 fy = half_y * far_x, fz = half_z * far_x;
        frust[0].set(near_x, ny, -nz);
        frust[1].set(near_x, -ny, -nz);
        frust[2].set(near_x, -ny, nz);
        frust[3].set(near_x, ny, nz);
        frust[4].set(far_x, fy, -fz);
        frust[5].set(far_x, -fy, -fz);
        frust[6].set(far_x, -fy, fz);
        frust[7].set(far_x, fy, fz);
    }

    struct Box
    {
        LLVector4a center, radius;
    };

    // Boxes throughout and around the frustum, some tiny, some huge, and
    // some placed to sit right on a plane.
    std::vector<Box> some_boxes(U32 seed, F32 far_x)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<F32> along(-20.f, far_x + 40.f);
        std::uniform_real_distribution<F32> across(-far_x / 2.f, far_x / 2.f);
        std::uniform_real_distribution<F32> size(0.01f, 10.f);
        std::vector<Box> boxes;
        for (int i = 0; i < 2000; ++i)
        {
            Box b;
            b.center.set(along(rng), across(rng), across(rng), 0.f);
            b.radius.set(size(rng), size(rng), size(rng), 0.f);
            boxes.push_back(b);
        }
        // touching the far plane and the near plane exactly
        Box far_touch;
        far_touch.center.set(far_x - 1.f, 0.f, 0.f, 0.f);
        far_touch.radius.set(1.f, 1.f, 1.f, 0.f);
        boxes.push_back(far_touch);
        Box near_touch;
        near_touch.center.set(2.f, 0.f, 0.f, 0.f);
        near_touch.radius.set(1.f, 0.1f, 0.1f, 0.f);
        boxes.push_back(near_touch);
        return boxes;
    }
}
} // namespace tut

namespace tut
{
    struct alplaneset_data
    {
        LLCamera mCamera;
        static constexpr F32 FAR_X = 100.f;

        alplaneset_data()
        {
            LLVector3 frust[8];
            frustum_corners(frust, 1.f, FAR_X, 0.8f, 0.6f);
            mCamera.calcAgentFrustumPlanes(frust);
        }

        // the plane-at-a-time loop, reached by handing the camera its own
        // planes
        S32 by_loop(const Box& b, bool no_far = false)
        {
            const LLPlane* planes = &mCamera.getAgentPlane(0);
            return no_far ? mCamera.AABBInFrustumNoFarClip(b.center, b.radius, planes) : mCamera.AABBInFrustum(b.center, b.radius, planes);
        }
    };
    typedef test_group<alplaneset_data> alplaneset_test;
    typedef alplaneset_test::object alplaneset_object;
    tut::alplaneset_test alplaneset_testcase("ALPlaneSet");

    // An empty set contains everything; a set with one plane splits space.
    template<> template<>
    void alplaneset_object::test<1>()
    {
        ALPlaneSet empty;
        const LLVector4a somewhere(1e6f, -1e6f, 3.f, 0.f);
        const LLVector4a big(1e5f, 1e5f, 1e5f, 0.f);
        ensure_equals("nothing is outside an empty set", empty.aabbTest(somewhere, big), 2);

        // x <= 10 is inside: normal +x, constant -10
        ALPlaneSet one;
        LLPlane plane(LLVector3(10.f, 0.f, 0.f), LLVector3(1.f, 0.f, 0.f));
        one.set(3, plane, plane.calcPlaneMask());
        ensure_equals("well inside", one.aabbTest(LLVector4a(0.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f)), 2);
        ensure_equals("well outside", one.aabbTest(LLVector4a(20.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f)), 0);
        ensure_equals("straddling", one.aabbTest(LLVector4a(10.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f)), 1);
        ensure_equals("the skipped lane does not count", one.aabbTest(LLVector4a(20.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f), 3), 2);
        one.disable(3);
        ensure_equals("a disabled lane does not count", one.aabbTest(LLVector4a(20.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f)), 2);
    }

    // The camera's set against its loop, over thousands of boxes, with and
    // without the far plane.
    template<> template<>
    void alplaneset_object::test<2>()
    {
        int outside = 0, crossing = 0, inside = 0;
        for (const Box& b : some_boxes(2, FAR_X))
        {
            const S32 expected = by_loop(b);
            const S32 got = mCamera.AABBInFrustum(b.center, b.radius);
            ensure_equals("AABBInFrustum at " + std::to_string(b.center[0]) + " " + std::to_string(b.center[1]) + " " + std::to_string(b.center[2]), got, expected);
            outside += expected == 0;
            crossing += expected == 1;
            inside += expected == 2;

            const S32 expected_no_far = by_loop(b, true);
            ensure_equals("AABBInFrustumNoFarClip at " + std::to_string(b.center[0]), mCamera.AABBInFrustumNoFarClip(b.center, b.radius), expected_no_far);
        }
        ensure("the boxes fell on every side: " + std::to_string(outside) + " " + std::to_string(crossing) + " " + std::to_string(inside),
               outside > 100 && crossing > 100 && inside > 100);

        // the obvious three
        ensure_equals("a box in the middle is inside", mCamera.AABBInFrustum(LLVector4a(50.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f)), 2);
        ensure_equals("a box behind the camera is outside", mCamera.AABBInFrustum(LLVector4a(-50.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f)), 0);
        ensure_equals("a box across the far plane crosses", mCamera.AABBInFrustum(LLVector4a(FAR_X, 0.f, 0.f), LLVector4a(5.f, 5.f, 5.f)), 1);
        ensure_equals("and is inside without the far plane", mCamera.AABBInFrustumNoFarClip(LLVector4a(FAR_X, 0.f, 0.f), LLVector4a(5.f, 5.f, 5.f)), 2);
    }

    // A user clip plane joins the set when enabled and leaves it when
    // disabled; an ignored plane leaves it.
    template<> template<>
    void alplaneset_object::test<3>()
    {
        // z <= 5 is inside
        LLPlane clip(LLVector3(0.f, 0.f, 5.f), LLVector3(0.f, 0.f, 1.f));
        mCamera.setUserClipPlane(clip);
        const Box above = {LLVector4a(50.f, 0.f, 20.f, 0.f), LLVector4a(1.f, 1.f, 1.f, 0.f)};
        ensure_equals("above the clip plane is outside", mCamera.AABBInFrustum(above.center, above.radius), 0);
        ensure_equals("and the loop agrees", by_loop(above), 0);
        for (const Box& b : some_boxes(3, FAR_X))
        {
            ensure_equals("with the clip plane", mCamera.AABBInFrustum(b.center, b.radius), by_loop(b));
        }

        mCamera.disableUserClipPlane();
        ensure_equals("above the disabled clip plane is inside", mCamera.AABBInFrustum(above.center, above.radius), 2);
        ensure_equals("and the loop agrees", by_loop(above), 2);

        mCamera.ignoreAgentFrustumPlane(LLCamera::AGENT_PLANE_FAR);
        const Box beyond = {LLVector4a(FAR_X + 50.f, 0.f, 0.f, 0.f), LLVector4a(1.f, 1.f, 1.f, 0.f)};
        ensure_equals("beyond an ignored far plane is inside", mCamera.AABBInFrustum(beyond.center, beyond.radius), 2);
        ensure_equals("and the loop agrees", by_loop(beyond), 2);
    }

    // The region set is the agent set shifted, far plane aside.
    template<> template<>
    void alplaneset_object::test<4>()
    {
        const LLVector3 shift(-256.f, 128.f, 30.f);
        mCamera.calcRegionFrustumPlanes(shift, FAR_X);
        for (const Box& b : some_boxes(4, FAR_X))
        {
            Box in_agent = b;
            in_agent.center.set(b.center[0] + shift.mV[0], b.center[1] + shift.mV[1], b.center[2] + shift.mV[2], 0.f);
            ensure_equals("AABBInRegionFrustumNoFarClip at " + std::to_string(b.center[0]),
                          mCamera.AABBInRegionFrustumNoFarClip(b.center, b.radius), by_loop(in_agent, true));
        }
        ensure_equals("a region box past the region far clip is outside",
                      mCamera.AABBInRegionFrustum(LLVector4a(FAR_X + 50.f - shift.mV[0], -shift.mV[1], -shift.mV[2], 0.f), LLVector4a(1.f, 1.f, 1.f)), 0);
    }
}
