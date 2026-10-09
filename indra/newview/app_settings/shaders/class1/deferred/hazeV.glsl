/**
 * @file class1/deferred/hazeV.glsl
 * @brief The haze pass's full-screen triangle, on the far plane.
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
 *
 * $/LicenseInfo$
 */

in vec3 position;

out vec2 vary_fragcoord;

void setAtmosAttenuation(vec3 c);
void setAdditiveColor(vec3 c);

void main()
{
    // Level with the depth the sky leaves cleared, so a depth test that passes only in front of the far plane
    // (LLPipeline::doAtmospherics) rejects the sky before it is shaded. Reverse-Z puts the far plane at ndc z 0.
#ifdef REVERSE_Z
    gl_Position = vec4(position.xy, 0.0, 1.0);
#else
    gl_Position = vec4(position.xy, 1.0, 1.0);
#endif

    // every varying the linked stages declare is written
    setAtmosAttenuation(vec3(1));
    setAdditiveColor(vec3(0));

    vary_fragcoord = position.xy * 0.5 + 0.5;
}
