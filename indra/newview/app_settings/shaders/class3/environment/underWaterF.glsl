/**
 * @file class3\environment\underWaterF.glsl
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

out vec4 frag_color;

uniform sampler2D bumpMap;
// 1 where the surface is drawn distorted, 0 where an exclusion prim covers it (the stencil's exclude bit).
uniform float exclusionDistort;

#ifdef TRANSPARENT_WATER
uniform sampler2D screenTex;
#endif

uniform vec4 fogCol;
uniform vec3 lightDir;
uniform float lightExp;
uniform vec2 fbScale;
uniform float refScale;
uniform float znear;
uniform float zfar;
uniform float kd;
uniform vec4 waterPlane;
uniform vec3 eyeVec;
// Shared per-frame sky/water constants, spliced from class1/deferred/environmentBlock.glsl
// and bound at UB_ENVIRONMENT. Members are read by bare name.
//[ENGINE_BLOCK Environment]

in vec3 refCoord;
in vec3 view;
in vec3 vary_position;

vec4 applyWaterFogViewLinearNoClip(vec3 pos, vec4 color);
void mirrorClip(vec3 position);
void water_wave_coords(vec2 r, out vec2 big, out vec4 little);

void main()
{
    mirrorClip(vary_position);
    vec2 screen_tc = (refCoord.xy/refCoord.z) * 0.5 + 0.5;

    vec4 color;

    //get detail normals
    vec2 bigwave;
    vec4 little;
    water_wave_coords(view.xy, bigwave, little);
    vec3 wave1 = texture(bumpMap, bigwave).xyz*2.0-1.0;
    vec3 wave2 = texture(bumpMap, little.xy).xyz*2.0-1.0;
    vec3 wave3 = texture(bumpMap, little.zw).xyz*2.0-1.0;
    vec3 wavef = normalize(wave1+wave2+wave3);

    //figure out distortion vector (ripply)
    vec2 distort = screen_tc;
    distort = mix(distort, distort+wavef.xy*refScale, exclusionDistort);

#ifdef TRANSPARENT_WATER
    vec4 fb = texture(screenTex, distort);
#else
    vec4 fb = vec4(waterFogColorLinear, 0.0);
#endif

    fb = applyWaterFogViewLinearNoClip(vary_position, fb);

    frag_color = max(fb, vec4(0));
}
