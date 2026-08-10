/**
 * @file class1/deferred/deferredUtil.glsl
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


/*  Parts of this file are taken from Sascha Willem's Vulkan GLTF refernce implementation
MIT License

Copyright (c) 2018 Sascha Willems

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

uniform sampler2D normalMap;
uniform sampler2D depthMap;
uniform sampler2D projectionMap; // rgba
uniform sampler2D brdfLut;

// projected lighted params
uniform mat4 proj_mat; //screen space to light space projector
uniform vec3 proj_n; // projector normal
uniform vec3 proj_p; //plane projection is emitting from (in screen space)
uniform float proj_focus; // distance from plane to begin blurring
uniform float proj_lod  ; // (number of mips in proj map)
uniform float proj_range; // range between near clip and far clip plane of projection
uniform float proj_ambiance;

// Classic (legacy pre-PBR) sky lighting is a per-program compile-time variant, not a runtime
// uniform: the two paths differ by whole blocks of maths and a probe sample, and only one of
// them is ever live for a given sky. A macro rather than a const global -- these sources are
// separately compiled units linked into one program, and several of them declare this.
#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif

// light params
uniform vec3 color; // light_color
uniform float size; // light_size

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]
uniform vec2 screen_res;

const float M_PI = 3.14159265;
const float ONE_OVER_PI = 0.3183098861;

// A floor on perceptual roughness. This is what bounds the specular peak of a punctual light,
// which is otherwise unbounded -- a delta light has no solid angle, and D goes as
// 1/(pi*alpha^2), so at alpha -> 0 a single pixel takes an arbitrarily large value and flickers
// as the camera moves.
//
// It cannot be lowered on the strength of the specular antialiasing in globalF.glsl. That
// measures normal VARIANCE, so it widens roughness exactly where a minified normal map lost
// detail and not at all on a flat polished surface -- which is the case this floor exists for.
// The two cover different surfaces; neither substitutes for the other.
//
// The sun is the exception and does not need it: sunDiscRoughness widens by the solar disc's
// real angular size, which is a physical bound rather than an invented one.
const float MIN_PBR_ROUGHNESS = 0.045;

// PUNCTUAL_LIGHT_SCALE and MAX_PUNCTUAL_RADIANCE are injected by llshadermgr.cpp alongside the
// GBuffer flags. Both have to hold across separately compiled objects, which a global const
// cannot do.

vec3 srgb_to_linear(vec3 cs);
vec3 linear_to_srgb(vec3 cs);
vec3 atmosFragLightingLinear(vec3 light, vec3 additive, vec3 atten);

vec4 decodeNormal(vec4 norm);
vec3 unpackGeoNormal(float v);

vec3 clampHDRRange(vec3 color)
{
    // Why do this?
    // There are situations where the color range will go to something insane - potentially producing infs and NaNs even.
    // This is a safety measure to prevent that.
    // -Geenz 2025-03-05
    // Infinity becomes the largest value the frame can carry, not 1.0. Substituting 1.0 turns
    // the brightest pixel in a linear frame into mid grey, so an overflow reads as a dark hole
    // exactly where something should be searing -- a failure that inverts the error rather than
    // bounding it. NaN still goes to black, which is the only safe answer for a value that
    // carries no magnitude at all.
    color = mix(color, vec3(MAX_PUNCTUAL_RADIANCE), isinf(color));
    color = mix(color, vec3(0.0), isnan(color));
    return color;
}

float calcLegacyDistanceAttenuation(float distance, float falloff)
{
    float dist_atten = 1.0 - clamp((distance + falloff)/(1.0 + falloff), 0.0, 1.0);
    dist_atten *= dist_atten;

    // Tweak falloff slightly to match pre-EEP attenuation
    // NOTE: this magic number also shows up in a great many other places, search for dist_atten *= to audit
    dist_atten *= 2.0;
    return dist_atten;
}

// The normalized Blinn-Phong specular lobe for a legacy glossiness.
//
// SPECULAR_EXPONENT is injected alongside the GBuffer flags, from RenderSpecularExponent. The
// square is this tree's mapping from an 8-bit material field to an exponent, and it is what
// makes the top of the glossiness range spend most of its resolution on very tight lobes.
//
// The normalization keeps the lobe's integral independent of the exponent, so polishing a
// surface concentrates its highlight rather than adding energy to it. Its 2^(-n/2) term only
// does anything as n approaches zero, where it holds the denominator off zero; by the exponents
// a glossy surface uses it is below 1e-6 and this is (n+2)(n+4) / 8*pi*n.
//
// Every caller takes nh from calcHalfVectors, which floors it above zero -- pow() is undefined
// at a zero base with a zero exponent, which is reachable here only through a glossiness of
// zero, and every call site already gates on that being positive.
float blinnPhongLobe(float nh, float glossiness)
{
    float n = glossiness * glossiness * SPECULAR_EXPONENT;
    float norm = ((n + 2.0) * (n + 4.0)) / (8.0 * M_PI * (exp2(-n * 0.5) + n));

    return pow(nh, n) * norm;
}

// In:
//   lv  unnormalized surface to light vector
//   n   normal of the surface
//   pos unnormalized camera to surface vector
// Out:
//   l   normalized surace to light vector
//   nl  diffuse angle
//   nh  specular angle
void calcHalfVectors(vec3 lv, vec3 n, vec3 v,
    out vec3 h, out vec3 l, out float nh, out float nl, out float nv, out float vh, out float lightDist)
{
    l  = normalize(lv);
    h  = normalize(l + v);

    // lower bound to avoid divide by zero
    float eps = 0.000001;
    nh = clamp(dot(n, h), eps, 1.0);
    nl = clamp(dot(n, l), eps, 1.0);
    nv = clamp(dot(n, v), eps, 1.0);
    vh = clamp(dot(v, h), eps, 1.0);

    lightDist = length(lv);
}

// In:
//   light_center
//   pos
// Out:
//   dist
//   l_dist
//   lv
//   proj_tc  Projector Textue Coordinates
bool clipProjectedLightVars(vec3 light_center, vec3 pos, out float dist, out float l_dist, out vec3 lv, out vec4 proj_tc )
{
    lv = light_center - pos.xyz;
    dist = length(lv);
    bool clipped = (dist >= size);
    if ( !clipped )
    {
        dist /= size;

        l_dist = -dot(lv, proj_n);
        vec4 projected_point = (proj_mat * vec4(pos.xyz, 1.0));
        clipped = (projected_point.z < 0.0);
        projected_point.xyz /= projected_point.w;
        proj_tc = projected_point;
    }

    return clipped;
}

vec2 getScreenCoordinate(vec2 screenpos)
{
    vec2 sc = screenpos.xy * 2.0;
    return sc - vec2(1.0, 1.0);
}

vec4 getNorm(vec2 screenpos)
{
    vec4 norm = decodeNormal(texture(normalMap, screenpos.xy));
    return norm;
}

vec4 getNormRaw(vec2 screenpos)
{
    vec4 norm = texture(normalMap, screenpos.xy);
    return norm;
}

// The un-perturbed surface normal, out of the blue channel the normal attachment shares between
// environment intensity and a packed geometric normal. A legacy fragment stored the former and
// has no geometric normal to give, so it gets the shading normal back and any test against it
// degenerates to a no-op rather than needing a guard at every call site.
vec3 getGeoNorm(vec2 screenpos)
{
    vec4 raw = getNormRaw(screenpos);
    return GET_GBUFFER_FLAG(raw.w, GBUFFER_FLAG_HAS_PBR) ? unpackGeoNormal(raw.b)
                                                         : decodeNormal(raw).xyz;
}

// Convert a stored screen-depth sample to its NDC z under the active depth convention.
// Under reverse-Z the C++ side sets glClipControl(GL_ZERO_TO_ONE) and feeds reversed
// zero-to-one projections, so the buffer already holds ndc z; the legacy convention needs
// the [0,1]->[-1,1] remap. Callers that also attach aoUtil.glsl must not redefine this.
float ndcZFromScreenDepth(float d)
{
#ifdef REVERSE_Z
    return d;
#else
    return 2.0 * d - 1.0;
#endif
}

// True when a stored depth sample lies on the far plane (an unwritten sky pixel). The far
// plane is 0 under reverse-Z and 1 otherwise.
bool isFarDepth(float d)
{
#ifdef REVERSE_Z
    return d <= 0.0;
#else
    return d >= 1.0;
#endif
}

// get linear depth value given a depth buffer sample d and znear and zfar values
float linearDepth(float d, float znear, float zfar)
{
#ifdef REVERSE_Z
    // Reversed zero-to-one: d=1 -> znear, d=0 -> zfar. No cancellation (all-positive denom).
    return znear * zfar / (znear + d * (zfar - znear));
#else
    d = d * 2.0 - 1.0;
    return znear * 2.0 * zfar / (zfar + znear - d * (zfar - znear));
#endif
}

float linearDepth01(float d, float znear, float zfar)
{
    return linearDepth(d, znear, zfar) / zfar;
}

float getDepth(vec2 pos_screen)
{
    float depth = texture(depthMap, pos_screen).r;
    return depth;
}

vec4 getTexture2DLodAmbient(vec2 tc, float lod)
{
#ifndef FXAA_GLSL_120
    vec4 ret = textureLod(projectionMap, tc, lod);
#else
    vec4 ret = texture(projectionMap, tc);
#endif
    // projectionMap is decoded on the sampler (SRGBDecode, see setupSpotLight), so the
    // cookie is filtered in linear -- the mipped LOD read included. Already linear here.

    vec2 dist = tc-vec2(0.5);
    float d = dot(dist,dist);
    ret *= min(clamp((0.25-d)/0.25, 0.0, 1.0), 1.0);

    return ret;
}

vec4 getTexture2DLodDiffuse(vec2 tc, float lod)
{
#ifndef FXAA_GLSL_120
    vec4 ret = textureLod(projectionMap, tc, lod);
#else
    vec4 ret = texture(projectionMap, tc);
#endif
    // projectionMap is decoded on the sampler (SRGBDecode, see setupSpotLight), so the
    // cookie is filtered in linear -- the mipped LOD read included. Already linear here.

    vec2 dist = vec2(0.5) - abs(tc-vec2(0.5));
    float det = min(lod/(proj_lod*0.5), 1.0);
    float d = min(dist.x, dist.y);
    float edge = 0.25*det;
    ret *= clamp(d/edge, 0.0, 1.0);

    return ret;
}

// lit     This is set by the caller: if (nl > 0.0) { lit = attenuation * nl * noise; }
// Uses:
//   color   Projected spotlight color
vec3 getProjectedLightAmbiance(float amb_da, float attenuation, float lit, float nl, float noise, vec2 projected_uv)
{
    vec4 amb_plcol = getTexture2DLodAmbient(projected_uv, proj_lod);
    vec3 amb_rgb   = amb_plcol.rgb * amb_plcol.a;

    amb_da += proj_ambiance;
    amb_da += (nl*nl*0.5+0.5) * proj_ambiance;
    amb_da *= attenuation * noise;
    amb_da = min(amb_da, 1.0-lit);

    return (amb_da * color.rgb * amb_rgb);
}

// Returns projected light in Linear
// Uses global spotlight color:
//  color
// NOTE: projected.a will be pre-multiplied with projected.rgb
vec3 getProjectedLightDiffuseColor(float light_distance, vec2 projected_uv)
{
    float diff = clamp((light_distance - proj_focus)/proj_range, 0.0, 1.0);
    float lod = diff * proj_lod;
    vec4 plcol = getTexture2DLodDiffuse(projected_uv.xy, lod);

    return color.rgb * plcol.rgb * plcol.a;
}

vec4 texture2DLodSpecular(vec2 tc, float lod)
{
#ifndef FXAA_GLSL_120
    vec4 ret = textureLod(projectionMap, tc, lod);
#else
    vec4 ret = texture(projectionMap, tc);
#endif
    // projectionMap is decoded on the sampler (SRGBDecode, see setupSpotLight), so the
    // cookie is filtered in linear -- the mipped LOD read included. Already linear here.

    vec2 dist = vec2(0.5) - abs(tc-vec2(0.5));
    float det = min(lod/(proj_lod*0.5), 1.0);
    float d = min(dist.x, dist.y);
    d *= min(1, d * (proj_lod - lod)); // BUG? extra factor compared to diffuse causes N repeats
    float edge = 0.25*det;
    ret *= clamp(d/edge, 0.0, 1.0);

    return ret;
}

// See: clipProjectedLightVars()
vec3 getProjectedLightSpecularColor(vec3 pos, vec3 n )
{
    vec3 slit = vec3(0);
    vec3 ref = reflect(normalize(pos), n);

    //project from point pos in direction ref to plane proj_p, proj_n
    vec3 pdelta = proj_p-pos;
    float l_dist = length(pdelta);
    float ds = dot(ref, proj_n);
    if (ds < 0.0)
    {
        vec3 pfinal = pos + ref * dot(pdelta, proj_n)/ds;
        vec4 stc = (proj_mat * vec4(pfinal.xyz, 1.0));
        if (stc.z > 0.0)
        {
            stc /= stc.w;
            slit = getProjectedLightDiffuseColor( l_dist, stc.xy ); // NOTE: Using diffuse due to texture2DLodSpecular() has extra: d *= min(1, d * (proj_lod - lod));
        }
    }
    return slit; // specular light
}

vec3 getProjectedLightSpecularColor(float light_distance, vec2 projected_uv)
{
    float diff = clamp((light_distance - proj_focus)/proj_range, 0.0, 1.0);
    float lod = diff * proj_lod;
    vec4 plcol = getTexture2DLodDiffuse(projected_uv.xy, lod); // NOTE: Using diffuse due to texture2DLodSpecular() has extra: d *= min(1, d * (proj_lod - lod));

    return color.rgb * plcol.rgb * plcol.a;
}

vec4 getPosition(vec2 pos_screen)
{
    float depth = getDepth(pos_screen);
    vec2 sc = getScreenCoordinate(pos_screen);
    vec4 ndc = vec4(sc.x, sc.y, ndcZFromScreenDepth(depth), 1.0);
    vec4 pos = inv_proj * ndc;
    pos /= pos.w;
    pos.w = 1.0;
    return pos;
}

// get position given a normalized device coordinate
vec3 getPositionWithNDC(vec3 ndc)
{
    vec4 pos = inv_proj * vec4(ndc, 1.0);
    return pos.xyz / pos.w;
}

vec4 getPositionWithDepth(vec2 pos_screen, float depth)
{
    vec2 sc = getScreenCoordinate(pos_screen);
    vec3 ndc = vec3(sc.x, sc.y, ndcZFromScreenDepth(depth));
    return vec4(getPositionWithNDC(ndc), 1.0);
}

vec2 getScreenCoord(vec4 clip)
{
    vec4 ndc = clip;
         ndc.xyz /= clip.w;
    vec2 screen = vec2( ndc.xy * 0.5 );
         screen += 0.5;
    return screen;
}

vec2 getScreenXY(vec4 clip)
{
    vec4 ndc = clip;
         ndc.xyz /= clip.w;
    vec2 screen = vec2( ndc.xy * 0.5 );
         screen += 0.5;
         screen *= screen_res;
    return screen;
}

// Color utils

vec3 colorize_dot(float x)
{
    if (x > 0.0) return vec3( 0, x, 0 );
    if (x < 0.0) return vec3(-x, 0, 0 );
                 return vec3( 0, 0, 1 );
}

vec3 hue_to_rgb(float hue)
{
    if (hue > 1.0) return vec3(0.5);
    vec3 rgb = abs(hue * 6. - vec3(3, 2, 4)) * vec3(1, -1, -1) + vec3(-1, 2, 2);
    return clamp(rgb, 0.0, 1.0);
}

// PBR Utils

vec2 BRDF(float NoV, float roughness)
{
    return texture(brdfLut, vec2(NoV, roughness)).rg;
}

// Lagarde and de Rousiers 2014, "Moving Frostbite to PBR"
float computeSpecularAO(float NoV, float ao, float roughness)
{
    return clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
}

// Ambient occlusion as a colour rather than a scalar (Jimenez et al. 2016, "Practical Real-Time
// Strategies for Accurate Indirect Occlusion"; this is the polynomial fit Filament ships).
//
// Multiplying irradiance by a single occlusion figure models light that entered the cavity and
// never came back out. Real light entering a cavity bounces, and every bounce is a multiply by
// the surface's own albedo, so what leaves is tinted and there is more of it than one multiply
// allows. The scalar form therefore drives a saturated surface toward neutral black in its own
// creases -- red cloth goes grey in the folds instead of a deeper red -- and the error is worst
// where the occlusion is strongest, which is where the eye is looking.
//
// Per channel, so a dark channel stays dark and a bright one recovers. Never darker than the
// scalar it replaces.
vec3 gtaoMultiBounce(float visibility, vec3 albedo)
{
    vec3 a =  2.0404 * albedo - 0.3324;
    vec3 b = -4.7951 * albedo + 0.6417;
    vec3 c =  2.7552 * albedo + 0.6903;

    return max(vec3(visibility), ((visibility * a + b) * visibility + c) * visibility);
}

// Clamp radiance to a ceiling without turning it a different colour on the way.
//
// A per-channel clamp lets one channel saturate while the others keep climbing, so a highlight
// pushed past the ceiling drifts toward whichever primary saturated first -- a bright warm
// specular goes orange, then red. Scaling by the largest channel holds the ratios, so the
// colour is preserved exactly and only the magnitude gives.
vec3 clampRadiance(vec3 c)
{
    c = max(c, vec3(0));
    float peak = max(max(c.r, c.g), c.b);
    return peak > MAX_PUNCTUAL_RADIANCE ? c * (MAX_PUNCTUAL_RADIANCE / peak) : c;
}

// Multiple-scattering compensation for the punctual specular lobe (Turquin 2019, "Practical
// multiple scattering compensation for microfacet models").
//
// Single-scatter GGX returns only the light that leaves the microsurface after one bounce, and
// the rest is dropped rather than redistributed. The loss grows with roughness and with F0, so
// it is worst on rough metal -- the same deficit pbrIbl compensates for, and it has to be
// compensated on this side too or a rough metal brightens correctly under a probe and stays
// dark under a lamp.
//
// brdf.x + brdf.y is the LUT's directional albedo at F0 = 1, which is exactly the single-
// scatter energy the lobe did return. It reaches 1 at roughness 0, so a mirror gets a
// compensation of 1 and nothing changes there.
//
// Constant across the lights hitting a fragment, so callers evaluate it once and apply it to
// the accumulated specular rather than paying a LUT fetch per light.
vec3 pbrEnergyCompensation(vec3 specularColor, float perceptualRoughness, float nv)
{
    perceptualRoughness = max(perceptualRoughness, MIN_PBR_ROUGHNESS);
    vec2 brdf = BRDF(clamp(nv, 0.0, 1.0), 1.0 - perceptualRoughness);
    float Ess = max(brdf.x + brdf.y, 1e-4);
    return 1.0 + specularColor * (1.0 / Ess - 1.0);
}

// Bend a reflection lookup from the mirror direction toward the normal as the surface
// roughens (Lagarde and de Rousiers 2014, "Moving Frostbite to PBR", listing 21).
//
// The mirror direction is where a perfectly smooth surface reflects from, but a GGX lobe on a
// rough surface is not centred there -- it leans toward the normal, and increasingly so with
// roughness. Sampling the prefiltered probe along the mirror direction therefore fetches from
// slightly the wrong place on exactly the surfaces whose lobe is widest, which reads as
// reflections sliding across curvature.
vec3 getSpecularDominantDir(vec3 n, vec3 r, float perceptualRoughness)
{
    float smoothness = 1.0 - perceptualRoughness;
    float lerpFactor = smoothness * (sqrt(smoothness) + perceptualRoughness);
    return normalize(mix(n, r, lerpFactor));
}

// Attenuate a reflection that a normal map has aimed into the surface it sits on.
//
// Perturbing the shading normal can point the reflection vector below the geometric horizon,
// where the probe still returns whatever radiance lies that way -- light arriving through the
// ground the surface is resting on. Filament's form: fade quadratically as the reflection
// approaches the geometric plane, and cut it entirely once past.
float horizonOcclusion(vec3 r, vec3 geometricNormal)
{
    // Both normalized here rather than trusted from the caller. The term is a dot product read
    // as a cosine, so an unnormalized reflection vector scales it by that vector's length -- and
    // the natural thing to hand this is an eye-space reflect(), whose length is the distance to
    // the camera. That turns a fixed geometric relationship into a distance-dependent one.
    vec3 rn = normalize(r);
    vec3 gn = normalize(geometricNormal);

    // Clamped low as well as high, before the square. Squaring is only a falloff shape while the
    // value is in [0,1]; below zero it turns the sign around and grows, so a reflection further
    // under the horizon would brighten rather than darken -- the failure is silent, and it is
    // worst exactly where the term was supposed to bite hardest.
    float horizon = clamp(1.0 + dot(rn, gn), 0.0, 1.0);
    return horizon * horizon;
}

// Widen a specular lobe to account for the sun being a disc rather than a point (Karis 2013,
// "Real Shading in Unreal Engine 4" -- the sphere-light normalization).
//
// A delta light gives a smooth surface an infinitely small highlight, which is why one has to
// be floored into existence by MIN_PBR_ROUGHNESS. The sun subtends about half a degree, so its
// highlight has a real angular size; giving it one lets the roughness floor stay low without
// the sun's own reflection aliasing.
float sunDiscRoughness(float perceptualRoughness)
{
    // Angular radius of the solar disc in radians -- about 0.27 degrees.
    const float SUN_ANGULAR_RADIUS = 0.00465;
    float alpha = perceptualRoughness * perceptualRoughness;
    return sqrt(clamp(alpha + SUN_ANGULAR_RADIUS, 0.0, 1.0));
}

// set colorDiffuse and colorSpec to the results of GLTF PBR style IBL
void pbrIbl(vec3 diffuseColor,
            vec3 specularColor,
            vec3 radiance, // radiance map sample
            vec3 irradiance, // irradiance map sample
            float ao,       // ambient visibility: the material's own occlusion combined with
                            // whatever screen-space term the caller has, already on that
                            // caller's strength curve. One number, spent once per lobe.
            float nv,       // normal dot view vector
            float perceptualRough,
            out vec3 diffuseOut,
            out vec3 specularOut)
{
    nv = clamp(nv, 0.0, 1.0);

    // retrieve a scale and bias to F0. See [1], Figure 3
    vec2 brdf = BRDF(nv, 1.0-perceptualRough);

    // Multiple-scattering IBL, Fdez-Aguera 2019, "A Multiple-Scattering Microfacet Model for
    // Real-Time Image-Based Lighting".
    //
    // The split-sum approximation integrates one bounce off the microsurface and discards
    // everything that leaves after a second or later one. That loss grows with roughness and
    // with F0, so it is worst exactly where it is most visible: rough metal, which reads far
    // too dark under a bright probe. FmsEms is the missing energy returned.
    //
    // It also settles how the two lobes divide the incoming light. k_D is what the specular
    // lobe did not take, derived per-pixel from the surface's own directional albedo, which
    // is why calcDiffuseSpecular hands the full base colour to the diffuse term instead of
    // pre-splitting it by a constant.
    vec3 Fr = max(vec3(1.0 - perceptualRough), specularColor) - specularColor;
    vec3 k_S = specularColor + Fr * pow(1.0 - nv, 5.0);
    vec3 FssEss = k_S * brdf.x + brdf.y;

    float Ems = 1.0 - (brdf.x + brdf.y);
    vec3 F_avg = specularColor + (1.0 - specularColor) / 21.0;
    // The denominator is bounded away from zero for any real LUT sample; the guard costs
    // nothing and keeps a degenerate one (a white metal against a fully absorbing tap) from
    // reaching the rest of the frame as an inf.
    vec3 FmsEms = Ems * FssEss * F_avg / max(vec3(1.0) - F_avg * Ems, vec3(1e-4));
    vec3 k_D = diffuseColor * (1.0 - FssEss + FmsEms);

    // Both lobes off the one visibility.
    //
    // The screen-space term used to be multiplied into irradiance before this function was
    // called, which made it a different kind of quantity from the material's own occlusion
    // instead of the same measurement at a different scale. Three things followed from that.
    // It was bounded in absolute radiometric units, so how hard a corner darkened depended on
    // how bright the sky was rather than on the geometry. It went through a saturation matrix,
    // so occluded ambient lost its hue -- chroma surgery on the term that carries all of the
    // ambient colour. And it never met the multi-bounce fit, which is the correction it needed
    // most, being the larger of the two and the one that makes creases look dirty.
    //
    // Whatever the caller combines is what both lobes get. The specular side had no
    // screen-space occlusion at all before, so a corner darkened diffusely still returned a
    // full-strength reflection of the sky.
    diffuseOut = (FmsEms + k_D) * irradiance * gtaoMultiBounce(ao, diffuseColor);
    specularOut = radiance * FssEss * computeSpecularAO(nv, ao, perceptualRough * perceptualRough);
}


// Encapsulate the various inputs used by the various functions in the shading equation
// We store values in this struct to simplify the integration of alternative implementations
// of the shading terms, outlined in the Readme.MD Appendix.
struct PBRInfo
{
    float NdotL;                  // cos angle between normal and light direction
    float NdotV;                  // cos angle between normal and view direction
    float NdotH;                  // cos angle between normal and half vector
    float LdotH;                  // cos angle between light direction and half vector
    float VdotH;                  // cos angle between view direction and half vector
    float perceptualRoughness;    // roughness value, as authored by the model creator (input to shader)
    float metalness;              // metallic value at the surface
    vec3 reflectance0;            // full reflectance color (normal incidence angle)
    vec3 reflectance90;           // reflectance color at grazing angle
    float alphaRoughness;         // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3 diffuseColor;            // color contribution from diffuse lighting
    vec3 specularColor;           // color contribution from specular lighting
};

// Basic Lambertian diffuse
// Implementation from Lambert's Photometria https://archive.org/details/lambertsphotome00lambgoog
// See also [1], Equation 1
vec3 diffuse(PBRInfo pbrInputs)
{
    return pbrInputs.diffuseColor / M_PI;
}

// The following equation models the Fresnel reflectance term of the spec equation (aka F())
// Implementation of fresnel from [4], Equation 15
vec3 specularReflection(PBRInfo pbrInputs)
{
    return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}

// Specular visibility (aka Vis), the geometric attenuation G with the microfacet BRDF's
// 1/(4 NdotL NdotV) denominator folded in. Height-correlated Smith, per Heitz 2014
// "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"; this is the form
// glTF 2.0 Appendix B specifies.
//
// Correlating the two directions matters: a separable Smith treats masking and shadowing as
// independent events, which double-counts occlusion and over-darkens grazing angles on rough
// surfaces. Folding the denominator in also removes a division by a NdotL*NdotV product that
// approaches zero at exactly those angles.
float visibilityOcclusion(PBRInfo pbrInputs)
{
    float NdotL = pbrInputs.NdotL;
    float NdotV = pbrInputs.NdotV;
    float a2 = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;

    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);

    float v = lambdaV + lambdaL;
    return v > 0.0 ? 0.5 / v : 0.0;
}

// The following equation(s) model the distribution of microfacet normals across the area being drawn (aka D())
// Implementation from "Average Irregularity Representation of a Roughened Surface for Ray Reflection" by T. S. Trowbridge, and K. P. Reitz
// Follows the distribution function recommended in the SIGGRAPH 2013 course notes from EPIC Games [1], Equation 3.
float microfacetDistribution(PBRInfo pbrInputs)
{
    float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
    float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
    return roughnessSq / (M_PI * f * f);
}

void pbrPunctual(vec3 diffuseColor, vec3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    vec3 n, // normal
                    vec3 v, // surface point to camera
                    vec3 l,
                    out float nl,
                    out vec3 diff,
                    out vec3 spec) //surface point to light
{
    // make sure specular highlights from punctual lights don't fall off of polished surfaces
    perceptualRoughness = max(perceptualRoughness, MIN_PBR_ROUGHNESS);

    float alphaRoughness = perceptualRoughness * perceptualRoughness;

    // Grazing reflectance is 1 for every material, per glTF 2.0 Appendix B: at 90 degrees a
    // smooth surface reflects all of it regardless of what F0 says about normal incidence.
    // Deriving f90 from F0 instead only diverges where F0 is below 4%, which for a
    // metallic-roughness material means a metal with a near-black base colour -- and dimming
    // its rim is the opposite of what the physics gives.
    vec3 specularEnvironmentR0 = specularColor.rgb;
    vec3 specularEnvironmentR90 = vec3(1.0);

    vec3 h = normalize(l+v);                        // Half vector between both l and v

    // Zero, not an epsilon: a surface turned away from the light receives nothing. The floor
    // this replaces existed to keep NdotL out of a denominator, and visibilityOcclusion no
    // longer has one -- with NdotL at zero its own denominator is carried by the NdotV term,
    // which the guard there covers.
    float NdotL = clamp(dot(n, l), 0.0, 1.0);
    float NdotV = clamp(abs(dot(n, v)), 0.001, 1.0);
    float NdotH = clamp(dot(n, h), 0.0, 1.0);
    float LdotH = clamp(dot(l, h), 0.0, 1.0);
    float VdotH = clamp(dot(v, h), 0.0, 1.0);

    PBRInfo pbrInputs = PBRInfo(
        NdotL,
        NdotV,
        NdotH,
        LdotH,
        VdotH,
        perceptualRoughness,
        metallic,
        specularEnvironmentR0,
        specularEnvironmentR90,
        alphaRoughness,
        diffuseColor,
        specularColor
    );

    // Calculate the shading terms for the microfacet specular shading model
    vec3 F = specularReflection(pbrInputs);
    float Vis = visibilityOcclusion(pbrInputs);
    float D = microfacetDistribution(pbrInputs);

    // Calculation of analytical lighting contribution
    vec3 diffuseContrib = (1.0 - F) * diffuse(pbrInputs);
    vec3 specContrib = F * Vis * D;

    nl = NdotL;

    diff = diffuseContrib;
    spec = specContrib;
}

vec3 pbrCalcPointLightOrSpotLight(vec3 diffuseColor, vec3 specularColor,
                    float perceptualRoughness,
                    float metallic,
                    vec3 n, // normal
                    vec3 p, // pixel position
                    vec3 v, // view vector (negative normalized pixel position)
                    vec3 lp, // light position
                    vec3 ld, // light direction (for spotlights)
                    vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight, float ambiance)
{
    vec3 color = vec3(0,0,0);

    vec3 lv = lp.xyz - p;

    float lightDist = length(lv);

    float dist = lightDist / lightSize;
    if (dist <= 1.0)
    {
        lv /= lightDist;

        float dist_atten = calcLegacyDistanceAttenuation(dist, falloff);

        // spotlight coefficient.
        float spot = max(dot(-ld, lv), is_pointlight);
        // spot*spot => GL_SPOT_EXPONENT=2
        float spot_atten = spot*spot;

        vec3 intensity = spot_atten * dist_atten * lightColor * PUNCTUAL_LIGHT_SCALE;

        float nl = 0;
        vec3 diffPunc = vec3(0);
        vec3 specPunc = vec3(0);

        pbrPunctual(diffuseColor, specularColor, perceptualRoughness, metallic, n.xyz, v, lv, nl, diffPunc, specPunc);
        specPunc *= pbrEnergyCompensation(specularColor, perceptualRoughness, dot(n.xyz, v));
        color = intensity * clampRadiance(nl * (diffPunc + specPunc));
    }
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;
    return color * final_scale;
}

// The legacy (Blinn-Phong) twin of pbrCalcPointLightOrSpotLight: one punctual light against a
// legacy surface, in the form the forward passes need to light alpha in place. Two entry points
// over one model -- a diffuse-only one for the surfaces that carry no specular data, and the
// full one.
//
// Term for term the deferred model -- see the legacy branch of class3/deferred/pointLightF.glsl.
// The forward passes each carried a copy of it, and both copies had drifted: they recovered the
// light's radius by inverting the linear attenuation, ran an inlined distance falloff, fed the
// geometry term and its own denominator the attenuated angular value rather than NdotL, took the
// dot products without the epsilon floors those denominators need, and capped the lit factor at
// 1 where the deferred path lets it reach 2. A blended surface was lit differently from the
// opaque one behind it, and most differently where a local light was closest.
//
// lightSize and falloff are the deferred pair (light_deferred_attenuation), which LLRender fills
// from the same mSize/mFalloff the deferred pass reads. The reconstruction they replace was
// algebraically exact, so the distance term is unchanged by routing through it.
//
// The spot cone is the one term with no deferred counterpart: deferred spot lights are their own
// pass with a projector texture that a forward pass cannot reach. Applied to the result, the way
// pbrCalcPointLightOrSpotLight applies it, rather than folded into the angular term -- there it
// would also land in the specular geometry term and its denominator, and partly cancel itself.
//
// glare is the forward paths' glow accumulator. It drives an alpha channel rather than a colour,
// so it keeps the [0,1] scale it has always been on while the colour takes the deferred path's
// radiance bound.

// The geometry and attenuations both entry points work from, so the model lives in one place
// while only one of them takes the specular LUT. A sampler passed to a shader that never reads
// it still costs that program a texture channel and renumbers the ones after it, and whether it
// did would come down to the driver folding away a branch on a constant argument -- which makes
// the channel layout, and anything downstream that reads a unit by number, driver-dependent.
struct LegacyPunctualInfo
{
    float nh;
    float nl;
    float nv;
    float vh;
    float dist_atten;
    float spot_atten;
};

bool calcLegacyPunctual(vec3 n, vec3 p, vec3 v, vec3 lp, vec3 ld,
                    float lightSize, float falloff, float is_pointlight,
                    out LegacyPunctualInfo lt)
{
    lt = LegacyPunctualInfo(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);

    vec3 lv = lp - p;
    float dist = length(lv) / lightSize;

    // The facing test is against the raw dot product, before calcHalfVectors floors it. That
    // floor exists so the Blinn-Phong terms can divide by it, which would leave a test made
    // after the call constant and let back faces through the whole light.
    if (dist > 1.0 || dot(n, lv) <= 0.0)
    {
        return false;
    }

    vec3 h, l;
    float lightDist;
    calcHalfVectors(lv, n, v, h, l, lt.nh, lt.nl, lt.nv, lt.vh, lightDist);

    lt.dist_atten = calcLegacyDistanceAttenuation(dist, falloff);

    // spot*spot => GL_SPOT_EXPONENT=2
    float spot = max(dot(-ld, l), is_pointlight);
    lt.spot_atten = spot * spot;

    return true;
}

// The cone weight over both lobes once they are summed, then the deferred path's bound.
vec3 finishLegacyPunctual(vec3 col, float spot_atten)
{
    float final_scale = 1.0;
    if (classic_mode > 0)
        final_scale = 0.9;

    return max(clampRadiance(col * spot_atten) * final_scale, vec3(0));
}

// Diffuse only, for the legacy surfaces that carry no specular data at all.
vec3 calcLegacyPointLightDiffuse(vec3 diffuse,
                    vec3 n,  // normal
                    vec3 p,  // pixel position
                    vec3 v,  // view vector (negative normalized pixel position)
                    vec3 lp, // light position
                    vec3 ld, // light direction (for spotlights)
                    vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight)
{
    LegacyPunctualInfo lt;
    if (!calcLegacyPunctual(n, p, v, lp, ld, lightSize, falloff, is_pointlight, lt))
    {
        return vec3(0);
    }

    return finishLegacyPunctual(lightColor * (lt.nl * lt.dist_atten) * diffuse, lt.spot_atten);
}

vec3 calcLegacyPointLightOrSpotLight(vec3 diffuse, vec4 spec,
                    vec3 n,  // normal
                    vec3 p,  // pixel position
                    vec3 v,  // view vector (negative normalized pixel position)
                    vec3 lp, // light position
                    vec3 ld, // light direction (for spotlights)
                    vec3 lightColor,
                    float lightSize, float falloff, float is_pointlight,
                    inout float glare)
{
    LegacyPunctualInfo lt;
    if (!calcLegacyPunctual(n, p, v, lp, ld, lightSize, falloff, is_pointlight, lt))
    {
        return vec3(0);
    }

    vec3 col = lightColor * (lt.nl * lt.dist_atten) * diffuse;

    if (spec.a > 0.0)
    {
        float lit = min(lt.nl * 6.0, 1.0) * lt.dist_atten;

        float fres = pow(1.0 - lt.vh, 5) * 0.4 + 0.5;
        float gtdenom = 2.0 * lt.nh;
        float gt = max(0.0, min(gtdenom * lt.nv / lt.vh, gtdenom * lt.nl / lt.vh));

        float scol = fres * blinnPhongLobe(lt.nh, spec.a) * gt / (lt.nh * lt.nl);
        vec3 speccol = lit * scol * lightColor * spec.rgb;

        col += speccol;

        vec3 glareSrc = clamp(speccol * lt.spot_atten, vec3(0), vec3(1));
        float cur_glare = max(max(glareSrc.r, glareSrc.g), glareSrc.b);
        glare = max(glare, glareSrc.r);
        glare += cur_glare;
    }

    return finishLegacyPunctual(col, lt.spot_atten);
}

// Split a glTF metallic-roughness base colour into the two lobes' albedos.
//
// c_diff = lerp(baseColor, black, metallic), per glTF 2.0 Appendix B. Only the metallic
// factor takes from the diffuse lobe here. The dielectric share the specular lobe reflects is
// not a constant 4%: it depends on the view angle, and both consumers already account for it
// where the angle is known -- pbrPunctual weights the diffuse term by (1 - F), and pbrIbl
// derives k_D from the surface's directional albedo. Scaling by (1 - f0) as well would charge
// dielectrics for that reflection twice and leave every one of them ~4% dark.
void calcDiffuseSpecular(vec3 baseColor, float metallic, inout vec3 diffuseColor, inout vec3 specularColor)
{
    vec3 f0 = vec3(0.04);
    diffuseColor = baseColor * (1.0 - metallic);
    specularColor = mix(f0, baseColor, metallic);
}

vec3 pbrBaseLight(vec3 diffuseColor, vec3 specularColor, float metallic, vec3 v, vec3 norm, float perceptualRoughness, vec3 light_dir, vec3 sunlit, float scol, vec3 radiance, vec3 irradiance, vec3 colorEmissive, float ao, vec3 additive, vec3 atten)
{
    perceptualRoughness = max(perceptualRoughness, MIN_PBR_ROUGHNESS);
    vec3 color = vec3(0);

    float NdotV = clamp(abs(dot(norm, v)), 0.001, 1.0);
    vec3 iblDiff = vec3(0);
    vec3 iblSpec = vec3(0);
    pbrIbl(diffuseColor, specularColor, radiance, irradiance, ao, NdotV, perceptualRoughness, iblDiff, iblSpec);

    color += iblDiff;

    // For classic mode, we use a special version of pbrPunctual that basically gives us a deconstructed form of the lighting.
    float nl = 0;
    vec3 diffPunc = vec3(0);
    vec3 specPunc = vec3(0);
    // The sun is a disc, not a delta light. Widening its lobe by the disc's own angular size
    // is what gives a polished surface a sun highlight with a real edge, and is why
    // MIN_PBR_ROUGHNESS no longer has to invent one for every light at once.
    pbrPunctual(diffuseColor, specularColor, sunDiscRoughness(perceptualRoughness), metallic, norm, v, normalize(light_dir), nl, diffPunc, specPunc);
    specPunc *= pbrEnergyCompensation(specularColor, perceptualRoughness, NdotV);

    // Depending on the sky, we combine these differently.
    if (classic_mode > 0)
    {
        irradiance.rgb = srgb_to_linear(irradiance * 0.9); // BINGO

        // Reconstruct the diffuse lighting that we do for blinn-phong materials here.
        // A special note about why we do some really janky stuff for classic mode.
        // Since adding classic mode, we've moved the lambertian diffuse multiply out from pbrPunctual and instead handle it in the different light type calcs.
        // This will never be 100% correct, but at the very least we can make it look mostly correct with legacy skies and classic mode.

        float da = pow(nl, 1.2);

        vec3 sun_contrib = vec3(min(da, scol));

        // Multiply by PI to account for lambertian diffuse colors.  Otherwise things will be too dark when lit by the sun on legacy skies.
        sun_contrib = srgb_to_linear(linear_to_srgb(sun_contrib) * sunlit * 0.7) * M_PI;

        // Manually recombine everything here.  We have to separate the shading to ensure that lighting is able to more closely match blinn-phong.
        //
        // Occlusion has to be applied by hand on this path: it discards iblDiff and rebuilds the
        // ambient term from raw irradiance, so it sees neither the material's occlusion nor the
        // caller's screen-space one. It used to receive the latter anyway, because the caller
        // multiplied it into irradiance before handing it over -- moving that out is what makes
        // this explicit rather than accidental, and it picks up the material occlusion that this
        // branch has never applied.
        vec3 finalAmbient = irradiance.rgb * diffuseColor.rgb * gtaoMultiBounce(ao, diffuseColor); // BINGO
        vec3 finalSun = clampRadiance(sun_contrib * ((diffPunc.rgb + specPunc.rgb) * scol)); // QUESTIONABLE BINGO?
        color.rgb = srgb_to_linear(linear_to_srgb(finalAmbient) + (linear_to_srgb(finalSun) * 1.1));
        //color.rgb = sun_contrib * diffuseColor.rgb;
    }
    else
    {
        color += clampRadiance(nl * (diffPunc + specPunc)) * sunlit * PUNCTUAL_LIGHT_SCALE * scol;
    }

    color.rgb += iblSpec.rgb;

    color += colorEmissive;

    return color;
}

uniform vec4 waterPlane;
uniform float waterSign;

// discard if given position in eye space is on the wrong side of the waterPlane according to waterSign
void waterClip(vec3 pos)
{
    // TODO: make this less branchy
    if (waterSign > 0)
    {
        if ((dot(pos.xyz, waterPlane.xyz) + waterPlane.w) < 0.0)
        {
            discard;
        }
    }
    else
    {
        if ((dot(pos.xyz, waterPlane.xyz) + waterPlane.w) > 0.0)
        {
            discard;
        }
    }

}

