/**
 * @file llglfloor_test.cpp
 * @brief The GL floor's version test and the Intel drivers held below it, without a context.
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

#include "../llgl.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // The version as initGL assembles it from GL_VERSION's major and minor.
    F32 assembled_version(S32 major, S32 minor)
    {
        return major + minor * .1f;
    }

    // Puts back the two fields isIntelLegacy reads, however the case ends.
    struct ManagerFieldsRestore
    {
        bool mIsIntel  = gGLManager.mIsIntel;
        F32  mGLVersion = gGLManager.mGLVersion;
        ~ManagerFieldsRestore()
        {
            gGLManager.mIsIntel   = mIsIntel;
            gGLManager.mGLVersion = mGLVersion;
        }
    };

    bool intel_legacy(bool intel, S32 major, S32 minor)
    {
        gGLManager.mIsIntel   = intel;
        gGLManager.mGLVersion = assembled_version(major, minor);
        return gGLManager.isIntelLegacy();
    }

    TEST_CASE("llglfloor: the floor takes every 4.x from 4.1 and refuses 4.0 and below", "[llglfloor]")
    {
        for (S32 minor = 1; minor <= 6; ++minor)
        {
            CAPTURE(minor);
            CHECK(LLGLManager::meetsVersionFloor(assembled_version(4, minor)));
        }
        CHECK_FALSE(LLGLManager::meetsVersionFloor(assembled_version(4, 0)));
        CHECK_FALSE(LLGLManager::meetsVersionFloor(assembled_version(3, 3)));
        CHECK_FALSE(LLGLManager::meetsVersionFloor(assembled_version(3, 0)));
        CHECK_FALSE(LLGLManager::meetsVersionFloor(assembled_version(2, 1)));
    }

    TEST_CASE("llglfloor: Intel short of 4.6 is held outside macOS and nothing else is", "[llglfloor]")
    {
        ManagerFieldsRestore restore;
#if LL_DARWIN
        CHECK_FALSE(intel_legacy(true, 4, 1));
#else
        CHECK(intel_legacy(true, 4, 1));
        CHECK(intel_legacy(true, 4, 3));
        CHECK(intel_legacy(true, 4, 5));
#endif
        CHECK_FALSE(intel_legacy(true, 4, 6));
        CHECK_FALSE(intel_legacy(false, 4, 1));
        CHECK_FALSE(intel_legacy(false, 4, 5));
    }
}
