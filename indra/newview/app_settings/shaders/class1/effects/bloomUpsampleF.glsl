/**
 * @file bloomUpsampleF.glsl
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

// 3x3 tent-filtered upsample. Output is additively blended against the larger
// mip bound to the framebuffer (set via GL_ONE, GL_ONE at the call site).
out vec4 frag_color;

uniform sampler2D diffuseMap;
uniform vec2 bloom_texel_size; // 1 / source (smaller) mip resolution
uniform float bloom_scatter;

in vec2 vary_texcoord0;

void main()
{
    vec2 uv = vary_texcoord0;
    vec2 t  = bloom_texel_size * bloom_scatter;

    vec4 r;
    r  = texture(diffuseMap, uv + t * vec2(-1.0, -1.0)) * (1.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2( 0.0, -1.0)) * (2.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2( 1.0, -1.0)) * (1.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2(-1.0,  0.0)) * (2.0 / 16.0);
    r += texture(diffuseMap, uv)                         * (4.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2( 1.0,  0.0)) * (2.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2(-1.0,  1.0)) * (1.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2( 0.0,  1.0)) * (2.0 / 16.0);
    r += texture(diffuseMap, uv + t * vec2( 1.0,  1.0)) * (1.0 / 16.0);

    frag_color = r;
}
