/**
 * @file alsanitizer.cpp
 * @brief What a process the sanitizers check does that a shipped one does not.
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

#include "alsanitizer.h"

#if LL_LINUX
#include <dlfcn.h>
#include <link.h>

#include <string>
#include <vector>
#endif

void LL::pinLoadedLibraries()
{
#if LL_LINUX
    // Named first, and opened after: dlopen() is not for the loader's own
    // walk of what it has loaded.
    std::vector<std::string> names;
    dl_iterate_phdr(
        [](dl_phdr_info* info, size_t, void* out) -> int
        {
            if (info->dlpi_name && info->dlpi_name[0])
            {
                static_cast<std::vector<std::string>*>(out)->emplace_back(info->dlpi_name);
            }
            return 0;
        },
        &names);
    for (const std::string& name : names)
    {
        dlopen(name.c_str(), RTLD_NOW | RTLD_NOLOAD | RTLD_NODELETE);
    }
#endif
}
