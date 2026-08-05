/**
 * @file class1/deferred/reflectionProbesBlock.glsl
 * @brief THE declaration of the shared "ReflectionProbes" engine UBO
 *        (UB_REFLECTION_PROBES). Not compiled directly: loadShaderFile() splices
 *        this file's contents wherever a shader says
 *        //[ENGINE_BLOCK ReflectionProbes], so every consumer shares one
 *        declaration instead of a per-file copy.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 * $/LicenseInfo$
 *
 * The member order/types MUST byte-match (std140) the C++
 * LLReflectionMapManager::ReflectionProbeData, packed in updateUniforms().
 */

#define MAX_REFMAP_COUNT 256  // must match LL_MAX_REFLECTION_PROBE_COUNT

layout (std140) uniform ReflectionProbes
{
    // list of OBBs for user override probes
    // box is a set of 3 planes outward facing planes and the depth of the box along that plane
    // for each box refBox[i]...
    /// box[0..2] - plane 0 .. 2 in [A,B,C,D] notation
    //  box[3][0..2] - plane thickness
    mat4 refBox[MAX_REFMAP_COUNT];
    mat4 heroBox;
    // list of bounding spheres for reflection probes sorted by distance to camera (closest first)
    vec4 refSphere[MAX_REFMAP_COUNT];
    // extra parameters
    //  x - irradiance scale
    //  y - radiance scale
    //  z - fade in
    //  w - znear
    vec4 refParams[MAX_REFMAP_COUNT];
    vec4 heroSphere;
    // index  of cube map in reflectionProbes for a corresponding reflection probe
    // e.g. cube map channel of refSphere[2] is stored in refIndex[2]
    // refIndex.x - cubemap channel in reflectionProbes
    // refIndex.y - index in refNeighbor of neighbor list (index is ivec4 index, not int index)
    // refIndex.z - number of neighbors
    // refIndex.w - priority, if negative, this probe has a box influence
    ivec4 refIndex[MAX_REFMAP_COUNT];

    // neighbor list data (refSphere indices, not cubemap array layer)
    ivec4 refNeighbor[1024];

    ivec4 refBucket[256];

    // number of reflection probes present in refSphere
    int refmapCount;

    int heroShape;
    int heroMipCount;
    int heroProbeCount;

    // Screen-space reflection march parameters (see ReflectionProbeData). noiseSine is NOT
    // here -- it advances per bind and stays a loose uniform.
    float iterationCount;
    float rayStep;
    float distanceBias;
    float depthRejectBias;
    float glossySampleCount;
    float adaptiveStepMultiplier;
};
