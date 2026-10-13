/**
 * @file alscripttextcache_test.cpp
 * @brief Tests for ALScriptTextCache: texts by identity and asset, the least lately read let go of past a budget.
 *
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
 */

#include "linden_common.h"

#include "../preprocessor/alscripttextcache.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alscripttextcache_data
    {
        ALScriptTextCache cache;
    };

    TEST_CASE_METHOD(alscripttextcache_data, "alscripttextcache: a text is taken only as its asset; a failure is kept until put or forgotten", "[alscripttextcache]")
    {
        const LLUUID one = LLUUID::generateNewID(), two = LLUUID::generateNewID();
        std::string  text;
        {
            INFO("nothing held");
            CHECK_FALSE(cache.take("inventory:a", one, text));
            CHECK_FALSE(cache.holds("inventory:a"));
        }
        cache.failed("inventory:a");
        CHECK(cache.hasFailed("inventory:a"));
        cache.put("inventory:a", one, "integer a;\n");
        UNSCOPED_INFO("put: no longer failed");
        CHECK_FALSE(cache.hasFailed("inventory:a"));
        {
            INFO("taken as its asset");
            CHECK(cache.take("inventory:a", one, text));
            CHECK(text == "integer a;\n");
        }
        UNSCOPED_INFO("not as another");
        CHECK_FALSE(cache.take("inventory:a", two, text));
        {
            INFO("held whatever the asset");
            CHECK(cache.held("inventory:a", text));
            CHECK(cache.paths() == std::vector<std::string>{ "inventory:a" });
        }
        cache.put("inventory:a", two, "integer b;\n");
        UNSCOPED_INFO("put again: the bytes counted once");
        CHECK(cache.bytes() == size_t(11));
        cache.failed("inventory:b");
        cache.forgetFailure("inventory:b");
        CHECK_FALSE(cache.hasFailed("inventory:b"));
    }

    TEST_CASE_METHOD(alscripttextcache_data, "alscripttextcache: past the budget the least lately read goes first; the newest always stays", "[alscripttextcache]")
    {
        const LLUUID      asset = LLUUID::generateNewID();
        const std::string third(ALScriptTextCache::BUDGET / 3 + 1, 'x');
        std::string       text;
        cache.put("a", asset, third);
        cache.put("b", asset, third);
        UNSCOPED_INFO("a read, so kept longer");
        CHECK(cache.take("a", asset, text));
        cache.put("c", asset, third);
        {
            INFO("b, least lately read, gone");
            CHECK_FALSE(cache.holds("b"));
            CHECK(cache.holds("a"));
            CHECK(cache.holds("c"));
        }
        cache.put("huge", asset, std::string(ALScriptTextCache::BUDGET + 1, 'y'));
        {
            INFO("the newest kept alone");
            CHECK(cache.holds("huge"));
            REQUIRE(cache.paths().size() == 1);
        }
    }

    TEST_CASE_METHOD(alscripttextcache_data, "alscripttextcache: a hundred texts past the budget trim to within it; failures are bounded", "[alscripttextcache]")
    {
        const LLUUID      asset = LLUUID::generateNewID();
        const std::string fiftieth(ALScriptTextCache::BUDGET / 50, 'x');
        for (int i = 0; i < 100; ++i)
        {
            cache.put("t" + std::to_string(i), asset, fiftieth);
        }
        UNSCOPED_INFO("within the budget");
        CHECK(cache.bytes() <= ALScriptTextCache::BUDGET);
        {
            INFO("the newest kept, the oldest gone");
            CHECK(cache.holds("t99"));
            CHECK(cache.holds("t60"));
            CHECK_FALSE(cache.holds("t0"));
            CHECK_FALSE(cache.holds("t40"));
        }
        for (size_t i = 0; i <= ALScriptTextCache::FAILURES_KEPT; ++i)
        {
            cache.failed("f" + std::to_string(i));
        }
        {
            INFO("past the bound, the failures before it let go of");
            CHECK_FALSE(cache.hasFailed("f0"));
            CHECK(cache.hasFailed("f" + std::to_string(ALScriptTextCache::FAILURES_KEPT)));
        }
    }
}
