/**
 * @file llshadermgr.h
 * @brief Shader Manager
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#ifndef LL_SHADERMGR_H
#define LL_SHADERMGR_H

#include "llgl.h"
#include "llglslshader.h"

class LLShaderMgr
{
public:
    LLShaderMgr();
    virtual ~LLShaderMgr();

    // Note: although you can use statically hashed strings to just bind a random uniform, it's generally preferably that you use this.
    // Always document what the actual shader uniform is next to the shader uniform in this struct.
    // clang-format off
    typedef enum
    {                                       // Shader uniform name, set in LLShaderMgr::initAttribsAndUniforms()
        MODELVIEW_MATRIX = 0,               //  "modelview_matrix"
        PROJECTION_MATRIX,                  //  "projection_matrix"
        INVERSE_PROJECTION_MATRIX,          //  "inv_proj"
        MODELVIEW_PROJECTION_MATRIX,        //  "modelview_projection_matrix"
        INVERSE_MODELVIEW_MATRIX,           //  "inv_modelview"
        IDENTITY_MATRIX,                    //  "identity_matrix"
        NORMAL_MATRIX,                      //  "normal_matrix"
        TEXTURE_MATRIX0,                    //  "texture_matrix0"
        TEXTURE_MATRIX1,                    //  "texture_matrix1"
        OBJECT_PLANE_S,                     //  "object_plane_s"
        OBJECT_PLANE_T,                     //  "object_plane_t"

        TEXTURE_BASE_COLOR_TRANSFORM,         //  "texture_base_color_transform" (GLTF)
        TEXTURE_NORMAL_TRANSFORM,             //  "texture_normal_transform" (GLTF)
        TEXTURE_METALLIC_ROUGHNESS_TRANSFORM, //  "texture_metallic_roughness_transform" (GLTF)
        TEXTURE_EMISSIVE_TRANSFORM,           //  "texture_emissive_transform" (GLTF)
        BASE_COLOR_TEXCOORD,                  //  "base_color_texcoord" (GLTF)
        EMISSIVE_TEXCOORD,                    //  "emissive_texcoord" (GLTF)
        NORMAL_TEXCOORD,                      //  "normal_texcoord" (GLTF)
        METALLIC_ROUGHNESS_TEXCOORD,          //  "metallic_roughness_texcoord" (GLTF)

        TERRAIN_TEXTURE_TRANSFORMS,           //  "terrain_texture_transforms" (GLTF)

        VIEWPORT,                           //  "viewport"
        LIGHT_POSITION,                     //  "light_position"
        LIGHT_DIRECTION,                    //  "light_direction"
        LIGHT_ATTENUATION,                  //  "light_attenuation"
        LIGHT_DEFERRED_ATTENUATION,         //  "light_deferred_attenuation"
        LIGHT_DIFFUSE,                      //  "light_diffuse"
        LIGHT_AMBIENT,                      //  "light_ambient"
        MULTI_LIGHT_COUNT,                  //  "light_count"
        MULTI_LIGHT,                        //  "light"
        MULTI_LIGHT_COL,                    //  "light_col"
        MULTI_LIGHT_FAR_Z,                  //  "far_z"
        PROJECTOR_MATRIX,                   //  "proj_mat"
        PROJECTOR_NEAR,                     //  "proj_near"
        PROJECTOR_P,                        //  "proj_p"
        PROJECTOR_N,                        //  "proj_n"
        PROJECTOR_ORIGIN,                   //  "proj_origin"
        PROJECTOR_RANGE,                    //  "proj_range"
        PROJECTOR_AMBIANCE,                 //  "proj_ambiance"
        PROJECTOR_SHADOW_INDEX,             //  "proj_shadow_idx"
        PROJECTOR_SHADOW_FADE,              //  "shadow_fade"
        PROJECTOR_FOCUS,                    //  "proj_focus"
        PROJECTOR_LOD,                      //  "proj_lod"
        PROJECTOR_AMBIENT_LOD,              //  "proj_ambient_lod"
        DIFFUSE_COLOR,                      //  "color"
        EMISSIVE_COLOR,                     //  "emissiveColor"
        METALLIC_FACTOR,                    //  "metallicFactor"
        ROUGHNESS_FACTOR,                   //  "roughnessFactor"
        CLIP_PLANE,                         //  "clipPlane"
        CLIP_SIGN,                          //  "clipSign"
        DIFFUSE_MAP,                        //  "diffuseMap"
        ALTERNATE_DIFFUSE_MAP,              //  "altDiffuseMap"
        SPECULAR_MAP,                       //  "specularMap"
        METALLIC_ROUGHNESS_MAP,             //  "metallicRoughnessMap"
        NORMAL_MAP,                         //  "normalMap"
        EMISSIVE_MAP,                       //  "emissiveMap"
        BUMP_MAP,                           //  "bumpMap"
        BUMP_MAP2,                          //  "bumpMap2"
        ENVIRONMENT_MAP,                    //  "environmentMap"
        SCENE_MAP,                          //  "sceneMap"
        SCENE_DEPTH,                        //  "sceneDepth"
        REFLECTION_PROBES,                  //  "reflectionProbes"
        IRRADIANCE_PROBES,                  //  "irradianceProbes"
        HERO_PROBE,                         //  "heroProbes"
        CLOUD_NOISE_MAP,                    //  "cloud_noise_texture"
        CLOUD_NOISE_MAP_NEXT,               //  "cloud_noise_texture_next"
        LIGHTNORM,                          //  "lightnorm"
        SUNLIGHT_COLOR,                     //  "sunlight_color"
        AMBIENT,                            //  "ambient_color"
        SKY_HDR_SCALE,                      //  "sky_hdr_scale"
        SKY_SUNLIGHT_SCALE,                 //  "sky_sunlight_scale"
        SKY_AMBIENT_SCALE,                  //  "sky_ambient_scale"
        BLUE_HORIZON,                       //  "blue_horizon"
        BLUE_DENSITY,                       //  "blue_density"
        HAZE_HORIZON,                       //  "haze_horizon"
        HAZE_DENSITY,                       //  "haze_density"
        CLOUD_SHADOW,                       //  "cloud_shadow"
        DENSITY_MULTIPLIER,                 //  "density_multiplier"
        DISTANCE_MULTIPLIER,                //  "distance_multiplier"
        MAX_Y,                              //  "max_y"
        GLOW,                               //  "glow"
        CLOUD_COLOR,                        //  "cloud_color"
        CLOUD_POS_DENSITY1,                 //  "cloud_pos_density1"
        CLOUD_POS_DENSITY2,                 //  "cloud_pos_density2"
        CLOUD_SCALE,                        //  "cloud_scale"
        GAMMA,                              //  "gamma"
        SCENE_LIGHT_STRENGTH,               //  "scene_light_strength"
        LIGHT_CENTER,                       //  "center"
        LIGHT_SIZE,                         //  "size"
        LIGHT_FALLOFF,                      //  "falloff"
        BOX_CENTER,                         //  "box_center"
        BOX_SIZE,                           //  "box_size"

        GLOW_MIN_LUMINANCE,                 //  "minLuminance"
        GLOW_MAX_EXTRACT_ALPHA,             //  "maxExtractAlpha"
        GLOW_LUM_WEIGHTS,                   //  "lumWeights"
        GLOW_WARMTH_WEIGHTS,                //  "warmthWeights"
        GLOW_WARMTH_AMOUNT,                 //  "warmthAmount"
        GLOW_STRENGTH,                      //  "glowStrength"
        GLOW_DELTA,                         //  "glowDelta"
        GLOW_NOISE_MAP,                     //  "glowNoiseMap"

        BLOOM_THRESHOLD,                    //  "bloom_threshold"
        BLOOM_KNEE,                         //  "bloom_knee"
        BLOOM_TEXEL_SIZE,                   //  "bloom_texel_size"
        BLOOM_SCATTER,                      //  "bloom_scatter"
        BLOOM_STRENGTH,                     //  "bloom_strength"
        BLOOM_ALPHA_GLOW_BOOST,             //  "alpha_glow_boost"
        BLOOM_SAMPLER,                      //  "bloomMap"
        HALATION_STRENGTH,                  //  "halation_strength"
        HALATION_TINT,                      //  "halation_tint"
        HALATION_LUM_WEIGHTS,               //  "halation_lum_weights"

        MINIMUM_ALPHA,                      //  "minimum_alpha"
        EMISSIVE_BRIGHTNESS,                //  "emissive_brightness"

        DEFERRED_SHADOW_MATRIX,             //  "shadow_matrix"
        DEFERRED_ENV_MAT,                   //  "env_mat"
        DEFERRED_SHADOW_CLIP,               //  "shadow_clip"
        DEFERRED_SSAO_RADIUS,               //  "ssao_radius"
        DEFERRED_SSAO_MAX_RADIUS,           //  "ssao_max_radius"
        DEFERRED_SSAO_FACTOR,               //  "ssao_factor"
        DEFERRED_SSAO_FACTOR_INV,           //  "ssao_factor_inv"
        DEFERRED_SSAO_EFFECT_MAT,           //  "ssao_effect_mat"
        DEFERRED_SSAO_IRRADIANCE_SCALE,     //  "ssao_irradiance_scale"
        DEFERRED_SSAO_IRRADIANCE_MAX,       //  "ssao_irradiance_max"
        DEFERRED_SCREEN_RES,                //  "screen_res"
        DEFERRED_NEAR_CLIP,                 //  "near_clip"
        DEFERRED_SHADOW_OFFSET,             //  "shadow_offset"
        DEFERRED_SHADOW_BIAS,               //  "shadow_bias"
        DEFERRED_SPOT_SHADOW_BIAS,          //  "spot_shadow_bias"
        DEFERRED_SPOT_SHADOW_OFFSET,        //  "spot_shadow_offset"
        DEFERRED_SUN_DIR,                   //  "sun_dir"
        DEFERRED_MOON_DIR,                  //  "moon_dir"
        DEFERRED_SHADOW_RES,                //  "shadow_res"
        DEFERRED_PROJ_SHADOW_RES,           //  "proj_shadow_res"
        DEFERRED_DEPTH_CUTOFF,              //  "depth_cutoff"
        DEFERRED_NORM_CUTOFF,               //  "norm_cutoff"
        DEFERRED_SHADOW_TARGET_WIDTH,       //  "shadow_target_width"

        DEFERRED_SSR_ITR_COUNT,             //  "iterationCount"
        DEFERRED_SSR_RAY_STEP,              //  "rayStep"
        DEFERRED_SSR_DIST_BIAS,             //  "distanceBias"
        DEFERRED_SSR_REJECT_BIAS,           //  "depthRejectBias"
        DEFERRED_SSR_GLOSSY_SAMPLES,        //  "glossySampleCount"
        DEFERRED_SSR_NOISE_SINE,            //  "noiseSine"
        DEFERRED_SSR_ADAPTIVE_STEP_MULT,    //  "adaptiveStepMultiplier"

        MODELVIEW_DELTA_MATRIX,             //  "modelview_delta"
        INVERSE_MODELVIEW_DELTA_MATRIX,     //  "inv_modelview_delta"
        CUBE_SNAPSHOT,                      //  "cube_snapshot"

        FXAA_TC_SCALE,                      //  "tc_scale"
        FXAA_RCP_SCREEN_RES,                //  "rcp_screen_res"
        FXAA_RCP_FRAME_OPT,                 //  "rcp_frame_opt"
        FXAA_RCP_FRAME_OPT2,                //  "rcp_frame_opt2"

        DOF_FOCAL_DISTANCE,                 //  "focal_distance"
        DOF_BLUR_CONSTANT,                  //  "blur_constant"
        DOF_TAN_PIXEL_ANGLE,                //  "tan_pixel_angle"
        DOF_MAGNIFICATION,                  //  "magnification"
        DOF_MAX_COF,                        //  "max_cof"
        DOF_RES_SCALE,                      //  "res_scale"
        DOF_WIDTH,                          //  "dof_width"
        DOF_HEIGHT,                         //  "dof_height"

        DEFERRED_DEPTH,                     //  "depthMap"
        DEFERRED_SHADOW0,                   //  "shadowMap0"
        DEFERRED_SHADOW1,                   //  "shadowMap1"
        DEFERRED_SHADOW2,                   //  "shadowMap2"
        DEFERRED_SHADOW3,                   //  "shadowMap3"
        DEFERRED_SHADOW4,                   //  "shadowMap4"
        DEFERRED_SHADOW5,                   //  "shadowMap5"
        DEFERRED_DIFFUSE,                   //  "diffuseRect"
        DEFERRED_SPECULAR,                  //  "specularRect"
        DEFERRED_EMISSIVE,                  //  "emissiveRect"
        EXPOSURE_MAP,                       //  "exposureMap"
        DEFERRED_BRDF_LUT,                  //  "brdfLut"
        DEFERRED_NOISE,                     //  "noiseMap"
        DEFERRED_LIGHTFUNC,                 //  "lightFunc"
        DEFERRED_LIGHT,                     //  "lightMap"
        DEFERRED_PROJECTION,                //  "projectionMap"
        DEFERRED_NORM_MATRIX,               //  "norm_mat"
        IMPOSTOR_NORM_ROTATION,             //  "impostor_norm_rot"
        SPECULAR_COLOR,                     //  "specular_color"
        ENVIRONMENT_INTENSITY,              //  "env_intensity"

        AVATAR_MATRIX,                      //  "matrixPalette"
        SKIN_ORIGIN,                        //  "skin_origin" (rigged path: agent-space rebase origin the palette translations were made relative to)

        WATER_SCREENTEX,                    //  "screenTex"
        WATER_EXCLUSIONTEX,                 //  "exclusionTex"
        WATER_EYEVEC,                       //  "eyeVec"
        WATER_TIME,                         //  "time"
        WATER_WAVE_DIR1,                    //  "waveDir1"
        WATER_WAVE_DIR2,                    //  "waveDir2"
        WATER_LIGHT_DIR,                    //  "lightDir"
        WATER_SPECULAR,                     //  "specular"
        WATER_SPECULAR_EXP,                 //  "lightExp"
        WATER_FOGCOLOR,                     //  "waterFogColor"
        WATER_FOGCOLOR_LINEAR,              //  "waterFogColorLinear"
        WATER_FOGDENSITY,                   //  "waterFogDensity"
        WATER_FOGKS,                        //  "waterFogKS"
        WATER_REFSCALE,                     //  "refScale"
        WATER_WATERHEIGHT,                  //  "waterHeight"
        WATER_WATERPLANE,                   //  "waterPlane"
        WATER_WATERSIGN,                    //  "waterSign"
        WATER_NORM_SCALE,                   //  "normScale"
        WATER_FRESNEL_SCALE,                //  "fresnelScale"
        WATER_FRESNEL_OFFSET,               //  "fresnelOffset"
        WATER_BLUR_MULTIPLIER,              //  "blurMultiplier"
        WATER_ABOVE_WATER,                  //  "above_water"

        WL_CAMPOSLOCAL,                     //  "camPosLocal"
// [RLVa:KB] - @setsphere
        RLV_EFFECT_MODE,
        RLV_EFFECT_PARAM1,
        RLV_EFFECT_PARAM2,
        RLV_EFFECT_PARAM3,
        RLV_EFFECT_PARAM4,
        RLV_EFFECT_PARAM5,
// [/RLVa:KB]

        AVATAR_WIND,                        //  "gWindDir"
        AVATAR_SINWAVE,                     //  "gSinWaveParams"
        AVATAR_GRAVITY,                     //  "gGravity"

        TERRAIN_DETAIL0,                    //  "detail_0"
        TERRAIN_DETAIL1,                    //  "detail_1"
        TERRAIN_DETAIL2,                    //  "detail_2"
        TERRAIN_DETAIL3,                    //  "detail_3"

        TERRAIN_ALPHARAMP,                  //  "alpha_ramp"
        TERRAIN_PAINTMAP,                   //  "paint_map"

        TERRAIN_DETAIL0_BASE_COLOR,                //  "detail_0_base_color" (GLTF)
        TERRAIN_DETAIL1_BASE_COLOR,                //  "detail_1_base_color" (GLTF)
        TERRAIN_DETAIL2_BASE_COLOR,                //  "detail_2_base_color" (GLTF)
        TERRAIN_DETAIL3_BASE_COLOR,                //  "detail_3_base_color" (GLTF)
        TERRAIN_DETAIL0_NORMAL,                    //  "detail_0_normal" (GLTF)
        TERRAIN_DETAIL1_NORMAL,                    //  "detail_1_normal" (GLTF)
        TERRAIN_DETAIL2_NORMAL,                    //  "detail_2_normal" (GLTF)
        TERRAIN_DETAIL3_NORMAL,                    //  "detail_3_normal" (GLTF)
        TERRAIN_DETAIL0_METALLIC_ROUGHNESS,        //  "detail_0_metallic_roughness" (GLTF)
        TERRAIN_DETAIL1_METALLIC_ROUGHNESS,        //  "detail_1_metallic_roughness" (GLTF)
        TERRAIN_DETAIL2_METALLIC_ROUGHNESS,        //  "detail_2_metallic_roughness" (GLTF)
        TERRAIN_DETAIL3_METALLIC_ROUGHNESS,        //  "detail_3_metallic_roughness" (GLTF)
        TERRAIN_DETAIL0_EMISSIVE,                  //  "detail_0_emissive" (GLTF)
        TERRAIN_DETAIL1_EMISSIVE,                  //  "detail_1_emissive" (GLTF)
        TERRAIN_DETAIL2_EMISSIVE,                  //  "detail_2_emissive" (GLTF)
        TERRAIN_DETAIL3_EMISSIVE,                  //  "detail_3_emissive" (GLTF)

        TERRAIN_BASE_COLOR_FACTORS,                //  "baseColorFactors" (GLTF)
        TERRAIN_METALLIC_FACTORS,                  //  "metallicFactors" (GLTF)
        TERRAIN_ROUGHNESS_FACTORS,                 //  "roughnessFactors" (GLTF)
        TERRAIN_EMISSIVE_COLORS,                   //  "emissiveColors" (GLTF)
        TERRAIN_MINIMUM_ALPHAS,                    //  "minimum_alphas" (GLTF)

        REGION_SCALE,                              //  "region_scale" (GLTF)

        GLTF_MINIMUM_ALPHA,                        //  "gltf_minimum_alpha" (GLTF)
        GLTF_BASECOLOR_TRANSFORM,                  //  "gltf_basecolor_transform" (GLTF)
        GLTF_EMISSIVE_COLOR,                       //  "gltf_emissive_color" (GLTF)
        GLTF_EMISSIVE_TRANSFORM,                   //  "gltf_emissive_transform" (GLTF)
        GLTF_ROUGHNESS_FACTOR,                     //  "gltf_roughness_factor" (GLTF)
        GLTF_METALLIC_FACTOR,                      //  "gltf_metallic_factor" (GLTF)
        GLTF_NORMAL_TRANSFORM,                     // "gltf_normal_transform" (GLTF)
        GLTF_MR_TRANSFORM,                         // "gltf_mr_transform" (GLTF)

        MAT_SPECULAR_COLOR,                     //  "mat_specular_color"
        MAT_ENV_INTENSITY,                      //  "mat_env_intensity"
        MAT_MINIMUM_ALPHA,                      //  "mat_minimum_alpha"
        MAT_EMISSIVE_BRIGHTNESS,                //  "mat_emissive_brightness"

        SHINY_ORIGIN,                       //  "origin"
        DISPLAY_GAMMA,                      //  "display_gamma"


        // precomputed textures
        BLEND_FACTOR,                       //  "blend_factor"

        MOISTURE_LEVEL,                     //  "moisture_level"
        DROPLET_RADIUS,                     //  "droplet_radius"
        ICE_LEVEL,                          //  "ice_level"
        RAINBOW_MAP,                        //  "rainbow_map"
        HALO_MAP,                           //  "halo_map"

        MOON_BRIGHTNESS,                    //  "moon_brightness"

        CLOUD_VARIANCE,                     //  "cloud_variance"

        REFLECTION_PROBE_AMBIANCE,          //  "reflection_probe_ambiance"
        REFLECTION_PROBE_MAX_LOD,           //  "max_probe_lod"
        REFLECTION_PROBE_STRENGTH,          //  "probe_strength"

        RES_SCALE,                          //  "resScale"
        DIRECTION,                          //  "direction"
        ZNEAR,                              //  "znear"
        ZFAR,                               //  "zfar"
        SOURCE_IDX,                         //  "sourceIdx"
        MIP_LEVEL,                          //  "mipLevel"
        ROUGHNESS,                          //  "roughness"
        U_WIDTH,                            //  "u_width"


        SUN_MOON_GLOW_FACTOR,               //  "sun_moon_glow_factor"
        SUN_UP_FACTOR,                      //  "sun_up_factor"
        MOONLIGHT_COLOR,                    //  "moonlight_color"

        DEBUG_NORMAL_DRAW_LENGTH,           //  "debug_normal_draw_length"

        TINT,                               //  "tint"
        AMBIANCE,                           //  "ambiance"
        ALPHA_SCALE,                        //  "alpha_scale"

        NORM_SCALE,                         //  "norm_scale"
        STEP_X,                             //  "stepX"
        STEP_Y,                             //  "stepY"
        BUMP_CODE,                          //  "bump_code"

        DELTA,                              //  "delta"
        DIST_FACTOR,                        //  "dist_factor"
        KERN,                               //  "kern"
        KERN_SCALE,                         //  "kern_scale"

        // Debug
        TOLERANCE,                          //  "tolerance"
        DITHER_SCALE,                       //  "dither_scale"
        DITHER_SCALE_S,                     //  "dither_scale_s"
        DITHER_SCALE_T,                     //  "dither_scale_t"

        SMAA_EDGE_TEX,                      //  "edgesTex"
        SMAA_AREA_TEX,                      //  "areaTex"
        SMAA_SEARCH_TEX,                    //  "searchTex"
        SMAA_BLEND_TEX,                     //  "blendTex"
        SMAA_PREDICATION_TEX,               //  "predicationTex"
        SMAA_RT_METRICS,                    //  "SMAA_RT_METRICS"

        // CAS
        CAS_PARAM_0,                       //  "cas_param_0"
        CAS_PARAM_1,                       //  "cas_param_1"
        OUT_SCREEN_RES,                     //  "out_screen_res"

        //Exposure and tonemapping
        DT,                                 //  "dt"
        NOISE_VEC,                          //  "noiseVec"
        DYNAMIC_EXPOSURE_PARAMS,            //  "dynamic_exposure_params"
        DYNAMIC_EXPOSURE_PARAMS2,           //  "dynamic_exposure_params2"

        EXPOSURE,                           //  "exposure"
        TONEMAP_TYPE,                       //  "tonemap_type"
        TONEMAP_MIX,                        //  "tonemap_mix"
        TONEMAP_PARAMS,                     //  "tonemap_params"
        HDRI_SPLIT_SCREEN,                  //  "hdri_split_screen"
        DIFFUSE_LUMINANCE_SCALE,            //  "diffuse_luminance_scale"

        // Alchemy-specific uniforms for Sky
        CUSTOM_ALPHA,                       //  "custom_alpha"
        METEOR_WIDTH_PIXELS,                //  "meteor_width_pixels"
        AURORA_INTENSITY,                   //  "aurora_intensity"
        AURORA_TIME,                        //  "aurora_time"

        // Alchemy Effects Stack
        FRAME_ID,                           //  "uFrameId"
        SCREEN_RESOLUTION,                  //  "uResolution"

        // Chromatic Aberration
        CA_AMOUNT,                          //  "uCAAmount"        (pre-squared × 0.02 on CPU)
        CA_FALLOFF,                         //  "uCAFalloff"       (reciprocal on CPU)
        CA_ANGLE_SIN_COS,                   //  "uCAAngleSinCos"   vec2(sin, cos) on CPU
        CA_OFFSET_R,                        //  "uCAOffsetR"
        CA_OFFSET_B,                        //  "uCAOffsetB"
        CA_ANISOTROPY,                      //  "uCAAnisotropy"

        // Lens Flare
        LENS_FLARE_STRENGTH,                //  "uLensFlareStrength"
        LENS_FLARE_SUN_POS,                 //  "uLensFlareSunPos"
        LENS_FLARE_SUN_VISIBILITY,          //  "uLensFlareSunVisibility"
        LENS_FLARE_STREAK_LENGTH,           //  "uLensFlareStreakLength"
        LENS_FLARE_STREAK_FALLOFF,          //  "uLensFlareStreakFalloff"
        LENS_FLARE_STREAK_WIDTH,            //  "uLensFlareStreakWidth"
        LENS_FLARE_STREAK_INTENSITY,         //  "uLensFlareStreakIntensity"
        LENS_FLARE_STREAK_TINT,             //  "uLensFlareStreakTint"
        LENS_FLARE_CHROMATIC_SPREAD,        //  "uLensFlareChromaticSpread"
        LENS_FLARE_GLOW_RADIUS,             //  "uLensFlareGlowRadius"
        LENS_FLARE_GLOW_FALLOFF,            //  "uLensFlareGlowFalloff"
        LENS_FLARE_GLOW,                    //  "uLensFlareGlow"
        LENS_FLARE_GHOST_COUNT,             //  "uLensFlareGhostCount"
        LENS_FLARE_GHOST_SPACING,           //  "uLensFlareGhostSpacing"
        LENS_FLARE_GHOST,                   //  "uLensFlareGhost"
        LENS_FLARE_HALO_RADIUS,             //  "uLensFlareHaloRadius"
        LENS_FLARE_HALO_WIDTH,              //  "uLensFlareHaloWidth"
        LENS_FLARE_HALO,                    //  "uLensFlareHalo"
        LENS_FLARE_OCCLUSION_RADIUS,        //  "uLensFlareOcclusionRadius"
        LENS_FLARE_STARBURST,               //  "uLensFlareStarburst"
        LENS_FLARE_STARBURST_SPIKES,        //  "uLensFlareStarburstSpikes"
        LENS_FLARE_STARBURST_SHARPNESS,     //  "uLensFlareStarburstSharpness"
        LENS_FLARE_STARBURST_FALLOFF,       //  "uLensFlareStarburstFalloff"
        LENS_FLARE_OCCLUSION_TAPS,          //  "uLensFlareOcclusionTaps"
        LENS_FLARE_LIGHT_COLOR,             //  "uLensFlareLightColor"

        // Color Correction LUT
        COLOR_GRADE_LUT,                    //  "uColorGradeLut"
        COLOR_GRADE_LUT_SIZE,               //  "uColorGradeLutSize"
        COLOR_GRADE_LUT_STRENGTH,           //  "uColorGradeLutStrength"

        // Linear-space grading (pre-tonemap) — CPU-precomputed
        COLOR_GRADE_WHITE_BALANCE_GAIN,     //  "uWhiteBalanceGain"
        COLOR_GRADE_LIFT,                   //  "uLift"
        COLOR_GRADE_INV_GAMMA_CC,           //  "uInvGammaCC"       (1 / gamma)
        COLOR_GRADE_GAIN,                   //  "uGain"

        // Split toning (tints are pre-divided by dot(tint, LUMA) on the CPU)
        SPLIT_TONE_SHADOW_RATIO,            //  "uShadowRatio"
        SPLIT_TONE_HIGHLIGHT_RATIO,         //  "uHighlightRatio"
        SPLIT_TONE_MIDTONE_RATIO,           //  "uMidtoneRatio"
        SPLIT_TONE_MIDTONE_AMOUNT,          //  "uMidtoneAmount"
        SPLIT_TONE_MID,                     //  "uSplitToneMid"     (0.5 + balance * 0.4)
        SPLIT_TONE_AMOUNT,                  //  "uToneAmount"

        // Display-space grading — all CPU-precomputed to scale/bias pairs
        COLOR_GRADE_BWP_SCALE,              //  "uBWPScale"         (1 / (white - black))
        COLOR_GRADE_BWP_BIAS,               //  "uBWPBias"          (-black * scale)
        COLOR_GRADE_BC_SCALE,               //  "uBCScale"          (contrast)
        COLOR_GRADE_BC_BIAS,                //  "uBCBias"           ((bright - 0.5) * c + 0.5)
        COLOR_GRADE_HIGHLIGHTS_SCALED,      //  "uHighlightsScaled" (highlights * 0.3)
        COLOR_GRADE_SHADOWS_SCALED,         //  "uShadowsScaled"    (shadows * 0.3)
        COLOR_GRADE_SATURATION,             //  "uSaturation"
        COLOR_GRADE_VIBRANCE,               //  "uVibrance"
        COLOR_GRADE_HUE_SHIFT_NORM,         //  "uHueShiftNorm"     (degrees / 360)

        // Per-channel filmic curves
        COLOR_GRADE_CURVE_TOE,              //  "uCurveToe"
        COLOR_GRADE_CURVE_INV_RANGE,        //  "uCurveInvRange"    (1 / (shoulder - toe))
        COLOR_GRADE_CURVE_STRENGTH,         //  "uCurveStrength"

        // Vignette
        VIGNETTE_AMOUNT,                    //  "uVignetteAmount"
        VIGNETTE_RADIUS,                    //  "uVignetteRadius"
        VIGNETTE_SOFT,                      //  "uVignetteSoft"
        VIGNETTE_SHAPE,                     //  "uVignetteShape"
        VIGNETTE_COLOR,                     //  "uVignetteColor"
        VIGNETTE_MID_COLOR,                 //  "uVignetteMidColor"
        VIGNETTE_MID_POINT,                 //  "uVignetteMidPoint"
        VIGNETTE_CENTER,                    //  "uVignetteCenter"
        VIGNETTE_ASPECT,                    //  "uVignetteAspect"
        VIGNETTE_FEATHER,                   //  "uVignetteFeather"

        // CVD Compensation
        CVD_MODE,                           //  "uCompensateMode"
        CVD_AMOUNT,                         //  "uCompensateAmount"

        // Film Grain
        GRAIN_AMOUNT,                      //  "uGrainAmount"
        GRAIN_STYLE,                       //  "uGrainStyle"
        GRAIN_SIZE,                        //  "uGrainSize"
        GRAIN_RANGE,                       //  "uGrainRange"
        GRAIN_TINT,                        //  "uGrainTint"
        GRAIN_ANIMATE,                     //  "uGrainAnimate"

        // Dithering
        DITHER_AMOUNT,                      //  "uDitherAmount"
        DITHER_BITS,                        //  "uDitherBits"
        DITHER_ANIMATE,                     //  "uDitherAnimate"

        // Previews
        PREVIEW_MODE,                       //  "uPreviewMode"

        // End Alchemy Effects Stack
        TEXT_SHADOW_MODE,                   //  "textShadowMode"


        END_RESERVED_UNIFORMS
    } eGLSLReservedUniforms;
    // clang-format on

    // singleton pattern implementation
    static LLShaderMgr * instance();

    virtual void initAttribsAndUniforms(void);

    bool attachShaderFeatures(LLGLSLShader * shader);
    void dumpObjectLog(GLuint ret, bool warns = true, const std::string& filename = "");
    void dumpShaderSource(U32 shader_code_count, GLchar** shader_code_text);
    bool    linkProgramObject(GLuint obj, bool suppress_errors = false, const std::string& shader_name = "unknown");
    bool    validateProgramObject(GLuint obj);
    // `cache_key` overrides the map key the compiled object is stored under; empty means the
    // path. Shared objects are compiled once and attached by name, so a source that keys on a
    // compile-time variant axis is compiled once per axis value under distinct keys.
    GLuint loadShaderFile(const std::string& filename, S32 & shader_level, GLenum type, std::map<std::string, std::string>* defines = NULL, S32 texture_index_channels = -1, const std::string& cache_key = std::string());

    // Suffixes marking the axis copies of a shared object. Not legal path character sequences,
    // so they cannot collide with a real file.
    static constexpr const char* CLASSIC_OBJECT_SUFFIX = "|CLASSIC_MODE";
    static constexpr const char* MIRROR_OBJECT_SUFFIX  = "|MIRROR_CLIP";

    // Which compile-time axes a shared object's own source varies on. Passed explicitly at each
    // attach so a program cannot ask for a copy that was never compiled: no shared source keys
    // on more than one axis, and a key naming an axis its file ignores has no entry.
    enum ObjectVariantAxis : U32
    {
        OBJ_AXIS_CLASSIC = 1 << 0,
        OBJ_AXIS_MIRROR  = 1 << 1,
    };

    // Key of the copy of `path` matching `shader`'s defines, considering only `axes`: a program
    // carrying CLASSIC_MODE must attach the object compiled with it, since the define cannot
    // reach an object that was compiled once for everyone. Falls back to the plain path when no
    // copy was compiled for the axis, which is how a source that does not vary at the class
    // level in use costs one object rather than two -- see the definition. `stage` picks which
    // object map to look in and must match the attach the result is passed to.
    std::string variantObjectKey(const std::string& path, U32 axes, const LLGLSLShader* shader, GLenum stage) const;

    // Implemented in the application to actually point to the shader directory.
    virtual std::string getShaderDirPrefix(void) = 0; // Pure Virtual

    // Implemented in the application to actually update out of date uniforms for a particular shader
    virtual void updateShaderUniforms(LLGLSLShader * shader) = 0; // Pure Virtual

    void initShaderCache(bool enabled, const LLUUID& old_cache_version, const LLUUID& current_cache_version, bool second_instance);
    void clearShaderCache();
    void persistShaderCacheMetadata();

    bool loadCachedProgramBinary(LLGLSLShader* shader);
    bool saveCachedProgramBinary(LLGLSLShader* shader);

public:
    // Map of shader names to compiled
    std::map<std::string, GLuint> mVertexShaderObjects;
    std::map<std::string, GLuint> mFragmentShaderObjects;

    //global (reserved slot) shader parameters
    std::vector<std::string> mReservedAttribs;

    std::vector<std::string> mReservedUniforms;

    struct ProgramBinaryData
    {
        GLsizei mBinaryLength = 0;
        GLenum mBinaryFormat = 0;
        F32 mLastUsedTime = 0.0;
    };
    std::map<LLUUID, ProgramBinaryData> mShaderBinaryCache;
    LLUUID mShaderCacheVersion;
    bool mShaderCacheEnabled = false;
    std::string mShaderCacheDir;

protected:

    // our parameter manager singleton instance
    static LLShaderMgr * sInstance;

}; //LLShaderMgr

#endif
