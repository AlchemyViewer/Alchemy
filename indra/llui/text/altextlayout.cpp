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

#include "altextchars.h"
#include "llfontfreetype.h"
#include "llstring.h"

#include <algorithm>

namespace
{
    const ALTextLayout::Line EMPTY_LINE;

    // A stretch of a line longer than this is shaped a piece at a time.
    constexpr size_t SHAPE_PIECE_LEAST = 256;
    constexpr size_t SHAPE_PIECE_MOST  = 1024;

    // Where the piece of a long stretch that begins at `from` ends: after a
    // space, where no font joins one glyph to the next, and after one the
    // four bytes before it pick rather than one so far along. An edit then
    // changes the piece it is in and no other, and every other piece is
    // found shaped already. The bytes pick one space in sixteen; where they
    // pick none by the most a piece may be, the last space before that
    // ends it, and where there is no space at all, the stretch's end.
    size_t pieceEnd(const std::string& text, size_t from, size_t end)
    {
        if (end - from <= SHAPE_PIECE_MOST)
        {
            return end;
        }
        size_t last_space = std::string::npos;
        for (size_t i = from + SHAPE_PIECE_LEAST; i < end; ++i)
        {
            if (text[i] == ' ')
            {
                last_space = i;
                U32 picked = 0;
                for (size_t k = i - 4; k < i; ++k)
                {
                    picked = picked * 31 + static_cast<U8>(text[k]);
                }
                if ((picked & 15) == 0)
                {
                    return i + 1;
                }
            }
            if (i >= from + SHAPE_PIECE_MOST && last_space != std::string::npos)
            {
                return last_space + 1;
            }
        }
        return end;
    }
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
    // Each line is cut into rows again as it is next asked for, from the
    // glyphs it has; until then it keeps its height.
    mWrapWidth = llmax(0, pixels);
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

ALTextLayout::Entry::Entry(const Entry& other)
:   height(other.height),
    width(other.width),
    valid(other.valid),
    trimmed(other.trimmed),
    laid(other.laid ? std::make_unique<Line>(*other.laid) : nullptr)
{
}

ALTextLayout::Entry& ALTextLayout::Entry::operator=(const Entry& other)
{
    if (this != &other)
    {
        height  = other.height;
        width   = other.width;
        valid   = other.valid;
        trimmed = other.trimmed;
        laid    = other.laid ? std::make_unique<Line>(*other.laid) : nullptr;
    }
    return *this;
}

void ALTextLayout::invalidateAll()
{
    // Every line laid out again when next asked for: where the lines are
    // the same lines, each keeps what it was laid out into, for the next
    // layout to fill again rather than make anew.
    const size_t count = mDocument ? static_cast<size_t>(mDocument->lineCount()) : 0;
    if (mLines.size() == count)
    {
        for (Entry& entry : mLines)
        {
            entry.height  = 0;
            entry.width   = 0.f;
            entry.valid   = false;
            entry.trimmed = false;
        }
    }
    else
    {
        mLines.assign(count, Entry());
    }
    mLinesHeld = 0;
    // What is folded stays folded through a change of font; a document of
    // another length is another document.
    if (mHidden.size() != mLines.size())
    {
        mHidden.assign(mLines.size(), 0);
        mHiddenCount = 0;
        mHiddenByCount.fill(0);
        ++mHiddenRevision;
    }
    heightsMoved();
    mContentWidth = -1.f;
    mWidestLine   = -1;
}

void ALTextLayout::heightsMoved()
{
    mHeightsStale = true;
    ++mHeightsRevision;
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
                ownersChanged(mHidden[l], 0);
            }
        }
    }
    if (moved)
    {
        // The widest line moves with the lines, unless the edit replaced
        // it; the lines it made count by their bytes, in the widest too.
        const S32 widest = mWidestLine >= 0 ? edit.lineAfter(mWidestLine) : -1;
        mLines.applySpans(spans, line_count, Entry());
        if (mWidestLine >= 0 && widest < 0)
        {
            mContentWidth = -1.f;
        }
        mWidestLine = widest;
        S32 shift   = 0;
        for (const ALTextDocument::Edit::LineSpan& span : spans)
        {
            for (S32 made = 0; made < span.made; ++made)
            {
                widthChanged(span.first + shift + made);
            }
            shift = span.shiftAfter;
        }
    }
    else
    {
        // As many lines as before: each the edit touched is laid out again
        // when next asked for, and keeps its height until then, so that
        // the lines below keep their tops -- but not its width, which was
        // the width of the text it had, let go of or not.
        for (const ALTextDocument::Edit::LineSpan& span : spans)
        {
            for (S32 l = llmax(span.first, 0); l <= span.last && l < lineCount(); ++l)
            {
                mLines[l].valid   = false;
                mLines[l].trimmed = false;
                widthChanged(l);
            }
        }
    }
    // The lines an edit makes are in sight: somebody is typing there.
    mHidden.applySpans(spans, line_count, 0, 0);
    mHiddenCount -= hidden_gone;
    // Which lines are hidden changed only where a hidden one went, or the
    // hidden ones moved: not for a line typed in with none folded.
    if (hidden_gone > 0 || (mHiddenCount > 0 && moved))
    {
        ++mHiddenRevision;
    }
    if (moved || hidden_gone > 0)
    {
        heightsMoved();
    }
}

F32 ALTextLayout::contentWidth()
{
    refreshIfFontsChanged();
    if (mContentWidth < 0.f)
    {
        const F32 per_byte = spaceAdvance() / mScaleX;
        mContentWidth      = 0.f;
        mWidestLine        = -1;
        for (S32 i = 0; i < lineCount(); ++i)
        {
            const F32 width = countedWidth(i, per_byte);
            if (width > mContentWidth)
            {
                mContentWidth = width;
                mWidestLine   = i;
            }
        }
    }
    return mContentWidth;
}

F32 ALTextLayout::countedWidth(S32 index, F32 per_byte) const
{
    const Entry& entry = mLines[static_cast<size_t>(index)];
    if (entry.valid || entry.trimmed)
    {
        return entry.width;
    }
    return mDocument ? static_cast<F32>(mDocument->lineLength(index)) * per_byte : 0.f;
}

void ALTextLayout::widthChanged(S32 index)
{
    if (mContentWidth < 0.f || index < 0 || index >= lineCount())
    {
        return;
    }
    if (index == mWidestLine)
    {
        // It may have narrowed, and another line be the widest.
        mContentWidth = -1.f;
        return;
    }
    const F32 width = countedWidth(index, spaceAdvance() / mScaleX);
    if (width > mContentWidth)
    {
        mContentWidth = width;
        mWidestLine   = index;
    }
}

// --- hidden lines --------------------------------------------------------------

void ALTextLayout::setHidden(HiddenBy by, S32 first, S32 last, bool hidden)
{
    const U8 bits = static_cast<U8>(by);
    first         = llmax(first, 0);
    last          = llmin(last, static_cast<S32>(mHidden.size()) - 1);
    for (S32 l = first; l <= last; ++l)
    {
        // The owner's bit set or let go; the line hidden or shown only
        // where that leaves it hidden by somebody, or by nobody.
        const U8 was = mHidden[l];
        const U8 now = hidden ? static_cast<U8>(was | bits) : static_cast<U8>(was & ~bits);
        mHidden[l]   = now;
        ownersChanged(was, now);
        if ((was != 0) != (now != 0))
        {
            mHiddenCount += now != 0 ? 1 : -1;
            ++mHiddenRevision;
            if (!mHeightsStale && static_cast<size_t>(l) < mHeights.size())
            {
                mHeights.set(static_cast<size_t>(l), countedHeight(l));
                ++mHeightsRevision;
            }
        }
    }
}

void ALTextLayout::ownersChanged(U8 was, U8 now)
{
    const U8 changed = static_cast<U8>(was ^ now);
    for (size_t i = 0; i < mHiddenByCount.size(); ++i)
    {
        const U8 bit = static_cast<U8>(1u << i);
        if ((changed & bit) != 0)
        {
            mHiddenByCount[i] += (now & bit) != 0 ? 1 : -1;
        }
    }
}

S32 ALTextLayout::hiddenCount(HiddenBy by) const
{
    switch (by)
    {
        case HiddenBy::Folds:
            return mHiddenByCount[0];
        case HiddenBy::Host:
            return mHiddenByCount[1];
        default:
            return mHiddenCount;
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

S32 ALTextLayout::visibleAfter(S32 index)
{
    const S32 count = lineCount();
    const S32 next  = llmax(index + 1, 0);
    if (next >= count || !hidden(next))
    {
        return llmin(next, count);
    }
    ensureHeights();
    if (rowHeight() <= 0)
    {
        const S32 found = visibleFrom(next, 1);
        return found >= 0 ? found : count;
    }
    // A hidden line takes no height and one in sight always some, so the
    // line the top of the hidden run falls in is the first in sight after it.
    return llmin(static_cast<S32>(mHeights.reach(mHeights.before(static_cast<size_t>(next)))), count);
}

S32 ALTextLayout::visibleBefore(S32 index)
{
    const S32 prev = llmin(index, lineCount()) - 1;
    if (prev < 0 || !hidden(prev))
    {
        return llmax(prev, -1);
    }
    ensureHeights();
    if (rowHeight() <= 0)
    {
        return visibleFrom(prev, -1);
    }
    // And the line the last pixel above the hidden run falls in is the
    // last in sight before it; with nothing above the run, there is none.
    const S32 above = mHeights.before(static_cast<size_t>(prev));
    return above > 0 ? static_cast<S32>(mHeights.reach(above - 1)) : -1;
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

void ALTextLayout::setGapProvider(gap_provider_t provider)
{
    mGaps = std::move(provider);
    gapsChanged();
}

void ALTextLayout::invalidateLine(S32 index)
{
    if (index < 0 || index >= lineCount())
    {
        return;
    }
    // Laid out again when next asked for, keeping its height until then.
    mLines[index].valid   = false;
    mLines[index].trimmed = false;
    widthChanged(index);
}

void ALTextLayout::layoutLine(S32 index, Line& out)
{
    ++mLinesLaidOut;
    ++mLinesHeld;
    out.placed.clear();
    out.glyphs.clear();
    out.rows.clear();
    out.fonts.clear();
    out.boxes.clear();
    out.width   = 0.f;
    out.height  = 0;
    out.valid   = true;
    out.ordered = true;

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
    // document's own, go in out.fonts: what a row's text is tall for.
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
        for (size_t from = begin; from < end;)
        {
            const size_t to = pieceEnd(source, from, end);
            // Read where the shaper keeps it, before anything else is shaped.
            const std::vector<ALShapedGlyph>& shaped = ALFontShaping::shapeLine(in, source, from, to);
            const S32                         base   = static_cast<S32>(from);
            for (const ALShapedGlyph& sg : shaped)
            {
                out.placed.push_back(LLFontGL::Placed{ sg.face, sg.glyph_id, (x + sg.x_offset) * inv_x, sg.y_offset * inv_y });
                out.glyphs.push_back(Glyph{ base + sg.cluster, x * inv_x, sg.x_advance * inv_x, -1, true, substitution });
                x += sg.x_advance;
                if (!subpixel)
                {
                    x = static_cast<F32>(ll_round(x));
                }
            }
            from = to;
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
            out.fonts.emplace_back(out.glyphs.size(), run.font);
            shape_with(run.font->getFontFreetype(), text, run_from, run_to, substitution);
            out.fonts.emplace_back(out.glyphs.size(), nullptr);
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
    // The boxes on the line, by glyph, with their heights, go in
    // out.boxes: what a row may be taller for.
    auto substitute = [&](const Substitution& sub) {
        const size_t first = out.glyphs.size();
        if (sub.shown.empty())
        {
            if (sub.height > 0)
            {
                out.boxes.emplace_back(first, sub.height);
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
        const F32 stop = alNextTabStop(x, tab_stop);
        out.placed.push_back(LLFontGL::Placed{ nullptr, 0, x * inv_x, 0.f });
        out.glyphs.push_back(Glyph{ static_cast<S32>(at), x * inv_x, (stop - x) * inv_x });
        x     = stop;
        piece = at + 1;
    }
    out.width = x * inv_x;
    for (size_t k = 1; k < out.glyphs.size() && out.ordered; ++k)
    {
        out.ordered = out.glyphs[k].cluster >= out.glyphs[k - 1].cluster;
    }
    // The widest line, kept up in the UI's pixels as the width is: wider,
    // or found again where this line was the widest and has narrowed.
    if (mContentWidth >= 0.f)
    {
        if (index == mWidestLine && out.width < mContentWidth)
        {
            mContentWidth = -1.f;
        }
        else if (out.width > mContentWidth)
        {
            mContentWidth = out.width;
            mWidestLine   = index;
        }
    }
    wrapLine(index, out);
}

void ALTextLayout::wrapLine(S32 index, Line& out)
{
    out.rows.clear();
    out.height    = 0;
    out.wrappedAt = mWrapWidth;
    const std::string& text = mDocument ? mDocument->line(index) : LLStringUtil::null;

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
        for (const auto& [glyph, font] : out.fonts)
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
        for (const auto& [glyph, height] : out.boxes)
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
    Entry& entry = mLines[index];
    if (!entry.laid)
    {
        entry.laid = std::make_unique<Line>();
    }
    Line& laid = *entry.laid;
    if (!entry.valid || laid.wrappedAt != mWrapWidth)
    {
        // Shaped again, or only cut into rows again where its glyphs are
        // current and the wrap width moved.
        if (entry.valid)
        {
            wrapLine(index, laid);
        }
        else
        {
            layoutLine(index, laid);
        }
        entry.valid   = true;
        entry.trimmed = false;
        entry.height  = laid.height;
        entry.width   = laid.width;
        if (!mHeightsStale && static_cast<size_t>(index) < mHeights.size() && mHeights.at(static_cast<size_t>(index)) != countedHeight(index))
        {
            mHeights.set(static_cast<size_t>(index), countedHeight(index));
            ++mHeightsRevision;
        }
    }
    return laid;
}

S32 ALTextLayout::trim(S32 first, S32 last)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    S32 let_go = 0;
    S32 held   = 0;
    for (size_t i = 0; i < mLines.size(); ++i)
    {
        Entry& entry = mLines[i];
        if (!entry.laid)
        {
            continue;
        }
        const S32 index = static_cast<S32>(i);
        if (index >= first && index <= last)
        {
            ++held;
            continue;
        }
        // Its height stays as it counts in the column, and its width, where
        // it was current, as the widest line's.
        entry.trimmed = entry.valid || entry.trimmed;
        entry.valid   = false;
        entry.laid.reset();
        ++let_go;
    }
    mLinesHeld = held;
    return let_go;
}

S32 ALTextLayout::countedHeight(S32 index) const
{
    return countedHeight(index, rowHeight());
}

S32 ALTextLayout::countedHeight(S32 index, S32 row_h) const
{
    if (mHidden[index])
    {
        return 0;
    }
    // A line of the text, and not hidden: its gap asked straight, without
    // gapRows' checks again, once a line where every height is summed.
    const S32 height = mLines[index].height;
    return (height > 0 ? height : row_h) + (mGaps ? llmax(0, mGaps(index)) * row_h : 0);
}

void ALTextLayout::ensureHeights()
{
    refreshIfFontsChanged();
    if (!mHeightsStale)
    {
        return;
    }
    // The font's row asked once, not once a line.
    const S32 row_h = rowHeight();
    mHeightScratch.resize(mLines.size());
    for (size_t i = 0; i < mLines.size(); ++i)
    {
        mHeightScratch[i] = countedHeight(static_cast<S32>(i), row_h);
    }
    mHeights.assign(mHeightScratch);
    mEndGap       = mGaps ? llmax(0, mGaps(lineCount())) : 0;
    mHeightsStale = false;
}

S32 ALTextLayout::lineTop(S32 index)
{
    ensureHeights();
    const S32 count = static_cast<S32>(mLines.size());
    const S32 at    = llclamp(index, 0, count);
    if (at == count)
    {
        return mHeights.total() + mEndGap * rowHeight();
    }
    return mHeights.before(static_cast<size_t>(at)) + (mHidden[at] ? 0 : gapHeight(at));
}

S32 ALTextLayout::totalHeight()
{
    ensureHeights();
    return mHeights.total() + mEndGap * rowHeight();
}

// --- gaps ------------------------------------------------------------------------

void ALTextLayout::gapsChanged()
{
    heightsMoved();
}

void ALTextLayout::gapChanged(S32 index)
{
    if (index == lineCount())
    {
        // Below the text: nothing is summed past it.
        const S32 rows = mGaps ? llmax(0, mGaps(index)) : 0;
        if (!mHeightsStale && rows != mEndGap)
        {
            mEndGap = rows;
            ++mHeightsRevision;
        }
        return;
    }
    if (index < 0 || index > lineCount() || mHeightsStale || static_cast<size_t>(index) >= mHeights.size())
    {
        return;
    }
    if (mHeights.at(static_cast<size_t>(index)) != countedHeight(index))
    {
        mHeights.set(static_cast<size_t>(index), countedHeight(index));
        ++mHeightsRevision;
    }
}

S32 ALTextLayout::gapRows(S32 index) const
{
    if (!mGaps || index < 0 || index > lineCount() || hidden(index))
    {
        return 0;
    }
    return llmax(0, mGaps(index));
}

S32 ALTextLayout::gapTop(S32 index)
{
    ensureHeights();
    const S32 count = static_cast<S32>(mLines.size());
    return mHeights.before(static_cast<size_t>(llclamp(index, 0, count)));
}

S32 ALTextLayout::gapAtY(S32 y)
{
    ensureHeights();
    if (!mGaps || y < 0)
    {
        return -1;
    }
    // Past every line's rows, the gap below the text; else the line whose
    // height the y falls in, where it falls above its text.
    const S32 lines = mHeights.total();
    if (y >= lines)
    {
        return mEndGap > 0 && y < lines + mEndGap * rowHeight() ? lineCount() : -1;
    }
    const S32 line = static_cast<S32>(mHeights.reach(y));
    if (line < 0 || line >= lineCount() || mHidden[line])
    {
        return -1;
    }
    return y < mHeights.before(static_cast<size_t>(line)) + gapHeight(line) ? line : -1;
}

S32 ALTextLayout::lineAtY(S32 y)
{
    ensureHeights();
    if (mLines.empty())
    {
        return 0;
    }
    // The last line whose top is at or above y. Hidden lines share a top
    // with the line after them, so the last of a run is the one in sight
    // -- unless the run reaches the end, where the nearest in sight is
    // above it: found through the heights, not by stepping back over the
    // run, which a comparison folding all after its last change has in
    // sight whenever what it shows fits the view.
    const S32 line = llclamp(static_cast<S32>(mHeights.reach(y)), 0, lineCount() - 1);
    if (mHidden[line])
    {
        const S32 above = visibleBefore(line + 1);
        if (above >= 0)
        {
            return above;
        }
        const S32 below = visibleAfter(line);
        if (below < lineCount())
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
    if (entry.rows.size() <= 1)
    {
        return 0;
    }
    // The first row whose bottom is below y; the last, past them all.
    const auto last = entry.rows.end() - 1;
    const auto it   = std::upper_bound(entry.rows.begin(), last, y, [](S32 at, const Row& row) { return at < row.top + row.height; });
    return static_cast<S32>(it - entry.rows.begin());
}

S32 ALTextLayout::rowOf(S32 index, S32 column)
{
    const Line& entry = line(index);
    if (entry.rows.size() <= 1)
    {
        return 0;
    }
    // The first row that ends past the column; the last, past them all.
    const auto last  = entry.rows.end() - 1;
    const auto after = [column](const Row& row) { return column < row.end; };
    const auto it    = entry.ordered ? std::upper_bound(entry.rows.begin(), last, column, [](S32 at, const Row& row) { return at < row.end; })
                                     : std::find_if(entry.rows.begin(), last, after);
    return static_cast<S32>(it - entry.rows.begin());
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
    size_t     k   = row.glyphBegin;
    if (entry.ordered)
    {
        const auto first = entry.glyphs.begin() + static_cast<std::ptrdiff_t>(row.glyphBegin);
        const auto end   = entry.glyphs.begin() + static_cast<std::ptrdiff_t>(row.glyphEnd);
        k                = static_cast<size_t>(std::partition_point(first, end, [column](const Glyph& g) { return g.cluster < column; }) - entry.glyphs.begin());
    }
    for (; k < row.glyphEnd; ++k)
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
    const size_t which = static_cast<size_t>(llclamp(r, 0, static_cast<S32>(entry.rows.size()) - 1));
    const Row&   row   = entry.rows[which];
    if (x <= 0.f)
    {
        return row.begin;
    }
    // The first glyph after the row's first that begins a cluster past x,
    // and the one that begins the cluster before it: x lies between their
    // left edges. The pens go forward whichever way the text is written,
    // so both are found by a search, and a step or two over the glyphs of
    // one cluster. An inlay is a cell of its own, though it shares its
    // column with the character beside it: a point on it is its column,
    // and the character rounds by its own middle, not by the middle of
    // the two together.
    if (row.glyphBegin >= row.glyphEnd)
    {
        return row.end;
    }
    const auto starts_one = [&](size_t k) {
        return k == row.glyphBegin || entry.glyphs[k].cluster != entry.glyphs[k - 1].cluster ||
               (entry.glyphs[k].inlay >= 0) != (entry.glyphs[k - 1].inlay >= 0);
    };
    const auto first      = entry.glyphs.begin() + static_cast<std::ptrdiff_t>(row.glyphBegin);
    const auto end        = entry.glyphs.begin() + static_cast<std::ptrdiff_t>(row.glyphEnd);
    size_t     k = static_cast<size_t>(std::partition_point(first + 1, end, [&](const Glyph& g) { return g.pen - row.xStart <= x; }) - entry.glyphs.begin());
    while (k < row.glyphEnd && !starts_one(k))
    {
        ++k;
    }
    size_t before = k > row.glyphBegin ? k - 1 : row.glyphBegin;
    while (before > row.glyphBegin && !starts_one(before))
    {
        --before;
    }
    const bool at_end  = k >= row.glyphEnd;
    const S32  cluster = entry.glyphs[before].cluster;
    // A row the line wraps after ends where the next row begins, which is
    // a place on the next row: on or past its last cluster is that
    // cluster -- before the space that hangs past the edge -- so that the
    // column is on the row it was asked of.
    if (at_end && which + 1 < entry.rows.size())
    {
        return cluster;
    }
    const F32 right = at_end ? row.width : entry.glyphs[k].pen - row.xStart;
    const F32 left  = entry.glyphs[before].pen - row.xStart;
    if (x >= right)
    {
        return row.end;
    }
    const S32 next_cluster = at_end ? row.end : entry.glyphs[k].cluster;
    return (round && (x - left) * 2.f >= (right - left)) ? next_cluster : cluster;
}

// static
ALTextLayout::Row ALTextLayout::rowWithin(const Line& line, const Row& row, F32 from, F32 to)
{
    if (!line.ordered || row.glyphBegin >= row.glyphEnd)
    {
        return row;
    }
    const auto first = line.glyphs.begin() + static_cast<std::ptrdiff_t>(row.glyphBegin);
    const auto end   = line.glyphs.begin() + static_cast<std::ptrdiff_t>(row.glyphEnd);
    // The first glyph whose right edge reaches `from`, and the first whose
    // left edge is past `to`; one more either side of those.
    auto lo = std::partition_point(first, end, [&](const Glyph& g) { return g.pen + g.advance - row.xStart < from; });
    auto hi = std::partition_point(lo, end, [&](const Glyph& g) { return g.pen - row.xStart <= to; });
    lo      = lo == first ? lo : lo - 1;
    hi      = hi == end ? hi : hi + 1;
    if (lo == first && hi == end)
    {
        return row;
    }
    Row seen        = row;
    seen.glyphBegin = static_cast<size_t>(lo - line.glyphs.begin());
    seen.glyphEnd   = static_cast<size_t>(hi - line.glyphs.begin());
    seen.begin      = lo == first ? row.begin : lo->cluster;
    seen.end        = hi == end ? row.end : hi->cluster;
    return seen;
}
