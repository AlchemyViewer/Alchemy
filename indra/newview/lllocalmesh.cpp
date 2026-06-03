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
    };

    // Build the joint alias map the loaders use to recognise rig joints,
    // mirroring LLModelPreview::getJointAliases() but against the agent avatar.
    void buildJointAliases(JointMap& joint_map)
    {
        if (!isAgentAvatarValid())
        {
            return;
        }

        joint_map = gAgentAvatarp->getJointAliases();

        std::vector<std::string> cv_names, attach_names;
        gAgentAvatarp->getSortedJointNames(1, cv_names);
        gAgentAvatarp->getSortedJointNames(2, attach_names);
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

    // LLModelLoader::joint_lookup_func_t -- resolve a joint name against the
    // agent's skeleton. (opaque is the LoadContext, unused here.)
    LLJoint* lookupJoint(const std::string& name, void* /*opaque*/)
    {
        return isAgentAvatarValid() ? gAgentAvatarp->getJoint(name) : nullptr;
    }

    // LLModelLoader::state_callback_t -- record the last state for diagnostics.
    void onLoadState(U32 state, void* opaque)
    {
        if (LoadContext* ctx = static_cast<LoadContext*>(opaque))
        {
            ctx->mLoadState = state;
        }
    }

    // LLModelLoader::load_callback_t -- runs on the main thread once parsing is
    // done. Hands the scene to the unit, optionally spawns it, and reaps the
    // loader (deferred, since we are inside the loader's own callback).
    void onModelLoaded(LLModelLoader::scene& scene, LLModelLoader::model_list& /*models*/, S32 /*lod*/, void* opaque)
    {
        LoadContext* ctx = static_cast<LoadContext*>(opaque);
        if (!ctx)
        {
            return;
        }

        const LLUUID tracking_id = ctx->mTrackingID;
        const U32 load_state = ctx->mLoadState;

        LLLocalMeshMgr* mgr = LLLocalMeshMgr::instanceExists() ? LLLocalMeshMgr::getInstance() : nullptr;
        LLLocalMesh* unit = mgr ? mgr->getUnit(tracking_id) : nullptr;
        if (unit) // null if the unit was removed while loading
        {
            if (load_state < LLModelLoader::ERROR_PARSING && !scene.empty())
            {
                unit->onLoadComplete(scene);
            }
            else
            {
                LL_WARNS("LocalMesh") << "Parse failed (state " << load_state << ") for " << unit->getFilename() << LL_ENDL;
                unit->markFailed();
            }

            if (unit->getValid())
            {
                if (unit->wantsSpawn())
                {
                    mgr->spawnInWorld(tracking_id);
                }
            }
            else
            {
                mgr->delUnit(tracking_id); // drop the failed unit
            }
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
    , mTruncated(false)
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
    LoadContext* ctx = new LoadContext();
    ctx->mTrackingID = mTrackingID;

    JointMap joint_alias_map;
    buildJointAliases(joint_alias_map);

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
        gAgentAvatarp->getJointMatricesAndHierarhy(viewer_skeleton);
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

void LLLocalMesh::onLoadComplete(LLModelLoader::scene& scene)
{
    assembleFromScene(scene);

    mState = (mVolume.notNull() && mNumFaces > 0) ? ST_LOADED : ST_FAILED;

    if (mState == ST_LOADED)
    {
        mLastModified = std::filesystem::last_write_time(fsyspath(mFilename));
        LL_INFOS("LocalMesh") << "Loaded local mesh '" << mShortName << "' [" << mWorldID << "]: "
                              << mNumFaces << " faces, " << mNumVertices << " verts, " << mNumTriangles << " tris, "
                              << (isRigged() ? llformat("rigged (%d joints)", mNumJoints) : std::string("static"))
                              << (mTruncated ? " [TRUNCATED to MAX_MODEL_FACES]" : "")
                              << LL_ENDL;
    }
}

void LLLocalMesh::assembleFromScene(LLModelLoader::scene& scene)
{
    std::vector<LLVolumeFace> faces;
    const LLMeshSkinInfo* skin_src = nullptr;

    for (LLModelLoader::scene::iterator iter = scene.begin(); iter != scene.end() && !mTruncated; ++iter)
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
                    mTruncated = true;
                    break;
                }

                LLVolumeFace face = mdl->getVolumeFace(fi); // deep copy
                transformFace(face, mat);

                mNumVertices += face.mNumVertices;
                mNumTriangles += face.mNumIndices / 3;
                faces.push_back(face);
            }

            if (mTruncated)
            {
                break;
            }
        }
    }

    if (faces.empty())
    {
        LL_WARNS("LocalMesh") << "Local mesh produced no geometry: " << mFilename << LL_ENDL;
        return;
    }

    LLVolumeParams params;
    params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
    mVolume = new LLVolume(params, 1.f);
    mVolume->copyFacesFrom(faces);

    // Optimize the index buffer and generate tangents, matching what
    // unpackVolumeFaces() does for a real mesh asset. Loaded mesh assets are
    // required to carry tangents (see LLVolume::genTangents) and raycast
    // picking dereferences them, so this must run before setMeshAssetLoaded().
    if (!mVolume->cacheOptimize(true))
    {
        LL_WARNS("LocalMesh") << "cacheOptimize failed for '" << mShortName << "'" << LL_ENDL;
    }
    mVolume->setMeshAssetLoaded(true);

    if (skin_src)
    {
        // Round-trip through LLSD to get a clean, owned copy bound to our UUID.
        LLSD sd = skin_src->asLLSD(true, skin_src->mLockScaleIfJointPosition);
        mSkinInfo = new LLMeshSkinInfo(mWorldID, sd);
        mNumJoints = (S32)skin_src->mJointNames.size();
    }

    mNumFaces = (S32)faces.size();

    if (mTruncated)
    {
        LL_WARNS("LocalMesh") << "Local mesh '" << mShortName << "' exceeds " << MAX_MODEL_FACES
                              << " faces; extra faces were dropped from the preview." << LL_ENDL;
    }
}

/*=======================================*/
/*  LLLocalMeshMgr: manager class        */
/*=======================================*/
LLLocalMeshMgr::LLLocalMeshMgr()
{
}

LLLocalMeshMgr::~LLLocalMeshMgr()
{
    for (LLLocalMesh* unit : mMeshList)
    {
        delete unit;
    }
    mMeshList.clear();
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

    // Place a few meters in front of the agent at native scale.
    const LLVector3 pos = gAgent.getPositionAgent() + gAgent.getAtAxis() * 3.f;
    vol->setPositionAgent(pos);
    vol->setScale(LLVector3(1.f, 1.f, 1.f), false);

    // isSculpted()/isMesh() key off the PARAMS_SCULPT extra param (not the
    // volume params alone). Without it, LLVOVolume::setVolume skips the entire
    // mesh-load block and the object stays a plain prim. local_origin=false so
    // nothing is sent to the sim for this client-only object.
    LLSculptParams sculpt_params;
    sculpt_params.setSculptTexture(unit->getWorldID(), LL_SCULPT_TYPE_MESH);
    vol->setParameterEntry(LLNetworkData::PARAMS_SCULPT, sculpt_params, false);

    // Reference the local mesh by its world UUID; the repository injection
    // resolves it to the decoded geometry.
    LLVolumeParams params;
    params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
    params.setSculptID(unit->getWorldID(), LL_SCULPT_TYPE_MESH);
    vol->setVolume(params, LLVolumeLODGroup::NUM_LODS - 1);

    // Give every face a visible default texture (file materials are not yet
    // applied in this milestone).
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

    mSpawnedObjects.emplace_back(unit->getWorldID(), obj);

    LL_INFOS("LocalMesh") << "Spawned local mesh '" << unit->getShortName() << "' (" << num_faces
                          << " faces) at " << pos << LL_ENDL;
    return obj;
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
