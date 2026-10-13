/**
 * @file alwindowsdlheadless_test.cpp
 * @brief The hidden window backend: what it hands out, and a worker
 *        thread's context sharing the main one's objects.
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

#include "../llglheaders.h"

#include "llheadlessgl_fixture.h"

#include "llwindow.h"

#if LL_DARWIN
#include <OpenGL/OpenGL.h>
#elif LL_LINUX
#include "SDL3/SDL_video.h"
#endif

#include <catch2/catch_test_macros.hpp>

#include <thread>

namespace
{
    // Whether a GL context is current on the calling thread, as the platform
    // reports it: a worker's shared context is made outside SDL, so SDL's own
    // record of the current context never names it.
    bool contextCurrentHere()
    {
#if LL_WINDOWS
        return wglGetCurrentContext() != nullptr;
#elif LL_DARWIN
        return CGLGetCurrentContext() != nullptr;
#else
        // EGL, reached through SDL as the shared contexts themselves are.
        using GetCurrentContext = void* (*)();
        const auto get_current = reinterpret_cast<GetCurrentContext>(SDL_EGL_GetProcAddress("eglGetCurrentContext"));
        return get_current && get_current() != nullptr;
#endif
    }

    struct alwindowsdlheadless_data
    {
        // The window is the shared context's, which has the image, render
        // and shader classes up on it: the tests look only at the window
        // and its shared contexts, and leave the context's bindings as
        // they found them.
        static ll_test::HeadlessGL& gl()
        {
            return ll_test::sharedHeadlessGL();
        }

        alwindowsdlheadless_data() { gl(); }
    };


    // The window the manager handed the fixture is the hidden backend, valid,
    // with an SDL window behind it and the size that was asked for.
    TEST_CASE_METHOD(alwindowsdlheadless_data, "ALWindowSDLHeadless: the fixture's window is the hidden backend at the size asked for", "[ALWindowSDLHeadless]")
    {
        LLWindow* window = gl().window();
        REQUIRE(window != nullptr);
        CHECK(LLWindowManager::getBackend() == ALWindowBackend::Hidden);
        // createWindow would have returned null on an invalid one; what is
        // left to see is the SDL window behind it.
        UNSCOPED_INFO("there is an SDL window behind it");
        CHECK(window->getPlatformWindow() != nullptr);

        LLCoordWindow size;
        REQUIRE(window->getSize(&size));
        CHECK(size.mX == ll_test::HeadlessGL::WIDTH);
        CHECK(size.mY == ll_test::HeadlessGL::HEIGHT);
    }

    // A worker thread's shared context, as LLImageGLThread and the VBO
    // streaming thread use it: made on the main thread, bound and released
    // on the worker, and the texture the worker makes on it is the same
    // object on the main thread's context afterwards.
    TEST_CASE_METHOD(alwindowsdlheadless_data, "ALWindowSDLHeadless: a texture made on a worker's shared context is the main one's too", "[ALWindowSDLHeadless]")
    {
        LLWindow* window = gl().window();

        void* shared = window->createSharedContext();
        REQUIRE(shared != nullptr);

        // What the worker saw, checked on this thread once it has ended.
        bool current_on_worker = false;
        GLuint name = 0;
        GLenum worker_error = GL_NO_ERROR;
        std::thread worker([&]
        {
            window->makeContextCurrent(shared);

            // With no context current, GL does nothing -- or, on macOS,
            // crashes -- so the worker makes nothing unless the bind took.
            current_on_worker = contextCurrentHere();
            if (current_on_worker)
            {
                glGenTextures(1, &name);
                glBindTexture(GL_TEXTURE_2D, name);
                const U8 texel[4] = { 0x12, 0x34, 0x56, 0x78 };
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
                glBindTexture(GL_TEXTURE_2D, 0);
                // The object is complete and visible to the share group once
                // the commands that made it have run.
                glFinish();
                worker_error = glGetError();
            }

            window->destroySharedContext(shared);
        });
        worker.join();

        // Unless the shared context was current there, the worker's error
        // and texture say nothing.
        REQUIRE(current_on_worker);
        CHECK(worker_error == (GLenum)GL_NO_ERROR);
        UNSCOPED_INFO("the worker made a texture");
        CHECK(name != 0);
        UNSCOPED_INFO("the main context sees the worker's texture");
        CHECK(glIsTexture(name) == GL_TRUE);

        // The shared context's texture slots cache what they bound, so the
        // read puts back the texture it found.
        GLint prior = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prior);
        glBindTexture(GL_TEXTURE_2D, name);
        GLint width = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glBindTexture(GL_TEXTURE_2D, prior);
        UNSCOPED_INFO("with the storage the worker gave it");
        CHECK(width == 1);
        UNSCOPED_INFO("no error on the main context");
        CHECK(glGetError() == (GLenum)GL_NO_ERROR);

        glDeleteTextures(1, &name);
    }

    TEST_CASE_METHOD(alwindowsdlheadless_data, "ALWindowSDLHeadless: a shared context nobody bound can still be destroyed", "[ALWindowSDLHeadless]")
    {
        LLWindow* window = gl().window();
        void* shared = window->createSharedContext();
        REQUIRE(shared != nullptr);
        window->destroySharedContext(shared);
        UNSCOPED_INFO("the main context is untouched");
        CHECK(glGetError() == (GLenum)GL_NO_ERROR);
    }
}
