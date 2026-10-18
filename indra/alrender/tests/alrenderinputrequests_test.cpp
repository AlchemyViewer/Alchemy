/**
 * @file alrenderinputrequests_test.cpp
 * @brief What the render inputs' changes since they were last applied ask of the next application, and what it does.
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
    using Requests    = ALRenderInputRequests;
    using Change      = Requests::Change;
    using Application = ALRenderApplication;
    using Programs    = Application::Programs;

    TEST_CASE("alrenderinputrequests: nothing marked applies nothing", "[alrenderinputrequests]")
    {
        Requests requests;
        CHECK_FALSE(requests.pending());
        CHECK_FALSE(requests.windowResized());
        CHECK_FALSE(requests.take().has_value());
    }

    TEST_CASE("alrenderinputrequests: a batch of changes is one application", "[alrenderinputrequests]")
    {
        // A preset: the antialiasing, the bloom, the shadow detail and the probes in one frame.
        Requests requests;
        for (int i = 0; i < 12; ++i)
        {
            requests.mark(Change::Inputs);
        }
        CHECK(requests.pending());

        const std::optional<Requests::Application> application = requests.take();
        REQUIRE(application.has_value());
        CHECK_FALSE(application->mRebuildPrograms);
        CHECK_FALSE(application->mSkipFrame);
        CHECK(application->mChanges == 12);

        CHECK_FALSE(requests.pending());
        CHECK_FALSE(requests.take().has_value());
    }

    TEST_CASE("alrenderinputrequests: a resize leaves its frame undrawn", "[alrenderinputrequests]")
    {
        Requests requests;
        requests.mark(Change::Window);
        CHECK(requests.windowResized());
        const std::optional<Requests::Application> application = requests.take();
        REQUIRE(application.has_value());
        CHECK(application->mSkipFrame);
        CHECK_FALSE(requests.windowResized());
    }

    TEST_CASE("alrenderinputrequests: a purged binary cache rebuilds every program", "[alrenderinputrequests]")
    {
        Requests requests;
        requests.mark(Change::Inputs);
        requests.mark(Change::Programs);
        const std::optional<Requests::Application> application = requests.take();
        REQUIRE(application.has_value());
        CHECK(application->mRebuildPrograms);
        CHECK(application->mChanges == 2);
    }

    TEST_CASE("alrenderinputrequests: a change marked while an application runs is the next one's",
              "[alrenderinputrequests]")
    {
        Requests requests;
        requests.mark(Change::Programs);
        REQUIRE(requests.take().has_value());

        // The application's reload sets a setting whose listener marks again.
        requests.mark(Change::Inputs);
        const std::optional<Requests::Application> second = requests.take();
        REQUIRE(second.has_value());
        CHECK_FALSE(second->mRebuildPrograms);
        CHECK(second->mChanges == 1);
    }

    TEST_CASE("alrenderinputrequests: a snapshot holds every application until it ends, and its end marks the change back",
              "[alrenderinputrequests]")
    {
        Requests requests;
        requests.overrideBegins();
        CHECK(requests.overriding());

        // A setting changed, and the window resized, while the snapshot renders: both wait.
        requests.mark(Change::Inputs);
        requests.mark(Change::Window);
        CHECK_FALSE(requests.pending());
        CHECK_FALSE(requests.windowResized());
        CHECK_FALSE(requests.take().has_value());

        requests.overrideEnds();
        CHECK_FALSE(requests.overriding());
        CHECK(requests.windowResized());
        const std::optional<Requests::Application> application = requests.take();
        REQUIRE(application.has_value());
        CHECK(application->mSkipFrame);
        CHECK(application->mChanges == 3);
    }

    TEST_CASE("alrenderinputrequests: snapshots back to back leave one application after the last",
              "[alrenderinputrequests]")
    {
        // A 360 capture: six faces, one snapshot each. Each face's display() applies nothing while its snapshot holds
        // the size, and the end of one marks the size back without reconciling to it.
        Requests requests;
        for (int face = 0; face < 6; ++face)
        {
            requests.overrideBegins();
            CHECK_FALSE(requests.take().has_value());
            requests.overrideEnds();
            if (face < 5)
            {
                // Between faces nothing runs the frame's application.
                CHECK(requests.pending());
            }
        }
        const std::optional<Requests::Application> application = requests.take();
        REQUIRE(application.has_value());
        CHECK(application->mChanges == 6);
        CHECK_FALSE(requests.take().has_value());
    }

    // The reference: what the viewer loads and builds at defaults.
    ALShaderInputs shaders()
    {
        return ALShaderInputs::make(ALShaderSettings{});
    }

    ALRenderResourceInputs resources()
    {
        ALRenderResourceInputs in;
        in.mWidth  = 2560;
        in.mHeight = 1440;
        return in;
    }

    TEST_CASE("alrenderapplication: inputs that did not move apply nothing", "[alrenderinputrequests]")
    {
        const Application application = Application::make(false, shaders(), shaders(), resources(), resources());
        CHECK(application.idle());
        CHECK(application.mPrograms == Programs::Kept);
        CHECK_FALSE(application.mSharedObjects);
        CHECK_FALSE(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: a local shader input reloads its programs and nothing else", "[alrenderinputrequests]")
    {
        ALShaderInputs latched = shaders();
        latched.mLocal.mAvatarCloth = !latched.mLocal.mAvatarCloth;
        const Application application = Application::make(false, shaders(), latched, resources(), resources());
        CHECK(application.mPrograms == Programs::Reloaded);
        CHECK_FALSE(application.mSharedObjects);
        // No reconcile follows a reload that moves no resource input.
        CHECK_FALSE(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: a global shader input compiles the shared objects again", "[alrenderinputrequests]")
    {
        ALShaderInputs latched = shaders();
        latched.mGlobal.mSSR = !latched.mGlobal.mSSR;
        const Application application = Application::make(false, shaders(), latched, resources(), resources());
        CHECK(application.mPrograms == Programs::Reloaded);
        CHECK(application.mSharedObjects);
        CHECK_FALSE(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: a resource input reconciles and leaves the programs loaded", "[alrenderinputrequests]")
    {
        ALRenderResourceInputs requested = resources();
        requested.mFSAAType = 2;
        const Application application = Application::make(false, shaders(), shaders(), resources(), requested);
        CHECK(application.mPrograms == Programs::Kept);
        CHECK(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: the depth convention reloads the programs, then reconciles", "[alrenderinputrequests]")
    {
        ALShaderInputs latched = shaders();
        latched.mGlobal.mReverseZ = !latched.mGlobal.mReverseZ;
        ALRenderResourceInputs requested = resources();
        requested.mReverseZ = latched.mGlobal.mReverseZ;
        const Application application = Application::make(false, shaders(), latched, resources(), requested);
        CHECK(application.mPrograms == Programs::Reloaded);
        CHECK(application.mSharedObjects);
        CHECK(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: a first load or a new context builds everything", "[alrenderinputrequests]")
    {
        const Application application = Application::make(false, std::nullopt, shaders(), std::nullopt, resources());
        CHECK(application.mPrograms == Programs::Rebuilt);
        CHECK(application.mSharedObjects);
        CHECK(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: a rebuild asked for with nothing moved rebuilds the programs alone",
              "[alrenderinputrequests]")
    {
        const Application application = Application::make(true, shaders(), shaders(), resources(), resources());
        CHECK(application.mPrograms == Programs::Rebuilt);
        CHECK(application.mSharedObjects);
        CHECK_FALSE(application.mReconcile);
    }

    TEST_CASE("alrenderapplication: a snapshot's end reconciles back to the window's size", "[alrenderinputrequests]")
    {
        // The snapshot reconciled the resources to its size; the window's is what the settings ask for again.
        ALRenderResourceInputs snapshot = resources();
        snapshot.mWidth  = 7680;
        snapshot.mHeight = 4320;
        const Application application = Application::make(false, shaders(), shaders(), snapshot, resources());
        CHECK(application.mPrograms == Programs::Kept);
        CHECK(application.mReconcile);
    }
}
