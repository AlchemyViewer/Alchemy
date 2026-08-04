/**
 * @file altexture3d.cpp
 * @brief ALTexture3D implementation
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

#include "altexture3d.h"

#include "llimagegl.h"
#include "llrender.h"

using namespace LLImageGLMemory;

ALTexture3D::~ALTexture3D()
{
    destroyGL();
}

bool ALTexture3D::allocate(U32 width, U32 height, U32 depth,
                           S32 internal_format, U32 primary_format, U32 type,
                           const void* data)
{
    llassert(gGLManager.mInited);

    if (width == 0 || height == 0 || depth == 0)
    {
        LL_WARNS() << "Refusing to allocate a 3D texture with a zero dimension: "
                   << width << "x" << height << "x" << depth << LL_ENDL;
        return false;
    }

    destroyGL();

    U32 texname = 0;
    LLImageGL::generateTextures(1, &texname);
    if (texname == 0)
    {
        LL_WARNS() << "Failed to generate a texture name for a 3D texture." << LL_ENDL;
        return false;
    }

    mWidth = width;
    mHeight = height;
    mDepth = depth;

    // The LLImageGL exists to carry the name, target and sampler state -- it is never
    // asked to allocate, since its allocation path is 2D.
    mImage = new LLImageGL(width, height, 4, false);
    mImage->setTexName(texname);
    mImage->setTarget(GL_TEXTURE_3D, ALTextureSlot::TT_TEXTURE_3D);

    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE_3D, texname);

    if (gGLManager.mHasTextureStorage)
    {
        // Immutable: one call covers the whole volume, and it may only be made once for
        // the object, so a later resize has to build a new texture.
        glTexStorage3D(GL_TEXTURE_3D, 1, internal_format, width, height, depth);
        mImage->markStorageAllocated();
        if (data != nullptr)
        {
            glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, width, height, depth, primary_format, type, data);
        }
    }
    else
    {
        glTexImage3D(GL_TEXTURE_3D, 0, internal_format, width, height, depth, 0, primary_format, type, data);
    }

    // Accounted as depth slices of a width*height surface, matching how the cube map
    // paths account their faces.
    alloc_tex_image(width, height, internal_format, depth, false);


    gGL.getTextureSlot(0)->unbind();

    stop_glerror();
    return true;
}

void ALTexture3D::setImage(U32 primary_format, U32 type, const void* data)
{
    if (mImage.isNull() || data == nullptr)
    {
        return;
    }

    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE_3D, mImage->getTexName());
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, mWidth, mHeight, mDepth, primary_format, type, data);
    gGL.getTextureSlot(0)->unbind();
    stop_glerror();
}

void ALTexture3D::bind(S32 stage)
{
    if (mImage.isNull())
    {
        return;
    }

    // Bound by name, so the sampler is passed explicitly -- otherwise this samples with GL's
    // defaults. Bilinear clamp: a colour-grading LUT is interpolated and must not wrap, or
    // the ends of the ramp bleed into each other.
    gGL.getTextureSlot(stage)->bindManual(ALTextureSlot::TT_TEXTURE_3D, mImage->getTexName(),
                                      gGL.getSampler(ALSamplers::BilinearClamp));
}

void ALTexture3D::unbind(S32 stage)
{
    gGL.getTextureSlot(stage)->unbind();
}

void ALTexture3D::destroyGL()
{
    // LLImageGL owns the name and releases it (and its accounting) on destruction.
    mImage = nullptr;
    mWidth = mHeight = mDepth = 0;
}

GLuint ALTexture3D::getGLName() const
{
    return mImage.notNull() ? mImage->getTexName() : 0;
}
