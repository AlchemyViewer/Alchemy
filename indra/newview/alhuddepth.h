/**
 * @file alhuddepth.h
 * @brief The window's depth under the HUD: cleared where the HUD can draw, so it tests against itself alone, and
 *        the scene's written back after it for the 3D UI that follows.
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
#include "llglslshader.h"
#include "llglstates.h"
#include "llmath.h"
#include "llmatrix4a.h"
#include "llrect.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llvertexbuffer.h"
#include "v3math.h"

#include <cfloat>
#include <vector>

/// The HUD is drawn over the window's framebuffer, whose depth the final blit filled with the scene's for the 3D UI
/// around it (LLPipeline::renderFinalize). Within the viewport rects the HUD can cover (render_hud_attachments, a
/// rect per attachment point), clear() empties that depth before the HUD draws, and restore() writes the scene's
/// back after it wherever the scene is nearer than what the HUD left.
namespace ALHUDDepth
{

/// The pixels of a width x height viewport a box covers through `mvp`, an orthographic projection of the frame the
/// box is in: grown by a pixel each way and clipped to the viewport, empty where it misses.
inline LLRect boxRect(const LLMatrix4a& mvp, const LLVector3& lo, const LLVector3& hi, S32 width, S32 height)
{
    F32 min_x = FLT_MAX;
    F32 min_y = FLT_MAX;
    F32 max_x = -FLT_MAX;
    F32 max_y = -FLT_MAX;
    for (U32 i = 0; i < 8; ++i)
    {
        const LLVector4a corner((i & 1) ? hi.mV[VX] : lo.mV[VX], (i & 2) ? hi.mV[VY] : lo.mV[VY],
                                (i & 4) ? hi.mV[VZ] : lo.mV[VZ], 1.f);
        LLVector4a clip;
        mvp.transform4(corner, clip);
        // The projection is orthographic, so w is 1; the divide only guards against a degenerate one.
        const F32 w = clip[3] != 0.f ? clip[3] : 1.f;
        const F32 x = (clip[0] / w * 0.5f + 0.5f) * (F32)width;
        const F32 y = (clip[1] / w * 0.5f + 0.5f) * (F32)height;
        min_x = llmin(min_x, x);
        min_y = llmin(min_y, y);
        max_x = llmax(max_x, x);
        max_y = llmax(max_y, y);
    }

    // Far off-screen corners are clamped before they become integers.
    const F32 limit = (F32)(llmax(width, height) + 1);
    LLRect rect(llfloor(llclamp(min_x, -limit, limit)) - 1, llceil(llclamp(max_y, -limit, limit)) + 1,
                llceil(llclamp(max_x, -limit, limit)) + 1, llfloor(llclamp(min_y, -limit, limit)) - 1);
    rect.intersectWith(LLRect(0, height, width, 0));
    return rect;
}

/// Adds `rect` to `rects`, folding in every rect it overlaps until it overlaps none: HUDs sit in the corners and
/// along the edges, and one rect around them all would take in the screen between.
inline void mergeRect(std::vector<LLRect>& rects, LLRect rect)
{
    bool merged = true;
    while (merged)
    {
        merged = false;
        for (size_t i = 0; i < rects.size(); ++i)
        {
            if (rect.overlaps(rects[i]))
            {
                rect.unionWith(rects[i]);
                rects[i] = rects.back();
                rects.pop_back();
                merged = true;
                break;
            }
        }
    }
    rects.push_back(rect);
}

/// Clears the bound framebuffer's depth within each rect of a viewport whose origin is (x, y).
inline void clear(const std::vector<LLRect>& rects, S32 x, S32 y)
{
    // Depth writes on: they mask the clear.
    LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_ALWAYS);
    for (const LLRect& rect : rects)
    {
        LLGLSScissor scissor(x + rect.mLeft, y + rect.mBottom, rect.getWidth(), rect.getHeight());
        gGL.clear(GL_DEPTH_BUFFER_BIT);
    }
}

/// Writes `scene`'s depth over the bound framebuffer's within each rect of a viewport whose origin is (x, y),
/// wherever it is nearer than what is there, through `program` (interface/copyF.glsl with COPY_DEPTH and
/// DEPTH_ONLY) over `triangle`, the full-screen triangle the final blit is drawn with: the same values it wrote, so
/// the 3D UI drawn after the HUD is hidden by the world as before, and by the HUD where the HUD is in front.
inline void restore(LLGLSLShader& program, LLRenderTarget& scene, LLVertexBuffer& triangle,
                    const std::vector<LLRect>& rects, S32 x, S32 y)
{
    if (rects.empty() || !program.isComplete())
    {
        return;
    }

    LLGLSLShader* previous = LLGLSLShader::sCurBoundShaderPtr;
    {
        LLGLSColorMask mask(false, false);
        LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LESS);
        LLGLDisable blend(GL_BLEND);
        LLGLDisable cull(GL_CULL_FACE);

        program.bind();
        program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &scene);
        triangle.setBuffer();
        for (const LLRect& rect : rects)
        {
            LLGLSScissor scissor(x + rect.mLeft, y + rect.mBottom, rect.getWidth(), rect.getHeight());
            triangle.drawArrays(LLRender::TRIANGLES, 0, 3);
        }
        program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    }

    if (previous)
    {
        previous->bind();
    }
    else
    {
        LLGLSLShader::unbind();
    }
}

} // namespace ALHUDDepth
