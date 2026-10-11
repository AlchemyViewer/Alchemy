/**
 * @file altesttempdir.h
 * @brief A directory of a test's own, removed when it is done with
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#ifndef AL_ALTESTTEMPDIR_H
#define AL_ALTESTTEMPDIR_H

#include "linden_common.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace ll_test
{
    // A new, empty directory under the temporary one, named by the caller
    // and a random suffix, and removed with everything in it when this goes.
    // A fixed name -- or one under the working directory, which is what
    // LLDir's temp directory is under the headless UI -- is shared by two
    // runs of the same test at once, and by a run and what a crashed one
    // left.
    class TempDir
    {
    public:
        explicit TempDir(std::string_view prefix)
        {
            // Where CI's runner says its jobs' temporary files go, as the
            // shared test harness has it, or the system's.
            const char* runner_temp = std::getenv("RUNNER_TEMP");
            const std::filesystem::path base = (runner_temp && *runner_temp) ? std::filesystem::path(runner_temp)
                                                                             : std::filesystem::temp_directory_path();
            std::random_device random;
            for (S32 tries = 0; tries < 100; ++tries)
            {
                char suffix[24];
                std::snprintf(suffix, sizeof(suffix), "_%08x%08x", static_cast<U32>(random()), static_cast<U32>(random()));
                const std::filesystem::path path = base / (std::string(prefix) + suffix);
                std::error_code ec;
                if (std::filesystem::create_directory(path, ec))
                {
                    mPath = path;
                    return;
                }
            }
            throw std::runtime_error("no temporary directory could be made under " + base.string());
        }

        ~TempDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(mPath, ec);
        }

        const std::filesystem::path& path() const { return mPath; }
        std::string                  string() const { return mPath.string(); }

        TempDir(const TempDir&) = delete;
        TempDir& operator=(const TempDir&) = delete;

    private:
        std::filesystem::path mPath;
    };
}

#endif // AL_ALTESTTEMPDIR_H
