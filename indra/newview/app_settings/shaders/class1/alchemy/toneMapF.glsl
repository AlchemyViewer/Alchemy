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

out vec4 frag_color;
in vec2 vary_fragcoord;

uniform sampler2D diffuseRect;
uniform sampler2D exposureMap;

uniform float exposure;
uniform float aces_mix;

vec3 srgb_to_linear(vec3 cl);
vec3 linear_to_srgb(vec3 cl);

#if TONEMAP_METHOD == 3
void RunLPMFilter(inout vec3 diff);
#endif

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
    float b = 0.0; // pedestal
    
    return uchimura(x, P, a, m, l, c, b);
}

//--------------------------------------------------------------------------------------

// Hable, http://filmicworlds.com/blog/filmic-tonemapping-operators/
uniform vec3 tone_uncharted_a = vec3(0.22, 0.30, 0.10); // A, B, C
uniform vec3 tone_uncharted_b = vec3(0.20, 0.01, 0.30); // D, E, F
uniform vec3 tone_uncharted_c = vec3(8.0, 2.0, 0.0); // W, ExposureBias, Unused
vec3 Uncharted2Tonemap(vec3 x)
{
    float ExposureBias = tone_uncharted_c.y;
    float A = tone_uncharted_a.x * ExposureBias * ExposureBias;
    float B = tone_uncharted_a.y * ExposureBias;
    float C = tone_uncharted_a.z;
    float D = tone_uncharted_b.x;
    float E = tone_uncharted_b.y;
    float F = tone_uncharted_b.z;
    
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 uncharted2(vec3 col)
{
    return Uncharted2Tonemap(col)/Uncharted2Tonemap(vec3(tone_uncharted_c.x));
}

//=============================

void main()
{
    vec4 diff = texture(diffuseRect, vary_fragcoord);
 
#if TONEMAP_METHOD != 0 
    float exp_scale = texture(exposureMap, vec2(0.5,0.5)).r;
    diff.rgb *= exposure * exp_scale;
#endif

#if TONEMAP_METHOD == 1 // Aces Hill method
    diff.rgb = mix(ACES_Hill(diff.rgb), diff.rgb, aces_mix);
#elif TONEMAP_METHOD == 2 // Uchimura's Gran Turismo method
    diff.rgb = uchimura(diff.rgb);
#elif TONEMAP_METHOD == 3 // AMD Tonemapper
    RunLPMFilter(diff.rgb);
#elif TONEMAP_METHOD == 4 // Uncharted
    diff.rgb = uncharted2(diff.rgb);
#endif
    // We should always be 0-1 past here.
    diff.rgb = clamp(diff.rgb, 0, 1);
    frag_color = diff;
}
