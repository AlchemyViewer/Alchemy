/**
 * @file alcasfold_gl_fixture.h
 * @brief Contrast adaptive sharpening as a pass of its own followed by the final blit, and as the blit's own sample:
 *        each drawn as LLPipeline::renderFinalize draws it, for the test that holds them together and the benchmark
 *        that times them.
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

#ifndef AL_CASFOLD_GL_FIXTURE_H
#define AL_CASFOLD_GL_FIXTURE_H

#include "alpostchain_gl_fixture.h"

#include "alsamplerstate.h"

#include <cmath>

// CasSetup, as the pipeline takes it, each including file's own, and included as pipeline.cpp includes it: A_GCC
// picks the CPU half's GCC and Clang spellings off Windows, and the helpers no caller here uses are let be.
#if LL_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif LL_GNUC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wrestrict"
#endif
#ifndef LL_WINDOWS
#define A_GCC 1
#endif
#define A_CPU 1
namespace
{
#include "../app_settings/shaders/class1/deferred/CASF.glsl"
}
#if LL_CLANG
#pragma clang diagnostic pop
#elif LL_GNUC
#pragma GCC diagnostic pop
#endif

namespace ll_test
{
    class CASFold
    {
    public:
        // The lens distortion's coefficients, as renderFinalize sends them once the amount is folded in; none is no
        // distortion at all.
        struct Lens
        {
            F32 k1    = 0.f;
            F32 k2    = 0.f;
            F32 scale = 1.f;
        };

        CASFold()
        {
            mPost.build(mCASProgram, "Contrast Adaptive Sharpening Shader",
                        { { "deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER }, { "deferred/CASF.glsl", GL_FRAGMENT_SHADER } },
                        {}, [](LLShaderFeatures& features) { features.hasSrgb = true; });
            const auto post_effects = [](LLShaderFeatures& features)
            {
                features.isDeferred     = true;
                features.hasPostEffects = true;
            };
            mPost.build(mBlitProgram, "Blit With Post Effects Shader",
                        { { "deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER },
                          { "alchemy/blitWithEffectsF.glsl", GL_FRAGMENT_SHADER } },
                        {}, post_effects);
            mPost.build(mBlitSharpenProgram, "Blit With Post Effects Shader (Sharpened)",
                        { { "deferred/postDeferredNoTCV.glsl", GL_VERTEX_SHADER },
                          { "alchemy/blitWithEffectsF.glsl", GL_FRAGMENT_SHADER },
                          { "deferred/CASF.glsl", GL_FRAGMENT_SHADER } },
                        { { "CAS_LIBRARY", "1" } }, post_effects);
        }

        ~CASFold()
        {
            mPost.unloadPrograms();
            mSource.release();
            mSharpened.release();
            mOut.release();
            mDepth.release();
        }

        // The post chain's last target at src_width x src_height, in its format under HDR, holding a pattern with
        // contrast everywhere; CAS's own target beside it; and what the blit draws into at out_width x out_height.
        void setUp(U32 src_width, U32 src_height, U32 out_width, U32 out_height, U32 out_format = GL_RGBA32F)
        {
            REQUIRE(mSource.allocate(src_width, src_height, GL_RGB10_A2));
            REQUIRE(mSharpened.allocate(src_width, src_height, GL_RGB10_A2));
            REQUIRE(mOut.allocate(out_width, out_height, out_format));
            REQUIRE(mDepth.allocate(src_width, src_height, 0, true));
            PostChainGL::write(mSource, PostChainGL::pattern(src_width, src_height));
        }

        // CAS's pass over the source into its own target, as applyCAS draws it.
        void cas(F32 sharpness)
        {
            LLGLDisable blend(GL_BLEND);
            LLGLDepthTest depth(GL_FALSE);
            mSharpened.bindTarget();
            mCASProgram.bind();
            varAU4(const0);
            varAU4(const1);
            CasSetup(const0, const1, sharpness, (AF1)mSource.getWidth(), (AF1)mSource.getHeight(),
                     (AF1)mSharpened.getWidth(), (AF1)mSharpened.getHeight());
            mCASProgram.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, const0);
            mCASProgram.uniform4uiv(LLShaderMgr::CAS_PARAM_1, 1, const1);
            mCASProgram.uniform2f(LLShaderMgr::OUT_SCREEN_RES, (F32)mSharpened.getWidth(), (F32)mSharpened.getHeight());
            mCASProgram.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &mSource, ALSamplers::PointMirror);
            mPost.drawTriangle();
            mCASProgram.unbind();
            mSharpened.flush();
        }

        // The final blit from `src`, with every print effect off, as renderFinalize draws it; the program that sharpens
        // as it samples where `sharpen` is set.
        void blit(LLRenderTarget& src, bool sharpen, F32 sharpness, const Lens& lens)
        {
            LLGLDisable blend(GL_BLEND);
            LLGLDepthTest depth(GL_FALSE);
            mOut.bindTarget();
            LLGLSLShader& program = sharpen ? mBlitSharpenProgram : mBlitProgram;
            program.bind();
            program.bindTexture(LLShaderMgr::DEFERRED_DIFFUSE, &src);
            program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &mDepth);
            program.uniform1ui(LLShaderMgr::FRAME_ID, 0);
            program.uniform2f(LLShaderMgr::SCREEN_RESOLUTION, (F32)mOut.getWidth(), (F32)mOut.getHeight());

            const bool distort = lens.k1 != 0.f || lens.k2 != 0.f;
            program.uniform1f(LLShaderMgr::LENS_DISTORT_AMOUNT, distort ? 1.f : 0.f);
            program.uniform2f(LLShaderMgr::LENS_DISTORT_K, lens.k1, lens.k2);
            program.uniform1f(LLShaderMgr::LENS_DISTORT_SCALE, lens.scale);
            program.uniform2f(LLShaderMgr::LENS_DISTORT_SQUEEZE, 1.f, 1.f);
            program.uniform2f(LLShaderMgr::LENS_DISTORT_CENTER, 0.f, 0.f);
            program.uniform2f(LLShaderMgr::LENS_DISTORT_TANGENTIAL, 0.f, 0.f);

            program.uniform1f(LLShaderMgr::VIGNETTE_AMOUNT, 0.f);
            program.uniform1i(LLShaderMgr::CVD_MODE, 0);
            program.uniform1f(LLShaderMgr::CVD_AMOUNT, 0.f);
            program.uniform1f(LLShaderMgr::GRAIN_AMOUNT, 0.f);
            program.uniform1i(LLShaderMgr::PREVIEW_MODE, 0);
            program.uniform1i(LLShaderMgr::REFERENCE_WIPE_MODE, 0);

            if (sharpen)
            {
                varAU4(const0);
                varAU4(const1);
                CasSetup(const0, const1, sharpness, (AF1)src.getWidth(), (AF1)src.getHeight(), (AF1)src.getWidth(),
                         (AF1)src.getHeight());
                program.uniform4uiv(LLShaderMgr::CAS_PARAM_0, 1, const0);
                program.uniform4uiv(LLShaderMgr::CAS_PARAM_1, 1, const1);
            }

            mPost.drawTriangle();
            program.unbind();
            mOut.flush();
        }

        // CAS's pass then the blit from its target, as renderFinalize drew a sharpened frame; read back.
        std::vector<F32> drawTwoPass(F32 sharpness, const Lens& lens)
        {
            cas(sharpness);
            blit(mSharpened, false, 0.f, lens);
            return PostChainGL::read(mOut);
        }

        // The blit sharpening the source as it samples it; read back.
        std::vector<F32> drawFolded(F32 sharpness, const Lens& lens)
        {
            blit(mSource, true, sharpness, lens);
            return PostChainGL::read(mOut);
        }

        // The blit with no sharpening at all; read back.
        std::vector<F32> drawPlain(const Lens& lens)
        {
            blit(mSource, false, 0.f, lens);
            return PostChainGL::read(mOut);
        }

        // CAS's own target after its pass.
        std::vector<F32> drawCAS(F32 sharpness)
        {
            cas(sharpness);
            return PostChainGL::read(mSharpened);
        }

        SharedGLScope mGL;
        PostChainGL mPost;
        LLGLSLShader mCASProgram;
        LLGLSLShader mBlitProgram;
        LLGLSLShader mBlitSharpenProgram;
        LLRenderTarget mSource;
        LLRenderTarget mSharpened;
        LLRenderTarget mOut;
        LLRenderTarget mDepth;
    };
}

#endif // AL_CASFOLD_GL_FIXTURE_H
