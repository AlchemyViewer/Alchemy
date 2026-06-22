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

    // Bind the cache to a font. Builds an internal encode font from the source
    // font's FACE, scaled to the face's upem so encoded coordinates land in font
    // design units (the blob format's preferred range — see hb-gpu.h), and
    // COPIES the source font's variation-axis coordinates onto it. The variation
    // copy is essential for variable fonts: a bold/weight/optical instance
    // (e.g. fonts.xml `font_weight="700"`) lives in the source hb_font's var
    // coords, NOT in the bare face — without copying them the analytic path would
    // encode the DEFAULT master (regular weight) and "bold" variable faces would
    // render un-emboldened. Re-callable; clears contents.
    //
    // When `color` is true the cache encodes COLR(v1) color glyphs via the
    // hb-gpu paint encoder (blobs the paint-renderer fragment shader rasterizes
    // into premultiplied RGBA), using CPAL palette `palette`. Otherwise it
    // encodes monochrome outlines via the draw encoder. The two blob formats
    // are mutually exclusive per face — a face is either a color face or it
    // isn't — so one cache only ever holds one kind.
    void init(hb_font_t* src_font, bool color = false, unsigned palette = 0);

    // True when this cache holds paint (color) blobs — the renderer binds the
    // paint program + premultiplied blending for these, vs. the draw program +
    // straight-alpha coverage for monochrome caches.
    bool isColor() const { return mColor; }

    // Encode (on cache miss) and return the glyph's location. Returns a
    // non-drawable GlyphLoc when the glyph has no outline or the cache is
    // uninitialized. Use the result before the next getGlyph()/reset(): the
    // backing flat_map may rehash on insert, invalidating prior references (all
    // current callers consume it immediately within one loop iteration).
    const GlyphLoc& getGlyph(U32 glyph_id);

    // Upload any pending glyph bytes (from the shared arena) and bind the shared
    // GL_TEXTURE_BUFFER texture to the active texture unit for sampling. Lazily
    // creates the GL objects. Returns false if GL texture-buffer support is
    // unavailable. Static: the store is global, not per-cache.
    static bool bindBufferTexture();

    // Drop the shared GL objects (e.g. context teardown) but keep the CPU arena,
    // so a later bindBufferTexture() re-uploads without re-encoding. Static.
    static void destroyGL();

    // Clear the shared arena + bump the generation so dependent vertex buffers
    // (and every per-face map) rebuild. Invoked on overflow eviction and at
    // teardown. Static: resets the global store. Per-face glyph maps are dropped
    // lazily when each cache next notices the generation moved.
    static void reset();

    static S32 getGeneration() { return sGeneration; }
    U32 getGlyphCount() const { return static_cast<U32>(mCache.size()); }
    static U32 getArenaTexels() { return static_cast<U32>(sArena.size()) / kBytesPerTexel; }

    // Texel-capacity ceiling before reset()-eviction kicks in. Exposed mainly
    // for tests; production uses the default. Static: applies to the shared store.
    static void setMaxTexels(U32 max_texels) { sMaxTexels = max_texels; }

private:
    void        ensureEncoder();
    static bool ensureGLBuffer();
    GlyphLoc    encodeGlyph(U32 glyph_id);
    GlyphLoc    encodeDrawGlyph(U32 glyph_id);
    GlyphLoc    encodePaintGlyph(U32 glyph_id);

    // 256K texels = 2 MB. Generous for a single face's working set; eviction is
    // a full reset (see getGlyph), so this is a soft ceiling, not a hard quota.
    static constexpr U32 kDefaultMaxTexels = 1u << 18;

    // Exactly one encoder is live, selected by mColor at init(): the draw
    // encoder for monochrome outlines, the paint encoder for COLR color glyphs.
    hb_gpu_draw_t*  mEncoder      = nullptr;
    hb_gpu_paint_t* mPaintEncoder = nullptr;
    hb_font_t*      mEncodeFont   = nullptr;
    bool            mColor        = false;
    unsigned        mPalette      = 0;

    // GLOBAL shared texel store. Every per-face cache (mono + color, all faces)
    // appends its encoded blobs into ONE arena / GL buffer, so glyph_loc is a
    // global offset and a single buffer texture serves all glyph rendering. This
    // is what lets glyphs from any face batch together through gGL (no per-face
    // texel-buffer rebind). Eviction is a full reset of the shared arena, which
    // bumps sGeneration; each per-face cache compares mLastArenaGen and clears its
    // (now-stale) glyph_id->offset map. Main-thread only (font rendering is).
    static std::vector<U8> sArena;
    static U32 sUploadedBytes;       // arena bytes already in the GL store
    static U32 sMaxTexels;
    static U32 sGLBuffer;            // buffer object holding the texel data
    static U32 sGLTexture;           // texture viewing the buffer as RGBA16I
    static U32 sGLCapacityBytes;     // allocated GL store size
    static S32 sGeneration;          // bumped on every shared-arena reset

    // Per-face: the glyph_id -> global GlyphLoc map and the arena generation it
    // was built against (cleared when the shared arena resets out from under it).
    boost::unordered_flat_map<U32, GlyphLoc> mCache;
    S32 mLastArenaGen = 0;
};

#endif // LL_HAS_HB_GPU
#endif // LL_LLFONTGPUGLYPHCACHE_H
