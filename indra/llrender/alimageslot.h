/**
 * @file alimageslot.h
 * @brief One image unit: the texture level a shader loads from and stores to, tracked like a texture slot.
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

class ALTexture3D;
class LLImageGL;
class LLRenderTarget;

// GL 4.2's minimum GL_MAX_IMAGE_UNITS, so every unit here exists on every context that has images at all.
constexpr U32 AL_NUM_IMAGE_SLOTS = 8;

// One image unit (glBindImageTexture): a single level of a texture, or every layer of it, that a shader reads and
// writes texel by texel with imageLoad / imageStore, as a format and an access. D3D11's unordered access view at an
// index, which is what ALTextureSlot's shader resource view is to it.
//
// LLRender owns one per unit (getImageSlot) and keeps them as it keeps the texture slots: a bind that matches what
// the unit holds issues nothing, refreshState() re-issues what each holds, and forgetTextures() drops a deleted name.
// A program's image uniforms are given their units at link, in reserved-uniform order, so a caller binds through
// LLGLSLShader::bindImage rather than naming a unit.
//
// Shader writes through a unit reach later reads only past the barrier for that kind of read: LLRender::memoryBarrier.
class ALImageSlot
{
    friend class LLRender;
public:
    enum eAccess : U8
    {
        READ_ONLY,
        WRITE_ONLY,
        READ_WRITE,
    };

    // glBindImageTexture calls issued, for checking that a redundant bind issues none.
    static U32 sImageBinds;

    ALImageSlot(S32 index = -1);

    // The image's format is the texture's own sized internal format; a format image load/store cannot take (sRGB,
    // depth, compressed) warns and binds nothing (returns false). `level` is the mip level the shader addresses.
    bool bind(LLRenderTarget* target, U32 attachment, eAccess access, U32 level = 0);
    bool bind(LLImageGL* image, eAccess access, U32 level = 0);
    // Layered: every slice of the volume, addressed by the shader's z.
    bool bind(ALTexture3D* texture, eAccess access, U32 level = 0);
    // A texture by name. `layered` binds every layer of an array, cube or volume; otherwise `layer` picks one.
    bool bindManual(U32 texture, U32 level, bool layered, S32 layer, eAccess access, U32 format);

    // Unit back to texture 0.
    void unbind();

    // Re-issue what this unit holds, after GL state was changed behind LLRender's back.
    void refreshState();

    S32 getIndex() const { return mIndex; }
    U32 getCurrTexture() const { return mCurrTexture; }
    U32 getCurrLevel() const { return mCurrLevel; }
    U32 getCurrFormat() const { return mCurrFormat; }
    eAccess getCurrAccess() const { return mCurrAccess; }

    // A sized internal format image load/store takes: GL's table of image unit formats.
    static bool isImageFormat(U32 format);

private:
    // What a fresh context's unit holds: texture 0, at GL's initial level, layer, access and format.
    void reset();

    S32     mIndex;
    U32     mCurrTexture = 0;
    U32     mCurrLevel   = 0;
    S32     mCurrLayer   = 0;
    U32     mCurrFormat;
    eAccess mCurrAccess  = READ_ONLY;
    bool    mCurrLayered = false;
};
