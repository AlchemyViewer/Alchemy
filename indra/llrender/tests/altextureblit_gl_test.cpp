/**
 * @file altextureblit_gl_test.cpp
 * @brief ALTextureBlit on the hidden window: a copy from a 2D or rectangle texture into a 2D one, upside down or
 *        not, that leaves bound the framebuffer it found and keeps its framebuffers until released.
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

#include "../altextureblit.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglstates.h"
#include "../llrender.h"
#include "../llrendertarget.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
    constexpr S32 W = 4;
    constexpr S32 H = 4;

    // Row y of the source is the colour (y + 1) * 40 in red and 255 - y in green, so every row differs from every
    // other and from nothing.
    U32 rowColour(S32 y)
    {
        const U32 r = (U32)(y + 1) * 40;
        const U32 g = 255 - (U32)y;
        return r | (g << 8) | (0xFFu << 24);
    }

    struct altextureblit_data
    {
        ~altextureblit_data()
        {
            mBlit.release();
            if (mTarget.isBoundInStack())
            {
                mTarget.flush();
            }
            mTarget.release();
            for (U32 texture : { mSrc, mDst })
            {
                if (texture)
                {
                    gGL.forgetTextures(1, &texture);
                    glDeleteTextures(1, &texture);
                }
            }
        }

        // A source of `target` with its rows as rowColour gives them, and a destination of 0.
        void setUp(GLenum target)
        {
            mSrcTarget = target;
            std::vector<U32> rows((size_t)W * H);
            for (S32 y = 0; y < H; ++y)
            {
                for (S32 x = 0; x < W; ++x)
                {
                    rows[(size_t)y * W + x] = rowColour(y);
                }
            }
            glGenTextures(1, &mSrc);
            glBindTexture(target, mSrc);
            glTexStorage2D(target, 1, GL_RGBA8, W, H);
            glTexSubImage2D(target, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rows.data());
            glBindTexture(target, 0);

            const std::vector<U32> zero((size_t)W * H, 0);
            glGenTextures(1, &mDst);
            glBindTexture(GL_TEXTURE_2D, mDst);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, zero.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            gGL.getTextureSlot(0)->unbind();
        }

        // Row y of the destination, the colour of its first texel, all of its texels alike or 0.
        U32 dstRow(S32 y)
        {
            std::vector<U32> texels((size_t)W * H, 0);
            glBindTexture(GL_TEXTURE_2D, mDst);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
            const U32 first = texels[(size_t)y * W];
            for (S32 x = 1; x < W; ++x)
            {
                if (texels[(size_t)y * W + x] != first)
                {
                    return 0;
                }
            }
            return first;
        }

        ll_test::SharedGLScope mGL;
        ALTextureBlit mBlit;
        LLRenderTarget mTarget;
        GLenum mSrcTarget = GL_TEXTURE_2D;
        U32 mSrc = 0;
        U32 mDst = 0;
    };

    TEST_CASE_METHOD(altextureblit_data, "altextureblit_gl: a 2D texture is copied row for row", "[altextureblit_gl]")
    {
        setUp(GL_TEXTURE_2D);
        mBlit.blit(GL_TEXTURE_2D, mSrc, mDst, W, H, false);
        for (S32 y = 0; y < H; ++y)
        {
            CAPTURE(y);
            CHECK(dstRow(y) == rowColour(y));
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    TEST_CASE_METHOD(altextureblit_data, "altextureblit_gl: a flipped copy puts the rows in the opposite order", "[altextureblit_gl]")
    {
        setUp(GL_TEXTURE_2D);
        mBlit.blit(GL_TEXTURE_2D, mSrc, mDst, W, H, true);
        for (S32 y = 0; y < H; ++y)
        {
            CAPTURE(y);
            CHECK(dstRow(y) == rowColour(H - 1 - y));
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    TEST_CASE_METHOD(altextureblit_data, "altextureblit_gl: a rectangle texture is copied as a 2D one is", "[altextureblit_gl]")
    {
        setUp(GL_TEXTURE_RECTANGLE);
        mBlit.blit(GL_TEXTURE_RECTANGLE, mSrc, mDst, W, H, true);
        for (S32 y = 0; y < H; ++y)
        {
            CAPTURE(y);
            CHECK(dstRow(y) == rowColour(H - 1 - y));
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // A scissor in force does not clip the copy, and is in force again after it.
    TEST_CASE_METHOD(altextureblit_data, "altextureblit_gl: the scissor does not clip the copy", "[altextureblit_gl]")
    {
        setUp(GL_TEXTURE_2D);
        {
            LLGLSScissor corner(0, 0, 1, 1);
            mBlit.blit(GL_TEXTURE_2D, mSrc, mDst, W, H, false);
            UNSCOPED_INFO("the scissor test is on again");
            CHECK(glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE);
        }
        for (S32 y = 0; y < H; ++y)
        {
            CAPTURE(y);
            CHECK(dstRow(y) == rowColour(y));
        }
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }

    // The framebuffer bound before the copy is bound after it, for drawing and reading, and the copy's framebuffers
    // are kept for the next copy until released.
    TEST_CASE_METHOD(altextureblit_data, "altextureblit_gl: the framebuffer bound before is bound after, and release drops the copy's", "[altextureblit_gl]")
    {
        setUp(GL_TEXTURE_2D);
        REQUIRE(mTarget.allocate(W, H, GL_RGBA8));
        mTarget.bindTarget();
        CHECK_FALSE(mBlit.hasFramebuffers());
        mBlit.blit(GL_TEXTURE_2D, mSrc, mDst, W, H, false);
        GLint draw = -1, read = -1;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        CHECK((U32)draw == LLRenderTarget::sCurFBO);
        CHECK((U32)read == LLRenderTarget::sCurFBO);
        CHECK(LLRenderTarget::sCurFBO != 0);
        CHECK(mBlit.hasFramebuffers());
        mTarget.flush();

        mBlit.release();
        CHECK_FALSE(mBlit.hasFramebuffers());
        UNSCOPED_INFO("a released blit makes its framebuffers again");
        mBlit.blit(GL_TEXTURE_2D, mSrc, mDst, W, H, true);
        CHECK(dstRow(0) == rowColour(H - 1));
        CHECK((U32)glGetError() == (U32)GL_NO_ERROR);
    }
}
