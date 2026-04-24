/**
 * @file llfontfreetypesvg.cpp
 * @brief Freetype font library SVG glyph rendering
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

#include "linden_common.h"

#include "llfontfreetypesvg.h"

#include <cfloat>

#if LL_WINDOWS
#pragma warning (push)
#pragma warning (disable : 4702)
#endif

#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvgrast.h>

#if LL_WINDOWS
#pragma warning (pop)
#endif

struct LLSvgRenderData
{
    FT_UInt    GlyphIndex = 0;
    FT_Error   Error = FT_Err_Ok; // FreeType currently (@2.12.1) ignores the error value returned by the preset glyph slot callback so we return it at render time
    // (See https://github.com/freetype/freetype/blob/5faa1df8b93ebecf0f8fd5fe8fda7b9082eddced/src/base/ftobjs.c#L1170)
    NSVGimage* pNSvgImage = nullptr;
    float      Scale = 0.f;
    // Pixel-space translation applied at rasterisation so the path's left
    // edge lines up with bitmap x=0. Without this the bitmap would either
    // waste a margin (if path doesn't start at 0) or clip (if path
    // extends past the bitmap). See OnPresetGlypthSlot for the sizing
    // rationale.
    float      TranslateX = 0.f;
};

// static
FT_Error LLFontFreeTypeSvgRenderer::OnInit(FT_Pointer* state)
{
    // The SVG driver hook state is shared across all callback invocations; since our state is lightweight
    // we store it in the glyph instead.
    *state = nullptr;

    return FT_Err_Ok;
}

// static
void LLFontFreeTypeSvgRenderer::OnFree(FT_Pointer* state)
{
}

// static
void LLFontFreeTypeSvgRenderer::OnDataFinalizer(void* objectp)
{
    FT_GlyphSlot glyph_slot = static_cast<FT_GlyphSlot>(objectp);

    LLSvgRenderData* pData = static_cast<LLSvgRenderData*>(glyph_slot->generic.data);
    glyph_slot->generic.data = nullptr;
    glyph_slot->generic.finalizer = nullptr;
    delete(pData);
}

//static
FT_Error LLFontFreeTypeSvgRenderer::OnPresetGlypthSlot(FT_GlyphSlot glyph_slot, FT_Bool cache, FT_Pointer*)
{
    FT_SVG_Document document = static_cast<FT_SVG_Document>(glyph_slot->other);

    if (!glyph_slot->generic.data)
    {
        glyph_slot->generic.data = new LLSvgRenderData();
        glyph_slot->generic.finalizer = LLFontFreeTypeSvgRenderer::OnDataFinalizer;
    }
    LLSvgRenderData* datap = static_cast<LLSvgRenderData*>(glyph_slot->generic.data);

    // Any leftover NSVGimage was parsed for a previous glyph. Discard it
    // whenever the slot no longer matches — otherwise OnRender will happily
    // rasterise the *previous* glyph's SVG into the *current* glyph's
    // bitmap, which is exactly how e.g. 🔶 and 🔸 end up looking identical
    // when the second glyph's parse is skipped. This happens whenever some
    // earlier codepath invoked FT_Load_Glyph (for metrics, or via HarfBuzz)
    // on an SVG glyph without a matching FT_Render_Glyph to clear
    // pNSvgImage. Forcing a re-parse on !cache, or on a glyph-index
    // mismatch, keeps the data consistent regardless of who left the
    // orphan behind.
    const bool glyph_matches = (datap->GlyphIndex == glyph_slot->glyph_index);
    if (datap->pNSvgImage && (!cache || !glyph_matches))
    {
        nsvgDelete(datap->pNSvgImage);
        datap->pNSvgImage = nullptr;
    }

    if (!cache)
    {
        datap->GlyphIndex = glyph_slot->glyph_index;
        datap->Error = FT_Err_Ok;
    }

    // NOTE: nsvgParse modifies the input string so we need a temporary copy
    if (!datap->pNSvgImage)
    {
        char* document_buffer = new char[document->svg_document_length + 1];
        memcpy(document_buffer, document->svg_document, document->svg_document_length);
        document_buffer[document->svg_document_length] = '\0';

        datap->pNSvgImage = nsvgParse(document_buffer, "px", 0.);

        delete[] document_buffer;
    }

    if (!datap->pNSvgImage)
    {
        datap->Error = FT_Err_Invalid_SVG_Document;
        return FT_Err_Invalid_SVG_Document;
    }

    // We don't (currently) support transformations so test for an identity rotation matrix + zero translation
    if (document->transform.xx != 1 << 16 || document->transform.yx != 0 ||
        document->transform.xy != 0 || document->transform.yy != 1 << 16 ||
        document->delta.x > 0 || document->delta.y > 0)
    {
        datap->Error = FT_Err_Unimplemented_Feature;
        return FT_Err_Unimplemented_Feature;
    }

    // Scale every glyph against the font's units_per_EM, not
    // pNSvgImage->width. nanosvg computes the latter from the *path
    // extent*, not the SVG canvas — so two glyphs whose artwork fills
    // different fractions of the source canvas (e.g. 🔶 at 1977 units and
    // 🔸 at 1204 units in Twemoji's 2048-upem grid) end up scaled by
    // different factors and both fill the same bitmap, collapsing the
    // size difference the font author encoded. Using upem keeps every
    // glyph on the same em grid so relative sizes survive rasterisation.
    const float svg_scale = (float)document->metrics.x_ppem / (float)document->units_per_EM;
    datap->Scale = svg_scale;

    // Find the path's horizontal extent so we can size the bitmap tightly
    // and leave any trailing em-grid slack as gap inside the advance box.
    // Without this, consecutive glyphs whose bitmaps equal the advance
    // width render edge-to-edge — e.g. regional-indicator flag pairs
    // touch with no margin.
    float min_x = FLT_MAX, max_x = -FLT_MAX;
    for (NSVGshape* s = datap->pNSvgImage->shapes; s; s = s->next)
    {
        if (s->bounds[0] < min_x) min_x = s->bounds[0];
        if (s->bounds[2] > max_x) max_x = s->bounds[2];
    }
    if (min_x >= max_x)
    {
        min_x = 0.f;
        max_x = (float)document->units_per_EM;
    }
    const float path_x_px = min_x * svg_scale;
    const int bw = llmax(1, (int)ceilf((max_x - min_x) * svg_scale));

    glyph_slot->bitmap.width = bw;
    // Vertical handling stays on the full em box; nanosvg's rasteriser
    // clips content outside the bitmap and the existing ascender-based
    // bitmap_top positions it correctly against the baseline.
    glyph_slot->bitmap.rows = document->metrics.y_ppem;
    // bitmap_left positions the bitmap's left edge in the advance box;
    // bitmap_left + bw < advance yields the visible margin. For most
    // Twemoji glyphs this recovers a small pixel gap at the font's
    // natural ppem.
    glyph_slot->bitmap_left = (int)floorf(path_x_px);
    glyph_slot->bitmap_top = (FT_Int)(glyph_slot->face->size->metrics.ascender / 64.f);
    glyph_slot->bitmap.pitch = bw * 4;
    glyph_slot->bitmap.pixel_mode = FT_PIXEL_MODE_BGRA;

    // Rasterise-time translation shifts the path so its left edge aligns
    // with bitmap x=0. Y is left at 0 — the existing layout has always
    // worked that way.
    datap->TranslateX = -path_x_px;

    /* Copied as-is from fcft (MIT license) */

    // Compute all the bearings and set them correctly. The outline is scaled already, we just need to use the bounding box.
    float horiBearingX = 0.f;
    float horiBearingY = -(float)glyph_slot->bitmap_top;

    // XXX parentheses correct?
    float vertBearingX = glyph_slot->metrics.horiBearingX / 64.0f - glyph_slot->metrics.horiAdvance / 64.0f / 2;
    float vertBearingY = (glyph_slot->metrics.vertAdvance / 64.0f - glyph_slot->metrics.height / 64.0f) / 2;

    // Do conversion in two steps to avoid 'bad function cast' warning
    glyph_slot->metrics.width = glyph_slot->bitmap.width * 64;
    glyph_slot->metrics.height = glyph_slot->bitmap.rows * 64;
    glyph_slot->metrics.horiBearingX = (FT_Pos)(horiBearingX * 64);
    glyph_slot->metrics.horiBearingY = (FT_Pos)(horiBearingY * 64);
    glyph_slot->metrics.vertBearingX = (FT_Pos)(vertBearingX * 64);
    glyph_slot->metrics.vertBearingY = (FT_Pos)(vertBearingY * 64);
    if (glyph_slot->metrics.vertAdvance == 0)
    {
        glyph_slot->metrics.vertAdvance = (FT_Pos)(glyph_slot->bitmap.rows * 1.2f * 64);
    }

    return FT_Err_Ok;
}

// static
FT_Error LLFontFreeTypeSvgRenderer::OnRender(FT_GlyphSlot glyph_slot, FT_Pointer*)
{
    LLSvgRenderData* datap = static_cast<LLSvgRenderData*>(glyph_slot->generic.data);
    llassert(FT_Err_Ok == datap->Error);
    if (FT_Err_Ok != datap->Error)
    {
        return datap->Error;
    }

    // Render to glyph bitmap. TranslateX shifts the path so its left edge
    // aligns with bitmap x=0 (see OnPresetGlypthSlot for why we size the
    // bitmap tight around the path extent).
    NSVGrasterizer* nsvgRasterizer = nsvgCreateRasterizer();
    nsvgRasterize(nsvgRasterizer, datap->pNSvgImage, datap->TranslateX, 0, datap->Scale, glyph_slot->bitmap.buffer, glyph_slot->bitmap.width, glyph_slot->bitmap.rows, glyph_slot->bitmap.pitch);
    nsvgDeleteRasterizer(nsvgRasterizer);
    nsvgDelete(datap->pNSvgImage);
    datap->pNSvgImage = nullptr;

    // Convert from RGBA to BGRA
    U32* pixel_buffer = (U32*)glyph_slot->bitmap.buffer; U8* byte_buffer = glyph_slot->bitmap.buffer;
    for (size_t y = 0, h = glyph_slot->bitmap.rows; y < h; y++)
    {
        for (size_t x = 0, w = glyph_slot->bitmap.pitch / 4; x < w; x++)
        {
            size_t pixel_idx = y * w + x;
            size_t byte_idx = pixel_idx * 4;
            U8 alpha = byte_buffer[byte_idx + 3];
            // Store as ARGB (*TODO - do we still have to care about endianness?)
            pixel_buffer[y * w + x] = alpha << 24 | (byte_buffer[byte_idx] * alpha / 0xFF) << 16 | (byte_buffer[byte_idx + 1] * alpha / 0xFF) << 8 | (byte_buffer[byte_idx + 2] * alpha / 0xFF);
        }
    }

    glyph_slot->format = FT_GLYPH_FORMAT_BITMAP;
    glyph_slot->bitmap.pixel_mode = FT_PIXEL_MODE_BGRA;
    return FT_Err_Ok;
}
