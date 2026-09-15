/**
 * @file noise_test.cpp
 * @brief The terrain noise tables are the same on every platform
 *
 * Copyright (c) 2026, Rye
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

#include "../noise.h"

#include <cstdlib>

namespace tut
{
    // Every expected value below was read off the tables as MSVC's CRT built them from
    // srand(42) / rand(), which is what every Windows viewer has textured terrain from. A
    // platform whose tables differ paints its terrain differently from every other platform.
    struct noise_data
    {
        noise_data()
        {
            // The tables fill on the first evaluation.
            F32 origin[2] = { 0.f, 0.f };
            noise2(origin);
        }
    };

    typedef test_group<noise_data> noise_group;
    typedef noise_group::object    noise_object;
    tut::noise_group ng("noise");

    // The generator is MSVC's rand(). Where that CRT is the one linked, it is the reference.
    template<> template<>
    void noise_object::test<1>()
    {
#ifdef _MSC_VER
        srand(42);
        ALNoiseRand rand(42);
        for (int i = 0; i < 65536; ++i)
        {
            ensure_equals("draw agrees with the CRT", rand.next(), ::rand());
        }
#else
        skip("the CRT here is not the reference generator");
#endif
    }

    // The same draws, written down, for the platforms whose CRT is not the reference.
    template<> template<>
    void noise_object::test<2>()
    {
        static const S32 expected[16] = {
            175, 400, 17869, 30056, 16083, 12879, 8016, 7644,
            15809, 1769, 32409, 29950, 13471, 7099, 2336, 23225
        };
        ALNoiseRand rand(42);
        for (int i = 0; i < 16; ++i)
        {
            ensure_equals("draw", rand.next(), expected[i]);
        }
    }

    // The permutation table: its head, its whole (as a weighted sum), and its wrap.
    template<> template<>
    void noise_object::test<3>()
    {
        static const S32 head[32] = {
            204, 117, 7, 120, 49, 222, 28, 96, 237, 17, 30, 184, 85, 140, 181, 18,
            38, 91, 243, 40, 172, 232, 118, 145, 219, 253, 201, 24, 164, 215, 252, 210
        };
        for (int i = 0; i < 32; ++i)
        {
            ensure_equals("p head", p[i], head[i]);
        }
        long long sum = 0;
        for (int i = 0; i < 256; ++i)
        {
            sum += (long long)p[i] * (i + 1);
        }
        ensure_equals("p weighted sum", sum, 4318278LL);
        ensure_equals("p wraps at 256", p[257], 117);
        ensure_equals("p wraps at 512", p[513], 117);
    }

    // The gradient tables. g1 holds multiples of 1/256 and compares exactly; g2 and g3 are
    // normalised, so they get the tolerance a float sqrt needs and no more.
    template<> template<>
    void noise_object::test<4>()
    {
        static const F32 g1_head[8] = {
            -0.31640625f, 0.3125f, -0.37890625f, -0.94140625f,
            0.8984375f, 0.8984375f, 0.50390625f, -0.19140625f
        };
        for (int i = 0; i < 8; ++i)
        {
            ensure_equals("g1 head", g1[i], g1_head[i]);
        }

        static const F32 g2_head[4][2] = {
            { 0.574801087f, 0.818293214f },
            { 0.751729369f, 0.659471691f },
            { 0.985672355f, 0.168671206f },
            { -0.561883032f, -0.827216685f }
        };
        for (int i = 0; i < 4; ++i)
        {
            ensure_approximately_equals("g2 head x", g2[i][0], g2_head[i][0], 20);
            ensure_approximately_equals("g2 head y", g2[i][1], g2_head[i][1], 20);
        }

        static const F32 g3_head[4][3] = {
            { 0.494845539f, -0.214115858f, -0.842189074f },
            { -0.217894495f, -0.975788414f, -0.018947348f },
            { -0.414242476f, -0.350064069f, 0.840153754f },
            { -0.393896222f, 0.645989835f, -0.653867722f }
        };
        for (int i = 0; i < 4; ++i)
        {
            ensure_approximately_equals("g3 head x", g3[i][0], g3_head[i][0], 20);
            ensure_approximately_equals("g3 head y", g3[i][1], g3_head[i][1], 20);
            ensure_approximately_equals("g3 head z", g3[i][2], g3_head[i][2], 20);
        }
    }
}
