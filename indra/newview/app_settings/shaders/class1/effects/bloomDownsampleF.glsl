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
// When FIRST_DOWNSAMPLE is defined, each 2x2 group is weighted by a partial Karis
// average (1 / (1 + lum)) to suppress fireflies from point-source highlights.
out vec4 frag_color;

uniform sampler2D diffuseMap;
uniform vec2 bloom_texel_size; // 1 / source (larger) mip resolution

in vec2 vary_texcoord0;

vec4 partialKaris(vec4 c)
{
#ifdef FIRST_DOWNSAMPLE
    float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
    float w = 1.0 / (1.0 + lum);
    return c * w;
#else
    return c;
#endif
}

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
    vec4 grp0 = (d + e + i + j) * 0.25;
    vec4 grp1 = (a + b + f + g) * 0.25;
    vec4 grp2 = (b + c + g + h) * 0.25;
    vec4 grp3 = (f + g + k + l) * 0.25;
    vec4 grp4 = (g + h + l + m) * 0.25;

    grp0 = partialKaris(grp0);
    grp1 = partialKaris(grp1);
    grp2 = partialKaris(grp2);
    grp3 = partialKaris(grp3);
    grp4 = partialKaris(grp4);

    frag_color = grp0 * 0.5 + (grp1 + grp2 + grp3 + grp4) * 0.125;
}
