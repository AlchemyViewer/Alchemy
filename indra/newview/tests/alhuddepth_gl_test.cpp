/**
 * @file alhuddepth_gl_test.cpp
 * @brief ALHUDDepth on the hidden window, with the viewer's own copy shader: the window's depth under the HUD
 *        cleared for it and the scene's written back after it, and the rects that bound both.
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

#include "../alhuddepth.h"

#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include "../test/lltut.h"

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace tut
{
    namespace
    {
        constexpr S32 W = 64;
        constexpr S32 H = 64;
        constexpr F32 FOV = 1.f;
        constexpr F32 NEAR_PLANE = 0.1f;
        constexpr F32 FORWARD_FAR = 100.f;
        constexpr F32 HUD_DEPTH = 10.f;

        // The scene: a world surface just past the near plane under the lower left quarter, one five metres out
        // under the lower right, and sky above.
        constexpr F32 NEAR_WORLD = NEAR_PLANE * 1.001f;
        constexpr F32 MID_WORLD = 5.f;

        // The HUD point's box covers columns and rows 16 to 47 (the rect, grown a pixel, 15 to 48); what is drawn
        // on it, columns 16 to 31, a metre into the HUD.
        const LLVector3 HUD_BOX_LO(-0.25f, -0.25f, -2.f);
        const LLVector3 HUD_BOX_HI(0.25f, 0.25f, 0.f);
        constexpr F32 HUD_Z = -1.f;

        const LLColor4 SKY(0.f, 0.f, 1.f, 1.f);
        const LLColor4 NEAR_COLOUR(1.f, 0.f, 0.f, 1.f);
        const LLColor4 MID_COLOUR(0.f, 1.f, 0.f, 1.f);
        const LLColor4 HUD_COLOUR(1.f, 1.f, 1.f, 1.f);
        const LLColor4 OVERLAY_COLOUR(1.f, 1.f, 0.f, 1.f);

        // Places quads by their eye-space corners through a projection, as the HUD and the 3D UI are drawn.
        const char* kQuadVertex =
            "#version 400\n"
            "layout(location = 0) in vec3 position;\n"
            "uniform mat4 proj;\n"
            "void main() { gl_Position = proj * vec4(position, 1.0); }\n";

        const char* kQuadFragment =
            "#version 400\n"
            "uniform vec4 color;\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = color; }\n";

        LLColor4U unorm(const LLColor4& c)
        {
            return LLColor4U((U8)ll_round(c.mV[0] * 255.f), (U8)ll_round(c.mV[1] * 255.f), (U8)ll_round(c.mV[2] * 255.f),
                             (U8)ll_round(c.mV[3] * 255.f));
        }
    }

    struct alhuddepth_data
    {
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(true, true, true, false);
            return instance;
        }

        alhuddepth_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
            gl();
        }

        ~alhuddepth_data()
        {
            tearDown();
            setConvention(false);
            glViewport(0, 0, ll_test::HeadlessGL::WIDTH, ll_test::HeadlessGL::HEIGHT);
        }

        void tearDown()
        {
            LLGLSLShader::unbind();
            glUseProgram(0);
            mBlitProgram.unload();
            mCopyDepthProgram.unload();
            mShaders.clearShaderObjects();
            if (mQuadProgram)
            {
                glDeleteProgram(mQuadProgram);
                mQuadProgram = 0;
            }
            if (mVAO)
            {
                glDeleteVertexArrays(1, &mVAO);
                mVAO = 0;
            }
            if (mVBO)
            {
                glDeleteBuffers(1, &mVBO);
                mVBO = 0;
            }
            mWindow.release();
            mScene.release();
            mTriangle = nullptr;
        }

        // Forward depth, and reverse-Z where the context can clip zero to one.
        static std::vector<bool> conventions()
        {
            std::vector<bool> reverse = { false };
            if (gGLManager.mHasClipControl)
            {
                reverse.push_back(true);
            }
            return reverse;
        }

        static std::string named(bool reverse, const std::string& what)
        {
            return what + (reverse ? " (reverse-Z)" : " (forward)");
        }

        void setConvention(bool reverse)
        {
            LLRender::sReverseZ = reverse;
            if (gGLManager.mHasClipControl)
            {
                glClipControl(GL_LOWER_LEFT, reverse ? GL_ZERO_TO_ONE : GL_NEGATIVE_ONE_TO_ONE);
            }
            glClearDepth(reverse ? 0.0 : 1.0);
            LLGLDepthTest::rebase();
        }

        // The final blit's copy (copyF with COPY_DEPTH, as blitWithEffectsF writes depth) and the restore's
        // (COPY_DEPTH and DEPTH_ONLY, as LLViewerShaderMgr builds gCopyDepthProgram), with the shared objects
        // LLViewerShaderMgr::loadBasicShaders compiles for them; a window whose depth is 24-bit, as the window's
        // framebuffer is, and a scene whose depth is the pipeline's.
        void setUp(bool reverse)
        {
            tearDown();
            mReverse = reverse;
            setConvention(reverse);

            std::map<std::string, std::string> defines;
            for (const char* object : { "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                S32 level = 1;
                ensure(std::string("compiled ") + object, mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            S32 level = 1;
            ensure("compiled deferred/globalF.glsl", mShaders.loadShaderFile("deferred/globalF.glsl", level, GL_FRAGMENT_SHADER, &defines) != 0);

            mBlitProgram.mName = "Copy Depth Blit";
            mBlitProgram.mShaderFiles.clear();
            mBlitProgram.mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
            mBlitProgram.mShaderFiles.push_back(std::make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
            mBlitProgram.clearPermutations();
            mBlitProgram.addPermutation("COPY_DEPTH", "1");
            mBlitProgram.mShaderLevel = 1;
            ensure("blit copy builds", mBlitProgram.createShader());

            mCopyDepthProgram.mName = "Copy Depth Shader";
            mCopyDepthProgram.mShaderFiles.clear();
            mCopyDepthProgram.mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
            mCopyDepthProgram.mShaderFiles.push_back(std::make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
            mCopyDepthProgram.clearPermutations();
            mCopyDepthProgram.addPermutation("COPY_DEPTH", "1");
            mCopyDepthProgram.addPermutation("DEPTH_ONLY", "1");
            mCopyDepthProgram.mShaderLevel = 1;
            ensure("depth copy builds", mCopyDepthProgram.createShader());

            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kQuadVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kQuadFragment);
            ensure("quad shaders compile", vs && fs);
            mQuadProgram = glCreateProgram();
            glAttachShader(mQuadProgram, vs);
            glAttachShader(mQuadProgram, fs);
            glLinkProgram(mQuadProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(mQuadProgram, GL_LINK_STATUS, &linked);
            ensure("quad program links", linked == GL_TRUE);
            glGenVertexArrays(1, &mVAO);
            glGenBuffers(1, &mVBO);

            ensure("window", mWindow.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                              LLRenderTarget::DEPTH_FMT_24));
            ensure("scene", mScene.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                            reverse ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24));

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            ensure("triangle", mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();

            mSceneProjection = al_perspective(FOV, 1.f, NEAR_PLANE, reverse ? std::numeric_limits<F32>::infinity() : FORWARD_FAR);

            // As get_hud_matrices builds it: a unit-high ortho over the HUD's depth, its depth row flattened.
            mHUDProjection = al_ortho(-0.5f, 0.5f, -0.5f, 0.5f, 0.f, HUD_DEPTH);
            mHUDProjection.mMatrix[2].getF32ptr()[2] = reverse ? 0.005f : -0.01f;

            mRects.clear();
            ALHUDDepth::mergeRect(mRects, ALHUDDepth::boxRect(mHUDProjection, HUD_BOX_LO, HUD_BOX_HI, W, H));
            drawScene();
        }

        void quad(const LLMatrix4a& proj, const LLVector3 corners[4], const LLColor4& colour)
        {
            const F32 v[18] = { corners[0].mV[0], corners[0].mV[1], corners[0].mV[2], corners[1].mV[0], corners[1].mV[1], corners[1].mV[2],
                                corners[2].mV[0], corners[2].mV[1], corners[2].mV[2], corners[0].mV[0], corners[0].mV[1], corners[0].mV[2],
                                corners[2].mV[0], corners[2].mV[1], corners[2].mV[2], corners[3].mV[0], corners[3].mV[1], corners[3].mV[2] };
            // LLRender keeps a vertex array bound for its own buffers, and LLVertexBuffer remembers which buffer is
            // bound: both are handed back as they were.
            LLGLSLShader::unbind();
            gGL.flush();
            GLint previous_vao = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
            glUseProgram(mQuadProgram);
            glUniformMatrix4fv(glGetUniformLocation(mQuadProgram, "proj"), 1, GL_FALSE, proj.getF32ptr());
            glUniform4fv(glGetUniformLocation(mQuadProgram, "color"), 1, colour.mV);
            glBindVertexArray(mVAO);
            glBindBuffer(GL_ARRAY_BUFFER, mVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STREAM_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
            glEnableVertexAttribArray(0);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glDisableVertexAttribArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray((GLuint)previous_vao);
            glUseProgram(0);
        }

        // A quad of the scene over normalized device x0..x1, y0..y1, `dist` metres out.
        void worldQuad(F32 x0, F32 x1, F32 y0, F32 y1, F32 dist, const LLColor4& colour)
        {
            const F32 sx = dist / mSceneProjection.getRow<0>()[0];
            const F32 sy = dist / mSceneProjection.getRow<1>()[1];
            const LLVector3 corners[4] = { LLVector3(x0 * sx, y0 * sy, -dist), LLVector3(x1 * sx, y0 * sy, -dist),
                                           LLVector3(x1 * sx, y1 * sy, -dist), LLVector3(x0 * sx, y1 * sy, -dist) };
            quad(mSceneProjection, corners, colour);
        }

        // A 3D UI quad over pixels (x - 2..x + 2, y - 2..y + 2), `dist` metres out, depth tested as the 3D UI is.
        void overlay(S32 x, S32 y, F32 dist)
        {
            auto ndc_x = [](S32 px) { return (F32)px / (F32)W * 2.f - 1.f; };
            auto ndc_y = [](S32 px) { return (F32)px / (F32)H * 2.f - 1.f; };
            mWindow.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
                worldQuad(ndc_x(x - 2), ndc_x(x + 3), ndc_y(y - 2), ndc_y(y + 3), dist, OVERLAY_COLOUR);
            }
            mWindow.flush();
        }

        void drawScene()
        {
            mScene.bindTarget();
            {
                LLGLSColorMask mask(true, true);
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
                gGL.setClearColor(SKY);
                mScene.clear();
                worldQuad(-1.f, 0.f, -1.f, 0.f, NEAR_WORLD, NEAR_COLOUR);
                worldQuad(0.f, 1.f, -1.f, 0.f, MID_WORLD, MID_COLOUR);
            }
            mScene.flush();
        }

        // The final blit: the scene's colour and its depth, verbatim, over the whole window.
        void blit()
        {
            mWindow.bindTarget();
            {
                LLGLSColorMask mask(true, true);
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
                LLGLDisable blend(GL_BLEND);
                mBlitProgram.bind();
                mBlitProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, &mScene, ALSamplers::PointClamp);
                mBlitProgram.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mScene);
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
                mBlitProgram.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                mBlitProgram.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
                LLGLSLShader::unbind();
            }
            mWindow.flush();
        }

        void clearUnderHUD()
        {
            mWindow.bindTarget();
            ALHUDDepth::clear(mRects, 0, 0);
            mWindow.flush();
        }

        // The HUD's quad, depth tested as the HUD draws.
        void drawHUD()
        {
            const LLVector3 corners[4] = { LLVector3(-0.25f, -0.25f, HUD_Z), LLVector3(0.f, -0.25f, HUD_Z),
                                           LLVector3(0.f, 0.25f, HUD_Z), LLVector3(-0.25f, 0.25f, HUD_Z) };
            mWindow.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
                quad(mHUDProjection, corners, HUD_COLOUR);
            }
            mWindow.flush();
        }

        void restoreUnderHUD()
        {
            mWindow.bindTarget();
            ALHUDDepth::restore(mCopyDepthProgram, mScene, *mTriangle, mRects, 0, 0);
            mWindow.flush();
        }

        LLColor4U colourAt(S32 x, S32 y)
        {
            mWindow.bindTarget();
            const std::vector<U8> px = ll_test::readFramebufferRGBA(W, H);
            mWindow.flush();
            const size_t i = ((size_t)y * W + x) * 4;
            return LLColor4U(px[i], px[i + 1], px[i + 2], px[i + 3]);
        }

        std::vector<F32> depths(LLRenderTarget& target)
        {
            std::vector<F32> d((size_t)W * H, 0.f);
            target.bindTarget();
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, d.data());
            target.flush();
            return d;
        }

        bool inRects(S32 x, S32 y) const
        {
            for (const LLRect& rect : mRects)
            {
                if (x >= rect.mLeft && x < rect.mRight && y >= rect.mBottom && y < rect.mTop)
                {
                    return true;
                }
            }
            return false;
        }

        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mBlitProgram;
        LLGLSLShader mCopyDepthProgram;
        GLuint mQuadProgram = 0;
        GLuint mVAO = 0;
        GLuint mVBO = 0;
        LLRenderTarget mWindow;
        LLRenderTarget mScene;
        LLPointer<LLVertexBuffer> mTriangle;
        LLMatrix4a mSceneProjection;
        LLMatrix4a mHUDProjection;
        std::vector<LLRect> mRects;
        bool mReverse = false;
    };

    typedef test_group<alhuddepth_data> alhuddepth_t;
    typedef alhuddepth_t::object alhuddepth_object_t;
    tut::alhuddepth_t alhuddepth_testcase("ALHUDDepth");

    // The final blit's copy fills the window's depth with the scene's, as the window's 24 bits hold it.
    template<> template<>
    void alhuddepth_object_t::test<1>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            blit();
            const std::vector<F32> scene = depths(mScene);
            const std::vector<F32> window = depths(mWindow);
            F32 worst = 0.f;
            for (size_t i = 0; i < scene.size(); ++i)
            {
                worst = llmax(worst, std::fabs(scene[i] - window[i]));
            }
            ensure(named(reverse, "the window holds the scene's depth"), worst < 1e-6f);
            ensure(named(reverse, "and its colour"), colourAt(40, 24) == unorm(MID_COLOUR));
            const F32 cleared = reverse ? 0.f : 1.f;
            ensure_equals(named(reverse, "sky is the cleared depth"), window[(size_t)40 * W + 40], cleared);
            ensure(named(reverse, "the near surface is nearer than the mid one"),
                   reverse ? window[(size_t)24 * W + 24] > window[(size_t)24 * W + 40] : window[(size_t)24 * W + 24] < window[(size_t)24 * W + 40]);
        }
    }

    // The reported clipping: against the scene's depth the HUD loses to a world surface at the near plane. With the
    // depth under it cleared, it draws over it.
    template<> template<>
    void alhuddepth_object_t::test<2>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            blit();
            drawHUD();
            ensure(named(reverse, "uncleared, the near world hides the HUD"), colourAt(24, 24) == unorm(NEAR_COLOUR));
            ensure(named(reverse, "where there is sky, the HUD shows anyway"), colourAt(24, 40) == unorm(HUD_COLOUR));

            blit();
            clearUnderHUD();
            drawHUD();
            ensure(named(reverse, "cleared, the HUD shows over the near world"), colourAt(24, 24) == unorm(HUD_COLOUR));
            ensure(named(reverse, "and over the sky"), colourAt(24, 40) == unorm(HUD_COLOUR));
        }
    }

    // After the scene's depth is written back, the 3D UI under the HUD's rect is hidden by the world as before, and
    // by the HUD where the HUD is in front. Without it, what the world hides shows through.
    template<> template<>
    void alhuddepth_object_t::test<3>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            blit();
            clearUnderHUD();
            drawHUD();
            restoreUnderHUD();
            overlay(40, 24, 8.f);
            overlay(44, 20, 3.f);
            overlay(24, 40, 3.f);
            overlay(40, 40, 3.f);
            ensure(named(reverse, "behind the world, off the HUD: hidden"), colourAt(40, 24) == unorm(MID_COLOUR));
            ensure(named(reverse, "in front of the world, off the HUD: shown"), colourAt(44, 20) == unorm(OVERLAY_COLOUR));
            ensure(named(reverse, "on the HUD: hidden behind it"), colourAt(24, 40) == unorm(HUD_COLOUR));
            ensure(named(reverse, "over sky, off the HUD: shown"), colourAt(40, 40) == unorm(OVERLAY_COLOUR));

            blit();
            clearUnderHUD();
            drawHUD();
            overlay(40, 24, 8.f);
            ensure(named(reverse, "unrestored, what the world hides shows through"), colourAt(40, 24) == unorm(OVERLAY_COLOUR));
        }
    }

    // Outside every rect nothing changes: the clear empties the rects alone, and with nothing drawn between, the
    // restore gives back exactly the depth the blit wrote.
    template<> template<>
    void alhuddepth_object_t::test<4>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            ensure_equals(named(reverse, "one rect"), mRects.size(), size_t(1));
            ensure(named(reverse, "the rect is inside the window and not all of it"),
                   mRects[0].mLeft > 0 && mRects[0].mRight < W && mRects[0].mBottom > 0 && mRects[0].mTop < H);

            blit();
            const std::vector<F32> before = depths(mWindow);
            clearUnderHUD();
            const std::vector<F32> cleared = depths(mWindow);
            const F32 far_value = reverse ? 0.f : 1.f;
            size_t changed_outside = 0;
            size_t uncleared_inside = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const size_t i = (size_t)y * W + x;
                    if (inRects(x, y))
                    {
                        uncleared_inside += cleared[i] != far_value ? 1 : 0;
                    }
                    else
                    {
                        changed_outside += cleared[i] != before[i] ? 1 : 0;
                    }
                }
            }
            ensure_equals(named(reverse, "the clear leaves the outside alone"), changed_outside, size_t(0));
            ensure_equals(named(reverse, "and empties the inside"), uncleared_inside, size_t(0));

            restoreUnderHUD();
            const std::vector<F32> restored = depths(mWindow);
            size_t differing = 0;
            for (size_t i = 0; i < before.size(); ++i)
            {
                differing += restored[i] != before[i] ? 1 : 0;
            }
            ensure_equals(named(reverse, "the restore gives back the blit's depth"), differing, size_t(0));
        }
    }

    // boxRect: the pixels a box covers through an orthographic projection, grown a pixel each way, clipped to the
    // viewport, empty off it.
    template<> template<>
    void alhuddepth_object_t::test<5>()
    {
        const LLMatrix4a ortho = LLMatrix4a::ortho(-0.5f, 0.5f, -0.5f, 0.5f, 0.f, HUD_DEPTH);

        LLRect rect = ALHUDDepth::boxRect(ortho, LLVector3(-0.25f, -0.25f, -2.f), LLVector3(0.25f, 0.25f, 0.f), W, H);
        ensure("pixels 16 to 48 grown to 15 to 49", rect == LLRect(15, 49, 49, 15));

        // x from -0.1 to 0.13: pixels 25.6 to 40.32; y from 0 to 0.2: pixels 32 to 44.8.
        rect = ALHUDDepth::boxRect(ortho, LLVector3(-0.1f, 0.f, -1.f), LLVector3(0.13f, 0.2f, -1.f), W, H);
        ensure("fractional edges floored and ceiled before the growth", rect == LLRect(24, 46, 42, 31));

        // x from 0.4 to 0.8: pixels 57.6 to 83.2.
        rect = ALHUDDepth::boxRect(ortho, LLVector3(0.4f, -0.1f, -1.f), LLVector3(0.8f, 0.1f, -1.f), W, H);
        ensure("clipped to the viewport", rect == LLRect(56, 40, 64, 24));

        rect = ALHUDDepth::boxRect(ortho, LLVector3(0.6f, -0.1f, -1.f), LLVector3(0.9f, 0.1f, -1.f), W, H);
        ensure("off the viewport, empty", rect.isEmpty());
    }

    // mergeRect: overlapping rects fold into one, disjoint ones are kept, and a rect bridging two folds them all.
    template<> template<>
    void alhuddepth_object_t::test<6>()
    {
        std::vector<LLRect> rects;
        ALHUDDepth::mergeRect(rects, LLRect(0, 10, 10, 0));
        ALHUDDepth::mergeRect(rects, LLRect(5, 15, 15, 5));
        ensure_equals("overlapping, one", rects.size(), size_t(1));
        ensure("their union", rects[0] == LLRect(0, 15, 15, 0));

        ALHUDDepth::mergeRect(rects, LLRect(30, 40, 40, 30));
        ensure_equals("disjoint, kept apart", rects.size(), size_t(2));

        ALHUDDepth::mergeRect(rects, LLRect(14, 31, 31, 14));
        ensure_equals("bridged, one", rects.size(), size_t(1));
        ensure("around all three", rects[0] == LLRect(0, 40, 40, 0));
    }
}
