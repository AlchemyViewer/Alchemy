/**
 * @file altestlinks.cpp
 * @brief A directory junction for a test, Windows' link to a folder that
 *        needs no privilege.
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

#include "altestlinks.h"

#if LL_WINDOWS
#include "llwin32headers.h"
#include <winioctl.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#endif

namespace ll_test
{
#if LL_WINDOWS
    namespace
    {
        // The reparse data a mount point (a junction) carries: the user-mode
        // headers declare the control code but not the buffer, which the
        // driver kit's ntifs.h has, so its layout is written out here.
        struct MountPointReparseData
        {
            ULONG  ReparseTag;
            USHORT ReparseDataLength;
            USHORT Reserved;
            USHORT SubstituteNameOffset;
            USHORT SubstituteNameLength;
            USHORT PrintNameOffset;
            USHORT PrintNameLength;
            WCHAR  PathBuffer[1];
        };

        std::error_code lastError()
        {
            return std::error_code(static_cast<int>(GetLastError()), std::system_category());
        }
    }

    std::error_code createDirectoryJunction(const std::filesystem::path& target, const std::filesystem::path& link)
    {
        std::error_code       ec;
        std::filesystem::path absolute = std::filesystem::absolute(target, ec);
        if (ec)
        {
            return ec;
        }
        // The NT name the junction stores, and the Win32 one it shows.
        const std::wstring print      = absolute.make_preferred().native();
        const std::wstring substitute = L"\\??\\" + print;
        const size_t       names      = (substitute.size() + 1 + print.size() + 1) * sizeof(WCHAR);
        const size_t       size       = offsetof(MountPointReparseData, PathBuffer) + names;
        std::unique_ptr<char[]>      bytes(new char[size]());
        MountPointReparseData* data = reinterpret_cast<MountPointReparseData*>(bytes.get());
        data->ReparseTag            = IO_REPARSE_TAG_MOUNT_POINT;
        data->ReparseDataLength     = static_cast<USHORT>(size - offsetof(MountPointReparseData, SubstituteNameOffset));
        data->SubstituteNameOffset  = 0;
        data->SubstituteNameLength  = static_cast<USHORT>(substitute.size() * sizeof(WCHAR));
        data->PrintNameOffset       = static_cast<USHORT>(data->SubstituteNameLength + sizeof(WCHAR));
        data->PrintNameLength       = static_cast<USHORT>(print.size() * sizeof(WCHAR));
        std::memcpy(data->PathBuffer, substitute.c_str(), (substitute.size() + 1) * sizeof(WCHAR));
        std::memcpy(data->PathBuffer + substitute.size() + 1, print.c_str(), (print.size() + 1) * sizeof(WCHAR));

        if (!CreateDirectoryW(link.c_str(), nullptr))
        {
            return lastError();
        }
        HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            ec = lastError();
            RemoveDirectoryW(link.c_str());
            return ec;
        }
        DWORD returned = 0;
        if (!DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data, static_cast<DWORD>(size), nullptr, 0, &returned, nullptr))
        {
            ec = lastError();
        }
        CloseHandle(handle);
        if (ec)
        {
            RemoveDirectoryW(link.c_str());
        }
        return ec;
    }
#else
    std::error_code createDirectoryJunction(const std::filesystem::path&, const std::filesystem::path&)
    {
        return std::make_error_code(std::errc::not_supported);
    }
#endif
}
