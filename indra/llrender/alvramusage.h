/**
 * @file alvramusage.h
 * @brief What the viewer holds in video memory, by what it is for.
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

#include "stdtypes.h"

/// What the viewer holds in video memory, in bytes, by what it is for. Each figure is the size of what was allocated,
/// recorded as it was allocated and dropped as it was deleted, and counted in one figure alone: texture storage at
/// every level it was given and at the size its format pads to, buffers at the size of their stores. A texture leaves
/// its figure as it is deleted, though GL keeps its storage while the deferred delete holds its name; a buffer store
/// kept for the frames the GPU may still read it (a ring replaced at a new size) stays counted until it is released,
/// and one the driver keeps after it was respecified is not counted. Storage GL refused for want of video memory stays
/// counted; that GL ran out is noted (take_gl_out_of_memory).
struct ALVRAMUsage
{
    U64 mStreamedTextures = 0; ///< textures fetched by asset ID or from the bake service, but where the UI alone
                               ///< holds them (LLImageGLMemory::STREAMED_TEXTURE)
    U64 mOtherTextures    = 0; ///< every other texture: UI, media, local images, lookup tables
    U64 mRenderTargets    = 0; ///< render target attachments, the transient pool's among them
    U64 mReflectionProbes = 0; ///< the reflection and hero probes' cube map arrays
    U64 mGeometry         = 0; ///< vertex and index buffers, in use and kept by the pool for reuse
    U64 mGeometryPooled   = 0; ///< of mGeometry, what the pool keeps for reuse
    U64 mGPUBuffers       = 0; ///< every other buffer: ALGPUBuffer stores, the stores ALUniformBuffer makes itself and
                               ///< the buffer LLImageGL::scaleDown reads back through

    /// The textures, both kinds.
    [[nodiscard]] U64 textures() const { return mStreamedTextures + mOtherTextures; }
    /// Everything counted.
    [[nodiscard]] U64 total() const { return textures() + mRenderTargets + mReflectionProbes + mGeometry + mGPUBuffers; }

    /// What is held now.
    static ALVRAMUsage current();
};
