/**
 * @file class1/deferred/skyCubeF.glsl
 * @brief WindLight's dome baked into the environment cube the shiny and probe-less reflections read where reflection
 *        probes are off: the six faces side by side across one strip (ALSkyCube::bake).
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

out vec4 frag_color;

// Across the strip, 0 to 1 each way.
in vec2 vary_fragcoord;

// Each face's forward, right and up in the dome's frame (north, up, east), in GL's face order: the texel at s and t,
// each -1 to 1 across its face, looks along forward + s right + t up (ALSkyCube::face).
uniform vec3 skyCubeFace[18];

vec3 windlightDome(vec3 dir);

void main()
{
    float across = vary_fragcoord.x * 6.0;
    int   face   = min(int(across), 5);
    vec2  st     = vec2(fract(across), vary_fragcoord.y) * 2.0 - 1.0;

    vec3 dir = normalize(skyCubeFace[face * 3] + st.x * skyCubeFace[face * 3 + 1] + st.y * skyCubeFace[face * 3 + 2]);

    // Half the dome, as the haze takes it for far water: WindLight's colour before the sky program doubles it, which
    // the readers double again where they take it as the legacy environment (applyLegacyEnv).
    frag_color = vec4(windlightDome(dir) * 0.5, 1.0);
}
