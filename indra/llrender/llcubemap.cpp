/**
 * @file llcubemap.cpp
 * @brief LLCubeMap class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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
#include "linden_common.h"

#include "llworkerthread.h"

#include "llcubemap.h"

#include "v4coloru.h"
#include "v3math.h"
#include "v3dmath.h"
#include "m3math.h"
#include "m4math.h"

#include "llrender.h"
#include "llglslshader.h"

#include "llglheaders.h"

namespace {
    const U16 RESOLUTION = 64;
}

bool LLCubeMap::sUseCubeMaps = true;

LLCubeMap::LLCubeMap()
    : mTextureStage(0),
      mMatrixStage(0)
{
    mTargets[0] = GL_TEXTURE_CUBE_MAP_NEGATIVE_X;
    mTargets[1] = GL_TEXTURE_CUBE_MAP_POSITIVE_X;
    mTargets[2] = GL_TEXTURE_CUBE_MAP_NEGATIVE_Y;
    mTargets[3] = GL_TEXTURE_CUBE_MAP_POSITIVE_Y;
    mTargets[4] = GL_TEXTURE_CUBE_MAP_NEGATIVE_Z;
    mTargets[5] = GL_TEXTURE_CUBE_MAP_POSITIVE_Z;
}

LLCubeMap::~LLCubeMap()
{
}

void LLCubeMap::initGL()
{
    llassert(gGLManager.mInited);

    if (LLCubeMap::sUseCubeMaps)
    {
        // Not initialized, do stuff.
        if (mImages[0].isNull())
        {
            U32 texname = 0;

            LLImageGL::generateTextures(1, &texname);

            // Linear RGBA8, not sRGB: the faces are copies of the sky textures, whose
            // pixels have always been stored and sampled raw. Were a decode ever wanted,
            // it is a sampler decision now (ALSampler::SRGBDecode), not a storage one.
            const S32 internal_format = GL_RGBA8;

            // Allocate the whole cube up front. glTexStorage2D on GL_TEXTURE_CUBE_MAP
            // allocates all six faces in a single call, and may only be called once for
            // the object -- so it has to happen here rather than inside the per-face
            // loop. The faces are then told storage exists, which makes their uploads
            // sub-image writes instead of allocations.
            bool immutable = false;
            if (gGLManager.mHasTextureStorage)
            {
                gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP, texname);
                glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, internal_format, RESOLUTION, RESOLUTION);
                // count of 6: alloc_tex_image's count covers cube faces. The per-face
                // path used to account one face at a time onto the same texture name,
                // each call releasing the last, so only one face's worth was ever
                // recorded for the whole cube.
                LLImageGLMemory::alloc_tex_image(RESOLUTION, RESOLUTION, internal_format, 6, false);
                immutable = true;
                stop_glerror();
            }

            for (int i = 0; i < 6; i++)
            {
                mImages[i] = new LLImageGL(RESOLUTION, RESOLUTION, 4, false);
                // Explicit either way, so each face's format matches the storage exactly
                // rather than relying on the auto-format switch agreeing with it.
                mImages[i]->setExplicitFormat(internal_format, GL_RGBA);
                mImages[i]->setTarget(mTargets[i], ALTextureSlot::TT_CUBE_MAP);
                if (immutable)
                {
                    mImages[i]->markStorageAllocated();
                }
                mRawImages[i] = new LLImageRaw(RESOLUTION, RESOLUTION, 4);
                if (!mImages[i]->createGLTexture(0, mRawImages[i], texname))
                {
                    LL_WARNS() << "Failed to create GL texture for environment cubemap face " << i << LL_ENDL;
                }

                gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_CUBE_MAP, texname);
                stop_glerror();
            }
            gGL.getTextureSlot(0)->unbind();
        }
        disable();
    }
    else
    {
        LL_WARNS() << "Using cube map without extension!" << LL_ENDL;
    }
}

void LLCubeMap::initRawData(const std::vector<LLPointer<LLImageRaw> >& rawimages)
{
    bool flip_x[6] =    { false, true,  false, false, true,  false };
    bool flip_y[6] =    { true,  true,  true,  false, true,  true  };
    bool transpose[6] = { false, false, false, false, true,  true  };

    // Yes, I know that this is inefficient! - djs 08/08/02
    for (int i = 0; i < 6; i++)
    {
        LLImageDataSharedLock lockIn(rawimages[i]);
        LLImageDataLock lockOut(mRawImages[i]);

        const U8 *sd = rawimages[i]->getData();
        U8 *td = mRawImages[i]->getData();

        S32 offset = 0;
        S32 sx, sy, so;
        for (int y = 0; y < 64; y++)
        {
            for (int x = 0; x < 64; x++)
            {
                sx = x;
                sy = y;
                if (flip_y[i])
                {
                    sy = 63 - y;
                }
                if (flip_x[i])
                {
                    sx = 63 - x;
                }
                if (transpose[i])
                {
                    S32 temp = sx;
                    sx = sy;
                    sy = temp;
                }

                so = 64*sy + sx;
                so *= 4;
                *(td + offset++) = *(sd + so++);
                *(td + offset++) = *(sd + so++);
                *(td + offset++) = *(sd + so++);
                *(td + offset++) = *(sd + so++);
            }
        }
    }
}

void LLCubeMap::initGLData()
{
    LL_PROFILE_ZONE_SCOPED;
    for (int i = 0; i < 6; i++)
    {
        mImages[i]->setSubImage(mRawImages[i], 0, 0, RESOLUTION, RESOLUTION);
    }
}

void LLCubeMap::init(const std::vector<LLPointer<LLImageRaw> >& rawimages)
{
    if (!gGLManager.mIsDisabled)
    {
        initGL();
        initRawData(rawimages);
        initGLData();
    }
}

// initReflectionMap and initEnvironmentMap were deleted here: no callers, and both
// bodies had gone illegal under immutable storage -- their per-face createGLTexture
// path would hand a cube FACE target to glTexStorage2D, which only accepts the
// GL_TEXTURE_CUBE_MAP target (allocating all six faces at once; see initGL).

GLuint LLCubeMap::getGLName()
{
    return mImages[0]->getTexName();
}

void LLCubeMap::bind()
{
    // Clamp + anisotropic: what the environment map has always been sampled with, named
    // here now instead of written onto face 0 at load.
    gGL.getTextureSlot(mTextureStage)->bind(this, ALSamplers::AnisoClamp);
}

void LLCubeMap::enable(S32 stage)
{
    enableTexture(stage);
}

// Remembers which slot bind() will use. It used to also mark that slot "enabled for
// TT_CUBE_MAP" ahead of the bind that would set the target anyway.
void LLCubeMap::enableTexture(S32 stage)
{
    mTextureStage = stage;
}

void LLCubeMap::disable(void)
{
    disableTexture();
}

void LLCubeMap::disableTexture(void)
{
    if (mTextureStage >= 0 && LLCubeMap::sUseCubeMaps)
    {
        // Followed by re-enabling slot 0 for TT_TEXTURE when the cube map had been on it --
        // restoring a target for the benefit of nothing, since the next bind names its own.
        gGL.getTextureSlot(mTextureStage)->unbind();
    }
}

void LLCubeMap::setMatrix(S32 stage)
{
    mMatrixStage = stage;

    if (mMatrixStage < 0) return;

    // No getTextureSlot(stage)->activate() here any more: it guarded a texture matrix stack
    // that MM_TEXTURE0 no longer selects through the active unit. See LLRender::eMatrixMode.

    LLVector3 x(gGLModelView+0);
    LLVector3 y(gGLModelView+4);
    LLVector3 z(gGLModelView+8);

    LLMatrix3 mat3;
    mat3.setRows(x,y,z);
    LLMatrix4 trans(mat3);
    trans.transpose();

    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.pushMatrix();
    gGL.loadMatrix((F32 *)trans.mMatrix);
    gGL.matrixMode(LLRender::MM_MODELVIEW);
}

void LLCubeMap::restoreMatrix()
{
    if (mMatrixStage < 0) return;

    gGL.matrixMode(LLRender::MM_TEXTURE0);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
}


void LLCubeMap::destroyGL()
{
    for (S32 i = 0; i < 6; i++)
    {
        mImages[i] = NULL;
    }
}
