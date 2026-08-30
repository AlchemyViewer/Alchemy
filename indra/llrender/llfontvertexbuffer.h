/**
 * @file llfontgl.h
 * @author Andrii Kleshchev
 * @brief Buffer storage for font rendering.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLFONTVERTEXBUFFER_H
#define LL_LLFONTVERTEXBUFFER_H

#include "llfontgl.h"

class LLVertexBufferData;

namespace ll_test { struct VertexBufferProbe; }

// Rendering fonts is expensive, this class is intended to store
// vertex buffers for rendered text, so that they can be reused.
// LLFontVertexBuffer tracks font and rendering parameters, but
// expects caller to track text changes and call reset() when
// text changes.
class LLFontVertexBuffer
{
    friend struct ll_test::VertexBufferProbe;
public:
    LLFontVertexBuffer();
    ~LLFontVertexBuffer();

    void reset();

    // `begin_offset` and `max_bytes` index the UTF-8. The buffer keeps no copy
    // of the text, so nothing about the caching depends on the text itself.
    S32 renderBytes(const LLFontGL* fontp,
        std::string_view text,
        S32 begin_offset,
        LLRect rect,
        const LLColor4& color,
        LLFontGL::HAlign halign = LLFontGL::LEFT, LLFontGL::VAlign valign = LLFontGL::BASELINE,
        U8 style = LLFontGL::NORMAL,
        LLFontGL::ShadowType shadow = LLFontGL::NO_SHADOW,
        S32 max_bytes = S32_MAX, S32 max_pixels = S32_MAX,
        F32* right_x = NULL,
        bool use_ellipses = false,
        bool use_color = true);

    S32 renderBytes(const LLFontGL* fontp,
        std::string_view text,
        S32 begin_offset,
        LLRectf rect,
        const LLColor4& color,
        LLFontGL::HAlign halign = LLFontGL::LEFT, LLFontGL::VAlign valign = LLFontGL::BASELINE,
        U8 style = LLFontGL::NORMAL,
        LLFontGL::ShadowType shadow = LLFontGL::NO_SHADOW,
        S32 max_bytes = S32_MAX,
        F32* right_x = NULL,
        bool use_ellipses = false,
        bool use_color = true);

    S32 renderBytes(const LLFontGL* fontp,
        std::string_view text,
        S32 begin_offset,
        F32 x, F32 y,
        const LLColor4& color,
        LLFontGL::HAlign halign = LLFontGL::LEFT, LLFontGL::VAlign valign = LLFontGL::BASELINE,
        U8 style = LLFontGL::NORMAL,
        LLFontGL::ShadowType shadow = LLFontGL::NO_SHADOW,
        S32 max_bytes = S32_MAX, S32 max_pixels = S32_MAX,
        F32* right_x = NULL,
        bool use_ellipses = false,
        bool use_color = true);

    static void enableBufferCollection(bool enable) { sEnableBufferCollection = enable; }
private:

    S32 renderImpl(const LLFontGL* fontp,
         std::string_view text,
         S32 begin_offset,
         F32 x, F32 y,
         const LLColor4& color,
         LLFontGL::HAlign halign, LLFontGL::VAlign valign,
         U8 style,
         LLFontGL::ShadowType shadow,
         S32 max_bytes, S32 max_pixels,
         F32* right_x,
         bool use_ellipses,
         bool use_color);

    void genBuffers(const LLFontGL* fontp,
         std::string_view text,
         S32 begin_offset,
         F32 x, F32 y,
         const LLColor4& color,
         LLFontGL::HAlign halign, LLFontGL::VAlign valign,
         U8 style,
        LLFontGL::ShadowType shadow,
         S32 max_bytes, S32 max_pixels,
         F32* right_x,
         bool use_ellipses,
         bool use_color);

    void renderBuffers();

    // Color-only cache regen path: when only the foreground color changed
    // (geometry-affecting params unchanged), rewrite the captured color
    // attribute streams in place via LLVertexBuffer::setColorData. Avoids the
    // full HarfBuzz shape + per-glyph vertex build that genBuffers does. Used
    // by the dominant hover/fade animation case in UI buttons.
    void recolorBuffers(const LLColor4& color, LLFontGL::ShadowType shadow);

    // Each pass (shadow, foreground) is captured into its own list so that
    // every vertex within a list shares the same color (foreground color, or
    // derived shadow color). Color-only cache regen mutates the captured
    // color attribute streams without rebuilding geometry.
    std::list<LLVertexBufferData> mShadowBufferList;
    std::list<LLVertexBufferData> mForegroundBufferList;
    // Set during genBuffers: did any captured batch render glyphs from the
    // color (RGBA emoji) atlas? Mixed text+emoji strings can't be recolored
    // safely without per-entry glyph-type tracking, since emoji glyphs use a
    // fixed (255,255,255) RGB regardless of foreground color. Mixed strings
    // fall through to full genBuffers on color change.
    bool mLastUsesColorAtlas = false;

    // Snapshot of LLFontGL::sEnableShaderShadow at genBuffers time. Required
    // because LLVertexBufferData doesn't capture shader uniforms; renderBuffers
    // must re-push shadowMode before replaying mShadowBufferList and reset
    // shadowMode = 0 before mForegroundBufferList. If the static flag flips
    // between gen and replay, the captured stream still replays with the
    // shader state it was built for. shadowMode is the only shadow uniform —
    // atlas texel size / channel layout derive from the bound texture in
    // uiF.glsl, which the captured streams DO rebind per batch.
    bool mLastUsedShaderShadow = false;
    S32 mChars = 0;
    const LLFontGL *mLastFont = nullptr;
    S32 mLastOffset = 0;
    S32 mLastMaxBytes = 0;
    S32 mLastMaxPixels = 0;
    F32 mLastX = 0.f;
    F32 mLastY = 0.f;
    LLColor4 mLastColor;
    LLFontGL::HAlign mLastHalign = LLFontGL::LEFT;
    LLFontGL::VAlign mLastValign = LLFontGL::BASELINE;
    U8 mLastStyle = LLFontGL::NORMAL;
    LLFontGL::ShadowType mLastShadow = LLFontGL::NO_SHADOW;
    bool mLastUseEllipses = false;
    bool mLastUseColor = true;
    F32 mLastRightX = 0.f;

    // LLFontGL's statics
    F32 mLastScaleX = 1.f;
    F32 mLastScaleY = 1.f;
    F32 mLastVertDPI = 0.f;
    F32 mLastHorizDPI = 0.f;
    S32 mLastResGeneration = 0;
    LLCoordGL mLastOrigin;

    // Adding new characters to bitmap cache can alter value from getBitmapWidth();
    // which alters whole string. So rerender when new characters were added to cache.
    // U64 to match LLFontGL::getCacheGeneration's summed stamp.
    U64 mLastFontCacheGen = 0;

    static bool sEnableBufferCollection;

public:
    // Toggle for the color-only regen fast path. When false, color changes
    // always fall through to full genBuffers (legacy behavior). Useful for
    // A/B comparison and bisecting any visual regression.
    static void enableColorOnlyRegen(bool enable) { sEnableColorOnlyRegen = enable; }
private:
    static bool sEnableColorOnlyRegen;
};

// Extracting width from a font is expensive, and due to
// mechanics of font rendering, we need width separately
// and usually before rendering.
// LLFontWidthBuffer tracks font and rendering parameters,
// but expects caller to track text changes and call reset()
// when text changes.
class LLFontWidthBuffer
{
public:
    LLFontWidthBuffer();
    ~LLFontWidthBuffer();

    void reset();

    F32 getWidthBytes(const LLFontGL* fontp,
        std::string_view utf8text,
        S32 begin_offset,
        S32 max_bytes,
        bool no_padding);

    static void enableBufferCollection(bool enable) { sEnableBufferCollection = enable; }
private:
        // The cache check is the same whichever unit the caller measures in;
        // only the call that fills it differs.
        template <typename MEASURE>
        F32 cachedWidth(const LLFontGL* fontp, S32 begin_offset, S32 max_bytes,
                        bool no_padding, MEASURE&& measure);

        const LLFontGL* mLastFont = nullptr;

        // One slot per span, because one segment is asked about several per
        // frame and each answer used to evict the last: the selection rects
        // want two spans, the cursor's document rect wants the line-start
        // prefix and the whole segment, and reflow wants its own. With a
        // single slot every one of those was a full measurement walk.
        //
        // Round-robin rather than LRU -- at this size the bookkeeping would
        // cost more than the miss it saves. The font and the scale/DPI state
        // are shared by every slot, so a change in those empties all of them.
        struct WidthSlot
        {
            S32  offset     = 0;
            S32  max_bytes  = 0;
            bool no_padding = false;
            bool valid      = false;
            F32  width      = 0.f;
        };
        static constexpr size_t WIDTH_SLOT_COUNT = 4;
        WidthSlot mSlots[WIDTH_SLOT_COUNT];
        size_t    mNextSlot = 0;

        // LLFontGL's values that affect width calculation
        F32 mLastScaleX = 1.f;
        F32 mLastScaleY = 1.f;
        F32 mLastVertDPI = 0.f;
        F32 mLastHorizDPI = 0.f;
        S32 mLastResGeneration = 0;

        // Cache generation tracking. U64 to match LLFontGL::getCacheGeneration.
        U64 mLastFontCacheGen = 0;

        static bool sEnableBufferCollection;
};

#endif
