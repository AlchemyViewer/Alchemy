/**
 * @file alfontcolrv1.h
 * @brief COLRv1 paint-tree rasterizer using HarfBuzz's hb-raster backend.
 *        Publishes the result as either a BGRA bitmap (the existing color-
 *        glyph atlas path consumes unchanged) or a single-channel grayscale
 *        (luminance-shaded) bitmap that the Grayscale atlas can consume for
 *        monochrome rendering.
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

#include "stdtypes.h"
#include "v4coloru.h"

#include <vector>

struct hb_font_t;

class ALFontColrV1Painter
{
public:
    // Output pixel format requested by the caller. BGRA is hb-raster's
    // native format; Gray is a hybrid luminance/alpha fold suitable for
    // the Grayscale atlas (luminance-shaded silhouette that tints with
    // text_color at draw time).
    enum class OutputFormat
    {
        BGRA,
        Gray,
    };

    // Result of a paint pass. mBitmap layout depends on mFormat:
    //   - BGRA: 4 bytes per pixel, premultiplied, top-row-first.
    //   - Gray: 1 byte per pixel (coverage = max(Rec.709 luminance,
    //           alpha * 0.15)), top-row-first.
    // mPitch is bytes per row and is ALWAYS POSITIVE: surf_w*4 for BGRA,
    // surf_w for Gray. mBitmap stays valid until the next paintGlyph call
    // on the same painter.
    struct Result
    {
        const U8*    mBitmap = nullptr;
        S32          mWidth  = 0;
        S32          mHeight = 0;
        S32          mPitch  = 0;
        S32          mLeft   = 0;  // bitmap_left equivalent
        S32          mTop    = 0;  // bitmap_top equivalent
        OutputFormat mFormat = OutputFormat::BGRA;
    };

    ALFontColrV1Painter();
    ~ALFontColrV1Painter();

    // Walk hb_font's paint tree for `glyph_index` and rasterize into the
    // painter's internal staging surface. `foreground` is the active text
    // color used to resolve foreground-color paint slots (COLRv1 references
    // CPAL palette entry 0xFFFF as "use foreground"). `palette_index`
    // selects which CPAL palette HB resolves color refs against; 0 is the
    // default palette. `format` selects the output pixel layout (see
    // OutputFormat above). `fallback_point_size` is the requested type
    // size in pixels; only used when hb_font_get_ppem returns zero AND
    // both upstream extent sources fail (rare ZWJ-fallback path).
    //
    // Returns true on success and populates `out`. Returns false when the
    // glyph has no paint tree, hb-raster fails to allocate, or staging
    // allocation failed; the caller falls back to FT outline rasterization.
    bool paintGlyph(hb_font_t*       hb_font,
                    U32              glyph_index,
                    F32              fallback_point_size,
                    const LLColor4U& foreground,
                    U32              palette_index,
                    OutputFormat     format,
                    Result&          out);

    // Source-compat overload for callers that don't care about output
    // format and just want the painter's native BGRA result.
    bool paintGlyph(hb_font_t*       hb_font,
                    U32              glyph_index,
                    F32              fallback_point_size,
                    const LLColor4U& foreground,
                    U32              palette_index,
                    Result&          out)
    {
        return paintGlyph(hb_font, glyph_index, fallback_point_size,
                          foreground, palette_index, OutputFormat::BGRA, out);
    }

private:
    ALFontColrV1Painter(const ALFontColrV1Painter&) = delete;
    ALFontColrV1Painter& operator=(const ALFontColrV1Painter&) = delete;

    // Staging surface. Reused across paintGlyph calls; resized on demand.
    // For BGRA output: premultiplied, top-row-first, surf_w*4 bytes/row.
    // For Gray output: 8-bit coverage, top-row-first, surf_w bytes/row.
    std::vector<U8> mStaging;
    S32             mStagingWidth  = 0;
    S32             mStagingHeight = 0;
};
