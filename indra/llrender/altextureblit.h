/**
 * @file altextureblit.h
 * @brief A copy from one texture into another through framebuffers it keeps, for textures no render target holds.
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

// Copies level 0 of one texture into another by glBlitFramebuffer, through a pair of framebuffers it makes on its first
// copy and keeps: the copy from a texture no LLRenderTarget holds, such as one whose storage a platform's interop
// supplies (a D3D11 texture, an IOSurface, a dma-buf), into another that need not be a target's either, upside down if
// asked.
//
// All operations require a current GL context, the one the framebuffers were made in. Non-copyable.
class ALTextureBlit
{
public:
    ALTextureBlit() = default;
    ~ALTextureBlit();

    ALTextureBlit(const ALTextureBlit&) = delete;
    ALTextureBlit& operator=(const ALTextureBlit&) = delete;

    // Copy the `width` x `height` rectangle at the origin of `src`, a texture of `src_target` (GL_TEXTURE_2D or
    // GL_TEXTURE_RECTANGLE), into the same rectangle of `dst`, a GL_TEXTURE_2D, texel for texel, its rows in the
    // opposite order where `flip_y`. What gGL has batched is drawn first, and the scissor does not clip the copy.
    // Neither texture stays attached, and the framebuffer bound before (LLRenderTarget::sCurFBO) is bound again.
    void blit(U32 src_target, U32 src, U32 dst, S32 width, S32 height, bool flip_y);

    // Delete the framebuffers, if any. Call from a teardown that still has the context; the destructor calls it as a
    // backstop, and issues no GL once the context is gone.
    void release();

    bool hasFramebuffers() const { return mReadFBO != 0 || mDrawFBO != 0; }

private:
    U32 mReadFBO = 0;
    U32 mDrawFBO = 0;
};
