/**
 * @file llviewerdisplay.h
 * @brief LLViewerDisplay class header file
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

#ifndef LL_LLVIEWERDISPLAY_H
#define LL_LLVIEWERDISPLAY_H

class LLMatrix4a;

void display_startup();
void display_cleanup();

void display(bool rebuild = true, F32 zoom_factor = 1.f, int subfield = 0, bool for_snapshot = false);

// Ends the frame display() showed, when it is paced (ALFrameEnd): the main loop's work in the time its end would wait
// for the GPU, then the wait. The main loop calls it straight after display(), out of display()'s scope.
void display_end_frame();

// Whether the window shows nothing -- inactive (minimized), hidden, or a session without one -- so that display()
// draws nothing.
bool display_window_hidden();

// The projection and modelview the HUD's attachments are drawn with over the whole world view. False where the
// agent's avatar wears no HUD.
bool get_hud_matrices(LLMatrix4a& proj, LLMatrix4a& model);

extern bool gDisplaySwapBuffers;
// A snapshot whose camera looks away from the main view's, as a 360 capture's faces do: what the main view's queries
// found occluded says nothing of what it sees.
extern bool gSnapshotOffMainView;
extern bool gDepthDirty;
extern bool gTeleportDisplay;
extern LLFrameTimer gTeleportDisplayTimer;
extern bool         gForceRenderLandFence;

#endif // LL_LLVIEWERDISPLAY_H
