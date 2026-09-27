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

#include "../alscripttextcache.h"

#include "../test/lltut.h"

namespace tut
{
    struct alscripttextcache_data
    {
        ALScriptTextCache cache;
    };

    typedef test_group<alscripttextcache_data> alscripttextcache_group;
    typedef alscripttextcache_group::object    alscripttextcache_object;
    alscripttextcache_group                    alscripttextcache_instance("alscripttextcache");

    template<> template<>
    void alscripttextcache_object::test<1>()
    {
        set_test_name("a text held by its identity and asset, taken only as that asset; a failure kept until put or forgotten");
        const LLUUID one = LLUUID::generateNewID(), two = LLUUID::generateNewID();
        std::string  text;
        ensure("nothing held", !cache.take("inventory:a", one, text) && !cache.holds("inventory:a"));
        cache.failed("inventory:a");
        ensure("failed", cache.hasFailed("inventory:a"));
        cache.put("inventory:a", one, "integer a;\n");
        ensure("put: no longer failed", !cache.hasFailed("inventory:a"));
        ensure("taken as its asset", cache.take("inventory:a", one, text) && text == "integer a;\n");
        ensure("not as another", !cache.take("inventory:a", two, text));
        ensure("held whatever the asset", cache.held("inventory:a", text) && cache.paths() == std::vector<std::string>{ "inventory:a" });
        cache.put("inventory:a", two, "integer b;\n");
        ensure_equals("put again: the bytes counted once", cache.bytes(), size_t(11));
        cache.failed("inventory:b");
        cache.forgetFailure("inventory:b");
        ensure("forgotten", !cache.hasFailed("inventory:b"));
    }

    template<> template<>
    void alscripttextcache_object::test<2>()
    {
        set_test_name("past the budget the least lately read goes first, and the newest stays whatever its size");
        const LLUUID      asset = LLUUID::generateNewID();
        const std::string third(ALScriptTextCache::BUDGET / 3 + 1, 'x');
        std::string       text;
        cache.put("a", asset, third);
        cache.put("b", asset, third);
        ensure("a read, so kept longer", cache.take("a", asset, text));
        cache.put("c", asset, third);
        ensure("b, least lately read, gone", !cache.holds("b") && cache.holds("a") && cache.holds("c"));
        cache.put("huge", asset, std::string(ALScriptTextCache::BUDGET + 1, 'y'));
        ensure("the newest kept alone", cache.holds("huge") && cache.paths().size() == 1);
    }

    template<> template<>
    void alscripttextcache_object::test<3>()
    {
        set_test_name("a hundred texts past the budget: the least lately read go, in one trim, until what is held is within it; failures bounded");
        const LLUUID      asset = LLUUID::generateNewID();
        const std::string fiftieth(ALScriptTextCache::BUDGET / 50, 'x');
        for (int i = 0; i < 100; ++i)
        {
            cache.put("t" + std::to_string(i), asset, fiftieth);
        }
        ensure("within the budget", cache.bytes() <= ALScriptTextCache::BUDGET);
        ensure("the newest kept, the oldest gone", cache.holds("t99") && cache.holds("t60") && !cache.holds("t0") && !cache.holds("t40"));
        for (size_t i = 0; i <= ALScriptTextCache::FAILURES_KEPT; ++i)
        {
            cache.failed("f" + std::to_string(i));
        }
        ensure("past the bound, the failures before it let go of", !cache.hasFailed("f0") && cache.hasFailed("f" + std::to_string(ALScriptTextCache::FAILURES_KEPT)));
    }
}
