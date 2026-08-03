/**
 * @file llcubemap.h
 * @brief LLCubeMap class definition
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

#ifndef LL_LLCUBEMAP_H
#define LL_LLCUBEMAP_H

#include "llgl.h"

#include <vector>

class LLVector3;

// Environment map hack!
class LLCubeMap : public LLRefCount
{
public:
    LLCubeMap();
    void init(const std::vector<LLPointer<LLImageRaw> >& rawimages);

    void initGL();
    void initRawData(const std::vector<LLPointer<LLImageRaw> >& rawimages);
    void initGLData();

    void bind();
    void enable(S32 stage);

    void enableTexture(S32 stage);
    S32  getStage(void) { return mTextureStage; }

    void disable(void);
    void disableTexture(void);
    void setMatrix(S32 stage);
    void restoreMatrix();

    U32 getResolution() { return mImages[0].notNull() ? mImages[0]->getWidth(0) : 0; }

    // generate mip maps for this Cube Map using GL
    // NOTE: Cube Map MUST already be resident in VRAM

    GLuint getGLName();

    void destroyGL();

public:
    static bool sUseCubeMaps;

protected:
    friend class ALTextureSlot;
    ~LLCubeMap();
    LLGLenum mTargets[6];
    LLPointer<LLImageGL> mImages[6];
    LLPointer<LLImageRaw> mRawImages[6];
    S32 mTextureStage;
    S32 mMatrixStage;
};

#endif
