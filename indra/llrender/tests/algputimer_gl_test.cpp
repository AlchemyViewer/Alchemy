/**
 * @file algputimer_gl_test.cpp
 * @brief ALGPUTimer on the hidden window: a span's time read back, one span at a time, as timestamps and as Elapsed
 *        queries, and a time left unread past its frame's batch.
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
#include "../algputimer.h"
#include "../alquerypool.h"
#include "../llgl.h"
#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
    // The GL_TIME_ELAPSED query open on the context, 0 for none.
    GLint openTimer()
    {
        GLint name = -1;
        glGetQueryiv(GL_TIME_ELAPSED, GL_CURRENT_QUERY, &name);
        return name;
    }

    // Puts the timestamps flag back however the case leaves.
    struct TimestampFlag
    {
        explicit TimestampFlag(bool counts) : mHad(gGLManager.mHasTimestampQuery)
        {
            gGLManager.mHasTimestampQuery = mHad && counts;
        }
        ~TimestampFlag() { gGLManager.mHasTimestampQuery = mHad; }

        bool mHad;
    };

    // The spans to try: timestamps where the context counts them, and Elapsed queries everywhere.
    std::vector<bool> span_kinds()
    {
        std::vector<bool> kinds{ false };
        if (gGLManager.mHasTimestampQuery)
        {
            kinds.push_back(true);
        }
        return kinds;
    }

    // The pool goes here, and a timer query left open, so that the next case can open one.
    struct algputimer_data
    {
        algputimer_data()
        {
            // An error the context held before the case is not the case's.
            while (glGetError() != GL_NO_ERROR)
            {
            }
        }

        ~algputimer_data()
        {
            if (openTimer() != 0)
            {
                glEndQuery(GL_TIME_ELAPSED);
            }
            mPool.release();
            mFrames.release();
        }

        // The frame ended as LLRender ends it: the pool's batch closed, then the frame's fence laid.
        void endFrame()
        {
            mPool.endFrame();
            mFrames.endFrame();
        }

        // The frame ended, finished on the GPU and shown finished, as the next frame's end shows it, then read.
        void finishFrame()
        {
            endFrame();
            glFinish();
            REQUIRE(mFrames.wait(mFrames.frame() - 1, 1000000000ull));
            mPool.retire();
        }

        ll_test::SharedGLScope mGL;
        ALFrameFences mFrames;
        // Read a frame on, as the cases time their reads.
        ALQueryPool mPool{ mFrames, 1 };
    };


    // A span's time is read once, and the timer then takes another span.
    TEST_CASE_METHOD(algputimer_data, "algputimer_gl: a span's time is read once, and the next span begins after it", "[algputimer_gl]")
    {
        for (const bool timestamps : span_kinds())
        {
            TimestampFlag flag(timestamps);
            INFO((timestamps ? "timestamps" : "Elapsed queries"));
            ALGPUTimer timer(&mPool);
            U64 ns = 0;
            {
                INFO("nothing is pending before the first span");
                CHECK_FALSE(timer.isPending());
                CHECK_FALSE(timer.poll(ns, true));
            }

            REQUIRE(timer.begin());
            timer.end();
            {
                INFO("the ended span's time is pending");
                CHECK(timer.isPending());
                CHECK(openTimer() == 0);
            }
            {
                INFO("the time is read, and only once");
                CHECK(timer.poll(ns, true));
                CHECK_FALSE(timer.isPending());
                CHECK_FALSE(timer.poll(ns, true));
            }
            {
                INFO("the next span begins and is read in its turn");
                REQUIRE(timer.begin());
                timer.end();
                CHECK(timer.poll(ns, true));
            }
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // The autotune profiles an avatar again before the last profile's time is in. The span that would begin then is
    // refused, its end ends nothing, and another timer query can still begin.
    TEST_CASE_METHOD(algputimer_data, "algputimer_gl: a span cannot begin while the last one's time is pending", "[algputimer_gl]")
    {
        for (const bool timestamps : span_kinds())
        {
            TimestampFlag flag(timestamps);
            INFO((timestamps ? "timestamps" : "Elapsed queries"));
            ALGPUTimer timer(&mPool);
            REQUIRE(timer.begin());
            timer.end();
            REQUIRE(timer.isPending());

            {
                INFO("the second span is refused");
                CHECK_FALSE(timer.begin());
            }
            timer.end();
            {
                INFO("no timer query is left open");
                CHECK(openTimer() == 0);
            }

            // Another avatar's, or the shader profiler's.
            GLuint other = 0;
            glGenQueries(1, &other);
            glBeginQuery(GL_TIME_ELAPSED, other);
            {
                INFO("another timer query begins");
                CHECK(glGetError() == GL_NO_ERROR);
            }
            glEndQuery(GL_TIME_ELAPSED);
            glDeleteQueries(1, &other);

            {
                INFO("the first span's time is still there to read");
                U64 ns = 0;
                CHECK(timer.poll(ns, true));
            }
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A time is read without waiting once its frame has ended and the GPU finished it; one left unread until the pool
    // has dropped its frame's results has expired, and frees the timer.
    TEST_CASE_METHOD(algputimer_data, "algputimer_gl: a time is read once its frame is, and expires unread once its frame's results go", "[algputimer_gl]")
    {
        ALGPUTimer timer(&mPool);
        U64 ns = 0;
        REQUIRE(timer.begin());
        timer.end();
        finishFrame();
        {
            INFO("read without waiting once its frame has been");
            CHECK(timer.poll(ns));
            CHECK_FALSE(timer.isPending());
        }

        REQUIRE(timer.begin());
        timer.end();
        // Each frame times something else, so that each is read and its results kept in turn, until the timer's own
        // frame's make way: its frame and RESULT_FRAMES more.
        for (U32 frame = 0; frame <= ALQueryPool::RESULT_FRAMES; ++frame)
        {
            ALQuerySpan other = mPool.beginSpan();
            mPool.endSpan(other);
            finishFrame();
        }
        {
            INFO("expired once its frame's results were dropped, and the timer free again");
            CHECK_FALSE(timer.poll(ns));
            CHECK_FALSE(timer.isPending());
            CHECK(timer.begin());
            timer.end();
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
}
