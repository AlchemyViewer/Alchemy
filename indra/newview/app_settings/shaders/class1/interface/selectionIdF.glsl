/**
 * @file selectionIdF.glsl
 * @brief The selection outline's id pass (ALSelectionOutline): a selected object's id, and whether the scene
 *        shows the surface.
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

// Shared matrix stack + derived matrices, spliced from
// class1/deferred/matricesBlock.glsl and bound at UB_MATRICES.
//[ENGINE_BLOCK Matrices]

// .rg: the id, low byte then high byte; .b: 1 where the scene shows this surface, 0 where it hides it.
out vec4 frag_color;

// The object's id, 1 to 65535. 0 is what the target is cleared to: no selected object.
uniform int selection_id;

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

// Distance in front of the eye of a stored depth, through the projection the scene and this pass share.
// The cleared depth of an infinite projection unprojects to w = 0, floored as deferredUtil.glsl floors it.
float eyeDistance(float depth)
{
#ifdef REVERSE_Z
    float ndc_z = depth;
#else
    float ndc_z = depth * 2.0 - 1.0;
#endif
    vec4 pos = inv_proj * vec4(0.0, 0.0, ndc_z, 1.0);
    return -pos.z / max(pos.w, 0.000001);
}

void main()
{
    // The slope is taken outside the branch, where derivatives are defined whatever it does.
    float own = eyeDistance(gl_FragCoord.z);
    float slope = fwidth(own);

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

    frag_color = vec4(float(selection_id & 255) / 255.0, float((selection_id >> 8) & 255) / 255.0, visible, 1.0);
}
