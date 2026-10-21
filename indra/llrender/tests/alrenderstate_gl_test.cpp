/**
 * @file alrenderstate_gl_test.cpp
 * @brief LLRender's cached clear colour, scissor box and patch size on the hidden window: what a
 *        clear fills with and cuts to, what GL holds after refreshState, and what LLGLSScissor
 *        hands back.
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
#include "../llglstates.h"
#include "../llrender.h"
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
        constexpr U32 SIZE = 4;

        const LLColor4U RED(255, 0, 0, 255);
        const LLColor4U GREEN(0, 255, 0, 255);

        LLColor4U pixel(S32 x, S32 y)
        {
            U8 rgba[4] = { 0 };
            glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            return LLColor4U(rgba[0], rgba[1], rgba[2], rgba[3]);
        }

        LLColor4 glClearValue()
        {
            F32 value[4] = { 0.f };
            glGetFloatv(GL_COLOR_CLEAR_VALUE, value);
            return LLColor4(value[0], value[1], value[2], value[3]);
        }

        void glScissorBox(S32 (&box)[4])
        {
            GLint value[4] = { 0 };
            glGetIntegerv(GL_SCISSOR_BOX, value);
            for (U32 i = 0; i < 4; ++i)
            {
                box[i] = value[i];
            }
        }

    // The clear colour and scissor box the tests set go back with mGL.
    struct alrenderstate_data
    {
        ~alrenderstate_data()
        {
            // A failed check throws out of a test with the target still bound, and a bound target cannot be released.
            if (mTarget.isBoundInStack())
            {
                mTarget.flush();
            }
            mTarget.release();
        }

        void setUp()
        {
            REQUIRE(mTarget.allocate(SIZE, SIZE, GL_RGBA8));
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget mTarget;
    };


    // The clear colour a target's clear fills with is the one gGL was given, and GL holds what gGL says it does.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: a target's clear fills with the colour gGL was given", "[alrenderstate_gl]")
    {
        setUp();
        mTarget.bindTarget();
        gGL.setClearColor(LLColor4::red);
        CHECK(glClearValue() == gGL.getClearColor());
        mTarget.clear();
        CHECK(pixel(1, 1) == RED);
        gGL.setClearColor(LLColor4::green);
        mTarget.clear();
        CHECK(pixel(1, 1) == GREEN);
        mTarget.flush();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // refreshState is for state changed behind gGL's back: it puts back what gGL holds, where the setters would see
    // their cache agreeing and issue nothing.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: refreshState puts back the clear colour and scissor box gGL holds", "[alrenderstate_gl]")
    {
        gGL.setClearColor(LLColor4::green);
        gGL.setScissor(1, 1, 2, 2);

        glClearColor(1.f, 0.f, 0.f, 1.f);
        glScissor(0, 0, 1, 1);
        gGL.setClearColor(LLColor4::green);
        gGL.setScissor(1, 1, 2, 2);
        UNSCOPED_INFO("an agreeing cache issues nothing");
        CHECK(glClearValue() == LLColor4::red);

        gGL.refreshState();
        CHECK(glClearValue() == LLColor4::green);
        S32 box[4];
        glScissorBox(box);
        CHECK(box[0] == 1);
        CHECK(box[1] == 1);
        CHECK(box[2] == 2);
        CHECK(box[3] == 2);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // LLGLSScissor cuts a clear to its box with the test on, and hands back the box and the test it found, nested
    // scopes included.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: LLGLSScissor cuts a clear to its box and hands back what it found", "[alrenderstate_gl]")
    {
        setUp();
        mTarget.bindTarget();
        gGL.setScissor(0, 0, SIZE, SIZE);
        gGL.setClearColor(LLColor4::red);
        mTarget.clear();

        {
            LLGLSScissor left(0, 0, SIZE / 2, SIZE);
            UNSCOPED_INFO("the test is on in the scope");
            CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
            gGL.setClearColor(LLColor4::green);
            mTarget.clear();
            {
                LLGLSScissor corner(0, 0, 1, 1);
                S32 box[4];
                gGL.getScissor(box);
                {
                    INFO("the inner scope's box");
                    CHECK(box[0] == 0);
                    CHECK(box[1] == 0);
                    CHECK(box[2] == 1);
                    CHECK(box[3] == 1);
                }
            }
            S32 box[4];
            glScissorBox(box);
            {
                INFO("the outer box is back");
                CHECK(box[0] == 0);
                CHECK(box[1] == 0);
                CHECK(box[2] == (S32)SIZE / 2);
                CHECK(box[3] == (S32)SIZE);
            }
            UNSCOPED_INFO("the test is still on");
            CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
        }

        UNSCOPED_INFO("inside the box: cleared green");
        CHECK(pixel(0, 1) == GREEN);
        UNSCOPED_INFO("outside the box: still red");
        CHECK(pixel(SIZE - 1, 1) == RED);
        UNSCOPED_INFO("the test is off again");
        CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE);
        S32 box[4];
        glScissorBox(box);
        {
            INFO("the first box is back");
            CHECK(box[0] == 0);
            CHECK(box[1] == 0);
            CHECK(box[2] == (S32)SIZE);
            CHECK(box[3] == (S32)SIZE);
        }
        mTarget.flush();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // refreshState puts back the patch size too.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: refreshState puts back the patch size gGL holds", "[alrenderstate_gl]")
    {
        // Handed back however the test ends: the shared context outlives it, and its scope does not keep this.
        struct PatchSizeRestore
        {
            GLint mFound = 0;
            PatchSizeRestore() { glGetIntegerv(GL_PATCH_VERTICES, &mFound); }
            ~PatchSizeRestore()
            {
                gGL.setPatchVertices((U32)mFound);
                glPatchParameteri(GL_PATCH_VERTICES, mFound);
            }
        } restore;

        gGL.setPatchVertices(4);
        glPatchParameteri(GL_PATCH_VERTICES, 3);
        gGL.setPatchVertices(4);
        GLint patch = 0;
        glGetIntegerv(GL_PATCH_VERTICES, &patch);
        UNSCOPED_INFO("an agreeing cache issues nothing");
        CHECK((S32)patch == 3);

        gGL.refreshState();
        glGetIntegerv(GL_PATCH_VERTICES, &patch);
        CHECK((S32)patch == 4);
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
