/**
 * @file class1/deferred/skyCompositeMainF.glsl
 * @brief The sky's pass: the whole sky (deferred/skyCompositeF.glsl) at every pixel the world left at the far plane,
 *        into the lit screen.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

out vec4 frag_color;

in vec2 vary_fragcoord;

// The classic sky's gain on the lit screen, which the soften pass's classic variant gives what it lights as well
// (softenLightF.glsl's writeColor): 1.1 under a classic sky, 1 otherwise (LLSettingsVOSky::applySpecial).
uniform float sky_classic_scale;

// deferred/skyCompositeF.glsl
vec3 skyComposite(vec2 tc);
// deferred/deferredUtil.glsl
vec3 clampRadiance(vec3 c, float ceiling);

void main()
{
    // Nothing here lights or hazes the sky. The lit screen's alpha, its glow, is 0 at the sky, where local lights add
    // over it as over what the soften pass wrote. The colour is held to 65504, the largest a half float holds: the lit
    // screen and the probes' faces are RGBA16F, and a store past it is infinity, which the post chain turns into NaN.
    frag_color.rgb = clampRadiance(skyComposite(vary_fragcoord) * sky_classic_scale, MAX_SCENE_RADIANCE);
    frag_color.a   = 0.0;
}
