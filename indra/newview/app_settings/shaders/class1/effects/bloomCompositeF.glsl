/**
 * @file bloomCompositeF.glsl
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

// Pre-tonemap composite. Output is additively blended into mRT->screen
// (GL_ONE, GL_ONE at the call site). With BLOOM_HALATION defined, the halation
// signal rides in the alpha channel of the bloom pyramid, tinted and gated by
// halation_strength. Otherwise the pyramid is RGB-only and halation is skipped.
out vec4 frag_color;

uniform sampler2D bloomMap;
uniform float bloom_strength;
#ifdef BLOOM_HALATION
uniform float halation_strength;
uniform vec3  halation_tint;
#endif

in vec2 vary_texcoord0;

void main()
{
#ifdef BLOOM_HALATION
    vec4 s = texture(bloomMap, vary_texcoord0);
    vec3 result = s.rgb * bloom_strength
                + s.a   * halation_strength * halation_tint;
#else
    vec3 result = texture(bloomMap, vary_texcoord0).rgb * bloom_strength;
#endif
    frag_color = vec4(result, 0.0);
}
