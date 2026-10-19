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

// The blur carries sqrt(colour) under DOF_SQRT_ENCODED; see postDeferredF.glsl.
vec3 dofDecode(vec3 c)
{
#ifdef DOF_SQRT_ENCODED
    return c * c;
#else
    return c;
#endif
}

// The blur at tc, upsampled. The hardware's bilinear read filters what the texels hold, which under
// DOF_SQRT_ENCODED is sqrt(colour); and without the near blur a near-field pixel's texel holds its own sharp
// colour (the gather leaves the near field to this pass), part of which a background pixel beside the foreground
// would take back in. Those cases filter the four texels by hand, decoded, leaving the near-field ones out while
// any other is there.
vec3 dofBlur(vec2 tc)
{
#if FRONT_BLUR && !defined(DOF_SQRT_ENCODED)
    return textureLod(diffuseRect, tc, 0.0).rgb;
#else
    vec2  size = vec2(textureSize(diffuseRect, 0));
    vec2  p    = tc * size - 0.5;
    vec2  f    = fract(p);
    ivec2 i0   = ivec2(floor(p));
    ivec2 hi   = ivec2(size) - 1;

    ivec2 t[4] = ivec2[4](i0, i0 + ivec2(1, 0), i0 + ivec2(0, 1), i0 + ivec2(1, 1));
    float w[4] = float[4]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);

    vec3  sum      = vec3(0.0);
    float sum_w    = 0.0;
    vec3  filtered = vec3(0.0);
    for (int k = 0; k < 4; ++k)
    {
        ivec2 c = clamp(t[k], ivec2(0), hi);
        vec3  v = dofDecode(texelFetch(diffuseRect, c, 0).rgb);
        filtered += v * w[k];
#if FRONT_BLUR
        float wk = w[k];
#else
        float wk = (dofCoF((vec2(c) + 0.5) / size) > 0.0) ? 0.0 : w[k];
#endif
        sum   += v * wk;
        sum_w += wk;
    }
    return (sum_w > 0.0) ? sum / sum_w : filtered;
#endif
}

void main()
{
    vec2 tc = vary_fragcoord.xy;

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

    // A pixel in focus reads no blur.
    vec3 color = diff;
    if (a > 0.0)
    {
        color = mix(diff, dofBlur(tc), a);
    }

    // The scene's alpha as it was: it carries the legacy prim-glow tag, which the passes after this one read.
    frag_color = vec4(color, sharp.a);
}
