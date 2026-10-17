/**
 * @file altestsanitizer.h
 * @brief Whether the tests are built with a sanitizer whose own limits a case may pass.
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

#ifndef AL_ALTESTSANITIZER_H
#define AL_ALTESTSANITIZER_H

namespace ll_test
{
    // Whether the tests are built with ThreadSanitizer: GCC says so by
    // __SANITIZE_THREAD__, Clang by __has_feature.
#if defined(__SANITIZE_THREAD__)
    inline constexpr bool THREAD_SANITIZER = true;
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
    inline constexpr bool THREAD_SANITIZER = true;
#else
    inline constexpr bool THREAD_SANITIZER = false;
#endif
#else
    inline constexpr bool THREAD_SANITIZER = false;
#endif

    // ThreadSanitizer keeps a call stack of its own beside each thread's, of
    // this many frames, fixed when it is built: a call deeper than that
    // writes past its end, and the process falls over without a report,
    // however much stack the thread itself has. A case that recurses that
    // deep on purpose -- a script as long as the grid takes, through LL's
    // compiler -- is past what it can follow.
    inline constexpr unsigned THREAD_SANITIZER_FRAMES = 65536;
}

#endif // AL_ALTESTSANITIZER_H
