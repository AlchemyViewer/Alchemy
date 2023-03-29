/**
 * @file toneMapF.glsl
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2021, Rye Mutt<rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

/*[EXTRA_CODE_HERE]*/

#ifdef DEFINE_GL_FRAGCOLOR
out vec4 frag_color;
#else
#define frag_color gl_FragColor
#endif

in vec2 vary_fragcoord;

uniform sampler2D diffuseRect;
uniform sampler2D emissiveRect;
uniform sampler2D exposureMap;

uniform vec2 screen_res;
uniform float exposure;

vec3 srgb_to_linear(vec3 cl);
vec3 linear_to_srgb(vec3 cl);

#if COLOR_GRADE_LUT != 0
uniform sampler3D colorgrade_lut;
uniform vec4 colorgrade_lut_size;
#endif

vec3 reinhard(vec3 x)
{
    return x/(1+x);
}

vec3 reinhard2(vec3 x) {
    const float L_white = 4.0;
    return (x * (1.0 + x / (L_white * L_white))) / (1.0 + x);
}

vec3 filmic(vec3 color)
{
    color = max(vec3(0.), color - vec3(0.004));
    color = (color * (6.2 * color + .5)) / (color * (6.2 * color + 1.7) + 0.06);
    return color;
}

vec3 unreal(vec3 x)
{
    // Unreal 3, Documentation: "Color Grading"
    // Adapted to be close to Tonemap_ACES, with similar range
    // Gamma 2.2 correction is baked in, don't use with sRGB conversion!
    return x / (x + 0.155) * 1.019;
}

vec3 ACES_Narkowicz(vec3 x)
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}

// ACES filmic tone map approximation
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3
(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777
);


// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3
(
    1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602
);

vec3 RRTAndODTFit(vec3 color)
{
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACES_Hill(vec3 color)
{
    color = ACESInputMat * color;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = ACESOutputMat * color;

    return color;
}

vec3 uchimura(vec3 x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;
    
    vec3 w0 = vec3(1.0 - smoothstep(0.0, m, x));
    vec3 w2 = vec3(step(m + l0, x));
    vec3 w1 = vec3(1.0 - w0 - w2);
    
    vec3 T = vec3(m * pow(x / m, vec3(c)) + b);
    vec3 S = vec3(P - (P - S1) * exp(CP * (x - S0)));
    vec3 L = vec3(m + a * (x - m));
    
    return T * w0 + L * w1 + S * w2;
}

uniform vec3 tone_uchimura_a = vec3(1.0, 1.0, 0.22);
uniform vec3 tone_uchimura_b = vec3(0.4, 1.33, 0.0);
vec3 uchimura(vec3 x)
{
    float P = tone_uchimura_a.x; // max display brightness
    float a = tone_uchimura_a.y; // contrast
    float m = tone_uchimura_a.z; // linear section start
    float l = tone_uchimura_b.x; // linear section length
    float c = tone_uchimura_b.y; // black
    float b = tone_uchimura_b.z; // pedestal
    
    return uchimura(x, P, a, m, l, c, b);
}

//--------------------------------------------------------------------------------------
// AMD Tonemapper
//--------------------------------------------------------------------------------------
// General tonemapping operator, build 'b' term.
float ColToneB(float hdrMax, float contrast, float shoulder, float midIn, float midOut) 
{
    return
        -((-pow(midIn, contrast) + (midOut*(pow(hdrMax, contrast*shoulder)*pow(midIn, contrast) -
            pow(hdrMax, contrast)*pow(midIn, contrast*shoulder)*midOut)) /
            (pow(hdrMax, contrast*shoulder)*midOut - pow(midIn, contrast*shoulder)*midOut)) /
            (pow(midIn, contrast*shoulder)*midOut));
}

// General tonemapping operator, build 'c' term.
float ColToneC(float hdrMax, float contrast, float shoulder, float midIn, float midOut) 
{
    return (pow(hdrMax, contrast*shoulder)*pow(midIn, contrast) - pow(hdrMax, contrast)*pow(midIn, contrast*shoulder)*midOut) /
           (pow(hdrMax, contrast*shoulder)*midOut - pow(midIn, contrast*shoulder)*midOut);
}

// General tonemapping operator, p := {contrast,shoulder,b,c}.
float ColTone(float x, vec4 p) 
{ 
    float z = pow(x, p.r); 
    return z / (pow(z, p.g)*p.b + p.a); 
}

uniform vec3 tone_lottes_a = vec3(1.4, 1.0, 16.0);
vec3 AMDTonemapper(vec3 color)
{
    float hdrMax = tone_lottes_a.z; // How much HDR range before clipping. HDR modes likely need this pushed up to say 25.0.
    float contrast = tone_lottes_a.x; // Use as a baseline to tune the amount of contrast the tonemapper has.
    float shoulder = tone_lottes_a.y; // Likely don’t need to mess with this factor, unless matching existing tonemapper is not working well..
    const float midIn = 0.18; // most games will have a {0.0 to 1.0} range for LDR so midIn should be 0.18.
    const float midOut = 0.18; // Use for LDR. For HDR10 10:10:10:2 use maybe 0.18/25.0 to start. For scRGB, I forget what a good starting point is, need to re-calculate.

    float b = ColToneB(hdrMax, contrast, shoulder, midIn, midOut);
    float c = ColToneC(hdrMax, contrast, shoulder, midIn, midOut);

    #define EPS 1e-6f
    float peak = max(color.r, max(color.g, color.b));
    peak = max(EPS, peak);

    vec3 ratio = color / peak;
    peak = ColTone(peak, vec4(contrast, shoulder, b, c) );
    // then process ratio

    // probably want send these pre-computed (so send over saturation/crossSaturation as a constant)
    float crosstalk = 4.0; // controls amount of channel crosstalk
    float saturation = contrast; // full tonal range saturation control
    float crossSaturation = contrast*16.0; // crosstalk saturation

    float white = 1.0;

    // wrap crosstalk in transform
    ratio = pow(abs(ratio), vec3(saturation / crossSaturation));
    ratio = mix(ratio, vec3(white), vec3(pow(peak, crosstalk)));
    ratio = pow(abs(ratio), vec3(crossSaturation));

    // then apply ratio to peak
    color = peak * ratio;
    return color;
}

//--------------------------------------------------------------------------------------

// Hable, http://filmicworlds.com/blog/filmic-tonemapping-operators/
uniform vec3 tone_uncharted_a = vec3(0.15, 0.50, 0.10); // A, B, C
uniform vec3 tone_uncharted_b = vec3(0.20, 0.02, 0.30); // D, E, F
uniform vec3 tone_uncharted_c = vec3(11.2, 2.0, 0.0); // W, ExposureBias, Unused
vec3 Uncharted2Tonemap(vec3 x)
{
    float A = tone_uncharted_a.x;
    float B = tone_uncharted_a.y;
    float C = tone_uncharted_a.z;
    float D = tone_uncharted_b.x;
    float E = tone_uncharted_b.y;
    float F = tone_uncharted_b.z;
    
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 uncharted2(vec3 col)
{
    float ExposureBias = tone_uncharted_c.y;
    return Uncharted2Tonemap(ExposureBias*col)/Uncharted2Tonemap(vec3(tone_uncharted_c.x));;
}

//=================================
// borrowed noise from:
//	<https://www.shadertoy.com/view/4dS3Wd>
//	By Morgan McGuire @morgan3d, http://graphicscodex.com
//
float hash(float n) { return fract(sin(n) * 1e4); }
float hash(vec2 p) { return fract(1e4 * sin(17.0 * p.x + p.y * 0.1) * (0.1 + abs(sin(p.y * 13.0 + p.x)))); }

float noise(float x) {
	float i = floor(x);
	float f = fract(x);
	float u = f * f * (3.0 - 2.0 * f);
	return mix(hash(i), hash(i + 1.0), u);
}

float noise(vec2 x) {
	vec2 i = floor(x);
	vec2 f = fract(x);

	// Four corners in 2D of a tile
	float a = hash(i);
	float b = hash(i + vec2(1.0, 0.0));
	float c = hash(i + vec2(0.0, 1.0));
	float d = hash(i + vec2(1.0, 1.0));

	// Simple 2D lerp using smoothstep envelope between the values.
	// return vec3(mix(mix(a, b, smoothstep(0.0, 1.0, f.x)),
	//			mix(c, d, smoothstep(0.0, 1.0, f.x)),
	//			smoothstep(0.0, 1.0, f.y)));

	// Same code, with the clamps in smoothstep and common subexpressions
	// optimized away.
	vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

//=============================

uniform float gamma;
vec3 legacyGamma(vec3 color)
{
    color = 1. - clamp(color, vec3(0.), vec3(1.));
    color = 1. - pow(color, vec3(gamma)); // s/b inverted already CPU-side

    return color;
}

void main()
{
    vec4 diff = texture(diffuseRect, vary_fragcoord) + texture2D(emissiveRect, vary_fragcoord);
 
    float exp_scale = texture(exposureMap, vec2(0.5,0.5)).r;
    diff.rgb *= exposure * exp_scale;
    
    #if TONEMAP_METHOD == 0 // None, Gamma Correct Only
    #define NEEDS_GAMMA_CORRECT 1
    #elif TONEMAP_METHOD == 1 // Linear
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = clamp(diff.rgb, 0, 1);
    #elif TONEMAP_METHOD == 2 // Reinhard method
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = reinhard(diff.rgb);
    #elif TONEMAP_METHOD == 3 // Reinhard2 method
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = reinhard2(diff.rgb);
    #elif TONEMAP_METHOD == 4 // Filmic method
    #define NEEDS_GAMMA_CORRECT 0
    diff.rgb = filmic(diff.rgb);
    #elif TONEMAP_METHOD == 5 // Unreal method
    #define NEEDS_GAMMA_CORRECT 0
    diff.rgb = unreal(diff.rgb);
    #elif TONEMAP_METHOD == 6 // Aces Narkowicz method
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = ACES_Narkowicz(diff.rgb);
    #elif TONEMAP_METHOD == 10 // Aces Hill method
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb *= 1.0/0.6;
    diff.rgb = ACES_Hill(diff.rgb);
    #elif TONEMAP_METHOD == 7 // Uchimura's Gran Turismo method
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = uchimura(diff.rgb);
    #elif TONEMAP_METHOD == 8 // AMD Tonemapper
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = AMDTonemapper(diff.rgb);
    #elif TONEMAP_METHOD == 9 // Uncharted
    #define NEEDS_GAMMA_CORRECT 1
    diff.rgb = uncharted2(diff.rgb);
    #else
    #define NEEDS_GAMMA_CORRECT 1
    #endif
    
    // We should always be 0-1 past here.
    diff.rgb = clamp(diff.rgb, 0, 1);

    #if NEEDS_GAMMA_CORRECT != 0
    diff.rgb = linear_to_srgb(diff.rgb);
    #endif
    
    #if COLOR_GRADE_LUT != 0
    // Invert coord for compat with DX-style LUT
    diff.g = colorgrade_lut_size.y > 0.5 ? 1.0 - diff.g : diff.g;

    // swap bluegreen if needed
    diff.rgb = colorgrade_lut_size.z > 0.5 ? diff.rbg: diff.rgb;

    //see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter24.html
    vec3 scale = (vec3(colorgrade_lut_size.x) - 1.0) / vec3(colorgrade_lut_size.x);
    vec3 offset = 1.0 / (2.0 * vec3(colorgrade_lut_size.x));
    diff = vec4(textureLod(colorgrade_lut, scale * diff.rgb + offset, 0).rgb, diff.a);
    #endif

    diff.rgb = legacyGamma(diff.rgb);

    vec2 tc = vary_fragcoord.xy*screen_res*4.0;
    vec3 seed = (diff.rgb+vec3(1.0))*vec3(tc.xy, tc.x+tc.y);
    vec3 nz = vec3(noise(seed.rg), noise(seed.gb), noise(seed.rb));
    diff.rgb += nz*0.003;

    frag_color = diff;
}
