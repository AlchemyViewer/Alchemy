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

// Second-order SH irradiance, nine RGB coefficients per probe in a 9-wide strip, one row per
// cubemap layer. See class1/interface/shProjectF.glsl -- irradiance is band-limited to these
// nine numbers, so the cubemap this replaces was storing a nine-degree-of-freedom function in
// sixteen thousand texels.
uniform sampler2D          shCoeffs;

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

// list of probeIndexes shader will actually use after "getRefIndex" is called
// (stores refIndex/refSphere indices, NOT rerflectionProbes layer)
int probeIndex[REF_SAMPLE_COUNT];

// number of probes stored in probeIndex
int probeInfluences = 0;

vec3 getSpecularDominantDir(vec3 n, vec3 r, float perceptualRoughness);
float horizonOcclusion(vec3 r, vec3 geometricNormal);

bool isAbove(vec3 pos, vec4 plane)
{
    return (dot(plane.xyz, pos) + plane.w) > 0;
}

bool sample_automatic = true;

// return true if probe at index i influences position pos
bool shouldSampleProbe(int i, vec3 pos)
{
    if (refIndex[i].w < 0)
    {
        vec4 v = refBox[i] * vec4(pos, 1.0);
        if (abs(v.x) > 1 ||
            abs(v.y) > 1 ||
            abs(v.z) > 1)
        {
            return false;
        }

        // never allow automatic probes to encroach on box probes
        sample_automatic = false;
    }
    else
    {
        if (refIndex[i].w == 0 && !sample_automatic)
        {
            return false;
        }

        vec3 delta = pos.xyz - refSphere[i].xyz;
        float d = dot(delta, delta);
        float r2 = refSphere[i].w;
        r2 *= r2;

        if (d > r2)
        { // outside bounding sphere
            return false;
        }
    }

    return true;
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

// call before sampleRef
// populate "probeIndex" with N probe indices that influence pos where N is REF_SAMPLE_COUNT
void preProbeSample(vec3 pos)
{
    // Start the populate from empty here rather than relying on probeInfluences'
    // global initializer. That initializer runs once per invocation, so this function
    // only produces a correct list because nothing currently calls it twice -- the
    // three entry points that do are all mutually exclusive. Resetting here makes the
    // populate self-contained instead of correct by coincidence; it is a no-op on
    // every existing call path.
    probeInfluences = 0;

#if REFMAP_LEVEL > 0

    int start = getStartIndex(pos);

    // TODO: make some sort of structure that reduces the number of distance checks
    for (int i = start; i < refmapCount && probeInfluences < REF_SAMPLE_COUNT; ++i)
    {
        // found an influencing probe
        if (shouldSampleProbe(i, pos))
        {
            probeIndex[probeInfluences] = i;
            ++probeInfluences;
            if (probeInfluences == REF_SAMPLE_COUNT)
            {
                break;
            }

            int neighborIdx = refIndex[i].y;
            if (neighborIdx != -1)
            {
                int neighborCount = refIndex[i].z;

                int count = 0;
                while (count < neighborCount)
                {
                    // check up to REF_SAMPLE_COUNT-1 neighbors (neighborIdx is ivec4 index)

                    // sample refNeighbor[neighborIdx].x
                    int idx = refNeighbor[neighborIdx].x;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;
                    if (count == neighborCount)
                    {
                        break;
                    }

                    // sample refNeighbor[neighborIdx].y
                    idx = refNeighbor[neighborIdx].y;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;
                    if (count == neighborCount)
                    {
                        break;
                    }

                    // sample refNeighbor[neighborIdx].z
                    idx = refNeighbor[neighborIdx].z;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;
                    if (count == neighborCount)
                    {
                        break;
                    }

                    // sample refNeighbor[neighborIdx].w
                    idx = refNeighbor[neighborIdx].w;
                    if (shouldSampleProbe(idx, pos))
                    {
                        probeIndex[probeInfluences++] = idx;
                        if (probeInfluences == REF_SAMPLE_COUNT)
                        {
                            break;
                        }
                    }
                    count++;

                    ++neighborIdx;
                }

                break;
            }
        }
    }

    if (sample_automatic && probeInfluences < REF_SAMPLE_COUNT)
    { // probe at index 0 is a special probe for smoothing out automatic probes
        probeIndex[probeInfluences++] = 0;
    }
#else
    probeIndex[probeInfluences++] = 0;
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
// guards `radius2 - d2` against negative values so callers that pass a position outside the sphere
// (or use the 4096*4096 automatic-probe hack) get a clamped intersection instead of NaN.
vec3 sphereIntersect(vec3 origin, vec3 dir, vec3 center, float radius2)
{
        float t0, t1; // solutions for t if the ray intersects

        vec3 L = center - origin;
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
vec3 boxIntersect(vec3 origin, vec3 dir, mat4 i, out float d, float scale)
{
    // Intersection with OBB convert to unit box space
    // Transform in local unit parallax cube space (scaled and rotated)
    mat4 clipToLocal = i;

    vec3 RayLS = mat3(clipToLocal) * dir;
    vec3 PositionLS = (clipToLocal * vec4(origin, 1.0)).xyz;

    d = 1.0-max(max(abs(PositionLS.x), abs(PositionLS.y)), abs(PositionLS.z));

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

vec3 boxIntersect(vec3 origin, vec3 dir, mat4 i, out float d)
{
    return boxIntersect(origin, dir, i, d, 1.0);
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


// The distance-only weight of a box probe.
//
// tapRefMap and tapIrradianceMap return this through an out parameter that the box branch
// used not to write at all. An out parameter is copy-out from an uninitialised local, so the
// caller was accumulating an undefined value into dwsum -- and dwsum is what decides how far
// a manual probe takes over from the automatic ones.
//
//  d  - boxIntersect's normalized depth inside the volume: 1 at the centre, 0 at the wall
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

// Tap a reflection probe
// pos - position of pixel
// dir - pixel normal
//  w - weight of sample (distance and angular attenuation)
//  dw - weight of sample (distance only)
// lod - which mip to sample (lower is higher res, sharper reflections)
// c - center of probe
// r2 - radius of probe squared
// i - index of probe
vec3 tapRefMap(vec3 pos, vec3 dir, out float w, out float dw, float lod, vec3 c, int i)
{
    // parallax adjustment
    vec3 v;

    if (refIndex[i].w < 0)
    {  // box probe
        float d = 0;
        v = boxIntersect(pos, dir, refBox[i], d);

        w = max(d, 0.001);
        dw = boxDistanceWeight(d, refParams[i].z);
    }
    else
    { // sphere probe
        float r = refSphere[i].w;

        float rr = r * r;

        v = sphereIntersect(pos, dir, c,
        refIndex[i].w < 1 ? 4096.0*4096.0 : // <== effectively disable parallax correction for automatically placed probes to keep from bombing the world with obvious spheres
                rr);

        w = sphereWeight(pos, dir, refSphere[i].xyz, r, refParams[i], dw);
    }

    v -= c;
    v = env_mat * v;

    // Unnormalized on purpose: a cubemap fetch takes a direction of any length. The normalize
    // that used to sit here was assigned to a local nothing read, so it was one inverse square
    // root per probe tap -- up to REF_SAMPLE_COUNT of them per pixel, in a function every
    // deferred and forward lighting path calls -- and it was a normalize of a vector with no
    // guarantee of being non-zero.
    vec4 ret = textureLod(reflectionProbes, vec4(v.xyz, refIndex[i].x), lod) * refParams[i].y;

    return ret.rgb;
}

// Tap an irradiance map
// pos - position of pixel
// dir - pixel normal
// w - weight of sample (distance and angular attenuation)
// dw - weight of sample (distance only)
// i - index of probe
// Reconstruct irradiance for a normal direction from a probe's nine coefficients.
//
// The band scaling is the clamped-cosine convolution (Ramamoorthi and Hanrahan): pi, 2pi/3 and
// pi/4 for bands 0, 1 and 2. They are divided through by pi here so the result is the
// cosine-weighted AVERAGE radiance rather than irradiance proper -- that is the quantity the
// cubemap held and the quantity pbrIbl multiplies by diffuseColor, so the units downstream are
// unchanged.
vec3 evalSHIrradiance(vec3 n, int layer)
{
    vec3 L0 = texelFetch(shCoeffs, ivec2(0, layer), 0).rgb;
    vec3 L1 = texelFetch(shCoeffs, ivec2(1, layer), 0).rgb;
    vec3 L2 = texelFetch(shCoeffs, ivec2(2, layer), 0).rgb;
    vec3 L3 = texelFetch(shCoeffs, ivec2(3, layer), 0).rgb;
    vec3 L4 = texelFetch(shCoeffs, ivec2(4, layer), 0).rgb;
    vec3 L5 = texelFetch(shCoeffs, ivec2(5, layer), 0).rgb;
    vec3 L6 = texelFetch(shCoeffs, ivec2(6, layer), 0).rgb;
    vec3 L7 = texelFetch(shCoeffs, ivec2(7, layer), 0).rgb;
    vec3 L8 = texelFetch(shCoeffs, ivec2(8, layer), 0).rgb;

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

vec3 tapIrradianceMap(vec3 pos, vec3 dir, out float w, out float dw, vec3 c, int i, vec3 amblit)
{
    // parallax adjustment
    vec3 v;
    if (refIndex[i].w < 0)
    {
        float d = 0.0;
        v = boxIntersect(pos, dir, refBox[i], d, 3.0);
        w = max(d, 0.001);
        dw = boxDistanceWeight(d, refParams[i].z);
    }
    else
    {
        float r = refSphere[i].w; // radius of sphere volume

        // pad sphere for manual probe extending into automatic probe space
        float rr = r * r;

        v = sphereIntersect(pos, dir, c,
        refIndex[i].w < 1 ? 4096.0*4096.0 : // <== effectively disable parallax correction for automatically placed probes to keep from bombing the world with obvious spheres
                rr);

        w = sphereWeight(pos, dir, refSphere[i].xyz, r, refParams[i], dw);
    }

    v -= c;
    v = env_mat * v;

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

vec3 sampleProbes(vec3 pos, vec3 dir, float lod)
{
    float wsum[2];
    wsum[0] = 0;
    wsum[1] = 0;

    float dwsum[2];
    dwsum[0] = 0;
    dwsum[1] = 0;

    vec3 col[2];
    col[0] = vec3(0);
    col[1] = vec3(0);

    for (int idx = 0; idx < probeInfluences; ++idx)
    {
        int i = probeIndex[idx];
        int p = clamp(abs(refIndex[i].w), 0, 1);

        if (p == 0 && !sample_automatic)
        {
            continue;
        }

        float w = 0;
        float dw = 0;
        vec3 refcol;

        {
            refcol = tapRefMap(pos, dir, w, dw, lod, refSphere[i].xyz, i);

            col[p] += refcol.rgb*w;
            wsum[p] += w;
            dwsum[p] += dw;
        }
    }

    // mix automatic and manual probes
    if (sample_automatic && wsum[0] > 0.0)
    { // some automatic probes were sampled
        col[0] *= 1.0/wsum[0];
        if (wsum[1] > 0.0)
        { //some manual probes were sampled, mix between the two
            col[1] *= 1.0/wsum[1];
            col[1] = mix(col[0], col[1], min(dwsum[1], 1.0));
            col[0] = vec3(0);
        }
    }
    else if (wsum[1] > 0.0)
    {
        // manual probes were sampled but no automatic probes were
        col[1] *= 1.0/wsum[1];
        col[0] = vec3(0);
    }

    return col[1]+col[0];
}

vec3 sampleProbeAmbient(vec3 pos, vec3 dir, vec3 amblit)
{
    // modified copy/paste of sampleProbes follows, will likely diverge from sampleProbes further
    // as irradiance map mixing is tuned independently of radiance map mixing
    float wsum[2];
    wsum[0] = 0;
    wsum[1] = 0;

    float dwsum[2];
    dwsum[0] = 0;
    dwsum[1] = 0;

    vec3 col[2];
    col[0] = vec3(0);
    col[1] = vec3(0);

    for (int idx = 0; idx < probeInfluences; ++idx)
    {
        int i = probeIndex[idx];
        int p = clamp(abs(refIndex[i].w), 0, 1);

        if (p == 0 && !sample_automatic)
        {
            continue;
        }

        {
            float w = 0;
            float dw = 0;

            vec3 refcol = tapIrradianceMap(pos, dir, w, dw, refSphere[i].xyz, i, amblit);

            col[p] += refcol*w;
            wsum[p] += w;
            dwsum[p] += dw;
        }
    }

    // mix automatic and manual probes
    if (sample_automatic && wsum[0] > 0.0)
    { // some automatic probes were sampled
        col[0] *= 1.0/wsum[0];
        if (wsum[1] > 0.0)
        { //some manual probes were sampled, mix between the two
            col[1] *= 1.0/wsum[1];
            col[1] = mix(col[0], col[1], min(dwsum[1], 1.0));
            col[0] = vec3(0);
        }
    }
    else if (wsum[1] > 0.0)
    {
        // manual probes were sampled but no automatic probes were
        col[1] *= 1.0/wsum[1];
        col[0] = vec3(0);
    }

    return col[1]+col[0];
}

#if defined(HERO_PROBES)

uniform vec4 clipPlane;
uniform samplerCubeArray   heroProbes;

void tapHeroProbe(inout vec3 glossenv, vec3 pos, vec3 norm, float glossiness)
{
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
    glossenv = mix(glossenv, textureLod(heroProbes, vec4(env_mat * refnormpersp, 0), (1.0-glossiness)*heroMipCount).xyz, w);
}

#else

void tapHeroProbe(inout vec3 glossenv, vec3 pos, vec3 norm, float glossiness)
{
}

#endif



void doProbeSample(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit)
{
    // TODO - don't hard code lods
    float reflection_lods = max_probe_lod;

    vec3 refnormpersp = normalize(reflect(pos.xyz, norm.xyz));

    // A GGX lobe is not centred on the mirror direction -- it leans toward the normal as the
    // surface roughens. Sampling a prefiltered probe along the mirror direction therefore reads
    // from the wrong place on exactly the surfaces whose lobe is widest.
    float perceptualRoughness = 1.0 - glossiness;
    refnormpersp = getSpecularDominantDir(norm.xyz, refnormpersp, perceptualRoughness);

    ambenv = amblit;

    if (classic_mode == 0)
        ambenv = sampleProbeAmbient(pos, norm, amblit);

    float lod = perceptualRoughness*reflection_lods;
    glossenv = sampleProbes(pos, refnormpersp, lod);

#if defined(SSR)
    // Skip the march only where it would contribute nothing anyway. Asking the SSR unit for its
    // own fade threshold keeps this from drifting away from it again.
    if (cube_snapshot != 1 && glossiness > ssrMinGlossiness())
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

void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit)
{
    preProbeSample(pos);
    doProbeSample(ambenv, glossenv, tc, pos, norm, glossiness, transparent, amblit);
}

void sampleReflectionProbesWater(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, vec3 amblit)
{
    // don't sample automatic probes for water
    sample_automatic = false;
    preProbeSample(pos);
    sample_automatic = true;
    // always include void probe on water
    if (probeInfluences < REF_SAMPLE_COUNT)
    {
        probeIndex[probeInfluences++] = 0;
    }

    doProbeSample(ambenv, glossenv, tc, pos, norm, glossiness, false, amblit);
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

void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit)
{
    float reflection_lods = max_probe_lod;
    preProbeSample(pos);

    vec3 refnormpersp = reflect(pos.xyz, norm.xyz);

    ambenv = amblit;

    if (classic_mode == 0)
        ambenv = sampleProbeAmbient(pos, norm, amblit);

    if (glossiness > 0.0)
    {
        // (1.0 - glossiness) is this path's perceptual roughness -- it is already what selects
        // the prefiltered mip, so the lobe correction reads it the same way.
        float perceptualRoughness = 1.0 - glossiness;

        // Bend toward the normal with roughness, as doProbeSample does for PBR. A prefiltered
        // probe sampled along the mirror direction fetches from slightly the wrong place on
        // exactly the surfaces whose lobe is widest, which reads as reflections sliding across
        // curvature. The two lookups below keep the mirror direction on purpose: legacyenv is
        // the roughness-zero case, where the bend is the identity anyway, and the screen-space
        // march wants the true reflection ray.
        vec3 dominantDir = getSpecularDominantDir(norm.xyz, normalize(refnormpersp), perceptualRoughness);

        float lod = perceptualRoughness*reflection_lods;
        glossenv = sampleProbes(pos, dominantDir, lod);
    }

    if (envIntensity > 0.0)
    {
        legacyenv = sampleProbes(pos, normalize(refnormpersp), 0.0);
    }

#if defined(SSR)
    // Gated the same way the PBR path is. Both consumers below blend by ssr.a, and ssr.a is
    // already zero at these glossiness values -- the march's own fade reaches zero at
    // ssrMinGlossiness(), and the transparent branch scales by glossiness directly. alphaF
    // calls this with glossiness 0, so every legacy alpha fragment in the scene was paying for
    // a full ray march whose result was then multiplied by nothing.
    if (cube_snapshot != 1 && (transparent ? glossiness > 0.0 : glossiness > ssrMinGlossiness()))
    {
        vec4 ssr = vec4(0);

        vec3 ssrDir = normalize(refnormpersp);

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

