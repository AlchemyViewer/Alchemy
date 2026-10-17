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

#include "indra_constants.h"
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
#include "llsingleton.h"
#include "lluuid.h"
#include "llvertexbuffer.h"
#include "v4color.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>
#include <vector>

class LLFace;
class LLViewerObject;
class LLVOAvatar;

/// Outlines of the selected, rect-highlighted and media-focused objects, and the glow of what the pointer is over
/// (ALHoverGlow), drawn by LLSelectMgr::renderSilhouettes over the window's framebuffer: world objects after the
/// final blit, with the world camera, and HUD objects after the HUD, with its matrices. Each object asks for its
/// contour, its wireframe or both, as their settings are, or glows (PRIORITY_HOVER); the passes a frame runs are
/// the ones its objects ask for (passesFor).
///
/// The id pass (interface/selectionIdV.glsl, selectionIdF.glsl) draws each object's selected faces, rigged ones
/// through the program's rigged variant, into a window-sized RGBA8 target with a depth buffer of its own, which
/// keeps the nearest selected surface: the object's id in .rg, 16 bits with 0 for none, in .b whether the outline
/// draws the surface (1 where the scene shows it, 0.5 where the scene hides it and hidden parts are drawn dimmed, 0
/// where they are left out), and in .a its priority. Faces are culled as the scene culls them, back faces unless a
/// GLTF material is double-sided, so a face the scene does not draw takes no pixel. Where a face's alpha mode cuts
/// it out by its texture's alpha, the fragments are discarded, so the outline follows what is drawn rather than
/// the cards it is drawn on. While the view asks for whole faces (View::mWholeFaces) they are kept instead, as
/// surface the scene does not show, and so is every fragment of a face the scene shows none of: a face at 100%
/// transparency, which has geometry while its object is selected (LLVOVolume::isInvisibleFace), one wearing the
/// library's transparent texture, and one whose texture has nothing its cut would keep (faceAlphaTest). Such surface
/// is written at the cleared depth, behind every surface the scene shows, so it never hides the outline of a shown
/// one behind it, as an invisible root's box would its children's; and every object counts as in front of it.
/// Integer formats are not used because LLGLSLShader gives integer samplers no unit.
///
/// Ids are given in priority order, the highest first (EPriority). The jump pass (interface/selectionJumpF.glsl)
/// marks, in a one-byte target of its own, the texels of the id target on the near side of a jump: a step in an
/// object's own surface between two neighbours, as where one frond of a mesh lies over another. It is told from a
/// crease or a curve, whose step lies between the slopes either side, by a step outside both, which carried on at
/// its own slope each side misses by more than JUMP_MARGIN (in the shader) of the distance.
///
/// The edge pass (interface/selectionOutlineF.glsl) is a full-screen triangle, scissored to the objects' projected
/// boxes grown by the reach of their contour or glow. A pixel draws the contour of the drawn surfaces within its reach that stand
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
/// An object at PRIORITY_HOVER is not outlined but glows: a soft halo around its visible surface reaching
/// View::mGlowRadius pixels past it (glowOpacity), over what lies behind it as the contour is and never over the
/// object itself, under every other object's contour and edges. Its hidden parts are left out, and it draws no edge
/// with what it touches: the outline of a selected object beside it is drawn as though it were not there, except
/// where it stands in front.
///
/// A frame walks what was added once (collectFaces): each object's selected faces with geometry, as the scene draws
/// them, unless the box they cover, grown by what reaches past it, cannot touch the window. The id pass and the
/// wireframe draw that list, sorted so that the draws share the matrix, joint palette, vertex buffer and texture
/// transform before them where they can, the lowest priority first. The boxes give the rects the later passes run
/// over (mergeRects): projected with their part behind the eye cut away, the overlapping joined, a handful at most.
///
/// The wireframe (AlchemySelectionWireframe) is drawn first, the outline over it: the same faces with the same
/// alpha cut and culling as the id pass, their triangles' edges drawn as lines View::mWireWidth
/// pixels wide from window-space edge distances a geometry stage gives each fragment (selectionWireframeG.glsl,
/// selectionWireframeF.glsl). In the world the window's depth is the scene's, which the final blit wrote: lines in
/// front of it are drawn, pulled in front of their own surface by a polygon offset, and lines behind it in the
/// hidden colour. On the HUD the window's depth also holds the scene's wherever that is the nearer, so the lines
/// are tested against the id target's depth instead, and a line behind another selected surface is hidden.
///
/// Lines are kept apart from what lies behind them as View::mContrast says (AlchemySelectionOutlineContrast). Under
/// CONTRAST_RIM the contour keeps the palette's colour, which says what the object is to the selection, and is cased
/// by a rim outside it, dark around a light line and light around a dark one: on any backdrop one of the two stands
/// out. Under CONTRAST_SHIFT, before the wireframe and the outline draw, the bound framebuffer under them is copied at
/// 1 / BACKDROP_SCALE of its size (backdropRect), and the texels showing a selected surface are marked
/// (drawBackdropMask). Where a contour or an edge is drawn, the edge pass reads the mean OKLab colour of the unmarked
/// texels around it, and where the line's colour is less than MIN_CONTRAST from it, moves the line's lightness away,
/// its hue kept, lines of different lightness keeping their order (selectionContrastF.glsl, contrastLightness); the
/// wireframe, drawn on its object, reads every texel. The glow is left as its colour is.
///
/// The disk a pixel searches grows with the square of the contour's width, and most pixels in the scissor are far
/// from anything to draw. The tile pass (interface/selectionTileF.glsl) records for each TILE_SIZE square of the
/// id target the lowest and highest nonzero id it holds, whether it holds the near side of a jump and whether
/// it holds the drawn surface of an object that glows. The edge pass leaves a pixel at once when no tile within its
/// reach holds what it could draw from: any id, off every object; on one, an id not its own or a jump. Whatever a
/// pixel draws, it draws from such a texel within its reach, whose own tile records it, so this changes nothing it
/// draws. Inside a linkset every tile holds other ids, so the tile pass also records each tile's nearest eye distance
/// of a surface that does not glow: where no tile within a pixel's reach comes nearer than the plane of the pixel's
/// own surface does anywhere in that reach, no object can stand in front of it there, and the pixel searches only as
/// far as the edges between objects reach.
///
/// The glow reaches farther than the lines, and its disk would be searched by every pixel near a glowing object.
/// Where something glows, the glow reach pass (interface/selectionGlowReachF.glsl) records for each tile the
/// lowest and highest id of the tiles holding a glowing surface within the glow's reach of it, so a pixel learns
/// from one texel whether a glow other than its own object's can be near; and the glow row pass
/// (interface/selectionGlowRowF.glsl) records for each texel how far along its row the nearest glowing texel lies.
/// Off every object, where every glowing surface stands in front, the nearest glowing texel in the disk is the
/// nearest of those a row, which the edge pass reads up and down its column; on an object, which stand in front
/// depends on the object's plane, and the disk is searched. Both give what the full search gives (GLOW_DISK in
/// selectionOutlineF.glsl), and the jump pass leaves out what glows, whose own surface draws nothing.
///
/// The targets exist only while something is drawn: they are made the first frame there is, given up once a frame
/// and TARGET_KEEP_SECONDS have passed without a call drawing with them (the world calls render every frame and the
/// HUD while the selection is on it, so one call can have nothing to draw while the other draws; and the pointer
/// passes from one glowing object to the next with frames between), and given up when the pipeline's context goes or
/// the main depth format changes (LLPipeline::releaseDepthDependents).
class ALSelectionOutline : public LLSingleton<ALSelectionOutline>
{
    LLSINGLETON(ALSelectionOutline);

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
        /// What the pointer is over (ALHoverGlow): it glows instead of being outlined, under every other object's
        /// lines (PRIORITY_HOVER in selectionUtilF.glsl).
        PRIORITY_HOVER,
        PRIORITY_COUNT
    };

    /// What an object outlined above PRIORITY_HOVER is drawn with, as a mask.
    enum EPart : U32
    {
        /// Its contour, and the edges between it and what it touches (RenderHighlightSelections).
        PART_CONTOUR = 1 << 0,
        /// Its faces' triangles as lines (AlchemySelectionWireframe).
        PART_WIREFRAME = 1 << 1,
    };

    /// How the lines are kept apart from what lies behind them (AlchemySelectionOutlineContrast).
    enum EContrast : S32
    {
        /// Drawn as the palette has them.
        CONTRAST_OFF = 0,
        /// The lines' lightness moved away from that of what lies behind them where the two are too alike, their
        /// hue kept (selectionContrastF.glsl).
        CONTRAST_SHIFT = 1,
        /// The palette's colours exactly, the contour cased by a rim View::mRimWidth wide outside it, dark around a
        /// light line and light around a dark one, which stands out where the line does not.
        CONTRAST_RIM = 2,
    };

    /// AlchemySelectionOutlineContrast's value as a mode, CONTRAST_RIM where it is none.
    static EContrast contrastMode(S32 setting)
    {
        return (setting == CONTRAST_OFF || setting == CONTRAST_SHIFT) ? (EContrast)setting : CONTRAST_RIM;
    }

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
        /// The wireframe's lines' width in pixels before their pixel of anti-aliasing.
        S32 mWireWidth = 1;
        /// How far in pixels the glow reaches past its object's silhouette (glowRadius), and its brightness
        /// (RenderHighlightBrightness), which glowOpacity scales it by.
        S32 mGlowRadius = 1;
        F32 mGlowBrightness = 0.f;
        /// Whether the outline and the wireframe draw every face whole, whatever its texture's alpha would cut away:
        /// AlchemySelectionOutlineGeometry where RLVa allows Highlight Transparent, or Highlight Transparent itself.
        bool mWholeFaces = false;
        /// Whether the glow, which follows the surface the pointer is over, draws every face whole: only under
        /// Highlight Transparent, where the scene itself draws them so.
        bool mWholeGlow = false;
        /// How the lines are kept apart from what lies behind them (AlchemySelectionOutlineContrast), and the width in
        /// pixels of the contour's rim under CONTRAST_RIM.
        EContrast mContrast = CONTRAST_OFF;
        S32 mRimWidth = 1;
    };

    /// How far the contour reaches from the object it is drawn around in `view`, its rim included (lineReach).
    static S32 contourReach(const View& view)
    {
        return lineReach(view.mWidth + (view.mContrast == CONTRAST_RIM ? view.mRimWidth : 0));
    }

    /// Whether the lines in `view` read what lies behind them: under CONTRAST_SHIFT, the contour and its edges where
    /// they are drawn, and the wireframe's lines where they are.
    static bool readsBackdrop(const View& view, bool contour, bool wireframe)
    {
        return view.mContrast == CONTRAST_SHIFT && (contour || wireframe);
    }

    /// What the objects of a frame ask for between them.
    struct Wants
    {
        /// A contour (PART_CONTOUR).
        bool mContour = false;
        /// A wireframe (PART_WIREFRAME).
        bool mWireframe = false;
        /// A glow (PRIORITY_HOVER).
        bool mGlow = false;
    };

    /// The passes render() runs for a frame.
    struct Passes
    {
        /// The id pass: the outline's and the glow's ids, and the depth of the selected surfaces the HUD's lines
        /// are tested against.
        bool mIds = false;
        /// The wireframe.
        bool mWireframe = false;
        /// The jump, tile and edge passes, which draw the outline and the glow from the ids.
        bool mEdges = false;

        bool any() const { return mIds || mWireframe || mEdges; }
    };

    /// The passes a frame whose objects ask for `wants` needs, in the world or on the HUD (`hud`), the wireframe
    /// only where its program loaded (`wireframe_ready`). The world's lines are tested against the window's depth
    /// and need no id pass; the HUD's against the id target's.
    static Passes passesFor(const Wants& wants, bool hud, bool wireframe_ready);

    /// What an object added asking for `parts` at `priority` is drawn with (add): at PRIORITY_HOVER nothing but its
    /// glow.
    static U32 partsFor(EPriority priority, U32 parts) { return priority == PRIORITY_HOVER ? 0u : parts; }

    /// Whether adding an object again at `adding` replaces what it was added with at `added` (add): at a priority as
    /// high or higher. What the pointer is over glows only where it is not outlined.
    static bool replaces(EPriority added, EPriority adding) { return adding <= added; }

    /// Whether the outline or the glow draws an object drawn with `parts` (partsFor) at `priority`.
    static bool outlined(U32 parts, EPriority priority) { return priority == PRIORITY_HOVER || (parts & PART_CONTOUR); }

    /// Whether such an object is drawn into the id pass: where it is outlined or glows, and on the HUD for its
    /// wireframe, whose lines are tested against the id target's depth.
    static bool inIdPass(U32 parts, EPriority priority, bool hud)
    {
        return outlined(parts, priority) || (hud && (parts & PART_WIREFRAME));
    }

    /// The world camera's, in the UI stage, where the scene's projection is still loaded.
    static View worldView();

    /// The HUD's, as its attachments were drawn with. False without a HUD to draw.
    static bool hudView(View& view);

    /// A view with these matrices whose contour is `contour_width` pixels wide at a UI scale of 1
    /// (AlchemySelectionOutlineWidth), drawn at `ui_scale`, LLUI's scale factor: the same widths in the world and
    /// on the HUD. The edges between objects are half the contour's width, a pixel at least. Its glow is
    /// `glow_thickness` (RenderHighlightThickness, glowRadius) and `glow_brightness` (RenderHighlightBrightness), held
    /// to 0 to MAX_GLOW_BRIGHTNESS, and to its default where it is not a number; its wireframe's lines are
    /// `wire_width` pixels wide at a UI scale of 1 (AlchemySelectionWireframeWidth), held to 1 to MAX_WIRE_WIDTH.
    static View makeView(const LLMatrix4a& modelview, const LLMatrix4a& projection, F32 contour_width, F32 ui_scale,
                         bool hud, F32 glow_thickness = DEFAULT_GLOW_THICKNESS,
                         F32 glow_brightness = DEFAULT_GLOW_BRIGHTNESS, F32 wire_width = DEFAULT_WIRE_WIDTH);

    /// Ids the target can tell apart; 0 is no selected object.
    static constexpr U32 MAX_IDS = 65535;
    /// Ids to a pair of palette rows: their visible colours, then their hidden ones.
    static constexpr U32 PALETTE_WIDTH = 256;
    /// What a hidden part keeps of its outline's opacity.
    static constexpr F32 HIDDEN_ALPHA = 0.55f;
    /// The contour's width in pixels at a UI scale of 1: AlchemySelectionOutlineWidth's default, while the wireframe
    /// is off; AlchemySelectionOutlineWireframeWidth's, while it is on and its lines already mark the object; and the
    /// range both are held to.
    static constexpr F32 DEFAULT_CONTOUR_WIDTH = 3.f;
    static constexpr F32 DEFAULT_WIREFRAME_CONTOUR_WIDTH = 2.f;
    static constexpr F32 MIN_CONTOUR_WIDTH = 1.f;
    static constexpr F32 MAX_CONTOUR_WIDTH = 16.f;
    /// The least width of the edges between objects in pixels at a UI scale of 1. They are half the contour's
    /// width: they mark where prims meet inside a selection, and as wide as the contour they would cover small prims.
    static constexpr F32 INNER_WIDTH = 1.f;
    /// The width of the contour's rim under CONTRAST_RIM in pixels at a UI scale of 1.
    static constexpr F32 RIM_WIDTH = 1.f;
    /// The side of the id target's squares the tile pass records, in texels (TILE_SIZE in selectionTileF.glsl and
    /// selectionOutlineF.glsl).
    static constexpr U32 TILE_SIZE = 8;
    /// What an edge between two objects keeps of its object's outline opacity (INNER_OPACITY in
    /// selectionOutlineF.glsl).
    static constexpr F32 INNER_OPACITY = 0.8f;
    /// The window behind the lines is copied at 1 / BACKDROP_SCALE of its resolution, and read up to BACKDROP_REACH
    /// pixels from where a line is drawn: four texels square about its own (backdropColour in
    /// selectionContrastF.glsl), five pixels to one side, and a sixth for the copy's even edges.
    static constexpr S32 BACKDROP_SCALE = 2;
    static constexpr S32 BACKDROP_REACH = 6;
    /// How far apart in OKLab lightness a line and the backdrop around it are kept (MIN_CONTRAST in
    /// selectionContrastF.glsl).
    static constexpr F32 MIN_CONTRAST = 0.3f;
    /// The texture alpha an alpha-blended face is outlined from: wherever it is drawn at all, so a uniformly
    /// translucent texture, as glass is, keeps its face, while the clear texels around a leaf on its card are cut.
    static constexpr F32 BLEND_ALPHA_CUTOFF = 0.1f;
    /// A face whose texture is not read: every fragment is outlined.
    static constexpr F32 NO_ALPHA_TEST = -1.f;
    /// A face the scene shows none of, whose texture is not read either: every fragment is cut (faceAlphaTest).
    static constexpr F32 UNSHOWN = 2.f;

    /// How a face's fragments are tested against its texture's alpha (setAlphaTest): those under mCutoff are cut, and
    /// with mKeepCut kept as surface the scene does not show, which the id pass lays behind every surface it shows;
    /// without, they are left out.
    struct AlphaTest
    {
        F32 mCutoff = NO_ALPHA_TEST;
        bool mKeepCut = false;
    };
    /// The base colour transform of a face whose texture coordinates have their transform in them.
    static constexpr LLGLTFMaterial::TextureTransform::Pack IDENTITY_TRANSFORM = { 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
    /// The wireframe's lines' width in pixels at a UI scale of 1: AlchemySelectionWireframeWidth's default, and the
    /// widest it is held to.
    static constexpr F32 DEFAULT_WIRE_WIDTH = 1.f;
    static constexpr F32 MAX_WIRE_WIDTH = 8.f;
    /// How far the wireframe's lines are drawn in front of their own surface beyond its slope, in the depth
    /// buffer's least steps (glPolygonOffset's units).
    static constexpr F32 WIRE_OFFSET_UNITS = 4.f;
    /// The glow's reach in pixels at a UI scale of 1 for each unit of RenderHighlightThickness: 9 at its default,
    /// 0.6. LL's glow blurred a 256-texel-wide copy of the object over 8 x 8 taps 3.5 x thickness texels out at
    /// most, a reach that grew with the window, about 16 pixels across a window 1920 wide; this one keeps its width
    /// in pixels whatever the window's.
    static constexpr F32 GLOW_PIXELS = 15.f;
    /// RenderHighlightThickness's and RenderHighlightBrightness's defaults.
    static constexpr F32 DEFAULT_GLOW_THICKNESS = 0.6f;
    static constexpr F32 DEFAULT_GLOW_BRIGHTNESS = 4.f;
    /// The glow's reach in pixels at a UI scale of 1 is held to this, the contour's widest: the edge pass searches
    /// a disk that wide around the pixels on an object near a glowing one.
    static constexpr F32 MAX_GLOW_RADIUS = 16.f;
    /// The glow's reach in pixels is held to this at any UI scale: the glow row pass records a distance one past the
    /// reach, and one more than that, in a byte.
    static constexpr S32 MAX_GLOW_PIXELS = 253;
    static constexpr F32 MAX_GLOW_BRIGHTNESS = 64.f;
    /// The glow's profile at its object's silhouette, before the brightness: the default brightness lays it on
    /// opaque there, as LL's glow did (GLOW_PEAK in selectionOutlineF.glsl).
    static constexpr F32 GLOW_PEAK = 0.25f;

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

    /// Whether an object drawn with `parts` (partsFor), its hidden parts dimmed where `show_hidden`, is drawn in
    /// wireframe pass `pass`: with a wireframe, and in WIRE_HIDDEN only where its hidden parts are drawn.
    static bool inWirePass(U32 parts, bool show_hidden, EWirePass pass)
    {
        return (parts & PART_WIREFRAME) && (pass != WIRE_HIDDEN || show_hidden);
    }

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

    /// Outlines `object` this frame with `parts` (EPart): its faces whose bit is set in te_mask, in `colour`, whose
    /// alpha is the outline's opacity, at `priority`. Parts the scene hides are dimmed when show_hidden and left out
    /// otherwise. At PRIORITY_HOVER the object glows in `colour` instead, its hidden parts left out, whatever
    /// `parts` and show_hidden say. Adding an object again keeps what it was added with at the higher priority, and
    /// at the same priority the later.
    void add(LLViewerObject* object, U32 te_mask, const LLColor4& colour, bool show_hidden, EPriority priority,
             U32 parts);

    /// Draws what was added since the last call over the bound framebuffer, with `view`, and forgets it, giving up
    /// the targets that are stale (stale).
    void render(const View& view);

    /// Gives up the targets.
    void release();

    /// How long the targets are kept after a call last drew with them: long enough for the pause between one
    /// selection and the next, or the pointer passing from one glowing object to the next, not to make them again.
    static constexpr F64 TARGET_KEEP_SECONDS = 30.0;

    /// Whether a target last drawn with in frame `used_frame` (LLFrameTimer::getFrameCount), at `used_at` seconds
    /// (LLFrameTimer::getTotalSeconds), is given up in frame `frame` at `now`: once a whole frame has passed without
    /// it, and TARGET_KEEP_SECONDS.
    static bool stale(U32 used_frame, F64 used_at, U32 frame, F64 now)
    {
        return frame - used_frame > 1 && now - used_at >= TARGET_KEEP_SECONDS;
    }

    /// Palette rows for ids 1 to `count`, and the id 0 that is never drawn.
    static U32 paletteRows(U32 count) { return 2 * (count / PALETTE_WIDTH + 1); }

    /// Writes id's visible and hidden texels into an RGBA8 palette PALETTE_WIDTH texels wide.
    static void writePalette(std::vector<U8>& texels, U32 id, const LLColor4& colour, bool show_hidden);

    /// A line `width` pixels wide at a UI scale of 1, at `ui_scale`: at least a pixel.
    static S32 lineWidth(F32 width, F32 ui_scale) { return llmax(1, ll_round(width * ui_scale)); }

    /// The contour's width in pixels for the setting `width` at `ui_scale`, the setting held to MIN_CONTOUR_WIDTH
    /// to MAX_CONTOUR_WIDTH, and to the least where it is not a number.
    static S32 contourWidth(F32 width, F32 ui_scale);

    /// The contour width setting a view takes: AlchemySelectionOutlineWireframeWidth's `wireframe_width` while the
    /// wireframe is drawn (AlchemySelectionWireframe), AlchemySelectionOutlineWidth's `width` while it is not.
    static F32 contourSetting(bool wireframe, F32 width, F32 wireframe_width) { return wireframe ? wireframe_width : width; }

    /// How far in pixels the glow reaches past its object's silhouette for RenderHighlightThickness `thickness` at
    /// `ui_scale`: thickness x GLOW_PIXELS at a UI scale of 1, held to MAX_GLOW_RADIUS, and its default where the
    /// setting is not a number; a pixel at least, and MAX_GLOW_PIXELS at most.
    static S32 glowRadius(F32 thickness, F32 ui_scale);

    /// The glow's opacity before its colour's alpha `dist` pixels from the centre of its object's nearest texel, its
    /// silhouette half a texel out: brightness x GLOW_PEAK x (1 - smoothstep(0, 1, x)), x the distance past the
    /// silhouette over `radius`, held to 1. It falls smoothly to nothing at `radius` past the silhouette, and the
    /// brightness keeps a band of it opaque at the silhouette where it is over 1 / GLOW_PEAK. As the edge pass
    /// computes it.
    static F32 glowOpacity(F32 dist, S32 radius, F32 brightness);

    /// The alpha test of a face whose texture is `texture_id`, its highest alpha `max_alpha` (LLImageGL::getMaxAlpha),
    /// from `cutoff`, what its alpha mode gives (legacyAlphaCutoff, gltfAlphaCutoff), `whole` (wholeFaces), and
    /// `invisible`, whether the scene draws nothing of the face (LLVOVolume::isInvisibleFace). A face the scene shows
    /// none of is outlined whole, as surface the scene does not show (UNSHOWN): an invisible one; one wearing the
    /// library's transparent texture (IMG_TRANSPARENT), which builders put on roots, collision and touch prims to hide
    /// them while they can still be picked and edited; and one whose texture has nothing at or above its cutoff. Else
    /// what the cutoff cuts is kept as such surface where `whole`, and left out where not.
    static AlphaTest faceAlphaTest(const LLUUID& texture_id, U8 max_alpha, F32 cutoff, bool whole, bool invisible)
    {
        if (invisible || (cutoff > 0.f && (texture_id == IMG_TRANSPARENT || (F32)max_alpha / 255.f < cutoff)))
        {
            return { UNSHOWN, true };
        }
        return { cutoff, whole };
    }

    /// Whether an object at `priority` is drawn with its faces whole in `view` (faceAlphaTest): what glows as
    /// View::mWholeGlow says, and what is outlined as View::mWholeFaces says.
    static bool wholeFaces(const View& view, EPriority priority)
    {
        return priority == PRIORITY_HOVER ? view.mWholeGlow : view.mWholeFaces;
    }

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
    /// by `reach`, clipped to the target. Where the box reaches behind the eye, the part of it in front is projected,
    /// its edges cut where they cross. False when the box cannot touch the target.
    static bool scissorRect(const LLMatrix4a& mvp, const LLVector4a extents[2], S32 width, S32 height, S32 reach,
                            LLRect& rect);

    /// The most rects the passes run over a frame (mergeRects).
    static constexpr size_t MAX_RECTS = 8;
    /// More rects than this are joined into the one around them all, rather than two by two.
    static constexpr size_t MAX_RECTS_JOINED_IN_PAIRS = 64;

    /// The rects the passes run over: `rects` with every two that overlap joined into the rect around both, until
    /// none do, so a pass drawing each once draws no pixel twice; then while there are more than MAX_RECTS the two
    /// whose joining adds least area, and past MAX_RECTS_JOINED_IN_PAIRS all of them into one.
    static void mergeRects(std::vector<LLRect>& rects);

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

    /// The id, priority and hidden parts the bound id program writes for the faces drawn next. Faces not
    /// `outlined`, drawn for the depth of the HUD's wireframe alone, are written as drawn nowhere: in the id target
    /// they hide what lies behind them, and draw no line and mask no other's.
    static void setId(LLGLSLShader& program, U32 id, EPriority priority, bool show_hidden, bool outlined = true);

    /// Draws a face into the id pass from `buffer`'s bound range, culled as the scene culls it: both sides of a
    /// double-sided GLTF material, the front alone of everything else (LLRenderPass::pushGLTFBatch).
    static void drawFace(LLVertexBuffer& buffer, U32 start, U32 end, U32 count, U32 offset, bool double_sided);

    /// The base colour transform a program was last given, which setAlphaTest leaves alone while it is the one
    /// asked for; not valid until it is given one, as when a program is bound.
    struct TransformCache
    {
        LLGLTFMaterial::TextureTransform::Pack mPack;
        bool mValid = false;
    };

    /// How the bound id or wireframe program tests the alpha of the face drawn next, whose texture is bound as its
    /// diffuse map (AlphaTest): with NO_ALPHA_TEST or UNSHOWN the texture is not read. `transform` is the base
    /// colour's texture transform, packed as LLGLTFMaterial::TextureTransform::getPacked packs it, the identity for a
    /// legacy face, whose texture coordinates have their transform in them; with `bound`, it is given only where it
    /// is not the one the program has.
    static void setAlphaTest(LLGLSLShader& program, const AlphaTest& test,
                             const LLGLTFMaterial::TextureTransform::Pack& transform, TransformCache* bound = nullptr);

    /// The jump pass with the bound jump program: into `jumps`, R8 at the size of `ids`, the id target, 1 on the
    /// texels on the near side of a jump and 0 elsewhere, over the texels under `rects`; the others are cleared, as
    /// `ids` holds nothing there when the rects cover every selected texel. Its depth is read through `view`'s
    /// projection, which the id pass drew with.
    static void drawJumps(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& jumps,
                          const std::vector<LLRect>& rects, const View& view, LLVertexBuffer& triangle);

    /// The tile pass with the bound tile program: into `tiles`, a texel to each TILE_SIZE square of `ids`, the id
    /// target, and of `jumps`, the jump pass's marks: in its first attachment, RGBA16, the square's lowest and highest
    /// id, whether it holds the near side of a jump and whether it holds a glowing surface; in its second, R32F, the
    /// eye distance of its nearest texel of an object that does not glow, read through `view`'s projection, and past
    /// every distance where there is none. Over the tiles under `rects`, rects of `ids`; the others are cleared, and
    /// hold nothing when the rects cover every selected texel.
    static void drawTiles(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& jumps, LLRenderTarget& tiles,
                          const std::vector<LLRect>& rects, const View& view, LLVertexBuffer& triangle);

    /// Clears the jump pass's target where a frame draws no contour, which reads no jumps but the tile pass does.
    static void clearJumps(LLRenderTarget& jumps);

    /// The tiles of the id target a rect of its texels touches.
    static LLRect tileRect(const LLRect& rect);

    /// The rect of a width x height window the backdrop is copied from before the lines over `rect` are drawn: the
    /// rect grown by BACKDROP_REACH, held to the window, and to even edges, so each texel of the copy is the mean of a
    /// square of BACKDROP_SCALE pixels.
    static LLRect backdropRect(const LLRect& rect, S32 width, S32 height);

    /// The backdrop mask pass with the bound mask program: into the alpha of `backdrop`, 1 on each texel none of
    /// whose window pixels shows a selected surface in `ids`, the id target, and 0 on the others, over the texels
    /// under `rects`, rects of the window; its colour is left as the copy left it. What lies behind a contour is read
    /// from the texels it marks alone, so an object does not count as its own backdrop.
    static void drawBackdropMask(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& backdrop,
                                 const std::vector<LLRect>& rects, LLVertexBuffer& triangle);

    /// The OKLab lightness a line of lightness `line` takes over a backdrop of lightness `back` under CONTRAST_SHIFT,
    /// as contrastLightness in selectionContrastF.glsl gives it, where the two colours are less than MIN_CONTRAST
    /// apart: its own where the lightnesses are MIN_CONTRAST apart already; else, below the turn, MIN_CONTRAST lighter
    /// than the backdrop; above it, MIN_CONTRAST darker and darker again by as much as the line is darker than white,
    /// so lines of different lightness keep their order and their distance, as a root's yellow over a child's blue.
    /// The turn is the line's own lightness held to where both sides have room, so a light line stays light over all
    /// but light backdrops and a dark one dark over all but dark ones.
    static F32 contrastLightness(F32 line, F32 back)
    {
        const F32 turn = llclamp(line, MIN_CONTRAST, 1.f - MIN_CONTRAST);
        if (back < turn)
        {
            return llmax(line, back + MIN_CONTRAST);
        }
        return line <= back - MIN_CONTRAST ? line : llmax(back - MIN_CONTRAST - (1.f - line), 0.f);
    }

    /// Binds `contrast` to the bound edge or wireframe program: the mode, the rim's width under CONTRAST_RIM, and
    /// under CONTRAST_SHIFT `backdrop`, the window behind the lines (drawBackdropMask), without which the lines are
    /// drawn as the palette has them.
    static void bindContrast(LLGLSLShader& program, EContrast contrast, S32 rim_width, LLRenderTarget* backdrop);

    /// The glow reach pass with the bound glow reach program: into `reach`, RGBA16 at the size of `tiles`, the tile
    /// pass's record, for each tile the lowest and highest id of the tiles within `view`'s glow radius of it that hold
    /// a glowing surface, over the tiles under `rects`, rects of the id target; the others are cleared, and hold no
    /// glowing surface near them when the rects cover every selected texel and the glow's reach around them.
    static void drawGlowReach(LLGLSLShader& program, LLRenderTarget& tiles, LLRenderTarget& reach,
                              const std::vector<LLRect>& rects, const View& view, LLVertexBuffer& triangle);

    /// The glow row pass with the bound glow row program: into `rows`, R8 at the size of `ids`, the id target, for
    /// each texel how far along its row the nearest glowing texel within `view`'s glow radius lies, with `reach`, the
    /// glow reach pass's record, over the texels under `rects`; the others are cleared, as for drawGlowReach.
    static void drawGlowRows(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& reach, LLRenderTarget& rows,
                             const std::vector<LLRect>& rects, const View& view, LLVertexBuffer& triangle);

    /// Binds what a wireframe pass reads to the bound wireframe program: the pass, the lines' width, the viewport's
    /// size, the palette, the contrast (bindContrast), and for WIRE_ID_DEPTH the depth of `ids`, the id target.
    static void bindWirePass(LLGLSLShader& program, EWirePass pass, S32 wire_width, S32 width, S32 height, U32 palette,
                             LLRenderTarget* ids, EContrast contrast, LLRenderTarget* backdrop);

    /// The edge pass with the bound outline program and `view`'s widths and contrast: ids and their depth from `ids`,
    /// the id target, the depth through `view`'s projection, which the id pass drew with, the jump pass's marks from
    /// `jumps` and the tile pass's record from `tiles`. With `glow_reach` and `glow_rows`, the glow reach and row
    /// passes' records, where an object glows, the pass draws the glow; and only then. With `backdrop`, what lies
    /// behind the lines (bindContrast). Over each of `rects`, rects of the viewport at (vp_x, vp_y), where given, and
    /// over the viewport and scissor in force where not.
    static void drawEdges(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& jumps, LLRenderTarget& tiles,
                          LLRenderTarget* glow_reach, LLRenderTarget* glow_rows, LLRenderTarget* backdrop, U32 palette,
                          const View& view, LLVertexBuffer& triangle, const std::vector<LLRect>* rects = nullptr,
                          S32 vp_x = 0, S32 vp_y = 0);

private:
    // The full-screen triangle with the bound program, `projection` loaded for its draw where given: the triangle
    // takes no matrix, and the passes read depth through the projection's inverse.
    static void drawTriangle(LLVertexBuffer& triangle, const LLMatrix4a* projection);

    // Clears `target`, then draws the full-screen triangle into its texels under each of `rects`.
    static void drawUnder(LLRenderTarget& target, const std::vector<LLRect>& rects, LLVertexBuffer& triangle,
                          const LLMatrix4a* projection);

    struct Entry
    {
        LLViewerObject* mObject;
        U32             mTEMask;
        LLColor4        mColour;
        bool            mShowHidden;
        EPriority       mPriority;
        // EPart; none at PRIORITY_HOVER.
        U32             mParts;
    };

    // The box the faces of an object cover, in the frame the view's modelview takes: unbounded where a rigged face's
    // joints have not been measured yet.
    struct FaceBox
    {
        LLVector4a mLo{ FLT_MAX, FLT_MAX, FLT_MAX };
        LLVector4a mHi{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        bool mBounded = true;
    };

    // A face a frame draws, as the face walk found it (collectFaces).
    struct FaceDraw
    {
        LLFace* mFace;
        // From the frame its vertex buffer is in, as LLVolumeGeometryManager::registerFace places it, to the frame
        // the view's modelview takes; none for a rigged face, skinned into agent space through its joint palette.
        const LLMatrix4a* mModel;
        // Its object's place in the ids' order: its id is one more.
        U32 mIndex;
        EPriority mPriority;
        bool mRigged;
    };

    // Finds what a frame draws of what was added: each object's selected faces with geometry, as the scene draws
    // them, where the box they cover grown by what reaches past it, its contour, glow or wireframe, can touch a
    // width x height target through `mvp`. The rects of those drawn into the id pass (inIdPass) go into mRects, and
    // of those drawn with lines, a contour or a wireframe, into mLineRects, each merged (mergeRects); the faces into
    // mFaces, the rigged apart, the lowest priority first, then by what their draws share: matrix or joint palette,
    // and vertex buffer.
    void collectFaces(const View& view, S32 width, S32 height, const LLMatrix4a& mvp);

    // Draws the faces of mFaces that are rigged (or not) and `drawn` passes into `program`, the bound id or wireframe
    // program or the rigged variant of one, as the scene draws them: in the frame mFaces gives, a rigged one through
    // its joint palette, with its alpha test and culling (bindFace, drawFace). `begin` is called with a face's
    // FaceDraw before the first face of each object.
    template <typename Drawn, typename Begin>
    void drawFaceList(LLGLSLShader& program, bool rigged, const View& view, Drawn&& drawn, Begin&& begin);

    // The id pass: what was added and is drawn into it (inIdPass) into mIdMap, a width x height target.
    void drawIds(const View& view, S32 width, S32 height);

    // The wireframe of what was added with one, over the bound framebuffer under the outline: the world's visible
    // lines and its hidden ones against the window's depth, the HUD's against the id target's; kept apart from
    // `backdrop` under CONTRAST_SHIFT.
    void drawWireframes(const View& view, S32 width, S32 height, LLRenderTarget* backdrop);

    // When a target was last drawn with: the frame (LLFrameTimer::getFrameCount) and the time
    // (LLFrameTimer::getTotalSeconds).
    struct Use
    {
        U32 mFrame = 0;
        F64 mTime = 0.0;
    };

    // Gives up the targets stale in frame `frame` at `now`.
    void releaseStale(U32 frame, F64 now);

    // What a face is drawn with beyond its geometry.
    struct FaceBinding
    {
        // A texture animation was loaded, which the caller unloads after the draw.
        bool mAnimated = false;
        // Its GLTF material is double-sided: the scene culls neither side.
        bool mDoubleSided = false;
    };

    // Binds `face`'s texture and alpha test to the bound program, its base colour transform where `bound` says the
    // program has another, and loads its texture animation, if it is drawn with one. With `whole` what its texture
    // cuts is kept (faceAlphaTest).
    static FaceBinding bindFace(LLGLSLShader& program, LLFace* face, bool whole, TransformCache& bound);

    void clearEntries();

    std::vector<Entry> mEntries;
    boost::unordered_flat_map<const LLViewerObject*, U32> mEntryIndex;
    std::vector<EPriority> mPriorities;
    std::vector<U32> mOrder;
    std::vector<U8> mPaletteTexels;
    // What the palette holds, so a frame with the same colours uploads nothing.
    std::vector<U8> mUploadedTexels;

    LLRenderTarget mIdMap;
    LLRenderTarget mJumpMap;
    LLRenderTarget mTileMap;
    LLRenderTarget mGlowReachMap;
    LLRenderTarget mGlowRowMap;
    LLRenderTarget mBackdropMap;
    U32 mPalette = 0;
    U32 mPaletteRows = 0;

    // When the id target, the jump and tile targets, the glow's, the backdrop and the palette were last drawn with.
    Use mIdUse;
    Use mEdgeUse;
    Use mGlowUse;
    Use mBackdropUse;
    Use mPaletteUse;

    // This frame's faces, and the pixels the id pass's objects and their lines and glow can touch, and those the
    // lines can (collectFaces).
    std::vector<FaceDraw> mFaces;
    std::vector<LLRect> mRects;
    std::vector<LLRect> mLineRects;

    // Joint palettes uploaded to the bound rigged program, so a mesh's faces upload theirs once.
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

inline S32 ALSelectionOutline::glowRadius(F32 thickness, F32 ui_scale)
{
    // Asked outright: /fp:fast may answer a comparison with a NaN either way.
    const F32 held = llisnan(thickness) ? DEFAULT_GLOW_THICKNESS : thickness;
    return llmin(lineWidth(llclamp(held * GLOW_PIXELS, 0.f, MAX_GLOW_RADIUS), ui_scale), MAX_GLOW_PIXELS);
}

inline F32 ALSelectionOutline::glowOpacity(F32 dist, S32 radius, F32 brightness)
{
    const F32 x = llclamp(llmax(dist - 0.5f, 0.f) / (F32)llmax(radius, 1), 0.f, 1.f);
    const F32 falloff = 1.f - x * x * (3.f - 2.f * x);
    return llclamp(brightness * GLOW_PEAK * falloff, 0.f, 1.f);
}

inline ALSelectionOutline::View ALSelectionOutline::makeView(const LLMatrix4a& modelview, const LLMatrix4a& projection,
                                                             F32 contour_width, F32 ui_scale, bool hud,
                                                             F32 glow_thickness, F32 glow_brightness, F32 wire_width)
{
    View view;
    view.mModelview = modelview;
    view.mProjection = projection;
    view.mWidth = contourWidth(contour_width, ui_scale);
    view.mInnerWidth = llmax(lineWidth(INNER_WIDTH, ui_scale), view.mWidth / 2);
    view.mRimWidth = lineWidth(RIM_WIDTH, ui_scale);
    view.mHUD = hud;
    // Asked outright: /fp:fast may answer a comparison with a NaN either way.
    const F32 wire = llisnan(wire_width) ? DEFAULT_WIRE_WIDTH : llclamp(wire_width, 1.f, MAX_WIRE_WIDTH);
    view.mWireWidth = lineWidth(wire, ui_scale);
    view.mGlowRadius = glowRadius(glow_thickness, ui_scale);
    view.mGlowBrightness =
        llisnan(glow_brightness) ? DEFAULT_GLOW_BRIGHTNESS : llclamp(glow_brightness, 0.f, MAX_GLOW_BRIGHTNESS);
    return view;
}

inline ALSelectionOutline::Passes ALSelectionOutline::passesFor(const Wants& wants, bool hud, bool wireframe_ready)
{
    Passes passes;
    passes.mWireframe = wants.mWireframe && wireframe_ready;
    passes.mEdges = wants.mContour || wants.mGlow;
    passes.mIds = passes.mEdges || (passes.mWireframe && hud);
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
    // A point is in front of the eye where its clip w passes this.
    constexpr F32 FRONT_W = 1e-6f;

    const LLRect target(0, height, width, 0);
    F32 min_x = FLT_MAX;
    F32 min_y = FLT_MAX;
    F32 max_x = -FLT_MAX;
    F32 max_y = -FLT_MAX;
    bool any = false;
    const auto take = [&](const LLVector4a& clip)
    {
        const F32 x = (clip[0] / clip[3] * 0.5f + 0.5f) * (F32)width;
        const F32 y = (clip[1] / clip[3] * 0.5f + 0.5f) * (F32)height;
        min_x = llmin(min_x, x);
        min_y = llmin(min_y, y);
        max_x = llmax(max_x, x);
        max_y = llmax(max_y, y);
        any = true;
    };

    LLVector4a clip[8];
    for (U32 i = 0; i < 8; ++i)
    {
        const LLVector4a corner(extents[i & 1][0], extents[(i >> 1) & 1][1], extents[(i >> 2) & 1][2], 1.f);
        mvp.transform4(corner, clip[i]);
        if (clip[i][3] > FRONT_W)
        {
            take(clip[i]);
        }
    }

    // The part of the box in front of the eye has the corners in front, and where each of its twelve edges from one
    // in front to one behind crosses: clip space is linear along an edge.
    for (U32 i = 0; i < 8; ++i)
    {
        for (U32 axis : { 1u, 2u, 4u })
        {
            const U32 j = i | axis;
            if (j == i || (clip[i][3] > FRONT_W) == (clip[j][3] > FRONT_W))
            {
                continue;
            }
            const F32 t = (FRONT_W - clip[i][3]) / (clip[j][3] - clip[i][3]);
            take(LLVector4a(clip[i][0] + t * (clip[j][0] - clip[i][0]), clip[i][1] + t * (clip[j][1] - clip[i][1]),
                            0.f, FRONT_W));
        }
    }

    if (!any)
    {
        return false;
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

inline void ALSelectionOutline::mergeRects(std::vector<LLRect>& rects)
{
    rects.erase(std::remove_if(rects.begin(), rects.end(), [](const LLRect& rect) { return !rect.notEmpty(); }),
                rects.end());
    if (rects.size() > MAX_RECTS_JOINED_IN_PAIRS)
    {
        LLRect all = rects.front();
        for (const LLRect& rect : rects)
        {
            all.unionWith(rect);
        }
        rects.assign(1, all);
        return;
    }

    const auto overlap = [](const LLRect& a, const LLRect& b)
    {
        return a.mLeft < b.mRight && b.mLeft < a.mRight && a.mBottom < b.mTop && b.mBottom < a.mTop;
    };
    const auto area = [](const LLRect& rect) { return (S64)rect.getWidth() * (S64)rect.getHeight(); };
    for (;;)
    {
        // Every two that overlap joined, again until none do: a join can reach a third.
        bool joined = false;
        for (size_t i = 0; i < rects.size() && !joined; ++i)
        {
            for (size_t j = i + 1; j < rects.size(); ++j)
            {
                if (overlap(rects[i], rects[j]))
                {
                    rects[i].unionWith(rects[j]);
                    rects.erase(rects.begin() + j);
                    joined = true;
                    break;
                }
            }
        }
        if (joined)
        {
            continue;
        }
        if (rects.size() <= MAX_RECTS)
        {
            return;
        }

        // Too many: the two whose joining adds least.
        size_t best_i = 0;
        size_t best_j = 1;
        S64 best = std::numeric_limits<S64>::max();
        for (size_t i = 0; i < rects.size(); ++i)
        {
            for (size_t j = i + 1; j < rects.size(); ++j)
            {
                LLRect both = rects[i];
                both.unionWith(rects[j]);
                const S64 added = area(both) - area(rects[i]) - area(rects[j]);
                if (added < best)
                {
                    best = added;
                    best_i = i;
                    best_j = j;
                }
            }
        }
        rects[best_i].unionWith(rects[best_j]);
        rects.erase(rects.begin() + best_j);
    }
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
    if (scene)
    {
        program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, scene);
    }
    else
    {
        // Not read without a scene; left empty rather than holding whatever the unit last had.
        program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    }
    program.uniform1i(LLShaderMgr::SELECTION_SCENE_DEPTH, scene ? 1 : 0);
    program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)width, (F32)height);
}

inline void ALSelectionOutline::setId(LLGLSLShader& program, U32 id, EPriority priority, bool show_hidden, bool outlined)
{
    program.uniform1i(LLShaderMgr::SELECTION_ID, (GLint)id);
    program.uniform1i(LLShaderMgr::SELECTION_PRIORITY, (GLint)priority);
    program.uniform1i(LLShaderMgr::SELECTION_SHOW_HIDDEN, show_hidden ? 1 : 0);
    program.uniform1i(LLShaderMgr::SELECTION_OUTLINED, outlined ? 1 : 0);
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

inline void ALSelectionOutline::bindContrast(LLGLSLShader& program, EContrast contrast, S32 rim_width,
                                             LLRenderTarget* backdrop)
{
    // A shift reads what lies behind the lines; without it they are drawn as the palette has them.
    const EContrast mode = (contrast == CONTRAST_SHIFT && !backdrop) ? CONTRAST_OFF : contrast;
    if (mode == CONTRAST_SHIFT)
    {
        // Read a texel at a time: its alpha says whether it is the backdrop of a contour (drawBackdropMask).
        program.bindTexture(LLShaderMgr::SELECTION_BACKDROP_MAP, backdrop, ALSamplers::PointClamp);
    }
    else
    {
        // Not read; left empty rather than holding whatever the unit last had.
        program.unbindTexture(LLShaderMgr::SELECTION_BACKDROP_MAP);
    }
    program.uniform1i(LLShaderMgr::SELECTION_CONTRAST, (GLint)mode);
    program.uniform1i(LLShaderMgr::OUTLINE_RIM_WIDTH, mode == CONTRAST_RIM ? llmax(rim_width, 1) : 0);
}

inline void ALSelectionOutline::bindWirePass(LLGLSLShader& program, EWirePass pass, S32 wire_width, S32 width, S32 height,
                                             U32 palette, LLRenderTarget* ids, EContrast contrast,
                                             LLRenderTarget* backdrop)
{
    // The wireframe's lines are drawn on their object and have no rim: they are shifted or drawn as they are.
    bindContrast(program, contrast == CONTRAST_SHIFT ? CONTRAST_SHIFT : CONTRAST_OFF, 0, backdrop);
    program.uniform1i(LLShaderMgr::WIREFRAME_PASS, (GLint)pass);
    program.uniform1i(LLShaderMgr::WIREFRAME_WIDTH, llmax(wire_width, 1));
    program.uniform2f(LLShaderMgr::DEFERRED_SCREEN_RES, (F32)width, (F32)height);
    const S32 channel = program.enableTexture(LLShaderMgr::SELECTION_PALETTE);
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

inline void ALSelectionOutline::setAlphaTest(LLGLSLShader& program, const AlphaTest& test,
                                             const LLGLTFMaterial::TextureTransform::Pack& transform, TransformCache* bound)
{
    program.setMinimumAlpha(test.mCutoff);
    program.uniform1i(LLShaderMgr::SELECTION_KEEP_CUT, test.mKeepCut ? 1 : 0);
    // An array, which LLGLSLShader's uniform cache does not keep, and the identity on every legacy face.
    if (bound && bound->mValid && std::equal(std::begin(transform), std::end(transform), std::begin(bound->mPack)))
    {
        return;
    }
    program.uniform4fv(LLShaderMgr::TEXTURE_BASE_COLOR_TRANSFORM, 2, transform);
    if (bound)
    {
        std::copy(std::begin(transform), std::end(transform), std::begin(bound->mPack));
        bound->mValid = true;
    }
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

inline void ALSelectionOutline::drawUnder(LLRenderTarget& target, const std::vector<LLRect>& rects,
                                          LLVertexBuffer& triangle, const LLMatrix4a* projection)
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

        const LLRect whole(0, (S32)target.getHeight(), (S32)target.getWidth(), 0);
        for (const LLRect& rect : rects)
        {
            LLRect under = rect;
            under.intersectWith(whole);
            if (under.notEmpty())
            {
                LLGLSScissor scissor(under.mLeft, under.mBottom, under.getWidth(), under.getHeight());
                drawTriangle(triangle, projection);
            }
        }
    }
    target.flush();
}

inline void ALSelectionOutline::drawJumps(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& jumps,
                                          const std::vector<LLRect>& rects, const View& view, LLVertexBuffer& triangle)
{
    program.bindTexture(LLShaderMgr::SELECTION_ID_MAP, &ids, ALSamplers::PointClamp);
    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &ids);
    drawUnder(jumps, rects, triangle, &view.mProjection);
    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    program.unbindTexture(LLShaderMgr::SELECTION_ID_MAP);
}

inline void ALSelectionOutline::clearJumps(LLRenderTarget& jumps)
{
    jumps.bindTarget();
    {
        LLGLSColorMask mask(true, true);
        LLGLDisable scissor(GL_SCISSOR_TEST);
        gGL.setClearColor(LLColor4(0.f, 0.f, 0.f, 0.f));
        jumps.clear();
    }
    jumps.flush();
}

inline LLRect ALSelectionOutline::tileRect(const LLRect& rect)
{
    const S32 left = llmax(rect.mLeft, 0) / (S32)TILE_SIZE;
    const S32 bottom = llmax(rect.mBottom, 0) / (S32)TILE_SIZE;
    const S32 right = (S32)tileCount((U32)llmax(rect.mRight, 0));
    const S32 top = (S32)tileCount((U32)llmax(rect.mTop, 0));
    return LLRect(left, top, right, bottom);
}

inline LLRect ALSelectionOutline::backdropRect(const LLRect& rect, S32 width, S32 height)
{
    LLRect copied(rect.mLeft - BACKDROP_REACH, rect.mTop + BACKDROP_REACH, rect.mRight + BACKDROP_REACH,
                  rect.mBottom - BACKDROP_REACH);
    copied.intersectWith(LLRect(0, llmax(height, 0), llmax(width, 0), 0));
    // Down to whole squares of BACKDROP_SCALE pixels, all of them inside the window.
    copied.mLeft -= copied.mLeft % BACKDROP_SCALE;
    copied.mBottom -= copied.mBottom % BACKDROP_SCALE;
    copied.mRight -= copied.mRight % BACKDROP_SCALE;
    copied.mTop -= copied.mTop % BACKDROP_SCALE;
    return copied;
}

inline void ALSelectionOutline::drawBackdropMask(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& backdrop,
                                                 const std::vector<LLRect>& rects, LLVertexBuffer& triangle)
{
    program.bindTexture(LLShaderMgr::SELECTION_ID_MAP, &ids, ALSamplers::PointClamp);
    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &ids);
    backdrop.bindTarget();
    {
        // The alpha alone: the colour is the copy's.
        LLGLSColorMask mask(false, true);
        LLGLDepthTest depth(GL_FALSE);
        LLGLDisable blend(GL_BLEND);
        const LLRect whole(0, (S32)backdrop.getHeight(), (S32)backdrop.getWidth(), 0);
        for (const LLRect& rect : rects)
        {
            LLRect under(rect.mLeft / BACKDROP_SCALE, (rect.mTop + BACKDROP_SCALE - 1) / BACKDROP_SCALE,
                         (rect.mRight + BACKDROP_SCALE - 1) / BACKDROP_SCALE, rect.mBottom / BACKDROP_SCALE);
            under.intersectWith(whole);
            if (under.notEmpty())
            {
                LLGLSScissor scissor(under.mLeft, under.mBottom, under.getWidth(), under.getHeight());
                drawTriangle(triangle, nullptr);
            }
        }
    }
    backdrop.flush();
    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    program.unbindTexture(LLShaderMgr::SELECTION_ID_MAP);
}

inline void ALSelectionOutline::drawTiles(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& jumps,
                                          LLRenderTarget& tiles, const std::vector<LLRect>& rects, const View& view,
                                          LLVertexBuffer& triangle)
{
    program.bindTexture(LLShaderMgr::SELECTION_ID_MAP, &ids, ALSamplers::PointClamp);
    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &ids);
    program.bindTexture(LLShaderMgr::SELECTION_JUMP_MAP, &jumps, ALSamplers::PointClamp);
    std::vector<LLRect> tile_rects;
    tile_rects.reserve(rects.size());
    for (const LLRect& rect : rects)
    {
        tile_rects.push_back(tileRect(rect));
    }
    drawUnder(tiles, tile_rects, triangle, &view.mProjection);
    program.unbindTexture(LLShaderMgr::SELECTION_JUMP_MAP);
    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    program.unbindTexture(LLShaderMgr::SELECTION_ID_MAP);
}

inline void ALSelectionOutline::drawGlowReach(LLGLSLShader& program, LLRenderTarget& tiles, LLRenderTarget& reach,
                                              const std::vector<LLRect>& rects, const View& view,
                                              LLVertexBuffer& triangle)
{
    // The glow reach program reads no depth, and needs no projection.
    program.bindTexture(LLShaderMgr::SELECTION_TILE_MAP, &tiles, ALSamplers::PointClamp);
    program.uniform1i(LLShaderMgr::OUTLINE_GLOW_RADIUS, llclamp(view.mGlowRadius, 1, MAX_GLOW_PIXELS));
    std::vector<LLRect> tile_rects;
    tile_rects.reserve(rects.size());
    for (const LLRect& rect : rects)
    {
        tile_rects.push_back(tileRect(rect));
    }
    drawUnder(reach, tile_rects, triangle, nullptr);
    program.unbindTexture(LLShaderMgr::SELECTION_TILE_MAP);
}

inline void ALSelectionOutline::drawGlowRows(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& reach,
                                             LLRenderTarget& rows, const std::vector<LLRect>& rects, const View& view,
                                             LLVertexBuffer& triangle)
{
    // The glow row program reads no depth, and needs no projection.
    program.bindTexture(LLShaderMgr::SELECTION_ID_MAP, &ids, ALSamplers::PointClamp);
    program.bindTexture(LLShaderMgr::SELECTION_GLOW_REACH_MAP, &reach, ALSamplers::PointClamp);
    program.uniform1i(LLShaderMgr::OUTLINE_GLOW_RADIUS, llclamp(view.mGlowRadius, 1, MAX_GLOW_PIXELS));
    drawUnder(rows, rects, triangle, nullptr);
    program.unbindTexture(LLShaderMgr::SELECTION_GLOW_REACH_MAP);
    program.unbindTexture(LLShaderMgr::SELECTION_ID_MAP);
}

inline void ALSelectionOutline::drawEdges(LLGLSLShader& program, LLRenderTarget& ids, LLRenderTarget& jumps,
                                          LLRenderTarget& tiles, LLRenderTarget* glow_reach, LLRenderTarget* glow_rows,
                                          LLRenderTarget* backdrop, U32 palette, const View& view, LLVertexBuffer& triangle,
                                          const std::vector<LLRect>* rects, S32 vp_x, S32 vp_y)
{
    // Point sampled all: the ids, their depth, the jumps, the tiles, the glow's records, the backdrop and the
    // palette are data, and texelFetch reads them whatever the filter.
    const bool glow = glow_reach && glow_rows;
    bindContrast(program, view.mContrast, view.mRimWidth, backdrop);
    program.bindTexture(LLShaderMgr::SELECTION_ID_MAP, &ids, ALSamplers::PointClamp);
    program.bindDepthTexture(LLShaderMgr::DEFERRED_DEPTH, &ids);
    program.bindTexture(LLShaderMgr::SELECTION_JUMP_MAP, &jumps, ALSamplers::PointClamp);
    program.bindTexture(LLShaderMgr::SELECTION_TILE_MAP, &tiles, ALSamplers::PointClamp, 0);
    program.bindTexture(LLShaderMgr::SELECTION_TILE_DEPTH_MAP, &tiles, ALSamplers::PointClamp, 1);
    if (glow)
    {
        program.bindTexture(LLShaderMgr::SELECTION_GLOW_REACH_MAP, glow_reach, ALSamplers::PointClamp);
        program.bindTexture(LLShaderMgr::SELECTION_GLOW_ROW_MAP, glow_rows, ALSamplers::PointClamp);
    }
    else
    {
        // Not read without a glow; left empty rather than holding whatever the units last had.
        program.unbindTexture(LLShaderMgr::SELECTION_GLOW_REACH_MAP);
        program.unbindTexture(LLShaderMgr::SELECTION_GLOW_ROW_MAP);
    }
    const S32 channel = program.enableTexture(LLShaderMgr::SELECTION_PALETTE);
    if (channel > -1)
    {
        gGL.getTextureSlot(channel)->bindManual(ALTextureSlot::TT_TEXTURE, palette, gGL.getSampler(ALSamplers::PointClamp));
    }
    program.uniform1i(LLShaderMgr::OUTLINE_WIDTH, llmax(view.mWidth, 1));
    program.uniform1i(LLShaderMgr::OUTLINE_INNER_WIDTH, llmax(view.mInnerWidth, 1));
    // A radius of 0 is no glow: the pass reaches no further than the lines.
    program.uniform1i(LLShaderMgr::OUTLINE_GLOW_RADIUS, glow ? llclamp(view.mGlowRadius, 1, MAX_GLOW_PIXELS) : 0);
    program.uniform1f(LLShaderMgr::OUTLINE_GLOW_BRIGHTNESS, view.mGlowBrightness);

    if (rects)
    {
        // The rects do not overlap, so no pixel is blended twice.
        for (const LLRect& rect : *rects)
        {
            LLGLSScissor scissor(vp_x + rect.mLeft, vp_y + rect.mBottom, rect.getWidth(), rect.getHeight());
            drawTriangle(triangle, &view.mProjection);
        }
    }
    else
    {
        drawTriangle(triangle, &view.mProjection);
    }

    program.unbindTexture(LLShaderMgr::SELECTION_BACKDROP_MAP);
    program.unbindTexture(LLShaderMgr::SELECTION_PALETTE);
    program.unbindTexture(LLShaderMgr::SELECTION_GLOW_ROW_MAP);
    program.unbindTexture(LLShaderMgr::SELECTION_GLOW_REACH_MAP);
    program.unbindTexture(LLShaderMgr::SELECTION_TILE_DEPTH_MAP);
    program.unbindTexture(LLShaderMgr::SELECTION_TILE_MAP);
    program.unbindTexture(LLShaderMgr::SELECTION_JUMP_MAP);
    program.unbindTexture(LLShaderMgr::DEFERRED_DEPTH);
    program.unbindTexture(LLShaderMgr::SELECTION_ID_MAP);
}
