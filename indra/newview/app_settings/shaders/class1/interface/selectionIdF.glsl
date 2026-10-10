/**
 * @file selectionIdF.glsl
 * @brief The selection outline's id pass (ALSelectionOutline): a selected object's id and priority, and whether
 *        the scene shows the surface, where the face's texture does not cut it out.
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

// .rg: the id, low byte then high byte; .b: whether the outline draws this surface, 1 where the scene shows it,
// 0.5 where the scene hides it and the object's hidden parts are drawn dimmed, 0 where they are left out; .a: the
// object's priority.
out vec4 frag_color;

in vec2 vary_texcoord0;

// The object's id, 1 to 65535. 0 is what the target is cleared to: no selected object.
uniform int selection_id;

// The object's priority (ALSelectionOutline::EPriority), 0 the highest.
uniform int selection_priority;

// 1 where the object's hidden parts are drawn dimmed, 0 where they are left out.
uniform int selection_show_hidden;

// 1 where the scene's depth decides what is hidden. 0 where it cannot be read, as on the HUD, which is drawn into
// the window's framebuffer: every surface then counts as visible.
uniform int selection_scene_depth;

// The scene's depth. It is at render resolution, which need not be this target's, so it is read by uv.
uniform sampler2D depthMap;

// This target's size in pixels.
uniform vec2 screen_res;

// How far in front of this surface the scene's may lie, as a fraction of the distance, and still be this
// surface: float noise between two draws of the same triangle.
const float VISIBLE_SLACK = 0.001;

// selectionUtilF.glsl, through the projection the scene and this pass share.
float eyeDistance(float depth);

// selectionAlphaF.glsl
float faceAlpha(vec2 texcoord);
bool isCutOut(float alpha);

void main()
{
    // The slope is taken outside the branch, where derivatives are defined whatever it does.
    float own = eyeDistance(gl_FragCoord.z);
    float slope = fwidth(own);
    float alpha = faceAlpha(vary_texcoord0);

    float visible = 1.0;
    if (selection_scene_depth != 0)
    {
        float scene = eyeDistance(texture(depthMap, gl_FragCoord.xy / screen_res).r);

        // The point-sampled scene texel is up to half of itself away from this pixel's centre, so where it is
        // coarser than this target the same surface can read nearer by that much of its own slope.
        vec2 texels = screen_res / vec2(textureSize(depthMap, 0));
        float reach = 0.5 * max(max(texels.x, texels.y), 1.0) * slope;

        visible = (own <= scene + reach + own * VISIBLE_SLACK) ? 1.0 : 0.0;
    }

    if (isCutOut(alpha))
    {
        discard;
    }

    float drawn = (visible > 0.5) ? 1.0 : ((selection_show_hidden != 0) ? 0.5 : 0.0);
    frag_color = vec4(float(selection_id & 255) / 255.0, float((selection_id >> 8) & 255) / 255.0, drawn,
                      float(selection_priority) / 255.0);
}
