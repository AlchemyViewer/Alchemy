/**
 * @file aldbfaultvfs.h
 * @brief A VFS over the platform's that fails or holds writes on request, and
 *        what else the aldb tests share.
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

#include <chrono>
#include <functional>
#include <string>

namespace ll_test
{
    // SQLite's platform VFS with faults on request, registered as
    // "aldb_fault" the first time its name is asked for. What it is told
    // applies to every file opened through it from then on, and stays until
    // reset(); a test resets it as it ends (ALDbFaults).
    class ALDbFaultVfs
    {
    public:
        static const char* name();

        // Every write returns `rc` (SQLITE_FULL, SQLITE_IOERR_WRITE), or
        // writes again at SQLITE_OK.
        static void failWrites(int rc);
        // Counting the shared locks asked for from now, numbers `from` to
        // `from + count - 1` find the file busy, as when another process is
        // writing it; a count of 0 stops it.
        static void busySharedLocks(int from, int count);
        // Every write waits until released: a disk that hangs.
        static void holdWrites(bool hold);
        // Files opened from now on offer no shared memory, as on a network
        // filesystem, so that a WAL cannot run over them.
        static void withoutSharedMemory(bool without);
        static void reset();

        // Files open through it now.
        static int openFiles();
        // Writes waiting, held.
        static int heldWrites();
    };

    struct ALDbFaults
    {
        ALDbFaults() = default;
        ~ALDbFaults() { ALDbFaultVfs::reset(); }
        ALDbFaults(const ALDbFaults&)            = delete;
        ALDbFaults& operator=(const ALDbFaults&) = delete;
    };

    // A directory of the test's own under the system's temporary one,
    // removed with everything in it as the test ends.
    class ALDbTempDir
    {
    public:
        ALDbTempDir();
        ~ALDbTempDir();
        ALDbTempDir(const ALDbTempDir&)            = delete;
        ALDbTempDir& operator=(const ALDbTempDir&) = delete;

        const std::string& path() const { return mPath; }
        // A path inside it, as UTF-8.
        std::string file(const std::string& name) const;

    private:
        std::string mPath;
    };

    // Waits up to `limit` for `done` to say true, polling.
    bool waitUntil(std::chrono::milliseconds limit, const std::function<bool()>& done);

    // The running test binary, to run again as a helper.
    std::string currentExecutable();
}
