/**
 * @file aldbfaultvfs.cpp
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

#include "linden_common.h"

#include "aldbfaultvfs.h"

#include "fsyspath.h"
#include "llcond.h"
#include "lltimer.h"
#include "lluuid.h"

#include <sqlite3.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#if LL_WINDOWS
#include "llwin32headers.h"
#elif LL_DARWIN
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace
{
    // The wrapper a file opened through the VFS is: its own methods, and the
    // platform's file right after it in the same allocation.
    struct FaultFile
    {
        sqlite3_file  base;
        sqlite3_file* real;
    };

    sqlite3_file* realOf(sqlite3_file* file)
    {
        return reinterpret_cast<FaultFile*>(file)->real;
    }

    sqlite3_vfs*      sPlatform = nullptr;
    sqlite3_vfs       sFault{};
    std::atomic<int>  sFailWrites{ SQLITE_OK };
    std::atomic<int>  sSharedLocks{ 0 };
    std::atomic<int>  sBusyFrom{ 0 };
    std::atomic<int>  sBusyCount{ 0 };
    std::atomic<bool> sWithoutShm{ false };
    std::atomic<int>  sOpenFiles{ 0 };
    std::atomic<int>  sHeldWrites{ 0 };
    LLBoolCond        sHold{ false };

    int fileClose(sqlite3_file* file)
    {
        sqlite3_file* real = realOf(file);
        const int     rc   = real->pMethods ? real->pMethods->xClose(real) : SQLITE_OK;
        --sOpenFiles;
        return rc;
    }

    int fileRead(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset)
    {
        return realOf(file)->pMethods->xRead(realOf(file), buffer, amount, offset);
    }

    int fileWrite(sqlite3_file* file, const void* buffer, int amount, sqlite3_int64 offset)
    {
        if (sHold.get())
        {
            ++sHeldWrites;
            sHold.wait_equal(false);
            --sHeldWrites;
        }
        if (const int rc = sFailWrites.load(); rc != SQLITE_OK)
        {
            return rc;
        }
        return realOf(file)->pMethods->xWrite(realOf(file), buffer, amount, offset);
    }

    int fileTruncate(sqlite3_file* file, sqlite3_int64 size)
    {
        return realOf(file)->pMethods->xTruncate(realOf(file), size);
    }

    int fileSync(sqlite3_file* file, int flags)
    {
        return realOf(file)->pMethods->xSync(realOf(file), flags);
    }

    int fileSize(sqlite3_file* file, sqlite3_int64* size)
    {
        return realOf(file)->pMethods->xFileSize(realOf(file), size);
    }

    int fileLock(sqlite3_file* file, int lock)
    {
        if (lock == SQLITE_LOCK_SHARED && sBusyCount.load() > 0)
        {
            const int attempt = ++sSharedLocks;
            if (attempt >= sBusyFrom.load() && attempt < sBusyFrom.load() + sBusyCount.load())
            {
                return SQLITE_BUSY;
            }
        }
        return realOf(file)->pMethods->xLock(realOf(file), lock);
    }

    int fileUnlock(sqlite3_file* file, int lock)
    {
        return realOf(file)->pMethods->xUnlock(realOf(file), lock);
    }

    int fileCheckReserved(sqlite3_file* file, int* reserved)
    {
        return realOf(file)->pMethods->xCheckReservedLock(realOf(file), reserved);
    }

    int fileControl(sqlite3_file* file, int op, void* arg)
    {
        return realOf(file)->pMethods->xFileControl(realOf(file), op, arg);
    }

    int fileSectorSize(sqlite3_file* file)
    {
        return realOf(file)->pMethods->xSectorSize(realOf(file));
    }

    int fileDeviceCharacteristics(sqlite3_file* file)
    {
        return realOf(file)->pMethods->xDeviceCharacteristics(realOf(file));
    }

    int fileShmMap(sqlite3_file* file, int region, int size, int extend, void volatile** out)
    {
        return realOf(file)->pMethods->xShmMap(realOf(file), region, size, extend, out);
    }

    int fileShmLock(sqlite3_file* file, int offset, int count, int flags)
    {
        return realOf(file)->pMethods->xShmLock(realOf(file), offset, count, flags);
    }

    void fileShmBarrier(sqlite3_file* file)
    {
        realOf(file)->pMethods->xShmBarrier(realOf(file));
    }

    int fileShmUnmap(sqlite3_file* file, int remove)
    {
        return realOf(file)->pMethods->xShmUnmap(realOf(file), remove);
    }

    int fileFetch(sqlite3_file* file, sqlite3_int64 offset, int amount, void** out)
    {
        return realOf(file)->pMethods->xFetch(realOf(file), offset, amount, out);
    }

    int fileUnfetch(sqlite3_file* file, sqlite3_int64 offset, void* page)
    {
        return realOf(file)->pMethods->xUnfetch(realOf(file), offset, page);
    }

    // Version 3: shared memory and memory mapping, as the platform's has.
    const sqlite3_io_methods sMethods = {
        3,           fileClose,         fileRead,          fileWrite,      fileTruncate,
        fileSync,    fileSize,          fileLock,          fileUnlock,     fileCheckReserved,
        fileControl, fileSectorSize,    fileDeviceCharacteristics,
        fileShmMap,  fileShmLock,       fileShmBarrier,    fileShmUnmap,   fileFetch,
        fileUnfetch,
    };

    // Version 1: no shared memory, so SQLite will not put the file in WAL.
    const sqlite3_io_methods sMethodsWithoutShm = {
        1,           fileClose,      fileRead,  fileWrite,  fileTruncate,
        fileSync,    fileSize,       fileLock,  fileUnlock, fileCheckReserved,
        fileControl, fileSectorSize, fileDeviceCharacteristics,
        nullptr,     nullptr,        nullptr,   nullptr,    nullptr,
        nullptr,
    };

    int vfsOpen(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* file, int flags, int* out_flags)
    {
        FaultFile* fault = reinterpret_cast<FaultFile*>(file);
        fault->real      = reinterpret_cast<sqlite3_file*>(fault + 1);
        const int rc     = sPlatform->xOpen(sPlatform, name, fault->real, flags, out_flags);
        if (fault->real->pMethods)
        {
            file->pMethods = sWithoutShm ? &sMethodsWithoutShm : &sMethods;
            ++sOpenFiles;
        }
        else
        {
            file->pMethods = nullptr;
        }
        return rc;
    }

    int vfsDelete(sqlite3_vfs*, const char* name, int sync)
    {
        return sPlatform->xDelete(sPlatform, name, sync);
    }

    int vfsAccess(sqlite3_vfs*, const char* name, int flags, int* out)
    {
        return sPlatform->xAccess(sPlatform, name, flags, out);
    }

    int vfsFullPathname(sqlite3_vfs*, const char* name, int size, char* out)
    {
        return sPlatform->xFullPathname(sPlatform, name, size, out);
    }

    void* vfsDlOpen(sqlite3_vfs*, const char* name)
    {
        return sPlatform->xDlOpen(sPlatform, name);
    }

    void vfsDlError(sqlite3_vfs*, int size, char* out)
    {
        sPlatform->xDlError(sPlatform, size, out);
    }

    void (*vfsDlSym(sqlite3_vfs*, void* handle, const char* symbol))(void)
    {
        return sPlatform->xDlSym(sPlatform, handle, symbol);
    }

    void vfsDlClose(sqlite3_vfs*, void* handle)
    {
        sPlatform->xDlClose(sPlatform, handle);
    }

    int vfsRandomness(sqlite3_vfs*, int size, char* out)
    {
        return sPlatform->xRandomness(sPlatform, size, out);
    }

    int vfsSleep(sqlite3_vfs*, int microseconds)
    {
        return sPlatform->xSleep(sPlatform, microseconds);
    }

    int vfsCurrentTime(sqlite3_vfs*, double* out)
    {
        return sPlatform->xCurrentTime(sPlatform, out);
    }

    int vfsGetLastError(sqlite3_vfs*, int size, char* out)
    {
        return sPlatform->xGetLastError ? sPlatform->xGetLastError(sPlatform, size, out) : 0;
    }

    int vfsCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64* out)
    {
        return sPlatform->xCurrentTimeInt64(sPlatform, out);
    }

    void registerFault()
    {
        sPlatform = sqlite3_vfs_find(nullptr);
        sFault.iVersion          = 2;
        sFault.szOsFile          = int(sizeof(FaultFile)) + sPlatform->szOsFile;
        sFault.mxPathname        = sPlatform->mxPathname;
        sFault.zName             = "aldb_fault";
        sFault.xOpen             = vfsOpen;
        sFault.xDelete           = vfsDelete;
        sFault.xAccess           = vfsAccess;
        sFault.xFullPathname     = vfsFullPathname;
        sFault.xDlOpen           = vfsDlOpen;
        sFault.xDlError          = vfsDlError;
        sFault.xDlSym            = vfsDlSym;
        sFault.xDlClose          = vfsDlClose;
        sFault.xRandomness       = vfsRandomness;
        sFault.xSleep            = vfsSleep;
        sFault.xCurrentTime      = vfsCurrentTime;
        sFault.xGetLastError     = vfsGetLastError;
        sFault.xCurrentTimeInt64 = vfsCurrentTimeInt64;
        sqlite3_vfs_register(&sFault, 0);
    }
}

namespace ll_test
{
    const char* ALDbFaultVfs::name()
    {
        static const bool registered = (registerFault(), true);
        (void)registered;
        return sFault.zName;
    }

    void ALDbFaultVfs::failWrites(int rc)
    {
        sFailWrites = rc;
    }

    void ALDbFaultVfs::busySharedLocks(int from, int count)
    {
        sBusyCount   = 0;
        sSharedLocks = 0;
        sBusyFrom    = from;
        sBusyCount   = count;
    }

    void ALDbFaultVfs::holdWrites(bool hold)
    {
        sHold.set_all(hold);
    }

    void ALDbFaultVfs::withoutSharedMemory(bool without)
    {
        sWithoutShm = without;
    }

    void ALDbFaultVfs::reset()
    {
        failWrites(SQLITE_OK);
        busySharedLocks(0, 0);
        withoutSharedMemory(false);
        holdWrites(false);
    }

    int ALDbFaultVfs::openFiles()
    {
        return sOpenFiles;
    }

    int ALDbFaultVfs::heldWrites()
    {
        return sHeldWrites;
    }

    ALDbTempDir::ALDbTempDir()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path  root = fs::temp_directory_path(ec) / "aldb-tests";
        // A name already taken is drawn again; a directory that cannot be made
        // at all (no space, no permission, a file in the way) fails the test.
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            const fs::path candidate = root / LLUUID::generateNewID().asString();
            if (fs::create_directories(candidate, ec))
            {
                mPath = fsyspath(candidate).string();
                return;
            }
            if (ec)
            {
                break;
            }
        }
        throw std::runtime_error("cannot make a directory under " + fsyspath(root).string() + ": " + ec.message());
    }

    ALDbTempDir::~ALDbTempDir()
    {
        // A file a test still held open on Windows cannot go at once; a few
        // tries, then left for the system's own cleaning.
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(mPath), ec);
            if (!ec)
            {
                return;
            }
            ms_sleep(50);
        }
    }

    std::string ALDbTempDir::file(const std::string& name) const
    {
        return fsyspath(fsyspath(mPath) / fsyspath(name)).string();
    }

    bool waitUntil(std::chrono::milliseconds limit, const std::function<bool()>& done)
    {
        LLTimer timer;
        timer.setTimerExpirySec(F32(limit.count()) / 1000.f);
        while (!done())
        {
            if (timer.hasExpired())
            {
                return false;
            }
            ms_sleep(5);
        }
        return true;
    }

    std::string currentExecutable()
    {
#if LL_WINDOWS
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
            if (length == 0)
            {
                return {};
            }
            if (length < buffer.size())
            {
                buffer.resize(length);
                return fsyspath(std::filesystem::path(buffer)).string();
            }
            buffer.resize(buffer.size() * 2);
        }
#elif LL_DARWIN
        uint32_t    size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        {
            return {};
        }
        buffer.resize(std::strlen(buffer.c_str()));
        return buffer;
#else
        std::error_code ec;
        return std::filesystem::read_symlink("/proc/self/exe", ec).string();
#endif
    }
}
