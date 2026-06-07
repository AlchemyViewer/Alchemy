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

#include <algorithm>     // std::sort (stable rez-order listing of spawned copies)
#include <unordered_map> // mSpawnedCopies: instance id -> copy (O(1) per-copy lookup/erase)
#include <unordered_set> // dedup avatars when rebuilding overrides across copies

/* model loaders (shared with the mesh upload path) */
#include "lldaeloader.h"
#include "gltf/llgltfloader.h"
#include "lljointdata.h"
#include "llskinningutil.h"

/* geometry */
#include "llmatrix4a.h"
#include "llquaternion.h" // linkset root rotation across reload
#include "llvolume.h"
#include "llvolumemgr.h" // LLVolumeLODGroup

/* viewer */
#include "fsyspath.h"
#include "indra_constants.h" // IMG_DEFAULT
#include "llagent.h"
#include "llanimationstates.h" // ANIM_AGENT_STAND (preview avatar)
#include "llcallbacklist.h"  // doOnIdleOneTime
#include "llinventoryicon.h"
#include "lllocalbitmaps.h"  // import a mesh's diffuse maps as mesh-owned local bitmaps
#include "lllocalgltfmaterials.h" // import a glTF mesh's materials as mesh-owned local materials
#include "llprimitive.h"     // LL_PCODE_VOLUME
#include "object_flags.h"    // FLAGS_OBJECT_* for owner permissions
#include "llscrolllistctrl.h"
#include "llselectmgr.h"    // deselect before re-parenting onto the avatar
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "lltrans.h"
#include "llviewerjointattachment.h"
#include "llvoavatarself.h"
#include "llvovolume.h"
#include "lltextureentry.h"
#include "llgltfmaterial.h"
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

    // Normalize a face set into a unit box centred at the origin and return its
    // authored size, mirroring LLModel::normalizeVolumeFaces() (the upload path's
    // convention). The returned size becomes the prim scale, so the preview
    // renders at native dimensions, is centred on its pivot, and reports correct
    // extents to the build tools. center_out (if given) receives the pre-normalize
    // centre, used to place the part within the model. Tangents are intentionally
    // not touched here -- cacheOptimize() generates them on the normalized geometry.
    LLVector3 normalizeFaces(std::vector<LLVolumeFace>& faces, LLVector3* center_out = nullptr)
    {
        if (faces.empty())
        {
            if (center_out) center_out->setZero();
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

        if (center_out)
        {
            // Pre-normalize centre (in the faces' current/scene space).
            center_out->set(-trans[0], -trans[1], -trans[2]);
        }

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

    // Loader-built models carry skin weights in mSkinWeights (a position-keyed
    // map), NOT in face.mWeights -- the per-vertex array is only filled when a
    // mesh is *downloaded* (LLVolume::unpackVolumeFaces). The upload path
    // serializes weights by looking each vertex position up via
    // getJointInfluences(); reproduce that here and pack the result exactly as a
    // download would: each component is jointIndex + weight, the weight
    // U16-quantized then clamped to [0.001, 0.999], up to 4 influences, with a
    // (joint 0, ~1.0) fallback for any unweighted vertex. Must run BEFORE
    // cacheOptimize() so its vertex remap carries the weights along. Without this
    // the rigged draw path skins the mesh to garbage (holes, exploded verts,
    // avatar deformation, vanishing parts). The per-vertex lookup goes through an
    // LLModel::JointWeightCache (O(1)) instead of getJointInfluences() (O(V)),
    // turning this pass from O(V^2) to O(V) -- see the cache's header doc.
    void populateFaceWeights(LLVolumeFace& face, LLModel& mdl, const LLModel::JointWeightCache& locator)
    {
        if (face.mNumVertices <= 0 || mdl.mSkinWeights.empty())
        {
            return;
        }
        face.allocateWeights(face.mNumVertices);
        if (!face.mWeights)
        {
            return;
        }
        for (S32 v = 0; v < face.mNumVertices; ++v)
        {
            const LLVector3 pos(face.mPositions[v].getF32ptr());
            const LLModel::weight_list& weights = locator.influences(pos);

            F32 packed[4] = { 0.f, 0.f, 0.f, 0.f };
            S32 cur = 0;
            F32 wsum = 0.f;
            for (LLModel::weight_list::const_iterator it = weights.begin();
                 it != weights.end() && cur < 4; ++it)
            {
                if (it->mJointIdx < 0 || it->mJointIdx >= 255)
                {
                    continue; // matches LLModel::writeModel()'s joint-index guard
                }
                const U16 influence = (U16)(it->mWeight * 65535.f);
                const F32 w = llclamp((F32)influence / 65535.f, 0.001f, 0.999f);
                packed[cur] = (F32)it->mJointIdx + w;
                wsum += w;
                ++cur;
            }
            if (cur == 0 || wsum <= 0.f)
            {
                packed[0] = 0.999f; // joint 0 at full weight
            }
            face.mWeights[v].loadua(packed);
        }
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
        LL_INFOS("LocalMesh") << verb << " local mesh '" << unit->getShortName() << "': "
                              << unit->getNumParts() << " part(s), " << unit->getNumFaces() << " faces, "
                              << unit->getNumVertices() << " verts, " << unit->getNumTriangles() << " tris, "
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
LLLocalMesh::LLLocalMesh(std::string filename, bool include_joints)
    : mFilename(filename)
    , mShortName(gDirUtilp->getBaseFileName(filename, true))
    , mFormat(FMT_NONE)
    , mState(ST_LOADING)
    , mSpawnWhenReady(false)
    , mIncludeJointPositions(include_joints)
    , mLastModified()
    , mNumFaces(0)
    , mNumVertices(0)
    , mNumTriangles(0)
    , mNumJoints(0)
    , mReloading(false)
    , mPendingModified()
    , mFailedModified()
{
    mTrackingID.generate();

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

namespace
{
    // Record a mesh-owned unit referenced by the current parse (deduped).
    void track_owned(std::vector<LLUUID>& owned, const LLUUID& id)
    {
        if (id.notNull() && std::find(owned.begin(), owned.end(), id) == owned.end())
        {
            owned.push_back(id);
        }
    }

    // Release (delUnit) every id in `had` that the current parse no longer keeps and
    // that no sibling mesh still references (so a shared texture/material survives).
    template <typename Mgr>
    void release_dropped(Mgr* mgr, const std::vector<LLUUID>& had, const std::vector<LLUUID>& keep,
                         const LLLocalMesh* exclude)
    {
        for (const LLUUID& id : had)
        {
            if (std::find(keep.begin(), keep.end(), id) == keep.end()
                && !LLLocalMeshMgr::getInstance()->isImportOwnedByOther(id, exclude))
            {
                mgr->delUnit(id);
            }
        }
    }
}

LLUUID LLLocalMesh::registerOwnedBitmap(const std::string& filename, std::vector<LLUUID>& owned)
{
    LLLocalBitmapMgr* mgr = LLLocalBitmapMgr::getInstance();
    // Reuse an existing unit for this file (another face of this mesh, a prior
    // reload, or one the user already loaded) so we don't duplicate it.
    LLUUID tracking_id = mgr->getUnitID(filename);
    if (tracking_id.isNull())
    {
        tracking_id = mgr->addUnit(filename, /*mesh_owned=*/true);
    }
    // Keep mesh-owned units we reference this parse; a reused USER unit is left
    // untracked (the user owns it, and we must not release it on reload/delete).
    if (tracking_id.notNull() && mgr->isMeshOwned(tracking_id))
    {
        track_owned(owned, tracking_id);
    }
    return mgr->getWorldID(tracking_id); // null if the image failed to load
}

void LLLocalMesh::importGLTFMaterials(std::map<std::string, LLUUID>& out_by_name, std::vector<LLUUID>& owned)
{
    LLLocalGLTFMaterialMgr* mgr = LLLocalGLTFMaterialMgr::getInstance();

    // Load the file's materials once as mesh-owned, unless a prior parse already did.
    // (A user-loaded copy from the Materials tab is a separate, non-mesh-owned set
    // and doesn't count.) Import-time dedup can leave gaps in per-file material
    // indices, so enumerate units by tracking id rather than walking indices.
    std::vector<LLUUID> tids;
    mgr->getTrackingIDs(mFilename, tids);
    bool has_mesh_owned = false;
    for (const LLUUID& tid : tids)
    {
        if (mgr->isMeshOwned(tid)) { has_mesh_owned = true; break; }
    }
    if (!has_mesh_owned)
    {
        mgr->addUnit(mFilename, /*mesh_owned=*/true);
        tids.clear();
        mgr->getTrackingIDs(mFilename, tids);
    }

    // Keep this parse's mesh-owned units referenced (a user-loaded copy is left
    // untracked -- the user owns it; we must not release it on reload/delete).
    for (const LLUUID& tid : tids)
    {
        if (mgr->isMeshOwned(tid))
        {
            track_owned(owned, tid);
        }
    }
    mgr->getWorldIDsByName(mFilename, out_by_name);
}

bool LLLocalMesh::ingestScene(LLModelLoader::scene& scene)
{
    // Build into locals first; members are only overwritten once we know the
    // parse produced geometry, so a failed reload keeps showing the last good
    // mesh. Each LLModel becomes its own part (<= 8 faces) -- exactly the split
    // the upload path makes -- so a >8-face or multi-node file spawns as a
    // linkset instead of dropping geometry.
    //
    // A *rigged* unit keeps the loader's geometry and skin verbatim: the loader
    // already normalized the mesh to a unit box and built a matching bind-shape
    // matrix, so re-normalizing or baking the instance transform here would desync
    // the skinning once the preview is attached to an avatar. Static units bake
    // the instance transforms and normalize for in-world linkset placement.
    bool unit_rigged = false;
    for (auto iter = scene.begin(); iter != scene.end() && !unit_rigged; ++iter)
    {
        for (LLModelInstance& instance : iter->second)
        {
            LLModel* mdl = instance.mModel.notNull() ? instance.mModel.get() : instance.mLOD[LLModel::LOD_HIGH].get();
            if (mdl && !mdl->mSkinInfo.mJointNames.empty())
            {
                unit_rigged = true;
                break;
            }
        }
    }

    std::vector<LLLocalMeshPart> parts;
    std::vector<LLVector3> centers; // static units only: scene-space centre per part
    S32 num_vertices = 0, num_triangles = 0, num_joints = 0;

    // glTF: import the file's materials once (mesh-owned) and map them by binding
    // name, so each face can be pointed at the matching local-gltf material below.
    // Mesh-owned imports referenced by THIS parse. On commit we release any the
    // previous parse used but this one dropped (e.g. a material removed in an edit),
    // so reloads don't accumulate stale local textures/materials.
    std::vector<LLUUID> new_owned_bitmaps;
    std::vector<LLUUID> new_owned_materials;

    std::map<std::string, LLUUID> gltf_mat_by_name;
    if (mFormat == FMT_GLTF)
    {
        importGLTFMaterials(gltf_mat_by_name, new_owned_materials);
    }

    for (auto iter = scene.begin(); iter != scene.end(); ++iter)
    {
        LLMatrix4a mat;
        mat.loadu(iter->first);

        for (LLModelInstance& instance : iter->second)
        {
            LLModel* mdl = instance.mModel.notNull() ? instance.mModel.get() : instance.mLOD[LLModel::LOD_HIGH].get();
            if (!mdl || mdl->getNumVolumeFaces() <= 0)
            {
                continue;
            }

            std::vector<LLVolumeFace> faces;
            faces.reserve(mdl->getNumVolumeFaces());
            // Built once per model (empty/cheap for static meshes): O(1) per-vertex
            // weight lookup so the rigged weight pass below is O(V), not O(V^2).
            const LLModel::JointWeightCache weight_locator(*mdl);
            for (S32 fi = 0; fi < mdl->getNumVolumeFaces(); ++fi)
            {
                LLVolumeFace face = mdl->getVolumeFace(fi); // deep copy
                if (!unit_rigged)
                {
                    // Static: bake the instance transform into authored world space.
                    transformFace(face, mat);
                }
                else
                {
                    // Rigged: the loader leaves face.mWeights empty (weights live
                    // in mSkinWeights). Fill them so the injected volume skins to
                    // the avatar the same way a downloaded rigged mesh would.
                    populateFaceWeights(face, *mdl, weight_locator);
                }
                num_vertices += face.mNumVertices;
                num_triangles += face.mNumIndices / 3;
                faces.push_back(face);
            }
            if (faces.empty())
            {
                continue;
            }

            LLLocalMeshPart part;
            part.mWorldID.generate();
            part.mNumFaces = (S32)faces.size();
            part.mName = !instance.mLabel.empty() ? instance.mLabel : mdl->getName();

            // Capture each face's material (M7.2). faces[fi] lines up 1:1 with
            // mdl->mMaterialList[fi] (cacheOptimize keeps face order), so a flat
            // per-face vector is the minimal representation. For Blinn-Phong (.dae)
            // register the diffuse image as a mesh-owned local bitmap and remember
            // its world id + color; glTF is handled via the material manager (M7.4),
            // so its faces fall through to the default texture here.
            part.mFaceMaterials.resize(faces.size());
            if (mFormat == FMT_DAE)
            {
                for (S32 fi = 0; fi < (S32)faces.size() && fi < (S32)mdl->mMaterialList.size(); ++fi)
                {
                    material_map::const_iterator mit = instance.mMaterial.find(mdl->mMaterialList[fi]);
                    if (mit == instance.mMaterial.end())
                    {
                        continue;
                    }
                    const LLImportMaterial& im = mit->second;
                    LLLocalMeshFaceMaterial& fm = part.mFaceMaterials[fi];
                    fm.mDiffuseColor = im.mDiffuseColor;
                    fm.mFullbright   = im.mFullbright;
                    if (!im.mDiffuseMapFilename.empty())
                    {
                        fm.mDiffuseID = registerOwnedBitmap(im.mDiffuseMapFilename, new_owned_bitmaps);
                    }
                }
            }
            else if (mFormat == FMT_GLTF && !gltf_mat_by_name.empty())
            {
                for (S32 fi = 0; fi < (S32)faces.size() && fi < (S32)mdl->mMaterialList.size(); ++fi)
                {
                    std::map<std::string, LLUUID>::const_iterator it =
                        gltf_mat_by_name.find(mdl->mMaterialList[fi]);
                    if (it != gltf_mat_by_name.end())
                    {
                        part.mFaceMaterials[fi].mRenderMaterialID = it->second;
                    }
                }
            }

            if (unit_rigged)
            {
                // Keep the loader's (already unit-box) geometry as-is; record the
                // authored size for the in-world static view. Placement comes from
                // the rig once attached, so no per-part offset.
                LLVector3 nscale, ntrans;
                mdl->getNormalizedScaleTranslation(nscale, ntrans);
                part.mScale  = nscale;
                part.mOffset = LLVector3::zero;
            }
            else
            {
                // Normalize to a unit box; keep size (prim scale) + scene-space
                // centre (offset within the model, computed below).
                LLVector3 center;
                part.mScale = normalizeFaces(faces, &center);
                centers.push_back(center);
            }

            // Keep each face's mNormalizedScale consistent with the geometry that
            // actually reaches cacheOptimize() below. cacheOptimize reconstructs the
            // mesh in its un-normalized ("original") frame to generate tangents with
            // correct UV proportions -- scaling positions/normals by mNormalizedScale
            // and back. The static path re-normalizes here (a SECOND normalization
            // after the loader's), so a rotated or non-uniformly scaled instance
            // transform makes the re-normalized size differ from the loader's stored
            // scale. Leaving mNormalizedScale stale runs the normals through
            // normalize(normalize(n / S_stale) * S_stale), which is NOT identity for a
            // non-uniform model (e.g. a tall, narrow body) -- so a rezzed preview
            // renders with visibly skewed normals (and tangents). part.mScale is the
            // size that un-normalizes this part's unit-box geometry, so it is the
            // correct value for both paths (rigged already matches; this makes the
            // invariant explicit).
            for (LLVolumeFace& nf : faces)
            {
                nf.mNormalizedScale = part.mScale;
            }

            LLVolumeParams vparams;
            vparams.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
            part.mVolume = new LLVolume(vparams, 1.f);
            part.mVolume->copyFacesFrom(faces);
            // cacheOptimize generates tangents (loaded mesh assets require them
            // and raycast picking dereferences them) -- before setMeshAssetLoaded.
            // Mirrors the download path (LLVolume::unpackVolumeFacesInternal).
            if (!part.mVolume->cacheOptimize(true))
            {
                LL_WARNS("LocalMesh") << "cacheOptimize failed for '" << mShortName << "'" << LL_ENDL;
            }
            part.mVolume->setMeshAssetLoaded(true);

            if (!mdl->mSkinInfo.mJointNames.empty())
            {
                // Owned copy bound to this part's world id. By default include_joints
                // is false: it strips the alt-inverse-bind matrices (joint-position
                // overrides) + pelvis offset while keeping joint names, inverse bind,
                // and bind shape -- everything skinning needs. That mirrors the
                // mesh-upload preview default (show_joint_overrides off): the overrides
                // reshape the avatar skeleton on attach, and a mesh weighted for the
                // default skeleton (the common case) scrunches when they're applied.
                // Fitted bodies that DO need the overrides opt in via the mesh tab's
                // "Joint Positions" toggle (mIncludeJointPositions).
                LLSD sd = mdl->mSkinInfo.asLLSD(mIncludeJointPositions, false);
                part.mSkinInfo = new LLMeshSkinInfo(part.mWorldID, sd);
                num_joints = llmax(num_joints, (S32)mdl->mSkinInfo.mJointNames.size());
            }

            parts.push_back(part);
        }
    }

    if (parts.empty())
    {
        // Failed parse: drop only the units this attempt newly created, keeping the
        // old owned set and the last-good geometry.
        release_dropped(LLLocalBitmapMgr::getInstance(), new_owned_bitmaps, mOwnedBitmaps, this);
        release_dropped(LLLocalGLTFMaterialMgr::getInstance(), new_owned_materials, mOwnedMaterials, this);
        LL_WARNS("LocalMesh") << "Local mesh produced no geometry: " << mFilename << LL_ENDL;
        return false; // keep any previously loaded geometry intact
    }

    if (!unit_rigged)
    {
        // Combined bounding box across all parts -> the model centre. Each part's
        // offset is relative to it, so the spawn drops the whole model centred in
        // front of the agent with the parts in their authored positions.
        LLVector3 cmin = centers[0] - parts[0].mScale * 0.5f;
        LLVector3 cmax = centers[0] + parts[0].mScale * 0.5f;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            update_min_max(cmin, cmax, centers[i] - parts[i].mScale * 0.5f);
            update_min_max(cmin, cmax, centers[i] + parts[i].mScale * 0.5f);
        }
        const LLVector3 model_center = (cmin + cmax) * 0.5f;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            parts[i].mOffset = centers[i] - model_center;
        }
    }

    // Commit. First release mesh-owned imports the previous version used but this one
    // no longer does (e.g. a material/texture removed in an edit), then adopt the new
    // owned sets so they're freed with the mesh later.
    release_dropped(LLLocalBitmapMgr::getInstance(), mOwnedBitmaps, new_owned_bitmaps, this);
    release_dropped(LLLocalGLTFMaterialMgr::getInstance(), mOwnedMaterials, new_owned_materials, this);
    mOwnedBitmaps   = std::move(new_owned_bitmaps);
    mOwnedMaterials = std::move(new_owned_materials);

    mParts        = std::move(parts);
    mNumFaces     = 0;
    for (const LLLocalMeshPart& p : mParts) { mNumFaces += p.mNumFaces; }
    mNumVertices  = num_vertices;
    mNumTriangles = num_triangles;
    mNumJoints    = num_joints;
    mState        = ST_LOADED;
    mLastModified = mPendingModified; // the exact version we just parsed

    return true;
}

bool LLLocalMesh::isRigged() const
{
    for (const LLLocalMeshPart& p : mParts)
    {
        if (p.mSkinInfo.notNull())
        {
            return true;
        }
    }
    return false;
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

bool LLLocalMesh::forceReload()
{
    // Re-parse the current file regardless of mtime so a changed build option (e.g.
    // the joint-position toggle) takes effect. Reuses the reload path: onLoadResult()
    // rebuilds the geometry and refreshes the in-world linkset if one exists.
    if (mReloading || !gDirUtilp->fileExists(mFilename))
    {
        return false;
    }
    mReloading = true;
    startLoad(); // captures mPendingModified
    return true;
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
    cleanup(); // release spawned objects + preview avatar (idempotent)

    for (LLLocalMesh* unit : mMeshList)
    {
        delete unit;
    }
    mMeshList.clear();
}

void LLLocalMeshMgr::cleanup()
{
    // Release every client-only object we rezzed and the preview avatar. Called
    // from LLViewerObjectList::killAllObjects() so they die while the object list
    // and regions are still valid, rather than later when this singleton is torn
    // down. markDead() is idempotent, so the kill loop re-marking them is fine.
    mTimer.stopTimer();

    for (auto& entry : mSpawnedCopies)
    {
        for (LLPointer<LLViewerObject>& p : entry.second.mPrims)
        {
            if (p.notNull() && !p->isDead())
            {
                detachRootIfAttached(p.get()); // unwear before it dies (only the root is worn)
                p->markDead();
            }
        }
    }
    mSpawnedCopies.clear();

    if (mPreviewAvatar.notNull())
    {
        if (!mPreviewAvatar->isDead())
        {
            mPreviewAvatar->markDead();
        }
        mPreviewAvatar = nullptr;
    }
}

void LLLocalMeshMgr::despawnObjectsInRegion(LLViewerRegion* regionp)
{
    // The hosting region is being torn down (LLViewerObjectList::killObjects).
    // Release our client-only objects in it so they don't dangle past their
    // region; the loaded units stay, so a later spawn/reload re-creates them in
    // whatever region is current then. markDead() is idempotent.
    for (auto iter = mSpawnedCopies.begin(); iter != mSpawnedCopies.end(); )
    {
        SpawnedCopy& copy = iter->second;
        LLViewerObject* root = copy.mPrims.empty() ? nullptr : copy.mPrims.front().get();
        // A copy is a single linkset in one region, so the root's region covers it all.
        if (!root || root->isDead() || root->getRegion() == regionp)
        {
            for (LLPointer<LLViewerObject>& p : copy.mPrims)
            {
                if (p.notNull() && !p->isDead())
                {
                    detachRootIfAttached(p.get()); // unwear before it dies
                    p->markDead();
                }
            }
            iter = mSpawnedCopies.erase(iter);
        }
        else
        {
            ++iter;
        }
    }

    if (mPreviewAvatar.notNull() && (mPreviewAvatar->isDead() || mPreviewAvatar->getRegion() == regionp))
    {
        if (!mPreviewAvatar->isDead())
        {
            mPreviewAvatar->markDead();
        }
        mPreviewAvatar = nullptr; // recreated lazily in the current region on next load
    }
}

LLVOAvatar* LLLocalMeshMgr::getPreviewAvatar(bool run_stand_anim)
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
            // Leave the skeleton at its REST pose by default. The FBX loader rebuilds missing
            // inverse binds as inverse(joint world matrix), so the joints must sit at the
            // canonical bind pose; the stand animation would pose the arms and skew those
            // binds. Only run it if this avatar is actually being rendered.
            if (run_stand_anim)
            {
                av->startMotion(ANIM_AGENT_STAND);
            }
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

LLUUID LLLocalMeshMgr::addUnit(const std::string& filename, bool include_joints)
{
    return addUnitInternal(filename, include_joints);
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

LLUUID LLLocalMeshMgr::addUnitInternal(const std::string& filename, bool include_joints)
{
    // No double-add: a file that's already loaded just returns its existing unit
    // (so it can be rezzed again rather than duplicated in the list).
    if (LLUUID existing = getUnitID(filename); existing.notNull())
    {
        return existing;
    }

    LLLocalMesh* unit = new LLLocalMesh(filename, include_joints);
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
    mUnitsChangedSignal();
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
            despawnUnit(tracking_id);
            // Release the mesh-owned local bitmaps + materials imported for this unit.
            for (const LLUUID& bid : unit->mOwnedBitmaps)
            {
                LLLocalBitmapMgr::getInstance()->delUnit(bid);
            }
            for (const LLUUID& mid : unit->mOwnedMaterials)
            {
                LLLocalGLTFMaterialMgr::getInstance()->delUnit(mid);
            }
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
    mUnitsChangedSignal();
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

const LLLocalMeshPart* LLLocalMeshMgr::findPart(const LLUUID& world_id) const
{
    if (world_id.isNull())
    {
        return nullptr;
    }
    for (LLLocalMesh* unit : mMeshList)
    {
        for (const LLLocalMeshPart& part : unit->getParts())
        {
            if (part.mWorldID == world_id)
            {
                return &part;
            }
        }
    }
    return nullptr;
}

bool LLLocalMeshMgr::isLocal(const LLUUID& world_id) const
{
    return findPart(world_id) != nullptr;
}

LLVolume* LLLocalMeshMgr::getVolumeForWorldID(const LLUUID& world_id) const
{
    const LLLocalMeshPart* part = findPart(world_id);
    return part ? part->mVolume.get() : nullptr;
}

const LLMeshSkinInfo* LLLocalMeshMgr::getSkinInfoForWorldID(const LLUUID& world_id) const
{
    const LLLocalMeshPart* part = findPart(world_id);
    return part ? part->mSkinInfo.get() : nullptr;
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

bool LLLocalMeshMgr::isImportOwnedByOther(const LLUUID& tracking_id, const LLLocalMesh* exclude) const
{
    if (tracking_id.isNull())
    {
        return false;
    }
    for (const LLLocalMesh* unit : mMeshList)
    {
        if (unit == exclude)
        {
            continue;
        }
        if (std::find(unit->mOwnedBitmaps.begin(), unit->mOwnedBitmaps.end(), tracking_id) != unit->mOwnedBitmaps.end()
            || std::find(unit->mOwnedMaterials.begin(), unit->mOwnedMaterials.end(), tracking_id) != unit->mOwnedMaterials.end())
        {
            return true;
        }
    }
    return false;
}

std::vector<std::string> LLLocalMeshMgr::getFilenames() const
{
    std::vector<std::string> out;
    out.reserve(mMeshList.size());
    for (const LLLocalMesh* unit : mMeshList)
    {
        out.push_back(unit->getFilename());
    }
    return out;
}

void LLLocalMeshMgr::despawnPreviewObject(LLViewerObject* obj)
{
    if (!obj)
    {
        return;
    }

    // Map the object (linkset root or child) back to the single rezzed copy that owns
    // it and derez just that copy -- so the in-world Delete key removes one copy, not
    // every copy of the unit. The loaded file stays in the list so it can be
    // re-spawned, and onLoadResult won't auto-respawn it on a later file save.
    despawnInstance(instanceForObject(obj));
}

void LLLocalMeshMgr::despawn(const LLUUID& tracking_id)
{
    if (tracking_id.notNull())
    {
        despawnUnit(tracking_id); // removes the in-world linkset, keeps the unit loaded
    }
}

boost::signals2::connection LLLocalMeshMgr::setUnitsChangedCallback(const std::function<void()>& cb)
{
    return mUnitsChangedSignal.connect(cb);
}

LLViewerObject* LLLocalMeshMgr::findRootForObject(const LLViewerObject* obj) const
{
    if (!obj)
    {
        return nullptr;
    }
    // Resolve obj (root or child) to its rezzed copy, then return that copy's root.
    return getInstanceRoot(instanceForObject(obj));
}

LLUUID LLLocalMeshMgr::instanceForObject(const LLViewerObject* obj) const
{
    if (obj)
    {
        // obj may be any prim of a copy (root or child); match it within each copy.
        // Only called by the infrequent per-object paths (in-world Delete, attach/
        // detach), so scanning the copies' prims is fine.
        for (const auto& entry : mSpawnedCopies)
        {
            for (const LLPointer<LLViewerObject>& p : entry.second.mPrims)
            {
                if (p.get() == obj)
                {
                    return entry.first;
                }
            }
        }
    }
    return LLUUID::null;
}

bool LLLocalMeshMgr::getPreviewDisplay(const LLViewerObject* obj, std::string& name_out, std::string& path_out) const
{
    auto it = mSpawnedCopies.find(instanceForObject(obj));
    if (it == mSpawnedCopies.end())
    {
        return false;
    }
    LLLocalMesh* unit = getUnit(it->second.mTrackingID);
    if (!unit)
    {
        return false;
    }
    path_out = unit->getFilename();
    name_out = unit->getShortName();
    // One prim per part (see spawnLinkset), so the prim's index in this copy maps
    // 1:1 to its part -- prefer the part's own sub-mesh name when it has one.
    const std::vector<LLPointer<LLViewerObject>>& prims = it->second.mPrims;
    const std::vector<LLLocalMeshPart>& parts = unit->getParts();
    for (size_t i = 0; i < prims.size(); ++i)
    {
        if (prims[i].get() == obj)
        {
            if (i < parts.size() && !parts[i].mName.empty())
            {
                name_out = parts[i].mName;
            }
            break;
        }
    }
    return true;
}

LLViewerObject* LLLocalMeshMgr::getInstanceRoot(const LLUUID& instance_id) const
{
    auto it = mSpawnedCopies.find(instance_id);
    if (it != mSpawnedCopies.end() && !it->second.mPrims.empty())
    {
        LLViewerObject* root = it->second.mPrims.front().get();
        if (root && !root->isDead())
        {
            return root;
        }
    }
    return nullptr;
}

bool LLLocalMeshMgr::isRiggedPreview(const LLViewerObject* obj) const
{
    if (!obj || !obj->isLocalOnly())
    {
        return false;
    }
    auto it = mSpawnedCopies.find(instanceForObject(obj));
    if (it != mSpawnedCopies.end())
    {
        LLLocalMesh* unit = getUnit(it->second.mTrackingID);
        return unit && unit->isRigged();
    }
    return false;
}

bool LLLocalMeshMgr::isPreviewAttached(const LLViewerObject* obj) const
{
    LLViewerObject* root = findRootForObject(obj);
    return root && root->isAttachment();
}

void LLLocalMeshMgr::attachPreviewToAvatar(LLViewerObject* obj, S32 attach_point)
{
    if (!isAgentAvatarValid())
    {
        return;
    }
    LLViewerObject* root = findRootForObject(obj);
    if (!root || root->isAttachment())
    {
        return; // not one of ours, or already worn
    }

    // Reproduce a server attach's end state for this client-only linkset:
    //  * encode the chosen attachment-point id into the state every prim carries, so
    //    getTargetAttachmentPoint() binds the linkset to that point and isAttachment()/
    //    getAvatar() recognise them. The point matters: the avatar sorts rigged
    //    render order by attachment-point id, so the user picks it from the normal
    //    "Attach" menu. ATTACHMENT_ID_FROM_STATE is a symmetric nibble-swap, so
    //    applying it to the id yields the state (id 1 == chest == state 0x10).
    //  * make the root a child of the avatar in the object tree so getAvatar()'s
    //    parent walk reaches the agent (root for itself, children via the root) --
    //    this is what routes the faces into the rigged draw path,
    //  * attachObject() to parent the drawable to the joint (recursively, including
    //    children). The preview skin carries no joint-position overrides (stripped
    //    in ingestScene to match the upload default), so the agent skeleton is left
    //    as-is and a default-weighted mesh renders correctly.
    const LLUUID instance_id = instanceForObject(root);
    const S32 point = (attach_point > 0) ? attach_point : 1; // default to chest
    const U8 attach_state = (U8)ATTACHMENT_ID_FROM_STATE(point);
    if (auto it = mSpawnedCopies.find(instance_id); it != mSpawnedCopies.end())
    {
        for (LLPointer<LLViewerObject>& p : it->second.mPrims)
        {
            if (p.notNull() && !p->isDead())
            {
                p->setAttachmentState(attach_state);
            }
        }
    }

    LLSelectMgr::getInstance()->deselectAll(); // dropping the in-world selection before reparenting

    // Normal attach path: addChild() makes the root a child of the avatar in the
    // object tree (so getAvatar()'s parent walk reaches the agent and routes the
    // rigged faces into the avatar draw path), then attaches it -- parenting the
    // drawable to the joint. The LLVOAvatarSelf::attachObject() override and the RLV
    // watchdog detect the client-only flag and skip the inventory/COF/RLV
    // bookkeeping a preview lacks.
    gAgentAvatarp->addChild(root);

    mUnitsChangedSignal(); // worn state changed -> refresh the list status
    LL_INFOS("LocalMesh") << "Attached local mesh preview to avatar" << LL_ENDL;
}

void LLLocalMeshMgr::detachPreviewFromAvatar(LLViewerObject* obj)
{
    LLViewerObject* root = findRootForObject(obj);
    if (!root || !root->isAttachment() || !isAgentAvatarValid())
    {
        return;
    }
    const LLUUID instance_id = instanceForObject(root);

    LLSelectMgr::getInstance()->deselectAll();

    // Detach in place (NOT despawn/respawn) so the same prims -- and any edits the
    // user made (textures, colours, transforms) -- survive being taken off.
    //
    // Clear the attachment state on this copy's WHOLE linkset FIRST: the makeStatic()
    // inside LLViewerJointAttachment::removeObject() is guarded on !isAttachment(), so
    // with the state still set it's a no-op and the drawables stay ACTIVE on the
    // avatar's spatial bridge -- once detached they then render adrift from their
    // bounding boxes (mesh in one spot, bbox across the region). Clearing first lets
    // removeObject() re-home the linkset (root + children) into the region partition.
    if (auto it = mSpawnedCopies.find(instance_id); it != mSpawnedCopies.end())
    {
        for (LLPointer<LLViewerObject>& p : it->second.mPrims)
        {
            if (p.notNull() && !p->isDead())
            {
                p->setAttachmentState(0);
            }
        }
    }

    // LLVOAvatar::removeChild() clears the object-tree parent AND detaches
    // (removeObject -> makeStatic, now effective), so a single call does both.
    gAgentAvatarp->removeChild(root);

    // setupDrawable() parented the root drawable's xform to the attachment joint;
    // restore it as a region root so its world position comes from getPositionAgent().
    if (root->mDrawable.notNull())
    {
        root->mDrawable->mXform.setParent(NULL);
    }

    // Put it back in-world a few metres in front of the agent.
    root->setPositionAgent(gAgent.getPositionAgent() + gAgent.getAtAxis() * 3.f);
    root->markForUpdate();

    mUnitsChangedSignal(); // worn state changed -> refresh the list status
}

void LLLocalMeshMgr::detachRootIfAttached(LLViewerObject* root)
{
    // Only the linkset root is the avatar's direct attachment; children hang off the
    // root in the object tree, so skip anything that isn't worn on the avatar.
    if (!root || !root->isAttachment() || !isAgentAvatarValid()
        || root->getParent() != (LLViewerObject*)gAgentAvatarp)
    {
        return;
    }
    // Clear the attachment state first so the makeStatic() inside removeObject()
    // runs (it's guarded on !isAttachment()), then remove from the avatar in one
    // call: LLVOAvatar::removeChild() clears the object-tree parent AND detaches via
    // LLVOAvatarSelf::detachObject() (local-safe, no inventory/COF/RLV traffic).
    // Used by the despawn/cleanup paths right before markDead(), so -- unlike
    // detachPreviewFromAvatar() -- no in-world re-home is needed.
    root->setAttachmentState(0);
    gAgentAvatarp->removeChild(root);
}

LLViewerObject* LLLocalMeshMgr::spawnInWorld(const LLUUID& tracking_id)
{
    LLLocalMesh* unit = getUnit(tracking_id);
    if (!unit || !unit->getValid() || unit->getParts().empty())
    {
        LL_WARNS("LocalMesh") << "spawnInWorld: no valid unit for " << tracking_id << LL_ENDL;
        return nullptr;
    }

    if (!isAgentAvatarValid() || !gAgent.getRegion())
    {
        LL_WARNS("LocalMesh") << "spawnInWorld: agent/region not ready" << LL_ENDL;
        return nullptr;
    }

    // Always rez a NEW copy a few metres in front of the agent. `base` is the root
    // prim's world position (root = the model centre + part 0's offset).
    const std::vector<LLLocalMeshPart>& parts = unit->getParts();
    const LLVector3 base = gAgent.getPositionAgent() + gAgent.getAtAxis() * 3.f + parts[0].mOffset;
    LLUUID instance_id;
    instance_id.generate();

    LLViewerObject* root = spawnLinkset(tracking_id, instance_id, base, LLQuaternion(), false, 0);
    if (root)
    {
        LL_INFOS("LocalMesh") << "Spawned local mesh '" << unit->getShortName() << "' as "
                              << unit->getNumParts() << " prim(s), " << unit->getNumFaces() << " faces" << LL_ENDL;
    }
    return root;
}

LLViewerObject* LLLocalMeshMgr::spawnLinkset(const LLUUID& tracking_id, const LLUUID& instance_id,
                                             const LLVector3& base, const LLQuaternion& root_rot,
                                             bool attach, S32 attach_point)
{
    LLLocalMesh* unit = getUnit(tracking_id);
    if (!unit || !unit->getValid() || unit->getParts().empty()
        || !isAgentAvatarValid() || !gAgent.getRegion())
    {
        return nullptr;
    }
    const std::vector<LLLocalMeshPart>& parts = unit->getParts();

    // One prim per part, linked into a single linkset rooted at the first prim --
    // the same structure an upload of this file would rez. The whole linkset is one
    // copy, stored under instance_id, so it can be addressed independently of the
    // unit's other copies.
    SpawnedCopy copy;
    copy.mTrackingID = tracking_id;
    copy.mSeq        = mNextSpawnSeq++;

    LLViewerObject* root = nullptr;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const LLLocalMeshPart& part = parts[i];

        LLViewerObject* obj = gObjectList.createObjectViewer(LL_PCODE_VOLUME, gAgent.getRegion());
        LLVOVolume* vol = dynamic_cast<LLVOVolume*>(obj);
        if (!vol)
        {
            LL_WARNS("LocalMesh") << "spawnLinkset: failed to create volume object" << LL_ENDL;
            if (obj) { obj->markDead(); }
            continue;
        }

        // Client-only object: mark it so the build/select code skips server
        // traffic and deletes it locally. Selectable/movable with full owner
        // permission flags so the tools enable manipulation.
        vol->mbCanSelect = true;
        vol->mIsLocalOnly = true;
        vol->setFlagsWithoutUpdate(FLAGS_OBJECT_YOU_OWNER | FLAGS_OBJECT_MODIFY | FLAGS_OBJECT_MOVE | FLAGS_OBJECT_COPY | FLAGS_OBJECT_TRANSFER, true);

        // Build the drawable and force a valid LOD before setVolume (a NO_LOD
        // drawable would make LLVOVolume::setVolume build a placeholder cube and
        // skip the repository injection). See applyPartGeometry.
        gPipeline.createObject(obj);
        vol->setLOD(LLVolumeLODGroup::NUM_LODS - 1);

        if (root)
        {
            root->addChild(vol); // link into the root's linkset (sets mParent)
            // Parent the drawable too, so the child renders/moves relative to the
            // root. Server linksets get this in processUpdateMessage(); a
            // client-only linkset must establish it explicitly.
            vol->setDrawableParent(root->mDrawable);
        }
        else
        {
            root = obj;
        }

        vol->setScale(part.mScale, false);
        applyPartGeometry(vol, part);

        // Place each prim at the model centre + its offset. Children are already
        // parented, so setPositionAgent converts to a parent-relative offset; the
        // root's rotation is still identity here, making that a pure translation.
        vol->setPositionAgent(base + (part.mOffset - parts[0].mOffset));

        copy.mPrims.push_back(obj);
    }

    if (root)
    {
        // Store the copy before attaching -- attachPreviewToAvatar() looks it up by id.
        mSpawnedCopies[instance_id] = std::move(copy);
        root->setRotation(root_rot); // identity for a fresh rez; the saved orientation on reload
        if (attach)
        {
            attachPreviewToAvatar(root, attach_point); // restore a worn copy across a re-spawn
        }
    }

    mUnitsChangedSignal(); // rezzed state changed
    return root;
}

void LLLocalMeshMgr::respawnInstancesInPlace(const LLUUID& tracking_id)
{
    // Capture each existing copy's identity, transform and attachment, then re-rez
    // them in place. Used when a reload changes the prim count (hot-swap can't swap
    // 1:1), so every copy refreshes without moving or losing its worn state.
    struct Saved
    {
        LLUUID       mInstanceID;
        LLVector3    mBase;
        LLQuaternion mRot;
        bool         mAttached = false;
        S32          mPoint = 0;
    };
    std::vector<Saved> saved;
    for (const auto& entry : mSpawnedCopies)
    {
        const SpawnedCopy& copy = entry.second;
        if (copy.mTrackingID != tracking_id || copy.mPrims.empty())
        {
            continue;
        }
        LLViewerObject* r = copy.mPrims.front().get(); // mPrims[0] is the root
        if (!r || r->isDead())
        {
            continue;
        }
        Saved s;
        s.mInstanceID = entry.first;
        s.mBase       = r->getPositionAgent();
        s.mRot        = r->getRotation();
        s.mAttached   = r->isAttachment();
        s.mPoint      = s.mAttached ? ATTACHMENT_ID_FROM_STATE(r->getAttachmentState()) : 0;
        saved.push_back(s);
    }

    despawnUnit(tracking_id); // drop all copies; re-rez them below at the same places

    for (const Saved& s : saved)
    {
        spawnLinkset(tracking_id, s.mInstanceID, s.mBase, s.mRot, s.mAttached, s.mPoint);
    }
}

namespace
{
// A face's user-visible render state, snapshotted so user edits survive a hot-swap:
// applyPartGeometry re-applies the file's imported materials and resets every face,
// which would otherwise wipe textures/materials/glow/etc. the user applied to the
// in-world preview.
struct PreservedFace
{
    LLTextureEntry te;            // diffuse id, color, glow, bump/shiny/fullbright, transforms, Blinn-Phong material, glTF override
    LLUUID         render_mat_id; // glTF render material id (kept in the param block, not the TE)
};

std::vector<PreservedFace> capturePreservedFaces(LLVOVolume* vol)
{
    std::vector<PreservedFace> out;
    const U8 n = vol->getNumTEs();
    out.reserve(n);
    for (U8 i = 0; i < n; ++i)
    {
        PreservedFace pf;
        if (const LLTextureEntry* tep = vol->getTE(i))
        {
            pf.te = *tep;
        }
        pf.render_mat_id = vol->getRenderMaterialID(i);
        out.push_back(pf);
    }
    return out;
}

void restorePreservedFaces(LLVOVolume* vol, const std::vector<PreservedFace>& saved)
{
    const U8 n = vol->getNumTEs();
    bool any_render_mat = false;
    for (U8 i = 0; i < n && i < (U8)saved.size(); ++i)
    {
        const LLTextureEntry& te = saved[i].te;
        vol->setTETexture(i, te.getID());
        vol->setTEColor(i, te.getColor());
        vol->setTEGlow(i, te.getGlow());
        vol->setTEBumpShinyFullbright(i, te.getBumpShinyFullbright());
        F32 ss = 1.f, st = 1.f, os = 0.f, ot = 0.f;
        te.getScale(&ss, &st);
        te.getOffset(&os, &ot);
        vol->setTEScale(i, ss, st);
        vol->setTEOffset(i, os, ot);
        vol->setTERotation(i, te.getRotation());
        if (te.getMaterialParams().notNull())
        {
            vol->setTEMaterialParams(i, te.getMaterialParams()); // Blinn-Phong normal/specular
        }
        if (saved[i].render_mat_id.notNull())
        {
            // glTF PBR material (+ override); client-only, so update_server=false.
            vol->setRenderMaterialID((S32)i, saved[i].render_mat_id, false, true);
            if (const LLGLTFMaterial* ov = te.getGLTFMaterialOverride())
            {
                LLPointer<LLGLTFMaterial> ovp = new LLGLTFMaterial(*ov);
                vol->setTEGLTFMaterialOverride(i, ovp);
            }
            any_render_mat = true;
        }
    }
    if (any_render_mat)
    {
        vol->setHasRenderMaterialParams(true);
    }
    vol->markForUpdate();
}
} // namespace

bool LLLocalMeshMgr::hotSwapInWorld(const LLUUID& tracking_id)
{
    LLLocalMesh* unit = getUnit(tracking_id);
    if (!unit || !unit->getValid())
    {
        return false;
    }
    const std::vector<LLLocalMeshPart>& parts = unit->getParts();

    // Only structurally identical copies can be swapped 1:1; a changed prim count
    // needs a real re-spawn (to add/remove prims) -- caller falls back. The copies
    // are already grouped (one entry per copy), so just check each copy's prim count.
    S32 copies = 0;
    for (const auto& entry : mSpawnedCopies)
    {
        if (entry.second.mTrackingID != tracking_id)
        {
            continue;
        }
        ++copies;
        if (entry.second.mPrims.size() != parts.size())
        {
            return false;
        }
    }
    if (copies == 0)
    {
        return false;
    }

    // Point each prim at the freshly decoded geometry by swapping its mesh (sculpt)
    // id. The new id has an empty volume cache, so the repository copies the new
    // faces and the prim rebuilds in place; LLVOVolume also drops its cached skin
    // when the mesh id changes, so rigged skinning refreshes too. No despawn -> each
    // copy's attachment, selection and transform are all preserved.
    for (auto& entry : mSpawnedCopies)
    {
        SpawnedCopy& copy = entry.second;
        if (copy.mTrackingID != tracking_id)
        {
            continue;
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            LLViewerObject* o = copy.mPrims[i].get();
            LLVOVolume* vol = (o && !o->isDead()) ? dynamic_cast<LLVOVolume*>(o) : nullptr;
            if (!vol)
            {
                return false; // a dead/unexpected prim -> let the caller re-spawn
            }
            vol->setScale(parts[i].mScale, false);
            // Preserve user-applied face params (diffuse/normal/specular/glow/color/
            // render material/transforms) across the swap -- applyPartGeometry resets
            // every face to the file's imported material.
            std::vector<PreservedFace> preserved = capturePreservedFaces(vol);
            applyPartGeometry(vol, parts[i]);
            restorePreservedFaces(vol, preserved);
        }
    }

    LL_INFOS("LocalMesh") << "Hot-swapped local mesh '" << unit->getShortName() << "' ("
                          << copies << " copy(ies), " << parts.size() << " prim(s) each) in place" << LL_ENDL;
    return true;
}

LLViewerObject* LLLocalMeshMgr::getSpawnedRoot(const LLUUID& tracking_id) const
{
    // Any one live copy's root -- enough for "is this unit in-world?" checks.
    for (const auto& entry : mSpawnedCopies)
    {
        const SpawnedCopy& copy = entry.second;
        if (copy.mTrackingID == tracking_id && !copy.mPrims.empty())
        {
            LLViewerObject* root = copy.mPrims.front().get();
            if (root && !root->isDead())
            {
                return root;
            }
        }
    }
    return nullptr;
}

S32 LLLocalMeshMgr::getSpawnedCount(const LLUUID& tracking_id) const
{
    S32 count = 0;
    for (const auto& entry : mSpawnedCopies)
    {
        const SpawnedCopy& copy = entry.second;
        if (copy.mTrackingID == tracking_id && !copy.mPrims.empty()
            && copy.mPrims.front().notNull() && !copy.mPrims.front()->isDead())
        {
            ++count;
        }
    }
    return count;
}

std::vector<LLLocalMeshMgr::SpawnedInstance> LLLocalMeshMgr::getSpawnedInstances() const
{
    // Collect with the rez sequence so the list is stable in rez order, independent
    // of the map's (unspecified) iteration order.
    std::vector<std::pair<U32, SpawnedInstance> > ordered;
    for (const auto& entry : mSpawnedCopies)
    {
        const SpawnedCopy& copy = entry.second;
        if (copy.mPrims.empty() || copy.mPrims.front().isNull() || copy.mPrims.front()->isDead())
        {
            continue;
        }
        ordered.push_back({copy.mSeq, {entry.first, copy.mTrackingID, copy.mPrims.front().get()}});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const std::pair<U32, SpawnedInstance>& a, const std::pair<U32, SpawnedInstance>& b)
              { return a.first < b.first; });

    std::vector<SpawnedInstance> out;
    out.reserve(ordered.size());
    for (const auto& pr : ordered)
    {
        out.push_back(pr.second);
    }
    return out;
}

void LLLocalMeshMgr::despawnInstance(const LLUUID& instance_id)
{
    auto it = mSpawnedCopies.find(instance_id);
    if (it == mSpawnedCopies.end())
    {
        return;
    }
    for (LLPointer<LLViewerObject>& p : it->second.mPrims)
    {
        if (p.notNull() && !p->isDead())
        {
            detachRootIfAttached(p.get()); // unwear before it dies (only the root is worn)
            p->markDead();                 // the root's markDead cascades to its children
        }
    }
    mSpawnedCopies.erase(it);
    mUnitsChangedSignal(); // rezzed state changed
}

void LLLocalMeshMgr::despawnAll()
{
    for (auto& entry : mSpawnedCopies)
    {
        for (LLPointer<LLViewerObject>& p : entry.second.mPrims)
        {
            if (p.notNull() && !p->isDead())
            {
                detachRootIfAttached(p.get()); // unwear before it dies
                p->markDead();                 // the root's markDead cascades to its children
            }
        }
    }
    mSpawnedCopies.clear();
    mUnitsChangedSignal(); // rezzed state changed
}

std::string LLLocalMeshMgr::statusText(LLViewerObject* root) const
{
    if (!root)
    {
        return std::string();
    }
    if (root->isAttachment())
    {
        const S32 point_id = ATTACHMENT_ID_FROM_STATE(root->getAttachmentState());
        std::string point_name = llformat("%d", point_id);
        if (isAgentAvatarValid())
        {
            LLVOAvatar::attachment_map_t::const_iterator iter =
                gAgentAvatarp->mAttachmentPoints.find(point_id);
            if (iter != gAgentAvatarp->mAttachmentPoints.end() && iter->second)
            {
                point_name = iter->second->getName();
            }
        }
        LLSD args;
        args["POINT"] = point_name;
        return LLTrans::getString("LocalAssetAttached", args);
    }
    return LLTrans::getString("LocalAssetRezzed");
}

void LLLocalMeshMgr::setIncludeJointPositions(const LLUUID& tracking_id, bool include)
{
    LLLocalMesh* unit = getUnit(tracking_id);
    if (!unit || unit->mIncludeJointPositions == include)
    {
        return;
    }
    unit->mIncludeJointPositions = include;
    unit->forceReload(); // rebuild the skin with/without the joint-position overrides
    mUnitsChangedSignal(); // persisted property changed -> let LLLocalAssetPaths re-save
}

bool LLLocalMeshMgr::getIncludeJointPositions(const LLUUID& tracking_id) const
{
    const LLLocalMesh* unit = getUnit(tracking_id);
    return unit && unit->mIncludeJointPositions;
}

void LLLocalMeshMgr::applyPartGeometry(LLVOVolume* vol, const LLLocalMeshPart& part)
{
    // isSculpted()/isMesh() key off the PARAMS_SCULPT extra param (not the volume
    // params alone). local_origin=false so nothing is sent to the sim for this
    // client-only object.
    LLSculptParams sculpt_params;
    sculpt_params.setSculptTexture(part.mWorldID, LL_SCULPT_TYPE_MESH);
    vol->setParameterEntry(LLNetworkData::PARAMS_SCULPT, sculpt_params, false);

    // Reference the part by its world id; the repository injection resolves it to
    // the decoded geometry.
    LLVolumeParams params;
    params.setType(LL_PCODE_PROFILE_SQUARE, LL_PCODE_PATH_LINE);
    params.setSculptID(part.mWorldID, LL_SCULPT_TYPE_MESH);
    vol->setVolume(params, LLVolumeLODGroup::NUM_LODS - 1);

    // Apply each face's imported material (M7.2): the diffuse map (a mesh-owned
    // local bitmap), diffuse color and fullbright captured at ingest. Faces with no
    // map fall back to the default texture. No sendTEUpdate() -- the object is
    // client-only (isLocalOnly) and these setters only touch local render state.
    //
    // Use the PART's decoded face count, not vol->getVolume()'s. At initial spawn
    // the viewer object's mesh volume can still report a placeholder face count --
    // the repository injection that resolves the sculpt id to our geometry hasn't
    // realized on this object yet -- so getNumVolumeFaces() reads too few faces and
    // only the first face(s) get textures/materials; the rest stay default until a
    // reload/hot-swap (where the volume is already realized) re-runs this. That is
    // why a fresh import "only materials one face/link until the source is
    // refreshed". part.mNumFaces is the geometry we injected (== part.mVolume's face
    // count), available synchronously, so setNumTEs() and the per-face apply below
    // always cover every face -- and the object then carries the full TE count for
    // later edits (e.g. Apply-to-Selected, which is bounded by getNumTEs()).
    const S32 num_faces = part.mNumFaces;
    bool any_render_mat = false;
    for (S32 i = 0; i < num_faces && i < (S32)part.mFaceMaterials.size(); ++i)
    {
        if (part.mFaceMaterials[i].mRenderMaterialID.notNull())
        {
            any_render_mat = true;
            break;
        }
    }
    if (num_faces > 0)
    {
        vol->setNumTEs((U8)num_faces);

        // Mark the render-material extra param IN USE *before* applying per-face
        // materials. setRenderMaterialID() only persists an id into the param block
        // when getRenderMaterialParams() returns it, and that returns null until the
        // param is in use. Applied per-face beforehand, each call created a throwaway
        // block and only the last face's id survived -- so the rebuild's
        // updateTEMaterialTextures() then cleared every other face's material (a
        // freshly imported glTF mesh showed its material on only one face). A
        // client-only object never gets the server echo that would normally set this.
        if (any_render_mat)
        {
            vol->setHasRenderMaterialParams(true);
        }

        for (S32 i = 0; i < num_faces; ++i)
        {
            const LLLocalMeshFaceMaterial* fm =
                (i < (S32)part.mFaceMaterials.size()) ? &part.mFaceMaterials[i] : nullptr;
            const LLUUID tex = (fm && fm->mDiffuseID.notNull()) ? fm->mDiffuseID : IMG_DEFAULT;
            vol->setTETexture((U8)i, tex);
            if (fm)
            {
                vol->setTEColor((U8)i, fm->mDiffuseColor);
                vol->setTEFullbright((U8)i, fm->mFullbright ? 1 : 0);
                if (fm->mRenderMaterialID.notNull())
                {
                    // glTF PBR material (owns its own textures); update_server=false
                    // because the preview is client-only.
                    vol->setRenderMaterialID((S32)i, fm->mRenderMaterialID, false, true);
                }
            }
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
            logUnit("Reloaded", unit);
            // Refresh an in-world linkset: hot-swap the geometry in place (no despawn,
            // so attachment/selection/transform survive). A changed prim count can't be
            // swapped 1:1 -> fall back to a full re-spawn (which restores the attachment
            // if the preview was worn). Not spawned -> just keep the rebuilt data.
            if (getSpawnedRoot(tracking_id))
            {
                if (!hotSwapInWorld(tracking_id))
                {
                    respawnInstancesInPlace(tracking_id); // prim count changed -> re-rez each copy
                }
                // The skin may have changed (a live edit, or a Joint Positions
                // toggle), so recompute joint-position overrides on every affected
                // avatar. rebuildAttachmentOverrides() clears the old offsets
                // (resetting the skeleton) then re-adds from the current skin -- so a
                // toggle-off truly reverts the skeleton instead of leaving the
                // previous mesh's pelvis/joint offsets stuck on it. Each worn/animesh
                // copy may sit on a different avatar, so rebuild each one (deduped);
                // getAvatar() resolves the agent (attached) or the control avatar
                // (animesh); a plain rezzed rigged copy has none, so it's skipped.
                std::unordered_set<LLVOAvatar*> avatars;
                for (const SpawnedInstance& inst : getSpawnedInstances())
                {
                    if (inst.mTrackingID == tracking_id && inst.mRoot)
                    {
                        if (LLVOAvatar* av = inst.mRoot->getAvatar())
                        {
                            avatars.insert(av);
                        }
                    }
                }
                for (LLVOAvatar* av : avatars)
                {
                    av->rebuildAttachmentOverrides();
                }
            }
        }
        return;
    }

    // Initial load.
    if (assembled)
    {
        logUnit("Loaded", unit);
        if (unit->wantsSpawn())
        {
            LLViewerObject* root = spawnInWorld(tracking_id);
            // Deferred attach: Attach was pressed on this unit while it was still
            // undecoded (see addAndAttach) -- wear it now that it's in-world.
            if (root && unit->wantsAttach() >= 0)
            {
                attachPreviewToAvatar(root, unit->wantsAttach());
            }
        }
    }
    else
    {
        unit->markFailed();
        delUnit(tracking_id); // drop the failed unit
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

void LLLocalMeshMgr::addAndAttach(const std::string& filename, S32 attach_point)
{
    if (filename.empty())
    {
        return;
    }
    const LLUUID tracking_id = addUnit(filename); // dedups: existing unit if already loaded
    LLLocalMesh* unit = tracking_id.notNull() ? getUnit(tracking_id) : nullptr;
    if (!unit)
    {
        return;
    }
    if (unit->getValid())
    {
        // Already decoded -- spawn and wear it now.
        if (LLViewerObject* root = spawnInWorld(tracking_id))
        {
            attachPreviewToAvatar(root, attach_point);
        }
    }
    else
    {
        // Still loading (the common case for an undecoded row) -- onLoadResult will
        // spawn and attach it once the parse finishes.
        unit->setSpawnWhenReady(true);
        unit->setAttachWhenReady(attach_point);
    }
}

void LLLocalMeshMgr::despawnUnit(const LLUUID& tracking_id)
{
    for (auto iter = mSpawnedCopies.begin(); iter != mSpawnedCopies.end(); )
    {
        if (iter->second.mTrackingID == tracking_id)
        {
            for (LLPointer<LLViewerObject>& p : iter->second.mPrims)
            {
                if (p.notNull() && !p->isDead())
                {
                    detachRootIfAttached(p.get()); // unwear before it dies
                    p->markDead();                 // the root's markDead cascades to its children
                }
            }
            iter = mSpawnedCopies.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
    mUnitsChangedSignal(); // rezzed state changed
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
        const S32 count = getSpawnedCount(unit->getTrackingID());
        LLViewerObject* root = getSpawnedRoot(unit->getTrackingID());

        LLSD element;
        element["columns"][0]["column"] = "icon";
        element["columns"][0]["type"]   = "icon";
        element["columns"][0]["value"]  = icon_name;

        element["columns"][1]["column"] = "unit_name";
        element["columns"][1]["type"]   = "text";
        element["columns"][1]["value"]  = unit->getShortName();
        if (root)
        {
            element["columns"][1]["font"]["style"] = "BOLD"; // in-world (rezzed or worn)
        }

        // Status column (the mesh tab adds it): the in-world copy count, or -- when
        // there's exactly one copy -- that copy's state + attachment point.
        std::string status;
        if (count > 1)
        {
            LLSD args;
            args["COUNT"] = count;
            status = LLTrans::getString("LocalMeshInWorldCount", args);
        }
        else if (root)
        {
            status = statusText(root);
        }
        element["columns"][2]["column"] = "status";
        element["columns"][2]["type"]   = "text";
        element["columns"][2]["value"]  = status;

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
