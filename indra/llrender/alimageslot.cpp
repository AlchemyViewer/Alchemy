/**
 * @file alimageslot.cpp
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

#include "linden_common.h"

#include "alimageslot.h"

#include "altexture3d.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llrendertarget.h"

U32 ALImageSlot::sImageBinds = 0;

namespace
{
    GLenum to_gl_access(ALImageSlot::eAccess access)
    {
        switch (access)
        {
        case ALImageSlot::WRITE_ONLY: return GL_WRITE_ONLY;
        case ALImageSlot::READ_WRITE: return GL_READ_WRITE;
        case ALImageSlot::READ_ONLY:
        default:                      return GL_READ_ONLY;
        }
    }

    // A texture with more than one layer: the image binds all of them unless one is picked.
    bool is_layered(ALTextureSlot::eTextureType type)
    {
        return type == ALTextureSlot::TT_CUBE_MAP || type == ALTextureSlot::TT_CUBE_MAP_ARRAY
            || type == ALTextureSlot::TT_TEXTURE_3D;
    }
}

ALImageSlot::ALImageSlot(S32 index)
    : mIndex(index)
{
    llassert_always(index < (S32)AL_NUM_IMAGE_SLOTS);
    reset();
}

void ALImageSlot::reset()
{
    mCurrTexture = 0;
    mCurrLevel   = 0;
    mCurrLayer   = 0;
    mCurrFormat  = GL_R8;
    mCurrAccess  = READ_ONLY;
    mCurrLayered = false;
}

// static
bool ALImageSlot::isImageFormat(U32 format)
{
    switch (format)
    {
    case GL_RGBA32F:
    case GL_RGBA16F:
    case GL_RG32F:
    case GL_RG16F:
    case GL_R11F_G11F_B10F:
    case GL_R32F:
    case GL_R16F:
    case GL_RGBA32UI:
    case GL_RGBA16UI:
    case GL_RGB10_A2UI:
    case GL_RGBA8UI:
    case GL_RG32UI:
    case GL_RG16UI:
    case GL_RG8UI:
    case GL_R32UI:
    case GL_R16UI:
    case GL_R8UI:
    case GL_RGBA32I:
    case GL_RGBA16I:
    case GL_RGBA8I:
    case GL_RG32I:
    case GL_RG16I:
    case GL_RG8I:
    case GL_R32I:
    case GL_R16I:
    case GL_R8I:
    case GL_RGBA16:
    case GL_RGB10_A2:
    case GL_RGBA8:
    case GL_RG16:
    case GL_RG8:
    case GL_R16:
    case GL_R8:
    case GL_RGBA16_SNORM:
    case GL_RGBA8_SNORM:
    case GL_RG16_SNORM:
    case GL_RG8_SNORM:
    case GL_R16_SNORM:
    case GL_R8_SNORM:
        return true;
    default:
        return false;
    }
}

bool ALImageSlot::bind(LLRenderTarget* target, U32 attachment, eAccess access, U32 level)
{
    if (!target || attachment >= target->getNumTextures())
    {
        LL_WARNS("ImageSlot") << "Image slot " << mIndex << ": no colour attachment " << attachment << " to bind"
                              << LL_ENDL;
        llassert(false);
        return false;
    }
    return bindManual(target->getTexture(attachment), level, false, 0, access, target->getInternalFormat(attachment));
}

bool ALImageSlot::bind(LLImageGL* image, eAccess access, U32 level)
{
    if (!image || !image->getTexName())
    {
        LL_WARNS("ImageSlot") << "Image slot " << mIndex << ": no texture to bind" << LL_ENDL;
        llassert(false);
        return false;
    }
    return bindManual(image->getTexName(), level, is_layered(image->getTarget()), 0, access,
                      (U32)image->getStorageInternalFormat());
}

bool ALImageSlot::bind(ALTexture3D* texture, eAccess access, U32 level)
{
    if (!texture || !texture->isAllocated())
    {
        LL_WARNS("ImageSlot") << "Image slot " << mIndex << ": no volume to bind" << LL_ENDL;
        llassert(false);
        return false;
    }
    return bindManual(texture->getGLName(), level, true, 0, access, texture->getInternalFormat());
}

bool ALImageSlot::bindManual(U32 texture, U32 level, bool layered, S32 layer, eAccess access, U32 format)
{
    llassert(LLRender::sCompute);
    if (mIndex < 0 || !gGLManager.mHasCompute)
    {
        return false;
    }

    // An image reads and writes the texel's bits as `format` lays them out. sRGB would need a conversion on every
    // access, depth has no texel bits to give, and a compressed block is not a texel: GL takes none of them.
    if (!isImageFormat(format))
    {
        LL_WARNS_ONCE("ImageSlot") << "Image slot " << mIndex << ": format 0x" << std::hex << format << std::dec
                                   << " cannot be bound as an image" << LL_ENDL;
        return false;
    }

    if (mCurrTexture == texture && mCurrLevel == level && mCurrLayered == layered && mCurrLayer == layer
        && mCurrAccess == access && mCurrFormat == format)
    {
        return true;
    }

    // What is batched was meant to see the unit as it was.
    gGL.flush();

    glBindImageTexture(mIndex, texture, level, layered ? GL_TRUE : GL_FALSE, layer, to_gl_access(access), format);
    ++sImageBinds;

    mCurrTexture = texture;
    mCurrLevel   = level;
    mCurrLayered = layered;
    mCurrLayer   = layer;
    mCurrAccess  = access;
    mCurrFormat  = format;
    return true;
}

void ALImageSlot::unbind()
{
    if (mIndex < 0 || mCurrTexture == 0 || !gGLManager.mHasCompute)
    {
        return;
    }

    gGL.flush();

    glBindImageTexture(mIndex, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R8);
    ++sImageBinds;
    reset();
}

void ALImageSlot::refreshState()
{
    // Image units exist only where compute does; a 4.1 context has nothing here to restore.
    if (mIndex < 0 || !gGLManager.mHasCompute)
    {
        return;
    }

    gGL.flush();

    glBindImageTexture(mIndex, mCurrTexture, mCurrLevel, mCurrLayered ? GL_TRUE : GL_FALSE, mCurrLayer,
                       to_gl_access(mCurrAccess), mCurrFormat);
    ++sImageBinds;
}
