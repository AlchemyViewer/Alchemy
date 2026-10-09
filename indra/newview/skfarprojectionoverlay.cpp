/**
 * @file skfarprojectionoverlay.cpp
 * @brief Develop overlay that draws column pairs out to 100 km with the main projection.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "llviewerprecompiledheaders.h"

#include "skfarprojectionoverlay.h"

#include "skfarplane.h"

#include "llgl.h"
#include "llglstates.h"
#include "llrendertarget.h"
#include "pipeline.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewershadermgr.h"

extern bool gCubeSnapshot;

namespace
{
    // Owned for the session and never freed at exit, where GL is already gone. Its GL objects go whenever the
    // pipeline releases its own (skReleaseFarProjectionOverlay), so a recreated context or a new depth format
    // never finds names it does not own.
    LLRenderTarget* sTarget = nullptr;

    void box(const LLVector3& lo, const LLVector3& hi)
    {
        const LLVector3 v[8] = {
            { lo.mV[0], lo.mV[1], lo.mV[2] }, { hi.mV[0], lo.mV[1], lo.mV[2] }, { hi.mV[0], hi.mV[1], lo.mV[2] }, { lo.mV[0], hi.mV[1], lo.mV[2] },
            { lo.mV[0], lo.mV[1], hi.mV[2] }, { hi.mV[0], lo.mV[1], hi.mV[2] }, { hi.mV[0], hi.mV[1], hi.mV[2] }, { lo.mV[0], hi.mV[1], hi.mV[2] },
        };
        constexpr U8 quads[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
        for (const auto& q : quads)
        {
            for (U8 i : { q[0], q[1], q[2], q[0], q[2], q[3] })
            {
                gGL.vertex3fv(v[i].mV);
            }
        }
    }

    // A column on an axis-aligned square footprint, centred on the horizon at its bearing and offset.
    void column(const LLVector3& origin, const SKFarOverlayColumn& c)
    {
        const F32 rad = c.mBearing * DEG_TO_RAD;
        const LLVector3 forward(sinf(rad), cosf(rad), 0.f);
        const LLVector3 right(cosf(rad), -sinf(rad), 0.f);
        const LLVector3 centre = origin + forward * c.mDistance + right * c.mOffset;
        const LLVector3 half(c.mHalfWidth, c.mHalfWidth, 0.5f * c.mHeight);
        box(centre - half, centre + half);
    }
}

void skReleaseFarProjectionOverlay()
{
    if (sTarget)
    {
        sTarget->release();
    }
}

void skRenderFarProjectionOverlay()
{
    static LLCachedControl<bool> enabled(gSavedSettings, "SKRenderFarProjectionOverlay", false);
    if (!enabled || gCubeSnapshot)
    {
        if (!enabled)
        {
            skReleaseFarProjectionOverlay();
        }
        return;
    }

    // The columns get a depth buffer of their own: the main one holds the scene, which would hide them, and the
    // overlay must leave nothing in it for later passes to read.
    const U32 width = gPipeline.mRT->screen.getWidth();
    const U32 height = gPipeline.mRT->screen.getHeight();
    if (!sTarget)
    {
        sTarget = new LLRenderTarget();
    }
    if ((!sTarget->isComplete() || sTarget->getWidth() != width || sTarget->getHeight() != height) &&
        !sTarget->allocate(width, height, GL_RGBA8, true, false, ALTextureSlot::TT_TEXTURE, LLRenderTarget::MIPS_NONE, LLPipeline::mainDepthFormat()))
    {
        return;
    }

    const LLVector3 origin = LLViewerCamera::getInstance()->getOrigin();

    gGL.flush();
    sTarget->bindTarget();
    {
        LLGLSColorMask mask(true, true);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        sTarget->clear();
        gDebugProgram.bind();
        LLGLDepthTest depth(GL_TRUE, GL_TRUE);
        LLGLDisable cull(GL_CULL_FACE);
        LLGLDisable blend(GL_BLEND);

        // Nearer before farther, so a farther column only shows where depth order lets it.
        for (bool nearer : { true, false })
        {
            if (nearer)
            {
                gGL.diffuseColor4f(1.f, 0.5f, 0.f, 1.f);
            }
            else
            {
                gGL.diffuseColor4f(0.f, 0.8f, 1.f, 1.f);
            }
            gGL.begin(LLRender::TRIANGLES);
            for (const SKFarOverlayColumn& c : skFarOverlayColumns())
            {
                if (c.mNearer == nearer)
                {
                    column(origin, c);
                }
            }
            gGL.end();
            gGL.flush();
        }
        gDebugProgram.unbind();
    }
    sTarget->flush();

    // Laid over the frame without its depth, so the columns show in front of the scene.
    LLGLSColorMask mask(true, false);
    LLGLDepthTest depth(GL_FALSE, GL_FALSE);
    LLGLEnable blend(GL_BLEND);
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
    gCopyProgram.bind();
    gCopyProgram.bindTexture(LLShaderMgr::DIFFUSE_MAP, sTarget);
    gPipeline.mScreenTriangleVB->setBuffer();
    gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
    gCopyProgram.unbind();
    gGL.setSceneBlendType(LLRender::BT_ALPHA); // the tree's default, as other passes leave it
}
