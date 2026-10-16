/**
 * @file alfontcolrv1.cpp
 * @brief COLRv1 paint-tree rasterizer using HarfBuzz's hb-raster backend.
 *        See LLFontFreetype::renderColrV1Glyph for the call site.
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

#include "linden_common.h"

#include "alfontcolrv1.h"

#include "llerror.h"

#include <hb.h>
#include <hb-ft.h>
#include <hb-ot.h>
#include <hb-raster.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H

#include <cstring>
#include <vector>

namespace
{
    // Floor (×256, fixed-point) applied to the alpha channel when folding
    // BGRA premul → Gray. Keeps fully-saturated palette black visible as a
    // ~15% silhouette rather than collapsing to zero.
    constexpr U32 GRAY_ALPHA_FLOOR_256 = 38;  // ≈ 0.15 * 256

    // mStaging high-water cap: shrink the per-thread staging buffer when
    // the current glyph fits comfortably under this threshold AND the
    // buffer's capacity is more than 4× what the current glyph needs, so
    // one huge transient glyph doesn't strand multiple MB of per-thread
    // memory after returning to a stream of small glyphs. Above the
    // threshold we leave the allocation alone (no point releasing memory
    // we'll likely need on the next call).
    constexpr size_t STAGING_HIGH_WATER_BYTES = 256 * 1024;

    // Per-thread hb_raster_paint_t. Created on first use, destroyed on
    // thread exit. Reusing the paint context across glyphs is the
    // documented hb-raster contract — recycle images via
    // hb_raster_paint_recycle_image; the paint itself owns the scratch
    // surfaces and recycles them between glyphs.
    struct PaintHolder
    {
        hb_raster_paint_t* p = nullptr;
        ~PaintHolder()
        {
            if (p)
                hb_raster_paint_destroy(p);
        }
    };

    thread_local PaintHolder s_paint;

    hb_raster_paint_t* get_paint()
    {
        if (!s_paint.p)
            s_paint.p = hb_raster_paint_create_or_fail();
        return s_paint.p;
    }

    // Scope guard: temporarily override the hb-ft font's FT load flags for
    // the duration of a COLRv1 rasterization call, then restore. Color emoji
    // outlines must be unhinted — autohinted/grid-fitted curves visibly
    // distort small-ppem bezier shapes — but the hb_font is shared with text
    // shaping, which legitimately wants the user's chosen hinting. Localize
    // the override so shaping is unaffected once paintGlyph returns.
    struct LoadFlagsGuard
    {
        hb_font_t* font;
        int        prev;
        LoadFlagsGuard(hb_font_t* f, int new_flags)
            : font(f), prev(hb_ft_font_get_load_flags(f))
        {
            hb_ft_font_set_load_flags(font, new_flags);
        }
        ~LoadFlagsGuard()
        {
            hb_ft_font_set_load_flags(font, prev);
        }
        LoadFlagsGuard(const LoadFlagsGuard&)            = delete;
        LoadFlagsGuard& operator=(const LoadFlagsGuard&) = delete;
    };

    // Per-source-pixel hybrid fold. max(Rec.709 luminance, alpha * 0.15) so
    // the Grayscale-atlas invariant Y == alpha holds for foreground-only
    // pixels (white premul: r=g=b=a) — the rendered BGRA collapses cleanly
    // to a luminance mask the renderer can tint with text_color.
    inline U8 fold_gray(U8 b, U8 g, U8 r, U8 a)
    {
        const U32 y_256     = (U32)r * 54u + (U32)g * 183u + (U32)b * 18u;
        const U32 floor_256 = (U32)a * GRAY_ALPHA_FLOOR_256;
        const U32 cov_256   = (y_256 > floor_256) ? y_256 : floor_256;
        const U32 cov       = cov_256 >> 8;
        return cov > 255u ? (U8)255u : (U8)cov;
    }
}

ALFontColrV1Painter::ALFontColrV1Painter()
{
}

ALFontColrV1Painter::~ALFontColrV1Painter()
{
}

bool ALFontColrV1Painter::paintGlyph(hb_font_t*       hb_font,
                                     U32              glyph_index,
                                     F32              fallback_point_size,
                                     const LLColor4U& foreground,
                                     U32              palette_index,
                                     OutputFormat     format,
                                     Result&          out)
{
    out = Result{};
    out.mFormat = format;

    if (!hb_font)
    {
        LL_DEBUGS("ColrV1") << "null hb_font, gid=" << glyph_index << LL_ENDL;
        return false;
    }

    // Reject out-of-range glyph indices up front. Without this, an unknown
    // gid passes through hb_raster_paint_glyph as a no-op and the 2-em
    // fallback below produces a "successful" empty rectangle rather than
    // a clean failure. Also assert the face actually has a COLRv1 paint
    // table — hb_raster_paint_glyph_or_fail is COLRv1-only; COLRv0 / CBDT /
    // SBIX / SVG go through FT_LOAD_COLOR upstream of this rasterizer.
    if (hb_face_t* hb_face = hb_font_get_face(hb_font))
    {
        llassert(hb_ot_color_has_paint(hb_face));
        const unsigned gcount = hb_face_get_glyph_count(hb_face);
        if (gcount && glyph_index >= gcount)
        {
            LL_DEBUGS("ColrV1") << "gid out of range, gid=" << glyph_index
                                << " gcount=" << gcount << LL_ENDL;
            return false;
        }
    }

    hb_raster_paint_t* p = get_paint();
    if (!p)
    {
        LL_DEBUGS("ColrV1") << "hb_raster_paint_create_or_fail returned null, gid="
                            << glyph_index << LL_ENDL;
        return false;
    }

    // Resolve glyph extents in 26.6 fixed-point pixels (HB scaled units).
    // Source preference matches the canonical hb-raster example:
    //   1. hb_font_get_glyph_extents — usually the OUTLINE bbox, which for
    //      COLRv1 emoji fonts is generally an em-square placeholder that
    //      generously contains the COLRv1 paint tree (incl. AA spread).
    //   2. FT_Get_Color_Glyph_ClipBox — spec-authoritative bbox per the
    //      COLR table, but some fonts ship clip boxes that are tighter
    //      than the actual rendered extent and clip AA edges (notably
    //      NotoEmoji's circle glyphs, which surfaced as sliced edges).
    //   3. 2-em fallback — for glyphs whose outline is a true stub (ZWJ
    //      ligature emoji) AND have no COLR clip box.
    // The result is handed to hb_raster_paint_set_glyph_extents, which
    // applies scale_factor + base_transform internally to produce a
    // pixel-aligned surface bbox using the SAME math the painter uses
    // for paint commands — so paint coords are guaranteed to land inside
    // the surface (no off-by-one between our bbox calc and the painter's).
    hb_glyph_extents_t ge = {};
    bool got_extents = false;

    if (hb_font_get_glyph_extents(hb_font, glyph_index, &ge))
        got_extents = (ge.width != 0 && ge.height != 0);

    if (!got_extents)
    {
        FT_Face ft_face = hb_ft_font_get_ft_face(hb_font);
        FT_ClipBox clip = {};
        if (ft_face && FT_Get_Color_Glyph_ClipBox(ft_face, glyph_index, &clip))
        {
            const FT_Pos xs[4] = { clip.bottom_left.x, clip.top_left.x,
                                   clip.top_right.x,   clip.bottom_right.x };
            const FT_Pos ys[4] = { clip.bottom_left.y, clip.top_left.y,
                                   clip.top_right.y,   clip.bottom_right.y };
            FT_Pos xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
            for (int i = 1; i < 4; ++i)
            {
                if (xs[i] < xmin) xmin = xs[i];
                if (xs[i] > xmax) xmax = xs[i];
                if (ys[i] < ymin) ymin = ys[i];
                if (ys[i] > ymax) ymax = ys[i];
            }
            ge.x_bearing = (hb_position_t)xmin;
            ge.y_bearing = (hb_position_t)ymax;
            ge.width     = (hb_position_t)(xmax - xmin);
            ge.height    = (hb_position_t)(ymin - ymax);
            got_extents = (ge.width != 0 && ge.height != 0);
        }
    }
    if (!got_extents)
    {
        unsigned x_ppem = 0, y_ppem = 0;
        hb_font_get_ppem(hb_font, &x_ppem, &y_ppem);
        if (x_ppem == 0 && fallback_point_size > 0.f)
            x_ppem = static_cast<unsigned>(fallback_point_size + 0.5f);
        if (y_ppem == 0 && fallback_point_size > 0.f)
            y_ppem = static_cast<unsigned>(fallback_point_size + 0.5f);
        if (x_ppem == 0 || y_ppem == 0)
        {
            LL_DEBUGS("ColrV1") << "no extents and no ppem fallback, gid="
                                << glyph_index << LL_ENDL;
            return false;
        }
        const S32 ppem_x_64 = static_cast<S32>(x_ppem) * 64;
        const S32 ppem_y_64 = static_cast<S32>(y_ppem) * 64;
        ge.x_bearing = -ppem_x_64;
        ge.y_bearing =  ppem_y_64 + (ppem_y_64 / 2);  // 1.5em above baseline
        ge.width     =  2 * ppem_x_64;
        ge.height    = -2 * ppem_y_64;
    }

    // Sanity-clip ge dimensions to a safe-arithmetic range before the
    // padding step below. Realistic fonts produce values in the tens of
    // thousands of 26.6 units (≈ thousands of pixels); MAX_COORD_26_6
    // sits orders of magnitude past that but well below INT32_MAX, so
    // padding cannot push the int32 fields into overflow / UB. A
    // malformed font reporting absurd extents bails here rather than
    // tripping a downstream sign flip in hb-raster's set_glyph_extents.
    // Range-check height directly (don't take its absolute value) because
    // `-ge.height` is UB when ge.height == INT32_MIN.
    {
        constexpr hb_position_t MAX_COORD_26_6 = 1 << 24;  // 16M units = 256K px
        if (ge.x_bearing >  MAX_COORD_26_6 || ge.x_bearing < -MAX_COORD_26_6
         || ge.y_bearing >  MAX_COORD_26_6 || ge.y_bearing < -MAX_COORD_26_6
         || ge.width    >  MAX_COORD_26_6 || ge.width    < 0
         || ge.height   >  MAX_COORD_26_6 || ge.height   < -MAX_COORD_26_6)
        {
            LL_DEBUGS("ColrV1") << "extents out of safe range, gid=" << glyph_index
                                << " ge=" << ge.x_bearing << "," << ge.y_bearing
                                << "," << ge.width << "," << ge.height << LL_ENDL;
            return false;
        }
    }

    // Pad the 26.6 bbox by 2 px on every side. set_glyph_extents floors the
    // lower-left corner of the (post-transform, post-scale) bbox; if the
    // glyph's outline bbox is integer-pixel aligned (e.g. an em-square
    // placeholder), AA coverage at the edge falls exactly on the boundary
    // and one column of subpixel coverage lands outside the surface,
    // visibly slicing curved edges. 2 px gives AA + small paint-tree
    // overshoot enough room without inflating mWidth meaningfully.
    {
        constexpr hb_position_t PAD_26_6 = 2 * 64;
        ge.x_bearing -= PAD_26_6;
        ge.y_bearing += PAD_26_6;
        ge.width     += 2 * PAD_26_6;
        ge.height    -= 2 * PAD_26_6;
    }

    // hb-raster's canonical setup for FT-backed fonts: scale_factor of 64
    // is "units per pixel" (i.e., shrink incoming 26.6 coords by 64 to
    // get pixels). Identity transform places the glyph origin at the
    // painter's origin (0, 0).
    hb_raster_paint_set_scale_factor(p, 64.f, 64.f);
    hb_raster_paint_set_transform(p, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    if (!hb_raster_paint_set_glyph_extents(p, &ge))
    {
        LL_DEBUGS("ColrV1") << "set_glyph_extents rejected padded ge, gid="
                            << glyph_index << LL_ENDL;
        return false;
    }

    // Foreground color used for CPAL palette index 0xFFFF resolution.
    // HB_COLOR packs (b, g, r, a). LLColor4U is straight (unpremultiplied)
    // RGBA bytes; hb-raster premultiplies internally as needed.
    const hb_color_t hb_fg = HB_COLOR(
        foreground.mV[VBLUE],
        foreground.mV[VGREEN],
        foreground.mV[VRED],
        foreground.mV[VALPHA]);
    hb_raster_paint_set_foreground(p, hb_fg);

    // CPAL palette selection. Clamp out-of-range indices to 0 explicitly
    // so behavior is pinned at our boundary rather than relying on
    // hb-raster's internal handling of oversized palette ids.
    if (hb_face_t* hb_face = hb_font_get_face(hb_font))
    {
        const unsigned palette_count = hb_ot_color_palette_get_count(hb_face);
        if (palette_count == 0u || palette_index >= palette_count)
            palette_index = 0u;
    }
    hb_raster_paint_set_palette(p, palette_index);

    // _or_fail surfaces "this glyph has no COLR paint tree" as a clean
    // false return, letting the caller fall through to FT outline
    // rasterization. The void variant silently no-ops in that case and
    // hb_raster_paint_render then hands back an all-zero image — which
    // we'd "successfully" copy into the atlas as an invisible glyph.
    //
    // Wrap _or_fail and _render together in a load-flags scope: hb-raster
    // pulls outline data via HB → hb-ft → FT_Load_Glyph, which honors the
    // hb_font's currently-set FT load flags. Color emoji must be unhinted
    // for faithful rasterization. Restored on scope exit so any concurrent
    // text-shaping call sees the user's original hinting choice.
    hb_raster_image_t* img = nullptr;
    {
        LoadFlagsGuard load_flags_guard(hb_font,
            FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);

        if (!hb_raster_paint_glyph_or_fail(p, hb_font, glyph_index))
        {
            LL_DEBUGS("ColrV1") << "no COLRv1 paint tree, gid=" << glyph_index
                                << " (caller falls through to FT outline)" << LL_ENDL;
            return false;
        }

        img = hb_raster_paint_render(p);
    }
    if (!img)
    {
        LL_DEBUGS("ColrV1") << "hb_raster_paint_render returned null, gid="
                            << glyph_index << LL_ENDL;
        return false;
    }

    hb_raster_extents_t e{};
    hb_raster_image_get_extents(img, &e);
    if (e.width == 0 || e.height == 0 || e.width > 1024 || e.height > 1024)
    {
        // Cap matches LLFontBitmapCache's 1024x1024 atlas sheet ceiling;
        // a glyph above that wouldn't fit a single sheet anyway.
        LL_DEBUGS("ColrV1") << "image size " << e.width << "x" << e.height
                            << " out of range, gid=" << glyph_index << LL_ENDL;
        hb_raster_paint_recycle_image(p, img);
        return false;
    }

    if (hb_raster_image_get_format(img) != HB_RASTER_FORMAT_BGRA32)
    {
        // hb-raster picks BGRA32 for COLRv1 / CBDT / SBIX paths; anything
        // else (A8) means a plain outline glyph that FT should handle.
        LL_DEBUGS("ColrV1") << "non-BGRA32 image format, gid=" << glyph_index << LL_ENDL;
        hb_raster_paint_recycle_image(p, img);
        return false;
    }

    const uint8_t* src    = hb_raster_image_get_buffer(img);
    const unsigned src_st = e.stride;
    if (!src || src_st < e.width * 4u)
    {
        LL_DEBUGS("ColrV1") << "bad image buffer (src=" << static_cast<const void*>(src)
                            << " stride=" << src_st << " width=" << e.width
                            << "), gid=" << glyph_index << LL_ENDL;
        hb_raster_paint_recycle_image(p, img);
        return false;
    }

    const S32 W = static_cast<S32>(e.width);
    const S32 H = static_cast<S32>(e.height);

    // Source pixel rows are stored bottom-to-top (hb-raster's y-up
    // convention). Result contract is top-row-first with a positive pitch,
    // so we walk source rows in reverse during copy.
    auto src_row = [&](S32 dst_y) -> const uint8_t*
    {
        const S32 sy = (H - 1) - dst_y;
        return src + static_cast<ptrdiff_t>(sy) * static_cast<ptrdiff_t>(src_st);
    };

    if (format == OutputFormat::Gray)
    {
        const size_t bytes = static_cast<size_t>(W) * static_cast<size_t>(H);
        mStaging.resize(bytes);
        for (S32 y = 0; y < H; ++y)
        {
            const uint8_t* row = src_row(y);
            U8* dst = mStaging.data() + static_cast<ptrdiff_t>(y) * W;
            for (S32 x = 0; x < W; ++x)
            {
                dst[x] = fold_gray(row[x*4+0], row[x*4+1],
                                   row[x*4+2], row[x*4+3]);
            }
        }
        out.mPitch  = W;
        out.mFormat = OutputFormat::Gray;
    }
    else
    {
        const S32 dst_stride = W * 4;
        const size_t bytes = static_cast<size_t>(dst_stride) * static_cast<size_t>(H);
        mStaging.resize(bytes);
        for (S32 y = 0; y < H; ++y)
        {
            std::memcpy(mStaging.data() + static_cast<ptrdiff_t>(y) * dst_stride,
                        src_row(y),
                        static_cast<size_t>(dst_stride));
        }
        out.mPitch  = dst_stride;
        out.mFormat = OutputFormat::BGRA;
    }

    mStagingWidth  = W;
    mStagingHeight = H;

    hb_raster_paint_recycle_image(p, img);

    if (mStaging.capacity() > mStaging.size() * 4
        && mStaging.size() < STAGING_HIGH_WATER_BYTES)
    {
        mStaging.shrink_to_fit();
    }

    out.mBitmap = mStaging.data();
    out.mWidth  = W;
    out.mHeight = H;

    // Convert hb-raster's lower-left, y-up bitmap origin to FT's
    // bitmap_left / bitmap_top convention (y-down distance from baseline
    // to the bitmap's TOP edge; positive when the glyph sits above
    // baseline).
    out.mLeft = e.x_origin;
    out.mTop  = e.y_origin + static_cast<S32>(e.height);
    return true;
}
