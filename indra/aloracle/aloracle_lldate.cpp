/**
 * @file aloracle_lldate.cpp
 * @brief aloracle's functions over LLDate's ISO-8601 text.
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

#include "aloracleboundary.h"

#include "lldate.h"

int32_t aloracle_lldate_format(double seconds, aloracle_bytes* text)
{
    return aloracle::guarded([&]() -> int32_t
    {
        if (!aloracle::clear(text))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        LLDate date;
        date.secondsSinceEpoch(seconds);
        return aloracle::give(date.asString(), text);
    });
}

int32_t aloracle_lldate_parse(const uint8_t* data, size_t size, double* seconds)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        if (!seconds || !aloracle::take(data, size, text))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        LLDate date;
        const bool parsed = date.fromString(text);
        *seconds = date.secondsSinceEpoch();
        return parsed ? ALORACLE_OK : ALORACLE_FAILED;
    });
}
