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
#include "llpointer.h"
#include "llsingleton.h"
#include "lluuid.h"

#include <filesystem>
#include <list>
#include <string>
#include <utility>
#include <vector>

class LLScrollListCtrl;
class LLViewerObject;
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

    // Stats (for UI + logging).
    S32 getNumFaces() const     { return mNumFaces; }
    S32 getNumVertices() const  { return mNumVertices; }
    S32 getNumTriangles() const { return mNumTriangles; }
    S32 getNumJoints() const    { return mNumJoints; }

    // Spawn the preview in-world automatically once loading completes.
    void setSpawnWhenReady(bool b) { mSpawnWhenReady = b; }
    bool wantsSpawn() const         { return mSpawnWhenReady; }

    // Main-thread completion hooks driven by the load callback.
    void onLoadComplete(LLModelLoader::scene& scene);
    void markFailed() { mState = ST_FAILED; }

private:
    enum EFormat { FMT_NONE, FMT_DAE, FMT_GLTF };
    enum EState  { ST_LOADING, ST_LOADED, ST_FAILED };

    void startLoad();
    void assembleFromScene(LLModelLoader::scene& scene);

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

    S32  mNumFaces;
    S32  mNumVertices;
    S32  mNumTriangles;
    S32  mNumJoints;
    bool mTruncated; // geometry exceeded MAX_MODEL_FACES and was clipped
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

private:
    LLUUID addUnitInternal(const std::string& filename);
    void   despawnForWorldID(const LLUUID& world_id);

    typedef std::list<LLLocalMesh*>::iterator       local_list_iter;
    typedef std::list<LLLocalMesh*>::const_iterator local_list_citer;
    std::list<LLLocalMesh*> mMeshList;

    // Client-only objects we've rezzed, paired with the world ID they show.
    std::vector<std::pair<LLUUID, LLPointer<LLViewerObject> > > mSpawnedObjects;
};

#endif // LL_LLLOCALMESH_H
