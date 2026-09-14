/**
 * @file llsdl.h
 * @brief SDL initialization
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#pragma once

#include "llpreprocessor.h"

#include "SDL3/SDL.h"

extern bool gSDLMainHandled;

void sdl_logger(void *userdata, int category, SDL_LogPriority priority, const char *message);
// Apply our SDL hints. Must run before the *first* SDL_InitSubSystem(SDL_INIT_VIDEO)
// of the process: some hints (notably SDL_HINT_MAC_SCROLL_MOMENTUM and
// SDL_HINT_MAC_PRESS_AND_HOLD) are consumed by SDL's Cocoa registerUserDefaults,
// which runs once during the first video init and is never revisited. The splash
// screen brings video up before init_sdl(), so it calls this first too. Idempotent.
void set_sdl_hints();
void init_sdl(const std::string& app_name);
void quit_sdl();

// Shared GL contexts for worker threads (texture upload, VBO streaming).
//
// A worker asks for a context that shares the main context's object
// namespace, binds it on its own thread, and releases it when done. Rather
// than SDL3's one-hidden-carrier-SDL_Window-per-context pattern, which forces
// a main-thread-only deferred window destruction, the contexts are made with
// the platform GL API behind SDL, against whatever context is current:
//   * Windows  -- WGL sibling context on the current DC
//   * macOS    -- CGL context sharing the current CGLContextObj, drawable-less
//   * Linux    -- EGL context made current surfaceless (EGL_NO_SURFACE), on
//                 Wayland and X11 alike: set_sdl_hints() has SDL create the
//                 main context with EGL (SDL_HINT_VIDEO_FORCE_EGL), so there
//                 is no GLX path and no X11 header in the viewer.
//
// sdl_create_shared_context runs on the main thread with the main context
// current and returns an opaque heap handle, or null. The other two take that
// handle; make-current on the worker, destroy on whichever thread is
// finishing with it. A window backend that hands these out tracks the live
// handles itself, so what a worker failed to release can be reclaimed when
// the main context goes.
void* sdl_create_shared_context();
void  sdl_make_shared_context_current(void* handle);
void  sdl_destroy_shared_context(void* handle);
