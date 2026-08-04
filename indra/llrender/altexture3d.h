/**
 * @file altexture3d.h
 * @brief ALTexture3D class definition
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

#include "llgl.h"

// A volume (3D) texture.
//
// Deliberately a separate type rather than a depth dimension bolted onto LLImageGL:
// that class is 2D throughout -- discard levels, pick masks, alpha analysis, downscale
// via a 2D framebuffer -- and giving it a depth that is 1 on almost every path is how
// fields end up meaning different things in different places. D3D11 agrees: ID3D11Texture3D
// is its own resource type with its own description, not a 2D texture with a depth field.
//
// Follows LLCubeMapArray's shape: this owns the GL texture and wraps it in an LLImageGL
// purely for binding, filtering and lifetime.
class ALTexture3D : public LLRefCount
{
public:
    ALTexture3D() = default;

    // Allocate immutable storage for a single level and upload it.
    //
    // internal_format must be sized (GL_RGB8, GL_RGBA8, ...), since that is what
    // glTexStorage3D requires. data may be null to allocate without uploading.
    // Returns false and leaves the object empty if allocation fails.
    bool allocate(U32 width, U32 height, U32 depth,
                  S32 internal_format, U32 primary_format, U32 type,
                  const void* data);

    // Replace the contents of the (already allocated) level 0.
    void setImage(U32 primary_format, U32 type, const void* data);

    void bind(S32 stage);
    void unbind(S32 stage);

    void destroyGL();

    bool  isAllocated() const { return mImage.notNull(); }
    U32   getWidth() const { return mWidth; }
    U32   getHeight() const { return mHeight; }
    U32   getDepth() const { return mDepth; }
    GLuint getGLName() const;

protected:
    friend class ALTextureSlot;
    ~ALTexture3D();

    LLPointer<LLImageGL> mImage;
    U32 mWidth = 0;
    U32 mHeight = 0;
    U32 mDepth = 0;
};
