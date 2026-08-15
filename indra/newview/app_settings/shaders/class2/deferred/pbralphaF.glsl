/**
 * @file class1\deferred\pbralphaF.glsl
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

// NOTE: base colour and emissive are NOT converted here. Their samplers ask the hardware
// to decode (GLTF_COLOR_SAMPLER, pushGLTFBatchIndexed), which converts each texel before
// filtering rather than after -- converting a blend taken in sRGB space is what made
// minified albedo read too dark. Doing it here as well would convert twice.

/*[EXTRA_CODE_HERE]*/

#ifndef IS_HUD

uniform sampler2D diffuseMap;  //always in sRGB space
uniform sampler2D bumpMap;
uniform sampler2D emissiveMap;
uniform sampler2D specularMap; // PBR: Packed: Occlusion, Metal, Roughness

uniform float metallicFactor;
uniform float roughnessFactor;
uniform vec3 emissiveColor;

#if defined(HAS_SUN_SHADOW) || defined(HAS_SSAO)
uniform sampler2D lightMap;
#endif

uniform int sun_up_factor;
uniform vec3 sun_dir;
uniform vec3 moon_dir;
// Classic (legacy pre-PBR) sky lighting is a per-program compile-time variant, not a runtime
// uniform: the two paths differ by whole blocks of maths and a probe sample, and only one of
// them is ever live for a given sky. A macro rather than a const global -- these sources are
// separately compiled units linked into one program, and several of them declare this.
#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif

out vec4 frag_color;

in vec3 vary_fragcoord;

#ifdef HAS_SUN_SHADOW
  uniform vec2 screen_res;
#endif

in vec3 vary_position;

in vec2 base_color_texcoord;
in vec2 normal_texcoord;
in vec2 metallic_roughness_texcoord;
in vec2 emissive_texcoord;

in vec4 vertex_color;

in vec3 vary_normal;
in vec3 vary_tangent;
flat in float vary_sign;


#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
#endif

// Lights
// See: LLRender::syncLightState()
// Shared forward-light arrays, spliced from class1/deferred/lightsBlock.glsl and
// bound at UB_LIGHTS. Members are read by bare name.
//[ENGINE_BLOCK Lights]

vec3 linear_to_srgb(vec3 c);

void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);
vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color);

void calcHalfVectors(vec3 lv, vec3 n, vec3 v, out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float calcLegacyDistanceAttenuation(float distance, float falloff);
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit_linear);

void mirrorClip(vec3 pos);
void waterClip(vec3 pos);

void calcDiffuseSpecular(vec3 baseColor, float metallic, inout vec3 diffuseColor, inout vec3 specularColor);
float filterSpecularRoughness(float perceptualRoughness, vec3 n);
vec3 pbrEnergyCompensation(vec3 specularColor, float perceptualRoughness, float nv);
vec3 clampRadiance(vec3 c);
vec3 clampHDRRange(vec3 color);
float horizonOcclusion(vec3 r, vec3 geometricNormal);

vec3 pbrBaseLight(vec3 diffuseColor,
                  vec3 specularColor,
                  float metallic,
                  vec3 pos,
                  vec3 norm,
                  float perceptualRoughness,
                  vec3 light_dir,
                  vec3 sunlit,
                  float scol,
                  vec3 radiance,
                  vec3 irradiance,
                  vec3 colorEmissive,
                  float ao,
                  vec3 additive,
                  vec3 atten);

vec3 pbrCalcPointLightOrSpotLight(vec3 diffuseColor, vec3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    vec3 n, // normal
                    vec3 p, // pixel position
                    vec3 v, // view vector (negative normalized pixel position)
                    vec3 lp, // light position
                    vec3 ld, // light direction (for spotlights)
                    vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight, float ambiance);

void main()
{
    mirrorClip(vary_position);

    vec3 color = vec3(0,0,0);

    vec3  light_dir   = (sun_up_factor == 1) ? sun_dir : moon_dir;
    vec3  pos         = vary_position;

    waterClip(pos);

    vec4 basecolor = texture(diffuseMap, base_color_texcoord.xy).rgba;
#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }
#endif

    vec3 col = vertex_color.rgb * basecolor.rgb;

#ifdef FOR_IMPOSTOR
    // Flat, unlit base colour -- the impostor bake wants ALBEDO, not a shaded result.
    //
    // An impostor is a G-buffer capture that gets replayed into the scene G-buffer and lit
    // once at composite time. Shading here and then handing that result over as albedo lights
    // it twice: sun, shadow, probe irradiance and fog all applied at bake, then applied again
    // to the billboard. deferred/alphaF has had a FOR_IMPOSTOR branch for exactly this
    // reason; the PBR and legacy-material alpha paths never got one, so they were the two
    // still double-lighting. Matches that branch: base colour times vertex colour, nothing
    // else, with coverage in alpha for the bake's premultiplied compositing.
    //
    // The material response is genuinely lost for blended surfaces in an impostor. It cannot
    // be kept: a G-buffer has one normal and one ORM per texel and blended layers have no
    // single value for either, which is why the flat path is the convention here.
    frag_color = max(vec4(col, basecolor.a * vertex_color.a), vec4(0));
#else

    vec3 vNt = texture(bumpMap, normal_texcoord.xy).xyz*2.0-1.0;
    float sign = vary_sign;
    vec3 vN = vary_normal;
    vec3 vT = vary_tangent.xyz;

    vec3 vB = sign * cross(vN, vT);
    vec3 norm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    // Held in a variable because the geometric normal below has to turn with it. Flipping only
    // the shading normal leaves the two pointing into opposite hemispheres on a back face, and
    // the horizon test reads that as a reflection fully underground.
    //
    // The tangent basis above is built from the raw vary_normal on purpose: mikktspace flips the
    // result, not the inputs.
    float facing = gl_FrontFacing ? 1.0 : -1.0;
    norm *= facing;

    float scol = 1.0;
    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;
    calcAtmosphericVarsLinear(pos.xyz, norm, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    vec3 sunlit_linear = sunlit;

    vec2 frag = vary_fragcoord.xy/vary_fragcoord.z*0.5+0.5;

#ifdef HAS_SUN_SHADOW
    scol = sampleDirectionalShadow(pos.xyz, norm.xyz, frag);
#endif

    vec3 orm = texture(specularMap, metallic_roughness_texcoord.xy).rgb; //orm is packed into "emissiveRect" to keep the data in linear color space

    float perceptualRoughness = orm.g * roughnessFactor;
    float metallic = orm.b * metallicFactor;
    float ao = orm.r;

    // The deferred path filters roughness once into the GBuffer; this one shades in place, so
    // it does the same correction against its own normal here.
    perceptualRoughness = filterSpecularRoughness(perceptualRoughness, norm);

    // emissiveColor is the emissive color factor from GLTF and is already in linear space
    vec3 colorEmissive = emissiveColor;
    // emissiveMap here is a vanilla RGB texture encoded as sRGB, manually convert to linear
    colorEmissive *= texture(emissiveMap, emissive_texcoord.xy).rgb;

    // PBR IBL
    float gloss      = 1.0 - perceptualRoughness;
    vec3  irradiance = amblit;
    vec3  radiance  = vec3(0);
    // frag, not a rescaled vary_position: the tc argument is a screen UV. It reaches
    // tapScreenSpaceReflection, where it drives the screen-edge vignette and the per-pixel
    // ray jitter, and vary_position is an eye-space metre offset -- outside [0,1] for all but
    // a sliver of the frame, so the vignette never fades and the jitter stops decorrelating.
    sampleReflectionProbes(irradiance, radiance, frag, pos.xyz, norm.xyz, gloss, true, amblit);

    // A normal map can aim the reflection below the surface it sits on, where the probe happily
    // returns radiance arriving through the geometry. vary_normal is the untouched interpolated
    // vertex normal, so it is the geometric horizon to test against -- the deferred path has no
    // equivalent, because the GBuffer only ever stored the perturbed normal.
    radiance *= horizonOcclusion(reflect(normalize(pos.xyz), norm.xyz), vary_normal * facing);

    vec3 diffuseColor = vec3(0.0);
    vec3 specularColor = vec3(0.0);
    calcDiffuseSpecular(col.rgb, metallic, diffuseColor, specularColor);

    vec3 v = -normalize(pos.xyz);

    // The material's occlusion alone: the SSAO buffer is built from the opaque depth pass, so it
    // describes the surface behind this one, not this one.
    color = pbrBaseLight(diffuseColor, specularColor, metallic, v, norm.xyz, perceptualRoughness, light_dir, sunlit_linear, scol, radiance, irradiance, colorEmissive, ao, additive, atten);

    vec3 light = vec3(0);

    // Punctual lights
#define LIGHT_LOOP(i) light += pbrCalcPointLightOrSpotLight(diffuseColor, specularColor, perceptualRoughness, metallic, norm.xyz, pos.xyz, v, light_position[i].xyz, light_direction[i].xyz, light_diffuse[i].rgb, light_deferred_attenuation[i].x, light_deferred_attenuation[i].y, light_attenuation[i].z, light_attenuation[i].w);

    LIGHT_LOOP(1)
    LIGHT_LOOP(2)
    LIGHT_LOOP(3)
    LIGHT_LOOP(4)
    LIGHT_LOOP(5)
    LIGHT_LOOP(6)
    LIGHT_LOOP(7)

    color.rgb += light.rgb;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, vec4(color, 1.0)).rgb;

    float a = basecolor.a*vertex_color.a;
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
    // Scrubbed the same way softenLightF scrubs the opaque result. max() alone does not do it:
    // it passes an infinity through unchanged, and its answer for a NaN is undefined. This path
    // runs the same unbounded punctual lobe the opaque one does, so it can produce the same
    // values -- and whatever it writes goes on to the bloom pyramid.
    frag_color = max(vec4(clampHDRRange(color.rgb * final_scale), a), vec4(0));
#endif // FOR_IMPOSTOR
}

#else

uniform sampler2D diffuseMap;  //always in sRGB space
uniform sampler2D emissiveMap;

uniform vec3 emissiveColor;

out vec4 frag_color;

in vec3 vary_position;

in vec2 base_color_texcoord;
in vec2 emissive_texcoord;

in vec4 vertex_color;

#ifdef HAS_ALPHA_MASK
uniform float minimum_alpha; // PBR alphaMode: MASK, See: mAlphaCutoff, setAlphaCutoff()
#endif

vec3 linear_to_srgb(vec3 c);


void main()
{
    vec3 color = vec3(0,0,0);

    vec3  pos         = vary_position;

    vec4 basecolor = texture(diffuseMap, base_color_texcoord.xy).rgba;
#ifdef HAS_ALPHA_MASK
    if (basecolor.a < minimum_alpha)
    {
        discard;
    }
#endif

    color = vertex_color.rgb * basecolor.rgb;

    // emissiveColor is the emissive color factor from GLTF and is already in linear space
    vec3 colorEmissive = emissiveColor;
    // emissiveMap here is a vanilla RGB texture encoded as sRGB, manually convert to linear
    colorEmissive *= texture(emissiveMap, emissive_texcoord.xy).rgb;


    float a = basecolor.a*vertex_color.a;
    color += colorEmissive;

    color = linear_to_srgb(color);
    frag_color = max(vec4(color.rgb,a), vec4(0));
}

#endif
