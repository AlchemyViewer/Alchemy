/**
 * @file alwaterheightbake.cpp
 * @brief The heights of a water normal map, integrated from its slopes.
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

#include "alwaterheightbake.h"

#include "llmath.h"

#include <pocketfft_hdronly.h>

#include <bit>
#include <cmath>
#include <complex>
#include <exception>

namespace ALWaterHeightBake
{

namespace
{
using Complex = std::complex<F32>;

// The transforms' layout for a width x height field. pocketfft's axis 0 is v, from row to row, and axis 1 is u, along a
// row. The last axis transformed is the one halved, so the half spectrum is `height` rows of width / 2 + 1 values, u's
// frequencies 0 to width / 2. Strides are in bytes, not elements.
struct Layout
{
    pocketfft::shape_t mShape;
    pocketfft::stride_t mRealStride;
    pocketfft::stride_t mHalfStride;
    pocketfft::shape_t mAxes;
    size_t mHalfWidth;
    F32 mInverseScale;

    Layout(S32 width, S32 height)
        : mShape{ (size_t)height, (size_t)width },
          mRealStride{ (std::ptrdiff_t)((size_t)width * sizeof(F32)), (std::ptrdiff_t)sizeof(F32) },
          mHalfStride{ (std::ptrdiff_t)(((size_t)width / 2 + 1) * sizeof(Complex)), (std::ptrdiff_t)sizeof(Complex) },
          mAxes{ 0, 1 },
          mHalfWidth((size_t)width / 2 + 1),
          mInverseScale(1.f / ((F32)width * (F32)height))
    {
    }

    size_t halfCount() const { return mHalfWidth * mShape[0]; }
};

void forward(const Layout& layout, const F32* field, Complex* spectrum)
{
    pocketfft::r2c(layout.mShape, layout.mRealStride, layout.mHalfStride, layout.mAxes, pocketfft::FORWARD, field,
                   spectrum, 1.f);
}

// The output's real shape, not the half one: c2r learns from it whether the halved axis was even or odd.
void inverse(const Layout& layout, const Complex* spectrum, F32* field)
{
    pocketfft::c2r(layout.mShape, layout.mHalfStride, layout.mRealStride, layout.mAxes, pocketfft::BACKWARD, spectrum,
                   field, layout.mInverseScale);
}

bool bakeable(S32 width, S32 height)
{
    return width >= MIN_SIZE && height >= MIN_SIZE;
}

void skip(S32 width, S32 height, const char* why)
{
    LL_WARNS("Water") << "No heights baked for a " << width << "x" << height << " map: " << why << LL_ENDL;
}

// The two height fields of slope fields of the right size. pocketfft throws on a bad shape and the buffers on running
// out of memory; the callers catch both.
void integrateSlopes(const F32* slope_x, const F32* slope_y, S32 width, S32 height, Heights& out)
{
    const Layout layout(width, height);

    // The slopes' spectra along u and along v, each in turn replaced by a height field's.
    std::vector<Complex> big(layout.halfCount());
    std::vector<Complex> mid(layout.halfCount());
    forward(layout, slope_x, big.data());
    forward(layout, slope_y, mid.data());

    const S32 half = (S32)layout.mHalfWidth;
    for (S32 j = 0; j < height; ++j)
    {
        // Cycles per repeat along v, wrapped onto the full axis: past the middle, a row is a negative frequency.
        const F32 ky = (F32)(j > height / 2 ? j - height : j);
        // At an even axis's Nyquist frequency a sample cannot tell +k from -k, so a derivative there has no sign.
        const bool nyquist_v = height % 2 == 0 && j == height / 2;
        Complex* big_row = big.data() + (size_t)j * layout.mHalfWidth;
        Complex* mid_row = mid.data() + (size_t)j * layout.mHalfWidth;
        for (S32 i = 0; i < half; ++i)
        {
            const bool nyquist_u = width % 2 == 0 && i == width / 2;
            if ((i == 0 && j == 0) || nyquist_u || nyquist_v)
            {
                big_row[i] = Complex(0.f, 0.f);
                mid_row[i] = Complex(0.f, 0.f);
                continue;
            }

            const F32 kx = (F32)i;
            const Complex gx = big_row[i];
            const Complex gy = mid_row[i];
            // H(k) = -i (k . G(k)) / (2 pi |k|^2), and -i (a + ib) = b - ia. The mid field's v frequency counts
            // MID_ASPECT times in the world, where its error is measured.
            const F32 big_scale = 1.f / (F_TWO_PI * (kx * kx + ky * ky));
            const F32 mid_ky = MID_ASPECT * ky;
            const F32 mid_scale = 1.f / (F_TWO_PI * (kx * kx + mid_ky * mid_ky));
            const Complex big_dot = kx * gx + ky * gy;
            const Complex mid_dot = kx * gx + mid_ky * gy;
            big_row[i] = Complex(big_dot.imag() * big_scale, -big_dot.real() * big_scale);
            mid_row[i] = Complex(mid_dot.imag() * mid_scale, -mid_dot.real() * mid_scale);
        }
    }

    const size_t count = (size_t)width * (size_t)height;
    out.mBig.resize(count);
    out.mMid.resize(count);
    inverse(layout, big.data(), out.mBig.data());
    inverse(layout, mid.data(), out.mMid.data());

    F32 max_big = 0.f;
    F32 max_mid = 0.f;
    for (size_t n = 0; n < count; ++n)
    {
        max_big = llmax(max_big, std::fabs(out.mBig[n]));
        max_mid = llmax(max_mid, std::fabs(out.mMid[n]));
    }
    out.mWidth = width;
    out.mHeight = height;
    out.mMaxBig = BIG_METRES * max_big;
    out.mMaxMid = MID_METRES * max_mid;
}
} // namespace

Heights bake(const U8* pixels, size_t size, S32 width, S32 height, S32 components)
{
    Heights out;
    if (!bakeable(width, height))
    {
        skip(width, height, "too small");
        return out;
    }
    if (components < MIN_COMPONENTS)
    {
        skip(width, height, "fewer than three components");
        return out;
    }
    const size_t count = (size_t)width * (size_t)height;
    if (!pixels || size / (size_t)components < count)
    {
        skip(width, height, "fewer pixels than texels");
        return out;
    }

    try
    {
        // n = rgb * 2 - 1 as the water shaders decode it, and the slopes the normal leans by.
        constexpr F32 TO_NORMAL = 2.f / 255.f;
        std::vector<F32> slope_x(count);
        std::vector<F32> slope_y(count);
        const U8* texel = pixels;
        for (size_t n = 0; n < count; ++n, texel += components)
        {
            const F32 nx = (F32)texel[0] * TO_NORMAL - 1.f;
            const F32 ny = (F32)texel[1] * TO_NORMAL - 1.f;
            const F32 nz = llmax((F32)texel[2] * TO_NORMAL - 1.f, MIN_NORMAL_Z);
            slope_x[n] = -nx / nz;
            slope_y[n] = -ny / nz;
        }
        integrateSlopes(slope_x.data(), slope_y.data(), width, height, out);
    }
    catch (const std::exception& e)
    {
        skip(width, height, e.what());
        out = Heights();
    }
    return out;
}

bool integrate(const std::vector<F32>& slope_x, const std::vector<F32>& slope_y, S32 width, S32 height, Heights& out)
{
    out = Heights();
    if (!bakeable(width, height))
    {
        skip(width, height, "too small");
        return false;
    }
    const size_t count = (size_t)width * (size_t)height;
    if (slope_x.size() != count || slope_y.size() != count)
    {
        skip(width, height, "slope fields of another size");
        return false;
    }

    try
    {
        integrateSlopes(slope_x.data(), slope_y.data(), width, height, out);
    }
    catch (const std::exception& e)
    {
        skip(width, height, e.what());
        out = Heights();
        return false;
    }
    return true;
}

bool roundTrip(std::vector<F32>& field, S32 width, S32 height)
{
    if (!bakeable(width, height) || field.size() != (size_t)width * (size_t)height)
    {
        skip(width, height, "a field of another size");
        return false;
    }

    try
    {
        const Layout layout(width, height);
        std::vector<Complex> spectrum(layout.halfCount());
        std::vector<F32> result(field.size());
        forward(layout, field.data(), spectrum.data());
        inverse(layout, spectrum.data(), result.data());
        field.swap(result);
    }
    catch (const std::exception& e)
    {
        skip(width, height, e.what());
        return false;
    }
    return true;
}

bool fitToSize(const U8* pixels, S32 width, S32 height, S32 components, S32 max_size, std::vector<U8>& out,
               S32& out_width, S32& out_height)
{
    out.clear();
    out_width = 0;
    out_height = 0;
    if (!pixels || width < 1 || height < 1 || components < 1 || max_size < 1)
    {
        return false;
    }
    out.assign(pixels, pixels + (size_t)width * (size_t)height * (size_t)components);
    S32 w = width;
    S32 h = height;
    std::vector<U8> next;
    while (w > max_size || h > max_size)
    {
        // Each side halved where it is past the limit; an odd side's last box takes its last three rows or columns.
        const S32 nw = w > max_size ? w / 2 : w;
        const S32 nh = h > max_size ? h / 2 : h;
        next.assign((size_t)nw * (size_t)nh * (size_t)components, 0);
        for (S32 j = 0; j < nh; ++j)
        {
            const S32 j0 = nh == h ? j : 2 * j;
            const S32 j1 = nh == h ? j + 1 : (j == nh - 1 ? h : 2 * j + 2);
            for (S32 i = 0; i < nw; ++i)
            {
                const S32 i0 = nw == w ? i : 2 * i;
                const S32 i1 = nw == w ? i + 1 : (i == nw - 1 ? w : 2 * i + 2);
                for (S32 c = 0; c < components; ++c)
                {
                    U32 sum = 0;
                    for (S32 y = j0; y < j1; ++y)
                    {
                        for (S32 x = i0; x < i1; ++x)
                        {
                            sum += out[((size_t)y * (size_t)w + (size_t)x) * (size_t)components + (size_t)c];
                        }
                    }
                    const U32 count = (U32)((j1 - j0) * (i1 - i0));
                    next[((size_t)j * (size_t)nw + (size_t)i) * (size_t)components + (size_t)c] = (U8)((sum + count / 2) / count);
                }
            }
        }
        out.swap(next);
        w = nw;
        h = nh;
    }
    out_width = w;
    out_height = h;
    return true;
}

U16 halfOf(F32 value)
{
    // Rounded to the nearest half, ties to even, from the float's bits alone.
    const U32 bits = std::bit_cast<U32>(value);
    const U32 sign = (bits >> 16) & 0x8000u;
    const U32 magnitude = bits & 0x7fffffffu;
    if (magnitude >= 0x7f800000u)
    {
        // Infinity, or a NaN kept a NaN.
        return (U16)(sign | 0x7c00u | (magnitude > 0x7f800000u ? 0x0200u : 0u));
    }
    if (magnitude >= 0x477ff000u)
    {
        // Past the largest half, 65504, as it rounds: infinity.
        return (U16)(sign | 0x7c00u);
    }
    if (magnitude < 0x38800000u)
    {
        // Below the smallest normal half: a subnormal, or 0. The mantissa with its leading one, shifted into the
        // half's units, rounded to the nearest, ties to even.
        if (magnitude < 0x33000000u)
        {
            return (U16)sign;
        }
        const U32 exponent = magnitude >> 23;
        const U32 mantissa = (magnitude & 0x7fffffu) | 0x800000u;
        const U32 shift = 126u - exponent;
        U32 half = mantissa >> shift;
        const U32 rest = mantissa & ((1u << shift) - 1u);
        const U32 tie = 1u << (shift - 1u);
        if (rest > tie || (rest == tie && (half & 1u)))
        {
            ++half;
        }
        return (U16)(sign | half);
    }
    // A normal half: the exponent rebiased and the mantissa cut to ten bits, rounded to the nearest, ties to even; a
    // carry out of the mantissa steps the exponent, as it should.
    U32 half = ((magnitude - 0x38000000u) >> 13);
    const U32 rest = magnitude & 0x1fffu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))
    {
        ++half;
    }
    return (U16)(sign | half);
}

std::vector<U16> halfTexels(const Heights& heights)
{
    std::vector<U16> texels;
    if (!heights.valid())
    {
        return texels;
    }
    const size_t count = (size_t)heights.mWidth * (size_t)heights.mHeight;
    texels.resize(count * 2);
    for (size_t n = 0; n < count; ++n)
    {
        texels[n * 2] = halfOf(heights.mBig[n]);
        texels[n * 2 + 1] = halfOf(heights.mMid[n]);
    }
    return texels;
}

} // namespace ALWaterHeightBake
