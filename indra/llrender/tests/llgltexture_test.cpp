/**
 * @file llgltexture_test.cpp
 * @brief Tests for textures without a GL image.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

#include "../llgltexture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("LLGLTexture: a texture not yet generated has no GL image", "[LLGLTexture]")
    {
        LLPointer<LLGLTexture> texture = new LLGLTexture();
        UNSCOPED_INFO("an uninitialized texture has no GL image");
        CHECK(texture->getGLTexture() == nullptr);
        UNSCOPED_INFO("an uninitialized texture has no GPU storage");
        CHECK_FALSE(texture->hasGLTexture());
    }
}
