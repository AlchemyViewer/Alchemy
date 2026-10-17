/**
 * @file algpubuffer_gl_test.cpp
 * @brief ALGPUBuffer on the hidden window: what its store holds after each way of writing it, what it accounts, how
 *        a move leaves it, which stores are mapped; LLRender's indirect draw binding over a buffer's life; and the
 *        rings, ALGPUReadback's results in order and ALGPUStream's regions round the ring.
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

    // The program the tests bind, and compute's switch, go back with mGL.
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
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // ALGPUReadback: each request copies what its source held then; a request with every slot pending is refused; the
    // results come back oldest first; and with none pending collect has nothing to give.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: ALGPUReadback gives back each request's copy oldest first", "[algpubuffer_gl]")
    {
        constexpr U32 SLOTS = 3;
        constexpr U32 COUNT = 16;
        ALGPUBuffer source;
        REQUIRE(source.allocate(WORDS * sizeof(U32), ALGPUBuffer::DYNAMIC, nullptr, "source (test)"));
        ALGPUReadback readback;
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

        // A request made, its copy finished: collect gives it without waiting.
        const std::vector<U32> last = pattern(WORDS, 7u);
        source.upload(0, last.data(), last.size() * sizeof(U32));
        CHECK(readback.request(source, 0, COUNT * sizeof(U32)));
        glFinish();
        UNSCOPED_INFO("a finished copy is collected without waiting");
        REQUIRE(readback.collect(result.data(), result.size() * sizeof(U32)));
        UNSCOPED_INFO("and holds the source");
        CHECK(result == std::vector<U32>(last.begin(), last.begin() + COUNT));
        CHECK(glGetError() == GLenum(GL_NO_ERROR));
    }

    // ALGPUStream: each map gives the next region, round the ring, at offsets a storage range may start at; what is
    // written there and published is what the GPU reads at the offset publish returns; and the ring wraps.
    TEST_CASE_METHOD(algpubuffer_data, "algpubuffer_gl: ALGPUStream maps each region in turn round the ring", "[algpubuffer_gl]")
    {
        constexpr U32 REGIONS = 3;
        constexpr U32 COUNT = 10;
        ALGPUStream stream;
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
}
