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

vec2 octEncode(vec3 n)
{
    n /= ( abs( n.x ) + abs( n.y ) + abs( n.z ) );
    n.xy = n.z >= 0.0 ? n.xy : OctWrap( n.xy );
    return n.xy * 0.5 + 0.5;
}

vec3 octDecode(vec2 f)
{
    f = f * 2.0 - 1.0;
    vec3 n = vec3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
    float t = clamp( -n.z , 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

// The blue channel of the normal attachment is a union, discriminated by the gbuffer flag that
// sits beside it: envIntensity for a legacy fragment, a packed geometric normal for a PBR one.
// PBR fragments never read envIntensity -- every consumer of it is behind a non-PBR branch -- so
// the channel was dead weight on exactly the surfaces that want a geometric normal.
//
// Bit budget comes from the attachment format, which is why it is a compile-time decision rather
// than a runtime one. GL_RGBA16 affords 8:8; GL_RGB10_A2 affords 5:5, about six degrees, which is
// coarse for shading but ample for the horizon and occlusion tests that consume this.
#ifdef GBUFFER_NORM_HDR
#define GEO_OCT_LEVELS 255.0
#define GEO_OCT_RANGE  65535.0
#else
#define GEO_OCT_LEVELS 31.0
#define GEO_OCT_RANGE  1023.0
#endif

float packGeoNormal(vec3 g)
{
    vec2 o = clamp(octEncode(g), 0.0, 1.0);
    vec2 q = floor(o * GEO_OCT_LEVELS + 0.5);
    return ((q.x * (GEO_OCT_LEVELS + 1.0)) + q.y) / GEO_OCT_RANGE;
}

vec3 unpackGeoNormal(float v)
{
    float f = v * GEO_OCT_RANGE;
    float x = floor(f / (GEO_OCT_LEVELS + 1.0));
    float y = f - x * (GEO_OCT_LEVELS + 1.0);
    return octDecode(vec2(x, y) / GEO_OCT_LEVELS);
}

// Roughness across two 8-bit channels, for the PBR half of the ORM attachment.
//
// A single UNORM8 channel gives roughness 1/255 steps, and the GGX lobe width goes as roughness
// squared, so a step costs 2*(1/255)/roughness of the lobe -- 17% at the MIN_PBR_ROUGHNESS floor,
// where highlights are narrowest and a jump in width is most visible. It also swallowed the finer
// half of what filterSpecularRoughness computes, since those corrections land below a step.
//
// The room is already there: alpha of that attachment carries legacy glossiness, and the three
// PBR writers all store a literal zero in it. Same union as the geometric normal, discriminated
// by the same flag, and legacy never sets that flag so its glossiness is untouched.
//
// Split so both halves are naturally in [0,1] and the hardware's own UNORM rounding is what the
// decode undoes. The coarse half is already an exact multiple of 1/255 and survives storage
// unchanged; the fine half subdivides one of those steps, so the residual error is half a step of
// the fine channel -- about 8e-6 of roughness.
vec2 packRoughness(float r)
{
    r = clamp(r, 0.0, 1.0);
    float hi = floor(r * 255.0) / 255.0;
    return vec2(hi, (r - hi) * 255.0);
}

float unpackRoughness(vec2 p)
{
    return p.x + p.y / 255.0;
}

// Assemble the PBR ORM texel: occlusion and metallic keep their channels, roughness takes green
// and alpha. Writers should use this rather than composing frag_data[1] by hand, so the split
// stays in one place.
vec4 packORM(vec3 orm)
{
    vec2 r = packRoughness(orm.g);
    return vec4(orm.r, r.x, orm.b, r.y);
}

// Legacy writers: blue carries environment intensity.
vec4 encodeNormal(vec3 n, float env, float gbuffer_flag)
{
    return vec4(octEncode(n), env, gbuffer_flag);
}

// PBR writers: blue carries the geometric normal instead. Pass the interpolated vertex normal,
// before any normal map has perturbed it -- the point of keeping it is to know where the real
// surface faces when the shading normal no longer says.
vec4 encodeNormalGeo(vec3 n, vec3 geometric_normal, float gbuffer_flag)
{
    return vec4(octEncode(n), packGeoNormal(geometric_normal), gbuffer_flag);
}

vec4 decodeNormal(vec4 norm)
{
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    return vec4(octDecode(norm.xy), 0.0);
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


// Fold the sub-pixel normal variance a minified normal map has lost back into roughness
// (Kaplanyan et al. 2016, "Filtering Distributions of Normals for Shading Antialiasing"; this
// is the screen-space form Filament ships).
//
// A normal map at distance packs many normals into one pixel. Averaging them shortens the
// normal but the shading still treats it as a single direction with the material's authored
// roughness, so a specular lobe that should have been widened by the spread stays narrow and
// snaps between pixels as the camera moves. That is the crawling on distant normal-mapped
// surfaces, and it is worst on the smooth materials whose lobe is narrowest to begin with.
//
// Screen-space derivatives of the shading normal measure that spread per pixel, for free,
// after the map has already been filtered by the hardware. Converting it to an equivalent
// roughness widens the lobe by exactly the amount that pixel lost -- so the fix is proportional
// to the error rather than a constant floor applied to every surface at every distance.
//
// Variance is added in alpha (roughness squared) space, which is where a Gaussian lobe width
// composes linearly. Returns perceptual roughness, the space everything else here works in.
//
// Derivatives, so uniform control flow only.
float filterSpecularRoughness(float perceptualRoughness, vec3 n)
{
    // Filament's defaults: how much of the measured variance to trust, and a ceiling so a
    // silhouette or a normal-map discontinuity cannot drive a surface to fully rough.
    const float SPEC_AA_VARIANCE  = 0.15;
    const float SPEC_AA_THRESHOLD = 0.25;

    vec3 du = dFdx(n);
    vec3 dv = dFdy(n);

    float variance = SPEC_AA_VARIANCE * (dot(du, du) + dot(dv, dv));
    float kernel   = min(2.0 * variance, SPEC_AA_THRESHOLD);

    float alpha = perceptualRoughness * perceptualRoughness;
    return sqrt(sqrt(clamp(alpha * alpha + kernel, 0.0, 1.0)));
}
