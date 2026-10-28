/**
 * @file aldbrecovery.h
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

#pragma once

#include <optional>
#include <string>

namespace ALDbRecovery
{
    // Moves a database file and its -wal and -shm to <path>.corrupt-<when>,
    // all three or none: a WAL left behind would be replayed into whatever
    // file next takes the name. Fails, moving nothing, where the file is held
    // open by another process (Windows will not rename it). Where it lies
    // now, or nothing.
    std::optional<std::string> quarantine(const std::string& path, std::string* error = nullptr);

    // Writes what SQLite's recover API can read of the database at `from`
    // into a new database at `to`, its user_version and application_id
    // among it. `to` must not exist.
    bool salvage(const std::string& from, const std::string& to, std::string* error = nullptr);

    // Removes a database file and its -wal and -shm. False if any of them
    // is still there.
    bool remove(const std::string& path, std::string* error = nullptr);

    // "20261026T204812Z": the moment, in UTC, as a file name takes it.
    std::string stamp();
}
