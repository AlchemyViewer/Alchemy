/**
 * @file meteorsF.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

out vec4 frag_data[4];

in vec4 vary_color; // rgb linear, a = envelope alpha
in vec2 vary_coord; // (u, v) — u in [-1,1] along streak, v in [-1,1] across

void main()
{
    // Along-streak profile: faded tail (u=-1), bright head (u=+1). The head
    // gets an extra localized flash from the cubic term to read as a distinct
    // "tip" instead of a uniform streak.
    float along_t = clamp((vary_coord.x + 1.0) * 0.5, 0.0, 1.0);
    float head_flash = pow(along_t, 6.0);
    float along = along_t + head_flash * 1.2;

    // Across-streak: Gaussian falloff. Tight — meteors are narrow.
    float across = exp(-vary_coord.y * vary_coord.y * 6.0);

    float shape = along * across;
    if (shape < 0.003) discard;

    // Scale by envelope alpha (set CPU-side from age curve).
    vec3 color = vary_color.rgb * shape * vary_color.a;

    // Opt out of haze in the gbuffer flag, same as stars.
    frag_data[1] = vec4(0.0);
    frag_data[2] = vec4(0.0, 1.0, 0.0, GBUFFER_FLAG_SKIP_ATMOS);

#if defined(HAS_EMISSIVE)
    // Meteors are bright, fast events — give the bloom pipeline plenty to work
    // with. The shape-weighted color is also written to the emissive target so
    // the streak trails leave a glow.
    frag_data[0] = vec4(0.0);
    frag_data[3] = vec4(color * 4.0, shape);
#else
    frag_data[0] = vec4(color * 2.0, shape);
#endif
}
