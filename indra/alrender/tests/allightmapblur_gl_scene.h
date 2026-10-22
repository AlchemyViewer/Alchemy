/**
 * @file allightmapblur_gl_scene.h
 * @brief A G-buffer of planes, a step between them, a ball and sky, and a light map of noisy occlusion over it,
 *        blurred both ways the viewer blurs it: the two fragment passes through a scratch (blurLightF.glsl), and the
 *        one dispatch (blurLightC.glsl). allightmapblur_gl_test compares the two, and both with the blur worked on the
 *        CPU; alcompute_gl_bench times them.
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
            for (LLRenderTarget* target : { &mGBuffer, &mLight, &mScratch, &mFloatScratch, &mComputeLight, &mBlurred })
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

        // Where the box standing in front of the floor and the wall is.
        bool isBox(S32 x, S32 y) const
        {
            return x > mW * 16 / 25 && x < mW * 9 / 10 && y > mH / 5 && y < mH * 7 / 10;
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
        // above it, a box standing in front of both on the right, whose face is a step in depth the blur keeps off, and
        // a ball on the left, whose curve leaves its taps every distance from each pixel's plane.
        void surface(S32 x, S32 y, F32& depth, LLVector3& normal) const
        {
            const LLVector3 d = ray(x, y);
            // A plane n.p = c meets the ray at t = c / n.d, at view z = -t.
            const auto meet = [&](const LLVector3& n, F32 c) { return c / (n * d); };
            // The ball a quarter of the view wide, centred left of the middle, 8 m out.
            const F32 across = (F32)mW / (F32)mH * TAN_HALF_FOV * 8.f;
            const LLVector3 centre(-0.35f * across, 0.05f * TAN_HALF_FOV * 8.f, -8.f);
            const F32 radius = 0.25f * across;
            const F32 b = d * centre;
            const F32 disc = b * b - (d * d) * (centre * centre - radius * radius);
            if (disc > 0.f)
            {
                const F32 t = (b - std::sqrt(disc)) / (d * d);
                normal = d * t - centre;
                normal.normVec();
                depth = t;
                return;
            }
            if (isBox(x, y))
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
            REQUIRE(mFloatScratch.ensure({ .width = w, .height = h, .color = { GL_RGBA32F } }, mGBuffer) ==
                    LLRenderTarget::EnsureResult::Built);
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
            mInvProj.setInverse(proj);
            mReverse = reverse;

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
            ll_test::writeTexture(staging.getDepth(), mW, mH, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            mGBuffer.copyContents(staging, 0, 0, mW, mH, 0, 0, mW, mH, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            ll_test::writeTexture(mGBuffer.getTexture(), mW, mH, GL_RGBA, GL_UNSIGNED_SHORT, normals.data());
            for (LLRenderTarget* target : { &mLight, &mComputeLight })
            {
                ll_test::writeTexture(target->getTexture(), mW, mH, GL_RGBA, GL_FLOAT, mSource.data());
            }

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
        // and down from it back into the map, each over geometry alone. Through a single-precision scratch, the
        // fragment passes keep between the axes what the dispatch keeps in shared memory.
        void blurFragment(const ALLightMapBlur::Kernel& kernel, bool float_scratch = false)
        {
            LLRenderTarget& scratch = float_scratch ? mFloatScratch : mScratch;
            scratch.bindTarget();
            gGL.setClearColor(LLColor4::white);
            scratch.clear(GL_COLOR_BUFFER_BIT);
            gGL.setClearColor(LLColor4::transparent);
            bindDeferred(mFragment, mLight, kernel);
            mFragment.uniform2f(LLShaderMgr::DELTA, 1.f, 0.f);
            {
                LLGLDisable blend(GL_BLEND);
                ALDrawnOnly drawn;
                mTriangle->setBuffer();
                mTriangle->drawArrays(LLRender::TRIANGLES, 0, 3);
            }
            scratch.flush();

            bindDeferred(mFragment, scratch, kernel);
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

        // The occlusion `box` on the box and `elsewhere` everywhere else, in both light maps.
        void layOcclusion(F32 box, F32 elsewhere)
        {
            for (S32 y = 0; y < mH; ++y)
            {
                for (S32 x = 0; x < mW; ++x)
                {
                    mSource[(size_t(y) * mW + x) * 4 + 1] = isBox(x, y) ? box : elsewhere;
                }
            }
            for (LLRenderTarget* target : { &mLight, &mComputeLight })
            {
                ll_test::writeTexture(target->getTexture(), mW, mH, GL_RGBA, GL_FLOAT, mSource.data());
            }
        }

        std::vector<F32> read(const LLRenderTarget& target) const
        {
            // A read back sees what the dispatch stored past this.
            gGL.memoryBarrier(LLRender::BARRIER_TEXTURE_UPDATE);
            std::vector<F32> texels(size_t(mW) * mH * 4);
            ll_test::readTexture(target.getTexture(), GL_RGBA, GL_FLOAT, texels.data());
            return texels;
        }

        // The blurred occlusion as blurLightUtil.glsl defines the blur, on the CPU, from what the GPU reads: the depth,
        // the normals and the light map read back, and inv_proj as LLRender inverts the projection. The sky keeps the
        // light map's own.
        std::vector<F32> reference(const ALLightMapBlur::Kernel& kernel) const
        {
            std::vector<F32> depth(size_t(mW) * mH);
            ll_test::readTexture(mGBuffer.getDepth(), GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
            const std::vector<F32> normals = read(mGBuffer);
            const std::vector<F32> light = read(mComputeLight);
            const F32* m = mInvProj.getF32ptr(); // column-major, as GLSL indexes inv_proj[column]

            const auto clampTexel = [](S32 i, S32 size) { return llclamp(i, 0, size - 1); };
            const auto mirror = [](S32 i, S32 size)
            {
                const S32 period = 2 * size;
                const S32 r = (i < 0 ? -1 - i : i) % period;
                return r < size ? r : period - 1 - r;
            };
            const auto isFar = [&](F32 d) { return mReverse ? d <= 0.f : d >= 1.f; };
            const auto viewDepth = [&](S32 x, S32 y)
            {
                const S32 cx = clampTexel(x, mW);
                const S32 cy = clampTexel(y, mH);
                const F32 d = depth[size_t(cy) * mW + cx];
                if (isFar(d))
                {
                    return -1.f;
                }
                const F32 sx = ((F32)cx + 0.5f) / (F32)mW * 2.f - 1.f;
                const F32 sy = ((F32)cy + 0.5f) / (F32)mH * 2.f - 1.f;
                const F32 w = m[3] * sx + m[7] * sy + m[11] * (mReverse ? d : 2.f * d - 1.f) + m[15];
                return 1.f / llmax(w, 0.000001f);
            };
            const auto normal = [&](S32 x, S32 y)
            {
                const size_t i = (size_t(y) * mW + x) * 4;
                F32 fx = normals[i] * 2.f - 1.f;
                F32 fy = normals[i + 1] * 2.f - 1.f;
                LLVector3 n(fx, fy, 1.f - std::fabs(fx) - std::fabs(fy));
                const F32 t = llclamp(-n.mV[2], 0.f, 1.f);
                n.mV[0] += n.mV[0] >= 0.f ? -t : t;
                n.mV[1] += n.mV[1] >= 0.f ? -t : t;
                n.normVec();
                return n;
            };
            const auto kern = [&](S32 step)
            {
                const S32 i = step >> 1;
                return (step & 1) == 0 ? kernel.mTaps[i] : (kernel.mTaps[i] + kernel.mTaps[i + 1]) * 0.5f;
            };

            // One axis over `in`, the occlusion each tap reads, mirrored. The sky keeps the light map's occlusion down,
            // and across the clear of the fragment passes' scratch.
            const auto pass = [&](const std::vector<F32>& in, S32 dx, S32 dy, std::vector<F32>& out, bool keep_sky)
            {
                for (S32 y = 0; y < mH; ++y)
                {
                    for (S32 x = 0; x < mW; ++x)
                    {
                        const size_t i = size_t(y) * mW + x;
                        const F32 t = viewDepth(x, y);
                        if (t <= 0.f)
                        {
                            out[i] = keep_sky ? light[i * 4 + 1] : 1.f;
                            continue;
                        }
                        const LLVector3 n = normal(x, y);
                        const F32 tx = 2.f / (F32)mW;
                        const F32 ty = 2.f / (F32)mH;
                        const LLVector3 m0(m[0], m[1], m[2]);
                        const LLVector3 m1(m[4], m[5], m[6]);
                        const LLVector3 m3(m[12], m[13], m[14]);
                        const F32 across = (n * m0) * tx;
                        const F32 down = (n * m1) * ty;
                        const F32 plane0 = n * m3 + (across + down) * 0.5f - n * (m0 + m1);
                        const F32 sx = ((F32)x + 0.5f) / (F32)mW * 2.f - 1.f;
                        const F32 sy = ((F32)y + 0.5f) / (F32)mH * 2.f - 1.f;
                        const LLVector3 pos = (m0 * sx + m1 * sy + m3) * t;
                        const F32 offset = (plane0 + (F32)x * across + (F32)y * down) * t;

                        F32 step[2] = { kernel.mScale * (F32)dx / (1.f + n.mV[0] * n.mV[0]),
                                        kernel.mScale * (F32)dy / (1.f + n.mV[1] * n.mV[1]) };
                        const F32 shorten = llmax(-pos.mV[2] * kernel.mDistFactor, 1.f);
                        step[0] /= shorten;
                        step[1] /= shorten;
                        const F32 tolerance = pos.mV[2] * pos.mV[2] * 0.00005f;
                        const F32 tc_mod = (F32)((x + y + 1) & 1);
                        const F32 origin[2] = { (F32)x + 0.5f + (tc_mod - 0.5f) * kernel.mTaps[1].mV[VZ] * step[0] * 0.5f,
                                                (F32)y + 0.5f + (tc_mod - 0.5f) * kernel.mTaps[1].mV[VZ] * step[1] * 0.5f };

                        F32 weight_sum = kernel.mTaps[0].mV[VY];
                        F32 ao = weight_sum * in[i];
                        for (S32 tap = 0; tap < 12; ++tap)
                        {
                            const S32 s = tap < 6 ? tap + 1 : tap - 5;
                            const F32 side = tap < 6 ? 1.f : -1.f;
                            const LLVector3 k = kern(s);
                            const S32 txl = (S32)std::floor(origin[0] + side * k.mV[VZ] * step[0] * 2.f);
                            const S32 tyl = (S32)std::floor(origin[1] + side * k.mV[VZ] * step[1] * 2.f);
                            const F32 d = (plane0 + (F32)txl * across + (F32)tyl * down) * viewDepth(txl, tyl) - offset;
                            if (viewDepth(txl, tyl) > 0.f && d * d <= tolerance)
                            {
                                ao += in[size_t(mirror(tyl, mH)) * mW + mirror(txl, mW)] * k.mV[VY];
                                weight_sum += k.mV[VY];
                            }
                        }
                        out[i] = llmax(ao / weight_sum, 0.f);
                    }
                }
            };

            std::vector<F32> occlusion(size_t(mW) * mH);
            for (size_t i = 0; i < occlusion.size(); ++i)
            {
                occlusion[i] = light[i * 4 + 1];
            }
            std::vector<F32> across(occlusion.size());
            std::vector<F32> blurred(occlusion.size());
            pass(occlusion, 1, 0, across, false);
            pass(across, 0, 1, blurred, true);
            return blurred;
        }

        ll_test::SharedGLScope    mGL;
        ll_test::TestShaderMgr    mShaders;
        LLGLSLShader              mFragment;
        LLGLSLShader              mCompute;
        LLRenderTarget            mGBuffer;
        LLRenderTarget            mLight;
        LLRenderTarget            mScratch;
        LLRenderTarget            mFloatScratch;
        LLRenderTarget            mComputeLight;
        LLRenderTarget            mBlurred;
        LLPointer<LLVertexBuffer> mTriangle;
        std::vector<F32>          mSource;
        LLMatrix4a                mInvProj;
        bool                      mReverse = false;
        S32                       mW = 0;
        S32                       mH = 0;
    };
}
