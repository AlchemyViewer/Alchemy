/**
 * @file lldir_linux.h
 * @brief Definition of directory utilities class for linux
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#if !LL_LINUX
#error This header must not be included when compiling for any target other than Linux. Consider including lldir.h instead.
#endif // !LL_LINUX

#ifndef LL_LLDIR_LINUX_H
#define LL_LLDIR_LINUX_H

#include "lldir.h"

#include <dirent.h>
#include <errno.h>

// Where one profile's files go on Linux: the XDG Base Directory layout, or
// the single dot directory older viewers kept everything in. Free of LLDir
// state so the tests can drive it against a scratch home.
namespace LLDirXDG
{
    struct Layout
    {
        std::string data;   // mOSUserAppDir: per-account folders, user skins, default chat logs
        std::string config; // LL_PATH_USER_SETTINGS
        std::string cache;  // the default LL_PATH_CACHE
        std::string state;  // LL_PATH_LOGS: logs, markers, the crash database
    };

    // $var when it holds an absolute path (the spec says to ignore any
    // other), else home/fallback.
    std::string baseDir(const char* var, const std::string& home, const std::string& fallback);

    // dir_name under each XDG base.
    Layout layout(const std::string& home, const std::string& dir_name);

    // Everything under root, as before XDG and as $<APP>_USER_DIR still has it.
    Layout legacyLayout(const std::string& root);

    // Creates path and any missing parents, as 0700 like the spec asks.
    bool makeDirs(const std::string& path);

    enum class Migration
    {
        NONE,   // nothing to move: to is the profile
        MOVED,  // legacy_root moved into to
        FAILED  // legacy_root is still the profile, for this run at least
    };

    // Moves the profile at legacy_root into to, once: when legacy_root is a
    // real directory, no viewer is running from it and to.data does not exist
    // yet. A move that fails part way is put back. Anything worth logging is
    // added to notes.
    Migration migrate(const std::string& legacy_root, const Layout& to, std::vector<std::string>& notes);
}

class LLDir_Linux : public LLDir
{
public:
    LLDir_Linux();
    virtual ~LLDir_Linux();

    /*virtual*/ void initAppDirs(const std::string &app_name,
        const std::string& app_read_only_data_dir);

    virtual std::string getCurPath();
    virtual U32 countFilesInDir(const std::string &dirname, const std::string &mask);

    /*virtual*/ std::string getLLPluginFilename(std::string base_name);

private:
    DIR *mDirp;
    int mCurrentDirIndex;
    int mCurrentDirCount;
    std::string mCurrentDir;
};

#endif // LL_LLDIR_LINUX_H


