/**
 * @file lllocalrendermaterials.h
 * @brief Local GLTF materials header
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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

#ifndef LL_LOCALGLTFMATERIALS_H
#define LL_LOCALGLTFMATERIALS_H

#include "lleventtimer.h"
#include "llpointer.h"
#include "llgltfmateriallist.h"
#include <boost/signals2/signal.hpp>
#include <filesystem>
#include <map>

class LLScrollListCtrl;
class LLGLTFMaterial;
class LLViewerObject;
class LLTextureEntry;

class LLLocalGLTFMaterial : public LLFetchedGLTFMaterial
{
public: /* main */
    LLLocalGLTFMaterial(std::string filename, S32 index);
    virtual ~LLLocalGLTFMaterial();

public: /* accessors */
    std::string getFilename() const;
    std::string getShortName() const;
    LLUUID      getTrackingID() const;
    LLUUID      getWorldID() const;
    S32         getIndexInFile() const;
    std::string getMaterialName() const { return mMaterialName; } // glTF material name (face binding)
    // Other glTF material names (face bindings) that deduplicated onto this unit:
    // a mesh import collapses content-identical materials to one, but every
    // original face still binds by its own name, so we keep them for the name->id
    // map (see LLLocalGLTFMaterialMgr::getWorldIDsByName).
    const std::vector<std::string>& getAliasNames() const { return mAliasNames; }
    void        addAliasName(const std::string& name) { mAliasNames.push_back(name); }
    // Imported by a local mesh for its own faces: hidden from the Materials tab +
    // cross-session persistence (it reappears when the mesh re-decodes).
    bool        isMeshOwned() const { return mIsMeshOwned; }
    void        setMeshOwned(bool b) { mIsMeshOwned = b; }

public:
    bool updateSelf();

private:
    bool loadMaterial();

private: /* private enums */
    enum ELinkStatus
    {
        LS_ON,
        LS_BROKEN,
    };

    enum EExtension
    {
        ET_MATERIAL_GLTF,
        ET_MATERIAL_GLB,
    };

private: /* members */
    std::string mFilename;
    std::string mShortName;
    LLUUID      mTrackingID;
    LLUUID      mWorldID;
    std::filesystem::file_time_type mLastModified;
    EExtension  mExtension;
    ELinkStatus mLinkStatus;
    S32         mUpdateRetries;
    S32         mMaterialIndex; // Single file can have more than one
    std::string mMaterialName;  // glTF material name, for matching a mesh face's binding
    std::vector<std::string> mAliasNames; // other face bindings deduplicated onto this unit
    bool        mIsMeshOwned = false; // imported by a local mesh (see isMeshOwned)
};

class LLLocalGLTFMaterialTimer : public LLEventTimer
{
public:
    LLLocalGLTFMaterialTimer();
    ~LLLocalGLTFMaterialTimer();

public:
    void startTimer();
    void stopTimer();
    bool isRunning();
    bool tick();
};

class LLLocalGLTFMaterialMgr : public LLSingleton<LLLocalGLTFMaterialMgr>
{
    LLSINGLETON(LLLocalGLTFMaterialMgr);
    ~LLLocalGLTFMaterialMgr();
public:
    S32          addUnit(const std::vector<std::string>& filenames);
    S32          addUnit(const std::string& filename); // file can hold multiple materials
    S32          addUnit(const std::string& filename, LLUUID& outID); // returns first material id as outID
    // Add a file's materials as mesh-owned: hidden from the Materials tab + persistence.
    S32          addUnit(const std::string& filename, bool mesh_owned);
protected:
    S32          addUnitInternal(const std::string& filename, LLUUID& outID, bool mesh_owned = false); // file can hold multiple materials
public:
    void         delUnit(LLUUID tracking_id);
    // The lookups below resolve within ONE ownership class: a user-loaded set and a
    // mesh-owned import of the same file are distinct units (see addUnitInternal),
    // and an ownership-blind match could hand the Materials tab a mesh's hidden
    // imports to delete (or bind a mesh's faces to the user's deletable copies).
    LLUUID       getUnitID(const std::string& filename, S32 index, bool mesh_owned);
    // Tracking ids of every unit of the class loaded from a file, in list order.
    // Unlike walking getUnitID(file, 0..N), this is safe across the index gaps
    // that import-time deduplication leaves in getIndexInFile().
    void         getTrackingIDs(const std::string& filename, std::vector<LLUUID>& out, bool mesh_owned);

    LLUUID       getWorldID(LLUUID tracking_id);
    bool         isMeshOwned(const LLUUID& tracking_id) const; // imported by a local mesh
    bool         isLocal(LLUUID world_id);
    void         getFilenameAndIndex(LLUUID tracking_id, std::string &filename, S32 &index);
    // Map each of a file's materials by name (empty name -> "mat<index>", matching the
    // model loader's face bindings) to its world id, for texturing a local mesh.
    void         getWorldIDsByName(const std::string& filename, std::map<std::string, LLUUID>& out, bool mesh_owned);
    std::vector<std::string> getFilenames() const; // distinct loaded files (persistence)

    void         feedScrollList(LLScrollListCtrl* ctrl);
    // Fired when the unit list changes (add/remove) so the Local Assets floater
    // refreshes reactively.
    boost::signals2::connection setUnitsChangedCallback(const std::function<void()>& cb);
    void         doUpdates();

private:
    std::list<LLPointer<LLLocalGLTFMaterial> >    mMaterialList;
    LLLocalGLTFMaterialTimer           mTimer;
    boost::signals2::signal<void()>    mUnitsChangedSignal; // add/remove
    typedef std::list<LLPointer<LLLocalGLTFMaterial> >::iterator local_list_iter;
};

#endif // LL_LOCALGLTFMATERIALS_H

