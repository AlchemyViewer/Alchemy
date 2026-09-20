/**
 * @file altextlayout.cpp
 * @brief Where every glyph of a document's lines sits, laid out once and kept.
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

#include "altextlayout.h"

#include "llfontfreetype.h"
#include "llstring.h"

#include <algorithm>

namespace
{
    const ALTextLayout::Line EMPTY_LINE;
}

ALTextLayout::ALTextLayout() = default;

ALTextLayout::~ALTextLayout() = default;

void ALTextLayout::attach(ALTextDocument* document)
{
    mConnection.release();
    mDocument = document;
    if (mDocument)
    {
        mConnection = mDocument->onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
    }
    invalidateAll();
}

void ALTextLayout::setFont(const LLFontGL* font)
{
    if (font == mFont)
    {
        return;
    }
    mFont         = font;
    mSpaceAdvance = -1.f;
    invalidateAll();
}

void ALTextLayout::setWrapWidth(S32 pixels)
{
    pixels = llmax(0, pixels);
    if (pixels == mWrapWidth)
    {
        return;
    }
    mWrapWidth = pixels;
    invalidateAll();
}

void ALTextLayout::setTabWidth(S32 spaces)
{
    spaces = llmax(1, spaces);
    if (spaces == mTabWidth)
    {
        return;
    }
    mTabWidth = spaces;
    invalidateAll();
}

S32 ALTextLayout::rowHeight() const
{
    return mFont ? mFont->getLineSpacing() : 0;
}

void ALTextLayout::refreshIfFontsChanged()
{
    if (mFontGeneration == LLFontGL::sResolutionGeneration && mScaleX == LLFontGL::sScaleX && mScaleY == LLFontGL::sScaleY)
    {
        return;
    }
    mFontGeneration = LLFontGL::sResolutionGeneration;
    mScaleX         = LLFontGL::sScaleX > 0.f ? LLFontGL::sScaleX : 1.f;
    mScaleY         = LLFontGL::sScaleY > 0.f ? LLFontGL::sScaleY : 1.f;
    mSpaceAdvance   = -1.f;
    invalidateAll();
}

void ALTextLayout::invalidateAll()
{
    mLines.assign(mDocument ? mDocument->lineCount() : 0, Line());
    // What is folded stays folded through a change of font; a document of
    // another length is another document.
    if (mHidden.size() != mLines.size())
    {
        mHidden.assign(mLines.size(), 0);
        mHiddenCount = 0;
    }
    mTopsDirty    = true;
    mContentWidth = -1.f;
}

void ALTextLayout::onEdit(const ALTextDocument::Edit& edit)
{
    const S32 first = llclamp(edit.range.begin.line, 0, static_cast<S32>(mLines.size()));
    const S32 last  = llclamp(edit.range.end.line, first, static_cast<S32>(mLines.size()) - 1);
    const S32 made  = 1 + static_cast<S32>(std::count(edit.inserted.begin(), edit.inserted.end(), '\n'));
    if (first < static_cast<S32>(mLines.size()))
    {
        mLines.erase(mLines.begin() + first, mLines.begin() + last + 1);
        for (S32 l = first; l <= last; ++l)
        {
            mHiddenCount -= mHidden[l] ? 1 : 0;
        }
        mHidden.erase(mHidden.begin() + first, mHidden.begin() + last + 1);
    }
    mLines.insert(mLines.begin() + first, made, Line());
    // The lines an edit makes are in sight: somebody is typing there.
    mHidden.insert(mHidden.begin() + first, made, 0);
    if (mDocument)
    {
        mLines.resize(mDocument->lineCount());
        mHidden.resize(mDocument->lineCount(), 0);
    }
    mTopsDirty    = true;
    mContentWidth = -1.f;
}

F32 ALTextLayout::contentWidth()
{
    refreshIfFontsChanged();
    if (mContentWidth < 0.f)
    {
        F32       widest  = 0.f;
        const F32 per_byte = spaceAdvance() / mScaleX;
        for (size_t i = 0; i < mLines.size(); ++i)
        {
            const F32 width = mLines[i].valid ? mLines[i].width
                                              : (mDocument ? static_cast<F32>(mDocument->lineLength(static_cast<S32>(i))) * per_byte : 0.f);
            widest = llmax(widest, width);
        }
        mContentWidth = widest;
    }
    return mContentWidth;
}

// --- hidden lines --------------------------------------------------------------

void ALTextLayout::setHidden(S32 first, S32 last, bool hidden)
{
    first = llmax(first, 0);
    last  = llmin(last, static_cast<S32>(mHidden.size()) - 1);
    for (S32 l = first; l <= last; ++l)
    {
        if (static_cast<bool>(mHidden[l]) != hidden)
        {
            mHidden[l] = hidden ? 1 : 0;
            mHiddenCount += hidden ? 1 : -1;
            mTopsDirty = true;
        }
    }
}

S32 ALTextLayout::visibleFrom(S32 index, S32 direction) const
{
    const S32 count = static_cast<S32>(mHidden.size());
    for (S32 l = index; l >= 0 && l < count; l += (direction < 0 ? -1 : 1))
    {
        if (!mHidden[l])
        {
            return l;
        }
    }
    return -1;
}

F32 ALTextLayout::spaceAdvance()
{
    if (mSpaceAdvance < 0.f)
    {
        mSpaceAdvance = 0.f;
        if (mFont && mFont->getFontFreetype())
        {
            std::vector<ALShapedGlyph> space;
            ALFontShaping::shapeRun(mFont->getFontFreetype(), " ", 0, 1, space);
            for (const ALShapedGlyph& glyph : space)
            {
                mSpaceAdvance += glyph.x_advance;
            }
        }
        if (mSpaceAdvance <= 0.f)
        {
            mSpaceAdvance = 8.f;
        }
    }
    return mSpaceAdvance;
}

void ALTextLayout::layoutLine(S32 index, Line& out)
{
    out.placed.clear();
    out.glyphs.clear();
    out.rows.clear();
    out.width = 0.f;
    out.valid = true;

    const std::string&    text = mDocument ? mDocument->line(index) : LLStringUtil::null;
    const LLFontFreetype* face = mFont ? mFont->getFontFreetype() : nullptr;
    const bool            subpixel = face && face->useSubpixelPen();
    const F32             tab_stop = spaceAdvance() * static_cast<F32>(mTabWidth);
    const F32             inv_x    = 1.f / mScaleX;
    const F32             inv_y    = 1.f / mScaleY;
    // The pen, in the screen's pixels; what is kept is in the UI's.
    F32                   x        = 0.f;

    // The text between tabs is shaped a piece at a time, and every tab is a
    // gap to the next stop. The pen moves the way the font's own draw moves
    // it, rounded after every glyph unless the face keeps a subpixel pen, so
    // that what is laid out here is what renderGlyphs puts on the screen.
    auto shape = [&](size_t begin, size_t end) {
        if (!face || end <= begin)
        {
            return;
        }
        ALFontShaping::shapeRun(face, text, begin, end, mShaped);
        for (const ALShapedGlyph& sg : mShaped)
        {
            out.placed.push_back(LLFontGL::Placed{ sg.face, sg.glyph_id, (x + sg.x_offset) * inv_x, sg.y_offset * inv_y });
            out.glyphs.push_back(Glyph{ sg.cluster, x * inv_x, sg.x_advance * inv_x });
            x += sg.x_advance;
            if (!subpixel)
            {
                x = static_cast<F32>(ll_round(x));
            }
        }
    };
    size_t piece = 0;
    for (size_t at = text.find('\t'); at != std::string::npos; at = text.find('\t', piece))
    {
        shape(piece, at);
        const F32 stop = (floorf(x / tab_stop) + 1.f) * tab_stop;
        out.placed.push_back(LLFontGL::Placed{ nullptr, 0, x * inv_x, 0.f });
        out.glyphs.push_back(Glyph{ static_cast<S32>(at), x * inv_x, (stop - x) * inv_x });
        x     = stop;
        piece = at + 1;
    }
    shape(piece, text.size());
    out.width = x * inv_x;
    if (mContentWidth >= 0.f && x > mContentWidth)
    {
        mContentWidth = x;
    }

    // Rows. One, unless the line is wider than the wrap and has somewhere
    // to break.
    const size_t glyph_count = out.glyphs.size();
    const S32    length      = static_cast<S32>(text.size());
    auto add_row = [&](size_t glyph_begin, size_t glyph_end) {
        Row row;
        row.glyphBegin = glyph_begin;
        row.glyphEnd   = glyph_end;
        row.begin      = glyph_begin < glyph_count ? out.glyphs[glyph_begin].cluster : length;
        row.end        = glyph_end < glyph_count ? out.glyphs[glyph_end].cluster : length;
        row.xStart     = glyph_begin < glyph_count ? out.glyphs[glyph_begin].pen : out.width;
        row.width      = (glyph_end < glyph_count ? out.glyphs[glyph_end].pen : out.width) - row.xStart;
        out.rows.push_back(row);
    };
    if (mWrapWidth <= 0 || out.width <= static_cast<F32>(mWrapWidth) || glyph_count == 0)
    {
        add_row(0, glyph_count);
        return;
    }

    utf8str_line_break_opportunities(text, mBreaks);
    const F32 wrap       = static_cast<F32>(mWrapWidth);
    size_t    row_begin  = 0;
    while (row_begin < glyph_count)
    {
        // The first glyph whose right edge is past the wrap. A space or a
        // tab never is: whitespace at a break hangs past the edge rather
        // than being what forces the break.
        const F32 x_start = out.glyphs[row_begin].pen;
        size_t    over    = row_begin;
        while (over < glyph_count)
        {
            const Glyph& glyph = out.glyphs[over];
            const char   byte  = text[glyph.cluster];
            if (byte != ' ' && byte != '\t' && glyph.pen + glyph.advance - x_start > wrap)
            {
                break;
            }
            ++over;
        }
        if (over == glyph_count)
        {
            add_row(row_begin, glyph_count);
            break;
        }
        // The last place a line may begin that is past this row's first
        // cluster and not past the glyph that overflowed.
        const S32 first_cluster = out.glyphs[row_begin].cluster;
        const S32 over_cluster  = out.glyphs[over].cluster;
        S32       break_at      = -1;
        for (size_t opportunity : mBreaks)
        {
            const S32 at = static_cast<S32>(opportunity);
            if (at > first_cluster && at <= over_cluster)
            {
                break_at = at;
            }
            else if (at > over_cluster)
            {
                break;
            }
        }
        size_t break_glyph;
        if (break_at >= 0)
        {
            break_glyph = row_begin;
            while (break_glyph < glyph_count && out.glyphs[break_glyph].cluster < break_at)
            {
                ++break_glyph;
            }
        }
        else
        {
            // Nowhere to break: at the overflowing cluster, or after it
            // when it is the row's first, so that every row holds
            // something.
            break_glyph = over;
            while (break_glyph > row_begin && out.glyphs[break_glyph - 1].cluster == over_cluster)
            {
                --break_glyph;
            }
            if (break_glyph == row_begin)
            {
                break_glyph = over + 1;
                while (break_glyph < glyph_count && out.glyphs[break_glyph].cluster == over_cluster)
                {
                    ++break_glyph;
                }
            }
        }
        add_row(row_begin, break_glyph);
        row_begin = break_glyph;
    }
}

const ALTextLayout::Line& ALTextLayout::line(S32 index)
{
    refreshIfFontsChanged();
    if (index < 0 || index >= lineCount())
    {
        return EMPTY_LINE;
    }
    Line& entry = mLines[index];
    if (!entry.valid)
    {
        const size_t rows_before = entry.rows.size();
        layoutLine(index, entry);
        if (entry.rows.size() != llmax(rows_before, size_t(1)))
        {
            mTopsDirty = true;
        }
    }
    return entry;
}

void ALTextLayout::ensureTops()
{
    refreshIfFontsChanged();
    if (!mTopsDirty)
    {
        return;
    }
    const S32 row = rowHeight();
    mTops.resize(mLines.size() + 1);
    S32 top = 0;
    for (size_t i = 0; i < mLines.size(); ++i)
    {
        mTops[i] = top;
        if (!mHidden[i])
        {
            top += row * static_cast<S32>(mLines[i].valid ? mLines[i].rows.size() : 1);
        }
    }
    mTops[mLines.size()] = top;
    mTopsDirty           = false;
}

S32 ALTextLayout::lineTop(S32 index)
{
    ensureTops();
    return mTops[llclamp(index, 0, static_cast<S32>(mLines.size()))];
}

S32 ALTextLayout::totalHeight()
{
    ensureTops();
    return mTops.back();
}

S32 ALTextLayout::lineAtY(S32 y)
{
    ensureTops();
    if (mLines.empty())
    {
        return 0;
    }
    // The last line whose top is at or above y. Hidden lines share a top
    // with the line after them, so the last of a run is the one in sight
    // -- unless the run reaches the end, where the nearest in sight is
    // above it.
    const auto after = std::upper_bound(mTops.begin(), mTops.end() - 1, y);
    const S32  line  = llclamp(static_cast<S32>(after - mTops.begin()) - 1, 0, lineCount() - 1);
    if (mHidden[line])
    {
        const S32 above = visibleFrom(line, -1);
        if (above >= 0)
        {
            return above;
        }
        const S32 below = visibleFrom(line, 1);
        if (below >= 0)
        {
            return below;
        }
    }
    return line;
}

S32 ALTextLayout::rowOf(S32 index, S32 column)
{
    const Line& entry = line(index);
    for (size_t r = 0; r + 1 < entry.rows.size(); ++r)
    {
        if (column < entry.rows[r].end)
        {
            return static_cast<S32>(r);
        }
    }
    return entry.rows.empty() ? 0 : static_cast<S32>(entry.rows.size()) - 1;
}

F32 ALTextLayout::xOf(S32 index, S32 column, S32* row_out)
{
    const Line& entry = line(index);
    const S32   r     = rowOf(index, column);
    if (row_out)
    {
        *row_out = r;
    }
    if (entry.rows.empty())
    {
        return 0.f;
    }
    const Row& row = entry.rows[r];
    for (size_t k = row.glyphBegin; k < row.glyphEnd; ++k)
    {
        if (entry.glyphs[k].cluster >= column)
        {
            return entry.glyphs[k].pen - row.xStart;
        }
    }
    return row.width;
}

S32 ALTextLayout::columnAt(S32 index, S32 r, F32 x, bool round)
{
    const Line& entry = line(index);
    if (entry.rows.empty())
    {
        return 0;
    }
    const Row& row = entry.rows[llclamp(r, 0, static_cast<S32>(entry.rows.size()) - 1)];
    if (x <= 0.f)
    {
        return row.begin;
    }
    // Each cluster's left edge and the next cluster's, which is its right.
    S32 cluster = row.begin;
    F32 left    = 0.f;
    for (size_t k = row.glyphBegin; k <= row.glyphEnd; ++k)
    {
        const bool at_end     = (k == row.glyphEnd);
        const bool starts_one = at_end || k == row.glyphBegin || entry.glyphs[k].cluster != entry.glyphs[k - 1].cluster;
        if (!starts_one)
        {
            continue;
        }
        const S32 next_cluster = at_end ? row.end : entry.glyphs[k].cluster;
        const F32 right        = at_end ? row.width : entry.glyphs[k].pen - row.xStart;
        if (k != row.glyphBegin && x < right)
        {
            return (round && (x - left) * 2.f >= (right - left)) ? next_cluster : cluster;
        }
        cluster = next_cluster;
        left    = right;
    }
    return row.end;
}
