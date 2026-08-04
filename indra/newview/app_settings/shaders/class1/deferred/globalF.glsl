/**
 * @file class1/deferred/globalF.glsl
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


 // Global helper functions included in every fragment shader
 // DO NOT declare sampler uniforms here as OS X doesn't compile
 // them out

// clipPlane stays a runtime uniform: the MIRROR_CLIP copy reads it for the actual clip, and it
// is also consumed independently of the mirror pass by the hero-probe tap in reflectionProbeF.
uniform vec4 clipPlane;
uniform float clipSign;

// Clips geometry behind the mirror plane, and only ever applies during the hero-probe mirror
// pass. That makes it a compile-time variant rather than a runtime test: the base copy of this
// object compiles the clip -- and its clipPlane read -- away entirely, and the pass binds the
// MIRROR_CLIP copy instead. See LLGLSLShader::VARIANT_MIRROR.
void mirrorClip(vec3 pos)
{
#ifdef MIRROR_CLIP
    if ((dot(pos.xyz, clipPlane.xyz) + clipPlane.w) < 0.0)
    {
        discard;
    }
#endif
}

 // Octahedron normal vector encoding
 // https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
 //

vec2 OctWrap( vec2 v )
{
    return ( 1.0 - abs( v.yx ) ) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec4 encodeNormal(vec3 n, float env, float gbuffer_flag)
{
    n /= ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
    n.xy = n.z >= 0.0 ? n.xy : OctWrap( n.xy );
    n.xy = n.xy * 0.5 + 0.5;
    return vec4(n.xy, env, gbuffer_flag);
}

vec4 decodeNormal(vec4 norm)
{
    vec2 f = norm.xy;
    f = f * 2.0 - 1.0;

    // https://twitter.com/Stubbesaurus/status/937994790553227264
    vec4 n;
    n.xyz = vec3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
    float t = clamp( -n.z , 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    n.xyz = normalize(n.xyz);
    return n;
}

// Interleaved gradient noise (Jimenez 2014). Screen-stable: no temporal term, so the
// pattern does not shimmer frame to frame.
float screenNoise(vec2 frag_px)
{
    return fract(52.9829189 * fract(dot(frag_px, vec2(0.06711056, 0.00583715))));
}

// Break quantization banding at the emissive store. R11F_G11F_B10F keeps 6/6/5 explicit
// mantissa bits, so a value quantizes at roughly 2^-7/2^-7/2^-6 of its own magnitude --
// coarse enough to band in the sky writers' smooth gradients, and coarsest in blue, which is
// exactly where a sky lives. Adding ~1 ulp of screen-stable noise before the ROP rounds
// trades the bands for imperceptible grain. Multiplicative, so black stays black and the
// amplitude tracks the format's scale-relative precision; on the non-HDR GL_RGB8 emissive
// it lands near a single 8-bit step, harmless.
vec3 ditherEmissive(vec3 v, vec2 frag_px)
{
    float n = screenNoise(frag_px) - 0.5;
    return v * (1.0 + n * vec3(1.0 / 64.0, 1.0 / 64.0, 1.0 / 32.0));
}

