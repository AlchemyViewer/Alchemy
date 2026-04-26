/**
 * @file llfontgl.cpp
 * @brief Wrapper around FreeType
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "llfontgl.h"

// Linden library includes
#include "llcontrol.h"
#include "llfasttimer.h"
#include "llfontfreetype.h"
#include "llfontbitmapcache.h"
#include "llfontregistry.h"
#include "llfontshaping.h"
#include "llgl.h"
#include "llimagegl.h"
#include "llrender.h"
#include "llstl.h"
#include "v4color.h"
#include "lltexture.h"
#include "lldir.h"
#include "llstring.h"

// Third party library includes
#include <boost/tokenizer.hpp>

#if LL_WINDOWS
#include <Shlobj.h>
#include <Knownfolders.h>
#include <Objbase.h>
#endif // LL_WINDOWS

const S32 BOLD_OFFSET = 1;

// static class members
F32 LLFontGL::sVertDPI = 96.f;
F32 LLFontGL::sHorizDPI = 96.f;
F32 LLFontGL::sScaleX = 1.f;
F32 LLFontGL::sScaleY = 1.f;
S32 LLFontGL::sResolutionGeneration = 0;
bool LLFontGL::sDisplayFont = true ;
std::string LLFontGL::sAppDir;

LLColor4 LLFontGL::sShadowColor(0.f, 0.f, 0.f, 1.f);
LLFontRegistry* LLFontGL::sFontRegistry = NULL;

LLCoordGL LLFontGL::sCurOrigin;
F32 LLFontGL::sCurDepth;
std::vector<std::pair<LLCoordGL, F32> > LLFontGL::sOriginStack;

const F32 PAD_UVY = 0.5f; // half of vertical padding between glyphs in the glyph texture
const F32 DROP_SHADOW_SOFT_STRENGTH = 0.3f;

LLFontGL::LLFontGL()
{
}

LLFontGL::~LLFontGL()
{
}

void LLFontGL::reset()
{
    mFontFreetype->reset(sVertDPI, sHorizDPI);
}

void LLFontGL::destroyGL()
{
    mFontFreetype->destroyGL();
}

bool LLFontGL::loadFace(const std::string& filename, F32 point_size, const F32 vert_dpi, const F32 horz_dpi, S32 weight, bool is_fallback, S32 face_n, EFontHinting hinting, S32 flags)
{
    if(mFontFreetype == reinterpret_cast<LLFontFreetype*>(NULL))
    {
        mFontFreetype = new LLFontFreetype;
    }

    return mFontFreetype->loadFace(filename, point_size, vert_dpi, horz_dpi, weight, is_fallback, face_n, hinting, flags);
}

S32 LLFontGL::getNumFaces(const std::string& filename)
{
    if (mFontFreetype == reinterpret_cast<LLFontFreetype*>(NULL))
    {
        mFontFreetype = new LLFontFreetype;
    }

    return mFontFreetype->getNumFaces(filename);
}

S32 LLFontGL::getCacheGeneration() const
{
    const LLFontBitmapCache* font_bitmap_cache = mFontFreetype->getFontBitmapCache();
    return font_bitmap_cache->getCacheGeneration();
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, const LLRect& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
    ShadowType shadow, S32 max_chars, F32* right_x, bool use_ellipses, bool use_color) const
{
    LLRectf rect_float((F32)rect.mLeft, (F32)rect.mTop, (F32)rect.mRight, (F32)rect.mBottom);
    return render(wstr, begin_offset, rect_float, color, halign, valign, style, shadow, max_chars, right_x, use_ellipses, use_color);
}

S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, const LLRectf& rect, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                     ShadowType shadow, S32 max_chars, F32* right_x, bool use_ellipses, bool use_color) const
{
    F32 x = rect.mLeft;
    F32 y = 0.f;

    switch(valign)
    {
    case TOP:
        y = rect.mTop;
        break;
    case VCENTER:
        y = rect.getCenterY();
        break;
    case BASELINE:
    case BOTTOM:
        y = rect.mBottom;
        break;
    default:
        y = rect.mBottom;
        break;
    }
    return render(wstr, begin_offset, x, y, color, halign, valign, style, shadow, max_chars, (S32)rect.getWidth(), right_x, use_ellipses, use_color);
}


S32 LLFontGL::render(const LLWString &wstr, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style,
                     ShadowType shadow, S32 max_chars, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    if(!sDisplayFont) //do not display texts
    {
        return static_cast<S32>(wstr.length());
    }

    if (wstr.empty())
    {
        return 0;
    }

    gGL.getTexUnit(0)->enable(LLTexUnit::TT_TEXTURE);

    S32 scaled_max_pixels = max_pixels == S32_MAX ? S32_MAX : llceil((F32)max_pixels * sScaleX);

    // determine which style flags need to be added programmatically by stripping off the
    // style bits that are drawn by the underlying Freetype font
    U8 style_to_add = (style | mFontDescriptor.getStyle()) & ~mFontFreetype->getStyle();

    F32 drop_shadow_strength = 0.f;
    if (shadow != NO_SHADOW)
    {
        F32 luminance;
        color.calcHSL(NULL, NULL, &luminance);
        drop_shadow_strength = clamp_rescale(luminance, 0.35f, 0.6f, 0.f, 1.f);
        if (luminance < 0.35f)
        {
            shadow = NO_SHADOW;
        }
    }

    gGL.pushUIMatrix();

    gGL.loadUIIdentity();

    LLVector2 origin(floorf(sCurOrigin.mX*sScaleX), floorf(sCurOrigin.mY*sScaleY));

    // Depth translation, so that floating text appears 'in-world'
    // and is correctly occluded.
    gGL.translatef(0.f,0.f,sCurDepth);

    S32 chars_drawn = 0;
    S32 i;
    S32 length;

    if (-1 == max_chars)
    {
        max_chars = length = (S32)wstr.length() - begin_offset;
    }
    else
    {
        length = llmin((S32)wstr.length() - begin_offset, max_chars );
    }

    F32 cur_x, cur_y, cur_render_x, cur_render_y;

    // Not guaranteed to be set correctly
    gGL.setSceneBlendType(LLRender::BT_ALPHA);

    // Subpixel pen position: cumulative advance stays fractional through the
    // loop so HarfBuzz GPOS kerning isn't crushed by per-glyph ll_round. Each
    // glyph still snaps to integer pixels at draw time via ll_round on
    // glyph_x in the screen_rect construction below — atlas bitmaps are
    // pixel-aligned, only the pen position is sub-pixel. False for
    // HINTING_DEFAULT (foundry hints align to integer grid), true for
    // FORCE_AUTOHINT and NO_HINTING.
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    cur_x = ((F32)x * sScaleX) + origin.mV[VX];
    cur_y = ((F32)y * sScaleY) + origin.mV[VY];

    // Offset y by vertical alignment.
    // use unscaled font metrics here
    switch (valign)
    {
    case TOP:
        cur_y -= llceil(mFontFreetype->getAscenderHeight());
        break;
    case BOTTOM:
        cur_y += llceil(mFontFreetype->getDescenderHeight());
        break;
    case VCENTER:
        cur_y -= llceil((llceil(mFontFreetype->getAscenderHeight()) - llceil(mFontFreetype->getDescenderHeight())) / 2.f);
        break;
    case BASELINE:
        // Baseline, do nothing.
        break;
    default:
        break;
    }

    switch (halign)
    {
    case LEFT:
        break;
    case RIGHT:
        cur_x -= llmin(scaled_max_pixels, ll_round(getWidthF32(wstr.c_str(), begin_offset, length) * sScaleX));
        break;
    case HCENTER:
        cur_x -= llmin(scaled_max_pixels, ll_round(getWidthF32(wstr.c_str(), begin_offset, length) * sScaleX)) / 2;
        break;
    default:
        break;
    }

    cur_render_y = cur_y;
    cur_render_x = cur_x;

    F32 start_x = (F32)ll_round(cur_x);

    const LLFontBitmapCache* font_bitmap_cache = mFontFreetype->getFontBitmapCache();

    // This looks wrong, value is dynamic.
    // LLFontBitmapCache::nextOpenPos can alter these values when
    // new characters get added to cache, which affects whole string.
    // Todo: Perhaps value should update after symbols were added?
    F32 inv_width = 1.f / font_bitmap_cache->getBitmapWidth();
    F32 inv_height = 1.f / font_bitmap_cache->getBitmapHeight();

    const S32 LAST_CHARACTER = LLFontFreetype::LAST_CHAR_FULL;

    bool draw_ellipses = false;
    if (use_ellipses)
    {
        // check for too long of a string
        S32 string_width = ll_round(getWidthF32(wstr.c_str(), begin_offset, max_chars) * sScaleX);
        if (string_width > scaled_max_pixels)
        {
            // use four dots for ellipsis width to generate padding
            const LLWString dots(U"....");
            scaled_max_pixels = llmax(0, scaled_max_pixels - ll_round(getWidthF32(dots.c_str())));
            draw_ellipses = true;
        }
    }

    const LLFontGlyphInfo* next_glyph = NULL;

    // string can have more than one glyph per char (ex: bold or shadow),
    // make sure that GLYPH_BATCH_SIZE won't end up with half a symbol.
    // See drawGlyph.
    // Ex: with shadows it's 6 glyps per char. 30 fits exactly 5 chars.
    static constexpr S32 GLYPH_BATCH_SIZE = 30;
    static thread_local LLVector4a vertices[GLYPH_BATCH_SIZE * 6];
    static thread_local LLVector2 uvs[GLYPH_BATCH_SIZE * 6];
    static thread_local LLColor4U colors[GLYPH_BATCH_SIZE * 6];

    LLColor4U text_color(color);
    // Preserve the transparency to render fading emojis in fading text (e.g.
    // for the chat console)... HB
    LLColor4U emoji_color(255, 255, 255, text_color.mV[VALPHA]);

    std::pair<EFontGlyphType, S32> bitmap_entry = std::make_pair(EFontGlyphType::Grayscale, -1);
    S32 glyph_count = 0;
    llwchar last_char = wstr[begin_offset];

    // Strict-monospace root face never participates in whole-slice shaping —
    // even synthesized "1 glyph per cp" output goes through the renderer's
    // shaped branch which uses a different glyph cache (mShapedGlyphInfoMap)
    // and atlas position than the codepoint path's mCharGlyphInfoMap, and
    // the divergent atlas slots produce visible rendering differences in
    // monospace contexts. Fall back to legacy emoji-cluster shape ranges so
    // ASCII goes through the codepoint path (guaranteed visual parity with
    // toggle-off) while embedded emoji clusters still shape via HarfBuzz.
    const bool root_strict_mono = mFontFreetype->isFixedWidth()
                               && !mFontFreetype->getAllowMonospaceLigatures();
    std::vector<std::pair<size_t, size_t>> shape_ranges;
    if (length > 0 && !root_strict_mono)
    {
        // Single all-encompassing shape range; the loop's shaped path below
        // will consume the entire string in one HarfBuzz-positioned pass.
        shape_ranges.emplace_back(static_cast<size_t>(begin_offset),
                                  static_cast<size_t>(begin_offset + length));
    }
    else
    {
        shape_ranges = wstring_find_emoji_clusters(LLWStringView(wstr.data() + begin_offset, length));
        for (auto& r : shape_ranges)
        {
            r.first  += begin_offset;
            r.second += begin_offset;
        }
    }
    std::vector<std::vector<LLShapedGlyph>> shape_glyphs(shape_ranges.size());
    for (size_t s = 0; s < shape_ranges.size(); ++s)
    {
        LLFontShaping::shapeRun(mFontFreetype, wstr,
                                shape_ranges[s].first,
                                shape_ranges[s].second,
                                shape_glyphs[s]);
    }
    size_t next_shape_run = 0;

    for (i = begin_offset; i < begin_offset + length; i++)
    {
        // Entered a shaped run? Emit its HarfBuzz-positioned glyphs in one
        // go and jump past the range. When shaping produced no glyphs (rare —
        // face/HB failure) we fall through to the codepoint path so the
        // range still draws something, even if ZWJ presentation is wrong.
        if (next_shape_run < shape_ranges.size()
            && (S32)shape_ranges[next_shape_run].first == i)
        {
            const auto  run_range  = shape_ranges[next_shape_run];
            const auto& run_glyphs = shape_glyphs[next_shape_run];
            ++next_shape_run;

            if (!run_glyphs.empty())
            {
                next_glyph = NULL;  // drop any kerning prefetch from before the run
                bool overflow = false;
                for (const LLShapedGlyph& sg : run_glyphs)
                {
                    // Cache lives on the root face and its bitmap atlas; the
                    // fallback face is only the *source* for the glyph. This
                    // mirrors the non-shaped fallback path in addGlyphFromFont
                    // where `this` is the root and `fontp` is the fallback.
                    const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                        sg.face, sg.glyph_id,
                        (!use_color) ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
                    if (!sfgi)
                        continue;

                    std::pair<EFontGlyphType, S32> next_bitmap_entry = sfgi->mBitmapEntry;
                    if (next_bitmap_entry != bitmap_entry)
                    {
                        if (glyph_count > 0)
                        {
                            gGL.begin(LLRender::TRIANGLES);
                            gGL.vertexBatchPreTransformed(vertices, uvs, colors, glyph_count * 6);
                            gGL.end();
                            glyph_count = 0;
                        }
                        bitmap_entry = next_bitmap_entry;
                        LLImageGL* font_image = font_bitmap_cache->getImageGL(bitmap_entry.first, bitmap_entry.second);
                        gGL.getTexUnit(0)->bind(font_image);
                    }

                    const F32 glyph_x = cur_render_x + sg.x_offset + (F32)sfgi->mXBearing;
                    const F32 glyph_y = cur_render_y + sg.y_offset + (F32)sfgi->mYBearing;

                    if ((start_x + scaled_max_pixels) < (glyph_x + (F32)sfgi->mWidth))
                    {
                        overflow = true;
                        break;
                    }

                    LLRectf uv_rect(sfgi->mXBitmapOffset * inv_width,
                                    (sfgi->mYBitmapOffset + sfgi->mHeight + PAD_UVY) * inv_height,
                                    (sfgi->mXBitmapOffset + sfgi->mWidth) * inv_width,
                                    (sfgi->mYBitmapOffset - PAD_UVY) * inv_height);
                    LLRectf screen_rect((F32)ll_round(glyph_x),
                                        (F32)ll_round(glyph_y),
                                        (F32)ll_round(glyph_x) + (F32)sfgi->mWidth,
                                        (F32)ll_round(glyph_y) - (F32)sfgi->mHeight);

                    if (glyph_count >= GLYPH_BATCH_SIZE)
                    {
                        gGL.begin(LLRender::TRIANGLES);
                        gGL.vertexBatchPreTransformed(vertices, uvs, colors, glyph_count * 6);
                        gGL.end();
                        glyph_count = 0;
                    }

                    const LLColor4U& col = bitmap_entry.first == EFontGlyphType::Grayscale
                                               ? text_color : emoji_color;
                    drawGlyph(glyph_count, vertices, uvs, colors, screen_rect, uv_rect,
                              col, style_to_add, shadow, drop_shadow_strength);

                    cur_x += sg.x_advance;
                    cur_y += sg.y_advance;
                    if (!subpixel_pen)
                        cur_x = (F32)ll_round(cur_x);
                    cur_render_x = cur_x;
                    cur_render_y = cur_y;
                }

                chars_drawn += (S32)(run_range.second - run_range.first);
                if (overflow)
                    break;

                i = (S32)run_range.second - 1;  // loop's ++ lands past the run
                // Force a rebind on the next non-shape glyph — the duplicate
                // last_char guard relies on matching wch to avoid stale atlas
                // binds and shaped runs bypassed that path.
                last_char = 0;
                continue;
            }
            // Empty run_glyphs — shaping failed. Fall through to the
            // codepoint path for this iteration.
        }

        llwchar wch = wstr[i];

        const LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, (!use_color) ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
        }
        if (!fgi)
        {
            LL_ERRS() << "Missing Glyph Info" << LL_ENDL;
            break;
        }
        // Per-glyph bitmap texture.
        std::pair<EFontGlyphType, S32> next_bitmap_entry = fgi->mBitmapEntry;
        if (next_bitmap_entry != bitmap_entry || last_char != wch)
        {
            // Actually draw the queued glyphs before switching their texture;
            // otherwise the queued glyphs will be taken from wrong textures.
            if (glyph_count > 0)
            {
                gGL.begin(LLRender::TRIANGLES);
                {
                    gGL.vertexBatchPreTransformed(vertices, uvs, colors, glyph_count * 6);
                }
                gGL.end();
                glyph_count = 0;
            }

            bitmap_entry = next_bitmap_entry;
            LLImageGL* font_image = font_bitmap_cache->getImageGL(bitmap_entry.first, bitmap_entry.second);
            gGL.getTexUnit(0)->bind(font_image);

            // For some reason it's not enough to compare by bitmap_entry.
            // Issue hits emojis, japenese and chinese glyphs, only on first run.
            // Todo: figure it out, there might be a bug with raw image data.
            last_char = wch;
        }

        if ((start_x + scaled_max_pixels) < (cur_x + fgi->mXBearing + fgi->mWidth))
        {
            // Not enough room for this character.
            break;
        }

        // Draw the text at the appropriate location
        //Specify vertices and texture coordinates
        LLRectf uv_rect((fgi->mXBitmapOffset) * inv_width,
                (fgi->mYBitmapOffset + fgi->mHeight + PAD_UVY) * inv_height,
                (fgi->mXBitmapOffset + fgi->mWidth) * inv_width,
                (fgi->mYBitmapOffset - PAD_UVY) * inv_height);
        // snap glyph origin to whole screen pixel
        LLRectf screen_rect((F32)ll_round(cur_render_x + (F32)fgi->mXBearing),
                    (F32)ll_round(cur_render_y + (F32)fgi->mYBearing),
                    (F32)ll_round(cur_render_x + (F32)fgi->mXBearing) + (F32)fgi->mWidth,
                    (F32)ll_round(cur_render_y + (F32)fgi->mYBearing) - (F32)fgi->mHeight);

        if (glyph_count >= GLYPH_BATCH_SIZE)
        {
            gGL.begin(LLRender::TRIANGLES);
            {
                gGL.vertexBatchPreTransformed(vertices, uvs, colors, glyph_count * 6);
            }
            gGL.end();

            glyph_count = 0;
        }

        const LLColor4U& col =
            bitmap_entry.first == EFontGlyphType::Grayscale ? text_color
                                                            : emoji_color;
        drawGlyph(glyph_count, vertices, uvs, colors, screen_rect, uv_rect,
                  col, style_to_add, shadow, drop_shadow_strength);

        chars_drawn++;
        cur_x += fgi->mXAdvance;
        cur_y += fgi->mYAdvance;

        llwchar next_char = wstr[i+1];
        if (next_char && (next_char < LAST_CHARACTER))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(next_char, (!use_color) ? EFontGlyphType::Grayscale : EFontGlyphType::Color);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }

        // Round after kerning. With subpixel_pen on the cumulative advance
        // stays fractional through the loop and only the per-glyph draw rect
        // (above) snaps to integer pixels. Without subpixel_pen we keep the
        // legacy per-glyph round so native-hinted glyphs stay on the
        // integer grid the foundry designed them for.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);

        cur_render_x = cur_x;
        cur_render_y = cur_y;
    }

    gGL.begin(LLRender::TRIANGLES);
    {
        gGL.vertexBatchPreTransformed(vertices, uvs, colors, glyph_count * 6);
    }
    gGL.end();


    if (right_x)
    {
        *right_x = (cur_x - origin.mV[VX]) / sScaleX;
    }

    //FIXME: add underline as glyph?
    if (style_to_add & UNDERLINE)
    {
        F32 descender = (F32)llfloor(mFontFreetype->getDescenderHeight());

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.begin(LLRender::LINES);
        gGL.vertex2f(start_x, cur_y - descender);
        gGL.vertex2f(cur_x, cur_y - descender);
        gGL.end();
    }

    if (draw_ellipses)
    {
        // recursively render ellipses at end of string
        // we've already reserved enough room
        static const LLWString elipses_wstr(U"...");
        render(elipses_wstr,
                0,
                (cur_x - origin.mV[VX]) / sScaleX, (F32)y,
                color,
                LEFT, valign,
                style_to_add,
                shadow,
                S32_MAX, max_pixels,
                right_x,
                false,
                use_color);
    }

    gGL.popUIMatrix();

    return chars_drawn;
}

S32 LLFontGL::render(const LLWString &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color) const
{
    return render(text, begin_offset, x, y, color, LEFT, BASELINE, NORMAL, NO_SHADOW);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, F32 x, F32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow, S32 max_chars, S32 max_pixels, F32* right_x, bool use_ellipses, bool use_color) const
{
    return render(utf8str_to_wstring(text), begin_offset, x, y, color, halign, valign, style, shadow, max_chars, max_pixels, right_x, use_ellipses, use_color);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color) const
{
    return renderUTF8(text, begin_offset, (F32)x, (F32)y, color, LEFT, BASELINE, NORMAL, NO_SHADOW);
}

S32 LLFontGL::renderUTF8(const std::string &text, S32 begin_offset, S32 x, S32 y, const LLColor4 &color, HAlign halign, VAlign valign, U8 style, ShadowType shadow) const
{
    return renderUTF8(text, begin_offset, (F32)x, (F32)y, color, halign, valign, style, shadow);
}

// font metrics - override for LLFontFreetype that returns units of virtual pixels
F32 LLFontGL::getAscenderHeight() const
{
    return mFontFreetype->getAscenderHeight() / sScaleY;
}

F32 LLFontGL::getDescenderHeight() const
{
    return mFontFreetype->getDescenderHeight() / sScaleY;
}

S32 LLFontGL::getLineHeight() const
{
    // Glyph bounding-box height (no foundry line gap). Single ceil avoids
    // the double-ceil over-estimate that older `ceil(asc) + ceil(desc)`
    // produced when both fractional parts were non-zero. For full
    // baseline-to-baseline distance including the font's recommended line
    // gap, use getLineSpacing().
    return llceil((mFontFreetype->getAscenderHeight() + mFontFreetype->getDescenderHeight()) / sScaleY);
}

S32 LLFontGL::getLineSpacing() const
{
    // Foundry-recommended baseline-to-baseline distance — face->height,
    // which is ascender + descender + lineGap. Used by multi-line text
    // layout so consecutive lines sit at the spacing the font designer
    // intended; for many screen fonts lineGap is 0 and this matches
    // getLineHeight().
    return llceil(mFontFreetype->getLineHeight() / sScaleY);
}

S32 LLFontGL::getWidth(const std::string& utf8text) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidth(wtext.c_str(), 0, S32_MAX);
}

S32 LLFontGL::getWidth(const llwchar* wchars) const
{
    return getWidth(wchars, 0, S32_MAX);
}

S32 LLFontGL::getWidth(const std::string& utf8text, S32 begin_offset, S32 max_chars) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidth(wtext.c_str(), begin_offset, max_chars);
}

S32 LLFontGL::getWidth(const llwchar* wchars, S32 begin_offset, S32 max_chars) const
{
    F32 width = getWidthF32(wchars, begin_offset, max_chars);
    // llceil, not ll_round: getWidth's contract is "minimum integer pixel
    // width that contains the rendered text". With subpixel pen position
    // (mUseSubpixelPen), getWidthF32 returns fractional widths; ll_round
    // would truncate fractions < 0.5 (e.g. 28.4 -> 28) and clip the last
    // glyph in callers that size layout rects to the returned width
    // (LLButton::resize, scroll list cells, tooltip backgrounds, etc.).
    return llceil(width);
}

F32 LLFontGL::getWidthF32(const std::string& utf8text) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidthF32(wtext.c_str(), 0, S32_MAX);
}

F32 LLFontGL::getWidthF32(const llwchar* wchars) const
{
    return getWidthF32(wchars, 0, S32_MAX);
}

F32 LLFontGL::getWidthF32(const std::string& utf8text, S32 begin_offset, S32 max_chars) const
{
    LLWString wtext = utf8str_to_wstring(utf8text);
    return getWidthF32(wtext.c_str(), begin_offset, max_chars);
}

F32 LLFontGL::getWidthF32(const llwchar* wchars, S32 begin_offset, S32 max_chars, bool no_padding) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    const S32 LAST_CHARACTER = LLFontFreetype::LAST_CHAR_FULL;

    F32 cur_x = 0;
    const S32 max_index = begin_offset + max_chars;

    // Mirror render()'s pen accumulation policy so width measurements agree
    // with what's drawn — see the comment at render()'s subpixel_pen.
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    // Determine the tight slice we'll actually measure (bounded by max_chars
    // and the first NUL) so shaping only runs over real content.
    S32 measure_end = begin_offset;
    while (measure_end < max_index && wchars[measure_end] != 0)
        ++measure_end;
    const S32 measure_len = measure_end - begin_offset;

    // Same shape-first preprocessing as render(); kept consistent so caret
    // positions and ellipsis cutoffs agree with what's drawn. Strict-monospace
    // root face skips whole-slice shaping (visual parity with codepoint path);
    // see render() for the rationale.
    const bool root_strict_mono = mFontFreetype->isFixedWidth()
                               && !mFontFreetype->getAllowMonospaceLigatures();
    std::vector<std::pair<size_t, size_t>> shape_ranges;
    if (measure_len > 0)
    {
        if (!root_strict_mono)
        {
            shape_ranges.emplace_back(static_cast<size_t>(0),
                                      static_cast<size_t>(measure_len));
        }
        else
        {
            shape_ranges = wstring_find_emoji_clusters(
                LLWStringView(wchars + begin_offset, (size_t)measure_len));
        }
    }
    std::vector<std::vector<LLShapedGlyph>> shape_glyphs(shape_ranges.size());
    // LLFontShaping needs an LLWString, not a raw pointer; build one only if
    // we actually have shape ranges to feed it.
    if (!shape_ranges.empty())
    {
        LLWString slice(wchars + begin_offset, wchars + measure_end);
        for (size_t s = 0; s < shape_ranges.size(); ++s)
        {
            const size_t rb = shape_ranges[s].first;
            const size_t re = shape_ranges[s].second;
            shape_ranges[s].first  = rb + begin_offset;
            shape_ranges[s].second = re + begin_offset;
            LLFontShaping::shapeRun(mFontFreetype, slice, rb, re, shape_glyphs[s]);
        }
    }
    size_t next_shape_run = 0;

    const LLFontGlyphInfo* next_glyph = NULL;

    F32 width_padding = 0.f;
    for (S32 i = begin_offset; i < max_index && wchars[i] != 0; i++)
    {
        if (next_shape_run < shape_ranges.size()
            && (S32)shape_ranges[next_shape_run].first == i)
        {
            const auto  run_range  = shape_ranges[next_shape_run];
            const auto& run_glyphs = shape_glyphs[next_shape_run];
            ++next_shape_run;

            if (!run_glyphs.empty())
            {
                next_glyph = NULL;
                F32 run_padding = 0.f;
                for (const LLShapedGlyph& sg : run_glyphs)
                {
                    const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                        sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                    if (!sfgi)
                        continue;
                    if (!no_padding)
                    {
                        run_padding = llmax(0.f,
                            run_padding - sg.x_advance,
                            (F32)(sfgi->mWidth + sfgi->mXBearing) - sg.x_advance);
                    }
                    cur_x += sg.x_advance;
                    // Match render()'s pen motion so caret positions and
                    // ellipsis cutoffs agree with what's drawn.
                    if (!subpixel_pen)
                        cur_x = (F32)ll_round(cur_x);
                }
                width_padding = run_padding;
                i = (S32)run_range.second - 1;
                continue;
            }
            // Fall through to codepoint path when shaping failed.
        }

        llwchar wch = wchars[i];

        const LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);
        }

        F32 advance = mFontFreetype->getXAdvance(fgi);

        if (!no_padding)
        {
            // for the last character we want to measure the greater of its width and xadvance values
            // so keep track of the difference between these values for the each character we measure
            // so we can fix things up at the end
            width_padding = llmax(0.f,                                          // always use positive padding amount
                width_padding - advance,                        // previous padding left over after advance of current character
                (F32)(fgi->mWidth + fgi->mXBearing) - advance); // difference between width of this character and advance to next character
        }

        cur_x += advance;
        llwchar next_char = wchars[i+1];

        if (((i + 1) < begin_offset + max_chars)
            && next_char
            && (next_char < LAST_CHARACTER))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(next_char, EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }
        // Round after kerning. With subpixel_pen on, accumulator stays
        // fractional so cumulative kerning precision survives.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);
    }

    if (!no_padding)
    {
        // add in extra pixels for last character's width past its xadvance
        cur_x += width_padding;
    }

    return cur_x / sScaleX;
}

void LLFontGL::generateASCIIglyphs()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    for (U32 i = 32; (i < 127); i++)
    {
        mFontFreetype->getGlyphInfo(i, EFontGlyphType::Grayscale);
    }
}

// Returns the max number of complete characters from text (up to max_chars) that can be drawn in max_pixels
S32 LLFontGL::maxDrawableChars(const llwchar* wchars, F32 max_pixels, S32 max_chars, EWordWrapStyle end_on_word_boundary) const
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    if (!wchars || !wchars[0] || max_chars == 0)
    {
        return 0;
    }

    llassert(max_pixels >= 0.f);
    llassert(max_chars >= 0);

    bool clip = false;
    F32 cur_x = 0;

    S32 start_of_last_word = 0;
    bool in_word = false;

    // avoid S32 overflow when max_pixels == S32_MAX by staying in floating point
    F32 scaled_max_pixels = max_pixels * sScaleX;
    F32 width_padding = 0.f;

    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    LLFontGlyphInfo* next_glyph = NULL;

    // Pre-shape the entire slice when FontShapeAllText is on so advance accounts
    // for GPOS pair-kerning and ligatures. The codepoint loop below uses a
    // parallel walker (shape_idx) to map shaped advances onto codepoints — a
    // ligature glyph spans multiple cps but its full advance fires on the
    // cluster's start cp, so trailing cps of the cluster contribute zero.
    // Skip for strict-monospace root face — see render() for rationale.
    const bool root_strict_mono = mFontFreetype->isFixedWidth()
                               && !mFontFreetype->getAllowMonospaceLigatures();
    std::vector<LLShapedGlyph> shape_glyphs;
    size_t shape_idx = 0;
    if (!root_strict_mono && max_chars > 0 && wchars[0])
    {
        S32 measure_end = 0;
        while (measure_end < max_chars && wchars[measure_end] != 0)
            ++measure_end;
        if (measure_end > 0)
        {
            LLWString slice(wchars, wchars + measure_end);
            LLFontShaping::shapeRun(mFontFreetype, slice, 0, (size_t)measure_end, shape_glyphs);
        }
    }
    const bool use_shaped = !shape_glyphs.empty();

    S32 i;
    for (i=0; (i < max_chars); i++)
    {
        llwchar wch = wchars[i];

        if(wch == 0)
        {
            // Null terminator.  We're done.
            break;
        }

        if (in_word)
        {
            if (LLStringOps::isSpace(wch))
            {
                if(wch !=(0x00A0))
                {
                    in_word = false;
                }
            }
            if (iswindividual(wch))
            {
                if (LLStringOps::isPunct(wchars[i+1]))
                {
                    in_word=true;
                }
                else
                {
                    in_word=false;
                    start_of_last_word = i;
                }
            }
        }
        else
        {
            start_of_last_word = i;
            if (!LLStringOps::isSpace(wch) || !iswindividual(wch))
            {
                in_word = true;
            }
        }

        if (use_shaped)
        {
            // Sum advances and the largest extent of any glyph whose cluster
            // lands on this codepoint. Trailing codepoints of a multi-cp
            // cluster (ligatures, ZWJ sequences) consume zero glyphs and
            // pass through with cur_x unchanged.
            F32 advance_this = 0.f;
            F32 extent_this  = 0.f;
            while (shape_idx < shape_glyphs.size()
                   && shape_glyphs[shape_idx].cluster <= i)
            {
                const auto& sg = shape_glyphs[shape_idx];
                advance_this += sg.x_advance;
                const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                    sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                if (sfgi)
                    extent_this = llmax(extent_this, (F32)(sfgi->mWidth + sfgi->mXBearing));
                ++shape_idx;
            }
            width_padding = llmax(0.f,
                                  width_padding - advance_this,
                                  extent_this - advance_this);
            cur_x += advance_this;
            if (scaled_max_pixels < cur_x + width_padding)
            {
                clip = true;
                break;
            }
            if (!subpixel_pen)
                cur_x = (F32)ll_round(cur_x);
            continue;
        }

        LLFontGlyphInfo* fgi = next_glyph;
        next_glyph = NULL;
        if(!fgi)
        {
            fgi = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);

            if (NULL == fgi)
            {
                return 0;
            }
        }

        // account for glyphs that run beyond the starting point for the next glyphs
        width_padding = llmax(  0.f,                                                    // always use positive padding amount
                                width_padding - fgi->mXAdvance,                         // previous padding left over after advance of current character
                                (F32)(fgi->mWidth + fgi->mXBearing) - fgi->mXAdvance);  // difference between width of this character and advance to next character

        cur_x += fgi->mXAdvance;

        // clip if current character runs past scaled_max_pixels (using width_padding)
        if (scaled_max_pixels < cur_x + width_padding)
        {
            clip = true;
            break;
        }

        if (((i+1) < max_chars) && wchars[i+1])
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(wchars[i+1], EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(fgi, next_glyph);
        }

        // Round after kerning.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);
    }

    if( clip )
    {
        switch (end_on_word_boundary)
        {
        case ONLY_WORD_BOUNDARIES:
            i = start_of_last_word;
            break;
        case WORD_BOUNDARY_IF_POSSIBLE:
            if (start_of_last_word != 0)
            {
                i = start_of_last_word;
            }
            break;
        default:
        case ANYWHERE:
            // do nothing
            break;
        }
    }
    return i;
}

S32 LLFontGL::firstDrawableChar(const llwchar* wchars, F32 max_pixels, S32 text_len, S32 start_pos, S32 max_chars) const
{
    if (!wchars || !wchars[0] || max_chars == 0)
    {
        return 0;
    }

    F32 total_width = 0.0;
    S32 drawable_chars = 0;

    F32 scaled_max_pixels = max_pixels * sScaleX;
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    S32 start = llmin(start_pos, text_len - 1);

    // pre-shape [0, start+1) and project advances onto a
    // per-codepoint table. Walking backward through that table substitutes for
    // the legacy fgi->mXAdvance + getXKerning chain. Ligatures and ZWJ
    // clusters get their full advance attributed to the cluster's first cp;
    // trailing cps contribute zero, which is what we want for measurement.
    // Skip for strict-monospace root face — see render() for rationale.
    const bool root_strict_mono = mFontFreetype->isFixedWidth()
                               && !mFontFreetype->getAllowMonospaceLigatures();
    std::vector<F32> per_cp_advance;
    F32 per_cp_last_extent = 0.f;
    if (!root_strict_mono && start >= 0 && wchars[0])
    {
        LLWString slice(wchars, wchars + start + 1);
        std::vector<LLShapedGlyph> shape_glyphs;
        LLFontShaping::shapeRun(mFontFreetype, slice, 0, (size_t)(start + 1), shape_glyphs);
        if (!shape_glyphs.empty())
        {
            per_cp_advance.assign(start + 1, 0.f);
            for (const auto& sg : shape_glyphs)
            {
                if (sg.cluster >= 0 && sg.cluster <= start)
                {
                    per_cp_advance[sg.cluster] += sg.x_advance;
                    if (sg.cluster == start)
                    {
                        const LLFontGlyphInfo* sfgi = mFontFreetype->getGlyphInfoByIndex(
                            sg.face, sg.glyph_id, EFontGlyphType::Unspecified);
                        if (sfgi)
                            per_cp_last_extent = llmax(per_cp_last_extent,
                                                       (F32)(sfgi->mWidth + sfgi->mXBearing));
                    }
                }
            }
        }
    }
    const bool use_shaped = !per_cp_advance.empty();

    for (S32 i = start; i >= 0; i--)
    {
        if (use_shaped)
        {
            // Last cp uses extent so the rightmost glyph stays fully visible.
            F32 width = (i == start)
                        ? llmax(per_cp_last_extent, per_cp_advance[i])
                        : per_cp_advance[i];
            if (scaled_max_pixels < (total_width + width))
                break;
            total_width += width;
            drawable_chars++;
            if (max_chars >= 0 && drawable_chars >= max_chars)
                break;
            if (!subpixel_pen)
                total_width = (F32)ll_round(total_width);
            continue;
        }

        llwchar wch = wchars[i];

        const LLFontGlyphInfo* fgi= mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);

        // last character uses character width, since the whole character needs to be visible
        // other characters just use advance
        F32 width = (i == start)
            ? (F32)(fgi->mWidth + fgi->mXBearing)   // use actual width for last character
            : fgi->mXAdvance;                       // use advance for all other characters

        if( scaled_max_pixels < (total_width + width) )
        {
            break;
        }

        total_width += width;
        drawable_chars++;

        if( max_chars >= 0 && drawable_chars >= max_chars )
        {
            break;
        }

        if ( i > 0 )
        {
            // kerning
            total_width += mFontFreetype->getXKerning(wchars[i-1], wch);
        }

        // Round after kerning.
        if (!subpixel_pen)
            total_width = (F32)ll_round(total_width);
    }

    if (drawable_chars == 0)
    {
        return start_pos; // just draw last character
    }
    else
    {
        // if only 1 character is drawable, we want to return start_pos as the first character to draw
        // if 2 are drawable, return start_pos and character before start_pos, etc.
        return start_pos + 1 - drawable_chars;
    }

}

S32 LLFontGL::charFromPixelOffset(const llwchar* wchars, S32 begin_offset, F32 target_x, F32 max_pixels, S32 max_chars, bool round) const
{
    if (!wchars || !wchars[0] || max_chars == 0)
    {
        return 0;
    }

    F32 cur_x = 0;
    const bool subpixel_pen = mFontFreetype->useSubpixelPen();

    target_x *= sScaleX;

    // max_chars is S32_MAX by default, so make sure we don't get overflow
    const S32 max_index = begin_offset + llmin(S32_MAX - begin_offset, max_chars - 1);

    F32 scaled_max_pixels = max_pixels * sScaleX;

    // Locate the tight slice we'll consider (bounded by max_chars and the
    // first NUL) so we can shape the same window render() does. Without
    // this, per-codepoint advances disagree with the rendered composite
    // glyph: clicks land in the middle of an emoji cluster instead of on
    // its edges.
    S32 slice_end = begin_offset;
    while (slice_end < max_index && wchars[slice_end] != 0)
        ++slice_end;
    const S32 slice_len = slice_end - begin_offset;

    const bool root_strict_mono = mFontFreetype->isFixedWidth()
                               && !mFontFreetype->getAllowMonospaceLigatures();
    std::vector<std::pair<size_t, size_t>> shape_ranges;
    if (slice_len > 0)
    {
        if (!root_strict_mono)
        {
            shape_ranges.emplace_back(static_cast<size_t>(0),
                                      static_cast<size_t>(slice_len));
        }
        else
        {
            shape_ranges = wstring_find_emoji_clusters(
                LLWStringView(wchars + begin_offset, (size_t)slice_len));
        }
    }
    std::vector<std::vector<LLShapedGlyph>> shape_glyphs(shape_ranges.size());
    if (!shape_ranges.empty())
    {
        LLWString slice(wchars + begin_offset, wchars + slice_end);
        for (size_t s = 0; s < shape_ranges.size(); ++s)
        {
            const size_t rb = shape_ranges[s].first;
            const size_t re = shape_ranges[s].second;
            shape_ranges[s].first  = rb + begin_offset;
            shape_ranges[s].second = re + begin_offset;
            LLFontShaping::shapeRun(mFontFreetype, slice, rb, re, shape_glyphs[s]);
        }
    }
    size_t next_shape_run = 0;

    const LLFontGlyphInfo* next_glyph = NULL;

    S32 pos;
    for (pos = begin_offset; pos < max_index; pos++)
    {
        llwchar wch = wchars[pos];
        if (!wch)
        {
            break; // done
        }

        // Per-glyph hit-test inside a shape range. Each glyph's `cluster`
        // points back to a codepoint in the original wstr; clicks land on
        // that boundary. For ligatures (one glyph spans multiple cps) and
        // mark stacks (multiple glyphs share one cluster), this correctly
        // snaps to the nearest cluster boundary — you can't put the cursor
        // mid-ligature. Mid-test matches the legacy codepoint path's
        // unrounded `cur_x + W*0.5f` so behavior agrees character-for-
        // character on Latin text.
        if (next_shape_run < shape_ranges.size()
            && (S32)shape_ranges[next_shape_run].first == pos)
        {
            const auto  run_range  = shape_ranges[next_shape_run];
            const auto& run_glyphs = shape_glyphs[next_shape_run];
            ++next_shape_run;

            if (!run_glyphs.empty())
            {
                // sg.cluster is SLICE-LOCAL (0-based from the slice passed to
                // shapeRun, which starts at `wchars + begin_offset`). The
                // function returns positions relative to begin_offset, so
                // sg.cluster IS the right answer — no further subtraction
                // is needed and we must NOT use the outer-loop `pos`
                // (which is wstr-local) for shaped hits.
                F32 run_x = cur_x;
                for (const LLShapedGlyph& sg : run_glyphs)
                {
                    const F32 glyph_start = run_x;
                    run_x += sg.x_advance;
                    if (!subpixel_pen)
                        run_x = (F32)ll_round(run_x);

                    if (round)
                    {
                        if (target_x < glyph_start + sg.x_advance * 0.5f)
                            return llmin(max_chars, (S32)sg.cluster);
                    }
                    else if (target_x < glyph_start + sg.x_advance)
                    {
                        return llmin(max_chars, (S32)sg.cluster);
                    }

                    if (scaled_max_pixels < run_x)
                        return llmin(max_chars, (S32)sg.cluster);
                }

                // Click is past the entire shaped run; advance and continue.
                cur_x = run_x;
                pos = (S32)run_range.second - 1;  // loop's ++ lands past the run
                next_glyph = NULL;
                continue;
            }
        }

        const LLFontGlyphInfo* glyph = next_glyph;
        next_glyph = NULL;
        if(!glyph)
        {
            glyph = mFontFreetype->getGlyphInfo(wch, EFontGlyphType::Unspecified);
        }

        F32 char_width = mFontFreetype->getXAdvance(glyph);

        if (round)
        {
            // Note: if the mouse is on the left half of the character, the pick is to the character's left
            // If it's on the right half, the pick is to the right.
            if (target_x  < cur_x + char_width*0.5f)
            {
                break;
            }
        }
        else if (target_x  < cur_x + char_width)
        {
            break;
        }

        if (scaled_max_pixels < cur_x + char_width)
        {
            break;
        }

        cur_x += char_width;

        if (((pos + 1) < max_index)
            && (wchars[(pos + 1)]))
        {
            // Kern this puppy.
            next_glyph = mFontFreetype->getGlyphInfo(wchars[pos + 1], EFontGlyphType::Unspecified);
            cur_x += mFontFreetype->getXKerning(glyph, next_glyph);
        }


        // Round after kerning.
        if (!subpixel_pen)
            cur_x = (F32)ll_round(cur_x);
    }

    return llmin(max_chars, pos - begin_offset);
}

const LLFontDescriptor& LLFontGL::getFontDesc() const
{
    return mFontDescriptor;
}

// static
void LLFontGL::initClass(F32 screen_dpi, F32 x_scale, F32 y_scale, const std::string& app_dir, bool create_gl_textures)
{
    sVertDPI = (F32)llfloor(screen_dpi * y_scale);
    sHorizDPI = (F32)llfloor(screen_dpi * x_scale);
    sScaleX = x_scale;
    sScaleY = y_scale;
    sAppDir = app_dir;

    // Font registry init
    if (!sFontRegistry)
    {
        sFontRegistry = new LLFontRegistry(create_gl_textures);
        sFontRegistry->parseFontInfo("fonts.xml");
    }
    else
    {
        sFontRegistry->reset();
    }

    LLFontGL::loadDefaultFonts();
}

void LLFontGL::dumpTextures()
{
    if (mFontFreetype.notNull())
    {
        mFontFreetype->dumpFontBitmaps();
    }
}

// static
void LLFontGL::dumpFonts()
{
    sFontRegistry->dump();
}

// static
void LLFontGL::dumpFontTextures()
{
    sFontRegistry->dumpTextures();
}

// Force standard fonts to get generated up front.
// This is primarily for error detection purposes.
// Don't do this during initClass because it can be slow and we want to get
// the viewer window on screen first. JC
// static
bool LLFontGL::loadDefaultFonts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    bool succ = true;
    succ &= (NULL != getFontSansSerifSmall());
    succ &= (NULL != getFontSansSerif());
    succ &= (NULL != getFontSansSerifBig());
    succ &= (NULL != getFontSansSerifHuge());
    succ &= (NULL != getFontSansSerifBold());
    succ &= (NULL != getFontMonospace());
    return succ;
}

void LLFontGL::loadCommonFonts()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    getFont(LLFontDescriptor("SansSerif", "Small", BOLD));
    getFont(LLFontDescriptor("SansSerif", "Large", BOLD));
    getFont(LLFontDescriptor("SansSerif", "Huge", BOLD));
    getFont(LLFontDescriptor("Monospace", "Medium", 0));
}

// static
void LLFontGL::destroyDefaultFonts()
{
    // Remove the actual fonts.
    delete sFontRegistry;
    sFontRegistry = NULL;
}

//static
void LLFontGL::destroyAllGL()
{
    if (sFontRegistry)
    {
        sFontRegistry->destroyGL();
    }
}

// static
U8 LLFontGL::getStyleFromString(const std::string &style)
{
    S32 ret = 0;
    if (style.find("BOLD") != style.npos)
    {
        ret |= BOLD;
    }
    if (style.find("ITALIC") != style.npos)
    {
        ret |= ITALIC;
    }
    if (style.find("UNDERLINE") != style.npos)
    {
        ret |= UNDERLINE;
    }
    return ret;
}

// static
std::string LLFontGL::getStringFromStyle(U8 style)
{
    std::string style_string;
    if (style == NORMAL)
    {
        style_string += "|NORMAL";
    }
    if (style & BOLD)
    {
        style_string += "|BOLD";
    }
    if (style & ITALIC)
    {
        style_string += "|ITALIC";
    }
    if (style & UNDERLINE)
    {
        style_string += "|UNDERLINE";
    }
    return style_string;
}

// static
std::string LLFontGL::nameFromFont(const LLFontGL* fontp)
{
    return fontp->mFontDescriptor.getName();
}


// static
std::string LLFontGL::sizeFromFont(const LLFontGL* fontp)
{
    return fontp->mFontDescriptor.getSize();
}

// static
std::string LLFontGL::nameFromHAlign(LLFontGL::HAlign align)
{
    if (align == LEFT)          return std::string("left");
    else if (align == RIGHT)    return std::string("right");
    else if (align == HCENTER)  return std::string("center");
    else return std::string();
}

// static
LLFontGL::HAlign LLFontGL::hAlignFromName(const std::string& name)
{
    LLFontGL::HAlign gl_hfont_align = LLFontGL::LEFT;
    if (name == "left")
    {
        gl_hfont_align = LLFontGL::LEFT;
    }
    else if (name == "right")
    {
        gl_hfont_align = LLFontGL::RIGHT;
    }
    else if (name == "center")
    {
        gl_hfont_align = LLFontGL::HCENTER;
    }
    //else leave left
    return gl_hfont_align;
}

// static
std::string LLFontGL::nameFromVAlign(LLFontGL::VAlign align)
{
    if (align == TOP)           return std::string("top");
    else if (align == VCENTER)  return std::string("center");
    else if (align == BASELINE) return std::string("baseline");
    else if (align == BOTTOM)   return std::string("bottom");
    else return std::string();
}

// static
LLFontGL::VAlign LLFontGL::vAlignFromName(const std::string& name)
{
    LLFontGL::VAlign gl_vfont_align = LLFontGL::BASELINE;
    if (name == "top")
    {
        gl_vfont_align = LLFontGL::TOP;
    }
    else if (name == "center")
    {
        gl_vfont_align = LLFontGL::VCENTER;
    }
    else if (name == "baseline")
    {
        gl_vfont_align = LLFontGL::BASELINE;
    }
    else if (name == "bottom")
    {
        gl_vfont_align = LLFontGL::BOTTOM;
    }
    //else leave baseline
    return gl_vfont_align;
}

//static
LLFontGL* LLFontGL::getFontEmojiSmall()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Small", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontEmojiMedium()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Medium", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontEmojiLarge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Large", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontEmojiHuge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Emoji", "Huge", 0));
    return fontp;;
}

//static
LLFontGL* LLFontGL::getFontMonospace()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("Monospace","Monospace",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmall()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmallBold()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",BOLD));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifSmallItalic()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",ITALIC));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerif()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Small",0));
    return fontp;
}

// static
LLFontGL* LLFontGL::getFontSansSerifMedium()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif", "Medium", 0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifBig()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Large",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifHuge()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Huge",0));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFontSansSerifBold()
{
    static LLFontGL* fontp = getFont(LLFontDescriptor("SansSerif","Medium",BOLD));
    return fontp;
}

//static
LLFontGL* LLFontGL::getFont(const LLFontDescriptor& desc)
{
    return sFontRegistry->getFont(desc);
}

//static
LLFontGL* LLFontGL::getFontByName(const std::string& name)
{
    // check for most common fonts first
    if (name == "SANSSERIF")
    {
        return getFontSansSerif();
    }
    else if (name == "SANSSERIF_SMALL")
    {
        return getFontSansSerifSmall();
    }
    else if (name == "SANSSERIF_BIG")
    {
        return getFontSansSerifBig();
    }
    else if (name == "SMALL" || name == "OCRA")
    {
        // *BUG: Should this be "MONOSPACE"?  Do we use "OCRA" anymore?
        // Does "SMALL" mean "SERIF"?
        return getFontMonospace();
    }
    else
    {
        return NULL;
    }
}

//static
LLFontGL* LLFontGL::getFontDefault()
{
    return getFontSansSerif(); // Fallback to sans serif as default font
}


// static
std::string LLFontGL::getFontPathSystem()
{
#if LL_DARWIN
    // HACK for macOS
    return "/System/Library/Fonts/";

#elif LL_WINDOWS
    auto system_root = LLStringUtil::getenv("SystemRoot");
    if (! system_root.empty())
    {
        std::string fontpath(gDirUtilp->add(system_root, "fonts") + gDirUtilp->getDirDelimiter());
        LL_INFOS() << "from SystemRoot: " << fontpath << LL_ENDL;
        return fontpath;
    }

    wchar_t *pwstr = NULL;
    HRESULT okay = SHGetKnownFolderPath(FOLDERID_Fonts, 0, NULL, &pwstr);
    if (SUCCEEDED(okay) && pwstr)
    {
        std::string fontpath(ll_convert_wide_to_string(pwstr));
        // SHGetKnownFolderPath() contract requires us to free pwstr
        CoTaskMemFree(pwstr);
        LL_INFOS() << "from SHGetKnownFolderPath(): " << fontpath << LL_ENDL;
        return fontpath;
    }
#endif

    LL_WARNS() << "Could not determine system fonts path" << LL_ENDL;
    return {};
}


// static
std::string LLFontGL::getFontPathLocal()
{
    std::string local_path;

    // Backup files if we can't load from system fonts directory.
    // We could store this in an end-user writable directory to allow
    // end users to switch fonts.
    if (LLFontGL::sAppDir.length())
    {
        // use specified application dir to look for fonts
        local_path = LLFontGL::sAppDir + "/fonts/";
    }
    else
    {
        // assume working directory is executable directory
        local_path = "./fonts/";
    }
    return local_path;
}

LLFontGL::LLFontGL(const LLFontGL &source)
{
    LL_ERRS() << "Not implemented!" << LL_ENDL;
}

LLFontGL &LLFontGL::operator=(const LLFontGL &source)
{
    LL_ERRS() << "Not implemented" << LL_ENDL;
    return *this;
}

void LLFontGL::renderTriangle(LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, F32 slant_amt) const
{
    S32 index = 0;

    vertex_out[index].set(screen_rect.mRight, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mBottom);
    colors_out[index] = color;
    index++;


    vertex_out[index].set(screen_rect.mRight, screen_rect.mTop, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mTop);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mLeft, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mLeft, uv_rect.mBottom);
    colors_out[index] = color;
    index++;

    vertex_out[index].set(screen_rect.mRight, screen_rect.mBottom, 0.f);
    uv_out[index].set(uv_rect.mRight, uv_rect.mBottom);
    colors_out[index] = color;
}

void LLFontGL::drawGlyph(S32& glyph_count, LLVector4a* vertex_out, LLVector2* uv_out, LLColor4U* colors_out, const LLRectf& screen_rect, const LLRectf& uv_rect, const LLColor4U& color, U8 style, ShadowType shadow, F32 drop_shadow_strength) const
{
    F32 slant_offset;
    slant_offset = ((style & ITALIC) ? ( -mFontFreetype->getAscenderHeight() * 0.2f) : 0.f);

    //FIXME: bold and drop shadow are mutually exclusive only for convenience
    //Allow both when we need them.
    if (style & BOLD)
    {
        for (S32 pass = 0; pass < 2; pass++)
        {
            LLRectf screen_rect_offset = screen_rect;

            screen_rect_offset.translate((F32)(pass * BOLD_OFFSET), 0.f);
            renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_offset, uv_rect, color, slant_offset);
            glyph_count++;
        }
    }
    else if (shadow == DROP_SHADOW_SOFT)
    {
        LLColor4U shadow_color = LLFontGL::sShadowColor;
        shadow_color.mV[VALPHA] = U8(color.mV[VALPHA] * drop_shadow_strength * DROP_SHADOW_SOFT_STRENGTH);
        for (S32 pass = 0; pass < 5; pass++)
        {
            LLRectf screen_rect_offset = screen_rect;

            switch(pass)
            {
            case 0:
                screen_rect_offset.translate(-1.f, -1.f);
                break;
            case 1:
                screen_rect_offset.translate(1.f, -1.f);
                break;
            case 2:
                screen_rect_offset.translate(1.f, 1.f);
                break;
            case 3:
                screen_rect_offset.translate(-1.f, 1.f);
                break;
            case 4:
                screen_rect_offset.translate(0, -2.f);
                break;
            }

            renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_offset, uv_rect, shadow_color, slant_offset);
            glyph_count++;
        }
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
    else if (shadow == DROP_SHADOW)
    {
        LLColor4U shadow_color = LLFontGL::sShadowColor;
        shadow_color.mV[VALPHA] = U8(color.mV[VALPHA] * drop_shadow_strength);
        LLRectf screen_rect_shadow = screen_rect;
        screen_rect_shadow.translate(1.f, -1.f);
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect_shadow, uv_rect, shadow_color, slant_offset);
        glyph_count++;
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
    else // normal rendering
    {
        renderTriangle(&vertex_out[glyph_count * 6], &uv_out[glyph_count * 6], &colors_out[glyph_count * 6], screen_rect, uv_rect, color, slant_offset);
        glyph_count++;
    }
}
