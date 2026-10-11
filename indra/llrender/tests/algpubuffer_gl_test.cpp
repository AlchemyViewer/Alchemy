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

#include "../test/lltut.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace tut
{
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

        std::vector<U32> readAll(const ALGPUBuffer& buffer)
        {
            std::vector<U32> words(buffer.size() / sizeof(U32), 0xFFFFFFFFu);
            buffer.read(0, words.data(), words.size() * sizeof(U32));
            return words;
        }

        GLint indirectBinding()
        {
            GLint name = -1;
            glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING, &name);
            return name;
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
        void setUpProgram()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            ensure("shaders compile", vs && fs);
            mProgram.mProgramObject = glCreateProgram();
            mProgram.mName = "indirect point (test)";
            mProgram.attachObject(vs);
            mProgram.attachObject(fs);
            const bool linked = mProgram.mapAttributes();
            glDeleteShader(vs);
            glDeleteShader(fs);
            ensure("program links", linked);
            ensure("program maps", mProgram.mapUniforms());
            mProgram.bind();
        }

        ll_test::SharedGLScope mGL;
        ll_test::TestShaderMgr mShaders;
        LLGLSLShader mProgram;
    };

    typedef test_group<algpubuffer_data> algpubuffer_t;
    typedef algpubuffer_t::object algpubuffer_object_t;
    tut::algpubuffer_t tut_algpubuffer("algpubuffer_gl");
    }

    // What a buffer allocated with data holds, what an upload, a clear of a range and a whole clear leave, and what
    // the store accounts for while it lives.
    template<> template<>
    void algpubuffer_object_t::test<1>()
    {
        const U64 before = ALGPUBuffer::sBytesAllocated;
        std::vector<U32> expected = pattern(WORDS, 1000u);
        {
            ALGPUBuffer buffer;
            ensure("allocates", buffer.allocate(WORDS * sizeof(U32), ALGPUBuffer::DYNAMIC, expected.data(), "words (test)"));
            ensure("allocated", buffer.isAllocated());
            ensure_equals("its size", buffer.size(), size_t(WORDS * sizeof(U32)));
            ensure_equals("its flags", buffer.getFlags(), U32(ALGPUBuffer::DYNAMIC));
            ensure_equals("accounted", ALGPUBuffer::sBytesAllocated, before + WORDS * sizeof(U32));
            ensure("holds its data", readAll(buffer) == expected);

            const std::vector<U32> middle = pattern(8, 77u);
            buffer.upload(16 * sizeof(U32), middle.data(), middle.size() * sizeof(U32));
            std::copy(middle.begin(), middle.end(), expected.begin() + 16);
            ensure("holds the upload where it went", readAll(buffer) == expected);

            buffer.clearRange(40 * sizeof(U32), 6 * sizeof(U32), 0xDEADBEEFu);
            std::fill(expected.begin() + 40, expected.begin() + 46, 0xDEADBEEFu);
            ensure("holds the cleared range, and only it", readAll(buffer) == expected);

            std::vector<U32> part(4, 0u);
            ensure("reads at an offset", buffer.read(42 * sizeof(U32), part.data(), part.size() * sizeof(U32)));
            ensure("the words there", part == std::vector<U32>(expected.begin() + 42, expected.begin() + 46));

            buffer.clear(7u);
            ensure("whole clear", readAll(buffer) == std::vector<U32>(WORDS, 7u));
        }
        ensure_equals("destruction returns what it accounted", ALGPUBuffer::sBytesAllocated, before);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A buffer that is not DYNAMIC clears on the GPU, and copies between buffers and within one go where they are
    // told.
    template<> template<>
    void algpubuffer_object_t::test<2>()
    {
        const std::vector<U32> source_words = pattern(WORDS, 5u);
        ALGPUBuffer source;
        ALGPUBuffer target;
        ensure("source allocates", source.allocate(WORDS * sizeof(U32), 0, source_words.data(), "source (test)"));
        ensure("target allocates", target.allocate(WORDS * sizeof(U32), 0, nullptr, "target (test)"));
        target.clear(0u);
        ensure("a static buffer clears", readAll(target) == std::vector<U32>(WORDS, 0u));

        target.copyFrom(source, 4 * sizeof(U32), 32 * sizeof(U32), 10 * sizeof(U32));
        std::vector<U32> expected(WORDS, 0u);
        std::copy(source_words.begin() + 4, source_words.begin() + 14, expected.begin() + 32);
        ensure("copied between buffers", readAll(target) == expected);

        target.copyFrom(target, 32 * sizeof(U32), 0, 10 * sizeof(U32));
        std::copy(expected.begin() + 32, expected.begin() + 42, expected.begin());
        ensure("copied within one", readAll(target) == expected);
        ensure("the source untouched", readAll(source) == source_words);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // release empties a buffer and returns its accounting; a move takes the store and leaves the source empty, and a
    // move over a buffer that held a store releases that store.
    template<> template<>
    void algpubuffer_object_t::test<3>()
    {
        const U64 before = ALGPUBuffer::sBytesAllocated;
        const std::vector<U32> words = pattern(WORDS, 9u);

        ALGPUBuffer first;
        ensure("allocates", first.allocate(WORDS * sizeof(U32), 0, words.data(), "first (test)"));
        first.release();
        ensure("released", !first.isAllocated());
        ensure_equals("no size", first.size(), size_t(0));
        ensure_equals("no name", first.getGLName(), 0u);
        ensure_equals("accounting returned", ALGPUBuffer::sBytesAllocated, before);

        ensure("allocates again", first.allocate(WORDS * sizeof(U32), 0, words.data(), "first (test)"));
        const U32 name = first.getGLName();
        ALGPUBuffer moved(std::move(first));
        ensure("the source is empty", !first.isAllocated() && first.size() == 0 && first.getGLName() == 0);
        ensure_equals("the target took the name", moved.getGLName(), name);
        ensure("and the store", readAll(moved) == words);
        ensure_equals("accounted once", ALGPUBuffer::sBytesAllocated, before + WORDS * sizeof(U32));

        ALGPUBuffer other;
        ensure("other allocates", other.allocate(2 * WORDS * sizeof(U32), 0, nullptr, "other (test)"));
        other = std::move(moved);
        ensure("the moved-from is empty", !moved.isAllocated() && moved.getGLName() == 0);
        ensure_equals("the assigned took the name", other.getGLName(), name);
        ensure("and the store", readAll(other) == words);
        ensure_equals("its own store released", ALGPUBuffer::sBytesAllocated, before + WORDS * sizeof(U32));

        other.release();
        ensure_equals("all returned", ALGPUBuffer::sBytesAllocated, before);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // LLRender binds an indirect draw's buffer once and keeps it bound; after refreshState it binds it again, since
    // what is bound is no longer known; and a buffer's release forgets its name, so a new buffer that GL hands the
    // same name is bound rather than taken for bound already.
    template<> template<>
    void algpubuffer_object_t::test<4>()
    {
        setUpProgram();
        ALDrawArraysIndirectCommand command;
        command.mCount = 1;
        command.mInstanceCount = 1;

        ALGPUBuffer first;
        ensure("allocates", first.allocate(sizeof(command), 0, &command, "draw (test)"));
        gGL.drawArraysIndirect(LLRender::POINTS, first, 0);
        ensure_equals("bound for the draw", indirectBinding(), (GLint)first.getGLName());
        gGL.drawArraysIndirect(LLRender::POINTS, first, 0);
        ensure_equals("still bound", indirectBinding(), (GLint)first.getGLName());

        // Behind LLRender's back.
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
        gGL.refreshState();
        gGL.drawArraysIndirect(LLRender::POINTS, first, 0);
        ensure_equals("bound again after refreshState", indirectBinding(), (GLint)first.getGLName());
        ensure_equals("no GL error after refreshState", glGetError(), GLenum(GL_NO_ERROR));

        const U32 name = first.getGLName();
        first.release();
        ensure_equals("deleting it unbound it", indirectBinding(), 0);
        ALGPUBuffer second;
        ensure("another allocates", second.allocate(sizeof(command), 0, &command, "draw again (test)"));
        if (second.getGLName() != name)
        {
            skip("GL did not hand the released name out again");
        }
        gGL.drawArraysIndirect(LLRender::POINTS, second, 0);
        ensure_equals("the reused name is bound", indirectBinding(), (GLint)second.getGLName());
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // A READBACK store is mapped where persistent maps resolve, and a STREAM one too; an upload into a mapped STREAM
    // store goes through the map, and the GPU sees it in a copy issued after it.
    template<> template<>
    void algpubuffer_object_t::test<5>()
    {
        const bool persistent = glBufferStorage != nullptr;
        ALGPUBuffer readback;
        ALGPUBuffer stream;
        ALGPUBuffer check;
        ensure("readback allocates", readback.allocate(WORDS * sizeof(U32), ALGPUBuffer::READBACK, nullptr, "readback (test)"));
        ensure("stream allocates", stream.allocate(WORDS * sizeof(U32), ALGPUBuffer::STREAM, nullptr, "stream (test)"));
        ensure("check allocates", check.allocate(WORDS * sizeof(U32), 0, nullptr, "check (test)"));
        ensure_equals("readback mapped where persistent maps resolve", readback.getMapped() != nullptr, persistent);
        ensure_equals("stream too", stream.getMapped() != nullptr, persistent);

        const std::vector<U32> words = pattern(WORDS, 300u);
        stream.upload(0, words.data(), words.size() * sizeof(U32));
        check.copyFrom(stream, 0, 0, WORDS * sizeof(U32));
        ensure("the GPU sees the stream's upload", readAll(check) == words);
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // ALGPUReadback: each request copies what its source held then; a request with every slot pending is refused; the
    // results come back oldest first; and with none pending collect has nothing to give.
    template<> template<>
    void algpubuffer_object_t::test<6>()
    {
        constexpr U32 SLOTS = 3;
        constexpr U32 COUNT = 16;
        ALGPUBuffer source;
        ensure("source allocates", source.allocate(WORDS * sizeof(U32), ALGPUBuffer::DYNAMIC, nullptr, "source (test)"));
        ALGPUReadback readback;
        ensure("readback allocates", readback.allocate(COUNT * sizeof(U32), SLOTS, "readback (test)"));
        ensure_equals("a slot's size", readback.getSlotSize(), size_t(COUNT * sizeof(U32)));

        std::vector<U32> result(COUNT, 0u);
        ensure("nothing to collect before a request", !readback.collect(result.data(), result.size() * sizeof(U32)));
        for (U32 i = 0; i < SLOTS; ++i)
        {
            const std::vector<U32> words = pattern(WORDS, 1000u * (i + 1));
            source.upload(0, words.data(), words.size() * sizeof(U32));
            ensure("request " + std::to_string(i), readback.request(source, 8 * sizeof(U32), COUNT * sizeof(U32)));
        }
        ensure_equals("every slot pending", readback.getPending(), SLOTS);
        ensure("a request past them refused", !readback.request(source, 0, COUNT * sizeof(U32)));

        for (U32 i = 0; i < SLOTS; ++i)
        {
            ensure("collect " + std::to_string(i), readback.collect(result.data(), result.size() * sizeof(U32), true));
            const std::vector<U32> words = pattern(WORDS, 1000u * (i + 1));
            ensure("result " + std::to_string(i) + " is what the source held at its request",
                   result == std::vector<U32>(words.begin() + 8, words.begin() + 8 + COUNT));
        }
        ensure_equals("none pending", readback.getPending(), 0u);
        ensure("nothing more to collect", !readback.collect(result.data(), result.size() * sizeof(U32), true));

        // A request made, its copy finished: collect gives it without waiting.
        const std::vector<U32> last = pattern(WORDS, 7u);
        source.upload(0, last.data(), last.size() * sizeof(U32));
        ensure("one more request", readback.request(source, 0, COUNT * sizeof(U32)));
        glFinish();
        ensure("a finished copy is collected without waiting", readback.collect(result.data(), result.size() * sizeof(U32)));
        ensure("and holds the source", result == std::vector<U32>(last.begin(), last.begin() + COUNT));
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }

    // ALGPUStream: each map gives the next region, round the ring, at offsets a storage range may start at; what is
    // written there and published is what the GPU reads at the offset publish returns; and the ring wraps.
    template<> template<>
    void algpubuffer_object_t::test<7>()
    {
        constexpr U32 REGIONS = 3;
        constexpr U32 COUNT = 10;
        ALGPUStream stream;
        ensure("stream allocates", stream.allocate(COUNT * sizeof(U32), REGIONS, "stream (test)"));
        ensure_equals("a region's size", stream.getRegionSize(), size_t(COUNT * sizeof(U32)));
        ALGPUBuffer check;
        ensure("check allocates", check.allocate(COUNT * sizeof(U32), 0, nullptr, "check (test)"));

        const size_t alignment = gGLManager.mShaderStorageBufferOffsetAlignment > 0
                               ? (size_t)gGLManager.mShaderStorageBufferOffsetAlignment
                               : 256;
        std::vector<size_t> offsets;
        for (U32 frame = 0; frame < 2 * REGIONS; ++frame)
        {
            U8* region = stream.map();
            ensure("a region to write", region != nullptr);
            const std::vector<U32> words = pattern(COUNT, 100u * (frame + 1));
            memcpy(region, words.data(), words.size() * sizeof(U32));
            const size_t offset = stream.publish(words.size() * sizeof(U32));
            ensure("frame " + std::to_string(frame) + " at an aligned offset", offset % alignment == 0);
            offsets.push_back(offset);

            check.copyFrom(stream.getBuffer(), offset, 0, COUNT * sizeof(U32));
            ensure("frame " + std::to_string(frame) + " reaches the GPU", readAll(check) == words);
        }
        for (U32 frame = 0; frame < REGIONS; ++frame)
        {
            for (U32 other = frame + 1; other < REGIONS; ++other)
            {
                ensure("the ring's regions apart", offsets[frame] != offsets[other]);
            }
            ensure_equals("the ring wraps to the same region", offsets[frame + REGIONS], offsets[frame]);
        }
        ensure_equals("no GL error", glGetError(), GLenum(GL_NO_ERROR));
    }
}
