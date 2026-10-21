/**
 * @file avatarSkinV.glsl
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


in vec4 weight;

// Three rows of LL_CHARACTER_MAX_JOINTS_PER_MESH (15) joints, row r of joint k at 15 * r + k
// (LLViewerJointMesh::uploadJointMatrices).
uniform vec4 matrixPalette[45];

mat4 getSkinnedTransform()
{
    mat4 ret;
    // weight.x is joint + blend toward the next joint. A vertex bound wholly to the last
    // joint carries exactly 14.0 (the upper body has hundreds), and its "next" joint is
    // past the end of a row -- for the last row, past the end of the array -- so both
    // indices stay in the row. Its blend is 0, but mix still reads the second operand,
    // and an out-of-range uniform read can come back NaN.
    int   i = clamp(int(floor(weight.x)), 0, 14);
    int   j = min(i + 1, 14);
    float x = fract(weight.x);

    ret[0] = mix(matrixPalette[i+0], matrixPalette[j+0], x);
    ret[1] = mix(matrixPalette[i+15],matrixPalette[j+15], x);
    ret[2] = mix(matrixPalette[i+30],matrixPalette[j+30], x);
    ret[3] = vec4(0,0,0,1);

    return ret;
}

// A skinned normal: through the cofactor of the transform's rotation and scale, whose rows are trans[0..2].xyz, its
// inverse transpose times its determinant, so a joint a shape scales unevenly tilts the normal as it tilts the
// surface. Not unit length.
vec3 getSkinnedNormal(mat4 trans, vec3 normal)
{
    vec3 r0 = trans[0].xyz;
    vec3 r1 = trans[1].xyz;
    vec3 r2 = trans[2].xyz;
    return vec3(dot(cross(r1, r2), normal), dot(cross(r2, r0), normal), dot(cross(r0, r1), normal));
}

in vec4 clothing;

uniform vec4 gWindDir;
uniform vec4 gSinWaveParams;
uniform vec4 gGravity;
// The main camera's eye-space depth as a function of this pass's eye space: (0, 0, 1, 0) in the main view, and in a
// shadow or capture pass the row that carries its eye space to the main camera's.
uniform vec4 gClothDepthRow;

const vec4 gMinMaxConstants = vec4(1.0, 0.166666, 0.0083143, .00018542);     // #minimax-generated coefficients
const vec4 gPiConstants = vec4(0.159154943, 6.28318530, 3.141592653, 1.5707963); // # {1/2PI, 2PI, PI, PI/2}

// RenderAvatarCloth: pos_in through the skinned transform, rippled by the wind and pulled down as far as its
// clothing weight lets it, and norm (skinned and normalised) turned by the ripple. Every program that draws a
// body takes it under AVATAR_CLOTH, the deferred pass and the shadow passes casting it alike, and
// LLDrawPoolAvatar sets the four uniforms for each avatar in the eye space it skins into.
vec4 getClothedPosition(mat4 trans, vec4 pos_in, inout vec3 norm)
{
    vec4 pos;

    //wind
    vec4 windEffect;
    windEffect = vec4(dot(norm, gWindDir.xyz));
    // The ripple's phase runs along the main camera's depth whatever eye space this pass skins into, so a shadow
    // casts the cloth as the camera draws it.
    vec4 eye_pos = vec4(dot(trans[0], pos_in), dot(trans[1], pos_in), dot(trans[2], pos_in), 1.0);
    pos.x = dot(gClothDepthRow, eye_pos);
    windEffect.xyz = pos.x * vec3(0.015, 0.015, 0.015)
                        + windEffect.xyz;
    windEffect.w = windEffect.w * 2.0 + 1.0;                // move wind offset value to [-1, 3]
    windEffect.w = windEffect.w*gWindDir.w;                 // modulate wind strength

    windEffect.xyz = windEffect.xyz*gSinWaveParams.xyz
                        +vec3(gSinWaveParams.w);            // use sin wave params to scale and offset input

    //reduce to period of 2 PI
    vec4 temp1, temp0, temp2, offsetPos;
    temp1.xyz = windEffect.xyz * gPiConstants.x;            // change input as multiple of [0-2PI] to [0-1]
    temp0.y = mod(temp1.x,1.0);
    windEffect.x = temp0.y * gPiConstants.y;                // scale from [0,1] to [0, 2PI]
    temp1.z = temp1.z - gPiConstants.w;                     // shift normal oscillation by PI/2
    temp0.y = mod(temp1.z,1.0);

    windEffect.z = temp0.y * gPiConstants.y;                // scale from [0,1] to [0, 2PI]
    windEffect.xyz = windEffect.xyz + vec3(-3.141592);      // offset to [-PI, PI]

    //calculate sinusoid
    vec4 sinWave;
    temp1 = windEffect*windEffect;
    sinWave = -temp1 * gMinMaxConstants.w
                + vec4(gMinMaxConstants.z);                 // y = -(x^2)/7! + 1/5!
    sinWave = sinWave * -temp1 + vec4(gMinMaxConstants.y);  // y = -(x^2) * (-(x^2)/7! + 1/5!) + 1/3!
    sinWave = sinWave * -temp1 + vec4(gMinMaxConstants.x);  // y = -(x^2) * (-(x^2) * (-(x^2)/7! + 1/5!) + 1/3!) + 1
    sinWave = sinWave * windEffect;                         // y = x * (-(x^2) * (-(x^2) * (-(x^2)/7! + 1/5!) + 1/3!) + 1)

    // sinWave.x holds sin(norm . wind_direction) with primary frequency
    // sinWave.y holds sin(norm . wind_direction) with secondary frequency
    // sinWave.z hold cos(norm . wind_direction) with primary frequency
    sinWave.xyz = sinWave.xyz * gWindDir.w
                + vec3(windEffect.w);                       // multiply by wind strength in gWindDir.w [-wind, wind]

    // add normal facing bias offset [-wind,wind] -> [-wind - .25, wind + 1]
    temp1 = vec4(dot(norm, gGravity.xyz));                  // how much is this normal facing in direction of gGravity?
    temp1 = min(temp1, vec4(0.2,0.0,0.0,0.0));              // clamp [-1, 1] to [-1, 0.2]
    temp1 = temp1*vec4(1.5,0.0,0.0,0.0);                    // scale from [-1,0.2] to [-1.5, 0.3]
    sinWave.x = sinWave.x + temp1.x;                        // add gGravity effect to sinwave (only primary frequency)
    sinWave.xyz = sinWave.xyz * clothing.w;                 // modulate by clothing coverage

    sinWave.xyz = max(sinWave.xyz, vec3(-1.0, -1.0, -1.0)); // clamp to underlying body shape
    offsetPos = clothing * sinWave.x;                       // multiply wind effect times clothing displacement
    temp2 = gWindDir*sinWave.z + vec4(norm,0);              // calculate normal offset due to wind oscillation
    offsetPos = vec4(1.0,1.0,1.0,0.0)*offsetPos+pos_in;     // add to offset vertex position, and zero out effect from w
    norm += temp2.xyz*2.0;                                  // add sin wave effect on normals (exaggerated)

    //renormalize normal (again)
    norm = normalize(norm);

    pos.x = dot(trans[0], offsetPos);
    pos.y = dot(trans[1], offsetPos);
    pos.z = dot(trans[2], offsetPos);
    pos.w = 1.0;

    return pos;
}
