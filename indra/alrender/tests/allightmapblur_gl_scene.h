/**
 * @file allightmapblur_gl_scene.h
 * @brief A G-buffer of planes, a step between them and sky, and a light map of noisy occlusion over it, blurred both
 *        ways the viewer blurs it: the two fragment passes through a scratch (blurLightF.glsl), and the one dispatch
 *        (blurLightC.glsl). allightmapblur_gl_test compares the two; alcompute_gl_bench times them.
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

#pragma once

#include "allightmapblur.h"

#include "aldepthbounds.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llmatrix4a.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstrider.h"
#include "llvertexbuffer.h"

#include "../../llrender/tests/llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace allightmapblur_gl
{
    using Depth = ALRenderTargetDesc::Depth;

    constexpr F32 NEAR_CLIP    = 0.25f;
    constexpr F32 FAR_CLIP     = 256.f;
    constexpr F32 TAN_HALF_FOV = 0.6f;

    // globalF.glsl's octEncode, to the normal attachment's 16 bits.
    inline void octEncode(const LLVector3& n, U16& x, U16& y)
    {
        LLVector3 v = n / (std::fabs(n.mV[0]) + std::fabs(n.mV[1]) + std::fabs(n.mV[2]));
        if (v.mV[2] < 0.f)
        {
            const F32 ox = (1.f - std::fabs(v.mV[1])) * (v.mV[0] >= 0.f ? 1.f : -1.f);
            const F32 oy = (1.f - std::fabs(v.mV[0])) * (v.mV[1] >= 0.f ? 1.f : -1.f);
            v.mV[0] = ox;
            v.mV[1] = oy;
        }
        x = (U16)std::lround(llclamp(v.mV[0] * 0.5f + 0.5f, 0.f, 1.f) * 65535.f);
        y = (U16)std::lround(llclamp(v.mV[1] * 0.5f + 0.5f, 0.f, 1.f) * 65535.f);
    }

    // A hash in [0, 1) of a pixel and a channel: the occlusion's noise, which is what the blur is there to take out.
    inline F32 noise(S32 x, S32 y, S32 c)
    {
        U32 h = (U32)x * 73856093u ^ (U32)y * 19349663u ^ (U32)c * 83492791u;
        h ^= h >> 13;
        h *= 0x5bd1e995u;
        h ^= h >> 15;
        return (F32)(h & 0xffffu) / 65536.f;
    }

    struct Scene
    {
        Scene()
        : mShaders(std::string(AL_TEST_SHADER_DIR) + "/class")
        {
        }

        ~Scene()
        {
            LLGLSLShader::unbind();
            mFragment.unload();
            mCompute.unload();
            mShaders.clearShaderObjects();
            for (LLRenderTarget* target : { &mGBuffer, &mLight, &mScratch, &mComputeLight, &mBlurred })
            {
                target->release();
            }
            mTriangle = nullptr;
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadIdentity();
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            if (LLRender::sReverseZ)
            {
                LLRender::sReverseZ = false;
                if (gGLManager.mHasClipControl)
                {
                    glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
                }
                glClearDepth(1.0);
                LLGLDepthTest::rebase();
            }
        }

        // Where the sky is: the top rows, and a notch into the left of the wall below them.
        bool isSky(S32 x, S32 y) const
        {
            return y >= mH - mH / 12 || (y >= mH - mH / 5 && x < mW / 5);
        }

        // The view ray through pixel x, y, at z = -1.
        LLVector3 ray(S32 x, S32 y) const
        {
            const F32 aspect = (F32)mW / (F32)mH;
            const F32 nx = ((F32)x + 0.5f) / (F32)mW * 2.f - 1.f;
            const F32 ny = ((F32)y + 0.5f) / (F32)mH * 2.f - 1.f;
            return LLVector3(nx * aspect * TAN_HALF_FOV, ny * TAN_HALF_FOV, -1.f);
        }

        // The surface under pixel x, y, in view space: a floor falling away below the middle, a wall facing the camera
        // above it, and a box standing in front of both on the right, whose face is a step in depth the blur keeps off.
        void surface(S32 x, S32 y, F32& depth, LLVector3& normal) const
        {
            const LLVector3 d = ray(x, y);
            // A plane n.p = c meets the ray at t = c / n.d, at view z = -t.
            const auto meet = [&](const LLVector3& n, F32 c) { return c / (n * d); };
            if (x > mW * 16 / 25 && x < mW * 9 / 10 && y > mH / 5 && y < mH * 7 / 10)
            {
                normal = LLVector3(0.25f, 0.f, 1.f);
                normal.normVec();
                depth = meet(normal, normal * LLVector3(0.f, 0.f, -6.f));
                return;
            }
            if (y < mH / 2)
            {
                normal = LLVector3(0.f, 1.f, 0.15f);
                normal.normVec();
                depth = meet(normal, normal * LLVector3(0.f, -2.f, -10.f));
                return;
            }
            normal = LLVector3(-0.2f, 0.1f, 1.f);
            normal.normVec();
            depth = meet(normal, normal * LLVector3(0.f, 0.f, -25.f));
        }

        // The programs as LLViewerShaderMgr builds them, under the convention, and at `width` by `height` a G-buffer, a
        // light map and the blurred occlusion's target, in ALRenderResourcePlan's formats, filled with the scene.
        void setUp(bool reverse, S32 width, S32 height)
        {
            mW = width;
            mH = height;

            // The session computes, as the dispatch needs; mGL puts the switch back.
            LLRender::sCompute = true;
            if (reverse)
            {
                REQUIRE(gGLManager.mHasClipControl);
                LLRender::sReverseZ = true;
                glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
                glClearDepth(0.0);
                LLGLDepthTest::rebase();
            }

            // The fragment passes: their own stages, the taps they share with the dispatch and the deferred objects
            // those read, attached as the viewer attaches them to a deferred program.
            mFragment.mName = "Deferred Blur Light Shader";
            mFragment.mFeatures.attachNothing = true;
            mFragment.mShaderFiles.clear();
            mFragment.mShaderFiles.push_back(std::make_pair("deferred/blurLightV.glsl", GL_VERTEX_SHADER));
            mFragment.mShaderFiles.push_back(std::make_pair("deferred/blurLightF.glsl", GL_FRAGMENT_SHADER));
            mFragment.mShaderFiles.push_back(std::make_pair("deferred/blurLightUtil.glsl", GL_FRAGMENT_SHADER));
            mFragment.mShaderFiles.push_back(std::make_pair("deferred/deferredUtil.glsl", GL_FRAGMENT_SHADER));
            mFragment.mShaderFiles.push_back(std::make_pair("deferred/globalF.glsl", GL_FRAGMENT_SHADER));
            mFragment.mShaderLevel = 1;
            UNSCOPED_INFO("the fragment passes' program builds");
            REQUIRE(mFragment.createShader());

            mCompute.mName = "Deferred Blur Light Compute Shader";
            mCompute.mShaderFiles.clear();
            mCompute.mShaderFiles.push_back(std::make_pair("deferred/blurLightC.glsl", GL_COMPUTE_SHADER));
            mCompute.mShaderFiles.push_back(std::make_pair("deferred/blurLightUtil.glsl", GL_COMPUTE_SHADER));
            mCompute.mShaderFiles.push_back(std::make_pair("deferred/deferredUtil.glsl", GL_COMPUTE_SHADER));
            mCompute.mShaderFiles.push_back(std::make_pair("deferred/globalF.glsl", GL_COMPUTE_SHADER));
            mCompute.mShaderLevel = 1;
            UNSCOPED_INFO("the dispatch's program builds");
            REQUIRE(mCompute.createShader());
            REQUIRE(mCompute.isCompute());

            // The G-buffer's normal and its depth, which the light map and the scratch share as the pool gives them.
            const U32 w = (U32)mW;
            const U32 h = (U32)mH;
            const ALRenderTargetDesc gbuffer{ .width = w, .height = h, .color = { GL_RGBA16 },
                                              .depth = reverse ? Depth::D32F : Depth::D24 };
            const ALRenderTargetDesc light{ .width = w, .height = h, .color = { GL_RGBA8 } };
            REQUIRE(mGBuffer.ensure(gbuffer) == LLRenderTarget::EnsureResult::Built);
            REQUIRE(mLight.ensure(light, mGBuffer) == LLRenderTarget::EnsureResult::Built);
            REQUIRE(mScratch.ensure(light, mGBuffer) == LLRenderTarget::EnsureResult::Built);
            REQUIRE(mComputeLight.ensure(light, mGBuffer) == LLRenderTarget::EnsureResult::Built);
            REQUIRE(mBlurred.ensure({ .width = w, .height = h, .color = { GL_RG8 } }) == LLRenderTarget::EnsureResult::Built);

            // The projection the depth is stored through, and whose inverse places it.
            LLMatrix4a proj;
            proj.clear();
            F32* p = proj.getF32ptr();
            p[0]  = 1.f / (TAN_HALF_FOV * (F32)mW / (F32)mH);
            p[5]  = 1.f / TAN_HALF_FOV;
            p[10] = reverse ? NEAR_CLIP / (FAR_CLIP - NEAR_CLIP) : (FAR_CLIP + NEAR_CLIP) / (NEAR_CLIP - FAR_CLIP);
            p[11] = -1.f;
            p[14] = reverse ? FAR_CLIP * NEAR_CLIP / (FAR_CLIP - NEAR_CLIP) : 2.f * FAR_CLIP * NEAR_CLIP / (NEAR_CLIP - FAR_CLIP);
            gGL.matrixMode(LLRender::MM_PROJECTION);
            gGL.loadMatrix(proj);
            gGL.matrixMode(LLRender::MM_MODELVIEW);
            gGL.loadIdentity();

            std::vector<F32> depth(size_t(mW) * mH);
            std::vector<U16> normals(size_t(mW) * mH * 4);
            mSource.assign(size_t(mW) * mH * 4, 1.f);
            for (S32 y = 0; y < mH; ++y)
            {
                for (S32 x = 0; x < mW; ++x)
                {
                    const size_t i = size_t(y) * mW + x;
                    if (isSky(x, y))
                    {
                        depth[i] = reverse ? 0.f : 1.f;
                        continue;
                    }
                    F32 t;
                    LLVector3 n;
                    surface(x, y, t, n);
                    const F32 z = -t;
                    const F32 ndc = (p[10] * z + p[14]) / -z;
                    depth[i] = reverse ? ndc : ndc * 0.5f + 0.5f;
                    octEncode(n, normals[i * 4], normals[i * 4 + 1]);
                    normals[i * 4 + 3] = (U16)std::lround(0.34f * 65535.f);
                    // The sun's shadow, occlusion of noise about a gradient, and the spot lights' shadows.
                    mSource[i * 4]     = 0.25f + 0.5f * noise(x, y, 0);
                    mSource[i * 4 + 1] = llclamp(0.3f + 0.4f * (F32)x / (F32)mW + 0.5f * (noise(x, y, 1) - 0.5f), 0.f, 1.f);
                    mSource[i * 4 + 2] = noise(x, y, 2);
                    mSource[i * 4 + 3] = noise(x, y, 3);
                }
            }
            // The depth reaches the G-buffer by a blit, as the viewer's passes lay it, not by an upload into it: AMD's
            // driver leaves its per-tile depth summary as it was across an upload, and the depth bounds test of
            // ALDrawnOnly then lets the fragment passes draw over sky that holds exactly the cleared value.
            LLRenderTarget staging;
            REQUIRE(staging.ensure({ .width = w, .height = h, .depth = gbuffer.depth }) == LLRenderTarget::EnsureResult::Built);
            glBindTexture(GL_TEXTURE_2D, staging.getDepth());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mW, mH, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            mGBuffer.copyContents(staging, 0, 0, mW, mH, 0, 0, mW, mH, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindTexture(GL_TEXTURE_2D, mGBuffer.getTexture());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mW, mH, GL_RGBA, GL_UNSIGNED_SHORT, normals.data());
            for (LLRenderTarget* target : { &mLight, &mComputeLight })
            {
                glBindTexture(GL_TEXTURE_2D, target->getTexture());
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mW, mH, GL_RGBA, GL_FLOAT, mSource.data());
            }
            glBindTexture(GL_TEXTURE_2D, 0);

            mTriangle = new LLVertexBuffer(LLVertexBuffer::MAP_VERTEX);
            REQUIRE(mTriangle->allocateBuffer(3, 0));
            LLStrider<LLVector3> vert;
            mTriangle->getVertexStrider(vert);
            vert[0].set(-1.f, 1.f, 0.f);
            vert[1].set(-1.f, -3.f, 0.f);
            vert[2].set(3.f, 1.f, 0.f);
            mTriangle->unmapBuffer();
        }

        // What LLPipeline::bindDeferredShader gives either program of the blur: the G-buffer's normal and depth, the
        // light map it reads and the screen's size.
        void bindDeferred(LLGLSLShader& program, LLRenderTarget& light, const ALLightMapBlur::Kernel& kernel)
        {
            program.bind();
            S32 channel = program.enableTexture(LLShaderMgr::NORMAL_MAP);
            REQUIRE(channel > -1);
            mGBuffer.bindTexture(0, channel, ALSamplers::PointClamp);
            channel = program.enableTexture(LLShaderMgr::DEFERRED_DEPTH);
            REQUIRE(channel > -1);
            gGL.getTextureSlot(channel)->bind(&mGBuffer, true);
            channel = program.enableTexture(LLShaderMgr::DEFERRED_LIGHT);
            REQUIRE(channel > -1);
            light.bindTexture(0, channel, ALSamplers::PointMirror);
            program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)mW, (F32)mH);
            ALLightMapBlur::setKernel(program, kernel);
        }

        // renderDeferredLighting's two passes: across into the scratch, cleared white as the sun pass clears the map,
        // and down from it back into the map, each over geometry alone.
        void blurFragment(const ALLightMapBlur::Kernel& kernel)
        {
            mScratch.bindTarget();
            gGL.setClearColor(LLColor4::white);
            mScratch.clear(GL_COLOR_BUFFER_BIT);
            gGL.setClearColor(LLColor4::transparent);
            bindDeferred(mFragment, mLight, kernel);
            mFragment.uniform2f(LLShaderMgr::DELTA, 1.f, 0.f);
            {
                LLGLDisable blend(GL_BLEND);
                ALDrawnOnly drawn;
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mScratch.flush();

            bindDeferred(mFragment, mScratch, kernel);
            mLight.bindTarget();
            mFragment.uniform2f(LLShaderMgr::DELTA, 0.f, 1.f);
            {
                LLGLDisable blend(GL_BLEND);
                ALDrawnOnly drawn;
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            mLight.flush();
            mFragment.unbind();
        }

        // The blurred target cleared to a value the scene puts nowhere, so a texel the dispatch leaves unwritten shows.
        void poisonBlurred()
        {
            mBlurred.bindTarget();
            gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
            mBlurred.clear(GL_COLOR_BUFFER_BIT);
            gGL.setClearColor(LLColor4::transparent);
            mBlurred.flush();
        }

        // The dispatch, into the blurred target.
        bool blurCompute(const ALLightMapBlur::Kernel& kernel)
        {
            bindDeferred(mCompute, mComputeLight, kernel);
            const bool dispatched = ALLightMapBlur::dispatch(mCompute, kernel, mBlurred);
            mCompute.unbind();
            return dispatched;
        }

        std::vector<F32> read(LLRenderTarget& target) const
        {
            // A read back sees what the dispatch stored past this.
            gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
            std::vector<F32> texels(size_t(mW) * mH * 4);
            glBindTexture(GL_TEXTURE_2D, target.getTexture());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, texels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            return texels;
        }

        ll_test::SharedGLScope    mGL;
        ll_test::TestShaderMgr    mShaders;
        LLGLSLShader              mFragment;
        LLGLSLShader              mCompute;
        LLRenderTarget            mGBuffer;
        LLRenderTarget            mLight;
        LLRenderTarget            mScratch;
        LLRenderTarget            mComputeLight;
        LLRenderTarget            mBlurred;
        LLPointer<LLVertexBuffer> mTriangle;
        std::vector<F32>          mSource;
        S32                       mW = 0;
        S32                       mH = 0;
    };
}
