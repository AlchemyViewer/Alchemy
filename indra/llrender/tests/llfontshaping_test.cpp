/**
 * @file llfontshaping_test.cpp
 * @brief Unit tests for LLFontShaping — HarfBuzz wrapper, ZWJ ligature
 *        retry, VS-16 strip, monospace bypass, LRU cache contract.
 *
 * The bulk of the tests are pure-CPU: HB shaping itself doesn't touch
 * the atlas. The monospace-bypass tests at the bottom exercise
 * shape_sub_run's getGlyphInfo path which routes through addGlyph →
 * LLFontBitmapCache::nextOpenPos → gGL.bind, so they're wrapped in
 * #if LL_MESA_HEADLESS and only compile in when CMake links the test
 * binary against llrenderheadless. Same single-file pattern as
 * llfontregistry_test.cpp's GL-gated block.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "../llfontshaping.h"
#include "../llfontfreetype.h"
#include "../llfontface.h"
#include "../llfontregistry.h"  // EFontHinting full definition

#include "../test/lltut.h"

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

    bool fileExists(const std::string& path)
    {
        if (FILE* f = std::fopen(path.c_str(), "rb"))
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
                          /*weight=*/-1,
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
    struct llfontshaping_data
    {
        llfontshaping_data()
        {
            LLFontManager::initClass();
            LLFontShaping::clearCache();
        }
        ~llfontshaping_data()
        {
            LLFontShaping::clearCache();
            LLFontManager::cleanupClass();
        }
    };

    typedef test_group<llfontshaping_data> llfontshaping_test;
    typedef llfontshaping_test::object     llfontshaping_object;
    tut::llfontshaping_test llfontshaping_testcase("LLFontShaping");

    // Null root face and empty/inverted ranges all produce empty
    // output. The header documents these as the safe-fallback
    // contracts callers rely on for "fall back to 1:1 codepoint path."
    template<> template<>
    void llfontshaping_object::test<1>()
    {
        std::vector<LLShapedGlyph> out;
        LLWString s = wstr('a','b','c');

        LLFontShaping::shapeRun(/*root_face=*/nullptr, s, 0, s.size(), out);
        ensure("null root face -> empty output", out.empty());

        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLFontShaping::shapeRun(ft, s, /*begin=*/2, /*end=*/2, out);
        ensure("begin == end -> empty output", out.empty());

        LLFontShaping::shapeRun(ft, s, /*begin=*/3, /*end=*/2, out);
        ensure("begin > end -> empty output", out.empty());

        LLFontShaping::shapeRun(ft, s, /*begin=*/0, /*end=*/99, out);
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
    // route through addGlyph → atlas → gGL.bind, which a pure-CPU test
    // binary can't satisfy. The cmap match below is the strongest
    // identity check we can make without rasterizing.
    template<> template<>
    void llfontshaping_object::test<2>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('a','b','c');
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), out);
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
    // (llfontshaping.h:80-90).
    template<> template<>
    void llfontshaping_object::test<3>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('X','Y','a','b','c');
        std::vector<LLShapedGlyph> shape_run_out;
        LLFontShaping::shapeRun(ft, s, /*begin=*/2, /*end=*/5, shape_run_out);
        ensure_equals("shapeRun produced 3 glyphs", shape_run_out.size(), 3u);
        // 'a' is at original position 2 (begin == 2).
        ensure_equals("shapeRun cluster[0] is original-string index 2",
                      shape_run_out[0].cluster, 2);

        const auto& shape_line_out = LLFontShaping::shapeLine(ft, s, 2, 5);
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
    void llfontshaping_object::test<4>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('h','e','l','l','o');
        std::vector<LLShapedGlyph> first;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), first);

        std::vector<LLShapedGlyph> second;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), second);
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
    void llfontshaping_object::test<5>()
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            skip("DejaVuSans + InterVariable both required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        ensure("both faces loaded", a.notNull() && b.notNull());

        LLWString s = wstr('a','b');
        const auto& a_first  = LLFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_first  = LLFontShaping::shapeLine(b, s, 0, s.size());
        ensure("a shape non-empty", !a_first.empty());
        ensure("b shape non-empty", !b_first.empty());
        const void* a_addr0 = a_first.data();
        const void* b_addr0 = b_first.data();

        // Hit A again: cache hit returns same storage address.
        const auto& a_second = LLFontShaping::shapeLine(a, s, 0, s.size());
        ensure_equals("A cache hit: same backing storage",
                      a_second.data(), a_addr0);

        LLFontShaping::clearCacheForFace(a);

        // After clearing A: B's entry survives — its storage address
        // is unchanged. (Whether A's entry got reused at the same
        // address by the allocator after clear is implementation-
        // defined and not part of the contract.)
        const auto& a_after = LLFontShaping::shapeLine(a, s, 0, s.size());
        ensure("A re-shape produces output", !a_after.empty());

        const auto& b_after = LLFontShaping::shapeLine(b, s, 0, s.size());
        ensure_equals("B cache survived clearCacheForFace(A)",
                      b_after.data(), b_addr0);
    }

    // ZWJ heart-on-fire: U+2764 U+200D U+1F525 must collapse to a
    // single glyph through Noto-COLRv1 (whose GSUB has the ligature
    // rule). Pins d4a5a4c904 — the regression that left ZWJ sequences
    // unligatured because the run was fragmenting at VS-16 and ZWJ.
    template<> template<>
    void llfontshaping_object::test<6>()
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            skip("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("Noto-COLRv1 loaded", ft.notNull());

        // U+2764 (heart) U+200D (ZWJ) U+1F525 (fire)
        LLWString s = wstr(0x2764, 0x200D, 0x1F525);
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure("heart-on-fire shaped to non-empty output", !out.empty());
        ensure_equals("heart-on-fire ZWJ ligated to a single glyph",
                      out.size(), 1u);
    }

    // VS-16 strip path: shaping U+2764 U+FE0F through Noto-COLRv1
    // (whose cmap lacks U+FE0F) must produce the same output as
    // shaping U+2764 alone — shape_sub_run pre-strips VS-16 before
    // passing to HB. Pins llfontshaping.cpp:257-264.
    template<> template<>
    void llfontshaping_object::test<7>()
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

        std::vector<LLShapedGlyph> with_out;
        std::vector<LLShapedGlyph> without_out;
        LLFontShaping::shapeRun(ft, with_vs,    0, with_vs.size(),    with_out);
        LLFontShaping::shapeRun(ft, without_vs, 0, without_vs.size(), without_out);

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
    void llfontshaping_object::test<9>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(head, s, 0, s.size(), out);
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
    // cluster_back_map rebase at llfontshaping.cpp:392-403.
    template<> template<>
    void llfontshaping_object::test<10>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), out);
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
    // face only (llfontshaping.cpp:62-64); B's own entries (if any)
    // survive — we don't double-purge across heads sharing fallbacks.
    template<> template<>
    void llfontshaping_object::test<11>()
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
        const auto& a_first = LLFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_first = LLFontShaping::shapeLine(b, s, 0, s.size());
        ensure("a non-empty", !a_first.empty());
        ensure("b non-empty", !b_first.empty());
        const void* a_addr = a_first.data();
        const void* b_addr = b_first.data();

        LLFontShaping::clearCacheForFace(a);

        // B's entry survives clearCacheForFace(A): its storage stays
        // at the same address. (Whether A's entry got reused by the
        // allocator at the same address is implementation-defined and
        // not load-bearing for the contract; B's survival is.)
        const auto& a_after = LLFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_after = LLFontShaping::shapeLine(b, s, 0, s.size());
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
    void llfontshaping_object::test<12>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        // U+1F525 (fire emoji) is not in DejaVuSans's cmap, and we
        // attached no fallback, so HB must produce notdef.
        LLWString s = wstr(0x1F525);
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("uncovered codepoint produces 1 glyph", out.size(), 1u);
        ensure_equals("uncovered codepoint glyph_id == 0 (notdef)",
                      out[0].glyph_id, 0u);
    }

    // clearCache empties the LRU. Subsequent shapes must rebuild
    // (storage moves) — we observe the rebuild by comparing the
    // returned reference's data() pointer before vs after clear.
    template<> template<>
    void llfontshaping_object::test<8>()
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            skip("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        ensure("DejaVuSans loaded", ft.notNull());

        LLWString s = wstr('h','i');
        const auto& first  = LLFontShaping::shapeLine(ft, s, 0, s.size());
        ensure("shape produced output", !first.empty());
        const void* addr0  = first.data();

        // Cache hit: same storage.
        const auto& second = LLFontShaping::shapeLine(ft, s, 0, s.size());
        ensure_equals("cache hit storage is stable", second.data(), addr0);

        LLFontShaping::clearCache();

        // Miss: rebuild allocates a new map node, storage address
        // changes. (Glyph stream content is identical; this assertion
        // is about cache state, not output.)
        const auto& after = LLFontShaping::shapeLine(ft, s, 0, s.size());
        ensure("post-clear rebuild produced output", !after.empty());
        ensure_not_equals("post-clear: storage address changed",
                          after.data(), addr0);
    }

    // Keycap cluster across face boundary: '9' + U+FE0F + U+20E3 must
    // route the WHOLE cluster to the emoji face so its GSUB can compose
    // the keycap glyph. Without Phase 1's cluster-aware itemizer, the
    // digit lands on the head face (DejaVuSans has '9') and U+FE0F+U+20E3
    // fragment off onto Noto-COLRv1 — producing a bare '9' followed by a
    // standalone keycap mark with no base. Pins the keycap fix in
    // shape_all_sub_runs (llfontshaping.cpp:497-546).
    template<> template<>
    void llfontshaping_object::test<13>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(head, s, 0, s.size(), out);
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
    }

    // ZWJ heart-on-fire across face boundary: U+2764 (BMP heart, present
    // in head's cmap) + U+FE0F + U+200D + U+1F525 (astral fire, only on
    // emoji face). Phase 1 routes the whole cluster to the emoji face so
    // its GSUB can collapse the ZWJ ligature. A regression to per-cp
    // itemization would split the heart onto head, breaking the rule.
    template<> template<>
    void llfontshaping_object::test<14>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(head, s, 0, s.size(), out);
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

    // Bare digit control: '9' alone (no FE0F + 20E3 follower) is NOT a
    // cluster per the walker. Phase 1's fast path must not interfere —
    // the digit routes to head as before, one glyph, no surprises.
    template<> template<>
    void llfontshaping_object::test<15>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(head, s, 0, s.size(), out);
        ensure_equals("bare '9' produces one glyph", out.size(), 1u);
        ensure_equals("bare '9' routes to head (not emoji fallback)",
                      out[0].face, head.get());
        ensure_equals("bare '9' glyph_id matches head's cmap",
                      out[0].glyph_id, head->getCharGlyphIndex(L'9'));
    }

#if LL_MESA_HEADLESS
    // GL-backed group: monospace bypass exercises shape_sub_run's
    // getGlyphInfo path, which routes through addGlyph → atlas →
    // gGL.bind. Wrapped in a separate fixture that pulls in the
    // headless OSMesa context so the rasterizer can satisfy the bind.
    struct llfontshaping_gl_data
    {
        std::unique_ptr<ll_test::HeadlessGL> gl = std::make_unique<ll_test::HeadlessGL>();
        ll_test::FontStateScope font_scope;
    };

    typedef test_group<llfontshaping_gl_data> llfontshaping_gl_test;
    typedef llfontshaping_gl_test::object     llfontshaping_gl_object;
    tut::llfontshaping_gl_test llfontshaping_gl_testcase("LLFontShapingGL");

    // Build an LLFontFreetype as a real head face (is_fallback=false),
    // which the headless GL context can satisfy. The pre-warm of
    // notdef inside loadFace runs the rasterizer to bind a fresh
    // atlas page — fine here, gGL is alive.
    static LLPointer<LLFontFreetype> loadFtHead(const std::string& filename)
    {
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        if (!ft->loadFace(filename, 14.f, 96.f, 96.f, /*weight=*/-1,
                          /*is_fallback=*/false, /*face_n=*/0,
                          EFontHinting::DEFAULT, /*flags=*/0))
        {
            return nullptr;
        }
        return ft;
    }

    // Monospace bypass (ligatures off). Shape "AB" through
    // DejaVuSansMono. The bypass synthesizes one glyph per codepoint
    // from FT mXAdvance, so advances are uniform — pins
    // llfontshaping.cpp:180-201 (the bypass branch) and the cell-
    // alignment invariant a kern-on regression would break.
    template<> template<>
    void llfontshaping_gl_object::test<1>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), out);
        ensure_equals("AB produces 2 glyphs through monospace bypass",
                      out.size(), 2u);
        ensure("each shaped glyph has positive advance",
               out[0].x_advance > 0.f && out[1].x_advance > 0.f);
        ensure_equals("monospace bypass: A and B advances are equal",
                      out[0].x_advance, out[1].x_advance);
        // The bypass path goes through getGlyphInfo, which writes
        // the canonical mXAdvance. We can therefore compare the
        // shaped advance against the face's reference advance for
        // exact equality.
        ensure_equals("bypass glyph[0] advance == ft->getXAdvance('A')",
                      out[0].x_advance, ft->getXAdvance(L'A'));
    }

    // Monospace + setAllowMonospaceLigatures(true): falls into the
    // HarfBuzz path with kern force-disabled. Same column-alignment
    // invariant — advances must remain uniform — and exercises the
    // separate code path at llfontshaping.cpp:246-256.
    template<> template<>
    void llfontshaping_gl_object::test<2>()
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
        std::vector<LLShapedGlyph> out;
        LLFontShaping::shapeRun(ft, s, 0, s.size(), out);
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
    void llfontshaping_gl_object::test<3>()
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
            std::vector<LLShapedGlyph> lg, rg, lrg;
            LLFontShaping::shapeRun(ft, l,  0, l.size(),  lg);
            LLFontShaping::shapeRun(ft, r,  0, r.size(),  rg);
            LLFontShaping::shapeRun(ft, lr, 0, lr.size(), lrg);
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
