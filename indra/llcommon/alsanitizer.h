/**
 * @file alsanitizer.h
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

#pragma once

#include "llpreprocessor.h"

namespace LL
{
    // Whether the build is checked by LeakSanitizer, which AddressSanitizer
    // runs as the process ends: GCC says so by __SANITIZE_ADDRESS__, Clang by
    // __has_feature.
#if defined(__SANITIZE_ADDRESS__)
    inline constexpr bool LEAK_SANITIZER = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(leak_sanitizer)
    inline constexpr bool LEAK_SANITIZER = true;
#else
    inline constexpr bool LEAK_SANITIZER = false;
#endif
#else
    inline constexpr bool LEAK_SANITIZER = false;
#endif

    // Keeps every library loaded now mapped until the process ends, on Linux;
    // elsewhere it does nothing. For a process LeakSanitizer checks, once its
    // GL context is made: SDL unloads the GL driver with its last window, and
    // what a library keeps for the process in its globals -- Mesa's code heap,
    // a vendor's EGL state -- is then held by nothing LeakSanitizer can see.
    // Nor can it name an unloaded library's frames, so all of it would be
    // reported, and nothing could say it was the driver's.
    LL_COMMON_API void pinLoadedLibraries();
}
