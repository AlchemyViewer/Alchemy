/**
 * @file dofCombineF.glsl
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2007, Linden Research, Inc.
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

uniform sampler2D diffuseRect; // the blur
uniform sampler2D lightMap;    // the scene, sharp

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;

uniform float max_cof;
// Fraction of the screen the blur was rendered at: CameraDoFResScale. Drives the CoF arithmetic.
uniform float res_scale;

in vec2 vary_fragcoord;

// The circle of confusion at a point of the scene (dofCoFUtilF.glsl).
float dofCoF(vec2 tc);

void main()
{
    vec2 tc = vary_fragcoord.xy;

    vec4 dof = texture(diffuseRect, tc);

    vec4 sharp = texture(lightMap, tc);
    vec3 diff  = sharp.rgb;
    float sc   = dofCoF(tc);

    #if FRONT_BLUR
        float a = min(abs(sc) * res_scale*res_scale, 1.0);
    #else
        float a = (sc > 0.0) ? 0.0 : min(abs(sc) * res_scale*res_scale, 1.0);
    #endif

    if (a > 0.25 && a < 0.75)
    { //help out the transition a bit
        float sc = a/res_scale;

        vec3 col;
        col = texture(lightMap, vary_fragcoord.xy+vec2(sc,sc)/screen_res).rgb;
        col += texture(lightMap, vary_fragcoord.xy+vec2(-sc,sc)/screen_res).rgb;
        col += texture(lightMap, vary_fragcoord.xy+vec2(sc,-sc)/screen_res).rgb;
        col += texture(lightMap, vary_fragcoord.xy+vec2(-sc,-sc)/screen_res).rgb;

        diff = mix(diff, col*0.25, a);
    }

    // The scene's alpha as it was: it carries the legacy prim-glow tag, which the passes after this one read.
    frag_color = vec4(mix(diff, dof.rgb, a), sharp.a);
}
