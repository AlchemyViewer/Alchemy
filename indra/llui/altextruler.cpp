/**
 * @file altextruler.cpp
 * @brief The bar down the side of a text view: a ruler of where things are, or a map of the text.
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

#include "altextruler.h"

#include "alsurface.h"
#include "altextchars.h"
#include "altextfeatures.h"
#include "altextview.h"
#include "llfocusmgr.h"
#include "lllocalcliprect.h"
#include "llrender2dutils.h"
#include "llwindow.h"

#include <algorithm>
#include <string>

namespace
{
    // The map: a line's height and a character's width in it, the room
    // around its lines, and a mark's width beside one.
    const S32 MAP_LINE_H = 2;
    const S32 MAP_CHAR_W = 1;
    const S32 MAP_PAD    = 2;
    const S32 MAP_MARK_W = 3;
}

ALTextRuler::Params::Params()
:   view("view")
{
    mouse_opaque = true;
}

ALTextRuler::ALTextRuler(const Params& p)
:   LLView(p),
    mView(*p.view())
{
}

bool ALTextRuler::isMap() const
{
    return mView.scrollMap();
}

// --- the ruler ------------------------------------------------------------------------

LLRect ALTextRuler::vThumb(const LLRect& track)
{
    const S32 total   = llmax(1, mView.layout().totalHeight());
    const S32 page    = llmax(1, mView.textRect().getHeight());
    const S32 track_h = llmax(1, track.getHeight());
    const S32 thumb_h = llclamp(track_h * page / total, llmin(THUMB_MIN, track_h), track_h);
    const S32 range   = llmax(1, total - page);
    const S32 travel  = track_h - thumb_h;
    const S32 top     = track.mTop - static_cast<S32>(static_cast<F32>(mView.scrollY()) / static_cast<F32>(range) * static_cast<F32>(travel));
    return LLRect(track.mLeft + 2, top, track.mRight - 2, top - thumb_h);
}

void ALTextRuler::scrollToRulerY(S32 y, S32 offset)
{
    if (isMap() || !getVisible())
    {
        return;
    }
    const LLRect track  = getLocalRect();
    const LLRect thumb  = vThumb(track);
    const S32    travel = llmax(1, track.getHeight() - thumb.getHeight());
    const S32    total  = llmax(1, mView.layout().totalHeight());
    const S32    page   = llmax(1, mView.textRect().getHeight());
    const S32    top    = y - offset;
    mView.setScrollY(static_cast<S32>(static_cast<F32>(track.mTop - top) / static_cast<F32>(travel) * static_cast<F32>(llmax(0, total - page))));
}

void ALTextRuler::drawRuler(F32 alpha)
{
    // The caret and the marks always, the thumb while wanted.
    const LLRect          ruler    = getLocalRect();
    const LLColor4&       ink      = mView.textColor();
    const F32             shown    = mView.barAlpha() * alpha;
    ALTextLayout&         layout   = mView.layout();
    const ALTextDocument& document = mView.document();
    const ALTextFeatures* features = mView.features();
    gl_rect_2d(ruler, ink % (0.04f * alpha));
    const S32 total   = llmax(1, layout.totalHeight());
    const S32 track_h = llmax(1, ruler.getHeight());
    const auto yOf    = [&](S32 line) { return ruler.mTop - static_cast<S32>(static_cast<F32>(layout.lineTop(line)) / static_cast<F32>(total) * static_cast<F32>(track_h)); };
    const S32 middle  = ruler.mLeft + WIDTH / 2;
    // The lines with a mark, found again only where the text or the
    // marks have changed; their colours asked every frame, which a
    // change of theme may change.
    const U32 marks_revision = features ? features->marksRevision() : 0;
    if (mMarksVersion != document.version() || mMarksRevision != marks_revision || !mMarksValid)
    {
        mMarkLines.clear();
        LLColor4  unused;
        const S32 count = features ? document.lineCount() : 0;
        for (S32 line = 0; line < count; ++line)
        {
            if (features->mapMark(line, unused))
            {
                mMarkLines.push_back(line);
            }
        }
        mMarksVersion  = document.version();
        mMarksRevision = marks_revision;
        mMarksValid    = true;
    }
    LLColor4 mark;
    for (const S32 line : mMarkLines)
    {
        if (features->mapMark(line, mark))
        {
            const S32 y = yOf(line);
            gl_rect_2d(middle, y, ruler.mRight - 2, y - 2, mark % alpha);
        }
    }
    // The matches by the pixel rows they fall on, found again only as
    // they, the track or the text's height change, and drawn as runs:
    // a letter found over a long text is tens of thousands of matches
    // and a few hundred rows.
    const std::vector<ALTextRange>& matches = mView.matchesFound();
    if (!matches.empty())
    {
        if (!mMatchRowsValid || mMatchRowsGeneration != mView.matchesGeneration() || mMatchRowsTop != ruler.mTop || mMatchRowsHeight != track_h ||
            mMatchRowsTotal != total)
        {
            mMatchRows.assign(static_cast<size_t>(track_h) + 2, 0);
            for (const ALTextRange& match : matches)
            {
                const S32 row = llclamp(ruler.mTop - yOf(match.begin.line), 0, track_h);
                mMatchRows[static_cast<size_t>(row)]     = 1;
                mMatchRows[static_cast<size_t>(row) + 1] = 1;
            }
            mMatchRowsValid      = true;
            mMatchRowsGeneration = mView.matchesGeneration();
            mMatchRowsTop        = ruler.mTop;
            mMatchRowsHeight     = track_h;
            mMatchRowsTotal      = total;
        }
        const LLColor4 found = mView.findMatchColor() % alpha;
        for (size_t row = 0; row < mMatchRows.size();)
        {
            if (!mMatchRows[row])
            {
                ++row;
                continue;
            }
            size_t end = row;
            while (end < mMatchRows.size() && mMatchRows[end])
            {
                ++end;
            }
            gl_rect_2d(ruler.mLeft + 2, ruler.mTop - static_cast<S32>(row), middle, ruler.mTop - static_cast<S32>(end), found);
            row = end;
        }
    }
    // The blip: where the caret is.
    const S32 caret_y = yOf(mView.caret().line);
    gl_rect_2d(ruler.mLeft + 2, caret_y, ruler.mRight - 2, caret_y - 2, mView.cursorColor() % alpha);
    if (shown > 0.f)
    {
        gl_rect_2d(vThumb(ruler), ink % (0.35f * shown));
    }
}

// --- the map ---------------------------------------------------------------------------

S32 ALTextRuler::mapScroll(const LLRect& map)
{
    // The lines the map shows, and how far its window is down them: as
    // far, in proportion, as the text is scrolled. The lines found again
    // only where which are hidden may have changed.
    ALTextLayout& layout = mView.layout();
    const S32     count  = mView.document().lineCount();
    if (!mMapLinesValid || mMapLinesRevision != layout.hiddenRevision() || mMapLinesCount != count)
    {
        mMapLines.clear();
        for (S32 line = 0; line < count; ++line)
        {
            if (!layout.hidden(line))
            {
                mMapLines.push_back(line);
            }
        }
        mMapLinesRevision = layout.hiddenRevision();
        mMapLinesCount    = count;
        mMapLinesValid    = true;
    }
    const S32 doc_h = static_cast<S32>(mMapLines.size()) * MAP_LINE_H;
    const S32 map_h = map.getHeight() - 2 * MAP_PAD;
    if (doc_h <= map_h)
    {
        return 0;
    }
    const S32 total = layout.totalHeight();
    const S32 page  = llmax(1, mView.textRect().getHeight());
    const F32 how   = total > page ? static_cast<F32>(mView.scrollY()) / static_cast<F32>(total - page) : 0.f;
    return static_cast<S32>(llclamp(how, 0.f, 1.f) * static_cast<F32>(doc_h - map_h));
}

S32 ALTextRuler::mapLineAt(S32 y)
{
    const LLRect map = getLocalRect();
    if (!isMap() || map.isEmpty() || mView.document().lineCount() == 0)
    {
        return -1;
    }
    const S32 scroll  = mapScroll(map);
    const S32 ordinal = llclamp((map.mTop - MAP_PAD - y + scroll) / MAP_LINE_H, 0, static_cast<S32>(mMapLines.size()) - 1);
    return mMapLines.empty() ? -1 : mMapLines[ordinal];
}

void ALTextRuler::drawPreview(F32 alpha)
{
    if (mHoverY < 0 || mDragging)
    {
        return;
    }
    const LLRect    map  = getLocalRect();
    const S32       line = mapLineAt(mHoverY);
    const LLFontGL* font = mView.getFont();
    if (line < 0 || !font)
    {
        return;
    }
    // Seven lines around the one under the mouse, in a box beside the
    // map, on the view's ground a shade towards its ink, the line under
    // the mouse washed as the caret's line is; kept on screen.
    const S32             PAD      = 6;
    const S32             GUTTER   = 8;
    const ALTextDocument& document = mView.document();
    const S32             count    = document.lineCount();
    const S32             first    = llmax(0, line - 3);
    const S32             last     = llmin(count - 1, line + 3);
    const S32             row_h    = mView.layout().rowHeight();
    const S32             rows     = last - first + 1;
    // The view's own rect, where the ruler has it.
    LLRect local = mView.getLocalRect();
    local.translate(-getRect().mLeft, -getRect().mBottom);
    const S32    width  = llmin(560, llmax(160, local.getWidth() - map.getWidth() - 3 * PAD));
    const S32    height = rows * row_h + 2 * PAD;
    S32          top    = llmin(local.mTop - PAD, mHoverY + height / 2);
    top                 = llmax(top, local.mBottom + PAD + height);
    const S32    right  = map.mLeft - PAD;
    const LLRect box(right - width, top, right, top - height);
    const LLColor4& bg    = mView.backgroundColor();
    const LLColor4& ink   = mView.textColor();
    const LLColor4  faint = ALSurface::shade(bg, ink, 0.45f) % alpha;
    const LLColor4  wash  = ALSurface::shade(bg, ink, 0.14f) % alpha;
    ALSurface::draw(box, bg, ink, alpha);
    LLLocalClipRect clip(LLRect(box.mLeft + 1, box.mTop - 1, box.mRight - 1, box.mBottom + 1));
    // The numbers take the room the widest needs.
    const std::string widest    = std::to_string(last + 1);
    const S32         numbers   = static_cast<S32>(font->getWidth(widest)) + GUTTER;
    const S32         tab_width = mView.getTabWidth();
    S32               y         = box.mTop - PAD;
    for (S32 l = first; l <= last; ++l, y -= row_h)
    {
        if (l == line)
        {
            gl_rect_2d(LLRect(box.mLeft + 1, y, box.mRight - 1, y - row_h), wash, true);
        }
        const S32 baseline = y - row_h + static_cast<S32>(font->getDescenderHeight()) + 1;
        font->renderUTF8(std::to_string(l + 1), 0, static_cast<F32>(box.mLeft + PAD + numbers - GUTTER), static_cast<F32>(baseline),
                         faint, LLFontGL::RIGHT, LLFontGL::BOTTOM, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
        // The line in its colours, token by token, tabs as spaces.
        const std::string&                text   = document.line(l);
        const std::vector<ALSyntaxToken>& tokens = mView.highlighter().tokens(l);
        F32                               x      = static_cast<F32>(box.mLeft + PAD + numbers);
        const F32                         limit  = static_cast<F32>(box.mRight - PAD);
        // The tabs to the view's own stops, counted along the whole line.
        S32                               column = 0;
        auto                              run    = [&](S32 begin, S32 end, const LLColor4& color) {
            if (begin >= end || x >= limit)
            {
                return;
            }
            const std::string piece = alExpandTabs(std::string_view(text).substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin)), column, tab_width);
            F32 right_x = x;
            font->renderUTF8(piece, 0, x, static_cast<F32>(baseline), color % alpha, LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::NORMAL,
                             LLFontGL::NO_SHADOW, S32_MAX, static_cast<S32>(limit - x), &right_x, false);
            x = right_x;
        };
        S32 at = 0;
        for (const ALSyntaxToken& token : tokens)
        {
            run(at, token.begin, ink);
            run(token.begin, token.end, token.kind == ALSyntaxKind::Text ? ink : mView.colorForKind(token.kind));
            at = token.end;
        }
        run(at, static_cast<S32>(text.size()), ink);
    }
}

void ALTextRuler::scrollToMapY(S32 y)
{
    const S32 line = mapLineAt(y);
    if (line < 0)
    {
        return;
    }
    const S32 page = llmax(1, mView.textRect().getHeight());
    mView.setScrollY(mView.layout().lineTop(line) - page / 2);
}

void ALTextRuler::readMapRuns(S32 line, S32 columns, std::vector<MapRun>& out)
{
    // Each run of glyphs of one kind, by column, as far as the map is
    // wide; spaces and tabs part runs.
    const std::string&                text      = mView.document().line(line);
    const std::vector<ALSyntaxToken>& tokens    = mView.highlighter().tokens(line);
    const S32                         tab_width = mView.getTabWidth();
    size_t                            t         = 0;
    S32                               col       = 0;
    S32                               from      = -1;
    ALSyntaxKind                      kind      = ALSyntaxKind::Text;
    const auto                        end       = [&](S32 to) {
        if (from >= 0)
        {
            const S32 x0 = llmin(columns, from);
            const S32 x1 = llmin(columns, to);
            if (x1 > x0)
            {
                out.push_back(MapRun{ x0, x1, kind });
            }
            from = -1;
        }
    };
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xC0) == 0x80)
        {
            continue;  // the rest of a character
        }
        while (t < tokens.size() && tokens[t].end <= static_cast<S32>(i))
        {
            ++t;
        }
        const ALSyntaxKind here = t < tokens.size() && tokens[t].begin <= static_cast<S32>(i) ? tokens[t].kind : ALSyntaxKind::Text;
        if (c == '\t')
        {
            end(col);
            col = alNextTabStop(col, tab_width);
        }
        else if (c == ' ')
        {
            end(col);
            ++col;
        }
        else
        {
            if (from >= 0 && here != kind)
            {
                end(col);
            }
            if (from < 0)
            {
                from = col;
                kind = here;
            }
            ++col;
        }
        if (col > columns)
        {
            break;
        }
    }
    end(col);
}

void ALTextRuler::drawMap(F32 alpha)
{
    const LLRect    map  = getLocalRect();
    const LLColor4& bg   = mView.backgroundColor();
    const LLColor4& ink  = mView.textColor();
    LLColor4        base = bg;
    for (S32 i = 0; i < 3; ++i)
    {
        base.mV[i] = bg.mV[i] + (ink.mV[i] - bg.mV[i]) * 0.05f;
    }
    gl_rect_2d(map, base % alpha);

    const S32 scroll = mapScroll(map);
    const S32 rows   = static_cast<S32>(mMapLines.size());
    if (rows == 0)
    {
        return;
    }
    ALTextLayout&        layout      = mView.layout();
    ALSyntaxHighlighter& highlighter = mView.highlighter();
    const bool           on_left     = mView.scrollMapOnLeft();
    const S32            tab_width   = mView.getTabWidth();
    const S32            map_h       = map.getHeight() - 2 * MAP_PAD;
    const S32            first       = scroll / MAP_LINE_H;
    const S32            last        = llmin(rows - 1, (scroll + map_h) / MAP_LINE_H);
    const S32            inner_left  = map.mLeft + MAP_PAD + (on_left ? MAP_MARK_W : 0);
    const S32            inner_right = map.mRight - MAP_PAD - (on_left ? 0 : MAP_MARK_W);
    const S32            mark_left   = on_left ? map.mLeft + 1 : map.mRight - MAP_MARK_W;
    LLLocalClipRect clip(map);
    // Each line's runs of text, read again only where something they are
    // read from has moved.
    const S32 columns = llmax(0, (inner_right - inner_left) / MAP_CHAR_W);
    MapRuns&  runs    = mMapRuns;
    bool      fresh   = runs.version == mView.document().version() && runs.grammar == highlighter.grammar().get() && runs.tabWidth == tab_width &&
                 runs.columns == columns && runs.first == first && runs.last == last && runs.hidden == layout.hiddenRevision();
    for (S32 o = first; fresh && o <= last; ++o)
    {
        fresh = runs.revisions[static_cast<size_t>(o - first)] == highlighter.revision(mMapLines[o]);
    }
    if (!fresh)
    {
        runs.version  = mView.document().version();
        runs.grammar  = highlighter.grammar().get();
        runs.tabWidth = tab_width;
        runs.columns  = columns;
        runs.first    = first;
        runs.last     = last;
        runs.hidden   = layout.hiddenRevision();
        runs.starts.clear();
        runs.revisions.clear();
        runs.runs.clear();
        for (S32 o = first; o <= last; ++o)
        {
            const S32 line = mMapLines[o];
            runs.starts.push_back(runs.runs.size());
            runs.revisions.push_back(highlighter.revision(line));
            readMapRuns(line, columns, runs.runs);
        }
        runs.starts.push_back(runs.runs.size());
    }
    // Every run, mark and tick in one batch, which is one draw: a rectangle
    // drawn apiece is a draw apiece, and the map has thousands.
    const ALTextFeatures*           features = mView.features();
    const std::vector<ALTextRange>& matches  = mView.matchesFound();
    const S32                       caret    = mView.caret().line;
    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLES);
    for (S32 o = first; o <= last; ++o)
    {
        const S32 line   = mMapLines[o];
        const S32 top    = map.mTop - MAP_PAD - (o * MAP_LINE_H - scroll);
        const S32 bottom = top - MAP_LINE_H;
        // Each run of glyphs, a rectangle in its kind's ink.
        const size_t from = runs.starts[static_cast<size_t>(o - first)];
        const size_t to   = runs.starts[static_cast<size_t>(o - first) + 1];
        for (size_t r = from; r < to; ++r)
        {
            const MapRun&   run      = runs.runs[r];
            const bool      plain    = run.kind == ALSyntaxKind::Text;
            const LLColor4& kind_ink = plain ? ink : mView.colorForKind(run.kind);
            gl_rect_2d_in_batch(inner_left + run.from * MAP_CHAR_W, top, inner_left + run.to * MAP_CHAR_W, bottom, kind_ink % (alpha * (plain ? 0.45f : 0.7f)));
        }
        // A mark beside the line, and a match in it.
        LLColor4 mark;
        if (features && features->mapMark(line, mark))
        {
            gl_rect_2d_in_batch(mark_left, top, mark_left + MAP_MARK_W, bottom - 1, mark % alpha);
        }
        if (!matches.empty())
        {
            auto found = std::lower_bound(matches.begin(), matches.end(), line, [](const ALTextRange& m, S32 l) { return m.end.line < l; });
            if (found != matches.end() && found->begin.line <= line)
            {
                const S32 tick_left = on_left ? map.mRight - MAP_MARK_W : map.mLeft + 1;
                gl_rect_2d_in_batch(tick_left, top, tick_left + MAP_MARK_W, bottom - 1, mView.findMatchColor() % alpha);
            }
        }
        if (line == caret)
        {
            gl_rect_2d_in_batch(inner_left, top, inner_right, top - 1, mView.cursorColor() % (alpha * 0.6f));
        }
    }
    gGL.end();
    // The rows on screen, as a window over the map.
    const S32  page        = llmax(1, mView.textRect().getHeight());
    const S32  top_line    = layout.lineAtY(mView.scrollY());
    const S32  bottom_line = layout.lineAtY(mView.scrollY() + page - 1);
    const auto ordinal     = [&](S32 line) { return static_cast<S32>(std::lower_bound(mMapLines.begin(), mMapLines.end(), line) - mMapLines.begin()); };
    const S32  y0          = map.mTop - MAP_PAD - (ordinal(top_line) * MAP_LINE_H - scroll);
    const S32  y1          = map.mTop - MAP_PAD - ((ordinal(bottom_line) + 1) * MAP_LINE_H - scroll);
    gl_rect_2d(map.mLeft, y0, map.mRight, y1, ink % (alpha * 0.12f));
    gl_rect_2d(map.mLeft, y0, map.mRight, y1, ink % (alpha * 0.3f), false);
}

// --- LLView ------------------------------------------------------------------------------

void ALTextRuler::draw()
{
    const F32 alpha = getDrawContext().mAlpha;
    if (isMap())
    {
        drawMap(alpha);
    }
    else
    {
        drawRuler(alpha);
    }
}

bool ALTextRuler::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (mView.takesFocus())
    {
        mView.setFocus(true);
    }
    mDragging = true;
    gFocusMgr.setMouseCapture(this);
    if (isMap())
    {
        // The map: the view goes where it is pressed, and follows a drag.
        scrollToMapY(y);
        return true;
    }
    // The ruler: its thumb taken hold of where it was pressed, or brought
    // to where the track was.
    const LLRect thumb = vThumb(getLocalRect());
    mDragOffset        = thumb.pointInRect(x, y) ? y - thumb.mTop : -thumb.getHeight() / 2;
    scrollToRulerY(y, mDragOffset);
    return true;
}

bool ALTextRuler::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mDragging)
    {
        mDragging = false;
        gFocusMgr.setMouseCapture(nullptr);
        return true;
    }
    return LLView::handleMouseUp(x, y, mask);
}

bool ALTextRuler::handleHover(S32 x, S32 y, MASK mask)
{
    // The mouse is here: the bars stay in sight.
    mView.showBars();
    if (mDragging && hasMouseCapture())
    {
        if (isMap())
        {
            scrollToMapY(y);
        }
        else
        {
            scrollToRulerY(y, mDragOffset);
        }
    }
    else
    {
        // Resting on the map previews the lines there, as long as it rests.
        mHoverY = isMap() && mView.scrollMapPreview() ? y : -1;
    }
    // The arrow, as over any scroll bar, not the text's cursor.
    if (LLWindow* window = getWindow())
    {
        window->setCursor(UI_CURSOR_ARROW);
    }
    return true;
}

void ALTextRuler::onMouseLeave(S32 x, S32 y, MASK mask)
{
    mHoverY = -1;
    LLView::onMouseLeave(x, y, mask);
}

void ALTextRuler::onMouseCaptureLost()
{
    mDragging = false;
}
