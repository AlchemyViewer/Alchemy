/**
 * @file alssrhistory_bench.cpp
 * @brief What keeping last frame's scene for the screen-space reflections costs the GPU at 1440p: the colour copy into
 *        a history of the screen's format, and into one of R11F_G11F_B10F. A history swapped with the screen copies
 *        nothing.
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

// Each row is the GPU time, by GL_TIME_ELAPSED, of one frame's copy, the median of 101 runs after 10 unmeasured, as
// copyScreenSpaceReflections issues it: a nearest blit of the screen's colour. The depth copy beside it is the same
// for every way of keeping the history, and is timed on its own row. The output is a table, read against the same
// rows from another build on the same quiet machine.

#include "linden_common.h"

#include "alpostchain_gl_fixture.h"

#include "altestbenchmark.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

TEST_CASE("alssrhistory_bench: last frame's scene for the reflections at 1440p", "[!benchmark][alssrhistory_bench]")
{
    ll_test::skipUnlessOptimised();

    ll_test::SharedGLScope gl;
    ll_test::PostChainGL   post;

    constexpr U32 W = 2560;
    constexpr U32 H = 1440;

    // The scene's depth and stencil, and the screen over it.
    LLRenderTarget gbuffer;
    REQUIRE(gbuffer.allocate(W, H, GL_RGBA8, true, true, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                             LLRenderTarget::DEPTH_FMT_32F));
    LLRenderTarget screen;
    REQUIRE(screen.allocate(W, H, GL_RGBA16F));
    ll_test::PostChainGL::write(screen, ll_test::PostChainGL::pattern(W, H));

    LLGLSLShader copy_depth;
    post.build(copy_depth, "Copy Depth Shader",
               { { "interface/copyV.glsl", GL_VERTEX_SHADER }, { "interface/copyF.glsl", GL_FRAGMENT_SHADER } },
               { { "COPY_DEPTH", "1" }, { "DEPTH_ONLY", "1" } });

    std::printf("\n%-40s %12s\n", "1440p", "GPU");
    for (U32 format : { (U32)GL_RGBA16F, (U32)GL_R11F_G11F_B10F })
    {
        LLRenderTarget history;
        REQUIRE(history.allocate(W, H, format, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE,
                                 LLRenderTarget::DEPTH_FMT_32F));
        const U64 colour = ll_test::gpuMedianNs([&] {
            history.copyContents(screen, 0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        });
        std::printf("%-40s %9.3f ms\n", format == GL_RGBA16F ? "colour copy into RGBA16F (today)" : "colour copy into R11F_G11F_B10F",
                    colour / 1e6);

        if (format == GL_RGBA16F)
        {
            const U64 depth = ll_test::gpuMedianNs([&] {
                history.bindTarget();
                LLGLSColorMask no_color(false, false);
                LLGLDepthTest depth_test(GL_TRUE, GL_TRUE, GL_ALWAYS);
                LLGLDisable no_blend(GL_BLEND);
                copy_depth.bind();
                copy_depth.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &gbuffer, ALSamplers::PointClamp);
                post.drawTriangle();
                copy_depth.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
                copy_depth.unbind();
                history.flush();
            });
            std::printf("%-40s %9.3f ms\n", "depth copy (every option)", depth / 1e6);
        }
        history.release();
    }
    post.unloadPrograms();
    SUCCEED();
}
