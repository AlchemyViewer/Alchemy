/**
 * @file aldriversync_bench.cpp
 * @brief Which GL calls make the application thread wait for a threaded driver's own thread: each call timed alone
 *        and timed with that thread behind by thousands of queued draws, beside glGetError, which always waits, and
 *        glUniform4f, which never should.
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

// A driver that runs GL on a thread of its own (NVIDIA's threaded optimization, Mesa's glthread, Apple's
// multithreaded engine) queues most calls and returns; a call that returns a value, or that the driver must answer
// from its own thread's state, waits until that thread has run everything queued before it. The case queues a
// backlog of draws, cheap to queue and dear to run, then times the call under test: one that waits takes about as
// long as the backlog takes to drain, one that does not returns in microseconds. Without a driver thread the draws
// run as they are issued and nothing is left to wait for, which the first line of the table says.
//
// NVIDIA's driver threads a process by its application profile, which the viewer sets for its own executable
// (llappviewerwin32.cpp): run the binary under the viewer's executable name for the profile to apply.

#include "linden_common.h"

#include "../llgl.h"
#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include "altestbenchmark.h"
#include "llwindow.h"

#include <SDL3/SDL_video.h>

#if LL_WINDOWS
#include "llwin32headers.h"
#include <tlhelp32.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    F64 micros(Clock::time_point from, Clock::time_point to)
    {
        return std::chrono::duration<F64, std::micro>(to - from).count();
    }

    F64 median(std::vector<F64> values)
    {
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }

    // Each thread of the process and the CPU time it has spent, in milliseconds; empty off Windows.
    std::vector<std::pair<U32, F64>> thread_cpu_ms()
    {
        std::vector<std::pair<U32, F64>> threads;
#if LL_WINDOWS
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        THREADENTRY32 entry = {};
        entry.dwSize = sizeof(entry);
        for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
        {
            if (entry.th32OwnerProcessID != GetCurrentProcessId())
            {
                continue;
            }
            const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
            if (!thread)
            {
                continue;
            }
            FILETIME created, exited, kernel, user;
            if (GetThreadTimes(thread, &created, &exited, &kernel, &user))
            {
                const auto ms = [](const FILETIME& time)
                {
                    return F64((U64(time.dwHighDateTime) << 32) | time.dwLowDateTime) / 10000.0;
                };
                threads.emplace_back(entry.th32ThreadID, ms(kernel) + ms(user));
            }
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
#endif
        return threads;
    }

    U32 this_thread_id()
    {
#if LL_WINDOWS
        return GetCurrentThreadId();
#else
        return 0;
#endif
    }

    GLuint compile(GLenum stage, const char* source)
    {
        const GLuint shader = glCreateShader(stage);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        return shader;
    }

    // A call under test: what it needs made first, ahead of any backlog; what it needs laid last, behind it; the call;
    // and what it leaves to undo. Only the call is timed.
    struct Probe
    {
        const char*           name;
        std::function<void()> before;
        std::function<void()> call;
        std::function<void()> after;
        std::function<void()> last = nullptr;
    };

    class DriverSync
    {
    public:
        DriverSync()
        {
            const GLuint vertex = compile(GL_VERTEX_SHADER,
                                          "#version 410 core\n"
                                          "uniform vec4 u;\n"
                                          "void main()\n"
                                          "{\n"
                                          "    vec2 p = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;\n"
                                          "    gl_Position = vec4(p * u.x, 0.0, 1.0);\n"
                                          "}\n");
            const GLuint fragment = compile(GL_FRAGMENT_SHADER,
                                            "#version 410 core\n"
                                            "uniform vec4 u;\n"
                                            "out vec4 colour;\n"
                                            "void main() { colour = u; }\n");
            mProgram = glCreateProgram();
            glAttachShader(mProgram, vertex);
            glAttachShader(mProgram, fragment);
            glLinkProgram(mProgram);
            glDetachShader(mProgram, vertex);
            glDetachShader(mProgram, fragment);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            GLint linked = GL_FALSE;
            glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);
            mUniform = glGetUniformLocation(mProgram, "u");

            const GLuint vertex2 = compile(GL_VERTEX_SHADER,
                                           "#version 410 core\n"
                                           "uniform vec4 u;\n"
                                           "out vec2 uv;\n"
                                           "void main()\n"
                                           "{\n"
                                           "    uv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
                                           "    gl_Position = vec4((uv * 2.0 - 1.0) * u.x, 0.0, 1.0);\n"
                                           "}\n");
            const GLuint fragment2 = compile(GL_FRAGMENT_SHADER,
                                             "#version 410 core\n"
                                             "uniform vec4 u;\n"
                                             "uniform sampler2D t0;\n"
                                             "uniform sampler2D t1;\n"
                                             "uniform sampler2D t2;\n"
                                             "uniform sampler2D t3;\n"
                                             "in vec2 uv;\n"
                                             "out vec4 colour;\n"
                                             "void main() { colour = u * (texture(t0, uv) + texture(t1, uv) + texture(t2, uv) + texture(t3, uv)); }\n");
            mProgram2 = glCreateProgram();
            glAttachShader(mProgram2, vertex2);
            glAttachShader(mProgram2, fragment2);
            glLinkProgram(mProgram2);
            glDetachShader(mProgram2, vertex2);
            glDetachShader(mProgram2, fragment2);
            glDeleteShader(vertex2);
            glDeleteShader(fragment2);
            linked = GL_FALSE;
            glGetProgramiv(mProgram2, GL_LINK_STATUS, &linked);
            REQUIRE(linked == GL_TRUE);
            mUniform2 = glGetUniformLocation(mProgram2, "u");
            glUseProgram(mProgram2);
            for (GLint unit = 0; unit < 4; ++unit)
            {
                const std::string sampler = "t" + std::to_string(unit);
                glUniform1i(glGetUniformLocation(mProgram2, sampler.c_str()), unit);
            }
            glUseProgram(0);
            glGenTextures(SAMPLED, mSampled);
            const U32 texel = 0xff8040c0;
            for (GLuint texture : mSampled)
            {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
                std::vector<U32> texels(16, texel);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            glBindTexture(GL_TEXTURE_2D, 0);

            glGenVertexArrays(1, &mVertexArray);
            glGenTextures(1, &mTarget);
            glBindTexture(GL_TEXTURE_2D, mTarget);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, SIDE, SIDE);
            glGenTextures(1, &mUploadTexture);
            glBindTexture(GL_TEXTURE_2D, mUploadTexture);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, UPLOAD_SIDE, UPLOAD_SIDE);
            glBindTexture(GL_TEXTURE_2D, 0);
            glGenFramebuffers(1, &mFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mTarget, 0);
            REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            mPixels.assign(size_t(UPLOAD_SIDE) * UPLOAD_SIDE * 4, 0x7f);
            glGenBuffers(1, &mBuffer);
            glBindBuffer(GL_COPY_WRITE_BUFFER, mBuffer);
            glBufferData(GL_COPY_WRITE_BUFFER, BUFFER_BYTES, nullptr, GL_DYNAMIC_DRAW);
            glGenBuffers(1, &mUnpack);
            glBindBuffer(GL_COPY_WRITE_BUFFER, mUnpack);
            glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)mPixels.size(), mPixels.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            glGenQueries(1, &mQuery);
            glGenQueries(1, &mStampQuery);
            if (gGLManager.mHasQueryBufferObject && glGetQueryBufferObjectui64v)
            {
                glGenBuffers(1, &mStampStore);
                glBindBuffer(GL_COPY_WRITE_BUFFER, mStampStore);
                glBufferData(GL_COPY_WRITE_BUFFER, sizeof(U64), nullptr, GL_DYNAMIC_READ);
                glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            }
            glFinish();
        }

        ~DriverSync()
        {
            glFinish();
            glDeleteQueries(1, &mQuery);
            glDeleteQueries(1, &mStampQuery);
            glDeleteBuffers(1, &mBuffer);
            glDeleteBuffers(1, &mUnpack);
            if (mStampStore)
            {
                glDeleteBuffers(1, &mStampStore);
            }
            glDeleteFramebuffers(1, &mFramebuffer);
            glDeleteTextures(1, &mTarget);
            glDeleteTextures(1, &mUploadTexture);
            glDeleteVertexArrays(1, &mVertexArray);
            glDeleteProgram(mProgram);
            glDeleteProgram(mProgram2);
            glDeleteTextures(SAMPLED, mSampled);
        }

        // `draws` one-triangle draws into the small target, each switching program and one of four textures and setting
        // a uniform, so the driver validates program and textures afresh every draw: microseconds of the driver
        // thread's time each, a few small calls of the application's where it queues them.
        void backlog(U32 draws)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, mFramebuffer);
            glViewport(0, 0, SIDE, SIDE);
            glBindVertexArray(mVertexArray);
            for (U32 i = 0; i < draws; ++i)
            {
                const bool sampled = (i & 1) != 0;
                glUseProgram(sampled ? mProgram2 : mProgram);
                glActiveTexture(GL_TEXTURE0 + ((i >> 1) & 3));
                glBindTexture(GL_TEXTURE_2D, mSampled[(i >> 3) % SAMPLED]);
                glUniform4f(sampled ? mUniform2 : mUniform, 0.5f + F32(i & 7) * 0.01f, 0.f, 0.f, 1.f);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            for (GLint unit = 3; unit >= 0; --unit)
            {
                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, 0);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // The call's time with nothing queued, and with `draws` queued before it: medians of REPS runs, each from a
        // drained driver.
        std::pair<F64, F64> time(const Probe& probe, U32 draws)
        {
            std::vector<F64> idle;
            std::vector<F64> behind;
            for (U32 rep = 0; rep < REPS; ++rep)
            {
                for (const bool queued : { false, true })
                {
                    glFinish();
                    if (probe.before)
                    {
                        probe.before();
                    }
                    if (queued)
                    {
                        backlog(draws);
                    }
                    if (probe.last)
                    {
                        probe.last();
                    }
                    const Clock::time_point start = Clock::now();
                    probe.call();
                    const Clock::time_point end = Clock::now();
                    (queued ? behind : idle).push_back(micros(start, end));
                    glFinish();
                    if (probe.after)
                    {
                        probe.after();
                    }
                }
            }
            return { median(idle), median(behind) };
        }

        static constexpr GLsizei SIDE         = 16;
        static constexpr GLsizei UPLOAD_SIDE  = 256;
        static constexpr GLsizeiptr BUFFER_BYTES = 64 * 1024;
        static constexpr U32 REPS = 9;

        static constexpr GLsizei SAMPLED = 4;

        ll_test::SharedGLScope mGL;
        GLuint mProgram = 0;
        GLint  mUniform = -1;
        GLuint mProgram2 = 0;
        GLint  mUniform2 = -1;
        GLuint mSampled[SAMPLED] = {};
        GLuint mVertexArray = 0;
        GLuint mTarget = 0;
        GLuint mFramebuffer = 0;
        GLuint mUploadTexture = 0;
        GLuint mBuffer = 0;
        GLuint mUnpack = 0;
        GLuint mQuery = 0;
        GLuint mStampQuery = 0;
        GLuint mStampStore = 0;
        std::vector<U8> mPixels;
    };
}

TEST_CASE_METHOD(DriverSync, "aldriversync_bench: which calls wait for a threaded driver's own thread", "[!benchmark][aldriversync_bench]")
{
    ll_test::skipUnlessOptimised();

    // Frames first, each a burst of draws and a swap, to the window shown: NVIDIA's driver decides to thread a context
    // from how the application drives it across frames, and runs the hidden window's on the application's thread.
    SDL_Window* window = static_cast<SDL_Window*>(ll_test::sharedHeadlessGL().window()->getPlatformWindow());
    SDL_ShowWindow(window);
    for (U32 frame = 0; frame < 300; ++frame)
    {
        backlog(2000);
        ll_test::sharedHeadlessGL().swapBuffer();
    }

    // A backlog sized to keep the driver's thread busy for some milliseconds: what queuing it costs the application,
    // what glFinish then waits for, and which threads spent the CPU time across both. A driver that runs GL on a
    // thread of its own spends it there rather than on this one.
    constexpr U32 DRAWS = 20000;
    glFinish();
    const std::vector<std::pair<U32, F64>> cpu_before = thread_cpu_ms();
    const Clock::time_point queue_start = Clock::now();
    backlog(DRAWS);
    const Clock::time_point queued = Clock::now();
    glFinish();
    const Clock::time_point finished = Clock::now();
    const std::vector<std::pair<U32, F64>> cpu_after = thread_cpu_ms();
    const F64 queue_us = micros(queue_start, queued);
    const F64 finish_us = micros(queued, finished);
    F64 main_ms = 0.0;
    F64 busiest_other_ms = 0.0;
    for (const auto& [thread, after_ms] : cpu_after)
    {
        for (const auto& [before_thread, before_ms] : cpu_before)
        {
            if (before_thread == thread)
            {
                const F64 spent = after_ms - before_ms;
                if (thread == this_thread_id())
                {
                    main_ms = spent;
                }
                else
                {
                    busiest_other_ms = std::max(busiest_other_ms, spent);
                }
            }
        }
    }
    const bool threaded = busiest_other_ms > 0.5 * std::max(main_ms, 1.0);
    std::printf("\n%s\n%u draws queued in %.0f us, glFinish then took %.0f us; CPU time across both: this thread %.1f ms, "
                "the busiest other %.1f ms: %s\n",
                (const char*)glGetString(GL_RENDERER), DRAWS, queue_us, finish_us, main_ms, busiest_other_ms,
                threaded ? "the driver runs GL on a thread of its own" : "the driver runs GL on this thread");

    GLsync fence = nullptr;
    GLsync old_fence = nullptr;
    GLuint name = 0;
    GLint value = 0;
    GLuint64 result = 0;
    const auto delete_name_texture = [&] { glDeleteTextures(1, &name); name = 0; };
    const auto delete_name_buffer = [&] { glDeleteBuffers(1, &name); name = 0; };
    const auto delete_name_query = [&] { glDeleteQueries(1, &name); name = 0; };
    const auto delete_fence = [&] { glDeleteSync(fence); fence = nullptr; };
    const auto delete_old_fence = [&] { glDeleteSync(old_fence); old_fence = nullptr; };
    // A fence laid and a timestamp counted with nothing queued, then given the time to finish.
    const auto lay_old_fence = [&] { old_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); glFlush();
                                     std::this_thread::sleep_for(std::chrono::milliseconds(5)); };
    const auto count_old_query = [&] { glQueryCounter(mQuery, GL_TIMESTAMP); glFlush();
                                       std::this_thread::sleep_for(std::chrono::milliseconds(5)); };

    std::vector<Probe> probes = {
        { "glUniform4f (queued: the control)", nullptr, [&] { glUseProgram(mProgram); glUniform4f(mUniform, 1.f, 0.f, 0.f, 1.f); }, nullptr },
        { "glFlush", nullptr, [&] { glFlush(); }, nullptr },
        { "glGetError", nullptr, [&] { glGetError(); }, nullptr },
        { "glGetIntegerv(GL_CURRENT_PROGRAM)", nullptr, [&] { glGetIntegerv(GL_CURRENT_PROGRAM, &value); }, nullptr },
        { "glIsEnabled(GL_BLEND)", nullptr, [&] { value = glIsEnabled(GL_BLEND); }, nullptr },
        { "glCheckFramebufferStatus", nullptr, [&] { value = (GLint)glCheckFramebufferStatus(GL_FRAMEBUFFER); }, nullptr },
        { "glFenceSync (lay)", nullptr, [&] { fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); }, delete_fence },
        { "glClientWaitSync(0, 0), fence laid behind", nullptr,
          [&] { value = (GLint)glClientWaitSync(fence, 0, 0); }, delete_fence,
          [&] { fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); } },
        { "glClientWaitSync(0, 0), fence signalled before", lay_old_fence,
          [&] { value = (GLint)glClientWaitSync(old_fence, 0, 0); }, delete_old_fence },
        { "glClientWaitSync(FLUSH, 0), signalled before", lay_old_fence,
          [&] { value = (GLint)glClientWaitSync(old_fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0); }, delete_old_fence },
        { "glGetSynciv(STATUS), signalled before", lay_old_fence,
          [&] { glGetSynciv(old_fence, GL_SYNC_STATUS, 1, nullptr, &value); }, delete_old_fence },
        { "glGetQueryObjectui64v(AVAILABLE), done before", count_old_query,
          [&] { glGetQueryObjectui64v(mQuery, GL_QUERY_RESULT_AVAILABLE, &result); }, nullptr },
        { "glGetQueryObjectui64v(RESULT), done before", count_old_query,
          [&] { glGetQueryObjectui64v(mQuery, GL_QUERY_RESULT, &result); }, nullptr },
        { "glGenTextures", nullptr, [&] { glGenTextures(1, &name); }, delete_name_texture },
        { "glGenBuffers", nullptr, [&] { glGenBuffers(1, &name); }, delete_name_buffer },
        { "glGenQueries", nullptr, [&] { glGenQueries(1, &name); }, delete_name_query },
        { "glDeleteTextures (an unused texture)", [&] { glGenTextures(1, &name); glBindTexture(GL_TEXTURE_2D, name);
                                                        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4);
                                                        glBindTexture(GL_TEXTURE_2D, 0); },
          [&] { glDeleteTextures(1, &name); name = 0; }, nullptr },
        { "glMapBufferRange(UNSYNCHRONIZED) + unmap", nullptr,
          [&] {
              glBindBuffer(GL_COPY_WRITE_BUFFER, mBuffer);
              void* data = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, 4096,
                                            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
              if (data)
              {
                  memset(data, 1, 4096);
              }
              glUnmapBuffer(GL_COPY_WRITE_BUFFER);
              glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
          },
          nullptr },
        { "glBufferSubData 4 KB from client memory", nullptr,
          [&] {
              glBindBuffer(GL_COPY_WRITE_BUFFER, mBuffer);
              glBufferSubData(GL_COPY_WRITE_BUFFER, 0, 4096, mPixels.data());
              glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
          },
          nullptr },
        { "glTexSubImage2D 256 KB from client memory", nullptr,
          [&] {
              glBindTexture(GL_TEXTURE_2D, mUploadTexture);
              glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UPLOAD_SIDE, UPLOAD_SIDE, GL_RGBA, GL_UNSIGNED_BYTE, mPixels.data());
              glBindTexture(GL_TEXTURE_2D, 0);
          },
          nullptr },
        { "glTexSubImage2D 256 KB from an unpack buffer", nullptr,
          [&] {
              glBindBuffer(GL_PIXEL_UNPACK_BUFFER, mUnpack);
              glBindTexture(GL_TEXTURE_2D, mUploadTexture);
              glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UPLOAD_SIDE, UPLOAD_SIDE, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
              glBindTexture(GL_TEXTURE_2D, 0);
              glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
          },
          nullptr },
    };
    if (mStampStore)
    {
        probes.push_back({ "glQueryCounter + query buffer copy (WAIT)", nullptr,
                           [&] {
                               glQueryCounter(mStampQuery, GL_TIMESTAMP);
                               glGetQueryBufferObjectui64v(mStampQuery, mStampStore, GL_QUERY_RESULT, 0);
                           },
                           nullptr });
        probes.push_back({ "glQueryCounter + query buffer copy (NO_WAIT)", nullptr,
                           [&] {
                               glQueryCounter(mStampQuery, GL_TIMESTAMP);
                               glGetQueryBufferObjectui64v(mStampQuery, mStampStore, GL_QUERY_RESULT_NO_WAIT, 0);
                           },
                           nullptr });
    }
    if (glGetGraphicsResetStatus)
    {
        probes.push_back({ "glGetGraphicsResetStatus", nullptr, [&] { value = (GLint)glGetGraphicsResetStatus(); }, nullptr });
    }

    std::printf("%-48s %12s %14s  %s\n", "call", "alone (us)", "behind (us)", "waits for the driver's thread?");
    for (const Probe& probe : probes)
    {
        const auto [alone_us, behind_us] = time(probe, DRAWS);
        std::printf("%-48s %12.1f %14.1f  %s\n", probe.name, alone_us, behind_us,
                    behind_us > alone_us + std::max(200.0, 0.1 * finish_us) ? "yes" : "no");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    SDL_HideWindow(window);
    SUCCEED();
}
