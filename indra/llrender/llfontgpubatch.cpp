/**
 * @file llfontgpubatch.cpp
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

#include "linden_common.h"

#include "llfontgpubatch.h"

#if LL_HAS_HB_GPU

#include "llfontgpuglyphcache.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llvertexbuffer.h"

U32 LLFontGpuBatch::buildVertices(LLFontGpuGlyphCache& cache,
                                  const std::vector<Placement>& placements, F32 scale, F32 slant)
{
    mVerts.clear();
    mVerts.reserve(placements.size() * 6);

    for (const Placement& p : placements)
    {
        const LLFontGpuGlyphCache::GlyphLoc& loc = cache.getGlyph(p.glyph_id);
        if (!loc.drawable())
        {
            continue;   // space / outline-less glyph: nothing to draw
        }

        // Glyph box in design (em) units. height is negative (downward), so the
        // bottom edge is yb + h.
        const F32 lu = (F32)loc.mXBearing;
        const F32 ru = (F32)(loc.mXBearing + loc.mWidth);
        const F32 tu = (F32)loc.mYBearing;
        const F32 bu = (F32)(loc.mYBearing + loc.mHeight);

        // Faux italic: shear the screen quad's bottom edge by `slant` while the
        // em-space sample coordinate (texcoord) stays upright, so the analytic
        // glyph samples normally but draws slanted -- the same trick the atlas
        // path uses in renderTriangle. (Top corners get 0; bottom corners get
        // `slant`.) The dilation Jacobian stays diagonal, which leaves a tiny
        // AA approximation on the slanted edges -- acceptable for faux italic.
        auto emit = [&](F32 u, F32 v, F32 shear)
        {
            Vertex vert;
            vert.pos[0] = p.pen_x + u * scale + shear;
            vert.pos[1] = p.pen_y + v * scale;
            vert.tc[0]  = u;
            vert.tc[1]  = v;
            vert.glyphLoc = loc.mTexelOffset;
            vert.col[0] = p.color[0]; vert.col[1] = p.color[1];
            vert.col[2] = p.color[2]; vert.col[3] = p.color[3];
            mVerts.push_back(vert);
        };

        // 6 verts (TL,BL,BR,TL,BR,TR) — must match HB_CORNER_NORMAL[] in the
        // vertex shader, which derives each corner's outward normal from
        // gl_VertexID % 6.
        emit(lu, tu, 0.f);    // TL
        emit(lu, bu, slant);  // BL
        emit(ru, bu, slant);  // BR
        emit(lu, tu, 0.f);    // TL
        emit(ru, bu, slant);  // BR
        emit(ru, tu, 0.f);    // TR
    }

    return (U32)mVerts.size();
}

bool LLFontGpuBatch::render(LLGLSLShader& program, LLFontGpuGlyphCache& cache,
                            const std::vector<Placement>& placements, F32 scale,
                            S32 viewport_w, S32 viewport_h, F32 slant)
{
    // NOTE: a batch must fit within the cache's texel budget; if buildVertices
    // triggered an eviction mid-run the earlier glyphLocs would be stale. Runs
    // are small relative to the cache ceiling, so this is acceptable for now.
    const U32 nverts = buildVertices(cache, placements, scale, slant);
    if (nverts == 0)
    {
        return false;
    }

    constexpr U32 kMask = LLVertexBuffer::MAP_VERTEX | LLVertexBuffer::MAP_TEXCOORD0 |
                          LLVertexBuffer::MAP_COLOR  | LLVertexBuffer::MAP_GLYPH_LOC;

    if (mVB.isNull() || mVB->getNumVerts() < nverts)
    {
        mVB = new LLVertexBuffer(kMask);
        if (!mVB->allocateBuffer(nverts, 0))
        {
            mVB = nullptr;
            return false;
        }
    }

    {
        LLStrider<LLVector3> pos;
        LLStrider<LLVector2> tc;
        LLStrider<LLColor4U> col;
        LLStrider<U32>       gloc;
        if (!mVB->getVertexStrider(pos) || !mVB->getTexCoord0Strider(tc) ||
            !mVB->getColorStrider(col) || !mVB->getGlyphLocStrider(gloc))
        {
            return false;
        }
        for (U32 i = 0; i < nverts; ++i)
        {
            const Vertex& v = mVerts[i];
            pos[i]  = LLVector3(v.pos[0], v.pos[1], 0.f);
            tc[i]   = LLVector2(v.tc[0], v.tc[1]);
            col[i]  = LLColor4U(v.col[0], v.col[1], v.col[2], v.col[3]);
            gloc[i] = v.glyphLoc;
        }
    }
    mVB->unmapBuffer();

    program.bind();

    static const LLStaticHashedString sViewport("viewport");
    static const LLStaticHashedString sJac("jac");
    static const LLStaticHashedString sAtlas("hb_gpu_atlas");

    program.uniform2f(sViewport, (F32)viewport_w, (F32)viewport_h);
    // Inverse em->screen Jacobian: uniform scale, same sign on both axes.
    const F32 inv_s = (scale != 0.f) ? (1.f / scale) : 0.f;
    program.uniform4f(sJac, inv_s, 0.f, 0.f, inv_s);

    // Bind the glyph texel buffer as hb_gpu_atlas on unit 0. Activate the unit
    // through gGL so its tracking stays consistent; the buffer texture itself
    // lives on the GL_TEXTURE_BUFFER target, which gGL does not manage.
    gGL.getTexUnit(0)->activate();
    if (!cache.bindBufferTexture())
    {
        return false;
    }
    program.uniform1i(sAtlas, 0);

    mVB->setBuffer();
    mVB->drawArrays(LLRender::TRIANGLES, 0, nverts);
    return true;
}

void LLFontGpuBatch::destroyGL()
{
    mVB = nullptr;
}

LLFontGpuBatch::LLFontGpuBatch() = default;

LLFontGpuBatch::~LLFontGpuBatch()
{
    destroyGL();
}

#endif // LL_HAS_HB_GPU
