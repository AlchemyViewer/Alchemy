/**
 * @file class1/deferred/skyBodiesF.glsl
 * @brief The sun disc and the moon as functions of the view ray: what each laid over the sky where a ray from the
 *        camera crosses its quad, for the sky's composite.
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2005, 2020 Linden Research, Inc.
 *
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

// Each quad is its centre, then its right and up half-extents each over its squared length, in the dome's frame
// (north, up, east) about the camera (ALSkyLayers::rayQuad).

// The sun disc's texture pair, the sky's blend between them (0 whenever they match or one is missing), and its quad.
uniform sampler2D skySunMap;
uniform sampler2D skySunMapNext;
uniform float     skySunBlend;
uniform vec3      skySunQuad[3];

// The moon's texture, its quad, its brightness and its fade toward the horizon.
uniform sampler2D skyMoonMap;
uniform vec3      skyMoonQuad[3];
uniform float     skyMoonFade;
uniform float     moon_brightness;

// Where the ray along dir, a unit vector in the dome's frame, meets the plane of quad: its texture coordinates there,
// (0, 0) to (1, 1) across the quad and continuing past it, and in hit whether the ray crosses the quad itself. The
// coordinates are worked out unclipped so that their derivatives stay smooth at the quad's edge, as a raster quad's
// helper pixels extrapolate them.
vec2 skyQuadCoord(vec3 dir, vec3 quad[3], out bool hit)
{
    vec3  normal = cross(quad[1], quad[2]);
    float t      = dot(quad[0], normal) / dot(dir, normal);
    vec2  offset = vec2(dot(t * dir - quad[0], quad[1]), dot(t * dir - quad[0], quad[2]));
    hit = t > 0.0 && max(abs(offset.x), abs(offset.y)) <= 1.0;
    return offset * 0.5 + 0.5;
}

// The sun disc along dir: its colour, display encoded, and its opacity, as the sun's program wrote them. Uniform control
// flow only: the coordinates' derivatives are taken before the test of the quad.
vec4 skySun(vec3 dir)
{
    bool hit;
    vec2 uv = skyQuadCoord(dir, skySunQuad, hit);
    vec2 uv_dx = dFdx(uv);
    vec2 uv_dy = dFdy(uv);
    vec4 c = vec4(0.0);
    if (hit)
    {
        c = textureGrad(skySunMap, uv, uv_dx, uv_dy);
        if (skySunBlend > 0.0)
        {
            c = mix(c, textureGrad(skySunMapNext, uv, uv_dx, uv_dy), skySunBlend);
        }
    }
    return c;
}

// The moon along dir: its colour, display encoded, and its opacity, as the moon's program wrote them. Uniform control
// flow only.
vec4 skyMoon(vec3 dir)
{
    bool hit;
    vec2 uv = skyQuadCoord(dir, skyMoonQuad, hit);
    vec2 uv_dx = dFdx(uv);
    vec2 uv_dy = dFdy(uv);
    vec4 c = vec4(0.0);
    if (hit)
    {
        c = textureGrad(skyMoonMap, uv, uv_dx, uv_dy);

        // SL-14113 The quad around the disc leaves the stars behind it alone.
        // Moon texture has transparent pixels <0x55,0x55,0x55,0x00>
        if (c.a <= 2./255.) // 0.00784
        {
            c = vec4(0.0);
        }

        c.rgb *= moon_brightness;
        c.a   *= skyMoonFade;
    }
    return c;
}
