/**
 * @file llfontgpuglyphcache_test.cpp
 * @brief Unit tests for LLFontGpuGlyphCache — the atlas-free (hb-gpu) glyph
 *        store: encode-once / size-independent caching, negative caching for
 *        outline-less glyphs, overflow eviction, and (headless) GL upload.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../test/lltut.h"

#include "../llfontgpuglyphcache.h"

#if LL_HAS_HB_GPU

#include "../llfontface.h"
#include "../llfontfreetype.h"   // LLFontManager, gFontManagerp
#include "../llfontregistry.h"   // EFontHinting

#include <hb.h>
#include <hb-ot.h>   // hb_ot_var_get_axis_count

#if LL_MESA_HEADLESS
#  include "llheadlessgl_fixture.h"
#endif

#include <cstdio>
#include <string>

namespace
{
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif
    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;

    // Plain outline TrueType face in the source tree; .ttf so HarfBuzz opens it
    // directly without a brotli/woff2-enabled build.
    constexpr const char* kOutlineFont = "IBMPlexMono-Regular.ttf";
    // COLRv1 vector color font — exercises the hb-gpu paint encode path.
    constexpr const char* kColorFont   = "Noto-COLRv1.ttf";
    // Variable font with a wght axis (.ttf, no woff2/brotli dependency) —
    // exercises encoding a non-default variation instance (bold).
    constexpr const char* kVarFont     = "IBMPlexSansVar-Roman.ttf";

    bool fileExists(const std::string& path)
    {
        if (FILE* f = std::fopen(path.c_str(), "rb"))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    // RAII for an hb_face_t (+ a default-instance hb_font_t) opened from the
    // test font; null/empty if absent. init() takes the font so callers can set
    // variation axes on it before encoding (see the variable-weight test).
    struct TestFace
    {
        hb_blob_t* blob = nullptr;
        hb_face_t* face = nullptr;
        hb_font_t* font = nullptr;

        explicit TestFace(const char* font_name = kOutlineFont)
        {
            // The texel store is now GLOBAL (shared static across all caches), so
            // it persists across test cases. Reset it (arena + default ceiling) at
            // the start of each test that builds a TestFace, so offset/eviction
            // assertions see a clean store regardless of prior tests.
            LLFontGpuGlyphCache::reset();
            LLFontGpuGlyphCache::setMaxTexels(1u << 18);

            const std::string path = std::string(kFontDir) + font_name;
            if (!fileExists(path))
            {
                return;
            }
            blob = hb_blob_create_from_file(path.c_str());
            if (hb_blob_get_length(blob) > 0)
            {
                face = hb_face_create(blob, 0);
                font = hb_font_create(face);
            }
        }
        ~TestFace()
        {
            if (font) hb_font_destroy(font);
            if (face) hb_face_destroy(face);
            if (blob) hb_blob_destroy(blob);
        }
        bool valid() const { return face != nullptr; }
    };

    // Resolve a codepoint to its FreeType/HarfBuzz glyph index on the face.
    U32 gidFor(hb_face_t* face, hb_codepoint_t cp)
    {
        hb_font_t* f = hb_font_create(face);
        hb_codepoint_t g = 0;
        hb_font_get_nominal_glyph(f, cp, &g);
        hb_font_destroy(f);
        return static_cast<U32>(g);
    }
}

namespace tut
{
    struct llfontgpuglyphcache_data
    {
    };

    typedef test_group<llfontgpuglyphcache_data> llfontgpuglyphcache_test;
    typedef llfontgpuglyphcache_test::object     llfontgpuglyphcache_object;
    tut::llfontgpuglyphcache_test llfontgpuglyphcache_testcase("LLFontGpuGlyphCache");

    // (1) Encode + cache a drawable glyph; a second lookup is a cache hit
    // (same location, no growth, no re-encode).
    template<> template<>
    void llfontgpuglyphcache_object::test<1>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }

        const U32 gid_A = gidFor(tf.face, (hb_codepoint_t)'A');
        ensure("cmap maps 'A'", gid_A != 0);

        LLFontGpuGlyphCache cache;
        cache.init(tf.font);

        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid_A);
        ensure("'A' is drawable", loc.drawable());
        ensure("'A' has texels", loc.mTexelCount > 0);
        ensure("first glyph lands at offset 0", loc.mTexelOffset == 0);
        ensure("'A' has non-zero extents", loc.mWidth != 0 && loc.mHeight != 0);
        ensure_equals("one glyph cached", cache.getGlyphCount(), 1U);

        const U32 texels_after_first = cache.getArenaTexels();
        const LLFontGpuGlyphCache::GlyphLoc loc2 = cache.getGlyph(gid_A);
        ensure_equals("cache hit: same offset", loc2.mTexelOffset, loc.mTexelOffset);
        ensure_equals("cache hit: same count",  loc2.mTexelCount,  loc.mTexelCount);
        ensure_equals("cache hit: no arena growth", cache.getArenaTexels(), texels_after_first);
        ensure_equals("cache hit: still one glyph", cache.getGlyphCount(), 1U);
    }

    // (2) An outline-less glyph (space) is cached as a negative result: the
    // second lookup returns the identical location and does not grow the arena.
    template<> template<>
    void llfontgpuglyphcache_object::test<2>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }

        const U32 gid_sp = gidFor(tf.face, (hb_codepoint_t)' ');
        ensure("cmap maps space", gid_sp != 0);

        LLFontGpuGlyphCache cache;
        cache.init(tf.font);

        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid_sp);
        const U32 texels_after_first = cache.getArenaTexels();

        const LLFontGpuGlyphCache::GlyphLoc loc2 = cache.getGlyph(gid_sp);
        ensure_equals("space cached: same offset", loc2.mTexelOffset, loc.mTexelOffset);
        ensure_equals("space cached: same count",  loc2.mTexelCount,  loc.mTexelCount);
        ensure_equals("space cached: no arena growth", cache.getArenaTexels(), texels_after_first);
        ensure_equals("space counts as one cache entry", cache.getGlyphCount(), 1U);
    }

    // (3) Overflow eviction: with the ceiling set to one glyph's size, adding a
    // second drawable glyph clears the cache, bumps the generation, and lands
    // the survivor back at offset 0.
    template<> template<>
    void llfontgpuglyphcache_object::test<3>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }

        const U32 gid_A = gidFor(tf.face, (hb_codepoint_t)'A');
        const U32 gid_B = gidFor(tf.face, (hb_codepoint_t)'B');
        ensure("cmap maps 'A' and 'B'", gid_A != 0 && gid_B != 0 && gid_A != gid_B);

        LLFontGpuGlyphCache cache;
        cache.init(tf.font);

        const LLFontGpuGlyphCache::GlyphLoc a = cache.getGlyph(gid_A);
        ensure("'A' drawable", a.drawable());

        // Ceiling = exactly 'A's footprint, so the next glyph tips us over.
        cache.setMaxTexels(a.mTexelCount);
        const S32 gen_before = cache.getGeneration();

        const LLFontGpuGlyphCache::GlyphLoc b = cache.getGlyph(gid_B);
        ensure("'B' drawable", b.drawable());
        ensure("eviction bumped generation", cache.getGeneration() > gen_before);
        ensure_equals("only the survivor remains", cache.getGlyphCount(), 1U);
        ensure_equals("survivor reset to offset 0", b.mTexelOffset, 0U);

        // 'A' must have been evicted — re-fetching it re-encodes (new lookup).
        ensure("'A' no longer present until re-fetched",
               cache.getGlyphCount() == 1U);
    }

    // (5) Face integration: a real LLFontFace (loaded through LLFontManager)
    // hands out a GPU glyph cache wired to its hb_face, and that cache encodes
    // the face's glyphs. Validates the lazy hb_font_get_face init path. No GL.
    template<> template<>
    void llfontgpuglyphcache_object::test<5>()
    {
        const std::string path = std::string(kFontDir) + kOutlineFont;
        if (!fileExists(path))
        {
            skip("outline test font not present in test data dir");
        }

        LLFontManager::initClass();
        {
            LLFontFaceKey key{ path, /*face_index=*/0, /*point_size=*/14.f,
                               /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
                               EFontHinting::DEFAULT, /*flags=*/0 };
            LLPointer<LLFontFace> face = gFontManagerp->getOrCreateFace(key);
            ensure("face loaded", face.notNull() && face->isValid());

            LLFontGpuGlyphCache* cache = face->getGpuGlyphCache();
            ensure("face hands out a GPU glyph cache", cache != nullptr);
            ensure("GPU cache is stable across calls", face->getGpuGlyphCache() == cache);

            hb_codepoint_t gid = 0;
            ensure("cmap maps 'A'",
                   hb_font_get_nominal_glyph(face->getHbFont(), (hb_codepoint_t)'A', &gid) && gid != 0);
            ensure("face cache encodes 'A'", cache->getGlyph((U32)gid).drawable());
        }
        LLFontManager::cleanupClass();
    }

    // (6) Color mode: a cache init'd with color=true encodes a COLRv1 glyph
    // through the hb-gpu paint encoder into the same RGBA16I arena. The paint
    // blob differs from a draw blob but the cache contract (drawable, texels,
    // offset 0, extents) is identical. No GL.
    template<> template<>
    void llfontgpuglyphcache_object::test<6>()
    {
        TestFace tf(kColorFont);
        if (!tf.valid())
        {
            skip("COLRv1 test font not present in test data dir");
        }

        // U+2764 HEAVY BLACK HEART — a COLR-painted glyph in Noto-COLRv1.
        const U32 gid = gidFor(tf.face, (hb_codepoint_t)0x2764);
        ensure("cmap maps U+2764", gid != 0);

        LLFontGpuGlyphCache cache;
        cache.init(tf.font, /*color=*/true, /*palette=*/0);
        ensure("cache reports color mode", cache.isColor());

        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid);
        ensure("color glyph is drawable", loc.drawable());
        ensure("color glyph has texels", loc.mTexelCount > 0);
        ensure("first glyph lands at offset 0", loc.mTexelOffset == 0);
        ensure("color glyph has non-zero extents", loc.mWidth != 0 && loc.mHeight != 0);
        ensure_equals("one glyph cached", cache.getGlyphCount(), 1U);

        // Second lookup is a cache hit (no re-encode, no growth).
        const U32 texels = cache.getArenaTexels();
        const LLFontGpuGlyphCache::GlyphLoc loc2 = cache.getGlyph(gid);
        ensure_equals("cache hit: same offset", loc2.mTexelOffset, loc.mTexelOffset);
        ensure_equals("cache hit: no growth", cache.getArenaTexels(), texels);
    }

    // (7) Variable-font weight axis: the encode font must honor the SOURCE
    // font's variation coords, so encoding a glyph from a font set to wght=700
    // (bold) yields a heavier outline than the face's default master. Regression
    // for the GPU path rendering variable "bold" faces (fonts.xml
    // font_weight="700") at regular weight — the encode font was built from the
    // bare face, silently dropping the weight axis.
    template<> template<>
    void llfontgpuglyphcache_object::test<7>()
    {
        TestFace tf(kVarFont);
        if (!tf.valid())
        {
            skip("variable test font not present in test data dir");
        }
        if (hb_ot_var_get_axis_count(tf.face) == 0)
        {
            skip("test font is not variable");
        }

        const U32 gid_I = gidFor(tf.face, (hb_codepoint_t)'I');
        ensure("cmap maps 'I'", gid_I != 0);

        // Regular instance: tf.font is the face default (no axes set), so the
        // cache copies an empty coord set and encodes the default master.
        LLFontGpuGlyphCache reg;
        reg.init(tf.font);
        const LLFontGpuGlyphCache::GlyphLoc loc_reg = reg.getGlyph(gid_I);
        ensure("regular 'I' drawable", loc_reg.drawable());

        // Bold instance: same face, wght axis pushed to 700.
        hb_font_t* bold_font = hb_font_create(tf.face);
        hb_font_set_variation(bold_font, HB_TAG('w', 'g', 'h', 't'), 700.f);
        LLFontGpuGlyphCache bold;
        bold.init(bold_font);
        const LLFontGpuGlyphCache::GlyphLoc loc_bold = bold.getGlyph(gid_I);
        hb_font_destroy(bold_font);
        ensure("bold 'I' drawable", loc_bold.drawable());

        // 'I' is a plain vertical stem in IBM Plex Sans; at wght=700 the stem is
        // thicker, so the design-unit bbox width is strictly larger. If the
        // encode font had ignored the source font's var coords (the bug), both
        // would encode the default master and these would be equal.
        ensure("bold 'I' stem wider than regular master",
               loc_bold.mWidth > loc_reg.mWidth);
    }


#if LL_MESA_HEADLESS
    inline ll_test::HeadlessGL& getSharedHeadlessGL()
    {
        static ll_test::HeadlessGL gl(/*needs_vbos=*/true, /*needs_imagegl=*/true,
                                      /*needs_llrender=*/true, /*needs_render=*/false);
        return gl;
    }

    // (4) GL upload path: after caching a glyph, binding the texture buffer
    // uploads pending bytes and binds the RGBA16I buffer texture without error.
    // Skips cleanly if this GL lacks texture-buffer support (e.g. some OSMesa).
    template<> template<>
    void llfontgpuglyphcache_object::test<4>()
    {
        TestFace tf;
        if (!tf.valid())
        {
            skip("outline test font not present in test data dir");
        }
        getSharedHeadlessGL();

        LLFontGpuGlyphCache cache;
        cache.init(tf.font);
        const U32 gid_A = gidFor(tf.face, (hb_codepoint_t)'A');
        const LLFontGpuGlyphCache::GlyphLoc loc = cache.getGlyph(gid_A);
        ensure("'A' drawable", loc.drawable());

        while (glGetError() != GL_NO_ERROR) { /* drain */ }

        if (!cache.bindBufferTexture())
        {
            skip("GL texture buffer unsupported in this context");
        }
        ensure_equals("no GL error after buffer upload + bind",
                      (int)glGetError(), (int)GL_NO_ERROR);

        // A second bind with no new glyphs is a no-op upload and still clean.
        ensure("second bind succeeds", cache.bindBufferTexture());
        ensure_equals("no GL error on idempotent bind",
                      (int)glGetError(), (int)GL_NO_ERROR);

        cache.destroyGL();
    }
#endif // LL_MESA_HEADLESS
}

#endif // LL_HAS_HB_GPU
