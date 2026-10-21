/**
 * @file aluniformbuffer_gl_test.cpp
 * @brief ALUniformBuffer's persistently mapped rings on the hidden window: the store each one makes and how it is
 *        mapped, what the GPU reads where a slice or a scratch payload is bound, what the rings account, and the
 *        scratch ring's segments reused by the frames they were left in.
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

#include "../alframefences.h"
#include "../algpubuffer.h"
#include "../aluniformbuffer.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llrender.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

namespace
{
        // A point no engine block is bound at.
        constexpr U32 BINDING = 20;
        constexpr U32 WORDS   = 16;
        constexpr U32 BYTES   = WORDS * sizeof(U32);

        std::vector<U32> pattern(U32 count, U32 base)
        {
            std::vector<U32> words(count);
            for (U32 i = 0; i < count; ++i)
            {
                words[i] = base + i * 5u;
            }
            return words;
        }

        // What is bound at BINDING.
        struct BoundRange
        {
            GLint   mName  = 0;
            GLint64 mStart = -1;
            GLint64 mSize  = -1;
        };

        BoundRange boundRange()
        {
            BoundRange range;
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, BINDING, &range.mName);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_START, BINDING, &range.mStart);
            glGetInteger64i_v(GL_UNIFORM_BUFFER_SIZE, BINDING, &range.mSize);
            return range;
        }

        // How a buffer's store was made and is mapped, as GL reports it.
        struct StoreFlags
        {
            GLint mImmutable = 0;
            GLint mStorage   = 0;  // what glBufferStorage was given
            GLint mAccess    = 0;  // what the map was made with
            GLint mMapped    = 0;
        };

        StoreFlags storeFlags(GLuint name)
        {
            StoreFlags flags;
            glBindBuffer(GL_COPY_READ_BUFFER, name);
            glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_IMMUTABLE_STORAGE, &flags.mImmutable);
            glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_STORAGE_FLAGS, &flags.mStorage);
            glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_ACCESS_FLAGS, &flags.mAccess);
            glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_MAPPED, &flags.mMapped);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            return flags;
        }

        // What the GPU reads of WORDS words at `offset` in buffer `name`: copied GPU-side, then read back. Called
        // inside a check, so it asserts only on failure.
        std::vector<U32> gpuWords(GLuint name, GLint64 offset)
        {
            std::vector<U32> words(WORDS, 0xFFFFFFFFu);
            ALGPUBuffer copy;
            if (!copy.allocate(BYTES, 0, nullptr, "copy (test)"))
            {
                FAIL_CHECK("the copy could not be allocated");
                return words;
            }
            glBindBuffer(GL_COPY_READ_BUFFER, name);
            glBindBuffer(GL_COPY_WRITE_BUFFER, copy.getGLName());
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)offset, 0, BYTES);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            if (!copy.read(0, words.data(), BYTES))
            {
                FAIL_CHECK("the copy's read failed");
            }
            return words;
        }

    // The rings are the process's, and the mode a switch every block reads: each case ends the rings and puts the
    // mode back while the context is still up.
    struct aluniformbuffer_data
    {
        ~aluniformbuffer_data()
        {
            ALUniformBuffer::cleanupClass();
            ALUniformBuffer::sUpdateMode = mMode;
        }

        ll_test::SharedGLScope mGL;
        S32 mMode = ALUniformBuffer::sUpdateMode;
    };


    // Mode 3 streams a block into a coherent ring and mode 4 into one it flushes slice by slice: each an immutable
    // store mapped for writing for as long as it lives, and accounted with the GPU buffers. A slice is bound where it
    // was written and the GPU reads there what the block held; the next is bound past it and leaves it alone. A switch
    // of mode makes the ring again rather than keeping both.
    TEST_CASE_METHOD(aluniformbuffer_data, "aluniformbuffer_gl: the persistent modes bind each slice where the ring holds it", "[aluniformbuffer_gl]")
    {
        if (!glBufferStorage)
        {
            SKIP("no buffer storage: the persistent modes stream as UPDATE_ORPHAN");
        }

        const U64   before   = ALGPUBuffer::sBytesAllocated;
        const GLint writable = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT;
        U64         ring     = 0;
        for (const S32 mode : { (S32)ALUniformBuffer::UPDATE_PERSISTENT, (S32)ALUniformBuffer::UPDATE_PERSISTENT_FLUSH })
        {
            CAPTURE(mode);
            const bool coherent = mode == ALUniformBuffer::UPDATE_PERSISTENT;
            ALUniformBuffer::sUpdateMode = mode;

            ALUniformBuffer block;
            block.initShadowed(BYTES);

            const std::vector<U32> first = pattern(WORDS, 100u * (U32)mode);
            memcpy(block.beginWrite(), first.data(), BYTES);
            block.endWrite(0, BYTES);
            REQUIRE(block.dirty());
            block.flush(BINDING);
            const BoundRange first_range = boundRange();
            {
                INFO("bound in the ring, not the block's own store");
                REQUIRE(first_range.mName != 0);
                REQUIRE(first_range.mName != (GLint)block.getGLName());
                CHECK(first_range.mSize == (GLint64)BYTES);
            }

            const StoreFlags flags = storeFlags((GLuint)first_range.mName);
            UNSCOPED_INFO("an immutable store");
            CHECK(flags.mImmutable == GL_TRUE);
            UNSCOPED_INFO("mapped");
            CHECK(flags.mMapped == GL_TRUE);
            UNSCOPED_INFO("stored for persistent writes, and coherent for mode 3 only");
            CHECK(flags.mStorage == (writable | (coherent ? GL_MAP_COHERENT_BIT : 0)));
            UNSCOPED_INFO("mapped coherent for mode 3, flushed explicitly for mode 4");
            CHECK(flags.mAccess == (writable | (coherent ? GL_MAP_COHERENT_BIT : GL_MAP_FLUSH_EXPLICIT_BIT)));

            const U64 accounted = ALGPUBuffer::sBytesAllocated - before;
            UNSCOPED_INFO("the ring accounted with the GPU buffers");
            CHECK(accounted >= BYTES);
            if (ring)
            {
                UNSCOPED_INFO("one ring, not one per mode");
                CHECK(accounted == ring);
            }
            ring = accounted;

            UNSCOPED_INFO("the GPU reads the slice as the block held it");
            CHECK(gpuWords((GLuint)first_range.mName, first_range.mStart) == first);

            const std::vector<U32> second = pattern(WORDS, 7000u + (U32)mode);
            memcpy(block.beginWrite(), second.data(), BYTES);
            block.endWrite(0, BYTES);
            block.flush(BINDING);
            const BoundRange second_range = boundRange();
            UNSCOPED_INFO("the next slice in the same ring");
            CHECK(second_range.mName == first_range.mName);
            UNSCOPED_INFO("past the first");
            CHECK(second_range.mStart >= first_range.mStart + (GLint64)BYTES);
            UNSCOPED_INFO("the GPU reads the second slice as the block held it");
            CHECK(gpuWords((GLuint)second_range.mName, second_range.mStart) == second);
            UNSCOPED_INFO("and the first as it was");
            CHECK(gpuWords((GLuint)first_range.mName, first_range.mStart) == first);
            CHECK(glGetError() == GLenum(GL_NO_ERROR));
        }

        ALUniformBuffer::cleanupClass();
        UNSCOPED_INFO("cleanupClass returns what the ring accounted");
        CHECK(ALGPUBuffer::sBytesAllocated == before);
    }

    // The scratch ring: a payload written once is bound by range where it was written, in a coherent immutable store
    // mapped for as long as it lives and accounted with the GPU buffers; the next payload lands past it and leaves it
    // alone.
    TEST_CASE_METHOD(aluniformbuffer_data, "aluniformbuffer_gl: scratch payloads are bound where the ring holds them", "[aluniformbuffer_gl]")
    {
        if (!glBufferStorage)
        {
            SKIP("no buffer storage: the scratch ring is the orphan fallback");
        }

        const U64 before = ALGPUBuffer::sBytesAllocated;
        const std::vector<U32> first = pattern(WORDS, 40u);
        const S64 first_offset = ALUniformBuffer::scratchWrite(first.data(), BYTES);
        REQUIRE(first_offset >= 0);
        ALUniformBuffer::scratchBindRange(BINDING, first_offset, BYTES);
        const BoundRange first_range = boundRange();
        {
            INFO("bound where it was written");
            REQUIRE(first_range.mName != 0);
            CHECK(first_range.mStart == first_offset);
            CHECK(first_range.mSize == (GLint64)BYTES);
        }

        const GLint coherent = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        const StoreFlags flags = storeFlags((GLuint)first_range.mName);
        UNSCOPED_INFO("an immutable store");
        CHECK(flags.mImmutable == GL_TRUE);
        UNSCOPED_INFO("mapped");
        CHECK(flags.mMapped == GL_TRUE);
        UNSCOPED_INFO("stored and mapped for coherent persistent writes");
        CHECK(flags.mStorage == coherent);
        CHECK(flags.mAccess == coherent);
        UNSCOPED_INFO("the ring accounted with the GPU buffers");
        CHECK(ALGPUBuffer::sBytesAllocated >= before + BYTES);
        UNSCOPED_INFO("the GPU reads the payload as written");
        CHECK(gpuWords((GLuint)first_range.mName, first_range.mStart) == first);

        const std::vector<U32> second = pattern(WORDS, 9000u);
        const S64 second_offset = ALUniformBuffer::scratchWrite(second.data(), BYTES);
        REQUIRE(second_offset >= first_offset + (S64)BYTES);
        ALUniformBuffer::scratchBindRange(BINDING, second_offset, BYTES);
        const BoundRange second_range = boundRange();
        UNSCOPED_INFO("the next payload in the same ring");
        CHECK(second_range.mName == first_range.mName);
        CHECK(second_range.mStart == second_offset);
        UNSCOPED_INFO("the GPU reads the second payload as written");
        CHECK(gpuWords((GLuint)second_range.mName, second_range.mStart) == second);
        UNSCOPED_INFO("and the first as it was");
        CHECK(gpuWords((GLuint)first_range.mName, first_range.mStart) == first);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));

        ALUniformBuffer::cleanupClass();
        UNSCOPED_INFO("cleanupClass returns what the ring accounted");
        CHECK(ALGPUBuffer::sBytesAllocated == before);
    }

    // The head steps into a scratch segment with no GL call once the frame that segment was left in has finished; a
    // segment left in the open frame waits for what has been issued, with a fence of its own.
    TEST_CASE_METHOD(aluniformbuffer_data, "aluniformbuffer_gl: a scratch segment is reused once the frame it was left in has finished", "[aluniformbuffer_gl]")
    {
        if (!glBufferStorage)
        {
            SKIP("no buffer storage: the scratch ring is the orphan fallback");
        }
        // The largest payload the scratch ring takes is a whole segment of its smallest ring: four of them fill it.
        constexpr size_t SEGMENT  = 256 * 1024;
        constexpr U32    SEGMENTS = 4;
        // A ring of the smallest size, its head at the start.
        ALUniformBuffer::cleanupClass();
        ALFrameFences& frames = gGL.frameFences();
        // Every frame so far finished and seen.
        glFinish();
        REQUIRE(frames.wait(frames.frame() - 1, 1000000000ull));

        const std::vector<U8> payload(SEGMENT, 0x5a);
        // One payload a segment: the first lands at the start, and each after steps the head, leaving segments 0-2 in
        // this frame.
        for (U32 i = 0; i < SEGMENTS; ++i)
        {
            const S64 offset = ALUniformBuffer::scratchWrite(payload.data(), payload.size());
            REQUIRE(offset == S64(i * SEGMENT));
        }
        frames.endFrame();
        glFinish();
        // The next frame's end shows it finished.
        frames.endFrame();

        const U32 calls = frames.getCalls();
        // Into segments 0, 1 and 2, left in the frame that has finished; segment 3 is left in this one.
        for (U32 i = 0; i < SEGMENTS - 1; ++i)
        {
            const S64 offset = ALUniformBuffer::scratchWrite(payload.data(), payload.size());
            REQUIRE(offset == S64(i * SEGMENT));
        }
        {
            INFO("no GL call for a segment whose frame has finished");
            CHECK(frames.getCalls() == calls);
        }

        // Into segment 3, left in this frame.
        const S64 offset = ALUniformBuffer::scratchWrite(payload.data(), payload.size());
        REQUIRE(offset == S64((SEGMENTS - 1) * SEGMENT));
        {
            INFO("a segment left in the open frame waits for what has been issued");
            CHECK(frames.getCalls() >= calls + 2);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
