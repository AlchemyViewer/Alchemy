/**
 * @file skfarprojectionoverlay.h
 * @brief Develop overlay that draws column pairs out to 100 km with the main projection,
 *        so depth order and the far plane can be judged by eye.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

// Draws skFarOverlayColumns() around the camera when SKRenderFarProjectionOverlay is on: nearer columns orange,
// farther ones cyan, depth tested against each other in a depth buffer of their own and laid over the frame.
// The scene does not hide them, culling and reach are bypassed, and the projection alone decides what shows:
// a finite plane clips the far pairs, and broken depth order shows cyan over orange.
void skRenderFarProjectionOverlay();

// Releases the overlay's render target with the pipeline's own (LLPipeline::releaseGLBuffers).
void skReleaseFarProjectionOverlay();
