/**
 * @file llhudrender.cpp
 * @brief LLHUDRender class implementation
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

#include "llviewerprecompiledheaders.h"

#include "llhudrender.h"

#include "llrender.h"
#include "llgl.h"
#include "llviewercamera.h"
#include "v3math.h"
#include "llquaternion.h"
#include "llfontgl.h"
#include "llglheaders.h"
#include "llviewerwindow.h"
#include "llui.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

void hud_render_utf8text(const std::string &str, const LLVector3 &pos_agent,
                     const LLFontGL &font,
                     const U8 style,
                     const LLFontGL::ShadowType shadow,
                     const F32 x_offset, const F32 y_offset,
                     const LLColor4& color,
                     const bool orthographic)
{
    LLWString wstr(utf8str_to_wstring(str));
    hud_render_text(wstr, pos_agent, font, style, shadow, x_offset, y_offset, color, orthographic);
}

void hud_render_text(const LLWString &wstr, const LLVector3 &pos_agent,
                    const LLFontGL &font,
                    const U8 style,
                    const LLFontGL::ShadowType shadow,
                    const F32 x_offset, const F32 y_offset,
                    const LLColor4& color,
                    const bool orthographic)
{
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    // Do cheap plane culling
    LLVector3 dir_vec = pos_agent - camera->getOrigin();
    dir_vec /= dir_vec.magVec();

    if (wstr.empty() || (!orthographic && dir_vec * camera->getAtAxis() <= 0.f))
    {
        return;
    }

    LLVector3 right_axis;
    LLVector3 up_axis;
    if (orthographic)
    {
        right_axis.setVec(0.f, -1.f / gViewerWindow->getWorldViewHeightScaled(), 0.f);
        up_axis.setVec(0.f, 0.f, 1.f / gViewerWindow->getWorldViewHeightScaled());
    }
    else
    {
        camera->getPixelVectors(pos_agent, up_axis, right_axis);
    }
    LLCoordFrame render_frame = *camera;
    LLQuaternion rot;
    if (!orthographic)
    {
        rot = render_frame.getQuaternion();
        rot = rot * LLQuaternion(-F_PI_BY_TWO, camera->getYAxis());
        rot = rot * LLQuaternion(F_PI_BY_TWO, camera->getXAxis());
    }
    else
    {
        rot = LLQuaternion(-F_PI_BY_TWO, LLVector3(0.f, 0.f, 1.f));
        rot = rot * LLQuaternion(-F_PI_BY_TWO, LLVector3(0.f, 1.f, 0.f));
    }
    F32 angle;
    LLVector3 axis;
    rot.getAngleAxis(&angle, axis);

    LLVector3 render_pos = pos_agent + (x_offset * right_axis) + (y_offset * up_axis);

    //get the render_pos in screen space

    LLRect world_view_rect = gViewerWindow->getWorldViewRectRaw();
    glm::ivec4 viewport(world_view_rect.mLeft, world_view_rect.mBottom, world_view_rect.getWidth(), world_view_rect.getHeight());

    glm::vec3 win_coord = al_project(glm::vec3(render_pos), get_current_modelview(), get_current_projection(), viewport);

    //fonts all render orthographically, set up projection``
    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.pushMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();
    LLUI::pushMatrix();

    gl_state_for_2d(world_view_rect.getWidth(), world_view_rect.getHeight());
    gViewerWindow->setup3DViewport();

    // Split the projected position into an integer UI-pixel matrix translate
    // (LLRender2D::translate stores into LLCoordGL via (S32) cast and
    // discards sub-pixel info) and a sub-pixel residual passed to
    // font.render as the (x, y) origin. The font's per-glyph 8-phase
    // rasterization positions horizontal stems to ~1/8 px, so the string
    // slides smoothly through fractional screen pixels as the camera moves
    // instead of jumping a whole pixel at every integer boundary the matrix
    // snap would otherwise enforce. The previous floorf on x_offset/y_offset
    // above tried to snap along perspective-distorted pixel basis vectors
    // and is replaced by this single screen-space split.
    const F32 ui_x  = (win_coord.x - (F32)world_view_rect.mLeft)   / LLFontGL::sScaleX;
    const F32 ui_y  = (win_coord.y - (F32)world_view_rect.mBottom) / LLFontGL::sScaleY;
    const F32 int_x = floorf(ui_x);
    const F32 int_y = floorf(ui_y);
    const F32 frac_x = ui_x - int_x;
    const F32 frac_y = ui_y - int_y;

    LLUI::loadIdentity();
    gGL.loadIdentity();
    // Place the text at the 3D point's window depth so it occludes against the scene.
    // The 2D ortho (gl_state_for_2d) becomes reversed-ZO under reverse-Z and win_coord.z
    // is the reversed window depth, so the required translate-z is the exact negation.
    const F32 hud_text_z = LLRender::sReverseZ ? ((win_coord.z * 2.f) - 1.f)
                                               : -((win_coord.z * 2.f) - 1.f);
    LLUI::translate(int_x, int_y, hud_text_z);
    F32 right_x;

    font.render(wstr, 0, frac_x, 1.f + frac_y, color, LLFontGL::LEFT, LLFontGL::BASELINE, style, shadow, static_cast<S32>(wstr.length()), 1000, &right_x, /*use_ellipses*/false, /*use_color*/true);

    LLUI::popMatrix();
    gGL.popMatrix();

    gGL.matrixMode(LLRender::MM_PROJECTION);
    gGL.popMatrix();
    gGL.matrixMode(LLRender::MM_MODELVIEW);
}
