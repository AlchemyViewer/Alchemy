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
    // deliberately out of scope here.
    //
    // Results are cached behind a bounded LRU keyed by (codepoints, face),
    // so repeated render/width/hit-test calls on the same text do not pay
    // HarfBuzz's cost every frame. The cache is global (shared across all
    // LLFontGL instances backed by the same face) and invalidated via
    // clearCache() whenever a face is reloaded.
    //
    // On entry out_glyphs is cleared. On any failure (null face, bad range,
    // HarfBuzz init failure) it remains empty — the caller should fall back
    // to the 1:1 codepoint path for that run. Empty results are cached too
    // so the failure isn't re-attempted on every frame.
    void shapeRun(const LLFontFreetype* root_face,
                  const LLWString&      wstr,
                  size_t                begin,
                  size_t                end,
                  std::vector<LLShapedGlyph>& out_glyphs);

    // Drop every cached shaping result. Must be called when any LLFontFreetype
    // reloads its FT_Face, since cached LLShapedGlyph entries carry glyph
    // indices that are only valid for the face's current state. Safe to call
    // when the cache is empty. Single-threaded; the shape path is main-thread
    // only.
    void clearCache();
}

#endif // LL_LLFONTSHAPING_H
