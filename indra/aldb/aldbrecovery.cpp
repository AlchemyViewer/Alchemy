/**
 * @file aldbrecovery.cpp
 * @brief Moving a damaged database file aside, and salvaging what can be read
 *        of it into a new one.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
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

#include "linden_common.h"

#include "aldbrecovery.h"

#include "fsyspath.h"
#include "lldate.h"

#include <sqlite3.h>
#include <sqlite3recover.h>

#include <fmt/format.h>

#include <array>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace
{
    constexpr std::array<const char*, 2> SIDE_FILES = { "-wal", "-shm" };

    void say(std::string* error, std::string text)
    {
        if (error)
        {
            *error = std::move(text);
        }
    }
}

std::string ALDbRecovery::stamp()
{
    const std::time_t now = std::time_t(LLDate::now().secondsSinceEpoch());
    std::tm           utc{};
#if LL_WINDOWS
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    return fmt::format("{:04}{:02}{:02}T{:02}{:02}{:02}Z", utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                       utc.tm_min, utc.tm_sec);
}

std::optional<std::string> ALDbRecovery::quarantine(const std::string& path, std::string* error)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    // A name nothing has: a second damaged copy within one second gets -2.
    const std::string base   = path + ".corrupt-" + stamp();
    std::string       target = base;
    for (int n = 2; fs::exists(fsyspath(target), ec) || fs::exists(fsyspath(target + "-wal"), ec); ++n)
    {
        target = base + "-" + std::to_string(n);
    }

    fs::rename(fsyspath(path), fsyspath(target), ec);
    if (ec)
    {
        say(error, fmt::format("could not move {} aside: {}", path, ec.message()));
        return std::nullopt;
    }
    for (const char* side : SIDE_FILES)
    {
        const std::string from = path + side;
        if (!fs::exists(fsyspath(from), ec))
        {
            continue;
        }
        fs::rename(fsyspath(from), fsyspath(target + side), ec);
        if (ec)
        {
            // All or none: put back what moved, so the file and its WAL stay
            // together where they were.
            say(error, fmt::format("could not move {} aside: {}", from, ec.message()));
            std::error_code back;
            for (const char* moved : SIDE_FILES)
            {
                if (moved == side)
                {
                    break;
                }
                fs::rename(fsyspath(target + moved), fsyspath(path + moved), back);
            }
            fs::rename(fsyspath(target), fsyspath(path), back);
            return std::nullopt;
        }
    }
    return target;
}

bool ALDbRecovery::salvage(const std::string& from, const std::string& to, std::string* error)
{
    sqlite3* source = nullptr;
    int      rc     = sqlite3_open_v2(from.c_str(), &source, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    if (rc != SQLITE_OK)
    {
        say(error, fmt::format("could not open {}: {}", from, source ? sqlite3_errmsg(source) : sqlite3_errstr(rc)));
        sqlite3_close_v2(source);
        return false;
    }

    sqlite3_recover* recover = sqlite3_recover_init(source, "main", to.c_str());
    if (!recover)
    {
        say(error, "the recover API could not start");
        sqlite3_close_v2(source);
        return false;
    }
    sqlite3_recover_run(recover);
    rc = sqlite3_recover_errcode(recover);
    const std::string message = sqlite3_recover_errmsg(recover) ? sqlite3_recover_errmsg(recover) : "";
    sqlite3_recover_finish(recover);
    sqlite3_close_v2(source);

    // A read-only connection leaves the -shm it made, and a -wal it made
    // empty, where a writer's close would remove them. The index is rebuilt
    // from the WAL whenever one is opened, and an empty WAL holds nothing.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(fsyspath(from + "-shm"), ec);
        const fs::path wal = fsyspath(from + "-wal");
        if (fs::exists(wal, ec) && fs::file_size(wal, ec) == 0 && !ec)
        {
            fs::remove(wal, ec);
        }
    }

    if (rc != SQLITE_OK)
    {
        say(error, fmt::format("recovering {} failed: {}", from, message));
        return false;
    }
    return true;
}

bool ALDbRecovery::remove(const std::string& path, std::string* error)
{
    namespace fs = std::filesystem;
    bool ok = true;
    for (const std::string& file : { path, path + "-wal", path + "-shm" })
    {
        std::error_code ec;
        fs::remove(fsyspath(file), ec);
        if (ec)
        {
            say(error, fmt::format("could not remove {}: {}", file, ec.message()));
            ok = false;
        }
    }
    return ok;
}
