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
        const LLFontFreetype* face;

        bool operator==(const ShapeCacheKey& o) const noexcept
        {
            return face == o.face && codepoints == o.codepoints;
        }
    };

    struct ShapeCacheKeyHash
    {
        size_t operator()(const ShapeCacheKey& k) const noexcept
        {
            size_t h = std::hash<std::u32string>{}(k.codepoints);
            boost::hash_combine(h, k.face);
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

    // Shape a new run into `out_glyphs`. Clusters are written in
    // slice-local coordinates (0..len) so the result can be cached and
    // later rebased for any caller.
    void shape_fresh(const LLFontFreetype*            face,
                     hb_font_t*                       hb_font,
                     const LLWString&                 wstr,
                     size_t                           begin,
                     size_t                           end,
                     std::vector<LLShapedGlyph>&      out_glyphs)
    {
        hb_buffer_t* buf = hb_buffer_create();
        if (!buf)
            return;

        const uint32_t* codepoints = reinterpret_cast<const uint32_t*>(wstr.data() + begin);
        const int       len        = static_cast<int>(end - begin);
        hb_buffer_add_utf32(buf, codepoints, len, 0, len);

        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_script(buf, HB_SCRIPT_COMMON);
        hb_buffer_set_language(buf, hb_language_get_default());

        hb_shape(hb_font, buf, nullptr, 0);

        unsigned int glyph_count = 0;
        const hb_glyph_info_t*     infos     = hb_buffer_get_glyph_infos(buf, &glyph_count);
        const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buf, &glyph_count);

        out_glyphs.reserve(glyph_count);
        constexpr F32 INV_64 = 1.f / 64.f;
        for (unsigned int i = 0; i < glyph_count; ++i)
        {
            LLShapedGlyph sg;
            sg.face      = face;
            sg.glyph_id  = infos[i].codepoint;
            sg.cluster   = static_cast<S32>(infos[i].cluster);  // slice-local
            sg.x_advance = positions[i].x_advance * INV_64;
            sg.y_advance = positions[i].y_advance * INV_64;
            sg.x_offset  = positions[i].x_offset  * INV_64;
            sg.y_offset  = positions[i].y_offset  * INV_64;
            out_glyphs.push_back(sg);
        }

        hb_buffer_destroy(buf);
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

    U32 base_glyph = 0; // Unused here; selectShapingFace returns it as a side effect.
    const LLFontFreetype* face = root_face->selectShapingFace(wstr[begin], base_glyph);
    if (!face)
        return;

    // Build the cache key. Copying the slice into a u32string is cheap for
    // the sizes we see (shaping runs are typically 2-7 codepoints).
    ShapeCacheKey key;
    key.codepoints.assign(wstr.data() + begin, wstr.data() + end);
    key.face = face;

    if (auto it = sShapeIndex.find(key); it != sShapeIndex.end())
    {
        // Hit: splice to front for LRU, copy out with clusters rebased to
        // the caller's wstr coordinates.
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

    // Miss: shape, insert, then copy out. shape_fresh may leave out_glyphs
    // empty (hb_buffer_create failure or hb_font null) — we still cache
    // empty results so we don't re-try the failure every frame.
    hb_font_t* hb_font = face->getHbFont();
    std::vector<LLShapedGlyph> shaped;
    if (hb_font)
        shape_fresh(face, hb_font, wstr, begin, end, shaped);

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
