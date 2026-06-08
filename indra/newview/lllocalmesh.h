/**
 * @file lllocalmesh.h
 * @brief Local Mesh preview header
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

// Local Mesh is the mesh analog of Local Bitmap (lllocalbitmaps.h): it loads a
// Collada (.dae) or glTF (.gltf/.glb) file from disk via the same loaders the
// mesh upload path uses, decodes it to in-memory geometry + skin, and assigns
// it a client-only "world" UUID so the rest of the viewer can reference it like
// any other mesh asset -- without uploading to the asset server.
//
// Loading runs on the model-loader's worker thread (LLModelLoader::start), the
// same as the upload floater, because the glTF asset loader blocks on work it
// posts back to the main thread; decoding synchronously on the main thread
// would deadlock. The decoded result is assembled on a main-thread callback.

#ifndef LL_LLLOCALMESH_H
#define LL_LLLOCALMESH_H

#include "llmodelloader.h"  // LLModelLoader, LLModelLoader::scene, JointMap, LLMeshSkinInfo
#include "lleventtimer.h"   // LLEventTimer (live-reload polling)
#include "llpointer.h"
#include "llsingleton.h"
#include "lluuid.h"
#include "v3math.h"
#include "v4color.h"

#include <boost/signals2/signal.hpp>
#include <filesystem>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class LLQuaternion;
class LLScrollListCtrl;
class LLViewerObject;
class LLViewerRegion;
class LLVOAvatar;
class LLVOVolume;
class LLVolume;

// One uploadable sub-mesh of a local mesh file: a single LLModel's geometry,
// normalized to a unit box (as the upload path does), plus where it sits within
// the model. A file with more than 8 faces -- or multiple mesh nodes -- yields
// several parts, exactly the multi-prim linkset an upload of the same file would
// produce. A simple single-mesh file is just one part.
// A single face's material as imported from the model file, resolved to ids the
// viewer can render directly (no upload). For Blinn-Phong (.dae) the diffuse map is
// a mesh-owned local-bitmap world id; for glTF the whole material is a local-gltf
// world id applied as the face's render material.
struct LLLocalMeshFaceMaterial
{
    LLUUID   mDiffuseID;                  // local-bitmap world id (Blinn-Phong), or null
    LLColor4 mDiffuseColor = LLColor4::white;
    bool     mFullbright   = false;
    LLUUID   mRenderMaterialID;          // local-gltf world id (glTF) -> face render material, or null
};

struct LLLocalMeshPart
{
    std::string               mName;          // sub-mesh (model/instance) name, for build-floater display
    LLUUID                    mWorldID;       // unique mesh id objects/repository reference
    LLPointer<LLVolume>       mVolume;        // normalized geometry (<= 8 faces)
    LLPointer<LLMeshSkinInfo> mSkinInfo;      // null if not rigged
    LLVector3                 mScale;         // object scale (decomposed instance scale; rigged: authored size)
    LLQuaternion              mRotation;      // object rotation (decomposed instance rotation; rigged: identity)
    LLVector3                 mOffset;        // scene-space position of the part (static); zero for rigged
    S32                       mNumFaces = 0;
    std::vector<LLLocalMeshFaceMaterial> mFaceMaterials; // parallel to the volume's faces
};

// A single local mesh file and its decoded, in-memory representation (one or more
// parts spawned together as a linkset).
class LLLocalMesh
{
public:
    LLLocalMesh(std::string filename, bool include_joints = false);
    ~LLLocalMesh();

    std::string getFilename() const   { return mFilename; }
    std::string getShortName() const  { return mShortName; }
    LLUUID      getTrackingID() const { return mTrackingID; }

    bool getValid() const   { return mState == ST_LOADED; }
    bool isLoading() const  { return mState == ST_LOADING; }
    bool isFailed() const   { return mState == ST_FAILED; }

    // Decoded parts -- consumed by the spawn path and repository injection.
    const std::vector<LLLocalMeshPart>& getParts() const { return mParts; }
    bool isRigged() const; // true if any part is rigged
    bool hasUVs() const { return mHasUVs; } // false if the source mesh shipped without texcoords

    // Stats (for UI + logging).
    S32 getNumParts() const     { return (S32)mParts.size(); }
    S32 getNumFaces() const     { return mNumFaces; }
    S32 getNumVertices() const  { return mNumVertices; }
    S32 getNumTriangles() const { return mNumTriangles; }
    S32 getNumJoints() const    { return mNumJoints; }

    // Spawn the preview in-world automatically once loading completes.
    void setSpawnWhenReady(bool b) { mSpawnWhenReady = b; }
    bool wantsSpawn() const         { return mSpawnWhenReady; }

    // Deferred attach: wear the preview at this attachment point once loading
    // completes (-1 = don't). Set by LLLocalMeshMgr::addAndAttach when the user
    // hits Attach on a not-yet-decoded unit. Requires wantsSpawn().
    void setAttachWhenReady(S32 attach_point) { mAttachWhenReady = attach_point; }
    S32  wantsAttach() const                  { return mAttachWhenReady; }

private:
    friend class LLLocalMeshMgr; // orchestrates loading/reload and spawning

    enum EFormat { FMT_NONE, FMT_DAE, FMT_GLTF };
    enum EState  { ST_LOADING, ST_LOADED, ST_FAILED };

    void startLoad();

    // Assemble decoded geometry into parts and commit them to this unit; returns
    // false (leaving the previous parts untouched) if the scene yielded nothing,
    // so a failed live-reload keeps showing the last good mesh. Each call mints
    // fresh part world ids so a reload serves new geometry cleanly.
    bool ingestScene(LLModelLoader::scene& scene);
    // Register a model-referenced image as a mesh-owned local bitmap (deduped by
    // filename) and return its world id. Records every mesh-owned unit it references
    // into `owned` (the set this parse keeps) so ingestScene can release the rest.
    LLUUID registerOwnedBitmap(const std::string& filename, std::vector<LLUUID>& owned);
    // Register this glTF file's materials (mesh-owned, deduped) and map each by its
    // face-binding name to its world id, for applying them per face in ingestScene.
    // Records the file's mesh-owned material units into `owned` (kept this parse).
    void importGLTFMaterials(std::map<std::string, LLUUID>& out_by_name, std::vector<LLUUID>& owned);
    void markFailed() { mState = ST_FAILED; }

    // Live reload (M3): poll the source file's mtime and, on a change, kick an
    // async re-parse. The geometry swap happens back in the load callback.
    bool pollForReload();       // true if a reload was started this poll
    bool forceReload();         // re-parse now (e.g. after a build-option change)
    void finishReload(bool ok); // clear in-flight state after the parse returns
    bool isReloading() const { return mReloading; }

    std::string mFilename;
    std::string mShortName;
    LLUUID      mTrackingID; // stable, identifies this unit in UI
    EFormat     mFormat;
    EState      mState;
    bool        mSpawnWhenReady;
    S32         mAttachWhenReady = -1; // attach point to wear at once loaded (-1 = none)
    bool        mIncludeJointPositions = false; // bake joint-position overrides into the skin

    std::filesystem::file_time_type mLastModified; // for live reload (M3)

    std::vector<LLLocalMeshPart> mParts; // one per LLModel; spawned together as a linkset

    // Local-bitmap tracking ids imported for this unit's materials (mesh-owned);
    // released from LLLocalBitmapMgr when this unit is deleted.
    std::vector<LLUUID> mOwnedBitmaps;
    // Local-gltf-material tracking ids imported for this unit (mesh-owned); released
    // from LLLocalGLTFMaterialMgr when this unit is deleted.
    std::vector<LLUUID> mOwnedMaterials;

    S32  mNumFaces;     // totals across all parts
    S32  mNumVertices;
    S32  mNumTriangles;
    S32  mNumJoints;
    bool mHasUVs = true; // false if the source mesh had no UV coordinates (untexturable)

    // Live-reload bookkeeping.
    bool                            mReloading;       // an async re-parse is in flight
    std::filesystem::file_time_type mPendingModified; // mtime of the in-flight reload
    std::filesystem::file_time_type mFailedModified;  // mtime that last failed to parse
};

// Periodic tick that lets loaded units watch their source files for changes.
class LLLocalMeshTimer : public LLEventTimer
{
public:
    LLLocalMeshTimer();
    void startTimer();
    void stopTimer();
    bool isRunning();
    bool tick() override;
};

// Owns all loaded local meshes and resolves between tracking IDs, world IDs and
// filenames.
class LLLocalMeshMgr : public LLSingleton<LLLocalMeshMgr>
{
    LLSINGLETON(LLLocalMeshMgr);
    ~LLLocalMeshMgr();

public:
    LLUUID addUnit(const std::string& filename, bool include_joints = false);
    bool   addUnit(const std::vector<std::string>& filenames);
    void   delUnit(LLUUID tracking_id);

    LLUUID      getUnitID(const std::string& filename) const;
    std::string getFilename(const LLUUID& tracking_id) const;
    // Every currently-loaded source file path (for cross-session persistence).
    std::vector<std::string> getFilenames() const;

    LLLocalMesh* getUnit(const LLUUID& tracking_id) const;

    // True if a mesh-owned import (tracking id) is still referenced by some loaded
    // mesh other than `exclude` -- so a reload that drops it doesn't delUnit a unit a
    // sibling mesh (sharing the same texture/material file) still needs.
    bool isImportOwnedByOther(const LLUUID& tracking_id, const LLLocalMesh* exclude) const;

    // Per-unit toggle: include the mesh's joint-position overrides (alt-inverse-bind
    // + pelvis offset) when building its skin, for fitted bodies that need them.
    // Changing it re-parses the unit (and re-spawns it in place if it is in-world).
    void setIncludeJointPositions(const LLUUID& tracking_id, bool include);
    bool getIncludeJointPositions(const LLUUID& tracking_id) const;

    // Mesh repository injection: resolve a part's world id to its decoded data.
    bool                  isLocal(const LLUUID& world_id) const;
    LLVolume*             getVolumeForWorldID(const LLUUID& world_id) const;
    const LLMeshSkinInfo* getSkinInfoForWorldID(const LLUUID& world_id) const;

    // Despawn in-world preview copies but KEEP the loaded unit so it can be re-spawned
    // (a later file save won't auto-respawn a despawned unit -- see onLoadResult).
    // despawnPreviewObject derezzes just the copy that owns `obj`, letting the standard
    // in-world Delete key/menu "derez" a single client-only copy (which the sim delete
    // path can't touch) without dropping the file from the list. despawn() derezzes
    // ALL of a unit's copies ("Derez All"). To remove the file from the list entirely,
    // use delUnit().
    void despawnPreviewObject(LLViewerObject* obj);
    void despawn(const LLUUID& tracking_id);
    void despawnAll(); // derez every copy of every unit (Spawned tab "Derez All")

    // Attach/detach a preview linkset to the agent avatar, driven by the normal
    // "Attach"/"Detach" object menus (the viewer's sim attach/detach can't act on
    // a client-only object). Each acts on the preview linkset that owns `obj` (root
    // or child). attach_point is the avatar attachment-point id the user picked
    // (the key into LLVOAvatar::mAttachmentPoints; render order is sorted by it);
    // 0 means the default point (chest). isRiggedPreview is true when `obj` is a
    // local preview whose unit is rigged; isPreviewAttached reflects whether that
    // linkset is currently worn.
    void attachPreviewToAvatar(LLViewerObject* obj, S32 attach_point = 0, bool replace = false);
    void detachPreviewFromAvatar(LLViewerObject* obj);
    bool isRiggedPreview(const LLViewerObject* obj) const;
    bool isPreviewAttached(const LLViewerObject* obj) const;

    // Rez a NEW client-only copy ("instance") of the unit's parts in front of the
    // agent. A unit can be rezzed any number of times; each copy is independent with
    // its own instance id. Returns the new copy's linkset root.
    LLViewerObject* spawnInWorld(const LLUUID& tracking_id);
    // Convenience: load each file and spawn it in-world once it finishes loading.
    void addAndSpawn(const std::vector<std::string>& filenames);
    // Load a file (if needed) and, once decoded, spawn it and wear it at the given
    // attachment point -- so Attach works on a not-yet-decoded unit.
    void addAndAttach(const std::string& filename, S32 attach_point);

    // The root of (any) one rezzed copy of a unit, or null if it has none -- handy for
    // "is this unit in-world?" checks. Use getSpawnedInstances() to act per copy.
    LLViewerObject* getSpawnedRoot(const LLUUID& tracking_id) const;
    // For the build floater: resolve the preview that owns `obj` (root or child
    // prim) to a display name -- the selected sub-mesh's own name when it has one,
    // else the file's short name -- plus the source file path. False if not a preview.
    bool getPreviewDisplay(const LLViewerObject* obj, std::string& name_out, std::string& path_out) const;
    // Number of live rezzed copies of a unit.
    S32 getSpawnedCount(const LLUUID& tracking_id) const;

    // A single rezzed copy of a unit; mRoot is its linkset root.
    struct SpawnedInstance
    {
        LLUUID          mInstanceID;
        LLUUID          mTrackingID;
        LLViewerObject* mRoot;
    };
    // Every live rezzed copy across all units, in rez order (for the Spawned tab).
    std::vector<SpawnedInstance> getSpawnedInstances() const;
    // The linkset root of a specific rezzed copy, or null.
    LLViewerObject* getInstanceRoot(const LLUUID& instance_id) const;
    // Derez a single rezzed copy, leaving the loaded unit and the unit's other copies.
    void despawnInstance(const LLUUID& instance_id);
    // Localized in-world status of a copy's root ("Rezzed" / "Attached: <point>"),
    // shared by the mesh list and the Spawned tab. Empty for a null root.
    std::string statusText(LLViewerObject* root) const;

    void feedScrollList(LLScrollListCtrl* ctrl);

    // Fired when the unit list or any unit's in-world (rezzed) state changes, so the
    // Local Assets floater refreshes reactively no matter who made the change
    // (in-world Delete/derez, login reload, the floater itself, ...).
    boost::signals2::connection setUnitsChangedCallback(const std::function<void()>& cb);

    // Called by the load callback when an async (re)parse finishes (main thread).
    void onLoadResult(const LLUUID& tracking_id, LLModelLoader::scene& scene, U32 load_state);

    // Timer tick: poll every loaded unit's source file for changes.
    void doUpdates();

    // A dedicated, never-rendered UI avatar that the model loaders resolve joints
    // against (and dump joint-position overrides onto) so loading a rigged mesh
    // does not deform the real agent avatar. Created lazily on first load.
    // Left at its default REST pose by default: the FBX loader rebuilds missing inverse
    // binds as inverse(joint world matrix), so the skeleton must sit at the canonical bind
    // pose. Pass run_stand_anim=true only if a caller actually renders this avatar.
    LLVOAvatar* getPreviewAvatar(bool run_stand_anim = false);

    // Destroy all client-only preview objects and the preview avatar. Wired into
    // LLViewerObjectList::killAllObjects() so they are released while the object
    // list is still valid, ahead of singleton teardown. Idempotent.
    void cleanup();

    // Release the preview objects (and preview avatar) hosted by a region that is
    // being torn down. Wired into LLViewerObjectList::killObjects(region) so they
    // don't dangle past their host region. The loaded units stay; a later spawn
    // re-creates them in the current region.
    void despawnObjectsInRegion(LLViewerRegion* regionp);

private:
    LLUUID addUnitInternal(const std::string& filename, bool include_joints = false);
    void   despawnUnit(const LLUUID& tracking_id);

    // Reload every spawned copy of a unit in place by pointing each existing prim at
    // the freshly decoded geometry (new sculpt id) instead of despawning -- so
    // attachment, selection and transform are preserved and there's no flicker.
    // Returns false if the prim count changed (caller falls back to a re-spawn).
    bool hotSwapInWorld(const LLUUID& tracking_id);
    // Re-rez every existing copy of a unit at its current transform/attachment, used
    // when a reload changes the prim count so hot-swap can't swap 1:1.
    void respawnInstancesInPlace(const LLUUID& tracking_id);
    // Build one copy's linkset for (tracking_id, instance_id) rooted at `base` with
    // `root_rot`, optionally worn at attach_point. Returns the root.
    LLViewerObject* spawnLinkset(const LLUUID& tracking_id, const LLUUID& instance_id,
                                 const LLVector3& base, const LLQuaternion& root_rot,
                                 bool attach, S32 attach_point);
    // The instance id of the copy that owns `obj` (root or child), or null.
    LLUUID instanceForObject(const LLViewerObject* obj) const;

    // Find a decoded part by its world id (across all loaded units).
    const LLLocalMeshPart* findPart(const LLUUID& world_id) const;
    // The spawned linkset root for the unit that owns `obj` (root or child), or null.
    LLViewerObject* findRootForObject(const LLViewerObject* obj) const;
    // Detach a spawned root from the agent avatar if it is currently worn (used by
    // despawn/cleanup so an attached preview doesn't dangle on the avatar).
    void detachRootIfAttached(LLViewerObject* root);
    // Point a freshly created object at a part's geometry/skin (sculpt id +
    // setVolume + default textures). Does not set the object's transform.
    void applyPartGeometry(LLVOVolume* vol, const LLLocalMeshPart& part);

    typedef std::list<LLLocalMesh*>::iterator       local_list_iter;
    typedef std::list<LLLocalMesh*>::const_iterator local_list_citer;
    std::list<LLLocalMesh*> mMeshList;

    // A rezzed copy ("instance") of a unit: its whole linkset, with mPrims[0] the
    // root. Keyed by instance id in mSpawnedCopies, so per-copy lookup/erase is O(1)
    // and a copy's prims are grouped together -- per-unit/per-copy work then scales
    // with the copy count, not the total prim count across every copy.
    struct SpawnedCopy
    {
        LLUUID                                  mTrackingID; // the unit this copy came from
        U32                                     mSeq = 0;    // rez order, for stable listing
        std::vector<LLPointer<LLViewerObject> > mPrims;      // mPrims[0] is the linkset root
    };
    std::unordered_map<LLUUID, SpawnedCopy> mSpawnedCopies; // instance id -> copy
    U32 mNextSpawnSeq = 0; // monotonic rez counter feeding SpawnedCopy::mSeq

    boost::signals2::signal<void()> mUnitsChangedSignal; // add/remove/spawn/despawn

    LLLocalMeshTimer       mTimer;         // drives live-reload polling
    LLPointer<LLVOAvatar>  mPreviewAvatar; // skeleton for joint resolution (never the agent)
};

#endif // LL_LLLOCALMESH_H
