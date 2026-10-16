/**
 * @file llformat.cpp
 * @date   January 2007
 * @brief string formatting utility
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "llformat.h"

#include <cstdarg>

// common used function with va_list argument
// wrapper for vsnprintf to be called from llformatXXX functions.
static void va_format(std::string& out, const char *fmt, va_list va)
{
    char stack_buffer[1024];    /* Flawfinder: ignore */

    // vsnprintf reports the length the result wanted and always terminates what
    // it did write. _vsnprintf, which this used on Windows, does neither: on
    // truncation it returns -1 and leaves no terminator, so assigning from the
    // buffer read past its end until it happened upon a zero byte.
    va_list retry;
    va_copy(retry, va);
    const int needed = vsnprintf(stack_buffer, sizeof(stack_buffer), fmt, va); /* Flawfinder: ignore */

    if (needed < 0)
    {
        out.clear();
    }
    else if (static_cast<size_t>(needed) < sizeof(stack_buffer))
    {
        out.assign(stack_buffer, static_cast<size_t>(needed));
    }
    else
    {
        // Longer than the buffer, so format it again into one that fits.
        out.resize(static_cast<size_t>(needed) + 1);
        vsnprintf(out.data(), out.size(), fmt, retry); /* Flawfinder: ignore */
        out.resize(static_cast<size_t>(needed));
    }

    va_end(retry);
}

std::string llformat(const char *fmt, ...)
{
    std::string res;
    va_list va;
    va_start(va, fmt);
    va_format(res, fmt, va);
    va_end(va);
    return res;
}

std::string llformat_to_utf8(const char *fmt, ...)
{
    std::string res;
    va_list va;
    va_start(va, fmt);
    va_format(res, fmt, va);
    va_end(va);

#if LL_WINDOWS
    // made converting to utf8. See EXT-8318.
    res = ll_convert_string_to_utf8_string(res);
#endif
    return res;
}
