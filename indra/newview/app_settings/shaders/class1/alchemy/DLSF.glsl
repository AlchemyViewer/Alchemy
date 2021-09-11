/** 
 * @file DLSF.glsl
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
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#extension GL_ARB_shader_texture_lod : enable
#extension GL_EXT_gpu_shader4 : enable

/*[EXTRA_CODE_HERE]*/

#ifdef DEFINE_GL_FRAGCOLOR
out vec4 frag_color;
#else
#define frag_color gl_FragColor
#endif

VARYING vec2 vary_fragcoord;

uniform sampler2D tex0;

uniform vec3 sharpen_params;

// highlight fall-off start (prevents halos and noise in bright areas)
#define kHighBlock 0.65
// offset reducing sharpening in the shadows
#define kLowBlock (1.0 / 256.0)
#define kSharpnessMin (-1.0 / 14.0)
#define kSharpnessMax (-1.0 / 6.5)
#define kDenoiseMin (0.001)
#define kDenoiseMax (-0.1)

vec3 linear_to_srgb(vec3 cl);
vec3 srgb_to_linear(vec3 cl);

float GetLuma(vec4 p)
{
    return 0.212655 * p.r + 0.715158 * p.g + 0.072187 * p.b;
}

float Square(float v)
{
    return v * v;
}

void main()
{
    /**
    Image sharpening filter from GeForce Experience. Provided by NVIDIA Corporation.
    
    Copyright 2019 Suketu J. Shah. All rights reserved.
    Redistribution and use in source and binary forms, with or without modification, are permitted provided
    that the following conditions are met:
        1. Redistributions of source code must retain the above copyright notice, this list of conditions
        and the following disclaimer.
        2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
        and the following disclaimer in the documentation and/or other materials provided with the distribution.
        3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse
        or promote products derived from this software without specific prior written permission.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
    TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
    ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
    */

    vec4 x = texture2DLod(tex0, vary_fragcoord, 0.f);

    vec4 a = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2(-1,  0));
    vec4 b = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2( 1,  0));
    vec4 c = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2( 0,  1));
    vec4 d = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2( 0, -1));

    vec4 e = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2(-1, -1));
    vec4 f = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2( 1,  1));
    vec4 g = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2(-1,  1));
    vec4 h = texture2DLodOffset(tex0, vary_fragcoord, 0.0, ivec2( 1, -1));

    float lx = GetLuma(x);

    float la = GetLuma(a);
    float lb = GetLuma(b);
    float lc = GetLuma(c);
    float ld = GetLuma(d);

    float le = GetLuma(e);
    float lf = GetLuma(f);
    float lg = GetLuma(g);
    float lh = GetLuma(h);

    // cross min/max
    float ncmin = min(min(le, lf), min(lg, lh));
    float ncmax = max(max(le, lf), max(lg, lh));

    // plus min/max
    float npmin = min(min(min(la, lb), min(lc, ld)), lx);
    float npmax = max(max(max(la, lb), max(lc, ld)), lx);

    // compute "soft" local dynamic range -- average of 3x3 and plus shape
    float lmin = 0.5 * min(ncmin, npmin) + 0.5 * npmin;
    float lmax = 0.5 * max(ncmax, npmax) + 0.5 * npmax;

    // compute local contrast enhancement kernel
    float lw = lmin / (lmax + kLowBlock);
    float hw = Square(1.0 - Square(max(lmax - kHighBlock, 0.0) / ((1.0 - kHighBlock))));

    // noise suppression
    // Note: Ensure that the denoiseFactor is in the range of (10, 1000) on the CPU-side prior to launching this shader.
    // For example, you can do so by adding these lines
    //      const float kDenoiseMin = 0.001f;
    //      const float kDenoiseMax = 0.1f;
    //      float kernelDenoise = 1.0 / (kDenoiseMin + (kDenoiseMax - kDenoiseMin) * min(max(denoise, 0.0), 1.0));
    // where kernelDenoise is the value to be passed in to this shader (the amount of noise suppression is inversely proportional to this value),
    //       denoise is the value chosen by the user, in the range (0, 1)
	float kernelDenoise = 1.0 / (kDenoiseMin + (kDenoiseMax - kDenoiseMin) * sharpen_params.y);
    float nw = Square((lmax - lmin) * kernelDenoise);

    // pick conservative boost
    float boost = min(min(lw, hw), nw);

    // run variable-sigma 3x3 sharpening convolution
    // Note: Ensure that the sharpenFactor is in the range of (-1.0/14.0, -1.0/6.5f) on the CPU-side prior to launching this shader.
    // For example, you can do so by adding these lines
    //      const float kSharpnessMin = -1.0 / 14.0;
    //      const float kSharpnessMax = -1.0 / 6.5f;
    //      float kernelSharpness = kSharpnessMin + (kSharpnessMax - kSharpnessMin) * min(max(sharpen, 0.0), 1.0);
    // where kernelSharpness is the value to be passed in to this shader,
    //       sharpen is the value chosen by the user, in the range (0, 1)
    float kernelSharpness = kSharpnessMin + (kSharpnessMax - kSharpnessMin) * sharpen_params.x;
    float k = boost * kernelSharpness;

    float accum = lx;
    accum += la * k;
    accum += lb * k;
    accum += lc * k;
    accum += ld * k;
    accum += le * (k * 0.5);
    accum += lf * (k * 0.5);
    accum += lg * (k * 0.5);
    accum += lh * (k * 0.5);

    // normalize (divide the accumulator by the sum of convolution weights)
    accum /= 1.0 + 6.0 * k;

    // accumulator is in linear light space            
    float delta = accum - lx;
    x.x += delta;
    x.y += delta;
    x.z += delta;

    x.rgb = linear_to_srgb(x.rgb);

    frag_color = x;
}

