/**
 * @file llcubemaparray.cpp
 * @brief LLCubeMap class implementation
 *
 * $LicenseInfo:firstyear=2022&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2022, Linden Research, Inc.
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
#include "linden_common.h"

#include "llworkerthread.h"

#include "llcubemaparray.h"

#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "m3math.h"
#include "m4math.h"

#include "llrender.h"
#include "llglslshader.h"

#include "llglheaders.h"

//#pragma optimize("", off)

using namespace LLImageGLMemory;

// MUST match order of OpenGL face-layers
GLenum LLCubeMapArray::sTargets[6] =
{
    GL_TEXTURE_CUBE_MAP_POSITIVE_X,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
};

LLVector3 LLCubeMapArray::sLookVecs[6] =
{
        LLVector3(1, 0, 0),
        LLVector3(-1, 0, 0),
        LLVector3(0, 1, 0),
        LLVector3(0, -1, 0),
        LLVector3(0, 0, 1),
        LLVector3(0, 0, -1)
};

LLVector3 LLCubeMapArray::sUpVecs[6] =
{
    LLVector3(0, -1, 0),
    LLVector3(0, -1, 0),
    LLVector3(0, 0, 1),
    LLVector3(0, 0, -1),
    LLVector3(0, -1, 0),
    LLVector3(0, -1, 0)
};

LLVector3 LLCubeMapArray::sClipToCubeLookVecs[6] =
{
        LLVector3(0, 0, -1), //GOOD
        LLVector3(0, 0, 1), //GOOD

        LLVector3(1, 0, 0), // GOOD
        LLVector3(1, 0, 0), // GOOD

        LLVector3(1, 0, 0),
        LLVector3(-1, 0, 0),
};

LLVector3 LLCubeMapArray::sClipToCubeUpVecs[6] =
{
    LLVector3(-1, 0, 0), //GOOD
    LLVector3(1, 0, 0), //GOOD

    LLVector3(0, 1, 0), // GOOD
    LLVector3(0, -1, 0), // GOOD

    LLVector3(0, 0, -1),
    LLVector3(0, 0, 1)
};

LLCubeMapArray::LLCubeMapArray()
    : mTextureStage(0)
{

}

LLCubeMapArray::LLCubeMapArray(LLCubeMapArray& lhs, U32 width, U32 count) : mTextureStage(0)
{
    mWidth = width;
    mCount = count;

    // Allocate a new cubemap array with the same criteria as the incoming cubemap array
    allocate(mWidth, lhs.mImage->getComponents(), count, lhs.mImage->getUseMipMaps(), lhs.mHDR);

    U32 min_count = std::min(count, lhs.mCount);
    if (min_count == 0)
        return;

    const S32 components = lhs.mImage->getComponents();
    const GLenum format = (components == 4) ? GL_RGBA : GL_RGB;

    // glGetTexImage on a cube-map array returns ALL layers in a single call (spec §8.11);
    // sizing the destination for a single face would write past the end of the buffer.
    const size_t face_bytes = (size_t)lhs.mWidth * lhs.mWidth * components;
    std::vector<U8> src_layers(face_bytes * 6 * min_count);

    gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP_ARRAY, lhs.getGLName());
    glGetTexImage(GL_TEXTURE_CUBE_MAP_ARRAY, 0, format, GL_UNSIGNED_BYTE, src_layers.data());

    bind(0);
    for (U32 i = 0; i < min_count * 6; ++i)
    {
        LLPointer<LLImageRaw> face_image = new LLImageRaw(lhs.mWidth, lhs.mWidth, components);
        memcpy(face_image->getData(), src_layers.data() + i * face_bytes, face_bytes);
        LLPointer<LLImageRaw> scaled_image = face_image->scaled(mWidth, mWidth);
        glTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, 0, 0, i, mWidth, mWidth, 1, format, GL_UNSIGNED_BYTE, scaled_image->getData());
    }
    unbind();
}

LLCubeMapArray::~LLCubeMapArray()
{
}

void LLCubeMapArray::allocate(U32 resolution, U32 components, U32 count, bool use_mips, bool hdr)
{
    U32 texname = 0;
    mWidth = resolution;
    mCount = count;

    mHDR = hdr;

    LLImageGL::generateTextures(1, &texname);

    mImage = new LLImageGL(resolution, resolution, components, use_mips);
    mImage->setTexName(texname);
    mImage->setTarget(sTargets[0], ALTextureSlot::TT_CUBE_MAP_ARRAY);

    mImage->setUseMipMaps(use_mips);
    mImage->setHasMipMaps(use_mips);

    bind(0);
    free_cur_tex_image();

    U32 format = components == 4 ? GL_RGBA16F : GL_R11F_G11F_B10F;
    if (!hdr)
    {
        format = components == 4 ? GL_RGBA8 : GL_RGB8;
    }
    // One allocation for the whole array. glTexStorage3D covers every layer and mip in a
    // single call and may only be called once for the object, so it replaces the per-mip
    // glTexImage3D loop rather than sitting inside it. Every format above is sized, which
    // is what glTexStorage3D requires.
    if (gGLManager.mHasTextureStorage)
    {
        const S32 levels = use_mips ? LLImageGL::calcMipLevelCount(resolution, resolution) : 1;
        glTexStorage3D(GL_TEXTURE_CUBE_MAP_ARRAY, levels, format, resolution, resolution, count * 6);
        mImage->markStorageAllocated();
        stop_glerror();
    }
    else
    {
        U32 mip = 0;
        U32 mip_resolution = resolution;
        while (mip_resolution >= 1)
        {
            glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, format, mip_resolution, mip_resolution, count * 6, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            if (!use_mips)
            {
                break;
            }
            mip_resolution /= 2;
            ++mip;
        }
    }

    alloc_tex_image(resolution, resolution, format, count * 6, use_mips);

    //glGenerateMipmap(GL_TEXTURE_CUBE_MAP_ARRAY);  // <=== latest AMD drivers do not appreciate this method of allocating mipmaps

    unbind();
}

void LLCubeMapArray::bind(S32 stage)
{
    mTextureStage = stage;
    // Bound by name, so the sampler has to be passed explicitly -- mImage's filter/address
    // are sampler inputs now, not texture-object state, and bindManual would otherwise leave
    // this unit on sampler 0 and sample the probe array with GL's defaults
    // (GL_NEAREST_MIPMAP_LINEAR: nearest within the mip, i.e. blocky reflections).
    // Anisotropic when there is a mip chain to filter across, bilinear when there is not --
    // the choice the allocation used to write onto the image. Clamp either way: a probe array
    // that wrapped would fetch a neighbouring face at the seams.
    const ALSampler key = mImage->getUseMipMaps() ? ALSamplers::AnisoClamp
                                                  : ALSamplers::BilinearClamp;
    gGL.getTextureSlot(stage)->bindManual(ALTextureSlot::TT_CUBE_MAP_ARRAY, getGLName(),
                                      gGL.getSampler(key));
}

void LLCubeMapArray::unbind()
{
    gGL.getTextureSlot(mTextureStage)->unbind();
    mTextureStage = -1;
}

void LLCubeMapArray::copyFaceFromFramebuffer(S32 mip, S32 cube_index, S32 face, S32 res)
{
    // Sub-image write, so this stays legal now that the array has immutable storage.
    glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0, cube_index * 6 + face, 0, 0, res, res);
    stop_glerror();
}

GLuint LLCubeMapArray::getGLName()
{
    return mImage->getTexName();
}

void LLCubeMapArray::destroyGL()
{
    mImage = NULL;
}
