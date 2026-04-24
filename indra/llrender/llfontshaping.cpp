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
    // lines each with 1-3 shaping runs, plus a few editor buffers. 2048
    // bounds worst-case memory (~250 KB) while absorbing realistic working
    // sets without eviction churn.
    constexpr size_t SHAPE_CACHE_LIMIT = 2048;

    ShapeLru   sShapeLru;
    ShapeIndex sShapeIndex;

    // Shape a single sub-run through its owning face and append the
    // resulting glyphs to `out_glyphs`. Clusters are written in wstr
    // coordinates relative to `sub_begin_in_slice` — i.e. local to the
    // cached slice, not the original caller's wstr — so the cache can
    // rebase them at copy-out time. Reserves/destroys its own hb_buffer.
    void shape_sub_run(const LLFontFreetype*        face,
                       const LLWString&             slice,
                       size_t                       sub_begin_in_slice,
                       size_t                       sub_end_in_slice,
                       std::vector<LLShapedGlyph>&  out_glyphs)
    {
        if (!face || sub_begin_in_slice >= sub_end_in_slice)
            return;
        hb_font_t* hb_font = face->getHbFont();
        if (!hb_font)
            return;
        hb_buffer_t* buf = hb_buffer_create();
        if (!buf)
            return;

        const uint32_t* codepoints = reinterpret_cast<const uint32_t*>(slice.data() + sub_begin_in_slice);
        const int       len        = static_cast<int>(sub_end_in_slice - sub_begin_in_slice);
        // offset=0 so HB cluster values come back local to this sub-run;
        // we rebase below to the slice's coordinate system.
        hb_buffer_add_utf32(buf, codepoints, len, 0, len);

        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
        hb_buffer_set_language(buf, hb_language_get_default());

        hb_shape(hb_font, buf, nullptr, 0);

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

        hb_buffer_destroy(buf);
    }

    // Itemize [begin, end) into contiguous sub-runs whose codepoints share
    // the same owning face (as chosen by selectShapingFace). This is what
    // lets a keycap like 8️⃣ succeed: '8' lives in the root face while
    // U+FE0F and U+20E3 live in the emoji face, and each needs its own
    // hb_shape pass on the face that actually carries its glyphs.
    void shape_all_sub_runs(const LLFontFreetype* root_face,
                            const LLWString&      slice,
                            std::vector<LLShapedGlyph>& out_glyphs)
    {
        const size_t n = slice.size();
        if (!root_face || n == 0)
            return;

        const LLFontFreetype* cur_face = nullptr;
        size_t                cur_begin = 0;
        for (size_t i = 0; i < n; ++i)
        {
            U32 unused = 0;
            const LLFontFreetype* face = root_face->selectShapingFace(slice[i], unused);
            if (!face)
                face = root_face; // selectShapingFace never returns null, but defensive
            if (face != cur_face)
            {
                if (cur_face != nullptr)
                    shape_sub_run(cur_face, slice, cur_begin, i, out_glyphs);
                cur_face = face;
                cur_begin = i;
            }
        }
        if (cur_face != nullptr)
            shape_sub_run(cur_face, slice, cur_begin, n, out_glyphs);
    }
}

void LLFontShaping::shapeRun(const LLFontFreetype* root_face,
                             const LLWString&      wstr,
                             size_t                begin,
                             size_t                end,
                             std::vector<LLShapedGlyph>& out_glyphs)
{
    out_glyphs.clear();

    if (!root_face || begin >= end || end > wstr.size())
        return;

    // Build the cache key from the slice + root face. Itemization is
    // deterministic given (slice, root_face), so no need to encode the
    // per-codepoint face chain explicitly.
    ShapeCacheKey key;
    key.codepoints.assign(wstr.data() + begin, wstr.data() + end);
    key.root_face = root_face;

    if (auto it = sShapeIndex.find(key); it != sShapeIndex.end())
    {
        sShapeLru.splice(sShapeLru.begin(), sShapeLru, it->second);
        const auto& cached = it->second->second;
        out_glyphs.reserve(cached.size());
        const S32 base = static_cast<S32>(begin);
        for (const LLShapedGlyph& sg : cached)
        {
            LLShapedGlyph out = sg;
            out.cluster += base;
            out_glyphs.push_back(out);
        }
        return;
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

    const auto& cached = sShapeLru.front().second;
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
