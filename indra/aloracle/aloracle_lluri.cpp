/**
 * @file aloracle_lluri.cpp
 * @brief aloracle's functions over LLURI's escaping and queries.
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

#include "llsd.h"
#include "lluri.h"

static_assert((int)ALORACLE_LLURI_UNRESERVED == (int)LLURI::EscapeSet::UNRESERVED);
static_assert((int)ALORACLE_LLURI_HOST_AND_PORT == (int)LLURI::EscapeSet::HOST_AND_PORT);
static_assert((int)ALORACLE_LLURI_PATH_COMPONENT == (int)LLURI::EscapeSet::PATH_COMPONENT);
static_assert((int)ALORACLE_LLURI_QUERY_VARIABLE == (int)LLURI::EscapeSet::QUERY_VARIABLE);
static_assert((int)ALORACLE_LLURI_QUERY_VALUE == (int)LLURI::EscapeSet::QUERY_VALUE);
static_assert((int)ALORACLE_LLURI_URI_QUERY == (int)LLURI::EscapeSet::URI_QUERY);
static_assert((int)ALORACLE_LLURI_URI_DATA == (int)LLURI::EscapeSet::URI_DATA);
static_assert((int)ALORACLE_LLURI_URI_PATH == (int)LLURI::EscapeSet::URI_PATH);

int32_t aloracle_lluri_escape(int32_t set, const uint8_t* data, size_t size, aloracle_bytes* escaped)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        if (!aloracle::clear(escaped) || !aloracle::take(data, size, text)
            || set < ALORACLE_LLURI_UNRESERVED || set > ALORACLE_LLURI_URI_PATH)
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::give(LLURI::escape(text, (LLURI::EscapeSet)set), escaped);
    });
}

int32_t aloracle_lluri_unescape(const uint8_t* data, size_t size, aloracle_bytes* unescaped)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        if (!aloracle::clear(unescaped) || !aloracle::take(data, size, text))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::give(LLURI::unescape(text), unescaped);
    });
}

int32_t aloracle_lluri_query_map(const uint8_t* data, size_t size, aloracle_bytes* value)
{
    return aloracle::guarded([&]() -> int32_t
    {
        std::string text;
        if (!aloracle::clear(value) || !aloracle::take(data, size, text))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::giveValue(LLURI::queryMap(text), value);
    });
}

int32_t aloracle_lluri_map_to_query_string(const uint8_t* value, size_t size, aloracle_bytes* query)
{
    return aloracle::guarded([&]() -> int32_t
    {
        LLSD map;
        if (!aloracle::clear(query) || !aloracle::takeValue(value, size, map))
        {
            return ALORACLE_BAD_ARGUMENT;
        }
        return aloracle::give(LLURI::mapToQueryString(map), query);
    });
}
