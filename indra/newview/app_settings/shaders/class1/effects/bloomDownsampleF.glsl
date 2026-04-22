/**
 * @file bloomDownsampleF.glsl
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

// 13-tap downsample (Sledgehammer / Jimenez 2014 COD:AW presentation).
// When FIRST_DOWNSAMPLE is defined, each 2x2 box is folded via an outlier-
// detecting variant of Karis: a tap is downweighted only by how much brighter
// it is than the *other three* taps in its group. Uniform bright regions
// (e.g. a solid glowing surface) keep all weights at 1 and reduce to a plain
// box average so shape is preserved; isolated single-pixel fireflies still
// get crushed because they stick out above all their neighbors.
// The simpler 1/(1+max(rgb)) weight used previously suppressed coherent
// bright regions at their edges (cube edges lost half their energy per
// level), which made small bright surfaces collapse into round halos at
// distance.
out vec4 frag_color;

uniform sampler2D diffuseMap;
uniform vec2 bloom_texel_size; // 1 / source (larger) mip resolution

in vec2 vary_texcoord0;

#ifdef FIRST_DOWNSAMPLE
float maxLum(vec4 c)
{
    // max-channel brightness catches saturated highlights (red-only or
    // blue-only fireflies) that Rec.709 luma would underweight.
    return max(max(c.r, c.g), c.b);
}

vec4 karisBox(vec4 a, vec4 b, vec4 c, vec4 d)
{
    float la = maxLum(a);
    float lb = maxLum(b);
    float lc = maxLum(c);
    float ld = maxLum(d);

    float wa = 1.0 / (1.0 + max(la - max(max(lb, lc), ld), 0.0));
    float wb = 1.0 / (1.0 + max(lb - max(max(la, lc), ld), 0.0));
    float wc = 1.0 / (1.0 + max(lc - max(max(la, lb), ld), 0.0));
    float wd = 1.0 / (1.0 + max(ld - max(max(la, lb), lc), 0.0));

    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}
#endif

void main()
{
    vec2 uv = vary_texcoord0;
    vec2 t  = bloom_texel_size;

    vec4 a = texture(diffuseMap, uv + t * vec2(-2.0, -2.0));
    vec4 b = texture(diffuseMap, uv + t * vec2( 0.0, -2.0));
    vec4 c = texture(diffuseMap, uv + t * vec2( 2.0, -2.0));
    vec4 d = texture(diffuseMap, uv + t * vec2(-1.0, -1.0));
    vec4 e = texture(diffuseMap, uv + t * vec2( 1.0, -1.0));
    vec4 f = texture(diffuseMap, uv + t * vec2(-2.0,  0.0));
    vec4 g = texture(diffuseMap, uv);
    vec4 h = texture(diffuseMap, uv + t * vec2( 2.0,  0.0));
    vec4 i = texture(diffuseMap, uv + t * vec2(-1.0,  1.0));
    vec4 j = texture(diffuseMap, uv + t * vec2( 1.0,  1.0));
    vec4 k = texture(diffuseMap, uv + t * vec2(-2.0,  2.0));
    vec4 l = texture(diffuseMap, uv + t * vec2( 0.0,  2.0));
    vec4 m = texture(diffuseMap, uv + t * vec2( 2.0,  2.0));

    // Five overlapping 2x2 boxes: inner (center 4 taps) + four outer corners.
#ifdef FIRST_DOWNSAMPLE
    vec4 grp0 = karisBox(d, e, i, j);
    vec4 grp1 = karisBox(a, b, f, g);
    vec4 grp2 = karisBox(b, c, g, h);
    vec4 grp3 = karisBox(f, g, k, l);
    vec4 grp4 = karisBox(g, h, l, m);
#else
    vec4 grp0 = (d + e + i + j) * 0.25;
    vec4 grp1 = (a + b + f + g) * 0.25;
    vec4 grp2 = (b + c + g + h) * 0.25;
    vec4 grp3 = (f + g + k + l) * 0.25;
    vec4 grp4 = (g + h + l + m) * 0.25;
#endif

    frag_color = grp0 * 0.5 + (grp1 + grp2 + grp3 + grp4) * 0.125;
}
