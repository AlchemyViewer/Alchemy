/**
 * @file class1/deferred/skyCompositeF.glsl
 * @brief The sky in one pass: every layer worked out along the pixel's view ray and composited in registers, from the
 *        dome up, by alpha and additive blends, then decoded for the lit screen.
 *        The sky's pass (deferred/skyCompositeMainF.glsl) calls it at every pixel the world left at the far plane.
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

// The view's projection, spliced from class1/deferred/matricesBlock.glsl and bound at UB_MATRICES, and the rotation
// from eye space to the dome's frame, eyeToSky, spliced from class1/deferred/environmentBlock.glsl and bound at
// UB_ENVIRONMENT.
//[ENGINE_BLOCK Matrices]
//[ENGINE_BLOCK Environment]

// The layers drawn, one bit each: ALSkyLayers::Layer, which these must equal.
#define SKY_LAYER_STARS   1
#define SKY_LAYER_BLOOM   2
#define SKY_LAYER_SUN     4
#define SKY_LAYER_MOON    8
#define SKY_LAYER_METEORS 16
#define SKY_LAYER_AURORA  32
#define SKY_LAYER_CLOUDS  64
#define SKY_LAYER_HDRI    128

uniform int skyLayers;

// WindLight's fake HDR scale.
uniform float sky_hdr_scale;

// The HDRI sky: its map, the rotation it is turned by, the exposure it is scaled by, and the share of the screen from
// the right it takes beside WindLight's dome.
uniform sampler2D skyHdriMap;
uniform mat3      skyHdriRotation;
uniform float     skyHdriScale;
uniform float     hdri_split_screen;

// The stars' attachment of the G-buffer (ALSky::renderStars, ALSkyLayers::starAttachment): the emissive buffer, or the
// sRGB albedo, read decoded. It is cleared to black, where nothing else writes it, only in a view that draws the stars,
// and read only there.
#if defined(HAS_EMISSIVE)
uniform sampler2D emissiveRect;
#else
uniform sampler2D diffuseRect;
#endif

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

// windlight/windlightDomeF.glsl
vec3 windlightDome(vec3 dir);
vec3 windlightDomeHit(vec3 dir);
vec3 windlightDomeRaise(vec3 hit);

// deferred/skyBodiesF.glsl, deferred/skyMeteorsF.glsl, deferred/skyAuroraF.glsl, deferred/skyCloudsF.glsl
vec4 skySun(vec3 dir);
vec4 skyMoon(vec3 dir);
#ifdef SUN_BLOOM
vec4 skyBloom(vec3 dir);
#endif
vec3 skyMeteors(vec2 ndc);
vec3 skyAurora(vec3 dir);
vec4 skyClouds(vec3 dir);

// The view ray through the pixel at ndc, a unit vector in the dome's frame. Any depth between the planes lies on the
// ray; the far plane does not do, since under an infinite projection it unprojects to w = 0.
vec3 skyRay(vec2 ndc)
{
    vec4 eye = inv_proj * vec4(ndc, 0.5, 1.0);
    return normalize(eyeToSky * (eye.xyz / eye.w));
}

// The brightest the HDRI sky is drawn, after its exposure: R11F_G11F_B10F's largest value, the most the reflections'
// copy of the scene and the bloom pyramid (without halation) hold, so a file's sun keeps its full weight in them and
// in the probes. The sky's pass holds its output, with the classic gain, to the half floats it is stored in
// (skyCompositeMainF.glsl).
const float SKY_HDRI_MAX = 65024.0;

// The HDRI sky along dir, linear: the map at the point the ray meets the dome's sphere, raised as WindLight's dome
// raises it, turned and wrapped about the sphere. Negative texels, which some HDR files hold, are black.
vec3 skyHdri(vec3 dir)
{
    const float PI = 3.14159265;
    vec3 pos = skyHdriRotation * normalize(windlightDomeRaise(windlightDomeHit(dir)));
    vec2 texCoord = vec2(atan(pos.z, pos.x) + PI, acos(pos.y)) / vec2(2.0 * PI, PI);
    vec3 color = textureLod(skyHdriMap, texCoord, 0.0).rgb * skyHdriScale;
    return clamp(color, vec3(0.0), vec3(SKY_HDRI_MAX));
}

// What the stars added at tc, display encoded. The one texel point sampling picks at tc, read at level 0, which needs no
// derivatives: the composite reads it only where the clouds let the sky through.
vec3 skyStars(vec2 tc)
{
#if defined(HAS_EMISSIVE)
    return textureLod(emissiveRect, tc, 0.0).rgb;
#else
    return linear_to_srgb(textureLod(diffuseRect, tc, 0.0).rgb);
#endif
}

// The sky at tc, the pixel's place on the screen, linear and scaled for the lit screen. The layers composite in float
// and display encoded, from the dome up:
//  1. the dome, or an HDRI with its split screen, which stands alone;
//  2. plus the stars;
//  3. the sun bloom, alpha;
//  4. the sun, alpha;
//  5. the moon, alpha;
//  6. plus the meteors;
//  7. plus the aurora;
//  8. the clouds, alpha;
//  9. decoded, times sky_hdr_scale.
// The clouds are worked out first: where they are opaque nothing under them shows, and steps 1 to 7 are skipped. Every
// layer is gated on a uniform, and the clouds and the bodies, which take derivatives of their texture coordinates, are
// worked out ahead of the clouds' test, in uniform control flow; under it the dome, the stars, the meteors and the
// aurora take none.
vec3 skyComposite(vec2 tc)
{
    vec2 ndc = tc * 2.0 - 1.0;
    vec3 dir = skyRay(ndc);

    if ((skyLayers & SKY_LAYER_HDRI) != 0)
    {
        if (-ndc.x > (1.0 - hdri_split_screen) * 2.0 - 1.0)
        {
            return skyHdri(dir);
        }
        return srgb_to_linear(windlightDome(dir)) * sky_hdr_scale;
    }

    vec4 clouds = vec4(0.0);
    if ((skyLayers & SKY_LAYER_CLOUDS) != 0)
    {
        clouds = skyClouds(dir);
    }

#ifdef SUN_BLOOM
    vec4 bloom = vec4(0.0);
    if ((skyLayers & SKY_LAYER_BLOOM) != 0)
    {
        bloom = skyBloom(dir);
    }
#endif

    vec4 sun = vec4(0.0);
    if ((skyLayers & SKY_LAYER_SUN) != 0)
    {
        sun = skySun(dir);
    }

    vec4 moon = vec4(0.0);
    if ((skyLayers & SKY_LAYER_MOON) != 0)
    {
        moon = skyMoon(dir);
    }

    vec3 color = vec3(0.0);
    if (clouds.a < 1.0)
    {
        color = windlightDome(dir);

        if ((skyLayers & SKY_LAYER_STARS) != 0)
        {
            color += skyStars(tc);
        }

#ifdef SUN_BLOOM
        if ((skyLayers & SKY_LAYER_BLOOM) != 0)
        {
            color = mix(color, bloom.rgb, bloom.a);
        }
#endif

        if ((skyLayers & SKY_LAYER_SUN) != 0)
        {
            color = mix(color, sun.rgb, sun.a);
        }

        if ((skyLayers & SKY_LAYER_MOON) != 0)
        {
            color = mix(color, moon.rgb, moon.a);
        }

        if ((skyLayers & SKY_LAYER_METEORS) != 0)
        {
            color += skyMeteors(ndc);
        }

        if ((skyLayers & SKY_LAYER_AURORA) != 0)
        {
            color += skyAurora(dir);
        }
    }

    if ((skyLayers & SKY_LAYER_CLOUDS) != 0)
    {
        color = mix(color, clouds.rgb, clouds.a);
    }

    return srgb_to_linear(color) * sky_hdr_scale;
}
