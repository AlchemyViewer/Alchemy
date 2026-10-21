/**
 * @file alrenderstate_gl_test.cpp
 * @brief LLRender's cached clear colour, scissor box, patch size, polygon mode, cull face, point size,
 *        stencil state, blend equation and viewport on the hidden window: what a clear fills with and
 *        cuts to, what GL holds after refreshState, what a change flushes, and what LLGLSScissor and
 *        ALGLStencil hand back; the depth convention, a synchronous read, a copy from the window, and
 *        the driver's own strings.
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
#include "../llrender2dutils.h"
#include "../llrendertarget.h"
#include "../llvertexbuffer.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <list>

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

        // The first value GL holds for `pname`. Room for four: GL_POLYGON_MODE answers with two on some drivers.
        GLint glInteger(GLenum pname)
        {
            GLint value[4] = { -1, -1, -1, -1 };
            glGetIntegerv(pname, value);
            return value[0];
        }

        F32 glFloat(GLenum pname)
        {
            GLfloat value[4] = { -1.f, -1.f, -1.f, -1.f };
            glGetFloatv(pname, value);
            return value[0];
        }

        bool glViewportIs(S32 x, S32 y, S32 width, S32 height)
        {
            GLint value[4] = { -1, -1, -1, -1 };
            glGetIntegerv(GL_VIEWPORT, value);
            return value[0] == x && value[1] == y && value[2] == width && value[3] == height;
        }

        bool viewportIs(S32 x, S32 y, S32 width, S32 height)
        {
            S32 box[4];
            gGL.getViewport(box);
            return box[0] == x && box[1] == y && box[2] == width && box[3] == height;
        }

        // Whether `change` draws what gGL has batched: one triangle is batched into a list, which holds it once it
        // is drawn.
        template <typename Change> bool drawsBatched(Change&& change)
        {
            gUIProgram.bind();
            std::list<LLVertexBufferData> drawn;
            gGL.beginList(&drawn);
            gGL.begin(LLRender::TRIANGLES);
            gGL.vertex3f(0.f, 0.f, 0.f);
            gGL.vertex3f(1.f, 0.f, 0.f);
            gGL.vertex3f(0.f, 1.f, 0.f);
            gGL.end();
            change();
            const bool drew = !drawn.empty();
            gGL.endList();
            return drew;
        }

        // GL's stencil state, both faces, against what is given. The masks are held to the 8 bits a stencil buffer has:
        // past them, what glGetIntegerv answers for an unsigned mask is the driver's choice. GL answers the reference
        // held to the stencil bits of the draw framebuffer, which Mesa takes as of the last draw or clear into it, not
        // its bind: a check of one above 0 binds a target with stencil and clears it first.
        void checkStencil(GLenum func, GLint ref, GLuint read_mask, GLenum fail, GLenum depth_fail, GLenum pass,
                          GLuint write_mask)
        {
            CHECK((U32)glInteger(GL_STENCIL_FUNC) == func);
            CHECK(glInteger(GL_STENCIL_REF) == ref);
            CHECK(((U32)glInteger(GL_STENCIL_VALUE_MASK) & 0xFF) == (read_mask & 0xFF));
            CHECK((U32)glInteger(GL_STENCIL_FAIL) == fail);
            CHECK((U32)glInteger(GL_STENCIL_PASS_DEPTH_FAIL) == depth_fail);
            CHECK((U32)glInteger(GL_STENCIL_PASS_DEPTH_PASS) == pass);
            CHECK(((U32)glInteger(GL_STENCIL_WRITEMASK) & 0xFF) == (write_mask & 0xFF));
            CHECK((U32)glInteger(GL_STENCIL_BACK_FUNC) == func);
            CHECK(glInteger(GL_STENCIL_BACK_REF) == ref);
            CHECK(((U32)glInteger(GL_STENCIL_BACK_VALUE_MASK) & 0xFF) == (read_mask & 0xFF));
            CHECK((U32)glInteger(GL_STENCIL_BACK_FAIL) == fail);
            CHECK((U32)glInteger(GL_STENCIL_BACK_PASS_DEPTH_FAIL) == depth_fail);
            CHECK((U32)glInteger(GL_STENCIL_BACK_PASS_DEPTH_PASS) == pass);
            CHECK(((U32)glInteger(GL_STENCIL_BACK_WRITEMASK) & 0xFF) == (write_mask & 0xFF));
        }

        // gGL's stencil state against what is given.
        void checkCachedStencil(U32 func, S32 ref, U32 read_mask, U32 fail, U32 depth_fail, U32 pass, U32 write_mask)
        {
            U32 cached_func = 0, cached_read_mask = 0, cached_fail = 0, cached_depth_fail = 0, cached_pass = 0;
            S32 cached_ref = -1;
            gGL.getStencilFunc(cached_func, cached_ref, cached_read_mask);
            gGL.getStencilOp(cached_fail, cached_depth_fail, cached_pass);
            CHECK(cached_func == func);
            CHECK(cached_ref == ref);
            CHECK(cached_read_mask == read_mask);
            CHECK(cached_fail == fail);
            CHECK(cached_depth_fail == depth_fail);
            CHECK(cached_pass == pass);
            CHECK(gGL.getStencilMask() == write_mask);
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

        // The target, with depth and stencil where `stencil`.
        void setUp(bool stencil = false)
        {
            REQUIRE(mTarget.allocate(SIZE, SIZE, GL_RGBA8, stencil, stencil));
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

    // The polygon mode, cull face, point size, stencil state and blend equation: what gGL is given GL holds, a set
    // the cache agrees with issues nothing, and refreshState puts back what gGL holds.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: the raster state gGL is given is issued, kept and put back by refreshState", "[alrenderstate_gl]")
    {
        const auto set = []
        {
            gGL.setPolygonMode(GL_LINE);
            gGL.setCullFace(GL_FRONT);
            gGL.setPointSize(4.f);
            gGL.setStencilFunc(GL_EQUAL, 3, 0x0F);
            gGL.setStencilOp(GL_ZERO, GL_INCR, GL_REPLACE);
            gGL.setStencilMask(0xF0);
            gGL.setBlendEquation(GL_FUNC_ADD, GL_MAX);
        };
        const auto check_set = []
        {
            CHECK((U32)glInteger(GL_POLYGON_MODE) == (U32)GL_LINE);
            CHECK((U32)glInteger(GL_CULL_FACE_MODE) == (U32)GL_FRONT);
            CHECK(glFloat(GL_POINT_SIZE) == 4.f);
            checkStencil(GL_EQUAL, 3, 0x0F, GL_ZERO, GL_INCR, GL_REPLACE, 0xF0);
            CHECK((U32)glInteger(GL_BLEND_EQUATION_RGB) == (U32)GL_FUNC_ADD);
            CHECK((U32)glInteger(GL_BLEND_EQUATION_ALPHA) == (U32)GL_MAX);
        };

        setUp(true);
        mTarget.bindTarget();
        mTarget.clear();
        set();
        {
            INFO("what gGL is given, GL holds");
            check_set();
        }
        CHECK(gGL.getPolygonMode() == (U32)GL_LINE);
        CHECK(gGL.getCullFace() == (U32)GL_FRONT);
        CHECK(gGL.getPointSize() == 4.f);
        checkCachedStencil(GL_EQUAL, 3, 0x0F, GL_ZERO, GL_INCR, GL_REPLACE, 0xF0);
        U32 rgb = 0, alpha = 0;
        gGL.getBlendEquation(rgb, alpha);
        CHECK(rgb == (U32)GL_FUNC_ADD);
        CHECK(alpha == (U32)GL_MAX);

        // Behind gGL's back, then the same asked for again.
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glCullFace(GL_BACK);
        glPointSize(1.f);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glStencilMask(0xFF);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        set();
        {
            INFO("an agreeing cache issues nothing");
            CHECK((U32)glInteger(GL_POLYGON_MODE) == (U32)GL_FILL);
            CHECK((U32)glInteger(GL_CULL_FACE_MODE) == (U32)GL_BACK);
            CHECK(glFloat(GL_POINT_SIZE) == 1.f);
            checkStencil(GL_ALWAYS, 0, 0xFF, GL_KEEP, GL_KEEP, GL_KEEP, 0xFF);
            CHECK((U32)glInteger(GL_BLEND_EQUATION_ALPHA) == (U32)GL_FUNC_ADD);
        }

        gGL.refreshState();
        {
            INFO("refreshState puts back what gGL holds");
            check_set();
        }
        mTarget.flush();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A change of any of it draws what gGL has batched under the state before; a set the cache agrees with leaves the
    // batch where it is.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: a change of raster state draws what is batched first, and a redundant set does not", "[alrenderstate_gl]")
    {
        gGL.setViewport(0, 0, 8, 8);
        CHECK_FALSE(drawsBatched([] { gGL.setPolygonMode(gGL.getPolygonMode()); }));
        CHECK(drawsBatched([] { gGL.setPolygonMode(GL_LINE); }));
        CHECK_FALSE(drawsBatched([] { gGL.setCullFace(gGL.getCullFace()); }));
        CHECK(drawsBatched([] { gGL.setCullFace(GL_FRONT); }));
        CHECK_FALSE(drawsBatched([] { gGL.setPointSize(gGL.getPointSize()); }));
        CHECK(drawsBatched([] { gGL.setPointSize(3.f); }));
        CHECK_FALSE(drawsBatched([] { gGL.setStencilFunc(GL_ALWAYS, 0, ~0u); }));
        CHECK(drawsBatched([] { gGL.setStencilFunc(GL_EQUAL, 1, 0xFF); }));
        CHECK_FALSE(drawsBatched([] { gGL.setStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); }));
        CHECK(drawsBatched([] { gGL.setStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); }));
        CHECK_FALSE(drawsBatched([] { gGL.setStencilMask(~0u); }));
        CHECK(drawsBatched([] { gGL.setStencilMask(0x0F); }));
        CHECK_FALSE(drawsBatched([] { gGL.setBlendEquation(GL_FUNC_ADD, GL_FUNC_ADD); }));
        CHECK(drawsBatched([] { gGL.setBlendEquation(GL_FUNC_ADD, GL_MAX); }));
        CHECK_FALSE(drawsBatched([] { gGL.setViewport(0, 0, 8, 8); }));
        CHECK(drawsBatched([] { gGL.setViewport(0, 0, 4, 4); }));
        U8 rgba[4] = { 0 };
        CHECK(drawsBatched([&] { gGL.readPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba); }));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // Every viewport goes through gGL, a target's included: binding one sets the target's size, and flushing the
    // last puts back the window's, gGLViewport. refreshState puts back the one gGL holds.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: a target's bind and flush set the viewport through gGL", "[alrenderstate_gl]")
    {
        setUp();
        gGL.setViewport(1, 2, 3, 4);
        UNSCOPED_INFO("what gGL is given, GL holds");
        CHECK(glViewportIs(1, 2, 3, 4));

        mTarget.bindTarget();
        UNSCOPED_INFO("a bound target's size");
        CHECK(viewportIs(0, 0, SIZE, SIZE));
        CHECK(glViewportIs(0, 0, SIZE, SIZE));
        mTarget.flush();
        UNSCOPED_INFO("the window's, once no target is bound");
        CHECK(viewportIs(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]));
        CHECK(glViewportIs(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]));

        glViewport(0, 0, 1, 1);
        gGL.setViewport(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]);
        UNSCOPED_INFO("an agreeing cache issues nothing");
        CHECK(glViewportIs(0, 0, 1, 1));
        gGL.refreshState();
        UNSCOPED_INFO("refreshState puts back what gGL holds");
        CHECK(glViewportIs(gGLViewport[0], gGLViewport[1], gGLViewport[2], gGLViewport[3]));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // ALGLStencil turns the test on with what it is given, through gGL, and on exit turns it off with the rest at GL's
    // defaults.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: ALGLStencil sets its state through gGL and leaves GL's defaults", "[alrenderstate_gl]")
    {
        setUp(true);
        mTarget.bindTarget();
        mTarget.clear();
        {
            ALGLStencil stencil(GL_EQUAL, 3, 0x0F, GL_KEEP, GL_INCR, GL_REPLACE, 0xF0);
            UNSCOPED_INFO("the test is on in the scope");
            CHECK(glIsEnabled(GL_STENCIL_TEST) == GL_TRUE);
            checkCachedStencil(GL_EQUAL, 3, 0x0F, GL_KEEP, GL_INCR, GL_REPLACE, 0xF0);
            checkStencil(GL_EQUAL, 3, 0x0F, GL_KEEP, GL_INCR, GL_REPLACE, 0xF0);
        }
        UNSCOPED_INFO("the test is off again");
        CHECK(glIsEnabled(GL_STENCIL_TEST) == GL_FALSE);
        checkCachedStencil(GL_ALWAYS, 0, ~0u, GL_KEEP, GL_KEEP, GL_KEEP, ~0u);
        checkStencil(GL_ALWAYS, 0, ~0u, GL_KEEP, GL_KEEP, GL_KEEP, ~0u);
        mTarget.flush();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A clear writes the stencil only through the write mask gGL holds.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: a stencil clear writes only the bits of gGL's write mask", "[alrenderstate_gl]")
    {
        LLRenderTarget stenciled;
        REQUIRE(stenciled.allocate(SIZE, SIZE, GL_RGBA8, true, true));
        stenciled.bindTarget();
        glClearStencil(0xFF);
        gGL.setStencilMask(0x0F);
        stenciled.clear(GL_STENCIL_BUFFER_BIT);
        U8 value = 0;
        gGL.readPixels(1, 1, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &value);
        CHECK((U32)value == 0x0Fu);

        gGL.setStencilMask(~0u);
        glClearStencil(0);
        stenciled.flush();
        stenciled.release();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The clip control and depth clear value follow sReverseZ, issued by applyDepthConvention.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: applyDepthConvention issues the clip depth and depth clear of sReverseZ", "[alrenderstate_gl]")
    {
        for (bool reverse : { true, false, true })
        {
            CAPTURE(reverse);
            LLRender::sReverseZ = reverse;
            LLRender::applyDepthConvention();
            GLdouble clear_depth = -1.0;
            glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clear_depth);
            CHECK(clear_depth == (reverse ? 0.0 : 1.0));
            if (gGLManager.mHasClipControl)
            {
                CHECK((U32)glInteger(GL_CLIP_DEPTH_MODE) == (U32)(reverse ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE));
                CHECK((U32)glInteger(GL_CLIP_ORIGIN) == (U32)GL_LOWER_LEFT);
            }
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // readPixels reads the framebuffer bound for reading.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: readPixels reads the bound framebuffer", "[alrenderstate_gl]")
    {
        setUp();
        mTarget.bindTarget();
        gGL.setClearColor(LLColor4::red);
        mTarget.clear();
        U8 rgba[4] = { 0 };
        gGL.readPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        CHECK(LLColor4U(rgba[0], rgba[1], rgba[2], rgba[3]) == RED);
        mTarget.flush();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // copyFromWindow copies from the window's framebuffer whatever is bound, and leaves bound what was.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: copyFromWindow copies the window's framebuffer into attachment 0", "[alrenderstate_gl]")
    {
        setUp();
        LLRenderTarget bound;
        REQUIRE(bound.allocate(SIZE, SIZE, GL_RGBA8));
        // Every target red, the window green.
        mTarget.bindTarget();
        gGL.setClearColor(LLColor4::red);
        mTarget.clear();
        mTarget.flush();
        gGL.setClearColor(LLColor4::green);
        gGL.clear(GL_COLOR_BUFFER_BIT);
        bound.bindTarget();
        gGL.setClearColor(LLColor4::red);
        bound.clear();

        mTarget.copyFromWindow(0, 0, SIZE, SIZE);
        GLint draw = -1, read = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        UNSCOPED_INFO("what was bound stays bound");
        CHECK((U32)draw == LLRenderTarget::sCurFBO);
        CHECK((U32)read == LLRenderTarget::sCurFBO);
        bound.flush();
        bound.release();

        mTarget.bindTarget();
        UNSCOPED_INFO("the window's colour, not the bound target's");
        CHECK(pixel(1, 1) == GREEN);
        CHECK(pixel(SIZE - 1, SIZE - 1) == GREEN);
        mTarget.flush();
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The driver's own vendor and renderer strings, which the uppercased ones are made from.
    TEST_CASE_METHOD(alrenderstate_data, "alrenderstate_gl: gGLManager keeps GL_VENDOR and GL_RENDERER in the driver's case", "[alrenderstate_gl]")
    {
        const std::string vendor   = ll_safe_string((const char*)glGetString(GL_VENDOR));
        const std::string renderer = ll_safe_string((const char*)glGetString(GL_RENDERER));
        CHECK(gGLManager.mGLVendorString == vendor);
        CHECK(gGLManager.mGLRendererString == renderer);
        std::string upper = renderer;
        LLStringUtil::toUpper(upper);
        CHECK(gGLManager.mGLRenderer == upper);
        CHECK_FALSE(gGLManager.mGLVersionString.empty());
    }
}
