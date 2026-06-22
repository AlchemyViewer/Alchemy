/**
 * @file llfontgpubatch.h
 * @brief Glyph-quad geometry helper for the analytic (hb-gpu) font path.
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

#ifndef LL_LLFONTGPUBATCH_H
#define LL_LLFONTGPUBATCH_H

#include "stdtypes.h"
#include "llhbgpu.h"   // LL_HAS_HB_GPU

#if LL_HAS_HB_GPU

#include "llfontgpuglyphcache.h"   // GlyphLoc

class LLVector4a;
class LLVector2;
class LLColor4U;

// Stateless geometry helper for the analytic (hb-gpu) font path. Glyphs are
// emitted through gGL's batched immediate stream (exactly like the atlas path),
// so all that's needed is to lay out one glyph's six screen-space vertices: each
// carries the screen position AND the same corner in glyph design (em) units --
// the renderCoord the Slug rasterizer samples. The invariant
// position == pen + texcoord * scale keeps the two consistent, which is what makes
// the analytic lookup correct independent of the absolute unit scale.
//
// The quad is CPU pre-dilated by half a pixel for analytic-edge AA -- this
// replaces the in-shader hb_gpu_dilate, which relied on gl_VertexID and so can't
// run once glyphs share a draw with other geometry. UI is orthographic, so the
// half-pixel expansion is a constant screen offset per corner.
namespace LLFontGpuBatch
{
    // Append one glyph's 6 vertices (TL,BL,BR,TL,BR,TR) into the caller's arrays
    // (each must hold >= 6 elements). `slant` is a faux-italic screen-x shear
    // applied to the bottom edge (0 = upright). `glyph_loc` is the per-vertex
    // mode/lookup value handed to the UI shader (global texel offset, optionally
    // with the COLOR bit set).
    void buildGlyphQuad(LLVector4a* pos, LLVector2* uv, LLColor4U* col, U32* gloc,
                        const LLFontGpuGlyphCache::GlyphLoc& loc,
                        F32 pen_x, F32 pen_y, F32 scale, F32 slant,
                        const LLColor4U& color, U32 glyph_loc);
}

#endif // LL_HAS_HB_GPU
#endif // LL_LLFONTGPUBATCH_H
