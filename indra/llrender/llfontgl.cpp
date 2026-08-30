/**
 * @file llfontgl.cpp
 * @brief Wrapper around FreeType
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llfontgl.h"

// Linden library includes
#include "llfasttimer.h"
#include "llfontfreetype.h"
#include "llfontbitmapcache.h"
#include "llfontregistry.h"
#include "alfontshaping.h"
#include "llgl.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llstl.h"
#include "v4color.h"
#include "lltexture.h"
#include "lldir.h"
#include "llstring.h"
#include "llshadermgr.h"

// Third party library includes
#include <boost/tokenizer.hpp>

#if LL_WINDOWS
#include <Shlobj.h>
#include <Knownfolders.h>
#include <Objbase.h>
#endif // LL_WINDOWS

const S32 BOLD_OFFSET = 1;

// static class members
F32 LLFontGL::sVertDPI = 96.f;
F32 LLFontGL::sHorizDPI = 96.f;
F32 LLFontGL::sScaleX = 1.f;
F32 LLFontGL::sScaleY = 1.f;
S32 LLFontGL::sResolutionGeneration = 0;
bool LLFontGL::sFontsXmlDirty = false;
bool LLFontGL::sDisplayFont = true ;
bool LLFontGL::sUseDarkEmojiPalette = false;
bool LLFontGL::sForceMonochromeEmoji = false;
std::string LLFontGL::sAppDir;

LLColor4 LLFontGL::sShadowColor(0.f, 0.f, 0.f, 1.f);
bool     LLFontGL::sEnableShaderShadow = false;
LLFontRegistry* LLFontGL::sFontRegistry = NULL;

LLCoordGL LLFontGL::sCurOrigin;
F32 LLFontGL::sCurDepth;
std::vector<std::pair<LLCoordGL, F32> > LLFontGL::sOriginStack;

const F32 PAD_UVY = 0.5f; // half of vertical padding between glyphs in the glyph texture
const F32 DROP_SHADOW_SOFT_STRENGTH = 0.3f;

namespace
{
    // Slice-local result of itemizing + shaping a measurement window. Every
    // position here is a byte offset. Each
    // pair in `ranges` is [first, second) within `slice`; `glyphs[i]` points
    // into the global shape LRU and is non-null when `ranges[i]` produced
    // glyphs (which is always the case for shapeLine — empty results are
    // cached, so the pointer is valid even when the vector is empty).
    //
    // Cluster values inside the referenced glyph vectors are slice-local
    // RELATIVE TO `ranges[i].first` — i.e. each shape result is independently
    // 0-based. Callers that need positions in the slice's coordinate system
    // add `ranges[i].first`; callers that need wstr-coord positions add
    // both `ranges[i].first` and the slice's own offset within wstr.
    //
    // The pointers are valid until the next shape* call or clearCache. Since
    // the four measurement paths each hold a layout for the duration of one
    // call and don't fire other shape* in between, this is safe in practice.
    struct ShapeLayout
    {
        // One range, always: the slice is shaped end to end. Two vectors used
        // to hold that, from a design that partitioned the slice per face
        // before shape_sub_run's feature plan made the partition unnecessary.
        // Holding one element in two heap allocations cost a malloc and a free
        // apiece on every draw and every measurement.
        //
        // `glyphs` is null for an empty slice or a null face.
        size_t begin = 0;
        size_t end   = 0;
        const std::vector<ALShapedGlyph>* glyphs = nullptr;
        // Shape-cache mutation count at build time. The glyph pointer is
        // valid only while this matches ALFontShaping::cacheMutationCount();
        // holders llassert equality after their last dereference so a
        // use-after-invalidation trips a debug assert instead of reading
        // freed glyph runs.
        size_t mutation_snapshot = 0;
    };

    // Width of a shaped run in scaled pixels, including the extent overhang of
    // whichever glyph reaches furthest past its own advance. The one place that
    // arithmetic lives: renderBytes needs the same number to place a
    // right-aligned or centred string, and deriving it twice from the same
    // glyphs is how the drawn position and the measured one drift apart.
    F32 shaped_run_width(const LLFontFreetype* ft,
                         const std::vector<ALShapedGlyph>& glyphs,
                         bool no_padding, bool subpixel_pen)
    {
        F32 cur_x   = 0.f;
        F32 padding = 0.f;
        for (const ALShapedGlyph& sg : glyphs)
        {
            const LLFontGlyphInfo* gi = ft->getGlyphInfoByIndex(
                sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
            if (!gi)
                continue;
            if (!no_padding)
            {
                padding = llmax(0.f,
                                padding - sg.x_advance,
                                (F32)(gi->mWidth + gi->mXBearing) - sg.x_advance);
            }
            cur_x += sg.x_advance;
            // Match render()'s pen motion so caret positions and ellipsis
            // cutoffs agree with what is drawn.
            if (!subpixel_pen)
                cur_x = (F32)ll_round(cur_x);
        }
        return no_padding ? cur_x : (cur_x + padding);
    }

    // Build the shape layout for `slice` against `root_face`. One
    // all-encompassing range, shaped end-to-end through HarfBuzz. The
    // monospace feature plan in shape_sub_run (kern + ligatures off for
    // strict-mono, kern off only for ligatures-opt-in) preserves column
    // alignment without a separate codepoint partition. Empty slice or
    // null face: empty layout, no allocations.
    ShapeLayout build_shape_layout(const LLFontFreetype* root_face,
                                   std::string_view      slice)
    {
        ShapeLayout out;
        // Snapshot up front so the empty-layout early return below carries
        // the live mutation count too — a default 0 would trip the holders'
        // validity assert on every empty-label measurement (LLButton::resize
        // with "" at startup) once anything had ever shaped.
        out.mutation_snapshot = ALFontShaping::cacheMutationCount();
        if (!root_face || slice.empty())
            return out;

        out.begin  = 0;
        out.end    = slice.size();
        out.glyphs = &ALFontShaping::shapeLine(root_face, slice, out.begin, out.end);

        // Re-snapshot AFTER the shapeLine call — it may itself mutate
        // (miss-insert), which is fine: only mutations after this point
        // invalidate the pointer collected above.
        out.mutation_snapshot = ALFontShaping::cacheMutationCount();
        return out;
    }
}

LLFontGL::LLFontGL()
{
}

LLFontGL::~LLFontGL()
{
}

void LLFontGL::reset()
{
    mFontFreetype->reset(sVertDPI, sHorizDPI);
}

void LLFontGL::destroyGL()
{
    mFontFreetype->destroyGL();
}

bool LLFontGL::loadFace(const std::string& filename, F32 point_size, const F32 vert_dpi, const F32 horz_dpi, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags, const ALFontVarAxes& var_axes)
{
    if(mFontFreetype == reinterpret_cast<LLFontFreetype*>(NULL))
    {
        mFontFreetype = new LLFontFreetype;
    }

    return mFontFreetype->loadFace(filename, point_size, vert_dpi, horz_dpi, is_fallback, face_n, hinting, flags, var_axes);
}

S32 LLFontGL::getNumFaces(const std::string& filename)
{
    // Pure file probe — no instance state involved, so don't allocate a
    // throwaway LLFontFreetype just to count collection faces.
    return LLFontFreetype::getNumFaces(filename);
}

U64 LLFontGL::getCacheGeneration() const
{
    // Every render() call may sample from this font's head atlas AND from
    // each fallback face's atlas (e.g. emoji glyphs in an otherwise
    // grayscale string) — and from nothing else: glyphs route through
    // getGlyphInfo* whose mSourceFace is always the head face or a chain
    // member. Summing the per-instance generation of exactly those caches
    // gives a per-font invalidation stamp: it ticks for any mutation that
    // can stale this font's captured UVs and stays put for unrelated
    // fonts' glyph churn.
    //
    // Monotonic: each component only ever takes fresh values from the
    // shared LLFontBitmapCache::sNextGeneration counter, so every mutation
    // (including a face wrapper swap on reload, whose new cache starts at
    // a value above everything previously issued) strictly increases the
    // sum — no A+1/B-1 aliasing. addFallbackFont growing the chain adds a
    // component, which also only increases it.
    //
    // This used to return the global counter, which invalidated EVERY
    // cached text buffer viewer-wide whenever any font rasterized a glyph;
    // during glyph churn (first CJK chat fill, post-eviction warm-up) each
    // regen rasterized more glyphs and re-invalidated everything again for
    // several frames.
    const LLFontFreetype* ft = mFontFreetype.get();
    if (!ft)
        return 0;

    // Memoized, because the callers ask on the path where the answer is
    // "nothing changed, replay what you have" -- every cached-text widget,
    // every frame -- and answering meant walking the whole fallback chain,
    // which for the default SansSerif is a handful of faces before the OS
    // ones attach at runtime.
    //
    // Two things can move the sum. A component takes a new value, which it can
    // only draw from the global counter, so the counter moving is a necessary
    // condition. Or the chain gains a face that already had a cache, which
    // moves no counter at all -- so its length is part of the key too.
    const S32    global_gen = LLFontBitmapCache::getGlobalGeneration();
    const size_t chain_len  = ft->getFallbackFonts().size();
    if (mCacheGenValid && mCacheGenGlobal == global_gen && mCacheGenChain == chain_len)
    {
        return mCacheGenSum;
    }
    // U64 accumulator: components are non-negative S32s drawn from the
    // monotonic global counter, so the unsigned 64-bit sum can't overflow
    // within any reachable session and the comparison contract in the
    // vertex/width buffer caches stays exact.
    U64 gen = 0;
    if (const LLFontBitmapCache* cache = ft->getFontBitmapCache())
        gen += (U64)cache->getCacheGeneration();
    for (const auto& fb : ft->getFallbackFonts())
    {
        if (fb.first)
        {
            if (const LLFontBitmapCache* cache = fb.first->getFontBitmapCache())
                gen += (U64)cache->getCacheGeneration();
        }
    }

    mCacheGenSum    = gen;
    mCacheGenGlobal = global_gen;
    mCacheGenChain  = chain_len;
    mCacheGenValid  = true;
    return gen;
}

// Where a rect-anchored draw starts, which is the only thing the rect forms do
// beyond handing off to the x/y ones.
static void origin_from_rect(const LLRectf& rect, LLFontGL::VAlign valign, F32& x, F32& y)
{
    x = rect.mLeft;

    switch(valign)
    {
    case LLFontGL::TOP:
        y = rect.mTop;
        break;
    case LLFontGL::VCENTER:
        y = rect.getCenterY();
        break;
    case LLFontGL::BASELINE:
    case LLFontGL::BOTTOM:
        y = rect.mBottom;
        break;
    default:
        y = rect.mBottom;
        break;
    }
}

S32 LLFontGL::renderBytes(std::string_view utf8text, S32 begin_offset, const LLRect& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
    ShadowType shadow, S32 max_bytes, F32* right_x, bool use_ellipses, bool use_color) const
{
    LLRectf rect_float((F32)rect.mLeft, (F32)rect.mTop, (F32)rect.mRight, (F32)rect.mBottom);
    return renderBytes(utf8text, begin_offset, rect_float, color, halign, valign, style, shadow, max_bytes, right_x, use_ellipses, use_color);
}

S32 LLFontGL::renderBytes(std::string_view utf8text, S32 begin_offset, const LLRectf& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                          ShadowType shadow, S32 max_bytes, F32* right_x, bool use_ellipses, bool use_color) const
{
    F32 x, y;
    origin_from_rect(rect, valign, x, y);
    return renderBytes(utf8text, begin_offset, x, y, color, halign, valign, style, shadow, max_bytes, (S32)rect.getWidth(), right_x, use_ellipses, use_color);
}


S32 LLFontGL::renderBytes(std::string_view utf8text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                          ShadowType shadow, S32 max_bytes, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color, pass_boundary_cb_t on_pass_boundary) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if(!sDisplayFont) //do not display texts
    {
        return static_cast<S32>(utf8text.length());
    }

    if (utf8text.empty() || begin_offset < 0 || begin_offset >= (S32)utf8text.length())
    {
        return 0;
    }

    // Atlas-sheet eviction now runs once per frame from
    // LLFontGL::sweepGlyphCaches (driven by LLViewerWindow::checkSettings),
    // not here. Doing it inside render() forced every glyph render to
    // pay the throttle-check overhead and risked racing eviction with
    // glyph pointers active inside the same render call.


    S32 scaled_max_pixels = max_pixels == S32_MAX ? S32_MAX : llceil((F32)max_pixels * sScaleX);

    // determine which style flags need to be added programmatically by stripping off the
    // style bits that are drawn by the underlying Freetype font
    U8 style_to_add = (style | mFontDescriptor.getStyle()) & ~mFontFreetype->getStyle();

    // Shadow alpha and shadow type both depend only on per-render-call inputs
    // (foreground luminance, foreground alpha, requested shadow style). Compute
    // them once here so drawGlyph stays in the per-glyph hot path with no
    // luminance/alpha math. Shadow RGB is always sShadowColor (essentially black);
    // only alpha varies.
    F32 drop_shadow_strength = 0.f;
    LLColor4U precomputed_shadow_color = LLFontGL::sShadowColor;
    precomputed_shadow_color.mV[VALPHA] = 0;
    if (shadow != NO_SHADOW)
    {
        F32 luminance;
        color.calcHSL(NULL, NULL, &luminance);
        drop_shadow_strength = clamp_rescale(luminance, 0.35f, 0.6f, 0.f, 1.f);
        if (luminance < 0.35f)
        {
            shadow = NO_SHADOW;
        }
        else
        {
            const F32 soft_scale = (shadow == DROP_SHADOW_SOFT) ? DROP_SHADOW_SOFT_STRENGTH : 1.0f;
            const LLColor4U fg_u(color);
            precomputed_shadow_color.mV[VALPHA] = U8(fg_u.mV[VALPHA] * drop_shadow_strength * soft_scale);
        }
    }

    gGL.pushUIMatrix();

    gGL.loadUIIdentity();

    LLVector2 origin(floorf(sCurOrigin.mX*sScaleX), floorf(sCurOrigin.mY*sScaleY));

    // Depth translation, so that floating text appears 'in-world'
    // and is correctly occluded.
    gGL.translatef(0.f,0.f,sCurDepth);

    S32 chars_drawn = 0;
    S32 i;
    S32 length;

    if (-1 == max_bytes)
    {
        max_bytes = length = (S32)utf8text.length() - begin_offset;
    }
    else
    {
        length = llmin((S32)utf8text.length() - begin_offset, max_bytes );
    }

    // Nothing past an embedded NUL is ever drawn -- the walk below stops there
    // -- so nothing past it is worth shaping either, and stopping here is also
    // what lets the width be taken from the same glyphs the draw uses:
    // getWidthF32Bytes trims at the NUL, and a run measured past one would
    // place a right-aligned string by text that never appears.
    {
        S32 trimmed = 0;
        while (trimmed < length && utf8text[begin_offset + trimmed] != 0)
            ++trimmed;
        length = trimmed;
    }

    F32 cur_x, cur_y, cur_render_x, cur_render_y;

    // Not guaranteed to be set correctly
    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    // Subpixel pen position: cumulative advance stays fractional through the
    // loop so HarfBuzz GPOS kerning isn't crushed by per-glyph ll_round. Each
    // glyph still snaps to integer pixels at draw time via ll_round on
    // glyph_x in the screen_rect construction below — atlas bitmaps are
    // pixel-aligned, only the pen position is sub-pixel. False for
    // HINTING_DEFAULT (foundry hints align to integer grid), true for
    // FORCE_AUTOHINT and NO_HINTING.
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    cur_x = ((F32)x * sScaleX) + origin.mV[VX];
    cur_y = ((F32)y * sScaleY) + origin.mV[VY];

    // Offset y by vertical alignment.
    // use unscaled font metrics here
    switch (valign)
    {
    case TOP:
        cur_y -= llceil(mFontFreetype->getAscenderHeight());
        break;
    case BOTTOM:
        cur_y += llceil(mFontFreetype->getDescenderHeight());
        break;
    case VCENTER:
        cur_y -= llceil((llceil(mFontFreetype->getAscenderHeight()) - llceil(mFontFreetype->getDescenderHeight())) / 2.f);
        break;
    case BASELINE:
        // Baseline, do nothing.
        break;
    default:
        break;
    }

    // halign-RIGHT/HCENTER, the underline rule, and the ellipsis overflow
    // check all want the same unscaled string width. Compute once and
    // reuse — each call walks the string and (for non-mono fonts) does a
    // shape-cache lookup, so three calls on the worst-case path is real
    // measurement work paid for nothing.
    const bool needs_string_width = (halign == RIGHT) || (halign == HCENTER)
                                  || (style_to_add & UNDERLINE) || use_ellipses;

    // The slice is shaped here rather than further down, so the width comes
    // out of the glyphs this draw is about to lay down instead of a second
    // shape and a second walk of the same text. getWidthF32Bytes did both, on
    // every button label and every scroll-list cell.
    const std::string_view slice = utf8text.substr((size_t)begin_offset, (size_t)length);
    const ShapeLayout layout = build_shape_layout(mFontFreetype, slice);

    F32 string_width_unscaled = 0.f;
    if (needs_string_width)
    {
        // Shaping failing leaves nothing to measure from, so that case still
        // asks the codepoint walk -- which is where it lives.
        string_width_unscaled = (layout.glyphs && !layout.glyphs->empty())
            ? shaped_run_width(mFontFreetype, *layout.glyphs,
                               /*no_padding=*/false, subpixel_pen) / sScaleX
            : getWidthF32Bytes(utf8text, begin_offset, length);
    }
    const S32 scaled_string_width = ll_round(string_width_unscaled * sScaleX);

    switch (halign)
    {
    case LEFT:
        break;
    case RIGHT:
        cur_x -= llmin(scaled_max_pixels, scaled_string_width);
        break;
    case HCENTER:
        cur_x -= llmin(scaled_max_pixels, scaled_string_width) / 2;
        break;
    default:
        break;
    }

    cur_render_y = cur_y;
    cur_render_x = cur_x;

    F32 start_x = (F32)ll_round(cur_x);

    if (style_to_add & UNDERLINE)
    {
        // Draw the underline BEFORE glyph emission so descenders ('g',
        // 'y', 'p', 'q', 'j') sit on top of the rule rather than being
        // crossed by it — typographically correct and what every text
        // engine produces. Captured-list path (LLFontVertexBuffer) gets
        // an underline batch as the first entry in mForegroundBufferList,
        // so replay draws it first too. Non-capture mode just streams it
        // ahead of the glyph batches.
        //
        // Width: getWidthF32 is the same per-pen-accumulation walk the
        // render loop uses, clamped to scaled_max_pixels for the ellipses
        // path. Mirrors halign-RIGHT/HCENTER's existing math (lines 286,
        // 289). For the rare overflow-without-ellipses case the rule may
        // extend slightly past the last visible glyph; the typography
        // win on common usage outweighs that corner.
        //
        // Texture: sWhiteTexture sample = vec4(1,1,1,1), so vertex_color
        // multiplied through the standard ui shader produces a solid
        // colored stroke. Going through TRIANGLES + textured-quad keeps
        // the pipeline uniform with glyphs (the legacy LINES + unbind
        // sequence captured a texName=0 batch that re-played as an
        // unbind→sWhiteTexture dance and rendered inconsistently across
        // drivers). Using the face's own underline_position /
        // underline_thickness gives a typographically correct stroke
        // instead of a fixed 1px line stuck at the descender depth.
        const S32 underline_width = llmin(scaled_max_pixels, scaled_string_width);
        const F32 end_x = start_x + (F32)underline_width;
        const F32 y_bot = cur_y + mFontFreetype->getUnderlinePosition();
        const F32 y_top = y_bot + mFontFreetype->getUnderlineThickness();

        LLColor4U col(color);
        gGL.getTextureSlot(0)->bindManual(ALTextureSlot::TT_TEXTURE, ALTextureSlot::sWhiteTexture);
        gGL.color4ubv(col.mV);
        gGL.begin(LLRender::TRIANGLES);
        gGL.vertex2f(start_x, y_bot);
        gGL.vertex2f(end_x,   y_bot);
        gGL.vertex2f(start_x, y_top);
        gGL.vertex2f(end_x,   y_bot);
        gGL.vertex2f(end_x,   y_top);
        gGL.vertex2f(start_x, y_top);
        gGL.end();
    }

    // After the ALFontFace move, atlas ownership is per source face — heads
    // bind the atlas of whichever face produced each glyph. Track the current
    // (face, atlas) pair as we walk glyphs and flip on transitions. The
    // initial (face, cache, inv_width, inv_height) captures are deferred to
    // the first glyph.
    const ALFontFace*        current_face = nullptr;
    const LLFontBitmapCache* font_bitmap_cache = mFontFreetype->getBitmapCache();
    F32 inv_width  = font_bitmap_cache ? 1.f / font_bitmap_cache->getBitmapWidth()  : 0.f;
    F32 inv_height = font_bitmap_cache ? 1.f / font_bitmap_cache->getBitmapHeight() : 0.f;

    // textShadowMode is pushed once before pass A starts and reset to
    // passthrough (0) before pass B's foreground emission. It is the only
    // shadow uniform by design: per-pass constant, so the captured-buffer
    // replay (LLFontVertexBuffer::renderBuffers re-pushes it; LLVertexBufferData
    // doesn't capture uniforms) stays correct. Everything that varies per
    // batch in a mixed-atlas string — texel size of the bound atlas, alpha
    // channel layout — derives from the bound texture inside uiF.glsl
    // (textureSize + the RG-swizzle/.a sampling), so glyphs from
    // differently-sized head and fallback atlases all shadow correctly.
    // Skipped when sEnableShaderShadow is off (legacy multi-quad emission
    // still drives shadow geometry — shader stays at textShadowMode=0).
    const bool push_shader_shadow_uniforms =
        sEnableShaderShadow && (shadow != NO_SHADOW) && LLGLSLShader::sCurBoundShaderPtr;
    if (push_shader_shadow_uniforms)
    {
        const int mode = (shadow == DROP_SHADOW) ? 1 : 2; // SOFT
        LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::TEXT_SHADOW_MODE, mode);
    }

    bool draw_ellipses = false;
    if (use_ellipses)
    {
        // Use the hoisted string_width — it was computed with `length`
        // (clamped against max_chars), which is what we actually render,
        // so it's the right comparison.
        if (scaled_string_width > scaled_max_pixels)
        {
            // use four dots for ellipsis width to generate padding
            static const std::string dots("....");
            scaled_max_pixels = llmax(0, scaled_max_pixels - ll_round(getWidthF32(dots)));
            draw_ellipses = true;
        }
    }


    // string can have more than one glyph per char (ex: bold or shadow),
    // make sure that GLYPH_BATCH_SIZE won't end up with half a symbol.
    // See drawGlyph.
    // Ex: with shadows it's 6 glyps per char. 120 stays a multiple of 6
    // (20 chars at worst-case shadow expansion, 120 chars in the common
    // single-glyph path) and at 720 vertices per batch fills GL submit
    // payloads that 30 was leaving on the table.
    static constexpr S32 GLYPH_BATCH_SIZE = 120;
    static thread_local LLVector4a vertices[GLYPH_BATCH_SIZE * 6];
    static thread_local LLVector2 uvs[GLYPH_BATCH_SIZE * 6];
    static thread_local LLColor4U colors[GLYPH_BATCH_SIZE * 6];

    // Italic slant_offset depends only on style; hoist it out of the per-glyph
    // path so drawGlyphShadow/drawGlyphForeground don't recompute it.
    const F32 slant_offset = (style_to_add & ITALIC) ? (-mFontFreetype->getAscenderHeight() * 0.2f) : 0.f;

    // Two-pass split when the caller asked for a shadow: pass A walks the
    // string and emits shadow geometry (or nothing, when the luminance gate
    // above force-disabled it) plus per-glyph metadata into `deferred`; pass B
    // walks `deferred` and emits foreground geometry. The boundary callback
    // (if any) fires between the passes so LLFontVertexBuffer can swap capture
    // lists, giving each pass its own captured stream with a uniform color
    // across all vertices — the structural prerequisite for color-only cache
    // regen. We key on the *original* requested shadow type so the callback
    // contract matches genBuffers' list setup; dark text just emits an empty
    // shadow pass.
    const bool needs_two_pass = (on_pass_boundary != nullptr) || (shadow != NO_SHADOW);
    struct DeferredGlyph
    {
        LLRectf screen_rect;
        LLRectf uv_rect;
        std::pair<EFontGlyphType, S32> bitmap_entry;
        const ALFontFace* face;
        LLColor4U color;
    };
    // Reused rather than built per call: every shadowed draw in the UI takes
    // the two-pass path, so this was an allocate-and-free on each one. The
    // ellipsis tail below DOES re-enter this function, but only after pass B
    // has finished walking `deferred` -- nothing reads it again afterwards,
    // so the recursive clear is harmless. Keep it that way: a read of
    // `deferred` placed after the ellipsis render would see the tail's glyphs.
    static thread_local std::vector<DeferredGlyph> deferred;
    deferred.clear();
    if (needs_two_pass)
    {
        deferred.reserve(length);
    }

    LLColor4U text_color(color);
    // Preserve the transparency to render fading emojis in fading text (e.g.
    // for the chat console)... HB
    LLColor4U emoji_color(255, 255, 255, text_color.mV[VALPHA]);

    std::pair<EFontGlyphType, S32> bitmap_entry = std::make_pair(EFontGlyphType::Grayscale, -1);
    S32 glyph_count = 0;

    // Atlas texture the pending batch's UVs were built against. Every batch
    // submit re-asserts this binding: rasterizing a cache-missed glyph between
    // accumulation and flush rebinds unit 0 to the upload target and leaves it
    // bound (LLImageGL::setSubImage uploads with skip_unbind, and nextOpenPos
    // binds brand-new sheets to create their GL textures), so the texture
    // bound at flush time is NOT necessarily the one the pending quads were
    // built for. Submitting under the stomped binding samples another atlas
    // page — glyphs from unrelated text — which is exactly the legacy
    // "CJK/emoji on first render" corruption the old per-codepoint
    // `last_char != wch` flush band-aided around (the per-char flush kept the
    // pending batch to ~1 glyph, making the misdraw practically invisible).
    // bind() is a cached no-op when the binding didn't move, so the re-assert
    // costs nothing on the common path.
    LLImageGL* batch_image = nullptr;
    auto flush_batch = [&]()
    {
        if (glyph_count > 0)
        {
            if (batch_image)
            {
                gGL.getTextureSlot(0)->bindSampled(batch_image, ALSamplers::PointWrap);
            }
            gGL.begin(LLRender::TRIANGLES);
            gGL.vertexBatchPreTransformed(vertices, uvs, colors, glyph_count * 6);
            gGL.end();
            glyph_count = 0;
        }
    };

    // Where a glyph's bitmap lands for a given pen position. Shared so the
    // cluster measurement below and the emission that follows it cannot drift
    // apart: they have to agree on the subpixel phase, since the phase decides
    // which slot is consulted and so how wide the glyph is.
    auto place_glyph = [](const LLFontGlyphInfo* gi, F32 pen, U8& phase, S32& dest_int_x)
    {
        if (gi->mPhaseCount > 1)
        {
            const F32 frac_x = pen - floorf(pen);
            const U32 raw =
                (U32)floorf(frac_x * (F32)LLFontGlyphInfo::kNumPhases + 0.5f);
            if (raw >= LLFontGlyphInfo::kNumPhases)
            {
                phase = 0;
                dest_int_x = (S32)floorf(pen) + 1;
            }
            else
            {
                phase = (U8)raw;
                dest_int_x = (S32)floorf(pen);
            }
        }
        else
        {
            phase = 0;
            dest_int_x = ll_round(pen);
        }
    };

    // Itemize + shape the slice via the shared helper. Strict-monospace
    // gets emoji-cluster ranges so ASCII keeps the codepoint path's exact
    // metrics (visual parity with toggle-off) while embedded emoji
    // clusters still shape; the non-mono path gets a single range covering
    // the whole slice. Layout's ranges are slice-local; we compare against
    // `i - begin_offset` in the loop.
    // Constant for the whole draw, and it was being derived again for every
    // glyph -- twice in the innermost loop, once more per cluster measured.
    const EFontGlyphType glyph_type = (!use_color || LLFontGL::sForceMonochromeEmoji)
                                    ? EFontGlyphType::Grayscale : EFontGlyphType::Color;

    bool shape_run_taken = false;

    S32 next_i = begin_offset;
    for (i = begin_offset; i < begin_offset + length; i = next_i)
    {
        const LLCodepointAt at = utf8str_decode_at(utf8text, (size_t)i);
        next_i = (S32)at.next;
        const S32 i_slice = i - begin_offset;
        // Entered a shaped run? Emit its HarfBuzz-positioned glyphs in one
        // go and jump past the range. When shaping produced no glyphs (rare —
        // face/HB failure) we fall through to the codepoint path so the
        // range still draws something, even if ZWJ presentation is wrong.
        if (!shape_run_taken && layout.glyphs && (S32)layout.begin == i_slice)
        {
            const std::pair<size_t, size_t> run_range{ layout.begin, layout.end };
            const auto& run_glyphs = *layout.glyphs;
            shape_run_taken = true;

            if (!run_glyphs.empty())
            {
                bool overflow = false;
                // Byte offset within the run of the glyph that overflowed.
                // shapeLine returns clusters relative to its begin parameter
                // (here, run_range.first), so this is in [0, run_len). Used so
                // chars_drawn reflects only what was reached before clipping —
                // counting the full run on overflow lies
                // to callers that drive word-wrap / chunked draw off the
                // return value.
                S32 overflow_cluster_local = 0;

                // Whether every glyph sharing the cluster that starts at
                // `first` fits, measured from the pen it would start at.
                // Several glyphs can carry one cluster -- a mark stack, a
                // Devanagari conjunct, an emoji and its variation selector --
                // and the pen moves inside one, so this walks the cluster the
                // way the emission does rather than assuming the marks are
                // weightless. A cluster drawn with only some of its glyphs is
                // not a clipped syllable; it is a different syllable.
                auto cluster_fits = [&](size_t first, F32 pen_from) -> bool
                {
                    const S32 cluster = run_glyphs[first].cluster;
                    F32 sim_x = pen_from;
                    F32 right = pen_from;
                    for (size_t k = first;
                         k < run_glyphs.size() && run_glyphs[k].cluster == cluster;
                         ++k)
                    {
                        const ALShapedGlyph& g = run_glyphs[k];
                        const LLFontGlyphInfo* gi = mFontFreetype->getGlyphInfoByIndex(
                            g.face, g.glyph_id, glyph_type);
                        // A glyph with no info is skipped whole by the loop
                        // below, pen included, so it is skipped here too.
                        if (!gi)
                            continue;

                        U8  sim_phase;
                        S32 sim_dest;
                        place_glyph(gi, sim_x + g.x_offset, sim_phase, sim_dest);
                        const auto& sim_slot = gi->mPhaseSlots[sim_phase];
                        right = llmax(right,
                                      (F32)(sim_dest + sim_slot.mXBearing) + (F32)sim_slot.mWidth);

                        sim_x += g.x_advance;
                        if (!subpixel_pen)
                            sim_x = (F32)ll_round(sim_x);
                    }
                    return (start_x + scaled_max_pixels) >= right;
                };

                S32 open_cluster = -1;
                for (size_t sg_index = 0; sg_index < run_glyphs.size(); ++sg_index)
                {
                    const ALShapedGlyph& sg = run_glyphs[sg_index];

                    // Tested once per cluster, before any of it is emitted.
                    // Testing per glyph clips between two glyphs of the same
                    // cluster, which paints a base without its mark and then
                    // reports the whole cluster as undrawn.
                    //
                    // Only a cluster that actually carries several glyphs needs
                    // the walk, and most do not -- every Latin cluster is one
                    // glyph. Walking regardless costs a second
                    // getGlyphInfoByIndex and a second placement for every
                    // glyph drawn, both of which the emission below is about to
                    // do anyway. The single-glyph case is tested further down
                    // from the values it already has.
                    const bool starts_cluster = (sg.cluster != open_cluster);
                    const bool shares_cluster = starts_cluster
                        && (sg_index + 1) < run_glyphs.size()
                        && run_glyphs[sg_index + 1].cluster == sg.cluster;
                    if (shares_cluster)
                    {
                        if (!cluster_fits(sg_index, cur_render_x))
                        {
                            overflow = true;
                            overflow_cluster_local = sg.cluster;
                            break;
                        }
                    }
                    if (starts_cluster)
                    {
                        open_cluster = sg.cluster;
                    }

                    // Cache lives on the root face and its bitmap atlas; the
                    // fallback face is only the *source* for the glyph. The
                    // codepoint path also routes through getGlyphInfoByIndex
                    // (after wch -> glyph_index resolution), so this lookup
                    // hits the same atlas slot regardless of which path
                    // produced the ALShapedGlyph.
                    const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                        sg.face, sg.glyph_id, glyph_type);
                    if (!sfgi)
                        continue;

                    // Quantize the pen x to 1/N px resolution and split into
                    // integer pixel + subpixel phase. Doing them together (not
                    // independently) keeps phase pick consistent with the
                    // integer dest position when the fractional part is close
                    // to 1 — round(frac * N) wrapping to N must also bump the
                    // integer pen by 1, otherwise the bitmap lands one pixel
                    // off the visual position the phase was rasterized for.
                    const F32 pen_x = cur_render_x + sg.x_offset;
                    const F32 pen_y = cur_render_y + sg.y_offset;
                    U8 phase;
                    S32 dest_int_x;
                    place_glyph(sfgi, pen_x, phase, dest_int_x);
                    const auto& slot = sfgi->mPhaseSlots[phase];

                    const ALFontFace* glyph_face = sfgi->mSourceFace;
                    std::pair<EFontGlyphType, S32> next_bitmap_entry = slot.mBitmapEntry;
                    if (glyph_face != current_face || next_bitmap_entry != bitmap_entry)
                    {
                        // Drain the queued glyphs under their own texture
                        // before switching the batch to the new one.
                        flush_batch();
                        bitmap_entry = next_bitmap_entry;
                        if (glyph_face != current_face)
                        {
                            current_face = glyph_face;
                            font_bitmap_cache = current_face ? current_face->getBitmapCache() : nullptr;
                            if (font_bitmap_cache)
                            {
                                inv_width  = 1.f / font_bitmap_cache->getBitmapWidth();
                                inv_height = 1.f / font_bitmap_cache->getBitmapHeight();
                            }
                        }
                        // Null when the slot's sheet has been released
                        // (shouldn't happen mid-render — eviction runs at the
                        // frame boundary and purges glyph entries first). The
                        // emission guard below skips the quad rather than
                        // sampling whatever texture happens to be bound.
                        batch_image = font_bitmap_cache
                            ? font_bitmap_cache->getImageGL(bitmap_entry.first, bitmap_entry.second)
                            : nullptr;
                        if (batch_image)
                        {
                            gGL.getTextureSlot(0)->bindSampled(batch_image, ALSamplers::PointWrap);
                        }
                    }

                    const F32 glyph_x = (F32)(dest_int_x + slot.mXBearing);
                    const F32 glyph_y = (F32)ll_round(pen_y) + (F32)slot.mYBearing;

                    // The other half of the cluster test above. One glyph
                    // carrying a cluster means its own extent is the cluster's,
                    // so the answer is the lookup and the placement already
                    // done rather than a second pass over the same glyph.
                    if (starts_cluster && !shares_cluster
                        && (start_x + scaled_max_pixels) < (glyph_x + (F32)slot.mWidth))
                    {
                        overflow = true;
                        overflow_cluster_local = sg.cluster;
                        break;
                    }

                    if (batch_image)
                    {
                        LLRectf uv_rect(slot.mXBitmapOffset * inv_width,
                                        (slot.mYBitmapOffset + slot.mHeight + PAD_UVY) * inv_height,
                                        (slot.mXBitmapOffset + slot.mWidth) * inv_width,
                                        (slot.mYBitmapOffset - PAD_UVY) * inv_height);
                        LLRectf screen_rect(glyph_x,
                                            glyph_y,
                                            glyph_x + (F32)slot.mWidth,
                                            glyph_y - (F32)slot.mHeight);

                        if (glyph_count >= GLYPH_BATCH_SIZE)
                        {
                            flush_batch();
                        }

                        // Grayscale glyphs tint with text_color (the bitmap is a
                        // luminance / coverage mask). Color glyphs tint with
                        // emoji_color (white, preserving CPAL palette colors baked
                        // into the bitmap).
                        const LLColor4U& col = bitmap_entry.first == EFontGlyphType::Grayscale
                                             ? text_color : emoji_color;
                        if (needs_two_pass)
                        {
                            // BOLD suppresses shadow per the legacy drawGlyph contract
                            // (see FIXME at drawGlyphForeground): the bold doubled quad
                            // and the shadow taps are mutually exclusive. drawGlyphShadow
                            // doesn't see `style`, so gate the call here.
                            if (!(style_to_add & BOLD))
                            {
                                drawGlyphShadow(glyph_count, vertices, uvs, colors, screen_rect, uv_rect,
                                                precomputed_shadow_color, shadow, slant_offset);
                            }
                            deferred.push_back({screen_rect, uv_rect, bitmap_entry, current_face, col});
                        }
                        else
                        {
                            drawGlyphForeground(glyph_count, vertices, uvs, colors, screen_rect, uv_rect,
                                                col, style_to_add, slant_offset);
                        }
                    }

                    cur_x += sg.x_advance;
                    cur_y += sg.y_advance;
                    if (!subpixel_pen)
                        cur_x = (F32)ll_round(cur_x);
                    cur_render_x = cur_x;
                    cur_render_y = cur_y;
                }

                if (overflow)
                {
                    // Count the bytes strictly before the overflowing cluster.
                    // overflow_cluster_local is run-local (0-based within the
                    // run), so adding it directly gives that count.
                    chars_drawn += llmax(0, overflow_cluster_local);
                    break;
                }
                chars_drawn += (S32)(run_range.second - run_range.first);

                next_i = begin_offset + (S32)run_range.second;
                continue;
            }
        }

        // Nothing shaped at this position, and there is no second way to draw
        // it: shaping comes back empty only when the font has no face behind
        // it -- in which case there are no glyphs to be had either -- or when
        // the run held nothing but variation selectors the face does not
        // carry, which are meant to draw nothing.
        break;
    }

    // The layout's glyph pointers reach into the shape LRU; nothing inside
    // the loop may shape (glyph rasterization doesn't), or they dangle.
    llassert(layout.mutation_snapshot == ALFontShaping::cacheMutationCount());

    // End-of-pass flush. In single-pass mode this drains the foreground batch
    // and we're done. In two-pass mode this drains the shadow batch; pass B
    // below then walks the deferred metadata to emit foreground geometry.
    flush_batch();

    if (needs_two_pass)
    {
        // Pass-boundary callback: LLFontVertexBuffer uses this to close the
        // shadow capture list and open the foreground capture list. With no
        // callback this is a no-op (direct callers don't need separate lists).
        if (on_pass_boundary)
        {
            on_pass_boundary();
        }

        // Reset textShadowMode for foreground emission. Pass B's flushes (and any
        // subsequent UI rendering) take the shader's default-passthrough
        // branch.
        if (push_shader_shadow_uniforms)
        {
            LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::TEXT_SHADOW_MODE, 0);
        }

        // Pass B: emit foreground geometry from deferred metadata. Reset the
        // atlas-binding tracker; the first deferred glyph forces a (possibly
        // redundant, GL-driver-cheap) rebind to begin the foreground stream.
        // Only glyphs whose batch_image resolved in pass A made it into
        // `deferred`, so the getImageGL lookups below can only go null if a
        // sheet vanished mid-render — which the frame-boundary eviction
        // discipline rules out — but keep the same guard shape regardless.
        bitmap_entry = std::make_pair(EFontGlyphType::Grayscale, -1);
        current_face = nullptr;
        batch_image = nullptr;
        for (const DeferredGlyph& dg : deferred)
        {
            if (dg.face != current_face || dg.bitmap_entry != bitmap_entry)
            {
                flush_batch();
                bitmap_entry = dg.bitmap_entry;
                if (dg.face != current_face)
                {
                    current_face = dg.face;
                    font_bitmap_cache = current_face ? current_face->getBitmapCache() : nullptr;
                }
                batch_image = font_bitmap_cache
                    ? font_bitmap_cache->getImageGL(bitmap_entry.first, bitmap_entry.second)
                    : nullptr;
                if (batch_image)
                {
                    gGL.getTextureSlot(0)->bindSampled(batch_image, ALSamplers::PointWrap);
                }
            }

            if (!batch_image)
            {
                continue;
            }

            if (glyph_count >= GLYPH_BATCH_SIZE)
            {
                flush_batch();
            }

            drawGlyphForeground(glyph_count, vertices, uvs, colors,
                                dg.screen_rect, dg.uv_rect,
                                dg.color, style_to_add, slant_offset);
        }

        flush_batch();
    }

    if (right_x)
    {
        *right_x = (cur_x - origin.mV[VX]) / sScaleX;
    }

    if (draw_ellipses)
    {
        // recursively render ellipses at end of string
        // we've already reserved enough room.
        // NO_SHADOW for the ellipsis: the outer render's on_pass_boundary
        // already split the capture lists, and forwarding the callback would
        // double-fire it. Rendering the ellipsis without a shadow keeps the
        // captured streams consistent and avoids tinting the ellipsis shadow
        // with the foreground color in LLFontVertexBuffer.
        renderBytes("...",
                0,
                (cur_x - origin.mV[VX]) / sScaleX, (F32)y,
                color,
                LEFT, valign,
                style_to_add,
                NO_SHADOW,
                S32_MAX, max_pixels,
                right_x,
                false,
                use_color);
    }

    gGL.popUIMatrix();

    return chars_drawn;
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow, S32 max_bytes, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color) const
{
    return renderBytes(text, begin_offset, x, y, color, halign, valign, style, shadow, max_bytes, max_pixels, right_x, use_ellipses, use_color);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color) const
{
    return renderUTF8(text, begin_offset, (F32)x, (F32)y, color, LEFT, BASELINE, NORMAL, NO_SHADOW);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow) const
{
    return renderUTF8(text, begin_offset, (F32)x, (F32)y, color, halign, valign, style, shadow);
}

// font metrics - override for LLFontFreetype that returns units of virtual pixels
F32 LLFontGL::getAscenderHeight() const
{
    return mFontFreetype->getAscenderHeight() / sScaleY;
}

F32 LLFontGL::getDescenderHeight() const
{
    return mFontFreetype->getDescenderHeight() / sScaleY;
}

S32 LLFontGL::getLineHeight() const
{
    // Glyph bounding-box height (no foundry line gap). Single ceil avoids
    // the double-ceil over-estimate that older `ceil(asc) + ceil(desc)`
    // produced when both fractional parts were non-zero. For full
    // baseline-to-baseline distance including the font's recommended line
    // gap, use getLineSpacing().
    return llceil((mFontFreetype->getAscenderHeight() + mFontFreetype->getDescenderHeight()) / sScaleY);
}

S32 LLFontGL::getLineSpacing() const
{
    // Foundry-recommended baseline-to-baseline distance — face->height,
    // which is ascender + descender + lineGap. Used by multi-line text
    // layout so consecutive lines sit at the spacing the font designer
    // intended; for many screen fonts lineGap is 0 and this matches
    // getLineHeight().
    return llceil(mFontFreetype->getLineHeight() / sScaleY);
}

S32 LLFontGL::getWidth(std::string_view utf8text) const
{
    return getWidthBytes(utf8text, 0, S32_MAX);
}

S32 LLFontGL::getWidthBytes(std::string_view utf8text, S32 begin_offset, S32 max_bytes) const
{
    // llceil, not ll_round: getWidth's contract is "minimum integer pixel
    // width that contains the rendered text". With subpixel pen position
    // (mUseSubpixelPen), getWidthF32Bytes returns fractional widths; ll_round
    // would truncate fractions < 0.5 (e.g. 28.4 -> 28) and clip the last
    // glyph in callers that size layout rects to the returned width
    // (LLButton::resize, scroll list cells, tooltip backgrounds, etc.).
    return llceil(getWidthF32Bytes(utf8text, begin_offset, max_bytes));
}

F32 LLFontGL::getWidthF32(std::string_view utf8text) const
{
    return getWidthF32Bytes(utf8text, 0, S32_MAX);
}

F32 LLFontGL::getWidthF32Bytes(std::string_view utf8text, S32 begin_offset, S32 max_bytes, bool no_padding) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    const S32 text_len = (S32)utf8text.length();
    const S32 begin = llclamp(begin_offset, 0, text_len);
    // Clamp the upper bound the same way byteFromPixelOffset does. The default
    // max_bytes value is S32_MAX, so a non-zero begin_offset would otherwise
    // overflow the S32 sum (UB on signed overflow). Halign-right and underline
    // measurement paths in render() routinely call this with begin_offset > 0.
    const S32 max_index = llmin(text_len, begin + llmin(S32_MAX - begin, max_bytes));

    // Mirror render()'s pen accumulation policy so width measurements agree
    // with what's drawn — see the comment at render()'s subpixel_pen.
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    // Determine the tight slice we'll actually measure (bounded by max_bytes,
    // the view's own end, and the first embedded NUL) so shaping only runs over
    // real content.
    S32 measure_end = begin;
    while (measure_end < max_index && utf8text[measure_end] != 0)
        ++measure_end;
    const S32 measure_len = measure_end - begin;

    // Same shape-first preprocessing as render(); kept consistent so caret
    // positions and ellipsis cutoffs agree with what is drawn.
    std::string_view slice = utf8text.substr((size_t)begin, (size_t)measure_len);
    const ShapeLayout layout = build_shape_layout(mFontFreetype, slice);
    if (!layout.glyphs || layout.glyphs->empty())
    {
        // Nothing shaped, which happens two ways and has the same answer for
        // both. The face has no FT face behind it, so there are no metrics to
        // report; or the run was nothing but variation selectors that the face
        // does not carry, which were stripped before shaping and occupy no
        // width of their own.
        return 0.f;
    }

    const F32 width = shaped_run_width(mFontFreetype, *layout.glyphs,
                                       no_padding, subpixel_pen);
    // The glyph pointer must have stayed valid for the whole measurement.
    llassert(layout.mutation_snapshot == ALFontShaping::cacheMutationCount());

    return width / sScaleX;
}

void LLFontGL::generateASCIIglyphs()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    for (U32 i = 32; (i < 127); i++)
    {
        mFontFreetype->getGlyphInfo(i, EFontGlyphType::Grayscale);
    }
}

S32 LLFontGL::maxDrawableBytes(std::string_view utf8text, F32 max_pixels, S32 max_bytes, EWordWrapStyle end_on_word_boundary) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (utf8text.empty() || !utf8text[0] || max_bytes <= 0)
    {
        return 0;
    }

    // Everything below walks by offset up to max_bytes; the view's own end is the
    // other bound. Folding them together here keeps the walk inside the buffer
    // even when a caller asks for more text than it handed over.
    max_bytes = llmin(max_bytes, (S32)utf8text.length());

    llassert(max_pixels >= 0.f);

    bool clip = false;
    F32 cur_x = 0;

    // Where the line may end, per UAX #14. Tracked against the walk below so
    // that when the text clips we know the last place a break was allowed.
    S32 last_break = 0;

    // avoid S32 overflow when max_pixels == S32_MAX by staying in floating point
    F32 scaled_max_pixels = max_pixels * sScaleX;
    F32 width_padding = 0.f;

    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    // Pre-shape the entire slice so advance accounts for GPOS pair-kerning
    // and ligatures. The codepoint loop below uses a parallel walker
    // (shape_idx) to map shaped advances onto codepoints — a ligature
    // glyph spans multiple cps but its full advance fires on the cluster's
    // start cp, so trailing cps of the cluster contribute zero.
    // shapeLine returns a const ref into the shape LRU; clusters come back
    // slice-local (relative to begin=0), which equals 0..measure_end here.
    // Keep the ref bound for the lifetime of the loop — no other shape*
    // calls fire below, so the LRU entry can't be evicted underneath us.
    static const std::vector<ALShapedGlyph> sEmptyShape;
    const std::vector<ALShapedGlyph>* shape_glyphs = &sEmptyShape;
    size_t shape_idx = 0;

    S32 measure_end = 0;
    while (measure_end < max_bytes && utf8text[measure_end] != 0)
        ++measure_end;

    // Only the word-boundary styles ever read the break list. ANYWHERE is the
    // default argument, and asking ICU to enumerate every break in the window
    // so the answer can be discarded is a rule-engine pass for nothing.
    // The buffer outlives the call so a wrapping loop does not allocate per
    // line; nothing re-enters this function while `breaks` is live.
    static thread_local std::vector<size_t> breaks;
    const bool wants_breaks = (end_on_word_boundary != ANYWHERE);

    size_t break_idx = 0;
    size_t shape_gen = 0;
    bool   use_shaped = false;

    S32 i = 0;
    S32 next_i = 0;

    // Shape and walk the first `window` bytes, and say where the walk stopped.
    // Everything it touches lives in the enclosing scope because the answer is
    // read from there afterwards -- `clip` and `last_break` decide where a
    // clipped line retreats to. False means a glyph the font could not supply,
    // which the caller answers with zero rather than a measurement.
    auto measure_window = [&](S32 window) -> bool
    {
        clip          = false;
        cur_x         = 0.f;
        last_break    = 0;
        width_padding = 0.f;
        shape_idx     = 0;
        break_idx     = 0;
        shape_glyphs  = &sEmptyShape;

        if (window > 0)
        {
            std::string_view slice = utf8text.substr(0, (size_t)window);
            shape_glyphs = &ALFontShaping::shapeLine(mFontFreetype, slice, 0, (size_t)window);
            if (wants_breaks)
            {
                utf8str_line_break_opportunities(slice, breaks);
            }
        }
        if (!wants_breaks)
        {
            breaks.clear();
        }
        use_shaped = !shape_glyphs->empty();
        if (!use_shaped)
        {
            // Nothing shaped, so there is nothing to fit: either the font has
            // no face behind it, or the window held only variation selectors it
            // does not carry. Both answer with none of it drawn rather than
            // with a second measurement of the same text by other means.
            return false;
        }
        // shape_glyphs points into the shape LRU until the walk's last use.
        shape_gen = ALFontShaping::cacheMutationCount();
        (void)shape_gen;

    next_i = 0;
    for (i = 0; (i < window); i = next_i)
    {
        const LLCodepointAt at = utf8str_decode_at(utf8text, (size_t)i);
        next_i = (S32)at.next;
        llwchar wch = at.cp;

        if(wch == 0)
        {
            // Null terminator.  We're done.
            break;
        }

        // Carry the break cursor up to the last opportunity at or before i, so
        // a clip here knows where it may retreat to.
        while (break_idx < breaks.size() && breaks[break_idx] <= (size_t)i)
        {
            last_break = (S32)breaks[break_idx];
            ++break_idx;
        }

        if (use_shaped)
        {
            // Sum advances and the largest extent of any glyph whose cluster
            // lands on this character. Trailing characters of a multi-character
            // cluster (ligatures, ZWJ sequences) consume zero glyphs and
            // pass through with cur_x unchanged.
            F32 advance_this = 0.f;
            F32 extent_this  = 0.f;
            while (shape_idx < shape_glyphs->size()
                   && (*shape_glyphs)[shape_idx].cluster <= i)
            {
                const auto& sg = (*shape_glyphs)[shape_idx];
                advance_this += sg.x_advance;
                const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                    sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                if (sfgi)
                    extent_this = llmax(extent_this, (F32)(sfgi->mWidth + sfgi->mXBearing));
                ++shape_idx;
            }
            width_padding = llmax(0.f,
                                  width_padding - advance_this,
                                  extent_this - advance_this);
            cur_x += advance_this;
            if (scaled_max_pixels < cur_x + width_padding)
            {
                clip = true;
                break;
            }
            if (!subpixel_pen)
                cur_x = (F32)ll_round(cur_x);
        }
    }

    // No shape* call may fire while shape_glyphs is held (see the comment
    // at the shapeLine call above).
    llassert(shape_gen == ALFontShaping::cacheMutationCount());
    return true;
    };

    // Answering "how much fits in max_pixels" does not need the rest of the
    // document shaped. The wrapping callers hand this the whole remaining
    // segment and call it once per line, each time with a different suffix, so
    // each call was a fresh HarfBuzz pass and a fresh UAX #14 enumeration over
    // everything that was left -- quadratic in the length of a wrapped
    // paragraph, and a shape-cache entry per line holding all of it.
    //
    // Start from a window that comfortably covers the budget. A byte is never
    // more than a glyph and no glyph we ship advances less than a pixel, so a
    // byte per pixel is already generous for text that advances at all; text
    // that does not -- a long run of combining marks -- is what the widening
    // below is for. Widen only when the walk ran out of text before it ran out
    // of pixels, which is the one case the window can be wrong in.
    S32 window = measure_end;
    if (max_pixels < (F32)S32_MAX)
    {
        const F32 guess = max_pixels + 64.f;
        if (guess < (F32)measure_end)
        {
            window = (S32)guess;
        }
    }

    for (;;)
    {
        if (!measure_window(window))
        {
            return 0;
        }
        if (clip || window >= measure_end)
        {
            break;
        }
        window = (window > measure_end / 2) ? measure_end : (window * 2);
    }

    if( clip )
    {
        switch (end_on_word_boundary)
        {
        case ONLY_WORD_BOUNDARIES:
            i = last_break;
            break;
        case WORD_BOUNDARY_IF_POSSIBLE:
            // Zero means nothing on this line was a legal place to break, so
            // the caller gets the clip position and a mid-word split rather
            // than an empty line it would never make progress past.
            if (last_break != 0)
            {
                i = last_break;
            }
            break;
        default:
        case ANYWHERE:
            // do nothing
            break;
        }
    }
    // The walk only ever lands on character starts, so it can step past a
    // budget that stops inside one. Clamping to the budget would then hand back
    // an offset in the middle of a character, and the callers cut strings at
    // what they are given -- a segment whose own end falls mid-character is
    // enough to get here. Come back to the character that budget started in
    // instead: never more than was asked for, and never somewhere that splits.
    if (i <= max_bytes)
    {
        return i;
    }
    return (S32)utf8str_grapheme_align_backward(utf8text, (size_t)max_bytes);
}

S32 LLFontGL::firstDrawableByte(std::string_view utf8text, F32 max_pixels, S32 start_pos, S32 max_bytes) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (utf8text.empty() || !utf8text[0] || max_bytes <= 0)
    {
        return 0;
    }

    F32 total_width = 0.0;

    F32 scaled_max_pixels = max_pixels * sScaleX;
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    S32 start = llmin(start_pos, (S32)utf8text.length() - 1);
    if (start < 0)
    {
        return 0;
    }
    // start_pos may land inside a character; the run measured here begins at
    // that character's own start.
    while (start > 0 && ((unsigned char)utf8text[start] & 0xC0) == 0x80)
    {
        --start;
    }
    // And back to the start of the cluster holding it. A caller may not begin
    // drawing partway through one, so the cluster is the unit at both ends of
    // this walk -- and the extent lookup below keys on the cluster the shaper
    // reports, which is its first byte.
    start = (S32)utf8str_grapheme_align_backward(utf8text, (size_t)start);
    // Measure to the end of that cluster, not the end of the one character
    // there. A run cut after the first codepoint of a ZWJ sequence shapes as
    // its unligated parts, so the advances collected below are not the ones
    // render() lays down -- and the caller scrolls a field by them.
    const size_t measure_end = utf8str_step_grapheme_forward(utf8text, (size_t)start);

    // Pre-shape [0, measure_end) and collect the advance each cluster carries,
    // so the backward sweep below has something to walk. Ligatures and ZWJ
    // clusters get their full advance attributed to the cluster's first
    // character.
    //
    // Sparse on purpose, one entry per cluster in ascending byte order: a table
    // indexed by position would now be one float per BYTE rather than one per
    // character, which is memory this conversion is supposed to save. It also
    // states the thing the walk needs outright — a position that appears here
    // is a cluster start, and only a cluster start is somewhere the caller may
    // begin drawing from.
    // Reused rather than built per call: this runs on every cursor move that
    // scrolls a line editor, and the table is a few entries long.
    static thread_local std::vector<std::pair<S32, F32>> cluster_advance;
    cluster_advance.clear();
    F32 last_cluster_extent = 0.f;
    {
        std::string_view slice = utf8text.substr(0, measure_end);
        const auto& shape_glyphs = ALFontShaping::shapeLine(mFontFreetype, slice, 0, measure_end);
        const size_t shape_gen = ALFontShaping::cacheMutationCount();
        (void)shape_gen;
        cluster_advance.reserve(shape_glyphs.size());
        for (const auto& sg : shape_glyphs)
        {
            if (sg.cluster < 0 || (size_t)sg.cluster >= measure_end)
                continue;
            // HarfBuzz emits an LTR run with non-decreasing clusters, and the
            // direction is set LTR unconditionally, so glyphs sharing a cluster
            // arrive together and the table comes out ordered. Summing them as
            // they arrive replaces a sort and a compaction pass; the assert is
            // what says the ordering is being relied on rather than assumed.
            if (!cluster_advance.empty() && cluster_advance.back().first == sg.cluster)
            {
                cluster_advance.back().second += sg.x_advance;
            }
            else
            {
                llassert(cluster_advance.empty()
                         || cluster_advance.back().first < sg.cluster);
                cluster_advance.emplace_back(sg.cluster, sg.x_advance);
            }
            if (sg.cluster == start)
            {
                const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                    sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                if (sfgi)
                    last_cluster_extent = llmax(last_cluster_extent,
                                                (F32)(sfgi->mWidth + sfgi->mXBearing));
            }
        }
        // The fill above is shape_glyphs' last dereference — it must not
        // have been invalidated by a shape-cache mutation mid-hold.
        llassert(shape_gen == ALFontShaping::cacheMutationCount());
    }

    if (cluster_advance.empty())
    {
        // Nothing shaped, so there are no clusters to walk back through and no
        // metrics behind them. Drawing begins where it was asked to.
        return start;
    }

    // Where drawing may begin. Nothing fitting leaves it at the last cluster
    // rather than the last character: start is only a character start, and a
    // caller may not begin partway through a cluster even when that is all the
    // room there is. Ascending order, so the last entry at or before start is
    // the cluster holding it.
    S32 first = start;
    for (const auto& entry : cluster_advance)
    {
        if (entry.first > start)
            break;
        first = entry.first;
    }

    for (size_t k = cluster_advance.size(); k-- > 0; )
    {
        const S32 pos     = cluster_advance[k].first;
        const F32 advance = llmax(0.f, cluster_advance[k].second);
        // The last cluster uses its extent so the rightmost glyph stays fully
        // visible.
        const F32 width = (pos == start) ? llmax(last_cluster_extent, advance)
                                         : advance;
        if (scaled_max_pixels < (total_width + width))
            break;
        total_width += width;
        first = pos;
        if ((S32)(measure_end - (size_t)pos) >= max_bytes)
            break;
        if (!subpixel_pen)
            total_width = (F32)ll_round(total_width);
    }
    return first;
}

S32 LLFontGL::byteFromPixelOffset(std::string_view utf8text, S32 begin_offset, F32 target_x, F32 max_pixels, S32 max_bytes, bool round) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (utf8text.empty() || !utf8text[0] || max_bytes <= 0)
    {
        return 0;
    }

    F32 cur_x = 0;
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    target_x *= sScaleX;

    const S32 text_len = (S32)utf8text.length();
    begin_offset = llclamp(begin_offset, 0, text_len);

    // max_bytes is S32_MAX by default, so make sure we don't get overflow
    const S32 budget_end = llmin(text_len, begin_offset + llmin(S32_MAX - begin_offset, max_bytes));
    // Every character inside the budget is hit-tested, the last one included.
    // Running the walk out means the click was past all of them, and the answer
    // is then the budget's own end, so the caret lands after the final character
    // rather than on it. Stopping a character short would also shape a slice
    // narrower than the one render() draws, and measure a cluster it split.
    const S32 max_index = budget_end;

    F32 scaled_max_pixels = max_pixels * sScaleX;

    // Locate the tight slice we'll consider (bounded by max_bytes, the view's
    // own end, and the first embedded NUL) so we can shape the same window
    // render() does. Without this, per-character advances disagree with the
    // rendered composite glyph: clicks land in the middle of an emoji cluster
    // instead of on its edges.
    S32 slice_end = begin_offset;
    while (slice_end < max_index && utf8text[slice_end] != 0)
        ++slice_end;
    const S32 slice_len = slice_end - begin_offset;

    std::string_view slice = utf8text.substr((size_t)begin_offset, (size_t)slice_len);
    const ShapeLayout layout = build_shape_layout(mFontFreetype, slice);
    if (!layout.glyphs || layout.glyphs->empty())
    {
        // Nothing shaped: no face behind the font, or a run of nothing but
        // variation selectors it does not carry. Either way there is no glyph
        // to hit, so the answer is where the caller started.
        return 0;
    }
    const auto& run_glyphs = *layout.glyphs;

    // Per-glyph hit-test. Each glyph's `cluster` points back to a character
    // within the slice, so clicks land on those boundaries: for a ligature
    // (one glyph over several characters) and for a mark stack (several
    // glyphs on one cluster) this snaps to the nearest cluster edge rather
    // than letting the caret sit inside one.
    F32 run_x = 0.f;
    for (const ALShapedGlyph& sg : run_glyphs)
    {
        const F32 glyph_start = run_x;
        run_x += sg.x_advance;
        if (!subpixel_pen)
            run_x = (F32)ll_round(run_x);

        const S32 cluster_slice = (S32)sg.cluster;
        if (round)
        {
            if (target_x < glyph_start + sg.x_advance * 0.5f)
                return llmin(max_bytes, cluster_slice);
        }
        else if (target_x < glyph_start + sg.x_advance)
        {
            return llmin(max_bytes, cluster_slice);
        }

        if (scaled_max_pixels < run_x)
            return llmin(max_bytes, cluster_slice);
    }

    // Hit-test walk holds the layout's glyph pointers; early returns inside
    // the loop skip this check, which is fine — the assert is a tripwire
    // for shape-cache mutation mid-hold, not exhaustive coverage.
    llassert(layout.mutation_snapshot == ALFontShaping::cacheMutationCount());

    // Past every glyph, so the answer is the whole of what was considered.
    return llmin(max_bytes, slice_len);
}

const LLFontDescriptor& LLFontGL::getFontDesc() const
{
    return mFontDescriptor;
}

// static
void LLFontGL::initClass(F32 screen_dpi, F32 x_scale, F32 y_scale, const std::string& app_dir, const LLSD& font_overrides, bool create_gl_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    sVertDPI = (F32)llfloor(screen_dpi * y_scale);
    sHorizDPI = (F32)llfloor(screen_dpi * x_scale);
    sScaleX = x_scale;
    sScaleY = y_scale;
    sAppDir = app_dir;

    if (!sFontRegistry)
    {
        sFontRegistry = new LLFontRegistry(create_gl_textures);
        sFontRegistry->parseFontInfo("fonts.xml", font_overrides);
    }
    else
    {
        // Decide between the heavy full-reload (re-parse fonts.xml +
        // pointer-stable swap freetypes + re-rasterize ASCII) and the
        // light DPI-only reload (resetSelf each freetype). UI scale
        // changes go through the same initClass path as setting changes
        // and live-file changes, so the fast path is gated on:
        //   - fonts.xml content unchanged since the last parse, AND
        //   - the LLSD overrides byte-equal to the last applied snapshot.
        const bool xml_dirty = sFontsXmlDirty;
        sFontsXmlDirty = false;
        const bool overrides_changed = !sFontRegistry->overridesEqual(font_overrides);

        if (xml_dirty || overrides_changed)
        {
            // Tear down GL state up front so the destroyGL → my fix's
            // resetBitmapCache path clears stale glyph entries on every
            // face in mFaceCache before reload's new-freetype cycle
            // resolves them back via getOrCreateFace. Skipping this
            // would leave stale entries on faces whose key is unchanged
            // (override that doesn't move the file) and the next render
            // would draw blocks.
            destroyAllGL();
            if (sFontRegistry->reload(font_overrides))
            {
                // reload() has returned, so its pinned_old_fallbacks
                // local has destructed and dropped the last refs to
                // any orphan fallback freetypes. Trim now while their
                // faces sit at refcount 1 in mFaceCache.
                if (gFontManagerp)
                    gFontManagerp->collectGarbage();

                // Bump unconditionally on success so every metric-
                // sensitive widget re-flows on its next draw — even
                // no-op overrides (selecting the same family twice)
                // cycle through here, and skipping the bump would
                // leave widgets stuck on a stale layout if e.g. the
                // user picked a different family then reverted.
                ++sResolutionGeneration;
            }
        }
        else
        {
            // Fast path: fonts.xml + overrides identical, only DPI
            // changed (or nothing changed at all). Skip the parseFontInfo
            // + createFont + generateASCIIglyphs cycle — the heaviest
            // part of a font reload. Each freetype's mFace is re-resolved
            // via getOrCreateFace at the new DPI; cache miss creates a
            // fresh face wrapper, cache hit (DPI rounded to same ints)
            // reuses an existing wrapper. ASCII glyphs lazily re-rasterize
            // on first render through the new face.
            sFontRegistry->reloadForDpiChange();
            if (gFontManagerp)
                gFontManagerp->collectGarbage();
            ++sResolutionGeneration;
        }
    }

    LLFontGL::loadDefaultFonts();
}

// static
void LLFontGL::initClass(F32 screen_dpi, F32 x_scale, F32 y_scale,
                         const std::string& app_dir,
                         const std::string& fonts_xml_path,
                         const LLSD& font_overrides,
                         bool create_gl_textures)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    sVertDPI = (F32)llfloor(screen_dpi * y_scale);
    sHorizDPI = (F32)llfloor(screen_dpi * x_scale);
    sScaleX = x_scale;
    sScaleY = y_scale;
    sAppDir = app_dir;

    // Single-shot: bail if the registry was already created via either
    // overload. Reload-with-explicit-path isn't supported — the test
    // caller initializes once.
    if (sFontRegistry)
    {
        LL_WARNS() << "LLFontGL::initClass(explicit path) called with an existing registry; ignoring" << LL_ENDL;
        return;
    }

    sFontRegistry = new LLFontRegistry(create_gl_textures);
    sFontRegistry->parseFontInfoFromFile(fonts_xml_path, font_overrides);

    LLFontGL::loadDefaultFonts();
}

void LLFontGL::dumpTextures()
{
    if (mFontFreetype.notNull())
    {
        mFontFreetype->dumpFontBitmaps();
    }
}

// static
void LLFontGL::dumpFonts()
{
    sFontRegistry->dump();
}

// static
void LLFontGL::dumpFontTextures()
{
    sFontRegistry->dumpTextures();
}

// Force standard fonts to get generated up front.
// This is primarily for error detection purposes.
// Don't do this during initClass because it can be slow and we want to get
// the viewer window on screen first. JC
// static
bool LLFontGL::loadDefaultFonts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    bool succ = true;
    succ &= (NULL != getFontSansSerifSmall());
    succ &= (NULL != getFontSansSerif());
    succ &= (NULL != getFontSansSerifBig());
    succ &= (NULL != getFontSansSerifHuge());
    succ &= (NULL != getFontSansSerifBold());
    succ &= (NULL != getFontMonospace());
    return succ;
}

void LLFontGL::loadCommonFonts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    getFont(LLFontDescriptor("SansSerif", "Small", BOLD));
    getFont(LLFontDescriptor("SansSerif", "Large", BOLD));
    getFont(LLFontDescriptor("SansSerif", "Huge", BOLD));
    getFont(LLFontDescriptor("Monospace", "Medium", 0));
}

// static
void LLFontGL::destroyDefaultFonts()
{
    // Remove the actual fonts.
    delete sFontRegistry;
    sFontRegistry = NULL;
}

namespace
{
    // Set by setting listener (settings_setup_listeners in llviewercontrol.cpp);
    // drained by LLAppViewer::idle. Defers the actual reload off the
    // setValue() callstack so we never tear down an LLFontFreetype during
    // glyph rasterization or text render.
    bool s_pending_font_reload = false;
}

// static
void LLFontGL::schedulePendingReload()
{
    s_pending_font_reload = true;
}

// static
bool LLFontGL::consumePendingReload()
{
    if (!s_pending_font_reload)
        return false;
    s_pending_font_reload = false;
    return true;
}

// static
void LLFontGL::markFontsXmlDirty()
{
    sFontsXmlDirty = true;
}

// static
void LLFontGL::sweepGlyphCaches()
{
    if (sFontRegistry)
        sFontRegistry->sweepGlyphCaches();
}

// static
std::vector<LLFontRegistry::FamilyInfo> LLFontGL::getAvailableFamilies(
    LLFontRegistry::FamilyFilter filter)
{
    if (!sFontRegistry)
        return {};
    return sFontRegistry->getAvailableFamilies(filter);
}

//static
void LLFontGL::destroyAllGL()
{
    if (sFontRegistry)
    {
        sFontRegistry->destroyGL();
    }
}

// static
U8 LLFontGL::getStyleFromString(const std::string &style)
{
    // Tokenize on '|' so substrings inside other tokens ("FAUX-BOLD",
    // "NOTBOLD") don't accidentally set the bit. Empty input → NORMAL.
    S32 ret = 0;
    boost::char_separator<char> sep("|");
    boost::tokenizer<boost::char_separator<char>> tokens(style, sep);
    for (const auto& token : tokens)
    {
        if (token == "BOLD")           ret |= BOLD;
        else if (token == "ITALIC")    ret |= ITALIC;
        else if (token == "UNDERLINE") ret |= UNDERLINE;
    }
    return (U8)ret;
}

// static
const std::string& LLFontGL::getStringFromStyle(U8 style)
{
    // Three flags, so eight spellings, all of them fixed. NORMAL leads every
    // one: getStyleFromString reads it as zero, and the legacy strings this
    // has to match were written that way.
    //
    // A style carrying bits beyond the three indexes to NORMAL, which is what
    // building the string a piece at a time did with them.
    static const std::string sStyleStrings[8] =
    {
        "NORMAL",
        "NORMAL|BOLD",
        "NORMAL|ITALIC",
        "NORMAL|BOLD|ITALIC",
        "NORMAL|UNDERLINE",
        "NORMAL|BOLD|UNDERLINE",
        "NORMAL|ITALIC|UNDERLINE",
        "NORMAL|BOLD|ITALIC|UNDERLINE"
    };

    return sStyleStrings[style & (BOLD | ITALIC | UNDERLINE)];
}

// static
std::string LLFontGL::nameFromFont(const LLFontGL* fontp)
{
    return fontp->mFontDescriptor.getName();
}


// static
std::string LLFontGL::sizeFromFont(const LLFontGL* fontp)
{
    return fontp->mFontDescriptor.getSize();
}

// static
LLFontGL::HAlign LLFontGL::hAlignFromName(const std::string& name)
{
    LLFontGL::HAlign gl_hfont_align = LLFontGL::LEFT;
    if (name == "left")
    {
        gl_hfont_align = LLFontGL::LEFT;
    }
    else if (name == "right")
    {
        gl_hfont_align = LLFontGL::RIGHT;
    }
    else if (name == "center")
    {
        gl_hfont_align = LLFontGL::HCENTER;
    }
    //else leave left
    return gl_hfont_align;
}

//static
LLFontGL* LLFontGL::getFontEmojiSmall()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerifEmoji", "Small", 0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontEmojiMedium()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerifEmoji", "Medium", 0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontEmojiLarge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerifEmoji", "Large", 0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontEmojiHuge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerifEmoji", "Huge", 0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontMonospace()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Monospace","Monospace",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmall()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmallBold()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",BOLD));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmallItalic()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",ITALIC));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerif()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",0));
    return fontp;
}

// static
LLFontGL* LLFontGL::getFontSansSerifMedium()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif", "Medium", 0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifBig()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Large",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifHuge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Huge",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifBold()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Medium",BOLD));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFont(const LLFontDescriptor& desc)
{
    return sFontRegistry->getFont(desc);
}

//static
LLFontGL* LLFontGL::getFontByName(const std::string& name)
{
    // check for most common fonts first
    if (name == "SANSSERIF")
    {
        return getFontSansSerif();
    }
    else if (name == "SANSSERIF_SMALL")
    {
        return getFontSansSerifSmall();
    }
    else if (name == "SANSSERIF_BIG")
    {
        return getFontSansSerifBig();
    }
    else if (name == "SMALL" || name == "OCRA")
    {
        // *BUG: Should this be "MONOSPACE"?  Do we use "OCRA" anymore?
        // Does "SMALL" mean "SERIF"?
        return getFontMonospace();
    }
    else
    {
        return NULL;
    }
}

//static
LLFontGL* LLFontGL::getFontDefault()
{
    return getFontSansSerif(); // Fallback to sans serif as default font
}


// static
std::string LLFontGL::getFontPathSystem()
{
#if LL_DARWIN
    // HACK for macOS
    return "/System/Library/Fonts/";

#elif LL_WINDOWS
    static std::string system_font_path;
    if (!system_font_path.empty())
    {
        return system_font_path;
    }

    wchar_t *pwstr = NULL;
    HRESULT okay = SHGetKnownFolderPath(FOLDERID_Fonts, 0, NULL, &pwstr);
    if (SUCCEEDED(okay) && pwstr)
    {
        system_font_path = ll_convert_wide_to_string(pwstr) + gDirUtilp->getDirDelimiter();
        // SHGetKnownFolderPath() contract requires us to free pwstr
        CoTaskMemFree(pwstr);
        LL_INFOS() << "from SHGetKnownFolderPath(): " << system_font_path << LL_ENDL;
        return system_font_path;
    }
#endif

    LL_WARNS() << "Could not determine system fonts path" << LL_ENDL;
    return {};
}


// static
std::string LLFontGL::getFontPathLocal()
{
    std::string local_path;

    // Backup files if we can't load from system fonts directory.
    // We could store this in an end-user writable directory to allow
    // end users to switch fonts.
    if (LLFontGL::sAppDir.length())
    {
        // use specified application dir to look for fonts
        local_path = LLFontGL::sAppDir + "/fonts/";
    }
    else
    {
        // assume working directory is executable directory
        local_path = "./fonts/";
    }
    return local_path;
}

void LLFontGL::renderTriangle(LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, F32 slant_amt) const
{
    // slant_amt shifts the bottom edge horizontally relative to the top
    // (synthetic italic shear). Bottom-shifts-left + top-stays is the same
    // as top-shifts-right relative to baseline. Caller passes negative
    // slant_amt for italic, 0 for upright. The shear was lost in the
    // QUADS→TRIANGLES conversion (commit d9da5bbb33) and the parameter sat
    // dead until now; restoring it makes ITALIC render correctly when
    // style_to_add still has the bit (i.e. the underlying FT face wasn't
    // loaded with FT_STYLE_FLAG_ITALIC and no italic variant is wired up).
    S32 index = 0;

    vertex_out[index].set(screen_rect.mRight, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft + slant_amt, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mBottom);
    colors_out[index] = color;
    index++;


    vertex_out[index].set(screen_rect.mRight, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft + slant_amt, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mBottom);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mRight + slant_amt, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mBottom);
    colors_out[index] = color;
}

void LLFontGL::drawGlyphShadow(S32& glyph_count, LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& shadow_color, ShadowType shadow, F32 slant_offset) const
{
    if (shadow == NO_SHADOW)
    {
        return;
    }

    if (sEnableShaderShadow)
    {
        // Shader-based shadow: emit a single dilated quad. uiF.glsl with
        // textShadowMode > 0 takes 2 (DROP) or 5 (SOFT) atlas taps and accumulates
        // alpha as max() to reproduce the multi-quad coverage profile. The
        // dilated screen rect grows by 2px on each side to host the ±2px tap
        // pattern; the dilated UV rect grows by 2 atlas texels (callers fill
        // the slot's surrounding 2px atlas border with zeros via the atlas
        // padding bump in llfontbitmapcache, so out-of-glyph samples
        // contribute zero alpha).
        LLRectf dilated_screen(screen_rect.mLeft - 2.f, screen_rect.mTop + 2.f,
                               screen_rect.mRight + 2.f, screen_rect.mBottom - 2.f);
        // Atlas texel size mirrors the (mTop - mBottom) / height ratio used
        // elsewhere; rather than thread inv_width/inv_height into this leaf
        // function, derive the per-axis texel size from the rect itself.
        const F32 uv_per_px_x = (uv_rect.mRight - uv_rect.mLeft) / (screen_rect.mRight - screen_rect.mLeft);
        const F32 uv_per_px_y = (uv_rect.mTop - uv_rect.mBottom) / (screen_rect.mTop - screen_rect.mBottom);
        LLRectf dilated_uv(uv_rect.mLeft - 2.f * uv_per_px_x,
                           uv_rect.mTop + 2.f * uv_per_px_y,
                           uv_rect.mRight + 2.f * uv_per_px_x,
                           uv_rect.mBottom - 2.f * uv_per_px_y);
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], dilated_screen, dilated_uv, shadow_color, slant_offset);
        glyph_count++;
        return;
    }

    if (shadow == DROP_SHADOW_SOFT)
    {
        for (S32 pass = 0; pass < 5; pass++)
        {
            LLRectf screen_rect_offset = screen_rect;

            switch(pass)
            {
            case 0:
                screen_rect_offset.translate(-1.f, -1.f);
                break;
            case 1:
                screen_rect_offset.translate(1.f, -1.f);
                break;
            case 2:
                screen_rect_offset.translate(1.f, 1.f);
                break;
            case 3:
                screen_rect_offset.translate(-1.f, 1.f);
                break;
            case 4:
                screen_rect_offset.translate(0, -2.f);
                break;
            }

            renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_offset, uv_rect, shadow_color, slant_offset);
            glyph_count++;
        }
    }
    else // shadow == DROP_SHADOW
    {
        LLRectf screen_rect_shadow = screen_rect;
        screen_rect_shadow.translate(1.f, -1.f);
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_shadow, uv_rect, shadow_color, slant_offset);
        glyph_count++;
    }
}

void LLFontGL::drawGlyphForeground(S32& glyph_count, LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, U8 style, F32 slant_offset) const
{
    //FIXME: bold and drop shadow are mutually exclusive only for convenience
    //Allow both when we need them.
    if (style & BOLD)
    {
        for (S32 pass = 0; pass < 2; pass++)
        {
            LLRectf screen_rect_offset = screen_rect;

            screen_rect_offset.translate((F32)(pass * BOLD_OFFSET), 0.f);
            renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_offset, uv_rect, color, slant_offset);
            glyph_count++;
        }
    }
    else
    {
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
}

