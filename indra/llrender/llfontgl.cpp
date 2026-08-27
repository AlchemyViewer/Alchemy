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
    // Slice-local result of itemizing + shaping a measurement window. Each
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
        std::vector<std::pair<size_t, size_t>>           ranges;
        std::vector<const std::vector<ALShapedGlyph>*>   glyphs;
        // Shape-cache mutation count at build time. The glyph pointers are
        // valid only while this matches ALFontShaping::cacheMutationCount();
        // holders llassert equality after their last dereference so a
        // use-after-invalidation trips a debug assert instead of reading
        // freed glyph runs.
        size_t mutation_snapshot = 0;
    };

    // Build the shape layout for `slice` against `root_face`. One
    // all-encompassing range, shaped end-to-end through HarfBuzz. The
    // monospace feature plan in shape_sub_run (kern + ligatures off for
    // strict-mono, kern off only for ligatures-opt-in) preserves column
    // alignment without a separate codepoint partition. Empty slice or
    // null face: empty layout, no allocations.
    ShapeLayout build_shape_layout(const LLFontFreetype* root_face,
                                   LLWStringView         slice)
    {
        ShapeLayout out;
        // Snapshot up front so the empty-layout early return below carries
        // the live mutation count too — a default 0 would trip the holders'
        // validity assert on every empty-label measurement (LLButton::resize
        // with "" at startup) once anything had ever shaped.
        out.mutation_snapshot = ALFontShaping::cacheMutationCount();
        if (!root_face || slice.empty())
            return out;

        out.ranges.emplace_back(size_t(0), slice.size());

        out.glyphs.resize(out.ranges.size(), nullptr);
        for (size_t s = 0; s < out.ranges.size(); ++s)
        {
            out.glyphs[s] = &ALFontShaping::shapeLine(root_face, slice,
                                                     out.ranges[s].first,
                                                     out.ranges[s].second);
        }
        // Re-snapshot AFTER all shapeLine calls — each call may itself
        // mutate (miss-insert), which is fine: only mutations after this
        // point invalidate the pointers collected above. (With a single
        // range there's nothing to invalidate mid-build; with several,
        // entries just shaped sit at the LRU front, out of eviction's
        // reach.)
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
    return gen;
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, const LLRect& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
    ShadowType shadow, S32 max_chars, F32* right_x, bool use_ellipses, bool use_color) const
{
    LLRectf rect_float((F32)rect.mLeft, (F32)rect.mTop, (F32)rect.mRight, (F32)rect.mBottom);
    return render(wstr, begin_offset, rect_float, color, halign, valign, style, shadow, max_chars, right_x, use_ellipses, use_color);
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, const LLRectf& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                     ShadowType shadow, S32 max_chars, F32* right_x, bool use_ellipses, bool use_color) const
{
    F32 x = rect.mLeft;
    F32 y = 0.f;

    switch(valign)
    {
    case TOP:
        y = rect.mTop;
        break;
    case VCENTER:
        y = rect.getCenterY();
        break;
    case BASELINE:
    case BOTTOM:
        y = rect.mBottom;
        break;
    default:
        y = rect.mBottom;
        break;
    }
    return render(wstr, begin_offset, x, y, color, halign, valign, style, shadow, max_chars, (S32)rect.getWidth(), right_x, use_ellipses, use_color);
}


S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                     ShadowType shadow, S32 max_chars, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color, pass_boundary_cb_t on_pass_boundary) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if(!sDisplayFont) //do not display texts
    {
        return static_cast<S32>(wstr.length());
    }

    if (wstr.empty() || begin_offset < 0 || begin_offset >= (S32)wstr.length())
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

    if (-1 == max_chars)
    {
        max_chars = length = (S32)wstr.length() - begin_offset;
    }
    else
    {
        length = llmin((S32)wstr.length() - begin_offset, max_chars );
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
    const F32 string_width_unscaled = needs_string_width
        ? getWidthF32(wstr, begin_offset, length)
        : 0.f;
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
            static const LLWString dots(U"....");
            scaled_max_pixels = llmax(0, scaled_max_pixels - ll_round(getWidthF32(dots)));
            draw_ellipses = true;
        }
    }

    const LLFontGlyphInfo* next_glyph = NULL;

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
    std::vector<DeferredGlyph> deferred;
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

    // Itemize + shape the slice via the shared helper. Strict-monospace
    // gets emoji-cluster ranges so ASCII keeps the codepoint path's exact
    // metrics (visual parity with toggle-off) while embedded emoji
    // clusters still shape; the non-mono path gets a single range covering
    // the whole slice. Layout's ranges are slice-local; we compare against
    // `i - begin_offset` in the loop.
    LLWStringView slice(wstr.data() + begin_offset, (size_t)length);
    const ShapeLayout layout = build_shape_layout(mFontFreetype, slice);
    size_t next_shape_run = 0;

    for (i = begin_offset; i < begin_offset + length; i++)
    {
        const S32 i_slice = i - begin_offset;
        // Entered a shaped run? Emit its HarfBuzz-positioned glyphs in one
        // go and jump past the range. When shaping produced no glyphs (rare —
        // face/HB failure) we fall through to the codepoint path so the
        // range still draws something, even if ZWJ presentation is wrong.
        if (next_shape_run < layout.ranges.size()
            && (S32)layout.ranges[next_shape_run].first == i_slice)
        {
            const auto  run_range  = layout.ranges[next_shape_run];
            const auto& run_glyphs = *layout.glyphs[next_shape_run];
            ++next_shape_run;

            if (!run_glyphs.empty())
            {
                next_glyph = NULL;  // drop any kerning prefetch from before the run
                bool overflow = false;
                // Slice-local cp index of the glyph that overflowed. shapeLine
                // returns clusters relative to its begin parameter (here,
                // run_range.first), so this is in [0, run_len). Used so
                // chars_drawn reflects only cps whose cluster was reached
                // before clipping — counting the full run on overflow lies
                // to callers that drive word-wrap / chunked draw off the
                // return value.
                S32 overflow_cluster_local = 0;
                for (const ALShapedGlyph& sg : run_glyphs)
                {
                    // Cache lives on the root face and its bitmap atlas; the
                    // fallback face is only the *source* for the glyph. The
                    // codepoint path also routes through getGlyphInfoByIndex
                    // (after wch -> glyph_index resolution), so this lookup
                    // hits the same atlas slot regardless of which path
                    // produced the ALShapedGlyph.
                    const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                        sg.face, sg.glyph_id,
                        (!use_color || LLFontGL::sForceMonochromeEmoji)
                            ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
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
                    if (sfgi->mPhaseCount > 1)
                    {
                        const F32 frac_x = pen_x - floorf(pen_x);
                        const U32 raw =
                            (U32)floorf(frac_x * (F32)LLFontGlyphInfo::kNumPhases + 0.5f);
                        if (raw >= LLFontGlyphInfo::kNumPhases)
                        {
                            phase = 0;
                            dest_int_x = (S32)floorf(pen_x) + 1;
                        }
                        else
                        {
                            phase = (U8)raw;
                            dest_int_x = (S32)floorf(pen_x);
                        }
                    }
                    else
                    {
                        phase = 0;
                        dest_int_x = ll_round(pen_x);
                    }
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

                    if ((start_x + scaled_max_pixels) < (glyph_x + (F32)slot.mWidth))
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
                    // Count cps strictly before the overflowing cluster.
                    // overflow_cluster_local is slice-local (0-based within
                    // the run), so adding it directly gives the cp count.
                    chars_drawn += llmax(0, overflow_cluster_local);
                    break;
                }
                chars_drawn += (S32)(run_range.second - run_range.first);

                i = begin_offset + (S32)run_range.second - 1;  // loop's ++ lands past the run
                continue;
            }
            // Empty run_glyphs — shaping failed. Fall through to the
            // codepoint path for this iteration.
        }

        llwchar wch = wstr[i];

        const LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch,
                (!use_color || LLFontGL::sForceMonochromeEmoji)
                    ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
        }
        if (!fgi)
        {
            LL_ERRS() << "Missing Glyph Info" << LL_ENDL;
            break;
        }
        // Quantize pen x and split into integer dest + subpixel phase. See the
        // matching block in the shaped path above for the wrap-around rationale.
        U8 cp_phase;
        S32 cp_dest_int_x;
        if (fgi->mPhaseCount > 1)
        {
            const F32 frac_x = cur_render_x - floorf(cur_render_x);
            const U32 raw =
                (U32)floorf(frac_x * (F32)LLFontGlyphInfo::kNumPhases + 0.5f);
            if (raw >= LLFontGlyphInfo::kNumPhases)
            {
                cp_phase = 0;
                cp_dest_int_x = (S32)floorf(cur_render_x) + 1;
            }
            else
            {
                cp_phase = (U8)raw;
                cp_dest_int_x = (S32)floorf(cur_render_x);
            }
        }
        else
        {
            cp_phase = 0;
            cp_dest_int_x = ll_round(cur_render_x);
        }
        const auto& cp_slot = fgi->mPhaseSlots[cp_phase];

        // Per-glyph bitmap texture. Flush + rebind only when the atlas
        // slot actually changes; flush_batch re-asserts the batch's own
        // texture, so glyph rasterization between flushes (which leaves the
        // upload target bound) can't misdirect the queued quads. The legacy
        // `last_char != wch` clause forced a per-codepoint flush as a
        // band-aid over exactly that misdirection; Latin text now batches
        // up to GLYPH_BATCH_SIZE glyphs per draw the way it should.
        const ALFontFace* cp_glyph_face = fgi->mSourceFace;
        std::pair<EFontGlyphType, S32> next_bitmap_entry = cp_slot.mBitmapEntry;
        if (cp_glyph_face != current_face || next_bitmap_entry != bitmap_entry)
        {
            // Actually draw the queued glyphs before switching their texture;
            // otherwise the queued glyphs will be taken from wrong textures.
            flush_batch();

            bitmap_entry = next_bitmap_entry;
            if (cp_glyph_face != current_face)
            {
                current_face = cp_glyph_face;
                font_bitmap_cache = current_face ? current_face->getBitmapCache() : nullptr;
                if (font_bitmap_cache)
                {
                    inv_width  = 1.f / font_bitmap_cache->getBitmapWidth();
                    inv_height = 1.f / font_bitmap_cache->getBitmapHeight();
                }
            }
            // Defensive: getImageGL returns null when a sheet has been
            // released (collectGarbage) or when the slot index is out
            // of range. The emission guard below skips the glyph in that
            // case — emitting quads without a known binding would sample
            // whatever texture is currently bound.
            batch_image = font_bitmap_cache
                ? font_bitmap_cache->getImageGL(bitmap_entry.first, bitmap_entry.second)
                : nullptr;
            if (batch_image)
            {
                gGL.getTextureSlot(0)->bindSampled(batch_image, ALSamplers::PointWrap);
            }
        }

        if ((start_x + scaled_max_pixels) < (cur_x + cp_slot.mXBearing + cp_slot.mWidth))
        {
            // Not enough room for this character.
            break;
        }

        if (batch_image)
        {
            // Draw the text at the appropriate location
            //Specify vertices and texture coordinates
            LLRectf uv_rect((cp_slot.mXBitmapOffset) * inv_width,
                    (cp_slot.mYBitmapOffset + cp_slot.mHeight + PAD_UVY) * inv_height,
                    (cp_slot.mXBitmapOffset + cp_slot.mWidth) * inv_width,
                    (cp_slot.mYBitmapOffset - PAD_UVY) * inv_height);
            // Integer dest derived from quantized pen + per-phase bearing.
            const F32 cp_glyph_x = (F32)(cp_dest_int_x + cp_slot.mXBearing);
            const F32 cp_glyph_y = (F32)ll_round(cur_render_y) + (F32)cp_slot.mYBearing;
            LLRectf screen_rect(cp_glyph_x,
                        cp_glyph_y,
                        cp_glyph_x + (F32)cp_slot.mWidth,
                        cp_glyph_y - (F32)cp_slot.mHeight);

            if (glyph_count >= GLYPH_BATCH_SIZE)
            {
                flush_batch();
            }

            // Grayscale tints with text_color; Color tints with emoji_color
            // (white, preserving CPAL palette colors baked into the bitmap).
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

        chars_drawn++;
        cur_x += fgi->mXAdvance;
        cur_y += fgi->mYAdvance;

        llwchar next_char = wstr[i+1];
        if (next_char)
        {
            // Kern this puppy. The old `next_char < LAST_CHAR_FULL`
            // gate was a vestigial ASCII-bucket-cache limit; today
            // getXKerning works for any pair.
            next_glyph = mFontFreetype->getGlyphInfo(next_char,
                (!use_color || LLFontGL::sForceMonochromeEmoji)
                    ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }

        // Round after kerning. With subpixel_pen on the cumulative advance
        // stays fractional through the loop and only the per-glyph draw rect
        // (above) snaps to integer pixels. Without subpixel_pen we keep the
        // legacy per-glyph round so native-hinted glyphs stay on the
        // integer grid the foundry designed them for.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);

        cur_render_x = cur_x;
        cur_render_y = cur_y;
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
        static const LLWString elipses_wstr(U"...");
        render(elipses_wstr,
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

S32 LLFontGL::render(const LLWString &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color) const
{
    return render(text, begin_offset, x, y, color, LEFT, BASELINE, NORMAL, NO_SHADOW);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow, S32 max_chars, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color) const
{
    return render(utf8str_to_wstring(text), begin_offset, x, y, color, halign, valign, style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
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

S32 LLFontGL::getWidth(const std::string& utf8text) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidth(wtext, 0, S32_MAX);
}

S32 LLFontGL::getWidth(LLWStringView wchars) const
{
    return getWidth(wchars, 0, S32_MAX);
}

S32 LLFontGL::getWidth(const std::string& utf8text, S32 begin_offset, S32 max_chars) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidth(wtext, begin_offset, max_chars);
}

S32 LLFontGL::getWidth(LLWStringView wchars, S32 begin_offset, S32 max_chars) const
{
    F32 width = getWidthF32(wchars, begin_offset, max_chars);
    // llceil, not ll_round: getWidth's contract is "minimum integer pixel
    // width that contains the rendered text". With subpixel pen position
    // (mUseSubpixelPen), getWidthF32 returns fractional widths; ll_round
    // would truncate fractions < 0.5 (e.g. 28.4 -> 28) and clip the last
    // glyph in callers that size layout rects to the returned width
    // (LLButton::resize, scroll list cells, tooltip backgrounds, etc.).
    return llceil(width);
}

F32 LLFontGL::getWidthF32(const std::string& utf8text) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidthF32(wtext, 0, S32_MAX);
}

F32 LLFontGL::getWidthF32(LLWStringView wchars) const
{
    return getWidthF32(wchars, 0, S32_MAX);
}

F32 LLFontGL::getWidthF32(const std::string& utf8text, S32 begin_offset, S32 max_chars) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidthF32(wtext, begin_offset, max_chars);
}

F32 LLFontGL::getWidthF32(LLWStringView wchars, S32 begin_offset, S32 max_chars, bool no_padding) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    F32 cur_x = 0;
    const S32 text_len = (S32)wchars.length();
    const S32 begin = llclamp(begin_offset, 0, text_len);
    // Clamp the upper bound the same way charFromPixelOffset does. The default
    // max_chars value is S32_MAX, so a non-zero begin_offset would otherwise
    // overflow the S32 sum (UB on signed overflow). Halign-right and underline
    // measurement paths in render() routinely call this with begin_offset > 0.
    const S32 max_index = llmin(text_len, begin + llmin(S32_MAX - begin, max_chars));

    // Mirror render()'s pen accumulation policy so width measurements agree
    // with what's drawn — see the comment at render()'s subpixel_pen.
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    // Determine the tight slice we'll actually measure (bounded by max_chars,
    // the view's own end, and the first embedded NUL) so shaping only runs over
    // real content.
    S32 measure_end = begin;
    while (measure_end < max_index && wchars[measure_end] != 0)
        ++measure_end;
    const S32 measure_len = measure_end - begin;

    // Same shape-first preprocessing as render(); kept consistent so caret
    // positions and ellipsis cutoffs agree with what's drawn. The layout's
    // ranges come back slice-local; we compare against `i - begin`
    // in the loop rather than mutating ranges.
    LLWStringView slice = wchars.substr((size_t)begin, (size_t)measure_len);
    const ShapeLayout layout = build_shape_layout(mFontFreetype, slice);
    size_t next_shape_run = 0;

    const LLFontGlyphInfo* next_glyph = NULL;

    F32 width_padding = 0.f;
    for (S32 i = begin; i < max_index && wchars[i] != 0; i++)
    {
        const S32 i_slice = i - begin;
        if (next_shape_run < layout.ranges.size()
            && (S32)layout.ranges[next_shape_run].first == i_slice)
        {
            const auto  run_range  = layout.ranges[next_shape_run];
            const auto& run_glyphs = *layout.glyphs[next_shape_run];
            ++next_shape_run;

            if (!run_glyphs.empty())
            {
                next_glyph = NULL;
                F32 run_padding = 0.f;
                for (const ALShapedGlyph& sg : run_glyphs)
                {
                    const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                        sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                    if (!sfgi)
                        continue;
                    if (!no_padding)
                    {
                        run_padding = llmax(0.f,
                            run_padding - sg.x_advance,
                            (F32)(sfgi->mWidth + sfgi->mXBearing) - sg.x_advance);
                    }
                    cur_x += sg.x_advance;
                    // Match render()'s pen motion so caret positions and
                    // ellipsis cutoffs agree with what's drawn.
                    if (!subpixel_pen)
                        cur_x = (F32)ll_round(cur_x);
                }
                width_padding = run_padding;
                i = begin + (S32)run_range.second - 1;
                continue;
            }
            // Fall through to codepoint path when shaping failed.
        }

        llwchar wch = wchars[i];

        const LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);
        }

        F32 advance = mFontFreetype->getXAdvance(fgi);

        if (!no_padding)
        {
            // for the last character we want to measure the greater of its width and xadvance values
            // so keep track of the difference between these values for the each character we measure
            // so we can fix things up at the end
            width_padding = llmax(0.f,                                          // always use positive padding amount
                width_padding - advance,                        // previous padding left over after advance of current character
                (F32)(fgi->mWidth + fgi->mXBearing) - advance); // difference between width of this character and advance to next character
        }

        cur_x += advance;

        // Read the kerning partner only once it is known to be in range. The
        // old form indexed [i+1] before testing it, which stayed inside the
        // buffer only because callers handed over a NUL-terminated pointer.
        if ((i + 1) < max_index)
        {
            const llwchar next_char = wchars[i + 1];
            if (next_char)
            {
                // Kern this puppy. LAST_CHARACTER (255) was a vestigial limit
                // from the era of ASCII-bucketed glyph caches; getXKerning
                // works for any pair today and the codepoint path is the
                // shape-failure / strict-mono fallback where this matters.
                next_glyph = mFontFreetype->getGlyphInfo(next_char, EFontGlyphType::Unspecified);
                cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
            }
        }
        // Round after kerning. With subpixel_pen on, accumulator stays
        // fractional so cumulative kerning precision survives.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);
    }

    // Layout pointers must have stayed valid for the whole measurement walk.
    llassert(layout.mutation_snapshot == ALFontShaping::cacheMutationCount());

    if (!no_padding)
    {
        // add in extra pixels for last character's width past its xadvance
        cur_x += width_padding;
    }

    return cur_x / sScaleX;
}

void LLFontGL::generateASCIIglyphs()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    for (U32 i = 32; (i < 127); i++)
    {
        mFontFreetype->getGlyphInfo(i, EFontGlyphType::Grayscale);
    }
}

// Returns the max number of complete characters from text (up to max_chars) that can be drawn in max_pixels
S32 LLFontGL::maxDrawableChars(LLWStringView wchars, F32 max_pixels, S32 max_chars, EWordWrapStyle end_on_word_boundary) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (wchars.empty() || !wchars[0] || max_chars <= 0)
    {
        return 0;
    }

    // Everything below walks by index up to max_chars; the view's own end is the
    // other bound. Folding them together here keeps the walk inside the buffer
    // even when a caller asks for more characters than it handed over.
    max_chars = llmin(max_chars, (S32)wchars.length());

    llassert(max_pixels >= 0.f);

    bool clip = false;
    F32 cur_x = 0;

    S32 start_of_last_word = 0;
    bool in_word = false;

    // avoid S32 overflow when max_pixels == S32_MAX by staying in floating point
    F32 scaled_max_pixels = max_pixels * sScaleX;
    F32 width_padding = 0.f;

    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    LLFontGlyphInfo* next_glyph = NULL;

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
    if (max_chars > 0 && wchars[0])
    {
        S32 measure_end = 0;
        while (measure_end < max_chars && wchars[measure_end] != 0)
            ++measure_end;
        if (measure_end > 0)
        {
            LLWStringView slice = wchars.substr(0, (size_t)measure_end);
            shape_glyphs = &ALFontShaping::shapeLine(mFontFreetype, slice, 0, (size_t)measure_end);
        }
    }
    const bool use_shaped = !shape_glyphs->empty();
    // shape_glyphs points into the shape LRU until the loop's last use.
    const size_t shape_gen = ALFontShaping::cacheMutationCount();
    (void)shape_gen;

    S32 i;
    for (i=0; (i < max_chars); i++)
    {
        llwchar wch = wchars[i];

        if(wch == 0)
        {
            // Null terminator.  We're done.
            break;
        }

        if (in_word)
        {
            if (LLStringOps::isSpace(wch))
            {
                if(wch !=(0x00A0))
                {
                    in_word = false;
                }
            }
            if (iswindividual(wch))
            {
                // Past the end reads as 0, which is what the NUL terminator used
                // to supply here — isPunct(0) is false either way.
                const llwchar peek = ((i + 1) < max_chars) ? wchars[i + 1] : 0;
                if (LLStringOps::isPunct(peek))
                {
                    in_word=true;
                }
                else
                {
                    in_word=false;
                    start_of_last_word = i;
                }
            }
        }
        else
        {
            start_of_last_word = i;
            if (!LLStringOps::isSpace(wch) || !iswindividual(wch))
            {
                in_word = true;
            }
        }

        if (use_shaped)
        {
            // Sum advances and the largest extent of any glyph whose cluster
            // lands on this codepoint. Trailing codepoints of a multi-cp
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
            continue;
        }

        LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);

            if (NULL == fgi)
            {
                return 0;
            }
        }

        // account for glyphs that run beyond the starting point for the next glyphs
        width_padding = llmax(  0.f,                                                    // always use positive padding amount
                                width_padding - fgi->mXAdvance,                         // previous padding left over after advance of current character
                                (F32)(fgi->mWidth + fgi->mXBearing) - fgi->mXAdvance);  // difference between width of this character and advance to next character

        cur_x += fgi->mXAdvance;

        // clip if current character runs past scaled_max_pixels (using width_padding)
        if (scaled_max_pixels < cur_x + width_padding)
        {
            clip = true;
            break;
        }

        if (((i+1) < max_chars) && wchars[i+1])
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(wchars[i+1], EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }

        // Round after kerning.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);
    }

    // No shape* call may fire while shape_glyphs is held (see the comment
    // at the shapeLine call above).
    llassert(shape_gen == ALFontShaping::cacheMutationCount());

    if( clip )
    {
        switch (end_on_word_boundary)
        {
        case ONLY_WORD_BOUNDARIES:
            i = start_of_last_word;
            break;
        case WORD_BOUNDARY_IF_POSSIBLE:
            if (start_of_last_word != 0)
            {
                i = start_of_last_word;
            }
            break;
        default:
        case ANYWHERE:
            // do nothing
            break;
        }
    }
    return i;
}

S32 LLFontGL::firstDrawableChar(LLWStringView wchars, F32 max_pixels, S32 start_pos, S32 max_chars) const
{
    if (wchars.empty() || !wchars[0] || max_chars <= 0)
    {
        return 0;
    }

    F32 total_width = 0.0;
    S32 drawable_chars = 0;

    F32 scaled_max_pixels = max_pixels * sScaleX;
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    S32 start = llmin(start_pos, (S32)wchars.length() - 1);

    // pre-shape [0, start+1) and project advances onto a
    // per-codepoint table. Walking backward through that table substitutes for
    // the legacy fgi->mXAdvance + getXKerning chain. Ligatures and ZWJ
    // clusters get their full advance attributed to the cluster's first cp;
    // trailing cps contribute zero, which is what we want for measurement.
    std::vector<F32> per_cp_advance;
    F32 per_cp_last_extent = 0.f;
    if (start >= 0 && wchars[0])
    {
        // shapeLine ref valid through the per_cp_advance fill below since
        // no other shape* calls run in between. Clusters are slice-local
        // (begin=0), which equals 0..start here.
        LLWStringView slice = wchars.substr(0, (size_t)(start + 1));
        const auto& shape_glyphs = ALFontShaping::shapeLine(mFontFreetype, slice, 0, (size_t)(start + 1));
        const size_t shape_gen = ALFontShaping::cacheMutationCount();
        (void)shape_gen;
        if (!shape_glyphs.empty())
        {
            per_cp_advance.assign(start + 1, 0.f);
            for (const auto& sg : shape_glyphs)
            {
                if (sg.cluster >= 0 && sg.cluster <= start)
                {
                    per_cp_advance[sg.cluster] += sg.x_advance;
                    if (sg.cluster == start)
                    {
                        const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                            sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                        if (sfgi)
                            per_cp_last_extent = llmax(per_cp_last_extent,
                                                       (F32)(sfgi->mWidth + sfgi->mXBearing));
                    }
                }
            }
        }
        // The fill above is shape_glyphs' last dereference — it must not
        // have been invalidated by a shape-cache mutation mid-hold.
        llassert(shape_gen == ALFontShaping::cacheMutationCount());
    }
    const bool use_shaped = !per_cp_advance.empty();

    for (S32 i = start; i >= 0; i--)
    {
        if (use_shaped)
        {
            // Last cp uses extent so the rightmost glyph stays fully visible.
            F32 width = (i == start)
                        ? llmax(per_cp_last_extent, per_cp_advance[i])
                        : per_cp_advance[i];
            if (scaled_max_pixels < (total_width + width))
                break;
            total_width += width;
            drawable_chars++;
            if (max_chars >= 0 && drawable_chars >= max_chars)
                break;
            if (!subpixel_pen)
                total_width = (F32)ll_round(total_width);
            continue;
        }

        llwchar wch = wchars[i];

        const LLFontGlyphInfo* fgi= mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);

        // last character uses character width, since the whole character needs to be visible
        // other characters just use advance
        F32 width = (i == start)
            ? (F32)(fgi->mWidth + fgi->mXBearing)   // use actual width for last character
            : fgi->mXAdvance;                       // use advance for all other characters

        if( scaled_max_pixels < (total_width + width) )
        {
            break;
        }

        total_width += width;
        drawable_chars++;

        if( max_chars >= 0 && drawable_chars >= max_chars )
        {
            break;
        }

        if ( i > 0 )
        {
            // kerning
            total_width += mFontFreetype->getXKerning(wchars[i-1], wch);
        }

        // Round after kerning.
        if (!subpixel_pen)
            total_width = (F32)ll_round(total_width);
    }

    if (drawable_chars == 0)
    {
        return start_pos; // just draw last character
    }
    else
    {
        // if only 1 character is drawable, we want to return start_pos as the first character to draw
        // if 2 are drawable, return start_pos and character before start_pos, etc.
        return start_pos + 1 - drawable_chars;
    }

}

S32 LLFontGL::charFromPixelOffset(LLWStringView wchars, S32 begin_offset, F32 target_x, F32 max_pixels, S32 max_chars, bool round) const
{
    if (wchars.empty() || !wchars[0] || max_chars <= 0)
    {
        return 0;
    }

    F32 cur_x = 0;
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    target_x *= sScaleX;

    const S32 text_len = (S32)wchars.length();
    begin_offset = llclamp(begin_offset, 0, text_len);

    // max_chars is S32_MAX by default, so make sure we don't get overflow
    const S32 max_index = llmin(text_len, begin_offset + llmin(S32_MAX - begin_offset, max_chars - 1));

    F32 scaled_max_pixels = max_pixels * sScaleX;

    // Locate the tight slice we'll consider (bounded by max_chars, the view's
    // own end, and the first embedded NUL) so we can shape the same window
    // render() does. Without this, per-codepoint advances disagree with the
    // rendered composite glyph: clicks land in the middle of an emoji cluster
    // instead of on its edges.
    S32 slice_end = begin_offset;
    while (slice_end < max_index && wchars[slice_end] != 0)
        ++slice_end;
    const S32 slice_len = slice_end - begin_offset;

    LLWStringView slice = wchars.substr((size_t)begin_offset, (size_t)slice_len);
    const ShapeLayout layout = build_shape_layout(mFontFreetype, slice);
    size_t next_shape_run = 0;

    const LLFontGlyphInfo* next_glyph = NULL;

    S32 pos;
    for (pos = begin_offset; pos < max_index; pos++)
    {
        llwchar wch = wchars[pos];
        if (!wch)
        {
            break; // done
        }

        const S32 pos_slice = pos - begin_offset;
        // Per-glyph hit-test inside a shape range. Each glyph's `cluster`
        // points back to a codepoint within the slice; clicks land on
        // that boundary. For ligatures (one glyph spans multiple cps) and
        // mark stacks (multiple glyphs share one cluster), this correctly
        // snaps to the nearest cluster boundary — you can't put the cursor
        // mid-ligature. Mid-test matches the legacy codepoint path's
        // unrounded `cur_x + W*0.5f` so behavior agrees character-for-
        // character on Latin text.
        if (next_shape_run < layout.ranges.size()
            && (S32)layout.ranges[next_shape_run].first == pos_slice)
        {
            const auto  run_range  = layout.ranges[next_shape_run];
            const auto& run_glyphs = *layout.glyphs[next_shape_run];
            ++next_shape_run;

            if (!run_glyphs.empty())
            {
                // shapeLine returns cluster values relative to its `begin`
                // arg — i.e. relative to run_range.first within the slice.
                // The function returns slice-local positions, so add
                // run_range.first to get back into slice coords.
                const S32 cluster_base = (S32)run_range.first;
                F32 run_x = cur_x;
                for (const ALShapedGlyph& sg : run_glyphs)
                {
                    const F32 glyph_start = run_x;
                    run_x += sg.x_advance;
                    if (!subpixel_pen)
                        run_x = (F32)ll_round(run_x);

                    const S32 cluster_slice = cluster_base + (S32)sg.cluster;
                    if (round)
                    {
                        if (target_x < glyph_start + sg.x_advance * 0.5f)
                            return llmin(max_chars, cluster_slice);
                    }
                    else if (target_x < glyph_start + sg.x_advance)
                    {
                        return llmin(max_chars, cluster_slice);
                    }

                    if (scaled_max_pixels < run_x)
                        return llmin(max_chars, cluster_slice);
                }

                // Click is past the entire shaped run; advance and continue.
                cur_x = run_x;
                pos = begin_offset + (S32)run_range.second - 1;  // loop's ++ lands past the run
                next_glyph = NULL;
                continue;
            }
        }

        const LLFontGlyphInfo* glyph = next_glyph;
        next_glyph = NULL;
        if(!glyph)
        {
            glyph = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);
        }

        F32 char_width = mFontFreetype->getXAdvance(glyph);

        if (round)
        {
            // Note: if the mouse is on the left half of the character, the pick is to the character's left
            // If it's on the right half, the pick is to the right.
            if (target_x  < cur_x + char_width*0.5f)
            {
                break;
            }
        }
        else if (target_x  < cur_x + char_width)
        {
            break;
        }

        if (scaled_max_pixels < cur_x + char_width)
        {
            break;
        }

        cur_x += char_width;

        if (((pos + 1) < max_index)
            && (wchars[(pos + 1)]))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(wchars[pos + 1], EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(glyph, next_glyph);
        }


        // Round after kerning.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);
    }

    // Hit-test walk holds the layout's glyph pointers; early returns inside
    // the loop skip this check, which is fine — the assert is a tripwire
    // for shape-cache mutation mid-hold, not exhaustive coverage.
    llassert(layout.mutation_snapshot == ALFontShaping::cacheMutationCount());

    return llmin(max_chars, pos - begin_offset);
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
std::string LLFontGL::nameFromHAlign(LLFontGL::HAlign align)
{
    if (align == LEFT)          return std::string("left");
    else if (align == RIGHT)    return std::string("right");
    else if (align == HCENTER)  return std::string("center");
    else return std::string();
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

// static
std::string LLFontGL::nameFromVAlign(LLFontGL::VAlign align)
{
    if (align == TOP)           return std::string("top");
    else if (align == VCENTER)  return std::string("center");
    else if (align == BASELINE) return std::string("baseline");
    else if (align == BOTTOM)   return std::string("bottom");
    else return std::string();
}

// static
LLFontGL::VAlign LLFontGL::vAlignFromName(const std::string& name)
{
    LLFontGL::VAlign gl_vfont_align = LLFontGL::BASELINE;
    if (name == "top")
    {
        gl_vfont_align = LLFontGL::TOP;
    }
    else if (name == "center")
    {
        gl_vfont_align = LLFontGL::VCENTER;
    }
    else if (name == "baseline")
    {
        gl_vfont_align = LLFontGL::BASELINE;
    }
    else if (name == "bottom")
    {
        gl_vfont_align = LLFontGL::BOTTOM;
    }
    //else leave baseline
    return gl_vfont_align;
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

