/**
 * @file alfontshaping.cpp
 * @brief HarfBuzz shaping for multi-codepoint emoji sequences.
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

#include "alfontshaping.h"
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
        std::vector<ALShapedGlyph> glyphs;
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

    // Bumped by every mutation that can invalidate a reference returned by
    // shapeLine (miss-insert + its evictions, clears). LRU splices on cache
    // hit don't count — they never touch the index or a glyph vector. See
    // cacheMutationCount() in the header for the holder-side contract.
    size_t sShapeCacheMutations = 0;

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
                       std::vector<ALShapedGlyph>&  out_glyphs)
    {
        if (!face || sub_begin_in_slice >= sub_end_in_slice)
            return;

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

        // Monospace feature overrides. Two profiles:
        //
        //  - Strict (default for monospace faces): kern off (pair kerning
        //    breaks columns) AND every ligature lookup off (liga / calt /
        //    clig / dlig / rlig). Without these, GSUB collapses sequences
        //    like `==` `!=` `=>` into a single glyph spanning multiple
        //    cells, throwing off column count. mark / mkmk / ccmp stay on
        //    so combining marks still position correctly above their base
        //    via GPOS — that's the GPOS that monospace actually wants.
        //  - Ligatures-OK (opt-in via <font ligatures="on">): kern off,
        //    ligatures allowed. For programmer fonts where the user wants
        //    `=>` `!=` to render as designed; the user accepts that
        //    ligated runs no longer have one cell per codepoint.
        //
        // Variable-width faces get no feature override.
        static const hb_feature_t kFixedWidthStrict[] = {
            { HB_TAG('k','e','r','n'), 0, 0, (unsigned)-1 },
            { HB_TAG('l','i','g','a'), 0, 0, (unsigned)-1 },
            { HB_TAG('c','a','l','t'), 0, 0, (unsigned)-1 },
            { HB_TAG('c','l','i','g'), 0, 0, (unsigned)-1 },
            { HB_TAG('d','l','i','g'), 0, 0, (unsigned)-1 },
            { HB_TAG('r','l','i','g'), 0, 0, (unsigned)-1 },
        };
        static const hb_feature_t kFixedWidthLigaturesOk[] = {
            { HB_TAG('k','e','r','n'), 0, 0, (unsigned)-1 },
        };
        const hb_feature_t* features = nullptr;
        unsigned int num_features = 0;
        if (face->isFixedWidth())
        {
            if (face->getAllowMonospaceLigatures())
            {
                features = kFixedWidthLigaturesOk;
                num_features = (unsigned int)(sizeof(kFixedWidthLigaturesOk) / sizeof(kFixedWidthLigaturesOk[0]));
            }
            else
            {
                features = kFixedWidthStrict;
                num_features = (unsigned int)(sizeof(kFixedWidthStrict) / sizeof(kFixedWidthStrict[0]));
            }
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
            // VS-15 (U+FE0E, text presentation) and VS-16 (U+FE0F, emoji
            // presentation) both need stripping when the chosen face's cmap
            // lacks them. Otherwise HB shapes them as a notdef glyph that
            // sits between cluster bases and adjacent ZWJ / keycap parts and
            // prevents ligature lookups from matching (the rule's components
            // are heart + ZWJ + fire, not heart + .notdef + ZWJ + fire). The
            // cluster fast path's pick_cluster_face already treats both
            // selectors as strippable for coverage decisions; this aligns
            // do_shape with that policy.
            const bool strip_vs15 = !shape_face->faceHasGlyph((llwchar)0xFE0E);
            const bool strip_vs16 = !shape_face->faceHasGlyph((llwchar)0xFE0F);
            if (strip_vs15 || strip_vs16)
            {
                stripped_buf.clear();
                cluster_back_map.clear();
                stripped_buf.reserve(len);
                cluster_back_map.reserve(len);
                for (int k = 0; k < len; ++k)
                {
                    if ((strip_vs15 && codepoints[k] == 0xFE0E)
                     || (strip_vs16 && codepoints[k] == 0xFE0F))
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
            ALShapedGlyph sg;
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
            // ZWJ-retry candidates and corrupt GSUB tables can produce cluster
            // values outside [0, len). Clamp at the producer so consumers can
            // index into the original slice without per-site bounds checks.
            src_cluster  = llclamp(src_cluster, 0, len - 1);
            sg.cluster   = cluster_base + src_cluster;
            sg.x_advance = positions[i].x_advance * INV_64;
            sg.y_advance = positions[i].y_advance * INV_64;
            sg.x_offset  = positions[i].x_offset  * INV_64;
            sg.y_offset  = positions[i].y_offset  * INV_64;
            out_glyphs.push_back(sg);
        }
    }

    // Pick a single face to own an entire emoji cluster. The cluster walker
    // (wstring_find_emoji_clusters) has already certified [cb, ce) as one
    // cluster — keycap, ZWJ sequence, regional indicator pair, subdivision
    // flag, or astral + skin-tone modifier. Routing the whole cluster to
    // one face guarantees HarfBuzz sees every codepoint of the sequence in
    // one buffer, which is the only way GSUB can collapse keycap/ZWJ
    // ligatures into the composed glyph.
    //
    // Heuristic:
    //   1. Walk cluster codepoints, collect the non-root candidates that
    //      selectShapingFace points at (typically just one — the emoji
    //      face — but a multi-fallback chain may produce more).
    //   2. Prefer the first candidate that COVERS the cluster's
    //      non-strippable codepoints in cmap (VS-15 / VS-16 are
    //      strippable: shape_sub_run rebuilds the buffer without them
    //      when the face's cmap lacks them, and GSUB rules in modern
    //      emoji fonts are written against the VS-stripped form). A
    //      face that lacks the cluster's base ('#' for "#️⃣", the
    //      heart for "❤️‍🔥") would produce notdef on the base and
    //      block the keycap / ZWJ rule from matching at all.
    //   3. Fall back to root_face if no candidate covers — at least
    //      the base codepoints render via root's cmap, even if the
    //      ligature can't compose. Better than rendering tofu on a
    //      face that can't honor the cluster.
    const LLFontFreetype* pick_cluster_face(const LLFontFreetype* root_face,
                                            std::u32string_view   slice,
                                            size_t                cb,
                                            size_t                ce)
    {
        auto covers_non_strippable = [&](const LLFontFreetype* f) {
            for (size_t k = cb; k < ce; ++k)
            {
                const llwchar c = slice[k];
                if (c == 0xFE0E || c == 0xFE0F)
                    continue; // VS-15/16 strippable in shape_sub_run
                if (!f->faceHasGlyph(c))
                    return false;
            }
            return true;
        };

        // First non-root candidate with full coverage.
        for (size_t k = cb; k < ce; ++k)
        {
            U32 unused = 0;
            const LLFontFreetype* f = root_face->selectShapingFace(slice[k], unused);
            if (f && f != root_face && covers_non_strippable(f))
                return f;
        }
        // Root may itself cover the cluster (e.g. when the head IS an
        // emoji-aware font, or when the cluster is BMP-only and the
        // text font has the relevant glyphs).
        if (covers_non_strippable(root_face))
            return root_face;
        // No face has full coverage. Returning root keeps the base
        // codepoints visible and lets HB produce notdef for the rest;
        // the alternative (routing to a face that lacks the base)
        // produces notdef on the base AND on the trailing extender.
        return root_face;
    }

    // Itemize [begin, end) into contiguous sub-runs whose codepoints share
    // both an owning face and a Unicode script. Within an emoji cluster
    // (per wstring_find_emoji_clusters) the whole cluster rides on one
    // face chosen by pick_cluster_face — that's what keeps a keycap like
    // 9️⃣ on the emoji face that can compose '9' + U+FE0F + U+20E3 into
    // the composed glyph instead of fragmenting the digit off onto root.
    // Outside clusters, face boundaries follow per-codepoint
    // selectShapingFace. Script boundaries give HarfBuzz the right script
    // tag per buffer so GPOS lookups under script-specific feature lists
    // ('latn', 'cyrl', etc.) fire correctly. COMMON/INHERITED codepoints
    // (spaces, punctuation, VS selectors, ZWJ) inherit the surrounding
    // script and never trigger a boundary by themselves; this keeps
    // "Hello, world!" as one run with script LATIN rather than fragmenting
    // at every comma.
    void shape_all_sub_runs(const LLFontFreetype* root_face,
                            std::u32string_view   slice,
                            std::vector<ALShapedGlyph>& out_glyphs)
    {
        const size_t n = slice.size();
        if (!root_face || n == 0)
            return;

        hb_unicode_funcs_t* uf = hb_unicode_funcs_get_default();

        // Authoritative emoji cluster boundaries — the single source of truth
        // for "what counts as one emoji glyph in this slice" (see
        // project_emoji_cluster_walker_single_source). Drives the cluster
        // fast path inside the loop.
        const EmojiClusterList clusters = wstring_find_emoji_clusters(slice);
        size_t next_cluster_idx = 0;

        const LLFontFreetype* cur_face   = nullptr;
        hb_script_t           cur_script = HB_SCRIPT_INVALID;  // unknown until first non-neutral cp
        size_t                cur_begin  = 0;
        // True when the current run is (or begins with) an emoji cluster.
        // Drives the keeper's decision to retain emoji extenders on cur_face
        // even when cur_face's cmap doesn't carry the extender's glyph (e.g.
        // Noto-COLRv1 lacks VS-16 by design). The cluster fast path sets
        // this for every cluster it routes, so the per-codepoint logic below
        // only ever sees it false on isolated codepoints.
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
            // Cluster fast path: the cluster walker has identified [cb, ce)
            // as one emoji cluster — route it to a single face so HarfBuzz
            // can collapse the sequence in one buffer. Without this, e.g.
            // "9️⃣" fragments into '9' on root + VS-16+U+20E3 on the emoji
            // face, producing a digit followed by an unbound keycap mark.
            //
            // Each cluster shapes as its OWN sub-run (separate HarfBuzz
            // buffer), even when adjacent clusters route to the same face.
            // Merging them into one buffer would let GSUB rules match
            // across cluster boundaries — e.g. "3️⃣🏳️‍🌈" (keycap +
            // pride flag) on Noto-COLRv1 produced spurious matches that
            // flickered the pride flag down to a bare white flag.
            if (next_cluster_idx < clusters.size()
                && clusters[next_cluster_idx].first == i)
            {
                const size_t cb = clusters[next_cluster_idx].first;
                const size_t ce = clusters[next_cluster_idx].second;
                ++next_cluster_idx;

                const LLFontFreetype* cluster_face =
                    pick_cluster_face(root_face, slice, cb, ce);

                // First non-neutral script in the cluster, COMMON otherwise.
                hb_script_t cluster_script = HB_SCRIPT_COMMON;
                for (size_t k = cb; k < ce; ++k)
                {
                    const hb_script_t s = hb_unicode_script(uf, slice[k]);
                    if (s != HB_SCRIPT_COMMON && s != HB_SCRIPT_INHERITED)
                    {
                        cluster_script = s;
                        break;
                    }
                }

                // Flush any in-progress non-cluster run before the cluster.
                if (cur_face != nullptr)
                {
                    emit(cb);
                }

                // Emit the cluster as its own sub-run. Reset cur_face so
                // the next codepoint (whether ASCII or another cluster)
                // starts a fresh run via the per-codepoint init branch.
                // cur_run_is_emoji is intentionally not set here — it
                // only matters across codepoint iterations (it gates
                // the orphan-extender keep below), and we reset to a
                // fresh run immediately. Per-codepoint init re-derives
                // it from isPictographBase on the next non-cluster cp.
                cur_face  = cluster_face;
                cur_begin = cb;
                cur_script = cluster_script;
                emit(ce);
                cur_face = nullptr;
                cur_script = HB_SCRIPT_INVALID;

                i = ce - 1; // for-loop's ++i will land on ce
                continue;
            }

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

            // ORPHAN-EXTENDER SAFETY NET. Real emoji clusters are routed
            // atomically by the cluster fast path above (driven off
            // wstring_find_emoji_clusters), so ZWJ / VS / skin-tone /
            // keycap-combiner codepoints inside a cluster never reach
            // this branch — they were emit'd as part of the cluster's
            // own sub-run. What this code handles:
            //
            //   * Combining marks on non-emoji bases (e.g. Latin + diacritic
            //     where the head doesn't cover the mark). They must stay
            //     in the same HarfBuzz buffer as their preceding base for
            //     mark-to-base GPOS attachment to fire.
            //   * Orphan / malformed emoji extenders — a stray U+FE0F
            //     after a Latin letter, an isolated U+20E3, etc. The
            //     cluster walker doesn't recognize these as clusters,
            //     so they reach the per-codepoint loop. Keeping them on
            //     cur_face when it covers them gives the best-effort
            //     rendering for malformed input without splitting the
            //     codepoint across sub-runs.
            const llwchar wch = slice[i];
            const auto cat = hb_unicode_general_category(uf, wch);
            const bool is_mark = (cat == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK
                               || cat == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK
                               || cat == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK);
            // ZWJ is added on top of LLStringOps::isEmojiClusterExtender's
            // set: the cluster walker excludes ZWJ (it joins to a *next
            // base*, not as a trailing modifier) but the shape itemizer
            // wants to keep ZWJ glued to cur_face so HarfBuzz can ligate
            // the cluster across joiners.
            const bool is_emoji_extender = wch == 0x200D
                                        || LLStringOps::isEmojiClusterExtender(wch);
            if ((is_mark || is_emoji_extender) && face != cur_face)
            {
                if (cur_face->faceHasGlyph(wch))
                {
                    // cur_face covers the joiner — shape it inline with the base.
                    face = cur_face;
                }
                else if (is_emoji_extender && cur_run_is_emoji)
                {
                    // Orphan extender after a non-cluster pictograph base:
                    // the run started on an isolated pictograph (single-
                    // codepoint emoji that the walker didn't promote to a
                    // cluster), and a malformed extender follows. Keep
                    // the extender on cur_face so it doesn't split off
                    // onto root and render as a bare combining mark.
                    // shape_sub_run handles VS-16 stripping when cur_face's
                    // cmap lacks it, so the buffer reaches GSUB cleanly.
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

const std::vector<ALShapedGlyph>& ALFontShaping::shapeLine(
    const LLFontFreetype* root_face,
    LLWStringView         wstr,
    size_t                begin,
    size_t                end)
{
    // The shape path mutates global LRU/index state with no synchronization;
    // a stray call from a worker thread would corrupt the cache silently.
    llassert(on_main_thread());

    static const std::vector<ALShapedGlyph> sEmpty;

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
    std::vector<ALShapedGlyph> shaped;
    shape_all_sub_runs(root_face, slice, shaped);

    ShapeCacheKey key;
    key.codepoints.assign(slice.data(), slice.size());
    key.root_face = root_face;

    // One bump covers the insert and any evictions below — either way,
    // previously returned references may now dangle.
    ++sShapeCacheMutations;

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

void ALFontShaping::shapeRun(const LLFontFreetype* root_face,
                             LLWStringView         wstr,
                             size_t                begin,
                             size_t                end,
                             std::vector<ALShapedGlyph>& out_glyphs)
{
    out_glyphs.clear();

    const auto& cached = shapeLine(root_face, wstr, begin, end);
    if (cached.empty())
        return;

    out_glyphs.reserve(cached.size());
    const S32 base = static_cast<S32>(begin);
    for (const ALShapedGlyph& sg : cached)
    {
        ALShapedGlyph out = sg;
        out.cluster += base;
        out_glyphs.push_back(out);
    }
}

void ALFontShaping::clearCache()
{
    if (!sShapeIndex.empty())
        ++sShapeCacheMutations;
    sShapeIndex.clear();
    sShapeLru.clear();
}

size_t ALFontShaping::cacheSize()
{
    return sShapeIndex.size();
}

size_t ALFontShaping::cacheMutationCount()
{
    return sShapeCacheMutations;
}

void ALFontShaping::clearCacheForFace(const LLFontFreetype* face)
{
    if (!face)
        return;

    // Walk the index — its iterator gives us O(1) erase from the LRU via
    // the back-reference stored in each entry. Walking the LRU instead
    // would force a per-key index lookup on every match.
    bool erased_any = false;
    for (auto it = sShapeIndex.begin(); it != sShapeIndex.end(); )
    {
        bool references_face = (it->first.root_face == face);
        if (!references_face)
        {
            // Entries rooted at OTHER heads can still hold this face inside
            // their glyph runs: every fallback-sourced glyph stores a raw
            // pointer to the fallback that owns its glyph_id. Sweep those
            // too so a dying face can't leave sibling-rooted entries whose
            // sg.face dangles — see the header note on clearCacheForFace.
            // O(glyphs) per surviving entry. This runs on face teardown /
            // reload, and once per codepoint that first resolves through a
            // lazily-attached OS fallback — never per shape or per render.
            for (const ALShapedGlyph& sg : it->second.glyphs)
            {
                if (sg.face == face)
                {
                    references_face = true;
                    break;
                }
            }
        }
        if (references_face)
        {
            sShapeLru.erase(it->second.lru_pos);
            it = sShapeIndex.erase(it);
            erased_any = true;
        }
        else
        {
            ++it;
        }
    }
    if (erased_any)
        ++sShapeCacheMutations;
}
