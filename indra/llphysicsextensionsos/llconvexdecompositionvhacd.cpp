/**
* @file   llconvexdecompositionvhacd.cpp
* @author rye@alchemyviewer.org
* @brief  A VHACD based implementation of LLConvexDecomposition
*
* $LicenseInfo:firstyear=2025&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2025, Linden Research, Inc.
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

#include "linden_common.h"

#include "llmath.h"
#include "v3math.h"

#include <string.h>
#include <memory>

#define ENABLE_VHACD_IMPLEMENTATION 1
#include "VHACD.h"

#include "llconvexdecompositionvhacd.h"

constexpr S32 MAX_HULLS = 256;
constexpr S32 MAX_VERTICES_PER_HULL = 256;

// Hard ceiling on voxel resolution to keep a stray int from blowing up memory via int->uint32_t conversion.
constexpr S32 MIN_VOXEL_RESOLUTION = 10000;
constexpr S32 MAX_VOXEL_RESOLUTION = 16000000;

// Vertex/Triangle are reinterpret_cast'd through double*/uint32_t* when handed to VHACD::Compute.
// Lock in the layout so a future field addition fails the build instead of silently misreading the mesh.
static_assert(sizeof(VHACD::Vertex) == 3 * sizeof(double), "VHACD::Vertex must be tightly packed");
static_assert(alignof(VHACD::Vertex) == alignof(double), "VHACD::Vertex must be double-aligned");
static_assert(sizeof(VHACD::Triangle) == 3 * sizeof(uint32_t), "VHACD::Triangle must be tightly packed");
static_assert(alignof(VHACD::Triangle) == alignof(uint32_t), "VHACD::Triangle must be uint32_t-aligned");

thread_local int LLConvexDecompositionVHACD::sBoundDecompID = LLConvexDecompositionVHACD::INVALID_DECOMP_ID;

bool LLConvexDecompositionVHACD::isFunctional()
{
    return true;
}

LLConvexDecomposition* LLConvexDecompositionVHACD::getInstance()
{
    return LLSimpleton::getInstance();
}

LLCDResult LLConvexDecompositionVHACD::initSystem()
{
    createInstance();
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::initThread()
{
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::quitThread()
{
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::quitSystem()
{
    deleteSingleton();
    return LLCD_OK;
}

LLConvexDecompositionVHACD::LLConvexDecompositionVHACD()
{
    // Setup default parameters
    mVHACDParameters.m_logger = &mVHACDLogger;
    // We use CreateVHACD() (synchronous); make the params struct reflect that so it isn't misleading.
    mVHACDParameters.m_asyncACD = false;

    mDecompStages[0].mName = "Analyze";
    mDecompStages[0].mDescription = "Voxelize the source mesh and decompose it into approximating convex hulls.";

    LLCDParam param;
    param.mName = "Fill Mode";
    param.mDescription = "How interior voxels are classified: Flood (default, watertight meshes), Surface Only (hollow shells), or Raycast (meshes with holes).";
    param.mType = LLCDParam::LLCD_ENUM;
    param.mDetails.mEnumValues.mNumEnums = 3;

    // Index by ordinal (not by enum value) so reordering/adding values upstream can't OOB-write this array.
    static LLCDParam::LLCDEnumItem fill_enums[3];
    fill_enums[0].mName = "Flood";
    fill_enums[0].mValue = (int)VHACD::FillMode::FLOOD_FILL;
    fill_enums[1].mName = "Surface Only";
    fill_enums[1].mValue = (int)VHACD::FillMode::SURFACE_ONLY;
    fill_enums[2].mName = "Raycast";
    fill_enums[2].mValue = (int)VHACD::FillMode::RAYCAST_FILL;

    param.mDetails.mEnumValues.mEnumsArray = fill_enums;
    param.mDefault.mIntOrEnumValue = (int)VHACD::FillMode::FLOOD_FILL;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    enum EVoxelQualityLevels
    {
        E_LOW_QUALITY = 0,
        E_NORMAL_QUALITY,
        E_HIGH_QUALITY,
        E_VERY_HIGH_QUALITY,
        E_ULTRA_QUALITY,
        E_MAX_QUALITY,
        E_NUM_QUALITY_LEVELS
    };

    param.mName = "Voxel Resolution";
    param.mDescription = "Voxel grid resolution for the source mesh; higher values capture finer detail at the cost of compute time and memory.";
    param.mType = LLCDParam::LLCD_ENUM;
    param.mDetails.mEnumValues.mNumEnums = E_NUM_QUALITY_LEVELS;

    static LLCDParam::LLCDEnumItem voxel_quality_enums[E_NUM_QUALITY_LEVELS];
    voxel_quality_enums[E_LOW_QUALITY].mName = "Low";
    voxel_quality_enums[E_LOW_QUALITY].mValue = 200000;
    voxel_quality_enums[E_NORMAL_QUALITY].mName = "Normal";
    voxel_quality_enums[E_NORMAL_QUALITY].mValue = 400000;
    voxel_quality_enums[E_HIGH_QUALITY].mName = "High";
    voxel_quality_enums[E_HIGH_QUALITY].mValue = 800000;
    voxel_quality_enums[E_VERY_HIGH_QUALITY].mName = "Very High";
    voxel_quality_enums[E_VERY_HIGH_QUALITY].mValue = 1200000;
    voxel_quality_enums[E_ULTRA_QUALITY].mName = "Ultra";
    voxel_quality_enums[E_ULTRA_QUALITY].mValue = 1600000;
    voxel_quality_enums[E_MAX_QUALITY].mName = "Maximum";
    voxel_quality_enums[E_MAX_QUALITY].mValue = 2000000;

    param.mDetails.mEnumValues.mEnumsArray = voxel_quality_enums;
    param.mDefault.mIntOrEnumValue = 400000;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    param.mName = "Num Hulls";
    param.mDescription = "Maximum number of convex hulls produced. More hulls capture concavity better but raise simulation cost and upload land impact.";
    param.mType = LLCDParam::LLCD_FLOAT;
    param.mDetails.mRange.mLow.mFloat = 1.f;
    param.mDetails.mRange.mHigh.mFloat = MAX_HULLS;
    param.mDetails.mRange.mDelta.mFloat = 1.f;
    param.mDefault.mFloat = 8.f;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    param.mName = "Num Vertices";
    param.mDescription = "Maximum vertices per convex hull. Higher values let each hull better approximate curved surfaces.";
    param.mType = LLCDParam::LLCD_FLOAT;
    param.mDetails.mRange.mLow.mFloat = 3.f;
    param.mDetails.mRange.mHigh.mFloat = MAX_VERTICES_PER_HULL;
    param.mDetails.mRange.mDelta.mFloat = 1.f;
    param.mDefault.mFloat = 32.f;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    param.mName = "Error Tolerance";
    param.mDescription = "Allowed volume error per hull, expressed as a percentage. Stop subdividing once a hull is within this much of its source volume; smaller is more accurate but slower.";
    param.mType = LLCDParam::LLCD_FLOAT;
    param.mDetails.mRange.mLow.mFloat = 0.0001f;
    // Capped at 10%; anything higher produces hulls so coarse they're useless and the runtime clamp
    // in setParam tops out at 100 anyway.
    param.mDetails.mRange.mHigh.mFloat = 10.f;
    param.mDetails.mRange.mDelta.mFloat = 0.001f;
    param.mDefault.mFloat = 1.f;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    // Max Recursion Depth: VHACD doubles candidate hull count per level (capped by Num Hulls).
    // Default 10 matches VHACD; raising helps capture concavity on complex meshes at the cost of compute time.
    param.mName = "Max Recursion Depth";
    param.mDescription = "Maximum depth of the voxel-split tree. Each extra level doubles the candidate hull count (final count still capped by Num Hulls); raise for complex concave shapes.";
    param.mType = LLCDParam::LLCD_FLOAT;
    param.mDetails.mRange.mLow.mFloat = 1.f;
    param.mDetails.mRange.mHigh.mFloat = 20.f;
    param.mDetails.mRange.mDelta.mFloat = 1.f;
    param.mDefault.mFloat = 10.f;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    // Shrink Wrap: when true (VHACD default), hull vertices are projected back onto the source mesh
    // for a tighter fit. Disabling is faster but leaves voxel-grid stair-steps visible on the hull.
    param.mName = "Shrink Wrap";
    param.mDescription = "Project hull vertices back onto the source mesh for a tighter fit. Disable for faster decomposition that leaves voxel-grid stair-steps on the hull surface.";
    param.mType = LLCDParam::LLCD_BOOLEAN;
    param.mDefault.mBool = 1;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    // Find Best Plane: VHACD experimental, default false. When on, VHACD searches for the optimal split
    // location at each recursion step instead of always splitting at the midpoint.
    param.mName = "Find Best Plane";
    param.mDescription = "Search for the best split plane at each recursion step instead of splitting at the midpoint. Produces cleaner cuts on organic meshes but costs extra compute time.";
    param.mType = LLCDParam::LLCD_BOOLEAN;
    param.mDefault.mBool = 0;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    // Min Edge Length: VHACD stops recursing once a voxel patch has edges shorter than 2x this value
    // on all three axes. Default 2; larger values yield coarser hulls and finish faster.
    param.mName = "Min Edge Length";
    param.mDescription = "Smallest voxel patch (in voxels per side) that VHACD will keep subdividing. Smaller values produce finer hulls at the cost of compute time.";
    param.mType = LLCDParam::LLCD_FLOAT;
    param.mDetails.mRange.mLow.mFloat = 1.f;
    param.mDetails.mRange.mHigh.mFloat = 8.f;
    param.mDetails.mRange.mDelta.mFloat = 1.f;
    param.mDefault.mFloat = 2.f;
    param.mStage = 0;
    param.mReserved = -1;
    mDecompParams.push_back(param);

    for (const LLCDParam& default_param : mDecompParams)
    {
        const char* const name = default_param.mName;

        switch (default_param.mType)
        {
        case LLCDParam::LLCD_FLOAT:
        {
            setParam(name, default_param.mDefault.mFloat);
            break;
        }
        case LLCDParam::LLCD_ENUM:
        case LLCDParam::LLCD_INTEGER:
        {
            setParam(name, default_param.mDefault.mIntOrEnumValue);
            break;
        }
        case LLCDParam::LLCD_BOOLEAN:
        {
            setParam(name, (default_param.mDefault.mBool != 0));
            break;
        }
        case LLCDParam::LLCD_INVALID:
        default:
        {
            break;
        }
        }
    }
}

LLConvexDecompositionVHACD::~LLConvexDecompositionVHACD()
{
    LLMutexLock lock(&mDecompDataMutex);
    // sBoundDecompID is thread_local; per-thread values fall away with thread exit.
    // After the map is cleared, any subsequent getBoundDecomp() returns null regardless.
    mDecompData.clear();
}

void LLConvexDecompositionVHACD::genDecomposition(int& decomp)
{
    LLMutexLock lock(&mDecompDataMutex);

    mDecompData[mNextDecompID] = std::make_shared<LLDecompData>();
    decomp = mNextDecompID;

    ++mNextDecompID; // Increment decomposition ID. Never reuse to protect downstream consumers from misuse
}

void LLConvexDecompositionVHACD::deleteDecomposition(int decomp)
{
    LLMutexLock lock(&mDecompDataMutex);

    auto iter = mDecompData.find(decomp);
    if (iter != mDecompData.end())
    {
        if (sBoundDecompID == decomp)
        {
            sBoundDecompID = INVALID_DECOMP_ID;
        }
        mDecompData.erase(iter);
    }
}

void LLConvexDecompositionVHACD::bindDecomposition(int decomp)
{
    LLMutexLock lock(&mDecompDataMutex);

    if (mDecompData.contains(decomp))
    {
        sBoundDecompID = decomp;
    }
    else
    {
        LL_WARNS() << "Failed to bind unknown decomposition: " << decomp << LL_ENDL;
        sBoundDecompID = INVALID_DECOMP_ID;
    }
}

LLConvexDecompositionVHACD::data_ptr_t LLConvexDecompositionVHACD::getBoundDecomp()
{
    data_ptr_t bound_decomp;
    {
        LLMutexLock lock(&mDecompDataMutex);
        auto it = mDecompData.find(sBoundDecompID);
        if (it != mDecompData.end())
        {
            bound_decomp = it->second; // Take a copy of the shared_ptr to avoid potential deletion
        }
    }
    return bound_decomp;
}

LLCDResult LLConvexDecompositionVHACD::setParam(const char* name, float val)
{
    LLMutexLock lock(&mParamsMutex);

    using namespace std::literals;

    if (name == "Num Hulls"sv)
    {
        mVHACDParameters.m_maxConvexHulls = llclamp(ll_round(val), 1, MAX_HULLS);
    }
    else if (name == "Num Vertices"sv)
    {
        mVHACDParameters.m_maxNumVerticesPerCH = llclamp(ll_round(val), 3, MAX_VERTICES_PER_HULL);
    }
    else if (name == "Error Tolerance"sv)
    {
        mVHACDParameters.m_minimumVolumePercentErrorAllowed = llclamp(val, 0.0001f, 100.f);
    }
    else if (name == "Max Recursion Depth"sv)
    {
        mVHACDParameters.m_maxRecursionDepth = (uint32_t)llclamp(ll_round(val), 1, 20);
    }
    else if (name == "Min Edge Length"sv)
    {
        mVHACDParameters.m_minEdgeLength = (uint32_t)llclamp(ll_round(val), 1, 8);
    }
    else
    {
        return LLCD_UNKNOWN_PARAM;
    }
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::setParam(const char* name, bool val)
{
    LLMutexLock lock(&mParamsMutex);

    using namespace std::literals;

    if (name == "Shrink Wrap"sv)
    {
        mVHACDParameters.m_shrinkWrap = val;
    }
    else if (name == "Find Best Plane"sv)
    {
        mVHACDParameters.m_findBestPlane = val;
    }
    else
    {
        return LLCD_UNKNOWN_PARAM;
    }
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::setParam(const char* name, int val)
{
    LLMutexLock lock(&mParamsMutex);

    using namespace std::literals;

    if (name == "Fill Mode"sv)
    {
        if (val < (int)VHACD::FillMode::FLOOD_FILL || val > (int)VHACD::FillMode::RAYCAST_FILL)
        {
            return LLCD_BAD_VALUE;
        }
        mVHACDParameters.m_fillMode = (VHACD::FillMode)val;
    }
    else if (name == "Voxel Resolution"sv)
    {
        // m_resolution is uint32_t; clamp the signed input so negatives don't wrap to billions of voxels.
        mVHACDParameters.m_resolution = (uint32_t)llclamp(val, MIN_VOXEL_RESOLUTION, MAX_VOXEL_RESOLUTION);
    }
    else
    {
        return LLCD_UNKNOWN_PARAM;
    }
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::setMeshData( const LLCDMeshData* data, bool vertex_based )
{
    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp)
    {
        return LLCD_NULL_PTR;
    }

    return bound_decomp->mSourceMesh.from(data, vertex_based);
}

LLCDResult LLConvexDecompositionVHACD::registerCallback(int stage, llcdCallbackFunc callback )
{
    if (stage == 0)
    {
        LLMutexLock lock(&mParamsMutex);
        mCurrentCallbackFunc = callback;
        return LLCD_OK;
    }
    else
    {
        return LLCD_INVALID_STAGE;
    }
}

LLCDResult LLConvexDecompositionVHACD::executeStage(int stage)
{
    if (stage != 0)
    {
        return LLCD_INVALID_STAGE;
    }

    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp)
    {
        return LLCD_NULL_PTR;
    }

    bound_decomp->mDecomposedHulls.clear();

    const auto& decomp_mesh = bound_decomp->mSourceMesh;

    VHACDCallback callbacks;
    VHACD::IVHACD::Parameters current_params;
    {
        LLMutexLock lock(&mParamsMutex);

        current_params = mVHACDParameters;
        callbacks.setCallbackFunc(mCurrentCallbackFunc);
    }
    current_params.m_callback = &callbacks;

    // RAII handle so we don't leak the IVHACD on any return path; IVHACD has a non-virtual public destructor
    // and is freed exclusively through Release().
    auto vhacd_deleter = [](VHACD::IVHACD* p) { if (p) p->Release(); };
    std::unique_ptr<VHACD::IVHACD, decltype(vhacd_deleter)> vhacd_impl(VHACD::CreateVHACD(), vhacd_deleter);
    if (!vhacd_impl)
    {
        LL_WARNS() << "Failed to create VHACD instance" << LL_ENDL;
        return LLCD_NULL_PTR;
    }

    // Give the progress callback a handle so a zero-returning user callback can cooperatively cancel the run.
    callbacks.setCancelTarget(vhacd_impl.get());

    if (!vhacd_impl->Compute((const double*)decomp_mesh.mVertices.data(), static_cast<uint32_t>(decomp_mesh.mVertices.size()),
                             (const uint32_t*)decomp_mesh.mIndices.data(), static_cast<uint32_t>(decomp_mesh.mIndices.size()),
                             current_params))
    {
        return LLCD_INVALID_HULL_DATA;
    }

    uint32_t num_convex_hulls = vhacd_impl->GetNConvexHulls();
    if (num_convex_hulls == 0)
    {
        return LLCD_INVALID_HULL_DATA;
    }

    // Pull all hulls and tally volume so we can filter against the total.
    std::vector<VHACD::IVHACD::ConvexHull> hulls;
    hulls.reserve(num_convex_hulls);
    double total_volume = 0.0;
    for (uint32_t i = 0; i < num_convex_hulls; ++i)
    {
        VHACD::IVHACD::ConvexHull ch;
        if (!vhacd_impl->GetConvexHull(i, ch))
            continue;
        total_volume += ch.m_volume;
        hulls.push_back(std::move(ch));
    }

    // Physics engines reject hulls with fewer than 4 unique vertices (need a tetrahedron),
    // and sliver hulls below 0.01% of the total volume are noise that bloats the upload payload
    // without changing the simulated shape.
    constexpr size_t MIN_HULL_VERTICES = 4;
    const double volume_threshold = total_volume * 0.0001;
    for (auto& ch : hulls)
    {
        if (ch.m_points.size() < MIN_HULL_VERTICES || ch.m_volume < volume_threshold)
        {
            continue;
        }

        LLConvexMesh out_mesh;
        out_mesh.setVertices(ch.m_points);
        out_mesh.setIndices(ch.m_triangles);

        bound_decomp->mDecomposedHulls.push_back(std::move(out_mesh));
    }

    if (bound_decomp->mDecomposedHulls.empty())
    {
        LL_WARNS("VHACD") << "All " << num_convex_hulls << " hulls were filtered as degenerate." << LL_ENDL;
        return LLCD_INVALID_HULL_DATA;
    }

    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::buildSingleHull()
{
    LL_INFOS() << "Building single hull mesh" << LL_ENDL;

    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp || bound_decomp->mSourceMesh.mVertices.empty())
    {
        return LLCD_NULL_PTR;
    }

    bound_decomp->mSingleHullMesh.clear();

    VHACD::QuickHull quickhull;
    uint32_t num_tris = quickhull.ComputeConvexHull(bound_decomp->mSourceMesh.mVertices, MAX_VERTICES_PER_HULL);
    if (num_tris > 0)
    {
        bound_decomp->mSingleHullMesh.setVertices(quickhull.GetVertices());
        bound_decomp->mSingleHullMesh.setIndices(quickhull.GetIndices());

        return LLCD_OK;
    }

    return LLCD_INVALID_MESH_DATA;
}

int LLConvexDecompositionVHACD::getNumHullsFromStage(int stage)
{
    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp || stage != 0)
    {
        return 0;
    }

    return narrow(bound_decomp->mDecomposedHulls.size());
}

LLCDResult LLConvexDecompositionVHACD::getSingleHull( LLCDHull* hullOut )
{
    memset( hullOut, 0, sizeof(LLCDHull) );

    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp)
    {
        return LLCD_NULL_PTR;
    }

    if (bound_decomp->mSingleHullMesh.vertices.empty())
    {
        return LLCD_INVALID_HULL_DATA;
    }

    bound_decomp->mSingleHullMesh.to(hullOut);
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::getHullFromStage( int stage, int hull, LLCDHull* hullOut )
{
    memset( hullOut, 0, sizeof(LLCDHull) );

    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp)
    {
        return LLCD_NULL_PTR;
    }

    if (stage != 0)
    {
        return LLCD_INVALID_STAGE;
    }

    if (bound_decomp->mDecomposedHulls.empty() || S32(bound_decomp->mDecomposedHulls.size()) <= hull)
    {
        return LLCD_REQUEST_OUT_OF_RANGE;
    }

    bound_decomp->mDecomposedHulls[hull].to(hullOut);
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::getMeshFromStage( int stage, int hull, LLCDMeshData* meshDataOut )
{
    memset( meshDataOut, 0, sizeof(LLCDMeshData));

    data_ptr_t bound_decomp = getBoundDecomp();
    if (!bound_decomp)
    {
        return LLCD_NULL_PTR;
    }

    if (stage != 0)
    {
        return LLCD_INVALID_STAGE;
    }

    if (bound_decomp->mDecomposedHulls.empty() || S32(bound_decomp->mDecomposedHulls.size()) <= hull)
    {
        return LLCD_REQUEST_OUT_OF_RANGE;
    }

    bound_decomp->mDecomposedHulls[hull].to(meshDataOut);
    return LLCD_OK;
}

LLCDResult LLConvexDecompositionVHACD::getMeshFromHull( LLCDHull* hullIn, LLCDMeshData* meshOut )
{
    memset(meshOut, 0, sizeof(LLCDMeshData));

    LLVHACDMesh inMesh(hullIn);

    VHACD::QuickHull quickhull;
    uint32_t num_tris = quickhull.ComputeConvexHull(inMesh.mVertices, MAX_VERTICES_PER_HULL);
    if (num_tris > 0)
    {
        // thread_local: each caller thread gets its own scratch buffer so concurrent callers
        // don't stomp each other's pointer returned via meshOut.
        thread_local LLConvexMesh sMeshFromHullData;
        sMeshFromHullData.setVertices(quickhull.GetVertices());
        sMeshFromHullData.setIndices(quickhull.GetIndices());

        sMeshFromHullData.to(meshOut);
        return LLCD_OK;
    }

    return LLCD_INVALID_HULL_DATA;
}

LLCDResult LLConvexDecompositionVHACD::generateSingleHullMeshFromMesh(LLCDMeshData* meshIn, LLCDMeshData* meshOut)
{
    memset( meshOut, 0, sizeof(LLCDMeshData) );

    LLVHACDMesh inMesh(meshIn, true);

    VHACD::QuickHull quickhull;
    uint32_t num_tris = quickhull.ComputeConvexHull(inMesh.mVertices, MAX_VERTICES_PER_HULL);
    if (num_tris > 0)
    {
        // thread_local: see comment in getMeshFromHull.
        thread_local LLConvexMesh sSingleHullMeshFromMeshData;
        sSingleHullMeshFromMeshData.setVertices(quickhull.GetVertices());
        sSingleHullMeshFromMeshData.setIndices(quickhull.GetIndices());

        sSingleHullMeshFromMeshData.to(meshOut);
        return LLCD_OK;
    }

    return LLCD_INVALID_MESH_DATA;
}

void LLConvexDecompositionVHACD::loadMeshData(const char* fileIn, LLCDMeshData** meshDataOut)
{
    static LLCDMeshData meshData;
    memset( &meshData, 0, sizeof(LLCDMeshData) );
    *meshDataOut = &meshData;
}
