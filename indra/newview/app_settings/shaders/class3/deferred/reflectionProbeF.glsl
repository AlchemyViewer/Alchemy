/**
 * @file class3/deferred/reflectionProbeF.glsl
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#define FLT_MAX 3.402823466e+38

#if defined(SSR)
float tapScreenSpaceReflection(int totalSamples, vec2 tc, vec3 viewPos, vec3 n, vec3 rayDir, inout vec4 collectedColor, sampler2D source, float glossiness);
float ssrMinGlossiness();
#endif

uniform samplerCubeArray   reflectionProbes;

// Second-order SH irradiance, nine RGB coefficients per probe packed as halves into a row of
// four RGBA32UI texels (class1/interface/shPackUtilF.glsl), one row per cubemap layer. See
// class1/interface/shProjectF.glsl -- irradiance is band-limited to these nine numbers, so the
// cubemap this replaces was storing a nine-degree-of-freedom function in sixteen thousand texels.
uniform usampler2D         shCoeffs;

uniform sampler2D sceneMap;
uniform int cube_snapshot;
uniform float max_probe_lod;

uniform bool transparent_surface;

// Classic (legacy pre-PBR) sky lighting is a per-program compile-time variant, not a runtime
// uniform: the two paths differ by whole blocks of maths and a probe sample, and only one of
// them is ever live for a given sky. A macro rather than a const global -- these sources are
// separately compiled units linked into one program, and several of them declare this.
#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif

// Shared reflection-probe + SSR constants, spliced from
// class1/deferred/reflectionProbesBlock.glsl and bound at UB_REFLECTION_PROBES.
//[ENGINE_BLOCK ReflectionProbes]

// Inputs
uniform mat3 env_mat;

vec3 getSpecularDominantDir(vec3 n, vec3 r, float perceptualRoughness);
float horizonOcclusion(vec3 r, vec3 geometricNormal);

bool isAbove(vec3 pos, vec4 plane)
{
    return (dot(plane.xyz, pos) + plane.w) > 0;
}

// Whether probe `i` holds `pos`. A box holds what lies within it, `box_pos` the point in its unit cube, and shuts the
// automatic probes out once it does (`boxed`); a sphere holds what lies within its radius, an automatic one only while
// `automatic`, before any box has.
bool probeHolds(int i, vec3 pos, bool automatic, out bool boxed, out vec3 box_pos)
{
    boxed = false;
    box_pos = vec3(0);
    if (refIndex[i].w < 0)
    {
        vec4 v = refBox[i] * vec4(pos, 1.0);
        if (abs(v.x) > 1 ||
            abs(v.y) > 1 ||
            abs(v.z) > 1)
        {
            return false;
        }

        boxed = true;
        box_pos = v.xyz;
        return true;
    }

    if (refIndex[i].w == 0 && !automatic)
    {
        return false;
    }

    vec3 delta = pos.xyz - refSphere[i].xyz;
    float d = dot(delta, delta);
    float r2 = refSphere[i].w;
    r2 *= r2;

    // outside bounding sphere
    return !(d > r2);
}

int getStartIndex(vec3 pos)
{
#if 1
    int idx = clamp(int(floor(-pos.z)), 0, 255);
    return clamp(refBucket[idx].x, 1, refmapCount+1);
#else
    return 1;
#endif
}

// from https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-sphere-intersection

// original reference implementation:
/*
bool intersect(const Ray &ray) const
{
        float t0, t1; // solutions for t if the ray intersects
#if 0
        // geometric solution
        Vec3f L = center - orig;
        float tca = L.dotProduct(dir);
        // if (tca < 0) return false;
        float d2 = L.dotProduct(L) - tca * tca;
        if (d2 > radius2) return false;
        float thc = sqrt(radius2 - d2);
        t0 = tca - thc;
        t1 = tca + thc;
#else
        // analytic solution
        Vec3f L = orig - center;
        float a = dir.dotProduct(dir);
        float b = 2 * dir.dotProduct(L);
        float c = L.dotProduct(L) - radius2;
        if (!solveQuadratic(a, b, c, t0, t1)) return false;
#endif
        if (t0 > t1) std::swap(t0, t1);

        if (t0 < 0) {
            t0 = t1; // if t0 is negative, let's use t1 instead
            if (t0 < 0) return false; // both t0 and t1 are negative
        }

        t = t0;

        return true;
} */

// adapted -- assume that origin is inside sphere, return intersection of ray with edge of sphere.
// An origin outside it has no exit point and gets center + dir: the probe sampled along the ray without parallax.
vec3 sphereIntersect(vec3 origin, vec3 dir, vec3 center, float radius2)
{
        float t0, t1; // solutions for t if the ray intersects

        vec3 L = center - origin;

        // A point outside the parallax sphere (past 4 km of an automatic probe, which only an infinite projection
        // shows) has no exit point; its closest approach samples the horizon, and water there goes dark.
        if (dot(L, L) > radius2)
        {
            return center + dir;
        }
        float tca = dot(L,dir);

        float d2 = dot(L,L) - tca * tca;

        float thc = sqrt(max(radius2 - d2, 0.0));
        t0 = tca - thc;
        t1 = tca + thc;

        vec3 v = origin + dir * t1;
        return v;
}

// debug implementation, make no assumptions about origin
void sphereIntersectDebug(vec3 origin, vec3 dir, vec3 center, float radius2, float depth, inout vec4 col)
{
    float t[2]; // solutions for t if the ray intersects

    // geometric solution
    vec3 L = center - origin;
    float tca = dot(L, dir);
    // if (tca < 0) return false;
    float d2 = dot(L, L) - tca * tca;
    if (d2 > radius2) return;
    float thc = sqrt(radius2 - d2);
    t[0] = tca - thc;
    t[1] = tca + thc;

    for (int i = 0; i < 2; ++i)
    {
        if (t[i] > 0)
        {
            if (t[i] > depth)
            {
                float w = 0.125/((t[i]-depth)*0.125 + 1.0);
                col += vec4(0, 0, w, w)*(1.0-min(col.a, 1.0));
            }
            else
            {
                float w = 0.25;
                col += vec4(w,w,0,w)*(1.0-min(col.a, 1.0));
            }
        }
    }

}

// from https://seblagarde.wordpress.com/2012/09/29/image-based-lighting-approaches-and-parallax-corrected-cubemap/
/*
vec3 DirectionWS = normalize(PositionWS - CameraWS);
vec3 ReflDirectionWS = reflect(DirectionWS, NormalWS);

// Intersection with OBB convertto unit box space
// Transform in local unit parallax cube space (scaled and rotated)
vec3 RayLS = MulMatrix( float(3x3)WorldToLocal, ReflDirectionWS);
vec3 PositionLS = MulMatrix( WorldToLocal, PositionWS);

vec3 Unitary = vec3(1.0f, 1.0f, 1.0f);
vec3 FirstPlaneIntersect  = (Unitary - PositionLS) / RayLS;
vec3 SecondPlaneIntersect = (-Unitary - PositionLS) / RayLS;
vec3 FurthestPlane = max(FirstPlaneIntersect, SecondPlaneIntersect);
float Distance = min(FurthestPlane.x, min(FurthestPlane.y, FurthestPlane.z));

// Use Distance in WS directly to recover intersection
vec3 IntersectPositionWS = PositionWS + ReflDirectionWS * Distance;
vec3 ReflDirectionWS = IntersectPositionWS - CubemapPositionWS;

return texCUBE(envMap, ReflDirectionWS);
*/

// get point of intersection with given probe's box influence volume
// origin - ray origin in clip space
// dir - ray direction in clip space
// i - probe index in refBox/refSphere
// d - distance to nearest wall in clip space
// scale - scale of box, default 1.0
// boxIntersect from `PositionLS`, origin in the box's unit cube, which probeHolds finds it at.
vec3 boxIntersectFrom(vec3 origin, vec3 PositionLS, vec3 dir, mat4 i, float scale)
{
    mat4 clipToLocal = i;

    vec3 RayLS = mat3(clipToLocal) * dir;

    // Keep the denominator off zero, sign intact.
    //
    // A ray exactly parallel to one of the box's axes zeroes that component of RayLS. On its
    // own that is fine -- the division gives an infinity, and the max/min below discard it in
    // favour of the axis that actually bounds the ray. But if the position also sits exactly on
    // that face the numerator is zero too, and 0/0 is a NaN that min carries into Distance, into
    // the intersection point, and out as a cubemap fetch direction. An axis-parallel reflection
    // is not exotic on an axis-aligned box probe in an axis-aligned room; a reflection pointing
    // straight up zeroes two components at once.
    //
    // sign() would map a zero component to zero and leave the divide in place, so the sign is
    // taken by comparison instead, treating +0 as positive -- which is the direction the
    // infinity pointed before.
    vec3 raySign = vec3(RayLS.x < 0.0 ? -1.0 : 1.0,
                        RayLS.y < 0.0 ? -1.0 : 1.0,
                        RayLS.z < 0.0 ? -1.0 : 1.0);
    vec3 rayDenom = raySign * max(abs(RayLS), vec3(1e-6));

    vec3 Unitary = vec3(scale);
    vec3 FirstPlaneIntersect  = (Unitary - PositionLS) / rayDenom;
    vec3 SecondPlaneIntersect = (-Unitary - PositionLS) / rayDenom;
    vec3 FurthestPlane = max(FirstPlaneIntersect, SecondPlaneIntersect);
    float Distance = min(FurthestPlane.x, min(FurthestPlane.y, FurthestPlane.z));

    // Use Distance in CS directly to recover intersection
    vec3 IntersectPositionCS = origin + dir * Distance;

    return IntersectPositionCS;
}

// d - boxDepth: the normalized depth of `PositionLS` inside the volume, 1 at the centre, 0 at the wall
float boxDepth(vec3 PositionLS)
{
    return 1.0-max(max(abs(PositionLS.x), abs(PositionLS.y)), abs(PositionLS.z));
}

vec3 boxIntersect(vec3 origin, vec3 dir, mat4 i, out float d, float scale)
{
    // Intersection with OBB convert to unit box space
    // Transform in local unit parallax cube space (scaled and rotated)
    vec3 PositionLS = (i * vec4(origin, 1.0)).xyz;

    d = boxDepth(PositionLS);
    return boxIntersectFrom(origin, PositionLS, dir, i, scale);
}

void debugBoxCol(vec3 ro, vec3 rd, float t, vec3 p, inout vec4 col)
{
    vec3 v = ro + rd * t;

    v -= ro;
    vec3 pos = p - ro;


    bool behind = dot(v,v) > dot(pos,pos);

    float w = 0.25;

    if (behind)
    {
        w *= 0.5;
        w /= (length(v)-length(pos))*0.5+1.0;
        col += vec4(0,0,w,w)*(1.0-min(col.a, 1.0));
    }
    else
    {
        col += vec4(w,w,0,w)*(1.0-min(col.a, 1.0));
    }
}

// cribbed from https://iquilezles.org/articles/intersectors/
// axis aligned box centered at the origin, with size boxSize
void boxIntersectionDebug( in vec3 ro, in vec3 p, vec3 boxSize, inout vec4 col)
{
    vec3 rd = normalize(p-ro);

    vec3 m = 1.0/rd; // can precompute if traversing a set of aligned boxes
    vec3 n = m*ro;   // can precompute if traversing a set of aligned boxes
    vec3 k = abs(m)*boxSize;
    vec3 t1 = -n - k;
    vec3 t2 = -n + k;
    float tN = max( max( t1.x, t1.y ), t1.z );
    float tF = min( min( t2.x, t2.y ), t2.z );
    if( tN>tF || tF<0.0) return ; // no intersection

    float t = tN < 0 ? tF : tN;

    debugBoxCol(ro, rd, t, p, col);

    if (tN > 0) // eye is outside box, check backside, too
    {
        debugBoxCol(ro, rd, tF, p, col);
    }
}


void boxIntersectDebug(vec3 origin, vec3 pos, mat4 i, inout vec4 col)
{
    mat4 clipToLocal = i;

    // transform into unit cube space
    origin = (clipToLocal * vec4(origin, 1.0)).xyz;
    pos = (clipToLocal * vec4(pos, 1.0)).xyz;

    boxIntersectionDebug(origin, pos, vec3(1), col);
}


// The distance-only weight of a box probe, which decides how far a manual probe takes over from
// the automatic ones.
//
//  d  - boxDepth: the normalized depth inside the volume, 1 at the centre, 0 at the wall
//  fade - the probe's fade-in weight, as sphereWeight also folds in
float boxDistanceWeight(float d, float fade)
{
    return clamp(d, 0.0, 1.0) * fade;
}

// get the weight of a sphere probe
//  pos - position to be weighted
//  dir - normal to be weighted
//  origin - center of sphere probe
//  r - radius of probe influence volume
// i - index of probe in refSphere
// dw - distance weight
float sphereWeight(vec3 pos, vec3 dir, vec3 origin, float r, vec4 i, out float dw)
{
    float r1 = r * 0.5; // 50% of radius (outer sphere to start interpolating down)
    vec3 delta = pos.xyz - origin;
    float d2 = max(length(delta), 0.001);

    float atten = 1.0 - max(d2 - r1, 0.0) / max((r - r1), 0.001);
    float w = 1.0 / d2;

    w *= i.z;

    dw = w * atten * max(r, 1.0)*4;

    w *= atten;

    return w;
}

// The weight of probe `i` at `pos` (distance and angular attenuation), and its distance-only weight `dw`: the same for
// every tap of it, its irradiance and its radiance alike.
// box_pos - pos in a box's unit cube (probeHolds)
float probeWeight(int i, vec3 pos, vec3 box_pos, out float dw)
{
    float w;
    if (refIndex[i].w < 0)
    {  // box probe
        float d = boxDepth(box_pos);

        w = max(d, 0.001);
        dw = boxDistanceWeight(d, refParams[i].z);
    }
    else
    { // sphere probe
        w = sphereWeight(pos, vec3(0), refSphere[i].xyz, refSphere[i].w, refParams[i], dw);
        // The default probe is the fallback everywhere. Past its 4 km radius, which only an infinite projection
        // shows, its weight falls to zero and below, and far water would lose its sky reflection entirely.
        if (i == 0)
        {
            w = max(w, 1e-6);
        }
    }
    return w;
}

// Where a probe's cube is sampled for a ray from `pos` along `dir`, parallax corrected: a box's walls `scale` times out,
// a manual sphere's radius, and an automatic one's 4 km, which effectively disables parallax correction for
// automatically placed probes to keep from bombing the world with obvious spheres. Water takes the default probe
// without parallax.
vec3 probeSampleDir(int i, vec3 pos, vec3 box_pos, vec3 dir, float scale, bool water)
{
    vec3 c = refSphere[i].xyz;
    vec3 v;
    if (refIndex[i].w < 0)
    {
        v = boxIntersectFrom(pos, box_pos, dir, refBox[i], scale);
    }
    else
    {
        float r = refSphere[i].w;
        float rr = r * r;

        v = (water && i == 0) ? c + dir : sphereIntersect(pos, dir, c, refIndex[i].w < 1 ? 4096.0*4096.0 : rr);
    }

    v -= c;
    return env_mat * v;
}

// Tap a reflection probe
// pos - position of pixel
// dir - direction to sample
// lod - which mip to sample (lower is higher res, sharper reflections)
// i - index of probe
vec3 probeRadiance(int i, vec3 pos, vec3 box_pos, vec3 dir, float lod, bool water)
{
    vec3 v = probeSampleDir(i, pos, box_pos, dir, 1.0, water);

    // Unnormalized on purpose: a cubemap fetch takes a direction of any length. The normalize
    // that used to sit here was assigned to a local nothing read, so it was one inverse square
    // root per probe tap -- up to REF_SAMPLE_COUNT of them per pixel, in a function every
    // deferred and forward lighting path calls -- and it was a normalize of a vector with no
    // guarantee of being non-zero.
    vec4 ret = textureLod(reflectionProbes, vec4(v.xyz, refIndex[i].x), lod) * refParams[i].y;

    return ret.rgb;
}

// unpackHalf2x16 is GLSL 4.20's; below it, the halves shPackUtilF.glsl packed by hand are read
// the same way.
#if __VERSION__ >= 420
vec2 shUnpackHalves(uint u)
{
    return unpackHalf2x16(u);
}
#else
float shHalfValue(uint h)
{
    uint sign = (h & 0x8000u) << 16;
    uint e    = (h >> 10) & 0x1Fu;
    uint m    = h & 0x3FFu;

    if (e == 0u)
    {
        // Zero or a denormal: m units of 2^-24.
        float v = float(m) * 5.9604644775390625e-8;
        return (sign != 0u) ? -v : v;
    }
    if (e == 31u)
    {
        return uintBitsToFloat(sign | 0x7F800000u | (m << 13));
    }
    return uintBitsToFloat(sign | ((e + 112u) << 23) | (m << 13));
}

vec2 shUnpackHalves(uint u)
{
    return vec2(shHalfValue(u & 0xFFFFu), shHalfValue(u >> 16));
}
#endif

// Reconstruct irradiance for a normal direction from a probe's nine coefficients.
//
// The band scaling is the clamped-cosine convolution (Ramamoorthi and Hanrahan): pi, 2pi/3 and
// pi/4 for bands 0, 1 and 2. They are divided through by pi here so the result is the
// cosine-weighted AVERAGE radiance rather than irradiance proper -- that is the quantity the
// cubemap held and the quantity pbrIbl multiplies by diffuseColor, so the units downstream are
// unchanged.
vec3 evalSHIrradiance(vec3 n, int layer)
{
    // The row's 27 halves, two to a uint, eight to a texel, in coefficient order and red, green,
    // blue within each (shPackUtilF.glsl).
    uvec4 t0 = texelFetch(shCoeffs, ivec2(0, layer), 0);
    uvec4 t1 = texelFetch(shCoeffs, ivec2(1, layer), 0);
    uvec4 t2 = texelFetch(shCoeffs, ivec2(2, layer), 0);
    uvec4 t3 = texelFetch(shCoeffs, ivec2(3, layer), 0);

    vec2 h0  = shUnpackHalves(t0.x);  // L0.r  L0.g
    vec2 h1  = shUnpackHalves(t0.y);  // L0.b  L1.r
    vec2 h2  = shUnpackHalves(t0.z);  // L1.g  L1.b
    vec2 h3  = shUnpackHalves(t0.w);  // L2.r  L2.g
    vec2 h4  = shUnpackHalves(t1.x);  // L2.b  L3.r
    vec2 h5  = shUnpackHalves(t1.y);  // L3.g  L3.b
    vec2 h6  = shUnpackHalves(t1.z);  // L4.r  L4.g
    vec2 h7  = shUnpackHalves(t1.w);  // L4.b  L5.r
    vec2 h8  = shUnpackHalves(t2.x);  // L5.g  L5.b
    vec2 h9  = shUnpackHalves(t2.y);  // L6.r  L6.g
    vec2 h10 = shUnpackHalves(t2.z);  // L6.b  L7.r
    vec2 h11 = shUnpackHalves(t2.w);  // L7.g  L7.b
    vec2 h12 = shUnpackHalves(t3.x);  // L8.r  L8.g
    vec2 h13 = shUnpackHalves(t3.y);  // L8.b  (zero)

    vec3 L0 = vec3(h0, h1.x);
    vec3 L1 = vec3(h1.y, h2);
    vec3 L2 = vec3(h3, h4.x);
    vec3 L3 = vec3(h4.y, h5);
    vec3 L4 = vec3(h6, h7.x);
    vec3 L5 = vec3(h7.y, h8);
    vec3 L6 = vec3(h9, h10.x);
    vec3 L7 = vec3(h10.y, h11);
    vec3 L8 = vec3(h12, h13.x);

    const float A0 = 1.0;        // pi   / pi
    const float A1 = 0.6666667;  // 2pi/3 / pi
    const float A2 = 0.25;       // pi/4  / pi

    vec3 e = A0 * 0.282095 * L0
           + A1 * 0.488603 * (n.y * L1 + n.z * L2 + n.x * L3)
           + A2 * (1.092548 * (n.x * n.y * L4 + n.y * n.z * L5 + n.x * n.z * L7)
                 + 0.315392 * (3.0 * n.z * n.z - 1.0) * L6
                 + 0.546274 * (n.x * n.x - n.y * n.y) * L8);

    // Truncating at band 2 rings: a small bright source (the sun, in a probe capture) produces
    // Gibbs oscillation whose negative lobe lands on the far side of the sphere. Clamping is the
    // cheap half of the fix and the only part that matters visibly -- it cannot make a surface
    // subtract light. The expensive half, windowing the coefficients at projection time, is worth
    // adding only if the residual dark lobe turns out to be visible.
    return max(e, vec3(0.0));
}

// Tap an irradiance map
// pos - position of pixel
// dir - pixel normal
// i - index of probe
vec3 probeIrradiance(int i, vec3 pos, vec3 box_pos, vec3 dir, vec3 amblit, bool water)
{
    // pad the box for a manual probe extending into automatic probe space
    vec3 v = probeSampleDir(i, pos, box_pos, dir, 3.0, water);

    // The parallax correction can land on the probe's own origin, and normalize(0) is a NaN
    // going straight into the SH evaluation and out into the frame. The sample direction is what
    // the intersection is a correction of, so it is the right thing to fall back to.
    float len2 = dot(v, v);
    vec3 sampleDir = len2 > 1e-12 ? v * inversesqrt(len2) : env_mat * dir;

    vec3 col = evalSHIrradiance(sampleDir, refIndex[i].x);

    // refParams.x is an irradiance scale that may exceed 1, so it has two regimes and must be
    // spent in exactly one of them. Above 1 it is a boost, which only the multiply can apply
    // because the mix weight saturates. At or below 1 it is a fade toward the sky's own
    // ambient, which the mix already applies -- multiplying as well puts the probe term on
    // ambiance squared while the ambient term stays linear, so a half-ambiance probe
    // contributes a quarter of its irradiance instead of half.
    col *= max(refParams[i].x, 1.0);

    col = mix(amblit, col, min(refParams[i].x, 1.0));

    return col;
}

// What a pass over the probes holding a point taps of each: its irradiance along `norm` where `ambient`, and its
// radiance along `gloss_dir` at mip `gloss_lod` where `gloss`, and along `legacy_dir` at mip 0 where `legacy`.
struct ProbeTaps
{
    vec3  norm;
    vec3  amblit;
    bool  ambient;
    vec3  gloss_dir;
    float gloss_lod;
    bool  gloss;
    vec3  legacy_dir;
    bool  legacy;
    bool  water;
};

// The taps' weighted sums, the automatic probes' (0) apart from the manual ones' (1).
struct ProbeSums
{
    vec3  ambient0, ambient1;
    vec3  gloss0, gloss1;
    vec3  legacy0, legacy1;
    float wsum0, wsum1;
    float dwsum0, dwsum1;
};

// Where pos lies in probe `i`'s unit cube, if it is a box, for a probe tapped without probeHolds asking.
vec3 probeBoxPos(int i, vec3 pos)
{
    return refIndex[i].w < 0 ? (refBox[i] * vec4(pos, 1.0)).xyz : vec3(0);
}

// Probe `i`'s taps, weighted into `s`.
void probeTap(int i, vec3 pos, vec3 box_pos, ProbeTaps t, inout ProbeSums s)
{
    float dw = 0;
    float w = probeWeight(i, pos, box_pos, dw);
    vec3 ambient = t.ambient ? probeIrradiance(i, pos, box_pos, t.norm, t.amblit, t.water) : vec3(0);
    vec3 gloss = t.gloss ? probeRadiance(i, pos, box_pos, t.gloss_dir, t.gloss_lod, t.water) : vec3(0);
    vec3 legacy = t.legacy ? probeRadiance(i, pos, box_pos, t.legacy_dir, 0.0, t.water) : vec3(0);

    if (clamp(abs(refIndex[i].w), 0, 1) == 0)
    {
        s.ambient0 += ambient*w;
        s.gloss0 += gloss*w;
        s.legacy0 += legacy*w;
        s.wsum0 += w;
        s.dwsum0 += dw;
    }
    else
    {
        s.ambient1 += ambient*w;
        s.gloss1 += gloss*w;
        s.legacy1 += legacy*w;
        s.wsum1 += w;
        s.dwsum1 += dw;
    }
}

// mix automatic and manual probes, the automatic ones' sum `col0` only while `automatic`: once a box has shut them
// out, those tapped before it give nothing
vec3 probeMix(vec3 col0, vec3 col1, ProbeSums s, bool automatic)
{
    if (!automatic)
    {
        col0 = vec3(0);
    }

    if (automatic && s.wsum0 > 0.0)
    { // some automatic probes were sampled
        col0 *= 1.0/s.wsum0;
        if (s.wsum1 > 0.0)
        { //some manual probes were sampled, mix between the two
            col1 *= 1.0/s.wsum1;
            col1 = mix(col0, col1, min(s.dwsum1, 1.0));
            col0 = vec3(0);
        }
    }
    else if (s.wsum1 > 0.0)
    {
        // manual probes were sampled but no automatic probes were
        col1 *= 1.0/s.wsum1;
        col0 = vec3(0);
    }

    return col1+col0;
}

// Taps every probe that holds `pos`, up to REF_SAMPLE_COUNT, each as it is found: the probes from the first whose
// bounding sphere can reach this depth (getStartIndex), until one lies wholly beyond it, which the list being sorted on
// the nearest depth each reaches makes the last; and once one with neighbours holds it, its neighbours, which end the
// search. A box that holds the point shuts out the automatic probes, those found before it too; the default probe 0,
// which smooths out the automatic ones, comes last while they are not shut out. Water takes no automatic probe but
// probe 0, which it always takes. Sets each tap's mix of the automatic and manual probes it took.
void sampleProbeTaps(vec3 pos, ProbeTaps t, inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv)
{
    ProbeSums s;
    s.ambient0 = vec3(0);
    s.ambient1 = vec3(0);
    s.gloss0 = vec3(0);
    s.gloss1 = vec3(0);
    s.legacy0 = vec3(0);
    s.legacy1 = vec3(0);
    s.wsum0 = 0;
    s.wsum1 = 0;
    s.dwsum0 = 0;
    s.dwsum1 = 0;

    bool automatic = !t.water;
    bool boxed = false;
    vec3 box_pos = vec3(0);
    int count = 0;

#if REFMAP_LEVEL > 0
    int i = getStartIndex(pos);
    int neighbor = -1;
    int neighbor_base = 0;
    int neighbor_count = 0;
    // TODO: make some sort of structure that reduces the number of distance checks
    while (count < REF_SAMPLE_COUNT)
    {
        int idx;
        if (neighbor < 0)
        {
            // The probes are sorted on the nearest depth their bounding sphere reaches (refParams.w),
            // which the bucket's lower bound already relies on: from the first that lies wholly
            // beyond this point on, none can hold it.
            if (i >= refmapCount || refParams[i].w > -pos.z)
            {
                break;
            }
            idx = i++;
        }
        else
        {
            // up to REF_SAMPLE_COUNT-1 neighbors (neighbor_base is an ivec4 index)
            if (neighbor >= neighbor_count)
            {
                break;
            }
            idx = refNeighbor[neighbor_base + (neighbor >> 2)][neighbor & 3];
            ++neighbor;
        }

        if (!probeHolds(idx, pos, automatic, boxed, box_pos))
        {
            continue;
        }

        // never allow automatic probes to encroach on box probes
        if (boxed)
        {
            automatic = false;
        }

        probeTap(idx, pos, box_pos, t, s);
        ++count;

        if (neighbor < 0 && refIndex[idx].y != -1)
        {
            neighbor_base = refIndex[idx].y;
            neighbor_count = refIndex[idx].z;
            neighbor = 0;
        }
    }

    if (automatic && count < REF_SAMPLE_COUNT)
    { // probe at index 0 is a special probe for smoothing out automatic probes
        probeTap(0, pos, probeBoxPos(0, pos), t, s);
        ++count;
    }
#else
    // Water takes probe 0 below.
    if (!t.water)
    {
        probeTap(0, pos, probeBoxPos(0, pos), t, s);
        ++count;
    }
#endif

    if (t.water)
    {
        // always include void probe on water
        automatic = true;
        if (count < REF_SAMPLE_COUNT)
        {
            probeTap(0, pos, probeBoxPos(0, pos), t, s);
        }
    }

    if (t.ambient)
    {
        ambenv = probeMix(s.ambient0, s.ambient1, s, automatic);
    }
    if (t.gloss)
    {
        glossenv = probeMix(s.gloss0, s.gloss1, s, automatic);
    }
    if (t.legacy)
    {
        legacyenv = probeMix(s.legacy0, s.legacy1, s, automatic);
    }
}

#if defined(HERO_PROBES)

uniform vec4 clipPlane;
uniform samplerCubeArray   heroProbes;

void tapHeroProbe(inout vec3 glossenv, vec3 pos, vec3 norm, float glossiness)
{
    // No mirror this frame: the box, sphere and cube layer still hold the last one's.
    if (heroProbeCount < 1)
    {
        return;
    }

    float clipDist = dot(pos.xyz, clipPlane.xyz) + clipPlane.w;
    float w = 0;
    float dw = 0;
    float falloffMult = 10;
    vec3 refnormpersp = reflect(pos.xyz, norm.xyz);
    if (heroShape < 1)
    {
        float d = 0;
        boxIntersect(pos, norm, heroBox, d, 1.0);

        w = max(d, 0);
    }
    else
    {
        float r = heroSphere.w;

        w = sphereWeight(pos, refnormpersp, heroSphere.xyz, r, vec4(1), dw);
    }

    clipDist = clipDist * 0.95 + 0.05;
    clipDist = clamp(clipDist * falloffMult, 0, 1);
    w = clamp(w * falloffMult * clipDist, 0, 1);
    w = mix(0, w, clamp(glossiness - 0.75, 0, 1) * 4); // We only generate a quarter of the mips for the hero probes.  Linearly interpolate between normal probes and hero probes based upon glossiness.

    // Only the glossiest quarter's mips exist (LLHeroProbeManager::generateRadiance), and a
    // weight of 0 does not keep an undefined texel out of mix, so the fetch is skipped there.
    if (w <= 0.0)
    {
        return;
    }
    glossenv = mix(glossenv, textureLod(heroProbes, vec4(env_mat * refnormpersp, 0), (1.0-glossiness)*heroMaxLod).xyz, w);
}

#else

void tapHeroProbe(inout vec3 glossenv, vec3 pos, vec3 norm, float glossiness)
{
}

#endif



// What sampleReflectionProbes taps: the irradiance along the normal, and the radiance along the GGX lobe's dominant
// direction at the mip of its roughness.
ProbeTaps pbrProbeTaps(vec3 pos, vec3 norm, float glossiness, vec3 amblit, bool water)
{
    // TODO - don't hard code lods
    float reflection_lods = max_probe_lod;

    vec3 refnormpersp = normalize(reflect(pos.xyz, norm.xyz));

    // A GGX lobe is not centred on the mirror direction -- it leans toward the normal as the
    // surface roughens. Sampling a prefiltered probe along the mirror direction therefore reads
    // from the wrong place on exactly the surfaces whose lobe is widest.
    float perceptualRoughness = 1.0 - glossiness;
    refnormpersp = getSpecularDominantDir(norm.xyz, refnormpersp, perceptualRoughness);

    ProbeTaps t;
    t.norm = norm;
    t.amblit = amblit;
    t.ambient = classic_mode == 0;
    t.gloss_dir = refnormpersp;
    t.gloss_lod = perceptualRoughness*reflection_lods;
    t.gloss = true;
    t.legacy_dir = vec3(0);
    t.legacy = false;
    t.water = water;
    return t;
}

// What sampleReflectionProbesLegacy taps: the irradiance along the normal, the radiance along the dominant direction
// where the surface is glossy, and along the mirror direction at mip 0 where it takes the environment.
ProbeTaps legacyProbeTaps(vec3 pos, vec3 norm, float glossiness, float envIntensity, vec3 amblit)
{
    float reflection_lods = max_probe_lod;

    vec3 refnormpersp = reflect(pos.xyz, norm.xyz);

    ProbeTaps t;
    t.norm = norm;
    t.amblit = amblit;
    t.ambient = classic_mode == 0;
    t.gloss = glossiness > 0.0;
    t.gloss_dir = vec3(0);
    t.gloss_lod = 0.0;
    if (t.gloss)
    {
        // (1.0 - glossiness) is this path's perceptual roughness -- it is already what selects
        // the prefiltered mip, so the lobe correction reads it the same way.
        float perceptualRoughness = 1.0 - glossiness;

        // Bend toward the normal with roughness, as the PBR path does. A prefiltered
        // probe sampled along the mirror direction fetches from slightly the wrong place on
        // exactly the surfaces whose lobe is widest, which reads as reflections sliding across
        // curvature. The legacy lookup keeps the mirror direction on purpose: legacyenv is
        // the roughness-zero case, where the bend is the identity anyway, and the screen-space
        // march wants the true reflection ray.
        t.gloss_dir = getSpecularDominantDir(norm.xyz, normalize(refnormpersp), perceptualRoughness);
        t.gloss_lod = perceptualRoughness*reflection_lods;
    }
    t.legacy = envIntensity > 0.0;
    t.legacy_dir = normalize(refnormpersp);
    t.water = false;
    return t;
}

// The screen-space reflection and the hero probe over sampleReflectionProbes' probes.
void pbrProbeFinish(inout vec3 glossenv, vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 refnormpersp)
{
#if defined(SSR)
    // Skip the march only where it would contribute nothing anyway. Asking the SSR unit for its
    // own fade threshold keeps this from drifting away from it again. A transparent surface
    // marches unfaded and is weighted by glossiness below, which is zero only at zero, so its
    // gate is there too, as the legacy path's is; the fade's threshold would switch it on at
    // full weight in one step.
    if (cube_snapshot != 1 && (transparent ? glossiness > 0.0 : glossiness > ssrMinGlossiness()))
    {
        vec4 ssr = vec4(0);
        // refnormpersp, not norm: the march takes the same dominant direction the probe tap
        // above was bent to, so the two reflections being blended below agree about where they
        // are looking.
        if (transparent)
        {
            tapScreenSpaceReflection(1, tc, pos, norm, refnormpersp, ssr, sceneMap, 1);
            ssr.a *= glossiness;
        }
        else
        {
            tapScreenSpaceReflection(1, tc, pos, norm, refnormpersp, ssr, sceneMap, glossiness);
        }


        glossenv = mix(glossenv, ssr.rgb, ssr.a);
    }
#endif

    tapHeroProbe(glossenv, pos, norm, glossiness);
}

// The screen-space reflection and the hero probe over sampleReflectionProbesLegacy's probes.
void legacyProbeFinish(inout vec3 glossenv, inout vec3 legacyenv, vec2 tc, vec3 pos, vec3 norm, float glossiness,
        bool transparent)
{
#if defined(SSR)
    // Gated the same way the PBR path is. Both consumers below blend by ssr.a, and ssr.a is
    // already zero at these glossiness values -- the march's own fade reaches zero at
    // ssrMinGlossiness(), and the transparent branch scales by glossiness directly. alphaF
    // calls this with glossiness 0, so every legacy alpha fragment in the scene was paying for
    // a full ray march whose result was then multiplied by nothing.
    if (cube_snapshot != 1 && (transparent ? glossiness > 0.0 : glossiness > ssrMinGlossiness()))
    {
        vec4 ssr = vec4(0);

        vec3 ssrDir = normalize(reflect(pos.xyz, norm.xyz));

        if (transparent)
        {
            tapScreenSpaceReflection(1, tc, pos, norm, ssrDir, ssr, sceneMap, 1);
            ssr.a *= glossiness;
        }
        else
        {
            tapScreenSpaceReflection(1, tc, pos, norm, ssrDir, ssr, sceneMap, glossiness);
        }

        glossenv = mix(glossenv, ssr.rgb, ssr.a);
        legacyenv = mix(legacyenv, ssr.rgb, ssr.a);
    }
#endif

    tapHeroProbe(glossenv, pos, norm, glossiness);
    tapHeroProbe(legacyenv, pos, norm, 1.0);
}

void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit)
{
    ProbeTaps t = pbrProbeTaps(pos, norm, glossiness, amblit, false);
    vec3 legacyenv = vec3(0);
    ambenv = amblit;
    sampleProbeTaps(pos, t, ambenv, glossenv, legacyenv);
    pbrProbeFinish(glossenv, tc, pos, norm, glossiness, transparent, t.gloss_dir);
}

void sampleReflectionProbesWater(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, vec3 amblit)
{
    // don't sample automatic probes for water
    ProbeTaps t = pbrProbeTaps(pos, norm, glossiness, amblit, true);
    vec3 legacyenv = vec3(0);
    ambenv = amblit;
    sampleProbeTaps(pos, t, ambenv, glossenv, legacyenv);
    pbrProbeFinish(glossenv, tc, pos, norm, glossiness, false, t.gloss_dir);
}

void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit)
{
    ProbeTaps t = legacyProbeTaps(pos, norm, glossiness, envIntensity, amblit);
    ambenv = amblit;
    sampleProbeTaps(pos, t, ambenv, glossenv, legacyenv);
    legacyProbeFinish(glossenv, legacyenv, tc, pos, norm, glossiness, transparent);
}

// sampleReflectionProbes where `pbr`, sampleReflectionProbesLegacy where not, in one pass over the probes: a group of
// pixels holding both kinds walks the probes once, not once for each.
void sampleReflectionProbesEither(bool pbr, inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit)
{
    ProbeTaps t = pbr ? pbrProbeTaps(pos, norm, glossiness, amblit, false)
                      : legacyProbeTaps(pos, norm, glossiness, envIntensity, amblit);
    ambenv = amblit;
    sampleProbeTaps(pos, t, ambenv, glossenv, legacyenv);
    if (pbr)
    {
        pbrProbeFinish(glossenv, tc, pos, norm, glossiness, transparent, t.gloss_dir);
    }
    else
    {
        legacyProbeFinish(glossenv, legacyenv, tc, pos, norm, glossiness, transparent);
    }
}

void debugTapRefMap(vec3 pos, vec3 dir, float depth, int i, inout vec4 col)
{
    vec3 origin = vec3(0,0,0);

    bool manual_probe = abs(refIndex[i].w) > 0;

    if (manual_probe)
    {
        if (refIndex[i].w < 0)
        {
            boxIntersectDebug(origin, pos, refBox[i], col);
        }
        else
        {
            float r = refSphere[i].w; // radius of sphere volume
            float rr = r * r; // radius squared

            float t = 0.0;

            sphereIntersectDebug(origin, dir, refSphere[i].xyz, rr, depth, col);
        }
    }
}

vec4 sampleReflectionProbesDebug(vec3 pos)
{
    vec4 col = vec4(0,0,0,0);

    vec3 dir = normalize(pos);

    float d = length(pos);

    for (int i = 1; i < refmapCount; ++i)
    {
        debugTapRefMap(pos, dir, d, i, col);
    }

#if 0 //debug getStartIndex
    col.g = float(getStartIndex(pos));

    col.g /= 255.0;
    col.rb = vec2(0);
    col.a = 1.0;
#endif

    return col;
}

void applyGlossEnv(inout vec3 color, vec3 glossenv, vec4 spec, vec3 pos, vec3 norm)
{
    glossenv *= 0.5; // fudge darker
    float fresnel = clamp(1.0+dot(normalize(pos.xyz), norm.xyz), 0.3, 1.0);
    fresnel *= fresnel;
    fresnel *= spec.a;
    glossenv *= spec.rgb*fresnel;
    glossenv *= vec3(1.0) - color; // fake energy conservation
    color.rgb += glossenv*0.5;
}

 void applyLegacyEnv(inout vec3 color, vec3 legacyenv, vec4 spec, vec3 pos, vec3 norm, float envIntensity)
 {
    vec3 reflected_color = legacyenv;
    vec3 lookAt = normalize(pos);
    float fresnel = 1.0+dot(lookAt, norm.xyz);
    fresnel *= fresnel;
    fresnel = min(fresnel+envIntensity, 1.0);
    reflected_color *= (envIntensity*fresnel);
    color = mix(color.rgb, reflected_color*0.5, envIntensity);
 }

