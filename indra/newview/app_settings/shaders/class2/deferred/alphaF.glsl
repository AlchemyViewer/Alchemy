/**
 * @file alphaF.glsl
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

//class2/deferred/alphaF.glsl

/*[EXTRA_CODE_HERE]*/

#define INDEXED 1
#define NON_INDEXED 2
#define NON_INDEXED_NO_COLOR 3

out vec4 frag_color;

uniform mat3 env_mat;
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

#ifdef USE_DIFFUSE_TEX
uniform sampler2D diffuseMap;
#endif

in vec3 vary_fragcoord;
in vec3 vary_position;
in vec2 vary_texcoord0;
in vec3 vary_norm;

#ifdef USE_VERTEX_COLOR
in vec4 vertex_color; //vertex color should be treated as sRGB
#endif

uniform float minimum_alpha;

uniform mat4 proj_mat;
// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;
uniform int sun_up_factor;
// Shared forward-light arrays, spliced from class1/deferred/lightsBlock.glsl and
// bound at UB_LIGHTS. Members are read by bare name.
//[ENGINE_BLOCK Lights]

void waterClip(vec3 pos);

vec3 srgb_to_linear(vec3 c);
vec3 linear_to_srgb(vec3 c);

vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color);
void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
#endif

float getAmbientClamp();

void mirrorClip(vec3 pos);

void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit_linear);

// The shared legacy punctual model, not a copy of it -- the same function the deferred legacy
// branch is built from, so a blended surface is lit like the opaque one behind it. The
// diffuse-only entry point: these objects have no specular map or colour to give one.
vec3 calcLegacyPointLightDiffuse(vec3 diffuse,
                    vec3 n, vec3 p, vec3 v,
                    vec3 lp, vec3 ld, vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight);

void main()
{
    mirrorClip(vary_position);

    vec2 frag = vary_fragcoord.xy/vary_fragcoord.z*0.5+0.5;

    vec4 pos = vec4(vary_position, 1.0);
#ifndef IS_AVATAR_SKIN
    // clip against water plane unless this is a legacy avatar skin
    waterClip(pos.xyz);
#endif
    // Interpolation across a triangle shortens a normal that was unit-length at each vertex.
    // Every sibling forward path normalizes here, and the deferred path always reads a unit
    // normal out of the GBuffer, so leaving it raw made this the one surface whose NdotL was
    // scaled by an accident of where it sat within its triangle.
    vec3 norm = normalize(vary_norm);

    float shadow = 1.0f;

#ifdef HAS_SUN_SHADOW
    shadow = sampleDirectionalShadow(pos.xyz, norm.xyz, frag);
#endif

#ifdef USE_DIFFUSE_TEX
    vec4 diffuse_tap = texture(diffuseMap,vary_texcoord0.xy);
#endif

#ifdef USE_INDEXED_TEX
    vec4 diffuse_tap = diffuseLookup(vary_texcoord0.xy);
#endif

    vec4 diffuse_srgb = diffuse_tap;

#ifdef FOR_IMPOSTOR
    // Misnamed here too, for the same reason as the branch below: LINEAR_DIFFUSE decodes on
    // the sampler, so this is already linear.
    vec4 diffuse_linear = diffuse_srgb;

    float final_alpha = diffuse_linear.a * vertex_color.a;

    // Insure we don't pollute depth with invis pixels in impostor rendering
    //
    if (final_alpha < minimum_alpha)
    {
        discard;
    }

    // Linear in, linear out. The sampler decodes, the tint was linearized in the vertex
    // stage, and generateImpostor enables GL_FRAMEBUFFER_SRGB so the store encodes -- the
    // same convention every other forward writer into the bake target follows.
    //
    // This used to be the exception: sample encoded, tint in gamma space, write raw. It
    // survived only because the impostor's post-deferred pass did not encode either, which
    // is the same gap that left fullbright and PBR alpha too dark in the bake. Tinting now
    // happens in linear, which is a real (and correct) change for vertex-coloured alpha.
    vec4 color;
    color.rgb = diffuse_linear.rgb * vertex_color.rgb;
    color.a = final_alpha;

#else // FOR_IMPOSTOR

    // diffuse_srgb is misnamed on this path: the sampler decoded it, so it is already
    // linear. (Same for the FOR_IMPOSTOR branch above. IS_HUD is the one place the name is
    // still accurate -- it samples raw and decodes below, after its raw tint multiply.)
    vec4 diffuse_linear = diffuse_srgb;

    vec3 light_dir = (sun_up_factor == 1) ? sun_dir: moon_dir; // TODO -- factor out "sun_up_factor" and just send in the appropriate light vector

    float final_alpha = diffuse_linear.a;

#ifdef IS_AVATAR_SKIN
    if(final_alpha < minimum_alpha)
    {
        discard;
    }
#endif

#ifdef USE_VERTEX_COLOR
    final_alpha *= vertex_color.a;

    if (final_alpha < minimum_alpha)
    { // TODO: figure out how to get invisible faces out of
        // render batches without breaking glow
        discard;
    }

    diffuse_linear.rgb *= vertex_color.rgb; // both linear now (IS_HUD: both sRGB)
#ifndef LINEAR_DIFFUSE
    // The HUD permutation -- the only non-impostor variant without LINEAR_DIFFUSE --
    // deliberately skips the sampler decode -- mLinearDiffuse is unset,
    // HUDs keep pixel-exact gamma-space filtering -- and its tint stays raw in the vertex
    // stage, so decode the tinted product here, the same order the pre-sampler-decode code
    // used. That makes the trailing linear_to_srgb an exact round trip. Without it the
    // encoded texel falls through the linear math and is encoded AGAIN at the end, washing
    // out every alpha-blended HUD element.
    diffuse_linear.rgb = srgb_to_linear(diffuse_linear.rgb);
#endif
#endif // USE_VERTEX_COLOR

    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;

    calcAtmosphericVarsLinear(pos.xyz, norm, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    vec3 sunlit_linear = sunlit;
    vec3 amblit_linear = amblit;

    vec3 irradiance = amblit;
    vec3 glossenv = vec3(0.0);
    vec3 legacyenv = vec3(0.0);
    sampleReflectionProbesLegacy(irradiance, glossenv, legacyenv, frag, pos.xyz, norm.xyz, 0.0, 0.0, true, amblit_linear);


    float da = dot(norm.xyz, light_dir.xyz);
          da = clamp(da, -1.0, 1.0);

    float final_da = da;
          final_da = clamp(final_da, 0.0f, 1.0f);

    vec4 color = vec4(0.0);

    color.a   = final_alpha;

    color.rgb = irradiance;
    if (classic_mode > 0)
    {
        final_da = pow(final_da,1.2);
        vec3 sun_contrib = vec3(min(final_da, shadow));

        color.rgb = srgb_to_linear(color.rgb * 0.9 + linear_to_srgb(sun_contrib) * sunlit_linear * 0.7);
        sunlit_linear = srgb_to_linear(sunlit_linear);
    }
    else
    {
        vec3 sun_contrib = min(final_da, shadow) * sunlit_linear;
        color.rgb += sun_contrib;
    }

    color.rgb *= diffuse_linear.rgb;

    vec4 light = vec4(0,0,0,0);

    vec3 npos = normalize(-pos.xyz);

// light_deferred_attenuation carries the size/falloff pair the deferred pass reads, as the PBR
// alpha path already takes them. light_attenuation.z stays: it is the is-omni flag.
   #define LIGHT_LOOP(i) light.rgb += calcLegacyPointLightDiffuse(diffuse_linear.rgb, norm, pos.xyz, npos, light_position[i].xyz, light_direction[i].xyz, light_diffuse[i].rgb, light_deferred_attenuation[i].x, light_deferred_attenuation[i].y, light_attenuation[i].z);

    LIGHT_LOOP(1)
    LIGHT_LOOP(2)
    LIGHT_LOOP(3)
    LIGHT_LOOP(4)
    LIGHT_LOOP(5)
    LIGHT_LOOP(6)
    LIGHT_LOOP(7)

    // sum local light contrib in linear colorspace
    color.rgb += light.rgb;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, color).rgb;

#endif // #else // FOR_IMPOSTOR
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
#ifdef IS_HUD
    color.rgb = linear_to_srgb(color.rgb);
    final_scale = 1;
#endif

    color.rgb *= final_scale;
    frag_color = max(color, vec4(0));
}

