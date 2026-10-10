/**
 * @file alselectionoutline.h
 * @brief Outlines of selected world objects, drawn on the GPU from an id target in the UI stage.
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

#include "llglslshader.h"
#include "llimagegl.h"
#include "llmath.h"
#include "llmatrix4a.h"
#include "llrect.h"
#include "llrender.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llstaticstringtable.h"
#include "llvertexbuffer.h"
#include "v4color.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <cfloat>
#include <cmath>
#include <vector>

class LLViewerObject;
class LLVOAvatar;

/// Outlines of the selected, rect-highlighted and media-focused objects, drawn by LLSelectMgr::renderSilhouettes
/// over the window's framebuffer: world objects after the final blit, with the world camera, and HUD objects
/// after the HUD, with its matrices. A frame outlines one or the other, as the selection is in the world or on
/// the HUD.
///
/// The id pass (interface/selectionIdF.glsl) draws each object's selected faces, rigged ones through the
/// program's rigged variant, into a window-sized RGBA8 target with a depth buffer of its own, which keeps the
/// nearest selected surface: the object's id in .rg, 16 bits with 0 for none, and in .b whether the scene's
/// depth shows the surface. Integer formats are not used because LLGLSLShader gives integer samplers no unit.
///
/// The edge pass (interface/selectionOutlineF.glsl) is a full-screen triangle, scissored to the objects'
/// projected boxes grown by the outline's radius. It colours every pixel within the radius of a different id
/// from a palette holding a visible and a hidden colour per id: outside an object along its contour, and on both
/// sides of the edge between two objects, so every prim keeps its own outline. Hidden parts are dimmed or left
/// out per object.
///
/// The targets exist only while something is outlined: they are made the first frame there is, given up the
/// first frame there is not, and given up with the pipeline's own (LLPipeline::releaseGLBuffers).
class ALSelectionOutline
{
public:
    /// What render() draws with.
    struct View
    {
        /// From the frame object positions are in to the eye, and the eye to clip space.
        LLMatrix4a mModelview;
        LLMatrix4a mProjection;
        /// The outline's width in pixels.
        S32 mRadius = 1;
        /// The HUD: drawn into the window's framebuffer, whose depth cannot be read, so every surface counts as
        /// visible; and its rigged faces are left out, since their joints place them about the avatar in the
        /// world, which the HUD's matrices have no place for.
        bool mHUD = false;
    };

    /// The world camera's, in the UI stage, where the scene's projection is still loaded.
    static View worldView();

    /// The HUD's, as its attachments were drawn with. False without a HUD to draw.
    static bool hudView(View& view);

    /// Ids the target can tell apart; 0 is no selected object.
    static constexpr U32 MAX_IDS = 65535;
    /// Ids to a pair of palette rows: their visible colours, then their hidden ones.
    static constexpr U32 PALETTE_WIDTH = 256;
    /// Most rings of eight taps the edge pass reaches its radius with, however wide the outline.
    static constexpr S32 MAX_RINGS = 8;
    /// What a hidden part keeps of its outline's opacity.
    static constexpr F32 HIDDEN_ALPHA = 0.4f;

    /// The uniforms the passes set beyond LLShaderMgr's reserved ones.
    static constexpr const char* ID_UNIFORM = "selection_id";
    static constexpr const char* SCENE_DEPTH_UNIFORM = "selection_scene_depth";
    static constexpr const char* RADIUS_UNIFORM = "outline_radius";
    static constexpr const char* RINGS_UNIFORM = "outline_rings";

    /// Lives for the session and is never destroyed: its GL objects go with the pipeline's, and at exit GL is gone.
    static ALSelectionOutline& instance();

    /// Outlines `object` this frame: its faces whose bit is set in te_mask, in `colour`, whose alpha is the
    /// outline's opacity. Parts the scene hides are dimmed when show_hidden and left out otherwise. Adding an
    /// object again replaces what it was added with.
    void add(LLViewerObject* object, U32 te_mask, const LLColor4& colour, bool show_hidden);

    /// Draws what was added since the last call over the bound framebuffer, with `view`, and forgets it. With
    /// nothing added it gives up its targets.
    void render(const View& view);

    /// Gives up the targets.
    void release();

    /// Palette rows for ids 1 to `count`, and the id 0 that is never drawn.
    static U32 paletteRows(U32 count) { return 2 * (count / PALETTE_WIDTH + 1); }

    /// Writes id's visible and hidden texels into an RGBA8 palette PALETTE_WIDTH texels wide.
    static void writePalette(std::vector<U8>& texels, U32 id, const LLColor4& colour, bool show_hidden);

    /// The outline's width in pixels for a view `view_height` pixels tall: `thickness` of the distance across the
    /// line of sight, widened by the zoom (fov / default_fov), seen from that distance, so no distance changes it.
    static S32 radiusPixels(F32 thickness, F32 view_height, F32 fov, F32 default_fov);

    /// The outline's width in pixels on the HUD, whose projection is a unit high and whose zoom scales its
    /// objects: `thickness` of the view's height.
    static S32 hudRadiusPixels(F32 thickness, F32 view_height) { return llmax(1, ll_round(thickness * view_height)); }

    /// Rings the edge pass taps to reach `radius`: one a pixel up to MAX_RINGS.
    static S32 ringCount(S32 radius) { return llclamp(radius, 1, MAX_RINGS); }

    /// The pixels of a width x height target the outline of a box can touch: its projection through `mvp` grown
    /// by `radius`, clipped to the target. A box reaching behind the eye takes the whole target.
    /// False when the box cannot touch the target.
    static bool scissorRect(const LLMatrix4a& mvp, const LLVector4a extents[2], S32 width, S32 height, S32 radius,
                            LLRect& rect);

    /// Makes `palette` hold at least `rows` rows, recreating it when it holds fewer, and uploads them.
    static void uploadPalette(U32& palette, U32& palette_rows, const std::vector<U8>& texels, U32 rows);

    /// Binds what the id pass reads to the bound id program: the target's size, and the scene's depth, which
    /// decides what is hidden; with no scene every surface counts as visible.
    static void bindIdPass(LLGLSLShader& program, LLRenderTarget* scene, S32 width, S32 height);

    /// The id the bound id program writes for the faces drawn next.
    static void setId(LLGLSLShader& program, U32 id);

    /// The edge pass with the bound outline program, over the viewport and scissor in force.
    static void drawEdges(LLGLSLShader& program, LLRenderTarget& ids, U32 palette, S32 radius,
                          LLVertexBuffer& triangle);

private:
    ALSelectionOutline() = default;

    struct Entry
    {
        LLViewerObject* mObject;
        U32             mTEMask;
        LLColor4        mColour;
        bool            mShowHidden;
    };

    // The faces of entry id - 1 that are rigged (or not) into the bound id program, and the box they cover into
    // mScissor.
    void drawObject(LLGLSLShader& program, const Entry& entry, U32 id, bool rigged, const View& view,
                    const LLMatrix4a& mvp);

    void clearEntries();

    std::vector<Entry> mEntries;
    boost::unordered_flat_map<const LLViewerObject*, U32> mEntryIndex;
    std::vector<U8> mPaletteTexels;

    LLRenderTarget mIdMap;
    U32 mPalette = 0;
    U32 mPaletteRows = 0;

    // The pixels this frame's outlines can touch, and whether any can.
    LLRect mScissor;
    bool mScissorValid = false;

    // Joint palettes uploaded to the rigged id program this frame, so a mesh's faces upload theirs once.
    const LLVOAvatar* mLastAvatar = nullptr;
    U64 mLastMeshId = 0;
    bool mSkipLastSkin = false;
};

inline void ALSelectionOutline::writePalette(std::vector<U8>& texels, U32 id, const LLColor4& colour, bool show_hidden)
{
    const U32 x = id % PALETTE_WIDTH;
    const U32 row = (id / PALETTE_WIDTH) * 2;
    const size_t visible = ((size_t)row * PALETTE_WIDTH + x) * 4;
    const size_t hidden = visible + (size_t)PALETTE_WIDTH * 4;
    if (hidden + 4 > texels.size())
    {
        return;
    }

    auto unorm = [](F32 v) { return (U8)llclamp(ll_round(v * 255.f), 0, 255); };
    for (U32 c = 0; c < 3; ++c)
    {
        texels[visible + c] = unorm(colour.mV[c]);
        texels[hidden + c] = texels[visible + c];
    }
    texels[visible + 3] = unorm(colour.mV[VALPHA]);
    texels[hidden + 3] = show_hidden ? unorm(colour.mV[VALPHA] * HIDDEN_ALPHA) : 0;
}

inline S32 ALSelectionOutline::radiusPixels(F32 thickness, F32 view_height, F32 fov, F32 default_fov)
{
    // thickness * distance * (fov / default_fov) across the line of sight, and the projection puts
    // view_height / (2 tan(fov / 2)) pixels to a unit of that at unit distance.
    if (!(fov > 0.f) || !(default_fov > 0.f))
    {
        return 1;
    }
    const F32 width = thickness * view_height * fov / (default_fov * 2.f * tanf(fov * 0.5f));
    return llmax(1, ll_round(width));
}

inline bool ALSelectionOutline::scissorRect(const LLMatrix4a& mvp, const LLVector4a extents[2], S32 width, S32 height,
                                            S32 radius, LLRect& rect)
{
    const LLRect target(0, height, width, 0);
    F32 min_x = FLT_MAX;
    F32 min_y = FLT_MAX;
    F32 max_x = -FLT_MAX;
    F32 max_y = -FLT_MAX;
    U32 behind = 0;
    for (U32 i = 0; i < 8; ++i)
    {
        const LLVector4a corner(extents[i & 1][0], extents[(i >> 1) & 1][1], extents[(i >> 2) & 1][2], 1.f);
        LLVector4a clip;
        mvp.transform4(corner, clip);
        if (clip[3] <= 1e-6f)
        {
            ++behind;
            continue;
        }
        const F32 x = (clip[0] / clip[3] * 0.5f + 0.5f) * (F32)width;
        const F32 y = (clip[1] / clip[3] * 0.5f + 0.5f) * (F32)height;
        min_x = llmin(min_x, x);
        min_y = llmin(min_y, y);
        max_x = llmax(max_x, x);
        max_y = llmax(max_y, y);
    }

    if (behind == 8)
    {
        return false;
    }
    if (behind > 0)
    {
        rect = target;
        return true;
    }

    // Projections far off screen are clamped before they become integers.
    const F32 limit = (F32)(llmax(width, height) + radius + 1);
    min_x = llclamp(min_x, -limit, limit);
    min_y = llclamp(min_y, -limit, limit);
    max_x = llclamp(max_x, -limit, limit);
    max_y = llclamp(max_y, -limit, limit);

    rect.set(llfloor(min_x) - radius, llceil(max_y) + radius, llceil(max_x) + radius, llfloor(min_y) - radius);
    rect.intersectWith(target);
    return rect.notEmpty();
}

inline void ALSelectionOutline::uploadPalette(U32& palette, U32& palette_rows, const std::vector<U8>& texels, U32 rows)
{
    const U32 target = ALTextureSlot::getInternalType(ALTextureSlot::TT_TEXTURE);
    if (!palette || palette_rows < rows)
    {
        if (palette)
        {
            LLImageGL::deleteTextures(1, &palette);
        }
        LLImageGL::generateTextures(1, &palette);
        gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, palette);
        LLImageGL::allocateTexture2D(target, GL_RGBA8, PALETTE_WIDTH, rows, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        palette_rows = rows;
    }
    else
    {
        gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, palette);
    }
    // setManualSubImage writes whatever is bound on the active unit, slot 0 here.
    LLImageGL::setManualSubImage(target, 0, PALETTE_WIDTH, rows, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
    gGL.getTextureSlot(0)->unbind();
}

inline void ALSelectionOutline::bindIdPass(LLGLSLShader& program, LLRenderTarget* scene, S32 width, S32 height)
{
    static const LLStaticHashedString scene_depth_uniform(SCENE_DEPTH_UNIFORM);

    if (scene)
    {
        program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, scene);
    }
    else
    {
        // Not read without a scene; left empty rather than holding whatever the unit last had.
        program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    }
    program.uniform1i(scene_depth_uniform, scene ? 1 : 0);
    program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)width, (F32)height);
}

inline void ALSelectionOutline::setId(LLGLSLShader& program, U32 id)
{
    static const LLStaticHashedString id_uniform(ID_UNIFORM);
    program.uniform1i(id_uniform, (GLint)id);
}

inline void ALSelectionOutline::drawEdges(LLGLSLShader& program, LLRenderTarget& ids, U32 palette, S32 radius,
                                          LLVertexBuffer& triangle)
{
    static const LLStaticHashedString radius_uniform(RADIUS_UNIFORM);
    static const LLStaticHashedString rings_uniform(RINGS_UNIFORM);

    // Point sampled both: the ids and the palette are data, and texelFetch reads them whatever the filter.
    program.bindTexture(LLShaderMgr::DIFFUSE_MAP, &ids, ALSamplers::PointClamp);
    const S32 channel = program.enableTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, palette, gGL.getSampler(ALSamplers::PointClamp));
    }
    program.uniform1i(radius_uniform, llmax(radius, 1));
    program.uniform1i(rings_uniform, ringCount(radius));

    triangle.setBuffer();
    triangle.drawArrays(LLRender::TRIANGLES, 0, 3);

    program.unbindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP);
    program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
}
