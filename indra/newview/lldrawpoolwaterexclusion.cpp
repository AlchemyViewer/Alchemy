/**
 * @file lldrawpool.cpp
 * @brief LLDrawPoolMaterials class implementation
 * @author Jonathan "Geenz" Goodman
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolwaterexclusion.h"

#include "alwaterstencil.h"
#include "llglstates.h"
#include "llviewercamera.h"
#include "llviewershadermgr.h"
#include "pipeline.h"

LLDrawPoolWaterExclusion::LLDrawPoolWaterExclusion() : LLRenderPass(LLDrawPool::POOL_WATEREXCLUSION)
{
    LL_INFOS("DPInvisible") << "Creating water exclusion draw pool" << LL_ENDL;
}


// The PASS_INVISIBLE prims into the bound screen target's stencil: the exclude bit wherever one lies in front of the
// scene and on the camera's side of the water surface (waterExclusionF.glsl). No colour and no depth.
void LLDrawPoolWaterExclusion::render(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL;

    LLGLSLShader& shader = gWaterExclusionProgram;
    if (!shader.isComplete())
    {
        return;
    }

    shader.bind();
    gPipeline.mWaterSurface.bind(shader);

    // The camera the batches are drawn from, whose view the modelview starts at.
    const LLCamera& camera = LLViewerCamera::getCurrent();
    LLMatrix4a eye_to_agent;
    eye_to_agent.setAffineInverse(camera.getModelview());
    shader.uniformMatrix4fv(LLShaderMgr::INVERSE_MODELVIEW_MATRIX, eye_to_agent);
    shader.uniform3fv(LLShaderMgr::WL_CAMPOSLOCAL, 1, camera.getOrigin().mV);

    {
        LLGLDepthTest depth(GL_TRUE, GL_FALSE);
        LLGLSColorMask no_color(false, false);
        ALWaterStencil::Exclude stencil;
        pushBatches(LLRenderPass::PASS_INVISIBLE, false, false);
    }

    shader.unbind();
}
