/**
 * @file lllocalassetpaths.h
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

// Remembers the set of locally-loaded asset file paths (mesh / animation / texture /
// GLTF material) per account, so an artist's working set reappears after a relog.
// Only the file PATHS are saved.
//
// Crucially, on login the files are NOT decoded -- the saved paths are just listed in
// the Local Assets floater (dimmed) and each is decoded lazily when the artist first
// uses it. This keeps login fast and memory low. The path set tracks newly-loaded
// files via the managers' units-changed signals, and the floater removes paths the
// user deletes. Stored at LL_PATH_PER_SL_ACCOUNT/local_assets.xml.

#ifndef LL_LLLOCALASSETPATHS_H
#define LL_LLLOCALASSETPATHS_H

#include "llsd.h"
#include "llsingleton.h"

#include <boost/signals2/connection.hpp>
#include <string>
#include <vector>

class LLLocalAssetPaths : public LLSingleton<LLLocalAssetPaths>
{
    LLSINGLETON_EMPTY_CTOR(LLLocalAssetPaths);

public:
    enum EType { TYPE_MESH = 0, TYPE_ANIM, TYPE_TEXTURE, TYPE_MATERIAL };

    // Read the saved paths (WITHOUT decoding the files), prune any that have gone
    // missing, and watch the managers so newly-loaded files are remembered. Call once
    // the per-account dir is valid (login); a second call is a no-op.
    void loadAndWatch();

    // Saved file paths of a type (decoded or not) -- what the floater lists.
    LLSD getPaths(EType type) const;
    // Forget a path (the user removed it from the floater).
    void removePath(EType type, const std::string& path);
    // Persisted "include joint positions" flag for a mesh path (default false),
    // applied when the mesh is (lazily) decoded.
    bool getMeshJoints(const std::string& path) const;

private:
    void        onUnitsChanged();   // remember any newly-loaded files
    static const char* key(EType type);
    std::vector<std::string> currentFiles(EType type) const;
    std::string getFilePath() const;
    void        writeToDisk() const;

    LLSD mPaths;            // { meshes:[...], anims:[...], textures:[...], materials:[...] }
    bool mWatching = false; // loadAndWatch() runs once per session

    std::vector<boost::signals2::scoped_connection> mConnections;
};

#endif // LL_LLLOCALASSETPATHS_H
