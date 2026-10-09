/**
 * @file llrandom_test.cpp
 * @author Phoenix
 * @date 2007-01-25
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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
#include "../test/lltut.h"

#include "../llrand.h"
#include "../alrandmap.h"
#include "stringize.h"

#include <climits>
#include <cmath>
#include <limits>
#include <thread>
#include <vector>

// In llrand.h, every function is documented to return less than the high end
// -- specifically, because you can pass a negative extent, they're documented
// never to return a value equal to the extent.
// So that we don't need two different versions of ensure_in_range(), when
// testing extent < 0, negate the return value and the extent before passing
// into ensure_in_range().
template <typename NUMBER>
static void ensure_in_range(const std::string_view& name,
                            NUMBER value, NUMBER low, NUMBER high)
{
    auto failmsg{ stringize(name, " >= ", low, " (", value, ')') };
    tut::ensure(failmsg, (value >= low));
    failmsg = stringize(name, " < ", high, " (", value, ')');
    tut::ensure(failmsg, (value < high));
}

namespace
{
    // A normal deviate exceeded one time in a million. The statistical tests
    // below draw from fixed seeds, so they pass or fail the same way every
    // run; this bound is so that any sound generator passes them, not just
    // the one whose draws they were first run on.
    constexpr F64 ONE_IN_A_MILLION_Z = 4.753424;

    // The chi-square statistic a sound generator's counts exceed one time in
    // a million, for `dof` degrees of freedom, by the Wilson-Hilferty
    // approximation. For one or two degrees of freedom it overstates the
    // bound, which errs toward passing.
    F64 chi_square_limit(size_t dof)
    {
        const F64 k = F64(dof);
        const F64 c = 1.0 - 2.0 / (9.0 * k) + ONE_IN_A_MILLION_Z * std::sqrt(2.0 / (9.0 * k));
        return k * c * c * c;
    }

    // That `draws` spread over `counts` as evenly as chance allows.
    void ensure_uniform(const std::string& name, const std::vector<U64>& counts, U64 draws)
    {
        const F64 expected = F64(draws) / F64(counts.size());
        F64 stat = 0.0;
        for (const U64 count : counts)
        {
            const F64 off = F64(count) - expected;
            stat += off * off / expected;
        }
        const F64 limit = chi_square_limit(counts.size() - 1);
        tut::ensure(stringize(name, ": chi-square ", stat, " under ", limit), stat < limit);
    }

    // That draws uniform over [0, 1) have its mean, 1/2, and variance, 1/12.
    void ensure_unit_moments(const std::string& name, F64 sum, F64 sum_of_squares, U64 draws)
    {
        const F64 n        = F64(draws);
        const F64 mean     = sum / n;
        const F64 variance = sum_of_squares / n - mean * mean;
        // The spread of the mean is the variance over n; of the variance, its
        // fourth central moment, 1/80, less its square, over n.
        const F64 mean_bound     = ONE_IN_A_MILLION_Z * std::sqrt(1.0 / 12.0 / n);
        const F64 variance_bound = ONE_IN_A_MILLION_Z * std::sqrt((1.0 / 80.0 - 1.0 / 144.0) / n);
        tut::ensure(stringize(name, ": mean ", mean), std::fabs(mean - 0.5) < mean_bound);
        tut::ensure(stringize(name, ": variance ", variance), std::fabs(variance - 1.0 / 12.0) < variance_bound);
    }

    // Every kind of draw, interleaved, so a sequence differs if any of them
    // does.
    std::vector<F64> draw_each(S32 rounds)
    {
        std::vector<F64> out;
        for (S32 i = 0; i < rounds; ++i)
        {
            out.push_back(ll_drand());
            out.push_back(ll_frand());
            out.push_back(ll_rand(1000));
            out.push_back(ll_frand(-5.f));
            out.push_back(ll_drand(3.0));
            out.push_back(ll_rand_u32());
        }
        return out;
    }

    std::vector<F64> draw_drand(S32 count)
    {
        std::vector<F64> out;
        for (S32 i = 0; i < count; ++i)
        {
            out.push_back(ll_drand());
        }
        return out;
    }
}

namespace tut
{
    struct random
    {
    };

    typedef test_group<random> random_t;
    typedef random_t::object random_object_t;
    tut::random_t tut_random("LLSeedRand");

    template<> template<>
    void random_object_t::test<1>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            ensure_in_range("frand", ll_frand(), 0.0f, 1.0f);
        }
    }

    template<> template<>
    void random_object_t::test<2>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            ensure_in_range("drand", ll_drand(), 0.0, 1.0);
        }
    }

    template<> template<>
    void random_object_t::test<3>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            ensure_in_range("frand(2.0f)", ll_frand(2.0f) - 1.0f, -1.0f, 1.0f);
        }
    }

    template<> template<>
    void random_object_t::test<4>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            // Negate the result so we don't have to allow a templated low-end
            // comparison as well.
            ensure_in_range("-frand(-7.0)", -ll_frand(-7.0), 0.0f, 7.0f);
        }
    }

    template<> template<>
    void random_object_t::test<5>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            ensure_in_range("-drand(-2.0)", -ll_drand(-2.0), 0.0, 2.0);
        }
    }

    template<> template<>
    void random_object_t::test<6>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            ensure_in_range("rand(100)", ll_rand(100), 0, 100);
        }
    }

    template<> template<>
    void random_object_t::test<7>()
    {
        for(S32 ii = 0; ii < 100000; ++ii)
        {
            ensure_in_range("-rand(-127)", -ll_rand(-127), 0, 127);
        }
    }

    // Tests 8 to 15 hold for any generator under llrand. Tests 16 to 18 are
    // what the generator itself draws, and change when it does.

    template<> template<>
    void random_object_t::test<8>()
    {
        set_test_name("an extent of zero or one gives zero, and the widest extents hold their draws");
        ll_rand_seed(8);
        for (S32 ii = 0; ii < 10000; ++ii)
        {
            ensure_equals("rand(0)", ll_rand(0), 0);
            ensure_equals("rand(1)", ll_rand(1), 0);
            ensure_equals("rand(-1)", ll_rand(-1), 0);
            ensure_equals("frand(0)", ll_frand(0.f), 0.f);
            ensure_equals("drand(0)", ll_drand(0.0), 0.0);

            ensure_in_range("rand(INT_MAX)", ll_rand(INT_MAX), 0, INT_MAX);
            const S32 lowest = ll_rand(INT_MIN);
            ensure(stringize("rand(INT_MIN) in (INT_MIN, 0] (", lowest, ')'), lowest > INT_MIN && lowest <= 0);

            ensure_in_range("frand(FLT_MAX)", ll_frand(std::numeric_limits<F32>::max()), 0.f, std::numeric_limits<F32>::max());
            ensure_in_range("frand(FLT_MIN)", ll_frand(std::numeric_limits<F32>::min()), 0.f, std::numeric_limits<F32>::min());
            ensure_in_range("drand(DBL_MAX)", ll_drand(std::numeric_limits<F64>::max()), 0.0, std::numeric_limits<F64>::max());
            ensure_in_range("drand(DBL_MIN)", ll_drand(std::numeric_limits<F64>::min()), 0.0, std::numeric_limits<F64>::min());
        }
    }

    template<> template<>
    void random_object_t::test<9>()
    {
        set_test_name("a seed draws the same again, on any thread, and another seed draws otherwise");
        ll_rand_seed(1234);
        const std::vector<F64> first = draw_each(16);
        ll_rand_seed(1234);
        ensure("the same seed draws the same", draw_each(16) == first);

        std::vector<F64> elsewhere;
        std::thread([&elsewhere] {
            ll_rand_seed(1234);
            elsewhere = draw_each(16);
        }).join();
        ensure("on another thread too", elsewhere == first);

        ll_rand_seed(1235);
        ensure("another seed draws otherwise", draw_each(16) != first);
    }

    template<> template<>
    void random_object_t::test<10>()
    {
        set_test_name("rand(n) draws each of its n values as often as the others");
        ll_rand_seed(10);
        const U64 draws = 256000;
        for (const S32 n : { 2, 3, 7, 100, 256 })
        {
            std::vector<U64> counts(n);
            for (U64 ii = 0; ii < draws; ++ii)
            {
                ++counts.at(ll_rand(n));
            }
            ensure_uniform(stringize("rand(", n, ")"), counts, draws);
        }

        std::vector<U64> counts(7);
        for (U64 ii = 0; ii < draws; ++ii)
        {
            ++counts.at(-ll_rand(-7));
        }
        ensure_uniform("rand(-7)", counts, draws);
    }

    template<> template<>
    void random_object_t::test<11>()
    {
        set_test_name("frand() and drand() spread evenly over [0, 1)");
        ll_rand_seed(11);
        const U64        draws = 1 << 20;
        std::vector<U64> frand_bins(64);
        std::vector<U64> drand_bins(64);
        F64              frand_sum = 0.0, frand_squares = 0.0;
        F64              drand_sum = 0.0, drand_squares = 0.0;
        for (U64 ii = 0; ii < draws; ++ii)
        {
            const F32 f = ll_frand();
            ++frand_bins.at(size_t(f * 64.f));
            frand_sum += f;
            frand_squares += F64(f) * f;

            const F64 d = ll_drand();
            ++drand_bins.at(size_t(d * 64.0));
            drand_sum += d;
            drand_squares += d * d;
        }
        ensure_uniform("frand", frand_bins, draws);
        ensure_uniform("drand", drand_bins, draws);
        ensure_unit_moments("frand", frand_sum, frand_squares, draws);
        ensure_unit_moments("drand", drand_sum, drand_squares, draws);
    }

    template<> template<>
    void random_object_t::test<12>()
    {
        set_test_name("rand_u32() draws each of its bytes evenly and sets each bit half the time");
        ll_rand_seed(12);
        const U64                     draws = 256000;
        std::vector<std::vector<U64>> bytes(4, std::vector<U64>(256));
        std::vector<U64>              ones(32);
        for (U64 ii = 0; ii < draws; ++ii)
        {
            const U32 value = ll_rand_u32();
            for (U32 byte = 0; byte < 4; ++byte)
            {
                ++bytes[byte][(value >> (8 * byte)) & 0xFF];
            }
            for (U32 bit = 0; bit < 32; ++bit)
            {
                ones[bit] += (value >> bit) & 1;
            }
        }
        for (U32 byte = 0; byte < 4; ++byte)
        {
            ensure_uniform(stringize("byte ", byte), bytes[byte], draws);
        }
        for (U32 bit = 0; bit < 32; ++bit)
        {
            ensure_uniform(stringize("bit ", bit), { ones[bit], draws - ones[bit] }, draws);
        }
    }

    template<> template<>
    void random_object_t::test<13>()
    {
        set_test_name("a thread that was never seeded draws apart from every other");
        std::vector<F64> one, two;
        std::thread      first([&one] { one = draw_drand(8); });
        std::thread      second([&two] { two = draw_drand(8); });
        first.join();
        second.join();
        const std::vector<F64> here = draw_drand(8);
        ensure("two threads draw apart", one != two);
        ensure("and apart from this one", one != here && two != here);
    }

    template<> template<>
    void random_object_t::test<14>()
    {
        set_test_name("seeding and drawing on another thread leaves this thread's draws alone");
        ll_rand_seed(14);
        std::vector<F64> drawn = draw_drand(4);
        std::thread([] {
            ll_rand_seed(99);
            draw_drand(100);
        }).join();
        const std::vector<F64> after = draw_drand(4);
        drawn.insert(drawn.end(), after.begin(), after.end());

        ll_rand_seed(14);
        ensure("this thread's draws went on as if alone", draw_drand(8) == drawn);
    }

    template<> template<>
    void random_object_t::test<15>()
    {
        set_test_name("a unit value scales to each extent, an integer truncated toward zero, and never reaches it");
        using namespace ALRandMap;
        const F64 top53 = 1.0 - 0x1p-53;
        const F64 top48 = 1.0 - 0x1p-48;

        ensure_equals("0 of 100", extentS32(0.0, 100), 0);
        ensure_equals("a half of 100", extentS32(0.5, 100), 50);
        ensure_equals("the top of 100, to 48 bits", extentS32(top48, 100), 99);
        ensure_equals("the top of 100, to 53 bits", extentS32(top53, 100), 99);
        ensure_equals("the top of INT_MAX", extentS32(top53, INT_MAX), INT_MAX - 1);
        ensure_equals("the top of 2^30", extentS32(top53, 1 << 30), (1 << 30) - 1);
        ensure_equals("a half of -7, toward zero", extentS32(0.5, -7), -3);
        ensure_equals("the top of -7", extentS32(top53, -7), -6);
        ensure_equals("the top of INT_MIN", extentS32(top53, INT_MIN), INT_MIN + 1);
        ensure_equals("a unit of 1 gives 0, not the extent", extentS32(1.0, 100), 0);
        ensure_equals("nor the negative extent", extentS32(1.0, -100), 0);

        ensure_equals("a half of 2", extentReal<F32>(0.5f, 2.f), 1.f);
        ensure_equals("a half of -7", extentReal<F32>(0.5f, -7.f), -3.5f);
        ensure_equals("0 of 0", extentReal<F64>(0.5, 0.0), 0.0);
        ensure("the top F32 of 3 stays under 3", extentReal<F32>(1.f - 0x1p-24f, 3.f) < 3.f);
        ensure("the top F64 of 3 stays under 3", extentReal<F64>(top53, 3.0) < 3.0);
        ensure("the top F64 of -3 stays over -3", extentReal<F64>(top53, -3.0) > -3.0);
        ensure_equals("a unit of 1 gives 0, not the extent", extentReal<F32>(1.f, 2.f), 0.f);
        ensure_equals("nor the negative extent", extentReal<F64>(1.0, -2.0), 0.0);
    }

    template<> template<>
    void random_object_t::test<16>()
    {
        set_test_name("a draw's top 53 bits are the F64, its top 24 the F32 and its top 32 the U32, each exact");
        using namespace ALRandMap;
        const U64 all = ~U64(0);

        ensure_equals("0", unitF64(0), 0.0);
        ensure_equals("a half", unitF64(U64(1) << 63), 0.5);
        ensure_equals("the top", unitF64(all), 1.0 - 0x1p-53);
        ensure_equals("the low 11 bits are passed over", unitF64(0x7FF), 0.0);
        ensure_equals("the lowest bit used", unitF64(0x800), 0x1p-53);

        ensure_equals("0", unitF32(0), 0.f);
        ensure_equals("a half", unitF32(U64(1) << 63), 0.5f);
        ensure_equals("the top, which does not round up to 1", unitF32(all), 1.f - 0x1p-24f);
        ensure_equals("the low 40 bits are passed over", unitF32((U64(1) << 40) - 1), 0.f);
        ensure_equals("the lowest bit used", unitF32(U64(1) << 40), 0x1p-24f);

        ensure_equals("the bits of 0", bitsU32(0), 0u);
        ensure_equals("the top 32 bits", bitsU32(0x123456789ABCDEF0), 0x12345678u);
        ensure_equals("the bits of the top", bitsU32(all), 0xFFFFFFFFu);
    }

    template<> template<>
    void random_object_t::test<17>()
    {
        set_test_name("seed 42 draws what std::mt19937_64 does, from every standard library");
        const F64 drand[] = { 0x1.82a3befaddcbcp-1, 0x1.472f1f73724ap-1,  0x1.81192cfe1cbcfp-1, 0x1.171621fc50d68p-3,
                              0x1.ce79451c9a6c3p-1, 0x1.814dc629c7f48p-4, 0x1.262e1432cba84p-1, 0x1.7dd645e8fadeep-2 };
        const F32 frand[] = { 0x1.82a3bep-1f, 0x1.472f1ep-1f, 0x1.81192cp-1f, 0x1.17162p-3f,
                              0x1.ce7944p-1f, 0x1.814dcp-4f,  0x1.262e14p-1f, 0x1.7dd644p-2f };
        const S32 rand100[] = { 75, 63, 75, 13, 90, 9, 57, 37 };
        const U32 u32[] = { 0xc151df7d, 0xa3978fb9, 0xc08c967f, 0x22e2c43f, 0xe73ca28e, 0x1814dc62, 0x93170a19, 0x5f75917a };

        ll_rand_seed(42);
        for (S32 ii = 0; ii < 8; ++ii)
        {
            ensure_equals(stringize("drand ", ii), ll_drand(), drand[ii]);
        }
        ll_rand_seed(42);
        for (S32 ii = 0; ii < 8; ++ii)
        {
            ensure_equals(stringize("frand ", ii), ll_frand(), frand[ii]);
        }
        ll_rand_seed(42);
        for (S32 ii = 0; ii < 8; ++ii)
        {
            ensure_equals(stringize("rand(100) ", ii), ll_rand(100), rand100[ii]);
        }
        ll_rand_seed(42);
        for (S32 ii = 0; ii < 8; ++ii)
        {
            ensure_equals(stringize("rand_u32 ", ii), ll_rand_u32(), u32[ii]);
        }
    }

    template<> template<>
    void random_object_t::test<18>()
    {
        set_test_name("drand() draws to 53 bits and frand() to 24, and each uses all of its bits");
        ll_rand_seed(18);
        bool drand_last_bit = false;
        bool frand_last_bit = false;
        for (S32 ii = 0; ii < 100000; ++ii)
        {
            const F64 drawn = std::ldexp(ll_drand(), 53);
            ensure(stringize("drand ", ii, " is a multiple of 2^-53"), drawn == std::floor(drawn));
            drand_last_bit = drand_last_bit || std::fmod(drawn, 2.0) != 0.0;

            const F64 drawn_f = std::ldexp(F64(ll_frand()), 24);
            ensure(stringize("frand ", ii, " is a multiple of 2^-24"), drawn_f == std::floor(drawn_f));
            frand_last_bit = frand_last_bit || std::fmod(drawn_f, 2.0) != 0.0;
        }
        ensure("some drand sets the 53rd bit", drand_last_bit);
        ensure("some frand sets the 24th bit", frand_last_bit);
    }
}
