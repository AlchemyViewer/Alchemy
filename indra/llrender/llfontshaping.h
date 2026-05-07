/**
 * @file llfontshaping.h
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

#ifndef LL_LLFONTSHAPING_H
#define LL_LLFONTSHAPING_H

#include "llstring.h"

#include <vector>

class LLFontFreetype;

// One glyph produced by HarfBuzz shaping. Metrics are in pixels (already
// converted from HB's 26.6 fixed point).
struct LLShapedGlyph
{
    const LLFontFreetype* face;       // Face that owns glyph_id; shared by every glyph in a run.
    U32                   glyph_id;   // FT glyph index in `face`, *not* a Unicode codepoint.
    S32                   cluster;    // Index into the original LLWString (not the slice).
    F32                   x_advance;
    F32                   y_advance;
    F32                   x_offset;
    F32                   y_offset;
};

namespace LLFontShaping
{
    // Shape wstr[begin..end) using `root_face`'s fallback chain to pick a
    // single owning face for the whole run. Produces glyphs laid out LTR in
    // common script — the minimum setup needed for ZWJ families, VS15/16,
    // skin-tone modifiers, regional-indicator flag pairs, keycap sequences
    // and tag subdivision flags. General BiDi and script-aware shaping are
    // deliberately out of scope here, and HarfBuzz pre/post-context is not
    // populated at face/script split boundaries — Arabic init/medi/fina
    // forms across a split won't fire correctly.
    //
    // Results are cached behind a bounded LRU keyed by (codepoints, face),
    // so repeated render/width/hit-test calls on the same text do not pay
    // HarfBuzz's cost every frame. The cache is global (shared across all
    // LLFontGL instances backed by the same face) and invalidated via
    // clearCacheForFace(face) whenever a face is reloaded or destroyed.
    //
    // On entry out_glyphs is cleared. On any failure (null face, bad range,
    // HarfBuzz init failure) it remains empty — the caller should fall back
    // to the 1:1 codepoint path for that run. Empty results are cached too
    // so the failure isn't re-attempted on every frame.
    void shapeRun(const LLFontFreetype* root_face,
                  LLWStringView         wstr,
                  size_t                begin,
                  size_t                end,
                  std::vector<LLShapedGlyph>& out_glyphs);

    // Like shapeRun, but returns a const reference into the LRU instead of
    // copying out. Cluster values in the returned glyphs are SLICE-LOCAL —
    // i.e. relative to `begin`, not original-wstr coords. Callers that need
    // original-wstr clusters add `begin` once on consumption.
    //
    // The reference is invalidated by any subsequent shapeLine/shapeRun call
    // or by clearCache/clearCacheForFace. Intended for renderer hot paths
    // that shape once, iterate, and discard within a single function call.
    // On failure or bad input returns a reference to a static empty vector.
    const std::vector<LLShapedGlyph>& shapeLine(
        const LLFontFreetype* root_face,
        LLWStringView         wstr,
        size_t                begin,
        size_t                end);

    // Drop every cached shaping result. Use this on a registry-wide
    // reload (e.g. UI scale or skin change) where every face is being
    // rebuilt anyway. For single-face teardown prefer clearCacheForFace.
    // Single-threaded; the shape path is main-thread only.
    void clearCache();

    // Drop only the entries owned by `face`. Called from ~LLFontFreetype
    // and from loadFace() reload, so a face teardown doesn't blow away
    // unrelated entries cached for siblings. No-op when `face` is null
    // or has no entries. Single-threaded.
    void clearCacheForFace(const LLFontFreetype* face);

    // Number of entries currently held in the LRU. Cheap (boost::
    // unordered_map::size is O(1)). Exposed for tests that need to pin
    // the cache contract — production callers shouldn't be branching
    // on this. Use clearCache to get to a known-empty state.
    size_t cacheSize();
}

#endif // LL_LLFONTSHAPING_H
