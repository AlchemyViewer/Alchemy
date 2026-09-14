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

#include "../test/lltut.h"

#include <thread>

namespace tut
{
    struct alwindowsdlheadless_data
    {
        static ll_test::HeadlessGL& gl()
        {
            static ll_test::HeadlessGL instance(/*needs_vbos=*/false,
                                                /*needs_imagegl=*/false,
                                                /*needs_llrender=*/false,
                                                /*needs_render=*/false);
            return instance;
        }

        alwindowsdlheadless_data() { gl(); }
    };

    typedef test_group<alwindowsdlheadless_data> alwindowsdlheadless_test;
    typedef alwindowsdlheadless_test::object     alwindowsdlheadless_object;
    tut::alwindowsdlheadless_test alwindowsdlheadless_testcase("ALWindowSDLHeadless");

    // The window the manager handed the fixture is the hidden backend, valid,
    // with an SDL window behind it and the size that was asked for.
    template<> template<>
    void alwindowsdlheadless_object::test<1>()
    {
        LLWindow* window = gl().window();
        ensure("a window", window != nullptr);
        ensure("the hidden backend was asked for",
               LLWindowManager::getBackend() == ALWindowBackend::Hidden);
        // createWindow would have returned null on an invalid one; what is
        // left to see is the SDL window behind it.
        ensure("there is an SDL window behind it", window->getPlatformWindow() != nullptr);

        LLCoordWindow size;
        ensure("the size can be read", window->getSize(&size));
        ensure_equals("width as asked",  size.mX, ll_test::HeadlessGL::WIDTH);
        ensure_equals("height as asked", size.mY, ll_test::HeadlessGL::HEIGHT);
    }

    // A worker thread's shared context, as LLImageGLThread and the VBO
    // streaming thread use it: made on the main thread, bound and released
    // on the worker, and the texture the worker makes on it is the same
    // object on the main thread's context afterwards.
    template<> template<>
    void alwindowsdlheadless_object::test<2>()
    {
        LLWindow* window = gl().window();

        void* shared = window->createSharedContext();
        ensure("a shared context was made", shared != nullptr);

        GLuint name = 0;
        GLenum worker_error = GL_NO_ERROR;
        std::thread worker([&]
        {
            window->makeContextCurrent(shared);

            glGenTextures(1, &name);
            glBindTexture(GL_TEXTURE_2D, name);
            const U8 texel[4] = { 0x12, 0x34, 0x56, 0x78 };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
            glBindTexture(GL_TEXTURE_2D, 0);
            // The object is complete and visible to the share group once the
            // commands that made it have run.
            glFinish();
            worker_error = glGetError();

            window->destroySharedContext(shared);
        });
        worker.join();

        ensure_equals("the worker's GL calls raised no error", worker_error, (GLenum)GL_NO_ERROR);
        ensure("the worker made a texture", name != 0);
        ensure("the main context sees the worker's texture", glIsTexture(name) == GL_TRUE);

        glBindTexture(GL_TEXTURE_2D, name);
        GLint width = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        glBindTexture(GL_TEXTURE_2D, 0);
        ensure_equals("with the storage the worker gave it", width, 1);
        ensure_equals("and no error on this side", glGetError(), (GLenum)GL_NO_ERROR);

        glDeleteTextures(1, &name);
    }

    // A shared context nobody bound is still a shared context to destroy.
    template<> template<>
    void alwindowsdlheadless_object::test<3>()
    {
        LLWindow* window = gl().window();
        void* shared = window->createSharedContext();
        ensure("a shared context was made", shared != nullptr);
        window->destroySharedContext(shared);
        ensure_equals("the main context is untouched", glGetError(), (GLenum)GL_NO_ERROR);
    }
}
