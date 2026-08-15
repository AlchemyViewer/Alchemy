/**
 * @file class1/interface/shProjectF.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

// Project a probe's radiance cubemap onto second-order spherical harmonics.
//
// This replaces a Monte Carlo cosine convolution that rendered a 16x16x6 irradiance cubemap by
// re-integrating the whole environment separately for every one of its texels. The two scale on
// different quantities: that cost (output texels x samples), this costs (source texels), once.
//
// It is not an approximation of the cubemap it replaces. Ramamoorthi and Hanrahan showed that
// irradiance from ANY environment is band-limited -- convolving with a clamped cosine annihilates
// everything above the second band, so nine coefficients capture essentially all of it. The
// cubemap was an expensive encoding of a function with nine degrees of freedom.
//
// One fragment per coefficient: gl_FragCoord.x selects which, and the whole set for one probe is
// a 9x1 strip written into that probe's row. Nine fragments each walk the source, so the source
// is read nine times over; that redundancy is still far cheaper than what it replaces, and it
// keeps this to a single pass with no MRT juggling and no compute shader, which the GL floor here
// cannot assume.

out vec4 frag_color;

uniform samplerCubeArray reflectionProbes;
uniform int sourceIdx;
// Reuses the radiance pass's reserved uniform names rather than minting new slots.
uniform float mipLevel;  // source mip to integrate
uniform int   u_width;   // face edge length at that mip

// Unused, but the shared vertex stage emits it.
in vec3 vary_dir;

// Solid angle of one cubemap texel, via the exact spherical excess of its footprint on the
// sphere. A naive 1/r^3 weighting is noticeably wrong toward the face corners, where a texel
// projects to a much smaller patch than one at the centre -- and the corners are where six faces
// have to agree, so an inconsistent weight there shows up as seams in the reconstructed ambient.
float areaElement(float x, float y)
{
    return atan(x * y, sqrt(x * x + y * y + 1.0));
}

float texelSolidAngle(float u, float v, float inv_res)
{
    float x0 = u - inv_res;
    float y0 = v - inv_res;
    float x1 = u + inv_res;
    float y1 = v + inv_res;
    return areaElement(x1, y1) - areaElement(x0, y1) - areaElement(x1, y0) + areaElement(x0, y0);
}

// GL cubemap face conventions. uv spans [-1,1] across the face.
vec3 cubeDirection(int face, float u, float v)
{
    if (face == 0) return vec3( 1.0,   -v,   -u);
    if (face == 1) return vec3(-1.0,   -v,    u);
    if (face == 2) return vec3(   u,  1.0,    v);
    if (face == 3) return vec3(   u, -1.0,   -v);
    if (face == 4) return vec3(   u,   -v,  1.0);
                   return vec3(  -u,   -v, -1.0);
}

// Real second-order SH basis, evaluated for one index. Kept as a switch on a value that is
// uniform across the whole draw (it comes from the fragment's x position, and each fragment
// owns exactly one coefficient), so no lane in a quad disagrees about which branch it is in.
float shBasis(int i, vec3 d)
{
    if (i == 0) return 0.282095;
    if (i == 1) return 0.488603 * d.y;
    if (i == 2) return 0.488603 * d.z;
    if (i == 3) return 0.488603 * d.x;
    if (i == 4) return 1.092548 * d.x * d.y;
    if (i == 5) return 1.092548 * d.y * d.z;
    if (i == 6) return 0.315392 * (3.0 * d.z * d.z - 1.0);
    if (i == 7) return 1.092548 * d.x * d.z;
                return 0.546274 * (d.x * d.x - d.y * d.y);
}

void main()
{
    int coef = int(gl_FragCoord.x);

    float res     = float(u_width);
    float inv_res = 1.0 / res;

    vec3 sum = vec3(0.0);

    for (int face = 0; face < 6; ++face)
    {
        for (int y = 0; y < u_width; ++y)
        {
            // Texel centre in [-1,1].
            float v = (2.0 * (float(y) + 0.5) * inv_res) - 1.0;

            for (int x = 0; x < u_width; ++x)
            {
                float u = (2.0 * (float(x) + 0.5) * inv_res) - 1.0;

                vec3 dir = cubeDirection(face, u, v);
                float sa = texelSolidAngle(u, v, inv_res);

                vec3 radiance = textureLod(reflectionProbes, vec4(dir, float(sourceIdx)), mipLevel).rgb;

                sum += radiance * shBasis(coef, normalize(dir)) * sa;
            }
        }
    }

    // Signed: bands 1 and 2 are negative over half the sphere by construction, which is why this
    // target cannot be one of the unsigned float formats the radiance chain uses.
    frag_color = vec4(sum, 1.0);
}
