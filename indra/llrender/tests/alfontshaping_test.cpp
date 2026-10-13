/**
 * @file alfontshaping_test.cpp
 * @brief Unit tests for ALFontShaping — HarfBuzz wrapper, ZWJ ligature
 *        retry, VS-16 strip, monospace feature plans, LRU cache contract.
 *
 * The tests are pure-CPU: HB shaping itself doesn't touch the atlas. The
 * monospace kerning path, which renders through
 * renderAndCreateGlyph → LLFontBitmapCache::nextOpenPos → gGL.bind, is in
 * alfontshaping_gl_test.cpp.
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

#include "llfonttest_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <hb.h>
#include <unicode/uchar.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_MODULE_H

#include <hb.h>
#include <hb-ft.h>

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

// LLFontManager owns the global FT_Library. Forward-declared so the
// stem-darkening test below can FT_Property_Get against it.
extern FT_Library gFTLibrary;

namespace
{
    using namespace ll_test;

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


    // Null root face and empty/inverted ranges all produce empty
    // output. The header documents these as the safe-fallback
    // contracts callers rely on for "fall back to 1:1 codepoint path."
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a null face or an empty or inverted or overlong slice shapes nothing", "[ALFontShaping]")
    {
        std::vector<ALShapedGlyph> out;
        const Text s = text('a','b','c');

        ALFontShaping::shapeRun(/*root_face=*/nullptr, s, 0, s.size(), out);
        UNSCOPED_INFO("null root face");
        CHECK(out.empty());

        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        ALFontShaping::shapeRun(ft, s, /*begin=*/2, /*end=*/2, out);
        UNSCOPED_INFO("begin == end");
        CHECK(out.empty());

        ALFontShaping::shapeRun(ft, s, /*begin=*/3, /*end=*/2, out);
        UNSCOPED_INFO("begin > end");
        CHECK(out.empty());

        ALFontShaping::shapeRun(ft, s, /*begin=*/0, /*end=*/99, out);
        UNSCOPED_INFO("end > size");
        CHECK(out.empty());
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
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: ASCII shapes to one glyph per codepoint with its cmap id and an advance", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const Text s = text('a','b','c');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE(out.size() == 3u);

        for (const auto& g : out)
        {
            REQUIRE(g.face != nullptr);
            REQUIRE(g.glyph_id != 0u);
            REQUIRE(g.x_advance > 0.f);
        }
        // Identity check: the shape's glyph_id for each ASCII char
        // must match the face's cmap-direct lookup. A regression that
        // bound HB to the wrong face would produce ids that disagree.
        CHECK(out[0].glyph_id == ft->getCharGlyphIndex(L'a'));
        CHECK(out[1].glyph_id == ft->getCharGlyphIndex(L'b'));
        CHECK(out[2].glyph_id == ft->getCharGlyphIndex(L'c'));
    }

    // shapeRun rebases cluster indices to the original wstr;
    // shapeLine returns slice-local clusters. Pin both contracts —
    // the renderer hot path branches on this distinction
    // (alfontshaping.h:80-90).
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: shapeRun clusters index the whole string and shapeLine's the slice", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const Text s = text('X','Y','a','b','c');
        const size_t begin = s.at(2);
        const size_t end   = s.at(5);
        std::vector<ALShapedGlyph> shape_run_out;
        ALFontShaping::shapeRun(ft, s, begin, end, shape_run_out);
        REQUIRE(shape_run_out.size() == 3u);
        // 'a' starts where the slice does.
        CHECK(shape_run_out[0].cluster == (S32)begin);

        const auto& shape_line_out = ALFontShaping::shapeLine(ft, s, begin, end);
        REQUIRE(shape_line_out.size() == 3u);
        // shapeLine clusters are slice-local: 'a' is at slice offset 0.
        CHECK(shape_line_out[0].cluster == 0);
    }

    // Cache hit contract: shaping the same (slice, root_face) twice
    // hits the cache and yields a glyph stream that's bit-for-bit
    // identical to the first. This is what callers depend on across
    // frames (pen positions and glyph ids must not drift), and it's the
    // load-bearing invariant for the LRU's reuse semantics. A hit is told
    // by cacheMutationCount, which every miss advances and no hit does.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: shaping the same slice again hits the cache with identical glyphs", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const Text s = text('h','e','l','l','o');
        std::vector<ALShapedGlyph> first;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), first);
        REQUIRE_FALSE(first.empty());
        const size_t mutations = ALFontShaping::cacheMutationCount();

        std::vector<ALShapedGlyph> second;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), second);
        UNSCOPED_INFO("the second shape is a cache hit");
        CHECK(ALFontShaping::cacheMutationCount() == mutations);
        REQUIRE(second.size() == first.size());
        for (size_t i = 0; i < first.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(second[i].glyph_id == first[i].glyph_id);
            REQUIRE(second[i].cluster == first[i].cluster);
            REQUIRE(second[i].x_advance == first[i].x_advance);
        }
    }

    // clearCacheForFace purges only the entries owned by the named
    // face — entries from other heads must persist. Verify by populating
    // the cache from two distinct faces and checking both round-trip
    // via shapeLine; clear face A's cache and verify A's entry is gone
    // (the next shape of A misses and rebuilds it) while B's persists.
    //
    // A miss is told by cacheMutationCount, which every miss advances,
    // not by the rebuilt entry's address: the allocator may hand the
    // freed storage straight back.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: clearCacheForFace drops one face's entry and leaves another's", "[ALFontShaping]")
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(a_path) || !fileExists(b_path))
            SKIP("DejaVuSans + InterVariable both required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        REQUIRE(a.notNull());
        REQUIRE(b.notNull());

        const Text s = text('a','b');
        const auto& a_first  = ALFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_first  = ALFontShaping::shapeLine(b, s, 0, s.size());
        REQUIRE_FALSE(a_first.empty());
        REQUIRE_FALSE(b_first.empty());
        const void* a_addr0 = a_first.data();
        const void* b_addr0 = b_first.data();

        // Hit A again: cache hit returns same storage address.
        const auto& a_second = ALFontShaping::shapeLine(a, s, 0, s.size());
        UNSCOPED_INFO("A cache hit: same backing storage");
        CHECK(a_second.data() == a_addr0);

        ALFontShaping::clearCacheForFace(a);
        UNSCOPED_INFO("clearCacheForFace(A) dropped one of the two entries");
        CHECK(ALFontShaping::cacheSize() == 1u);

        // After clearing A: shaping A again misses, so A's entry was
        // the one dropped, and B's entry survives — its storage address
        // is unchanged.
        const size_t mutations = ALFontShaping::cacheMutationCount();
        const auto& a_after = ALFontShaping::shapeLine(a, s, 0, s.size());
        CHECK_FALSE(a_after.empty());
        UNSCOPED_INFO("A's entry was dropped: shaping A again rebuilt it");
        CHECK(ALFontShaping::cacheMutationCount() != mutations);

        const auto& b_after = ALFontShaping::shapeLine(b, s, 0, s.size());
        UNSCOPED_INFO("B cache survived clearCacheForFace(A)");
        CHECK(b_after.data() == b_addr0);
    }

    // ZWJ heart-on-fire: U+2764 U+200D U+1F525 must collapse to a
    // single glyph through Noto-COLRv1 (whose GSUB has the ligature
    // rule). Pins d4a5a4c904 — the regression that left ZWJ sequences
    // unligatured because the run was fragmenting at VS-16 and ZWJ.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: the ZWJ heart-on-fire ligates to one glyph through Noto-COLRv1", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // U+2764 (heart) U+200D (ZWJ) U+1F525 (fire)
        const Text s = text(0x2764, 0x200D, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        CHECK(out.size() == 1u);
    }

    // VS-16 strip path: shaping U+2764 U+FE0F through Noto-COLRv1
    // (whose cmap lacks U+FE0F) must produce the same output as
    // shaping U+2764 alone — shape_sub_run pre-strips VS-16 before
    // passing to HB. Pins alfontshaping.cpp:257-264.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a VS-16 the cmap lacks is stripped: same glyphs as the base alone", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // Sanity: confirm VS-16 is genuinely absent from this font's
        // cmap (otherwise the strip path doesn't fire and the test
        // verifies nothing useful).
        CHECK(ft->getCharGlyphIndex(0xFE0F) == 0u);

        const Text with_vs    = text(0x2764, 0xFE0F);
        const Text without_vs = text(0x2764);

        std::vector<ALShapedGlyph> with_out;
        std::vector<ALShapedGlyph> without_out;
        ALFontShaping::shapeRun(ft, with_vs,    0, with_vs.size(),    with_out);
        ALFontShaping::shapeRun(ft, without_vs, 0, without_vs.size(), without_out);

        REQUIRE(with_out.size() == without_out.size());
        REQUIRE_FALSE(with_out.empty());
        for (size_t i = 0; i < with_out.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(with_out[i].glyph_id == without_out[i].glyph_id);
        }
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
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: clearCache empties the LRU; subsequent shapes repopulate it", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const Text s = text('h','i');
        const auto& first = ALFontShaping::shapeLine(ft, s, 0, s.size());
        REQUIRE_FALSE(first.empty());
        UNSCOPED_INFO("first shape leaves one entry");
        CHECK(ALFontShaping::cacheSize() == 1u);
        // Copy the glyphs BEFORE clearCache invalidates `first` —
        // shapeLine returns a reference into the LRU, so the entry's
        // storage is gone the moment the LRU is cleared. Reading
        // `first` afterward would be a use-after-free.
        const std::vector<ALShapedGlyph> first_glyphs = first;
        const void* addr0 = first.data();

        // Cache hit: same storage AND the entry count stays at one.
        const auto& second = ALFontShaping::shapeLine(ft, s, 0, s.size());
        UNSCOPED_INFO("cache hit storage is stable");
        CHECK(second.data() == addr0);
        UNSCOPED_INFO("cache hit doesn't add an entry");
        CHECK(ALFontShaping::cacheSize() == 1u);

        ALFontShaping::clearCache();
        CHECK(ALFontShaping::cacheSize() == 0u);

        // Miss: re-shape repopulates the cache. Output content is
        // identical (shaping is deterministic given the same face +
        // codepoints); the cache state is what changed.
        const auto& after = ALFontShaping::shapeLine(ft, s, 0, s.size());
        REQUIRE_FALSE(after.empty());
        UNSCOPED_INFO("post-clear miss repopulated to one entry");
        CHECK(ALFontShaping::cacheSize() == 1u);
        UNSCOPED_INFO("post-clear shape has as many glyphs as the first");
        REQUIRE(after.size() == first_glyphs.size());
        for (size_t i = 0; i < after.size(); ++i)
        {
            CAPTURE(i);
            CHECK(after[i].glyph_id == first_glyphs[i].glyph_id);
            CHECK(after[i].cluster == first_glyphs[i].cluster);
            CHECK(after[i].x_advance == first_glyphs[i].x_advance);
        }
    }

    // Multi-hop shape run: ASCII + CJK + Emoji through a head (DejaVu)
    // with two fallbacks (SourceHanSans + Noto-COLRv1). shape_sub_run
    // itemizes by face coverage so each cluster routes to the right
    // face. Pins gap audit #3 — multi-hop selectShapingFace was
    // untested, the existing fallback tests use a single hop only.
    // SourceHanSans ships as a collection; loadFt opens its first face.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: ASCII then CJK then emoji route to the head and each of two fallbacks", "[ALFontShaping]")
    {
        const std::string dejavu = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string han    = std::string(kFontDir) + "SourceHanSans-VF.otf.ttc";
        const std::string emoji  = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(dejavu) || !fileExists(han) || !fileExists(emoji))
            SKIP("DejaVuSans + SourceHanSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head = loadFt(dejavu);
        LLPointer<LLFontFreetype> cjk  = loadFt(han);
        LLPointer<LLFontFreetype> emo  = loadFt(emoji);
        REQUIRE(head.notNull());
        REQUIRE(cjk.notNull());
        REQUIRE(emo.notNull());
        head->addFallbackFont(cjk);
        head->addFallbackFont(emo);

        // 'a' (head) + U+4F60 你 (cjk) + U+1F525 fire (emoji).
        const Text s = text('a', 0x4F60, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE(out.size() == 3u);

        // Each glyph carries the face it came from — verify the routing.
        CHECK(out[0].face == head.get());
        CHECK(out[1].face == cjk.get());
        CHECK(out[2].face == emo.get());
        // All three glyphs should have non-zero ids (no notdef anywhere).
        UNSCOPED_INFO("'a' resolved to non-zero glyph");
        CHECK(out[0].glyph_id != 0u);
        UNSCOPED_INFO("CJK resolved to non-zero glyph");
        CHECK(out[1].glyph_id != 0u);
        UNSCOPED_INFO("emoji resolved to non-zero glyph");
        CHECK(out[2].glyph_id != 0u);
    }

    // VS-16 strip preserves cluster identity in the original wstr.
    // After stripping U+FE0F before HB, the surviving codepoints'
    // clusters must point into ORIGINAL string positions (so a fire
    // emoji at position 3 stays cluster=3, even though it was at
    // position 2 in the post-strip buffer fed to HB). Pins
    // cluster_back_map rebase at alfontshaping.cpp:392-403.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: after a VS-16 strip clusters still index the original string", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // Heart + VS-16 + ZWJ + fire. The second codepoint (FE0F) gets
        // stripped. The output's clusters must still reference the original
        // codepoints {0, 2, 3} or the ZWJ-collapse may produce a single
        // cluster=0.
        const Text s = text(0x2764, 0xFE0F, 0x200D, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        // Whatever the final shape (ZWJ ligature collapses to 1 glyph,
        // or 2 with VS-16 stripped), every cluster must point into the
        // ORIGINAL string. A regression that left clusters in the post-strip
        // space would produce offsets short by the VS-16's three bytes.
        // Stronger pin: no cluster sits at the stripped VS-16's own offset;
        // the strip MUST rebase past it.
        for (const auto& g : out)
        {
            REQUIRE(g.cluster >= 0);
            REQUIRE(g.cluster < (S32)s.size());
            UNSCOPED_INFO("cluster does NOT point at stripped VS-16 position");
            REQUIRE(g.cluster != (S32)s.at(1));
        }
    }

    // clearCacheForFace selectivity post-multi-hop: an entry keyed on
    // face A whose glyph stream contains a fallback-face B glyph is
    // dropped by clearCacheForFace(A). The shape LRU is keyed on root
    // face only (alfontshaping.cpp:62-64); B's own entries (if any)
    // survive — we don't double-purge across heads sharing fallbacks.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: clearCacheForFace on a head keeps its fallback face's entry", "[ALFontShaping]")
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "SourceHanSans-VF.otf.ttc";
        if (!fileExists(a_path) || !fileExists(b_path))
            SKIP("DejaVuSans + SourceHanSans required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        REQUIRE(a.notNull());
        REQUIRE(b.notNull());
        a->addFallbackFont(b);

        // Shape via A — its entry's glyph stream contains a B glyph
        // for the CJK codepoint. Shape via B directly — separate entry.
        const Text s = text('a', 0x4F60);
        const auto& a_first = ALFontShaping::shapeLine(a, s, 0, s.size());
        const auto& b_first = ALFontShaping::shapeLine(b, s, 0, s.size());
        REQUIRE_FALSE(a_first.empty());
        REQUIRE_FALSE(b_first.empty());
        const void* b_addr = b_first.data();

        // One of the two entries goes. Which one is told by B's shape
        // below hitting, not by A's address: whether A's rebuilt entry
        // lands where the old one was is the allocator's business.
        ALFontShaping::clearCacheForFace(a);
        UNSCOPED_INFO("clearCacheForFace(A) dropped one of the two entries");
        CHECK(ALFontShaping::cacheSize() == 1u);

        const auto& a_after = ALFontShaping::shapeLine(a, s, 0, s.size());
        CHECK_FALSE(a_after.empty());

        // B's entry survives clearCacheForFace(A): shaping B again is a
        // hit, which leaves cacheMutationCount alone where a purged and
        // rebuilt entry would have advanced it, and finds B's storage at
        // the same address.
        const size_t mutations = ALFontShaping::cacheMutationCount();
        const auto& b_after = ALFontShaping::shapeLine(b, s, 0, s.size());
        UNSCOPED_INFO("B's entry survived clearCacheForFace(A): shaping B again hit it");
        CHECK(ALFontShaping::cacheMutationCount() == mutations);
        UNSCOPED_INFO("B's entry survived clearCacheForFace(A): same storage");
        CHECK(b_after.data() == b_addr);
    }

    // notdef contract: shaping a codepoint through a head with NO
    // fallback covering it must still produce output (one glyph with
    // glyph_id == 0). The renderer relies on this to draw the .notdef
    // rectangle for missing-glyph indication; an empty output would
    // silently drop the codepoint instead.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a codepoint no face covers shapes to one notdef glyph", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // U+1F525 (fire emoji) is not in DejaVuSans's cmap, and we
        // attached no fallback, so HB must produce notdef.
        const Text s = text(0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE(out.size() == 1u);
        CHECK(out[0].glyph_id == 0u);
    }

    // Keycap cluster across face boundary: '9' + U+FE0F + U+20E3 must
    // route the WHOLE cluster to the emoji face so its GSUB can compose
    // the keycap glyph. Without Phase 1's cluster-aware itemizer, the
    // digit lands on the head face (DejaVuSans has '9') and U+FE0F+U+20E3
    // fragment off onto Noto-COLRv1 — producing a bare '9' followed by a
    // standalone keycap mark with no base. Pins the keycap fix in
    // shape_all_sub_runs (alfontshaping.cpp:497-546).
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: keycap 9 goes whole to the emoji fallback and composes to one glyph", "[ALFontShaping]")
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            SKIP("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        REQUIRE(head.notNull());
        REQUIRE(emoji.notNull());
        head->addFallbackFont(emoji);

        const Text s = text('9', 0xFE0F, 0x20E3);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        // The emoji face has the GSUB rule digit + U+20E3 -> keycap glyph
        // (VS-16 stripped pre-shape). All output glyphs must come from
        // the emoji face — not head — to confirm the cluster routed as
        // one unit. A regression to per-codepoint routing would put the
        // '9' glyph on `head` and only the keycap mark on `emoji`.
        for (const auto& g : out)
        {
            REQUIRE(g.face == emoji.get());
        }
        // Strong pin: the GSUB rule actually fires across the face
        // boundary. A weaker form of the regression — routing matches
        // but GSUB doesn't collapse — would leave 2+ glyphs on the
        // emoji face (the '9' outline plus an unbound keycap mark),
        // matching exactly what the user sees in the buggy report.
        CHECK(out.size() == 1u);
    }

    // ZWJ heart-on-fire across face boundary: U+2764 (BMP heart, present
    // in head's cmap) + U+FE0F + U+200D + U+1F525 (astral fire, only on
    // emoji face). Phase 1 routes the whole cluster to the emoji face so
    // its GSUB can collapse the ZWJ ligature. A regression to per-cp
    // itemization would split the heart onto head, breaking the rule.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: the ZWJ heart-on-fire goes whole to the emoji fallback and ligates", "[ALFontShaping]")
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            SKIP("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        REQUIRE(head.notNull());
        REQUIRE(emoji.notNull());
        head->addFallbackFont(emoji);

        // Sanity: head DOES carry U+2764 — selectShapingFace would
        // happily route it to head without the cluster fast path.
        UNSCOPED_INFO("head face has U+2764 in cmap (without Phase 1 the heart "
               "would route to head and split the cluster)");
        CHECK(head->getCharGlyphIndex(0x2764) != 0u);

        const Text s = text(0x2764, 0xFE0F, 0x200D, 0x1F525);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        for (const auto& g : out)
        {
            REQUIRE(g.face == emoji.get());
        }
        // Pin the ligature too — Noto-COLRv1's GSUB collapses the
        // sequence to a single glyph. The first heart-on-fire case covers
        // the same rule when shaping directly through Noto; this asserts
        // it still fires when the head is a non-emoji font and the
        // cluster is routed across the fallback boundary.
        CHECK(out.size() == 1u);
    }

    // Bare digit control: '9' alone (no FE0F + 20E3 follower) is NOT a
    // cluster per the walker. Phase 1's fast path must not interfere —
    // the digit routes to head as before, one glyph, no surprises.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a bare 9 is no emoji cluster and stays on the head face", "[ALFontShaping]")
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            SKIP("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        REQUIRE(head.notNull());
        REQUIRE(emoji.notNull());
        head->addFallbackFont(emoji);

        const Text s = text('9');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE(out.size() == 1u);
        CHECK(out[0].face == head.get());
        CHECK(out[0].glyph_id == head->getCharGlyphIndex(L'9'));
    }

    // Hash-keycap directly through the emoji face: pin that Noto-COLRv1
    // actually has the GSUB rule for '#' + VS-16 + U+20E3 -> composed
    // keycap (one glyph). The '9' rule is exercised by the keycap 9 case
    // across a face boundary; this version uses Noto-COLRv1 directly so a font-
    // level missing rule is observable as glyph_count > 1 (not a routing
    // bug). User report: '#️⃣' renders as '#' + a standalone keycap mark
    // — diagnosed by this test as either a font-rule miss or a routing
    // miss, with the existing rule-fires path through 9️⃣ as the control.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: keycap # composes to one glyph through Noto-COLRv1 alone", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // Sanity: the font carries '#' and U+20E3 in cmap. If either
        // were missing, hb_shape would emit notdef before any GSUB
        // could match.
        CHECK(ft->getCharGlyphIndex(L'#') != 0u);
        CHECK(ft->getCharGlyphIndex(0x20E3) != 0u);

        const Text s = text('#', 0xFE0F, 0x20E3);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        // If the GSUB keycap rule fires, the run collapses to a single
        // composed glyph (the same shape the heart-on-fire and keycap 9
        // cases expect for their respective ligatures).
        CHECK(out.size() == 1u);
    }

    // Adjacent clusters: '3️⃣' (keycap) + '🏳️‍🌈' (pride flag, ZWJ
    // sequence). Both clusters route to the emoji face. The Phase 1
    // implementation merged them into one HarfBuzz buffer when their
    // chosen faces matched, which let GSUB rules match across the
    // cluster boundary — the user reported the pride flag flickering
    // down to a bare white flag (the rainbow part of the ZWJ ligature
    // collapsed against the wrong context). Each cluster must shape as
    // its own sub-run, so each rule sees only its own codepoints.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: adjacent keycap and pride-flag clusters each shape on their own", "[ALFontShaping]")
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            SKIP("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        REQUIRE(head.notNull());
        REQUIRE(emoji.notNull());
        head->addFallbackFont(emoji);

        // '3' VS-16 U+20E3 (keycap), then U+1F3F3 (waving white flag)
        // VS-16 ZWJ U+1F308 (rainbow) — pride flag.
        const Text s = text('3', 0xFE0F, 0x20E3,
                           0x1F3F3, 0xFE0F, 0x200D, 0x1F308);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        for (const auto& g : out)
        {
            REQUIRE(g.face == emoji.get());
        }
        // Both clusters compose: keycap '3️⃣' -> 1 glyph, pride flag
        // '🏳️‍🌈' -> 1 glyph. Total 2. A regression that merged the
        // two clusters into one buffer would produce a different count
        // (e.g. the pride flag's GSUB matching against the keycap mark
        // and falling back to bare white-flag + standalone rainbow).
        UNSCOPED_INFO("each adjacent cluster collapses independently");
        CHECK(out.size() == 2u);
    }

    // Cluster with no emoji-face coverage: route to root rather than to
    // a face that lacks the base. Simulates the "LimitedEmoji" scenario
    // where the emoji fallback's unicode_ranges excludes the keycap
    // mark — selectShapingFace returns root for U+20E3 and pick_cluster_face
    // must NOT route the cluster to a face that can't render its base.
    // We model this by attaching DejaVu (which has '#' but lacks U+20E3)
    // as the only fallback and shaping a keycap. Result must keep '#'
    // visible on root rather than tofu-ing the base on a non-covering face.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a keycap no face can compose keeps its base # on the root face", "[ALFontShaping]")
    {
        const std::string head_path     = std::string(kFontDir) + "InterVariable.woff2";
        const std::string fallback_path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(head_path) || !fileExists(fallback_path))
            SKIP("InterVariable + DejaVuSans required");

        LLPointer<LLFontFreetype> head     = loadFt(head_path);
        LLPointer<LLFontFreetype> fallback = loadFt(fallback_path);
        REQUIRE(head.notNull());
        REQUIRE(fallback.notNull());
        head->addFallbackFont(fallback);

        // Sanity: neither face has U+20E3 (keycap mark) — that's the
        // scenario this test exercises. Neither has the keycap GSUB
        // rule. Best-case outcome: the base '#' renders through root.
        CHECK(head->getCharGlyphIndex(0x20E3) == 0u);
        CHECK(fallback->getCharGlyphIndex(0x20E3) == 0u);
        CHECK(head->getCharGlyphIndex(L'#') != 0u);

        const Text s = text('#', 0xFE0F, 0x20E3);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        // The base '#' must end up on a face that has it (root). A
        // regression that routed to a non-covering face would tofu
        // the '#' (glyph_id == 0) which is the worse failure mode.
        bool found_hash = false;
        for (const auto& g : out)
        {
            if (g.face == head.get() && g.glyph_id == head->getCharGlyphIndex(L'#'))
                found_hash = true;
        }
        UNSCOPED_INFO("'#' base routed to a face that has it");
        CHECK(found_hash);
    }

    // Subdivision flag: U+1F3F4 (black flag) + tag chars 'gbeng' + the
    // U+E007F CANCEL TAG terminator. Validates that LLStringOps::
    // isEmojiClusterExtender includes the U+E0020-U+E007F tag-char range
    // and that pick_cluster_face routes the entire 7-codepoint cluster
    // to a face whose cmap covers the tag chars. A regression that
    // dropped tag-char support from the predicate would split the flag
    // off from its tag bytes and render the bytes as visible glyphs.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a tag-sequence subdivision flag goes whole to the emoji fallback", "[ALFontShaping]")
    {
        const std::string head_path  = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string emoji_path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(head_path) || !fileExists(emoji_path))
            SKIP("DejaVuSans + Noto-COLRv1 required");

        LLPointer<LLFontFreetype> head  = loadFt(head_path);
        LLPointer<LLFontFreetype> emoji = loadFt(emoji_path);
        REQUIRE(head.notNull());
        REQUIRE(emoji.notNull());
        head->addFallbackFont(emoji);

        // 🏴 (U+1F3F4) + 'g' 'b' 'e' 'n' 'g' tag chars + U+E007F.
        const Text s = text(0x1F3F4,
                           0xE0067, 0xE0062, 0xE0065, 0xE006E, 0xE0067,
                           0xE007F);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(head, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());
        // Routing assertion is the load-bearing one: every cluster
        // member must land on the emoji face. A predicate regression
        // that excluded tag chars would split the flag onto emoji and
        // the tag bytes onto root.
        for (const auto& g : out)
        {
            REQUIRE(g.face == emoji.get());
        }
        // Stretch goal: if Noto-COLRv1 carries the GB-ENG subdivision
        // flag, GSUB collapses the whole sequence to one glyph. Some
        // Noto-COLRv1 builds ship without subdivision flags — tolerate
        // up to N glyphs, but never MORE than the input length (which
        // would mean glyph duplication).
        UNSCOPED_INFO("output glyph count never exceeds input codepoint count");
        CHECK(out.size() <= s.codepoints());
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
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: monospace HB shaping keeps 200 glyphs on the cell with no drift", "[ALFontShaping]")
    {
        const char* mono_files[] = {
            "DejaVuSansMono.woff2",
            "SourceCodeVF-Upright.woff2",
        };

        // Mixed ASCII pool to exercise more lookups than the simple AV
        // pair in alfontshaping_gl_test.cpp: alphanumerics, punctuation, brackets,
        // symbols. Repeated to 200 chars so per-glyph drift
        // accumulates measurably.
        const std::string ascii_pool =
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
            "!@#$%^&*()_+-=[]{};':\",./<>?";
        constexpr size_t N = 200;
        // All ASCII, so a byte is a codepoint and no map is needed.
        std::string s;
        s.reserve(N);
        for (size_t i = 0; i < N; ++i)
            s.push_back(ascii_pool[i % ascii_pool.size()]);

        size_t fonts_tested = 0;
        for (const char* fname : mono_files)
        {
            const std::string path = std::string(kFontDir) + fname;
            if (!fileExists(path))
                continue;
            CAPTURE(fname);
            LLPointer<LLFontFreetype> ft = loadFt(path);
            REQUIRE(ft.notNull());
            REQUIRE(ft->isFixedWidth());
            ft->setAllowMonospaceLigatures(true);  // route through HB

            std::vector<ALShapedGlyph> out;
            ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
            REQUIRE(out.size() == N);

            // Reference: first glyph's advance. In a fixed-width face
            // all ASCII codepoints share one FT mXAdvance, so out[0]
            // sets the canonical cell. Deliberately don't use
            // ft->getXAdvance — that path asserts !mIsFallback and
            // the probe loads as fallback to skip GL pre-warm.
            const F32 ref_advance = out[0].x_advance;
            REQUIRE(ref_advance > 0.f);

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
            REQUIRE(max_dev < 0.5f);
            REQUIRE(cumulative_dev < 0.5f);
            // Bypass writes 0 offsets; HB output should match for
            // ASCII (no GPOS positioning adjustments fire on monospace
            // ASCII with kern off).
            REQUIRE(max_xoff < 0.5f);
            REQUIRE(max_yoff < 0.5f);
            ++fonts_tested;
        }
        if (fonts_tested == 0)
            SKIP("no monospace fonts present in test data dir");
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
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: strict monospace never ligates: one glyph per codepoint", "[ALFontShaping]")
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
            CAPTURE(fname);
            LLPointer<LLFontFreetype> ft = loadFt(path);
            REQUIRE(ft.notNull());
            REQUIRE(ft->isFixedWidth());
            // Default: ligatures off. Routes through HB with kFixedWidthStrict.
            REQUIRE_FALSE(ft->getAllowMonospaceLigatures());

            // All ASCII, so a byte is a codepoint here too.
            const std::string& s = ligature_bait;

            std::vector<ALShapedGlyph> out;
            ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
            REQUIRE(out.size() == s.size());
            ++fonts_tested;
        }
        if (fonts_tested == 0)
            SKIP("no monospace fonts present in test data dir");
    }

    // Combining marks position via GPOS in monospace shaping. The retired
    // bypass emitted U+0301 (acute) etc. with x_advance=0 and x/y_offset=0,
    // stacking the mark at cell origin without GPOS placement; the user-
    // visible result was misaligned diacritics on monospace text. After
    // routing monospace through HB, the mark / mkmk features stay enabled
    // (they're not in kFixedWidthStrict), so HB applies GPOS to position
    // the mark over its base. Pin: shape "a" + U+0301 (combining acute
    // accent) on a monospace face; either it composes into a precomposed
    // 'á', or the cluster has 2 glyphs sharing one cluster index, the
    // second has zero x_advance (mark consumes no cell), and GPOS moves
    // the mark back over its base (a negative x_offset). HarfBuzz's
    // normaliser composes "a" + U+0301 whenever the face has U+00E1, as
    // DejaVuSansMono does, so the mark path is pinned again with "q" +
    // U+0301, which has no precomposed form.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a monospace combining acute composes or sits as a GPOS-placed mark", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSansMono.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSansMono.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());
        CHECK(ft->isFixedWidth());

        const Text s = text(L'a', 0x0301);  // a + COMBINING ACUTE ACCENT
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
        REQUIRE_FALSE(out.empty());
        // Expected outcomes:
        //  (a) composed to precomposed 'á' → 1 glyph, full cell advance.
        //  (b) Two glyphs (a + acute) in one HB cluster → first carries
        //      full advance, second has zero advance and the mark
        //      positioned via GPOS.
        // Either is correct GPOS-driven behavior; the bypass produced
        // neither (it emitted 2 glyphs, both with full advance).
        const F32 ref_advance = out[0].x_advance;
        CHECK(ref_advance > 0.f);
        if (out.size() == 1)
        {
            // Composition path. One cell, no second glyph.
            UNSCOPED_INFO("cluster index is 0 (base)");
            CHECK(out[0].cluster == 0);
        }
        else if (out.size() == 2)
        {
            // Mark cluster path. Marks share the base's cluster index
            // and consume zero advance (a hard requirement — a non-zero
            // mark advance would shift the next character right).
            UNSCOPED_INFO("mark shares base cluster");
            CHECK(out[1].cluster == out[0].cluster);
            UNSCOPED_INFO("mark glyph has zero advance");
            CHECK(out[1].x_advance == 0.f);
            UNSCOPED_INFO("GPOS moved the mark back over its base");
            CHECK(out[1].x_offset < 0.f);
        }
        else
        {
            UNSCOPED_INFO("combining cluster produced unexpected glyph count");
            CHECK(false);
        }

        // "q" + U+0301 cannot compose, so it always takes path (b).
        const Text q = text(L'q', 0x0301);
        ALFontShaping::shapeRun(ft, q, 0, q.size(), out);
        REQUIRE(out.size() == 2u);
        UNSCOPED_INFO("mark shares base cluster");
        CHECK(out[1].cluster == out[0].cluster);
        UNSCOPED_INFO("mark glyph has zero advance");
        CHECK(out[1].x_advance == 0.f);
        UNSCOPED_INFO("GPOS moved the mark back over its base");
        CHECK(out[1].x_offset < 0.f);
    }

    // -----------------------------------------------------------------
    // FT/HB consistency block (the next eight cases). Locks the integration
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
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: HB's font scale is what hb-ft derives from the FT face's size", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        REQUIRE(face != nullptr);
        hb_font_t* hbf = face->getHbFont();
        REQUIRE(hbf != nullptr);

        int hb_x_scale = 0, hb_y_scale = 0;
        hb_font_get_scale(hbf, &hb_x_scale, &hb_y_scale);

        FT_Face ftf = face->face();
        const FT_Size_Metrics& m = ftf->size->metrics;
        // Replicate hb-ft's internal formula. Cast through uint64_t to
        // avoid 32-bit overflow at large UPEM × scale products.
        const int expected_x = (int)((((std::uint64_t)m.x_scale * (std::uint64_t)ftf->units_per_EM) + (1u << 15)) >> 16);
        const int expected_y = (int)((((std::uint64_t)m.y_scale * (std::uint64_t)ftf->units_per_EM) + (1u << 15)) >> 16);
        CHECK(hb_x_scale == expected_x);
        CHECK(hb_y_scale == expected_y);
        // Sanity: scale must be positive (zero would mean uninitialized).
        CHECK(hb_x_scale > 0);
        CHECK(hb_y_scale > 0);
    }

    // (Audit test 24 intentionally absent — it checked hb_font_get_ppem
    // against FT ppem, but hb_ft_font_create_referenced doesn't set
    // ppem on the HB font; only scale carries the size invariant. This
    // block is the audit's tests 23 and 25..31, in that order.)

    // For an unligatured / unkerned ASCII run, HB's per-glyph
    // x_advance must equal FT's slot->advance.x for the same glyph
    // index loaded with the same flags. Catches divergence in HB↔FT
    // outline scaling or load-flag plumbing. DejaVuSans has no GPOS
    // kern between adjacent lowercase letters, so HB's GPOS pass is
    // a no-op and per-glyph advances come straight from FT.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: HB advances equal FreeType's for an unkerned ASCII run", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        REQUIRE(face != nullptr);
        FT_Face ftf = face->face();
        const FT_Int32 load_flags = static_cast<FT_Int32>(face->hinting());

        const Text s = text('a','b','c','d','e','f');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE(out.size() == 6u);

        for (size_t i = 0; i < out.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(FT_Load_Glyph(ftf, out[i].glyph_id, load_flags) == 0);
            const F32 ft_advance = ftf->glyph->advance.x * (1.f / 64.f);
            // 26.6 -> float; equality in float at this precision
            // would be exact when no GPOS adjustment fired. Tolerate
            // 1/64 px to cover HB's internal rounding paths.
            const F32 delta = std::fabs(out[i].x_advance - ft_advance);
            REQUIRE(delta < (1.f / 64.f) + 1e-5f);
        }
    }

    // HB's reported load flags after construction must equal what the
    // FT renderGlyph path uses. Both are casts of the hinting the face
    // was loaded with; any refactor that splits the casts asymmetrically
    // breaks shaped vs codepoint advance consistency. The face loads at
    // LIGHT, not the default, so a hinting dropped on its way to either
    // cannot pass for it. hb_ft_font_get_load_flags has existed since
    // HB 1.7 — the codebase requires newer than that.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: HB's load flags are the hinting FT renders glyphs with", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        constexpr EFontHinting hinting = EFontHinting::LIGHT;
        LLPointer<LLFontFreetype> ft = loadFt(path, /*is_fallback=*/true, /*point_size=*/14.f, /*weight=*/-1, hinting);
        REQUIRE(ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        REQUIRE(face != nullptr);
        hb_font_t* hbf = face->getHbFont();
        REQUIRE(hbf != nullptr);

        const int hb_flags = hb_ft_font_get_load_flags(hbf);
        CHECK(hb_flags == static_cast<int>(face->hinting()));
        // LLFontFreetype::renderGlyph loads with (FT_Int32) of the hinting
        // the face was loaded with.
        CHECK(hb_flags == static_cast<int>(static_cast<FT_Int32>(hinting)));
    }

    // HB must see the same OT-VAR design coordinates that FT was
    // configured with. Without this, HB's GSUB/GPOS run at the
    // font's default axis values (typically wght=400) even when FT
    // renders varied outlines, breaking variation-aware kerning on
    // fonts like Inter at non-default weights.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: HB shapes at the variation design coordinates FT renders at", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "InterVariable.woff2";
        if (!fileExists(path))
            SKIP("InterVariable.woff2 not present");
        // Load with wght=600 so setVariationAxis sets a non-default
        // wght. opsz is auto-set from point_size whenever opsz_set is
        // false (load() handles both axes via the matching *_set
        // flag), so the verifier below sees a non-default opsz too.
        ALFontVarAxes va;
        va.wght = 600.f; va.wght_set = true;
        LLPointer<LLFontFreetype> ft = new LLFontFreetype;
        // A failed load leaves no face for the reads below.
        REQUIRE(ft->loadFace(path, /*point_size=*/14.f,
                            /*vert_dpi=*/96.f, /*horz_dpi=*/96.f,
                            /*is_fallback=*/true,
                            /*face_n=*/0, EFontHinting::DEFAULT,
                            /*flags=*/0, va));

        const ALFontFace* face = ft->getFontFace();
        REQUIRE(face != nullptr);
        FT_Face ftf = face->face();
        hb_font_t* hbf = face->getHbFont();
        REQUIRE(hbf != nullptr);

        FT_MM_Var* mm = nullptr;
        CHECK(FT_Get_MM_Var(ftf, &mm) == 0);
        REQUIRE(mm != nullptr);
        // Freed however the case ends, a failed REQUIRE below included.
        const std::unique_ptr<FT_MM_Var, void (*)(FT_MM_Var*)> mm_owner(
            mm, [](FT_MM_Var* p) { FT_Done_MM_Var(gFTLibrary, p); });
        const FT_UInt num_axis = mm->num_axis;
        CHECK(num_axis >= 1u);

        std::vector<FT_Fixed> ft_coords(num_axis);
        CHECK(FT_Get_Var_Design_Coordinates(ftf, num_axis, ft_coords.data()) == 0);

        unsigned int hb_len = 0;
        const float* hb_coords = hb_font_get_var_coords_design(hbf, &hb_len);
        REQUIRE(hb_coords != nullptr);
        // The loop below reads hb_coords[i] for every FT axis.
        REQUIRE(hb_len == (unsigned int)num_axis);

        for (FT_UInt i = 0; i < num_axis; ++i)
        {
            CAPTURE(i);
            const float ft_design = ft_coords[i] / 65536.0f;  // 16.16 -> design
            const float delta = std::fabs(hb_coords[i] - ft_design);
            // 16.16 round-trip: equality should be exact, but allow
            // 1/65536 to cover any internal float-conversion rounding.
            REQUIRE(delta < (1.f / 65536.f) + 1e-6f);
        }
    }

    // Stem darkening must be disabled on every FT hinter module — the
    // renderer composites in sRGB space and FT's stem darkening assumes
    // linear-space compositing. autofitter is off by default in FT
    // 2.7+, but cff/type1 default ON; without explicit Property_Set on
    // those, a CFF font with EFontHinting::DEFAULT picks up unwanted
    // stem darkening.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: stem darkening is off on every FreeType module that has the property", "[ALFontShaping]")
    {
        REQUIRE(gFTLibrary != nullptr);

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
            CAPTURE(mod);
            REQUIRE(no_darken != 0);
        }
    }

    // FT_Select_Charmap(FT_ENCODING_UNICODE) must succeed on a normal
    // Unicode font — the resulting active charmap encoding should be
    // FT_ENCODING_UNICODE so all FT_Get_Char_Index lookups operate on
    // the Unicode cmap subtable, not whichever subtable FT auto-picked.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a Unicode font's active FreeType charmap is the Unicode one", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        const ALFontFace* face = ft->getFontFace();
        REQUIRE(face != nullptr);
        FT_Face ftf = face->face();
        REQUIRE(ftf->charmap != nullptr);
        CHECK((int)ftf->charmap->encoding == (int)FT_ENCODING_UNICODE);
    }

    // VS-15 strip parity with VS-16: a face that lacks U+FE0E in cmap
    // must still produce identical shape output for `<base, VS-15>`
    // vs just `<base>` — the do_shape strip pass should drop VS-15
    // before HB so the notdef glyph for VS-15 doesn't sit between
    // base and follow-up codepoints. Mirrors the VS-16 strip case's proof.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a VS-15 the cmap lacks is stripped as VS-16 is", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "Noto-COLRv1.ttf";
        if (!fileExists(path))
            SKIP("Noto-COLRv1.ttf not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // Sanity: the test only verifies anything if Noto-COLRv1 also
        // lacks VS-15. Color emoji fonts typically don't ship VS-15.
        if (ft->getCharGlyphIndex(0xFE0E) != 0u)
            SKIP("Noto-COLRv1 unexpectedly has U+FE0E in cmap");

        const Text with_vs    = text(0x2764, 0xFE0E);
        const Text without_vs = text(0x2764);

        std::vector<ALShapedGlyph> with_out;
        std::vector<ALShapedGlyph> without_out;
        ALFontShaping::shapeRun(ft, with_vs,    0, with_vs.size(),    with_out);
        ALFontShaping::shapeRun(ft, without_vs, 0, without_vs.size(), without_out);

        REQUIRE(with_out.size() == without_out.size());
        REQUIRE_FALSE(with_out.empty());
        for (size_t i = 0; i < with_out.size(); ++i)
        {
            CAPTURE(i);
            REQUIRE(with_out[i].glyph_id == without_out[i].glyph_id);
        }
    }

    // Identity check between codepoint→glyph (cmap) and shape result
    // for a non-Latin codepoint that DejaVuSans covers. The ASCII case
    // covers ASCII; this extends the contract to the upper BMP so a regression
    // limited to Unicode-cmap binding (Defect B in the audit plan) is
    // caught even when ASCII still works.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: a non-ASCII codepoint shapes to the glyph the cmap gives it", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

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
            SKIP("no non-ASCII codepoint covered in DejaVuSans");

        const Text s = text(wch);
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE(out.size() == 1u);
        CHECK(out[0].glyph_id == ft->getCharGlyphIndex(wch));
        CHECK(out[0].x_advance > 0.f);
    }

    // Cluster index invariant: every glyph emitted by shapeRun has
    // cluster ∈ [begin, end). The producer-side clamp in shape_sub_run
    // protects downstream consumers (firstDrawableByte, maxDrawableBytes)
    // that index per-cluster advances by cluster. ZWJ-retry candidates
    // and corrupt GSUB tables can synthesize cluster values outside the
    // input range; the clamp pins them back into the slice.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: every cluster shapeRun emits lies within the slice it shaped", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // begin/end are written as codepoint indices and converted per case.
        struct Case { Text s; size_t begin; size_t end; };
        const std::vector<Case> cases = {
            { text('a','b','c'),                          0, 3 },
            { text('A','V','A','W','T','o','L','T'),      0, 8 },
            { text('h','e','l','l','o',' ','w','o','r','l','d'), 0, 11 },
            { text('a','b','c','d','e','f'),              2, 5 },
            // VS-16 / ZWJ extenders interleaved with Latin — exercises
            // the strip+rebase path even on a face that covers them.
            { text('A', 0xFE0F, 'B', 0x200D, 'C'),        0, 5 },
        };

        for (size_t ci = 0; ci < cases.size(); ++ci)
        {
            CAPTURE(ci);
            const auto& c = cases[ci];
            const size_t begin_bytes = c.s.at(c.begin);
            const size_t end_bytes   = c.s.at(c.end);
            std::vector<ALShapedGlyph> out;
            ALFontShaping::shapeRun(ft, c.s, begin_bytes, end_bytes, out);
            for (const auto& g : out)
            {
                REQUIRE(g.cluster >= (S32)begin_bytes);
                REQUIRE(g.cluster < (S32)end_bytes);
            }
        }
    }

    // Cluster atomicity: every glyph emitted by the cluster fast path
    // must carry the cluster's start codepoint as its cluster ID, not
    // a per-glyph ID HarfBuzz hands back when the face fails to ligate.
    // byteFromPixelOffset feeds sg.cluster directly into the cursor
    // position it returns for round=true mid-glyph hit-tests; a
    // mid-cluster cluster value lands the cursor inside the cluster
    // and oscillates the drag-select highlight rect across the
    // cluster's interior. Force a multi-glyph case by routing the
    // trans-flag ZWJ sequence through DejaVuSans alone (no emoji
    // fallback) — DejaVu has ZWJ but lacks the astral pictographs and
    // the GSUB rule for the composed flag, so HB emits one glyph per
    // covered codepoint plus notdefs.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: every glyph of an unligated emoji cluster carries the cluster's start", "[ALFontShaping]")
    {
        const std::string path = std::string(kFontDir) + "DejaVuSans.woff2";
        if (!fileExists(path))
            SKIP("DejaVuSans.woff2 not present");
        LLPointer<LLFontFreetype> ft = loadFt(path);
        REQUIRE(ft.notNull());

        // "X" + trans flag (5 cps, [1, 6)) + "Y". The cluster walker
        // identifies [1, 6) as one emoji cluster; the fast path shapes it
        // as a cluster sub-run on DejaVu, which can't ligate the
        // sequence. Without cluster atomicity, HB hands back glyph
        // cluster IDs 1..5 — the bug we're pinning.
        const Text s = text(L'X', 0x1F3F3, 0xFE0F, 0x200D, 0x26A7,
                           0xFE0F, L'Y');
        std::vector<ALShapedGlyph> out;
        ALFontShaping::shapeRun(ft, s, 0, s.size(), out);
        REQUIRE_FALSE(out.empty());

        // Every glyph whose cluster falls inside the flag must report the
        // cluster's start. Glyphs for X and Y are unaffected.
        const S32 flag_begin = (S32)s.at(1);
        const S32 flag_end   = (S32)s.at(6);
        bool saw_cluster_glyph = false;
        for (const auto& g : out)
        {
            if (g.cluster >= flag_begin && g.cluster < flag_end)
                saw_cluster_glyph = true;
            CAPTURE(g.cluster, flag_begin, flag_end);
            REQUIRE_FALSE((g.cluster > flag_begin && g.cluster < flag_end));
        }
        UNSCOPED_INFO("at least one glyph routed through the cluster sub-run "
               "(otherwise this test isn't exercising the fast path)");
        CHECK(saw_cluster_glyph);
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
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: addFallbackFont drops cached shapes so the new fallback is used", "[ALFontShaping]")
    {
        const std::string a_path = std::string(kFontDir) + "DejaVuSans.woff2";
        const std::string b_path = std::string(kFontDir) + "SourceHanSans-VF.otf.ttc";
        if (!fileExists(a_path) || !fileExists(b_path))
            SKIP("DejaVuSans + SourceHanSans required");

        LLPointer<LLFontFreetype> a = loadFt(a_path);
        LLPointer<LLFontFreetype> b = loadFt(b_path);
        REQUIRE(a.notNull());
        REQUIRE(b.notNull());

        // Pre-fallback shape: U+4F60 (你) is not in DejaVuSans's cmap, so
        // HB returns one notdef glyph (glyph_id=0) on face A.
        const Text s = text(0x4F60);
        std::vector<ALShapedGlyph> pre;
        ALFontShaping::shapeRun(a, s, 0, s.size(), pre);
        REQUIRE(pre.size() == 1u);
        CHECK(pre[0].face == a.get());
        CHECK(pre[0].glyph_id == 0u);
        UNSCOPED_INFO("pre-fallback shape leaves one cache entry");
        CHECK(ALFontShaping::cacheSize() == 1u);

        // Attach the CJK fallback. addFallbackFont must clear the cached
        // entry for `a` so the next shape() re-itemizes through the new
        // chain instead of returning the stale notdef.
        a->addFallbackFont(b);
        UNSCOPED_INFO("addFallbackFont(this) drops cached entries for this");
        CHECK(ALFontShaping::cacheSize() == 0u);

        // Post-fallback shape: U+4F60 should now route to face B with a
        // non-zero glyph_id (B has the CJK glyph in its cmap).
        std::vector<ALShapedGlyph> post;
        ALFontShaping::shapeRun(a, s, 0, s.size(), post);
        REQUIRE(post.size() == 1u);
        CHECK(post[0].face == b.get());
        CHECK(post[0].glyph_id != 0u);
    }

    // Two Unicode tables live in this process: HarfBuzz's own, which its
    // shapers consult, and ICU's, which every predicate in llcommon answers
    // from. Face selection reads both -- shape_all_sub_runs asks HarfBuzz
    // whether a codepoint is a mark and LLStringOps whether it is a pictograph
    // base -- so the two disagreeing would put those branches at odds over the
    // same character.
    //
    // They agree today, across the whole range. This holds them to it: bumping
    // either package past the other's Unicode version should fail here rather
    // than quietly change how text is shaped.
    TEST_CASE_METHOD(alfontshaping_data, "ALFontShaping: HarfBuzz's and ICU's Unicode tables agree on every codepoint", "[ALFontShaping]")
    {
        auto icu_to_hb = [](int8_t t) -> hb_unicode_general_category_t
        {
            switch (t)
            {
            case U_UNASSIGNED:             return HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED;
            case U_UPPERCASE_LETTER:       return HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER;
            case U_LOWERCASE_LETTER:       return HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER;
            case U_TITLECASE_LETTER:       return HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER;
            case U_MODIFIER_LETTER:        return HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER;
            case U_OTHER_LETTER:           return HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER;
            case U_NON_SPACING_MARK:       return HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK;
            case U_ENCLOSING_MARK:         return HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK;
            case U_COMBINING_SPACING_MARK: return HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK;
            case U_DECIMAL_DIGIT_NUMBER:   return HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER;
            case U_LETTER_NUMBER:          return HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER;
            case U_OTHER_NUMBER:           return HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER;
            case U_SPACE_SEPARATOR:        return HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR;
            case U_LINE_SEPARATOR:         return HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR;
            case U_PARAGRAPH_SEPARATOR:    return HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR;
            case U_CONTROL_CHAR:           return HB_UNICODE_GENERAL_CATEGORY_CONTROL;
            case U_FORMAT_CHAR:            return HB_UNICODE_GENERAL_CATEGORY_FORMAT;
            case U_PRIVATE_USE_CHAR:       return HB_UNICODE_GENERAL_CATEGORY_PRIVATE_USE;
            case U_SURROGATE:              return HB_UNICODE_GENERAL_CATEGORY_SURROGATE;
            case U_DASH_PUNCTUATION:       return HB_UNICODE_GENERAL_CATEGORY_DASH_PUNCTUATION;
            case U_START_PUNCTUATION:      return HB_UNICODE_GENERAL_CATEGORY_OPEN_PUNCTUATION;
            case U_END_PUNCTUATION:        return HB_UNICODE_GENERAL_CATEGORY_CLOSE_PUNCTUATION;
            case U_CONNECTOR_PUNCTUATION:  return HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION;
            case U_OTHER_PUNCTUATION:      return HB_UNICODE_GENERAL_CATEGORY_OTHER_PUNCTUATION;
            case U_MATH_SYMBOL:            return HB_UNICODE_GENERAL_CATEGORY_MATH_SYMBOL;
            case U_CURRENCY_SYMBOL:        return HB_UNICODE_GENERAL_CATEGORY_CURRENCY_SYMBOL;
            case U_MODIFIER_SYMBOL:        return HB_UNICODE_GENERAL_CATEGORY_MODIFIER_SYMBOL;
            case U_OTHER_SYMBOL:           return HB_UNICODE_GENERAL_CATEGORY_OTHER_SYMBOL;
            case U_INITIAL_PUNCTUATION:    return HB_UNICODE_GENERAL_CATEGORY_INITIAL_PUNCTUATION;
            case U_FINAL_PUNCTUATION:      return HB_UNICODE_GENERAL_CATEGORY_FINAL_PUNCTUATION;
            default:                       return HB_UNICODE_GENERAL_CATEGORY_UNASSIGNED;
            }
        };

        hb_unicode_funcs_t* uf = hb_unicode_funcs_get_default();
        S32 category_diffs = 0;
        S32 combining_diffs = 0;
        hb_codepoint_t first_category = 0;
        hb_codepoint_t first_combining = 0;

        for (hb_codepoint_t cp = 0; cp <= 0x10FFFF; ++cp)
        {
            if (hb_unicode_general_category(uf, cp) != icu_to_hb(u_charType((UChar32)cp)))
            {
                if (!category_diffs++)
                    first_category = cp;
            }
            if (hb_unicode_combining_class(uf, cp) != u_getCombiningClass((UChar32)cp))
            {
                if (!combining_diffs++)
                    first_combining = cp;
            }
        }

        UNSCOPED_INFO("HarfBuzz and ICU agree on general category"
                      " (first disagreement at U+" + llformat("%04X", first_category) + ")");
        CHECK(category_diffs == 0);
        UNSCOPED_INFO("HarfBuzz and ICU agree on combining class"
                      " (first disagreement at U+" + llformat("%04X", first_combining) + ")");
        CHECK(combining_diffs == 0);
    }
}
