/**
* @file materialF.glsl
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

//class1/deferred/materialF.glsl

// This shader is used for both writing opaque/masked content to the gbuffer and writing blended content to the framebuffer during the alpha pass.

#define DIFFUSE_ALPHA_MODE_NONE     0
#define DIFFUSE_ALPHA_MODE_BLEND    1
#define DIFFUSE_ALPHA_MODE_MASK     2
#define DIFFUSE_ALPHA_MODE_EMISSIVE 3

uniform float emissive_brightness;  // fullbright flag, 1.0 == fullbright, 0.0 otherwise
uniform int sun_up_factor;
// Classic (legacy pre-PBR) sky lighting is a per-program compile-time variant, not a runtime
// uniform: the two paths differ by whole blocks of maths and a probe sample, and only one of
// them is ever live for a given sky. A macro rather than a const global -- these sources are
// separately compiled units linked into one program, and several of them declare this.
#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif

vec4 applySkyAndWaterFog(vec3 pos, vec3 additive, vec3 atten, vec4 color);
void calcAtmosphericVarsLinear(vec3 inPositionEye, vec3 norm, vec3 light_dir, out vec3 sunlit, out vec3 amblit, out vec3 atten, out vec3 additive);
void calcHalfVectors(vec3 lv, vec3 n, vec3 v, out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist);
float blinnPhongLobe(float nh, float glossiness);

vec3 srgb_to_linear(vec3 cs);
vec3 linear_to_srgb(vec3 cs);

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

in vec3 vary_position;

void mirrorClip(vec3 pos);
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag);
float filterSpecularRoughness(float perceptualRoughness, vec3 n);

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)

out vec4 frag_color;

#ifdef HAS_SUN_SHADOW
float sampleDirectionalShadow(vec3 pos, vec3 norm, vec2 pos_screen);
#endif

void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit_linear);
void applyGlossEnv(inout vec3 color, vec3 glossenv, vec4 spec, vec3 pos, vec3 norm);
void applyLegacyEnv(inout vec3 color, vec3 legacyenv, vec4 spec, vec3 pos, vec3 norm, float envIntensity);

uniform samplerCube environmentMap;

// Inputs
uniform vec4 morphFactor;
uniform vec3 camPosLocal;
uniform mat3 env_mat;

uniform float is_mirror;

uniform vec3 sun_dir;
uniform vec3 moon_dir;

uniform mat4 proj_mat;
uniform vec2 screen_res;

// Shared forward-light arrays, spliced from class1/deferred/lightsBlock.glsl and
// bound at UB_LIGHTS. Members are read by bare name.
//[ENGINE_BLOCK Lights]

float getAmbientClamp();
void waterClip(vec3 pos);

// The shared legacy punctual model, not a copy of it -- the same function the deferred legacy
// branch is built from, so a blended surface is lit like the opaque one behind it.
vec3 calcLegacyPointLightOrSpotLight(vec3 diffuse, vec4 spec,
                    vec3 n, vec3 p, vec3 v,
                    vec3 lp, vec3 ld, vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight,
                    inout float glare);

#else
out vec4 frag_data[4];
#endif

uniform sampler2D diffuseMap;  //always in sRGB space

#ifdef HAS_NORMAL_MAP
uniform sampler2D bumpMap;
#endif

#ifdef HAS_SPECULAR_MAP
uniform sampler2D specularMap;

in vec2 vary_texcoord2;
#endif

uniform float env_intensity;
uniform vec4 specular_color;  // specular color RGB and specular exponent (glossiness) in alpha

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_MASK)
uniform float minimum_alpha;
#endif

#ifdef HAS_NORMAL_MAP
in vec3 vary_normal;
in vec3 vary_tangent;
flat in float vary_sign;
in vec2 vary_texcoord1;
#else
in vec3 vary_normal;
#endif

in vec4 vertex_color;
in vec2 vary_texcoord0;

// get the transformed normal and apply glossiness component from normal map
vec3 getNormal(inout float glossiness)
{
#ifdef HAS_NORMAL_MAP
    vec4 vNt = texture(bumpMap, vary_texcoord1.xy);
    glossiness *= vNt.a;
    vNt.xyz = vNt.xyz * 2 - 1;
    float sign = vary_sign;
    vec3 vN = vary_normal;
    vec3 vT = vary_tangent.xyz;

    vec3 vB = sign * cross(vN, vT);
    vec3 tnorm = normalize( vNt.x * vT + vNt.y * vB + vNt.z * vN );

    return tnorm;
#else
    return normalize(vary_normal);
#endif
}

vec4 getSpecular()
{
#ifdef HAS_SPECULAR_MAP
    vec4 spec = texture(specularMap, vary_texcoord2.xy);
    // The map is decoded on the sampler (filtered in linear), so linearise the tint to
    // match. Yields LINEAR spec: the forward path lights with it directly, the deferred
    // writer re-encodes to sRGB for the shared RGBA8 store.
    spec.rgb *= srgb_to_linear(specular_color.rgb);
#else
    vec4 spec = vec4(srgb_to_linear(specular_color.rgb), 1.0);
#endif
    return spec;
}

void alphaMask(float alpha)
{
#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_MASK)
    // Comparing floats cast from 8-bit values, produces acne right at the 8-bit transition points
    float bias = 0.001953125; // 1/512, or half an 8-bit quantization
    if (alpha < minimum_alpha-bias)
    {
        discard;
    }
#endif
}

void waterClip()
{
#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
    waterClip(vary_position.xyz);
#endif
}

float getEmissive(vec4 diffcol)
{
#if (DIFFUSE_ALPHA_MODE != DIFFUSE_ALPHA_MODE_EMISSIVE)
    return emissive_brightness;
#else
    return max(diffcol.a, emissive_brightness);
#endif
}

float getShadow(vec3 pos, vec3 norm)
{
#ifdef HAS_SUN_SHADOW
    #if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
        return sampleDirectionalShadow(pos, norm, vary_texcoord0.xy);
    #else
        return 1;
    #endif
#else
    return 1;
#endif
}

void main()
{
    mirrorClip(vary_position);
    waterClip();

    // diffcol == diffuse map combined with vertex color
    vec4 diffcol = texture(diffuseMap, vary_texcoord0.xy);
    diffcol.rgb *= vertex_color.rgb;
    alphaMask(diffcol.a);

    // spec == specular map combined with specular color
    vec4 spec = getSpecular();
    float env = env_intensity * spec.a;
    float glossiness = specular_color.a;
    vec3 norm = getNormal(glossiness);

    // Widen the lobe by whatever normal detail this pixel lost to minification, as the PBR
    // writers do. A normal map at distance packs many normals into one texel; averaging them
    // leaves the shading with a single direction and the authored lobe width, so a narrow
    // Blinn-Phong highlight snaps between pixels as the camera moves. Worked in the
    // (1 - glossiness) domain, which is what this path already treats as perceptual roughness
    // -- it is the same number that picks the probe mip in sampleReflectionProbesLegacy.
    //
    // Derivatives, so uniform control flow: this sits after the alpha mask, where the PBR
    // writers put theirs.
    glossiness = 1.0 - filterSpecularRoughness(1.0 - glossiness, norm);

    float emissive = getEmissive(diffcol);

#if (DIFFUSE_ALPHA_MODE == DIFFUSE_ALPHA_MODE_BLEND)
    //forward rendering, output lit linear color
    // diffcol arrived linear (decoded on the sampler) and its tint was linearised in the
    // vertex stage. Spec keeps its in-shader decode below -- its map is still sampled
    // encoded, matching the deferred writer.
    spec.a = glossiness; // pack glossiness into spec alpha for lighting functions

    vec3 pos = vary_position;

    float shadow = getShadow(pos, norm);

    vec4 diffuse = diffcol;

    vec3 color = vec3(0,0,0);

    vec3 light_dir = (sun_up_factor == 1) ? sun_dir : moon_dir;

    float bloom = 0.0;
    vec3 sunlit;
    vec3 amblit;
    vec3 additive;
    vec3 atten;
    calcAtmosphericVarsLinear(pos.xyz, norm.xyz, light_dir, sunlit, amblit, additive, atten);
    if (classic_mode > 0)
        sunlit *= 1.35;
    vec3 sunlit_linear = sunlit;
    vec3 amblit_linear = amblit;

    vec3 ambenv = amblit;
    vec3 glossenv = vec3(0.0);
    vec3 legacyenv = vec3(0.0);
    sampleReflectionProbesLegacy(ambenv, glossenv, legacyenv, pos.xy*0.5+0.5, pos.xyz, norm.xyz, glossiness, env, true, amblit_linear);

    color = ambenv;

    float da          = clamp(dot(norm.xyz, light_dir.xyz), 0.0, 1.0);
    if (classic_mode > 0)
    {
        da = pow(da,1.2);
        vec3 sun_contrib = vec3(min(da, shadow));

        color.rgb = srgb_to_linear(color.rgb * 0.9 + linear_to_srgb(sun_contrib) * sunlit_linear * 0.7);
        sunlit_linear = srgb_to_linear(sunlit_linear);
    }
    else
    {
        vec3 sun_contrib = min(da, shadow) * sunlit_linear;
        color.rgb += sun_contrib;
    }

    color *= diffcol.rgb;

    vec3 refnormpersp = reflect(pos.xyz, norm.xyz);

    float glare = 0.0;

    if (glossiness > 0.0)
    {
        vec3  lv = light_dir.xyz;
        vec3  h, l, v = -normalize(pos.xyz);
        float nh, nl, nv, vh, lightDist;
        vec3 n = norm.xyz;
        calcHalfVectors(lv, n, v, h, l, nh, nl, nv, vh, lightDist);

        if (nl > 0.0 && nh > 0.0)
        {
            float lit = min(nl*6.0, 1.0);

            float sa = nh;
            float fres = pow(1 - vh, 5) * 0.4+0.5;
            float gtdenom = 2 * nh;
            float gt = max(0,(min(gtdenom * nv / vh, gtdenom * nl / vh)));

            float scol = shadow*fres*blinnPhongLobe(nh, glossiness)*gt/(nh*nl);
            color.rgb += lit*scol*sunlit_linear.rgb*spec.rgb;
        }

        // add radiance map
        applyGlossEnv(color, glossenv, spec, pos.xyz, norm.xyz);
    }

    color = mix(color.rgb, diffcol.rgb, emissive);

    if (env > 0.0)
    {  // add environmentmap
        applyLegacyEnv(color, legacyenv, spec, pos.xyz, norm.xyz, env);

        float cur_glare = max(max(legacyenv.r, legacyenv.g), legacyenv.b);
        cur_glare = clamp(cur_glare, 0, 1);
        cur_glare *= env;
        glare += cur_glare;
    }

    vec3 npos = normalize(-pos.xyz);
    vec3 light = vec3(0, 0, 0);

// light_deferred_attenuation carries the size/falloff pair the deferred pass reads, as the PBR
// alpha path already takes them. light_attenuation.z stays: it is the is-omni flag.
#define LIGHT_LOOP(i) light.rgb += calcLegacyPointLightOrSpotLight(diffuse.rgb, spec, norm.xyz, pos.xyz, npos, light_position[i].xyz, light_direction[i].xyz, light_diffuse[i].rgb, light_deferred_attenuation[i].x, light_deferred_attenuation[i].y, light_attenuation[i].z, glare);

    LIGHT_LOOP(1)
        LIGHT_LOOP(2)
        LIGHT_LOOP(3)
        LIGHT_LOOP(4)
        LIGHT_LOOP(5)
        LIGHT_LOOP(6)
        LIGHT_LOOP(7)

    color += light;

    color.rgb = applySkyAndWaterFog(pos.xyz, additive, atten, vec4(color, 1.0)).rgb;

    glare *= 1.0-emissive;
    glare = min(glare, 1.0);
    float al = max(diffcol.a, glare) * vertex_color.a;
    float final_scale = 1;
    if (classic_mode > 0)
        final_scale = 1.1;
    frag_color = max(vec4(color * final_scale, al), vec4(0));

#else // mode is not DIFFUSE_ALPHA_MODE_BLEND, encode to gbuffer
    // deferred path               // See: C++: addDeferredAttachment(), shader: softenLightF.glsl

    float flag = GBUFFER_FLAG_HAS_ATMOS;

    frag_data[0] = max(vec4(diffcol.rgb, emissive), vec4(0));        // gbuffer is sRGB for legacy materials
    // frag_data[1] is a plain RGBA8 shared with PBR's linear ORM, so it cannot be sRGB
    // storage. The spec was FILTERED in linear (sampler decode); re-encode it here so it
    // lands sRGB the way softenLight reads it. Storage constraint, not a filtering one.
    frag_data[1] = max(vec4(linear_to_srgb(spec.rgb), glossiness), vec4(0));  // XYZ = Specular color. W = Specular exponent.
    frag_data[2] = encodeNormal(norm, env, flag);   // XY = Normal.  Z = Env. intensity. W = 1 skip atmos (mask off fog)

#if defined(HAS_EMISSIVE)
    frag_data[3] = vec4(0, 0, 0, 0);
#endif

#endif
}


