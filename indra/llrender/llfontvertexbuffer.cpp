/**
 * @file llfontvertexbuffer.cpp
 * @brief Buffer storage for font rendering.
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
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

#include "linden_common.h"

#include "llfontvertexbuffer.h"

#include "llfontbitmapcache.h"
#include "llfontfreetype.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llshadermgr.h"
#include "llvertexbuffer.h"

#include "llmath.h"  // clamp_rescale


bool LLFontVertexBuffer::sEnableBufferCollection = true;
bool LLFontVertexBuffer::sEnableColorOnlyRegen = true;

namespace
{
    // Mirrors the shadow-alpha derivation in LLFontGL::render preamble. Returns
    // the alpha byte (0..255) for the shadow color given foreground color and
    // the requested shadow type. Returns 0 (no shadow) when the foreground
    // luminance falls below the dark-text gate, matching the gate at line ~196
    // of llfontgl.cpp.
    U8 derive_shadow_alpha(const LLColor4& fg, LLFontGL::ShadowType shadow)
    {
        if (shadow == LLFontGL::NO_SHADOW)
        {
            return 0;
        }
        F32 luminance;
        fg.calcHSL(NULL, NULL, &luminance);
        if (luminance < 0.35f)
        {
            return 0;
        }
        const F32 strength = clamp_rescale(luminance, 0.35f, 0.6f, 0.f, 1.f);
        const F32 soft_scale = (shadow == LLFontGL::DROP_SHADOW_SOFT) ? 0.3f : 1.f; // DROP_SHADOW_SOFT_STRENGTH
        const LLColor4U fg_u(fg);
        const F32 alpha = (F32)fg_u.mV[VALPHA] * strength * soft_scale;
        return (U8)alpha;
    }
}

LLFontVertexBuffer::LLFontVertexBuffer()
{
}

LLFontVertexBuffer::~LLFontVertexBuffer()
{
    reset();
}

void LLFontVertexBuffer::reset()
{
    // Todo: some form of debug only frequecy check&assert to see if this is happening too often.
    // Regenerating this list is expensive
    mShadowBufferList.clear();
    mForegroundBufferList.clear();
}

S32 LLFontVertexBuffer::render(
    const LLFontGL* fontp,
    const LLWString& text,
    S32 begin_offset,
    LLRect rect,
    const LLColor4& color,
    LLFontGL::HAlign halign, LLFontGL::VAlign valign,
    U8 style,
    LLFontGL::ShadowType shadow,
    S32 max_chars, S32 max_pixels,
    F32* right_x,
    bool use_ellipses,
    bool use_color)
{
    LLRectf rect_float((F32)rect.mLeft, (F32)rect.mTop, (F32)rect.mRight, (F32)rect.mBottom);
    return render(fontp, text, begin_offset, rect_float, color, halign, valign, style, shadow, max_chars, right_x, use_ellipses, use_color);
}

S32 LLFontVertexBuffer::render(
    const LLFontGL* fontp,
    const LLWString& text,
    S32 begin_offset,
    LLRectf rect,
    const LLColor4& color,
    LLFontGL::HAlign halign, LLFontGL::VAlign valign,
    U8 style,
    LLFontGL::ShadowType shadow,
    S32 max_chars,
    F32* right_x,
    bool use_ellipses,
    bool use_color)
{
    F32 x = rect.mLeft;
    F32 y = 0.f;

    switch (valign)
    {
    case LLFontGL::TOP:
        y = rect.mTop;
        break;
    case LLFontGL::VCENTER:
        y = rect.getCenterY();
        break;
    case LLFontGL::BASELINE:
    case LLFontGL::BOTTOM:
        y = rect.mBottom;
        break;
    default:
        y = rect.mBottom;
        break;
    }
    return render(fontp, text, begin_offset, x, y, color, halign, valign, style, shadow, max_chars, (S32)rect.getWidth(), right_x, use_ellipses, use_color);
}

S32 LLFontVertexBuffer::render(
    const LLFontGL* fontp,
    const LLWString& text,
    S32 begin_offset,
    F32 x, F32 y,
    const LLColor4& color,
    LLFontGL::HAlign halign, LLFontGL::VAlign valign,
    U8 style,
    LLFontGL::ShadowType shadow,
    S32 max_chars , S32 max_pixels,
    F32* right_x,
    bool use_ellipses,
    bool use_color )
{
    if (!LLFontGL::sDisplayFont) //do not display texts
    {
        return static_cast<S32>(text.length());
    }
    if (!sEnableBufferCollection)
    {
        // For debug purposes and performance testing
        return fontp->render(text, begin_offset, x, y, color, halign, valign, style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
    }
    // Geometry-invalidating params: any change forces full genBuffers.
    const bool geometry_invalid =
            mLastX != x
         || mLastY != y
         || mLastFont != fontp
         || mLastHalign != halign
         || mLastValign != valign
         || mLastOffset != begin_offset
         || mLastMaxChars != max_chars
         || mLastMaxPixels != max_pixels
         || mLastStyle != style
         || mLastShadow != shadow // shadow-type change also alters dark-text gate threshold
         || mLastScaleX != LLFontGL::sScaleX
         || mLastScaleY != LLFontGL::sScaleY
         || mLastVertDPI != LLFontGL::sVertDPI
         || mLastHorizDPI != LLFontGL::sHorizDPI
         || mLastOrigin != LLFontGL::sCurOrigin
         || mLastResGeneration != LLFontGL::sResolutionGeneration
         || mLastFontCacheGen != fontp->getCacheGeneration();

    // Crossing the dark-text gate (luminance 0.35) toggles whether shadow
    // geometry was emitted at all, which is geometry-affecting. Detect with a
    // cheap epsilon-free comparison: if either side is on a different side of
    // the gate, fall back to full regen.
    const bool gate_crossed = (shadow != LLFontGL::NO_SHADOW)
        && ((derive_shadow_alpha(color, shadow) == 0) !=
            (derive_shadow_alpha(mLastColor, mLastShadow) == 0));

    if (mShadowBufferList.empty() && mForegroundBufferList.empty())
    {
        genBuffers(fontp, text, begin_offset, x, y, color, halign, valign,
            style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
    }
    else if (geometry_invalid || gate_crossed)
    {
        genBuffers(fontp, text, begin_offset, x, y, color, halign, valign,
            style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
    }
    else if (mLastColor != color)
    {
        // Color-only change: skip the geometry rebuild and just rewrite the
        // color attribute streams of the captured GPU buffers. This is the
        // hot path during button hover/press fades. Falls back to genBuffers
        // for mixed text+emoji strings (mLastUsesColorAtlas) since emoji
        // glyphs need fixed (255,255,255) RGB even on color change.
        if (sEnableColorOnlyRegen && !mLastUsesColorAtlas)
        {
            recolorBuffers(color, shadow);
            renderBuffers();
            if (right_x)
            {
                *right_x = mLastRightX;
            }
            return mChars;
        }
        genBuffers(fontp, text, begin_offset, x, y, color, halign, valign,
            style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
    }
    else
    {
        renderBuffers();

        if (right_x)
        {
            *right_x = mLastRightX;
        }
    }
    return mChars;
}

void LLFontVertexBuffer::genBuffers(
    const LLFontGL* fontp,
    const LLWString& text,
    S32 begin_offset,
    F32 x, F32 y,
    const LLColor4& color,
    LLFontGL::HAlign halign, LLFontGL::VAlign valign,
    U8 style, LLFontGL::ShadowType shadow,
    S32 max_chars, S32 max_pixels,
    F32* right_x,
    bool use_ellipses,
    bool use_color)
{
    // todo: add a debug build assert if this triggers too often for to long?
    mShadowBufferList.clear();
    mForegroundBufferList.clear();
    // Save before rendreing, it can change mid-render,
    // so will need to rerender previous characters
    mLastFontCacheGen = fontp->getCacheGeneration();

    // Snapshot shader-shadow state for the cache. The static flag could flip
    // between gen and replay, so we cache it alongside the captured streams
    // and use the snapshot in renderBuffers. shadowMode is the only shadow
    // uniform — atlas texel size and channel layout derive from the bound
    // texture inside uiF.glsl, so per-batch texture rebinds on replay carry
    // everything that varies.
    mLastUsedShaderShadow = LLFontGL::sEnableShaderShadow && (shadow != LLFontGL::NO_SHADOW);

    // Two-pass capture: pass A (shadow geometry) lands in mShadowBufferList,
    // pass B (foreground geometry) lands in mForegroundBufferList. The
    // pass-boundary callback runs at the seam between the two passes inside
    // LLFontGL::render. For NO_SHADOW renders, fontp->render emits only the
    // foreground stream and never invokes the callback; mShadowBufferList
    // stays empty.
    LLFontGL::pass_boundary_cb_t pass_boundary;
    if (shadow != LLFontGL::NO_SHADOW)
    {
        gGL.beginList(&mShadowBufferList);
        pass_boundary = [this]()
        {
            gGL.endList();
            gGL.beginList(&mForegroundBufferList);
        };
    }
    else
    {
        gGL.beginList(&mForegroundBufferList);
    }
    mChars = fontp->render(text, begin_offset, x, y, color, halign, valign,
        style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color, pass_boundary);
    gGL.endList();

    // Detect whether any captured batch sampled the color (RGBA emoji) atlas.
    // Mixed strings can't be recolored cheaply because emoji glyphs use a
    // fixed (255,255,255) RGB regardless of foreground color, while text
    // glyphs use the foreground RGB. Without per-entry tracking we'd shift
    // emoji colors on hover; flag the buffer as ineligible for color-only
    // regen and let it fall through to genBuffers when color changes.
    mLastUsesColorAtlas = false;
    if (const LLFontFreetype* face = fontp->getFontFreetype())
    {
        // After the ALFontFace move, color atlases live on individual face
        // wrappers — head primary, each fallback's face. Walk all of them
        // to gather the set of color atlas textures this batch might have
        // sampled from.
        std::vector<LLGLuint> color_texnames;
        auto collect_from_cache = [&color_texnames](const LLFontBitmapCache* cache) {
            if (!cache)
                return;
            const U32 color_atlas_count = cache->getNumBitmaps(EFontGlyphType::Color);
            for (U32 i = 0; i < color_atlas_count; ++i)
            {
                if (LLImageGL* img = cache->getImageGL(EFontGlyphType::Color, i))
                    color_texnames.push_back(img->getTexName());
            }
        };
        collect_from_cache(face->getBitmapCache());
        for (const auto& fb : face->getFallbackFonts())
        {
            if (fb.first)
                collect_from_cache(fb.first->getBitmapCache());
        }
        if (!color_texnames.empty())
        {
            auto entry_uses_color = [&color_texnames](const LLVertexBufferData& entry) {
                for (LLGLuint name : color_texnames)
                {
                    if (name && entry.mTexName == name)
                        return true;
                }
                return false;
            };
            for (const LLVertexBufferData& entry : mForegroundBufferList)
            {
                if (entry_uses_color(entry))
                {
                    mLastUsesColorAtlas = true;
                    break;
                }
            }
        }
    }

    mLastFont = fontp;
    mLastOffset = begin_offset;
    mLastMaxChars = max_chars;
    mLastMaxPixels = max_pixels;
    mLastX = x;
    mLastY = y;
    mLastColor = color;
    mLastHalign = halign;
    mLastValign = valign;
    mLastStyle = style;
    mLastShadow = shadow;

    mLastScaleX = LLFontGL::sScaleX;
    mLastScaleY = LLFontGL::sScaleY;
    mLastVertDPI = LLFontGL::sVertDPI;
    mLastHorizDPI = LLFontGL::sHorizDPI;
    mLastOrigin = LLFontGL::sCurOrigin;
    mLastResGeneration = LLFontGL::sResolutionGeneration;

    if (right_x)
    {
        mLastRightX = *right_x;
    }
}

void LLFontVertexBuffer::recolorBuffers(
    const LLColor4& color,
    LLFontGL::ShadowType shadow)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    gGL.flush(); // deliberately empty pending verts

    // Foreground: rebroadcast the new color across every captured fg vertex.
    // Pure-text path only (caller has guarded mLastUsesColorAtlas == false).
    const LLColor4U fg_u(color);
    const U8 shadow_alpha = derive_shadow_alpha(color, shadow);
    LLColor4U shadow_u = LLFontGL::sShadowColor;
    shadow_u.mV[VALPHA] = shadow_alpha;

    // Route through getColorStrider (mapVertexBuffer) instead of setColorData
    // for the recolor write. mapVertexBuffer writes into the buffer's CPU-side
    // shadow (mMappedData) and tracks a dirty region; the next setBuffer call
    // (driven by buffer.draw() in renderBuffers) flushes that region to GL.
    // setColorData would call glBufferSubData against the *currently bound*
    // buffer, which on the non-Apple GL path is not necessarily the entry's
    // buffer — that path corrupted unrelated UI elements' GL data when
    // clicking between buttons.
    auto recolor = [](std::list<LLVertexBufferData>& list, const LLColor4U& fill)
    {
        for (LLVertexBufferData& entry : list)
        {
            if (entry.mVB.isNull() || entry.mCount == 0)
                continue;
            LLStrider<LLColor4U> colors;
            if (!entry.mVB->getColorStrider(colors, 0, entry.mCount))
                continue;
            for (U32 i = 0; i < entry.mCount; ++i)
            {
                colors[i] = fill;
            }
        }
    };
    recolor(mForegroundBufferList, fg_u);
    recolor(mShadowBufferList, shadow_u);

    // Ensure all buffers are unmapped and data is sent to GPU before rendering
    LLVertexBuffer::flushBuffers();
    LLVertexBuffer::unbind();

    mLastColor = color;
}

void LLFontVertexBuffer::renderBuffers()
{
    gGL.flush(); // deliberately empty pending verts
    gGL.pushUIMatrix();

    gGL.loadUIIdentity();

    // Depth translation, so that floating text appears 'in-world'
    // and is correctly occluded.
    gGL.translatef(0.f, 0.f, LLFontGL::sCurDepth);
    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    // Note: ellipses should technically be covered by push/load/translate of their own
    // but it's more complexity, values do not change, skipping doesn't appear to break
    // anything, so we can skip that until it proves to cause issues.
    // Shadow first (under), foreground second (over). Pass-boundary order matches
    // the original interleaved-per-glyph emission's net visual stacking — shadow
    // contributions sit beneath glyph foregrounds rather than between them.
    if (mLastUsedShaderShadow && LLGLSLShader::sCurBoundShaderPtr)
    {
        const int mode = (mLastShadow == LLFontGL::DROP_SHADOW) ? 1 : 2; // SOFT
        LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::TEXT_SHADOW_MODE, mode);
    }
    for (LLVertexBufferData& buffer : mShadowBufferList)
    {
        buffer.draw();
    }
    if (mLastUsedShaderShadow && LLGLSLShader::sCurBoundShaderPtr)
    {
        LLGLSLShader::sCurBoundShaderPtr->uniform1i(LLShaderMgr::TEXT_SHADOW_MODE, 0);
    }
    for (LLVertexBufferData& buffer : mForegroundBufferList)
    {
        buffer.draw();
    }
    gGL.popUIMatrix();
}

// LLFontWidthBuffer
bool LLFontWidthBuffer::sEnableBufferCollection = true;

LLFontWidthBuffer::LLFontWidthBuffer()
{
}

LLFontWidthBuffer::~LLFontWidthBuffer()
{
}

void LLFontWidthBuffer::reset()
{
    mLastFont = nullptr;
    mLastOffset = 0;
    mLastMaxChars = 0;
    mLastNoPadding = false;
    mWidth = -1.f;
    mLastScaleX = 1.f;
    mLastScaleY = 1.f;
    mLastVertDPI = 0.f;
    mLastHorizDPI = 0.f;
    mLastResGeneration = 0;
    mLastFontCacheGen = 0;
}

F32 LLFontWidthBuffer::getWidth(
    const LLFontGL* fontp,
    const llwchar* wchars,
    S32 begin_offset,
    S32 max_chars,
    bool no_padding)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (!fontp || !wchars)
    {
        return 0.f;
    }

    if (!sEnableBufferCollection)
    {
        return fontp->getWidthF32(wchars, begin_offset, max_chars, no_padding);
    }

    // Check if we can use cached width
    bool needs_recalc = (mWidth < 0.f)
        || (mLastFont != fontp)
        || (mLastOffset != begin_offset)
        || (mLastMaxChars != max_chars)
        || (mLastNoPadding != no_padding)
        || (mLastScaleX != LLFontGL::sScaleX)
        || (mLastScaleY != LLFontGL::sScaleY)
        || (mLastVertDPI != LLFontGL::sVertDPI)
        || (mLastHorizDPI != LLFontGL::sHorizDPI)
        || (mLastResGeneration != LLFontGL::sResolutionGeneration)
        || (mLastFontCacheGen != fontp->getCacheGeneration());

    if (needs_recalc)
    {
        // Calculate width using the font
        mWidth = fontp->getWidthF32(wchars, begin_offset, max_chars, no_padding);

        // Cache the parameters
        mLastFont = fontp;
        mLastOffset = begin_offset;
        mLastMaxChars = max_chars;
        mLastNoPadding = no_padding;
        mLastScaleX = LLFontGL::sScaleX;
        mLastScaleY = LLFontGL::sScaleY;
        mLastVertDPI = LLFontGL::sVertDPI;
        mLastHorizDPI = LLFontGL::sHorizDPI;
        mLastResGeneration = LLFontGL::sResolutionGeneration;
        mLastFontCacheGen = fontp->getCacheGeneration();
    }

    return mWidth;
}
