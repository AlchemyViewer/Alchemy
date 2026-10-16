/**
 * @file lldir_linux.cpp
 * @brief Implementation of directory utilities for linux
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include "linden_common.h"

#include "lldir_linux.h"
#include "llerror.h"
#include "llrand.h"
#include "llstring.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <pwd.h>

#include <cstring>
#include <filesystem>

namespace
{
    std::string errnoText(int err)
    {
        return std::strerror(err);
    }

    // Whether a viewer holds an exec marker in logs_dir. The viewer keeps its
    // exec marker flock()ed for as long as it runs (LLAppViewer::
    // processMarkerFiles), whatever the marker's channel calls it.
    bool viewerRunningFrom(const std::string& logs_dir)
    {
        bool running = false;
        glob_t g;
        const std::string pattern = logs_dir + "/*.exec_marker";
        if (glob(pattern.c_str(), GLOB_NOSORT, NULL, &g) == 0)
        {
            for (size_t i = 0; i < g.gl_pathc && !running; ++i)
            {
                int fd = ::open(g.gl_pathv[i], O_RDONLY | O_CLOEXEC);
                if (fd == -1)
                {
                    continue;
                }
                if (::flock(fd, LOCK_EX | LOCK_NB) == 0)
                {
                    ::flock(fd, LOCK_UN);
                }
                else if (errno == EWOULDBLOCK)
                {
                    running = true;
                }
                ::close(fd);
            }
            globfree(&g);
        }
        return running;
    }
}

namespace LLDirXDG
{
    std::string baseDir(const char* var, const std::string& home, const std::string& fallback)
    {
        auto value = LLStringUtil::getoptenv(var);
        if (value && !value->empty() && value->front() == '/')
        {
            return *value;
        }
        return home + "/" + fallback;
    }

    Layout layout(const std::string& home, const std::string& dir_name)
    {
        Layout dirs;
        dirs.data = baseDir("XDG_DATA_HOME", home, ".local/share") + "/" + dir_name;
        dirs.config = baseDir("XDG_CONFIG_HOME", home, ".config") + "/" + dir_name;
        dirs.cache = baseDir("XDG_CACHE_HOME", home, ".cache") + "/" + dir_name;
        dirs.state = baseDir("XDG_STATE_HOME", home, ".local/state") + "/" + dir_name;
        return dirs;
    }

    Layout legacyLayout(const std::string& root)
    {
        Layout dirs;
        dirs.data = root;
        dirs.config = root + "/user_settings";
        dirs.cache = root + "/cache";
        dirs.state = root + "/logs";
        return dirs;
    }

    bool makeDirs(const std::string& path)
    {
        std::string partial;
        partial.reserve(path.size());
        for (size_t pos = 0; pos != std::string::npos;)
        {
            const size_t next = path.find('/', pos + 1);
            partial = path.substr(0, next);
            pos = next;
            if (partial.empty() || partial == "/")
            {
                continue;
            }
            if (::mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST)
            {
                return false;
            }
        }
        struct stat st;
        return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }

    Migration migrate(const std::string& legacy_root, const Layout& to, std::vector<std::string>& notes)
    {
        struct stat legacy, st;
        if (::lstat(legacy_root.c_str(), &legacy) != 0)
        {
            return Migration::NONE;
        }
        if (::lstat(to.data.c_str(), &st) == 0)
        {
            // An older viewer run after the move makes a new one; it is not
            // this profile, and the next move would have nowhere to go.
            notes.push_back("Ignoring " + legacy_root + ": the profile is at " + to.data);
            return Migration::NONE;
        }
        if (S_ISLNK(legacy.st_mode))
        {
            // Someone put the profile elsewhere on purpose; it stays there.
            notes.push_back("Keeping the profile at " + legacy_root + ", a symlink");
            return Migration::FAILED;
        }
        if (!S_ISDIR(legacy.st_mode))
        {
            return Migration::NONE;
        }
        if (viewerRunningFrom(legacy_root + "/logs"))
        {
            notes.push_back("A viewer is running from " + legacy_root + "; moving it to the XDG directories next time");
            return Migration::FAILED;
        }

        for (const std::string* dir : { &to.data, &to.config, &to.cache, &to.state })
        {
            const std::string parent = dir->substr(0, dir->rfind('/'));
            if (!makeDirs(parent))
            {
                notes.push_back("Couldn't create " + parent + ": " + errnoText(errno) + "; keeping the profile at " + legacy_root);
                return Migration::FAILED;
            }
        }

        if (::rename(legacy_root.c_str(), to.data.c_str()) != 0)
        {
            notes.push_back("Couldn't move " + legacy_root + " to " + to.data + ": " + errnoText(errno) + "; keeping it there");
            return Migration::FAILED;
        }

        // Settings and logs move out on their own. Neither is disposable, so
        // if either can't (another filesystem, a directory already there),
        // the whole move is put back rather than leave a profile split
        // between the two layouts.
        struct Moved
        {
            std::string from, to;
        };
        std::vector<Moved> moved;
        const Moved parts[] = {
            { to.data + "/user_settings", to.config },
            { to.data + "/logs", to.state },
        };
        for (const Moved& part : parts)
        {
            if (::lstat(part.from.c_str(), &st) != 0)
            {
                continue;
            }
            if (::rename(part.from.c_str(), part.to.c_str()) == 0)
            {
                moved.push_back(part);
                continue;
            }
            notes.push_back("Couldn't move " + part.from + " to " + part.to + ": " + errnoText(errno) + "; putting " + legacy_root + " back");
            for (auto it = moved.rbegin(); it != moved.rend(); ++it)
            {
                if (::rename(it->to.c_str(), it->from.c_str()) != 0)
                {
                    notes.push_back("Couldn't put back " + it->from + ": " + errnoText(errno));
                }
            }
            if (::rename(to.data.c_str(), legacy_root.c_str()) != 0)
            {
                notes.push_back("Couldn't put back " + legacy_root + ": " + errnoText(errno));
            }
            return Migration::FAILED;
        }

        // The cache rebuilds itself, so one that can't move (~/.cache on a
        // tmpfs, say) is dropped rather than holding the rest back.
        const std::string old_cache = to.data + "/cache";
        if (::lstat(old_cache.c_str(), &st) == 0 && ::rename(old_cache.c_str(), to.cache.c_str()) != 0)
        {
            notes.push_back("Couldn't move " + old_cache + " to " + to.cache + ": " + errnoText(errno) + "; removing it");
            std::error_code ec;
            std::filesystem::remove_all(old_cache, ec);
        }

        notes.push_back("Moved " + legacy_root + " to the XDG directories");
        return Migration::MOVED;
    }

    void adoptCache(const std::string& old_cache, const std::string& cache, std::vector<std::string>& notes)
    {
        struct stat st;
        if (old_cache == cache || ::lstat(cache.c_str(), &st) == 0 || ::lstat(old_cache.c_str(), &st) != 0 ||
            !S_ISDIR(st.st_mode))
        {
            return;
        }
        if (::rename(old_cache.c_str(), cache.c_str()) == 0)
        {
            notes.push_back("Moved the cache from " + old_cache + " to " + cache);
        }
        else
        {
            notes.push_back("Couldn't move the cache from " + old_cache + " to " + cache + ": " + errnoText(errno));
        }
    }
}


static std::string getCurrentUserHome(char* fallback)
{
    const uid_t uid = getuid();
    struct passwd *pw;

    pw = getpwuid(uid);
    if ((pw != NULL) && (pw->pw_dir != NULL))
    {
        return pw->pw_dir;
    }

    LL_INFOS() << "Couldn't detect home directory from passwd - trying $HOME" << LL_ENDL;
    auto home_env = LLStringUtil::getoptenv("HOME");
    if (home_env)
    {
        return *home_env;
    }
    else
    {
        LL_WARNS() << "Couldn't detect home directory!  Falling back to " << fallback << LL_ENDL;
        return fallback;
    }
}


LLDir_Linux::LLDir_Linux()
{
    mDirDelimiter = "/";
    mCurrentDirIndex = -1;
    mCurrentDirCount = -1;
    mDirp = NULL;

    char tmp_str[LL_MAX_PATH];  /* Flawfinder: ignore */
    if (getcwd(tmp_str, LL_MAX_PATH) == NULL)
    {
        strcpy(tmp_str, "/tmp");
        LL_WARNS() << "Could not get current directory; changing to "
                << tmp_str << LL_ENDL;
        if (chdir(tmp_str) == -1)
        {
            LL_ERRS() << "Could not change directory to " << tmp_str << LL_ENDL;
        }
    }

    const std::string start_dir = tmp_str;
    mExecutableFilename = "";
    mExecutablePathAndName = "";
    mExecutableDir = tmp_str;
    mWorkingDir = tmp_str;
    mOSUserDir = getCurrentUserHome(tmp_str);
    mOSUserAppDir = "";
    mLindenUserDir = "";

    char path [32]; /* Flawfinder: ignore */

    // *NOTE: /proc/%d/exe doesn't work on FreeBSD. But that's ok,
    // because this is the linux implementation.

    snprintf (path, sizeof(path), "/proc/%d/exe", (int) getpid ());
    int rc = readlink (path, tmp_str, sizeof (tmp_str)-1);  /* Flawfinder: ignore */
    if ( (rc != -1) && (rc <= ((int) sizeof (tmp_str)-1)) )
    {
        tmp_str[rc] = '\0'; //readlink() doesn't 0-terminate the buffer
        mExecutablePathAndName = tmp_str;
        char *path_end;
        if ((path_end = strrchr(tmp_str,'/')))
        {
            *path_end = '\0';
            mExecutableDir = tmp_str;
            mWorkingDir = tmp_str;
            mExecutableFilename = path_end+1;
        }
        else
        {
            mExecutableFilename = tmp_str;
        }
    }

    // The read-only data lives one directory up from the executable in the
    // installed tree (<root>/bin/alchemy-bin beside <root>/skins), at the
    // compile-time location of a system install, or, failing both, where
    // the viewer was started from.
    std::string install_root = mExecutableDir.substr(0, mExecutableDir.rfind('/'));
    if (!install_root.empty() && LLFile::isdir(install_root + "/skins"))
    {
        mAppRODataDir = install_root;
    }
    else
    {
#ifdef APP_RO_DATA_DIR
        mAppRODataDir = APP_RO_DATA_DIR;
#else
        mAppRODataDir = start_dir;
#endif
    }
    std::string::size_type build_dir_pos = mExecutableDir.rfind("/build-linux-");
    if (build_dir_pos != std::string::npos)
    {
        // ...we're in a dev checkout
        mSkinBaseDir = mExecutableDir.substr(0, build_dir_pos) + "/indra/newview/skins";
        LL_INFOS() << "Running in dev checkout with mSkinBaseDir "
         << mSkinBaseDir << LL_ENDL;
    }
    else
    {
        // ...normal installation running
        mSkinBaseDir = mAppRODataDir + mDirDelimiter + "skins";
    }

    mLLPluginDir = mExecutableDir + mDirDelimiter + "llplugin";

    // *TODO: don't use /tmp, use $HOME/.secondlife/tmp or something.
    mTempDir = "/tmp";
}

LLDir_Linux::~LLDir_Linux()
{
}

// Implementation


void LLDir_Linux::initAppDirs(const std::string &app_name,
                              const std::string& app_read_only_data_dir)
{
    // Allow override so test apps can read newview directory
    if (!app_read_only_data_dir.empty())
    {
        mAppRODataDir = app_read_only_data_dir;
        mSkinBaseDir = add(mAppRODataDir, "skins");
    }
    mAppName = app_name;

    std::string upper_app_name(app_name);
    LLStringUtil::toUpper(upper_app_name);
    std::string lower_app_name(app_name);
    LLStringUtil::toLower(lower_app_name);

    LLDirXDG::Layout dirs;
    auto app_home_env(LLStringUtil::getoptenv(upper_app_name + "_USER_DIR"));
    if (app_home_env)
    {
        // user has specified own userappdir i.e. $ALCHEMYNEXT_USER_DIR, and
        // gets everything under it as before XDG
        dirs = LLDirXDG::legacyLayout(*app_home_env);
    }
    else
    {
        // Older viewers kept everything in ~/.<app>; it moves to the XDG
        // directories the first time this one runs.
        const std::string legacy_root = mOSUserDir + "/." + lower_app_name;
        dirs = LLDirXDG::layout(mOSUserDir, lower_app_name);
        // Builds between the cache moving to XDG and the rest of the profile
        // following it named the cache's directory as the app is cased.
        LLDirXDG::adoptCache(LLDirXDG::baseDir("XDG_CACHE_HOME", mOSUserDir, ".cache") + "/" + app_name, dirs.cache,
                             mInitNotes);
        if (LLDirXDG::migrate(legacy_root, dirs, mInitNotes) == LLDirXDG::Migration::FAILED)
        {
            dirs = LLDirXDG::legacyLayout(legacy_root);
        }
        else
        {
            mLegacyUserAppDir = legacy_root;
        }
    }

    // create any directories we expect to write to.

    mOSUserAppDir = dirs.data;
    if (!LLDirXDG::makeDirs(mOSUserAppDir))
    {
        LL_WARNS() << "Couldn't create app user dir " << mOSUserAppDir << LL_ENDL;
        LL_WARNS() << "Default to base dir" << mOSUserDir << LL_ENDL;
        mOSUserAppDir = mOSUserDir;
    }

    // An XDG directory that can't be made falls back to where it was before
    // XDG, inside the user app dir.
    auto settle = [this](const std::string& dir, const char* what, const char* fallback_name)
    {
        if (LLDirXDG::makeDirs(dir))
        {
            return dir;
        }
        const std::string fallback = add(mOSUserAppDir, fallback_name);
        LL_WARNS() << "Couldn't create " << what << " dir " << dir << LL_ENDL;
        LL_WARNS() << "Default to " << fallback << LL_ENDL;
        LLDirXDG::makeDirs(fallback);
        return fallback;
    };
    mUserSettingsDir = settle(dirs.config, "LL_PATH_USER_SETTINGS", "user_settings");
    mLogsDir = settle(dirs.state, "LL_PATH_LOGS", "logs");
    mDefaultCacheDir = settle(dirs.cache, "LL_PATH_CACHE", "cache");

    mCAFile = getExpandedFilename(LL_PATH_EXECUTABLE, "ca-bundle.crt");
}

U32 LLDir_Linux::countFilesInDir(const std::string &dirname, const std::string &mask)
{
    U32 file_count = 0;
    glob_t g;

    std::string tmp_str;
    tmp_str = dirname;
    tmp_str += mask;

    if(glob(tmp_str.c_str(), GLOB_NOSORT, NULL, &g) == 0)
    {
        file_count = g.gl_pathc;

        globfree(&g);
    }

    return (file_count);
}

std::string LLDir_Linux::getCurPath()
{
    char tmp_str[LL_MAX_PATH];  /* Flawfinder: ignore */
    if (getcwd(tmp_str, LL_MAX_PATH) == NULL)
    {
        LL_WARNS() << "Could not get current directory" << LL_ENDL;
        tmp_str[0] = '\0';
    }
    return tmp_str;
}

/*virtual*/ std::string LLDir_Linux::getLLPluginFilename(std::string base_name)
{
    // Each plugin is now its own host executable named exactly for the plugin
    // (e.g. media_plugin_cef), launched directly - there is no separate SLPlugin
    // launcher or dlopen'd .so any more.
    return gDirUtilp->getLLPluginDir() + gDirUtilp->getDirDelimiter() + base_name;
}
