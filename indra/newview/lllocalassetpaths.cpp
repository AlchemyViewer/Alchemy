/**
 * @file lllocalassetpaths.cpp
 * @brief Per-account persistence of locally-loaded asset file paths
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

#include "lllocalassetpaths.h"

#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"

#include "lllocalanim.h"
#include "lllocalbitmaps.h"
#include "lllocalgltfmaterials.h"
#include "lllocalmesh.h"

#include <boost/bind/bind.hpp>
#include <set>

namespace
{
    const std::string LOCAL_ASSETS_FILE("local_assets.xml");
}

// static
const char* LLLocalAssetPaths::key(EType type)
{
    switch (type)
    {
        case TYPE_ANIM:     return "anims";
        case TYPE_TEXTURE:  return "textures";
        case TYPE_MATERIAL: return "materials";
        case TYPE_MESH:
        default:            return "meshes";
    }
}

std::vector<std::string> LLLocalAssetPaths::currentFiles(EType type) const
{
    switch (type)
    {
        case TYPE_ANIM:     return LLLocalAnimMgr::getInstance()->getFilenames();
        case TYPE_TEXTURE:  return LLLocalBitmapMgr::getInstance()->getFilenames();
        case TYPE_MATERIAL: return LLLocalGLTFMaterialMgr::getInstance()->getFilenames();
        case TYPE_MESH:
        default:            return LLLocalMeshMgr::getInstance()->getFilenames();
    }
}

std::string LLLocalAssetPaths::getFilePath() const
{
    // Empty until the per-account dir is set (i.e. after login).
    return gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, LOCAL_ASSETS_FILE);
}

LLSD LLLocalAssetPaths::getPaths(EType type) const
{
    const char* k = key(type);
    return mPaths.has(k) ? mPaths[k] : LLSD::emptyArray();
}

bool LLLocalAssetPaths::getMeshJoints(const std::string& path) const
{
    return mPaths.has("mesh_joints") &&
           mPaths["mesh_joints"].has(path) &&
           mPaths["mesh_joints"][path].asBoolean();
}

void LLLocalAssetPaths::writeToDisk() const
{
    const std::string path = getFilePath();
    if (path.empty())
    {
        return;
    }
    llofstream out(path.c_str());
    if (out.is_open())
    {
        LLSDSerialize::toXML(mPaths, out);
        out.close();
    }
    else
    {
        LL_WARNS("LocalAssets") << "Can't write local asset list: " << path << LL_ENDL;
    }
}

void LLLocalAssetPaths::removePath(EType type, const std::string& path)
{
    const char* k = key(type);
    bool changed = false;
    if (mPaths.has(k) && mPaths[k].isArray())
    {
        LLSD kept = LLSD::emptyArray();
        for (LLSD::array_const_iterator it = mPaths[k].beginArray(); it != mPaths[k].endArray(); ++it)
        {
            if (it->asString() == path) { changed = true; } else { kept.append(*it); }
        }
        if (changed) { mPaths[k] = kept; }
    }
    if (type == TYPE_MESH && mPaths.has("mesh_joints") && mPaths["mesh_joints"].has(path))
    {
        mPaths["mesh_joints"].erase(path);
        changed = true;
    }
    if (changed)
    {
        writeToDisk();
    }
}

void LLLocalAssetPaths::onUnitsChanged()
{
    // Remember any files that have become loaded since we last saved (lazy loads,
    // fresh adds, the texture picker, ...). We only ADD here; user removals go
    // through removePath().
    bool changed = false;
    for (S32 t = TYPE_MESH; t <= TYPE_MATERIAL; ++t)
    {
        const char* k = key((EType)t);
        if (!mPaths.has(k) || !mPaths[k].isArray())
        {
            mPaths[k] = LLSD::emptyArray();
        }
        std::set<std::string> have;
        for (LLSD::array_const_iterator it = mPaths[k].beginArray(); it != mPaths[k].endArray(); ++it)
        {
            have.insert(it->asString());
        }
        for (const std::string& file : currentFiles((EType)t))
        {
            if (have.insert(file).second)
            {
                mPaths[k].append(file);
                changed = true;
            }
        }
    }

    // Mesh-only: also remember each loaded mesh's joint-position-override flag (it can
    // change without the file list changing). Untouched for unloaded meshes.
    {
        LLLocalMeshMgr* mgr = LLLocalMeshMgr::getInstance();
        for (const std::string& file : mgr->getFilenames())
        {
            const bool joints = mgr->getIncludeJointPositions(mgr->getUnitID(file));
            const bool had = mPaths["mesh_joints"].has(file) && mPaths["mesh_joints"][file].asBoolean();
            if (joints != had)
            {
                if (joints) { mPaths["mesh_joints"][file] = true; }
                else        { mPaths["mesh_joints"].erase(file); }
                changed = true;
            }
        }
    }

    if (changed)
    {
        writeToDisk();
    }
}

void LLLocalAssetPaths::loadAndWatch()
{
    // Re-read the CURRENT account's saved paths on every call. Login cleanup is
    // re-entered on relog / account switch, so this refreshes mPaths for the new
    // account's local_assets.xml instead of leaving the previous account's set in
    // memory (which onUnitsChanged() would then write into the new account's file).
    reloadForAccount();

    if (mWatching)
    {
        return; // register the manager watchers only once per session
    }
    mWatching = true;

    // Watch for files that become loaded from here on, so the saved set stays current.
    mConnections.emplace_back(LLLocalMeshMgr::getInstance()->setUnitsChangedCallback(
        boost::bind(&LLLocalAssetPaths::onUnitsChanged, this)));
    mConnections.emplace_back(LLLocalAnimMgr::getInstance()->setUnitsChangedCallback(
        boost::bind(&LLLocalAssetPaths::onUnitsChanged, this)));
    mConnections.emplace_back(LLLocalBitmapMgr::getInstance()->setUnitsChangedCallback(
        boost::bind(&LLLocalAssetPaths::onUnitsChanged, this)));
    mConnections.emplace_back(LLLocalGLTFMaterialMgr::getInstance()->setUnitsChangedCallback(
        boost::bind(&LLLocalAssetPaths::onUnitsChanged, this)));
}

void LLLocalAssetPaths::reloadForAccount()
{
    // Discard any in-memory set from a previous account before reading.
    mPaths = LLSD();

    // Read the saved paths (no decoding).
    const std::string path = getFilePath();
    if (!path.empty() && gDirUtilp->fileExists(path))
    {
        llifstream in(path.c_str());
        if (in.is_open())
        {
            LLSDSerialize::fromXML(mPaths, in);
            in.close();
        }
    }

    // Normalize to four arrays and drop entries whose file no longer exists.
    bool pruned = false;
    for (S32 t = TYPE_MESH; t <= TYPE_MATERIAL; ++t)
    {
        const char* k = key((EType)t);
        LLSD kept = LLSD::emptyArray();
        if (mPaths.has(k) && mPaths[k].isArray())
        {
            for (LLSD::array_const_iterator it = mPaths[k].beginArray(); it != mPaths[k].endArray(); ++it)
            {
                const std::string file = it->asString();
                if (!file.empty() && gDirUtilp->fileExists(file)) { kept.append(file); }
                else { pruned = true; }
            }
        }
        mPaths[k] = kept;
    }

    // Drop joint flags for meshes no longer in the saved list.
    if (mPaths.has("mesh_joints") && mPaths["mesh_joints"].isMap())
    {
        std::set<std::string> mesh_set;
        for (LLSD::array_const_iterator it = mPaths["meshes"].beginArray(); it != mPaths["meshes"].endArray(); ++it)
        {
            mesh_set.insert(it->asString());
        }
        LLSD kept_joints = LLSD::emptyMap();
        for (LLSD::map_const_iterator it = mPaths["mesh_joints"].beginMap(); it != mPaths["mesh_joints"].endMap(); ++it)
        {
            if (mesh_set.count(it->first)) { kept_joints[it->first] = it->second; }
            else { pruned = true; }
        }
        mPaths["mesh_joints"] = kept_joints;
    }

    if (pruned)
    {
        writeToDisk();
    }
}
