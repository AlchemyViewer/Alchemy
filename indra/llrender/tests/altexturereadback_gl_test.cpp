/**
 * @file altexturereadback_gl_test.cpp
 * @brief ALTextureReadback on the hidden window: a level read back collected once its frame has finished and not
 *        before, with no call on the frame fences, buffers given back serving the reads after, the reads and bytes
 *        refused past the pool's bounds, reads no one collects expiring and free buffers going, the fences of its own
 *        it waits on where frames are stamped, and release.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "../alframefences.h"
#include "../altexturereadback.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llrender.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
    constexpr S32 W = 16;
    constexpr S32 H = 8;
    constexpr size_t LEVEL0 = size_t(W) * H * 4;
    constexpr size_t LEVEL1 = size_t(W / 2) * (H / 2) * 4;

    std::vector<U8> pattern(size_t bytes, U8 seed)
    {
        std::vector<U8> texels(bytes);
        for (size_t i = 0; i < bytes; ++i)
        {
            texels[i] = U8(i * 7 + seed);
        }
        return texels;
    }

    // An RGBA8 texture of two levels, each its own pattern, bound on the active unit while the case runs. The pool
    // keeps its frames by the case's own fences.
    struct altexturereadback_data
    {
        altexturereadback_data()
        {
            // Errors from before the case are not the case's.
            while (glGetError() != GL_NO_ERROR)
            {
            }
            mLevel0 = pattern(LEVEL0, 3);
            mLevel1 = pattern(LEVEL1, 101);
            glGenTextures(1, &mTexture);
            glBindTexture(GL_TEXTURE_2D, mTexture);
            glTexStorage2D(GL_TEXTURE_2D, 2, GL_RGBA8, W, H);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, mLevel0.data());
            glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, W / 2, H / 2, GL_RGBA, GL_UNSIGNED_BYTE, mLevel1.data());
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
        }

        ~altexturereadback_data()
        {
            mReadback.release();
            mFrames.release();
            glBindTexture(GL_TEXTURE_2D, 0);
            gGL.forgetTextures(1, &mTexture);
            glDeleteTextures(1, &mTexture);
            gGL.getTextureSlot(0)->unbind();
        }

        // Ends the frame as LLRender::endFrame() does: the fences first, then the pool.
        void endFrame()
        {
            mFrames.endFrame();
            mReadback.endFrame();
        }

        // Ends the frame, has the GPU finish it, and ends the next, whose end shows it finished.
        void finishFrame()
        {
            endFrame();
            glFinish();
            endFrame();
        }

        ALTextureReadback::Ticket readLevel0() { return mReadback.request(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, LEVEL0); }

        ll_test::SharedGLScope mGL;
        ALFrameFences mFrames;
        ALTextureReadback mReadback{ mFrames };
        U32 mTexture = 0;
        std::vector<U8> mLevel0;
        std::vector<U8> mLevel1;
    };


    // A read is the GPU's until the frame it was issued in is shown finished, then hands over the level once, and the
    // request and the collect neither lay nor poll a fence.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: a level read back is collected once its frame has finished", "[altexturereadback_gl]")
    {
        U32 calls = mFrames.getCalls();
        const ALTextureReadback::Ticket level0 = readLevel0();
        const ALTextureReadback::Ticket level1 = mReadback.request(GL_TEXTURE_2D, 1, GL_RGBA, GL_UNSIGNED_BYTE, LEVEL1);
        REQUIRE(level0 != 0);
        REQUIRE(level1 != 0);
        CHECK(level1 != level0);
        CHECK(mReadback.getReads() == 2);

        std::vector<U8> got(LEVEL0, 0);
        UNSCOPED_INFO("the open frame's read is the GPU's");
        CHECK(mReadback.state(level0) == ALTextureReadback::PENDING);
        CHECK(mReadback.collect(level0, got.data(), got.size()) == ALTextureReadback::PENDING);
        UNSCOPED_INFO("no call on the frame fences to ask or to look");
        CHECK(mFrames.getCalls() == calls);
        UNSCOPED_INFO("and, the frames fenced, no fence of the reads' own");
        CHECK(mReadback.getFences() == 0);

        finishFrame();
        CHECK(mReadback.state(level0) == ALTextureReadback::READY);
        calls = mFrames.getCalls();
        REQUIRE(mReadback.collect(level0, got.data(), got.size()) == ALTextureReadback::READY);
        CHECK(got == mLevel0);
        std::vector<U8> got1(LEVEL1, 0);
        REQUIRE(mReadback.collect(level1, got1.data(), got1.size()) == ALTextureReadback::READY);
        CHECK(got1 == mLevel1);
        CHECK(mFrames.getCalls() == calls);

        UNSCOPED_INFO("collected once");
        CHECK(mReadback.collect(level0, got.data(), got.size()) == ALTextureReadback::GONE);
        CHECK(mReadback.getReads() == 0);
        CHECK(mReadback.state(0) == ALTextureReadback::GONE);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A level three bytes a pixel and an odd number wide, as a sculpt map's often is, reads back tight, the bytes asked
    // for and no more, whatever pack alignment the context was left with: the read sets the alignment of 1 its bytes
    // count on.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: an RGB level of an odd width reads back tight", "[altexturereadback_gl]")
    {
        constexpr S32 RW = 13;
        constexpr S32 RH = 9;
        const std::vector<U8> rgb = pattern(size_t(RW) * RH * 3, 47);
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB8, RW, RH);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, RW, RH, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

        // GL's default, under which each 39-byte row would be read out to 40.
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        const ALTextureReadback::Ticket ticket = mReadback.request(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb.size());
        REQUIRE(ticket != 0);
        finishFrame();
        std::vector<U8> got(rgb.size(), 0);
        REQUIRE(mReadback.collect(ticket, got.data(), got.size()) == ALTextureReadback::READY);
        CHECK(got == rgb);

        glBindTexture(GL_TEXTURE_2D, mTexture);
        gGL.forgetTextures(1, &texture);
        glDeleteTextures(1, &texture);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A buffer given back, collected or cancelled, serves the next read of a size it holds; one is made only where
    // none is free.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: a buffer given back serves the reads after", "[altexturereadback_gl]")
    {
        const ALTextureReadback::Ticket first = readLevel0();
        REQUIRE(first != 0);
        CHECK(mReadback.getMade() == 1);
        const size_t bytes = mReadback.getBytes();
        CHECK(bytes >= ALTextureReadback::MIN_BUFFER);

        const ALTextureReadback::Ticket second = readLevel0();
        REQUIRE(second != 0);
        UNSCOPED_INFO("both in flight: a buffer each");
        CHECK(mReadback.getMade() == 2);

        finishFrame();
        std::vector<U8> got(LEVEL0, 0);
        REQUIRE(mReadback.collect(first, got.data(), got.size()) == ALTextureReadback::READY);
        mReadback.cancel(second);
        CHECK(mReadback.getReads() == 0);

        const ALTextureReadback::Ticket third = readLevel0();
        const ALTextureReadback::Ticket fourth = readLevel0();
        REQUIRE(third != 0);
        REQUIRE(fourth != 0);
        CHECK(mReadback.getMade() == 2);
        CHECK(mReadback.getBytes() == 2 * bytes);

        finishFrame();
        REQUIRE(mReadback.collect(fourth, got.data(), got.size()) == ALTextureReadback::READY);
        CHECK(got == mLevel0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Past MAX_READS in flight, or a read larger than the pool holds, a request is refused and reads nothing; a read
    // given up makes room again.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: requests past the pool's bounds are refused", "[altexturereadback_gl]")
    {
        std::vector<ALTextureReadback::Ticket> tickets;
        for (U32 i = 0; i < ALTextureReadback::MAX_READS; ++i)
        {
            tickets.push_back(readLevel0());
            REQUIRE(tickets.back() != 0);
        }
        CHECK(readLevel0() == 0);
        CHECK(mReadback.getRefused() == 1);

        mReadback.cancel(tickets.front());
        CHECK(readLevel0() != 0);

        mReadback.release();
        CHECK(mReadback.request(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, ALTextureReadback::MAX_BYTES + 1) == 0);
        CHECK(mReadback.getRefused() == 2);
        CHECK(mReadback.getBytes() == 0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A read no one collects expires EXPIRE_FRAMES frames on, its buffer given back, and a buffer left free goes
    // TRIM_FRAMES frames after it was last given back.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: reads no one collects expire, and free buffers go", "[altexturereadback_gl]")
    {
        const ALTextureReadback::Ticket ticket = readLevel0();
        REQUIRE(ticket != 0);
        for (U32 i = 0; i <= ALTextureReadback::EXPIRE_FRAMES; ++i)
        {
            endFrame();
        }
        CHECK(mReadback.state(ticket) == ALTextureReadback::GONE);
        CHECK(mReadback.getExpired() == 1);
        UNSCOPED_INFO("its buffer kept, free");
        CHECK(mReadback.getBytes() > 0);

        for (U32 i = 0; i <= ALTextureReadback::TRIM_FRAMES; ++i)
        {
            endFrame();
        }
        CHECK(mReadback.getBytes() == 0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Where the frames' ends stamp their frames, a read lays a fence of its own behind it and is ready only once that has
    // signalled too; a read given up leaves the fence on its buffer, and the next read into the buffer lays its own.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: while stamping, a read is ready only once its own fence has signalled", "[altexturereadback_gl]")
    {
        if (!ALFrameFences::canStamp())
        {
            SKIP("the context cannot stamp frames");
        }
        mFrames.setStamps(true);
        finishFrame();
        REQUIRE(mFrames.isStamping());

        const ALTextureReadback::Ticket ticket = readLevel0();
        REQUIRE(ticket != 0);
        CHECK(mReadback.getFences() == 1);
        CHECK(mReadback.state(ticket) == ALTextureReadback::PENDING);
        finishFrame();
        CHECK(mReadback.state(ticket) == ALTextureReadback::READY);
        CHECK(mReadback.getFences() == 0);
        std::vector<U8> got(LEVEL0, 0);
        REQUIRE(mReadback.collect(ticket, got.data(), got.size()) == ALTextureReadback::READY);
        CHECK(got == mLevel0);

        mReadback.cancel(readLevel0());
        CHECK(mReadback.getFences() == 1);
        const ALTextureReadback::Ticket next = readLevel0();
        REQUIRE(next != 0);
        CHECK(mReadback.getFences() == 1);
        finishFrame();
        REQUIRE(mReadback.collect(next, got.data(), got.size()) == ALTextureReadback::READY);
        CHECK(got == mLevel0);

        mFrames.setStamps(false);
        finishFrame();
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Release lets every buffer go and every read with it; a ticket is never given twice.
    TEST_CASE_METHOD(altexturereadback_data, "altexturereadback_gl: release ends every read, and tickets are not given again", "[altexturereadback_gl]")
    {
        const ALTextureReadback::Ticket before = readLevel0();
        REQUIRE(before != 0);
        mReadback.release();
        CHECK(mReadback.getReads() == 0);
        CHECK(mReadback.getBytes() == 0);
        CHECK(mReadback.state(before) == ALTextureReadback::GONE);

        const ALTextureReadback::Ticket after = readLevel0();
        REQUIRE(after != 0);
        CHECK(after != before);
        finishFrame();
        std::vector<U8> got(LEVEL0, 0);
        REQUIRE(mReadback.collect(after, got.data(), got.size()) == ALTextureReadback::READY);
        CHECK(got == mLevel0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
