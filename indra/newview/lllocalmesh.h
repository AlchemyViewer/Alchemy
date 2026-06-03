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

#include <filesystem>
#include <list>
#include <string>
#include <utility>
#include <vector>

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
struct LLLocalMeshPart
{
    LLUUID                    mWorldID;       // unique mesh id objects/repository reference
    LLPointer<LLVolume>       mVolume;        // normalized geometry (<= 8 faces)
    LLPointer<LLMeshSkinInfo> mSkinInfo;      // null if not rigged
    LLVector3                 mScale;         // authored size -> prim scale
    LLVector3                 mOffset;        // part centre relative to the whole-model centre
    S32                       mNumFaces = 0;
};

// A single local mesh file and its decoded, in-memory representation (one or more
// parts spawned together as a linkset).
class LLLocalMesh
{
public:
    LLLocalMesh(std::string filename);
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

    // Stats (for UI + logging).
    S32 getNumParts() const     { return (S32)mParts.size(); }
    S32 getNumFaces() const     { return mNumFaces; }
    S32 getNumVertices() const  { return mNumVertices; }
    S32 getNumTriangles() const { return mNumTriangles; }
    S32 getNumJoints() const    { return mNumJoints; }

    // Spawn the preview in-world automatically once loading completes.
    void setSpawnWhenReady(bool b) { mSpawnWhenReady = b; }
    bool wantsSpawn() const         { return mSpawnWhenReady; }

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
    void markFailed() { mState = ST_FAILED; }

    // Live reload (M3): poll the source file's mtime and, on a change, kick an
    // async re-parse. The geometry swap happens back in the load callback.
    bool pollForReload();       // true if a reload was started this poll
    void finishReload(bool ok); // clear in-flight state after the parse returns
    bool isReloading() const { return mReloading; }

    std::string mFilename;
    std::string mShortName;
    LLUUID      mTrackingID; // stable, identifies this unit in UI
    EFormat     mFormat;
    EState      mState;
    bool        mSpawnWhenReady;

    std::filesystem::file_time_type mLastModified; // for live reload (M3)

    std::vector<LLLocalMeshPart> mParts; // one per LLModel; spawned together as a linkset

    S32  mNumFaces;     // totals across all parts
    S32  mNumVertices;
    S32  mNumTriangles;
    S32  mNumJoints;

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
    LLUUID addUnit(const std::string& filename);
    bool   addUnit(const std::vector<std::string>& filenames);
    void   delUnit(LLUUID tracking_id);

    LLUUID      getUnitID(const std::string& filename) const;
    std::string getFilename(const LLUUID& tracking_id) const;

    LLLocalMesh* getUnit(const LLUUID& tracking_id) const;

    // Mesh repository injection: resolve a part's world id to its decoded data.
    bool                  isLocal(const LLUUID& world_id) const;
    LLVolume*             getVolumeForWorldID(const LLUUID& world_id) const;
    const LLMeshSkinInfo* getSkinInfoForWorldID(const LLUUID& world_id) const;

    // Delete the preview linkset that owns this object (and the loaded unit, so a
    // later file save does not respawn it). Lets the standard Delete key/menu work
    // on client-only previews, which the sim delete path can't touch.
    void deletePreviewObject(LLViewerObject* obj);

    // Attach/detach a rigged preview to the agent avatar (menu-driven, M4). Each
    // acts on the preview linkset that owns `obj` (root or child). isRiggedPreview
    // is true when `obj` is a local preview whose unit is rigged; isPreviewAttached
    // reflects whether that linkset is currently worn.
    void attachPreviewToAvatar(LLViewerObject* obj);
    void detachPreviewFromAvatar(LLViewerObject* obj);
    bool isRiggedPreview(const LLViewerObject* obj) const;
    bool isPreviewAttached(const LLViewerObject* obj) const;

    // Create the client-only linkset in-world referencing the unit's parts. If a
    // linkset for this unit already exists (live reload), it is replaced in place
    // and the root's transform preserved.
    LLViewerObject* spawnInWorld(const LLUUID& tracking_id);
    // Convenience: load each file and spawn it in-world once it finishes loading.
    void addAndSpawn(const std::vector<std::string>& filenames);

    void feedScrollList(LLScrollListCtrl* ctrl);

    // Called by the load callback when an async (re)parse finishes (main thread).
    void onLoadResult(const LLUUID& tracking_id, LLModelLoader::scene& scene, U32 load_state);

    // Timer tick: poll every loaded unit's source file for changes.
    void doUpdates();

    // A dedicated, never-rendered UI avatar that the model loaders resolve joints
    // against (and dump joint-position overrides onto) so loading a rigged mesh
    // does not deform the real agent avatar. Created lazily on first load.
    LLVOAvatar* getPreviewAvatar();

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
    LLUUID addUnitInternal(const std::string& filename);
    void   despawnUnit(const LLUUID& tracking_id);

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

    // Client-only objects we've rezzed, keyed by the tracking id of the unit they
    // belong to. A unit's linkset shares one tracking id; the first entry pushed
    // for a tracking id is the linkset root.
    std::vector<std::pair<LLUUID, LLPointer<LLViewerObject> > > mSpawnedObjects;

    LLLocalMeshTimer       mTimer;         // drives live-reload polling
    LLPointer<LLVOAvatar>  mPreviewAvatar; // skeleton for joint resolution (never the agent)
};

#endif // LL_LLLOCALMESH_H
