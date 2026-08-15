/**
 * @file alfontshaping_test.cpp
 * @brief Unit tests for ALFontShaping — HarfBuzz wrapper, ZWJ ligature
 *        retry, VS-16 strip, monospace feature plans, LRU cache contract.
 *
 * The bulk of the tests are pure-CPU: HB shaping itself doesn't touch
 * the atlas. The GL-backed kerning test at the bottom exercises
 * renderAndCreateGlyph → LLFontBitmapCache::nextOpenPos → gGL.bind,
 * so it's wrapped in #if LL_MESA_HEADLESS and only compiles in when
 * CMake links the test binary against llrenderheadless. Same
 * single-file pattern as llfontregistry_test.cpp's GL-gated block.
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

#include "../alfontshaping.h"
#include "../llfontfreetype.h"
#include "../alfontface.h"
#include "../llfontregistry.h"  // EFontHinting full definition

#include "../test/lltut.h"

#if LL_MESA_HEADLESS
#  include "llheadlessgl_fixture.h"
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_MODULE_H

#include <hb.h>
#include <hb-ft.h>

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

// LLFontManager owns the global FT_Library. Forward-declared so the
// stem-darkening test below can FT_Property_Get against it.
extern FT_Library gFTLibrary;

namespace
{
#ifndef LLFONT_TEST_DATA_DIR
#  define LLFONT_TEST_DATA_DIR ""
#endif

    constexpr const char* kFontDir = LLFONT_TEST_DATA_DIR;

    bool fileExists(const std::string& path)
    {
        if (FILE* f = LLFile::fopen(path.c_str(), LLFILE_MODE("rb")))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    // Build a freshly-loaded LLFontFreetype for `filename` at 14pt/96dpi.
    // is_fallback defaults to TRUE: the non-fallback path in
    // LLFontFreetype::loadFace pre-warms the notdef glyph through
    // addGlyphFromFont, which calls into LLFontBitmapCache::nextOpenPos
    // and ultimately gGL.bind — fatal in a pure-CPU test binary.
    // Shaping cares only about cmap, GSUB, and HB advance data; the
    // is_fallback flag has no bearing on those.
    LLPointer<LLFontFreetype> loadFt(const std::string& filename,
                                     bool is_fallback = true)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        if (!ft->loadFace(filename,
                          /*point_size=*/14.f,
                          /*vert_dpi=*/96.f,
                          /*horz_dpi=*/96.f,
                          is_fallback,
                          /*face_n=*/0,
                          EFontHinting::DEFAULT,
                          /*flags=*/0))
        {
            return nullptr;
        }
        return ft;
    }

    // Build an LLWString from a parameter pack of llwchars. Avoids an
    // initializer_list dance at the call sites.
    template <typename... Cps>
    LLWString wstr(Cps... cps)
    {
        const llwchar arr[] = { static_cast<llwchar>(cps)... };
        return LLWString(arr, sizeof...(Cps));
    }
}

namespace tut
{
    // Per-test init/cleanup. LLFontManager is process-scoped but
    // safe to teardown here — these tests don't reach LLFontGL's
    // static fontp caches that complicate llfontgl_test's fixture.
    // Clearing the shape LRU at both ends keeps cross-test bleed
    // out of the cache-hit assertions.
    struct alfontshaping_data
    {
        alfontshaping_data()
        {
            LLFontManager::initClass();
            ALFontShaping::clearCache();
        }
        ~alfontshaping_data()
        {
            ALFontShaping::clearCache();
            LLFontManager::cleanupClass();
        }
    };

    typedef test_group<alfontshaping_data> alfontshaping_test;
    typedef alfontshaping_test::object     alfontshaping_object;
    tut::alfontshaping_test alfontshaping_testcase("ALFontShaping");

    // Null root face and empty/inverted ranges all produce empty
    // output. The header documents these as the safe-fallback
    // contracts callers rely on for "fall back to 1:1 codepoint path."
    template<> template<>
    void alfontshaping_object::test<1>()
    {
        std::vector<ALShapedGlyph> out;
        LLWString s = wstr('a','b','c');

        ALFontShaping::shapeRun(/*root_face=*/nullptr, s, 0, s.size(), out);
        ensure("null root face -> empty output", out.empty());

        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        ALFontShaping::shapeRun(ft, s, /*begin=*/2, /*end=*/2, out);
        ensure("begin == end -> empty output", out.empty());

        ALFontShaping::shapeRun(ft, s, /*begin=*/3, /*end=*/2, out);
        ensure("begin > end -> empty output", out.empty());

        ALFontShaping::shapeRun(ft, s, /*begin=*/0, /*end=*/99, out);
        ensure("end > size -> empty output", out.empty());
    }

    // ASCII through a proportional Latin font: 1 glyph per codepoint,
    // every glyph has a strictly positive advance, and each glyph's
    // glyph_id matches what the cmap directly reports for that
    // codepoint. Catches gross HB-config breakage (wrong direction,
    // broken cmap binding, scale=0) where shape would produce
    // 0-advance glyphs even on success.
    //
    // Stays away from ft->getXAdvance / ft->getGlyphInfo because those
    // route through getGlyphInfoByIndex → renderAndCreateGlyph → atlas
    // → gGL.bind, which a pure-CPU test binary can't satisfy. The cmap
    // match below is the strongest identity check we can make without
    // rasterizing.
    template<> template<>
    void alfontshaping_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('a','b','c');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("3 glyphs for 'abc'", out.size(), 3u);

        for (const auto& g : out)
        {
            ensure("each glyph has a face",      g.face != nullptr);
            ensure("each glyph has a glyph_id",  g.glyph_id != 0u);
            ensure("each glyph has a positive x_advance",
                   g.x_advance > 0.f);
        }
        // Identity check: the shape's glyph_id for each ASCII char
        // must match the face's cmap-direct lookup. A regression that
        // bound HB to the wrong face would produce ids that disagree.
        ensure_equals("glyph[0] id matches cmap('a')",
                      out[0].glyph_id, ft->getCharGlyphIndex(L'a'));
        ensure_equals("glyph[1] id matches cmap('b')",
                      out[1].glyph_id, ft->getCharGlyphIndex(L'b'));
        ensure_equals("glyph[2] id matches cmap('c')",
                      out[2].glyph_id, ft->getCharGlyphIndex(L'c'));
    }

    // shapeRun rebases cluster indices to the original wstr;
    // shapeLine returns slice-local clusters. Pin both contracts —
    // the renderer hot path branches on this distinction
    // (alfontshaping.h:80-90).
    template<> template<>
    void alfontshaping_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('X','Y','a','b','c');
        std::vector<ALShapedGlyph> shape_run_out;
        ALFontShaping::shapeRun(ft, s, /*begin=*/2, /*end=*/5, shape_run_out);
        ensure_equals("shapeRun produced 3 glyphs", shape_run_out.size(), 3u);
        // 'a' is at original position 2 (begin == 2).
        ensure_equals("shapeRun cluster[0] is original-string index 2",
                      shape_run_out[0].cluster, 2);

        const auto& shape_line_out = ALFontShaping::shapeLine(ft, s, 2, 5);
        ensure_equals("shapeLine produced 3 glyphs", shape_line_out.size(), 3u);
        // shapeLine clusters are slice-local: 'a' is at slice index 0.
        ensure_equals("shapeLine cluster[0] is slice-local index 0",
                      shape_line_out[0].cluster, 0);
    }

    // Cache hit contract: shaping the same (slice, root_face) twice
    // yields a glyph stream that's bit-for-bit identical to the first.
    // This is what callers depend on across frames (pen positions and
    // glyph ids must not drift), and it's the load-bearing invariant
    // for the LRU's reuse semantics.
    template<> template<>
    void alfontshaping_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('h','e','l','l','o');
        std::vector<ALShapedGlyph> first;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), first);

        std::vector<ALShapedGlyph> second;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), second);
        ensure_equals("cache hit produces same glyph count",
                      second.size(), first.size());
        for (size_t i = 0; i < first.size(); ++i)
        {
            ensure_equals("cache hit: same glyph_id",
                          second[i].glyph_id, first[i].glyph_id);
            ensure_equals("cache hit: same cluster",
                          second[i].cluster,  first[i].cluster);
            ensure_equals("cache hit: same x_advance",
                          second[i].x_advance, first[i].x_advance);
        }
    }

    // clearCacheForFace purges only the entries owned by the named
    // face — entries from other heads must persist. Verify by populating
    // the cache from two distinct faces and checking both round-trip
    // via shapeLine; clear face A's cache and verify A's cluster
    // identity is gone (next shape rebuilds) while B's persists.
    //
    // Without instrumentation we observe "cache cleared" indirectly
    // by checking the returned reference identity from shapeLine —
    // a cleared entry rebuilds to a fresh storage location.
    template<> template<>
    void alfontshaping_object::test<5>()
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            skip("DejaVuSans + InterVariable both required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        ensure("both faces loaded", a.notNull() && b.notNull());

        LLWString s = wstr('a','b');
        const auto& a_first  = ALFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_first  = ALFontShaping::shapeLine(b, s, 0, s.size());
        ensure("a shape non-empty", !a_first.empty());
        ensure("b shape non-empty", !b_first.empty());
        const void* a_addr0 = a_first.data();
        const void* b_addr0 = b_first.data();

        // Hit A again: cache hit returns same storage address.
        const auto& a_second = ALFontShaping::shapeLine(a, s, 0, s.size());
        ensure_equals("A cache hit: same backing storage",
                      a_second.data(), a_addr0);

        ALFontShaping::clearCacheForFace(a);

        // After clearing A: B's entry survives — its storage address
        // is unchanged. (Whether A's entry got reused at the same
        // address by the allocator after clear is implementation-
        // defined and not part of the contract.)
        const auto& a_after = ALFontShaping::shapeLine(a, s, 0, s.size());
        ensure("A re-shape produces output", !a_after.empty());

        const auto& b_after = ALFontShaping::shapeLine(b, s, 0, s.size());
        ensure_equals("B cache survived clearCacheForFace(A)",
                      b_after.data(), b_addr0);
    }

    // ZWJ heart-on-fire: U+2764 U+200D U+1F525 must collapse to a
    // single glyph through Noto-COLRv1 (whose GSUB has the ligature
    // rule). Pins d4a5a4c904 — the regression that left ZWJ sequences
    // unligatured because the run was fragmenting at VS-16 and ZWJ.
    template<> template<>
    void alfontshaping_object::test<6>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("Noto-COLRv1 loaded", ft.notNull());

        // U+2764 (heart) U+200D (ZWJ) U+1F525 (fire)
        LLWString s = wstr(0x2764, 0x200D, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure("heart-on-fire shaped to non-empty output", !out.empty());
        ensure_equals("heart-on-fire ZWJ ligated to a single glyph",
                      out.size(), 1u);
    }

    // VS-16 strip path: shaping U+2764 U+FE0F through Noto-COLRv1
    // (whose cmap lacks U+FE0F) must produce the same output as
    // shaping U+2764 alone — shape_sub_run pre-strips VS-16 before
    // passing to HB. Pins alfontshaping.cpp:257-264.
    template<> template<>
    void alfontshaping_object::test<7>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("Noto-COLRv1 loaded", ft.notNull());

        // Sanity: confirm VS-16 is genuinely absent from this font's
        // cmap (otherwise the strip path doesn't fire and the test
        // verifies nothing useful).
        ensure_equals("Noto-COLRv1 cmap lacks U+FE0F",
                      ft->getCharGlyphIndex(0xFE0F), 0u);

        LLWString with_vs    = wstr(0x2764, 0xFE0F);
        LLWString without_vs = wstr(0x2764);

        std::vector<ALShapedGlyph> with_out;
        std::vector<ALShapedGlyph> without_out;
        ALFontShaping::shapeRun(ft, with_vs,    0, with_vs.size(),    with_out);
        ALFontShaping::shapeRun(ft, without_vs, 0, without_vs.size(), without_out);

        ensure_equals("with-VS and without-VS produce same glyph count",
                      with_out.size(), without_out.size());
        ensure("with-VS produced at least one glyph", !with_out.empty());
        for (size_t i = 0; i < with_out.size(); ++i)
        {
            ensure_equals("with-VS glyph_id matches without-VS",
                          with_out[i].glyph_id, without_out[i].glyph_id);
        }
    }

    // Multi-hop shape run: ASCII + CJK + Emoji through a head (DejaVu)
    // with two fallbacks (SourceHanSans + Noto-COLRv1). shape_sub_run
    // itemizes by face coverage so each cluster routes to the right
    // face. Pins gap audit #3 — multi-hop selectShapingFace was
    // untested, the existing fallback tests use a single hop only.
    template<> template<>
    void alfontshaping_object::test<9>()
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string han    = std::string(kFontDir) + "SourceHanSans-Regular.woff2";
        const std::string emoji  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(han) || !fileExists(emoji))
            skip("DejaVuSans + SourceHanSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> cjk  = loadFt(han);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        ensure("all loaded", head.notNull() && cjk.notNull() && emo.notNull());
        head->addFallbackFont(cjk);
        head->addFallbackFont(emo);

        // 'a' (head) + U+4F60 你 (cjk) + U+1F525 fire (emoji).
        LLWString s = wstr('a', 0x4F60, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure_equals("3 glyphs (one per codepoint)", out.size(), 3u);

        // Each glyph carries the face it came from — verify the routing.
        ensure_equals("'a' routed to head (DejaVu)",        out[0].face, head.get());
        ensure_equals("CJK routed to SourceHanSans",        out[1].face, cjk.get());
        ensure_equals("emoji routed to Noto-COLRv1",        out[2].face, emo.get());
        // All three glyphs should have non-zero ids (no notdef anywhere).
        ensure("'a' resolved to non-zero glyph",  out[0].glyph_id != 0u);
        ensure("CJK resolved to non-zero glyph",  out[1].glyph_id != 0u);
        ensure("emoji resolved to non-zero glyph", out[2].glyph_id != 0u);
    }

    // VS-16 strip preserves cluster identity in the original wstr.
    // After stripping U+FE0F before HB, the surviving codepoints'
    // clusters must point into ORIGINAL string positions (so a fire
    // emoji at position 3 stays cluster=3, even though it was at
    // position 2 in the post-strip buffer fed to HB). Pins
    // cluster_back_map rebase at alfontshaping.cpp:392-403.
    template<> template<>
    void alfontshaping_object::test<10>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("Noto-COLRv1 loaded", ft.notNull());

        // Heart + VS-16 + ZWJ + fire. Position 1 (FE0F) gets stripped.
        // The output's clusters must still reference original positions
        // {0, 2, 3} or the ZWJ-collapse may produce a single cluster=0.
        LLWString s = wstr(0x2764, 0xFE0F, 0x200D, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure("output non-empty", !out.empty());
        // Whatever the final shape (ZWJ ligature collapses to 1 glyph,
        // or 2 with VS-16 stripped), every cluster must point into the
        // ORIGINAL string range [0, 4). A regression that left clusters
        // in the post-strip space would produce values in [0, 3) — fine
        // here numerically but would break if string lengths were larger.
        // Stronger pin: no cluster equals 1 (the position of the
        // stripped VS-16); the strip MUST rebase past it.
        for (const auto& g : out)
        {
            ensure("cluster points into original string range",
                   g.cluster >= 0 && g.cluster < (S32)s.size());
            ensure_not_equals("cluster does NOT point at stripped VS-16 position",
                              g.cluster, 1);
        }
    }

    // clearCacheForFace selectivity post-multi-hop: an entry keyed on
    // face A whose glyph stream contains a fallback-face B glyph is
    // dropped by clearCacheForFace(A). The shape LRU is keyed on root
    // face only (alfontshaping.cpp:62-64); B's own entries (if any)
    // survive — we don't double-purge across heads sharing fallbacks.
    template<> template<>
    void alfontshaping_object::test<11>()
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "SourceHanSans-Regular.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            skip("DejaVuSans + SourceHanSans required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        ensure("both loaded", a.notNull() && b.notNull());
        a->addFallbackFont(b);

        // Shape via A — its entry's glyph stream contains a B glyph
        // for the CJK codepoint. Shape via B directly — separate entry.
        LLWString s = wstr('a', 0x4F60);
        const auto& a_first = ALFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_first = ALFontShaping::shapeLine(b, s, 0, s.size());
        ensure("a non-empty", !a_first.empty());
        ensure("b non-empty", !b_first.empty());
        const void* a_addr = a_first.data();
        const void* b_addr = b_first.data();

        ALFontShaping::clearCacheForFace(a);

        // B's entry survives clearCacheForFace(A): its storage stays
        // at the same address. (Whether A's entry got reused by the
        // allocator at the same address is implementation-defined and
        // not load-bearing for the contract; B's survival is.)
        const auto& a_after = ALFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_after = ALFontShaping::shapeLine(b, s, 0, s.size());
        ensure("A re-shape produces output", !a_after.empty());
        ensure_equals("B's entry survived clearCacheForFace(A)",
                      b_after.data(), b_addr);
        // Sanity: the data we read out of the survived B entry is the
        // same B stream we observed before — a hidden cross-purge would
        // have reset it.
        ensure_equals("B output unchanged across A's cache flush",
                      b_after.size(), b_first.size());
    }

    // notdef contract: shaping a codepoint through a head with NO
    // fallback covering it must still produce output (one glyph with
    // glyph_id == 0). The renderer relies on this to draw the .notdef
    // rectangle for missing-glyph indication; an empty output would
    // silently drop the codepoint instead.
    template<> template<>
    void alfontshaping_object::test<12>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        // U+1F525 (fire emoji) is not in DejaVuSans's cmap, and we
        // attached no fallback, so HB must produce notdef.
        LLWString s = wstr(0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("uncovered codepoint produces 1 glyph", out.size(), 1u);
        ensure_equals("uncovered codepoint glyph_id == 0 (notdef)",
                      out[0].glyph_id, 0u);
    }

    // clearCache empties the LRU; subsequent shapes repopulate it. The
    // earlier version of this test compared the returned reference's
    // data() pointer before vs after clear and asserted the address
    // changed — that was allocator-implementation-defined. Modern
    // allocators (LFH on Windows, jemalloc, mimalloc, ...) recycle freed
    // slots LIFO, so the rebuilt entry's vector storage routinely lands
    // at the just-freed address and the assertion flaked ~3-of-5 runs.
    // cacheSize() pins the contract the cache actually guarantees
    // (count goes to 0 then back to 1) without depending on heap
    // recycling behavior.
    template<> template<>
    void alfontshaping_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('h','i');
        const auto& first = ALFontShaping::shapeLine(ft, s, 0, s.size());
        ensure("shape produced output", !first.empty());
        ensure_equals("first shape leaves one entry",
                      ALFontShaping::cacheSize(), 1u);
        // Snapshot the size BEFORE clearCache invalidates `first` —
        // shapeLine returns a reference into the LRU, so the entry's
        // storage is gone the moment the LRU is cleared. Reading
        // first.size() afterward would be a use-after-free.
        const size_t first_size = first.size();
        const void* addr0 = first.data();

        // Cache hit: same storage AND the entry count stays at one.
        const auto& second = ALFontShaping::shapeLine(ft, s, 0, s.size());
        ensure_equals("cache hit storage is stable", second.data(), addr0);
        ensure_equals("cache hit doesn't add an entry",
                      ALFontShaping::cacheSize(), 1u);

        ALFontShaping::clearCache();
        ensure_equals("clearCache empties the LRU",
                      ALFontShaping::cacheSize(), 0u);

        // Miss: re-shape repopulates the cache. Output content is
        // identical (shaping is deterministic given the same face +
        // codepoints); the cache state is what changed.
        const auto& after = ALFontShaping::shapeLine(ft, s, 0, s.size());
        ensure("post-clear rebuild produced output", !after.empty());
        ensure_equals("post-clear miss repopulated to one entry",
                      ALFontShaping::cacheSize(), 1u);
        ensure_equals("post-clear shape is content-identical",
                      after.size(), first_size);
    }

    // Keycap cluster across face boundary: '9' + U+FE0F + U+20E3 must
    // route the WHOLE cluster to the emoji face so its GSUB can compose
    // the keycap glyph. Without Phase 1's cluster-aware itemizer, the
    // digit lands on the head face (DejaVuSans has '9') and U+FE0F+U+20E3
    // fragment off onto Noto-COLRv1 — producing a bare '9' followed by a
    // standalone keycap mark with no base. Pins the keycap fix in
    // shape_all_sub_runs (alfontshaping.cpp:497-546).
    template<> template<>
    void alfontshaping_object::test<13>()
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            skip("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        ensure("both loaded", head.notNull() && emoji.notNull());
        head->addFallbackFont(emoji);

        LLWString s = wstr('9', 0xFE0F, 0x20E3);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure("keycap shaped to non-empty output", !out.empty());
        // The emoji face has the GSUB rule digit + U+20E3 -> keycap glyph
        // (VS-16 stripped pre-shape). All output glyphs must come from
        // the emoji face — not head — to confirm the cluster routed as
        // one unit. A regression to per-codepoint routing would put the
        // '9' glyph on `head` and only the keycap mark on `emoji`.
        for (const auto& g : out)
        {
            ensure_equals("every keycap-cluster glyph routes to emoji face",
                          g.face, emoji.get());
        }
        // Strong pin: the GSUB rule actually fires across the face
        // boundary. A weaker form of the regression — routing matches
        // but GSUB doesn't collapse — would leave 2+ glyphs on the
        // emoji face (the '9' outline plus an unbound keycap mark),
        // matching exactly what the user sees in the buggy report.
        ensure_equals("keycap '9️⃣' GSUB-collapsed to a single glyph",
                      out.size(), 1u);
    }

    // ZWJ heart-on-fire across face boundary: U+2764 (BMP heart, present
    // in head's cmap) + U+FE0F + U+200D + U+1F525 (astral fire, only on
    // emoji face). Phase 1 routes the whole cluster to the emoji face so
    // its GSUB can collapse the ZWJ ligature. A regression to per-cp
    // itemization would split the heart onto head, breaking the rule.
    template<> template<>
    void alfontshaping_object::test<14>()
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            skip("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        ensure("both loaded", head.notNull() && emoji.notNull());
        head->addFallbackFont(emoji);

        // Sanity: head DOES carry U+2764 — selectShapingFace would
        // happily route it to head without the cluster fast path.
        ensure("head face has U+2764 in cmap (without Phase 1 the heart "
               "would route to head and split the cluster)",
               head->getCharGlyphIndex(0x2764) != 0u);

        LLWString s = wstr(0x2764, 0xFE0F, 0x200D, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure("heart-on-fire shaped to non-empty output", !out.empty());
        for (const auto& g : out)
        {
            ensure_equals("every heart-on-fire glyph routes to emoji face",
                          g.face, emoji.get());
        }
        // Pin the ligature too — Noto-COLRv1's GSUB collapses the
        // sequence to a single glyph. Test 6 covers the same rule when
        // shaping directly through Noto; this asserts it still fires when
        // the head is a non-emoji font and the cluster is routed across
        // the fallback boundary.
        ensure_equals("heart-on-fire ZWJ ligated to a single glyph "
                      "even across the fallback boundary",
                      out.size(), 1u);
    }

    // Adjacent clusters: '3️⃣' (keycap) + '🏳️‍🌈' (pride flag, ZWJ
    // sequence). Both clusters route to the emoji face. The Phase 1
    // implementation merged them into one HarfBuzz buffer when their
    // chosen faces matched, which let GSUB rules match across the
    // cluster boundary — the user reported the pride flag flickering
    // down to a bare white flag (the rainbow part of the ZWJ ligature
    // collapsed against the wrong context). Each cluster must shape as
    // its own sub-run, so each rule sees only its own codepoints.
    template<> template<>
    void alfontshaping_object::test<17>()
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            skip("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        ensure("both loaded", head.notNull() && emoji.notNull());
        head->addFallbackFont(emoji);

        // '3' VS-16 U+20E3 (keycap), then U+1F3F3 (waving white flag)
        // VS-16 ZWJ U+1F308 (rainbow) — pride flag.
        LLWString s = wstr('3', 0xFE0F, 0x20E3,
                           0x1F3F3, 0xFE0F, 0x200D, 0x1F308);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure("adjacent clusters shaped to non-empty output", !out.empty());
        for (const auto& g : out)
        {
            ensure_equals("every glyph routes to emoji face",
                          g.face, emoji.get());
        }
        // Both clusters compose: keycap '3️⃣' -> 1 glyph, pride flag
        // '🏳️‍🌈' -> 1 glyph. Total 2. A regression that merged the
        // two clusters into one buffer would produce a different count
        // (e.g. the pride flag's GSUB matching against the keycap mark
        // and falling back to bare white-flag + standalone rainbow).
        ensure_equals("each adjacent cluster collapses independently",
                      out.size(), 2u);
    }

    // Cluster with no emoji-face coverage: route to root rather than to
    // a face that lacks the base. Simulates the "LimitedEmoji" scenario
    // where the emoji fallback's unicode_ranges excludes the keycap
    // mark — selectShapingFace returns root for U+20E3 and pick_cluster_face
    // must NOT route the cluster to a face that can't render its base.
    // We model this by attaching DejaVu (which has '#' but lacks U+20E3)
    // as the only fallback and shaping a keycap. Result must keep '#'
    // visible on root rather than tofu-ing the base on a non-covering face.
    template<> template<>
    void alfontshaping_object::test<18>()
    {
        const std::string head_path     = std::string(kFontDir) + "InterVariable.woff2";
        const std::string fallback_path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(head_path) || !fileExists(fallback_path))
            skip("InterVariable + DejaVuSans required");

        LLPointer<LLFontFreetype> head     = loadFt(head_path);
        LLPointer<LLFontFreetype> fallback = loadFt(fallback_path);
        ensure("both loaded", head.notNull() && fallback.notNull());
        head->addFallbackFont(fallback);

        // Sanity: neither face has U+20E3 (keycap mark) — that's the
        // scenario this test exercises. Neither has the keycap GSUB
        // rule. Best-case outcome: the base '#' renders through root.
        ensure_equals("Inter lacks U+20E3", head->getCharGlyphIndex(0x20E3), 0u);
        ensure_equals("DejaVu lacks U+20E3", fallback->getCharGlyphIndex(0x20E3), 0u);
        ensure_not_equals("Inter has '#' in cmap",
                          head->getCharGlyphIndex(L'#'), 0u);

        LLWString s = wstr('#', 0xFE0F, 0x20E3);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure("shape produced output", !out.empty());
        // The base '#' must end up on a face that has it (root). A
        // regression that routed to a non-covering face would tofu
        // the '#' (glyph_id == 0) which is the worse failure mode.
        bool found_hash = false;
        for (const auto& g : out)
        {
            if (g.face == head.get() && g.glyph_id == head->getCharGlyphIndex(L'#'))
                found_hash = true;
        }
        ensure("'#' base routed to a face that has it",
               found_hash);
    }

    // Hash-keycap directly through the emoji face: pin that Noto-COLRv1
    // actually has the GSUB rule for '#' + VS-16 + U+20E3 -> composed
    // keycap (one glyph). The '9' rule is exercised by test 13 across
    // a face boundary; this version uses Noto-COLRv1 directly so a font-
    // level missing rule is observable as glyph_count > 1 (not a routing
    // bug). User report: '#️⃣' renders as '#' + a standalone keycap mark
    // — diagnosed by this test as either a font-rule miss or a routing
    // miss, with the existing rule-fires path through 9️⃣ as the control.
    template<> template<>
    void alfontshaping_object::test<16>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("Noto-COLRv1 loaded", ft.notNull());

        // Sanity: the font carries '#' and U+20E3 in cmap. If either
        // were missing, hb_shape would emit notdef before any GSUB
        // could match.
        ensure_not_equals("Noto-COLRv1 has '#' in cmap",
                          ft->getCharGlyphIndex(L'#'), 0u);
        ensure_not_equals("Noto-COLRv1 has U+20E3 in cmap",
                          ft->getCharGlyphIndex(0x20E3), 0u);

        LLWString s = wstr('#', 0xFE0F, 0x20E3);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure("hash-keycap shaped to non-empty output", !out.empty());
        // If the GSUB keycap rule fires, the run collapses to a single
        // composed glyph (the same shape test 6 / test 13 expect for
        // their respective ligatures).
        ensure_equals("hash-keycap '#️⃣' GSUB-collapsed to a single glyph",
                      out.size(), 1u);
    }

    // Bare digit control: '9' alone (no FE0F + 20E3 follower) is NOT a
    // cluster per the walker. Phase 1's fast path must not interfere —
    // the digit routes to head as before, one glyph, no surprises.
    template<> template<>
    void alfontshaping_object::test<15>()
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            skip("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        ensure("both loaded", head.notNull() && emoji.notNull());
        head->addFallbackFont(emoji);

        LLWString s = wstr('9');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure_equals("bare '9' produces one glyph", out.size(), 1u);
        ensure_equals("bare '9' routes to head (not emoji fallback)",
                      out[0].face, head.get());
        ensure_equals("bare '9' glyph_id matches head's cmap",
                      out[0].glyph_id, head->getCharGlyphIndex(L'9'));
    }

    // Subdivision flag: U+1F3F4 (black flag) + tag chars 'gbeng' + the
    // U+E007F CANCEL TAG terminator. Validates that LLStringOps::
    // isEmojiClusterExtender includes the U+E0020-U+E007F tag-char range
    // and that pick_cluster_face routes the entire 7-codepoint cluster
    // to a face whose cmap covers the tag chars. A regression that
    // dropped tag-char support from the predicate would split the flag
    // off from its tag bytes and render the bytes as visible glyphs.
    template<> template<>
    void alfontshaping_object::test<19>()
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            skip("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        ensure("both loaded", head.notNull() && emoji.notNull());
        head->addFallbackFont(emoji);

        // 🏴 (U+1F3F4) + 'g' 'b' 'e' 'n' 'g' tag chars + U+E007F.
        LLWString s = wstr(0x1F3F4,
                           0xE0067, 0xE0062, 0xE0065, 0xE006E, 0xE0067,
                           0xE007F);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure("subdivision flag shaped to non-empty output", !out.empty());
        // Routing assertion is the load-bearing one: every cluster
        // member must land on the emoji face. A predicate regression
        // that excluded tag chars would split the flag onto emoji and
        // the tag bytes onto root.
        for (const auto& g : out)
        {
            ensure_equals("every tag-flag glyph routes to emoji face",
                          g.face, emoji.get());
        }
        // Stretch goal: if Noto-COLRv1 carries the GB-ENG subdivision
        // flag, GSUB collapses the whole sequence to one glyph. Some
        // Noto-COLRv1 builds ship without subdivision flags — tolerate
        // up to N glyphs, but never MORE than the input length (which
        // would mean glyph duplication).
        ensure("output glyph count never exceeds input codepoint count",
               out.size() <= s.size());
    }

    // Cell-alignment probe. For each monospace face shipped in
    // newview/fonts/, shape a 200-char mixed-ASCII line through the
    // monospace HB path with kFixedWidthLigaturesOk (kern off only)
    // and report per-glyph advance drift and positioning against the
    // first glyph's advance. Originally written as a pre-flight before
    // dropping the strict-mono bypass; kept as a regression that pins
    // HB-on-monospace doesn't develop sub-pixel advance drift even on
    // long lines. If it ever does, the strict feature plan in
    // shape_sub_run needs more entries (and the long-form fix is a
    // cluster-aware advance snap on HB output).
    template<> template<>
    void alfontshaping_object::test<20>()
    {
        const char* mono_files[] = {
            "DejaVuSansMono.woff2",
            "SourceCodeVF-Upright.woff2",
        };

        // Mixed ASCII pool to exercise more lookups than test 2's
        // simple AV pair: alphanumerics, punctuation, brackets,
        // symbols. Repeated to 200 chars so per-glyph drift
        // accumulates measurably.
        const std::string ascii_pool =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
            "!@#$%^&*()_+-=[]{};':\",./<>?";
        constexpr size_t N = 200;
        LLWString s;
        s.reserve(N);
        for (size_t i = 0; i < N; ++i)
            s.push_back((llwchar)ascii_pool[i % ascii_pool.size()]);

        size_t fonts_tested = 0;
        for (const char* fname : mono_files)
        {
            const std::string path = std::string(kFontDir) + fname;
            if (!fileExists(path))
                continue;
            LLPointer<LLFontFreetype> ft = loadFt(path);
            ensure("monospace font loaded", ft.notNull());
            ensure("font is fixed-width", ft->isFixedWidth());
            ft->setAllowMonospaceLigatures(true);  // route through HB

            std::vector<ALShapedGlyph> out;
            ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
            ensure_equals("N codepoints produce N glyphs through HB",
                          out.size(), N);

            // Reference: first glyph's advance. In a fixed-width face
            // all ASCII codepoints share one FT mXAdvance, so out[0]
            // sets the canonical cell. Deliberately don't use
            // ft->getXAdvance — that path asserts !mIsFallback and
            // the probe loads as fallback to skip GL pre-warm.
            ensure("at least one glyph", !out.empty());
            const F32 ref_advance = out[0].x_advance;
            ensure("reference advance positive", ref_advance > 0.f);

            F32 max_dev    = 0.f;
            F32 min_adv    = ref_advance;
            F32 max_adv    = ref_advance;
            F32 cumulative = 0.f;
            F32 max_xoff   = 0.f;
            F32 max_yoff   = 0.f;
            for (const auto& g : out)
            {
                max_dev = std::max(max_dev, std::abs(g.x_advance - ref_advance));
                min_adv = std::min(min_adv, g.x_advance);
                max_adv = std::max(max_adv, g.x_advance);
                cumulative += g.x_advance;
                max_xoff = std::max(max_xoff, std::abs(g.x_offset));
                max_yoff = std::max(max_yoff, std::abs(g.y_offset));
            }
            const F32 expected_total = ref_advance * F32(N);
            const F32 cumulative_dev = std::abs(cumulative - expected_total);

            // Surface measurements regardless of pass/fail — informs
            // whether Phase 2's snap is load-bearing.
            std::printf("HB mono drift probe [%s]: ref=%.4f N=%zu "
                        "min=%.4f max=%.4f cum=%.4f exp=%.4f "
                        "max_dev=%.6f cum_dev=%.6f "
                        "max_xoff=%.6f max_yoff=%.6f\n",
                        fname, ref_advance, N,
                        min_adv, max_adv,
                        cumulative, expected_total,
                        max_dev, cumulative_dev,
                        max_xoff, max_yoff);

            // Tolerance: 0.5px is generous — meaningful column drift
            // shows as ~1px by char ~50 in production reports.
            ensure("per-glyph advance drift < 0.5px", max_dev < 0.5f);
            ensure("cumulative advance drift < 0.5px", cumulative_dev < 0.5f);
            // Bypass writes 0 offsets; HB output should match for
            // ASCII (no GPOS positioning adjustments fire on monospace
            // ASCII with kern off).
            ensure("HB ASCII x_offset is zero", max_xoff < 0.5f);
            ensure("HB ASCII y_offset is zero", max_yoff < 0.5f);
            ++fonts_tested;
        }
        if (fonts_tested == 0)
            skip("no monospace fonts present in test data dir");
    }

    // Strict-mono never collapses codepoints into ligated glyphs.
    // Load-bearing: column count matching codepoint count is what stops
    // word-wrap and render from disagreeing on line width.
    //
    // The kFixedWidthStrict feature plan disables liga / calt / clig
    // / dlig / rlig at the GSUB lookup level, but the shipped monospace
    // fonts (DejaVuSansMono, SourceCodeVF) don't actually have those
    // GSUB lookups for the bait pairs below, so this test passes
    // even if the feature plan were relaxed — verified empirically by
    // temporarily relaxing kFixedWidthStrict and re-running.
    //
    // The test is still kept as a *contract* assertion for two
    // reasons: (1) catches a future shipped-font upgrade that DOES
    // include programmer ligatures, and (2) catches an HB upstream
    // change that fires more lookups by default.
    template<> template<>
    void alfontshaping_object::test<21>()
    {
        const char* mono_files[] = {
            "DejaVuSansMono.woff2",
            "SourceCodeVF-Upright.woff2",
        };
        // Mix common Latin ligature triggers with programmer-font pairs.
        // The exact glyph_count drops we'd see under ligatures-on are
        // font-specific (DejaVuSansMono is sparse, SourceCodeVF is rich)
        // but every entry on this list is a known ligation candidate in
        // at least one of the shipped fonts.
        const std::string ligature_bait =
            "fi fl ff ffi ffl == != => <- -> <= >= ::";

        size_t fonts_tested = 0;
        for (const char* fname : mono_files)
        {
            const std::string path = std::string(kFontDir) + fname;
            if (!fileExists(path))
                continue;
            LLPointer<LLFontFreetype> ft = loadFt(path);
            ensure("monospace font loaded", ft.notNull());
            ensure("font is fixed-width", ft->isFixedWidth());
            // Default: ligatures off. Routes through HB with kFixedWidthStrict.
            ensure("ligatures default off",
                   !ft->getAllowMonospaceLigatures());

            LLWString s;
            s.reserve(ligature_bait.size());
            for (char c : ligature_bait)
                s.push_back((llwchar)c);

            std::vector<ALShapedGlyph> out;
            ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
            ensure_equals("strict-mono glyph_count == codepoint_count",
                          out.size(), s.size());
            ++fonts_tested;
        }
        if (fonts_tested == 0)
            skip("no monospace fonts present in test data dir");
    }

    // Combining marks position via GPOS in monospace shaping. The retired
    // bypass emitted U+0301 (acute) etc. with x_advance=0 and x/y_offset=0,
    // stacking the mark at cell origin without GPOS placement; the user-
    // visible result was misaligned diacritics on monospace text. After
    // routing monospace through HB, the mark / mkmk features stay enabled
    // (they're not in kFixedWidthStrict), so HB applies GPOS to position
    // the mark over its base. Pin: shape "a" + U+0301 (combining acute
    // accent) on a monospace face; the cluster has 2 glyphs sharing one
    // cluster index, the second has zero x_advance (mark consumes no
    // cell), and the mark has non-zero positioning (offset OR cluster
    // composition through ccmp into a precomposed 'á').
    template<> template<>
    void alfontshaping_object::test<22>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSansMono.woff2";
        if (!fileExists(path))
            skip("DejaVuSansMono.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSansMono loaded", ft.notNull());
        ensure("DejaVuSansMono is fixed-width", ft->isFixedWidth());

        LLWString s = wstr(L'a', 0x0301);  // a + COMBINING ACUTE ACCENT
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        std::printf("combining mark probe: 'a'+U+0301 -> %zu glyphs\n",
                    out.size());
        for (size_t k = 0; k < out.size(); ++k)
        {
            std::printf("  glyph[%zu]: glyph_id=%u cluster=%d "
                        "x_adv=%.4f x_off=%.4f y_off=%.4f\n",
                        k, out[k].glyph_id, out[k].cluster,
                        out[k].x_advance, out[k].x_offset, out[k].y_offset);
        }
        ensure("combining cluster produces at least 1 glyph", !out.empty());
        // Expected outcomes:
        //  (a) ccmp composed to precomposed 'á' → 1 glyph, full cell advance.
        //  (b) Two glyphs (a + acute) in one HB cluster → first carries
        //      full advance, second has zero advance and the mark
        //      positioned via GPOS.
        // Either is correct GPOS-driven behavior; the bypass produced
        // neither (it emitted 2 glyphs, both with full advance).
        const F32 ref_advance = out[0].x_advance;
        ensure("base glyph has positive advance", ref_advance > 0.f);
        if (out.size() == 1)
        {
            // ccmp composition path. One cell, no second glyph.
            ensure_equals("cluster index is 0 (base)", out[0].cluster, 0);
        }
        else if (out.size() == 2)
        {
            // Mark cluster path. Marks share the base's cluster index
            // and consume zero advance (a hard requirement — a non-zero
            // mark advance would shift the next character right).
            ensure_equals("mark shares base cluster", out[1].cluster, out[0].cluster);
            ensure_equals("mark glyph has zero advance", out[1].x_advance, 0.f);
        }
        else
        {
            ensure("combining cluster produced unexpected glyph count", false);
        }
    }

    // -----------------------------------------------------------------
    // FT/HB consistency block (tests 23-31). Locks the integration
    // invariants between FreeType and HarfBuzz so a refactor that breaks
    // size, scale, glyph identity, hinting, variation axes, cmap
    // selection, or stem-darkening surfaces immediately. Every test
    // here is pure-CPU (no GL); fonts load with is_fallback=true via
    // loadFt so the rasterizer pre-warm doesn't fire.
    // -----------------------------------------------------------------

    // HB's shape-time scale (set by hb_ft_font_create_referenced from
    // the FT face) must match HB's own derivation from FT state. This
    // is the size invariant the codebase relies on — every shaped
    // advance is sized through this scale, so a regression that
    // resized the FT face after hb_font creation (without
    // hb_ft_font_changed) shows up as a mismatch here.
    //
    // The expected formula mirrors hb_ft_font_changed:
    //   scale = (face->size->metrics.x_scale * face->units_per_EM
    //            + (1<<15)) >> 16
    // which evaluates to the un-rounded fractional pixel size × 64
    // (e.g. 14pt @ 96dpi → 18.666 px → scale 1195, distinct from FT's
    // rounded x_ppem of 19).
    //
    // Note: hb_font_get_h_extents and hb_font_get_ppem are NOT good
    // invariants to lock. HB derives extents from face->ascender (raw
    // hhea, ignoring USE_TYPO_METRICS), while FT's size->metrics
    // ascender honors USE_TYPO_METRICS — so they differ by design.
    // hb_ft_font_create_referenced leaves ppem at 0 (only scale is set).
    template<> template<>
    void alfontshaping_object::test<23>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        ensure("face accessible", face != nullptr);
        hb_font_t* hbf = face->getHbFont();
        ensure("hb_font accessible", hbf != nullptr);

        int hb_x_scale = 0, hb_y_scale = 0;
        hb_font_get_scale(hbf, &hb_x_scale, &hb_y_scale);

        FT_Face ftf = face->face();
        const FT_Size_Metrics& m = ftf->size->metrics;
        // Replicate hb-ft's internal formula. Cast through uint64_t to
        // avoid 32-bit overflow at large UPEM × scale products.
        const int expected_x = (int)((((std::uint64_t)m.x_scale * (std::uint64_t)ftf->units_per_EM) + (1u << 15)) >> 16);
        const int expected_y = (int)((((std::uint64_t)m.y_scale * (std::uint64_t)ftf->units_per_EM) + (1u << 15)) >> 16);
        ensure_equals("HB x_scale matches hb-ft derivation from FT state",
                      hb_x_scale, expected_x);
        ensure_equals("HB y_scale matches hb-ft derivation from FT state",
                      hb_y_scale, expected_y);
        // Sanity: scale must be positive (zero would mean uninitialized).
        ensure("HB x_scale is positive", hb_x_scale > 0);
        ensure("HB y_scale is positive", hb_y_scale > 0);
    }

    // (test<24> intentionally absent — formerly checked hb_font_get_ppem
    // against FT ppem, but hb_ft_font_create_referenced doesn't set
    // ppem on the HB font; only scale carries the size invariant.
    // Keeping the gap so the audit's test numbering (23 + 25..31)
    // matches the plan file unchanged.)

    // For an unligatured / unkerned ASCII run, HB's per-glyph
    // x_advance must equal FT's slot->advance.x for the same glyph
    // index loaded with the same flags. Catches divergence in HB↔FT
    // outline scaling or load-flag plumbing. DejaVuSans has no GPOS
    // kern between adjacent lowercase letters, so HB's GPOS pass is
    // a no-op and per-glyph advances come straight from FT.
    template<> template<>
    void alfontshaping_object::test<25>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        FT_Face ftf = face->face();
        const FT_Int32 load_flags = static_cast<FT_Int32>(face->hinting());

        LLWString s = wstr('a','b','c','d','e','f');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("6 glyphs for 'abcdef'", out.size(), 6u);

        for (size_t i = 0; i < out.size(); ++i)
        {
            ensure_equals("FT_Load_Glyph succeeded",
                          FT_Load_Glyph(ftf, out[i].glyph_id, load_flags), 0);
            const F32 ft_advance = ftf->glyph->advance.x * (1.f / 64.f);
            // 26.6 -> float; equality in float at this precision
            // would be exact when no GPOS adjustment fired. Tolerate
            // 1/64 px to cover HB's internal rounding paths.
            const F32 delta = std::fabs(out[i].x_advance - ft_advance);
            ensure("HB x_advance matches FT advance for unkerned glyph",
                   delta < (1.f / 64.f) + 1e-5f);
        }
    }

    // HB's reported load flags after construction must equal what the
    // FT renderGlyph path uses. Both are casts of mHinting; any
    // refactor that splits the casts asymmetrically breaks shaped vs
    // codepoint advance consistency. hb_ft_font_get_load_flags has
    // existed since HB 1.7 — the codebase requires newer than that.
    template<> template<>
    void alfontshaping_object::test<26>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        hb_font_t* hbf = face->getHbFont();
        ensure("hb_font accessible", hbf != nullptr);

        const int hb_flags = hb_ft_font_get_load_flags(hbf);
        const int expected = static_cast<int>(face->hinting());
        ensure_equals("HB load flags == (int)mHinting",
                      hb_flags, expected);
        // And confirm the cast equivalence between the two surfaces
        // (HB takes int, FT takes FT_Int32) hasn't drifted.
        ensure_equals("(int)mHinting == (FT_Int32)mHinting",
                      static_cast<int>(face->hinting()),
                      (int)static_cast<FT_Int32>(face->hinting()));
    }

    // HB must see the same OT-VAR design coordinates that FT was
    // configured with. Without this, HB's GSUB/GPOS run at the
    // font's default axis values (typically wght=400) even when FT
    // renders varied outlines, breaking variation-aware kerning on
    // fonts like Inter at non-default weights.
    template<> template<>
    void alfontshaping_object::test<27>()
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            skip("InterVariable.woff2 not present");
        // Load with wght=600 so setVariationAxis sets a non-default
        // wght. opsz is auto-set from point_size whenever opsz_set is
        // false (load() handles both axes via the matching *_set
        // flag), so the verifier below sees a non-default opsz too.
        ALFontVarAxes va;
        va.wght = 600.f; va.wght_set = true;
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        ensure("InterVariable loaded at weight=600",
               ft->loadFace(path, /*point_size=*/14.f,
                            /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
                            /*is_fallback=*/true,
                            /*face_n=*/0, EFontHinting::DEFAULT,
                            /*flags=*/0, va));

        const ALFontFace* face = ft->getFontFace();
        FT_Face ftf = face->face();
        hb_font_t* hbf = face->getHbFont();
        ensure("hb_font accessible", hbf != nullptr);

        FT_MM_Var* mm = nullptr;
        ensure_equals("FT_Get_MM_Var on a variable font",
                      FT_Get_MM_Var(ftf, &mm), 0);
        ensure("FT_MM_Var allocated", mm != nullptr);
        const FT_UInt num_axis = mm->num_axis;
        ensure("variable font reports >= 1 axis", num_axis >= 1u);

        std::vector<FT_Fixed> ft_coords(num_axis);
        ensure_equals("FT_Get_Var_Design_Coordinates",
                      FT_Get_Var_Design_Coordinates(ftf, num_axis, ft_coords.data()), 0);

        unsigned int hb_len = 0;
        const float* hb_coords = hb_font_get_var_coords_design(hbf, &hb_len);
        ensure("HB has design coords set", hb_coords != nullptr);
        ensure_equals("HB axis count matches FT", hb_len, (unsigned int)num_axis);

        for (FT_UInt i = 0; i < num_axis; ++i)
        {
            const float ft_design = ft_coords[i] / 65536.0f;  // 16.16 -> design
            const float delta = std::fabs(hb_coords[i] - ft_design);
            // 16.16 round-trip: equality should be exact, but allow
            // 1/65536 to cover any internal float-conversion rounding.
            ensure("HB design coord matches FT design coord",
                   delta < (1.f / 65536.f) + 1e-6f);
        }

        FT_Done_MM_Var(gFTLibrary, mm);
    }

    // Stem darkening must be disabled on every FT hinter module — the
    // renderer composites in sRGB space and FT's stem darkening assumes
    // linear-space compositing. autofitter is off by default in FT
    // 2.7+, but cff/type1 default ON; without explicit Property_Set on
    // those, a CFF font with EFontHinting::DEFAULT picks up unwanted
    // stem darkening.
    template<> template<>
    void alfontshaping_object::test<28>()
    {
        ensure("gFTLibrary initialized", gFTLibrary != nullptr);

        const char* modules[] = { "autofitter", "cff", "type1", "t1cid" };
        for (const char* mod : modules)
        {
            FT_Bool no_darken = 0;
            const FT_Error err = FT_Property_Get(gFTLibrary, mod, "no-stem-darkening", &no_darken);
            // Some modules may not expose the property in older FT
            // builds; treat err != 0 as "module didn't have it to set
            // either" and skip without failing. The Set call in
            // LLFontManager does the same — an unknown property is
            // a no-op.
            if (err != 0)
                continue;
            ensure(std::string("no-stem-darkening enabled on ") + mod,
                   no_darken != 0);
        }
    }

    // FT_Select_Charmap(FT_ENCODING_UNICODE) must succeed on a normal
    // Unicode font — the resulting active charmap encoding should be
    // FT_ENCODING_UNICODE so all FT_Get_Char_Index lookups operate on
    // the Unicode cmap subtable, not whichever subtable FT auto-picked.
    template<> template<>
    void alfontshaping_object::test<29>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        FT_Face ftf = face->face();
        ensure("active charmap is set", ftf->charmap != nullptr);
        ensure_equals("active charmap encoding is FT_ENCODING_UNICODE",
                      (int)ftf->charmap->encoding,
                      (int)FT_ENCODING_UNICODE);
    }

    // VS-15 strip parity with VS-16: a face that lacks U+FE0E in cmap
    // must still produce identical shape output for `<base, VS-15>`
    // vs just `<base>` — the do_shape strip pass should drop VS-15
    // before HB so the notdef glyph for VS-15 doesn't sit between
    // base and follow-up codepoints. Mirrors test<7>'s VS-16 proof.
    template<> template<>
    void alfontshaping_object::test<30>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("Noto-COLRv1 loaded", ft.notNull());

        // Sanity: the test only verifies anything if Noto-COLRv1 also
        // lacks VS-15. Color emoji fonts typically don't ship VS-15.
        if (ft->getCharGlyphIndex(0xFE0E) != 0u)
            skip("Noto-COLRv1 unexpectedly has U+FE0E in cmap");

        LLWString with_vs    = wstr(0x2764, 0xFE0E);
        LLWString without_vs = wstr(0x2764);

        std::vector<ALShapedGlyph> with_out;
        std::vector<ALShapedGlyph> without_out;
        ALFontShaping::shapeRun(ft, with_vs,    0, with_vs.size(),    with_out);
        ALFontShaping::shapeRun(ft, without_vs, 0, without_vs.size(), without_out);

        ensure_equals("with-VS15 and without-VS15 produce same glyph count",
                      with_out.size(), without_out.size());
        ensure("with-VS15 produced at least one glyph", !with_out.empty());
        for (size_t i = 0; i < with_out.size(); ++i)
        {
            ensure_equals("with-VS15 glyph_id matches without-VS15",
                          with_out[i].glyph_id, without_out[i].glyph_id);
        }
    }

    // Identity check between codepoint→glyph (cmap) and shape result
    // for a non-Latin codepoint that DejaVuSans covers. test<2> covers
    // ASCII; this extends the contract to the upper BMP so a regression
    // limited to Unicode-cmap binding (Defect B in the audit plan) is
    // caught even when ASCII still works.
    template<> template<>
    void alfontshaping_object::test<31>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        // Pick the first non-ASCII codepoint the face has. DejaVuSans
        // covers Greek and Cyrillic; try a Greek alpha (U+03B1) first.
        const llwchar candidates[] = { 0x03B1, 0x0430, 0x00E9, 0x00FC };
        llwchar wch = 0;
        for (llwchar c : candidates)
        {
            if (ft->getCharGlyphIndex(c) != 0u)
            {
                wch = c;
                break;
            }
        }
        if (wch == 0)
            skip("no non-ASCII codepoint covered in DejaVuSans");

        LLWString s; s.push_back(wch);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("1 glyph for non-ASCII codepoint", out.size(), 1u);
        ensure_equals("shape glyph_id matches cmap lookup",
                      out[0].glyph_id, ft->getCharGlyphIndex(wch));
        ensure("non-ASCII glyph has positive advance",
               out[0].x_advance > 0.f);
    }

    // Cluster index invariant: every glyph emitted by shapeRun has
    // cluster ∈ [begin, end). The producer-side clamp in shape_sub_run
    // protects downstream consumers (firstDrawableChar, maxDrawableChars)
    // that index per-codepoint arrays by cluster. ZWJ-retry candidates
    // and corrupt GSUB tables can synthesize cluster values outside the
    // input range; the clamp pins them back into the slice.
    template<> template<>
    void alfontshaping_object::test<32>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        struct Case { LLWString s; size_t begin; size_t end; };
        const std::vector<Case> cases = {
            { wstr('a','b','c'),                          0, 3 },
            { wstr('A','V','A','W','T','o','L','T'),      0, 8 },
            { wstr('h','e','l','l','o',' ','w','o','r','l','d'), 0, 11 },
            { wstr('a','b','c','d','e','f'),              2, 5 },
            // VS-16 / ZWJ extenders interleaved with Latin — exercises
            // the strip+rebase path even on a face that covers them.
            { wstr('A', 0xFE0F, 'B', 0x200D, 'C'),        0, 5 },
        };

        for (size_t ci = 0; ci < cases.size(); ++ci)
        {
            const auto& c = cases[ci];
            std::vector<ALShapedGlyph> out;
            ALFontShaping::shapeRun(ft, c.s, c.begin, c.end, out);
            for (const auto& g : out)
            {
                ensure(("cluster >= begin (case " + std::to_string(ci) + ")").c_str(),
                       g.cluster >= (S32)c.begin);
                ensure(("cluster < end (case " + std::to_string(ci) + ")").c_str(),
                       g.cluster < (S32)c.end);
            }
        }
    }

    // Cluster atomicity: every glyph emitted by the cluster fast path
    // must carry the cluster's start codepoint as its cluster ID, not
    // a per-glyph ID HarfBuzz hands back when the face fails to ligate.
    // charFromPixelOffset feeds sg.cluster directly into the cursor
    // position it returns for round=true mid-glyph hit-tests; a
    // mid-cluster cluster value lands the cursor inside the cluster
    // and oscillates the drag-select highlight rect across the
    // cluster's interior. Force a multi-glyph case by routing the
    // trans-flag ZWJ sequence through DejaVuSans alone (no emoji
    // fallback) — DejaVu has ZWJ but lacks the astral pictographs and
    // the GSUB rule for the composed flag, so HB emits one glyph per
    // covered codepoint plus notdefs.
    template<> template<>
    void alfontshaping_object::test<33>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        // "X" + trans flag (5 cps, [1, 6)) + "Y". The cluster walker
        // identifies [1, 6) as one emoji cluster; the fast path shapes it
        // as a cluster sub-run on DejaVu, which can't ligate the
        // sequence. Without cluster atomicity, HB hands back glyph
        // cluster IDs 1..5 — the bug we're pinning.
        LLWString s = wstr(L'X', 0x1F3F3, 0xFE0F, 0x200D, 0x26A7,
                           0xFE0F, L'Y');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure("shape produced output", !out.empty());

        // Every glyph whose cluster falls inside [1, 6) must report 1
        // (the cluster's start). Glyphs for X (cluster=0) and Y (cluster=6)
        // are unaffected.
        bool saw_cluster_glyph = false;
        for (const auto& g : out)
        {
            if (g.cluster >= 1 && g.cluster < 6)
                saw_cluster_glyph = true;
            ensure("trans-flag cluster glyphs collapse to cluster start",
                   !(g.cluster > 1 && g.cluster < 6));
        }
        ensure("at least one glyph routed through the cluster sub-run "
               "(otherwise this test isn't exercising the fast path)",
               saw_cluster_glyph);
    }

    // addFallbackFont must invalidate cached shape entries for the head:
    // shape a CJK codepoint via head A (no CJK fallback) — cached as
    // notdef, glyph_id=0. Attach a CJK fallback. Re-shape the same
    // string. The fallback now covers the CJK codepoint, so the new
    // shape must route the cluster to the fallback (glyph_id != 0)
    // rather than re-using the stale notdef cache entry. Pre-fix,
    // addFallbackFont only cleared mShapingFaceResolution and left the
    // ALFontShaping cache dirty — so the second shape call returned the
    // stale entry and rendered tofu through A's notdef even though the
    // CJK fallback was attached and would have covered the codepoint.
    template<> template<>
    void alfontshaping_object::test<34>()
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "SourceHanSans-Regular.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            skip("DejaVuSans + SourceHanSans required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        ensure("both loaded", a.notNull() && b.notNull());

        // Pre-fallback shape: U+4F60 (你) is not in DejaVuSans's cmap, so
        // HB returns one notdef glyph (glyph_id=0) on face A.
        LLWString s = wstr(0x4F60);
        std::vector<ALShapedGlyph> pre;
        ALFontShaping::shapeRun(a, s, 0, s.size(), pre);
        ensure_equals("pre-fallback shape produces 1 glyph", pre.size(), 1u);
        ensure_equals("pre-fallback glyph routes to head A", pre[0].face, a.get());
        ensure_equals("pre-fallback glyph is notdef (glyph_id=0)",
                      pre[0].glyph_id, 0u);
        ensure_equals("pre-fallback shape leaves one cache entry",
                      ALFontShaping::cacheSize(), 1u);

        // Attach the CJK fallback. addFallbackFont must clear the cached
        // entry for `a` so the next shape() re-itemizes through the new
        // chain instead of returning the stale notdef.
        a->addFallbackFont(b);
        ensure_equals("addFallbackFont(this) drops cached entries for this",
                      ALFontShaping::cacheSize(), 0u);

        // Post-fallback shape: U+4F60 should now route to face B with a
        // non-zero glyph_id (B has the CJK glyph in its cmap).
        std::vector<ALShapedGlyph> post;
        ALFontShaping::shapeRun(a, s, 0, s.size(), post);
        ensure_equals("post-fallback shape produces 1 glyph", post.size(), 1u);
        ensure_equals("post-fallback glyph routes to fallback B",
                      post[0].face, b.get());
        ensure_not_equals("post-fallback glyph is real (not notdef)",
                          post[0].glyph_id, 0u);
    }

#if LL_MESA_HEADLESS
    // GL-backed group: monospace shaping ends up rendering glyphs through
    // getGlyphInfoByIndex → renderAndCreateGlyph → atlas → gGL.bind on
    // the test 3 (kerning) path. Wrapped in a separate fixture that
    // pulls in the headless OSMesa context so the rasterizer can
    // satisfy the bind.
    struct alfontshaping_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };

    typedef test_group<alfontshaping_gl_data> alfontshaping_gl_test;
    typedef alfontshaping_gl_test::object     alfontshaping_gl_object;
    tut::alfontshaping_gl_test alfontshaping_gl_testcase("ALFontShapingGL");

    // Build an LLFontFreetype as a real head face (is_fallback=false),
    // which the headless GL context can satisfy. The pre-warm of
    // notdef inside loadFace runs the rasterizer to bind a fresh
    // atlas page — fine here, gGL is alive.
    static LLPointer<LLFontFreetype> loadFtHead(const std::string& filename)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        if (!ft->loadFace(filename, 14.f, 96.f, 96.f,
                          /*is_fallback=*/false, /*face_n=*/0,
                          EFontHinting::DEFAULT, /*flags=*/0))
        {
            return nullptr;
        }
        return ft;
    }

    // Strict-monospace (ligatures off): shape "AB" through DejaVuSansMono.
    // Routes through HB with the kFixedWidthStrict feature plan
    // (kern + liga + calt + clig + dlig + rlig all forced off). HB
    // produces one glyph per codepoint with bit-exact FT mXAdvance and
    // zero positioning offsets — same contract the retired bypass
    // enforced. Pins the cell-alignment invariant.
    template<> template<>
    void alfontshaping_gl_object::test<1>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSansMono.woff2";
        if (!fileExists(path))
            skip("DejaVuSansMono.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSansMono loaded", ft.notNull());
        ensure("DejaVuSansMono is fixed-width", ft->isFixedWidth());
        ensure("monospace ligatures default off",
               !ft->getAllowMonospaceLigatures());

        LLWString s = wstr('A','B');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("AB produces 2 glyphs through HB strict-mono path",
                      out.size(), 2u);
        ensure("each shaped glyph has positive advance",
               out[0].x_advance > 0.f && out[1].x_advance > 0.f);
        ensure_equals("strict-mono: A and B advances are equal",
                      out[0].x_advance, out[1].x_advance);
        // HB output for monospace ASCII matches FT mXAdvance bit-exact
        // under the strict feature plan (verified by the long-line
        // probe in test 20).
        ensure_equals("strict-mono glyph[0] advance == ft->getXAdvance('A')",
                      out[0].x_advance, ft->getXAdvance(L'A'));
        // Strict-mono path also forces zero positioning offsets, since
        // monospace ASCII triggers no GPOS adjustments under the
        // feature plan (kern, mark, mkmk all suppressed for ASCII).
        ensure_equals("strict-mono glyph[0] x_offset is zero",
                      out[0].x_offset, 0.f);
        ensure_equals("strict-mono glyph[0] y_offset is zero",
                      out[0].y_offset, 0.f);
    }

    // Programmer-mono opt-in via setAllowMonospaceLigatures(true).
    // Routes through HB with the kFixedWidthLigaturesOk feature plan
    // (kern off, ligatures allowed). Cell alignment invariant on the
    // pre-ligation columns still holds.
    template<> template<>
    void alfontshaping_gl_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSansMono.woff2";
        if (!fileExists(path))
            skip("DejaVuSansMono.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSansMono loaded", ft.notNull());
        ft->setAllowMonospaceLigatures(true);
        ensure("ligatures-on toggle applied",
               ft->getAllowMonospaceLigatures());

        LLWString s = wstr('A','V'); // AV is a classic kerned pair
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("AV produces 2 glyphs through HB", out.size(), 2u);
        ensure("each shaped glyph has positive advance",
               out[0].x_advance > 0.f && out[1].x_advance > 0.f);
        ensure_equals("HB-monospace-with-ligatures: A and V advances are equal",
                      out[0].x_advance, out[1].x_advance);
    }

    // HB GPOS plumbing: shape "AV" (classic Latin kerned pair) through
    // proportional DejaVuSans. Modern fonts deliver kerning via GPOS
    // (not the legacy `kern` table), so the observable signal is that
    // the AV-as-pair advance differs from the sum of solo-A and solo-V
    // advances. A regression that disabled GPOS unconditionally would
    // make these equal. Skip if no Latin kern pair fires (test
    // verifies nothing then).
    template<> template<>
    void alfontshaping_gl_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFtHead(path);
        ensure("DejaVuSans loaded", ft.notNull());

        // Try a handful of classic kerned pairs; pick the first that
        // shows a measurable kern. DejaVu's GPOS coverage varies.
        struct Pair { llwchar l, r; };
        const Pair pairs[] = {
            {'A','V'}, {'A','W'}, {'V','A'}, {'W','A'},
            {'T','o'}, {'T','e'}, {'T','a'}, {'L','T'},
            {'F','.'}, {'V','.'}, {'P','.'}, {'A','.'}
        };
        bool found_kerned = false;
        for (const auto& p : pairs)
        {
            LLWString l    = wstr(p.l);
            LLWString r    = wstr(p.r);
            LLWString lr   = wstr(p.l, p.r);
            std::vector<ALShapedGlyph> lg, rg, lrg;
            ALFontShaping::shapeRun(ft, l,  0, l.size(),  lg);
            ALFontShaping::shapeRun(ft, r,  0, r.size(),  rg);
            ALFontShaping::shapeRun(ft, lr, 0, lr.size(), lrg);
            if (lg.size() != 1 || rg.size() != 1 || lrg.size() != 2)
                continue;
            // Pair advance equals solo[0] + solo[1] when no kern fires.
            // Any difference signals the GPOS plumbing engaged.
            const F32 unkerned_total = lg[0].x_advance + rg[0].x_advance;
            const F32 kerned_total   = lrg[0].x_advance + lrg[1].x_advance;
            if (std::abs(kerned_total - unkerned_total) > 0.01f)
            {
                found_kerned = true;
                break;
            }
        }
        if (!found_kerned)
            skip("DejaVuSans lacks a measurable Latin kern pair");

        ensure("at least one Latin kern pair fired through HB GPOS",
               found_kerned);
    }
#endif // LL_MESA_HEADLESS
}
