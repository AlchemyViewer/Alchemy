/**
 * @file dofCoFUtilF.glsl
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

// Depth of field's circle of confusion, worked from the scene's depth wherever a pass needs it: the gather at its
// centre and at each tap whose own blur it compares, and the combine at its pixel. Point sampled with the mirrored
// repeat the gather reads the scene through, so a tap past the frame's edge reads the depth under the colour it reads.

uniform sampler2D depthMap;

uniform float focal_distance;
uniform float blur_constant;
uniform float tan_pixel_angle;
uniform float magnification;
uniform float max_cof;

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

float ndcZFromScreenDepth(float d);   // deferredUtil.glsl -- depth-convention aware

// The signed circle of confusion at tc, in pixels of the screen: positive in front of the focal plane, negative behind
// it, and max_cof across at the most.
float dofCoF(vec2 tc)
{
    float z = ndcZFromScreenDepth(texture(depthMap, tc).r);
    vec4 ndc = vec4(0.0, 0.0, z, 1.0);
    vec4 p = inv_proj*ndc;

    // (depth - focal_distance) / -depth, for the eye-space depth p.z/max(p.w, ...), in one divide.
    float defocus = focal_distance*max(p.w, 0.000001)/p.z - 1.0; // deferredUtil's RECONSTRUCT_W_FLOOR

    // The lens's scale, the same for every pixel: blur_constant / magnification / pixel_length * 1.414, where
    // tan_pixel_angle = pixel_length/-depth at the focal distance.
    float pixel_length = tan_pixel_angle*-focal_distance;
    float sc = defocus*(blur_constant*1.414/(magnification*pixel_length));

    return clamp(sc, -max_cof, max_cof);
}
