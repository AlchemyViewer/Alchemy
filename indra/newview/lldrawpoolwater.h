/**
 * @file lldrawpoolwater.h
 * @brief LLDrawPoolWater class definition
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

#ifndef LL_LLDRAWPOOLWATER_H
#define LL_LLDRAWPOOLWATER_H

#include "lldrawpool.h"

class LLGLSLShader;

// The water surface: no faces, objects or partitions. It draws the pipeline's water field (ALWaterSurface) with the
// water or underwater program, after the opaque world and before the alpha that lies above the water.
class LLDrawPoolWater final : public LLDrawPool
{
protected:
    LLPointer<LLViewerTexture> mWaterNormp[2];

public:
    LLDrawPoolWater();
    ~LLDrawPoolWater() override = default;

    bool isDead() override { return false; }
    LLViewerTexture* getTexture() override { return nullptr; }
    void resetDrawOrders() override {}

    S32 getNumPostDeferredPasses() override;
    void beginPostDeferredPass(S32 pass) override;
    void renderPostDeferred(S32 pass) override;

    void prerender() override;

    void setNormalMaps(const LLUUID& normalMapId, const LLUUID& nextNormalMapId);

    /// The environment's wave normal map `i`: the current one (0) and the next (1), null before either is set.
    LLViewerTexture* getNormalMap(S32 i) const { return mWaterNormp[i]; }
};

#endif // LL_LLDRAWPOOLWATER_H
