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
class LLVOAvatar;
class LLVOVolume;
class LLVolume;

// A single local mesh file and its decoded, in-memory representation.
class LLLocalMesh
{
public:
    LLLocalMesh(std::string filename);
    ~LLLocalMesh();

    std::string getFilename() const   { return mFilename; }
    std::string getShortName() const  { return mShortName; }
    LLUUID      getTrackingID() const { return mTrackingID; }
    LLUUID      getWorldID() const     { return mWorldID; }

    bool getValid() const   { return mState == ST_LOADED; }
    bool isLoading() const  { return mState == ST_LOADING; }
    bool isFailed() const   { return mState == ST_FAILED; }

    // Decoded geometry/skin -- consumed by the mesh repository injection.
    LLVolume*             getVolume() const   { return mVolume; }
    const LLMeshSkinInfo* getSkinInfo() const { return mSkinInfo; }
    bool                  isRigged() const    { return mSkinInfo.notNull(); }

    // Authored bounding-box size; the spawned static preview uses this as its
    // prim scale. (1,1,1) when rigged (the rig, not the object scale, governs).
    LLVector3             getScale() const    { return mScale; }

    // Stats (for UI + logging).
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

    // Assemble decoded geometry and commit it to this unit; returns false (and
    // leaves any previously loaded geometry untouched) if the scene yielded
    // nothing, so a failed live-reload keeps showing the last good mesh.
    bool ingestScene(LLModelLoader::scene& scene);
    void markFailed() { mState = ST_FAILED; }

    // Live reload (M3): poll the source file's mtime and, on a change, kick an
    // async re-parse. The geometry swap happens back in the load callback.
    bool pollForReload();       // true if a reload was started this poll
    void finishReload(bool ok); // clear in-flight state after the parse returns
    void regenerateWorldID();   // mint a fresh world id (keeps skin id in sync)
    bool isReloading() const { return mReloading; }

    std::string mFilename;
    std::string mShortName;
    LLUUID      mTrackingID; // stable, identifies this unit in UI
    LLUUID      mWorldID;    // stable mesh UUID objects reference (kept across reloads)
    EFormat     mFormat;
    EState      mState;
    bool        mSpawnWhenReady;

    std::filesystem::file_time_type mLastModified; // for live reload (M3)

    LLPointer<LLVolume>       mVolume;   // assembled high-LOD geometry, served for all LODs
    LLPointer<LLMeshSkinInfo> mSkinInfo; // null if not rigged
    LLVector3                 mScale;    // authored size; prim scale for the static preview ((1,1,1) when rigged)

    S32  mNumFaces;
    S32  mNumVertices;
    S32  mNumTriangles;
    S32  mNumJoints;
    bool mTruncated; // geometry exceeded MAX_MODEL_FACES and was clipped

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
    LLUUID      getTrackingID(const LLUUID& world_id) const;
    LLUUID      getWorldID(const LLUUID& tracking_id) const;
    bool        isLocal(const LLUUID& world_id) const;
    std::string getFilename(const LLUUID& tracking_id) const;

    LLLocalMesh* getUnit(const LLUUID& tracking_id) const;
    LLLocalMesh* getUnitByWorldID(const LLUUID& world_id) const;

    // True if the object is one of our client-only in-world preview spawns.
    // Used by LLSelectMgr to suppress all server traffic for these objects.
    bool isLocalPreview(const LLViewerObject* obj) const;

    // Create a client-only LLVOVolume in-world referencing the unit's mesh.
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

private:
    LLUUID addUnitInternal(const std::string& filename);
    void   despawnForWorldID(const LLUUID& world_id);

    // After a successful reload, re-point our spawned objects at the unit's new
    // world id so the repository serves the freshly decoded geometry.
    void repointSpawnedObjects(const LLUUID& old_world_id, LLLocalMesh* unit);
    // Apply a unit's mesh geometry/skin to an existing spawned object (shared by
    // the spawn and reload paths). Does not touch the object's transform.
    void applyUnitGeometry(LLVOVolume* vol, const LLLocalMesh* unit);

    typedef std::list<LLLocalMesh*>::iterator       local_list_iter;
    typedef std::list<LLLocalMesh*>::const_iterator local_list_citer;
    std::list<LLLocalMesh*> mMeshList;

    // Client-only objects we've rezzed, paired with the world ID they show.
    std::vector<std::pair<LLUUID, LLPointer<LLViewerObject> > > mSpawnedObjects;

    LLLocalMeshTimer       mTimer;         // drives live-reload polling
    LLPointer<LLVOAvatar>  mPreviewAvatar; // skeleton for joint resolution (never the agent)
};

#endif // LL_LLLOCALMESH_H
