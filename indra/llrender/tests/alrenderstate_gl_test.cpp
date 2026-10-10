/**
 * @file alrenderstate_gl_test.cpp
 * @brief LLRender's cached clear colour and scissor box on the hidden window: what a clear fills
 *        with and cuts to, what GL holds after refreshState, and what LLGLSScissor hands back.
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

#include "../test/lltut.h"

namespace tut
{
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
            ensure("target allocates", mTarget.allocate(SIZE, SIZE, GL_RGBA8));
        }

        ll_test::SharedGLScope mGL;
        LLRenderTarget mTarget;
    };

    typedef test_group<alrenderstate_data> alrenderstate_t;
    typedef alrenderstate_t::object alrenderstate_object_t;
    tut::alrenderstate_t tut_alrenderstate("alrenderstate_gl");
    }

    // The clear colour a target's clear fills with is the one gGL was given, and GL holds what gGL says it does.
    template<> template<>
    void alrenderstate_object_t::test<1>()
    {
        setUp();
        mTarget.bindTarget();
        gGL.setClearColor(LLColor4::red);
        ensure("GL holds the cached colour", glClearValue() == gGL.getClearColor());
        mTarget.clear();
        ensure_equals("cleared red", pixel(1, 1), RED);
        gGL.setClearColor(LLColor4::green);
        mTarget.clear();
        ensure_equals("cleared green", pixel(1, 1), GREEN);
        mTarget.flush();
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }

    // refreshState is for state changed behind gGL's back: it puts back what gGL holds, where the setters would see
    // their cache agreeing and issue nothing.
    template<> template<>
    void alrenderstate_object_t::test<2>()
    {
        gGL.setClearColor(LLColor4::green);
        gGL.setScissor(1, 1, 2, 2);

        glClearColor(1.f, 0.f, 0.f, 1.f);
        glScissor(0, 0, 1, 1);
        gGL.setClearColor(LLColor4::green);
        gGL.setScissor(1, 1, 2, 2);
        ensure("an agreeing cache issues nothing", glClearValue() == LLColor4::red);

        gGL.refreshState();
        ensure("refreshState restores the clear colour", glClearValue() == LLColor4::green);
        S32 box[4];
        glScissorBox(box);
        ensure("refreshState restores the scissor box", box[0] == 1 && box[1] == 1 && box[2] == 2 && box[3] == 2);
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }

    // LLGLSScissor cuts a clear to its box with the test on, and hands back the box and the test it found, nested
    // scopes included.
    template<> template<>
    void alrenderstate_object_t::test<3>()
    {
        setUp();
        mTarget.bindTarget();
        gGL.setScissor(0, 0, SIZE, SIZE);
        gGL.setClearColor(LLColor4::red);
        mTarget.clear();

        {
            LLGLSScissor left(0, 0, SIZE / 2, SIZE);
            ensure("the test is on in the scope", glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
            gGL.setClearColor(LLColor4::green);
            mTarget.clear();
            {
                LLGLSScissor corner(0, 0, 1, 1);
                S32 box[4];
                gGL.getScissor(box);
                ensure("the inner scope's box", box[0] == 0 && box[1] == 0 && box[2] == 1 && box[3] == 1);
            }
            S32 box[4];
            glScissorBox(box);
            ensure("the outer box is back", box[0] == 0 && box[1] == 0 && box[2] == (S32)SIZE / 2 && box[3] == (S32)SIZE);
            ensure("the test is still on", glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
        }

        ensure_equals("inside the box: cleared green", pixel(0, 1), GREEN);
        ensure_equals("outside the box: still red", pixel(SIZE - 1, 1), RED);
        ensure("the test is off again", glIsEnabled(GL_SCISSOR_TEST) == GL_FALSE);
        S32 box[4];
        glScissorBox(box);
        ensure("the first box is back", box[0] == 0 && box[1] == 0 && box[2] == (S32)SIZE && box[3] == (S32)SIZE);
        mTarget.flush();
        ensure_equals("no GL error", (U32)glGetError(), (U32)GL_NO_ERROR);
    }
}
