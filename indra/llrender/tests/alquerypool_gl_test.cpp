/**
 * @file alquerypool_gl_test.cpp
 * @brief ALQueryPool on the hidden window: each kind's names kept to its own target, the targets one query holds
 *        refused to the rest, results read only once a later frame's end has seen their frame finished, with no poll
 *        of their own, queries across a frame's end, the ring reading what it comes back to, results written by the
 *        GPU or asked of GL, pipeline statistics, spans, release, and conditional draws.
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
#include "../alquerypool.h"
#include "../llgl.h"
#include "../llglheaders.h"
#include "../llglstates.h"
#include "../llrender.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
        constexpr S32 SIZE = 16;

        // A triangle over the whole viewport, or wholly beside it where `hidden` is set, so that no sample passes.
        const char* kVertex =
            "#version 400\n"
            "uniform int hidden;\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
            "    gl_Position = vec4(p + (hidden != 0 ? 4.0 : 0.0), 0.0, 1.0);\n"
            "}\n";

        const char* kFragment =
            "#version 400\n"
            "out vec4 frag_color;\n"
            "void main() { frag_color = vec4(1.0); }\n";

        // Puts the timestamps flag back however the case leaves.
        struct TimestampFlag
        {
            explicit TimestampFlag(bool counts) : mHad(gGLManager.mHasTimestampQuery)
            {
                gGLManager.mHasTimestampQuery = counts;
            }
            ~TimestampFlag() { gGLManager.mHasTimestampQuery = mHad; }

            bool mHad;
        };

        // Puts the direct state access flag back however the case leaves. Turns it off, or leaves it as the context
        // has it.
        struct DirectStateAccessFlag
        {
            explicit DirectStateAccessFlag(bool named) : mHad(gGLManager.mHasDirectStateAccess)
            {
                gGLManager.mHasDirectStateAccess = mHad && named;
            }
            ~DirectStateAccessFlag() { gGLManager.mHasDirectStateAccess = mHad; }

            bool mHad;
        };

        // Puts the flag for the GPU's writes of the results back however the case leaves. Turns the writes on wherever
        // the context has query buffers, whatever its driver, or off.
        struct QueryBufferFlag
        {
            explicit QueryBufferFlag(bool writes) : mHad(gGLManager.mWritesQueryResults)
            {
                gGLManager.mWritesQueryResults = gGLManager.mHasQueryBufferObject && writes;
            }
            ~QueryBufferFlag() { gGLManager.mWritesQueryResults = mHad; }

            bool mHad;
        };

    // The target, program, frame fences and pool go here; the target's binding, the viewport and the tests' state with
    // mGL.
    struct alquerypool_data
    {
        ~alquerypool_data()
        {
            mPool.release();
            mFrames.release();
            if (mFBO)
            {
                glDeleteFramebuffers(1, &mFBO);
            }
            if (mColor)
            {
                glDeleteRenderbuffers(1, &mColor);
            }
            if (mVAO)
            {
                glDeleteVertexArrays(1, &mVAO);
            }
            if (mProgram)
            {
                glDeleteProgram(mProgram);
            }
        }

        // A SIZE x SIZE RGBA8 target cleared to black, with nothing between a fragment and its sample.
        void setUp()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kFragment);
            {
                INFO("shaders compile");
                REQUIRE(vs);
                REQUIRE(fs);
            }
            mProgram = glCreateProgram();
            glAttachShader(mProgram, vs);
            glAttachShader(mProgram, fs);
            glLinkProgram(mProgram);
            glDeleteShader(vs);
            glDeleteShader(fs);
            GLint linked = GL_FALSE;
            glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);

            glGenVertexArrays(1, &mVAO);
            glGenRenderbuffers(1, &mColor);
            glBindRenderbuffer(GL_RENDERBUFFER, mColor);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, SIZE, SIZE);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            glGenFramebuffers(1, &mFBO);
            glBindFramebuffer(GL_FRAMEBUFFER, mFBO);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, mColor);
            REQUIRE((U32)glCheckFramebufferStatus(GL_FRAMEBUFFER) == (U32)GL_FRAMEBUFFER_COMPLETE);

            glViewport(0, 0, SIZE, SIZE);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            clear();
            // Errors from before the case are not the case's.
            while (glGetError() != GL_NO_ERROR)
            {
            }
        }

        void clear()
        {
            glClearColor(0.f, 0.f, 0.f, 0.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        void draw(bool hidden)
        {
            glUseProgram(mProgram);
            glUniform1i(glGetUniformLocation(mProgram, "hidden"), hidden ? 1 : 0);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }

        U8 centreRed()
        {
            U8 rgba[4] = {};
            glFinish();
            glReadPixels(SIZE / 2, SIZE / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            return rgba[0];
        }

        // The frame ended as LLRender ends it: its batch closed, then its fence laid.
        void endFrame()
        {
            mPool.endFrame();
            mFrames.endFrame();
        }

        // The frame's batch closed, finished on the GPU and shown finished, as the next frame's end shows it, then read.
        void finishFrame()
        {
            endFrame();
            glFinish();
            REQUIRE(mFrames.wait(mFrames.frame() - 1, 1000000000ull));
            mPool.retire();
        }

        ll_test::SharedGLScope mGL;
        ALFrameFences mFrames;
        ALQueryPool mPool{ mFrames };
        GLuint mProgram = 0;
        GLuint mVAO = 0;
        GLuint mColor = 0;
        GLuint mFBO = 0;
    };


    // Each frame begins the kinds, and an elapsed span, in another order, so that names shared between kinds would meet
    // another target the second time round the ring.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: each kind's names stay begun with its own target", "[alquerypool_gl]")
    {
        setUp();
        // The spans are elapsed queries, which hold a target of their own.
        TimestampFlag no_timestamps(false);
        std::vector<ALQueryKind> kinds = { ALQueryKind::Occlusion, ALQueryKind::Samples, ALQueryKind::Primitives,
                                           ALQueryKind::OcclusionExact, ALQueryKind::Count };
        if (ALQueryPool::supports(ALQueryKind::Statistics))
        {
            kinds.push_back(ALQueryKind::Statistics);
        }
        const U32 begun = U32(kinds.size());
        ALQueryTicket samples;
        for (U32 frame = 0; frame < ALQueryPool::FRAMES * 2; ++frame)
        {
            for (U32 i = 0; i < begun; ++i)
            {
                const ALQueryKind kind = kinds[(i + frame) % begun];
                if (kind == ALQueryKind::Count)
                {
                    ALQuerySpan span = mPool.beginSpan();
                    REQUIRE(span.isValid());
                    draw(false);
                    mPool.endSpan(span);
                    continue;
                }
                ALQueryScope scope(mPool, kind);
                REQUIRE(scope.ticket().isValid());
                draw(false);
                if (kind == ALQueryKind::Samples)
                {
                    samples = scope.ticket();
                }
            }
            finishFrame();
        }
        {
            INFO("no begin met a name of another target");
            CHECK(glGetError() == GL_NO_ERROR);
        }
        U64 count = 0;
        {
            INFO("the last frame's samples are every pixel");
            REQUIRE(mPool.result(samples, count) == ALQueryPool::Status::Ready);
            CHECK(count == U64(SIZE * SIZE));
        }
    }

    // SAMPLES_PASSED and the two ANY_SAMPLES_PASSED targets are one: while one query holds it, the pool begins none of
    // the rest, and the query holding it counts on. A reserved target is refused the same way.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: a target held is refused to the rest, and counts on", "[alquerypool_gl]")
    {
        setUp();
        const ALQueryTicket samples = mPool.begin(ALQueryKind::Samples);
        REQUIRE(samples.isValid());
        {
            INFO("no occlusion query begins while SAMPLES_PASSED is open");
            CHECK_FALSE(mPool.canBegin(ALQueryKind::Occlusion));
            CHECK_FALSE(mPool.canBegin(ALQueryKind::OcclusionExact));
            CHECK_FALSE(mPool.begin(ALQueryKind::Occlusion).isValid());
        }
        {
            INFO("other targets are free");
            CHECK(mPool.canBegin(ALQueryKind::Primitives));
            CHECK(mPool.canBegin(ALQueryKind::Statistics) == ALQueryPool::supports(ALQueryKind::Statistics));
        }
        draw(false);
        mPool.end(ALQueryTicket());
        mPool.end(samples);
        U64 count = 0;
        {
            INFO("the query holding the target counted the draw");
            REQUIRE(mPool.wait(samples, count));
            CHECK(count == U64(SIZE * SIZE));
            CHECK(glGetError() == GL_NO_ERROR);
        }

        mPool.reserve(ALQueryKind::Samples, true);
        {
            INFO("a reserved target is refused to begin()");
            CHECK_FALSE(mPool.canBegin(ALQueryKind::Occlusion));
            CHECK_FALSE(mPool.begin(ALQueryKind::Samples).isValid());
        }
        const ALQueryTicket reserved = mPool.beginReserved(ALQueryKind::Samples);
        REQUIRE(reserved.isValid());
        draw(false);
        mPool.end(reserved);
        mPool.reserve(ALQueryKind::Samples, false);
        {
            INFO("and given back");
            CHECK(mPool.canBegin(ALQueryKind::Occlusion));
            REQUIRE(mPool.wait(reserved, count));
            CHECK(count == U64(SIZE * SIZE));
            CHECK(glGetError() == GL_NO_ERROR);
        }
    }

    // A frame's results are read only after its end, then stay RESULT_FRAMES frames: past the reuse of its batch's
    // names, for a user that reads only every few frames, as the object cache's cull does while the camera rests.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: results are read once their frame has ended, and kept past the reuse of its names", "[alquerypool_gl]")
    {
        setUp();
        ALQueryTicket seen;
        {
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            seen = scope.ticket();
            draw(false);
        }
        ALQueryTicket unseen;
        {
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            unseen = scope.ticket();
            draw(true);
        }
        REQUIRE(seen.isValid());
        REQUIRE(unseen.isValid());

        U64 value = 7;
        glFinish();
        mPool.retire();
        {
            INFO("not read while its frame is open, however long the GPU has had");
            CHECK(mPool.result(seen, value) == ALQueryPool::Status::Pending);
        }

        finishFrame();
        {
            INFO("read once its frame has ended and the GPU finished it");
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
            REQUIRE(mPool.result(unseen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 0);
        }

        // Every frame from here counts something, so that each batch is read and its results kept in turn.
        auto frame_with_query = [this]()
        {
            {
                ALQueryScope scope(mPool, ALQueryKind::Occlusion);
                draw(true);
            }
            finishFrame();
        };

        // Its frame was the first; the last of RESULT_FRAMES - 1 frames more still has it.
        for (U32 frame = 1; frame < ALQueryPool::FRAMES * 2; ++frame)
        {
            frame_with_query();
        }
        {
            INFO("still there once its batch's names are counting again");
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
        }
        for (U32 frame = ALQueryPool::FRAMES * 2; frame < ALQueryPool::RESULT_FRAMES; ++frame)
        {
            frame_with_query();
        }
        {
            INFO("still there " << ALQueryPool::RESULT_FRAMES - 1 << " frames on");
            CHECK(mPool.result(seen, value) == ALQueryPool::Status::Ready);
        }
        frame_with_query();
        {
            INFO("expired once its frame's results make way, RESULT_FRAMES frames on");
            CHECK(mPool.result(seen, value) == ALQueryPool::Status::Expired);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // wait() reads a query of the frame still open, and nothing for one still counting.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: wait reads a query of the open frame", "[alquerypool_gl]")
    {
        setUp();
        const ALQueryTicket open = mPool.begin(ALQueryKind::Samples);
        REQUIRE(open.isValid());
        draw(true);
        U64 count = 7;
        {
            INFO("a query still open is not waited for");
            CHECK_FALSE(mPool.wait(open, count));
        }
        mPool.end(open);
        {
            INFO("an ended one is");
            REQUIRE(mPool.wait(open, count));
            CHECK(count == 0);
            CHECK(mPool.result(open, count) == ALQueryPool::Status::Pending);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Spans of timestamps nest; spans of Elapsed queries, where the timestamps count nothing, do not.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: timestamp spans nest, and Elapsed spans refuse to", "[alquerypool_gl]")
    {
        setUp();
        U64 outer_ns = 0;
        U64 inner_ns = 0;
        if (gGLManager.mHasTimestampQuery)
        {
            ALQuerySpan outer = mPool.beginSpan();
            ALQuerySpan inner = mPool.beginSpan();
            REQUIRE(outer.isValid());
            REQUIRE(inner.isValid());
            draw(false);
            mPool.endSpan(inner);
            mPool.endSpan(outer);
            INFO("both are read, the outer at least as long");
            REQUIRE(mPool.wait(outer, outer_ns));
            REQUIRE(mPool.wait(inner, inner_ns));
            CHECK(outer_ns >= inner_ns);
        }
        {
            TimestampFlag no_timestamps(false);
            ALQuerySpan outer = mPool.beginSpan();
            REQUIRE(outer.isValid());
            ALQuerySpan inner = mPool.beginSpan();
            {
                INFO("a second Elapsed span is refused while the first is open");
                CHECK_FALSE(inner.isValid());
            }
            draw(false);
            mPool.endSpan(inner);
            mPool.endSpan(outer);
            INFO("the first is read");
            CHECK(mPool.wait(outer, outer_ns));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // release() deletes the queries, and every ticket expires; the pool counts again after.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: release deletes the queries and expires the tickets", "[alquerypool_gl]")
    {
        setUp();
        ALQueryTicket ticket;
        GLint name = 0;
        {
            ALQueryScope scope(mPool, ALQueryKind::OcclusionExact);
            ticket = scope.ticket();
            glGetQueryiv(GL_ANY_SAMPLES_PASSED, GL_CURRENT_QUERY, &name);
            draw(false);
        }
        REQUIRE(name != 0);
        REQUIRE(glIsQuery(name) == GL_TRUE);
        REQUIRE(mPool.getNameCount() > 0);

        mPool.release();
        U64 value = 0;
        {
            INFO("the query is deleted and its ticket expired");
            CHECK(glIsQuery(name) == GL_FALSE);
            CHECK(mPool.getNameCount() == 0);
            CHECK(mPool.result(ticket, value) == ALQueryPool::Status::Expired);
        }

        ALQueryTicket again;
        {
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            again = scope.ticket();
            draw(false);
        }
        finishFrame();
        {
            INFO("the pool counts again");
            REQUIRE(mPool.result(again, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A query still counting when release() comes is ended with its deletion, which GL would otherwise keep counting on
    // its target; its ticket ends nothing after.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: release ends a query still counting", "[alquerypool_gl]")
    {
        setUp();
        const ALQueryTicket open = mPool.begin(ALQueryKind::Samples);
        REQUIRE(open.isValid());
        draw(false);
        mPool.release();
        mPool.end(open);

        const ALQueryTicket again = mPool.begin(ALQueryKind::Samples);
        REQUIRE(again.isValid());
        draw(false);
        mPool.end(again);
        U64 count = 0;
        {
            INFO("its target counts for the next query");
            REQUIRE(mPool.wait(again, count));
            CHECK(count == U64(SIZE * SIZE));
            CHECK(glGetError() == GL_NO_ERROR);
        }
    }

    // A query begun in a frame and ended in the next holds its batch unread until it ends, and counts the draws on
    // both sides of the frame's end. The batch's other queries are read with it, from the same slot of their own kind.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: a query open across a frame's end holds its batch until it ends", "[alquerypool_gl]")
    {
        setUp();
        const ALQueryTicket open = mPool.begin(ALQueryKind::Samples);
        REQUIRE(open.isValid());
        ALQueryTicket primitives;
        {
            ALQueryScope scope(mPool, ALQueryKind::Primitives);
            primitives = scope.ticket();
            draw(false);
        }
        REQUIRE(primitives.isValid());
        endFrame();
        draw(false);
        glFinish();
        mPool.retire();
        U64 count = 0;
        {
            INFO("not read while it counts");
            CHECK(mPool.result(open, count) == ALQueryPool::Status::Pending);
        }
        mPool.end(open);
        glFinish();
        // The frame it began in shown finished, as the next frame's end shows it.
        REQUIRE(mFrames.wait(mFrames.frame() - 1, 1000000000ull));
        mPool.retire();
        {
            INFO("nor once it has ended, while the frame it ended in is open, the frame it began in finished");
            CHECK(mPool.result(open, count) == ALQueryPool::Status::Pending);
        }
        finishFrame();
        {
            INFO("read once it has ended, with both frames' draws");
            REQUIRE(mPool.result(open, count) == ALQueryPool::Status::Ready);
            CHECK(count == U64(2 * SIZE * SIZE));
            CHECK(glGetError() == GL_NO_ERROR);
        }
        {
            INFO("and the query that ended in its own frame with it");
            REQUIRE(mPool.result(primitives, count) == ALQueryPool::Status::Ready);
            CHECK(count == 1);
        }
    }

    // Where the GPU writes the results, reading a batch asks GL for none: each kind's, and each slot's, in its own place,
    // round the ring and back. Tested wherever the context has query buffers, whichever driver the session writes on.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: results are read from the GPU's writes, asking GL for none", "[alquerypool_gl]")
    {
        setUp();
        if (!gGLManager.mHasQueryBufferObject)
        {
            SKIP("the context has no query buffers");
        }
        QueryBufferFlag writes(true);
        const bool statistics = ALQueryPool::supports(ALQueryKind::Statistics);
        const U32  asked      = mPool.getAskedReads();
        auto query = [this](ALQueryKind kind, bool hidden)
        {
            ALQueryScope scope(mPool, kind);
            draw(hidden);
            return scope.ticket();
        };

        ALQueryTicket seen, unseen, exact, samples, no_samples, primitives, counted;
        ALQuerySpan   span;
        for (U32 frame = 0; frame < ALQueryPool::FRAMES * 2; ++frame)
        {
            span       = mPool.beginSpan();
            seen       = query(ALQueryKind::Occlusion, false);
            unseen     = query(ALQueryKind::Occlusion, true);
            samples    = query(ALQueryKind::Samples, false);
            no_samples = query(ALQueryKind::Samples, true);
            exact      = query(ALQueryKind::OcclusionExact, false);
            primitives = query(ALQueryKind::Primitives, false);
            if (statistics)
            {
                counted = query(ALQueryKind::Statistics, false);
            }
            mPool.endSpan(span);
            finishFrame();
        }

        U64 value = 7;
        {
            INFO("each kind's results, each slot's apart");
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
            REQUIRE(mPool.result(unseen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 0);
            REQUIRE(mPool.result(samples, value) == ALQueryPool::Status::Ready);
            CHECK(value == U64(SIZE * SIZE));
            REQUIRE(mPool.result(no_samples, value) == ALQueryPool::Status::Ready);
            CHECK(value == 0);
            REQUIRE(mPool.result(exact, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
            REQUIRE(mPool.result(primitives, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
            CHECK(mPool.result(span, value) == ALQueryPool::Status::Ready);
        }
        if (statistics)
        {
            ALStatistics stats;
            REQUIRE(mPool.result(counted, stats) == ALQueryPool::Status::Ready);
            CHECK(stats[ALStatistic::ClippingPrimitives] == 1);
            CHECK(stats[ALStatistic::FragmentInvocations] >= U64(SIZE * SIZE));
        }
        {
            INFO("no batch was read by asking GL");
            CHECK(mPool.getAskedReads() == asked);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Without direct state access the GPU writes the results through the query buffer binding, which is left as it was.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: without direct state access, results are written through the binding", "[alquerypool_gl]")
    {
        setUp();
        if (!gGLManager.mHasQueryBufferObject)
        {
            SKIP("the context has no query buffers");
        }
        QueryBufferFlag       writes(true);
        DirectStateAccessFlag through_binding(false);
        const U32 asked = mPool.getAskedReads();
        ALQueryTicket samples, unseen;
        for (U32 frame = 0; frame < ALQueryPool::FRAMES * 2; ++frame)
        {
            {
                ALQueryScope scope(mPool, ALQueryKind::Samples);
                samples = scope.ticket();
                draw(false);
            }
            {
                ALQueryScope scope(mPool, ALQueryKind::Occlusion);
                unseen = scope.ticket();
                draw(true);
            }
            finishFrame();
        }
        U64 value = 7;
        {
            INFO("written by the GPU, and asked of GL for none");
            REQUIRE(mPool.result(samples, value) == ALQueryPool::Status::Ready);
            CHECK(value == U64(SIZE * SIZE));
            REQUIRE(mPool.result(unseen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 0);
            CHECK(mPool.getAskedReads() == asked);
        }
        GLint bound = -1;
        glGetIntegerv(GL_QUERY_BUFFER_BINDING, &bound);
        {
            INFO("no buffer left at the binding");
            CHECK(bound == 0);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Without query buffers, a batch's results are asked of GL, once its frame has finished.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: without query buffers, results are asked of GL", "[alquerypool_gl]")
    {
        setUp();
        QueryBufferFlag no_buffers(false);
        const U32 asked = mPool.getAskedReads();
        ALQueryTicket samples;
        {
            ALQueryScope scope(mPool, ALQueryKind::Samples);
            samples = scope.ticket();
            draw(false);
        }
        finishFrame();
        U64 count = 0;
        {
            INFO("read, by asking GL");
            REQUIRE(mPool.result(samples, count) == ALQueryPool::Status::Ready);
            CHECK(count == U64(SIZE * SIZE));
            CHECK(mPool.getAskedReads() == asked + 1);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Off NVIDIA's driver the results are asked of GL, whether the context has query buffers or not (see
    // LLGLManager::initQueries).
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: off NVIDIA's driver, results are asked of GL", "[alquerypool_gl]")
    {
        setUp();
        if (gGLManager.mIsNVIDIA)
        {
            SKIP("NVIDIA's driver has the GPU write the results");
        }
        {
            INFO("no writes, whether the context has query buffers or not");
            CHECK_FALSE(gGLManager.mWritesQueryResults);
        }
        const U32 asked = mPool.getAskedReads();
        ALQueryTicket samples;
        {
            ALQueryScope scope(mPool, ALQueryKind::Samples);
            samples = scope.ticket();
            draw(false);
        }
        finishFrame();
        U64 count = 0;
        {
            INFO("read, by asking GL");
            REQUIRE(mPool.result(samples, count) == ALQueryPool::Status::Ready);
            CHECK(count == U64(SIZE * SIZE));
            CHECK(mPool.getAskedReads() == asked + 1);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A batch the ring comes back to is read before its names count again, retired or not; once the GPU has finished
    // it, without a stall.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: the ring reads a batch it comes back to", "[alquerypool_gl]")
    {
        setUp();
        ALQueryTicket seen;
        {
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            seen = scope.ticket();
            draw(false);
        }
        REQUIRE(seen.isValid());
        const U32 stalls = mPool.getStalls();
        endFrame();
        // The frame's fence too.
        glFinish();
        for (U32 frame = 1; frame < ALQueryPool::FRAMES; ++frame)
        {
            endFrame();
        }
        U64 value = 0;
        {
            INFO("read when its batch was reused, with retire() never called");
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
        }
        {
            INFO("no stall: the GPU had finished it");
            CHECK(mPool.getStalls() == stalls);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A batch is read once the frame fences show its frame finished, and not before: closed but its frame not ended, it
    // waits, however long the GPU has had.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: a batch is read only once its frame is shown finished", "[alquerypool_gl]")
    {
        setUp();
        ALQueryTicket seen;
        {
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            seen = scope.ticket();
            draw(false);
        }
        mPool.endFrame();
        glFinish();
        mPool.retire();
        U64 value = 0;
        {
            INFO("closed, with its frame's fence not laid");
            CHECK(mPool.result(seen, value) == ALQueryPool::Status::Pending);
        }
        mFrames.endFrame();
        glFinish();
        mPool.retire();
        {
            INFO("nor at its frame's end, however long the GPU has had");
            CHECK(mPool.result(seen, value) == ALQueryPool::Status::Pending);
        }
        endFrame();
        mPool.retire();
        {
            INFO("read once the next frame's end has seen its frame finished");
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // retire() polls no fence: a batch is read once a later frame's end has seen its frame finished.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: retire polls nothing, and reads what a frame's end saw", "[alquerypool_gl]")
    {
        setUp();
        ALQueryTicket seen;
        {
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            seen = scope.ticket();
            draw(false);
        }
        endFrame();
        glFinish();
        const U32 calls = mFrames.getCalls();
        mPool.retire();
        U64 value = 0;
        {
            INFO("no poll, and nothing read: its own frame's end does not look at it");
            CHECK(mFrames.getCalls() == calls);
            CHECK(mPool.result(seen, value) == ALQueryPool::Status::Pending);
        }
        endFrame();
        const U32 ended = mFrames.getCalls();
        mPool.retire();
        {
            INFO("read once the next frame's end has seen its frame finished, still with no poll");
            CHECK(mFrames.getCalls() == ended);
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A Statistics query counts every pipeline statistic at once, on targets of its own: the occlusion and primitives
    // queries of the frame begin while it counts.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: a statistics query counts its statistics and holds no other target", "[alquerypool_gl]")
    {
        setUp();
        if (!ALQueryPool::supports(ALQueryKind::Statistics))
        {
            SKIP("the context counts no pipeline statistics");
        }
        const ALQueryTicket statistics = mPool.begin(ALQueryKind::Statistics);
        REQUIRE(statistics.isValid());
        {
            INFO("the frame's other targets stay free, and a second statistics query is refused");
            CHECK(mPool.canBegin(ALQueryKind::Occlusion));
            CHECK(mPool.canBegin(ALQueryKind::Primitives));
            CHECK_FALSE(mPool.canBegin(ALQueryKind::Statistics));
        }
        ALQueryTicket seen;
        {
            ALQueryScope occlusion(mPool, ALQueryKind::Occlusion);
            seen = occlusion.ticket();
            draw(false);
        }
        mPool.end(statistics);
        finishFrame();

        ALStatistics counted;
        U64 value = 0;
        {
            INFO("one triangle clipped, every pixel shaded, and the occlusion query counted under it");
            REQUIRE(mPool.result(statistics, counted) == ALQueryPool::Status::Ready);
            CHECK(counted[ALStatistic::ClippingPrimitives] == 1);
            CHECK(counted[ALStatistic::FragmentInvocations] >= U64(SIZE * SIZE));
            REQUIRE(mPool.result(seen, value) == ALQueryPool::Status::Ready);
            CHECK(value == 1);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A conditional scope draws only where its query let a sample through, and draws with no query at all.
    TEST_CASE_METHOD(alquerypool_data, "alquerypool_gl: a conditional draw runs only if its query saw a sample", "[alquerypool_gl]")
    {
        setUp();
        // The queries count with colour writes off, so that only the conditional draws reach the target.
        auto query = [this](bool hidden)
        {
            LLGLSColorMask no_color(false, false);
            ALQueryScope scope(mPool, ALQueryKind::Occlusion);
            draw(hidden);
            return scope.ticket();
        };

        const ALQueryTicket none = query(true);
        {
            ALConditionalRender if_seen(mPool, none);
            draw(false);
        }
        {
            INFO("nothing passed, so the conditional draw did not run");
            CHECK(centreRed() == 0);
        }

        const ALQueryTicket some = query(false);
        {
            ALConditionalRender if_seen(mPool, some);
            draw(false);
        }
        {
            INFO("a sample passed, so it ran");
            CHECK(centreRed() == 255);
        }

        clear();
        {
            ALConditionalRender no_query(mPool, ALQueryTicket());
            draw(false);
        }
        {
            INFO("with no query it runs");
            CHECK(centreRed() == 255);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
}
