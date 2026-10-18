/**
 * @file llavatarnamecache_test.cpp
 * @author James Cook
 * @brief LLAvatarNameCache test cases.
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#include "../llavatarnamecache.h"

#include "httprequest.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // The cache holds an HTTP request, which needs the service up for as long as the cache lives.
    struct NameCacheService
    {
        NameCacheService() { LLCore::HttpRequest::createService(); }
        ~NameCacheService()
        {
            LLAvatarNameCache::deleteSingleton();
            LLCore::HttpRequest::destroyService();
        }
    };

    TEST_CASE("LLAvatarNameCache: a callback for an unknown agent is kept, one for the null id is not", "[LLAvatarNameCache]")
    {
        NameCacheService   service;
        LLAvatarNameCache& cache = LLAvatarNameCache::instance();

        S32  calls = 0;
        auto count = [&calls](const LLUUID&, const LLAvatarName&) { ++calls; };

        LLAvatarNameCache::callback_connection_t pending =
            cache.getNameCallback(LLUUID("3941037e-78ab-45f0-b421-bd6e77c1804d"), count);
        CHECK(pending.connected());
        pending.disconnect();

        CHECK_FALSE(cache.getNameCallback(LLUUID::null, count).connected());
        CHECK(calls == 0);
    }

    TEST_CASE("LLAvatarNameCache: max_age_from_cache_control parses a well-formed max-age", "[LLAvatarNameCache]")
    {
        bool valid = false;
        S32 max_age = 0;

        valid = max_age_from_cache_control("max-age=3600", &max_age);
        UNSCOPED_INFO("typical input valid");
        CHECK(valid);
        CHECK(max_age == 3600);

        valid = max_age_from_cache_control(
            " max-age=600 , no-cache,private=\"stuff\" ", &max_age);
        UNSCOPED_INFO("complex input valid");
        CHECK(valid);
        CHECK(max_age == 600);

        valid = max_age_from_cache_control(
            "no-cache, max-age = 123 ", &max_age);
        UNSCOPED_INFO("complex input 2 valid");
        CHECK(valid);
        CHECK(max_age == 123);
    }

    TEST_CASE("LLAvatarNameCache: max_age_from_cache_control rejects malformed input but accepts zero", "[LLAvatarNameCache]")
    {
        bool valid = false;
        S32 max_age = -1;

        valid = max_age_from_cache_control("", &max_age);
        UNSCOPED_INFO("empty input returns invalid");
        CHECK_FALSE(valid);
        UNSCOPED_INFO("empty input doesn't change val");
        CHECK(max_age == -1);

        valid = max_age_from_cache_control("no-cache", &max_age);
        UNSCOPED_INFO("no max-age field returns invalid");
        CHECK_FALSE(valid);

        valid = max_age_from_cache_control("max", &max_age);
        UNSCOPED_INFO("just 'max' returns invalid");
        CHECK_FALSE(valid);

        valid = max_age_from_cache_control("max-age", &max_age);
        UNSCOPED_INFO("partial max-age is invalid");
        CHECK_FALSE(valid);

        valid = max_age_from_cache_control("max-age=", &max_age);
        UNSCOPED_INFO("longer partial max-age is invalid");
        CHECK_FALSE(valid);

        valid = max_age_from_cache_control("max-age=FOO", &max_age);
        UNSCOPED_INFO("invalid integer max-age is invalid");
        CHECK_FALSE(valid);

        valid = max_age_from_cache_control("max-age 234", &max_age);
        UNSCOPED_INFO("space separated max-age is invalid");
        CHECK_FALSE(valid);

        valid = max_age_from_cache_control("max-age=0", &max_age);
        UNSCOPED_INFO("zero max-age is valid");
        CHECK(valid);

        // *TODO: Handle "0000" as zero
        //valid = max_age_from_cache_control("max-age=0000", &max_age);
        //ensure("multi-zero max-age is valid", valid);

        valid = max_age_from_cache_control("max-age=-123", &max_age);
        UNSCOPED_INFO("less than zero max-age is invalid");
        CHECK_FALSE(valid);
    }
}
