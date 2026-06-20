/**
 * @file llfontgpuglyphcache.cpp
 * @brief Atlas-free glyph store for the analytic (hb-gpu) font path.
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

#include "llfontgpuglyphcache.h"

#if LL_HAS_HB_GPU

// hb.h / hb-gpu.h arrive via llhbgpu.h (included by the header above).
#include "llgl.h"
#include "llglheaders.h"

// Start at 1 so a freshly default-constructed consumer holding generation 0
// always reads as stale against any live cache.
S32 LLFontGpuGlyphCache::sNextGeneration = 1;

LLFontGpuGlyphCache::LLFontGpuGlyphCache()
    : mGeneration(sNextGeneration++)
{
}

LLFontGpuGlyphCache::~LLFontGpuGlyphCache()
{
    destroyGL();
    if (mEncoder)
    {
        hb_gpu_draw_destroy(mEncoder);
        mEncoder = nullptr;
    }
    if (mPaintEncoder)
    {
        hb_gpu_paint_destroy(mPaintEncoder);
        mPaintEncoder = nullptr;
    }
    if (mEncodeFont)
    {
        hb_font_destroy(mEncodeFont);
        mEncodeFont = nullptr;
    }
}

void LLFontGpuGlyphCache::init(hb_face_t* face, bool color, unsigned palette)
{
    reset();

    mColor   = color;
    mPalette = palette;

    if (mEncodeFont)
    {
        hb_font_destroy(mEncodeFont);
        mEncodeFont = nullptr;
    }

    if (face)
    {
        // Scale the encode font to the face's upem so the outline coordinates
        // the encoder pulls land in font design units. The renderer applies
        // font_size/upem at the vertex stage; the blob itself is size-agnostic.
        unsigned upem = hb_face_get_upem(face);
        mEncodeFont = hb_font_create(face);
        hb_font_set_scale(mEncodeFont, static_cast<int>(upem), static_cast<int>(upem));
    }
}

void LLFontGpuGlyphCache::ensureEncoder()
{
    if (mColor)
    {
        if (!mPaintEncoder)
        {
            mPaintEncoder = hb_gpu_paint_create_or_fail();
            if (mPaintEncoder)
            {
                hb_gpu_paint_set_palette(mPaintEncoder, mPalette);
            }
        }
    }
    else if (!mEncoder)
    {
        mEncoder = hb_gpu_draw_create_or_fail();
    }
}

LLFontGpuGlyphCache::GlyphLoc LLFontGpuGlyphCache::encodeGlyph(U32 glyph_id)
{
    GlyphLoc loc;
    if (!mEncodeFont)
    {
        return loc;
    }

    ensureEncoder();
    return mColor ? encodePaintGlyph(glyph_id) : encodeDrawGlyph(glyph_id);
}

// Append a blob's texels to the arena and fill in `loc` from the blob + the
// glyph's design-unit extents. Shared by the draw and paint encoders, which
// differ only in which hb-gpu encoder produced the blob.
namespace
{
    void store_blob(std::vector<U8>& arena, hb_blob_t* blob,
                    const hb_glyph_extents_t& ext,
                    LLFontGpuGlyphCache::GlyphLoc& loc)
    {
        unsigned len = blob ? hb_blob_get_length(blob) : 0;
        if (blob && len >= LLFontGpuGlyphCache::kBytesPerTexel)
        {
            const char* data = hb_blob_get_data(blob, nullptr);
            U32 byte_off = static_cast<U32>(arena.size());
            arena.insert(arena.end(), data, data + len);

            loc.mTexelOffset = byte_off / LLFontGpuGlyphCache::kBytesPerTexel;
            loc.mTexelCount  = len / LLFontGpuGlyphCache::kBytesPerTexel;
            loc.mXBearing    = ext.x_bearing;
            loc.mYBearing    = ext.y_bearing;
            loc.mWidth       = ext.width;
            loc.mHeight      = ext.height;
        }
    }
}

LLFontGpuGlyphCache::GlyphLoc LLFontGpuGlyphCache::encodeDrawGlyph(U32 glyph_id)
{
    GlyphLoc loc;
    if (!mEncoder)
    {
        return loc;
    }

    hb_gpu_draw_glyph(mEncoder, mEncodeFont, static_cast<hb_codepoint_t>(glyph_id));

    hb_glyph_extents_t ext = { 0, 0, 0, 0 };
    hb_blob_t* blob = hb_gpu_draw_encode(mEncoder, &ext);
    store_blob(mArena, blob, ext, loc);

    if (blob)
    {
        // Hand the buffer back so the next encode can reuse the allocation.
        hb_gpu_draw_recycle_blob(mEncoder, blob);
    }
    return loc;
}

LLFontGpuGlyphCache::GlyphLoc LLFontGpuGlyphCache::encodePaintGlyph(U32 glyph_id)
{
    GlyphLoc loc;
    if (!mPaintEncoder)
    {
        return loc;
    }

    hb_gpu_paint_glyph(mPaintEncoder, mEncodeFont, static_cast<hb_codepoint_t>(glyph_id));

    hb_glyph_extents_t ext = { 0, 0, 0, 0 };
    hb_blob_t* blob = hb_gpu_paint_encode(mPaintEncoder, &ext);
    store_blob(mArena, blob, ext, loc);

    if (blob)
    {
        hb_gpu_paint_recycle_blob(mPaintEncoder, blob);
    }
    return loc;
}

const LLFontGpuGlyphCache::GlyphLoc& LLFontGpuGlyphCache::getGlyph(U32 glyph_id)
{
    static const GlyphLoc sEmpty;
    if (!mEncodeFont)
    {
        return sEmpty;
    }

    if (auto it = mCache.find(glyph_id); it != mCache.end())
    {
        return it->second;
    }

    GlyphLoc loc = encodeGlyph(glyph_id);

    // Overflow eviction: if this glyph pushed the arena past the ceiling and
    // there were prior glyphs, drop everything and re-encode this one into the
    // fresh arena (so it lands at offset 0). A lone glyph bigger than the whole
    // budget is pathological; we keep it rather than loop forever.
    if (loc.drawable() && getArenaTexels() > mMaxTexels && !mCache.empty())
    {
        reset();
        loc = encodeGlyph(glyph_id);
    }

    auto [it, ok] = mCache.emplace(glyph_id, loc);
    return it->second;
}

bool LLFontGpuGlyphCache::ensureGLBuffer()
{
    // glTexBuffer is resolved at GL bring-up; null means no texture-buffer
    // support (or GL not yet initialized).
    if (!glTexBuffer)
    {
        return false;
    }

    if (!mGLBuffer)
    {
        glGenBuffers(1, &mGLBuffer);
        glGenTextures(1, &mGLTexture);
        mGLCapacityBytes = 0;
        mUploadedBytes   = 0;
    }

    // Pre-size to the ceiling so steady-state growth needs no reallocation;
    // honor an oversized arena from the pathological lone-glyph case.
    U32 want = llmax(mMaxTexels * kBytesPerTexel, static_cast<U32>(mArena.size()));
    if (mGLCapacityBytes < want)
    {
        glBindBuffer(GL_TEXTURE_BUFFER, mGLBuffer);
        glBufferData(GL_TEXTURE_BUFFER, want, nullptr, GL_DYNAMIC_DRAW);
        mGLCapacityBytes = want;
        mUploadedBytes   = 0;   // store was reallocated — re-upload everything
        glBindTexture(GL_TEXTURE_BUFFER, mGLTexture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA16I, mGLBuffer);
    }
    return true;
}

bool LLFontGpuGlyphCache::bindBufferTexture()
{
    if (!ensureGLBuffer())
    {
        return false;
    }

    if (mUploadedBytes < mArena.size())
    {
        glBindBuffer(GL_TEXTURE_BUFFER, mGLBuffer);
        glBufferSubData(GL_TEXTURE_BUFFER,
                        mUploadedBytes,
                        mArena.size() - mUploadedBytes,
                        mArena.data() + mUploadedBytes);
        mUploadedBytes = static_cast<U32>(mArena.size());
    }

    glBindTexture(GL_TEXTURE_BUFFER, mGLTexture);
    return true;
}

void LLFontGpuGlyphCache::destroyGL()
{
    if (mGLTexture)
    {
        glDeleteTextures(1, &mGLTexture);
        mGLTexture = 0;
    }
    if (mGLBuffer)
    {
        glDeleteBuffers(1, &mGLBuffer);
        mGLBuffer = 0;
    }
    mGLCapacityBytes = 0;
    mUploadedBytes   = 0;
}

void LLFontGpuGlyphCache::reset()
{
    mArena.clear();
    mCache.clear();
    mUploadedBytes = 0;
    // Keep the GL objects (capacity is reused); the next bind re-uploads the
    // fresh, smaller arena. Bump generation so dependent vertex buffers rebuild.
    mGeneration = sNextGeneration++;
}

#endif // LL_HAS_HB_GPU
