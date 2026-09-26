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

F32 ALTextLayout::columnWidth()
{
    refreshIfFontsChanged();
    return spaceAdvance() / mScaleX;
}

void ALTextLayout::refreshIfFontsChanged()
{
    // Against the scale as the fonts have it, not as it is used: a scale of
    // nothing, used as one, would otherwise differ at every asking.
    if (mFontGeneration == LLFontGL::sResolutionGeneration && mRawScaleX == LLFontGL::sScaleX && mRawScaleY == LLFontGL::sScaleY)
    {
        return;
    }
    mFontGeneration = LLFontGL::sResolutionGeneration;
    mRawScaleX      = LLFontGL::sScaleX;
    mRawScaleY      = LLFontGL::sScaleY;
    mScaleX         = mRawScaleX > 0.f ? mRawScaleX : 1.f;
    mScaleY         = mRawScaleY > 0.f ? mRawScaleY : 1.f;
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
        ++mHiddenRevision;
    }
    mTopsDirty    = true;
    mContentWidth = -1.f;
}

void ALTextLayout::onEdit(const ALTextDocument::Edit& edit)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    const std::vector<ALTextDocument::Edit::LineSpan>& spans      = edit.lineSpans();
    const S32                                         line_count = mDocument ? mDocument->lineCount() : -1;
    // The hidden lines among those the edit replaced go with them.
    S32  hidden_gone = 0;
    bool moved       = false;
    for (const ALTextDocument::Edit::LineSpan& span : spans)
    {
        moved = moved || span.made != span.last - span.first + 1;
        if (mHiddenCount > 0)
        {
            const S32 size  = static_cast<S32>(mHidden.size());
            const S32 first = llclamp(span.first, 0, size);
            const S32 last  = llclamp(span.last, first, size - 1);
            for (S32 l = first; l <= last && l < size; ++l)
            {
                hidden_gone += mHidden[l] ? 1 : 0;
            }
        }
    }
    mLines.applySpans(spans, line_count, Line());
    // The lines an edit makes are in sight: somebody is typing there.
    mHidden.applySpans(spans, line_count, 0, 0);
    mHiddenCount -= hidden_gone;
    // Which lines are hidden changed only where a hidden one went, or the
    // hidden ones moved: not for a line typed in with none folded.
    if (hidden_gone > 0 || (mHiddenCount > 0 && moved))
    {
        ++mHiddenRevision;
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
            ++mHiddenRevision;
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

void ALTextLayout::setInlayProvider(inlay_provider_t provider)
{
    mInlays = std::move(provider);
    invalidateAll();
}

void ALTextLayout::setSubstitutionProvider(substitution_provider_t provider)
{
    mSubstitutions = std::move(provider);
    invalidateAll();
}

void ALTextLayout::setRunProvider(run_provider_t provider)
{
    mRuns = std::move(provider);
    invalidateAll();
}

void ALTextLayout::setIndentProvider(indent_provider_t provider)
{
    mIndents = std::move(provider);
    invalidateAll();
}

void ALTextLayout::invalidateLine(S32 index)
{
    if (index < 0 || index >= lineCount())
    {
        return;
    }
    mLines[index] = Line();
    mTopsDirty    = true;
    mContentWidth = -1.f;
}

void ALTextLayout::layoutLine(S32 index, Line& out)
{
    ++mLinesLaidOut;
    out.placed.clear();
    out.glyphs.clear();
    out.rows.clear();
    out.width  = 0.f;
    out.height = 0;
    out.valid  = true;

    const std::string&    text = mDocument ? mDocument->line(index) : LLStringUtil::null;
    const LLFontFreetype* face = mFont ? mFont->getFontFreetype() : nullptr;
    const bool            subpixel = face && face->useSubpixelPen();
    const F32             tab_stop = spaceAdvance() * static_cast<F32>(mTabWidth);
    const F32             inv_x    = 1.f / mScaleX;
    const F32             inv_y    = 1.f / mScaleY;
    // The pen, in the screen's pixels; what is kept is in the UI's.
    F32                   x        = 0.f;

    // What goes beside the text, by column.
    mInlayScratch.clear();
    if (mInlays)
    {
        mInlays(index, mInlayScratch);
        std::stable_sort(mInlayScratch.begin(), mInlayScratch.end(), [](const Inlay& a, const Inlay& b) { return a.column < b.column; });
    }
    // What stretches of the text show as something else: in order, within
    // the line, none over another.
    mSubstitutionScratch.clear();
    if (mSubstitutions)
    {
        mSubstitutions(index, mSubstitutionScratch);
        std::stable_sort(mSubstitutionScratch.begin(), mSubstitutionScratch.end(), [](const Substitution& a, const Substitution& b) { return a.begin < b.begin; });
        S32 reached = 0;
        for (size_t i = 0; i < mSubstitutionScratch.size();)
        {
            Substitution& sub = mSubstitutionScratch[i];
            sub.begin         = llclamp(sub.begin, 0, static_cast<S32>(text.size()));
            sub.end           = llclamp(sub.end, sub.begin, static_cast<S32>(text.size()));
            if (sub.begin < reached || sub.end == sub.begin)
            {
                mSubstitutionScratch.erase(mSubstitutionScratch.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            reached = sub.end;
            ++i;
        }
    }
    // What stretches are shaped in a font of their own: in order, within
    // the line, none over another, each with a face to shape by.
    mRunScratch.clear();
    if (mRuns)
    {
        mRuns(index, mRunScratch);
        std::stable_sort(mRunScratch.begin(), mRunScratch.end(), [](const Run& a, const Run& b) { return a.begin < b.begin; });
        S32 reached = 0;
        for (size_t i = 0; i < mRunScratch.size();)
        {
            Run& run  = mRunScratch[i];
            run.begin = llclamp(run.begin, 0, static_cast<S32>(text.size()));
            run.end   = llclamp(run.end, run.begin, static_cast<S32>(text.size()));
            if (run.begin < reached || run.end == run.begin || !run.font || !run.font->getFontFreetype() || run.font == mFont)
            {
                mRunScratch.erase(mRunScratch.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            reached = run.end;
            ++i;
        }
    }
    // The fonts the glyphs were shaped in, by glyph, where not the
    // document's own: what a row's text is tall for.
    std::vector<std::pair<size_t, const LLFontGL*>> font_spans;
    size_t next_inlay = 0;
    auto   gap        = [&](S32 cluster, F32 width, S32 inlay, bool before, S32 substitution = -1) {
        out.placed.push_back(LLFontGL::Placed{ nullptr, 0, x * inv_x, 0.f });
        out.glyphs.push_back(Glyph{ cluster, x * inv_x, width * inv_x, inlay, before, substitution });
        x += width;
        if (!subpixel)
        {
            x = static_cast<F32>(ll_round(x));
        }
    };
    // The inlays at a column, as gaps as wide as they said.
    auto inlays_at = [&](size_t column) {
        while (next_inlay < mInlayScratch.size() && mInlayScratch[next_inlay].column <= static_cast<S32>(column))
        {
            const Inlay& inlay = mInlayScratch[next_inlay++];
            if (inlay.width > 0.f)
            {
                gap(static_cast<S32>(column), inlay.width * mScaleX, inlay.id, inlay.before);
            }
        }
    };

    // The text between tabs is shaped a piece at a time, and every tab is a
    // gap to the next stop. The pen moves the way the font's own draw moves
    // it, rounded after every glyph unless the face keeps a subpixel pen, so
    // that what is laid out here is what renderGlyphs puts on the screen.
    // An inlay splits a piece too, so that its gap goes where its column is.
    // A substitution's text is shaped as itself, every glyph of it on the
    // stretch's first byte; a box is a gap of its width on the same.
    auto shape_with = [&](const LLFontFreetype* in, const std::string& source, size_t begin, size_t end, S32 substitution) {
        if (!in || end <= begin)
        {
            return;
        }
        ALFontShaping::shapeRun(in, source, begin, end, mShaped);
        for (const ALShapedGlyph& sg : mShaped)
        {
            out.placed.push_back(LLFontGL::Placed{ sg.face, sg.glyph_id, (x + sg.x_offset) * inv_x, sg.y_offset * inv_y });
            out.glyphs.push_back(Glyph{ sg.cluster, x * inv_x, sg.x_advance * inv_x, -1, true, substitution });
            x += sg.x_advance;
            if (!subpixel)
            {
                x = static_cast<F32>(ll_round(x));
            }
        }
    };
    // A piece of the text, shaped run by run where runs cross it.
    size_t next_run  = 0;
    auto   shape_run = [&](const std::string& source, size_t begin, size_t end, S32 substitution) {
        if (&source != &text)
        {
            shape_with(face, source, begin, end, substitution);
            return;
        }
        size_t from = begin;
        while (from < end)
        {
            while (next_run < mRunScratch.size() && static_cast<size_t>(mRunScratch[next_run].end) <= from)
            {
                ++next_run;
            }
            if (next_run >= mRunScratch.size() || static_cast<size_t>(mRunScratch[next_run].begin) >= end)
            {
                shape_with(face, text, from, end, substitution);
                break;
            }
            const Run&   run      = mRunScratch[next_run];
            const size_t run_from = llmax(from, static_cast<size_t>(run.begin));
            const size_t run_to   = llmin(end, static_cast<size_t>(run.end));
            shape_with(face, text, from, run_from, substitution);
            font_spans.emplace_back(out.glyphs.size(), run.font);
            shape_with(run.font->getFontFreetype(), text, run_from, run_to, substitution);
            font_spans.emplace_back(out.glyphs.size(), nullptr);
            from = run_to;
        }
    };
    auto shape = [&](size_t begin, size_t end) {
        size_t from = begin;
        while (next_inlay < mInlayScratch.size() && mInlayScratch[next_inlay].column < static_cast<S32>(end))
        {
            const size_t at = static_cast<size_t>(llmax(mInlayScratch[next_inlay].column, static_cast<S32>(from)));
            shape_run(text, from, at, -1);
            inlays_at(at);
            from = at;
        }
        shape_run(text, from, end, -1);
    };
    // The boxes on the line, by glyph, with their heights: what a row
    // may be taller for.
    std::vector<std::pair<size_t, S32>> boxes;
    auto substitute = [&](const Substitution& sub) {
        const size_t first = out.glyphs.size();
        if (sub.shown.empty())
        {
            if (sub.height > 0)
            {
                boxes.emplace_back(first, sub.height);
            }
            gap(sub.begin, llmax(0.f, sub.width) * mScaleX, -1, true, sub.id);
        }
        else
        {
            shape_run(sub.shown, 0, sub.shown.size(), sub.id);
            for (size_t k = first; k < out.glyphs.size(); ++k)
            {
                out.glyphs[k].cluster = sub.begin;
            }
        }
    };
    // Pieces: the text up to the next tab or substitution, whichever is
    // first, then that; an inlay inside a substitution goes after it.
    size_t next_sub = 0;
    size_t piece    = 0;
    while (true)
    {
        const size_t sub_at = next_sub < mSubstitutionScratch.size() ? static_cast<size_t>(mSubstitutionScratch[next_sub].begin) : std::string::npos;
        size_t       tab_at = text.find('\t', piece);
        if (tab_at != std::string::npos && tab_at >= sub_at)
        {
            tab_at = std::string::npos;
        }
        const size_t at = llmin(llmin(sub_at, tab_at), text.size());
        shape(piece, at);
        inlays_at(at);
        if (at >= text.size())
        {
            break;
        }
        if (at == sub_at)
        {
            const Substitution& sub = mSubstitutionScratch[next_sub++];
            substitute(sub);
            piece = static_cast<size_t>(sub.end);
            inlays_at(piece);
            continue;
        }
        const F32 stop = (floorf(x / tab_stop) + 1.f) * tab_stop;
        out.placed.push_back(LLFontGL::Placed{ nullptr, 0, x * inv_x, 0.f });
        out.glyphs.push_back(Glyph{ static_cast<S32>(at), x * inv_x, (stop - x) * inv_x });
        x     = stop;
        piece = at + 1;
    }
    out.width = x * inv_x;
    // The widest line, kept up in the UI's pixels as the width is.
    if (mContentWidth >= 0.f && out.width > mContentWidth)
    {
        mContentWidth = out.width;
    }

    // Rows. One, unless the line is wider than the wrap and has somewhere
    // to break. Each as tall as its text -- the document's font, or the
    // tallest font on it -- or as the tallest box on it, and stacked
    // under the line's top.
    const size_t glyph_count = out.glyphs.size();
    const S32    length      = static_cast<S32>(text.size());
    const S32    row_height  = rowHeight();
    const S32    row_ascent  = mFont ? ll_round(mFont->getAscenderHeight()) : 0;
    // How far in the rows start: the first, and the ones after it. A
    // row's x runs from the line's edge, so its start is that much
    // before its first glyph's pen and its width that much more.
    Indent indent;
    if (mIndents)
    {
        indent       = mIndents(index);
        indent.first = llmax(0.f, indent.first);
        indent.rest  = llmax(0.f, indent.rest);
    }
    auto add_row = [&](size_t glyph_begin, size_t glyph_end) {
        Row       row;
        const F32 in   = out.rows.empty() ? indent.first : indent.rest;
        row.glyphBegin = glyph_begin;
        row.glyphEnd   = glyph_end;
        row.begin      = glyph_begin < glyph_count ? out.glyphs[glyph_begin].cluster : length;
        row.end        = glyph_end < glyph_count ? out.glyphs[glyph_end].cluster : length;
        row.xStart     = (glyph_begin < glyph_count ? out.glyphs[glyph_begin].pen : out.width) - in;
        row.width      = (glyph_end < glyph_count ? out.glyphs[glyph_end].pen : out.width) - row.xStart;
        row.top        = out.height;
        row.textHeight = row_height;
        row.ascent     = row_ascent;
        // The fonts in force over the row: each span starts a font at a
        // glyph, or ends one with null, and a stretch under a font that
        // reaches into the row makes the row as tall as the font asks.
        const LLFontGL* current = nullptr;
        auto            tall_as = [&](const LLFontGL* used) {
            row.textHeight = llmax(row.textHeight, used->getLineSpacing());
            row.ascent     = llmax(row.ascent, ll_round(used->getAscenderHeight()));
        };
        for (const auto& [glyph, font] : font_spans)
        {
            if (glyph >= glyph_end)
            {
                break;
            }
            if (current && glyph > glyph_begin)
            {
                tall_as(current);
            }
            current = font;
        }
        if (current)
        {
            tall_as(current);
        }
        row.height = row.textHeight;
        for (const auto& [glyph, height] : boxes)
        {
            if (glyph >= glyph_begin && glyph < glyph_end)
            {
                row.height = llmax(row.height, height);
            }
        }
        out.height += row.height;
        out.rows.push_back(row);
    };
    if (mWrapWidth <= 0 || out.width + indent.first <= static_cast<F32>(mWrapWidth) || glyph_count == 0)
    {
        add_row(0, glyph_count);
        return;
    }

    utf8str_line_break_opportunities(text, mBreaks);
    const F32 wrap       = static_cast<F32>(mWrapWidth);
    size_t    row_begin  = 0;
    // The break opportunities are in order and the rows go forward, so
    // the ones a row has passed are not looked at again.
    size_t    break_from = 0;
    while (row_begin < glyph_count)
    {
        // The first glyph whose right edge is past the wrap, the row's
        // indent taken off it. A space or a tab never is: whitespace at
        // a break hangs past the edge rather than being what forces the
        // break.
        const F32 x_start  = out.glyphs[row_begin].pen;
        const F32 wrap_row = llmax(wrap * 0.25f, wrap - (row_begin == 0 ? indent.first : indent.rest));
        size_t    over     = row_begin;
        while (over < glyph_count)
        {
            const Glyph& glyph = out.glyphs[over];
            const char   byte  = text[glyph.cluster];
            if (byte != ' ' && byte != '\t' && glyph.pen + glyph.advance - x_start > wrap_row)
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
        while (break_from < mBreaks.size() && static_cast<S32>(mBreaks[break_from]) <= first_cluster)
        {
            ++break_from;
        }
        for (size_t b = break_from; b < mBreaks.size(); ++b)
        {
            const S32 at = static_cast<S32>(mBreaks[b]);
            if (at > over_cluster)
            {
                break;
            }
            break_at = at;
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
        // What the line counted as before: its last height, or a row's.
        const S32 before = entry.height > 0 ? entry.height : rowHeight();
        layoutLine(index, entry);
        if (entry.height != before)
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
            top += mLines[i].valid ? mLines[i].height : row;
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

S32 ALTextLayout::rowTop(S32 index, S32 row)
{
    const Line& entry = line(index);
    if (entry.rows.empty())
    {
        return 0;
    }
    return entry.rows[static_cast<size_t>(llclamp(row, 0, static_cast<S32>(entry.rows.size()) - 1))].top;
}

S32 ALTextLayout::rowHeightOf(S32 index, S32 row)
{
    const Line& entry = line(index);
    if (entry.rows.empty())
    {
        return rowHeight();
    }
    return entry.rows[static_cast<size_t>(llclamp(row, 0, static_cast<S32>(entry.rows.size()) - 1))].height;
}

S32 ALTextLayout::rowAtY(S32 index, S32 y)
{
    const Line& entry = line(index);
    for (size_t r = 0; r + 1 < entry.rows.size(); ++r)
    {
        if (y < entry.rows[r].top + entry.rows[r].height)
        {
            return static_cast<S32>(r);
        }
    }
    return entry.rows.empty() ? 0 : static_cast<S32>(entry.rows.size()) - 1;
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
        const Glyph& glyph = entry.glyphs[k];
        if (glyph.cluster < column)
        {
            continue;
        }
        // An inlay standing before the text at the column: the caret at
        // the column sits past it, unless the row ends with it.
        if (glyph.inlay >= 0 && glyph.inlayBefore && glyph.cluster == column && k + 1 < row.glyphEnd)
        {
            continue;
        }
        return glyph.pen - row.xStart;
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
