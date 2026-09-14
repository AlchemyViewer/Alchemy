/**
 * @file alwindowsdlheadless.cpp
 * @brief A window never shown, with the platform GL context on it
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

#include "alwindowsdlheadless.h"

#include "llgl.h"
#include "llrender.h"
#include "llsdl.h"

ALWindowSDLHeadless::ALWindowSDLHeadless(LLWindowCallbacks* callbacks,
    const std::string& title, const std::string& name,
    S32 x, S32 y, S32 width, S32 height,
    U32 flags, bool fullscreen, bool clear_background,
    bool enable_vsync, bool ignore_pixel_depth, U32 fsaa_samples)
:   LLWindowHeadless(callbacks, title, name, x, y, width, height, flags,
                     fullscreen, clear_background, enable_vsync, true, ignore_pixel_depth)
{
    setFSAASamples(fsaa_samples);

    // The same request LLWindowSDL makes, less the fullscreen and density
    // properties a window nobody sees has no use for.
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (fsaa_samples > 0)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, fsaa_samples);
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }

    SDL_GLContextFlag context_flags{};
    if (LLRender::sGLCoreProfile)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if LL_DARWIN
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        context_flags |= SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG;
#else
        // A core profile needs an explicit version of 3.2 or better, or the
        // driver hands back a compatibility context. Ask for the highest the
        // viewer targets and step down below if the driver caps lower.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
#endif
    }
    if (gDebugGL)
    {
        context_flags |= SDL_GL_CONTEXT_DEBUG_FLAG;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, context_flags);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);

    mWindow = SDL_CreateWindow(title.c_str(), width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!mWindow)
    {
        LL_WARNS("Window") << "Hidden window creation failed: " << SDL_GetError() << LL_ENDL;
        return;
    }

    mContext = SDL_GL_CreateContext(mWindow);
#if !LL_DARWIN
    if (!mContext && LLRender::sGLCoreProfile)
    {
        S32 major = 4, minor = 6;
        while (!mContext)
        {
            if (minor > 0)      { --minor; }
            else if (major > 3) { --major; minor = 3; }
            else                { break; }
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
            mContext = SDL_GL_CreateContext(mWindow);
        }
        if (mContext)
        {
            LL_INFOS("Window") << "Created GL core context at fallback version "
                               << major << "." << minor << LL_ENDL;
        }
    }
#endif
    if (!mContext)
    {
        LL_WARNS("Window") << "Hidden window GL context creation failed: " << SDL_GetError() << LL_ENDL;
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
        return;
    }

    if (!SDL_GL_MakeCurrent(mWindow, mContext))
    {
        LL_WARNS("Window") << "Hidden window GL context could not be made current: " << SDL_GetError() << LL_ENDL;
        SDL_GL_DestroyContext(mContext);
        SDL_DestroyWindow(mWindow);
        mContext = nullptr;
        mWindow = nullptr;
        return;
    }

    if (!gGLManager.initGL())
    {
        LL_WARNS("Window") << "GL initialisation failed on the hidden window" << LL_ENDL;
        SDL_GL_DestroyContext(mContext);
        SDL_DestroyWindow(mWindow);
        mContext = nullptr;
        mWindow = nullptr;
        return;
    }

#if LL_LINUX
    gGLManager.initEGL();
#endif

    LL_INFOS("Window") << "Hidden window " << width << "x" << height
                       << " on SDL video driver " << SDL_GetCurrentVideoDriver() << LL_ENDL;
}

ALWindowSDLHeadless::~ALWindowSDLHeadless()
{
    close();
}

void ALWindowSDLHeadless::close()
{
    LLWindowHeadless::close();

    {
        // Normal shutdown joins the GL worker threads first, so anything still
        // here is a context a worker failed to release.
        LLMutexLock lk(&mSharedCtxMutex);
        if (!mSharedContexts.empty())
        {
            LL_WARNS("Window") << mSharedContexts.size()
                               << " shared GL context(s) still alive at shutdown; releasing." << LL_ENDL;
            for (void* handle : mSharedContexts)
            {
                sdl_destroy_shared_context(handle);
            }
            mSharedContexts.clear();
        }
    }

    if (mContext)
    {
        gGLManager.shutdownGL();
        SDL_GL_DestroyContext(mContext);
        mContext = nullptr;
    }
    if (mWindow)
    {
        SDL_DestroyWindow(mWindow);
        mWindow = nullptr;
    }
}

void ALWindowSDLHeadless::swapBuffers()
{
    if (mWindow)
    {
        SDL_GL_SwapWindow(mWindow);
    }
}

void* ALWindowSDLHeadless::createSharedContext()
{
    if (!SDL_GL_MakeCurrent(mWindow, mContext))
    {
        LL_WARNS("Window") << "SDL_GL_MakeCurrent(main) failed in createSharedContext: "
                           << SDL_GetError() << LL_ENDL;
    }

    void* shared = sdl_create_shared_context();
    if (shared)
    {
        LLMutexLock lk(&mSharedCtxMutex);
        mSharedContexts.insert(shared);
    }
    return shared;
}

void ALWindowSDLHeadless::makeContextCurrent(void* context)
{
    sdl_make_shared_context_current(context);
}

void ALWindowSDLHeadless::destroySharedContext(void* context)
{
    if (!context) return;
    {
        LLMutexLock lk(&mSharedCtxMutex);
        mSharedContexts.erase(context);
    }
    sdl_destroy_shared_context(context);
}
