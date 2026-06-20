/**
 * @file llfontgpuglyphcache.h
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

#ifndef LL_LLFONTGPUGLYPHCACHE_H
#define LL_LLFONTGPUGLYPHCACHE_H

#include "stdtypes.h"
#include "llhbgpu.h"   // defines LL_HAS_HB_GPU; pulls hb.h (+ hb-gpu.h)

#if LL_HAS_HB_GPU

#include <boost/unordered/unordered_flat_map.hpp>
#include <vector>

// Analytic (hb-gpu / Slug-style) glyph store. The atlas-free counterpart to
// LLFontBitmapCache: instead of rasterizing each glyph at each ppem into a
// bitmap atlas, each glyph's outline is encoded ONCE — in font design units,
// so the encoding is size-independent — into a compact RGBA16I texel blob held
// in a GL_TEXTURE_BUFFER. The fragment shader reads a glyph by its texel offset
// (glyphLoc) and rasterizes it analytically at any scale.
//
// One instance per LLFontFace (same ownership model as LLFontBitmapCache), so
// the cache key is just the FreeType glyph index and the cache lifetime is
// tied to the face — no cross-face dangling-pointer hazard.
class LLFontGpuGlyphCache
{
public:
    // Per-glyph location in the texel buffer plus the font-unit extents the
    // renderer needs to build the draw quad. mTexelCount == 0 marks a glyph
    // with no drawable outline (space, empty .notdef, or an encode that
    // produced nothing); it is cached as a negative result so the encoder
    // isn't re-run on every lookup.
    struct GlyphLoc
    {
        U32 mTexelOffset = 0;   // glyphLoc handed to the fragment shader
        U32 mTexelCount  = 0;
        S32 mXBearing = 0;      // all four in font design units (upem-scaled)
        S32 mYBearing = 0;
        S32 mWidth    = 0;
        S32 mHeight   = 0;
        bool drawable() const { return mTexelCount != 0; }
    };

    static constexpr U32 kBytesPerTexel = 8;            // RGBA16I = 4 * int16

    LLFontGpuGlyphCache();
    ~LLFontGpuGlyphCache();

    LLFontGpuGlyphCache(const LLFontGpuGlyphCache&)            = delete;
    LLFontGpuGlyphCache& operator=(const LLFontGpuGlyphCache&) = delete;

    // Bind the cache to a face. Builds an internal encode font scaled to the
    // face's upem so encoded coordinates land in font design units (the blob
    // format's preferred range — see hb-gpu.h). Re-callable; clears contents.
    void init(hb_face_t* face);

    // Encode (on cache miss) and return the glyph's location. Returns a
    // non-drawable GlyphLoc when the glyph has no outline or the cache is
    // uninitialized. The reference is stable until the next reset()/eviction.
    const GlyphLoc& getGlyph(U32 glyph_id);

    // Upload any pending glyph bytes and bind the GL_TEXTURE_BUFFER texture to
    // the active texture unit for sampling. Lazily creates the GL objects.
    // Returns false if GL texture-buffer support is unavailable.
    bool bindBufferTexture();

    // Drop GL objects (e.g. context teardown) but keep the CPU encode cache, so
    // a later bindBufferTexture() re-uploads without re-encoding.
    void destroyGL();

    // Clear all cached glyphs + the arena and bump the generation so dependent
    // vertex buffers rebuild. Invoked on overflow eviction and face reload.
    void reset();

    S32 getGeneration() const { return mGeneration; }
    U32 getGlyphCount() const { return static_cast<U32>(mCache.size()); }
    U32 getArenaTexels() const { return static_cast<U32>(mArena.size()) / kBytesPerTexel; }

    // Texel-capacity ceiling before reset()-eviction kicks in. Exposed mainly
    // for tests; production uses the default.
    void setMaxTexels(U32 max_texels) { mMaxTexels = max_texels; }

private:
    void     ensureEncoder();
    bool     ensureGLBuffer();
    GlyphLoc encodeGlyph(U32 glyph_id);

    // 256K texels = 2 MB. Generous for a single face's working set; eviction is
    // a full reset (see getGlyph), so this is a soft ceiling, not a hard quota.
    static constexpr U32 kDefaultMaxTexels = 1u << 18;

    hb_gpu_draw_t* mEncoder    = nullptr;
    hb_font_t*     mEncodeFont = nullptr;

    // CPU source of truth, uploaded incrementally to the GL buffer.
    std::vector<U8> mArena;
    U32 mUploadedBytes = 0;          // arena bytes already in the GL store
    U32 mMaxTexels     = kDefaultMaxTexels;

    boost::unordered_flat_map<U32, GlyphLoc> mCache;

    U32 mGLBuffer        = 0;        // buffer object holding the texel data
    U32 mGLTexture       = 0;        // texture viewing the buffer as RGBA16I
    U32 mGLCapacityBytes = 0;        // allocated GL store size

    S32        mGeneration;
    static S32 sNextGeneration;
};

#endif // LL_HAS_HB_GPU
#endif // LL_LLFONTGPUGLYPHCACHE_H
