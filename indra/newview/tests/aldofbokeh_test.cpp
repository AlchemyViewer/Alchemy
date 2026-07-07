/**
 * @file aldofbokeh_test.cpp
 * @brief Unit tests for the pure DoF bokeh-kernel baking (ALDoFBokeh::bakeKernel)
 *
 * Copyright (c) 2026, Alchemy Viewer Project
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

#include "../aldofbokeh.h"

#include "llmath.h"

namespace tut
{
    struct dofbokeh_data
    {
    };

    typedef test_group<dofbokeh_data> dofbokeh_group;
    typedef dofbokeh_group::object    dofbokeh_object;
    tut::dofbokeh_group dofbokehinst("ALDoFBokeh");

    using namespace ALDoFBokeh;

    // The default/neutral setting values must produce an INACTIVE kernel whose
    // packed constants are the identity the shader's circular fast-path relies on,
    // and a neutral (1.0) intensity.
    template<> template<>
    void dofbokeh_object::test<1>()
    {
        Kernel k = bakeKernel(0, 0.f, 0.f, 1.f, 1.f);
        ensure("neutral input is inactive", !k.active);
        ensure_equals("no polygon segment", k.seg, 0.f);
        ensure_equals("edge factor is unity", k.edge, 1.f);
        ensure_distance("aniso x unity", k.anisoX, 1.f, 1e-6f);
        ensure_distance("aniso y unity", k.anisoY, 1.f, 1e-6f);
        ensure_distance("intensity unity", k.intensity, 1.f, 1e-6f);
    }

    // Any blade count below 3 is treated as "no polygon" (a 2-gon is degenerate).
    template<> template<>
    void dofbokeh_object::test<2>()
    {
        for (S32 n = 0; n < 3; ++n)
        {
            Kernel k = bakeKernel(n, 0.5f, 0.f, 1.f, 1.f);
            ensure("blades<3 is inactive", !k.active);
            ensure_equals("blades<3 has no polygon segment", k.seg, 0.f);
        }
    }

    // A hexagonal aperture bakes exactly the documented N-gon constants.
    template<> template<>
    void dofbokeh_object::test<3>()
    {
        Kernel k = bakeKernel(6, 0.f, 0.f, 1.f, 1.f);
        ensure("hexagon is active", k.active);
        ensure_distance("seg = 2pi/6", k.seg, F_PI / 3.f, 1e-5f);
        ensure_distance("halfSeg = pi/6", k.halfSeg, F_PI / 6.f, 1e-5f);
        ensure_distance("edge = cos(pi/6)", k.edge, cosf(F_PI / 6.f), 1e-5f);
    }

    // Rotation is converted from degrees to radians.
    template<> template<>
    void dofbokeh_object::test<4>()
    {
        Kernel k = bakeKernel(6, 0.f, 90.f, 1.f, 1.f);
        ensure_distance("90 deg -> pi/2 rad", k.rotationRad, F_PI * 0.5f, 1e-5f);
    }

    // Anamorphic squeeze activates on its own and is area preserving.
    template<> template<>
    void dofbokeh_object::test<5>()
    {
        Kernel k = bakeKernel(0, 0.f, 0.f, 1.6f, 1.f);
        ensure("anamorphic alone is active", k.active);
        ensure_distance("area is preserved", k.anisoX * k.anisoY, 1.f, 1e-5f);
        ensure("long axis grows", k.anisoY > 1.f);
        ensure("short axis shrinks", k.anisoX < 1.f);
    }

    // Out-of-range geometry inputs are clamped, never passed through.
    template<> template<>
    void dofbokeh_object::test<6>()
    {
        // blades 999 -> clamped to 12 (still a valid, active polygon);
        // roundness 5 -> clamped to 1; ratio 99 -> clamped to 4 (sqrt = 2).
        Kernel hi = bakeKernel(999, 5.f, 0.f, 99.f, 1.f);
        ensure("clamped blades still active", hi.active);
        ensure("roundness clamped to <= 1", hi.roundness <= 1.f);
        ensure("edge stays a valid cosine", hi.edge > 0.f && hi.edge < 1.f);
        ensure_distance("anamorphic ratio clamped high", hi.anisoY, 2.f, 1e-4f);

        // roundness -1 -> clamped to 0; ratio 0.01 -> clamped to 0.25 (sqrt = 0.5).
        Kernel lo = bakeKernel(6, -1.f, 0.f, 0.01f, 1.f);
        ensure("roundness clamped to >= 0", lo.roundness >= 0.f);
        ensure_distance("anamorphic ratio clamped low", lo.anisoX, 2.f, 1e-4f);
    }

    // Intensity is an independent, clamped passthrough: default 1.0 preserved,
    // out-of-range clamped, and it does not by itself activate the offset reshape.
    template<> template<>
    void dofbokeh_object::test<7>()
    {
        ensure_distance("default intensity passes through", bakeKernel(0, 0.f, 0.f, 1.f, 1.f).intensity, 1.f, 1e-6f);
        ensure_distance("a mid intensity passes through", bakeKernel(0, 0.f, 0.f, 1.f, 4.f).intensity, 4.f, 1e-6f);
        ensure_distance("negative intensity clamps to 0", bakeKernel(0, 0.f, 0.f, 1.f, -5.f).intensity, 0.f, 1e-6f);
        ensure_distance("huge intensity clamps to 16", bakeKernel(0, 0.f, 0.f, 1.f, 999.f).intensity, 16.f, 1e-6f);

        // Intensity alone must not flip the offset reshape on.
        ensure("intensity alone does not activate offset", !bakeKernel(0, 0.f, 0.f, 1.f, 8.f).active);
    }
}
