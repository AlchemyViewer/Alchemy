/**
 * @file class3/deferred/waterHazeF.glsl
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
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

out vec4 frag_color;

// Inputs
in vec4 vary_fragcoord;

vec4 getPositionWithDepth(vec2 pos_screen, float depth);
float getDepth(vec2 pos_screen);
bool isFarDepth(float d);             // deferredUtil.glsl -- depth-convention aware

vec4 getWaterFogView(vec3 pos);

// Void water has no floor. The sky behind it keeps the cleared far depth, which an infinite projection
// reconstructs a thousand km out and a finite one at its far plane, wherever that falls relative to the surface;
// it is fogged instead as water this deep past where the ray enters, at any distance.
const float VOID_WATER_FOG_DEPTH = 1024.0;
uniform vec4 waterPlane;

void main()
{
    vec2  tc           = vary_fragcoord.xy/vary_fragcoord.w*0.5+0.5;
    // From above, the hardware depth test (LLPipeline::doWaterHaze) keeps the field off what lies in front of it.
    float depth        = getDepth(tc.xy);

    vec4  pos          = getPositionWithDepth(tc, depth);

    if (isFarDepth(depth))
    {
        vec3 dir = normalize(pos.xyz);
        float es = -dot(dir, waterPlane.xyz);
        float entry = (waterPlane.w > 0.0 && es > 0.0) ? waterPlane.w / es : 0.0;
        pos.xyz = dir * (entry + VOID_WATER_FOG_DEPTH);
    }

    vec4 fogged = getWaterFogView(pos.xyz);
    fogged.a = max(pow(fogged.a, 1.7), 0);

    frag_color = max(fogged, vec4(0)); //output linear since local lights will be added to this shader's results

}
