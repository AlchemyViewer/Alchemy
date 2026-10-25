/**
 * @file copycolor_gl_test.cpp
 * @brief LLRenderTarget::drawCopy through the viewer's copy program, as LLPipeline::copyColor draws it, held against
 *        the nearest blit it replaces: scissored to a rect, as the water copies the screen, under HDR and without it;
 *        and over a whole target of the screen's size, as the reflections copy the scene.
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

#include "alpostchain_gl_fixture.h"

#include "llrect.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <vector>

namespace
{
    // The screen's size, and the rect copied: off every edge, so a copy past it shows on each side.
    constexpr U32 W = 48;
    constexpr U32 H = 32;
    const LLRect RECT(5, 22, 37, 3);

    class CopyColor
    {
    public:
        CopyColor()
        {
            mPost.build(mCopy, "Copy Shader",
                        { { "interface/copyV.glsl", GL_VERTEX_SHADER }, { "interface/copyF.glsl", GL_FRAGMENT_SHADER } });
        }

        ~CopyColor()
        {
            mPost.unloadPrograms();
            mSource.release();
            mBlitted.release();
            mDrawn.release();
        }

        // `src` into `dst`, only over `rect` where there is one, as LLPipeline::copyColor draws it.
        void draw(LLRenderTarget& dst, LLRenderTarget& src, const LLRect* rect)
        {
            LLRenderTarget::drawCopy(dst, src, rect, mCopy, mPost.triangle());
        }

        // An RGBA16F source of `width` x `height` holding a pattern scaled to `range`, and two copies of `format` at
        // `copy_width` x `copy_height`, with a depth of their own where `depth`, holding the same other pattern, as the
        // pool gave them.
        void setUp(U32 width, U32 height, U32 copy_width, U32 copy_height, U32 format, F32 range, bool depth = false)
        {
            REQUIRE(mSource.allocate(width, height, GL_RGBA16F));
            REQUIRE(mBlitted.allocate(copy_width, copy_height, format, depth));
            REQUIRE(mDrawn.allocate(copy_width, copy_height, format, depth));

            std::vector<F32> scene = ll_test::PostChainGL::pattern(width, height);
            for (F32& value : scene)
            {
                value *= range;
            }
            ll_test::PostChainGL::write(mSource, scene);
            const std::vector<F32> given = ll_test::PostChainGL::pattern(copy_width, copy_height, 7);
            ll_test::PostChainGL::write(mBlitted, given);
            ll_test::PostChainGL::write(mDrawn, given);
        }

        ll_test::SharedGLScope mGL;
        ll_test::PostChainGL   mPost;
        LLGLSLShader           mCopy;
        LLRenderTarget         mSource;
        LLRenderTarget         mBlitted;
        LLRenderTarget         mDrawn;
    };

    bool inRect(size_t texel)
    {
        const S32 x = S32(texel % W);
        const S32 y = S32(texel / W);
        return x >= RECT.mLeft && x < RECT.mRight && y >= RECT.mBottom && y < RECT.mTop;
    }
}

TEST_CASE_METHOD(CopyColor, "copycolor_gl: the copy program's draw over a rect writes what the nearest blit wrote and nothing past it",
                 "[copycolor_gl]")
{
    // The screen is RGBA16F; the water's copy is R11F_G11F_B10F under HDR, which keeps values past 1, and 8 bits without.
    const U32 format = GENERATE((U32)GL_R11F_G11F_B10F, (U32)GL_RGBA8);
    CAPTURE(format);
    setUp(W, H, W, H, format, format == GL_RGBA8 ? 1.f : 8.f);
    const std::vector<F32> before = ll_test::PostChainGL::read(mDrawn);

    mBlitted.copyContents(mSource, RECT.mLeft, RECT.mBottom, RECT.mRight, RECT.mTop, RECT.mLeft, RECT.mBottom, RECT.mRight,
                          RECT.mTop, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    draw(mDrawn, mSource, &RECT);
    const std::vector<F32> blitted = ll_test::PostChainGL::read(mBlitted);
    const std::vector<F32> drawn   = ll_test::PostChainGL::read(mDrawn);

    size_t differ_from_blit = 0;
    size_t copied           = 0;
    size_t written_past     = 0;
    for (size_t i = 0; i < drawn.size(); ++i)
    {
        differ_from_blit += drawn[i] != blitted[i] ? 1 : 0;
        if (inRect(i / 4))
        {
            copied += drawn[i] != before[i] ? 1 : 0;
        }
        else
        {
            written_past += drawn[i] != before[i] ? 1 : 0;
        }
    }
    UNSCOPED_INFO("the rect copied at all, so a draw that wrote nothing would not pass");
    CHECK(copied > 0);
    UNSCOPED_INFO("every channel of every texel as the blit left it");
    CHECK(differ_from_blit == 0);
    UNSCOPED_INFO("nothing outside the rect written");
    CHECK(written_past == 0);
}

TEST_CASE_METHOD(CopyColor, "copycolor_gl: the copy program's draw over a whole target of the source's size writes what the nearest blit wrote",
                 "[copycolor_gl]")
{
    // The reflections' copy of the scene: R11F_G11F_B10F with a depth of its own, at the screen's size. A stretch stays
    // the blit's (LLPipeline::copyColor).
    setUp(W, H, W, H, GL_R11F_G11F_B10F, 8.f, true);
    const std::vector<F32> before = ll_test::PostChainGL::read(mDrawn);

    mBlitted.copyContents(mSource, 0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    draw(mDrawn, mSource, nullptr);
    const std::vector<F32> blitted = ll_test::PostChainGL::read(mBlitted);
    const std::vector<F32> drawn   = ll_test::PostChainGL::read(mDrawn);

    size_t differ_from_blit = 0;
    size_t copied           = 0;
    for (size_t i = 0; i < drawn.size(); ++i)
    {
        differ_from_blit += drawn[i] != blitted[i] ? 1 : 0;
        copied += drawn[i] != before[i] ? 1 : 0;
    }
    UNSCOPED_INFO("the target copied at all, so a draw that wrote nothing would not pass");
    CHECK(copied > 0);
    UNSCOPED_INFO("every channel of every texel as the blit left it");
    CHECK(differ_from_blit == 0);
}
