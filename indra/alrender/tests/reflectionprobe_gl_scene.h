/**
 * @file reflectionprobe_gl_scene.h
 * @brief A scene of reflection probes, automatic and manual spheres and boxes with neighbour lists and the default
 *        probe, and points over them sampled through class3/deferred/reflectionProbeF.glsl's entry points: by the one
 *        pass over the probes that taps each as it finds it, and as the probes were sampled before, a list of those
 *        that hold the point and then a pass over it for each tap. reflectionprobe_gl_test compares the two;
 *        reflectionprobe_gl_bench times them.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
 * $/LicenseInfo$
 */

#pragma once

#include "aluniformbuffer.h"
#include "llcubemaparray.h"
#include "llfile.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimagegl.h"
#include "llmatrix4a.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace reflectionprobe_gl
{
    constexpr S32 PROBES = 20;
    constexpr U32 CUBE   = 16;
    // The screen-space reflections' scene: its texels each way, and its projection's planes.
    constexpr U32 SCENE      = 128;
    constexpr F32 SCENE_NEAR = 0.1f;
    constexpr F32 SCENE_FAR  = 64.f;

    // A full screen triangle.
    inline const char* const VERTEX = R"glsl(
in vec3 position;
void main()
{
    gl_Position = vec4(position, 1.0);
}
)glsl";

    // Each pixel's point, normal and surface, from a hash of the pixel: points over the probes' reach, and a few past
    // the default probe's, normals every way, glossiness and environment intensity with a share of each at exactly 0,
    // PBR or legacy, and a share transparent, which the screen-space reflections march for apart.
    inline const char* const INPUTS = R"glsl(
struct ProbeTestPixel
{
    vec2  tc;
    vec3  pos;
    vec3  norm;
    float glossiness;
    float envIntensity;
    vec3  amblit;
    bool  pbr;
    bool  transparent;
};

uint probeTestHash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float probeTestRandom(uvec2 px, uint c)
{
    return float(probeTestHash(px.x * 1973u + px.y * 9277u + c * 26699u) & 0xffffu) / 65536.0;
}

// Where z is 1, a surface over the probes in place of the hash, as a view holds one: every pixel's point, normal and
// surface near its neighbours', over a target of xy pixels.
uniform vec3 probe_test_surface;

ProbeTestPixel probeTestPixel(vec2 frag)
{
    uvec2 px = uvec2(frag);
    ProbeTestPixel p;
    p.tc = frag / 1024.0;
    if (probe_test_surface.z > 0.0)
    {
        vec2 uv = frag / probe_test_surface.xy;
        float height = 2.0 * sin(uv.x * 6.0) * cos(uv.y * 4.0);
        p.pos = vec3(mix(-12.0, 12.0, uv.x), -3.0 + height, -mix(0.5, 36.0, uv.y));
        vec2 slope = vec2(12.0 * cos(uv.x * 6.0) * cos(uv.y * 4.0) / 24.0, -8.0 * sin(uv.x * 6.0) * sin(uv.y * 4.0) / 35.5);
        p.norm = normalize(vec3(-slope.x, 1.0, slope.y));
        p.glossiness = 0.2 + 0.7 * fract(uv.y * 3.0);
        p.envIntensity = fract(uv.x * 2.0) < 0.3 ? 0.0 : 0.5;
        p.amblit = vec3(0.1, 0.15, 0.2);
        p.pbr = fract(uv.x * 4.0) < 0.5;
        p.transparent = fract(uv.y * 5.0) < 0.2;
        return p;
    }
    p.pos = vec3(mix(-12.0, 12.0, probeTestRandom(px, 0u)), mix(-6.0, 6.0, probeTestRandom(px, 1u)),
                 -mix(0.5, 36.0, probeTestRandom(px, 2u)));
    if (probeTestRandom(px, 9u) < 0.0625)
    {
        // Past the default probe's 4 km, which only an infinite projection shows.
        p.pos.z = -mix(4200.0, 8000.0, probeTestRandom(px, 10u));
    }
    float z = probeTestRandom(px, 3u) * 2.0 - 1.0;
    float a = probeTestRandom(px, 4u) * 6.2831853;
    float r = sqrt(max(1.0 - z * z, 0.0));
    p.norm = vec3(r * cos(a), r * sin(a), z);
    float g = probeTestRandom(px, 5u);
    p.glossiness = g < 0.2 ? 0.0 : g;
    float e = probeTestRandom(px, 6u);
    p.envIntensity = e < 0.3 ? 0.0 : e;
    p.amblit = vec3(0.2, 0.3, 0.4) * probeTestRandom(px, 7u);
    p.pbr = probeTestRandom(px, 8u) < 0.5;
    p.transparent = probeTestRandom(px, 11u) < 0.25;
    return p;
}
)glsl";

    // The entry points, as the viewer's shaders call them.
    inline const char* const FUSED = R"glsl(
layout(location = 0) out vec4 out_ambient;
layout(location = 1) out vec4 out_gloss;
layout(location = 2) out vec4 out_legacy;

uniform int probe_test_api;

struct ProbeTestPixel
{
    vec2  tc;
    vec3  pos;
    vec3  norm;
    float glossiness;
    float envIntensity;
    vec3  amblit;
    bool  pbr;
    bool  transparent;
};
ProbeTestPixel probeTestPixel(vec2 frag);

void sampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
    vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit_linear);
void sampleReflectionProbesWater(inout vec3 ambenv, inout vec3 glossenv,
    vec2 tc, vec3 pos, vec3 norm, float glossiness, vec3 amblit_linear);
void sampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
    vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit_linear);
void sampleReflectionProbesEither(bool pbr, inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
    vec2 tc, vec3 pos, vec3 norm, float glossiness, float envIntensity, bool transparent, vec3 amblit);

void main()
{
    ProbeTestPixel p = probeTestPixel(gl_FragCoord.xy);
    vec3 ambenv = vec3(-1.0);
    vec3 glossenv = vec3(-2.0);
    vec3 legacyenv = vec3(-3.0);
    if (probe_test_api == 0)
    {
        sampleReflectionProbes(ambenv, glossenv, p.tc, p.pos, p.norm, p.glossiness, p.transparent, p.amblit);
    }
    else if (probe_test_api == 1)
    {
        sampleReflectionProbesLegacy(ambenv, glossenv, legacyenv, p.tc, p.pos, p.norm, p.glossiness, p.envIntensity,
                                     p.transparent, p.amblit);
    }
    else if (probe_test_api == 2)
    {
        sampleReflectionProbesWater(ambenv, glossenv, p.tc, p.pos, p.norm, p.glossiness, p.amblit);
    }
    else
    {
        sampleReflectionProbesEither(p.pbr, ambenv, glossenv, legacyenv, p.tc, p.pos, p.norm, p.glossiness,
                                     p.envIntensity, p.transparent, p.amblit);
    }
    out_ambient = vec4(ambenv, 1.0);
    out_gloss = vec4(glossenv, 1.0);
    out_legacy = vec4(legacyenv, 1.0);
}
)glsl";

    // The probes sampled as they were before their taps were fused: preProbeSample's list of the probes that hold the
    // point, then a pass over it for the irradiance and one for each radiance tap, and after it the screen-space
    // reflection and the hero probe, with the leaves the fused pass still takes from reflectionProbeF.glsl and
    // screenSpaceReflUtil.glsl.
    inline const char* const LISTED = R"glsl(
layout(location = 0) out vec4 out_ambient;
layout(location = 1) out vec4 out_gloss;
layout(location = 2) out vec4 out_legacy;

uniform int probe_test_api;
uniform samplerCubeArray reflectionProbes;
uniform float max_probe_lod;
uniform mat3 env_mat;

#ifdef CLASSIC_MODE
#define classic_mode 1
#else
#define classic_mode 0
#endif

//[ENGINE_BLOCK ReflectionProbes]

struct ProbeTestPixel
{
    vec2  tc;
    vec3  pos;
    vec3  norm;
    float glossiness;
    float envIntensity;
    vec3  amblit;
    bool  pbr;
    bool  transparent;
};
ProbeTestPixel probeTestPixel(vec2 frag);

vec3 getSpecularDominantDir(vec3 n, vec3 r, float perceptualRoughness);
vec3 sphereIntersect(vec3 origin, vec3 dir, vec3 center, float radius2);
float sphereWeight(vec3 pos, vec3 dir, vec3 origin, float r, vec4 i, out float dw);
float boxDistanceWeight(float d, float fade);
vec3 evalSHIrradiance(vec3 n, int layer);
int getStartIndex(vec3 pos);
void tapHeroProbe(inout vec3 glossenv, vec3 pos, vec3 norm, float glossiness);

#if defined(SSR)
uniform sampler2D sceneMap;
uniform int cube_snapshot;
float ssrMinGlossiness();
float tapScreenSpaceReflection(int totalSamples, vec2 tc, vec3 viewPos, vec3 n, vec3 rayDir, inout vec4 collectedColor,
                               sampler2D source, float glossiness);
#endif

int probeIndex[REF_SAMPLE_COUNT];
int probeInfluences = 0;
bool sample_automatic = true;
bool water_sample = false;

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
        {
            return false;
        }
    }

    return true;
}

void preProbeSample(vec3 pos)
{
    probeInfluences = 0;

#if REFMAP_LEVEL > 0
    int start = getStartIndex(pos);

    for (int i = start; i < refmapCount && probeInfluences < REF_SAMPLE_COUNT; ++i)
    {
        if (refParams[i].w > -pos.z)
        {
            break;
        }

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
    {
        probeIndex[probeInfluences++] = 0;
    }
#else
    probeIndex[probeInfluences++] = 0;
#endif
}

vec3 listedBoxIntersect(vec3 origin, vec3 dir, mat4 i, out float d, float scale)
{
    mat4 clipToLocal = i;

    vec3 RayLS = mat3(clipToLocal) * dir;
    vec3 PositionLS = (clipToLocal * vec4(origin, 1.0)).xyz;

    d = 1.0-max(max(abs(PositionLS.x), abs(PositionLS.y)), abs(PositionLS.z));

    vec3 raySign = vec3(RayLS.x < 0.0 ? -1.0 : 1.0,
                        RayLS.y < 0.0 ? -1.0 : 1.0,
                        RayLS.z < 0.0 ? -1.0 : 1.0);
    vec3 rayDenom = raySign * max(abs(RayLS), vec3(1e-6));

    vec3 Unitary = vec3(scale);
    vec3 FirstPlaneIntersect  = (Unitary - PositionLS) / rayDenom;
    vec3 SecondPlaneIntersect = (-Unitary - PositionLS) / rayDenom;
    vec3 FurthestPlane = max(FirstPlaneIntersect, SecondPlaneIntersect);
    float Distance = min(FurthestPlane.x, min(FurthestPlane.y, FurthestPlane.z));

    vec3 IntersectPositionCS = origin + dir * Distance;

    return IntersectPositionCS;
}

vec3 tapRefMap(vec3 pos, vec3 dir, out float w, out float dw, float lod, vec3 c, int i)
{
    vec3 v;

    if (refIndex[i].w < 0)
    {
        float d = 0;
        v = listedBoxIntersect(pos, dir, refBox[i], d, 1.0);

        w = max(d, 0.001);
        dw = boxDistanceWeight(d, refParams[i].z);
    }
    else
    {
        float r = refSphere[i].w;

        float rr = r * r;

        v = (water_sample && i == 0) ? c + dir :
            sphereIntersect(pos, dir, c,
        refIndex[i].w < 1 ? 4096.0*4096.0 :
                rr);

        w = sphereWeight(pos, dir, refSphere[i].xyz, r, refParams[i], dw);
        if (i == 0)
        {
            w = max(w, 1e-6);
        }
    }

    v -= c;
    v = env_mat * v;

    vec4 ret = textureLod(reflectionProbes, vec4(v.xyz, refIndex[i].x), lod) * refParams[i].y;

    return ret.rgb;
}

vec3 tapIrradianceMap(vec3 pos, vec3 dir, out float w, out float dw, vec3 c, int i, vec3 amblit)
{
    vec3 v;
    if (refIndex[i].w < 0)
    {
        float d = 0.0;
        v = listedBoxIntersect(pos, dir, refBox[i], d, 3.0);
        w = max(d, 0.001);
        dw = boxDistanceWeight(d, refParams[i].z);
    }
    else
    {
        float r = refSphere[i].w;

        float rr = r * r;

        v = (water_sample && i == 0) ? c + dir :
            sphereIntersect(pos, dir, c,
        refIndex[i].w < 1 ? 4096.0*4096.0 :
                rr);

        w = sphereWeight(pos, dir, refSphere[i].xyz, r, refParams[i], dw);
        if (i == 0)
        {
            w = max(w, 1e-6);
        }
    }

    v -= c;
    v = env_mat * v;

    float len2 = dot(v, v);
    vec3 sampleDir = len2 > 1e-12 ? v * inversesqrt(len2) : env_mat * dir;

    vec3 col = evalSHIrradiance(sampleDir, refIndex[i].x);

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

    if (sample_automatic && wsum[0] > 0.0)
    {
        col[0] *= 1.0/wsum[0];
        if (wsum[1] > 0.0)
        {
            col[1] *= 1.0/wsum[1];
            col[1] = mix(col[0], col[1], min(dwsum[1], 1.0));
            col[0] = vec3(0);
        }
    }
    else if (wsum[1] > 0.0)
    {
        col[1] *= 1.0/wsum[1];
        col[0] = vec3(0);
    }

    return col[1]+col[0];
}

vec3 sampleProbeAmbient(vec3 pos, vec3 dir, vec3 amblit)
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

        {
            float w = 0;
            float dw = 0;

            vec3 refcol = tapIrradianceMap(pos, dir, w, dw, refSphere[i].xyz, i, amblit);

            col[p] += refcol*w;
            wsum[p] += w;
            dwsum[p] += dw;
        }
    }

    if (sample_automatic && wsum[0] > 0.0)
    {
        col[0] *= 1.0/wsum[0];
        if (wsum[1] > 0.0)
        {
            col[1] *= 1.0/wsum[1];
            col[1] = mix(col[0], col[1], min(dwsum[1], 1.0));
            col[0] = vec3(0);
        }
    }
    else if (wsum[1] > 0.0)
    {
        col[1] *= 1.0/wsum[1];
        col[0] = vec3(0);
    }

    return col[1]+col[0];
}

void doProbeSample(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit)
{
    float reflection_lods = max_probe_lod;

    vec3 refnormpersp = normalize(reflect(pos.xyz, norm.xyz));

    float perceptualRoughness = 1.0 - glossiness;
    refnormpersp = getSpecularDominantDir(norm.xyz, refnormpersp, perceptualRoughness);

    ambenv = amblit;

    if (classic_mode == 0)
        ambenv = sampleProbeAmbient(pos, norm, amblit);

    float lod = perceptualRoughness*reflection_lods;
    glossenv = sampleProbes(pos, refnormpersp, lod);

#if defined(SSR)
    if (cube_snapshot != 1 && (transparent ? glossiness > 0.0 : glossiness > ssrMinGlossiness()))
    {
        vec4 ssr = vec4(0);
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

void listedSampleReflectionProbes(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, bool transparent, vec3 amblit)
{
    preProbeSample(pos);
    doProbeSample(ambenv, glossenv, tc, pos, norm, glossiness, transparent, amblit);
}

void listedSampleReflectionProbesWater(inout vec3 ambenv, inout vec3 glossenv,
        vec2 tc, vec3 pos, vec3 norm, float glossiness, vec3 amblit)
{
    sample_automatic = false;
    preProbeSample(pos);
    sample_automatic = true;
    if (probeInfluences < REF_SAMPLE_COUNT)
    {
        probeIndex[probeInfluences++] = 0;
    }

    water_sample = true;
    doProbeSample(ambenv, glossenv, tc, pos, norm, glossiness, false, amblit);
    water_sample = false;
}

void listedSampleReflectionProbesLegacy(inout vec3 ambenv, inout vec3 glossenv, inout vec3 legacyenv,
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
        float perceptualRoughness = 1.0 - glossiness;
        vec3 dominantDir = getSpecularDominantDir(norm.xyz, normalize(refnormpersp), perceptualRoughness);

        float lod = perceptualRoughness*reflection_lods;
        glossenv = sampleProbes(pos, dominantDir, lod);
    }

    if (envIntensity > 0.0)
    {
        legacyenv = sampleProbes(pos, normalize(refnormpersp), 0.0);
    }

#if defined(SSR)
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

void main()
{
    ProbeTestPixel p = probeTestPixel(gl_FragCoord.xy);
    vec3 ambenv = vec3(-1.0);
    vec3 glossenv = vec3(-2.0);
    vec3 legacyenv = vec3(-3.0);
    if (probe_test_api == 0 || (probe_test_api == 3 && p.pbr))
    {
        listedSampleReflectionProbes(ambenv, glossenv, p.tc, p.pos, p.norm, p.glossiness, p.transparent, p.amblit);
    }
    else if (probe_test_api == 1 || probe_test_api == 3)
    {
        listedSampleReflectionProbesLegacy(ambenv, glossenv, legacyenv, p.tc, p.pos, p.norm, p.glossiness,
                                           p.envIntensity, p.transparent, p.amblit);
    }
    else
    {
        listedSampleReflectionProbesWater(ambenv, glossenv, p.tc, p.pos, p.norm, p.glossiness, p.amblit);
    }
    out_ambient = vec4(ambenv, 1.0);
    out_gloss = vec4(glossenv, 1.0);
    out_legacy = vec4(legacyenv, 1.0);
}
)glsl";

    // A hash in [0, 1) of an integer and a channel, for the probe layout and the textures.
    inline F32 random(U32 x, U32 c)
    {
        U32 h = x * 2654435761u ^ c * 2246822519u;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        return (F32)(h & 0xffffffu) / 16777216.f;
    }

    inline U16 toHalf(F32 f)
    {
        U32 u;
        std::memcpy(&u, &f, 4);
        const U32 sign = (u >> 16) & 0x8000u;
        const S32 e = (S32)((u >> 23) & 0xff) - 127 + 15;
        const U32 m = u & 0x7fffffu;
        if (e <= 0)
        {
            return (U16)sign;
        }
        return (U16)(sign | ((U32)e << 10) | (m >> 13));
    }

    // The ReflectionProbes block's members the scene fills, where the program lays them out.
    struct Layout
    {
        GLint refBox = -1, refSphere = -1, refParams = -1, refIndex = -1, refNeighbor = -1, refBucket = -1,
              refmapCount = -1;
        GLint heroBox = -1, heroSphere = -1, heroShape = -1, heroMaxLod = -1, heroProbeCount = -1;
        GLint iterationCount = -1, rayStep = -1, distanceBias = -1, depthRejectBias = -1, glossySampleCount = -1,
              adaptiveStepMultiplier = -1;
        GLint size = 0;
    };

    struct ProbeScene
    {
        ProbeScene()
        : mShaders(mDir.prefix())
        {
        }

        ~ProbeScene()
        {
            LLGLSLShader::unbind();
            mFused.unload();
            mListed.unload();
            mShaders.clearShaderObjects();
            mTarget.release();
            mSH.release();
            mScene.release();
            mCube = nullptr;
            mTriangle = nullptr;
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
        }

        // The viewer's files the programs read, as the viewer finds them for probes at class 3, and the test's own.
        void writeShaders()
        {
            const std::filesystem::path root = std::filesystem::path(AL_TEST_SHADER_DIR);
            const auto copy = [&](const std::filesystem::path& from, const std::string& to)
            {
                std::error_code ec;
                const std::string text = LLFile::getContents(from.string(), ec);
                REQUIRE_FALSE(ec);
                REQUIRE(mDir.write(to, text));
            };
            copy(root / "class3" / "deferred" / "reflectionProbeF.glsl", "deferred/reflectionProbeF.glsl");
            copy(root / "class1" / "deferred" / "deferredUtil.glsl", "deferred/deferredUtil.glsl");
            copy(root / "class1" / "deferred" / "globalF.glsl", "deferred/globalF.glsl");
            copy(root / "class1" / "deferred" / "textureUtilV.glsl", "deferred/textureUtilV.glsl");
            copy(root / "class1" / "objects" / "nonindexedTextureV.glsl", "objects/nonindexedTextureV.glsl");
            copy(root / "class1" / "environment" / "srgbF.glsl", "environment/srgbF.glsl");
            // The screen-space reflections' unit: its stubs at class 1, and its march at class 3, which build() puts
            // in its place where the programs march.
            std::error_code ec;
            mSSRStub = LLFile::getContents((root / "class1" / "deferred" / "screenSpaceReflUtil.glsl").string(), ec);
            REQUIRE_FALSE(ec);
            mSSRMarch = LLFile::getContents((root / "class3" / "deferred" / "screenSpaceReflUtil.glsl").string(), ec);
            REQUIRE_FALSE(ec);
            for (const auto& entry : std::filesystem::directory_iterator(root / "class1" / "deferred"))
            {
                const std::string name = entry.path().filename().string();
                if (name.size() > 10 && name.compare(name.size() - 10, 10, "Block.glsl") == 0)
                {
                    copy(entry.path(), "deferred/" + name);
                }
            }
            REQUIRE(mDir.write("test/probeTestV.glsl", VERTEX));
            REQUIRE(mDir.write("test/probeTestInputsF.glsl", INPUTS));
            REQUIRE(mDir.write("test/probeFusedF.glsl", FUSED));
            REQUIRE(mDir.write("test/probeListedF.glsl", LISTED));
        }

        // `main` over the probes at probe level `level`, `count` probes a point, with the classic sky where
        // `classic`, and where `tails` the screen-space reflections and the hero probe. The probe programs' features
        // attach what they read as they attach it to the viewer's, the shared objects the viewer compiles once under
        // its global defines, compiled here under this configuration's.
        bool build(LLGLSLShader& program, const char* main, S32 level, S32 count, bool classic, bool tails = false)
        {
            std::map<std::string, std::string> defines = LLGLSLShader::sGlobalDefines;
            defines["REFMAP_LEVEL"] = std::to_string(level);
            defines["REF_SAMPLE_COUNT"] = std::to_string(count);
            if (classic)
            {
                defines["CLASSIC_MODE"] = "1";
            }
            if (tails)
            {
                defines["SSR"] = "1";
                defines["HERO_PROBES"] = "1";
            }
            if (!mDir.write("deferred/screenSpaceReflUtil.glsl", tails ? mSSRMarch : mSSRStub))
            {
                return false;
            }
            // What LLShaderMgr::attachShaderFeatures attaches for the features below: an object it attaches that is
            // not here fails the build on every driver.
            const std::pair<const char*, GLenum> shared[] = {
                { "deferred/textureUtilV.glsl", GL_VERTEX_SHADER },
                { "objects/nonindexedTextureV.glsl", GL_VERTEX_SHADER },
                { "deferred/globalF.glsl", GL_FRAGMENT_SHADER },
                { "environment/srgbF.glsl", GL_FRAGMENT_SHADER },
                { "deferred/deferredUtil.glsl", GL_FRAGMENT_SHADER },
                { "deferred/screenSpaceReflUtil.glsl", GL_FRAGMENT_SHADER },
                { "deferred/reflectionProbeF.glsl", GL_FRAGMENT_SHADER },
            };
            for (const auto& [object, stage] : shared)
            {
                S32 shader_level = 1;
                if (mShaders.loadShaderFile(object, shader_level, stage, &defines) == 0)
                {
                    return false;
                }
            }

            program.unload();
            program.mName = main;
            program.mFeatures.hasSrgb = true;
            program.mFeatures.hasReflectionProbes = true;
            program.mShaderFiles.clear();
            program.mShaderFiles.push_back(std::make_pair("test/probeTestV.glsl", GL_VERTEX_SHADER));
            program.mShaderFiles.push_back(std::make_pair(main, GL_FRAGMENT_SHADER));
            program.mShaderFiles.push_back(std::make_pair("test/probeTestInputsF.glsl", GL_FRAGMENT_SHADER));
            program.mShaderLevel = 1;
            for (const auto& [name, value] : defines)
            {
                if (!LLGLSLShader::sGlobalDefines.contains(name))
                {
                    program.addPermutation(name, value);
                }
            }
            return program.createShader();
        }

        // At `width` by `height` pixels.
        void setUp(S32 width, S32 height)
        {
            mW = width;
            mH = height;
            writeShaders();
            REQUIRE(mTarget.ensure({ .width = (U32)mW, .height = (U32)mH,
                                     .color = { GL_RGBA32F, GL_RGBA32F, GL_RGBA32F } }) ==
                    LLRenderTarget::EnsureResult::Built);

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();

            // Each probe's cube: every face, mip and layer its own smooth colours, so a tap's direction, mip and
            // layer all show.
            mCube = new LLCubeMapArray();
            mCube->allocate(CUBE, 4, PROBES, true, true);
            mCube->bind(0);
            for (U32 level = 0, res = CUBE; res > 0; ++level, res >>= 1)
            {
                std::vector<F32> texels(size_t(res) * res * PROBES * 6 * 4);
                for (U32 layer = 0; layer < PROBES * 6; ++layer)
                {
                    for (U32 y = 0; y < res; ++y)
                    {
                        for (U32 x = 0; x < res; ++x)
                        {
                            F32* t = &texels[((size_t(layer) * res + y) * res + x) * 4];
                            const F32 u = ((F32)x + 0.5f) / (F32)res;
                            const F32 v = ((F32)y + 0.5f) / (F32)res;
                            t[0] = 0.2f + 0.8f * random(layer, 1) * u + 0.1f * (F32)level;
                            t[1] = 0.2f + 0.8f * random(layer, 2) * v;
                            t[2] = 0.2f + 0.6f * random(layer, 3) * (1.f - u * v);
                            t[3] = 1.f;
                        }
                    }
                }
                glTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, level, 0, 0, 0, res, res, PROBES * 6, GL_RGBA, GL_FLOAT,
                                texels.data());
            }
            gGL.getTextureSlot(0)->unbind();

            // Nine coefficients a probe, packed as halves, four texels to a row.
            REQUIRE(mSH.ensure({ .width = 4, .height = (U32)PROBES, .color = { GL_RGBA32UI } }) ==
                    LLRenderTarget::EnsureResult::Built);
            std::vector<U32> sh(size_t(4) * PROBES * 4, 0);
            for (U32 probe = 0; probe < PROBES; ++probe)
            {
                U16 halves[32] = {};
                for (U32 c = 0; c < 27; ++c)
                {
                    const F32 value = c < 3 ? 0.5f + random(probe * 31 + c, 9) : random(probe * 31 + c, 10) - 0.5f;
                    halves[c] = toHalf(value);
                }
                for (U32 word = 0; word < 16; ++word)
                {
                    sh[probe * 16 + word] = (U32)halves[word * 2] | ((U32)halves[word * 2 + 1] << 16);
                }
            }
            mSH.bindTexture(0, 0, ALSamplers::PointClamp);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, PROBES, GL_RGBA_INTEGER, GL_UNSIGNED_INT, sh.data());
            gGL.getTextureSlot(0)->unbind();

            // The scene the screen-space reflections march: seen through a quarter turn's field of view, a floor 4 m
            // below the eye up to a wall turned across the view, 24 to 36 m ahead, each texel's colour its own, and
            // stored through the projection the march projects by.
            LLMatrix4a proj;
            proj.clear();
            F32* m = proj.getF32ptr();
            m[0] = 1.f;
            m[5] = 1.f;
            m[10] = (SCENE_FAR + SCENE_NEAR) / (SCENE_NEAR - SCENE_FAR);
            m[11] = -1.f;
            m[14] = 2.f * SCENE_FAR * SCENE_NEAR / (SCENE_NEAR - SCENE_FAR);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(proj);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();

            REQUIRE(mScene.ensure({ .width = SCENE, .height = SCENE, .color = { GL_RGBA16F },
                                    .depth = ALRenderTargetDesc::Depth::D32F }) == LLRenderTarget::EnsureResult::Built);
            std::vector<F32> colour(size_t(SCENE) * SCENE * 4);
            std::vector<F32> depth(size_t(SCENE) * SCENE);
            for (U32 y = 0; y < SCENE; ++y)
            {
                for (U32 x = 0; x < SCENE; ++x)
                {
                    const size_t i = size_t(y) * SCENE + x;
                    const F32 dx = ((F32)x + 0.5f) / (F32)SCENE * 2.f - 1.f;
                    const F32 dy = ((F32)y + 0.5f) / (F32)SCENE * 2.f - 1.f;
                    const F32 wall = 30.f + 6.f * dx;
                    const F32 view_depth = dy < 0.f ? llmin(4.f / -dy, wall) : wall;
                    depth[i] = ((m[10] * -view_depth + m[14]) / view_depth) * 0.5f + 0.5f;
                    colour[i * 4 + 0] = 0.1f + 0.9f * random(x, 40);
                    colour[i * 4 + 1] = 0.1f + 0.9f * random(y, 41);
                    colour[i * 4 + 2] = 0.1f + 0.9f * random((U32)i, 42);
                    colour[i * 4 + 3] = 1.f;
                }
            }
            ll_test::writeTexture(mScene.getTexture(), SCENE, SCENE, GL_RGBA, GL_FLOAT, colour.data());
            ll_test::writeTexture(mScene.getDepth(), SCENE, SCENE, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
        }

        Layout layoutOf(const LLGLSLShader& program) const
        {
            Layout layout;
            const GLuint name = program.mProgramObject;
            const GLuint block = glGetUniformBlockIndex(name, "ReflectionProbes");
            REQUIRE(block != GL_INVALID_INDEX);
            glGetActiveUniformBlockiv(name, block, GL_UNIFORM_BLOCK_DATA_SIZE, &layout.size);
            const char* names[] = { "refBox", "refSphere", "refParams", "refIndex", "refNeighbor", "refBucket",
                                    "refmapCount", "heroBox", "heroSphere", "heroShape", "heroMaxLod",
                                    "heroProbeCount", "iterationCount", "rayStep", "distanceBias", "depthRejectBias",
                                    "glossySampleCount", "adaptiveStepMultiplier" };
            GLint* offsets[] = { &layout.refBox, &layout.refSphere, &layout.refParams, &layout.refIndex,
                                 &layout.refNeighbor, &layout.refBucket, &layout.refmapCount, &layout.heroBox,
                                 &layout.heroSphere, &layout.heroShape, &layout.heroMaxLod, &layout.heroProbeCount,
                                 &layout.iterationCount, &layout.rayStep, &layout.distanceBias,
                                 &layout.depthRejectBias, &layout.glossySampleCount, &layout.adaptiveStepMultiplier };
            for (size_t i = 0; i < LL_ARRAY_SIZE(names); ++i)
            {
                std::string member = names[i];
                GLuint index = GL_INVALID_INDEX;
                const char* query = member.c_str();
                glGetUniformIndices(name, 1, &query, &index);
                if (index == GL_INVALID_INDEX)
                {
                    member += "[0]";
                    query = member.c_str();
                    glGetUniformIndices(name, 1, &query, &index);
                }
                CAPTURE(member);
                REQUIRE(index != GL_INVALID_INDEX);
                glGetActiveUniformsiv(name, 1, &index, GL_UNIFORM_OFFSET, offsets[i]);
            }
            return layout;
        }

        // Twenty probes over the points' reach: the default probe; automatic spheres; manual spheres, and boxes turned
        // and stretched, some holding others' bounding spheres; sorted on the nearest depth each reaches, as the
        // viewer sorts them. Two probes have neighbour lists, one of four and one of seven, which take boxes and
        // automatic spheres alike, and the depth buckets start the search past the nearest probes for far points.
        void layProbes(const LLGLSLShader& program)
        {
            const Layout layout = layoutOf(program);
            std::vector<U8> block((size_t)layout.size, 0);
            const auto put = [&](GLint offset, size_t index, size_t stride, const void* data, size_t bytes)
            {
                std::memcpy(&block[(size_t)offset + index * stride], data, bytes);
            };

            struct Probe
            {
                LLVector3 centre;
                F32 radius = 0.f;
                S32 kind = 0; // 0 automatic, > 0 manual sphere, < 0 box
                LLVector3 extent;
                F32 turn = 0.f;
                F32 near_depth = 0.f;
            };
            std::vector<Probe> probes;
            for (S32 i = 1; i < PROBES; ++i)
            {
                Probe probe;
                probe.centre.set(-10.f + 20.f * random(i, 20), -4.f + 8.f * random(i, 21), -2.f - 30.f * random(i, 22));
                probe.kind = i % 3 == 0 ? -1 : (i % 3 == 1 ? 0 : 1 + i % 2);
                probe.extent.set(2.f + 4.f * random(i, 23), 1.5f + 3.f * random(i, 24), 2.f + 4.f * random(i, 25));
                probe.radius = probe.kind < 0 ? probe.extent.length() : 3.f + 6.f * random(i, 26);
                probe.turn = 3.f * random(i, 27);
                probe.near_depth = llmax(-probe.centre.mV[VZ] - probe.radius, 0.f);
                probes.push_back(probe);
            }
            std::sort(probes.begin(), probes.end(),
                      [](const Probe& a, const Probe& b) { return a.near_depth < b.near_depth; });
            Probe fallback;
            fallback.radius = 4096.f;
            probes.insert(probes.begin(), fallback);

            for (S32 i = 0; i < PROBES; ++i)
            {
                const Probe& probe = probes[i];
                // A box's matrix takes view space into its unit cube: turned about y, stretched by its extents.
                const F32 c = std::cos(probe.turn);
                const F32 s = std::sin(probe.turn);
                F32 box[16] = {};
                const F32 ex = probe.extent.mV[VX];
                const F32 ey = probe.extent.mV[VY];
                const F32 ez = probe.extent.mV[VZ];
                // Rows of the world-to-local rotation, each over its extent; column-major storage.
                const LLVector3 r0(c / ex, 0.f, -s / ex);
                const LLVector3 r1(0.f, 1.f / ey, 0.f);
                const LLVector3 r2(s / ez, 0.f, c / ez);
                const LLVector3 rows[3] = { r0, r1, r2 };
                for (S32 row = 0; row < 3; ++row)
                {
                    for (S32 col = 0; col < 3; ++col)
                    {
                        box[col * 4 + row] = rows[row].mV[col];
                    }
                    box[12 + row] = -(rows[row] * probe.centre);
                }
                box[15] = 1.f;
                put(layout.refBox, i, 64, box, sizeof(box));

                const F32 sphere[4] = { probe.centre.mV[VX], probe.centre.mV[VY], probe.centre.mV[VZ], probe.radius };
                put(layout.refSphere, i, 16, sphere, sizeof(sphere));
                const F32 params[4] = { 0.5f + random(i, 30), 0.5f + random(i, 31), 0.3f + 0.7f * random(i, 32),
                                        probe.near_depth };
                put(layout.refParams, i, 16, params, sizeof(params));
                S32 index[4] = { i, -1, 0, i == 0 ? 0 : probe.kind };
                if (i == 4)
                {
                    index[1] = 0;
                    index[2] = 4;
                }
                else if (i == 9)
                {
                    index[1] = 1;
                    index[2] = 7;
                }
                put(layout.refIndex, i, 16, index, sizeof(index));
            }
            const S32 neighbors[3][4] = { { 6, 3, 1, 12 }, { 15, 2, 11, 7 }, { 18, 5, 13, 0 } };
            for (size_t i = 0; i < 3; ++i)
            {
                put(layout.refNeighbor, i, 16, neighbors[i], sizeof(neighbors[i]));
            }
            for (S32 depth = 0; depth < 256; ++depth)
            {
                // The first probe a point this deep can lie in: none that ends nearer than it.
                S32 start = 1;
                while (start < PROBES && probes[start].near_depth + 2.f * probes[start].radius < (F32)depth &&
                       start < 3)
                {
                    ++start;
                }
                const S32 bucket[4] = { start, 0, 0, 0 };
                put(layout.refBucket, depth, 16, bucket, sizeof(bucket));
            }
            const S32 count = PROBES;
            put(layout.refmapCount, 0, 0, &count, sizeof(count));

            // A mirror's hero probe, a box turned a little about y or a sphere (mHeroShape), each holding part of the
            // points' reach, its cube the first probe's layer.
            {
                const F32 c = std::cos(0.4f);
                const F32 s = std::sin(0.4f);
                const LLVector3 centre(1.f, -2.f, -14.f);
                const LLVector3 rows[3] = { LLVector3(c / 9.f, 0.f, -s / 9.f), LLVector3(0.f, 1.f / 4.f, 0.f),
                                            LLVector3(s / 12.f, 0.f, c / 12.f) };
                F32 box[16] = {};
                for (S32 row = 0; row < 3; ++row)
                {
                    for (S32 col = 0; col < 3; ++col)
                    {
                        box[col * 4 + row] = rows[row].mV[col];
                    }
                    box[12 + row] = -(rows[row] * centre);
                }
                box[15] = 1.f;
                put(layout.heroBox, 0, 0, box, sizeof(box));
                const F32 sphere[4] = { 2.f, -1.f, -12.f, 12.f };
                put(layout.heroSphere, 0, 0, sphere, sizeof(sphere));
                const S32 shape = mHeroShape;
                put(layout.heroShape, 0, 0, &shape, sizeof(shape));
                const F32 max_lod = (F32)(LLImageGL::calcMipLevelCount(CUBE, CUBE) - 1);
                put(layout.heroMaxLod, 0, 0, &max_lod, sizeof(max_lod));
                const S32 heroes = 1;
                put(layout.heroProbeCount, 0, 0, &heroes, sizeof(heroes));
            }

            // The screen-space reflections' march, as the viewer's settings default it but for a wider bias, so the
            // scene's coarse texels are hit.
            const F32 march[6] = { 25.f, 0.1f, 0.3f, 0.001f, 4.f, 1.6f };
            const GLint march_offsets[6] = { layout.iterationCount, layout.rayStep, layout.distanceBias,
                                             layout.depthRejectBias, layout.glossySampleCount,
                                             layout.adaptiveStepMultiplier };
            for (size_t i = 0; i < 6; ++i)
            {
                put(march_offsets[i], 0, 0, &march[i], sizeof(F32));
            }

            mUBO.update(block.data(), block.size(), ALUniformBuffer::EUsage::STATIC);
        }

        // Draws `program` over the target, through entry point `api`.
        void draw(LLGLSLShader& program, S32 api)
        {
            // The triangle covers every pixel, so nothing is cleared.
            mTarget.bindTarget();
            program.bind();
            mUBO.bind(LLGLSLShader::UB_REFLECTION_PROBES);
            S32 channel = program.enableTexture(LLShaderMgr::REFLECTION_PROBES);
            REQUIRE(channel > -1);
            mCube->bind(channel);
            // The classic sky takes no irradiance from the probes, so no coefficients.
            channel = program.enableTexture(LLShaderMgr::SH_COEFFS);
            if (channel > -1)
            {
                mSH.bindTexture(0, channel, ALSamplers::PointClamp);
            }
            // The screen-space reflections' scene, and the hero probe's cube, where the programs take them.
            program.bindTexture(LLShaderMgr::SCENE_MAP, &mScene);
            program.bindDepthTexture(LLShaderMgr::SCENE_DEPTH, &mScene);
            channel = program.enableTexture(LLShaderMgr::HERO_PROBE);
            if (channel > -1)
            {
                mCube->bind(channel);
            }
            const LLMatrix4a identity = LLMatrix4a::identity();
            program.uniformMatrix4fv(LLShaderMgr::MODELVIEW_DELTA_MATRIX, identity);
            program.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_DELTA_MATRIX, identity);
            program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)mW, (F32)mH);
            program.uniform4f(LLShaderMgr::CLIP_PLANE, 0.f, 1.f, 0.2f, 3.f);
            // A turn about an axis off every one of the environment's, so a tap's direction matters in all three.
            const F32 env[9] = { 0.936f, 0.290f, -0.199f, -0.249f, 0.950f, 0.189f, 0.244f, -0.123f, 0.962f };
            program.uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, GL_FALSE, env);
            program.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, (F32)(LLImageGL::calcMipLevelCount(CUBE, CUBE) - 1));
            program.uniform1i(LLShaderMgr::CUBE_SNAPSHOT, 0);
            program.uniform1i(LLStaticHashedString("probe_test_api"), api);
            const GLint surface = program.getUniformLocation(LLStaticHashedString("probe_test_surface"));
            if (surface > -1)
            {
                glUniform3f(surface, (F32)mW, (F32)mH, mSurface ? 1.f : 0.f);
            }
            {
                LLGLDisable blend(GL_BLEND);
                LLGLDepthTest depth(GL_FALSE, GL_FALSE);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            // Nothing left bound for the textures' names to outlive: the scene releases them, and a texture made
            // after under a name they had must not find its unit already holding it.
            for (S32 uniform : { (S32)LLShaderMgr::REFLECTION_PROBES, (S32)LLShaderMgr::SH_COEFFS,
                                 (S32)LLShaderMgr::SCENE_MAP, (S32)LLShaderMgr::SCENE_DEPTH,
                                 (S32)LLShaderMgr::HERO_PROBE })
            {
                program.unbindTexture(uniform);
            }
            program.unbind();
            mTarget.flush();
        }

        // The three taps the last draw left, the ambient's, the gloss's and the legacy environment's, each a target.
        std::vector<F32> read() const
        {
            const size_t texels = size_t(mW) * mH * 4;
            std::vector<F32> taps(texels * 3);
            for (U32 attachment = 0; attachment < 3; ++attachment)
            {
                ll_test::readTexture(mTarget.getTexture(attachment), GL_RGBA, GL_FLOAT,
                                     &taps[size_t(attachment) * texels]);
            }
            return taps;
        }

        ll_test::SharedGLScope    mGL;
        ll_test::TestShaderDir    mDir;
        ll_test::TestShaderMgr    mShaders;
        LLGLSLShader              mFused;
        LLGLSLShader              mListed;
        LLRenderTarget            mTarget;
        LLRenderTarget            mSH;
        LLRenderTarget            mScene; ///< what the screen-space reflections march: a colour and its depth
        LLPointer<LLCubeMapArray> mCube;
        ALUniformBuffer           mUBO;
        LLPointer<LLVertexBuffer> mTriangle;
        S32                       mW = 0;
        S32                       mH = 0;
        bool                      mSurface = false; ///< the surface in place of the hash's points
        S32                       mHeroShape = 0;   ///< the hero probe's heroShape: 0 a box, 1 a sphere
        std::string               mSSRStub;         ///< class1/deferred/screenSpaceReflUtil.glsl
        std::string               mSSRMarch;        ///< class3/deferred/screenSpaceReflUtil.glsl
    };
}
