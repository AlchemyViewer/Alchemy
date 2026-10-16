/**
 * @file llcallbacklist_test.cpp
 * @brief The idle callback list, and the callbacks it is emptied of before they run.
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

#include "llcallbacklist.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace
{
    void count_call(void* data)
    {
        ++*static_cast<int*>(data);
    }

    TEST_CASE("llcallbacklist: a one-time callback runs once and lets go of what it holds", "[llcallbacklist]")
    {
        int runs = 0;
        auto held = std::make_shared<int>(0);
        std::weak_ptr<int> watch(held);
        doOnIdleOneTime([held, &runs]() { ++runs; });
        held.reset();
        CHECK_FALSE(watch.expired());

        gIdleCallbacks.callFunctions();
        CHECK(runs == 1);
        CHECK(watch.expired());

        gIdleCallbacks.callFunctions();
        CHECK(runs == 1);
    }

    TEST_CASE("llcallbacklist: emptying the list lets go of the callbacks still waiting, unrun", "[llcallbacklist]")
    {
        // As the viewer's cleanup does: a callback put off to an idle that never comes
        // still goes, and with it what it holds, which is often what it was to delete.
        int runs = 0;
        auto held = std::make_shared<int>(0);
        std::weak_ptr<int> watch(held);
        doOnIdleOneTime([held, &runs]() { ++runs; });
        doOnIdleRepeating([held, &runs]() { ++runs; return false; });
        int plain = 0;
        gIdleCallbacks.addFunction(&count_call, &plain);
        held.reset();

        gIdleCallbacks.deleteAllFunctions();
        CHECK(watch.expired());
        CHECK_FALSE(gIdleCallbacks.containsFunction(&count_call, &plain));

        gIdleCallbacks.callFunctions();
        CHECK(runs == 0);
        CHECK(plain == 0);
    }
}
