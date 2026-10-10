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

#include "llgltfmaterial.h"
#include "llglslshader.h"
#include "llglstates.h"
#include "llimagegl.h"
#include "llmaterial.h"
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

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>

class LLFace;
class LLViewerObject;
class LLVOAvatar;

/// Outlines of the selected, rect-highlighted and media-focused objects, drawn by LLSelectMgr::renderSilhouettes
/// over the window's framebuffer: world objects after the final blit, with the world camera, and HUD objects
/// after the HUD, with its matrices. A frame outlines one or the other, as the selection is in the world or on
/// the HUD.
///
/// The id pass (interface/selectionIdV.glsl, selectionIdF.glsl) draws each object's selected faces, rigged ones
/// through the program's rigged variant, into a window-sized RGBA8 target with a depth buffer of its own, which
/// keeps the nearest selected surface: the object's id in .rg, 16 bits with 0 for none, in .b whether the outline
/// draws the surface (1 where the scene shows it, 0.5 where the scene hides it and hidden parts are drawn dimmed, 0
/// where they are left out), and in .a its priority. Faces are culled as the scene culls them, back faces unless a
/// GLTF material is double-sided, so a face the scene does not draw takes no pixel. Where a face's alpha mode cuts
/// it out by its texture's alpha, the fragments are discarded, so the outline follows what is drawn rather than
/// the cards it is drawn on; the face's colour, whose alpha makes a prim invisible, is not read. Integer formats
/// are not used because LLGLSLShader gives integer samplers no unit.
///
/// Ids are given in priority order, the highest first (EPriority). The jump pass (interface/selectionJumpF.glsl)
/// copies the id target into another, marking the texels on the near side of a jump: a step in an object's own
/// surface between two neighbours, as where one frond of a mesh lies over another. It is told from a crease or a
/// curve, whose step lies between the slopes either side, by a step outside both, which carried on at its own slope
/// each side misses by more than JUMP_MARGIN (in the shader) of the distance.
///
/// The edge pass (interface/selectionOutlineF.glsl) is a full-screen triangle, scissored to the objects' projected
/// boxes grown by the contour's reach. A pixel draws the contour of the drawn surfaces within its reach that stand
/// in front of it: every object, off every object; on an object, another object, or the near side of a jump in its
/// own, whose surface is nearer than the plane of the pixel's own, read from the id target's depth and carried over
/// to the other's texel. The contour is a solid line View::mWidth pixels wide ending in a pixel of anti-aliasing,
/// from the distance to the nearest texel of each object in the disk it reaches: the highest priority's colour
/// within reach over the nearest object's. Where two objects touch, neither in front, the edge between them is
/// drawn thinner (View::mInnerWidth) and fainter (INNER_OPACITY), on the side of the one of higher priority if it is
/// drawn and on the drawn side if not, in that side's colour. An object over its own surface draws nothing but at
/// its jumps: anywhere else that would trace the creases of every mesh. Colours come from a palette holding a
/// visible and a hidden colour per id.
///
/// The wireframe (AlchemySelectionWireframe) is drawn first, the outline over it: the same faces through the same
/// face walk, alpha cut and culling as the id pass (drawFaces), their triangles' edges drawn as lines View::mWireWidth
/// pixels wide from window-space edge distances a geometry stage gives each fragment (selectionWireframeG.glsl,
/// selectionWireframeF.glsl). In the world the window's depth is the scene's, which the final blit wrote: lines in
/// front of it are drawn, pulled in front of their own surface by a polygon offset, and lines behind it in the
/// hidden colour. On the HUD the window's depth also holds the scene's wherever that is the nearer, so the lines
/// are tested against the id target's depth instead, and a line behind another selected surface is hidden.
///
/// The disk a pixel searches grows with the square of the contour's width, and most pixels in the scissor are far
/// from anything to draw. The tile pass (interface/selectionTileF.glsl) records for each TILE_SIZE square of the
/// marked target the lowest and highest nonzero id it holds and whether it holds the near side of a jump, and the
/// edge pass leaves a pixel at once when no tile within its reach holds what it could draw from: any id, off every
/// object; on one, an id not its own or a jump. Whatever a pixel draws, it draws from such a texel within its reach,
/// whose own tile records it, so the early out changes nothing it draws.
///
/// The targets exist only while something is outlined: they are made the first frame there is, given up the
/// first frame there is not, and given up with the pipeline's own (LLPipeline::releaseGLBuffers).
class ALSelectionOutline
{
public:
    /// What an outline is for, the highest priority first. Where two objects' outlines meet, the higher one's
    /// contour is laid over the lower one's, and the edge between them is drawn on its side.
    enum EPriority : U8
    {
        /// What a control-drag takes out of the selection.
        PRIORITY_SUBTRACT = 0,
        /// The object media is focused on.
        PRIORITY_FOCUS,
        /// The object picked in the inspect floater or the object panel's contents.
        PRIORITY_INSPECT,
        /// A selected root, or a root a drag would select.
        PRIORITY_ROOT,
        /// A selected child, or a child a drag would select.
        PRIORITY_CHILD,
        /// A transient selection's.
        PRIORITY_CONTEXT,
        PRIORITY_COUNT
    };

    /// What render() draws with.
    struct View
    {
        /// From the frame object positions are in to the eye, and the eye to clip space.
        LLMatrix4a mModelview;
        LLMatrix4a mProjection;
        /// The widths in pixels of the contour around the objects and of the edges between them, each ending in a
        /// further pixel of anti-aliasing.
        S32 mWidth = 1;
        S32 mInnerWidth = 1;
        /// The HUD: drawn into the window's framebuffer, whose depth cannot be read, so every surface counts as
        /// visible; and its rigged faces are left out, since their joints place them about the avatar in the
        /// world, which the HUD's matrices have no place for.
        bool mHUD = false;
        /// The outline (RenderHighlightSelections), drawn with or without the wireframe.
        bool mOutline = true;
        /// The selected faces' triangles drawn as lines under the outline (AlchemySelectionWireframe), mWireWidth
        /// pixels wide before their pixel of anti-aliasing, with or without the outline.
        bool mWireframe = false;
        S32 mWireWidth = 1;
    };

    /// The passes render() runs for a view.
    struct Passes
    {
        /// The id pass: the outline's ids, and the depth of the selected surfaces the HUD's lines are tested against.
        bool mIds = false;
        /// The wireframe.
        bool mWireframe = false;
        /// The jump, tile and edge passes, which draw the outline from the ids.
        bool mEdges = false;

        bool any() const { return mIds || mWireframe || mEdges; }
    };

    /// The passes `view` needs, the wireframe only where its program loaded (`wireframe_ready`). The world's lines
    /// are tested against the window's depth and need no id pass; the HUD's against the id target's.
    static Passes passesFor(const View& view, bool wireframe_ready);

    /// The world camera's, in the UI stage, where the scene's projection is still loaded.
    static View worldView();

    /// The HUD's, as its attachments were drawn with. False without a HUD to draw.
    static bool hudView(View& view);

    /// A view with these matrices whose contour is `contour_width` pixels wide at a UI scale of 1
    /// (AlchemySelectionOutlineWidth), drawn at `ui_scale`, LLUI's scale factor: the same widths in the world and
    /// on the HUD.
    static View makeView(const LLMatrix4a& modelview, const LLMatrix4a& projection, F32 contour_width, F32 ui_scale,
                         bool hud, bool wireframe = false);

    /// Ids the target can tell apart; 0 is no selected object.
    static constexpr U32 MAX_IDS = 65535;
    /// Ids to a pair of palette rows: their visible colours, then their hidden ones.
    static constexpr U32 PALETTE_WIDTH = 256;
    /// What a hidden part keeps of its outline's opacity.
    static constexpr F32 HIDDEN_ALPHA = 0.4f;
    /// The contour's width in pixels at a UI scale of 1: AlchemySelectionOutlineWidth's default, and the range it
    /// is held to.
    static constexpr F32 DEFAULT_CONTOUR_WIDTH = 2.f;
    static constexpr F32 MIN_CONTOUR_WIDTH = 1.f;
    static constexpr F32 MAX_CONTOUR_WIDTH = 16.f;
    /// The width of the edges between objects in pixels at a UI scale of 1. It does not follow the contour's: the
    /// edges mark where prims meet inside a selection, and as wide as the contour they would cover small prims.
    static constexpr F32 INNER_WIDTH = 1.f;
    /// The side of the id target's squares the tile pass records, in texels (TILE_SIZE in selectionTileF.glsl and
    /// selectionOutlineF.glsl).
    static constexpr U32 TILE_SIZE = 8;
    /// What an edge between two objects keeps of its object's outline opacity (INNER_OPACITY in
    /// selectionOutlineF.glsl).
    static constexpr F32 INNER_OPACITY = 0.6f;
    /// The texture alpha an alpha-blended face is outlined from: where it is drawn at least half opaque.
    static constexpr F32 BLEND_ALPHA_CUTOFF = 0.5f;
    /// A face whose texture is not read: every fragment is outlined.
    static constexpr F32 NO_ALPHA_TEST = -1.f;
    /// The base colour transform of a face whose texture coordinates have their transform in them.
    static constexpr LLGLTFMaterial::TextureTransform::Pack IDENTITY_TRANSFORM = { 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
    /// The wireframe's lines' width in pixels at a UI scale of 1.
    static constexpr F32 WIRE_WIDTH = 1.f;
    /// How far the wireframe's lines are drawn in front of their own surface beyond its slope, in the depth
    /// buffer's least steps (glPolygonOffset's units).
    static constexpr F32 WIRE_OFFSET_UNITS = 4.f;

    /// The uniforms the passes set beyond LLShaderMgr's reserved ones.
    static constexpr const char* ID_UNIFORM = "selection_id";
    static constexpr const char* PRIORITY_UNIFORM = "selection_priority";
    static constexpr const char* SHOW_HIDDEN_UNIFORM = "selection_show_hidden";
    static constexpr const char* SCENE_DEPTH_UNIFORM = "selection_scene_depth";
    static constexpr const char* WIDTH_UNIFORM = "outline_width";
    static constexpr const char* INNER_WIDTH_UNIFORM = "outline_inner_width";
    static constexpr const char* WIRE_PASS_UNIFORM = "wireframe_pass";
    static constexpr const char* WIRE_WIDTH_UNIFORM = "wireframe_width";

    /// What decides which of the wireframe's lines are hidden (wireframe_pass in selectionWireframeF.glsl).
    enum EWirePass : S32
    {
        /// In the world, the lines the window's depth, the scene's, passes as in front: drawn in their visible
        /// colour.
        WIRE_VISIBLE = 0,
        /// In the world, the lines it passes as behind: drawn in their hidden colour, dimmed or left out.
        WIRE_HIDDEN = 1,
        /// On the HUD, where the window's depth also holds the scene's, wherever it is the nearer: the id target's
        /// depth, the nearest selected surface, decides, and a line behind another selected surface is hidden.
        WIRE_ID_DEPTH = 2,
    };

    /// The GL state a wireframe pass draws in while it lives, over the window's framebuffer: colour blended in
    /// over its alpha, no depth written, back faces culled as the id pass culls them (drawFace), and for the
    /// world's passes the depth test, with the lines pulled in front of their own surface by `offset_factor`
    /// (glPolygonOffset's factor, wireOffsetFactor) and WIRE_OFFSET_UNITS. gGL.setPolygonOffset turns the pull
    /// toward the eye under either depth convention.
    class WireState
    {
    public:
        WireState(EWirePass pass, F32 offset_factor);
        ~WireState();

        WireState(const WireState&) = delete;
        WireState& operator=(const WireState&) = delete;

    private:
        LLGLSColorMask mMask{ true, false };
        LLGLEnable mCull{ GL_CULL_FACE };
        LLGLEnable mBlend{ GL_BLEND };
        LLGLDepthTest mDepth;
        LLGLEnable mOffset;
    };

    /// The polygon offset factor the wireframe's lines need in front of a window depth point-sampled from a scene
    /// `resample` window pixels to its texel: a surface's depth there was taken up to that many pixels away along
    /// its slope, and a pixel more for the line's own.
    static F32 wireOffsetFactor(F32 resample) { return llmax(resample, 1.f) + 1.f; }

    /// Lives for the session and is never destroyed: its GL objects go with the pipeline's, and at exit GL is gone.
    static ALSelectionOutline& instance();

    /// Outlines `object` this frame: its faces whose bit is set in te_mask, in `colour`, whose alpha is the
    /// outline's opacity, at `priority`. Parts the scene hides are dimmed when show_hidden and left out otherwise.
    /// Adding an object again replaces what it was added with.
    void add(LLViewerObject* object, U32 te_mask, const LLColor4& colour, bool show_hidden, EPriority priority);

    /// Draws what was added since the last call over the bound framebuffer, with `view`, and forgets it. With
    /// nothing added it gives up its targets.
    void render(const View& view);

    /// Gives up the targets.
    void release();

    /// Palette rows for ids 1 to `count`, and the id 0 that is never drawn.
    static U32 paletteRows(U32 count) { return 2 * (count / PALETTE_WIDTH + 1); }

    /// Writes id's visible and hidden texels into an RGBA8 palette PALETTE_WIDTH texels wide.
    static void writePalette(std::vector<U8>& texels, U32 id, const LLColor4& colour, bool show_hidden);

    /// A line `width` pixels wide at a UI scale of 1, at `ui_scale`: at least a pixel.
    static S32 lineWidth(F32 width, F32 ui_scale) { return llmax(1, ll_round(width * ui_scale)); }

    /// The contour's width in pixels for the setting `width` at `ui_scale`, the setting held to MIN_CONTOUR_WIDTH
    /// to MAX_CONTOUR_WIDTH, and to the least where it is not a number.
    static S32 contourWidth(F32 width, F32 ui_scale);

    /// Tiles across `pixels` texels of the id target.
    static U32 tileCount(U32 pixels) { return (pixels + TILE_SIZE - 1) / TILE_SIZE; }

    /// How far a line `width` pixels wide reaches from the edge it is drawn along: its width and the pixel its
    /// anti-aliasing takes.
    static S32 lineReach(S32 width) { return llmax(width, 1) + 1; }

    /// The order ids are given in, as indices into `priorities`: the highest priority first, and in the order
    /// they were added within one.
    static void priorityOrder(const std::vector<EPriority>& priorities, std::vector<U32>& order);

    /// The texture alpha below which a legacy face is not outlined, or NO_ALPHA_TEST. Its material's alpha mode
    /// decides: masks at their cutoff (0 to 255), blends at BLEND_ALPHA_CUTOFF, none and emissive not at all.
    /// Without a material, a texture with alpha is blended, as the face is drawn.
    static F32 legacyAlphaCutoff(bool has_material, U8 diffuse_alpha_mode, U8 mask_cutoff, bool texture_alpha);

    /// The same for a GLTF face from its material's alpha mode and cutoff.
    static F32 gltfAlphaCutoff(S32 alpha_mode, F32 alpha_cutoff);

    /// The pixels of a width x height target the outline of a box can touch: its projection through `mvp` grown
    /// by `reach`, clipped to the target. A box reaching behind the eye takes the whole target.
    /// False when the box cannot touch the target.
    static bool scissorRect(const LLMatrix4a& mvp, const LLVector4a extents[2], S32 width, S32 height, S32 reach,
                            LLRect& rect);

    /// Makes `palette` hold at least `rows` rows, recreating it when it holds fewer, and uploads them.
    static void uploadPalette(U32& palette, U32& palette_rows, const std::vector<U8>& texels, U32 rows);

    /// Binds what the id pass reads to the bound id program: the target's size, and the scene's depth, which
    /// decides what is hidden; with no scene every surface counts as visible.
    static void bindIdPass(LLGLSLShader& program, LLRenderTarget* scene, S32 width, S32 height);

    /// The GL state the id pass draws in while it lives: colour and depth written, the depth test keeping the
    /// nearest surface, back faces culled as the scene culls them (the main view keeps GL_BACK and
    /// counter-clockwise fronts), no blending and no scissor.
    struct IdPassState
    {
        LLGLSColorMask mMask{ true, true };
        LLGLDepthTest mDepth{ GL_TRUE, GL_TRUE, GL_LEQUAL };
        LLGLEnable mCull{ GL_CULL_FACE };
        LLGLDisable mBlend{ GL_BLEND };
        LLGLDisable mScissor{ GL_SCISSOR_TEST };
    };

    /// The id, priority and hidden parts the bound id program writes for the faces drawn next.
    static void setId(LLGLSLShader& program, U32 id, EPriority priority, bool show_hidden);

    /// Draws a face into the id pass from `buffer`'s bound range, culled as the scene culls it: both sides of a
    /// double-sided GLTF material, the front alone of everything else (LLRenderPass::pushGLTFBatch).
    static void drawFace(LLVertexBuffer& buffer, U32 start, U32 end, U32 count, U32 offset, bool double_sided);

    /// How the bound id program tests the alpha of the face drawn next, whose texture is bound as its diffuse map:
    /// fragments under `cutoff` are discarded, and with NO_ALPHA_TEST the texture is not read. `transform` is the
    /// base colour's texture transform, packed as LLGLTFMaterial::TextureTransform::getPacked packs it, the
    /// identity for a legacy face, whose texture coordinates have their transform in them.
    static void setAlphaTest(LLGLSLShader& program, F32 cutoff, const LLGLTFMaterial::TextureTransform::Pack& transform);

    /// The jump pass with the bound jump program: `ids`, the id target, copied into `marked` with the near sides of
    /// jumps marked, over the texels under `rect`; the others are cleared, as `ids` holds nothing there when the
    /// rect covers every selected texel. Its depth is read through `view`'s projection, which the id pass drew with.
    static void drawJumps(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& marked, const LLRect& rect,
                          const View& view, LLVertexBuffer& triangle);

    /// The tile pass with the bound tile program: into `tiles`, RGBA16, a texel to each TILE_SIZE square of
    /// `marked`, over the tiles under `rect`, a rect of `marked`; the others are cleared, and hold nothing when the
    /// rect covers every selected texel.
    static void drawTiles(LLGLSLShader& program, LLRenderTarget& marked, LLRenderTarget& tiles, const LLRect& rect,
                          LLVertexBuffer& triangle);

    /// Binds what a wireframe pass reads to the bound wireframe program: the pass, the lines' width, the viewport's
    /// size, the palette, and for WIRE_ID_DEPTH the depth of `ids`, the id target.
    static void bindWirePass(LLGLSLShader& program, EWirePass pass, S32 wire_width, S32 width, S32 height, U32 palette,
                             LLRenderTarget* ids);

    /// The edge pass with the bound outline program and `view`'s widths, over the viewport and scissor in force:
    /// ids from `marked`, their depth from `ids` through `view`'s projection, which the id pass drew with, and the
    /// tile pass's record from `tiles`.
    static void drawEdges(LLGLSLShader& program, LLRenderTarget& marked, LLRenderTarget& ids, LLRenderTarget& tiles,
                          U32 palette, const View& view, LLVertexBuffer& triangle);

private:
    ALSelectionOutline() = default;

    // The full-screen triangle with the bound program, `projection` loaded for its draw where given: the triangle
    // takes no matrix, and the passes read depth through the projection's inverse.
    static void drawTriangle(LLVertexBuffer& triangle, const LLMatrix4a* projection);

    // Clears `target`, then draws the full-screen triangle into its texels under `rect`.
    static void drawUnder(LLRenderTarget& target, const LLRect& rect, LLVertexBuffer& triangle,
                          const LLMatrix4a* projection);

    struct Entry
    {
        LLViewerObject* mObject;
        U32             mTEMask;
        LLColor4        mColour;
        bool            mShowHidden;
        EPriority       mPriority;
    };

    // The box the faces a walk drew cover, in the frame the view's modelview takes: unbounded where a rigged face's
    // joints have not been measured yet.
    struct FaceBox
    {
        LLVector4a mLo{ FLT_MAX, FLT_MAX, FLT_MAX };
        LLVector4a mHi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool mBounded = true;
    };

    // The faces of `entry` that are rigged (or not) drawn into the bound program as the scene draws them, for the id
    // pass and the wireframe alike: each in the frame LLVolumeGeometryManager::registerFace places it, a rigged one
    // through its joint palette, with its alpha cut and culling (bindFace, drawFace). `begin` is called once,
    // before the first face, with the object's matrices loaded. False when no face was drawn.
    template <typename Begin>
    bool drawFaces(LLGLSLShader& program, const Entry& entry, bool rigged, const View& view, FaceBox& box, Begin&& begin);

    // The faces of `entry` that are rigged (or not) into the bound id program as `id`, and the box they cover into
    // mScissor.
    void drawObject(LLGLSLShader& program, const Entry& entry, U32 id, bool rigged, const View& view,
                    const LLMatrix4a& mvp);

    // The id pass: what was added into mIdMap, a width x height target, in priority order, and the box it covers,
    // through `mvp`, into mScissor.
    void drawIds(const View& view, S32 width, S32 height, const LLMatrix4a& mvp);

    // The wireframe of what was added, over the bound framebuffer under the outline: the world's visible lines and
    // its hidden ones against the window's depth, the HUD's against the id target's.
    void drawWireframes(const View& view, S32 width, S32 height);

    // What a face is drawn with beyond its geometry.
    struct FaceBinding
    {
        // A texture animation was loaded, which the caller unloads after the draw.
        bool mAnimated = false;
        // Its GLTF material is double-sided: the scene culls neither side.
        bool mDoubleSided = false;
    };

    // Binds `face`'s texture and alpha test to the bound program and loads its texture animation, if it is drawn
    // with one.
    static FaceBinding bindFace(LLGLSLShader& program, LLFace* face);

    void clearEntries();

    std::vector<Entry> mEntries;
    boost::unordered_flat_map<const LLViewerObject*, U32> mEntryIndex;
    std::vector<EPriority> mPriorities;
    std::vector<U32> mOrder;
    std::vector<U8> mPaletteTexels;

    LLRenderTarget mIdMap;
    LLRenderTarget mMarkedMap;
    LLRenderTarget mTileMap;
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

inline S32 ALSelectionOutline::contourWidth(F32 width, F32 ui_scale)
{
    // Asked outright: /fp:fast may answer a comparison with a NaN either way.
    const F32 held = llisnan(width) ? MIN_CONTOUR_WIDTH : llclamp(width, MIN_CONTOUR_WIDTH, MAX_CONTOUR_WIDTH);
    return lineWidth(held, ui_scale);
}

inline ALSelectionOutline::View ALSelectionOutline::makeView(const LLMatrix4a& modelview, const LLMatrix4a& projection,
                                                             F32 contour_width, F32 ui_scale, bool hud, bool wireframe)
{
    View view;
    view.mModelview = modelview;
    view.mProjection = projection;
    view.mWidth = contourWidth(contour_width, ui_scale);
    view.mInnerWidth = lineWidth(INNER_WIDTH, ui_scale);
    view.mHUD = hud;
    view.mWireframe = wireframe;
    view.mWireWidth = lineWidth(WIRE_WIDTH, ui_scale);
    return view;
}

inline ALSelectionOutline::Passes ALSelectionOutline::passesFor(const View& view, bool wireframe_ready)
{
    Passes passes;
    passes.mWireframe = view.mWireframe && wireframe_ready;
    passes.mEdges = view.mOutline;
    passes.mIds = view.mOutline || (passes.mWireframe && view.mHUD);
    return passes;
}

inline void ALSelectionOutline::priorityOrder(const std::vector<EPriority>& priorities, std::vector<U32>& order)
{
    order.resize(priorities.size());
    for (U32 i = 0; i < (U32)order.size(); ++i)
    {
        order[i] = i;
    }
    std::stable_sort(order.begin(), order.end(), [&priorities](U32 a, U32 b) { return priorities[a] < priorities[b]; });
}

inline F32 ALSelectionOutline::legacyAlphaCutoff(bool has_material, U8 diffuse_alpha_mode, U8 mask_cutoff, bool texture_alpha)
{
    if (!texture_alpha)
    {
        return NO_ALPHA_TEST;
    }
    if (!has_material)
    {
        return BLEND_ALPHA_CUTOFF;
    }
    switch (diffuse_alpha_mode)
    {
        case LLMaterial::DIFFUSE_ALPHA_MODE_BLEND:
            return BLEND_ALPHA_CUTOFF;
        case LLMaterial::DIFFUSE_ALPHA_MODE_MASK:
            return (F32)mask_cutoff / 255.f;
        default:
            return NO_ALPHA_TEST;
    }
}

inline F32 ALSelectionOutline::gltfAlphaCutoff(S32 alpha_mode, F32 alpha_cutoff)
{
    switch (alpha_mode)
    {
        case LLGLTFMaterial::ALPHA_MODE_BLEND:
            return BLEND_ALPHA_CUTOFF;
        case LLGLTFMaterial::ALPHA_MODE_MASK:
            return alpha_cutoff;
        default:
            return NO_ALPHA_TEST;
    }
}

inline bool ALSelectionOutline::scissorRect(const LLMatrix4a& mvp, const LLVector4a extents[2], S32 width, S32 height,
                                            S32 reach, LLRect& rect)
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
    const F32 limit = (F32)(llmax(width, height) + reach + 1);
    min_x = llclamp(min_x, -limit, limit);
    min_y = llclamp(min_y, -limit, limit);
    max_x = llclamp(max_x, -limit, limit);
    max_y = llclamp(max_y, -limit, limit);

    rect.set(llfloor(min_x) - reach, llceil(max_y) + reach, llceil(max_x) + reach, llfloor(min_y) - reach);
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

inline void ALSelectionOutline::setId(LLGLSLShader& program, U32 id, EPriority priority, bool show_hidden)
{
    static const LLStaticHashedString id_uniform(ID_UNIFORM);
    static const LLStaticHashedString priority_uniform(PRIORITY_UNIFORM);
    static const LLStaticHashedString show_hidden_uniform(SHOW_HIDDEN_UNIFORM);
    program.uniform1i(id_uniform, (GLint)id);
    program.uniform1i(priority_uniform, (GLint)priority);
    program.uniform1i(show_hidden_uniform, show_hidden ? 1 : 0);
}

inline void ALSelectionOutline::drawFace(LLVertexBuffer& buffer, U32 start, U32 end, U32 count, U32 offset,
                                         bool double_sided)
{
    LLGLDisable no_cull(double_sided ? GL_CULL_FACE : 0);
    buffer.setBuffer();
    buffer.drawRange(LLRender::TRIANGLES, start, end, count, offset);
}

inline ALSelectionOutline::WireState::WireState(EWirePass pass, F32 offset_factor)
:   mDepth(pass != WIRE_ID_DEPTH ? GL_TRUE : GL_FALSE, GL_FALSE, pass == WIRE_HIDDEN ? GL_GREATER : GL_LEQUAL),
    mOffset(pass != WIRE_ID_DEPTH ? GL_POLYGON_OFFSET_FILL : 0)
{
    gGL.setSceneBlendType(LLRender::BT_ALPHA);
    // Toward the eye in forward depth's terms, which gGL turns for reverse-Z.
    gGL.setPolygonOffset(-offset_factor, -WIRE_OFFSET_UNITS);
}

inline ALSelectionOutline::WireState::~WireState()
{
    gGL.setPolygonOffset(0.f, 0.f);
}

inline void ALSelectionOutline::bindWirePass(LLGLSLShader& program, EWirePass pass, S32 wire_width, S32 width, S32 height,
                                             U32 palette, LLRenderTarget* ids)
{
    static const LLStaticHashedString pass_uniform(WIRE_PASS_UNIFORM);
    static const LLStaticHashedString width_uniform(WIRE_WIDTH_UNIFORM);

    program.uniform1i(pass_uniform, (GLint)pass);
    program.uniform1i(width_uniform, llmax(wire_width, 1));
    program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)width, (F32)height);
    const S32 channel = program.enableTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, palette, gGL.getSampler(ALSamplers::PointClamp));
    }
    if (ids && pass == WIRE_ID_DEPTH)
    {
        program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, ids);
    }
    else
    {
        // Not read; left empty rather than holding whatever the unit last had.
        program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    }
}

inline void ALSelectionOutline::setAlphaTest(LLGLSLShader& program, F32 cutoff,
                                             const LLGLTFMaterial::TextureTransform::Pack& transform)
{
    program.setMinimumAlpha(cutoff);
    program.uniform4fv(LLShaderMgr::TEXTURE_BASE_COLOR_TRANSFORM, 2, transform);
}

inline void ALSelectionOutline::drawTriangle(LLVertexBuffer& triangle, const LLMatrix4a* projection)
{
    if (projection)
    {
        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.pushMatrix();
        gGL.loadMatrix(*projection);
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }

    triangle.setBuffer();
    triangle.drawArrays(LLRender::TRIANGLES, 0, 3);

    if (projection)
    {
        gGL.matrixMode(LLRender::MM_PROJECTION);
        gGL.popMatrix();
        gGL.matrixMode(LLRender::MM_MODELVIEW);
    }
}

inline void ALSelectionOutline::drawUnder(LLRenderTarget& target, const LLRect& rect, LLVertexBuffer& triangle,
                                          const LLMatrix4a* projection)
{
    target.bindTarget();
    {
        LLGLSColorMask mask(true, true);
        LLGLDepthTest depth(GL_FALSE);
        LLGLDisable blend(GL_BLEND);
        {
            LLGLDisable scissor(GL_SCISSOR_TEST);
            gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
            target.clear();
        }

        LLRect under = rect;
        under.intersectWith(LLRect(0, (S32)target.getHeight(), (S32)target.getWidth(), 0));
        if (under.notEmpty())
        {
            LLGLSScissor scissor(under.mLeft, under.mBottom, under.getWidth(), under.getHeight());
            drawTriangle(triangle, projection);
        }
    }
    target.flush();
}

inline void ALSelectionOutline::drawJumps(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& marked,
                                          const LLRect& rect, const View& view, LLVertexBuffer& triangle)
{
    program.bindTexture(LLShaderMgr::DIFFUSE_MAP, &ids, ALSamplers::PointClamp);
    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &ids);
    drawUnder(marked, rect, triangle, &view.mProjection);
    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
}

inline void ALSelectionOutline::drawTiles(LLGLSLShader& program, LLRenderTarget& marked, LLRenderTarget& tiles,
                                          const LLRect& rect, LLVertexBuffer& triangle)
{
    const S32 left = llmax(rect.mLeft, 0) / (S32)TILE_SIZE;
    const S32 bottom = llmax(rect.mBottom, 0) / (S32)TILE_SIZE;
    const S32 right = (S32)tileCount((U32)llmax(rect.mRight, 0));
    const S32 top = (S32)tileCount((U32)llmax(rect.mTop, 0));

    // The tile program reads no depth, and needs no projection.
    program.bindTexture(LLShaderMgr::DIFFUSE_MAP, &marked, ALSamplers::PointClamp);
    drawUnder(tiles, LLRect(left, top, right, bottom), triangle, nullptr);
    program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
}

inline void ALSelectionOutline::drawEdges(LLGLSLShader& program, LLRenderTarget& marked, LLRenderTarget& ids,
                                          LLRenderTarget& tiles, U32 palette, const View& view, LLVertexBuffer& triangle)
{
    static const LLStaticHashedString width_uniform(WIDTH_UNIFORM);
    static const LLStaticHashedString inner_width_uniform(INNER_WIDTH_UNIFORM);

    // Point sampled all four: the ids, their depth, the tiles and the palette are data, and texelFetch reads them
    // whatever the filter.
    program.bindTexture(LLShaderMgr::DIFFUSE_MAP, &marked, ALSamplers::PointClamp);
    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &ids);
    program.bindTexture(LLShaderMgr::SPECULAR_MAP, &tiles, ALSamplers::PointClamp);
    const S32 channel = program.enableTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, palette, gGL.getSampler(ALSamplers::PointClamp));
    }
    program.uniform1i(width_uniform, llmax(view.mWidth, 1));
    program.uniform1i(inner_width_uniform, llmax(view.mInnerWidth, 1));

    drawTriangle(triangle, &view.mProjection);

    program.unbindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP);
    program.unbindTexture(LLShaderMgr::SPECULAR_MAP);
    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    program.unbindTexture(LLShaderMgr::DIFFUSE_MAP);
}
