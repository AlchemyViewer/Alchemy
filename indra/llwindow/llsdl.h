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
