/**
 * @file lllocalmesh.cpp
 * @brief Local Mesh preview source
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "llviewerprecompiledheaders.h"

#include "lllocalmesh.h"

/* model loaders (shared with the mesh upload path) */
#include "lldaeloader.h"
#include "gltf/llgltfloader.h"
#include "lljointdata.h"
#include "llskinningutil.h"

/* geometry */
#include "llmatrix4a.h"
#include "llvolume.h"
#include "llvolumemgr.h" // LLVolumeLODGroup

/* viewer */
#include "fsyspath.h"
#include "indra_constants.h" // IMG_DEFAULT
#include "llagent.h"
#include "llanimationstates.h" // ANIM_AGENT_STAND (preview avatar)
#include "llcallbacklist.h"  // doOnIdleOneTime
#include "llinventoryicon.h"
#include "llprimitive.h"     // LL_PCODE_VOLUME
#include "object_flags.h"    // FLAGS_OBJECT_* for owner permissions
#include "llscrolllistctrl.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include "llvovolume.h"
#include "pipeline.h"

/*=======================================*/
/*  Async load plumbing                  */
/*=======================================*/
namespace
{
    // Per-load context handed to the model loader as its opaque user data. It
    // owns the by-reference joint maps the loader holds for the duration of the
    // parse, so they outlive the (possibly deleted) LLLocalMesh. The load
    // callback frees it. The unit is looked up by tracking ID, so a unit
    // deleted mid-load can't be dereferenced.
    struct LoadContext
    {
        LLUUID            mTrackingID;
        LLModelLoader*    mLoader = nullptr;
        JointTransformMap mJointTransformMap;
        JointNameSet      mJointsFromNode;
        U32               mLoadState = LLModelLoader::STARTING;
        LLVOAvatar*       mAvatar = nullptr; // preview skeleton for joint lookup (never the agent)
    };

    // Build the joint alias map the loaders use to recognise rig joints,
    // mirroring LLModelPreview::getJointAliases(). Resolved against the preview
    // avatar (never the agent).
    void buildJointAliases(JointMap& joint_map, LLVOAvatar* av)
    {
        if (!av)
        {
            return;
        }

        joint_map = av->getJointAliases();

        std::vector<std::string> cv_names, attach_names;
        av->getSortedJointNames(1, cv_names);
        av->getSortedJointNames(2, attach_names);
        for (const std::string& name : cv_names)
        {
            joint_map[name] = name;
        }
        for (const std::string& name : attach_names)
        {
            joint_map[name] = name;
        }
    }

    // Transform a volume face's positions/normals in place and recompute extents.
    // Normals use the 3x3 rotation only (renormalized); adequate for preview and
    // exact for rigid/uniformly-scaled instances.
    void transformFace(LLVolumeFace& face, const LLMatrix4a& mat)
    {
        if (face.mNumVertices <= 0 || !face.mPositions)
        {
            return;
        }

        LLVector4a min, max;
        for (S32 i = 0; i < face.mNumVertices; ++i)
        {
            LLVector4a p;
            mat.affineTransform(face.mPositions[i], p);
            face.mPositions[i] = p;

            if (face.mNormals)
            {
                LLVector4a n;
                mat.rotate(face.mNormals[i], n);
                n.normalize3fast();
                face.mNormals[i] = n;
            }

            if (i == 0)
            {
                min = max = p;
            }
            else
            {
                update_min_max(min, max, p);
            }
        }

        if (face.mExtents)
        {
            face.mExtents[0] = min;
            face.mExtents[1] = max;
        }
    }

    // Normalize a merged face set into a unit box centred at the origin and
    // return its authored size, mirroring LLModel::normalizeVolumeFaces() (the
    // upload path's convention). The returned size becomes the spawned object's
    // prim scale, so the preview renders at native dimensions, is centred on its
    // pivot, and reports correct extents to the build tools. Tangents are
    // intentionally not touched here -- cacheOptimize() generates them
    // afterwards on the already-normalized geometry.
    LLVector3 normalizeFaces(std::vector<LLVolumeFace>& faces)
    {
        if (faces.empty())
        {
            return LLVector3(1.f, 1.f, 1.f);
        }

        LLVector4a min = faces[0].mExtents[0];
        LLVector4a max = faces[0].mExtents[1];
        for (size_t i = 1; i < faces.size(); ++i)
        {
            update_min_max(min, max, faces[i].mExtents[0]);
            update_min_max(min, max, faces[i].mExtents[1]);
        }

        // Translation that centres the geometry at the origin.
        LLVector4a trans;
        trans.setAdd(min, max);
        trans.mul(-0.5f);

        // Size along each axis (guard zero-thickness axes against div-by-zero).
        LLVector4a size;
        size.setSub(max, min);
        F32 sx = size[0], sy = size[1], sz = size[2], sw = size[3];
        if (fabs(sx) < F_APPROXIMATELY_ZERO) sx = 1.f;
        if (fabs(sy) < F_APPROXIMATELY_ZERO) sy = 1.f;
        if (fabs(sz) < F_APPROXIMATELY_ZERO) sz = 1.f;
        size.set(sx, sy, sz, sw);

        LLVector4a scale;
        scale.splat(1.f);
        scale.div(size); // 1 / size
        LLVector4a inv_scale(1.f);
        inv_scale.div(scale); // == size

        for (LLVolumeFace& face : faces)
        {
            // Shrink extents into the unit cube.
            face.mExtents[0].add(trans);
            face.mExtents[0].mul(scale);
            face.mExtents[1].add(trans);
            face.mExtents[1].mul(scale);

            for (S32 j = 0; j < face.mNumVertices; ++j)
            {
                face.mPositions[j].add(trans);
                face.mPositions[j].mul(scale);
                if (face.mNormals && !face.mNormals[j].equals3(LLVector4a::getZero()))
                {
                    face.mNormals[j].mul(inv_scale);
                    face.mNormals[j].normalize3();
                }
            }

            // Texture-coordinate extents (rendering and tangent generation use these).
            if (face.mTexCoords)
            {
                face.mTexCoordExtents[0] = face.mTexCoords[0];
                face.mTexCoordExtents[1] = face.mTexCoords[0];
                for (S32 j = 1; j < face.mNumVertices; ++j)
                {
                    update_min_max(face.mTexCoordExtents[0], face.mTexCoordExtents[1], face.mTexCoords[j]);
                }
            }
            else
            {
                face.mTexCoordExtents[0].set(0.f, 0.f);
                face.mTexCoordExtents[1].set(1.f, 1.f);
            }
        }

        return LLVector3(size[0], size[1], size[2]);
    }

    // LLModelLoader::joint_lookup_func_t -- resolve a joint name against this
    // load's preview avatar. NOT the agent: the DAE loader writes the model's
    // joint-position overrides straight onto the returned joint, which would
    // otherwise deform the user's own avatar.
    LLJoint* lookupJoint(const std::string& name, void* opaque)
    {
        LoadContext* ctx = static_cast<LoadContext*>(opaque);
        return (ctx && ctx->mAvatar) ? ctx->mAvatar->getJoint(name) : nullptr;
    }

    // LLModelLoader::state_callback_t -- record the last state for diagnostics.
    void onLoadState(U32 state, void* opaque)
    {
        if (LoadContext* ctx = static_cast<LoadContext*>(opaque))
        {
            ctx->mLoadState = state;
        }
    }

    // Emit a one-line summary of a unit's decoded geometry.
    void logUnit(const char* verb, const LLLocalMesh* unit)
    {
        LL_INFOS("LocalMesh") << verb << " local mesh '" << unit->getShortName() << "' [" << unit->getWorldID() << "]: "
                              << unit->getNumFaces() << " faces, " << unit->getNumVertices() << " verts, "
                              << unit->getNumTriangles() << " tris, size " << unit->getScale() << ", "
                              << (unit->isRigged() ? llformat("rigged (%d joints)", unit->getNumJoints()) : std::string("static"))
                              << LL_ENDL;
    }

    // LLModelLoader::load_callback_t -- runs on the main thread once parsing is
    // done. Hands the result to the manager (which assembles, spawns or swaps
    // geometry) and reaps the loader's context (deferred, since we are inside the
    // loader's own callback and it self-deletes after we return).
    void onModelLoaded(LLModelLoader::scene& scene, LLModelLoader::model_list& /*models*/, S32 /*lod*/, void* opaque)
    {
        LoadContext* ctx = static_cast<LoadContext*>(opaque);
        if (!ctx)
        {
            return;
        }

        if (LLLocalMeshMgr* mgr = LLLocalMeshMgr::instanceExists() ? LLLocalMeshMgr::getInstance() : nullptr)
        {
            mgr->onLoadResult(ctx->mTrackingID, scene, ctx->mLoadState);
        }

        // The model loader deletes itself in LLModelLoader::loadModelCallback
        // once this callback returns (it waits for its thread to stop, then
        // `delete this`). So we must NOT shut it down or delete it here -- that
        // was a double free. Free only our context, deferred so it outlives the
        // loader's self-deletion (the loader holds references into the
        // context's joint maps).
        doOnIdleOneTime([ctx]() { delete ctx; });
    }
}

/*=======================================*/
/*  LLLocalMesh: unit class              */
/*=======================================*/
LLLocalMesh::LLLocalMesh(std::string filename)
    : mFilename(filename)
    , mShortName(gDirUtilp->getBaseFileName(filename, true))
    , mFormat(FMT_NONE)
    , mState(ST_LOADING)
    , mSpawnWhenReady(false)
    , mLastModified()
    , mNumFaces(0)
    , mNumVertices(0)
    , mNumTriangles(0)
    , mNumJoints(0)
    , mScale(1.f, 1.f, 1.f)
    , mTruncated(false)
    , mReloading(false)
    , mPendingModified()
    , mFailedModified()
{
    mTrackingID.generate();
    mWorldID.generate();

    std::string ext = gDirUtilp->getExtension(mFilename);
    if (ext == "dae")
    {
        mFormat = FMT_DAE;
    }
    else if (ext == "gltf" || ext == "glb")
    {
        mFormat = FMT_GLTF;
    }
    else
    {
        LL_WARNS("LocalMesh") << "Unsupported extension for local mesh, aborting: " << mFilename << LL_ENDL;
        mState = ST_FAILED;
        return;
    }

    if (!isAgentAvatarValid())
    {
        // Joint lookups and (for glTF) the rest skeleton come from the agent
        // avatar, so we need a valid avatar before parsing.
        LL_WARNS("LocalMesh") << "Cannot load local mesh before the avatar is ready: " << mFilename << LL_ENDL;
        mState = ST_FAILED;
        return;
    }

    if (!gDirUtilp->fileExists(mFilename))
    {
        LL_WARNS("LocalMesh") << "Local mesh file not found: " << mFilename << LL_ENDL;
        mState = ST_FAILED;
        return;
    }

    startLoad();
}

LLLocalMesh::~LLLocalMesh()
{
    // A load may still be in flight. The model loader self-deletes in
    // LLModelLoader::loadModelCallback, and its callback looks this unit up by
    // ID (finding nothing once we're gone), so there is nothing to clean up
    // here.
}

void LLLocalMesh::startLoad()
{
    // Record the mtime we are about to parse; the completion handler stamps it as
    // the loaded version, so live-reload change detection compares against the
    // exact bytes we read (not whatever the file becomes mid-parse).
    std::error_code ec;
    mPendingModified = std::filesystem::last_write_time(fsyspath(mFilename), ec);

    LoadContext* ctx = new LoadContext();
    ctx->mTrackingID = mTrackingID;

    // Resolve joints against a dedicated UI avatar -- never the agent. The DAE
    // loader writes the model's joint-position overrides onto the looked-up
    // joints, so gAgentAvatarp here would deform the user's avatar.
    LLVOAvatar* preview_av = LLLocalMeshMgr::getInstance()->getPreviewAvatar();
    ctx->mAvatar = preview_av;

    JointMap joint_alias_map;
    buildJointAliases(joint_alias_map, preview_av);

    LLModelLoader::load_callback_t     load_cb    = onModelLoaded;
    LLModelLoader::joint_lookup_func_t joint_cb   = lookupJoint;
    LLModelLoader::texture_load_func_t texture_cb = [](LLImportMaterial&, void*) -> U32 { return 0; };
    LLModelLoader::state_callback_t    state_cb   = onLoadState;

    if (mFormat == FMT_DAE)
    {
        ctx->mLoader = new LLDAELoader(
            mFilename,
            LLModel::LOD_HIGH,
            load_cb,
            joint_cb,
            texture_cb,
            state_cb,
            ctx,
            ctx->mJointTransformMap,
            ctx->mJointsFromNode,
            joint_alias_map,
            LLSkinningUtil::getMaxJointCount(),
            gSavedSettings.getU32("ImporterModelLimit"),
            gSavedSettings.getU32("ImporterDebugMode"),
            gSavedSettings.getBOOL("ImporterPreprocessDAE"));
    }
    else // FMT_GLTF
    {
        std::vector<LLJointData> viewer_skeleton;
        if (preview_av)
        {
            preview_av->getJointMatricesAndHierarhy(viewer_skeleton);
        }
        ctx->mLoader = new LLGLTFLoader(
            mFilename,
            LLModel::LOD_HIGH,
            load_cb,
            joint_cb,
            texture_cb,
            state_cb,
            ctx,
            ctx->mJointTransformMap,
            ctx->mJointsFromNode,
            joint_alias_map,
            LLSkinningUtil::getMaxJointCount(),
            gSavedSettings.getU32("ImporterModelLimit"),
            gSavedSettings.getU32("ImporterDebugMode"),
            viewer_skeleton);
    }

    ctx->mLoader->mTrySLM = false;
    ctx->mLoader->start(); // parse on the worker thread; onModelLoaded fires on the main thread, then the loader self-deletes
}

bool LLLocalMesh::ingestScene(LLModelLoader::scene& scene)
{
    // Assemble into locals first; members are only overwritten once we know the
    // parse produced geometry, so a failed reload preserves the last good mesh.
    std::vector<LLVolumeFace> faces;
    const LLMeshSkinInfo* skin_src = nullptr;
    S32  num_vertices = 0;
    S32  num_triangles = 0;
    bool truncated = false;

    for (LLModelLoader::scene::iterator iter = scene.begin(); iter != scene.end() && !truncated; ++iter)
    {
        LLMatrix4a mat;
        mat.loadu(iter->first);

        for (LLModelInstance& instance : iter->second)
        {
            LLModel* mdl = instance.mModel.notNull() ? instance.mModel.get() : instance.mLOD[LLModel::LOD_HIGH].get();
            if (!mdl)
            {
                continue;
            }

            // Capture skin from the first rigged model encountered.
            if (!skin_src && !mdl->mSkinInfo.mJointNames.empty())
            {
                skin_src = &mdl->mSkinInfo;
            }

            for (S32 fi = 0; fi < mdl->getNumVolumeFaces(); ++fi)
            {
                if ((S32)faces.size() >= MAX_MODEL_FACES)
                {
                    // Faithful to the upload limit; warn and clip the remainder.
                    truncated = true;
                    break;
                }

                LLVolumeFace face = mdl->getVolumeFace(fi); // deep copy
                transformFace(face, mat);

                num_vertices += face.mNumVertices;
                num_triangles += face.mNumIndices / 3;
                faces.push_back(face);
            }

            if (truncated)
            {
                break;
            }
        }
    }

    if (faces.empty())
    {
        LL_WARNS("LocalMesh") << "Local mesh produced no geometry: " << mFilename << LL_ENDL;
        return false; // keep any previously loaded geometry intact
    }

    // Static meshes: re-normalize the merged geometry into a unit box centred at
    // the origin and keep the authored size for the prim scale. Both loaders
    // normalize per-model, but baking the instance transforms above puts the
    // merged result back into authored world space (often large and off-origin),
    // so the object pivot, bounding box and build-tool size would all be wrong
    // without this. Rigged meshes stay in skin/bind space -- the skeleton, not
    // the object transform, drives their placement (handled at rigged-attach).
    LLVector3 scale(1.f, 1.f, 1.f);
    if (!skin_src)
    {
        scale = normalizeFaces(faces);
    }

    LLVolumeParams params;
    params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
    LLPointer<LLVolume> volume = new LLVolume(params, 1.f);
    volume->copyFacesFrom(faces);

    // Optimize the index buffer and generate tangents, matching what
    // unpackVolumeFaces() does for a real mesh asset. Loaded mesh assets are
    // required to carry tangents (see LLVolume::genTangents) and raycast
    // picking dereferences them, so this must run before setMeshAssetLoaded().
    if (!volume->cacheOptimize(true))
    {
        LL_WARNS("LocalMesh") << "cacheOptimize failed for '" << mShortName << "'" << LL_ENDL;
    }
    volume->setMeshAssetLoaded(true);

    LLPointer<LLMeshSkinInfo> skin;
    S32 num_joints = 0;
    if (skin_src)
    {
        // Round-trip through LLSD to get a clean, owned copy bound to our UUID.
        LLSD sd = skin_src->asLLSD(true, skin_src->mLockScaleIfJointPosition);
        skin = new LLMeshSkinInfo(mWorldID, sd);
        num_joints = (S32)skin_src->mJointNames.size();
    }

    // Commit.
    mVolume       = volume;
    mSkinInfo     = skin; // null clears any prior rig
    mScale        = scale;
    mNumFaces     = (S32)faces.size();
    mNumVertices  = num_vertices;
    mNumTriangles = num_triangles;
    mNumJoints    = num_joints;
    mTruncated    = truncated;
    mState        = ST_LOADED;
    mLastModified = mPendingModified; // the exact version we just parsed

    if (truncated)
    {
        LL_WARNS("LocalMesh") << "Local mesh '" << mShortName << "' exceeds " << MAX_MODEL_FACES
                              << " faces; extra faces were dropped from the preview." << LL_ENDL;
    }

    return true;
}

bool LLLocalMesh::pollForReload()
{
    // Only watch units that are currently showing good geometry; skip while an
    // initial load or a previous reload is still in flight.
    if (mState != ST_LOADED || mReloading)
    {
        return false;
    }
    if (!gDirUtilp->fileExists(mFilename))
    {
        return false;
    }

    std::error_code ec;
    const std::filesystem::file_time_type mtime = std::filesystem::last_write_time(fsyspath(mFilename), ec);
    if (ec || mtime == mLastModified || mtime == mFailedModified)
    {
        return false; // unchanged, or a version we already know fails to parse
    }

    mReloading = true;
    LL_INFOS("LocalMesh") << "Source changed, reloading '" << mShortName << "'" << LL_ENDL;
    startLoad(); // captures mPendingModified
    return true;
}

void LLLocalMesh::finishReload(bool ok)
{
    mReloading = false;
    if (ok)
    {
        mFailedModified = std::filesystem::file_time_type(); // clear any prior failure
    }
    else
    {
        // Remember this version failed so we don't re-attempt it every tick; a
        // further edit (a new mtime) will be retried.
        mFailedModified = mPendingModified;
        LL_WARNS("LocalMesh") << "Reload parse failed for '" << mShortName << "'; keeping previous geometry" << LL_ENDL;
    }
}

void LLLocalMesh::regenerateWorldID()
{
    mWorldID.generate();
    if (mSkinInfo.notNull())
    {
        mSkinInfo->mMeshID = mWorldID; // keep the skin's id in sync with the new world id
    }
}

/*=======================================*/
/*  LLLocalMeshMgr: manager class        */
/*=======================================*/
LLLocalMeshMgr::LLLocalMeshMgr()
{
    mTimer.stopTimer(); // started on demand once the first unit is added
}

LLLocalMeshMgr::~LLLocalMeshMgr()
{
    for (LLLocalMesh* unit : mMeshList)
    {
        delete unit;
    }
    mMeshList.clear();

    if (mPreviewAvatar.notNull())
    {
        mPreviewAvatar->markDead();
        mPreviewAvatar = nullptr;
    }
}

LLVOAvatar* LLLocalMeshMgr::getPreviewAvatar()
{
    if ((mPreviewAvatar.isNull() || mPreviewAvatar->isDead()) && gAgent.getRegion())
    {
        // A dedicated, never-rendered UI avatar gives the model loaders a skeleton
        // to resolve joints against -- and to absorb the model's joint-position
        // overrides -- WITHOUT mutating the real agent avatar. Mirrors
        // LLModelPreview::createPreviewAvatar().
        LLVOAvatar* av = (LLVOAvatar*)gObjectList.createObjectViewer(LL_PCODE_LEGACY_AVATAR, gAgent.getRegion(), LLViewerObject::CO_FLAG_UI_AVATAR);
        if (av)
        {
            av->createDrawable(&gPipeline);
            av->mSpecialRenderMode = 1; // not part of the in-world render
            av->startMotion(ANIM_AGENT_STAND);
            av->hideSkirt();
        }
        else
        {
            LL_WARNS("LocalMesh") << "Failed to create preview avatar for joint resolution" << LL_ENDL;
        }
        mPreviewAvatar = av;
    }
    return mPreviewAvatar.get();
}

LLUUID LLLocalMeshMgr::addUnit(const std::string& filename)
{
    return addUnitInternal(filename);
}

bool LLLocalMeshMgr::addUnit(const std::vector<std::string>& filenames)
{
    bool any = false;
    for (const std::string& filename : filenames)
    {
        if (!filename.empty() && addUnitInternal(filename).notNull())
        {
            any = true;
        }
    }
    return any;
}

LLUUID LLLocalMeshMgr::addUnitInternal(const std::string& filename)
{
    LLLocalMesh* unit = new LLLocalMesh(filename);
    if (unit->isFailed())
    {
        // Immediate failure (bad extension / no avatar / missing file).
        LL_WARNS("LocalMesh") << "Could not start loading mesh file: " << filename << LL_ENDL;
        delete unit;
        return LLUUID::null;
    }

    // Loading (async) or already loaded -- keep it; completion is handled in
    // the load callback.
    mMeshList.push_back(unit);
    if (!mTimer.isRunning())
    {
        mTimer.startTimer(); // begin watching source files for live reload
    }
    return unit->getTrackingID();
}

void LLLocalMeshMgr::delUnit(LLUUID tracking_id)
{
    for (local_list_iter iter = mMeshList.begin(); iter != mMeshList.end(); )
    {
        LLLocalMesh* unit = *iter;
        if (unit->getTrackingID() == tracking_id)
        {
            despawnForWorldID(unit->getWorldID());
            iter = mMeshList.erase(iter);
            delete unit;
        }
        else
        {
            ++iter;
        }
    }

    if (mMeshList.empty())
    {
        mTimer.stopTimer(); // nothing left to watch
    }
}

LLUUID LLLocalMeshMgr::getUnitID(const std::string& filename) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getFilename() == filename)
        {
            return unit->getTrackingID();
        }
    }
    return LLUUID::null;
}

LLUUID LLLocalMeshMgr::getTrackingID(const LLUUID& world_id) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getWorldID() == world_id)
        {
            return unit->getTrackingID();
        }
    }
    return LLUUID::null;
}

LLUUID LLLocalMeshMgr::getWorldID(const LLUUID& tracking_id) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getTrackingID() == tracking_id)
        {
            return unit->getWorldID();
        }
    }
    return LLUUID::null;
}

bool LLLocalMeshMgr::isLocal(const LLUUID& world_id) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getWorldID() == world_id)
        {
            return true;
        }
    }
    return false;
}

std::string LLLocalMeshMgr::getFilename(const LLUUID& tracking_id) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getTrackingID() == tracking_id)
        {
            return unit->getFilename();
        }
    }
    return std::string();
}

LLLocalMesh* LLLocalMeshMgr::getUnit(const LLUUID& tracking_id) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getTrackingID() == tracking_id)
        {
            return unit;
        }
    }
    return nullptr;
}

LLLocalMesh* LLLocalMeshMgr::getUnitByWorldID(const LLUUID& world_id) const
{
    for (LLLocalMesh* unit : mMeshList)
    {
        if (unit->getWorldID() == world_id)
        {
            return unit;
        }
    }
    return nullptr;
}

bool LLLocalMeshMgr::isLocalPreview(const LLViewerObject* obj) const
{
    if (!obj)
    {
        return false;
    }
    for (const auto& spawned : mSpawnedObjects)
    {
        if (spawned.second.get() == obj)
        {
            return true;
        }
    }
    return false;
}

LLViewerObject* LLLocalMeshMgr::spawnInWorld(const LLUUID& tracking_id)
{
    LLLocalMesh* unit = getUnit(tracking_id);
    if (!unit || !unit->getValid() || !unit->getVolume())
    {
        LL_WARNS("LocalMesh") << "spawnInWorld: no valid unit for " << tracking_id << LL_ENDL;
        return nullptr;
    }

    if (!isAgentAvatarValid() || !gAgent.getRegion())
    {
        LL_WARNS("LocalMesh") << "spawnInWorld: agent/region not ready" << LL_ENDL;
        return nullptr;
    }

    LLViewerObject* obj = gObjectList.createObjectViewer(LL_PCODE_VOLUME, gAgent.getRegion());
    LLVOVolume* vol = dynamic_cast<LLVOVolume*>(obj);
    if (!vol)
    {
        LL_WARNS("LocalMesh") << "spawnInWorld: failed to create volume object" << LL_ENDL;
        if (obj)
        {
            obj->markDead();
        }
        return nullptr;
    }

    // Selectable and movable with the standard build tools. LLSelectMgr
    // suppresses all server traffic for these client-only objects (gated on
    // isLocalPreview), and we set full owner permission flags so the tools
    // enable manipulation (permYouOwner/permModify/permMove read these flags).
    vol->mbCanSelect = true;
    vol->setFlagsWithoutUpdate(FLAGS_OBJECT_YOU_OWNER | FLAGS_OBJECT_MODIFY | FLAGS_OBJECT_MOVE | FLAGS_OBJECT_COPY | FLAGS_OBJECT_TRANSFER, true);

    // Build the drawable, then force a valid LOD. LLDrawable's constructor calls
    // setNoLOD(); with mLOD == NO_LOD, LLPrimitive::setVolume builds a
    // placeholder cube and returns false, so LLVOVolume::setVolume skips
    // loadMesh entirely (giving a grey cube and a face-count churn that
    // corrupts the heap). A real LOD makes the repository injection serve our
    // decoded geometry directly, with the right face count from the start.
    gPipeline.createObject(obj);
    vol->setLOD(LLVolumeLODGroup::NUM_LODS - 1);

    // Place a few meters in front of the agent. The geometry was normalized to a
    // unit box centred on the origin, so the prim scale carries the authored size
    // and the pivot sits at the geometry's centre. Scale/position are set here
    // (spawn only); live reload preserves whatever transform the user has set.
    const LLVector3 pos = gAgent.getPositionAgent() + gAgent.getAtAxis() * 3.f;
    vol->setPositionAgent(pos);
    vol->setScale(unit->getScale(), false);

    applyUnitGeometry(vol, unit);

    mSpawnedObjects.emplace_back(unit->getWorldID(), obj);

    LL_INFOS("LocalMesh") << "Spawned local mesh '" << unit->getShortName() << "' ("
                          << (vol->getVolume() ? vol->getVolume()->getNumVolumeFaces() : 0)
                          << " faces) at " << pos << LL_ENDL;
    return obj;
}

void LLLocalMeshMgr::applyUnitGeometry(LLVOVolume* vol, const LLLocalMesh* unit)
{
    // isSculpted()/isMesh() key off the PARAMS_SCULPT extra param (not the
    // volume params alone). Without it, LLVOVolume::setVolume skips the entire
    // mesh-load block and the object stays a plain prim. local_origin=false so
    // nothing is sent to the sim for this client-only object.
    LLSculptParams sculpt_params;
    sculpt_params.setSculptTexture(unit->getWorldID(), LL_SCULPT_TYPE_MESH);
    vol->setParameterEntry(LLNetworkData::PARAMS_SCULPT, sculpt_params, false);

    // Reference the local mesh by its world UUID; the repository injection
    // resolves it to the decoded geometry. On reload the world id has changed,
    // so setVolume re-runs the mesh path and picks up the fresh geometry/skin.
    LLVolumeParams params;
    params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
    params.setSculptID(unit->getWorldID(), LL_SCULPT_TYPE_MESH);
    vol->setVolume(params, LLVolumeLODGroup::NUM_LODS - 1);

    // Give every face a visible default texture (file materials are not yet
    // applied in this milestone). The face count can change across reloads.
    LLVolume* v = vol->getVolume();
    const S32 num_faces = v ? v->getNumVolumeFaces() : 0;
    if (num_faces > 0)
    {
        vol->setNumTEs((U8)num_faces);
        for (S32 i = 0; i < num_faces; ++i)
        {
            vol->setTETexture((U8)i, IMG_DEFAULT);
        }
    }

    vol->markForUpdate();
}

void LLLocalMeshMgr::onLoadResult(const LLUUID& tracking_id, LLModelLoader::scene& scene, U32 load_state)
{
    LLLocalMesh* unit = getUnit(tracking_id);
    if (!unit) // removed while loading
    {
        return;
    }

    const bool reloading = unit->isReloading();
    const bool parse_ok  = (load_state < LLModelLoader::ERROR_PARSING) && !scene.empty();
    const bool assembled = parse_ok && unit->ingestScene(scene);

    if (!parse_ok)
    {
        LL_WARNS("LocalMesh") << "Parse failed (state " << load_state << ") for " << unit->getFilename() << LL_ENDL;
    }

    if (reloading)
    {
        unit->finishReload(assembled);
        if (assembled)
        {
            // Swap geometry under a fresh world id and re-point our spawns at it.
            const LLUUID old_world = unit->getWorldID();
            unit->regenerateWorldID();
            logUnit("Reloaded", unit);
            repointSpawnedObjects(old_world, unit);
        }
        return;
    }

    // Initial load.
    if (assembled)
    {
        logUnit("Loaded", unit);
        if (unit->wantsSpawn())
        {
            spawnInWorld(tracking_id);
        }
    }
    else
    {
        unit->markFailed();
        delUnit(tracking_id); // drop the failed unit
    }
}

void LLLocalMeshMgr::repointSpawnedObjects(const LLUUID& old_world_id, LLLocalMesh* unit)
{
    const LLUUID new_world_id = unit->getWorldID();
    for (auto& spawned : mSpawnedObjects)
    {
        if (spawned.first != old_world_id)
        {
            continue;
        }
        LLViewerObject* obj = spawned.second.get();
        if (obj && !obj->isDead())
        {
            if (LLVOVolume* vol = dynamic_cast<LLVOVolume*>(obj))
            {
                applyUnitGeometry(vol, unit); // keeps the object's transform
            }
        }
        spawned.first = new_world_id; // track the new id for future reloads/despawn
    }
}

void LLLocalMeshMgr::doUpdates()
{
    // Stop/restart around the sweep so a long poll can't re-enter via the timer.
    mTimer.stopTimer();
    for (LLLocalMesh* unit : mMeshList)
    {
        unit->pollForReload();
    }
    if (!mMeshList.empty())
    {
        mTimer.startTimer();
    }
}

void LLLocalMeshMgr::addAndSpawn(const std::vector<std::string>& filenames)
{
    for (const std::string& filename : filenames)
    {
        if (filename.empty())
        {
            continue;
        }
        const LLUUID tracking_id = addUnit(filename);
        if (tracking_id.notNull())
        {
            if (LLLocalMesh* unit = getUnit(tracking_id))
            {
                unit->setSpawnWhenReady(true);
            }
        }
    }
}

void LLLocalMeshMgr::despawnForWorldID(const LLUUID& world_id)
{
    for (auto iter = mSpawnedObjects.begin(); iter != mSpawnedObjects.end(); )
    {
        if (iter->first == world_id)
        {
            if (iter->second.notNull() && !iter->second->isDead())
            {
                iter->second->markDead();
            }
            iter = mSpawnedObjects.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

void LLLocalMeshMgr::feedScrollList(LLScrollListCtrl* ctrl)
{
    if (!ctrl)
    {
        return;
    }

    const std::string icon_name = LLInventoryIcon::getIconName(LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT);

    for (LLLocalMesh* unit : mMeshList)
    {
        LLSD element;
        element["columns"][0]["column"] = "icon";
        element["columns"][0]["type"]   = "icon";
        element["columns"][0]["value"]  = icon_name;

        element["columns"][1]["column"] = "unit_name";
        element["columns"][1]["type"]   = "text";
        element["columns"][1]["value"]  = unit->getShortName();

        LLSD data;
        data["id"]   = unit->getTrackingID();
        data["type"] = (S32)LLAssetType::AT_OBJECT;
        element["value"] = data;

        ctrl->addElement(element);
    }
}

/*=======================================*/
/*  LLLocalMeshTimer: live-reload poll   */
/*=======================================*/
static const F32 LOCAL_MESH_TIMER_HEARTBEAT = 3.0f; // seconds between file-change polls

LLLocalMeshTimer::LLLocalMeshTimer()
    : LLEventTimer(LOCAL_MESH_TIMER_HEARTBEAT)
{
}

void LLLocalMeshTimer::startTimer() { mEventTimer.start(); }
void LLLocalMeshTimer::stopTimer()  { mEventTimer.stop(); }
bool LLLocalMeshTimer::isRunning()  { return mEventTimer.getStarted(); }

bool LLLocalMeshTimer::tick()
{
    if (LLLocalMeshMgr::instanceExists())
    {
        LLLocalMeshMgr::getInstance()->doUpdates();
    }
    return false; // keep ticking
}
