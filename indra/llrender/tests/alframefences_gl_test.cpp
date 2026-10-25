/**
 * @file alframefences_gl_test.cpp
 * @brief ALFrameFences on the hidden window: a frame complete only once a later frame's end has seen it finished, one
 *        poll a frame and none of the frame just ended, waits on a pending frame and on the open one, the ring of
 *        fences kept whole, release, the pacing wait for the frame a given number before each frame's end, and frames
 *        stamped by the GPU in place of fences, with no call.
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

#include <algorithm>
#include <chrono>
#include <cmath>

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

    // A triangle over the whole viewport whose every fragment runs a loop as long as `iterations` says.
    const char* kBusyVertex =
        "#version 400\n"
        "void main()\n"
        "{\n"
        "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;\n"
        "    gl_Position = vec4(p, 0.0, 1.0);\n"
        "}\n";

    const char* kBusyFragment =
        "#version 400\n"
        "uniform int iterations;\n"
        "out vec4 frag_color;\n"
        "void main()\n"
        "{\n"
        "    float x = fract(gl_FragCoord.x * 0.0137 + gl_FragCoord.y * 0.0071);\n"
        "    for (int i = 0; i < iterations; ++i)\n"
        "    {\n"
        "        x = fract(sin(x * 12.9898 + 78.233) * 43758.5453);\n"
        "    }\n"
        "    frag_color = vec4(x, 0.0, 0.0, 1.0);\n"
        "}\n";

    // Frames whose commands the GPU takes a while over, for the pacing wait: it is seen to wait for a frame only where
    // the GPU is still drawing that frame when the wait begins. The work is draws of a loop over a small target, each
    // calibrated to take about DRAW_SECONDS, far short of a driver's watchdog, as many as a span asks.
    struct alframefences_paced_data : alframefences_data
    {
        static constexpr S32 SIZE = 256;
        static constexpr F64 DRAW_SECONDS = 0.01;
        // What one frame's work takes the GPU at the least: the cases' checks come within a millisecond of it, and the
        // pacing wait gives up after a second, which leaves room for other processes' work on the GPU.
        static constexpr F64 BUSY_SECONDS = 0.08;
        static constexpr S32 MAX_ITERATIONS = 1 << 24;

        ~alframefences_paced_data()
        {
            glFinish();
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

        void setUp()
        {
            const GLuint vs = ll_test::compileTestShader(GL_VERTEX_SHADER, kBusyVertex);
            const GLuint fs = ll_test::compileTestShader(GL_FRAGMENT_SHADER, kBusyFragment);
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
            mIterationsLocation = glGetUniformLocation(mProgram, "iterations");
            REQUIRE(mIterationsLocation >= 0);

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

            // The loop's own time, as what doubling it adds: a draw's fixed cost, a wait for the GPU behind another
            // process's work among it, is the same at any length. Doubled until that is long enough to time, then
            // scaled to the draw's length. The first draw, which may finish building the program, is not timed.
            timeDraw(1);
            S32 iterations = 256;
            F64 shorter = timeDraw(iterations);
            F64 added = 0.0;
            while (iterations < MAX_ITERATIONS / 2)
            {
                const F64 longer = timeDraw(iterations * 2);
                added = longer - shorter;
                if (added >= 0.005)
                {
                    break;
                }
                iterations *= 2;
                shorter = longer;
            }
            {
                INFO("the GPU can be kept busy: doubling " << iterations << " iterations adds " << added << " s");
                REQUIRE(added >= 0.005);
            }
            mIterations = (S32)std::clamp((F64)iterations * DRAW_SECONDS / added, 1.0, (F64)MAX_ITERATIONS);
            mDrawSeconds = added * mIterations / iterations;

            // The frames before the case's are finished and shown so.
            glFinish();
            mFrames.endFrame();
            glFinish();
            REQUIRE(mFrames.wait(mFrames.frame() - 1, SECOND));
            while (glGetError() != GL_NO_ERROR)
            {
            }
        }

        // Work the GPU takes `seconds` over at the least, as calibrated, issued in the open frame.
        void keepBusy(F64 seconds = BUSY_SECONDS)
        {
            const S32 draws = std::max(1, (S32)std::ceil(seconds / mDrawSeconds));
            for (S32 i = 0; i < draws; ++i)
            {
                draw(mIterations);
            }
        }

        void draw(S32 iterations)
        {
            glUseProgram(mProgram);
            glUniform1i(mIterationsLocation, iterations);
            glBindVertexArray(mVAO);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);
        }

        // One draw of `iterations` to its end on the GPU, the least of three: other processes' work only adds to it.
        F64 timeDraw(S32 iterations)
        {
            F64 least = 0.0;
            for (S32 i = 0; i < 3; ++i)
            {
                glFinish();
                const auto start = std::chrono::steady_clock::now();
                draw(iterations);
                glFinish();
                const F64 seconds = std::chrono::duration<F64>(std::chrono::steady_clock::now() - start).count();
                least = i == 0 ? seconds : std::min(least, seconds);
            }
            return least;
        }

        GLuint mProgram = 0;
        GLint  mIterationsLocation = -1;
        GLuint mVAO = 0;
        GLuint mColor = 0;
        GLuint mFBO = 0;
        S32    mIterations = 1;
        F64    mDrawSeconds = DRAW_SECONDS;
    };


    // The open frame is never complete, however long the GPU has had, and the frame a frame's end ends is not either:
    // the next frame's end shows it finished.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: a frame is complete once a later frame's end has seen it finished", "[alframefences_gl]")
    {
        const U32 frame = mFrames.frame();
        {
            INFO("frame 0 is complete, and the first frame is not");
            CHECK(mFrames.isComplete(0));
            CHECK_FALSE(mFrames.isComplete(frame));
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("the next frame opens");
            CHECK(mFrames.frame() == frame + 1);
        }
        glFinish();
        {
            INFO("not at its own end, however long the GPU has had");
            CHECK_FALSE(mFrames.isComplete(frame));
        }
        mFrames.endFrame();
        {
            INFO("complete once the next frame's end has seen it finished");
            CHECK(mFrames.isComplete(frame));
            CHECK_FALSE(mFrames.isComplete(frame + 1));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A frame's end makes one fence and polls only the frames ended before it, never the one it has just ended.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: a frame's end polls the frames before it, and lays one fence", "[alframefences_gl]")
    {
        const U32 frame = mFrames.frame();
        const U32 before = mFrames.getCalls();
        mFrames.endFrame();
        {
            INFO("the first frame's end lays its fence, and has nothing to poll");
            CHECK(mFrames.getCalls() == before + 1);
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("the next polls the frame before, which has finished, and lays its own fence");
            CHECK(mFrames.getCalls() == before + 3);
            CHECK(mFrames.isComplete(frame));
            CHECK_FALSE(mFrames.isComplete(frame + 1));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A frame's end polls the ended frames oldest first, as far as the GPU has finished them.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: a frame's end shows every ended frame the GPU has finished", "[alframefences_gl]")
    {
        const U32 first = mFrames.frame();
        for (U32 i = 0; i < 3; ++i)
        {
            mFrames.endFrame();
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("the three before it, and not itself");
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

    // Frames ended faster than the GPU finishes them fill the ring of fences; the frame's end then waits out the oldest
    // rather than lay a fence over it.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: frames ended back to back keep the ring of fences whole", "[alframefences_gl]")
    {
        const U32 first = mFrames.frame();
        for (U32 i = 0; i < ALFrameFences::FRAMES * 2; ++i)
        {
            mFrames.endFrame();
        }
        {
            INFO("each frame FRAMES behind the open one was polled or waited out to make room");
            CHECK(mFrames.isComplete(first + ALFrameFences::FRAMES - 1));
            CHECK(mFrames.isComplete(mFrames.frame() - 1 - ALFrameFences::FRAMES));
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("and the rest polled by the next frame's end");
            CHECK(mFrames.isComplete(mFrames.frame() - 2));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // release() deletes the fences and takes every frame so far as finished, the open one too; frames end after it.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: release completes every frame so far", "[alframefences_gl]")
    {
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
        mFrames.endFrame();
        {
            INFO("frames end again after it");
            CHECK(mFrames.isComplete(next));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A frame's end paced one frame ahead waits, every frame, for the frame before it, which the GPU is still drawing:
    // a pacing wait, not a stall. The frame it ends stays open to the GPU.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: a paced frame's end waits every frame for the frame that many before it", "[alframefences_gl]")
    {
        setUp();
        const U32 stalls = mFrames.getStalls();
        const U32 waits  = mFrames.getPacingWaits();
        const U32 calls  = mFrames.getCalls();
        for (U32 i = 0; i < 3; ++i)
        {
            const U32 busy = mFrames.frame();
            keepBusy();
            mFrames.endFrame(1);
            INFO("frame " << i << " of three, each the GPU's for a while");
            CHECK(mFrames.isComplete(busy - 1));
            CHECK_FALSE(mFrames.isComplete(busy));
        }
        {
            INFO("the second and third frames' ends waited for the GPU, and neither stalled");
            CHECK(mFrames.getPacingWaits() == waits + 2);
            CHECK(mFrames.getStalls() == stalls);
        }
        {
            INFO("a fence each, and one wait each for the frame before the second and third, with no poll before it");
            CHECK(mFrames.getCalls() == calls + 5);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Paced two frames ahead, a frame's end waits for the frame two before it and no later one: the frame between, which
    // the GPU is still drawing, is left to it.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: a paced frame's end waits for no frame after that one", "[alframefences_gl]")
    {
        setUp();
        const U32 before = mFrames.frame();
        mFrames.endFrame(2);
        const U32 busy = mFrames.frame();
        keepBusy();
        mFrames.endFrame(2);
        mFrames.endFrame(2);
        {
            INFO("the frame two before the last frame's end, and not the busy one after it");
            CHECK(mFrames.isComplete(before));
            CHECK_FALSE(mFrames.isComplete(busy));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Unpaced, a frame's end waits for no frame the GPU is still drawing, as before pacing was asked for.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: an unpaced frame's end waits for no ended frame", "[alframefences_gl]")
    {
        setUp();
        const U32 waits = mFrames.getPacingWaits();
        const U32 busy  = mFrames.frame();
        keepBusy();
        mFrames.endFrame();
        mFrames.endFrame();
        {
            INFO("the busy frame is left to the GPU, and nothing waited");
            CHECK_FALSE(mFrames.isComplete(busy));
            CHECK(mFrames.getPacingWaits() == waits);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // The frames' ends note their pacing waits in the slack: frames the GPU is behind on give work a budget past the
    // reserve. A wait that finds the GPU done after work shows none, and a frame's end that paces nothing, or a release,
    // takes it away. The budget is not held to the busy frames' calibrated length: calibrated on an idle GPU, the draws
    // can run faster once its clocks rise.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: paced frames the GPU is behind on give work a budget", "[alframefences_gl]")
    {
        setUp();
        ALPacingSlack& slack = mFrames.slack();
        // The first frame's end finds the frame before it finished, and shows no slack: FRAMES more wait for the GPU.
        const auto behind = [this]
        {
            for (U32 i = 0; i <= ALPacingSlack::FRAMES; ++i)
            {
                keepBusy(BUSY_SECONDS / 4.0);
                mFrames.endFrame(1);
            }
        };
        behind();
        {
            INFO("the budget is " << slack.budget() << " s");
            CHECK(slack.budget() > slack.reserve());
        }
        {
            // Work as long as the budget allows, on a clock of its own, then a wait for a frame the GPU has finished.
            F64 clock = 0.0;
            const F64 worked = slack.fill(
                [&clock]
                {
                    clock += 0.0001;
                    return true;
                },
                [&clock] { return clock; });
            REQUIRE(worked > 0.0);
            glFinish();
            mFrames.endFrame(1);
            INFO("a wait that found the GPU done shows no slack, whatever work came before it");
            CHECK(slack.budget() == 0.0);
        }
        behind();
        REQUIRE(slack.budget() > 0.0);
        mFrames.endFrame();
        {
            INFO("an unpaced frame's end forgets it");
            CHECK(slack.budget() == 0.0);
        }
        behind();
        REQUIRE(slack.budget() > 0.0);
        mFrames.release();
        {
            INFO("and so does a release");
            CHECK(slack.budget() == 0.0);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A frame the GPU spends more than a second on is paced too: the frame's end waits it out, in tries of a second,
    // rather than give up on it after the first.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: a paced frame's end waits out a frame longer than a second", "[alframefences_gl]")
    {
        setUp();
        const U32 slow = mFrames.frame();
        keepBusy(3.0);
        // A fence of the test's own behind the work, to ask the GPU itself whether the wait outlasted it.
        GLsync witness = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        mFrames.endFrame(1);
        const U32 waits = mFrames.getPacingWaits();
        mFrames.endFrame(1);
        const GLenum status = glClientWaitSync(witness, 0, 0);
        glDeleteSync(witness);
        {
            INFO("waited out, past the first try");
            CHECK((status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED));
            CHECK(mFrames.isComplete(slow));
            CHECK(mFrames.getPacingWaits() == waits + 1);
        }
        glFinish();
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // waitFinished() waits a frame out however long the GPU takes over it, and says whether it had to wait.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: waitFinished waits out a frame the GPU is still drawing", "[alframefences_gl]")
    {
        setUp();
        const U32 busy = mFrames.frame();
        keepBusy();
        mFrames.endFrame();
        const U32 stalls = mFrames.getStalls();
        bool stalled = false;
        mFrames.waitFinished(busy, &stalled);
        {
            INFO("waited out, the wait counted as a stall");
            CHECK(mFrames.isComplete(busy));
            CHECK(stalled);
            CHECK(mFrames.getStalls() == stalls + 1);
        }
        {
            INFO("a frame already finished is not waited for again");
            stalled = true;
            mFrames.waitFinished(busy, &stalled);
            CHECK_FALSE(stalled);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Stamped, a frame's end lays no fence and polls none: it asks the GPU to stamp the frame, and reads the stamps of
    // the frames before. A frame is complete once a later frame's end has read its stamp, as a fenced one is once a
    // later end has seen its fence signalled.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: a stamped frame is complete once a later frame's end has read its stamp, with no call", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp())
        {
            SKIP("no query buffers, timestamps or persistent maps on this context");
        }
        mFrames.setStamps(true);
        const U32 frame = mFrames.frame();
        const U32 calls = mFrames.getCalls();
        mFrames.endFrame();
        {
            INFO("the first stamped frame's end makes the stamps' store");
            REQUIRE(mFrames.isStamping());
        }
        glFinish();
        {
            INFO("not at its own end, however long the GPU has had");
            CHECK_FALSE(mFrames.isComplete(frame));
        }
        mFrames.endFrame();
        {
            INFO("complete once the next frame's end has read its stamp");
            CHECK(mFrames.isComplete(frame));
            CHECK_FALSE(mFrames.isComplete(frame + 1));
        }
        {
            INFO("neither end laid a fence or polled one");
            CHECK(mFrames.getCalls() == calls);
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Stamped frames ended faster than the GPU finishes them are waited out to make room, as fenced ones are. A wait for
    // an ended stamped frame reads its stamp, with no call; one for the open frame lays a fence of its own.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: stamped frames keep the ring whole, and are waited on by their stamps", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp())
        {
            SKIP("no query buffers, timestamps or persistent maps on this context");
        }
        mFrames.setStamps(true);
        const U32 first = mFrames.frame();
        for (U32 i = 0; i < ALFrameFences::FRAMES * 2; ++i)
        {
            mFrames.endFrame();
        }
        {
            INFO("each frame FRAMES behind the open one was read or waited out to make room");
            CHECK(mFrames.isComplete(first + ALFrameFences::FRAMES - 1));
            CHECK(mFrames.isComplete(mFrames.frame() - 1 - ALFrameFences::FRAMES));
        }
        const U32 ended = mFrames.frame() - 1;
        const U32 calls = mFrames.getCalls();
        {
            INFO("an ended frame, by its stamp");
            CHECK(mFrames.wait(ended, SECOND));
            CHECK(mFrames.isComplete(ended));
            CHECK(mFrames.getCalls() == calls);
        }
        mFrames.endFrame();
        const U32 open = mFrames.frame();
        {
            INFO("the open frame: a fence of its own, and every frame before it finished");
            CHECK(mFrames.wait(open, SECOND));
            CHECK(mFrames.getCalls() >= calls + 2);
            CHECK(mFrames.isComplete(open - 1));
            CHECK_FALSE(mFrames.isComplete(open));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // No longer asked for, the stamps go at the next frame's end, which waits out the frames stamped and lays a fence;
    // asked for again, they come again. release() lets the store go, and the next frame's end makes it again.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: stamps go back to fences and come again", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp())
        {
            SKIP("no query buffers, timestamps or persistent maps on this context");
        }
        mFrames.setStamps(true);
        mFrames.endFrame();
        const U32 stamped = mFrames.frame();
        mFrames.endFrame();
        REQUIRE(mFrames.isStamping());

        mFrames.setStamps(false);
        const U32 fenced = mFrames.frame();
        const U32 calls  = mFrames.getCalls();
        mFrames.endFrame();
        {
            INFO("the stamped frames waited out, and a fence laid");
            CHECK_FALSE(mFrames.isStamping());
            CHECK(mFrames.isComplete(stamped));
            CHECK(mFrames.getCalls() > calls);
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("the fenced frame complete once the next frame's end has polled its fence");
            CHECK(mFrames.isComplete(fenced));
        }

        mFrames.setStamps(true);
        mFrames.endFrame();
        CHECK(mFrames.isStamping());
        mFrames.release();
        {
            INFO("release lets the store go");
            CHECK_FALSE(mFrames.isStamping());
        }
        const U32 next = mFrames.frame();
        mFrames.endFrame();
        {
            INFO("the next frame's end makes it again");
            CHECK(mFrames.isStamping());
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("and frames are read complete by their stamps after it");
            CHECK(mFrames.isComplete(next));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Stamps asked for in the middle of a frame whose commands were told its end lays a fence, and laid none of their
    // own, start with the next frame: that frame's end lays the fence they count on, as an upload staged before the
    // viewer latches the setting does.
    TEST_CASE_METHOD(alframefences_data, "alframefences_gl: stamps asked for after a command counted on the frame's fence start with the next frame", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp())
        {
            SKIP("no query buffers, timestamps or persistent maps on this context");
        }
        mFrames.setStamps(false);
        mFrames.endFrame();
        REQUIRE_FALSE(mFrames.isStamping());

        // A command told the frame's end lays a fence, then the stamps asked for.
        CHECK_FALSE(mFrames.willStamp());
        mFrames.setStamps(true);
        const U32 counted_on = mFrames.frame();
        mFrames.endFrame();
        {
            INFO("that frame's end lays a fence");
            CHECK_FALSE(mFrames.isStamping());
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("its fence shows it finished");
            CHECK(mFrames.isComplete(counted_on));
            INFO("and the next frame's end stamps");
            CHECK(mFrames.isStamping());
        }
        mFrames.setStamps(false);
        mFrames.endFrame();
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A frame whose stamp the GPU has yet to write is not complete, however many frames' ends have read its slot since:
    // the GPU's write completes a stamped frame, not the frames' ends. Where the GPU writes the results behind the
    // frame; a driver that writes one as it is asked for waits for the frame there instead.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: a frame whose stamp the GPU has yet to write is not complete", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp() || !gGLManager.mWritesQueryResults)
        {
            SKIP("the GPU does not write the stamps behind the frames here");
        }
        setUp();
        mFrames.setStamps(true);
        mFrames.endFrame();
        REQUIRE(mFrames.isStamping());
        glFinish();
        mFrames.endFrame();

        const U32 busy = mFrames.frame();
        keepBusy();
        // The busy frame stamped, then its slot read twice while the GPU is still drawing it.
        mFrames.endFrame();
        mFrames.endFrame();
        mFrames.endFrame();
        {
            INFO("its stamp not yet written, the frame is not complete");
            CHECK_FALSE(mFrames.isComplete(busy));
        }
        glFinish();
        mFrames.endFrame();
        {
            INFO("once written, it is");
            CHECK(mFrames.isComplete(busy));
        }
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // A stamp that has not come in a second, the GPU still drawing its frame, is waited out on a fence laid behind it,
    // in tries of a second as a fenced frame is, rather than by glFinish: a fence and a wait at the least, where
    // glFinish was one call. The stamp has come once the fence has signalled, and the frames' ends go on stamping.
    // Where the GPU writes the stamps behind the frames.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: a stamp late by a second is waited out on a fence and stamping goes on", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp() || !gGLManager.mWritesQueryResults)
        {
            SKIP("the GPU does not write the stamps behind the frames here");
        }
        setUp();
        mFrames.setStamps(true);
        mFrames.endFrame();
        REQUIRE(mFrames.isStamping());
        glFinish();
        mFrames.endFrame();

        const U32 busy = mFrames.frame();
        // Well past the second a stamp is waited for, however the calibration rounds.
        keepBusy(2.5);
        mFrames.endFrame(1);
        const U32 calls = mFrames.getCalls();
        // Paced on the busy frame, whose stamp comes past the second.
        mFrames.endFrame(1);
        {
            INFO("the busy frame complete, by a fence of its own and its waits");
            CHECK(mFrames.isComplete(busy));
            CHECK(mFrames.getCalls() >= calls + 2);
        }
        {
            INFO("its stamp came: the frames' ends go on stamping");
            CHECK(mFrames.isStamping());
        }
        glFinish();
        CHECK(glGetError() == GL_NO_ERROR);
    }

    // Stamped and paced one frame ahead, a frame's end leaves the frame before it complete and its own open, whether the
    // GPU was still drawing the frame before or had finished it, and lays no fence or polls one on the way.
    TEST_CASE_METHOD(alframefences_paced_data, "alframefences_gl: a paced stamped frame's end waits for the frame before it, with no call", "[alframefences_gl]")
    {
        if (!ALFrameFences::canStamp())
        {
            SKIP("no query buffers, timestamps or persistent maps on this context");
        }
        setUp();
        mFrames.setStamps(true);
        const U32 stalls = mFrames.getStalls();
        const U32 calls  = mFrames.getCalls();
        for (U32 i = 0; i < 3; ++i)
        {
            const U32 busy = mFrames.frame();
            keepBusy();
            mFrames.endFrame(1);
            INFO("frame " << i << " of three, each the GPU's for a while");
            CHECK(mFrames.isComplete(busy - 1));
            CHECK_FALSE(mFrames.isComplete(busy));
        }
        {
            INFO("no stall, and no call");
            CHECK(mFrames.getStalls() == stalls);
            CHECK(mFrames.getCalls() == calls);
        }
        glFinish();
        CHECK(glGetError() == GL_NO_ERROR);
    }
}
