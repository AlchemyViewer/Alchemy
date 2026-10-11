/**
 * @file alselectionoutline_gl_test.cpp
 * @brief ALSelectionOutline's id and edge passes on the hidden window, with the viewer's own shader files: ids,
 *        cut-out faces, the contour's width and anti-aliasing, priorities and the edges between objects, hidden
 *        parts, the scissor, a rigged pose, the HUD, the wireframe, the hover glow and ALHoverGlow's fades, and the
 *        palette, width and scissor helpers.
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

#include "../alhoverglow.h"
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

#include <algorithm>
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
        bool blue(const Pixel& p) { return p.b > 0 && p.r == 0 && p.g == 0; }

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

        // Of the frame `px`, the sum of a column's coverage in red from row `y0` to `y1` - 1, in pixels: the width
        // of a line across it.
        // The rows a column's runs of drawn pixels cover, bottom up, each as its first row and one past its last.
        std::vector<std::pair<S32, S32>> columnRuns(const std::vector<U8>& px, S32 x)
        {
            std::vector<std::pair<S32, S32>> found;
            for (S32 y = 0; y < H; ++y)
            {
                if (black(at(px, x, y)))
                {
                    continue;
                }
                if (!found.empty() && found.back().second == y)
                {
                    found.back().second = y + 1;
                }
                else
                {
                    found.push_back({ y, y + 1 });
                }
            }
            return found;
        }

        F32 ink(const std::vector<U8>& px, S32 x, S32 y0, S32 y1)
        {
            F32 sum = 0.f;
            for (S32 y = y0; y < y1; ++y)
            {
                sum += (F32)at(px, x, y).r / 255.f;
            }
            return sum;
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

    struct alselectionoutline_data
    {
        // Each test starts forward, as the window does, whatever ran before:
        // the ones that set no convention of their own draw under it.
        alselectionoutline_data()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
            setConvention(false);
        }

        // The convention, matrices, viewport and shader the tests set go back
        // with mGL.
        ~alselectionoutline_data()
        {
            tearDown();
        }

        // Unloading a program deletes the shared objects attached to it, so a new convention compiles them again.
        void tearDown()
        {
            LLGLSLShader::unbind();
            mIdProgram.unload();
            mJumpProgram.unload();
            mWireProgram.unload();
            mCopyDepthProgram.unload();
            mTileProgram.unload();
            mOutlineProgram.unload();
            mFullSearchProgram.unload();
            mGlowReachProgram.unload();
            mGlowRowProgram.unload();
            mShaders.clearShaderObjects();

            mIdMap.release();
            mJumps.release();
            mGlowReach.release();
            mGlowRows.release();
            mWindow.release();
            mTiles.release();
            mAllTiles.release();
            mScene.release();
            mFar.release();
            mFrame.release();
            for (U32* texture : { &mPalette, &mRamp, &mClear, &mGlass })
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
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/selectionAlphaF.glsl", GL_FRAGMENT_SHADER));
            mIdProgram.mShaderFiles.push_back(std::make_pair("interface/selectionUtilF.glsl", GL_FRAGMENT_SHADER));
            mIdProgram.mShaderLevel = 1;
            ensure("id program builds with its rigged variant", mIdProgram.createShader(LLGLSLShader::VARIANT_RIGGED));
            ensure("id program has a rigged variant", mIdProgram.mRiggedVariant && mIdProgram.mRiggedVariant != &mIdProgram);

            mWireProgram.mName = "Selection Wireframe Shader";
            mWireProgram.mShaderFiles.clear();
            mWireProgram.mShaderFiles.push_back(std::make_pair("interface/selectionIdV.glsl", GL_VERTEX_SHADER));
            mWireProgram.mShaderFiles.push_back(std::make_pair("interface/selectionWireframeG.glsl", GL_GEOMETRY_SHADER));
            mWireProgram.mShaderFiles.push_back(std::make_pair("interface/selectionWireframeF.glsl", GL_FRAGMENT_SHADER));
            mWireProgram.mShaderFiles.push_back(std::make_pair("interface/selectionAlphaF.glsl", GL_FRAGMENT_SHADER));
            mWireProgram.mShaderFiles.push_back(std::make_pair("interface/selectionUtilF.glsl", GL_FRAGMENT_SHADER));
            mWireProgram.mShaderLevel = 1;
            ensure("wireframe program builds with its rigged variant", mWireProgram.createShader(LLGLSLShader::VARIANT_RIGGED));
            ensure("wireframe program has a rigged variant", mWireProgram.mRiggedVariant && mWireProgram.mRiggedVariant != &mWireProgram);

            // The final blit's depth copy, as LLViewerShaderMgr builds gCopyDepthProgram.
            mCopyDepthProgram.mName = "Copy Depth Shader";
            mCopyDepthProgram.mShaderFiles.clear();
            mCopyDepthProgram.mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
            mCopyDepthProgram.mShaderFiles.push_back(std::make_pair("interface/copyF.glsl", GL_FRAGMENT_SHADER));
            mCopyDepthProgram.clearPermutations();
            mCopyDepthProgram.addPermutation("COPY_DEPTH", "1");
            mCopyDepthProgram.addPermutation("DEPTH_ONLY", "1");
            mCopyDepthProgram.mShaderLevel = 1;
            ensure("copy depth program builds", mCopyDepthProgram.createShader());

            const std::pair<LLGLSLShader*, const char*> passes[] = { { &mJumpProgram, "selectionJumpF.glsl" },
                                                                     { &mTileProgram, "selectionTileF.glsl" },
                                                                     { &mOutlineProgram, "selectionOutlineF.glsl" },
                                                                     { &mFullSearchProgram, "selectionOutlineF.glsl" },
                                                                     { &mGlowReachProgram, "selectionGlowReachF.glsl" },
                                                                     { &mGlowRowProgram, "selectionGlowRowF.glsl" } };
            for (const auto& [program, fragment] : passes)
            {
                program->mName = fragment;
                program->mShaderFiles.clear();
                program->mShaderFiles.push_back(std::make_pair("interface/copyV.glsl", GL_VERTEX_SHADER));
                program->mShaderFiles.push_back(std::make_pair(std::string("interface/") + fragment, GL_FRAGMENT_SHADER));
                if (program != &mGlowReachProgram)
                {
                    program->mShaderFiles.push_back(std::make_pair("interface/selectionUtilF.glsl", GL_FRAGMENT_SHADER));
                }
                program->mShaderLevel = 1;
                program->clearPermutations();
                if (program == &mFullSearchProgram)
                {
                    // The edge pass searching the disk for the glow too: the full search.
                    program->addPermutation("GLOW_DISK", "1");
                }
                ensure(std::string(fragment) + "'s program builds", program->createShader());
            }

            const LLRenderTarget::eDepthFormat depth = mReverse ? LLRenderTarget::DEPTH_FMT_32F : LLRenderTarget::DEPTH_FMT_24;
            ensure("id target", mIdMap.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, depth));
            ensure("jump target", mJumps.allocate(W, H, GL_R8));
            ensure("glow reach target", mGlowReach.allocate(Outline::tileCount(W), Outline::tileCount(H), GL_RGBA16));
            ensure("glow row target", mGlowRows.allocate(W, H, GL_R8));
            ensure("frame target", mFrame.allocate(W, H, GL_RGBA8));
            ensure("window target", mWindow.allocate(W, H, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, depth));
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

        // A 4 x 4 texture whose every texel is white with alpha `alpha`.
        static U32 uniformTexture(U8 alpha)
        {
            std::vector<U8> texels(4 * 4 * 4, 255);
            for (size_t i = 3; i < texels.size(); i += 4)
            {
                texels[i] = alpha;
            }
            U32 texture = 0;
            LLImageGL::generateTextures(1, &texture);
            gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, texture);
            LLImageGL::allocateTexture2D(ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE), GL_RGBA8, 4, 4,
                                         GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
            gGL.getTextureSlot(0)->unbind();
            return texture;
        }

        // A texture that is clear everywhere, as the library's transparent texture (IMG_TRANSPARENT) is.
        U32 clearTexture()
        {
            if (!mClear)
            {
                mClear = uniformTexture(0);
            }
            return mClear;
        }

        // A texture drawn at three tenths everywhere, as a pane of glass is.
        U32 glassTexture()
        {
            if (!mGlass)
            {
                mGlass = uniformTexture(77);
            }
            return mGlass;
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
            // What it asks for (ALSelectionOutline::EPart), which ALSelectionOutline::add keeps as partsFor gives.
            U32 mParts = Outline::PART_CONTOUR | Outline::PART_WIREFRAME;

            U32 parts() const { return Outline::partsFor(mPriority, mParts); }
        };

        // A draw of `buffer` as `id` at PRIORITY_HOVER, as ALHoverGlow adds what the pointer is over: hidden parts
        // left out, as ALSelectionOutline::add leaves them.
        Draw hoverDraw(LLVertexBuffer* buffer, U32 id) const
        {
            Draw draw{ buffer, id, Outline::PRIORITY_HOVER };
            draw.mShowHidden = false;
            return draw;
        }

        // The draws into the bound id or wireframe program, as ALSelectionOutline's face walk draws faces: each with its
        // id, alpha test, texture, texture animation and culling. A rigged program takes `joints`: joint 0's three
        // columns, rotation in .xyz and translation in .w. Only the draws `drawn` passes are drawn. False where a
        // texture had no unit to go to.
        template <typename Drawn>
        bool drawAll(LLGLSLShader& program, const std::vector<Draw>& draws, const F32* joints, Drawn&& drawn)
        {
            bool texture_read = true;
            if (joints)
            {
                const F32 origin[3] = { 0.f, 0.f, 0.f };
                program.uniformMatrix3x4fv(LLShaderMgr::AVATAR_MATRIX, 1, GL_FALSE, joints);
                program.uniform3fv(LLShaderMgr::SKIN_ORIGIN, 1, origin);
            }
            for (const Draw& draw : draws)
            {
                if (!drawn(draw))
                {
                    continue;
                }
                Outline::setId(program, draw.mId, draw.mPriority, draw.mShowHidden,
                               Outline::outlined(draw.parts(), draw.mPriority));
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
            return texture_read;
        }

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
                // The HUD's is the pass with no scene.
                const bool hud = scene == nullptr;
                texture_read = drawAll(program, draws, palette, [hud](const Draw& draw)
                                       { return Outline::inIdPass(draw.parts(), draw.mPriority, hud); });
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

        // view()'s, with a glow reaching `radius` pixels at `brightness`.
        Outline::View glowView(S32 radius, F32 brightness = Outline::DEFAULT_GLOW_BRIGHTNESS) const
        {
            Outline::View lines = view();
            lines.mGlowRadius = radius;
            lines.mGlowBrightness = brightness;
            return lines;
        }

        // The jump and tile passes as ALSelectionOutline::render runs them, over the texels under `rect`, or the
        // whole id target, through `lines`' projection; the tile pass left out when `tiles` is false.
        void markPasses(const Outline::View& lines, const LLRect* rect = nullptr, bool tiles = true)
        {
            const LLRect under = rect ? *rect : LLRect(0, H, W, 0);
            mJumpProgram.bind();
            Outline::drawJumps(mJumpProgram, mIdMap, mJumps, under, lines, *mTriangle);
            if (tiles)
            {
                mTileProgram.bind();
                Outline::drawTiles(mTileProgram, mIdMap, mJumps, mTiles, under, *mTriangle);
            }
            LLGLSLShader::unbind();
        }

        // What the tile pass recorded, four 16-bit values a tile: the lowest and highest id, the jump and the glow.
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

        // Whether the jump pass marked the texel at (x, y) as the near side of a jump: its one channel, read back as red.
        bool markedAt(S32 x, S32 y)
        {
            return at(read(mJumps), x, y).r >= 128;
        }

        // The edge pass as ALSelectionOutline::render runs it, the jump and tile passes first, over the texels under
        // `mark_rect` or all of them, and with `glow` the glow reach and row passes, then over a cleared frame,
        // scissored when asked, drawing the glow with `glow`. `untiled` is the full search: every tile holds every id,
        // a jump and a glowing surface, and the glow is searched for in the disk (GLOW_DISK), so every pixel searches
        // its whole disk as far as the lines and the glow reach.
        std::vector<U8> edgePass(const Outline::View& lines, const LLRect* scissor = nullptr, bool untiled = false,
                                 const LLRect* mark_rect = nullptr, bool glow = false)
        {
            Outline::uploadPalette(mPalette, mPaletteRows, mPaletteTexels, mRows);
            markPasses(lines, mark_rect, !untiled);
            LLRenderTarget& tiles = untiled ? mAllTiles : mTiles;
            if (glow)
            {
                const LLRect under = mark_rect ? *mark_rect : LLRect(0, H, W, 0);
                mGlowReachProgram.bind();
                Outline::drawGlowReach(mGlowReachProgram, tiles, mGlowReach, under, lines, *mTriangle);
                mGlowRowProgram.bind();
                Outline::drawGlowRows(mGlowRowProgram, mIdMap, mGlowReach, mGlowRows, under, lines, *mTriangle);
                LLGLSLShader::unbind();
            }

            mFrame.bindTarget();
            std::vector<U8> px;
            {
                LLGLSColorMask mask(true, true);
                gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
                mFrame.clear();

                LLGLDepthTest depth(GL_FALSE);
                LLGLEnable blend(GL_BLEND);
                gGL.setSceneBlendType(LLRender::BT_ALPHA);

                LLGLSLShader& program = untiled ? mFullSearchProgram : mOutlineProgram;
                program.bind();
                LLRenderTarget* glow_reach = glow ? &mGlowReach : nullptr;
                LLRenderTarget* glow_rows = glow ? &mGlowRows : nullptr;
                if (scissor)
                {
                    LLGLSScissor scissored(scissor->mLeft, scissor->mBottom, scissor->getWidth(), scissor->getHeight());
                    Outline::drawEdges(program, mIdMap, mJumps, tiles, glow_reach, glow_rows, mPalette, lines, *mTriangle);
                }
                else
                {
                    Outline::drawEdges(program, mIdMap, mJumps, tiles, glow_reach, glow_rows, mPalette, lines, *mTriangle);
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

        // The window's depth: the far value, and the scene's from `scene` over it, point-sampled as the final blit
        // copies it.
        void windowDepth(LLRenderTarget* scene)
        {
            mWindow.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
                mWindow.clear(GL_DEPTH_BUFFER_BIT);
                if (scene)
                {
                    LLGLSColorMask mask(false, false);
                    mCopyDepthProgram.bind();
                    mCopyDepthProgram.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, scene);
                    mTriangle->setBuffer();
                    mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
                    mCopyDepthProgram.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                    LLGLSLShader::unbind();
                }
            }
            mWindow.flush();
        }

        // The wireframe as ALSelectionOutline::drawWireframes draws it, over the window stand-in, its colour cleared
        // and its depth kept: `passes` in turn, through `projection`, the window's depth `resample` window pixels to
        // the scene's texel, lines `wire_width` pixels wide. A rigged draw takes `joints` as idPass does.
        std::vector<U8> wireframe(const std::vector<Outline::EWirePass>& passes, const LLMatrix4a& projection,
                                  const std::vector<Draw>& draws, S32 wire_width = 1, F32 resample = 1.f,
                                  const F32* joints = nullptr)
        {
            Outline::uploadPalette(mPalette, mPaletteRows, mPaletteTexels, mRows);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(projection);
            gGL.matrixMode(LLRender::MM_MODELVIEW);

            bool texture_read = true;
            std::vector<U8> px;
            mWindow.bindTarget();
            {
                {
                    LLGLSColorMask mask(true, true);
                    LLGLDisable scissor(GL_SCISSOR_TEST);
                    gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
                    mWindow.clear(GL_COLOR_BUFFER_BIT);
                }
                for (Outline::EWirePass pass : passes)
                {
                    Outline::WireState state(pass, Outline::wireOffsetFactor(resample));
                    LLGLSLShader& program = joints ? *mWireProgram.mRiggedVariant : mWireProgram;
                    mWireProgram.bind(joints != nullptr);
                    Outline::bindWirePass(program, pass, wire_width, W, H, mPalette, &mIdMap);
                    texture_read = drawAll(program, draws, joints, [pass](const Draw& draw)
                                           { return Outline::inWirePass(draw.parts(), draw.mShowHidden, pass); }) &&
                                   texture_read;
                    program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
                    program.unbindTexture(LLShaderMgr::SELECTION_PALETTE);
                    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                    LLGLSLShader::unbind();
                }
                px = ll_test::readFramebufferRGBA(W, H);
            }
            mWindow.flush();

            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(mProjection);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            ensure("the wireframe program reads the face's texture", texture_read);
            return px;
        }

        // The world's two passes, visible lines then hidden ones.
        std::vector<U8> worldWireframe(const std::vector<Draw>& draws, S32 wire_width = 1, F32 resample = 1.f,
                                       const F32* joints = nullptr)
        {
            return wireframe({ Outline::WIRE_VISIBLE, Outline::WIRE_HIDDEN }, mProjection, draws, wire_width, resample, joints);
        }

        LLRect boxRect(S32 reach) const
        {
            const LLVector4a extents[2] = { LLVector4a(-2.f, -1.f, QUAD_Z), LLVector4a(2.f, 1.f, QUAD_Z) };
            LLRect rect;
            ensure("the quads' box is on screen", Outline::scissorRect(mProjection, extents, W, H, reach, rect));
            return rect;
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mIdProgram;
        LLGLSLShader mJumpProgram;
        LLGLSLShader mWireProgram;
        LLGLSLShader mCopyDepthProgram;
        LLGLSLShader mTileProgram;
        LLGLSLShader mOutlineProgram;
        LLGLSLShader mFullSearchProgram;
        LLGLSLShader mGlowReachProgram;
        LLGLSLShader mGlowRowProgram;
        LLRenderTarget mIdMap;
        // The jump pass's marks, one channel.
        LLRenderTarget mJumps;
        // The glow reach and row passes' records.
        LLRenderTarget mGlowReach;
        LLRenderTarget mGlowRows;
        // The window's framebuffer: what the wireframe draws over, its depth the scene's.
        LLRenderTarget mWindow;
        LLRenderTarget mTiles;
        LLRenderTarget mAllTiles;
        LLRenderTarget mScene;
        LLRenderTarget mFar;
        LLRenderTarget mFrame;
        U32 mPalette = 0;
        U32 mPaletteRows = 0;
        U32 mRows = 0;
        U32 mRamp = 0;
        U32 mClear = 0;
        U32 mGlass = 0;
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
    }

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
        ensure_equals("the setting's default", Outline::contourWidth(Outline::DEFAULT_CONTOUR_WIDTH, 1.f), 2);
        ensure_equals("the default at 1.5", Outline::contourWidth(Outline::DEFAULT_CONTOUR_WIDTH, 1.5f), 3);
        ensure_equals("the default at 2", Outline::contourWidth(Outline::DEFAULT_CONTOUR_WIDTH, 2.f), 4);
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
                                                 { 4.f, 1.f }, { 1.f, 1.f }, { 2.5f, 1.f }, { 0.25f, 1.f }, { 40.f, 1.f } };
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

            // Column 5 samples alpha 22, column 6 alpha 26: either side of a tenth.
            ids = pixelIds({ { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, blend, texture } });
            ensure_equals(named(reverse, "a blend cuts what is all but clear"), idAt(ids, 5, 32), 0U);
            ensure_equals(named(reverse, "and keeps the faintest it draws"), idAt(ids, 6, 32), ID_A);

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
                ensure(named(reverse, "the near side of the step marked" + at_z), markedAt(25, 32) && markedAt(32, 38));
                ensure(named(reverse, "not the far side, nor inside" + at_z), !markedAt(24, 32) && !markedAt(32, 32));

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
                const std::vector<U8> marked = read(mJumps);
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
                            marks += at(marked, x, y).r >= 128 ? 1 : 0;
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

    // The wireframe's lines are as wide in the window wherever they are: along an edge receding from four to twelve
    // metres, on the same quad four times as far, and twice as wide at twice the width. The width across a line is
    // read as the sum of its coverage down a column, the line's width over the cosine of its slope.
    template<> template<>
    void alselectionoutline_object_t::test<24>()
    {
        // The rows a column's runs of drawn pixels cover, bottom up.
        // The width down column x of the quad's diagonal, the middle of the three lines a column crosses.
        auto diagonal = [&](const std::vector<U8>& px, S32 x)
        {
            const std::vector<std::pair<S32, S32>> found = columnRuns(px, x);
            ensure_equals("a column crosses the bottom edge, the diagonal and the top edge", found.size(), size_t(3));
            return ink(px, x, found[1].first, found[1].second);
        };

        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            windowDepth(nullptr);

            // From three metres out at its left edge to twelve at its right, columns 13 to 39; its diagonal runs
            // from the near lower corner to the far upper one, about five metres out at column 26 and eight at 34.
            const std::vector<U8> near_px = worldWireframe({ { quad(-1.f, 1.5f, -1.f, 1.f, -3.f, -12.f), ID_A } });
            const F32 near_end = diagonal(near_px, 26);
            const F32 far_end = diagonal(near_px, 34);
            // The line's slope down the screen is about 0.91: a width of 1 reads as about 1.35 down a column.
            ensure(named(reverse, "the width down the diagonal, near: " + std::to_string(near_end)), near_end > 1.15f && near_end < 1.55f);
            ensure(named(reverse, "the same along the receding edge, far: " + std::to_string(far_end)), fabsf(far_end - near_end) < 0.1f);

            const std::vector<U8> far_px = worldWireframe({ { quad(-4.f, 6.f, -4.f, 4.f, -12.f, -48.f), ID_A } });
            ensure(named(reverse, "the same four times as far"), fabsf(diagonal(far_px, 26) - near_end) < 0.1f && fabsf(diagonal(far_px, 34) - far_end) < 0.1f);

            const std::vector<U8> wide_px = worldWireframe({ { quad(-1.f, 1.5f, -1.f, 1.f, -3.f, -12.f), ID_A } }, 2);
            const F32 narrow = diagonal(near_px, 30);
            const F32 wide = diagonal(wide_px, 30);
            ensure(named(reverse, "twice as wide at twice the width: " + std::to_string(wide)), wide > 1.8f * narrow && wide < 2.2f * narrow);
        }
    }

    // Across a line, coverage falls from its middle on either side, and is gone within a pixel and a half.
    template<> template<>
    void alselectionoutline_object_t::test<25>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            windowDepth(nullptr);
            // A square from 8 to 56 each way: its diagonal, through x = y, crosses each row between them.
            const std::vector<U8> px = wireframe({ Outline::WIRE_VISIBLE }, pixelProjection(), { { pixels(8, 56, 8, 56), ID_A } });
            for (S32 y = 16; y < 48; ++y)
            {
                const std::string row = ", row " + std::to_string(y);
                ensure(named(reverse, "the line is drawn" + row), at(px, y, y).r > 127);
                for (S32 step : { -1, 1 })
                {
                    U32 last = at(px, y, y).r;
                    for (S32 x = y + step; x != y + 4 * step; x += step)
                    {
                        const U32 r = at(px, x, y).r;
                        ensure(named(reverse, "coverage falls away from the line" + row), r <= last);
                        last = r;
                    }
                    ensure_equals(named(reverse, "and is gone" + row), last, 0U);
                }
            }
        }
    }

    // Lines behind something in the scene are drawn in the hidden colour with hidden selections shown, and not at
    // all without; the lines in front keep their colour.
    template<> template<>
    void alselectionoutline_object_t::test<26>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);

            // The brightest of a row's pixels between two columns.
            auto brightest = [](const std::vector<U8>& px, S32 y, S32 x0, S32 x1)
            {
                U32 most = 0;
                for (S32 x = x0; x < x1; ++x)
                {
                    most = llmax(most, (U32)at(px, x, y).r);
                }
                return most;
            };

            // A's diagonal crosses row 25 at about column 14 and row 38 at about 26: first with nothing in front.
            palette({ { ID_A, RED } }, true);
            windowDepth(nullptr);
            std::vector<U8> px = worldWireframe({ { quadA(), ID_A } });
            const U32 open = brightest(px, 25, 11, 18);
            ensure(named(reverse, "the line drawn with nothing in front"), open > 127);

            // Then with something three metres out over columns 0 to 19, in front of A's left part.
            occlude(3.f, 0, 0, 20, H);
            windowDepth(&mScene);
            px = worldWireframe({ { quadA(), ID_A } });
            const U32 hidden = brightest(px, 25, 11, 18);
            ensure(named(reverse, "the line in front drawn"), brightest(px, 38, 22, 30) > 127);
            ensure(named(reverse, "the line behind dimmed: " + std::to_string(hidden) + " of " + std::to_string(open)),
                   hidden * 100 >= open * 35 && hidden * 100 <= open * 45);

            Draw left_out = { quadA(), ID_A };
            left_out.mShowHidden = false;
            palette({ { ID_A, RED } }, false);
            px = worldWireframe({ left_out });
            ensure_equals(named(reverse, "the line behind left out"), brightest(px, 25, 11, 18), 0U);
            ensure(named(reverse, "the line in front kept"), brightest(px, 38, 22, 30) > 127);
        }
    }

    // Lines on their own surface, which the scene's depth holds, are all drawn: the window's depth is the scene's
    // point-sampled from half the resolution, as with RenderResolutionDivisor 2, nearer than the lines over half
    // their pixels on a surface turned away, and the polygon offset pulls them in front of it under either depth
    // convention. None is taken for hidden.
    template<> template<>
    void alselectionoutline_object_t::test<27>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            // Turned about 60 degrees from the eye, five metres deep across its width.
            LLVertexBuffer* tilted = quad(-1.5f, 1.5f, -1.f, 1.f, -3.5f, -8.5f);

            windowDepth(nullptr);
            const std::vector<U8> alone = worldWireframe({ { tilted, ID_A } }, 1, 2.f);

            sceneAt(W / 2, H / 2);
            idPass(mScene, &mFar, { { tilted, ID_A } });
            windowDepth(&mScene);
            const std::vector<U8> on_surface = worldWireframe({ { tilted, ID_A } }, 1, 2.f);

            size_t drawn = 0;
            size_t lost = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    drawn += black(at(alone, x, y)) ? 0 : 1;
                    lost += at(alone, x, y).r != at(on_surface, x, y).r ? 1 : 0;
                }
            }
            ensure(named(reverse, "lines are drawn"), drawn > 100);
            ensure_equals(named(reverse, "every line pixel drawn on its own surface as on nothing"), lost, size_t(0));
        }
    }

    // A face's cut-out texels draw no lines, its kept ones do; a rigged face's lines are where its joints put it; a
    // face the scene culls draws none.
    template<> template<>
    void alselectionoutline_object_t::test<28>()
    {
        // Joint 0 moved 1.5 m along +x.
        const F32 right[12] = { 1.f, 0.f, 0.f, 1.5f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f };
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            windowDepth(nullptr);

            // The ramp's alpha masked at 64: columns up to 15 cut out, 16 on kept. The diagonal, from (0, 16) to
            // (64, 48), crosses column 8 at row 20 and column 40 at row 36.
            const F32 mask = Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_MASK, 64, true);
            std::vector<U8> px = wireframe({ Outline::WIRE_VISIBLE }, pixelProjection(),
                                           { { pixels(0, W, 16, 48), ID_A, Outline::PRIORITY_ROOT, mask, ramp() } });
            ensure_equals(named(reverse, "no line across the cut-out texels"), ink(px, 8, 17, 24), 0.f);
            ensure(named(reverse, "nor along their edge"), black(at(px, 8, 16)));
            ensure(named(reverse, "lines across the kept ones"), ink(px, 40, 33, 40) > 0.5f && !black(at(px, 40, 16)));

            // Posed, the quad covers columns 43 to 54 and rows 26 to 37.
            LLVertexBuffer* bound = quad(-0.5f, 0.5f, -0.5f, 0.5f, QUAD_Z, QUAD_Z, true);
            px = worldWireframe({ { bound, ID_A } }, 1, 1.f, right);
            ensure(named(reverse, "the rigged face's lines where it is posed"), ink(px, 49, 26, 38) > 0.5f);
            ensure(named(reverse, "not at its bind pose"), ink(px, 32, 26, 38) == 0.f);

            // A face seen from behind, wound clockwise, as the id pass culls it: no lines unless double-sided.
            const LLVector3 back_on[4] = { LLVector3(16.f, 16.f, PIXEL_Z), LLVector3(16.f, 48.f, PIXEL_Z),
                                           LLVector3(48.f, 48.f, PIXEL_Z), LLVector3(48.f, 16.f, PIXEL_Z) };
            Draw back = { quadAt(back_on), ID_A };
            px = wireframe({ Outline::WIRE_VISIBLE }, pixelProjection(), { back });
            ensure_equals(named(reverse, "no lines on a face from behind"), lit(px), size_t(0));
            back.mDoubleSided = true;
            px = wireframe({ Outline::WIRE_VISIBLE }, pixelProjection(), { back });
            ensure(named(reverse, "lines on a double-sided one"), lit(px) > 50);
        }
    }

    // On the HUD the window's depth also holds the scene's, wherever that is the nearer, so the HUD's lines are
    // tested against the id target's depth instead: drawn whatever the window's depth holds, here the nearest value,
    // and hidden where they lie behind another selected surface.
    template<> template<>
    void alselectionoutline_object_t::test<29>()
    {
        constexpr U32 REAR = 1;
        constexpr U32 FRONT = 2;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // The rear from 16 to 47 each way, its diagonal through x = y; the front nearer, over columns 28 to 43
            // and rows 20 to 35, its diagonal through x = y + 8. The rear's diagonal passes behind the front at
            // (32, 32), four pixels from its nearest edge.
            const std::vector<Draw> draws = { { pixels(16, 48, 16, 48), REAR, Outline::PRIORITY_ROOT },
                                              { quad(28.f, 44.f, 20.f, 36.f, PIXEL_Z + 1.f, PIXEL_Z + 1.f), FRONT, Outline::PRIORITY_CHILD } };
            pixelIds(draws);
            mWindow.bindTarget();
            {
                LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
                glClearDepth(reverse ? 1.0 : 0.0);
                mWindow.clear(GL_DEPTH_BUFFER_BIT);
                glClearDepth(reverse ? 0.0 : 1.0);
            }
            mWindow.flush();

            for (bool show_hidden : { false, true })
            {
                const std::string hidden_parts = show_hidden ? ", hidden parts drawn" : ", hidden parts left out";
                std::vector<Draw> shown = draws;
                for (Draw& draw : shown)
                {
                    draw.mShowHidden = show_hidden;
                }
                palette({ { REAR, RED }, { FRONT, GREEN } }, show_hidden);
                const std::vector<U8> px = wireframe({ Outline::WIRE_ID_DEPTH }, pixelProjection(), shown);
                ensure(named(reverse, "the rear's line in the open" + hidden_parts), red(at(px, 20, 20)) && at(px, 20, 20).r > 200);
                ensure(named(reverse, "the front's line" + hidden_parts), green(at(px, 40, 32)) && at(px, 40, 32).g > 200);
                if (show_hidden)
                {
                    const U32 dimmed = at(px, 32, 32).r;
                    const U32 open = at(px, 20, 20).r;
                    ensure(named(reverse, "the rear's line behind the front dimmed: " + std::to_string(dimmed)),
                           red(at(px, 32, 32)) && dimmed * 100 >= open * 35 && dimmed * 100 <= open * 45);
                }
                else
                {
                    ensure(named(reverse, "the rear's line behind the front left out"), black(at(px, 32, 32)));
                }
            }
        }
    }

    // The wireframe's width, a pixel at a UI scale of 1 and scaled with it, the same for the world and the HUD; and
    // the polygon offset its lines need over a resampled depth.
    template<> template<>
    void alselectionoutline_object_t::test<30>()
    {
        ensure_equals("a pixel at 1", Outline::lineWidth(Outline::WIRE_WIDTH, 1.f), 1);
        ensure_equals("two at 2", Outline::lineWidth(Outline::WIRE_WIDTH, 2.f), 2);
        for (bool hud : { false, true })
        {
            const Outline::View scaled = Outline::makeView(identity(), identity(), 2.f, 2.f, hud);
            ensure_equals("its width at the UI scale", scaled.mWireWidth, 2);
        }
        ensure_equals("a window depth at the scene's resolution", Outline::wireOffsetFactor(1.f), 2.f);
        ensure_equals("at half of it", Outline::wireOffsetFactor(2.f), 3.f);
        ensure_equals("never under the window's", Outline::wireOffsetFactor(0.5f), 2.f);
    }

    // A triangle reaching behind the eye, as a floor or a wall a builder stands at has, is clipped at the near plane
    // where GL clips it: its edges' parts in front are drawn at their width, the cut along the near plane draws no
    // line, and its texture coordinates are carried to the cut, so the alpha cut still follows the texture.
    template<> template<>
    void alselectionoutline_object_t::test<31>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            windowDepth(nullptr);

            // One corner a metre behind the eye, the other three six metres out: on screen, the bottom edge from the
            // bottom of the window to (61, 22), the diagonal from its left side to (61, 42), and the top edge along
            // row 42. Both triangles hold the corner behind, and the diagonal is the edge between them.
            const LLVector3 one_behind[4] = { LLVector3(-3.f, -1.f, 1.f), LLVector3(3.f, -1.f, -6.f),
                                              LLVector3(3.f, 1.f, -6.f), LLVector3(-3.f, 1.f, -6.f) };
            LLVertexBuffer* reaching = quadAt(one_behind);
            std::vector<U8> px = worldWireframe({ { reaching, ID_A } });
            std::vector<std::pair<S32, S32>> found = columnRuns(px, 40);
            ensure_equals(named(reverse, "column 40 crosses the bottom edge, the diagonal and the top edge"), found.size(), size_t(3));
            // The diagonal's slope down the screen is about a third: a width of 1 reads as about 1.05 down a column.
            const F32 across = ink(px, 40, found[1].first, found[1].second);
            ensure(named(reverse, "the diagonal at its width: " + std::to_string(across)), across > 0.9f && across < 1.25f);
            // The top edge, along row 41.8, has one triangle, which draws the half of its line inside the quad.
            ensure(named(reverse, "the top edge drawn inside the quad"), at(px, 40, 41).r > 127 && black(at(px, 40, 42)));

            // Two corners behind the eye: a wall crossing the near plane at the middle of the view, column 32, and
            // reaching ten metres out at column 43. Its edges all run near column 43; the cut at column 32 is no edge.
            px = worldWireframe({ { quad(-0.5f, 2.f, -1.f, 1.f, 2.4f, -10.1f), ID_A } });
            size_t at_cut = 0;
            size_t at_edges = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 32; x <= 38; ++x)
                {
                    at_cut += black(at(px, x, y)) ? 0 : 1;
                }
                for (S32 x = 41; x <= 45; ++x)
                {
                    at_edges += black(at(px, x, y)) ? 0 : 1;
                }
            }
            ensure_equals(named(reverse, "no line along the near plane's cut"), at_cut, size_t(0));
            ensure(named(reverse, "the edges in front drawn: " + std::to_string(at_edges)), at_edges > 40);

            // The first quad textured with the ramp, masked at half: its diagonal's texture coordinate runs from 0
            // behind the eye to 1, about 0.43 at column 20 and 0.57 at column 40.
            const F32 mask = Outline::legacyAlphaCutoff(true, LLMaterial::DIFFUSE_ALPHA_MODE_MASK, 128, true);
            px = worldWireframe({ { reaching, ID_A, Outline::PRIORITY_ROOT, mask, ramp() } });
            ensure_equals(named(reverse, "no line where the clipped triangle's texture is cut"), ink(px, 20, 24, 32), 0.f);
            ensure(named(reverse, "a line where it is kept"), ink(px, 40, 32, 38) > 0.5f);
        }
    }

    // The outline, the wireframe and the glow are drawn each without the others, and a frame runs only the passes its
    // objects ask for: the outline and the glow the id pass and the passes after it, the world's wireframe none of
    // them, since it is tested against the window's depth, and the HUD's the id pass alone, whose depth it is tested
    // against. What glows asks for nothing else, and a selection's role keeps its object whatever glows.
    template<> template<>
    void alselectionoutline_object_t::test<32>()
    {
        for (bool hud : { false, true })
        {
            const std::string where = hud ? " on the HUD" : " in the world";
            Outline::Wants wants;
            wants.mContour = true;
            Outline::Passes passes = Outline::passesFor(wants, hud, true);
            ensure("the outline alone runs the id pass" + where, passes.mIds);
            ensure("and the edge passes" + where, passes.mEdges);
            ensure("and no wireframe" + where, !passes.mWireframe);

            wants.mWireframe = true;
            passes = Outline::passesFor(wants, hud, true);
            ensure("both run everything" + where, passes.mIds && passes.mEdges && passes.mWireframe);
            passes = Outline::passesFor(wants, hud, false);
            ensure("a wireframe program that did not load leaves the outline" + where,
                   passes.mIds && passes.mEdges && !passes.mWireframe);

            wants.mContour = false;
            passes = Outline::passesFor(wants, hud, true);
            ensure("the wireframe alone is drawn" + where, passes.mWireframe);
            ensure("without the edge passes" + where, !passes.mEdges);
            ensure_equals("with the id pass only on the HUD" + where, passes.mIds, hud);
            passes = Outline::passesFor(wants, hud, false);
            ensure("nothing when its program did not load" + where, !passes.any());

            wants.mGlow = true;
            passes = Outline::passesFor(wants, hud, true);
            ensure("a glow beside a wireframe runs everything" + where, passes.mIds && passes.mEdges && passes.mWireframe);

            wants.mWireframe = false;
            passes = Outline::passesFor(wants, hud, true);
            ensure("a glow with the outline and the wireframe off runs the id and edge passes" + where,
                   passes.mIds && passes.mEdges && !passes.mWireframe);
            ensure("and whether or not the wireframe loaded" + where, Outline::passesFor(wants, hud, false).mEdges);

            wants.mGlow = false;
            ensure("nothing with all three off" + where, !Outline::passesFor(wants, hud, true).any());
        }

        const U32 both = Outline::PART_CONTOUR | Outline::PART_WIREFRAME;
        ensure_equals("what glows has no contour and no wireframe", Outline::partsFor(Outline::PRIORITY_HOVER, both), 0U);
        ensure_equals("a selection keeps what it asks for", Outline::partsFor(Outline::PRIORITY_ROOT, both), both);

        ensure("what glows is in the id pass", Outline::inIdPass(0, Outline::PRIORITY_HOVER, false));
        ensure("and drawn there", Outline::outlined(0, Outline::PRIORITY_HOVER));
        ensure("a contour is", Outline::inIdPass(Outline::PART_CONTOUR, Outline::PRIORITY_ROOT, false));
        ensure("a world wireframe alone is not", !Outline::inIdPass(Outline::PART_WIREFRAME, Outline::PRIORITY_ROOT, false));
        ensure("a HUD wireframe alone is, for its depth", Outline::inIdPass(Outline::PART_WIREFRAME, Outline::PRIORITY_ROOT, true));
        ensure("and drawn nowhere", !Outline::outlined(Outline::PART_WIREFRAME, Outline::PRIORITY_ROOT));

        for (Outline::EWirePass pass : { Outline::WIRE_VISIBLE, Outline::WIRE_HIDDEN, Outline::WIRE_ID_DEPTH })
        {
            ensure("what glows has no wireframe in any pass",
                   !Outline::inWirePass(Outline::partsFor(Outline::PRIORITY_HOVER, both), true, pass));
            ensure("nor an outline alone", !Outline::inWirePass(Outline::PART_CONTOUR, true, pass));
        }
        ensure("a wireframe's visible lines", Outline::inWirePass(Outline::PART_WIREFRAME, false, Outline::WIRE_VISIBLE));
        ensure("its hidden ones only where hidden parts are drawn",
               !Outline::inWirePass(Outline::PART_WIREFRAME, false, Outline::WIRE_HIDDEN) &&
                   Outline::inWirePass(Outline::PART_WIREFRAME, true, Outline::WIRE_HIDDEN));

        ensure("a glow never replaces a selection", !Outline::replaces(Outline::PRIORITY_CONTEXT, Outline::PRIORITY_HOVER));
        ensure("a selection replaces a glow", Outline::replaces(Outline::PRIORITY_HOVER, Outline::PRIORITY_CHILD));
        ensure("what a drag takes out replaces a root", Outline::replaces(Outline::PRIORITY_ROOT, Outline::PRIORITY_SUBTRACT));
        ensure("at one priority the later", Outline::replaces(Outline::PRIORITY_ROOT, Outline::PRIORITY_ROOT));
    }

    // The glow's reach and profile, the fades of what the pointer is over, and how long the targets are kept: each
    // object fades in over the fade time while it is named, the one before it fading out as it does, and is dropped
    // at nothing or when its object is gone.
    template<> template<>
    void alselectionoutline_object_t::test<33>()
    {
        const F32 nan = std::numeric_limits<F32>::quiet_NaN();
        ensure_equals("9 pixels at the default thickness", Outline::glowRadius(Outline::DEFAULT_GLOW_THICKNESS, 1.f), 9);
        ensure_equals("scaled with the UI", Outline::glowRadius(Outline::DEFAULT_GLOW_THICKNESS, 2.f), 18);
        ensure_equals("held to the widest", Outline::glowRadius(100.f, 1.f), (S32)Outline::MAX_GLOW_RADIUS);
        ensure_equals("a pixel at least", Outline::glowRadius(-1.f, 1.f), 1);
        ensure_equals("the default where the setting is not a number", Outline::glowRadius(nan, 1.f), 9);
        const Outline::View defaults = Outline::makeView(identity(), identity(), 2.f, 1.f, false);
        ensure_equals("a view's glow by default", defaults.mGlowRadius, 9);
        ensure_equals("and its brightness", defaults.mGlowBrightness, Outline::DEFAULT_GLOW_BRIGHTNESS);
        ensure_equals("a brightness that is not a number is the default",
                      Outline::makeView(identity(), identity(), 2.f, 1.f, false, 0.6f, nan).mGlowBrightness,
                      Outline::DEFAULT_GLOW_BRIGHTNESS);
        ensure_equals("and one past the brightest held to it",
                      Outline::makeView(identity(), identity(), 2.f, 1.f, false, 0.6f, 1000.f).mGlowBrightness,
                      Outline::MAX_GLOW_BRIGHTNESS);

        // The silhouette is half a texel out from its texel's centre.
        ensure_approximately_equals_range("opaque at the silhouette at the default brightness",
                                          Outline::glowOpacity(0.5f, 9, 4.f), 1.f, 1e-6f);
        ensure_approximately_equals_range("half as opaque at half of it", Outline::glowOpacity(0.5f, 9, 2.f), 0.5f, 1e-6f);
        ensure_approximately_equals_range("half way out, half of the silhouette's", Outline::glowOpacity(5.f, 9, 4.f), 0.5f,
                                          1e-6f);
        ensure_equals("nothing at its reach", Outline::glowOpacity(9.5f, 9, 4.f), 0.f);
        ensure_equals("nor past it", Outline::glowOpacity(30.f, 9, 4.f), 0.f);
        ensure_equals("held to opaque", Outline::glowOpacity(2.f, 9, Outline::MAX_GLOW_BRIGHTNESS), 1.f);
        const F32 last_step = Outline::glowOpacity(9.25f, 9, 4.f);
        ensure("fading smoothly into nothing: " + std::to_string(last_step), last_step > 0.f && last_step < 0.01f);
        F32 previous = 2.f;
        bool falling = true;
        for (S32 step = 0; step <= 40; ++step)
        {
            const F32 opacity = Outline::glowOpacity(0.5f + 0.25f * (F32)step, 9, 1.f);
            falling = falling && opacity <= previous;
            previous = opacity;
        }
        ensure("falling all the way out", falling);

        ensure_approximately_equals_range("half way in half the fade time", ALHoverGlow::stepFade(0.f, 0.05f, 0.1f, true),
                                          0.5f, 1e-6f);
        ensure_approximately_equals_range("out as fast", ALHoverGlow::stepFade(0.5f, 0.05f, 0.1f, false), 0.f, 1e-6f);
        ensure_equals("held to one", ALHoverGlow::stepFade(0.9f, 1.f, 0.1f, true), 1.f);
        ensure_equals("and to nothing", ALHoverGlow::stepFade(0.1f, 1.f, 0.1f, false), 0.f);
        ensure_equals("at once with no fade time", ALHoverGlow::stepFade(0.f, 0.f, 0.f, true), 1.f);
        ensure_equals("or one that is not a number", ALHoverGlow::stepFade(1.f, 0.f, nan, false), 0.f);
        ensure_equals("time running back moves nothing", ALHoverGlow::stepFade(0.5f, -1.f, 0.1f, true), 0.5f);

        // Steps of an eighth of a second over a quarter: halves, exactly.
        const LLUUID a("c0ffee00-0000-4000-8000-00000000000a");
        const LLUUID b("c0ffee00-0000-4000-8000-00000000000b");
        std::vector<LLUUID> alive = { a, b };
        auto is_alive = [&alive](const LLUUID& id) { return std::find(alive.begin(), alive.end(), id) != alive.end(); };
        // A fresh singleton for this case, given up at its end however it ends.
        struct FreshGlow
        {
            FreshGlow() { ALHoverGlow::deleteSingleton(); }
            ~FreshGlow() { ALHoverGlow::deleteSingleton(); }
        } fresh_glow;
        ALHoverGlow& glow = ALHoverGlow::instance();
        auto fade = [&glow](const LLUUID& id)
        {
            for (const ALHoverGlow::Glow& each : glow.glows())
            {
                if (each.mID == id)
                {
                    return each.mFade;
                }
            }
            return -1.f;
        };
        // A frame an eighth of a second long, the settle time between one and two of them.
        const F32 fade_time = 0.25f;
        ensure("the settle time lies between one frame and two",
               ALHoverGlow::SETTLE_SECONDS > 0.125 && ALHoverGlow::SETTLE_SECONDS <= 0.25);
        U32 frame = 0;
        auto at = [&](const LLUUID& id, F64 now)
        {
            ++frame;
            glow.hover(id, frame);
            glow.update(now, frame, fade_time, is_alive);
        };

        at(a, 10.0);
        ensure("named, nothing glows yet", glow.glows().empty());
        at(a, 10.125);
        ensure("nor before the name has settled", glow.glows().empty());
        at(a, 10.25);
        ensure_equals("settled, it fades in", fade(a), 0.5f);
        glow.update(10.25, frame, fade_time, is_alive);
        ensure_equals("a second update in a frame moves nothing", fade(a), 0.5f);
        at(a, 10.375);
        ensure_equals("in over the fade time", fade(a), 1.f);

        at(b, 10.5);
        at(b, 10.625);
        ensure_equals("the glow stays while the next name settles", fade(a), 1.f);
        ensure_equals("which does not glow yet", fade(b), -1.f);
        at(b, 10.75);
        ensure_equals("then the one before fades out", fade(a), 0.5f);
        ensure_equals("while the next fades in", fade(b), 0.5f);
        at(b, 10.875);
        ensure_equals("dropped at nothing", fade(a), -1.f);
        ensure_equals("one glows", glow.glows().size(), size_t(1));

        at(LLUUID::null, 11.0);
        at(b, 11.125);
        ensure_equals("a frame off it, at its edge or a seam, does not put it out", fade(b), 1.f);
        at(LLUUID::null, 11.25);
        at(LLUUID::null, 11.375);
        ensure_equals("nor does leaving it, until that settles", fade(b), 1.f);
        at(LLUUID::null, 11.5);
        ensure_equals("then it fades out", fade(b), 0.5f);

        at(a, 11.625);
        at(b, 11.75);
        at(a, 11.875);
        at(LLUUID::null, 12.0);
        ensure("a sweep across objects lights none of them", glow.glows().empty());

        at(a, 12.125);
        at(a, 12.25);
        at(a, 12.375);
        ensure_equals("one rested on lights", fade(a), 0.5f);
        alive.clear();
        at(a, 12.5);
        ensure("dropped when its object is gone, named or not", glow.glows().empty());
        at(LLUUID::null, 12.625);
        at(LLUUID::null, 12.75);
        ensure("nothing named, nothing glows", glow.glows().empty());

        glow.hover(b, 100);
        ensure("named a frame ago, it is still named", glow.hovered(101) == b);
        ensure("unnamed for longer, the name lapses", glow.hovered(102).isNull());

        const F64 kept = Outline::TARGET_KEEP_SECONDS;
        ensure("a target drawn with this frame is kept", !Outline::stale(5, 100.0, 5, 100.0 + kept * 2.0));
        ensure("and the next, however long it took", !Outline::stale(5, 100.0, 6, 100.0 + kept * 2.0));
        ensure("and for its time, however many frames pass", !Outline::stale(5, 100.0, 500, 100.0 + kept * 0.5));
        ensure("given up after both", Outline::stale(5, 100.0, 7, 100.0 + kept));
        ensure("across the frame counter's wrap",
               !Outline::stale(0xFFFFFFFFu, 100.0, 0u, 200.0) && Outline::stale(0xFFFFFFFEu, 100.0, 1u, 200.0));
    }

    // A glowing object draws a halo around its silhouette in its colour, falling off from the silhouette as
    // glowOpacity says, as bright as asked, to nothing at its reach; and nothing over itself, its own folds included,
    // whose jumps the jump pass leaves unmarked.
    template<> template<>
    void alselectionoutline_object_t::test<34>()
    {
        constexpr S32 RADIUS = 6;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // Columns 20 to 39 and rows 20 to 43, and a fold of the same object nearer over its middle, columns 25 to
            // 34: selected, the near side of a jump in its own surface.
            pixelIds({ { pixels(20, 40, 20, 44), ID_A }, { quad(25.f, 35.f, 25.f, 39.f, -1.f, -1.f), ID_A } });
            markPasses(glowView(RADIUS));
            ensure(named(reverse, "selected, the fold's edge is the near side of a jump"), markedAt(25, 32) && markedAt(34, 32));

            pixelIds({ hoverDraw(pixels(20, 40, 20, 44), ID_A), hoverDraw(quad(25.f, 35.f, 25.f, 39.f, -1.f, -1.f), ID_A) });
            palette({ { ID_A, GREEN } }, false);

            for (F32 brightness : { Outline::DEFAULT_GLOW_BRIGHTNESS, 2.f })
            {
                const std::string at_brightness = " at brightness " + std::to_string(brightness);
                const std::vector<U8> px = edgePass(glowView(RADIUS, brightness), nullptr, false, nullptr, true);
                ensure(named(reverse, "glowing, its fold's edge is not marked"), !markedAt(25, 32) && !markedAt(34, 32));
                for (S32 k = 0; k <= RADIUS + 2; ++k)
                {
                    // Column 40 + k is k + 1 from the centre of the nearest texel, in column 39.
                    const F32 expected = 255.f * Outline::glowOpacity((F32)(k + 1), RADIUS, brightness);
                    const Pixel p = at(px, 40 + k, 32);
                    ensure(named(reverse, "the glow " + std::to_string(k + 1) + " pixels out" + at_brightness + ": " +
                                              std::to_string(p.g) + " for " + std::to_string(expected)),
                           std::fabs((F32)p.g - expected) <= 2.f && p.r == 0 && p.b == 0);
                }
                const U8 between = at(px, 43, 32).g;
                ensure(named(reverse, "soft, not a solid band" + at_brightness), between > 10 && between < 245);
                ensure(named(reverse, "nothing over the object" + at_brightness),
                       black(at(px, 30, 32)) && black(at(px, 39, 32)) && black(at(px, 20, 20)));
                ensure(named(reverse, "nor over its far side beside its fold" + at_brightness),
                       black(at(px, 24, 32)) && black(at(px, 35, 32)));
            }
        }
    }

    // A selection's contour and the edges between selected objects are laid over the glow; and beside a glowing
    // object a selection is outlined as though it were not there, except where it stands in front.
    template<> template<>
    void alselectionoutline_object_t::test<35>()
    {
        constexpr U32 ID_GLOW = 400;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED }, { ID_B, RED }, { ID_GLOW, GREEN } }, true);

            // A selected object, columns 40 to 54, and a glowing one six columns to its left, 20 to 33.
            pixelIds({ { pixels(40, 55, 20, 44), ID_A }, hoverDraw(pixels(20, 34, 20, 44), ID_GLOW) });
            std::vector<U8> px = edgePass(glowView(9), nullptr, false, nullptr, true);
            for (S32 x : { 38, 39 })
            {
                const Pixel p = at(px, x, 32);
                ensure(named(reverse, "the contour opaque over the glow at column " + std::to_string(x)),
                       p.r == 255 && p.g == 0);
            }
            ensure(named(reverse, "the glow past the contour"), green(at(px, 37, 32)) && green(at(px, 36, 32)));

            // Touching it, at column 40: the selection's contour drawn over the glowing object as over nothing, and
            // no edge between them.
            pixelIds({ { pixels(40, 55, 20, 44), ID_A } });
            const std::vector<U8> alone = edgePass(glowView(9), nullptr, false, nullptr, true);
            pixelIds({ { pixels(40, 55, 20, 44), ID_A }, hoverDraw(pixels(20, 40, 20, 44), ID_GLOW) });
            px = edgePass(glowView(9), nullptr, false, nullptr, true);
            for (S32 x : { 37, 38, 39, 40, 41 })
            {
                const Pixel p = at(px, x, 32);
                const Pixel q = at(alone, x, 32);
                ensure(named(reverse, "the contour beside it as without it at column " + std::to_string(x)),
                       p.r == q.r && p.g == q.g && p.b == q.b && p.a == q.a);
            }

            // Two selected objects touching at column 30, their edge on A's side, and a glowing one nearer below
            // them, rows 8 to 17: its glow over A's surface, and the edge over the glow.
            pixelIds({ { pixels(10, 30, 20, 44), ID_A }, { pixels(30, 50, 20, 44), ID_B },
                       hoverDraw(quad(24.f, 36.f, 8.f, 18.f, -1.f, -1.f), ID_GLOW) });
            px = edgePass(glowView(9), nullptr, false, nullptr, true);
            // Pixel (29, 20) is a pixel from B and three from the glowing object's top row.
            const F32 glow = Outline::glowOpacity(3.f, 9, Outline::DEFAULT_GLOW_BRIGHTNESS);
            const Pixel edge = at(px, 29, 20);
            ensure(named(reverse, "the edge over the glow: red " + std::to_string(edge.r)),
                   std::fabs((F32)edge.r - 255.f * Outline::INNER_OPACITY) <= 2.f);
            ensure(named(reverse, "the glow under it: green " + std::to_string(edge.g)),
                   std::fabs((F32)edge.g - 255.f * glow * (1.f - Outline::INNER_OPACITY)) <= 2.f);
            ensure(named(reverse, "the glow over A's surface, which lies behind it"), green(at(px, 20, 21)));
        }
    }

    // A glowing object's hidden parts do not glow: its halo follows the part the scene shows.
    template<> template<>
    void alselectionoutline_object_t::test<36>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            // A, columns 9 to 31, behind something in the scene over columns 0 to 20.
            occlude(3.f, 0, 0, 21, H);
            idPass({ hoverDraw(quadA(), ID_A) });
            const std::vector<U8> ids = read(mIdMap);
            ensure_equals(named(reverse, "its hidden part left out"), (U32)at(ids, 12, 32).b, 0U);
            palette({ { ID_A, GREEN } }, false);
            const std::vector<U8> px = edgePass(glowView(6), nullptr, false, nullptr, true);
            ensure(named(reverse, "a glow past its visible side"), green(at(px, 32, 32)) && at(px, 32, 32).g > 200);
            ensure(named(reverse, "none past its hidden one"), black(at(px, 8, 32)) && black(at(px, 5, 32)));
            ensure(named(reverse, "nor above it"), black(at(px, 12, 46)));
            ensure(named(reverse, "but above the part shown"), green(at(px, 26, 45)));
        }
    }

    // The wireframe draws no lines for what glows, beside a selection whose lines it draws.
    template<> template<>
    void alselectionoutline_object_t::test<37>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);
            windowDepth(nullptr);
            const std::vector<U8> px = worldWireframe({ { quadA(), ID_A }, hoverDraw(quadB(), ID_B) });
            size_t selected = 0;
            size_t glowing = 0;
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    const Pixel p = at(px, x, y);
                    selected += (x >= 9 && x <= 30 && red(p)) ? 1 : 0;
                    glowing += p.g > 0 ? 1 : 0;
                }
            }
            ensure(named(reverse, "the selection's lines"), selected > 20);
            ensure_equals(named(reverse, "none for what glows"), glowing, size_t(0));
        }
    }

    // The tiles change nothing the glow draws, where it reaches past the lines; and a glowing object's glow lies in
    // its box grown by the glow's reach, which drawObject grows the scissor by.
    template<> template<>
    void alselectionoutline_object_t::test<38>()
    {
        constexpr U32 ID_GLOW = 400;
        constexpr U32 ID_FRONT = 401;
        constexpr S32 RADIUS = 9;
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED }, { ID_B, RED }, { ID_GLOW, GREEN }, { ID_FRONT, GREEN } }, true);
            // Two selections, a glowing object apart from them, and another nearer, over a corner of one.
            pixelIds({ { pixels(30, 50, 30, 50), ID_A }, { pixels(8, 22, 40, 56), ID_B }, hoverDraw(pixels(6, 22, 6, 24), ID_GLOW),
                       hoverDraw(quad(44.f, 58.f, 22.f, 36.f, -1.f, -1.f), ID_FRONT) });
            const std::vector<U8> tiled = edgePass(glowView(RADIUS), nullptr, false, nullptr, true);
            const std::vector<U8> untiled = edgePass(glowView(RADIUS), nullptr, true, nullptr, true);
            ensure(named(reverse, "the glow reaches past the lines"), green(at(tiled, 28, 15)));
            ensure(named(reverse, "the tiles change nothing it draws"), tiled == untiled);

            const LLVector4a extents[2] = { LLVector4a(6.f, 6.f, PIXEL_Z), LLVector4a(22.f, 24.f, PIXEL_Z) };
            LLRect rect;
            ensure(named(reverse, "its box is on screen"),
                   Outline::scissorRect(pixelProjection(), extents, W, H, Outline::lineReach(RADIUS), rect));
            pixelIds({ hoverDraw(pixels(6, 22, 6, 24), ID_GLOW) });
            const std::vector<U8> whole = edgePass(glowView(RADIUS), nullptr, false, nullptr, true);
            const std::vector<U8> scissored = edgePass(glowView(RADIUS), &rect, false, &rect, true);
            ensure(named(reverse, "its glow is drawn"), lit(whole) > 100);
            ensure(named(reverse, "and lies in its box grown by its reach"), whole == scissored);
        }
    }

    // A face wearing the library's transparent texture is drawn whole, legacy or GLTF, whatever its alpha mode, so
    // the invisible prims builders hide roots and touch areas with are outlined; any other texture keeps its alpha cut.
    template<> template<>
    void alselectionoutline_object_t::test<39>()
    {
        const LLUUID other("c0ffee00-0000-4000-8000-0000000000aa");
        const F32 blend = Outline::legacyAlphaCutoff(false, 0, 0, true);
        const F32 mask = Outline::gltfAlphaCutoff(LLGLTFMaterial::ALPHA_MODE_MASK, 0.5f);
        ensure_equals("the transparent texture is drawn whole", Outline::faceAlphaCutoff(IMG_TRANSPARENT, blend),
                      Outline::NO_ALPHA_TEST);
        ensure_equals("as a GLTF base colour too", Outline::faceAlphaCutoff(IMG_TRANSPARENT, mask), Outline::NO_ALPHA_TEST);
        ensure_equals("any other keeps its cut", Outline::faceAlphaCutoff(other, blend), blend);
        ensure_equals("a GLTF one too", Outline::faceAlphaCutoff(other, mask), mask);

        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            for (const LLUUID& texture : { IMG_TRANSPARENT, other })
            {
                const bool whole = texture == IMG_TRANSPARENT;
                const std::string which = whole ? "the transparent texture" : "another clear texture";
                idPass({ { quadA(), ID_A, Outline::PRIORITY_ROOT, Outline::faceAlphaCutoff(texture, blend), clearTexture() } });
                const std::vector<U8> ids = read(mIdMap);
                ensure_equals(named(reverse, which + (whole ? " drawn whole" : " cut away")), idAt(ids, 20, 32),
                              whole ? ID_A : 0U);
                const std::vector<U8> px = edgePass();
                ensure(named(reverse, which + (whole ? " outlined" : " not outlined")),
                       whole ? red(at(px, 8, 32)) : black(at(px, 8, 32)));
            }
        }
    }

    // On the HUD, an object in the id pass for its wireframe's depth alone is drawn nowhere there: its id is in the
    // target, hiding what lies behind it from the lines, and it has no contour while another object's is drawn.
    template<> template<>
    void alselectionoutline_object_t::test<40>()
    {
        for (bool reverse : conventions())
        {
            setUp(reverse);
            Draw wire_only{ pixels(36, 50, 20, 44), ID_B };
            wire_only.mParts = Outline::PART_WIREFRAME;
            const std::vector<U8> ids = pixelIds({ { pixels(10, 26, 20, 44), ID_A }, wire_only });
            ensure_equals(named(reverse, "its id is in the target"), idAt(ids, 40, 32), ID_B);
            ensure_equals(named(reverse, "drawn nowhere"), (U32)at(ids, 40, 32).b, 0U);
            ensure_equals(named(reverse, "the other drawn"), (U32)at(ids, 20, 32).b, 255U);

            palette({ { ID_A, RED }, { ID_B, GREEN } }, true);
            const std::vector<U8> px = edgePass(Outline::makeView(identity(), pixelProjection(), TEST_WIDTH, 1.f, true));
            ensure(named(reverse, "the other's contour"), red(at(px, 9, 32)) && red(at(px, 26, 32)));
            ensure(named(reverse, "none around it"), black(at(px, 35, 32)) && black(at(px, 50, 32)) && black(at(px, 43, 45)));
        }
    }

    // A blended face is cut only where its texture is all but clear: one translucent all over, as a pane of glass is,
    // is drawn whole, outlined and wireframed, where the same texture under a cutoff of a half is cut away.
    template<> template<>
    void alselectionoutline_object_t::test<41>()
    {
        const F32 blend = Outline::legacyAlphaCutoff(false, 0, 0, true);
        const F32 half = Outline::gltfAlphaCutoff(LLGLTFMaterial::ALPHA_MODE_MASK, 0.5f);
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED } }, true);
            const U32 glass = glassTexture();

            idPass({ { quadA(), ID_A, Outline::PRIORITY_ROOT, blend, glass } });
            std::vector<U8> ids = read(mIdMap);
            ensure_equals(named(reverse, "a blended pane of glass drawn whole"), idAt(ids, 20, 32), ID_A);
            std::vector<U8> px = edgePass();
            ensure(named(reverse, "and outlined"), red(at(px, 8, 32)));

            windowDepth(nullptr);
            px = worldWireframe({ { quadA(), ID_A, Outline::PRIORITY_ROOT, blend, glass } });
            ensure(named(reverse, "and wireframed"), lit(px) > 0);

            idPass({ { quadA(), ID_A, Outline::PRIORITY_ROOT, half, glass } });
            ids = read(mIdMap);
            ensure_equals(named(reverse, "under a cutoff of a half it is cut away"), idAt(ids, 20, 32), 0U);
            px = edgePass();
            ensure(named(reverse, "and not outlined"), black(at(px, 8, 32)));
            px = worldWireframe({ { quadA(), ID_A, Outline::PRIORITY_ROOT, half, glass } });
            ensure_equals(named(reverse, "nor wireframed"), lit(px), size_t(0));
        }
    }

    // The glow from the glow reach and row passes is the full search's, pixel for pixel: at reaches short and long,
    // around shapes thin, slanted and cut off by the target's edges, between two glowing objects, where the lower id's
    // colour is drawn where both are as near, beside selections and with one in front of it, and around an object
    // partly hidden.
    template<> template<>
    void alselectionoutline_object_t::test<42>()
    {
        constexpr U32 ID_GLOW = 400;
        constexpr U32 ID_OTHER = 401;
        const LLColor4 BLUE(0.f, 0.f, 1.f, 1.f);
        for (bool reverse : conventions())
        {
            setUp(reverse);
            palette({ { ID_A, RED }, { ID_B, RED }, { ID_GLOW, GREEN }, { ID_OTHER, BLUE } }, true);

            const LLVector3 diamond[4] = { LLVector3(44.f, 30.f, PIXEL_Z), LLVector3(54.f, 40.f, PIXEL_Z),
                                           LLVector3(44.f, 50.f, PIXEL_Z), LLVector3(34.f, 40.f, PIXEL_Z) };
            const std::vector<std::pair<std::string, std::vector<Draw>>> scenes = {
                { "two glowing objects, columns 10 to 19 and 29 to 38",
                  { hoverDraw(pixels(10, 20, 20, 44), ID_OTHER), hoverDraw(pixels(29, 39, 20, 44), ID_GLOW) } },
                { "a line a pixel thin and a slanted square",
                  { hoverDraw(pixels(8, 9, 10, 54), ID_GLOW), hoverDraw(quadAt(diamond), ID_OTHER) } },
                { "two cut off by the target's edges",
                  { hoverDraw(pixels(0, 12, 0, 12), ID_GLOW), hoverDraw(pixels(56, 64, 40, 64), ID_OTHER) } },
                { "beside selections, one in front of it",
                  { { pixels(30, 50, 30, 50), ID_A }, { quad(14.f, 26.f, 14.f, 26.f, PIXEL_Z + 1.f, PIXEL_Z + 1.f), ID_B },
                    hoverDraw(pixels(6, 30, 6, 30), ID_GLOW) } },
                { "two glowing objects five rows above and five columns right of pixel (30, 30)",
                  { hoverDraw(pixels(26, 35, 35, 41), ID_GLOW), hoverDraw(pixels(35, 41, 26, 35), ID_OTHER) } },
            };
            for (const auto& [name, draws] : scenes)
            {
                pixelIds(draws);
                for (S32 radius : { 1, 6, 9, 17, 33 })
                {
                    const std::string at_radius = name + ", a reach of " + std::to_string(radius);
                    const std::vector<U8> rows = edgePass(glowView(radius), nullptr, false, nullptr, true);
                    const std::vector<U8> full = edgePass(glowView(radius), nullptr, true, nullptr, true);
                    ensure(named(reverse, at_radius + ": it glows"), lit(rows) > 0);
                    ensure(named(reverse, at_radius + ": as the full search draws it"), rows == full);
                }
            }

            // Column 24 is five columns from each of the first scene's objects, column 23 four from the one on the left.
            pixelIds(scenes[0].second);
            const std::vector<U8> between = edgePass(glowView(9), nullptr, false, nullptr, true);
            ensure(named(reverse, "as near both along a row, the lower id's colour"), green(at(between, 24, 32)));
            ensure(named(reverse, "nearer the left, its colour"), blue(at(between, 23, 32)));
            pixelIds(scenes[4].second);
            const std::vector<U8> across = edgePass(glowView(9), nullptr, false, nullptr, true);
            ensure(named(reverse, "as near both along a column and a row, the lower id's colour"), green(at(across, 30, 30)));

            // Behind something in the scene over columns 0 to 20, its hidden part drawing no glow.
            occlude(3.f, 0, 0, 21, H);
            idPass({ hoverDraw(quadA(), ID_GLOW) });
            for (S32 radius : { 6, 17 })
            {
                const std::string at_radius = "partly hidden, a reach of " + std::to_string(radius);
                const std::vector<U8> rows = edgePass(glowView(radius), nullptr, false, nullptr, true);
                const std::vector<U8> full = edgePass(glowView(radius), nullptr, true, nullptr, true);
                ensure(named(reverse, at_radius + ": it glows"), lit(rows) > 0);
                ensure(named(reverse, at_radius + ": as the full search draws it"), rows == full);
            }
        }
    }
}
