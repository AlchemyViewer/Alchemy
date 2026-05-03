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
#include "llstring.h"

#include <boost/functional/hash.hpp>
#include <boost/unordered_map.hpp>

#include <hb.h>

#include <list>
#include <string>

namespace
{
    // LRU cache for shaped runs. Keyed by the codepoint sequence plus the
    // owning face — direction/script/language are fixed in shapeRun(), so
    // they don't need to participate in the key. Clusters in the cached
    // glyphs are stored in *slice-local* coordinates (begin=0) and rebased
    // to original-wstr coords at copy-out time, so the same codepoint
    // sequence in different wstring positions can share an entry.
    struct ShapeCacheKey
    {
        std::u32string        codepoints;
        // The root face alone determines itemization (fallback chain) and
        // therefore the shaped output for a given slice; per-codepoint face
        // selection is deterministic downstream.
        const LLFontFreetype* root_face;

        bool operator==(const ShapeCacheKey& o) const noexcept
        {
            return root_face == o.root_face && codepoints == o.codepoints;
        }
    };

    struct ShapeCacheKeyHash
    {
        size_t operator()(const ShapeCacheKey& k) const noexcept
        {
            size_t h = std::hash<std::u32string>{}(k.codepoints);
            boost::hash_combine(h, k.root_face);
            return h;
        }
    };

    using ShapeEntry    = std::pair<ShapeCacheKey, std::vector<LLShapedGlyph>>;
    using ShapeLru      = std::list<ShapeEntry>;
    using ShapeIndex    = boost::unordered_map<ShapeCacheKey, ShapeLru::iterator, ShapeCacheKeyHash>;

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
                       const LLWString&             slice,
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

        hb_font_t* hb_font = face->getHbFont();
        if (!hb_font)
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
                return;
        }
        hb_buffer_t* buf = sBuf;
        hb_buffer_clear_contents(buf);

        const uint32_t* codepoints = reinterpret_cast<const uint32_t*>(slice.data() + sub_begin_in_slice);
        const int       len        = static_cast<int>(sub_end_in_slice - sub_begin_in_slice);
        // offset=0 so HB cluster values come back local to this sub-run;
        // we rebase below to the slice's coordinate system.
        hb_buffer_add_utf32(buf, codepoints, len, 0, len);

        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_script(buf, script);
        hb_buffer_set_language(buf, hb_language_get_default());

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
        hb_shape(hb_font, buf, features, num_features);

        unsigned int glyph_count = 0;
        const hb_glyph_info_t*     infos     = hb_buffer_get_glyph_infos(buf, &glyph_count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

        out_glyphs.reserve(out_glyphs.size() + glyph_count);
        constexpr F32 INV_64 = 1.f / 64.f;
        const S32 cluster_base = static_cast<S32>(sub_begin_in_slice);
        for (unsigned int i = 0; i < glyph_count; ++i)
        {
            LLShapedGlyph sg;
            sg.face      = face;
            sg.glyph_id  = infos[i].codepoint;
            sg.cluster   = cluster_base + static_cast<S32>(infos[i].cluster);
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
                            const LLWString&      slice,
                            std::vector<LLShapedGlyph>& out_glyphs)
    {
        const size_t n = slice.size();
        if (!root_face || n == 0)
            return;

        hb_unicode_funcs_t* uf = hb_unicode_funcs_get_default();

        // Cluster-aware face routing pre-pass. wstring_find_emoji_clusters
        // identifies emoji shaping sequences (ZWJ families, keycaps, flags,
        // skin-toned bases, ❤️‍🔥-style BMP+astral compositions). For each
        // cluster, ask root_face for an emoji-fallback face whose charmap
        // covers every codepoint in the cluster — that's the face whose GSUB
        // can compose the ligature. selectShapingFace's per-codepoint walk
        // would otherwise route a BMP base like ❤ (U+2764) to root because
        // the emoji functor's astral range rejects it, splitting the cluster
        // across two faces and dropping the ligature. nullptr means no single
        // emoji face covers the cluster — leave per-codepoint routing alone.
        std::vector<const LLFontFreetype*> cluster_face(n, nullptr);
        const auto clusters = wstring_find_emoji_clusters(LLWStringView(slice.data(), n));
        for (const auto& [cb, ce] : clusters)
        {
            if (const LLFontFreetype* f = root_face->selectClusterFace(slice.data() + cb, ce - cb))
            {
                std::fill(cluster_face.begin() + cb, cluster_face.begin() + ce, f);
            }
        }

        const LLFontFreetype* cur_face   = nullptr;
        hb_script_t           cur_script = HB_SCRIPT_INVALID;  // unknown until first non-neutral cp
        size_t                cur_begin  = 0;

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
            const LLFontFreetype* face = cluster_face[i]
                ? cluster_face[i]
                : root_face->selectShapingFace(slice[i], unused);
            if (!face)
                face = root_face; // selectShapingFace never returns null, but defensive

            const hb_script_t cp_script = hb_unicode_script(uf, slice[i]);
            const bool is_neutral = (cp_script == HB_SCRIPT_COMMON
                                  || cp_script == HB_SCRIPT_INHERITED);

            if (cur_face == nullptr)
            {
                cur_face   = face;
                cur_begin  = i;
                cur_script = is_neutral ? HB_SCRIPT_INVALID : cp_script;
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
                cur_face   = face;
                cur_begin  = i;
                cur_script = is_neutral ? HB_SCRIPT_INVALID : cp_script;
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
    const LLWString&      wstr,
    size_t                begin,
    size_t                end)
{
    static const std::vector<LLShapedGlyph> sEmpty;

    if (!root_face || begin >= end || end > wstr.size())
        return sEmpty;

    // Build the cache key from the slice + root face. Itemization is
    // deterministic given (slice, root_face), so no need to encode the
    // per-codepoint face chain explicitly.
    ShapeCacheKey key;
    key.codepoints.assign(wstr.data() + begin, wstr.data() + end);
    key.root_face = root_face;

    if (auto it = sShapeIndex.find(key); it != sShapeIndex.end())
    {
        sShapeLru.splice(sShapeLru.begin(), sShapeLru, it->second);
        return it->second->second;
    }

    // Miss: itemize the slice into per-face sub-runs, shape each on its
    // owning face, and concatenate the glyph streams. Empty results are
    // cached so repeat misses don't re-shape on every frame.
    std::vector<LLShapedGlyph> shaped;
    shape_all_sub_runs(root_face, key.codepoints, shaped);

    sShapeLru.emplace_front(std::move(key), std::move(shaped));
    sShapeIndex.emplace(sShapeLru.front().first, sShapeLru.begin());

    while (sShapeLru.size() > SHAPE_CACHE_LIMIT)
    {
        sShapeIndex.erase(sShapeLru.back().first);
        sShapeLru.pop_back();
    }

    return sShapeLru.front().second;
}

void LLFontShaping::shapeRun(const LLFontFreetype* root_face,
                             const LLWString&      wstr,
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
