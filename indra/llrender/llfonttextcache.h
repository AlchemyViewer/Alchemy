/**
 * @file llfonttextcache.h
 * @author Andrii Kleshchev
 * @brief What one piece of text costs to draw, kept so it can be reused.
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

#ifndef LL_LLFONTTEXTCACHE_H
#define LL_LLFONTTEXTCACHE_H

#include "llfontgl.h"

#include <functional>
#include <string_view>

class LLVertexBufferData;

namespace ll_test { struct VertexBufferProbe; }

// Which text a cache is holding work for. Both caches below compare every
// input they are given against the one they last used -- font, position,
// alignment, style, colour, scale -- except the text, which they are handed as
// a view and cannot compare without keeping a copy of it. A second copy of
// every label in the UI is not worth the comparison.
//
// So the caller names the text instead: an address that is stable for as long
// as the text lives, and a counter its owner bumps when it changes.
// LLUIString::getGeneration is one such counter, which is what llui widgets
// pass. The address separates a widget's selected label from its unselected
// one; the counter catches either of them moving.
//
// A version may bump without the text differing -- reassigning a label to the
// value it already had rebuilds. That is the safe direction.
// It also holds what both caches independently tracked and independently
// compared: the font, and the state that changes what that font produces --
// the scale and DPI it rasterizes at, a resolution change, a glyph cache
// rebuilt underneath it. Seven fields and one comparison, once.
//
// This is the whole of what invalidates a width. Geometry dies on all of it
// and on more besides -- where the text sits, its colour, alignment, shadow,
// whether it ellipsizes -- so the two are nested, not separate: everything
// that kills a width kills the geometry too. What keeps them apart below is
// not the invalidation but the question. Widths are asked for spans that are
// never drawn, and asked before drawing the span that is, so they need slots
// of their own rather than an answer read back off a draw.
class ALFontCacheKey
{
public:
    // True when this names a different text than the last call did.
    bool sourceMoved(const void* owner, U32 version)
    {
        if (owner == mOwner && version == mVersion)
        {
            return false;
        }
        mOwner = owner;
        mVersion = version;
        mTextHashed = false;
        return true;
    }

    // True when anything about how this font renders has changed. Records the
    // new state as it goes, so ask it once per query and keep the answer --
    // folding it into a short-circuiting || would sometimes skip the record.
    bool environmentMoved(const LLFontGL* fontp);

    void forgetSource() { mOwner = nullptr; mTextHashed = false; }

    // Every query against one source has to be about the same text. The width
    // slots are keyed on the span and not on the bytes, so a caller that shows
    // this cache a second string is handed the first one's answer for that
    // span -- a password field's bullets and the text behind them are the
    // shape of the mistake. Fingerprinted rather than kept as a view: the text
    // may have moved between calls, and a stale view could not be compared
    // safely. Debug only; llassert compiles the call away with it.
    bool sameTextAsRecorded(std::string_view text)
    {
        const size_t print = fingerprint(text);
        if (!mTextHashed)
        {
<<<<<<< HEAD
            #ifdef LL_DEBUG
            mRecordedLength = text.size();
            #endif
            mTextHash  = print;
            mTextHashed = true;
            return true;
        }
        if (print != mTextHash)
        {
            #ifdef LL_DEBUG
            LL_WARNS() << "sameTextAsRecorded mismatch!"
                       << " recorded length: " << mRecordedLength
                       << " recorded hash: "   << mTextHash
                       << " new length: "      << text.size()
                       << " new hash: "        << print
                       << LL_ENDL;
            #endif
=======
            mRecordedText = std::string(text);   // <-- add (debug only)
            mTextHash = print;
            mTextHashed = true;
            return true;
        }
        if (print != mTextHash)                  // <-- change return to if-block
        {
            LL_WARNS() << "sameTextAsRecorded mismatch!"
                       << " recorded: \"" << mRecordedText << "\""
                       << " new: \"" << std::string(text) << "\""
                       << LL_ENDL;
>>>>>>> db56575f0e (fix: resolve font cache assertion crashes on login screen)
            return false;
        }
        return true;
    }

private:
    // The length and both ends, never the middle. A text widget names its
    // whole document here and then asks about spans of it, so hashing all of
    // what it is handed costs the length of the document per query -- on the
    // path of every width a reflow asks for, in every build that keeps
    // asserts. Bounded, it is a constant, and it still separates the strings
    // this exists to separate: a field's bullets are a different length from
    // the text behind them, and two labels differ within their first bytes.
    static size_t fingerprint(std::string_view text)
    {
        constexpr size_t SAMPLE = 32;
        const std::hash<std::string_view> hasher;
        size_t print = text.size();
        const auto mix = [&print, &hasher](std::string_view part)
        {
            print ^= hasher(part) + 0x9e3779b9U + (print << 6) + (print >> 2);
        };
        mix(text.substr(0, SAMPLE));
        if (text.size() > SAMPLE)
        {
            mix(text.substr(text.size() - SAMPLE));
        }
        return print;
    }


    const void*     mOwner = nullptr;   // never dereferenced, only compared
    U32             mVersion = 0;
    size_t          mTextHash = 0;
    bool            mTextHashed = false;
    #ifdef LL_DEBUG
<<<<<<< HEAD
        size_t mRecordedLength = 0;
=======
        std::string mRecordedText;
>>>>>>> db56575f0e (fix: resolve font cache assertion crashes on login screen)
    #endif

    const LLFontGL* mFont = nullptr;
    F32             mScaleX = 1.f;
    F32             mScaleY = 1.f;
    F32             mVertDPI = 0.f;
    F32             mHorizDPI = 0.f;
    S32             mResGeneration = 0;
    U64             mFontCacheGen = 0;
};

// Everything one piece of text costs to draw, kept so it can be reused: the
// shaped and laid-out geometry, and the widths measured from the same glyphs.
// These were two classes with two keys, and the width half had exactly one
// user while every other text draw in the tree measured uncached.
class LLFontTextCache : private ALFontCacheKey
{
    friend struct ll_test::VertexBufferProbe;
public:
    LLFontTextCache();
    ~LLFontTextCache();

    // Movable so a cache can live in a vector of whatever it caches for;
    // never copyable, because a captured GPU buffer has one owner.
    LLFontTextCache(const LLFontTextCache&) = delete;
    LLFontTextCache& operator=(const LLFontTextCache&) = delete;
    LLFontTextCache(LLFontTextCache&&) = default;
    LLFontTextCache& operator=(LLFontTextCache&&) = default;

    void reset();

    // Name the text being drawn, before drawing it. Everything else the buffer
    // notices for itself; this is the one input it is handed by view and so
    // cannot. Callers that never change their text may skip it.
    void setSource(const void* owner, U32 version)
    {
        // Drop the work, not the name: asking whether the source moved is what
        // records it, and reset() forgets it again. A caller that names its
        // text before every draw -- which is what this is for -- then looked
        // like a different text on every one of them, and nothing it drew was
        // ever replayed.
        if (sourceMoved(owner, version))
        {
            dropDerived();
        }
    }

    // `begin_offset` and `max_bytes` index the UTF-8. The buffer keeps no copy
    // of the text; setSource above is how it is told the text moved.
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

    // Width, from the same cache and behind the same key. Extracting it from a
    // font is expensive, it is wanted separately from the draw and usually
    // before it, and everything that invalidates it -- the text, the font, the
    // scale it rasterizes at -- has already been compared for the draw.
    //
    // Only ever ask it about the text setSource named. The slots are keyed on
    // the span, not on the bytes, so measuring some other string through this
    // returns the width of the span of the text it is holding: a password
    // field's bullets and the text behind them are different strings, and a
    // cache keyed on one cannot answer for the other.
    //
    // One slot per span, because one holder is asked about several per frame
    // and each answer used to evict the last: the selection rects want two
    // spans, the cursor's document rect wants the line-start prefix and the
    // whole segment, and reflow wants its own. Round-robin rather than LRU --
    // at this size the bookkeeping would cost more than the miss it saves.
    F32 getWidthBytes(const LLFontGL* fontp,
        std::string_view utf8text,
        S32 begin_offset,
        S32 max_bytes,
        bool no_padding);

    static void enableBufferCollection(bool enable) { sEnableBufferCollection = enable; }

    // How many times any cache anywhere has rebuilt its geometry. The whole
    // purpose of this class is that the number stops moving once the UI
    // settles, and nothing outside it can otherwise see whether it did.
    static U64 regenCount() { return sRegenCount; }
private:

    // Everything held here is derived from the font and the state it
    // rasterizes at, so a change to that kills all of it -- the measured
    // widths and the captured geometry alike. Both halves ask that question
    // and the answer records itself, so whichever asks first is the only one
    // told: each has to throw away the other's work as well, or the second
    // goes on answering from glyphs that have moved in the atlas underneath
    // it.
    void dropDerived();

    // The cache check is the same whichever unit the caller measures in;
    // only the call that fills it differs.
    template <typename MEASURE>
    F32 cachedWidth(const LLFontGL* fontp, S32 begin_offset, S32 max_bytes,
                    bool no_padding, MEASURE&& measure);

    // Which input stopped a draw from being a replay. Each gets its own zone
    // at the call, because a capture that says a thousand rebuilds happened
    // does not say whether the text moved, the window scaled or a colour
    // faded -- and those are three different fixes.
    enum class RegenReason
    {
        NoReason,
        NoCapture,
        FontState,
        Position,
        Span,
        Style,
        ShadowGate,
        Color
    };

    struct WidthSlot
    {
        S32  offset     = 0;
        S32  max_bytes  = 0;
        bool no_padding = false;
        bool valid      = false;
        F32  width      = 0.f;
    };
    static constexpr size_t WIDTH_SLOT_COUNT = 4;
    WidthSlot mWidthSlots[WIDTH_SLOT_COUNT];
    size_t    mNextWidthSlot = 0;

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

    // Whether a capture has been taken, which is not the same as holding
    // geometry. Text clipped to no width, or a cell with nothing in it, shapes
    // and measures and then emits no vertices at all. Reading that off the
    // lists instead answers "nothing generated yet", so such a piece of text
    // shapes itself again on every frame it stays on screen.
    bool mHasCapture = false;

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

    static bool sEnableBufferCollection;
    static U64  sRegenCount;

public:
    // Toggle for the color-only regen fast path. When false, color changes
    // always fall through to full genBuffers (legacy behavior). Useful for
    // A/B comparison and bisecting any visual regression.
    static void enableColorOnlyRegen(bool enable) { sEnableColorOnlyRegen = enable; }
private:
    static bool sEnableColorOnlyRegen;
};


#endif
