/**
 * @file alframefences_gl_test.cpp
 * @brief ALFrameFences on the hidden window: a frame complete only once ended and finished, the polls where glthread
 *        has caught up or where the user asks, waits on a pending frame and on the open one, the ring of fences
 *        kept whole without a poll, and release.
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
#include "../llgl.h"
#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    constexpr U64 SECOND = 1000000000ull;

    struct alframefences_data
    {
        alframefences_data()
        {
            // Errors from before the case are not the case's.
            while (glGetError() != GL_NO_ERROR)
            {
            }
        }

        ~alframefences_data() { mFrames.release(); }

        ll_test::SharedGLScope mGL;
        ALFrameFences mFrames;
    };


    // The open frame is never complete, however long the GPU has had; an ended one is once a poll sees it finished.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: a frame is complete once it has ended and the GPU finished it", "[alframefences_gl]")
    {
        ll_test::MesaGLThreadFlag no_glthread(false);
        const U32 frame = mFrames.frame();
        {
            INFO("frame 0 is complete, and the first frame is not");
            CHECK(mFrames.isComplete(0));
            CHECK_FALSE(mFrames.isComplete(frame));
        }
        glFinish();
        mFrames.poll();
        {
            INFO("not while it is open");
            CHECK_FALSE(mFrames.isComplete(frame));
        }
        mFrames.endFrame();
        {
            INFO("the next frame opens");
            CHECK(mFrames.frame() == frame + 1);
        }
        glFinish();
        mFrames.poll();
        {
            INFO("complete once it has ended, the GPU has finished it and a poll has seen it");
            CHECK(mFrames.isComplete(frame));
            CHECK_FALSE(mFrames.isComplete(frame + 1));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Under glthread poll() makes no call, and the frame's end polls in its place; elsewhere poll() polls and the
    // frame's end only lays its fence.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: under glthread the frame's end polls, and poll() makes no call", "[alframefences_gl]")
    {
        {
            ll_test::MesaGLThreadFlag glthread(true);
            const U32 frame = mFrames.frame();
            const U32 before = mFrames.getCalls();
            mFrames.endFrame();
            {
                INFO("its fence laid, and polled");
                CHECK(mFrames.getCalls() >= before + 2);
            }
            glFinish();
            const U32 ended = mFrames.getCalls();
            mFrames.poll();
            {
                INFO("poll() makes no call");
                CHECK(mFrames.getCalls() == ended);
            }
            mFrames.endFrame();
            {
                INFO("the next frame's end polls, and sees the frame finished");
                CHECK(mFrames.isComplete(frame));
            }
        }
        {
            ll_test::MesaGLThreadFlag no_glthread(false);
            // Every frame so far finished and seen, so that the next poll has one frame to look at.
            glFinish();
            mFrames.poll();
            const U32 frame = mFrames.frame();
            const U32 before = mFrames.getCalls();
            mFrames.endFrame();
            {
                INFO("its fence laid, and no poll");
                CHECK(mFrames.getCalls() == before + 1);
                CHECK_FALSE(mFrames.isComplete(frame));
            }
            glFinish();
            mFrames.poll();
            {
                INFO("poll() polls, and sees the frame finished");
                CHECK(mFrames.getCalls() == before + 2);
                CHECK(mFrames.isComplete(frame));
            }
            const U32 polled = mFrames.getCalls();
            mFrames.poll();
            {
                INFO("with nothing pending it makes no call");
                CHECK(mFrames.getCalls() == polled);
            }
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A poll walks the ended frames oldest first, as far as the GPU has finished them.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: a poll shows every ended frame the GPU has finished", "[alframefences_gl]")
    {
        ll_test::MesaGLThreadFlag no_glthread(false);
        const U32 first = mFrames.frame();
        for (U32 i = 0; i < 3; ++i)
        {
            mFrames.endFrame();
        }
        glFinish();
        mFrames.poll();
        {
            INFO("all three");
            CHECK(mFrames.isComplete(first));
            CHECK(mFrames.isComplete(first + 2));
            CHECK_FALSE(mFrames.isComplete(first + 3));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A wait for an ended frame the GPU has finished stalls on nothing; one for the open frame lays a fence of its own,
    // and shows every frame before it finished, the open one staying open.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: waits stall only on a frame the GPU has not finished", "[alframefences_gl]")
    {
        ll_test::MesaGLThreadFlag no_glthread(false);
        const U32 ended = mFrames.frame();
        mFrames.endFrame();
        glFinish();
        const U32 stalls = mFrames.getStalls();
        bool stalled = true;
        {
            INFO("an ended frame the GPU has finished");
            CHECK(mFrames.wait(ended, SECOND, &stalled));
            CHECK_FALSE(stalled);
            CHECK(mFrames.getStalls() == stalls);
            CHECK(mFrames.isComplete(ended));
        }
        {
            INFO("a complete frame again, with no call");
            const U32 calls = mFrames.getCalls();
            CHECK(mFrames.wait(ended, SECOND, &stalled));
            CHECK(mFrames.getCalls() == calls);
        }

        const U32 pending = mFrames.frame();
        mFrames.endFrame();
        const U32 open = mFrames.frame();
        const U32 calls = mFrames.getCalls();
        {
            INFO("the open frame: a fence of its own, and every frame before it finished");
            CHECK(mFrames.wait(open, SECOND, &stalled));
            CHECK(mFrames.getCalls() >= calls + 2);
            CHECK(mFrames.isComplete(pending));
            CHECK_FALSE(mFrames.isComplete(open));
            CHECK(mFrames.frame() == open);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Frames ended with no poll between them fill the ring of fences; the frame's end then waits out the oldest
    // rather than lay a fence over it.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: frames ended without a poll keep the ring of fences whole", "[alframefences_gl]")
    {
        ll_test::MesaGLThreadFlag no_glthread(false);
        const U32 first = mFrames.frame();
        for (U32 i = 0; i < ALFrameFences::FRAMES * 2; ++i)
        {
            mFrames.endFrame();
        }
        {
            INFO("each frame FRAMES behind the open one was waited out to make room");
            CHECK(mFrames.isComplete(first + ALFrameFences::FRAMES - 1));
            CHECK(mFrames.isComplete(mFrames.frame() - 1 - ALFrameFences::FRAMES));
        }
        glFinish();
        mFrames.poll();
        {
            INFO("and the rest are polled from where that left them");
            CHECK(mFrames.isComplete(mFrames.frame() - 1));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // release() deletes the fences and takes every frame so far as finished, the open one too; frames end after it.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: release completes every frame so far", "[alframefences_gl]")
    {
        ll_test::MesaGLThreadFlag no_glthread(false);
        const U32 ended = mFrames.frame();
        mFrames.endFrame();
        const U32 open = mFrames.frame();
        mFrames.release();
        {
            INFO("the pending frame and the open one");
            CHECK(mFrames.isComplete(ended));
            CHECK(mFrames.isComplete(open));
            CHECK_FALSE(mFrames.isComplete(mFrames.frame()));
        }
        const U32 next = mFrames.frame();
        mFrames.endFrame();
        glFinish();
        mFrames.poll();
        {
            INFO("frames end again after it");
            CHECK(mFrames.isComplete(next));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }
}
