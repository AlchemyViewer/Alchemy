/**
 * @file llglerrors_gl_test.cpp
 * @brief Reading a context's GL errors: every flag taken, and running out of video memory noted
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
#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

namespace
{
    struct llglerrors_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
    };

    // A read takes every flag the context has set, whichever comes first, and leaves none behind; a driver may keep only
    // the first error until it is read, so two errors set may be one flag. Errors that are not GL_OUT_OF_MEMORY raise
    // nothing for take_gl_out_of_memory, and with none set a read says GL_NO_ERROR.
    TEST_CASE_METHOD(llglerrors_data, "llglerrors: a read takes every flag and notes no running out where there was none", "[llglerrors]")
    {
        take_gl_out_of_memory();
        REQUIRE((U32)glGetError() == (U32)GL_NO_ERROR);

        glEnable(0x7fff);   // GL_INVALID_ENUM
        glLineWidth(-1.f);  // GL_INVALID_VALUE
        const GLenum error = read_gl_errors();
        UNSCOPED_INFO("the first of the two, whichever GL keeps first");
        CHECK((error == GL_INVALID_ENUM || error == GL_INVALID_VALUE));
        UNSCOPED_INFO("none left behind");
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
        CHECK_FALSE(take_gl_out_of_memory());

        UNSCOPED_INFO("nothing set");
        CHECK((U32)read_gl_errors() == (U32)GL_NO_ERROR);

        glEnable(0x7fff);
        clear_glerror();
        UNSCOPED_INFO("clear_glerror takes them too");
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
        CHECK_FALSE(take_gl_out_of_memory());
    }
}
