/**
 * @file llfontshaping.cpp
 * @brief HarfBuzz shaping for multi-codepoint emoji sequences.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llfontshaping.h"
#include "llfontfreetype.h"
#include "llstring.h"  // LLStringOps::isPictographBase
#include "llthread.h"

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

#include <hb.h>

#include <list>
#include <string>
#include <string_view>

namespace
{
    // LRU cache for shaped runs. Keyed by the codepoint sequence plus the
    // owning face — direction/script/language are fixed in shapeRun(), so
    // they don't need to participate in the key. Clusters in the cached
    // glyphs are stored in *slice-local* coordinates (begin=0) and rebased
    // to original-wstr coords at copy-out time, so the same codepoint
    // sequence in different wstring positions can share an entry.
    //
    // The index map owns the canonical key; the LRU list holds a pointer
    // back into the index so lookups don't store the key twice. Pointer
    // stability across rehash is guaranteed by boost::unordered_map (node-
    // based). Iterators may invalidate on rehash, so we store key pointers
    // and re-find when erasing — the LRU never holds a map iterator.
    struct ShapeCacheKey
    {
        std::u32string        codepoints;
        // The root face alone determines itemization (fallback chain) and
        // therefore the shaped output for a given slice; per-codepoint face
        // selection is deterministic downstream.
        const LLFontFreetype* root_face;
    };

    // Borrowed counterpart used for cache lookup so hits don't allocate a
    // u32string just to compute the hash.
    struct ShapeCacheKeyView
    {
        std::u32string_view   codepoints;
        const LLFontFreetype* root_face;
    };

    struct ShapeCacheKeyHash
    {
        // is_transparent enables boost::unordered_map's heterogeneous find,
        // so ShapeCacheKeyView can probe a map keyed on ShapeCacheKey.
        using is_transparent = void;

        size_t operator()(const ShapeCacheKey& k) const noexcept
        {
            return hash_impl(std::u32string_view(k.codepoints), k.root_face);
        }
        size_t operator()(const ShapeCacheKeyView& k) const noexcept
        {
            return hash_impl(k.codepoints, k.root_face);
        }
    private:
        // Always hash through the string_view path so both overloads agree
        // bit-for-bit — std::hash<u32string> and std::hash<u32string_view>
        // are required to produce the same value, but routing both through
        // the view variant removes any platform doubt.
        static size_t hash_impl(std::u32string_view sv, const LLFontFreetype* face) noexcept
        {
            size_t h = std::hash<std::u32string_view>{}(sv);
            boost::hash_combine(h, face);
            return h;
        }
    };

    struct ShapeCacheKeyEqual
    {
        using is_transparent = void;

        bool operator()(const ShapeCacheKey& a, const ShapeCacheKey& b) const noexcept
        {
            return a.root_face == b.root_face && a.codepoints == b.codepoints;
        }
        bool operator()(const ShapeCacheKey& a, const ShapeCacheKeyView& b) const noexcept
        {
            return a.root_face == b.root_face
                   && std::u32string_view(a.codepoints) == b.codepoints;
        }
        bool operator()(const ShapeCacheKeyView& a, const ShapeCacheKey& b) const noexcept
        {
            return a.root_face == b.root_face
                   && a.codepoints == std::u32string_view(b.codepoints);
        }
    };

    // ShapeLru holds non-owning pointers to keys stored inside ShapeIndex
    // entries. Forward-ordered for type dependencies: List depends on Key,
    // Entry depends on List, Index depends on Entry.
    using ShapeLru = std::list<const ShapeCacheKey*>;

    struct ShapeEntry
    {
        std::vector<LLShapedGlyph> glyphs;
        // Iterator into sShapeLru pointing at this entry's slot. List
        // iterators are stable across other-node insert/erase, so this
        // stays valid until the entry itself is erased.
        ShapeLru::iterator         lru_pos;
    };

    using ShapeIndex = boost::unordered_map<ShapeCacheKey, ShapeEntry,
                                            ShapeCacheKeyHash, ShapeCacheKeyEqual>;

    // Rough order-of-magnitude for an active chat window: a few dozen visible
    // lines each with 1-3 shaping runs, plus a few editor buffers. 8192
    // bounds worst-case memory (~1 MB) while absorbing realistic working
    // sets without eviction churn.
    constexpr size_t SHAPE_CACHE_LIMIT = 8192;

    ShapeLru   sShapeLru;
    ShapeIndex sShapeIndex;

    // Shape a single sub-run through its owning face and append the
    // resulting glyphs to `out_glyphs`. Clusters are written in wstr
    // coordinates relative to `sub_begin_in_slice` — i.e. local to the
    // cached slice, not the original caller's wstr — so the cache can
    // rebase them at copy-out time. Reserves/destroys its own hb_buffer.
    void shape_sub_run(const LLFontFreetype*        face,
                       const LLFontFreetype*        root_face,
                       std::u32string_view          slice,
                       size_t                       sub_begin_in_slice,
                       size_t                       sub_end_in_slice,
                       hb_script_t                  script,
                       std::vector<LLShapedGlyph>&  out_glyphs)
    {
        if (!face || sub_begin_in_slice >= sub_end_in_slice)
            return;

        // Strict monospace: bypass HarfBuzz entirely and synthesize one
        // glyph per codepoint using FT's canonical mXAdvance. HB's default
        // feature set has many always-on lookups (rclt, rlig, ccmp, locl,
        // mark, mkmk) that we can't disable via the features array, and
        // some fonts have non-zero positioning under those features that
        // breaks column alignment even after kern/liga/clig/dlig/calt are
        // off. Going direct to FT guarantees every glyph in the run gets
        // exactly the monospace cell width with no positioning drift.
        // Programmer fonts opting in via <font ligatures="on"> need real
        // shaping (for liga/calt) so they fall through to the HB path.
        //
        // Only valid when face is the head: getGlyphInfo asserts on
        // fallbacks (the chain walk is rooted at the head). When a fallback
        // face is itself fixed-width — e.g. SansSerif's DejaVuSansMono leg
        // resolving Letterlike Symbols like U+210C — fall through to HB.
        // Column alignment is already lost at the fallback boundary, so
        // taking the bypass here would offer no benefit even if it worked.
        if (face == root_face && face->isFixedWidth() && !face->getAllowMonospaceLigatures())
        {
            out_glyphs.reserve(out_glyphs.size() + (sub_end_in_slice - sub_begin_in_slice));
            for (size_t i = sub_begin_in_slice; i < sub_end_in_slice; ++i)
            {
                const llwchar wch = slice[i];
                const LLFontGlyphInfo* fgi = face->getGlyphInfo(wch, EFontGlyphType::Unspecified);
                if (!fgi)
                    continue;

                LLShapedGlyph sg;
                sg.face      = face;
                sg.glyph_id  = fgi->mGlyphIndex;
                sg.cluster   = static_cast<S32>(i);
                sg.x_advance = fgi->mXAdvance;
                sg.y_advance = 0.f;
                sg.x_offset  = 0.f;
                sg.y_offset  = 0.f;
                out_glyphs.push_back(sg);
            }
            return;
        }

        // Early-bail when the chosen face has no hb_font (allocation failure /
        // unloaded face). The retry loop below also rejects null hb_font per
        // candidate, so an empty primary face still produces an empty output
        // rather than dereffing a null pointer.
        if (!face->getHbFont())
            return;

        // Persistent shaping buffer reused across every sub-run. HarfBuzz
        // buffers retain their internal allocations across clear_contents,
        // so successive shape calls amortize the codepoint/glyph storage
        // and skip the create/destroy heap traffic that ran for every face
        // and script boundary. The shape path is main-thread only (see
        // header), so a thread_local pointer is just defensive — there's
        // never a second thread to contend. Leaks at process exit, which
        // is fine for a process-lifetime cache.
        static thread_local hb_buffer_t* sBuf = nullptr;
        if (!sBuf)
        {
            sBuf = hb_buffer_create();
            if (!sBuf)
            {
                // hb_buffer_create only fails on allocation failure; log
                // once so the empty-run fallback isn't completely silent
                // when the system is starved of memory.
                static thread_local bool sWarned = false;
                if (!sWarned)
                {
                    LL_WARNS("Font") << "hb_buffer_create returned null; shaping disabled" << LL_ENDL;
                    sWarned = true;
                }
                return;
            }
        }
        hb_buffer_t* buf = sBuf;
        hb_buffer_clear_contents(buf);

        const uint32_t* codepoints = reinterpret_cast<const uint32_t*>(slice.data() + sub_begin_in_slice);
        const int       len        = static_cast<int>(sub_end_in_slice - sub_begin_in_slice);

        // Programmer fonts opted into ligatures via <font ligatures="on">
        // still need kern disabled (monospace + GPOS pair-kerning is
        // fundamentally incompatible). Other features stay on so liga/
        // calt fire as the font intends for sequences like `=>` `!=`.
        static const hb_feature_t kFixedWidthLigaturesOk[] = {
            { HB_TAG('k','e','r','n'), 0, 0, (unsigned)-1 },
        };
        const hb_feature_t* features = nullptr;
        unsigned int num_features = 0;
        if (face->isFixedWidth() && face->getAllowMonospaceLigatures())
        {
            features = kFixedWidthLigaturesOk;
            num_features = (unsigned int)(sizeof(kFixedWidthLigaturesOk) / sizeof(kFixedWidthLigaturesOk[0]));
        }

        // VS-16 stripping for faces whose cmap lacks U+FE0F (notably Noto-
        // COLRv1, which uses 'ccmp' / pre-shape normalization in its design
        // and doesn't ship a cmap entry for VS-16). When VS-16 maps to
        // notdef in such a face, it sits in the buffer between the heart
        // and ZWJ glyphs and prevents the heart-on-fire ZWJ ligature rule
        // from matching (the rule's components are heart + ZWJ + fire,
        // not heart + .notdef + ZWJ + fire). Pre-stripping VS-16 lets the
        // ligature fire and produces a single glyph instead of two.
        //
        // Scratch buffers are thread_local so we don't allocate per
        // shape_sub_run call (one allocation per thread, amortized away
        // across all shape calls). Cleared and re-filled on each strip.
        // shape_sub_run is main-thread only per the header notes, but
        // thread_local is the same cost as static here and keeps the
        // assumption explicit.
        static thread_local std::vector<uint32_t> stripped_buf;
        static thread_local std::vector<int>      cluster_back_map;
        // True when stripping ran for the last do_shape call. Read by the
        // output loop to rebase HB's cluster values back to original input
        // positions via cluster_back_map.
        bool stripped_for_last_shape = false;

        // Helper: shape `codepoints` through `shape_face` and return the
        // resulting glyph count. The buffer ends up populated with the most
        // recent shape's output, so the caller reads infos/positions from
        // it after picking the winning face.
        auto do_shape = [&](const LLFontFreetype* shape_face) -> unsigned int
        {
            hb_font_t* hbf = shape_face ? shape_face->getHbFont() : nullptr;
            if (!hbf)
                return 0;

            const uint32_t* in_cps = codepoints;
            int             in_len = len;
            stripped_for_last_shape = false;
            if (!shape_face->faceHasGlyph((llwchar)0xFE0F))
            {
                stripped_buf.clear();
                cluster_back_map.clear();
                stripped_buf.reserve(len);
                cluster_back_map.reserve(len);
                for (int k = 0; k < len; ++k)
                {
                    if (codepoints[k] == 0xFE0F)
                        continue;
                    stripped_buf.push_back(codepoints[k]);
                    cluster_back_map.push_back(k);
                }
                if ((int)stripped_buf.size() != len)
                {
                    in_cps = stripped_buf.data();
                    in_len = (int)stripped_buf.size();
                    stripped_for_last_shape = true;
                }
            }

            hb_buffer_clear_contents(buf);
            // offset=0 so HB cluster values come back local to this sub-run;
            // we rebase below to the slice's coordinate system.
            hb_buffer_add_utf32(buf, in_cps, in_len, 0, in_len);
            hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
            hb_buffer_set_script(buf, script);
            hb_buffer_set_language(buf, hb_language_get_default());
            hb_shape(hbf, buf, features, num_features);
            unsigned int gc = 0;
            hb_buffer_get_glyph_infos(buf, &gc);
            return gc;
        };

        unsigned int glyph_count = do_shape(face);
        const LLFontFreetype* shape_face = face;

        // ZWJ-ligature retry: when the input contains U+200D and the chosen
        // face's GSUB didn't collapse the sequence (output count > 1), try
        // every other emoji-functor fallback whose charmap covers the base
        // codepoint. The first to produce strictly fewer glyphs wins —
        // typically the difference is "4 glyphs (heart, vs-16, zwj, fire)
        // unligatured" vs "1 glyph (heart-on-fire)". Skips when there's no
        // ZWJ (avoids wasted work on every shape) and when the run is
        // already a single glyph (already as ligated as possible). The
        // hb-buffer is reused by `do_shape`; whichever shape wins, the
        // buffer ends populated with that shape's output before we read
        // glyphs out below.
        bool has_zwj = false;
        for (int k = 0; k < len; ++k)
        {
            if (codepoints[k] == 0x200D) { has_zwj = true; break; }
        }
        if (has_zwj && glyph_count > 1)
        {
            unsigned int best_count = glyph_count;
            const LLFontFreetype* best_face = face;
            bool any_candidate_ran = false;
            const auto& fallbacks = root_face->getFallbackFonts();
            for (const auto& entry : fallbacks)
            {
                const LLFontFreetype* cand = entry.first.get();
                const auto& functor        = entry.second;
                if (!cand || cand == face)
                    continue;
                if (!functor || !functor((llwchar)codepoints[0]))
                    continue;
                if (!cand->faceHasGlyph((llwchar)codepoints[0]))
                    continue;
                const unsigned int cand_count = do_shape(cand);
                any_candidate_ran = true;
                if (cand_count > 0 && cand_count < best_count)
                {
                    best_count = cand_count;
                    best_face  = cand;
                }
            }
            // If candidates ran, the buffer holds the LAST candidate's output
            // (which may not be best_face). Re-shape with best_face so the
            // readout loop reads the winning face's glyphs. When no candidate
            // ran, the buffer still holds face's original output — skip the
            // extra HB call.
            if (any_candidate_ran)
            {
                glyph_count = do_shape(best_face);
                shape_face  = best_face;
            }
        }

        const hb_glyph_info_t*     infos     = hb_buffer_get_glyph_infos(buf, &glyph_count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

        out_glyphs.reserve(out_glyphs.size() + glyph_count);
        constexpr F32 INV_64 = 1.f / 64.f;
        const S32 cluster_base = static_cast<S32>(sub_begin_in_slice);
        for (unsigned int i = 0; i < glyph_count; ++i)
        {
            LLShapedGlyph sg;
            sg.face      = shape_face;
            sg.glyph_id  = infos[i].codepoint;
            // Rebase HB's cluster index. When VS-16 stripping ran for this
            // shape, HB sees a shorter input than the original; the output
            // cluster points into the stripped array and needs mapping back
            // to the original-index space cluster_back_map encodes.
            S32 src_cluster = static_cast<S32>(infos[i].cluster);
            if (stripped_for_last_shape
                && src_cluster >= 0
                && src_cluster < (S32)cluster_back_map.size())
            {
                src_cluster = cluster_back_map[src_cluster];
            }
            sg.cluster   = cluster_base + src_cluster;
            sg.x_advance = positions[i].x_advance * INV_64;
            sg.y_advance = positions[i].y_advance * INV_64;
            sg.x_offset  = positions[i].x_offset  * INV_64;
            sg.y_offset  = positions[i].y_offset  * INV_64;
            out_glyphs.push_back(sg);
        }
    }

    // Itemize [begin, end) into contiguous sub-runs whose codepoints share
    // both an owning face (as chosen by selectShapingFace) and a Unicode
    // script. Face boundaries let a keycap like 8️⃣ succeed: '8' lives in
    // the root face while U+FE0F and U+20E3 live in the emoji face. Script
    // boundaries give HarfBuzz the right script tag per buffer so GPOS
    // lookups under script-specific feature lists ('latn', 'cyrl', etc.)
    // fire correctly. COMMON/INHERITED codepoints (spaces, punctuation,
    // VS selectors, ZWJ) inherit the surrounding script and never trigger
    // a boundary by themselves; this keeps "Hello, world!" as one run with
    // script LATIN rather than fragmenting at every comma.
    void shape_all_sub_runs(const LLFontFreetype* root_face,
                            std::u32string_view   slice,
                            std::vector<LLShapedGlyph>& out_glyphs)
    {
        const size_t n = slice.size();
        if (!root_face || n == 0)
            return;

        hb_unicode_funcs_t* uf = hb_unicode_funcs_get_default();

        const LLFontFreetype* cur_face   = nullptr;
        hb_script_t           cur_script = HB_SCRIPT_INVALID;  // unknown until first non-neutral cp
        size_t                cur_begin  = 0;
        // True when the current run started on an emoji codepoint — i.e. the
        // run is "an emoji cluster" and cur_face is the chosen emoji handler.
        // Drives the keeper's decision to retain emoji extenders on cur_face
        // even when cur_face's cmap doesn't carry the extender's glyph (e.g.
        // Noto-COLRv1 lacks VS-16 by design). When false, the extender's own
        // selectShapingFace decision wins, so digit-keycap sequences like
        // "0️⃣" still fragment correctly off Inter and onto the emoji face.
        bool cur_run_is_emoji = false;

        auto set_cur_script_from = [&](hb_script_t cp_script, bool is_neutral)
        {
            cur_script = is_neutral ? HB_SCRIPT_INVALID : cp_script;
        };

        auto emit = [&](size_t end_excl) {
            // INVALID surfaces only when a run consists entirely of neutral
            // codepoints (e.g. a string of spaces); fall back to COMMON.
            const hb_script_t script = (cur_script == HB_SCRIPT_INVALID) ? HB_SCRIPT_COMMON
                                                                         : cur_script;
            shape_sub_run(cur_face, root_face, slice, cur_begin, end_excl, script, out_glyphs);
        };

        for (size_t i = 0; i < n; ++i)
        {
            U32 unused = 0;
            const LLFontFreetype* face = root_face->selectShapingFace(slice[i], unused);
            if (!face)
                face = root_face; // selectShapingFace never returns null, but defensive

            const hb_script_t cp_script = hb_unicode_script(uf, slice[i]);
            const bool is_neutral = (cp_script == HB_SCRIPT_COMMON
                                  || cp_script == HB_SCRIPT_INHERITED);

            if (cur_face == nullptr)
            {
                cur_face  = face;
                cur_begin = i;
                cur_run_is_emoji = LLStringOps::isPictographBase(slice[i]);
                set_cur_script_from(cp_script, is_neutral);
                continue;
            }

            // Combining marks must stay in the same HarfBuzz buffer as their
            // preceding base for mark-to-base GPOS attachment to fire — that's
            // what positions the mark above (or below) the base instead of
            // landing it standalone at the pen, which produces visible
            // collisions with the next base.
            //
            // Emoji-sequence extenders (ZWJ, VS15/16, skin-tone, keycap
            // combiner, tag chars) need the same treatment for a different
            // reason: HarfBuzz composes ZWJ ligatures (🧑 + ZWJ + 🚀 → 🧑‍🚀)
            // only when every codepoint of the sequence lands in one buffer
            // on a face whose GSUB knows the ligature. selectShapingFace
            // routes ZWJ/VS to root because they sit outside the emoji
            // functor's astral range, fragmenting the sequence and dropping
            // back to base + joiner-tofu + base. Keep them on cur_face when
            // it covers them so the emoji face's GSUB sees the whole cluster.
            const llwchar wch = slice[i];
            const auto cat = hb_unicode_general_category(uf, wch);
            const bool is_mark = (cat == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK
                               || cat == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK
                               || cat == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK);
            const bool is_emoji_extender =
                   wch == 0x200D                          // ZWJ
                || wch == 0xFE0E || wch == 0xFE0F         // VS15/VS16
                || wch == 0x20E3                          // keycap combiner
                || (wch >= 0x1F3FB && wch <= 0x1F3FF)     // skin-tone modifiers
                || (wch >= 0xE0020 && wch <= 0xE007F);    // tag characters
            if ((is_mark || is_emoji_extender) && face != cur_face)
            {
                if (cur_face->faceHasGlyph(wch))
                {
                    // cur_face covers the joiner — shape it inline with the base.
                    face = cur_face;
                }
                else if (is_emoji_extender && cur_run_is_emoji)
                {
                    // cur_face is the emoji handler for this run (the run
                    // started on a pictograph base) and lacks the extender
                    // in its cmap. Notable case: Noto-COLRv1 doesn't ship a
                    // cmap entry for U+FE0F (VS-16); its GSUB rules for ZWJ
                    // ligatures are written against the VS-stripped form
                    // (heart + ZWJ + fire, not heart + VS-16 + ZWJ + fire).
                    // Fragmenting at VS-16 would split the ligature across
                    // sub-runs and the rule could never match. Keep the
                    // extender on cur_face — shape_sub_run strips VS-16
                    // before passing the input to HB when the face's cmap
                    // lacks it, so the buffer reaches GSUB in the form the
                    // rules expect. Gating on cur_run_is_emoji (rather than
                    // cur_face != root_face) is what makes this work both
                    // when the head is a non-emoji font with an emoji
                    // fallback AND when the head IS an emoji font (e.g. the
                    // SansSerifEmoji family used by the emoji picker).
                    face = cur_face;
                }
                else if (is_mark)
                {
                    // cur_face lacks the mark glyph. Migrate the in-progress
                    // sub-run to the mark's face if it covers every base
                    // already collected; the bases re-render with a slightly
                    // different style but the mark gets correct positioning.
                    // Only worth doing for marks — for emoji extenders the
                    // emoji base sitting on cur_face is the whole point of
                    // routing it there, and the proposed `face` (root) by
                    // definition lacks the astral base.
                    //
                    // O(k) walk over the pending sub-run; bails on the first
                    // miss. Worst case O(n²) on combining-mark-heavy text
                    // where every mark fails — accepted because (a) such
                    // text is rare in practice and (b) a successful migration
                    // is a render-quality fix worth the cost.
                    bool mark_face_covers_sub_run = true;
                    for (size_t j = cur_begin; j < i; ++j)
                    {
                        if (!face->faceHasGlyph(slice[j]))
                        {
                            mark_face_covers_sub_run = false;
                            break;
                        }
                    }
                    if (mark_face_covers_sub_run)
                        cur_face = face;
                    // else: neither face covers everything — fall through to
                    // the face_change path. Mark will still render standalone,
                    // but with no better option available the split is the
                    // honest outcome.
                }
            }

            const bool face_change   = (face != cur_face);
            const bool script_change = !is_neutral
                                       && cur_script != HB_SCRIPT_INVALID
                                       && cp_script != cur_script;

            if (face_change || script_change)
            {
                emit(i);
                cur_face  = face;
                cur_begin = i;
                cur_run_is_emoji = LLStringOps::isPictographBase(slice[i]);
                set_cur_script_from(cp_script, is_neutral);
            }
            else if (cur_script == HB_SCRIPT_INVALID && !is_neutral)
            {
                // Adopt the first non-neutral script seen in this run so any
                // leading neutrals (spaces, punctuation) get the right tag.
                cur_script = cp_script;
            }
        }
        if (cur_face != nullptr)
            emit(n);
    }
}

const std::vector<LLShapedGlyph>& LLFontShaping::shapeLine(
    const LLFontFreetype* root_face,
    LLWStringView         wstr,
    size_t                begin,
    size_t                end)
{
    // The shape path mutates global LRU/index state with no synchronization;
    // a stray call from a worker thread would corrupt the cache silently.
    llassert(on_main_thread());

    static const std::vector<LLShapedGlyph> sEmpty;

    if (!root_face || begin >= end || end > wstr.size())
        return sEmpty;

    // Build the lookup view from the slice + root face. Itemization is
    // deterministic given (slice, root_face), so no need to encode the
    // per-codepoint face chain explicitly.
    std::u32string_view slice(wstr.data() + begin, end - begin);
    ShapeCacheKeyView lookup{slice, root_face};

    if (auto it = sShapeIndex.find(lookup); it != sShapeIndex.end())
    {
        sShapeLru.splice(sShapeLru.begin(), sShapeLru, it->second.lru_pos);
        return it->second.glyphs;
    }

    // Miss: itemize the slice into per-face sub-runs, shape each on its
    // owning face, and concatenate the glyph streams. Empty results are
    // cached so repeat misses don't re-shape on every frame.
    std::vector<LLShapedGlyph> shaped;
    shape_all_sub_runs(root_face, slice, shaped);

    ShapeCacheKey key;
    key.codepoints.assign(slice.data(), slice.size());
    key.root_face = root_face;

    auto [ins, inserted] = sShapeIndex.try_emplace(std::move(key));
    // Just-missed lookup means inserted is true; no duplicate to merge.
    sShapeLru.push_front(&ins->first);
    ins->second.glyphs  = std::move(shaped);
    ins->second.lru_pos = sShapeLru.begin();

    while (sShapeLru.size() > SHAPE_CACHE_LIMIT)
    {
        const ShapeCacheKey* tail = sShapeLru.back();
        sShapeLru.pop_back();
        sShapeIndex.erase(*tail);
    }

    return ins->second.glyphs;
}

void LLFontShaping::shapeRun(const LLFontFreetype* root_face,
                             LLWStringView         wstr,
                             size_t                begin,
                             size_t                end,
                             std::vector<LLShapedGlyph>& out_glyphs)
{
    out_glyphs.clear();

    const auto& cached = shapeLine(root_face, wstr, begin, end);
    if (cached.empty())
        return;

    out_glyphs.reserve(cached.size());
    const S32 base = static_cast<S32>(begin);
    for (const LLShapedGlyph& sg : cached)
    {
        LLShapedGlyph out = sg;
        out.cluster += base;
        out_glyphs.push_back(out);
    }
}

void LLFontShaping::clearCache()
{
    sShapeIndex.clear();
    sShapeLru.clear();
}

void LLFontShaping::clearCacheForFace(const LLFontFreetype* face)
{
    if (!face)
        return;

    // Walk the index — its iterator gives us O(1) erase from the LRU via
    // the back-reference stored in each entry. Walking the LRU instead
    // would force a per-key index lookup on every match.
    for (auto it = sShapeIndex.begin(); it != sShapeIndex.end(); )
    {
        if (it->first.root_face == face)
        {
            sShapeLru.erase(it->second.lru_pos);
            it = sShapeIndex.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
