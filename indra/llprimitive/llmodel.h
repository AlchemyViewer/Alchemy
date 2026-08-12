/**
 * @file llmodel.h
 * @brief Model handling class definitions
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLMODEL_H
#define LL_LLMODEL_H

#include "llpointer.h"
#include "llvolume.h"
#include "v4math.h"
#include "m4math.h"
#include <queue>
#include <unordered_map>

class daeElement;
class domMesh;

#define MAX_MODEL_FACES 8

// Thread-safe refcount because the mesh repository shares one instance between the main
// thread and the mesh worker threads rather than mirroring it -- see mFrozen below.
class alignas(16) LLMeshSkinInfo : public LLThreadSafeRefCount
{
    LL_ALIGN_NEW
public:
    LLMeshSkinInfo();
    LLMeshSkinInfo(LLSD& data);
    LLMeshSkinInfo(const LLUUID& mesh_id, LLSD& data);
    void fromLLSD(LLSD& data);
    LLSD asLLSD(bool include_joints, bool lock_scale_if_joint_position) const;
    void updateHash();
    U32 sizeBytes() const;

    LLUUID mMeshID;
    std::vector<std::string> mJointNames;
    mutable std::vector<S32> mJointNums;
    typedef std::vector<LLMatrix4a> matrix_list_t;
    matrix_list_t mInvBindMatrix;

    // bones/joints position overrides
    matrix_list_t mAlternateBindMatrix;

    // cached multiply of mBindShapeMatrix and mInvBindMatrix
    matrix_list_t mBindPoseMatrix;

    LLMatrix4a mBindShapeMatrix;

    float mPelvisOffset;
    bool mLockScaleIfJointPosition;
    bool mInvalidJointsScrubbed;
    bool mJointNumsInitialized;
    U64 mHash = 0;

    // Set before this skin is handed to more than one thread. mJointNums and
    // mJointNumsInitialized are the only fields written after construction, by
    // LLSkinningUtil::initJointNums(); freezing resolves them once against the skeleton
    // and makes that function a no-op, so a frozen skin has no mutable state left and
    // the same instance can be shared instead of copied.
    //
    // It says nothing about who currently holds the skin. Do not read it as a proxy for
    // membership of any cache.
    bool mFrozen = false;
};

class alignas(16) LLModel : public LLVolume
{
    LL_ALIGN_NEW
public:

    enum
    {
        LOD_IMPOSTOR = 0,
        LOD_LOW,
        LOD_MEDIUM,
        LOD_HIGH,
        LOD_PHYSICS,
        NUM_LODS
    };

    enum EModelStatus
    {
        NO_ERRORS = 0,
        VERTEX_NUMBER_OVERFLOW, //vertex number is >= 65535.
        BAD_ELEMENT,
        INVALID_STATUS
    } ;

    //convex_hull_decomposition is a vector of convex hulls
    //each convex hull is a set of points
    typedef std::vector<std::vector<LLVector3> > convex_hull_decomposition;
    typedef std::vector<LLVector3> hull;

    class PhysicsMesh
    {
    public:
        std::vector<LLVector3> mPositions;
        std::vector<LLVector3> mNormals;

        ~PhysicsMesh() {}

        void clear()
        {
            mPositions.clear();
            mNormals.clear();
        }

        bool empty() const
        {
            return mPositions.empty();
        }

        U32 sizeBytes() const
        {
            U32 res = sizeof(std::vector<LLVector3>) * 2;
            res += sizeof(LLVector3) * static_cast<U32>(mPositions.size());
            res += sizeof(LLVector3) * static_cast<U32>(mNormals.size());
            return res;
        }
    };

    class Decomposition
    {
    public:
        Decomposition() { }
        Decomposition(LLSD& data);
        ~Decomposition() { }
        void fromLLSD(LLSD& data);
        LLSD asLLSD() const;
        bool hasHullList() const;
        U32 sizeBytes() const;

        void merge(const Decomposition* rhs);

        LLUUID mMeshID;
        LLModel::convex_hull_decomposition mHull;
        LLModel::hull mBaseHull;

        std::vector<LLModel::PhysicsMesh> mMesh;
        LLModel::PhysicsMesh mBaseHullMesh;
        LLModel::PhysicsMesh mPhysicsShapeMesh;
    };

    LLModel(const LLVolumeParams& params, F32 detail);
    ~LLModel();

    bool loadModel(std::istream& is);
    bool loadSkinInfo(LLSD& header, std::istream& is);
    bool loadDecomposition(LLSD& header, std::istream& is);

    enum EWriteModelMode
    {
        WRITE_NO = 0,
        WRITE_BINARY,
        WRITE_HUMAN,
    };
    static LLSD writeModel(
        std::ostream& ostr,
        LLModel* physics,
        LLModel* high,
        LLModel* medium,
        LLModel* low,
        LLModel* imposotr,
        const LLModel::Decomposition& decomp,
        bool upload_skin,
        bool upload_joints,
        bool lock_scale_if_joint_position,
        EWriteModelMode write_mode = WRITE_BINARY,
        bool as_slm = false,
        int submodel_id = 0);

    static LLSD writeModelToStream(
        std::ostream& ostr,
        LLSD& mdl,
        EWriteModelMode write_mode = WRITE_BINARY, bool as_slm = false);

    void ClearFacesAndMaterials() { mVolumeFaces.clear(); mMaterialList.clear(); }

    std::string getName() const;
    EModelStatus getStatus() const {return mStatus;}
    static std::string getStatusString(U32 status) ;

    void setNumVolumeFaces(S32 count);
    void setVolumeFaceData(
        S32 f,
        LLStrider<LLVector3> pos,
        LLStrider<LLVector3> norm,
        LLStrider<LLVector2> tc,
        LLStrider<U16> ind,
        U32 num_verts,
        U32 num_indices);

    void generateNormals(F32 angle_cutoff);

    void addFace(const LLVolumeFace& face);

    void sortVolumeFacesByMaterialName();
    void normalizeVolumeFaces();
    void normalizeVolumeFacesAndWeights();
    void trimVolumeFacesToSize(U32 new_count = LL_SCULPT_MESH_MAX_FACES, LLVolume::face_list_t* remainder = NULL);
    void remapVolumeFaces();
    void optimizeVolumeFaces();
    void offsetMesh( const LLVector3& pivotPoint );
    void getNormalizedScaleTranslation(LLVector3& scale_out, LLVector3& translation_out) const;
    LLVector3 getTransformedCenter(const LLMatrix4& mat);

    //reorder face list based on mMaterialList in this and reference so
    //order matches that of reference (material ordering touchup)
    bool matchMaterialOrder(LLModel* ref, int& refFaceCnt, int& modelFaceCnt );
    bool isMaterialListSubset( LLModel* ref );
    bool needToAddFaces( LLModel* ref, int& refFaceCnt, int& modelFaceCnt );

    typedef std::vector<std::string> material_list;

    material_list mMaterialList;

    material_list& getMaterialList() { return mMaterialList; }

    //data used for skin weights
    class JointWeight
    {
    public:
        S32 mJointIdx;
        F32 mWeight;

        JointWeight()
        {
            mJointIdx = 0;
            mWeight = 0.f;
        }

        JointWeight(S32 idx, F32 weight)
            : mJointIdx(idx), mWeight(weight)
        {
        }

        bool operator<(const JointWeight& rhs) const
        {
            if (mWeight == rhs.mWeight)
            {
                return mJointIdx < rhs.mJointIdx;
            }

            return mWeight < rhs.mWeight;
        }

    };

    struct CompareWeightGreater
    {
        bool operator()(const JointWeight& lhs, const JointWeight& rhs)
        {
            return rhs < lhs; // strongest = first
        }
    };

    //Are the doubles the same w/in epsilon specified tolerance
    bool areEqual( double a, double b )
    {
        const float epsilon = 1e-5f;
        return fabs(a - b) < epsilon;
    }

    //Make sure that we return false for any values that are within the tolerance for equivalence
    bool jointPositionalLookup( const LLVector3& a, const LLVector3& b )
    {
        const float epsilon = 1e-5f;
        return (a - b).length() < epsilon;
    }

    //copy of position array for this model -- mPosition[idx].mV[X,Y,Z]
    std::vector<LLVector3> mPosition;

    //map of positions to skin weights --- mSkinWeights[pos].mV[0..4] == <joint_index>.<weight>
    //joint_index corresponds to mJointList
    typedef std::vector<JointWeight> weight_list;
    typedef std::map<LLVector3, weight_list > weight_map;
    weight_map mSkinWeights;

    //get list of weight influences closest to given position
    weight_list& getJointInfluences(const LLVector3& pos);

    // O(1) accelerator for getJointInfluences(). That function linearly scans
    // mSkinWeights, so calling it once per vertex (writeModel, LOD vertex-buffer
    // fill, local-mesh preview) is O(V^2) and stalls the main thread for seconds
    // on a dense rigged mesh. Build one of these once before a per-vertex loop,
    // then call influences() per vertex for an O(1) lookup -- making the weight
    // pass O(V). It snapshots pointers into the model's current mSkinWeights, so
    // construct it after the weights are final and do not mutate mSkinWeights
    // while it is alive. A position with no key within the weld epsilon falls
    // back to getJointInfluences() (preserving its exact-find / closest-point
    // path), so results are identical to calling that function directly.
    class JointWeightCache
    {
    public:
        explicit JointWeightCache(LLModel& model);
        const weight_list& influences(const LLVector3& pos) const;

    private:
        static constexpr F32 WELD_EPSILON = 1e-5f; // == jointPositionalLookup()'s tolerance
        struct CellKey
        {
            S32 x, y, z;
            bool operator==(const CellKey& o) const { return x == o.x && y == o.y && z == o.z; }
        };
        struct CellHash
        {
            size_t operator()(const CellKey& k) const
            {
                return (size_t)(((U32)k.x * 73856093u) ^ ((U32)k.y * 19349663u) ^ ((U32)k.z * 83492791u));
            }
        };
        static CellKey cellKey(const LLVector3& p);

        LLModel& mModel;
        std::unordered_map<CellKey, std::vector<const weight_map::value_type*>, CellHash> mCells;
    };

    LLMeshSkinInfo mSkinInfo;

    std::string mRequestedLabel; // name requested in UI, if any.
    std::string mLabel; // name computed from dae.

    LLVector3 mNormalizedScale;
    LLVector3 mNormalizedTranslation;

    float   mPelvisOffset;
    // convex hull decomposition
    S32 mDecompID;

    void setConvexHullDecomposition(
        const convex_hull_decomposition& decomp,
        const std::vector<LLModel::PhysicsMesh>& decomp_mesh);
    void updateHullCenters();

    LLVector3 mCenterOfHullCenters;
    std::vector<LLVector3> mHullCenter;
    U32 mHullPoints;

    //ID for storing this model in a .slm file
    S32 mLocalID;

    Decomposition mPhysics;

    EModelStatus mStatus ;

    // A model/object can only have 8 faces, spillover faces will
    // be moved to new model/object and assigned a submodel id.
    int mSubmodelID;
};

typedef std::vector<LLPointer<LLModel> >    model_list;
typedef std::queue<LLPointer<LLModel> > model_queue;

class LLModelMaterialBase
{
public:
    std::string mDiffuseMapFilename;
    std::string mDiffuseMapLabel;
    std::string mBinding;
    LLColor4        mDiffuseColor;
    bool            mFullbright;

    LLModelMaterialBase()
        : mFullbright(false)
    {
        mDiffuseColor.set(1,1,1,1);
    }
};

class LLImportMaterial : public LLModelMaterialBase
{
public:
    friend class LLMeshUploadThread;
    friend class LLModelPreview;

    bool operator<(const LLImportMaterial &params) const;

    LLImportMaterial() : LLModelMaterialBase()
    {
        mDiffuseColor.set(1,1,1,1);
    }

    LLImportMaterial(LLSD& data);
    virtual ~LLImportMaterial();

    LLSD asLLSD();

    const LLUUID&   getDiffuseMap() const                   { return mDiffuseMapID;     }
    void                setDiffuseMap(const LLUUID& texId)  { mDiffuseMapID = texId;    }

protected:

    LLUUID      mDiffuseMapID;
    void*       mOpaqueData{ nullptr };    // allow refs to viewer/platform-specific structs for each material
    // currently only stores an LLPointer< LLViewerFetchedTexture > > to
    // maintain refs to textures associated with each material for free
    // ref counting.
};

typedef std::map<std::string, LLImportMaterial> material_map;

class LLModelInstanceBase
{
public:
    LLPointer<LLModel> mModel;
    LLPointer<LLModel> mLOD[LLModel::NUM_LODS];
    LLUUID mMeshID;

    LLMatrix4 mTransform;
    material_map mMaterial;

    LLModelInstanceBase(LLModel* model, const LLMatrix4& transform, const material_map& materials)
        : mModel(model), mTransform(transform), mMaterial(materials)
    {
    }

    LLModelInstanceBase()
        : mModel(NULL)
    {
    }

    virtual ~LLModelInstanceBase()
    {
        mModel = NULL;
        for (int j = 0; j < LLModel::NUM_LODS; ++j)
        {
            mLOD[j] = NULL;
        }
    };
};

typedef std::vector<LLModelInstanceBase> model_instance_list;

class LLModelInstance : public LLModelInstanceBase
{
public:
    std::string mLabel;
    LLUUID mMeshID;
    S32 mLocalMeshID;

    LLModelInstance(LLModel* model, const std::string& label, const LLMatrix4& transform, const material_map& materials)
        : LLModelInstanceBase(model, transform, materials), mLabel(label)
    {
        mLocalMeshID = -1;
    }

    LLModelInstance(LLSD& data);

    ~LLModelInstance() {}

    LLSD asLLSD();
};

#define LL_DEGENERACY_TOLERANCE  1e-7f

inline F32 dot3fpu(const LLVector4a& a, const LLVector4a& b)
{
    volatile F32 p0 = a[0] * b[0];
    volatile F32 p1 = a[1] * b[1];
    volatile F32 p2 = a[2] * b[2];
    return p0 + p1 + p2;
}

bool ll_is_degenerate(const LLVector4a& a, const LLVector4a& b, const LLVector4a& c, F32 tolerance = LL_DEGENERACY_TOLERANCE);

bool validate_face(const LLVolumeFace& face);
bool validate_model(const LLModel* mdl);

#endif //LL_LLMODEL_H
