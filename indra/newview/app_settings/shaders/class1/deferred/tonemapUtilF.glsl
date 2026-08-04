/**
 * @file tonemapUtilF.glsl
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

// Contains code licensed under:
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/


/*[EXTRA_CODE_HERE]*/

uniform sampler2D exposureMap;
uniform vec2 screen_res;
in vec2 vary_fragcoord;

//===============================================================
// tone mapping taken from Khronos sample implementation
//===============================================================

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

// ACES tone map (faster approximation)
// see: https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 toneMapACES_Narkowicz(vec3 color)
{
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return (color * (A * color + B)) / (color * (C * color + D) + E);
}

// ACES filmic tone map approximation
// see https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
vec3 RRTAndODTFit(vec3 color)
{
    vec3 a = color * (color + 0.0245786) - 0.000090537;
    vec3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
    return a / b;
}

// tone mapping
vec3 toneMapACES_Hill(vec3 color)
{
    color = ACESInputMat * color;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = ACESOutputMat * color;

    return color;
}

// Khronos Neutral tonemapping
// https://github.com/KhronosGroup/ToneMapping/tree/main
// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.
vec3 PBRNeutralToneMapping( vec3 color )
{
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return mix(color, newPeak * vec3(1, 1, 1), g);
}

//--------------------------------------------------------------------------------------

uniform vec4 tonemap_params;
const float output_max_value = 1.0;

// Based on Reinhard's extended formula, see equation 4 in https://doi.org/cjbgrt
vec3 tonemap_reinhard(vec3 color)
{
    float white_squared = tonemap_params.x;
    // Updated version of the Reinhard tonemapper supporting HDR rendering.
    return color * (1.0f + color / white_squared) / (1.0f + color / output_max_value);
}

//--------------------------------------------------------------------------------------

vec3 tonemap_filmic(vec3 color)
{
    // These constants must match the those in the C++ code that calculates the parameters.
    // exposure_bias: Input scale (color *= bias, env->white *= bias) to make the brightness consistent with other tonemappers.
    // Also useful to scale the input to the range that the tonemapper is designed for (some require very high input values).
    // Has no effect on the curve's general shape or visual properties.
    const float exposure_bias = 2.0f;
    const float A = 0.22f * exposure_bias * exposure_bias; // bias baked into constants for performance
    const float B = 0.30f * exposure_bias;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.01f;
    const float F = 0.30f;

    vec3 color_tonemapped = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;

    return color_tonemapped / tonemap_params.x;
}

//--------------------------------------------------------------------------------------

// Adapted from https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// (MIT License).
vec3 tonemap_aces_boosted(vec3 color) {
    // These constants must match the those in the C++ code that calculates the parameters.
    const float exposure_bias = 1.2f;
    const float A = 0.0245786f;
    const float B = 0.000090537f;
    const float C = 0.983729f;
    const float D = 0.4329510f;
    const float E = 0.238081f;

    // Exposure bias baked into transform to save shader instructions. Equivalent to `color *= exposure_bias`
    const mat3 rgb_to_rrt = mat3(
            vec3(0.59719f * exposure_bias, 0.35458f * exposure_bias, 0.04823f * exposure_bias),
            vec3(0.07600f * exposure_bias, 0.90834f * exposure_bias, 0.01566f * exposure_bias),
            vec3(0.02840f * exposure_bias, 0.13383f * exposure_bias, 0.83777f * exposure_bias));

    const mat3 odt_to_rgb = mat3(
            vec3(1.60475f, -0.53108f, -0.07367f),
            vec3(-0.10208f, 1.10813f, -0.00605f),
            vec3(-0.00327f, -0.07276f, 1.07602f));

    color *= rgb_to_rrt;
    vec3 color_tonemapped = (color * (color + A) - B) / (color * (C * color + D) + E);
    color_tonemapped *= odt_to_rgb;

    return color_tonemapped / tonemap_params.x;
}

//--------------------------------------------------------------------------------------

// allenwp tonemapping curve; developed for use in the Godot game engine.
// Source and details: https://allenwp.com/blog/2025/05/29/allenwp-tonemapping-curve/
// Input must be a non-negative linear scene value.
vec3 allenwp_curve(vec3 x)
{
    // These constants must match the those in the C++ code that calculates the parameters.
    // 18% "middle gray" is perceptually 50% of the brightness of reference white.
    const float awp_crossover_point = 0.18;
    // When output_max_value and/or awp_crossover_point are no longer constant,
    // awp_shoulder_max can be calculated on the CPU and passed in as params.tonemap_e.
    const float awp_shoulder_max = output_max_value - awp_crossover_point;

    float awp_contrast = tonemap_params.x;
    float awp_toe_a = tonemap_params.y;
    float awp_slope = tonemap_params.z;
    float awp_w = tonemap_params.w;

    // Reinhard-like shoulder:
    vec3 s = x - awp_crossover_point;
    vec3 slope_s = awp_slope * s;
    s = slope_s * (1.0 + s / awp_w) / (1.0 + (slope_s / awp_shoulder_max));
    s += awp_crossover_point;

    // Sigmoid power function toe:
    vec3 t = pow(x, vec3(awp_contrast));
    t = t / (t + awp_toe_a);

    return mix(s, t, lessThan(x, vec3(awp_crossover_point)));
}

// This is an approximation and simplification of EaryChow's AgX implementation that is used by Blender.
// This code is based off of the script that generates the AgX_Base_sRGB.cube LUT that Blender uses.
// Source: https://github.com/EaryChow/AgX_LUT_Gen/blob/main/AgXBasesRGB.py
// Colorspace transformation source: https://www.colour-science.org:8010/apps/rgb_colourspace_transformation_matrix
vec3 tonemap_agx(vec3 color)
{
    // Input color should be non-negative!
    // Large negative values in one channel and large positive values in other
    // channels can result in a colour that appears darker and more saturated than
    // desired after passing it through the inset matrix. For this reason, it is
    // best to prevent negative input values.
    // This is done before the Rec. 2020 transform to allow the Rec. 2020
    // transform to be combined with the AgX inset matrix. This results in a loss
    // of color information that could be correctly interpreted within the
    // Rec. 2020 color space as positive RGB values, but is often not worth
    // the performance cost of an additional matrix multiplication.
    //
    // Additionally, this AgX configuration was created subjectively based on
    // output appearance in the Rec. 709 color gamut, so it is possible that these
    // matrices will not perform well with non-Rec. 709 output (more testing with
    // future wide-gamut displays is be needed).
    // See this comment from the author on the decisions made to create the matrices:
    // https://github.com/godotengine/godot-proposals/issues/12317#issuecomment-2835824250

    // Combined Rec. 709 to Rec. 2020 and Blender AgX inset matrices:
    const mat3 rec709_to_rec2020_agx_inset_matrix = mat3(
            0.544814746488245, 0.140416948464053, 0.0888104196149096,
            0.373787398372697, 0.754137554567394, 0.178871756420858,
            0.0813978551390581, 0.105445496968552, 0.732317823964232);

    // Combined inverse AgX outset matrix and Rec. 2020 to Rec. 709 matrices.
    const mat3 agx_outset_rec2020_to_rec709_matrix = mat3(
            1.96488741169489, -0.299313364904742, -0.164352742528393,
            -0.855988495690215, 1.32639796461980, -0.238183969428088,
            -0.108898916004672, -0.0270845997150571, 1.40253671195648);

    // Apply inset matrix.
    color = rec709_to_rec2020_agx_inset_matrix * color;

    // Use the allenwp tonemapping curve to match the Blender AgX curve while
    // providing stability across all variable dyanimc range (SDR, HDR, EDR).
    color = allenwp_curve(color);

    // Clipping to output_max_value is required to address a cyan colour that occurs
    // with very bright inputs.
    color = min(vec3(output_max_value), color);

    // Apply outset to make the result more chroma-laden and then go back to Rec. 709.
    color = agx_outset_rec2020_to_rec709_matrix * color;

    // Blender's lusRGB.compensate_low_side is too complex for this shader, so
    // simply return the color, even if it has negative components. These negative
    // components may be useful for subsequent color adjustments.
    return color;
}

//--------------------------------------------------------------------------------------

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
vec3 uchimura(vec3 color)
{
    float P = tone_uchimura_a.x; // max display brightness
    float a = tone_uchimura_a.y; // contrast
    float m = tone_uchimura_a.z; // linear section start
    float l = tone_uchimura_b.x; // linear section length
    float c = tone_uchimura_b.y; // black
    float b = tone_uchimura_b.z; // pedestal

    return uchimura(color, P, a, m, l, c, b);
}

//--------------------------------------------------------------------------------------

uniform float exposure;
uniform float tonemap_mix;
uniform int tonemap_type;

vec3 applyExposure(vec3 color)
{
    float exp_scale = texture(exposureMap, vec2(0.5,0.5)).r;
    return color * exposure * exp_scale;
}

vec3 applyToneMap(vec3 color)
{
    color = max(color, vec3(0.0)); // avoid negative values for tonemapping

    vec3 clamped_color = clamp(color.rgb, vec3(0.0), vec3(1.0));

    switch(tonemap_type)
    {
    case 0:
        color = PBRNeutralToneMapping(color);
        break;
    case 1:
        color = toneMapACES_Hill(color);
        break;
    case 2:
        color = tonemap_aces_boosted(color);
        break;
    case 3:
        color = tonemap_reinhard(color);
        break;
    case 4:
        color = tonemap_filmic(color);
        break;
    case 5:
        color = uchimura(color);
        break;
    case 6:
        color = tonemap_agx(color);
        break;
    }

    color.rgb = clamp(color.rgb, vec3(0.0), vec3(1.0));

    // mix tonemapped and linear here to provide adjustment
    return mix(clamped_color, color, tonemap_mix);
}

//===============================================================

void debugExposure(inout vec3 color)
{
    float exp_scale = texture(exposureMap, vec2(0.5,0.5)).r;
    exp_scale *= 0.5;
    if (abs(vary_fragcoord.y-exp_scale) < 0.01 && vary_fragcoord.x < 0.1)
    {
        color = vec3(1,0,0);
    }
}
