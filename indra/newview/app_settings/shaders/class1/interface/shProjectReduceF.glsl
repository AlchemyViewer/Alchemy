/**
 * @file class1/interface/shProjectReduceF.glsl
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

// Second half of the row-parallel SH projection. shProjectF.glsl with SH_ROW_PARTIAL wrote one
// partial coefficient per face row into shPartial (9 columns, 6 * u_width rows); this adds the
// rows into the probe's packed row of the coefficient target (shPackUtilF.glsl), one fragment
// per texel, each adding the up to four coefficients that texel holds a part of.
//
// Blending is off while probes update, so the sum lives here rather than in additive blending,
// and at most 4 * 6 * u_width texel fetches per fragment is a trivial draw. Rows are added from 0
// upward, the order the single-pass form visits faces and rows, so the two forms agree to
// floating-point rounding -- and scripts/content_tools/check_sh_projection.py shows the 16-bit
// partials move a coefficient by less than 1e-3 of the DC term.

out uvec4 frag_color;

uniform sampler2D shPartial;
uniform int u_width;   // face edge length that was integrated; shPartial has 6 * u_width rows

// Unused, but the shared vertex stage emits it.
in vec3 vary_dir;

int shPackedFirst(int t);
int shPackedLast(int t);
uvec4 shPackTexel(int t, vec3 sums[4]);

void main()
{
    int texel = int(gl_FragCoord.x);
    int first = shPackedFirst(texel);
    int count = shPackedLast(texel) - first + 1;
    int rows  = 6 * u_width;

    vec3 sums[4] = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    for (int k = 0; k < count; ++k)
    {
        for (int r = 0; r < rows; ++r)
        {
            sums[k] += texelFetch(shPartial, ivec2(first + k, r), 0).rgb;
        }
    }

    frag_color = shPackTexel(texel, sums);
}
