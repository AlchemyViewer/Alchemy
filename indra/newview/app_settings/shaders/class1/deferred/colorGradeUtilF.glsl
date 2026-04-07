/**
 * @file colorGradeUtilF.glsl
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2021-2026, Rye Mutt <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

uniform sampler3D color_grade_lut;
uniform vec4 color_grade_lut_size;

uniform float color_grade_stength;

vec3 applyLUTGrading(vec3 diff)
{
    vec3 original_diff = diff;

    // Invert coord for compat with DX-style LUT
    diff.g = color_grade_lut_size.y > 0.5 ? 1.0 - diff.g : diff.g;

    // swap bluegreen if needed
    diff.rgb = color_grade_lut_size.z > 0.5 ? diff.rbg: diff.rgb;

    //see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter24.html
    vec3 scale = (vec3(color_grade_lut_size.x) - 1.0) / vec3(color_grade_lut_size.x);
    vec3 offset = 1.0 / (2.0 * vec3(color_grade_lut_size.x));
    diff = textureLod(color_grade_lut, scale * diff.rgb + offset, 0).rgb;

    return mix(original_diff, diff, color_grade_stength);
}
