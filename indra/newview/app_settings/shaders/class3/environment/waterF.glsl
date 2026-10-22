/**
 * @file waterF.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

// class3/environment/waterF.glsl

#define WATER_MINIMAL 1

out vec4 frag_color;

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
#endif

void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);
vec4 applyWaterFogViewLinearNoClip(vec3 pos, vec4 color);

bool mirrorClipped(vec3 pos);

// PBR interface
vec2 BRDF(float NoV, float roughness);


uniform sampler2D bumpMap;
uniform sampler2D bumpMap2;
#ifdef TRANSPARENT_WATER
uniform sampler2D screenTex;
uniform sampler2D depthMap;
#endif


// Classic (legacy pre-PBR) sky lighting is a per-program compile-time variant, not a runtime
// uniform: the two paths differ by whole blocks of maths and a probe sample, and only one of
// them is ever live for a given sky. A macro rather than a const global -- these sources are
// separately compiled units linked into one program, and several of them declare this.
#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif
uniform vec3 lightDir;
uniform float blurMultiplier;
uniform float refScale;
uniform float kd;
uniform vec3 normScale;
uniform float fresnelScale;
// The roughness of the sun's reflection on each wave face (AlchemyRenderWaterGlintRoughness).
uniform float waterGlintRoughness;
uniform float fresnelOffset;

// waterFar.x is the wave clamp distance and .y the edge stretch, 0 unless the projection is infinite
// (ALFarPlane::WaterFar); waterRim is the field's outer rectangle relative to the camera, min xy then max xy.
uniform vec2 waterFar;
uniform vec4 waterRim;

// The pack's screen, which the water is drawn into and whose copy screenTex and depthMap hold, stretched to theirs.
uniform vec2 screen_res;

in vec3 view;
in vec3 vary_position;

vec3 srgb_to_linear(vec3 col);
vec3 linear_to_srgb(vec3 col);

vec3 atmosLighting(vec3 light);

vec3 transform_normal(vec3 vN, vec3 vT, vec3 vB, vec3 vNt)
{
    return normalize(vNt.x * vT + vNt.y * vB + vNt.z * vN);
}

void sampleReflectionProbesWater(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, vec3 amblit_linear);

void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit_linear);

void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit);


vec3 getPositionWithNDC(vec3 ndc);
float ndcZFromScreenDepth(float d);   // deferredUtil.glsl -- depth-convention aware

void water_wave_normals(sampler2D map_a, sampler2D map_b, vec2 r, out vec3 big, out vec3 little_xy, out vec3 little_zw);

void calculateFresnelFactors(out vec3 df3, out vec2 df2, vec3 viewVec, vec3 wave1, vec3 wave2, vec3 wave3, vec3 wavef)
{
    // We calculate the fresnel here.
    // We do this by getting the dot product for each sets of waves, and applying scale and offset.

    df3 = max(vec3(0), vec3(
        dot(viewVec, wave1),
        dot(viewVec, (wave2 + wave3) * 0.5),
        dot(viewVec, wave3)
    ) * fresnelScale + fresnelOffset);

    df3 *= df3;

    df2 = max(vec2(0), vec2(
        df3.x + df3.y + df3.z,
        dot(viewVec, wavef) * fresnelScale + fresnelOffset
    ));
}

void main()
{
    // Discarded at the end: the wave normals and the shadow take derivatives.
    bool clipped = mirrorClipped(vary_position);

    // The plane's frame and the light in eye space, the same at every point: agent z and x through the view.
    vec3 vN = normal_matrix[2];
    vec3 vT = normal_matrix[0];
    vec3 vB = cross(vN, vT);
    vec3 vary_light_dir = normal_matrix * lightDir;

    vec3 pos = vary_position.xyz;
    float linear_depth = 1 / -pos.z;

    //normalize view vector
    vec3 viewVec = normalize(pos.xyz);

    // The wave layers, from the camera-relative view vector, which interpolates precisely however far the quad reaches.
    vec3 wave1;
    vec3 wave2;
    vec3 wave3;
    water_wave_normals(bumpMap, bumpMap2, view.xy, wave1, wave2, wave3);

    vec2 distort = gl_FragCoord.xy / screen_res;

    vec3 wavef = (wave1 + wave2 * 0.4 + wave3 * 0.6) * 0.5;

    vec3 df3 = vec3(0);
    vec2 df2 = vec2(0);

    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;
    calcAtmosphericVarsLinear(pos.xyz, wavef, vary_light_dir, sunlit, amblit, additive, atten);

    calculateFresnelFactors(df3, df2, normalize(view.xyz), wave1, wave2, wave3, wavef);

    vec3 up = transform_normal(vN, vT, vB, vec3(0,0,1));

    // The reflection's normal: each wave layer's slopes by its own normal scale, as the water settings name the three,
    // where 2, the default, leaves them as they are; the composite's height doubled, halving its slopes. On the axes
    // of the composite, equal scales cancelled in the normalize, and the setting did nothing.
    vec3 wave_ibl = vec3((wave1.xy * normScale.x + wave2.xy * (0.4 * normScale.y) + wave3.xy * (0.6 * normScale.z)) * 0.25, wavef.z * 2.0);
    wave_ibl = transform_normal(vN, vT, vB, normalize(wave_ibl));

    // Never below the horizon. A wave tilted across a low view reflects the ray down, into the probes' lower faces:
    // the ground seen from above, where the next wave would hide it. Such a reflection is held on the horizon, and
    // its normal becomes the one that reflects the view ray there, which the probes and screen space reflections take.
    float ref_below = dot(reflect(viewVec, wave_ibl), up);
    if (ref_below < 0.0)
    {
        vec3 ref_horizon = reflect(viewVec, wave_ibl) - up * ref_below;
        vec3 ref_half = ref_horizon * inversesqrt(max(dot(ref_horizon, ref_horizon), 1e-12)) - viewVec;
        wave_ibl = dot(ref_half, ref_half) > 1e-12 ? normalize(ref_half) : up;
    }

    vec3 norm = transform_normal(vN, vT, vB, normalize(wavef));

    // The wave normal, its slopes steepened as the view grazes the surface, by one over the sine of the angle the view
    // looks down at, up to ten times, as WindLight's water had it (wavef.z *= max(-viewVec.z, 0.1)). Seen low, the
    // waves' faces turned toward the viewer fill more of each pixel: the sun's path breaks into a wide field of glints,
    // and what is seen through the surface ripples harder.
    vec3 steep_wave = wavef;
    steep_wave.z *= max(-normalize(view.xyz).z, 0.1);
    steep_wave = normalize(steep_wave);
    vec3 sun_norm = transform_normal(vN, vT, vB, steep_wave);

    // WindLight's ripples are offsets on the screen, which shrink with the distance along the surface, 5 m at least.
    float flat_dist = max(length(view.xy), 5.0);
    float flat_dmod = sqrt(flat_dist);

    // What is seen through the surface, displaced by the steepened wave, and less where the surface mostly reflects.
    vec2 distort2 = distort + steep_wave.xy * refScale / max(flat_dmod * df2.x, 1.0);

    distort2 = clamp(distort2, vec2(0), vec2(0.999));

    float shadow = 1.0f;

#ifdef HAS_SUN_SHADOW
    shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, distort);
#endif

    vec3 sunlit_linear = sunlit;
    float fade = 1;
#ifdef TRANSPARENT_WATER
    float depth = texture(depthMap, distort).r;

    vec3 refPos = getPositionWithNDC(vec3(distort*2.0-vec2(1.0), ndcZFromScreenDepth(depth)));

    // Calculate some distance fade in the water to better assist with refraction blending and reducing the refraction texture's "disconnect".
#ifdef SHORELINE_FADE
    fade = max(0,min(1, (pos.z - refPos.z) / 10));
#else
    fade = 1;
#endif
    distort2 = mix(distort, distort2, min(1, fade * 10));
    depth = texture(depthMap, distort2).r;

    refPos = getPositionWithNDC(vec3(distort2 * 2.0 - vec2(1.0), ndcZFromScreenDepth(depth)));

    if (pos.z < refPos.z - 0.05)
    {
        distort2 = distort;
    }

    vec4 fb = texture(screenTex, distort2);

#else
    // Opaque water: the "refraction" is the fog of 2048 m of water behind the surface, as
    // waterHazeF fogs the void past the field. Measured from the surface, not from the eye: a
    // point 2048 m from the eye lies above the plane wherever the surface is farther than that,
    // and there the clipping fog returned its input, white.
    vec4 fb = applyWaterFogViewLinearNoClip(pos + viewVec*2048.0, vec4(1.0));
#endif

    // The environment's blur, for the reflections and refractions alone.
    float perceptualRoughness = blurMultiplier;
    float gloss      = 1 - perceptualRoughness;

    vec3  irradiance = vec3(0);
    vec3  radiance  = vec3(0);
    vec3 legacyenv = vec3(0);

    // TODO: Make this an option.
#ifdef WATER_MINIMAL
    sampleReflectionProbesWater(irradiance, radiance, distort2, pos.xyz, wave_ibl.xyz, gloss, amblit);
#elif WATER_MINIMAL_PLUS
    sampleReflectionProbes(irradiance, radiance, distort2, pos.xyz, wave_ibl.xyz, gloss, false, amblit);
#endif

    vec3 v = -normalize(pos.xyz);

    vec3 colorEmissive = vec3(0);
    float ao = 1.0;

    float NdotV = clamp(abs(dot(norm, v)), 0.001, 1.0);

    // The sun on the steepened wave normal, as WindLight's water lit it: a Phong lobe about the mirror direction,
    // normalized, its exponent 2 / alpha^2 - 2 from the roughness (0.35 is WindLight's 128). Its tail falls off fast
    // enough that only the faces turned toward the sun glint and the faces between them stay dark; a microfacet lobe's
    // long tail lit every face within a few tens of degrees, and on the steepened slopes drew each crest across the path
    // as a streak. Water is a dielectric with no diffuse surface of its own: the glint is the light's own colour at the
    // Fresnel reflectance of the angle between the view and the light, 2% looking down at it and most of it at grazing.
    vec3 l = transform_normal(vN, vT, vB, lightDir);
    float glint_alpha = waterGlintRoughness * waterGlintRoughness;
    float glint_exp = max(2.0 / (glint_alpha * glint_alpha) - 2.0, 1.0);
    float lobe = pow(clamp(dot(reflect(-v, sun_norm), l), 0.0, 1.0), glint_exp);
    float glint = lobe * (glint_exp + 2.0) / 6.2831853;
    // The half vector's cosine with the view, sqrt((1 + v.l) / 2), which needs no half vector: v and l are opposite at
    // the horizon under a setting sun, and theirs would not normalize. Rounding can carry it a hair past 1 with the sun
    // straight behind the camera, where pow would take a negative base.
    float vh = sqrt(max(0.5 + 0.5 * dot(v, l), 0.0));
    float fresnel = 0.02 + 0.98 * pow(max(1.0 - vh, 0.0), 5.0);
    // Not dimmed by the eye path's haze here: the haze pass dims every water pixel by it
    // afterwards (doAtmospherics), the glint along with the reflection and refraction.
    vec3 punctual = min(glint * fresnel, 10.0) * sunlit_linear * shadow;

    // The glint's glow, as WindLight's water wrote it: the lobe at its peak of 1, by 0.1 rising to 0.3 as the light
    // climbs, which the glow and bloom spread into a soft halo about the sparkles. Shadowed glints have none.
    float glint_glow = lobe * (0.1 + 0.2 * max(lightDir.z, 0.0)) * shadow;

    radiance *= df2.y;
    vec3 color = vec3(0);
    color = mix(fb.rgb, radiance, min(1, df2.x)) + punctual.rgb;

    float water_haze_scale = 4;

    if (classic_mode > 0)
        water_haze_scale = 1;

    // This looks super janky, but we do this to restore water haze in the distance.
    // These values were finagled in to try and bring back some of the distant brightening on legacy water.  Also works reasonably well on PBR skies such as PBR midday.
    // color = mix(color, additive * water_haze_scale, (1 - atten));

    // We shorten the fade here at the shoreline so it doesn't appear too soft from a distance.
    fade *= 60;
    fade = min(1, fade);
    color = mix(fb.rgb, color, fade);
    glint_glow *= fade;

    // Over the last 15% of the edge water the sky behind it shows through; the pass is unblended, so the water
    // fades to the probes' sky along the view ray, which is what the missing water would have revealed.
    if (waterFar.y > 0.0)
    {
        vec2 to_rim2 = min(view.xy - waterRim.xy, waterRim.zw - view.xy);
        float gone = 1.0 - smoothstep(0.0, 0.15 * waterFar.y, min(to_rim2.x, to_rim2.y));
        if (gone > 0.0)
        {
            // The flat surface's reflection: at the rim's grazing angles that is the sky at the horizon. A normal across
            // the view ray would reflect it onto itself, into the probe's downward faces, and show the terrain from above.
            vec3 sky_amb = vec3(0);
            vec3 sky = vec3(0);
            sampleReflectionProbesWater(sky_amb, sky, distort, pos.xyz, up, 1.0, amblit);
            color = mix(color, sky, gone);
            glint_glow *= 1.0 - gone;
        }
    }

    // Compiled only in the mirror variant: a discard the compiler cannot see is dead costs every fragment its early
    // depth test.
#ifdef MIRROR_CLIP
    if (clipped)
    {
        discard;
    }
#endif

    // Not capped at 1: the glint and bright reflections are HDR and have to reach white and bloom, and
    // what shows through the surface must match the shore beside it. The alpha is the glint's glow, which
    // the pool adds to the glow of what lies beneath.
    frag_color = vec4(max(color.rgb, vec3(0)), glint_glow);
}

