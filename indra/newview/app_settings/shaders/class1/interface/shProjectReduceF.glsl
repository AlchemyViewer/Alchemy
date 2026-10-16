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
// rows, one fragment per coefficient, into the probe's nine-texel row of the coefficient target.
//
// Blending is off while probes update, so the sum lives here rather than in additive blending,
// and 6 * u_width texel fetches per fragment is a trivial draw. Rows are added from 0 upward,
// the order the single-pass form visits faces and rows, so the two forms agree to floating-point
// rounding -- and scripts/content_tools/check_sh_projection.py shows the 16-bit partials move a
// coefficient by less than 1e-3 of the DC term.

out vec4 frag_color;

uniform sampler2D shPartial;
uniform int u_width;   // face edge length that was integrated; shPartial has 6 * u_width rows

// Unused, but the shared vertex stage emits it.
in vec3 vary_dir;

void main()
{
    int coef = int(gl_FragCoord.x);
    int rows = 6 * u_width;

    vec3 sum = vec3(0.0);
    for (int r = 0; r < rows; ++r)
    {
        sum += texelFetch(shPartial, ivec2(coef, r), 0).rgb;
    }

    frag_color = vec4(sum, 1.0);
}
