/**
 * @file algpubuffer_gl_test.cpp
 * @brief ALGPUBuffer on the hidden window: what its store holds after each way of writing it, what it accounts, how
 *        a move leaves it, which stores are mapped; LLRender's indirect draw binding over a buffer's life; and the
 *        rings, ALGPUReadback's results in order and only once their frame has finished, its reads of the framebuffer,
 *        and ALGPUStream's regions round the ring, reused by the frames they were read in.
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
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglslshader.h"
#include "../llrender.h"
#include "../llshadermgr.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace
{
        constexpr U32 WORDS = 64;

        // A point at the centre, from no vertex arrays.
        const char* kVertex =
            "#version 400\n"
            "void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n";

        const char* kFragment =
            "#version 400\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = vec4(1.0); }\n";

        std::vector<U32> pattern(U32 count, U32 base)
        {
            std::vector<U32> words(count);
            for (U32 i = 0; i < count; ++i)
            {
                words[i] = base + i * 3u;
            }
            return words;
        }

        // What the buffer holds. Called inside a check, so it asserts only on failure: an assertion that passed would
        // take the check's UNSCOPED_INFO.
        std::vector<U32> readAll(const ALGPUBuffer& buffer)
        {
            std::vector<U32> words(buffer.size() / sizeof(U32), 0xFFFFFFFFu);
            if (!buffer.read(0, words.data(), words.size() * sizeof(U32)))
            {
                FAIL_CHECK("the buffer's read failed");
            }
            return words;
        }

        GLint indirectBinding()
        {
            GLint name = -1;
            glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &name);
            return name;
        }

        // Whether clearRange clears on the GPU, as it does where GL 4.3's clear resolved, for a store of any flags.
        // Elsewhere it uploads the pattern, which only a DYNAMIC or STREAM store takes.
        bool clearsOnGPU()
        {
            return glClearBufferSubData || (gGLManager.mHasDirectStateAccess && glClearNamedBufferSubData);
        }

    // The program the tests bind, and compute's switch, go back with mGL. The rings keep their frames by the case's own
    // frame fences.
    struct algpubuffer_data
    {
        algpubuffer_data()
        {
            LLRender::sCompute = ll_test::contextHasCompute();
        }

        ~algpubuffer_data()
        {
            LLGLSLShader::unbind();
            mProgram.unload();
            mFrames.release();
        }

        // A program that draws a point and declares no inputs, bound, for the indirect draws.
        void require_program()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            {
                INFO("shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "indirect point (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            const bool linked = mProgram.mapAttributes();
            glDeleteShader(vs);
            glDeleteShader(fs);
            REQUIRE(linked);
            REQUIRE(mProgram.mapUniforms());
            mProgram.bind();
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mProgram;
        ALFrameFences mFrames;
    };


    // What a buffer allocated with data holds, what an upload, a clear of a range and a whole clear leave, and what
    // the store accounts for while it lives.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: a DYNAMIC buffer holds what is written and accounts its store", "[algpubuffer_gl]")
    {
        const U64 before = ALGPUBuffer::sBytesAllocated;
        std::vector<U32> expected = pattern(WORDS, 1000u);
        {
            ALGPUBuffer buffer;
            REQUIRE(buffer.allocate(WORDS * sizeof(U32), ALGPUBuffer::DYNAMIC, expected.data(), "words (test)"));
            CHECK(buffer.isAllocated());
            REQUIRE(buffer.size() == size_t(WORDS * sizeof(U32)));
            CHECK(buffer.getFlags() == U32(ALGPUBuffer::DYNAMIC));
            CHECK(ALGPUBuffer::sBytesAllocated == before + WORDS * sizeof(U32));
            UNSCOPED_INFO("holds its data");
            CHECK(readAll(buffer) == expected);

            const std::vector<U32> middle = pattern(8, 77u);
            buffer.upload(16 * sizeof(U32), middle.data(), middle.size() * sizeof(U32));
            std::copy(middle.begin(), middle.end(), expected.begin() + 16);
            UNSCOPED_INFO("holds the upload where it went");
            CHECK(readAll(buffer) == expected);

            buffer.clearRange(40 * sizeof(U32), 6 * sizeof(U32), 0xDEADBEEFu);
            std::fill(expected.begin() + 40, expected.begin() + 46, 0xDEADBEEFu);
            UNSCOPED_INFO("holds the cleared range, and only it");
            CHECK(readAll(buffer) == expected);

            std::vector<U32> part(4, 0u);
            REQUIRE(buffer.read(42 * sizeof(U32), part.data(), part.size() * sizeof(U32)));
            CHECK(part == std::vector<U32>(expected.begin() + 42, expected.begin() + 46));

            buffer.clear(7u);
            CHECK(readAll(buffer) == std::vector<U32>(WORDS, 7u));
        }
        UNSCOPED_INFO("destruction returns what it accounted");
        CHECK(ALGPUBuffer::sBytesAllocated == before);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // Copies between buffers that are not DYNAMIC, and within one, go where they are told; and such a buffer clears
    // where the clear is the GPU's. Below GL 4.3 the clear is an upload, which a static store does not take, and is
    // asserted: the buffers a clear is for are compute's, and compute needs 4.3.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: copies between and within static buffers land where they are told", "[algpubuffer_gl]")
    {
        const std::vector<U32> source_words = pattern(WORDS, 5u);
        std::vector<U32> expected(WORDS, 0u);
        ALGPUBuffer source;
        ALGPUBuffer target;
        REQUIRE(source.allocate(WORDS * sizeof(U32), 0, source_words.data(), "source (test)"));
        REQUIRE(target.allocate(WORDS * sizeof(U32), 0, expected.data(), "target (test)"));

        target.copyFrom(source, 4 * sizeof(U32), 32 * sizeof(U32), 10 * sizeof(U32));
        std::copy(source_words.begin() + 4, source_words.begin() + 14, expected.begin() + 32);
        UNSCOPED_INFO("copied between buffers");
        CHECK(readAll(target) == expected);

        target.copyFrom(target, 32 * sizeof(U32), 0, 10 * sizeof(U32));
        std::copy(expected.begin() + 32, expected.begin() + 42, expected.begin());
        UNSCOPED_INFO("copied within one");
        CHECK(readAll(target) == expected);
        CHECK(readAll(source) == source_words);

        if (clearsOnGPU())
        {
            target.clear(0xA5A5A5A5u);
            CHECK(readAll(target) == std::vector<U32>(WORDS, 0xA5A5A5A5u));
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // release empties a buffer and returns its accounting; a move takes the store and leaves the source empty, and a
    // move over a buffer that held a store releases that store.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: release empties a buffer and a move takes its store", "[algpubuffer_gl]")
    {
        const U64 before = ALGPUBuffer::sBytesAllocated;
        const std::vector<U32> words = pattern(WORDS, 9u);

        ALGPUBuffer first;
        REQUIRE(first.allocate(WORDS * sizeof(U32), 0, words.data(), "first (test)"));
        first.release();
        CHECK_FALSE(first.isAllocated());
        CHECK(first.size() == size_t(0));
        CHECK(first.getGLName() == 0u);
        UNSCOPED_INFO("accounting returned");
        CHECK(ALGPUBuffer::sBytesAllocated == before);

        REQUIRE(first.allocate(WORDS * sizeof(U32), 0, words.data(), "first (test)"));
        const U32 name = first.getGLName();
        ALGPUBuffer moved(std::move(first));
        {
            INFO("the source is empty");
            CHECK_FALSE(first.isAllocated());
            CHECK(first.size() == 0);
            CHECK(first.getGLName() == 0);
        }
        CHECK(moved.getGLName() == name);
        CHECK(readAll(moved) == words);
        UNSCOPED_INFO("accounted once");
        CHECK(ALGPUBuffer::sBytesAllocated == before + WORDS * sizeof(U32));

        ALGPUBuffer other;
        REQUIRE(other.allocate(2 * WORDS * sizeof(U32), 0, nullptr, "other (test)"));
        other = std::move(moved);
        {
            INFO("the moved-from is empty");
            CHECK_FALSE(moved.isAllocated());
            CHECK(moved.getGLName() == 0);
        }
        CHECK(other.getGLName() == name);
        CHECK(readAll(other) == words);
        UNSCOPED_INFO("its own store released");
        CHECK(ALGPUBuffer::sBytesAllocated == before + WORDS * sizeof(U32));

        other.release();
        UNSCOPED_INFO("all returned");
        CHECK(ALGPUBuffer::sBytesAllocated == before);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // LLRender binds an indirect draw's buffer once and keeps it bound; after refreshState it binds it again, since
    // what is bound is no longer known; and a buffer's release forgets its name, so a new buffer that GL hands the
    // same name is bound rather than taken for bound already.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: LLRender keeps an indirect draw's buffer bound and rebinds when it must", "[algpubuffer_gl]")
    {
        require_program();
        ALDrawArraysIndirectCommand command;
        command.mCount = 1;
        command.mInstanceCount = 1;

        ALGPUBuffer first;
        REQUIRE(first.allocate(sizeof(command), 0, &command, "draw (test)"));
        gGL.drawArraysIndirect(LLRender::POINTS, first, 0);
        UNSCOPED_INFO("bound for the draw");
        CHECK(indirectBinding() == (GLint)first.getGLName());
        gGL.drawArraysIndirect(LLRender::POINTS, first, 0);
        UNSCOPED_INFO("still bound");
        CHECK(indirectBinding() == (GLint)first.getGLName());

        // Behind LLRender's back.
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        gGL.refreshState();
        gGL.drawArraysIndirect(LLRender::POINTS, first, 0);
        UNSCOPED_INFO("bound again after refreshState");
        CHECK(indirectBinding() == (GLint)first.getGLName());
        UNSCOPED_INFO("after refreshState");
        CHECK(glGetError() == GLenum(GL_NO_ERROR));

        const U32 name = first.getGLName();
        first.release();
        UNSCOPED_INFO("deleting it unbound it");
        CHECK(indirectBinding() == 0);
        ALGPUBuffer second;
        REQUIRE(second.allocate(sizeof(command), 0, &command, "draw again (test)"));
        if (second.getGLName() != name)
        {
            SKIP("GL did not hand the released name out again");
        }
        gGL.drawArraysIndirect(LLRender::POINTS, second, 0);
        UNSCOPED_INFO("the reused name is bound");
        CHECK(indirectBinding() == (GLint)second.getGLName());
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A READBACK store is mapped where persistent maps resolve, and a STREAM one too; an upload into a mapped STREAM
    // store goes through the map, and the GPU sees it in a copy issued after it.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: READBACK and STREAM stores are mapped where persistent maps resolve", "[algpubuffer_gl]")
    {
        const bool persistent = glBufferStorage != nullptr;
        ALGPUBuffer readback;
        ALGPUBuffer stream;
        ALGPUBuffer check;
        REQUIRE(readback.allocate(WORDS * sizeof(U32), ALGPUBuffer::READBACK, nullptr, "readback (test)"));
        REQUIRE(stream.allocate(WORDS * sizeof(U32), ALGPUBuffer::STREAM, nullptr, "stream (test)"));
        REQUIRE(check.allocate(WORDS * sizeof(U32), 0, nullptr, "check (test)"));
        UNSCOPED_INFO("readback mapped where persistent maps resolve");
        CHECK((readback.getMapped() != nullptr) == persistent);
        UNSCOPED_INFO("stream mapped where persistent maps resolve");
        CHECK((stream.getMapped() != nullptr) == persistent);

        const std::vector<U32> words = pattern(WORDS, 300u);
        stream.upload(0, words.data(), words.size() * sizeof(U32));
        check.copyFrom(stream, 0, 0, WORDS * sizeof(U32));
        UNSCOPED_INFO("the GPU sees the stream's upload");
        CHECK(readAll(check) == words);
        UNSCOPED_INFO("a coherent map has nothing to flush");
        stream.flushMapped(0, WORDS * sizeof(U32));
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // A FLUSH_EXPLICIT STREAM store is mapped where persistent maps resolve, its storage taking no flush bit and its
    // map no coherence; the GPU sees an upload into it, which flushes its own range, and a write into the map once
    // flushMapped has published that range.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: a FLUSH_EXPLICIT STREAM store's writes reach the GPU once flushed", "[algpubuffer_gl]")
    {
        const bool persistent = glBufferStorage != nullptr;
        ALGPUBuffer stream;
        ALGPUBuffer check;
        REQUIRE(stream.allocate(WORDS * sizeof(U32), ALGPUBuffer::STREAM | ALGPUBuffer::FLUSH_EXPLICIT, nullptr,
                                "flushed stream (test)"));
        REQUIRE(check.allocate(WORDS * sizeof(U32), 0, nullptr, "check (test)"));
        CHECK(stream.getFlags() == U32(ALGPUBuffer::STREAM | ALGPUBuffer::FLUSH_EXPLICIT));
        UNSCOPED_INFO("mapped where persistent maps resolve");
        CHECK((stream.getMapped() != nullptr) == persistent);

        std::vector<U32> expected = pattern(WORDS, 500u);
        stream.upload(0, expected.data(), expected.size() * sizeof(U32));
        check.copyFrom(stream, 0, 0, WORDS * sizeof(U32));
        UNSCOPED_INFO("the GPU sees the upload");
        CHECK(readAll(check) == expected);

        if (U8* const map = stream.getMapped())
        {
            GLint storage = 0;
            GLint access  = 0;
            glBindBuffer(GL_COPY_READ_BUFFER, stream.getGLName());
            glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_STORAGE_FLAGS, &storage);
            glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_ACCESS_FLAGS, &access);
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            UNSCOPED_INFO("stored for persistent writes, not coherent");
            CHECK(storage == GLint(GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT));
            UNSCOPED_INFO("mapped for persistent writes flushed explicitly");
            CHECK(access == GLint(GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_FLUSH_EXPLICIT_BIT));

            const std::vector<U32> middle = pattern(8, 9000u);
            memcpy(map + 16 * sizeof(U32), middle.data(), middle.size() * sizeof(U32));
            stream.flushMapped(16 * sizeof(U32), middle.size() * sizeof(U32));
            std::copy(middle.begin(), middle.end(), expected.begin() + 16);
            check.copyFrom(stream, 0, 0, WORDS * sizeof(U32));
            UNSCOPED_INFO("the GPU sees the flushed range");
            CHECK(readAll(check) == expected);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // ALGPUReadback: each request copies what its source held then; a request with every slot pending is refused; the
    // results come back oldest first; with none pending collect has nothing to give; and a result is collected without
    // waiting once a later frame's end has seen its frame finished, and not before, the readback laying no fence and
    // polling none of its own.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: ALGPUReadback gives back each request's copy oldest first", "[algpubuffer_gl]")
    {
        constexpr U32 SLOTS = 3;
        constexpr U32 COUNT = 16;
        ALGPUBuffer source;
        REQUIRE(source.allocate(WORDS * sizeof(U32), ALGPUBuffer::DYNAMIC, nullptr, "source (test)"));
        ALGPUReadback readback(&mFrames);
        REQUIRE(readback.allocate(COUNT * sizeof(U32), SLOTS, "readback (test)"));
        CHECK(readback.getSlotSize() == size_t(COUNT * sizeof(U32)));

        std::vector<U32> result(COUNT, 0u);
        UNSCOPED_INFO("nothing to collect before a request");
        REQUIRE_FALSE(readback.collect(result.data(), result.size() * sizeof(U32)));
        for (U32 i = 0; i < SLOTS; ++i)
        {
            const std::vector<U32> words = pattern(WORDS, 1000u * (i + 1));
            source.upload(0, words.data(), words.size() * sizeof(U32));
            CAPTURE(i);
            REQUIRE(readback.request(source, 8 * sizeof(U32), COUNT * sizeof(U32)));
        }
        CHECK(readback.getPending() == SLOTS);
        UNSCOPED_INFO("a request past them refused");
        CHECK_FALSE(readback.request(source, 0, COUNT * sizeof(U32)));

        for (U32 i = 0; i < SLOTS; ++i)
        {
            CAPTURE(i);
            REQUIRE(readback.collect(result.data(), result.size() * sizeof(U32), true));
            const std::vector<U32> words = pattern(WORDS, 1000u * (i + 1));
            UNSCOPED_INFO("what the source held at its request");
            REQUIRE(result == std::vector<U32>(words.begin() + 8, words.begin() + 8 + COUNT));
        }
        CHECK(readback.getPending() == 0u);
        UNSCOPED_INFO("nothing more to collect");
        REQUIRE_FALSE(readback.collect(result.data(), result.size() * sizeof(U32), true));

        // A request made, its copy finished: collect gives it without waiting once its frame has ended.
        const std::vector<U32> last = pattern(WORDS, 7u);
        source.upload(0, last.data(), last.size() * sizeof(U32));
        const U32 calls = mFrames.getCalls();
        CHECK(readback.request(source, 0, COUNT * sizeof(U32)));
        UNSCOPED_INFO("the request lays no fence");
        CHECK(mFrames.getCalls() == calls);
        glFinish();
        UNSCOPED_INFO("not collected while its frame is open, however long the GPU has had, and no poll");
        CHECK_FALSE(readback.collect(result.data(), result.size() * sizeof(U32)));
        CHECK(mFrames.getCalls() == calls);
        mFrames.endFrame();
        glFinish();
        UNSCOPED_INFO("nor at its frame's end");
        CHECK_FALSE(readback.collect(result.data(), result.size() * sizeof(U32)));
        mFrames.endFrame();
        UNSCOPED_INFO("collected without waiting once the next frame's end has seen its frame finished");
        REQUIRE(readback.collect(result.data(), result.size() * sizeof(U32)));
        UNSCOPED_INFO("and holds the source");
        CHECK(result == std::vector<U32>(last.begin(), last.begin() + COUNT));
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // ALGPUReadback::requestPixels: each request reads the rectangle it names of the framebuffer bound for reading, as
    // the framebuffer held it then, into its own slot; a request past the slots is refused; and the pack binding is
    // left at none, so that a read into memory after it writes to memory.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: ALGPUReadback reads pixels of the framebuffer into each slot", "[algpubuffer_gl]")
    {
        constexpr S32 SIZE = 8;
        GLuint colour = 0;
        GLuint fbo = 0;
        glGenRenderbuffers(1, &colour);
        glBindRenderbuffer(GL_RENDERBUFFER, colour);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, SIZE, SIZE);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colour);
        REQUIRE((U32)glCheckFramebufferStatus(GL_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);
        glViewport(0, 0, SIZE, SIZE);

        // The left half one colour and the right another, so that a rectangle read from the wrong place shows.
        auto fill = [](U8 left, U8 right)
        {
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, SIZE / 2, SIZE);
            glClearColor(left / 255.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            glScissor(SIZE / 2, 0, SIZE / 2, SIZE);
            glClearColor(right / 255.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        };
        // The red of each pixel of the 4 x 2 rectangle at (2, 3), row by row: two of the left half, two of the right.
        auto expected = [](U8 left, U8 right)
        {
            return std::vector<U8>{ left, left, right, right, left, left, right, right };
        };
        auto reds = [](const std::vector<U8>& rgba)
        {
            std::vector<U8> red;
            for (size_t i = 0; i < rgba.size(); i += 4)
            {
                red.push_back(rgba[i]);
            }
            return red;
        };

        constexpr S32    WIDTH = 4;
        constexpr S32    HEIGHT = 2;
        constexpr size_t BYTES = WIDTH * HEIGHT * 4;
        ALGPUReadback readback(&mFrames);
        REQUIRE(readback.allocate(BYTES, 2, "pixels (test)"));

        fill(51, 102);
        REQUIRE(readback.requestPixels(2, 3, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, BYTES));
        fill(153, 204);
        REQUIRE(readback.requestPixels(2, 3, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, BYTES));
        UNSCOPED_INFO("a request past the slots refused");
        CHECK_FALSE(readback.requestPixels(2, 3, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, BYTES));

        GLint pack = -1;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
        UNSCOPED_INFO("the pack binding left at none");
        CHECK(pack == 0);

        mFrames.endFrame();
        glFinish();
        // The next frame's end shows it finished.
        mFrames.endFrame();
        std::vector<U8> result(BYTES, 0);
        UNSCOPED_INFO("the first, its frame finished, collected without waiting");
        REQUIRE(readback.collect(result.data(), result.size()));
        UNSCOPED_INFO("the rectangle as the framebuffer held it at the first request");
        CHECK(reds(result) == expected(51, 102));
        REQUIRE(readback.collect(result.data(), result.size()));
        UNSCOPED_INFO("and at the second");
        CHECK(reds(result) == expected(153, 204));
        CHECK(readback.getPending() == 0u);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);
        glDeleteRenderbuffers(1, &colour);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // ALGPUStream: each map gives the next region, round the ring, at offsets a storage range may start at; what is
    // written there and published is what the GPU reads at the offset publish returns; and the ring wraps.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: ALGPUStream maps each region in turn round the ring", "[algpubuffer_gl]")
    {
        constexpr U32 REGIONS = 3;
        constexpr U32 COUNT = 10;
        ALGPUStream stream(&mFrames);
        REQUIRE(stream.allocate(COUNT * sizeof(U32), REGIONS, "stream (test)"));
        CHECK(stream.getRegionSize() == size_t(COUNT * sizeof(U32)));
        ALGPUBuffer check;
        REQUIRE(check.allocate(COUNT * sizeof(U32), 0, nullptr, "check (test)"));

        const size_t alignment = gGLManager.mShaderStorageBufferOffsetAlignment > 0
                               ? (size_t)gGLManager.mShaderStorageBufferOffsetAlignment
                               : 256;
        std::vector<size_t> offsets;
        for (U32 frame = 0; frame < 2 * REGIONS; ++frame)
        {
            CAPTURE(frame);
            U8* region = stream.map();
            REQUIRE(region != nullptr);
            const std::vector<U32> words = pattern(COUNT, 100u * (frame + 1));
            memcpy(region, words.data(), words.size() * sizeof(U32));
            const size_t offset = stream.publish(words.size() * sizeof(U32));
            REQUIRE(offset % alignment == 0);
            offsets.push_back(offset);

            check.copyFrom(stream.getBuffer(), offset, 0, COUNT * sizeof(U32));
            REQUIRE(readAll(check) == words);
            mFrames.endFrame();
        }
        for (U32 frame = 0; frame < REGIONS; ++frame)
        {
            for (U32 other = frame + 1; other < REGIONS; ++other)
            {
                UNSCOPED_INFO("the ring's regions apart");
                REQUIRE(offsets[frame] != offsets[other]);
            }
            UNSCOPED_INFO("the ring wraps to the same region");
            REQUIRE(offsets[frame + REGIONS] == offsets[frame]);
        }
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // ALGPUStream: a region is mapped again with no GL call once the frame its reads were issued in has finished; one
    // read in the open frame waits for what has been issued, with a fence of its own.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: ALGPUStream reuses a region once the frame it was read in has finished", "[algpubuffer_gl]")
    {
        constexpr U32 REGIONS = 2;
        constexpr U32 COUNT = 10;
        ALGPUStream stream(&mFrames);
        REQUIRE(stream.allocate(COUNT * sizeof(U32), REGIONS, "stream (test)"));

        // Both regions mapped and published in one frame: the second map notes the first region read in it.
        for (U32 i = 0; i < REGIONS; ++i)
        {
            REQUIRE(stream.map() != nullptr);
            stream.publish(COUNT * sizeof(U32));
        }
        mFrames.endFrame();
        glFinish();
        // The next frame's end shows it finished.
        mFrames.endFrame();

        const U32 calls = mFrames.getCalls();
        // Back to the first region, read in the frame that has finished. The second is noted read in this one: its
        // reads could be issued up to this map.
        REQUIRE(stream.map() != nullptr);
        stream.publish(COUNT * sizeof(U32));
        UNSCOPED_INFO("no GL call for a region whose frame has finished");
        CHECK(mFrames.getCalls() == calls);

        // The second, read in this open frame.
        REQUIRE(stream.map() != nullptr);
        stream.publish(COUNT * sizeof(U32));
        UNSCOPED_INFO("a region read in the open frame waits for what has been issued");
        CHECK(mFrames.getCalls() >= calls + 2);
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }
}
