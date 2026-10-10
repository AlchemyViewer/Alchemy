/**
 * @file alselectionoutline_gl_test.cpp
 * @brief ALSelectionOutline's id and edge passes on the hidden window, with the viewer's own shader files: ids,
 *        cut-out faces, the contour's width and anti-aliasing, priorities and the edges between objects, hidden
 *        parts, the scissor, a rigged pose, the HUD, and the palette, width and scissor helpers.
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

#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace tut
{
    namespace
    {
        using Outline = ALSelectionOutline;

        constexpr S32 W = 64;
        constexpr S32 H = 64;
        constexpr F32 FOV = 1.f;
        constexpr F32 NEAR_PLANE = 0.1f;
        constexpr F32 FORWARD_FAR = 100.f;

        // Two quads meeting at x = 0, five metres down -z: A covers pixel columns 9 to 31, B 32 to 54, and both
        // rows 20 to 43.
        constexpr F32 QUAD_Z = -5.f;
        constexpr U32 ID_A = 1;
        // Past 255, so the high byte and the second pair of palette rows carry it.
        constexpr U32 ID_B = 300;

        // How deep quads lie under the pixel projection, whose units are pixels.
        constexpr F32 PIXEL_Z = -2.f;

        // The contour width at a UI scale of 1 the cases' pixel positions are laid out for: two pixels and a pixel
        // of anti-aliasing, reaching three.
        constexpr F32 TEST_WIDTH = 2.f;

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

        // An id texel the scene hides, its object's hidden parts drawn dimmed: 0.5 in .b.
        bool drawnHidden(const Pixel& p) { return p.b == 127 || p.b == 128; }

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

        LLMatrix4a identity()
        {
            LLMatrix4a m;
            m.setIdentity();
            return m;
        }

        // A projection a pixel to the unit over the target, as the HUD's is a projection with no scene behind it.
        LLMatrix4a pixelProjection()
        {
            return al_ortho(0.f, (F32)W, 0.f, (F32)H, 0.f, 10.f);
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
            gGL.matrixMode(LLRender::MM_TEXTURE0);
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
            mJumpProgram.unload();
            mTileProgram.unload();
            mOutlineProgram.unload();
            mShaders.clearShaderObjects();

            mIdMap.release();
            mMarked.release();
            mTiles.release();
            mAllTiles.release();
            mScene.release();
            mFar.release();
            mFrame.release();
            for (U32* texture : { &mPalette, &mRamp })
            {
                if (*texture)
                {
                    LLImageGL::deleteTextures(1, texture);
                    *texture = 0;
                }
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
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/selectionIdV.glsl", GL_VERTEX_SHADER));
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/selectionIdF.glsl", GL_FRAGMENT_SHADER));
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/selectionUtilF.glsl", GL_FRAGMENT_SHADER));
            mIdProgram.mShaderLevel = 1;
            ensure("id program builds with its rigged variant", mIdProgram.createShader(LLGLSLShader::VARIANT_RIGGED));
            ensure("id program has a rigged variant", mIdProgram.mRiggedVariant && mIdProgram.mRiggedVariant != &mIdProgram);

            const std::pair<LLGLSLShader*, const char*> passes[] = { { &mJumpProgram, "selectionJumpF.glsl" },
                                                                     { &mTileProgram, "selectionTileF.glsl" },
                                                                     { &mOutlineProgram, "selectionOutlineF.glsl" } };
            for (const auto& [program, fragment] : passes)
            {
                program->mName = fragment;
                program->mShaderFiles.clear();
                program->mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
                program->mShaderFiles.push_back(std::make_pair(std::string("interface/") + fragment, GL_FRAGMENT_SHADER));
                program->mShaderFiles.push_back(std::make_pair("interface/selectionUtilF.glsl", GL_FRAGMENT_SHADER));
                program->mShaderLevel = 1;
                ensure(std::string(fragment) + "'s program builds", program->createShader());
            }

            const LLRenderTarget::eDepthFormat depth = mReverse ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24;
            ensure("id target", mIdMap.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, depth));
            ensure("marked target", mMarked.allocate(W, H, GL_RGBA8));
            ensure("frame target", mFrame.allocate(W, H, GL_RGBA8));
            ensure("tile target", mTiles.allocate(Outline::tileCount(W), Outline::tileCount(H), GL_RGBA16));
            // Every tile holding every id and a jump: the edge pass searches every pixel's disk, as it did before
            // the tiles.
            ensure("all tiles target", mAllTiles.allocate(Outline::tileCount(W), Outline::tileCount(H), GL_RGBA16));
            mAllTiles.bindTarget();
            gGL.setClearColor(LLColor4(1.f, 1.f, 1.f, 1.f));
            mAllTiles.clear();
            gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
            mAllTiles.flush();
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
            gGL.matrixMode(LLRender::MM_TEXTURE0);
            gGL.loadIdentity();
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

        // Something in the scene `dist` metres out over the pixels x to x + w - 1 and y to y + h - 1, all of them by
        // default: the scene's depth there.
        void occlude(F32 dist, S32 x = 0, S32 y = 0, S32 w = W, S32 h = H)
        {
            mScene.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE);
                LLGLSScissor scissor(x, y, w, h);
                glClearDepth(storedDepth(dist));
                mScene.clear(GL_DEPTH_BUFFER_BIT);
                glClearDepth(mReverse ? 0.0 : 1.0);
            }
            mScene.flush();
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

        // A quad through four corners, texture coordinates 0 to 1 across it from the first, every vertex weighted
        // wholly to joint 0 when `weights`, and coloured `colour`, or opaque white, as a face's vertices are.
        LLVertexBuffer* quadAt(const LLVector3 (&corners)[4], bool weights = false, const LLColor4U* colour = nullptr)
        {
            const U32 mask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 | LLVertexBuffer::MAP_COLOR |
                             (weights ? LLVertexBuffer::MAP_WEIGHT4 : 0);
            LLPointer<LLVertexBuffer> buffer = new LLVertexBuffer(mask);
            ensure("quad", buffer->allocateBuffer(4, 6));
            LLStrider<LLVector3> vert;
            LLStrider<LLVector2> texcoord;
            LLStrider<U16> index;
            buffer->getVertexStrider(vert);
            buffer->getTexCoord0Strider(texcoord);
            buffer->getIndexStrider(index);
            const LLVector2 uvs[4] = { LLVector2(0.f, 0.f), LLVector2(1.f, 0.f), LLVector2(1.f, 1.f), LLVector2(0.f, 1.f) };
            for (U32 i = 0; i < 4; ++i)
            {
                vert[i] = corners[i];
                texcoord[i] = uvs[i];
            }
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
            LLStrider<LLColor4U> colours;
            buffer->getColorStrider(colours);
            for (U32 i = 0; i < 4; ++i)
            {
                colours[i] = colour ? *colour : LLColor4U(255, 255, 255, 255);
            }
            buffer->unmapBuffer();
            mBuffers.push_back(buffer);
            return buffer.get();
        }

        // A quad over x0..x1, y0..y1, at depth z0 along its x0 edge and z1 along its x1 edge.
        LLVertexBuffer* quad(F32 x0, F32 x1, F32 y0, F32 y1, F32 z0, F32 z1, bool weights = false,
                             const LLColor4U* colour = nullptr)
        {
            const LLVector3 corners[4] = { LLVector3(x0, y0, z0), LLVector3(x1, y0, z1), LLVector3(x1, y1, z1), LLVector3(x0, y1, z0) };
            return quadAt(corners, weights, colour);
        }

        LLVertexBuffer* quadA() { return quad(-2.f, 0.f, -1.f, 1.f, QUAD_Z, QUAD_Z); }
        LLVertexBuffer* quadB() { return quad(0.f, 2.f, -1.f, 1.f, QUAD_Z, QUAD_Z); }

        // Under the pixel projection: pixel columns x0 to x1 - 1 and rows y0 to y1 - 1.
        LLVertexBuffer* pixels(S32 x0, S32 x1, S32 y0, S32 y1, const LLColor4U* colour = nullptr)
        {
            return quad((F32)x0, (F32)x1, (F32)y0, (F32)y1, PIXEL_Z, PIXEL_Z, false, colour);
        }

        // A texture 256 texels wide whose alpha is its column: white, and opaque at its right end.
        U32 ramp()
        {
            if (!mRamp)
            {
                std::vector<U8> texels(256 * 4);
                for (U32 x = 0; x < 256; ++x)
                {
                    texels[x * 4] = texels[x * 4 + 1] = texels[x * 4 + 2] = 255;
                    texels[x * 4 + 3] = (U8)x;
                }
                LLImageGL::generateTextures(1, &mRamp);
                gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, mRamp);
                LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RGBA8, 256, 1,
                                             GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
                gGL.getTextureSlot(0)->unbind();
            }
            return mRamp;
        }

        struct Draw
        {
            LLVertexBuffer* mBuffer;
            U32 mId;
            Outline::EPriority mPriority = Outline::PRIORITY_ROOT;
            // The face's alpha test, its texture, its base colour transform and its texture animation.
            F32 mCutoff = Outline::NO_ALPHA_TEST;
            U32 mTexture = 0;
            const LLGLTFMaterial::TextureTransform::Pack* mTransform = nullptr;
            const LLMatrix4a* mTextureMatrix = nullptr;
            // Hidden parts drawn dimmed, as LLSelectMgr asks of most roles; and a double-sided material.
            bool mShowHidden = true;
            bool mDoubleSided = false;
        };

        // The id pass as ALSelectionOutline::render runs it, into `target`, reading `scene`'s depth, or with no
        // scene as it runs for the HUD; `unread` is then bound where the scene's depth would be, which the pass must
        // ignore. A rigged pass takes `palette`: joint 0's three columns, rotation in .xyz and translation in .w.
        void idPass(LLRenderTarget& target, LLRenderTarget* scene, const std::vector<Draw>& draws,
                    const F32* palette = nullptr, LLRenderTarget* unread = nullptr)
        {
            // Checked once the target is let go, so a failure leaves nothing bound for the tests after it.
            bool texture_read = true;
            mIdProjection = gGL.getProjectionMatrix();
            target.bindTarget();
            {
                Outline::IdPassState state;
                gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
                target.clear();

                const bool rigged = palette != nullptr;
                LLGLSLShader& program = rigged ? *mIdProgram.mRiggedVariant : mIdProgram;
                mIdProgram.bind(rigged);
                Outline::bindIdPass(program, scene, (S32)target.getWidth(), (S32)target.getHeight());
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
                    Outline::setId(program, draw.mId, draw.mPriority, draw.mShowHidden);
                    Outline::setAlphaTest(program, draw.mCutoff, draw.mTransform ? *draw.mTransform : Outline::IDENTITY_TRANSFORM);
                    const S32 channel = draw.mTexture ? program.enableTexture(LLShaderMgr::DIFFUSE_MAP) : -1;
                    if (channel > -1)
                    {
                        gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, draw.mTexture,
                                                                gGL.getSampler(ALSamplers::PointClamp));
                    }
                    texture_read = texture_read && (!draw.mTexture || channel > -1);
                    if (draw.mTextureMatrix)
                    {
                        gGL.matrixMode(LLRender::MM_TEXTURE0);
                        gGL.loadMatrix(*draw.mTextureMatrix);
                        gGL.matrixMode(LLRender::MM_MODELVIEW);
                    }
                    Outline::drawFace(*draw.mBuffer, 0, 3, 6, 0, draw.mDoubleSided);
                    if (draw.mTextureMatrix)
                    {
                        gGL.matrixMode(LLRender::MM_TEXTURE0);
                        gGL.loadIdentity();
                        gGL.matrixMode(LLRender::MM_MODELVIEW);
                    }
                }
                program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
                program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                LLGLSLShader::unbind();
            }
            target.flush();
            ensure("the id program reads the face's texture", texture_read);
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

        std::vector<U8> pixelIds(const std::vector<Draw>& draws)
        {
            return projectedIds(pixelProjection(), draws);
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
            mRows = Outline::paletteRows(count);
            mPaletteTexels.assign((size_t)Outline::PALETTE_WIDTH * mRows * 4, 0);
            for (const auto& [id, colour] : colours)
            {
                Outline::writePalette(mPaletteTexels, id, colour, show_hidden);
            }
        }

        // The lines' widths at `ui_scale`, as the world's view has them with a contour TEST_WIDTH wide, which the
        // cases' pixel positions are laid out for, through the projection the last id pass drew with.
        Outline::View view(F32 ui_scale = 1.f) const
        {
            return Outline::makeView(identity(), mIdProjection, TEST_WIDTH, ui_scale, false);
        }

        // Lines `width` and `inner_width` pixels wide whatever the UI scale, through the last id pass's projection.
        Outline::View widths(S32 width, S32 inner_width) const
        {
            Outline::View lines = Outline::makeView(identity(), mIdProjection, 1.f, 1.f, false);
            lines.mWidth = width;
            lines.mInnerWidth = inner_width;
            return lines;
        }

        // The jump and tile passes as ALSelectionOutline::render runs them, over the texels under `rect`, or the
        // whole id target, through `lines`' projection; the tile pass left out when `tiles` is false.
        void markPasses(const Outline::View& lines, const LLRect* rect = nullptr, bool tiles = true)
        {
            const LLRect under = rect ? *rect : LLRect(0, H, W, 0);
            mJumpProgram.bind();
            Outline::drawJumps(mJumpProgram, mIdMap, mMarked, under, lines, *mTriangle);
            if (tiles)
            {
                mTileProgram.bind();
                Outline::drawTiles(mTileProgram, mMarked, mTiles, under, *mTriangle);
            }
            LLGLSLShader::unbind();
        }

        // What the tile pass recorded, four 16-bit values a tile: the lowest and highest id, the jump, and 1.
        std::vector<U16> tileRecords()
        {
            const S32 columns = (S32)Outline::tileCount(W);
            const S32 rows = (S32)Outline::tileCount(H);
            std::vector<U16> records((size_t)columns * rows * 4);
            mTiles.bindTarget();
            glReadPixels(0, 0, columns, rows, GL_RGBA, GL_UNSIGNED_SHORT, records.data());
            mTiles.flush();
            return records;
        }

        // Whether the jump pass marked the texel at (x, y) as the near side of a jump.
        bool markedAt(S32 x, S32 y)
        {
            return at(read(mMarked), x, y).a >= 128;
        }

        // The edge pass as ALSelectionOutline::render runs it, the jump and tile passes first, over the texels under
        // `mark_rect` or all of them, then over a cleared frame, scissored when asked. `untiled` holds every id and a
        // jump in every tile instead, so every pixel searches its whole disk.
        std::vector<U8> edgePass(const Outline::View& lines, const LLRect* scissor = nullptr, bool untiled = false,
                                 const LLRect* mark_rect = nullptr)
        {
            Outline::uploadPalette(mPalette, mPaletteRows, mPaletteTexels, mRows);
            markPasses(lines, mark_rect, !untiled);

            mFrame.bindTarget();
            std::vector<U8> px;
            {
                LLGLSColorMask mask(true, true);
                gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
                mFrame.clear();

                LLGLDepthTest depth(GL_FALSE);
                LLGLEnable blend(GL_BLEND);
                gGL.setSceneBlendType(LLRender::BT_ALPHA);

                mOutlineProgram.bind();
                LLRenderTarget& tiles = untiled ? mAllTiles : mTiles;
                if (scissor)
                {
                    LLGLSScissor scissored(scissor->mLeft, scissor->mBottom, scissor->getWidth(), scissor->getHeight());
                    Outline::drawEdges(mOutlineProgram, mMarked, mIdMap, tiles, mPalette, lines, *mTriangle);
                }
                else
                {
                    Outline::drawEdges(mOutlineProgram, mMarked, mIdMap, tiles, mPalette, lines, *mTriangle);
                }
                LLGLSLShader::unbind();
                px = ll_test::readFramebufferRGBA(W, H);
            }
            mFrame.flush();
            return px;
        }

        std::vector<U8> edgePass(const LLRect* scissor = nullptr)
        {
            return edgePass(view(), scissor);
        }

        LLRect boxRect(S32 reach) const
        {
            const LLVector4a extents[2] = { LLVector4a(-2.f, -1.f, QUAD_Z), LLVector4a(2.f, 1.f, QUAD_Z) };
            LLRect rect;
            ensure("the quads' box is on screen", Outline::scissorRect(mProjection, extents, W, H, reach, rect));
            return rect;
        }

        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mIdProgram;
        LLGLSLShader mJumpProgram;
        LLGLSLShader mTileProgram;
        LLGLSLShader mOutlineProgram;
        LLRenderTarget mIdMap;
        LLRenderTarget mMarked;
        LLRenderTarget mTiles;
        LLRenderTarget mAllTiles;
        LLRenderTarget mScene;
        LLRenderTarget mFar;
        LLRenderTarget mFrame;
        U32 mPalette = 0;
        U32 mPaletteRows = 0;
        U32 mRows = 0;
        U32 mRamp = 0;
        std::vector<U8> mPaletteTexels;
        LLPointer<LLVertexBuffer> mTriangle;
        std::vector<LLPointer<LLVertexBuffer>> mBuffers;
        LLMatrix4a mProjection;
        // The projection the last id pass drew with, which the edge pass reads its depth through.
        LLMatrix4a mIdProjection;
        bool mReverse = false;
    };

    typedef test_group<alselectionoutline_data> alselectionoutline_t;
    typedef alselectionoutline_t::object alselectionoutline_object_t;
    tut::alselectionoutline_t alselectionoutline_testcase("ALSelectionOutline");

    // Two objects side by side: each is outlined around its outer contour in its own colour, a solid line two
    // pixels wide at a UI scale of 1. The edge between them is drawn once, thin and faint, on the side of the one
    // given the lower id. Their insides and the space past the line are left alone.
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
            ensure_equals(named(reverse, "the priority in alpha"), (U32)at(ids, 20, 32).a, (U32)Outline::PRIORITY_ROOT);

            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);
            const std::vector<U8> px = edgePass();

            ensure_equals(named(reverse, "A outlined at full strength beside its contour"), (U32)at(px, 8, 32).r, 255U);
            ensure_equals(named(reverse, "and a pixel further"), (U32)at(px, 7, 32).r, 255U);
            ensure(named(reverse, "and in its colour"), red(at(px, 7, 32)));
            ensure(named(reverse, "nothing past the line"), black(at(px, 6, 32)));
            ensure(named(reverse, "A outlined above"), red(at(px, 20, 45)) && black(at(px, 20, 46)));
            ensure(named(reverse, "A outlined below"), red(at(px, 20, 18)) && black(at(px, 20, 17)));
            ensure(named(reverse, "B outlined right of its contour"), green(at(px, 56, 32)) && black(at(px, 57, 32)));
            ensure(named(reverse, "B outlined above"), green(at(px, 40, 45)));
            ensure(named(reverse, "nothing far away"), black(at(px, 2, 2)));

            const Pixel edge = at(px, 31, 32);
            ensure(named(reverse, "the shared edge on A's side, in A's colour"), red(edge));
            ensure(named(reverse, "fainter than the contour"), edge.r >= 150 && edge.r <= 156);
            ensure(named(reverse, "and not on B's"), black(at(px, 32, 32)));
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
            const std::vector<U8> px = edgePass();

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
                LLGLSScissor scissor(0, 0, 20, H);
                glClearDepth(storedDepth(3.f));
                mScene.clear(GL_DEPTH_BUFFER_BIT);
                glClearDepth(mReverse ? 0.0 : 1.0);
            }
            mScene.flush();

            idPass({ { quadA(), ID_A } });
            std::vector<U8> ids = read(mIdMap);
            ensure(named(reverse, "covered part marked hidden and drawn"), drawnHidden(at(ids, 12, 32)));
            ensure_equals(named(reverse, "uncovered part marked visible"), (U32)at(ids, 25, 32).b, 255U);
            ensure_equals(named(reverse, "hidden parts keep their id"), idAt(ids, 12, 32), ID_A);

            palette({ { ID_A, RED } }, true);
            std::vector<U8> px = edgePass();
            const U32 hidden = at(px, 8, 32).r;
            const U32 visible = at(px, 25, 44).r;
            ensure_equals(named(reverse, "visible outline at full strength"), visible, 255U);
            ensure(named(reverse, "hidden outline dimmed to the hidden alpha"), hidden >= 90 && hidden <= 115);

            Draw left_out = { quadA(), ID_A };
            left_out.mShowHidden = false;
            idPass({ left_out });
            ids = read(mIdMap);
            ensure_equals(named(reverse, "covered part marked not drawn"), (U32)at(ids, 12, 32).b, 0U);
            ensure_equals(named(reverse, "uncovered part still visible"), (U32)at(ids, 25, 32).b, 255U);
            palette({ { ID_A, RED } }, false);
            px = edgePass();
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
                            hidden += at(ids, x, y).b != 255 ? 1 : 0;
                        }
                    }
                }
                const std::string at_divisor = " at render resolution / " + std::to_string(divisor);
                ensure(named(reverse, "the surface covers pixels" + at_divisor), covered > 200);
                ensure_equals(named(reverse, "none of its own pixels hidden by itself" + at_divisor), hidden, size_t(0));
            }
        }
    }

    // The scissor is the objects' projected box grown by the contour's reach: the scissored edge pass draws exactly
    // what the unscissored one does. Without the growth the line outside the box is clipped.
    template<> template<>
    void alselectionoutline_object_t::test<5>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            idPass({ { quadA(), ID_A }, { quadB(), ID_B } });
            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);

            const std::vector<U8> full = edgePass();
            const S32 reach = Outline::lineReach(view().mWidth);
            ensure_equals(named(reverse, "the contour reaches its width and a pixel"), reach, 3);
            const LLRect rect = boxRect(reach);
            ensure_equals(named(reverse, "left: column 8.57 floored, less the reach"), rect.mLeft, 8 - reach);
            ensure_equals(named(reverse, "right: column 55.43 ceiled, plus the reach"), rect.mRight, 56 + reach);
            ensure_equals(named(reverse, "bottom: row 20.28 floored, less the reach"), rect.mBottom, 20 - reach);
            ensure_equals(named(reverse, "top: row 43.72 ceiled, plus the reach"), rect.mTop, 44 + reach);

            const std::vector<U8> scissored = edgePass(&rect);
            ensure(named(reverse, "something is outlined"), lit(full) > 0);
            ensure(named(reverse, "the scissored pass draws every outline pixel"), scissored == full);

            const LLRect tight = boxRect(0);
            const std::vector<U8> clipped = edgePass(&tight);
            ensure(named(reverse, "a box without the reach clips the line"), lit(clipped) < lit(full));
            ensure(named(reverse, "the line's outer pixels are what it loses"), !black(at(full, 7, 32)) && black(at(clipped, 7, 32)));
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
        ensure("a box reaching behind the eye is drawn", Outline::scissorRect(proj, straddling, W, H, 3, rect));
        ensure("over the whole target", rect == LLRect(0, H, W, 0));

        const LLVector4a behind[2] = { LLVector4a(-1.f, -1.f, 2.f), LLVector4a(1.f, 1.f, 5.f) };
        ensure("a box behind the eye is not", !Outline::scissorRect(proj, behind, W, H, 3, rect));

        const LLVector4a aside[2] = { LLVector4a(30.f, -1.f, -5.f), LLVector4a(32.f, 1.f, -5.f) };
        ensure("a box off to the side is not", !Outline::scissorRect(proj, aside, W, H, 3, rect));
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
            const std::vector<U8> px = edgePass();
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
        ensure_equals("one pair of rows up to 255", Outline::paletteRows(255), 2U);
        ensure_equals("two from 256", Outline::paletteRows(256), 4U);

        std::vector<U8> texels((size_t)Outline::PALETTE_WIDTH * 4 * 4, 0);
        Outline::writePalette(texels, 300, LLColor4(1.f, 0.5f, 0.f, 0.8f), true);
        const size_t visible = ((size_t)2 * Outline::PALETTE_WIDTH + 44) * 4;
        const size_t hidden = ((size_t)3 * Outline::PALETTE_WIDTH + 44) * 4;
        ensure_equals("visible red", (U32)texels[visible], 255U);
        ensure_equals("visible green", (U32)texels[visible + 1], 128U);
        ensure_equals("visible alpha", (U32)texels[visible + 3], 204U);
        ensure_equals("hidden keeps the colour", (U32)texels[hidden + 1], 128U);
        ensure_equals("hidden alpha dimmed", (U32)texels[hidden + 3], (U32)ll_round(0.8f * Outline::HIDDEN_ALPHA * 255.f));

        Outline::writePalette(texels, 300, LLColor4(1.f, 0.5f, 0.f, 0.8f), false);
        ensure_equals("hidden transparent with hidden parts off", (U32)texels[hidden + 3], 0U);
    }

    // The widths: the contour AlchemySelectionOutlineWidth wide, held to 1 to 16, and the edges between objects a
    // pixel, at a UI scale of 1; both scaled with it and rounded, never under a pixel; the world and the HUD alike.
    template<> template<>
    void alselectionoutline_object_t::test<9>()
    {
        ensure_equals("the setting's default", Outline::contourWidth(Outline::DEFAULT_CONTOUR_WIDTH, 1.f), 4);
        ensure_equals("the default at 1.5", Outline::contourWidth(Outline::DEFAULT_CONTOUR_WIDTH, 1.5f), 6);
        ensure_equals("the default at 2", Outline::contourWidth(Outline::DEFAULT_CONTOUR_WIDTH, 2.f), 8);
        ensure_equals("a setting between pixels rounds", Outline::contourWidth(2.5f, 1.f), 3);
        ensure_equals("a setting under 1 held to 1", Outline::contourWidth(0.25f, 1.f), 1);
        ensure_equals("a setting over 16 held to 16", Outline::contourWidth(40.f, 1.f), 16);
        ensure_equals("and scaled after", Outline::contourWidth(40.f, 2.f), 32);
        ensure_equals("a setting that is not a number held to 1", Outline::contourWidth(std::numeric_limits<F32>::quiet_NaN(), 1.f), 1);
        ensure_equals("never under a pixel", Outline::contourWidth(1.f, 0.2f), 1);
        ensure_equals("edges at 1", Outline::lineWidth(Outline::INNER_WIDTH, 1.f), 1);
        ensure_equals("edges at 2", Outline::lineWidth(Outline::INNER_WIDTH, 2.f), 2);
        ensure_equals("a line reaches its width and a pixel", Outline::lineReach(4), 5);

        const LLMatrix4a hud = al_ortho(-0.5f, 0.5f, -0.5f, 0.5f, 0.f, 10.f);
        const LLMatrix4a world = al_perspective(FOV, 1.f, NEAR_PLANE, FORWARD_FAR);
        for (F32 setting : { 1.f, Outline::DEFAULT_CONTOUR_WIDTH, 16.f, 40.f })
        {
            for (F32 ui_scale : { 1.f, 1.5f, 2.f })
            {
                const Outline::View in_world = Outline::makeView(identity(), world, setting, ui_scale, false);
                const Outline::View on_hud = Outline::makeView(identity(), hud, setting, ui_scale, true);
                const std::string with = " for " + std::to_string(setting) + " at UI scale " + std::to_string(ui_scale);
                ensure_equals("the contour" + with, in_world.mWidth, Outline::contourWidth(setting, ui_scale));
                ensure_equals("the edges" + with, in_world.mInnerWidth, Outline::lineWidth(Outline::INNER_WIDTH, ui_scale));
                ensure_equals("the HUD's contour as the world's" + with, on_hud.mWidth, in_world.mWidth);
                ensure_equals("the HUD's edges as the world's" + with, on_hud.mInnerWidth, in_world.mInnerWidth);
                ensure("the HUD's view is the HUD's" + with, on_hud.mHUD && !in_world.mHUD);
            }
        }
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
            ensure(named(reverse, "the world's pass hides A behind it"), drawnHidden(at(read(mIdMap), 20, 32)));

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
            const std::vector<U8> px = edgePass();
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

    // The contour is drawn as wide as the setting and the UI scale make it, held to the setting's range, and as many
    // pixels wide in the world, through a perspective with the scene's depth read, as on the HUD, through an
    // orthographic projection with none: its width is in pixels.
    template<> template<>
    void alselectionoutline_object_t::test<12>()
    {
        const std::pair<F32, F32> settings[] = { { Outline::DEFAULT_CONTOUR_WIDTH, 1.f }, { Outline::DEFAULT_CONTOUR_WIDTH, 2.f },
                                                 { 1.f, 1.f }, { 2.5f, 1.f }, { 0.25f, 1.f }, { 40.f, 1.f } };
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            for (const auto& [setting, ui_scale] : settings)
            {
                const std::string with = " for " + std::to_string(setting) + " at UI scale " + std::to_string((S32)ui_scale);
                const S32 width = Outline::contourWidth(setting, ui_scale);

                // The HUD's: its left edge is column 20.
                pixelIds({ { pixels(20, 52, 16, 48), ID_A } });
                std::vector<U8> px = edgePass(Outline::makeView(identity(), pixelProjection(), setting, ui_scale, true));
                S32 hud_band = 0;
                for (S32 x = 19; x >= 0 && at(px, x, 32).r == 255; --x)
                {
                    ++hud_band;
                }
                ensure_equals(named(reverse, "the HUD's contour is the setting's width" + with), hud_band, width);
                ensure(named(reverse, "nothing past it" + with), black(at(px, 19 - width - 1, 32)));

                // The world's: A's left edge is column 9, room for a line eight pixels wide.
                if (width <= 8)
                {
                    idPass({ { quadA(), ID_A } });
                    px = edgePass(Outline::makeView(identity(), mProjection, setting, ui_scale, false));
                    S32 world_band = 0;
                    for (S32 x = 8; x >= 0 && at(px, x, 32).r == 255; --x)
                    {
                        ++world_band;
                    }
                    ensure_equals(named(reverse, "the world's is the same" + with), world_band, hud_band);
                }
            }
        }
    }

    // A face's texture alpha cuts it out of the id target: a mask at its cutoff, a blend at BLEND_ALPHA_CUTOFF,
    // through the base colour's transform and the face's texture animation; an opaque face is not cut at all, and
    // the face's colour, transparent or not, never is. The ramp's alpha is its column, and pixel column c samples
    // column 4c + 2 of it.
    template<> template<>
    void alselectionoutline_object_t::test<13>()
    {
        ensure_equals("a mask at its cutoff", Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_MASK, 64, true), 64.f / 255.f);
        ensure_equals("a blend at the blend cutoff", Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_BLEND, 64, true), Outline::BLEND_ALPHA_CUTOFF);
        ensure_equals("no material blends", Outline::legacyAlphaCutoff(false, 0, 0, true), Outline::BLEND_ALPHA_CUTOFF);
        ensure_equals("opaque", Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_NONE, 64, true), Outline::NO_ALPHA_TEST);
        ensure_equals("emissive", Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE, 64, true), Outline::NO_ALPHA_TEST);
        ensure_equals("a texture without alpha", Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_MASK, 64, false), Outline::NO_ALPHA_TEST);
        ensure_equals("GLTF mask", Outline::gltfAlphaCutoff(LLGLTFMaterial::ALPHA_MODE_MASK, 0.25f), 0.25f);
        ensure_equals("GLTF blend", Outline::gltfAlphaCutoff(LLGLTFMaterial::ALPHA_MODE_BLEND, 0.25f), Outline::BLEND_ALPHA_CUTOFF);
        ensure_equals("GLTF opaque", Outline::gltfAlphaCutoff(LLGLTFMaterial::ALPHA_MODE_OPAQUE, 0.25f), Outline::NO_ALPHA_TEST);

        // Halved along u, as a GLTF transform and as a texture animation: column c samples 2c + 1. The transform is
        // packed as LLGLTFMaterial::TextureTransform::getPacked packs a scale of (0.5, 1).
        const LLGLTFMaterial::TextureTransform::Pack halved_pack = { 0.5f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
        LLMatrix4a halved_matrix;
        halved_matrix.setIdentity();
        halved_matrix.mMatrix[0].getF32ptr()[0] = 0.5f;

        // A transparent face colour, as a prim made invisible has.
        const LLColor4U invisible(255, 255, 255, 0);

        for (bool reverse : conventions())
        {
            setUp(reverse);
            const U32 texture = ramp();
            const F32 mask = Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_MASK, 64, true);
            const F32 blend = Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_BLEND, 0, true);
            const F32 opaque = Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_NONE, 64, true);

            // Column 15 samples alpha 62, column 16 alpha 66.
            std::vector<U8> ids = pixelIds({ { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, mask, texture } });
            ensure_equals(named(reverse, "a mask cuts under its cutoff"), idAt(ids, 15, 32), 0U);
            ensure_equals(named(reverse, "and keeps over it"), idAt(ids, 16, 32), ID_A);
            ensure_equals(named(reverse, "and keeps the opaque end"), idAt(ids, 63, 32), ID_A);

            // Column 31 samples alpha 126, column 32 alpha 130.
            ids = pixelIds({ { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, blend, texture } });
            ensure_equals(named(reverse, "a blend cuts under half"), idAt(ids, 31, 32), 0U);
            ensure_equals(named(reverse, "and keeps over it"), idAt(ids, 32, 32), ID_A);

            ids = pixelIds({ { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, opaque, texture } });
            ensure_equals(named(reverse, "an opaque face keeps its transparent texels"), idAt(ids, 0, 32), ID_A);
            ensure_equals(named(reverse, "and the rest"), idAt(ids, 40, 32), ID_A);

            ids = pixelIds({ { pixels(0, W, 16, 48, &invisible), ID_A, Outline::PRIORITY_ROOT, mask, texture } });
            ensure_equals(named(reverse, "a transparent face colour cuts nothing more"), idAt(ids, 16, 32), ID_A);
            ensure_equals(named(reverse, "and its texture still cuts"), idAt(ids, 15, 32), 0U);

            // Column 20 samples alpha 41 halved, 82 not; column 40 81 halved.
            Draw transformed = { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, mask, texture };
            transformed.mTransform = &halved_pack;
            ids = pixelIds({ transformed });
            ensure_equals(named(reverse, "the base colour transform moves the cut"), idAt(ids, 20, 32), 0U);
            ensure_equals(named(reverse, "and keeps past it"), idAt(ids, 40, 32), ID_A);

            Draw animated = { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, mask, texture };
            animated.mTextureMatrix = &halved_matrix;
            ids = pixelIds({ animated });
            ensure_equals(named(reverse, "the texture animation moves the cut"), idAt(ids, 20, 32), 0U);
            ensure_equals(named(reverse, "and keeps past it"), idAt(ids, 40, 32), ID_A);

            // A cut-out face shows what is behind it: here another object, which is outlined through the hole.
            ids = pixelIds({ { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, mask, texture },
                             { quad(0.f, (F32)W, 0.f, (F32)H, PIXEL_Z - 1.f, PIXEL_Z - 1.f), ID_B } });
            ensure_equals(named(reverse, "the one behind shows through the cut"), idAt(ids, 15, 32), ID_B);
            ensure_equals(named(reverse, "and not through the rest"), idAt(ids, 16, 32), ID_A);
        }
    }

    // The contour's anti-aliasing: across a diagonal edge, walking away from it along a row, the line's coverage
    // never rises, starts solid, takes values between and ends at nothing. Along a row the distance from a
    // 45-degree edge grows by sqrt(1/2) a pixel a pixel, so the walk takes twice the line's reach.
    template<> template<>
    void alselectionoutline_object_t::test<14>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // A diamond about (32.25, 32.25), 24 pixels to each point: its edge from (32.25, 8.25) to
            // (8.25, 32.25) runs through x = 40 - y, between pixel centres, so the pixels it covers are not left to
            // the rasteriser's tie rule.
            const LLVector3 diamond[4] = { LLVector3(32.25f, 8.25f, PIXEL_Z), LLVector3(56.25f, 32.25f, PIXEL_Z),
                                           LLVector3(32.25f, 56.25f, PIXEL_Z), LLVector3(8.25f, 32.25f, PIXEL_Z) };
            const std::vector<U8> ids = pixelIds({ { quadAt(diamond), ID_A } });
            palette({ { ID_A, RED } }, true);
            const Outline::View lines = view();
            const std::vector<U8> px = edgePass(lines);
            const S32 walk = 2 * Outline::lineReach(lines.mWidth);

            S32 between = 0;
            for (S32 y = 14; y <= 28; ++y)
            {
                const std::string row = ", row " + std::to_string(y);
                S32 first = -1;
                for (S32 x = 0; x < W && first < 0; ++x)
                {
                    first = idAt(ids, x, y) == ID_A ? x : -1;
                }
                ensure_equals(named(reverse, "the diamond's edge" + row), first, 40 - y);

                U32 last = 255;
                for (S32 x = first - 1; x >= first - walk; --x)
                {
                    const U32 r = at(px, x, y).r;
                    ensure(named(reverse, "coverage never rises away from the edge" + row), r <= last);
                    between += (r > 0 && r < 255) ? 1 : 0;
                    last = r;
                }
                ensure_equals(named(reverse, "solid beside the edge" + row), (U32)at(px, first - 1, y).r, 255U);
                ensure_equals(named(reverse, "nothing past the reach" + row), last, 0U);
            }
            ensure(named(reverse, "the edge is anti-aliased"), between >= 15);
        }
    }

    // A feature a pixel across is outlined all the way round: every pixel in the disk the line covers is drawn,
    // including those no straight or diagonal line from the feature passes through.
    template<> template<>
    void alselectionoutline_object_t::test<15>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            pixelIds({ { pixels(32, 33, 32, 33), ID_A } });
            palette({ { ID_A, RED } }, true);
            const Outline::View lines = view();
            const std::vector<U8> px = edgePass(lines);

            const S32 reach = Outline::lineReach(lines.mWidth);
            for (S32 dy = -reach - 1; dy <= reach + 1; ++dy)
            {
                for (S32 dx = -reach - 1; dx <= reach + 1; ++dx)
                {
                    if (dx == 0 && dy == 0)
                    {
                        continue;
                    }
                    const F32 dist = sqrtf((F32)(dx * dx + dy * dy));
                    const U32 r = at(px, 32 + dx, 32 + dy).r;
                    const std::string where = " at (" + std::to_string(dx) + ", " + std::to_string(dy) + ")";
                    if (dist <= (F32)lines.mWidth)
                    {
                        ensure_equals(named(reverse, "solid within the width" + where), r, 255U);
                    }
                    else if (dist < (F32)lines.mWidth + 0.9f)
                    {
                        ensure(named(reverse, "anti-aliased past it" + where), r > 0 && r < 255);
                    }
                    else if (dist >= (F32)reach)
                    {
                        ensure_equals(named(reverse, "nothing at the reach" + where), r, 0U);
                    }
                }
            }
            // Two across and one up: off every line of taps in eight directions.
            ensure(named(reverse, "the knight's move is outlined"), at(px, 34, 33).r > 0);
        }
    }

    // Priorities: ids are given the highest priority first, and in the order they were added within one.
    // Where a root and a child meet, the root's contour is laid over the child's, which shows under it only where
    // the root's fades; the edge between them is drawn on the root's side, a pixel wide, at INNER_OPACITY, and not
    // on the child's.
    template<> template<>
    void alselectionoutline_object_t::test<16>()
    {
        const std::vector<Outline::EPriority> priorities = { Outline::PRIORITY_CHILD, Outline::PRIORITY_ROOT,
                                                             Outline::PRIORITY_CONTEXT, Outline::PRIORITY_SUBTRACT,
                                                             Outline::PRIORITY_CHILD, Outline::PRIORITY_FOCUS,
                                                             Outline::PRIORITY_INSPECT };
        std::vector<U32> order;
        Outline::priorityOrder(priorities, order);
        ensure("the highest priority first, in the order added within one", order == std::vector<U32>({ 3, 5, 6, 1, 0, 4, 2 }));

        constexpr U32 ROOT = 1;
        constexpr U32 CHILD = 2;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // The root over columns 16 to 31, the child 32 to 47, both rows 16 to 47.
            const std::vector<U8> ids = pixelIds({ { pixels(16, 32, 16, 48), ROOT, Outline::PRIORITY_ROOT },
                                                   { pixels(32, 48, 16, 48), CHILD, Outline::PRIORITY_CHILD } });
            ensure_equals(named(reverse, "the child's priority in alpha"), (U32)at(ids, 40, 32).a, (U32)Outline::PRIORITY_CHILD);
            palette({ { ROOT, RED }, { CHILD, GREEN } }, true);
            const std::vector<U8> px = edgePass();

            // Above the child, a pixel from it and the root's corner a knight's move away.
            const Pixel over = at(px, 33, 48);
            ensure(named(reverse, "the root's contour over the child's"), over.r > over.g && over.r > 200);
            ensure(named(reverse, "the child's under it where it fades"), over.g > 0);
            ensure(named(reverse, "the root's alone beside it"), red(at(px, 31, 48)) && at(px, 31, 48).r == 255);
            ensure(named(reverse, "the child's alone past the root's reach"), green(at(px, 36, 48)) && at(px, 36, 48).g == 255);

            const Pixel edge = at(px, 31, 32);
            const U32 faint = (U32)ll_round(255.f * Outline::INNER_OPACITY);
            ensure(named(reverse, "the edge on the root's side, in its colour"), red(edge));
            ensure(named(reverse, "at INNER_OPACITY"), (U32)edge.r + 2 >= faint && (U32)edge.r <= faint + 2);
            ensure(named(reverse, "a pixel wide, where the contour is two"), black(at(px, 30, 32)));
            ensure(named(reverse, "and not on the child's side"), black(at(px, 32, 32)));
            ensure(named(reverse, "the insides left alone"), black(at(px, 20, 32)) && black(at(px, 40, 32)));
        }
    }

    // The tile pass's early out draws exactly what searching every pixel's disk draws: with edges along tile
    // borders and across them, features a pixel thin at tile corners, objects at the target's edges, an island
    // inside another, both priorities, and lines from one pixel wide to sixteen, whose reach spans five tiles. A
    // change along a tile's left or lower border belongs to the tile beyond it, which flags it; a tile inside an
    // object flags nothing.
    template<> template<>
    void alselectionoutline_object_t::test<17>()
    {
        const std::pair<S32, S32> line_widths[] = { { 1, 1 }, { 2, 1 }, { 4, 1 }, { 8, 2 }, { 16, 2 } };
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { 1, RED }, { 2, GREEN }, { 3, LLColor4(0.f, 0.f, 1.f, 1.f) }, { 4, LLColor4(1.f, 1.f, 0.f, 0.5f) } }, true);

            // A strip a pixel and a half across, running up and to the right at 45 degrees.
            const LLVector3 strip[4] = { LLVector3(4.f, 30.f, PIXEL_Z), LLVector3(5.5f, 30.f, PIXEL_Z),
                                         LLVector3(21.5f, 46.f, PIXEL_Z), LLVector3(20.f, 46.f, PIXEL_Z) };
            const std::vector<std::pair<std::string, std::vector<Draw>>> scenes = {
                { "edges on tile borders", { { pixels(16, 32, 16, 40), 1, Outline::PRIORITY_ROOT },
                                             { pixels(32, 48, 16, 40), 2, Outline::PRIORITY_CHILD },
                                             { pixels(16, 48, 40, 48), 3, Outline::PRIORITY_CHILD } } },
                { "edges across tile borders", { { pixels(13, 29, 21, 37), 1, Outline::PRIORITY_ROOT },
                                                 { pixels(29, 45, 21, 37), 2, Outline::PRIORITY_CHILD },
                                                 { pixels(50, 53, 50, 61), 3, Outline::PRIORITY_CHILD } } },
                { "thin features", { { pixels(23, 24, 23, 24), 1, Outline::PRIORITY_ROOT },
                                     { pixels(40, 41, 40, 41), 2, Outline::PRIORITY_CHILD },
                                     { pixels(39, 40, 8, 56), 3, Outline::PRIORITY_CHILD },
                                     { pixels(0, W, 7, 8), 4, Outline::PRIORITY_CHILD },
                                     { quadAt(strip), 1, Outline::PRIORITY_ROOT } } },
                { "objects at the target's corners", { { pixels(0, 10, 54, H), 1, Outline::PRIORITY_ROOT },
                                                       { pixels(58, W, 0, 5), 2, Outline::PRIORITY_CHILD } } },
                { "an island", { { pixels(20, 44, 20, 44), 2, Outline::PRIORITY_CHILD },
                                 { pixels(28, 36, 28, 36), 1, Outline::PRIORITY_ROOT } } },
                { "slanted surfaces touching, and one in front", { { quad(8.f, 40.f, 8.f, 30.f, -1.f, -5.f), 1, Outline::PRIORITY_ROOT },
                                                                   { quad(40.f, 56.f, 8.f, 30.f, -5.f, -7.f), 2, Outline::PRIORITY_CHILD },
                                                                   { quad(20.f, 50.f, 24.f, 44.f, -0.5f, -0.5f), 3, Outline::PRIORITY_CHILD } } },
                { "one object over itself, its jumps on tile borders", { { pixels(8, 56, 8, 56), 1, Outline::PRIORITY_ROOT },
                                                                         { quad(24.f, 40.f, 24.f, 40.f, PIXEL_Z + 1.f, PIXEL_Z + 1.f), 1, Outline::PRIORITY_ROOT },
                                                                         { quad(31.f, 50.f, 13.f, 21.f, PIXEL_Z + 0.5f, PIXEL_Z + 0.5f), 1, Outline::PRIORITY_ROOT } } },
                { "fronds of one mesh over each other and another object", { { quad(4.f, 40.f, 30.f, 34.f, -3.f, -6.f), 1, Outline::PRIORITY_CHILD },
                                                                            { quad(10.f, 14.f, 4.f, 60.f, -2.f, -2.f), 1, Outline::PRIORITY_CHILD },
                                                                            { quad(20.f, 60.f, 40.f, 46.f, -1.f, -4.f), 1, Outline::PRIORITY_CHILD },
                                                                            { quad(30.f, 31.f, 0.f, 64.f, -1.5f, -1.5f), 1, Outline::PRIORITY_CHILD },
                                                                            { pixels(36, 60, 20, 60), 2, Outline::PRIORITY_ROOT } } },
            };

            for (const auto& [name, draws] : scenes)
            {
                pixelIds(draws);
                for (const auto& [width, inner_width] : line_widths)
                {
                    const Outline::View lines = widths(width, inner_width);
                    const std::string what = name + ", lines " + std::to_string(width) + " and " + std::to_string(inner_width);
                    const std::vector<U8> untiled = edgePass(lines, nullptr, true);
                    const std::vector<U8> tiled = edgePass(lines);
                    ensure(named(reverse, what + ": something is drawn"), lit(untiled) > 0);
                    ensure(named(reverse, what + ": tiled as untiled"), tiled == untiled);
                }

                const std::vector<U16> records = tileRecords();
                size_t empty = 0;
                for (size_t i = 0; i < records.size(); i += 4)
                {
                    empty += records[i + 1] == 0 ? 1 : 0;
                }
                ensure(named(reverse, name + ": some tiles hold nothing"), empty > 0);
            }

            // The first scene again, its edges on tile borders: a tile holds the ids of its own texels, and a tile
            // across the border from another object's holds none of that object's.
            pixelIds(scenes[0].second);
            markPasses(view());
            std::vector<U16> records = tileRecords();
            const S32 columns = (S32)Outline::tileCount(W);
            auto record = [&](S32 column, S32 row, S32 i) { return (U32)records[((size_t)row * columns + column) * 4 + i]; };
            ensure(named(reverse, "the tile left of the root holds nothing"), record(1, 2, 0) == 0 && record(1, 2, 1) == 0);
            ensure(named(reverse, "the tile inside the root holds the root alone"), record(2, 2, 0) == 1 && record(2, 2, 1) == 1);
            ensure(named(reverse, "the tile inside the child beside it, the child alone"), record(4, 2, 0) == 2 && record(4, 2, 1) == 2);
            ensure(named(reverse, "the tile under the child above, the root alone"), record(2, 4, 0) == 1 && record(2, 4, 1) == 1);
            ensure(named(reverse, "no jump in it"), record(2, 2, 2) == 0);
            ensure(named(reverse, "a tile far away holds nothing"), record(0, 0, 1) == 0);

            // The second, across tile borders: the tile at column and row 3 holds both the root and the child.
            pixelIds(scenes[1].second);
            markPasses(view());
            records = tileRecords();
            ensure(named(reverse, "a tile across the edge holds both"), record(3, 3, 0) == 1 && record(3, 3, 1) == 2);

            // The tile pass over the tiles under a rect holding every selected texel and one more around them, as the
            // viewer runs it under its scissor, draws the same.
            pixelIds(scenes[1].second);
            const LLRect around(12, 62, 54, 20);
            for (const auto& [width, inner_width] : line_widths)
            {
                const Outline::View lines = widths(width, inner_width);
                const std::string what = "under a rect, lines " + std::to_string(width);
                ensure(named(reverse, what), edgePass(lines, nullptr, false, &around) == edgePass(lines, nullptr, true));
            }
        }
    }

    // Faces are culled as the scene culls them: a single-sided face seen from behind, which the scene does not draw,
    // takes no pixel from the object behind it, whose contour stays whole; a double-sided one is drawn, and outlined.
    template<> template<>
    void alselectionoutline_object_t::test<18>()
    {
        constexpr U32 FRONT = 1;
        constexpr U32 REAR = 2;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { FRONT, RED }, { REAR, GREEN } }, true);
            // The rear over columns and rows 16 to 47; the front over 24 to 39, nearer, wound clockwise: its back.
            const LLVector3 back_on[4] = { LLVector3(24.f, 24.f, PIXEL_Z + 1.f), LLVector3(24.f, 40.f, PIXEL_Z + 1.f),
                                           LLVector3(40.f, 40.f, PIXEL_Z + 1.f), LLVector3(40.f, 24.f, PIXEL_Z + 1.f) };
            LLVertexBuffer* rear = pixels(16, 48, 16, 48);
            LLVertexBuffer* front = quadAt(back_on);

            std::vector<U8> ids = pixelIds({ { rear, REAR, Outline::PRIORITY_CHILD }, { front, FRONT, Outline::PRIORITY_ROOT } });
            ensure_equals(named(reverse, "a single-sided face from behind takes no pixel"), idAt(ids, 32, 32), REAR);
            std::vector<U8> px = edgePass();
            ensure(named(reverse, "nothing traces it"), black(at(px, 23, 32)) && black(at(px, 24, 32)) && black(at(px, 40, 32)));
            ensure_equals(named(reverse, "the rear's contour is whole"), (U32)at(px, 15, 32).g, 255U);

            Draw double_sided = { front, FRONT, Outline::PRIORITY_ROOT };
            double_sided.mDoubleSided = true;
            ids = pixelIds({ { rear, REAR, Outline::PRIORITY_CHILD }, double_sided });
            ensure_equals(named(reverse, "a double-sided face from behind is drawn"), idAt(ids, 32, 32), FRONT);
            px = edgePass();
            ensure(named(reverse, "and outlined over the rear"), red(at(px, 23, 32)) && at(px, 23, 32).r == 255);
        }
    }

    // A visible object in front of a selected one the scene hides is outlined in full over it: its contour neither
    // turns into an edge between the two nor goes on the hidden one's side. With hidden parts drawn, the visible
    // object's contour is still drawn at full strength, and the hidden one's own dimmed.
    template<> template<>
    void alselectionoutline_object_t::test<19>()
    {
        constexpr U32 ROOT = 1;
        constexpr U32 FROND = 2;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // The root, six metres out over columns 12 to 51 and rows 18 to 46, behind something five metres out;
            // the frond four metres out over columns and rows 25 to 38, in front of it.
            occlude(5.f);
            LLVertexBuffer* root = quad(-2.f, 2.f, -1.5f, 1.5f, -6.f, -6.f);
            LLVertexBuffer* frond = quad(-0.5f, 0.5f, -0.5f, 0.5f, -4.f, -4.f);

            for (bool show_hidden : { false, true })
            {
                const std::string hidden_parts = show_hidden ? ", hidden parts drawn" : ", hidden parts left out";
                Draw hidden_root = { root, ROOT, Outline::PRIORITY_ROOT };
                hidden_root.mShowHidden = show_hidden;
                idPass({ hidden_root, { frond, FROND, Outline::PRIORITY_CHILD } });
                palette({ { ROOT, RED }, { FROND, GREEN } }, show_hidden);
                const std::vector<U8> px = edgePass();

                ensure(named(reverse, "the frond's contour over the root" + hidden_parts),
                       green(at(px, 24, 32)) && at(px, 24, 32).g == 255 && green(at(px, 23, 32)) && at(px, 23, 32).g == 255);
                ensure(named(reverse, "above it too" + hidden_parts), green(at(px, 32, 39)) && at(px, 32, 39).g == 255);
                ensure(named(reverse, "as wide as a contour" + hidden_parts), black(at(px, 21, 32)));
                ensure(named(reverse, "nothing inside the frond" + hidden_parts), black(at(px, 32, 32)) && black(at(px, 25, 32)));
                if (show_hidden)
                {
                    const U32 dimmed = at(px, 11, 32).r;
                    ensure(named(reverse, "the hidden root's contour dimmed"), red(at(px, 11, 32)) && dimmed >= 90 && dimmed <= 115);
                }
                else
                {
                    ensure(named(reverse, "the hidden root's contour left out"), black(at(px, 11, 32)));
                }
            }
        }
    }

    // A child in front of another draws its whole contour over it; two surfaces that touch on one plane, here a
    // wall seen at a slant, whose depth changes by some percent a pixel, draw only the thin edge between them, on
    // the side of the higher priority.
    template<> template<>
    void alselectionoutline_object_t::test<20>()
    {
        const LLColor4 BLUE(0.f, 0.f, 1.f, 1.f);
        for (bool reverse : conventions())
        {
            setUp(reverse);

            // Six metres out over columns 12 to 51, and four metres out over columns and rows 25 to 38.
            idPass({ { quad(-2.f, 2.f, -1.5f, 1.5f, -6.f, -6.f), 2, Outline::PRIORITY_CHILD },
                     { quad(-0.5f, 0.5f, -0.5f, 0.5f, -4.f, -4.f), 3, Outline::PRIORITY_CHILD } });
            palette({ { 2, GREEN }, { 3, BLUE } }, true);
            std::vector<U8> px = edgePass();
            const Pixel over = at(px, 24, 32);
            ensure(named(reverse, "the front child's contour over the rear"), over.b == 255 && over.r == 0 && over.g == 0);
            ensure(named(reverse, "its full width"), at(px, 23, 32).b == 255 && at(px, 32, 39).b == 255);
            ensure(named(reverse, "nothing inside it"), black(at(px, 32, 32)));

            // A wall from four metres out at the left to twelve at the right, split at eight, columns 31 and 32.
            idPass({ { quad(-1.6f, 0.f, -1.f, 1.f, -4.f, -8.f), ID_A, Outline::PRIORITY_ROOT },
                     { quad(0.f, 1.6f, -1.f, 1.f, -8.f, -12.f), ID_B, Outline::PRIORITY_CHILD } });
            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);
            px = edgePass();
            const Pixel edge = at(px, 31, 32);
            const U32 faint = (U32)ll_round(255.f * Outline::INNER_OPACITY);
            ensure(named(reverse, "the edge on the root's side"), red(edge) && (U32)edge.r + 2 >= faint && (U32)edge.r <= faint + 2);
            ensure(named(reverse, "no contour on the child's side"), black(at(px, 32, 32)) && black(at(px, 33, 32)));
            ensure(named(reverse, "none on the root's"), black(at(px, 30, 32)));
        }
    }

    // A selected object behind one the scene hides, its hidden parts left out, does not trace the hidden one's shape:
    // a frond behind a rock, in front of the visible root. And an object not drawn neither draws nor masks the
    // contour of one that is, though it is the nearer and of the higher priority.
    template<> template<>
    void alselectionoutline_object_t::test<21>()
    {
        constexpr U32 ROOT = 1;
        constexpr U32 FROND = 2;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // The root six metres out over columns 12 to 51; the frond five metres out over columns and rows 26 to
            // 37, behind a rock four and a half metres out over exactly those.
            occlude(4.5f, 26, 26, 12, 12);
            Draw frond = { quad(-0.5f, 0.5f, -0.5f, 0.5f, -5.f, -5.f), FROND, Outline::PRIORITY_CHILD };
            frond.mShowHidden = false;
            idPass({ { quad(-2.f, 2.f, -1.5f, 1.5f, -6.f, -6.f), ROOT, Outline::PRIORITY_ROOT }, frond });
            const std::vector<U8> ids = read(mIdMap);
            ensure_equals(named(reverse, "the frond is over the root"), idAt(ids, 32, 32), FROND);
            ensure_equals(named(reverse, "and not drawn"), (U32)at(ids, 32, 32).b, 0U);
            palette({ { ROOT, RED }, { FROND, GREEN } }, false);
            std::vector<U8> px = edgePass();
            for (const auto& [x, y] : { std::pair<S32, S32>{ 25, 32 }, { 24, 32 }, { 38, 32 }, { 39, 32 }, { 32, 25 }, { 32, 38 }, { 31, 31 } })
            {
                ensure(named(reverse, "nothing traces the frond at (" + std::to_string(x) + ", " + std::to_string(y) + ")"), black(at(px, x, y)));
            }
            ensure_equals(named(reverse, "the root's own contour drawn"), (U32)at(px, 11, 32).r, 255U);

            // A root five metres out over columns 9 to 31, hidden, and a child beside it over 32 to 54, visible, both
            // rows 21 to 43. Above the root, a pixel from it and a knight's move from the child.
            setUp(reverse);
            occlude(4.f, 0, 0, 32, H);
            Draw root = { quad(-2.f, 0.f, -1.f, 1.f, -5.f, -5.f), ROOT, Outline::PRIORITY_ROOT };
            root.mShowHidden = false;
            idPass({ root, { quad(0.f, 2.f, -1.f, 1.f, -5.f, -5.f), FROND, Outline::PRIORITY_CHILD } });
            palette({ { ROOT, RED }, { FROND, GREEN } }, false);
            px = edgePass();
            const Pixel above = at(px, 30, 44);
            ensure(named(reverse, "the child's contour where the hidden root is nearer"), green(above) && above.g > 200);

            // The two touch: the edge between them goes on the drawn side, the child's, though the root's is the
            // higher priority.
            const Pixel edge = at(px, 32, 32);
            const U32 faint = (U32)ll_round(255.f * Outline::INNER_OPACITY);
            ensure(named(reverse, "the edge on the drawn side"), green(edge) && (U32)edge.g + 2 >= faint && (U32)edge.g <= faint + 2);
            ensure(named(reverse, "not on the hidden root's"), black(at(px, 31, 32)));
        }
    }

    // One object over itself, as one frond of a mesh lies over another: the jump pass marks the near side of the
    // step, and the edge pass draws the near part's whole contour over the far part from it, as another object's,
    // and nothing inside the near part. A gap of three percent of the distance is a jump too. The tile holding the
    // near side records it.
    template<> template<>
    void alselectionoutline_object_t::test<22>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            // The far part six metres out over columns 12 to 51; the near part over columns and rows 25 to 38 at
            // four metres, and at 5.8.
            for (F32 near_z : { -4.f, -5.8f })
            {
                const F32 half = 0.125f * -near_z;
                const std::string at_z = " at " + std::to_string(-near_z) + " m";
                idPass({ { quad(-2.f, 2.f, -1.5f, 1.5f, -6.f, -6.f), ID_A }, { quad(-half, half, -half, half, near_z, near_z), ID_A } });
                const std::vector<U8> px = edgePass();
                const std::vector<U8> marked = read(mMarked);
                ensure(named(reverse, "the near side of the step marked" + at_z), at(marked, 25, 32).a >= 128 && at(marked, 32, 38).a >= 128);
                ensure(named(reverse, "not the far side, nor inside" + at_z), at(marked, 24, 32).a < 128 && at(marked, 32, 32).a < 128);

                ensure(named(reverse, "the near part's contour over the far part" + at_z),
                       red(at(px, 24, 32)) && at(px, 24, 32).r == 255 && at(px, 23, 32).r == 255 && at(px, 32, 39).r == 255);
                ensure(named(reverse, "as wide as a contour" + at_z), black(at(px, 21, 32)));
                ensure(named(reverse, "nothing inside the near part" + at_z), black(at(px, 25, 32)) && black(at(px, 26, 32)) && black(at(px, 32, 32)));
                ensure(named(reverse, "nothing over the far part away from it" + at_z), black(at(px, 15, 32)));
            }

            // The near part's edges on tile borders: the tiles holding them record a jump, a tile of the far part alone
            // none.
            pixelIds({ { pixels(8, 56, 8, 56), ID_A }, { quad(24.f, 40.f, 24.f, 40.f, PIXEL_Z + 1.f, PIXEL_Z + 1.f), ID_A } });
            markPasses(view());
            const std::vector<U16> records = tileRecords();
            const S32 columns = (S32)Outline::tileCount(W);
            ensure(named(reverse, "a tile on the near side's edge records the jump"), records[((size_t)3 * columns + 3) * 4 + 2] != 0);
            ensure(named(reverse, "a tile of the far part alone none"), records[((size_t)1 * columns + 1) * 4 + 2] == 0);
        }
    }

    // A surface bending, not stepping, marks no jump and draws nothing across itself: one mesh's inside corner at
    // 30, 45 and 60 degrees to the view, the outside and the inside of a cylinder out to their rims, and a plane
    // whose depth changes by three to five percent a pixel, in one piece and in two.
    template<> template<>
    void alselectionoutline_object_t::test<23>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);

            std::vector<std::pair<std::string, std::vector<Draw>>> surfaces;
            for (F32 degrees : { 30.f, 45.f, 60.f })
            {
                // Two walls 2.5 m long meeting at a right angle six metres out, the corner away from the eye.
                const F32 angle = degrees * DEG_TO_RAD;
                surfaces.push_back({ "an inside corner at " + std::to_string((S32)degrees) + " degrees",
                                     { { quad(-2.5f * cosf(angle), 0.f, -1.f, 1.f, -6.f + 2.5f * sinf(angle), -6.f), ID_A },
                                       { quad(0.f, 2.5f * sinf(angle), -1.f, 1.f, -6.f, -6.f + 2.5f * cosf(angle)), ID_A } } });
            }
            // Half a cylinder of radius 1.5 m in 48 facets: its outside about an axis six metres out, and its inside
            // about one four metres out.
            for (bool inside : { false, true })
            {
                std::vector<Draw> facets;
                constexpr S32 FACETS = 48;
                for (S32 i = 0; i < FACETS; ++i)
                {
                    const F32 a0 = F_PI * ((F32)i / FACETS - 0.5f);
                    const F32 a1 = F_PI * ((F32)(i + 1) / FACETS - 0.5f);
                    const F32 axis = inside ? -4.f : -6.f;
                    const F32 bulge = inside ? -1.5f : 1.5f;
                    facets.push_back({ quad(1.5f * sinf(a0), 1.5f * sinf(a1), -1.f, 1.f, axis + bulge * cosf(a0), axis + bulge * cosf(a1)), ID_A });
                }
                surfaces.push_back({ inside ? "a cylinder's inside" : "a cylinder's outside", facets });
            }
            surfaces.push_back({ "a slanted plane", { { quad(-1.6f, 1.6f, -1.f, 1.f, -4.f, -12.f), ID_A } } });
            surfaces.push_back({ "a slanted plane in two", { { quad(-1.6f, 0.f, -1.f, 1.f, -4.f, -8.f), ID_A },
                                                             { quad(0.f, 1.6f, -1.f, 1.f, -8.f, -12.f), ID_A } } });

            for (const auto& [name, draws] : surfaces)
            {
                idPass(draws);
                const std::vector<U8> ids = read(mIdMap);
                const std::vector<U8> px = edgePass();
                const std::vector<U8> marked = read(mMarked);
                // Every pixel searching its disk, so what the edge pass would draw is not hidden by the tiles.
                const std::vector<U8> searched = edgePass(view(), nullptr, true);
                size_t covered = 0;
                size_t marks = 0;
                size_t drawn = 0;
                for (S32 y = 0; y < H; ++y)
                {
                    for (S32 x = 0; x < W; ++x)
                    {
                        if (idAt(ids, x, y) == ID_A)
                        {
                            ++covered;
                            marks += at(marked, x, y).a >= 128 ? 1 : 0;
                            drawn += (black(at(px, x, y)) && black(at(searched, x, y))) ? 0 : 1;
                        }
                    }
                }
                ensure(named(reverse, name + ": it covers pixels"), covered > 200);
                ensure_equals(named(reverse, name + ": no jump marked"), marks, size_t(0));
                ensure_equals(named(reverse, name + ": nothing drawn across it"), drawn, size_t(0));
            }
        }
    }
}
