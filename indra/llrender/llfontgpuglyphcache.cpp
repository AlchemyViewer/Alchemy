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

// The shared (global) texel store. One arena / GL buffer serves every per-face
// cache so glyph_loc is a global offset and a single buffer texture covers all
// glyph rendering.
std::vector<U8> LLFontGpuGlyphCache::sArena;
U32 LLFontGpuGlyphCache::sUploadedBytes   = 0;
U32 LLFontGpuGlyphCache::sMaxTexels       = LLFontGpuGlyphCache::kDefaultMaxTexels;
U32 LLFontGpuGlyphCache::sGLBuffer        = 0;
U32 LLFontGpuGlyphCache::sGLTexture       = 0;
U32 LLFontGpuGlyphCache::sGLCapacityBytes = 0;
// Start at 1 so a freshly default-constructed consumer holding generation 0
// always reads as stale against the live store.
S32 LLFontGpuGlyphCache::sGeneration      = 1;

LLFontGpuGlyphCache::LLFontGpuGlyphCache()
    : mLastArenaGen(sGeneration)
{
}

LLFontGpuGlyphCache::~LLFontGpuGlyphCache()
{
    // Do NOT touch the shared GL store here — other faces' caches use it. Only
    // this cache's per-face encoders/map go away; the shared store is torn down
    // once via the static destroyGL() at GL teardown.
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

void LLFontGpuGlyphCache::init(hb_font_t* src_font, bool color, unsigned palette)
{
    // Re-init clears only THIS face's map (the shared arena belongs to all faces).
    // Sync to the current generation so getGlyph doesn't immediately re-clear.
    mCache.clear();
    mLastArenaGen = sGeneration;

    mColor   = color;
    mPalette = palette;

    if (mEncodeFont)
    {
        hb_font_destroy(mEncodeFont);
        mEncodeFont = nullptr;
    }

    hb_face_t* face = src_font ? hb_font_get_face(src_font) : nullptr;
    if (face)
    {
        // Scale the encode font to the face's upem so the outline coordinates
        // the encoder pulls land in font design units. The renderer applies
        // font_size/upem at the vertex stage; the blob itself is size-agnostic.
        unsigned upem = hb_face_get_upem(face);
        mEncodeFont = hb_font_create(face);
        hb_font_set_scale(mEncodeFont, static_cast<int>(upem), static_cast<int>(upem));

        // Carry the source font's variation-axis state onto the encode font.
        // hb_font_create() starts at the face's DEFAULT instance, so a variable
        // face configured for bold/weight/optical (the source font's var coords,
        // which LLFontFace mirrors from the FT_Face) would otherwise encode the
        // regular master and render un-emboldened. Copy the NORMALIZED coords:
        // src_font and the encode font share the exact same face, so normalized
        // values transfer directly with no design->normalized round-trip.
        unsigned num_coords = 0;
        const int* coords = hb_font_get_var_coords_normalized(src_font, &num_coords);
        if (coords && num_coords)
        {
            hb_font_set_var_coords_normalized(mEncodeFont, coords, num_coords);
        }
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
        // hb-gpu emits whole RGBA16I texels; trim any partial trailing texel
        // defensively so a non-multiple length can't misalign every subsequent
        // glyph's texel offset (byte_off / kBytesPerTexel).
        len -= len % LLFontGpuGlyphCache::kBytesPerTexel;
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
    store_blob(sArena, blob, ext, loc);

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
    store_blob(sArena, blob, ext, loc);

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

    // The shared arena may have been reset (eviction) by another face's cache
    // since we last looked; our stored offsets are then stale. Drop them.
    if (mLastArenaGen != sGeneration)
    {
        mCache.clear();
        mLastArenaGen = sGeneration;
    }

    if (auto it = mCache.find(glyph_id); it != mCache.end())
    {
        return it->second;
    }

    const bool had_prior = !sArena.empty();
    GlyphLoc loc = encodeGlyph(glyph_id);

    // Overflow eviction: if this glyph pushed the SHARED arena past the ceiling
    // and there was prior content, reset the whole store and re-encode this one
    // into the fresh arena (so it lands at offset 0). reset() bumps sGeneration;
    // sync ours and clear our (now-gone) entries. A lone glyph bigger than the
    // whole budget is pathological; we keep it rather than loop forever.
    if (loc.drawable() && getArenaTexels() > sMaxTexels && had_prior)
    {
        reset();
        mCache.clear();
        mLastArenaGen = sGeneration;
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

    if (!sGLBuffer)
    {
        glGenBuffers(1, &sGLBuffer);
        glGenTextures(1, &sGLTexture);
        sGLCapacityBytes = 0;
        sUploadedBytes   = 0;
    }

    // Pre-size to the ceiling so steady-state growth needs no reallocation;
    // honor an oversized arena from the pathological lone-glyph case. Compute the
    // ceiling in 64-bit so sMaxTexels * kBytesPerTexel can't overflow U32 (only
    // reachable via an absurd setMaxTexels, but cheap to guard).
    U32 want = static_cast<U32>(llmax((U64)sMaxTexels * kBytesPerTexel,
                                      (U64)sArena.size()));
    if (sGLCapacityBytes < want)
    {
        glBindBuffer(GL_TEXTURE_BUFFER, sGLBuffer);
        glBufferData(GL_TEXTURE_BUFFER, want, nullptr, GL_DYNAMIC_DRAW);
        sGLCapacityBytes = want;
        sUploadedBytes   = 0;   // store was reallocated — re-upload everything
        glBindTexture(GL_TEXTURE_BUFFER, sGLTexture);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA16I, sGLBuffer);
    }
    return true;
}

bool LLFontGpuGlyphCache::bindBufferTexture()
{
    if (!ensureGLBuffer())
    {
        return false;
    }

    if (sUploadedBytes < sArena.size())
    {
        glBindBuffer(GL_TEXTURE_BUFFER, sGLBuffer);
        glBufferSubData(GL_TEXTURE_BUFFER,
                        sUploadedBytes,
                        sArena.size() - sUploadedBytes,
                        sArena.data() + sUploadedBytes);
        sUploadedBytes = static_cast<U32>(sArena.size());
    }

    glBindTexture(GL_TEXTURE_BUFFER, sGLTexture);
    return true;
}

void LLFontGpuGlyphCache::destroyGL()
{
    if (sGLTexture)
    {
        glDeleteTextures(1, &sGLTexture);
        sGLTexture = 0;
    }
    if (sGLBuffer)
    {
        glDeleteBuffers(1, &sGLBuffer);
        sGLBuffer = 0;
    }
    sGLCapacityBytes = 0;
    sUploadedBytes   = 0;
}

void LLFontGpuGlyphCache::reset()
{
    sArena.clear();
    sUploadedBytes = 0;
    // Keep the GL objects (capacity is reused); the next bind re-uploads the
    // fresh, smaller arena. Bump generation so dependent vertex buffers (and
    // every per-face map) rebuild. Per-face maps clear lazily on their next
    // getGlyph when they notice the generation moved.
    ++sGeneration;
}

#endif // LL_HAS_HB_GPU
