/**
 * @file altestmatchers.h
 * @brief Catch2 matchers for what LL's TUT helpers compared: floats to a number of
 *        bits, LLSD deeply, and memory byte for byte.
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

#ifndef AL_ALTESTMATCHERS_H
#define AL_ALTESTMATCHERS_H

#include "llmath.h"
#include "llsd.h"

#include <catch2/catch_tostring.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>

namespace ll_test
{
    // ensure_approximately_equals: equal to frac_bits bits of the fraction, as
    // is_approx_equal_fraction() decides, not to an epsilon.
    template <typename F>
    class WithinBitsMatcher : public Catch::Matchers::MatcherBase<F>
    {
    public:
        WithinBitsMatcher(F expected, U32 frac_bits)
            : mExpected(expected), mFracBits(frac_bits)
        {
        }

        bool match(const F& actual) const override
        {
            return is_approx_equal_fraction(actual, mExpected, mFracBits);
        }

        std::string describe() const override
        {
            std::ostringstream out;
            out << "is " << mExpected << " to " << mFracBits << " bits of the fraction";
            return out.str();
        }

    private:
        F mExpected;
        U32 mFracBits;
    };

    inline WithinBitsMatcher<F32> WithinBits(F32 expected, U32 frac_bits)
    {
        return WithinBitsMatcher<F32>(expected, frac_bits);
    }

    inline WithinBitsMatcher<F64> WithinBits(F64 expected, U32 frac_bits)
    {
        return WithinBitsMatcher<F64>(expected, frac_bits);
    }

    // ensure_distance: strictly less than distance from expected, worked in
    // T as TUT worked it, so a value on the border fails, where Catch2's
    // WithinAbs, worked in double, would pass it.
    template <typename T>
    class WithinDistanceMatcher : public Catch::Matchers::MatcherBase<T>
    {
    public:
        WithinDistanceMatcher(T expected, T distance)
            : mExpected(expected), mDistance(distance)
        {
        }

        bool match(const T& actual) const override
        {
            return mExpected - mDistance < actual && actual < mExpected + mDistance;
        }

        std::string describe() const override
        {
            std::ostringstream out;
            out << "is strictly between " << Catch::Detail::stringify(T(mExpected - mDistance)) << " and "
                << Catch::Detail::stringify(T(mExpected + mDistance));
            return out.str();
        }

    private:
        T mExpected;
        T mDistance;
    };

    template <typename T>
    WithinDistanceMatcher<T> WithinDistance(T expected, T distance)
    {
        return WithinDistanceMatcher<T>(expected, distance);
    }

    // ensure_equals on LLSD: the same type, and the same value all the way
    // down, maps in key order and arrays in element order. A mismatch is
    // described by where it is, as in ["key"][3] type, so a deep difference
    // reads as more than "the two differ".
    class LLSDEqualsMatcher : public Catch::Matchers::MatcherBase<LLSD>
    {
    public:
        explicit LLSDEqualsMatcher(LLSD expected);

        bool match(const LLSD& actual) const override;
        std::string describe() const override;

    private:
        LLSD mExpected;
        // What match() last found, for describe(), which Catch2 asks after.
        mutable std::string mDifference;
    };

    inline LLSDEqualsMatcher LLSDEquals(LLSD expected)
    {
        return LLSDEqualsMatcher(std::move(expected));
    }

    // ensure_memory_matches: so many bytes at an address, compared as bytes.
    // The bytes are not copied; they must outlive the assertion.
    struct MemoryView
    {
        const void* data;
        size_t size;
    };

    inline MemoryView Memory(const void* data, size_t size)
    {
        return MemoryView{ data, size };
    }

    std::ostream& operator<<(std::ostream& out, const MemoryView& memory);

    class MemoryMatchesMatcher : public Catch::Matchers::MatcherBase<MemoryView>
    {
    public:
        explicit MemoryMatchesMatcher(MemoryView expected)
            : mExpected(expected)
        {
        }

        bool match(const MemoryView& actual) const override;
        std::string describe() const override;

    private:
        MemoryView mExpected;
        // Where match() last found the two to part, for describe().
        mutable std::string mDifference;
    };

    inline MemoryMatchesMatcher MemoryMatches(const void* expected, size_t size)
    {
        return MemoryMatchesMatcher(MemoryView{ expected, size });
    }
}

#endif // AL_ALTESTMATCHERS_H
