/**
 * @file shPackUtilF.glsl
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

/*[EXTRA_CODE_HERE]*/

// A probe's nine RGB coefficients in its row of the irradiance target, which the SH projection's
// last draw writes (shProjectF.glsl in one pass, shProjectReduceF.glsl in two) and
// evalSHIrradiance (class3/deferred/reflectionProbeF.glsl) reads: the 27 values in coefficient
// order, red, green and blue within each, as halves two to a uint (the earlier half in the low
// 16 bits), eight to an RGBA32UI texel. Four texels hold them, the last five halves zero, so a
// reader fetches a probe's set four times rather than once per coefficient.
//
// scripts/content_tools/check_sh_projection.py mirrors this layout, the packing below and
// evalSHIrradiance's unpack.

// packHalf2x16 is GLSL 4.20's. A 4.1 context (every macOS one) compiles at 400 and packs by
// hand: IEEE binary16, rounding to nearest even, out of range to infinity, a NaN kept a NaN.
#if __VERSION__ >= 420
uint shPackHalves(vec2 v)
{
    return packHalf2x16(v);
}
#else
uint shHalfBits(float f)
{
    uint x    = floatBitsToUint(f);
    uint sign = (x >> 16) & 0x8000u;
    uint e    = (x >> 23) & 0xFFu;
    uint m    = x & 0x7FFFFFu;

    if (e == 0xFFu)
    {
        return sign | 0x7C00u | ((m != 0u) ? 0x200u : 0u);
    }

    int he = int(e) - 112; // the half's biased exponent
    if (he >= 31)
    {
        return sign | 0x7C00u;
    }

    if (he <= 0)
    {
        // A denormal half, in units of 2^-24; below half of one it is zero.
        if (he < -10)
        {
            return sign;
        }
        m |= 0x800000u;
        uint shift = uint(14 - he);
        uint q     = m >> shift;
        uint rest  = m & ((1u << shift) - 1u);
        uint tie   = 1u << (shift - 1u);
        q += (rest > tie || (rest == tie && (q & 1u) != 0u)) ? 1u : 0u;
        return sign | q;
    }

    // A carry out of the mantissa rounds into the exponent, as far as infinity.
    uint h    = (uint(he) << 10) | (m >> 13);
    uint rest = m & 0x1FFFu;
    h += (rest > 0x1000u || (rest == 0x1000u && (h & 1u) != 0u)) ? 1u : 0u;
    return sign | h;
}

uint shPackHalves(vec2 v)
{
    return shHalfBits(v.x) | (shHalfBits(v.y) << 16);
}
#endif

// The first of the coefficients texel t of the row holds a part of, and the last: at most four.
int shPackedFirst(int t)
{
    return (8 * t) / 3;
}

int shPackedLast(int t)
{
    return min((8 * t + 7) / 3, 8);
}

// Texel t of the row, from the coefficients it holds a part of: sums[k] is coefficient
// shPackedFirst(t) + k.
uvec4 shPackTexel(int t, vec3 sums[4])
{
    int first = shPackedFirst(t);

    float h[8];
    for (int j = 0; j < 8; ++j)
    {
        int k    = 8 * t + j;  // the half's place in the row
        int coef = k / 3;
        h[j] = (coef < 9) ? sums[coef - first][k - 3 * coef] : 0.0;
    }

    return uvec4(shPackHalves(vec2(h[0], h[1])), shPackHalves(vec2(h[2], h[3])),
                 shPackHalves(vec2(h[4], h[5])), shPackHalves(vec2(h[6], h[7])));
}
