/**
 * @file alselectionoutline_gl_test.cpp
 * @brief ALSelectionOutline's id and edge passes on the hidden window, with the viewer's own shader files:
 *        ids and edges, hidden parts, the scissor, a rigged pose, and the palette, radius and scissor helpers.
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

#include "../alselectionoutline.h"

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
        constexpr S32 RADIUS = 3;

        // Two quads meeting at x = 0, five metres down -z: A covers pixel columns 9 to 31, B 32 to 54, and both
        // rows 20 to 43.
        constexpr F32 QUAD_Z = -5.f;
        constexpr U32 ID_A = 1;
        // Past 255, so the high byte and the second pair of palette rows carry it.
        constexpr U32 ID_B = 300;

        const LLColor4 RED(1.f, 0.f, 0.f, 1.f);
        const LLColor4 GREEN(0.f, 1.f, 0.f, 1.f);

        struct Pixel { U8 r, g, b, a; };

        Pixel at(const std::vector<U8>& px, S32 x, S32 y)
        {
            const size_t i = ((size_t)y * W + x) * 4;
            return { px[i], px[i + 1], px[i + 2], px[i + 3] };
        }

        bool black(const Pixel& p) { return p.r == 0 && p.g == 0 && p.b == 0; }
        bool red(const Pixel& p) { return p.r > 0 && p.g == 0 && p.b == 0; }
        bool green(const Pixel& p) { return p.g > 0 && p.r == 0 && p.b == 0; }

        U32 idAt(const std::vector<U8>& px, S32 x, S32 y)
        {
            const Pixel p = at(px, x, y);
            return (U32)p.r | ((U32)p.g << 8);
        }

        size_t lit(const std::vector<U8>& px)
        {
            size_t count = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    count += black(at(px, x, y)) ? 0 : 1;
                }
            }
            return count;
        }
    }

    struct alselectionoutline_data
    {
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(true, true, true, false);
            return instance;
        }

        alselectionoutline_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
            gl();
        }

        ~alselectionoutline_data()
        {
            tearDown();
            setConvention(false);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
            glViewport(0, 0, ll_test::HeadlessGL::WIDTH, ll_test::HeadlessGL::HEIGHT);
        }

        // Unloading a program deletes the shared objects attached to it, so a new convention compiles them again.
        void tearDown()
        {
            LLGLSLShader::unbind();
            mIdProgram.unload();
            mOutlineProgram.unload();
            mShaders.clearShaderObjects();

            mIdMap.release();
            mScene.release();
            mFar.release();
            mFrame.release();
            if (mPalette)
            {
                LLImageGL::deleteTextures(1, &mPalette);
                mPalette = 0;
            }
            mPaletteRows = 0;
            mBuffers.clear();
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

        // The programs as LLViewerShaderMgr builds them, and the shared objects LLViewerShaderMgr::loadBasicShaders
        // compiles for them to attach, under the depth convention asked for: loadShaderFile injects REVERSE_Z as
        // the viewer's compiles get it.
        void setUp(bool reverse)
        {
            tearDown();
            mReverse = reverse;
            setConvention(mReverse);

            // The palette length the viewer compiles objectSkinV with (LL_MAX_JOINTS_PER_MESH_OBJECT).
            std::map<std::string, std::string> defines = { { "MAX_JOINTS_PER_MESH_OBJECT", "110" } };
            for (const char* object : { "avatar/objectSkinV.glsl", "deferred/textureUtilV.glsl", "objects/nonindexedTextureV.glsl" })
            {
                S32 level = 1;
                ensure(std::string("compiled ") + object, mShaders.loadShaderFile(object, level, GL_VERTEX_SHADER, &defines) != 0);
            }
            S32 level = 1;
            ensure("compiled deferred/globalF.glsl", mShaders.loadShaderFile("deferred/globalF.glsl", level, GL_FRAGMENT_SHADER, &defines) != 0);

            mIdProgram.mName = "Selection Id Shader";
            mIdProgram.mShaderFiles.clear();
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/debugV.glsl", GL_VERTEX_SHADER));
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/selectionIdF.glsl", GL_FRAGMENT_SHADER));
            mIdProgram.mShaderLevel = 1;
            ensure("id program builds with its rigged variant", mIdProgram.createShader(LLGLSLShader::VARIANT_RIGGED));
            ensure("id program has a rigged variant", mIdProgram.mRiggedVariant && mIdProgram.mRiggedVariant != &mIdProgram);

            mOutlineProgram.mName = "Selection Outline Shader";
            mOutlineProgram.mShaderFiles.clear();
            mOutlineProgram.mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
            mOutlineProgram.mShaderFiles.push_back(std::make_pair("interface/selectionOutlineF.glsl", GL_FRAGMENT_SHADER));
            mOutlineProgram.mShaderLevel = 1;
            ensure("outline program builds", mOutlineProgram.createShader());

            const LLRenderTarget::eDepthFormat depth = mReverse ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24;
            ensure("id target", mIdMap.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, depth));
            ensure("frame target", mFrame.allocate(W, H, GL_RGBA8));
            ensure("far depth", mFar.allocate(W, H, 0, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, depth));
            clearDepth(mFar);
            sceneAt(W, H);

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            ensure("triangle", mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();

            mProjection = al_perspective(FOV, 1.f, NEAR_PLANE, mReverse ? std::numeric_limits<F32>::infinity() : FORWARD_FAR);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(mProjection);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();
        }

        // The scene's depth at width x height, cleared to the far value.
        void sceneAt(S32 width, S32 height)
        {
            const LLRenderTarget::eDepthFormat depth = mReverse ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24;
            ensure("scene depth", mScene.allocate(width, height, 0, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, depth));
            clearDepth(mScene);
        }

        void clearDepth(LLRenderTarget& target)
        {
            target.bindTarget();
            LLGLDepthTest depth(GL_TRUE, GL_TRUE);
            target.clear(GL_DEPTH_BUFFER_BIT);
            target.flush();
        }

        // The depth the projection stores for a surface `dist` metres in front of the eye.
        F32 storedDepth(F32 dist) const
        {
            LLVector4a clip;
            mProjection.transform4(LLVector4a(0.f, 0.f, -dist, 1.f), clip);
            const F32 ndc = clip[2] / clip[3];
            return mReverse ? ndc : ndc * 0.5f + 0.5f;
        }

        // A quad over x0..x1, y0..y1, at depth z0 along its x0 edge and z1 along its x1 edge, every vertex weighted
        // wholly to joint 0 when `weights`.
        LLVertexBuffer* quad(F32 x0, F32 x1, F32 y0, F32 y1, F32 z0, F32 z1, bool weights = false)
        {
            LLPointer<LLVertexBuffer> buffer = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX | (weights ? LLVertexBuffer::MAP_WEIGHT4 : 0));
            ensure("quad", buffer->allocateBuffer(4, 6));
            LLStrider<LLVector3> vert;
            LLStrider<U16> index;
            buffer->getVertexStrider(vert);
            buffer->getIndexStrider(index);
            vert[0].set(x0, y0, z0);
            vert[1].set(x1, y0, z1);
            vert[2].set(x1, y1, z1);
            vert[3].set(x0, y1, z0);
            const U16 indices[6] = { 0, 1, 2, 0, 2, 3 };
            for (U32 i = 0; i < 6; ++i)
            {
                index[i] = indices[i];
            }
            if (weights)
            {
                // Joint 0 in the integer part, its weight in the fraction, which the shader renormalises.
                LLStrider<LLVector4> weight;
                buffer->getWeight4Strider(weight);
                for (U32 i = 0; i < 4; ++i)
                {
                    weight[i].set(0.999f, 0.f, 0.f, 0.f);
                }
            }
            buffer->unmapBuffer();
            mBuffers.push_back(buffer);
            return buffer.get();
        }

        LLVertexBuffer* quadA() { return quad(-2.f, 0.f, -1.f, 1.f, QUAD_Z, QUAD_Z); }
        LLVertexBuffer* quadB() { return quad(0.f, 2.f, -1.f, 1.f, QUAD_Z, QUAD_Z); }

        struct Draw
        {
            LLVertexBuffer* mBuffer;
            U32 mId;
        };

        // The id pass as ALSelectionOutline::render runs it, into `target`, reading `scene`'s depth, or with no
        // scene as it runs for the HUD; `unread` is then bound where the scene's depth would be, which the pass must
        // ignore. A rigged pass takes `palette`: joint 0's three columns, rotation in .xyz and translation in .w.
        void idPass(LLRenderTarget& target, LLRenderTarget* scene, const std::vector<Draw>& draws,
                    const F32* palette = nullptr, LLRenderTarget* unread = nullptr)
        {
            target.bindTarget();
            {
                LLGLSColorMask mask(true, true);
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LEQUAL);
                LLGLDisable cull(GL_CULL_FACE);
                LLGLDisable blend(GL_BLEND);
                glClearColor(0.f, 0.f, 0.f, 0.f);
                target.clear();

                const bool rigged = palette != nullptr;
                LLGLSLShader& program = rigged ? *mIdProgram.mRiggedVariant : mIdProgram;
                mIdProgram.bind(rigged);
                ALSelectionOutline::bindIdPass(program, scene, (S32)target.getWidth(), (S32)target.getHeight());
                if (unread)
                {
                    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, unread);
                }
                if (rigged)
                {
                    const F32 origin[3] = { 0.f, 0.f, 0.f };
                    program.uniformMatrix3x4fv(LLShaderMgr::AVATAR_MATRIX, 1, GL_FALSE, palette);
                    program.uniform3fv(LLShaderMgr::SKIN_ORIGIN, 1, origin);
                }
                for (const Draw& draw : draws)
                {
                    ALSelectionOutline::setId(program, draw.mId);
                    draw.mBuffer->setBuffer();
                    draw.mBuffer->drawRange(LLRender::TRIANGLES, 0, 3, 6, 0);
                }
                program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                LLGLSLShader::unbind();
            }
            target.flush();
        }

        void idPass(const std::vector<Draw>& draws, const F32* palette = nullptr)
        {
            idPass(mIdMap, &mScene, draws, palette);
        }

        // The id pass under `projection` with the identity modelview: the HUD's shape, a projection and no scene.
        std::vector<U8> projectedIds(const LLMatrix4a& projection, const std::vector<Draw>& draws)
        {
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(projection);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            idPass(mIdMap, nullptr, draws);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(mProjection);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            return read(mIdMap);
        }

        std::vector<U8> read(LLRenderTarget& target)
        {
            target.bindTarget();
            std::vector<U8> px = ll_test::readFramebufferRGBA((S32)target.getWidth(), (S32)target.getHeight());
            target.flush();
            return px;
        }

        void palette(const std::vector<std::pair<U32, LLColor4>>& colours, bool show_hidden)
        {
            U32 count = 0;
            for (const auto& [id, colour] : colours)
            {
                count = llmax(count, id);
            }
            mRows = ALSelectionOutline::paletteRows(count);
            mPaletteTexels.assign((size_t)ALSelectionOutline::PALETTE_WIDTH * mRows * 4, 0);
            for (const auto& [id, colour] : colours)
            {
                ALSelectionOutline::writePalette(mPaletteTexels, id, colour, show_hidden);
            }
        }

        // The edge pass as ALSelectionOutline::render runs it, over a cleared frame, scissored when asked.
        std::vector<U8> edgePass(S32 radius, const LLRect* scissor = nullptr)
        {
            ALSelectionOutline::uploadPalette(mPalette, mPaletteRows, mPaletteTexels, mRows);

            mFrame.bindTarget();
            std::vector<U8> px;
            {
                LLGLSColorMask mask(true, true);
                glClearColor(0.f, 0.f, 0.f, 0.f);
                mFrame.clear();

                LLGLDepthTest depth(GL_FALSE);
                LLGLEnable blend(GL_BLEND);
                gGL.setSceneBlendType(LLRender::BT_ALPHA);
                LLGLState scissor_test(GL_SCISSOR_TEST, scissor ? LLGLState::ENABLED_STATE : LLGLState::DISABLED_STATE);
                if (scissor)
                {
                    glScissor(scissor->mLeft, scissor->mBottom, scissor->getWidth(), scissor->getHeight());
                }

                mOutlineProgram.bind();
                ALSelectionOutline::drawEdges(mOutlineProgram, mIdMap, mPalette, radius, *mTriangle);
                LLGLSLShader::unbind();
                px = ll_test::readFramebufferRGBA(W, H);
            }
            mFrame.flush();
            return px;
        }

        LLRect boxRect(S32 radius) const
        {
            const LLVector4a extents[2] = { LLVector4a(-2.f, -1.f, QUAD_Z), LLVector4a(2.f, 1.f, QUAD_Z) };
            LLRect rect;
            ensure("the quads' box is on screen", ALSelectionOutline::scissorRect(mProjection, extents, W, H, radius, rect));
            return rect;
        }

        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mIdProgram;
        LLGLSLShader mOutlineProgram;
        LLRenderTarget mIdMap;
        LLRenderTarget mScene;
        LLRenderTarget mFar;
        LLRenderTarget mFrame;
        U32 mPalette = 0;
        U32 mPaletteRows = 0;
        U32 mRows = 0;
        std::vector<U8> mPaletteTexels;
        LLPointer<LLVertexBuffer> mTriangle;
        std::vector<LLPointer<LLVertexBuffer>> mBuffers;
        LLMatrix4a mProjection;
        bool mReverse = false;
    };

    typedef test_group<alselectionoutline_data> alselectionoutline_t;
    typedef alselectionoutline_t::object alselectionoutline_object_t;
    tut::alselectionoutline_t alselectionoutline_testcase("ALSelectionOutline");

    // Two objects side by side: each has an outline around its outer contour, in its own colour, and the edge
    // between them is drawn on both sides, each in its own colour. Their insides and the space beyond the radius
    // are left alone.
    template<> template<>
    void alselectionoutline_object_t::test<1>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            idPass({ { quadA(), ID_A }, { quadB(), ID_B } });

            const std::vector<U8> ids = read(mIdMap);
            ensure_equals(named(reverse, "A's id"), idAt(ids, 20, 32), ID_A);
            ensure_equals(named(reverse, "B's id, past one byte"), idAt(ids, 40, 32), ID_B);
            ensure_equals(named(reverse, "nothing selected outside"), idAt(ids, 2, 2), 0U);

            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);
            const std::vector<U8> px = edgePass(RADIUS);

            const Pixel left = at(px, 8, 32);
            ensure(named(reverse, "A outlined left of its contour"), red(left));
            ensure_equals(named(reverse, "at full strength beside it"), (U32)left.r, 255U);
            ensure(named(reverse, "A outlined above"), red(at(px, 20, 44)));
            ensure(named(reverse, "A outlined below"), red(at(px, 20, 19)));
            ensure(named(reverse, "B outlined right of its contour"), green(at(px, 55, 32)));
            ensure(named(reverse, "B outlined above"), green(at(px, 40, 44)));

            ensure(named(reverse, "fading out to the radius"), at(px, 6, 32).r > 0 && at(px, 6, 32).r < left.r);
            ensure(named(reverse, "nothing past the radius"), black(at(px, 5, 32)));
            ensure(named(reverse, "nothing far away"), black(at(px, 2, 2)));

            ensure(named(reverse, "A's side of the shared edge in A's colour"), red(at(px, 31, 32)));
            ensure(named(reverse, "B's side of the shared edge in B's colour"), green(at(px, 32, 32)));
            ensure(named(reverse, "A's inside left alone"), black(at(px, 20, 32)));
            ensure(named(reverse, "B's inside left alone"), black(at(px, 44, 32)));
        }
    }

    // The same two quads under one id, as a mask without ids would see them: the contour stays and the edge
    // between them is gone. This is what the ids buy.
    template<> template<>
    void alselectionoutline_object_t::test<2>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            idPass({ { quadA(), ID_A }, { quadB(), ID_A } });
            palette({ { ID_A, RED } }, true);
            const std::vector<U8> px = edgePass(RADIUS);

            ensure(named(reverse, "contour still outlined"), red(at(px, 8, 32)));
            ensure(named(reverse, "contour still outlined on the far side"), red(at(px, 55, 32)));
            ensure(named(reverse, "no edge between them on one side"), black(at(px, 31, 32)));
            ensure(named(reverse, "no edge between them on the other"), black(at(px, 32, 32)));
        }
    }

    // Behind something nearer in the scene's depth, the surface is marked hidden and its outline drawn dimmed;
    // with hidden parts off, not at all. Where nothing covers it, the outline is unchanged.
    template<> template<>
    void alselectionoutline_object_t::test<3>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);

            // An occluder three metres out over columns 0 to 19, which covers A's left part.
            mScene.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE);
                LLGLEnable scissor(GL_SCISSOR_TEST);
                glScissor(0, 0, 20, H);
                glClearDepth(storedDepth(3.f));
                mScene.clear(GL_DEPTH_BUFFER_BIT);
                glClearDepth(mReverse ? 0.0 : 1.0);
            }
            mScene.flush();

            idPass({ { quadA(), ID_A } });
            const std::vector<U8> ids = read(mIdMap);
            ensure_equals(named(reverse, "covered part marked hidden"), (U32)at(ids, 12, 32).b, 0U);
            ensure_equals(named(reverse, "uncovered part marked visible"), (U32)at(ids, 25, 32).b, 255U);
            ensure_equals(named(reverse, "hidden parts keep their id"), idAt(ids, 12, 32), ID_A);

            palette({ { ID_A, RED } }, true);
            std::vector<U8> px = edgePass(RADIUS);
            const U32 hidden = at(px, 8, 32).r;
            const U32 visible = at(px, 25, 44).r;
            ensure_equals(named(reverse, "visible outline at full strength"), visible, 255U);
            ensure(named(reverse, "hidden outline dimmed to the hidden alpha"), hidden >= 90 && hidden <= 115);

            palette({ { ID_A, RED } }, false);
            px = edgePass(RADIUS);
            ensure(named(reverse, "hidden outline left out"), black(at(px, 8, 32)));
            ensure_equals(named(reverse, "visible outline kept"), (U32)at(px, 25, 44).r, 255U);
        }
    }

    // A surface that is itself in the scene is visible, at the window's resolution and at half of it, where the
    // scene's depth is read by uv from texels twice the size and, on a surface turned away, nearer than the
    // fragment by up to half a texel of its slope.
    template<> template<>
    void alselectionoutline_object_t::test<4>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);

            for (S32 divisor : { 1, 2 })
            {
                sceneAt(W / divisor, H / divisor);
                // Turned about 60 degrees from the eye: five metres deep across its width.
                LLVertexBuffer* tilted = quad(-1.5f, 1.5f, -1.f, 1.f, -3.5f, -8.5f);

                // The scene draws it into its own depth, at its own resolution.
                idPass(mScene, &mFar, { { tilted, ID_A } });
                idPass({ { tilted, ID_A } });

                const std::vector<U8> ids = read(mIdMap);
                size_t covered = 0;
                size_t hidden = 0;
                for (S32 y = 0; y < H; ++y)
                {
                    for (S32 x = 0; x < W; ++x)
                    {
                        if (idAt(ids, x, y) == ID_A)
                        {
                            ++covered;
                            hidden += at(ids, x, y).b == 0 ? 1 : 0;
                        }
                    }
                }
                const std::string at_divisor = " at render resolution / " + std::to_string(divisor);
                ensure(named(reverse, "the surface covers pixels" + at_divisor), covered > 200);
                ensure_equals(named(reverse, "none of its own pixels hidden by itself" + at_divisor), hidden, size_t(0));
            }
        }
    }

    // The scissor is the objects' projected box grown by the radius: the scissored edge pass draws exactly what
    // the unscissored one does. Without the growth the ring outside the box is clipped.
    template<> template<>
    void alselectionoutline_object_t::test<5>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            idPass({ { quadA(), ID_A }, { quadB(), ID_B } });
            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);

            const std::vector<U8> full = edgePass(RADIUS);
            const LLRect rect = boxRect(RADIUS);
            ensure_equals(named(reverse, "left: column 8.57 floored, less the radius"), rect.mLeft, 8 - RADIUS);
            ensure_equals(named(reverse, "right: column 55.43 ceiled, plus the radius"), rect.mRight, 56 + RADIUS);
            ensure_equals(named(reverse, "bottom: row 20.28 floored, less the radius"), rect.mBottom, 20 - RADIUS);
            ensure_equals(named(reverse, "top: row 43.72 ceiled, plus the radius"), rect.mTop, 44 + RADIUS);

            const std::vector<U8> scissored = edgePass(RADIUS, &rect);
            ensure(named(reverse, "something is outlined"), lit(full) > 0);
            ensure(named(reverse, "the scissored pass draws every outline pixel"), scissored == full);

            const LLRect tight = boxRect(0);
            const std::vector<U8> clipped = edgePass(RADIUS, &tight);
            ensure(named(reverse, "a box without the radius clips the ring"), lit(clipped) < lit(full));
            ensure(named(reverse, "the ring's outer pixels are what it loses"), !black(at(full, 6, 32)) && black(at(clipped, 6, 32)));
        }
    }

    // Where the scissor gives up: a box reaching behind the eye takes the whole target, and one wholly behind it
    // touches nothing.
    template<> template<>
    void alselectionoutline_object_t::test<6>()
    {
        const LLMatrix4a proj = al_perspective(FOV, 1.f, NEAR_PLANE, FORWARD_FAR);
        LLRect rect;

        const LLVector4a straddling[2] = { LLVector4a(-1.f, -1.f, -5.f), LLVector4a(1.f, 1.f, 2.f) };
        ensure("a box reaching behind the eye is drawn", ALSelectionOutline::scissorRect(proj, straddling, W, H, RADIUS, rect));
        ensure("over the whole target", rect == LLRect(0, H, W, 0));

        const LLVector4a behind[2] = { LLVector4a(-1.f, -1.f, 2.f), LLVector4a(1.f, 1.f, 5.f) };
        ensure("a box behind the eye is not", !ALSelectionOutline::scissorRect(proj, behind, W, H, RADIUS, rect));

        const LLVector4a aside[2] = { LLVector4a(30.f, -1.f, -5.f), LLVector4a(32.f, 1.f, -5.f) };
        ensure("a box off to the side is not", !ALSelectionOutline::scissorRect(proj, aside, W, H, RADIUS, rect));
    }

    // A rigged object's id lands where its joint palette puts it, not at its bind pose, and moves with the pose.
    template<> template<>
    void alselectionoutline_object_t::test<7>()
    {
        // Joint 0 moved 1.5 m along +x, then along -x.
        const F32 right[12] = { 1.f, 0.f, 0.f, 1.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };
        const F32 left[12] = { 1.f, 0.f, 0.f, -1.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };

        for (bool reverse : conventions())
        {
            setUp(reverse);
            LLVertexBuffer* bound = quad(-0.5f, 0.5f, -0.5f, 0.5f, QUAD_Z, QUAD_Z, true);

            idPass({ { bound, ID_A } }, right);
            std::vector<U8> ids = read(mIdMap);
            ensure_equals(named(reverse, "posed right"), idAt(ids, 49, 32), ID_A);
            ensure_equals(named(reverse, "not at the bind pose"), idAt(ids, 32, 32), 0U);
            ensure_equals(named(reverse, "not posed left"), idAt(ids, 14, 32), 0U);

            idPass({ { bound, ID_A } }, left);
            ids = read(mIdMap);
            ensure_equals(named(reverse, "posed left"), idAt(ids, 14, 32), ID_A);
            ensure_equals(named(reverse, "gone from the right"), idAt(ids, 49, 32), 0U);

            // Posed, it covers columns 9 to 20 and rows 26 to 37.
            palette({ { ID_A, RED } }, true);
            const std::vector<U8> px = edgePass(RADIUS);
            ensure(named(reverse, "outlined above where it is posed"), red(at(px, 14, 39)));
            ensure(named(reverse, "outlined below where it is posed"), red(at(px, 14, 24)));
            ensure(named(reverse, "not where it is not"), black(at(px, 49, 39)) && black(at(px, 49, 24)));
        }
    }

    // The palette: an id's visible colour above its hidden one, PALETTE_WIDTH ids to a pair of rows; the hidden
    // one dimmed, or transparent with hidden parts off.
    template<> template<>
    void alselectionoutline_object_t::test<8>()
    {
        ensure_equals("one pair of rows up to 255", ALSelectionOutline::paletteRows(255), 2U);
        ensure_equals("two from 256", ALSelectionOutline::paletteRows(256), 4U);

        std::vector<U8> texels((size_t)ALSelectionOutline::PALETTE_WIDTH * 4 * 4, 0);
        ALSelectionOutline::writePalette(texels, 300, LLColor4(1.f, 0.5f, 0.f, 0.8f), true);
        const size_t visible = ((size_t)2 * ALSelectionOutline::PALETTE_WIDTH + 44) * 4;
        const size_t hidden = ((size_t)3 * ALSelectionOutline::PALETTE_WIDTH + 44) * 4;
        ensure_equals("visible red", (U32)texels[visible], 255U);
        ensure_equals("visible green", (U32)texels[visible + 1], 128U);
        ensure_equals("visible alpha", (U32)texels[visible + 3], 204U);
        ensure_equals("hidden keeps the colour", (U32)texels[hidden + 1], 128U);
        ensure_equals("hidden alpha dimmed", (U32)texels[hidden + 3], (U32)ll_round(0.8f * ALSelectionOutline::HIDDEN_ALPHA * 255.f));

        ALSelectionOutline::writePalette(texels, 300, LLColor4(1.f, 0.5f, 0.f, 0.8f), false);
        ensure_equals("hidden transparent with hidden parts off", (U32)texels[hidden + 3], 0U);
    }

    // The radius: thickness times distance times the zoom, seen from that distance, so no distance changes it; it
    // grows with the window and is at least a pixel.
    template<> template<>
    void alselectionoutline_object_t::test<9>()
    {
        const F32 fov = 60.f * DEG_TO_RAD;
        ensure_equals("1080 rows at the default view", ALSelectionOutline::radiusPixels(0.01f, 1080.f, fov, fov), 9);
        ensure_equals("2160 rows", ALSelectionOutline::radiusPixels(0.01f, 2160.f, fov, fov), 19);
        ensure_equals("zoomed in to half the view", ALSelectionOutline::radiusPixels(0.01f, 1080.f, fov * 0.5f, fov), 10);
        ensure_equals("never under a pixel", ALSelectionOutline::radiusPixels(0.01f, 10.f, fov, fov), 1);
        ensure_equals("no view, a pixel", ALSelectionOutline::radiusPixels(0.01f, 1080.f, 0.f, fov), 1);
        ensure_equals("rings: one a pixel", ALSelectionOutline::ringCount(3), 3);
        ensure_equals("rings: at most MAX_RINGS", ALSelectionOutline::ringCount(19), ALSelectionOutline::MAX_RINGS);
    }

    // On the HUD the scene's depth is not read: a surface behind something nearer, in a depth bound where the
    // scene's would be, still counts as visible, and its outline is drawn at full strength with hidden parts off.
    template<> template<>
    void alselectionoutline_object_t::test<10>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);

            // An occluder three metres out over the whole view, in front of A.
            mScene.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE);
                glClearDepth(storedDepth(3.f));
                mScene.clear(GL_DEPTH_BUFFER_BIT);
                glClearDepth(mReverse ? 0.0 : 1.0);
            }
            mScene.flush();

            idPass({ { quadA(), ID_A } });
            ensure_equals(named(reverse, "the world's pass hides A behind it"), (U32)at(read(mIdMap), 20, 32).b, 0U);

            idPass(mIdMap, nullptr, { { quadA(), ID_A } }, nullptr, &mScene);
            const std::vector<U8> ids = read(mIdMap);
            size_t covered = 0;
            size_t hidden = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    if (idAt(ids, x, y) == ID_A)
                    {
                        ++covered;
                        hidden += at(ids, x, y).b == 0 ? 1 : 0;
                    }
                }
            }
            ensure(named(reverse, "the HUD's pass draws A"), covered > 200);
            ensure_equals(named(reverse, "and hides none of it"), hidden, size_t(0));

            palette({ { ID_A, RED } }, false);
            const std::vector<U8> px = edgePass(RADIUS);
            ensure_equals(named(reverse, "outlined at full strength"), (U32)at(px, 8, 32).r, 255U);
        }
    }

    // The HUD's projections keep the nearest surface in the id target's own depth, whichever is drawn first, under
    // both conventions: al_ortho (gGL.ortho) reverses its depth with reverse-Z, and so does the flattened depth row
    // get_hud_matrices gives the HUD. An orthographic projection left unreversed under reverse-Z keeps the farthest
    // instead, which is what the reversal is for.
    template<> template<>
    void alselectionoutline_object_t::test<11>()
    {
        constexpr F32 HUD_DEPTH = 10.f;
        constexpr U32 ID_NEAR = 1;
        constexpr U32 ID_FAR = 2;

        for (bool reverse : conventions())
        {
            setUp(reverse);
            // Columns and rows 19 to 44, and 6 to 57: the farther shows only around the nearer.
            LLVertexBuffer* nearer = quad(-0.2f, 0.2f, -0.2f, 0.2f, -2.f, -2.f);
            LLVertexBuffer* farther = quad(-0.4f, 0.4f, -0.4f, 0.4f, -6.f, -6.f);

            const LLMatrix4a ortho = al_ortho(-0.5f, 0.5f, -0.5f, 0.5f, 0.f, HUD_DEPTH);
            LLMatrix4a flattened = ortho;
            flattened.mMatrix[2].getF32ptr()[2] = reverse ? 0.005f : -0.01f;

            const LLMatrix4a* projections[] = { &ortho, &flattened };
            for (const LLMatrix4a* projection : projections)
            {
                const std::string which = projection == &ortho ? "al_ortho" : "the HUD's flattened row";
                for (bool near_first : { true, false })
                {
                    const std::vector<U8> ids = near_first
                        ? projectedIds(*projection, { { nearer, ID_NEAR }, { farther, ID_FAR } })
                        : projectedIds(*projection, { { farther, ID_FAR }, { nearer, ID_NEAR } });
                    const std::string order = near_first ? ", nearer drawn first" : ", farther drawn first";
                    ensure_equals(named(reverse, which + ": the nearer kept" + order), idAt(ids, 32, 32), ID_NEAR);
                    ensure_equals(named(reverse, which + ": the farther around it" + order), idAt(ids, 10, 32), ID_FAR);
                }
            }

            if (reverse)
            {
                const LLMatrix4a unreversed = LLMatrix4a::orthoZO(-0.5f, 0.5f, -0.5f, 0.5f, 0.f, HUD_DEPTH);
                const std::vector<U8> ids = projectedIds(unreversed, { { farther, ID_FAR }, { nearer, ID_NEAR } });
                ensure_equals(named(reverse, "unreversed, the farther wins"), idAt(ids, 32, 32), ID_FAR);
            }
        }
    }

    // The HUD's radius: its projection is a unit high and its zoom scales objects and thickness alike, so the
    // outline is `thickness` of the view's height whatever the zoom. A band a tenth of that projection high covers
    // the rows a thickness of a tenth gives.
    template<> template<>
    void alselectionoutline_object_t::test<12>()
    {
        ensure_equals("1080 rows", ALSelectionOutline::hudRadiusPixels(0.01f, 1080.f), 11);
        ensure_equals("2160 rows", ALSelectionOutline::hudRadiusPixels(0.01f, 2160.f), 22);
        ensure_equals("never under a pixel", ALSelectionOutline::hudRadiusPixels(0.01f, 10.f), 1);

        setUp(false);
        const LLMatrix4a hud = al_ortho(-0.5f, 0.5f, -0.5f, 0.5f, 0.f, 10.f);
        LLVertexBuffer* band = quad(-0.4f, 0.4f, 0.f, 0.1f, -2.f, -2.f);
        const std::vector<U8> ids = projectedIds(hud, { { band, ID_A } });
        S32 rows = 0;
        for (S32 y = 0; y < H; ++y)
        {
            rows += idAt(ids, 32, y) == ID_A ? 1 : 0;
        }
        ensure_equals("a band a tenth of the projection high", rows, ALSelectionOutline::hudRadiusPixels(0.1f, (F32)H));
    }
}
