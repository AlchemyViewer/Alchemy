/**
 * @file llfontgpubatch.h
 * @brief Geometry build + draw for the analytic (hb-gpu) font path.
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
#include "llpointer.h"

#if LL_HAS_HB_GPU

#include <vector>

class LLGLSLShader;
class LLFontGpuGlyphCache;
class LLVertexBuffer;

// Builds and draws analytic (hb-gpu) glyph geometry for a run of glyphs that
// share one face's LLFontGpuGlyphCache and one scale (point size). Each glyph
// becomes a screen-space quad whose vertices carry, in lock-step, the screen
// position and the same corner in glyph design (em) units -> the renderCoord the
// Slug rasterizer samples; the invariant position == pen + texcoord*scale keeps
// the two consistent, which is what makes the analytic lookup correct
// independent of the absolute unit scale.
//
// The geometry is uploaded through LLVertexBuffer (reserved attributes
// position / texcoord0 / diffuse_color + the integer glyph_loc), so VAO setup,
// buffer upload, matrix sync, and platform handling all go through the standard
// pipeline. The corner normal is derived in-shader from gl_VertexID and the
// Jacobian is a per-run uniform, so neither needs a vertex attribute.
class LLFontGpuBatch
{
public:
    // A glyph to draw: its index in the cache's face, the pen (baseline origin)
    // in screen pixels, and the RGBA color. The scale (pixels per design unit,
    // ppem/upem) is shared by the whole run and passed to build/render.
    struct Placement
    {
        U32 glyph_id = 0;
        F32 pen_x = 0.f;
        F32 pen_y = 0.f;
        U8  color[4] = { 255, 255, 255, 255 };
    };

    // CPU vertex (the layout-test view of the geometry, before it is strided
    // into the LLVertexBuffer's separate attribute arrays).
    struct Vertex
    {
        F32 pos[2];
        F32 tc[2];
        U32 glyphLoc;
        U8  col[4];
    };

    LLFontGpuBatch();
    ~LLFontGpuBatch();

    LLFontGpuBatch(const LLFontGpuBatch&)            = delete;
    LLFontGpuBatch& operator=(const LLFontGpuBatch&) = delete;

    // CPU-only: encode (on miss) and lay out 6 vertices per drawable glyph;
    // non-drawable glyphs (space) are skipped. Returns the vertex count.
    // `slant` is a faux-italic shear in screen pixels applied to each quad's
    // bottom edge (0 = upright), matching the atlas drawGlyph slant. Exposed
    // (with vertices()) for headless geometry tests.
    U32 buildVertices(LLFontGpuGlyphCache& cache,
                      const std::vector<Placement>& placements, F32 scale, F32 slant = 0.f);

    const std::vector<Vertex>& vertices() const { return mVerts; }

    // Build, upload, and draw: binds `program`, sets the viewport, the per-run
    // Jacobian, and the cache's glyph buffer as hb_gpu_atlas, then draws via
    // LLVertexBuffer (which syncs the MVP). Returns false if nothing is drawable
    // or GL texture-buffer support is missing. Requires a current GL context.
    bool render(LLGLSLShader& program, LLFontGpuGlyphCache& cache,
                const std::vector<Placement>& placements, F32 scale,
                S32 viewport_w, S32 viewport_h, F32 slant = 0.f);

    void destroyGL();

private:
    std::vector<Vertex>       mVerts;
    LLPointer<LLVertexBuffer> mVB;
};

#endif // LL_HAS_HB_GPU
#endif // LL_LLFONTGPUBATCH_H
