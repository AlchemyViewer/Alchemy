/**
 * @file alwindowsdlheadless.h
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

#pragma once

#include "llwindowheadless.h"
#include "llmutex.h"

#include "SDL3/SDL.h"

#include <set>

// The headless backend for anything that renders: the appearance utility, the
// GL-backed tests, a viewer with no desktop. An SDL window is created hidden
// and never shown, and the GL context on it is the one the platform gives --
// WGL on Windows, CGL on macOS, EGL on Linux -- the same GL the viewer runs
// on. Where Linux has no display at all, SDL's offscreen driver supplies an
// EGL context with no surface; init_sdl arranges that.
//
// Everything a window does besides own a context is LLWindowHeadless's
// virtual state: size, position, cursor, visibility. Input never arrives, so
// the keyboard is the headless one.
class ALWindowSDLHeadless final : public LLWindowHeadless
{
public:
    ALWindowSDLHeadless(LLWindowCallbacks* callbacks,
        const std::string& title, const std::string& name,
        S32 x, S32 y, S32 width, S32 height,
        U32 flags, bool fullscreen, bool clear_background,
        bool enable_vsync, bool ignore_pixel_depth, U32 fsaa_samples);
    ~ALWindowSDLHeadless() override;

    bool isValid() override { return mContext != nullptr; }

    // Tears the context and the window down; LLWindowManager::destroyWindow
    // calls it before it quits SDL. The destructor finishes anything left.
    void close() override;

    void swapBuffers() override;
    void* getPlatformWindow() override { return mWindow; }

    // Worker-thread contexts, through the same platform path LLWindowSDL uses.
    void* createSharedContext() override;
    void makeContextCurrent(void* context) override;
    void destroySharedContext(void* context) override;

private:
    SDL_Window*   mWindow = nullptr;
    SDL_GLContext mContext = nullptr;

    // Live shared-context handles, so any a worker failed to release can be
    // reclaimed before the main context goes.
    LLMutex mSharedCtxMutex;
    std::set<void*> mSharedContexts;
};
