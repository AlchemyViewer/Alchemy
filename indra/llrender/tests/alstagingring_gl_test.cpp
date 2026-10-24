/**
 * @file alstagingring_gl_test.cpp
 * @brief ALStagingRing on the hidden window: room aligned and apart in one mapped store, a segment entered again only
 *        once the frame it was left in has finished and never waited for, a take too large for a segment refused and
 *        the ring grown for it at the frame's end and shrunk again after a window, a texture upload read from the
 *        ring's store, and release.
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
#include "../algpubuffer.h"
#include "../alstagingring.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llrender.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    constexpr size_t SEGMENT = ALStagingRing::MIN_BYTES / ALStagingRing::SEGMENTS;

    // The ring keeps its frames by the case's own fences.
    struct alstagingring_data
    {
        alstagingring_data()
        {
            // Errors from before the case are not the case's.
            while (glGetError() != GL_NO_ERROR)
            {
            }
        }

        ~alstagingring_data()
        {
            mRing.release();
            mFrames.release();
            if (mTexture)
            {
                gGL.forgetTextures(1, &mTexture);
                glDeleteTextures(1, &mTexture);
            }
        }

        // The ring gives room only where buffers map persistently.
        void requireMaps()
        {
            if (!ALGPUBuffer::canMapPersistently())
            {
                SKIP("the context maps no buffer persistently");
            }
        }

        // Ends the frame as LLRender::endFrame() does: the fences first, then the ring.
        void endFrame()
        {
            mFrames.endFrame();
            mRing.endFrame();
        }

        ll_test::SharedGLScope mGL;
        ALFrameFences mFrames;
        ALStagingRing mRing{ mFrames };
        U32 mTexture = 0;
    };


    // Each take begins where the last one's room, rounded up to the alignment, ends, all in the one store, mapped.
    TEST_CASE_METHOD(alstagingring_data, "alstagingring_gl: room is aligned, apart and in one mapped store", "[alstagingring_gl]")
    {
        requireMaps();
        CHECK_FALSE(mRing.isAllocated());
        CHECK(mRing.size() == ALStagingRing::MIN_BYTES);

        const ALStagingRing::Span a = mRing.take(1);
        const ALStagingRing::Span b = mRing.take(300);
        const ALStagingRing::Span c = mRing.take(ALStagingRing::ALIGNMENT);
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        REQUIRE(c.isValid());
        CHECK(mRing.isAllocated());

        CHECK(a.mOffset == 0);
        CHECK(b.mOffset == ALStagingRing::ALIGNMENT);
        CHECK(c.mOffset == 3 * ALStagingRing::ALIGNMENT);
        CHECK(size_t(b.mData - a.mData) == b.mOffset);
        CHECK(size_t(c.mData - a.mData) == c.mOffset);
        CHECK(a.mBuffer != 0);
        CHECK(b.mBuffer == a.mBuffer);
        CHECK(c.mBuffer == a.mBuffer);
        CHECK(mRing.getTaken() == 3);

        UNSCOPED_INFO("nothing asked, no room");
        CHECK_FALSE(mRing.take(0).isValid());
        CHECK(mRing.getTaken() == 3);

        // The room is the CPU's to write.
        std::memset(c.mData, 0x5A, ALStagingRing::ALIGNMENT);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Takes whose frames the GPU has not finished fill the four segments; the next finds the first still the GPU's and
    // gets no room, at once and with no GL call. Once that frame has finished, the next take goes there. The ring's
    // frames end between the takes, the fences' frame staying open, so that no frame asks more than the ring keeps.
    TEST_CASE_METHOD(alstagingring_data, "alstagingring_gl: a segment is entered again only once the frame it was left in has finished", "[alstagingring_gl]")
    {
        requireMaps();
        constexpr size_t TAKE = SEGMENT / 4;

        for (U32 i = 0; i < 4 * ALStagingRing::SEGMENTS; ++i)
        {
            const ALStagingRing::Span room = mRing.take(TAKE);
            INFO("take " << i);
            REQUIRE(room.isValid());
            CHECK(room.mOffset == i * TAKE);
            mRing.endFrame();
        }

        const U32 calls = mFrames.getCalls();
        CHECK_FALSE(mRing.take(TAKE).isValid());
        CHECK(mRing.getBusy() == 1);
        UNSCOPED_INFO("a take never waits, nor polls");
        CHECK(mFrames.getCalls() == calls);
        mRing.endFrame();
        CHECK(mRing.size() == ALStagingRing::MIN_BYTES);

        // The frame ends, the GPU finishes it, and the next frame's end shows it finished.
        endFrame();
        glFinish();
        endFrame();
        const ALStagingRing::Span again = mRing.take(TAKE);
        REQUIRE(again.isValid());
        CHECK(again.mOffset == 0);
        CHECK(mRing.getBusy() == 1);
        CHECK(mRing.size() == ALStagingRing::MIN_BYTES);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Frames whose GPU work finishes go round the ring with no take refused and at the size they began at.
    TEST_CASE_METHOD(alstagingring_data, "alstagingring_gl: frames the GPU finishes go round the ring at its size", "[alstagingring_gl]")
    {
        requireMaps();
        constexpr size_t TAKE = SEGMENT / 4;
        constexpr U32 TAKES = 3 * 4 * ALStagingRing::SEGMENTS;

        size_t wraps = 0;
        for (U32 i = 0; i < TAKES; ++i)
        {
            const ALStagingRing::Span room = mRing.take(TAKE);
            INFO("take " << i);
            REQUIRE(room.isValid());
            CHECK(room.mOffset == (i % (4 * ALStagingRing::SEGMENTS)) * TAKE);
            wraps += i > 0 && room.mOffset == 0;
            glFinish();
            endFrame();
        }
        CHECK(wraps == 2);
        CHECK(mRing.getBusy() == 0);
        CHECK(mRing.size() == ALStagingRing::MIN_BYTES);
    }

    // A take more than a segment holds gets no room, and the frame's end makes the ring large enough for it; a window of
    // frames that ask little brings it back down.
    TEST_CASE_METHOD(alstagingring_data, "alstagingring_gl: a take too large for a segment grows the ring at the frame's end", "[alstagingring_gl]")
    {
        requireMaps();
        const size_t large = SEGMENT + 1;

        CHECK_FALSE(mRing.take(large).isValid());
        CHECK(mRing.getTooLarge() == 1);
        UNSCOPED_INFO("refused before the store is made");
        CHECK_FALSE(mRing.isAllocated());

        endFrame();
        // Eight frames of it, rounded up to a power of two: 4 MB, whose segments hold it.
        CHECK(mRing.size() == 4 * ALStagingRing::MIN_BYTES);
        const ALStagingRing::Span room = mRing.take(large);
        REQUIRE(room.isValid());
        CHECK(room.mOffset == 0);
        CHECK(mRing.getTooLarge() == 1);

        // The window the large frames were in keeps the size; the next one, asking little, halves it at least.
        glFinish();
        endFrame();
        for (U32 i = 0; i < 2 * ALStagingRing::WINDOW_FRAMES; ++i)
        {
            REQUIRE(mRing.take(ALStagingRing::ALIGNMENT).isValid());
            endFrame();
        }
        CHECK(mRing.size() == ALStagingRing::MIN_BYTES);
        CHECK(mRing.getBusy() == 0);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A texture's rows written into the ring and uploaded from its store at the room's offset, as LLImageGL uploads
    // them, are what the texture holds.
    TEST_CASE_METHOD(alstagingring_data, "alstagingring_gl: a texture upload reads its rows from the ring", "[alstagingring_gl]")
    {
        requireMaps();
        constexpr S32 W = 13;
        constexpr S32 H = 7;
        constexpr size_t BYTES = size_t(W) * H * 3;

        glGenTextures(1, &mTexture);
        glBindTexture(GL_TEXTURE_2D, mTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB8, W, H);

        // Off the store's start, so that the offset is the room's.
        REQUIRE(mRing.take(1).isValid());
        const ALStagingRing::Span room = mRing.take(BYTES);
        REQUIRE(room.isValid());
        REQUIRE(room.mOffset > 0);
        std::vector<U8> rows(BYTES);
        for (size_t i = 0; i < BYTES; ++i)
        {
            rows[i] = U8(i * 7 + 3);
        }
        std::memcpy(room.mData, rows.data(), BYTES);

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, room.mBuffer);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(room.mOffset)));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

        std::vector<U8> got(BYTES, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, got.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        gGL.getTextureSlot(0)->unbind();
        CHECK(got == rows);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Release lets the store go and forgets the ring's size; the next take makes a store again.
    TEST_CASE_METHOD(alstagingring_data, "alstagingring_gl: release lets the store go and the next take makes it again", "[alstagingring_gl]")
    {
        requireMaps();
        CHECK_FALSE(mRing.take(SEGMENT + 1).isValid());
        endFrame();
        REQUIRE(mRing.take(SEGMENT + 1).isValid());
        REQUIRE(mRing.isAllocated());
        REQUIRE(mRing.size() > ALStagingRing::MIN_BYTES);

        mRing.release();
        CHECK_FALSE(mRing.isAllocated());
        CHECK(mRing.size() == ALStagingRing::MIN_BYTES);

        const ALStagingRing::Span room = mRing.take(ALStagingRing::ALIGNMENT);
        REQUIRE(room.isValid());
        CHECK(room.mOffset == 0);
        CHECK(mRing.isAllocated());
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
