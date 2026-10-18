/**
 * @file alrenderinputrequests_test.cpp
 * @brief What the render inputs' changes since they were last applied ask of the next application.
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

#include "../alrenderinputrequests.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    using Requests = ALRenderInputRequests;
    using Change   = Requests::Change;

    // Whether an application does anything at all.
    bool idle(const Requests::Application& application)
    {
        return !application.mReloadShaders && !application.mReconcile && !application.mSkipFrame &&
               application.mChanges == 0;
    }

    TEST_CASE("alrenderinputrequests: nothing marked applies nothing", "[alrenderinputrequests]")
    {
        Requests requests;
        CHECK_FALSE(requests.pending());
        CHECK_FALSE(requests.windowResized());
        CHECK(idle(requests.take(true)));
    }

    TEST_CASE("alrenderinputrequests: a batch of resource changes reconciles once", "[alrenderinputrequests]")
    {
        // A preset: the antialiasing, the bloom and the shadow scale in one frame.
        Requests requests;
        for (int i = 0; i < 12; ++i)
        {
            requests.mark(Change::Resources);
        }
        CHECK(requests.pending());

        const Requests::Application application = requests.take(true);
        CHECK(application.mReconcile);
        CHECK_FALSE(application.mReloadShaders);
        CHECK_FALSE(application.mSkipFrame);
        CHECK(application.mChanges == 12);

        CHECK_FALSE(requests.pending());
        CHECK(idle(requests.take(true)));
    }

    TEST_CASE("alrenderinputrequests: a batch with a shader change reloads once and reconciles through the reload",
              "[alrenderinputrequests]")
    {
        // Vintage mode, setGraphicsLevel: shader inputs and resource inputs together.
        Requests requests;
        requests.mark(Change::Resources);
        requests.mark(Change::Shaders);
        requests.mark(Change::Resources);
        requests.mark(Change::Shaders);

        const Requests::Application application = requests.take(true);
        CHECK(application.mReloadShaders);
        // The reload reconciles the resources after the programs, so a reconcile of its own would be a second one.
        CHECK_FALSE(application.mReconcile);
        CHECK(application.mChanges == 4);

        CHECK(idle(requests.take(true)));
    }

    TEST_CASE("alrenderinputrequests: resource changes that leave the inputs as they were reconcile nothing",
              "[alrenderinputrequests]")
    {
        // A toggle and its undo in one frame.
        Requests requests;
        requests.mark(Change::Resources);
        requests.mark(Change::Resources);

        const Requests::Application application = requests.take(false);
        CHECK_FALSE(application.mReconcile);
        CHECK_FALSE(application.mReloadShaders);
        CHECK(application.mChanges == 2);
        CHECK_FALSE(requests.pending());
    }

    TEST_CASE("alrenderinputrequests: a shader change reloads whatever the resource inputs say",
              "[alrenderinputrequests]")
    {
        Requests requests;
        requests.mark(Change::Shaders);
        CHECK(requests.take(false).mReloadShaders);
    }

    TEST_CASE("alrenderinputrequests: a resize leaves its frame undrawn and reconciles only a new size",
              "[alrenderinputrequests]")
    {
        Requests requests;

        SECTION("to a new size")
        {
            requests.mark(Change::Window);
            CHECK(requests.windowResized());
            const Requests::Application application = requests.take(true);
            CHECK(application.mSkipFrame);
            CHECK(application.mReconcile);
            CHECK_FALSE(application.mReloadShaders);
        }

        SECTION("to the size it had")
        {
            requests.mark(Change::Window);
            const Requests::Application application = requests.take(false);
            CHECK(application.mSkipFrame);
            CHECK_FALSE(application.mReconcile);
        }

        SECTION("with a shader change")
        {
            requests.mark(Change::Window);
            requests.mark(Change::Shaders);
            const Requests::Application application = requests.take(true);
            CHECK(application.mSkipFrame);
            CHECK(application.mReloadShaders);
            CHECK_FALSE(application.mReconcile);
        }

        CHECK_FALSE(requests.windowResized());
    }

    TEST_CASE("alrenderinputrequests: a change marked while an application runs is the next one's",
              "[alrenderinputrequests]")
    {
        Requests requests;
        requests.mark(Change::Shaders);
        const Requests::Application first = requests.take(true);
        CHECK(first.mReloadShaders);

        // The reload sets a setting whose listener marks again.
        requests.mark(Change::Resources);
        CHECK(requests.pending());

        const Requests::Application second = requests.take(true);
        CHECK(second.mReconcile);
        CHECK_FALSE(second.mReloadShaders);
        CHECK(second.mChanges == 1);
    }

    TEST_CASE("alrenderinputrequests: a reload outside an application applies the shader and resource changes before it",
              "[alrenderinputrequests]")
    {
        // Settings changed before the pipeline's first load, which builds everything from them.
        Requests requests;
        requests.mark(Change::Shaders);
        requests.mark(Change::Resources);
        requests.reloadBegins();
        requests.shadersReloaded();

        CHECK_FALSE(requests.pending());
        CHECK(idle(requests.take(true)));
    }

    TEST_CASE("alrenderinputrequests: a change marked while a reload outside an application runs is the next one's",
              "[alrenderinputrequests]")
    {
        // A menu reloads the programs, and a setting the reload sets marks again before the reconcile that ends it.
        Requests requests;
        requests.mark(Change::Shaders);
        requests.mark(Change::Resources);
        requests.reloadBegins();
        requests.mark(Change::Resources);
        requests.mark(Change::Shaders);
        requests.shadersReloaded();

        CHECK(requests.pending());
        const Requests::Application application = requests.take(true);
        CHECK(application.mReloadShaders);
        CHECK(application.mChanges == 2);
        CHECK(idle(requests.take(true)));
    }

    TEST_CASE("alrenderinputrequests: a change marked while a reload inside an application runs is the next one's",
              "[alrenderinputrequests]")
    {
        Requests requests;
        requests.mark(Change::Shaders);
        const Requests::Application first = requests.take(true);
        REQUIRE(first.mReloadShaders);

        // The application's reload sets a setting whose listener marks again.
        requests.reloadBegins();
        requests.mark(Change::Resources);
        requests.shadersReloaded();

        CHECK(requests.pending());
        const Requests::Application second = requests.take(true);
        CHECK(second.mReconcile);
        CHECK_FALSE(second.mReloadShaders);
        CHECK(second.mChanges == 1);
    }

    TEST_CASE("alrenderinputrequests: a reload outside an application leaves a resize's frame undrawn",
              "[alrenderinputrequests]")
    {
        // A restored context reloads the programs, and the window it restores into was resized.
        Requests requests;
        requests.mark(Change::Window);
        requests.mark(Change::Shaders);
        requests.reloadBegins();
        requests.shadersReloaded();

        CHECK(requests.windowResized());
        const Requests::Application application = requests.take(false);
        CHECK(application.mSkipFrame);
        CHECK_FALSE(application.mReloadShaders);
        CHECK_FALSE(application.mReconcile);
        CHECK(application.mChanges == 1);
    }
}
