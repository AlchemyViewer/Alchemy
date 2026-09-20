/**
 * @file alcodeeditor.cpp
 * @brief The text view as a code editor: a gutter, marks, a matched bracket, squiggles.
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

#include "alcodeeditor.h"

#include "llrender2dutils.h"
#include "lluicolortable.h"

#include <algorithm>
#include <cmath>

static LLDefaultChildRegistry::Register<ALCodeEditor> r("code_editor");

namespace
{
    const S32 GUTTER_PAD  = 6;
    const S32 MARK_SIZE   = 6;
    const S32 MARK_INSET  = 3;
    const F32 SQUIGGLE_AMPLITUDE = 1.5f;
    const F32 SQUIGGLE_WAVE      = 6.f;

    const char* const MARK_COLOR_NAMES[] = { "TextFgColor", "CodeMarkNote", "CodeMarkWarning", "CodeMarkError", "CodeMarkRuntime" };
    static_assert(sizeof(MARK_COLOR_NAMES) / sizeof(MARK_COLOR_NAMES[0]) == static_cast<size_t>(ALCodeEditor::Mark::COUNT), "every mark has a colour");

    // The bracket a character is, if any: its partner, and which way to look.
    bool bracketOf(char c, char& partner, bool& opens)
    {
        switch (c)
        {
            case '(': partner = ')'; opens = true;  return true;
            case '[': partner = ']'; opens = true;  return true;
            case '{': partner = '}'; opens = true;  return true;
            case ')': partner = '('; opens = false; return true;
            case ']': partner = '['; opens = false; return true;
            case '}': partner = '{'; opens = false; return true;
            default:  return false;
        }
    }

    bool quiet(ALSyntaxKind kind)
    {
        return kind == ALSyntaxKind::String || kind == ALSyntaxKind::Comment || kind == ALSyntaxKind::DocComment ||
               kind == ALSyntaxKind::Escape || kind == ALSyntaxKind::AttributeValue;
    }
}

ALCodeEditor::Params::Params()
:   show_line_numbers("show_line_numbers", true),
    highlight_current_line("highlight_current_line", true),
    match_brackets("match_brackets", true),
    gutter_color("gutter_color"),
    line_number_color("line_number_color"),
    current_line_color("current_line_color"),
    bracket_match_color("bracket_match_color")
{
}

ALCodeEditor::ALCodeEditor(const Params& p)
:   ALTextView(p),
    mShowLineNumbers(p.show_line_numbers),
    mHighlightCurrentLine(p.highlight_current_line),
    mMatchBrackets(p.match_brackets),
    mGutterColor(p.gutter_color),
    mLineNumberColor(p.line_number_color),
    mCurrentLineColor(p.current_line_color),
    mBracketMatchColor(p.bracket_match_color)
{
    for (size_t mark = 0; mark < static_cast<size_t>(Mark::COUNT); ++mark)
    {
        mMarkColors[mark] = LLUIColorTable::instance().getColor(MARK_COLOR_NAMES[mark], LLColor4::red);
    }
    mMarks.assign(document().lineCount(), Mark::None);
    mEditConnection = document().onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
}

// --- marks and decorations ---------------------------------------------------

void ALCodeEditor::onEdit(const ALTextDocument::Edit& edit)
{
    const S32 first = llclamp(edit.range.begin.line, 0, static_cast<S32>(mMarks.size()));
    const S32 last  = llclamp(edit.range.end.line, first, static_cast<S32>(mMarks.size()) - 1);
    const S32 made  = 1 + static_cast<S32>(std::count(edit.inserted.begin(), edit.inserted.end(), '\n'));
    if (first < static_cast<S32>(mMarks.size()))
    {
        mMarks.erase(mMarks.begin() + first, mMarks.begin() + last + 1);
    }
    mMarks.insert(mMarks.begin() + first, made, Mark::None);
    mMarks.resize(document().lineCount(), Mark::None);

    // Decorations below the edit slide by the lines it added or took; the
    // ones it cut through go.
    const S32 delta = made - (last - first + 1);
    mDecorations.erase(std::remove_if(mDecorations.begin(), mDecorations.end(),
                                      [&](const Decoration& d) {
                                          const ALTextRange r = d.range.normalised();
                                          return r.end.line >= first && r.begin.line <= last;
                                      }),
                       mDecorations.end());
    for (Decoration& d : mDecorations)
    {
        if (d.range.normalised().begin.line > last)
        {
            d.range.begin.line += delta;
            d.range.end.line += delta;
        }
    }
}

void ALCodeEditor::setMark(S32 line, Mark mark)
{
    if (line >= 0 && line < static_cast<S32>(mMarks.size()))
    {
        mMarks[line] = mark;
    }
}

ALCodeEditor::Mark ALCodeEditor::markAt(S32 line) const
{
    return (line >= 0 && line < static_cast<S32>(mMarks.size())) ? mMarks[line] : Mark::None;
}

void ALCodeEditor::clearMarks()
{
    std::fill(mMarks.begin(), mMarks.end(), Mark::None);
}

void ALCodeEditor::setDecorations(std::vector<Decoration> decorations)
{
    mDecorations = std::move(decorations);
}

// --- brackets ----------------------------------------------------------------

bool ALCodeEditor::matchingBrackets(ALTextPos& open, ALTextPos& close)
{
    const ALTextPos at = caret();
    const std::string& line = document().line(at.line);
    // The bracket just before the caret, else the one under it.
    ALTextPos from = at;
    char      c = 0, partner = 0;
    bool      opens = false;
    if (at.column > 0 && bracketOf(line[at.column - 1], partner, opens))
    {
        from = ALTextPos(at.line, at.column - 1);
        c    = line[at.column - 1];
    }
    else if (at.column < static_cast<S32>(line.size()) && bracketOf(line[at.column], partner, opens))
    {
        c = line[at.column];
    }
    else
    {
        return false;
    }

    // Whether a byte of a line is inside a string or a comment.
    auto quietAt = [&](S32 l, S32 column) {
        for (const ALSyntaxToken& token : highlighter().tokens(l))
        {
            if (token.begin <= column && column < token.end)
            {
                return quiet(token.kind);
            }
        }
        return false;
    };
    if (quietAt(from.line, from.column))
    {
        return false;
    }

    S32 depth = 0;
    if (opens)
    {
        for (S32 l = from.line; l < document().lineCount(); ++l)
        {
            const std::string& text = document().line(l);
            for (S32 i = (l == from.line ? from.column : 0); i < static_cast<S32>(text.size()); ++i)
            {
                if ((text[i] == c || text[i] == partner) && !quietAt(l, i))
                {
                    depth += (text[i] == c) ? 1 : -1;
                    if (depth == 0)
                    {
                        open  = from;
                        close = ALTextPos(l, i);
                        return true;
                    }
                }
            }
        }
    }
    else
    {
        for (S32 l = from.line; l >= 0; --l)
        {
            const std::string& text = document().line(l);
            for (S32 i = (l == from.line ? from.column : static_cast<S32>(text.size()) - 1); i >= 0; --i)
            {
                if ((text[i] == c || text[i] == partner) && !quietAt(l, i))
                {
                    depth += (text[i] == c) ? 1 : -1;
                    if (depth == 0)
                    {
                        open  = ALTextPos(l, i);
                        close = from;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// --- the gutter --------------------------------------------------------------

void ALCodeEditor::setShowLineNumbers(bool show)
{
    mShowLineNumbers = show;
    reshape(getRect().getWidth(), getRect().getHeight());
}

S32 ALCodeEditor::gutterWidth() const
{
    if (!mShowLineNumbers || !getFont())
    {
        return 0;
    }
    S32 digits = 1;
    for (S32 n = document().lineCount(); n >= 10; n /= 10)
    {
        ++digits;
    }
    digits = llmax(digits, 3);
    return MARK_INSET + MARK_SIZE + GUTTER_PAD + digits * getFont()->getWidth("8") + GUTTER_PAD;
}

void ALCodeEditor::goToLine(S32 line)
{
    setCaret(document().lineStart(line));
}

void ALCodeEditor::drawGutter(const LLRect& text, F32 alpha)
{
    const S32 width = gutterWidth();
    if (width <= 0)
    {
        return;
    }
    const LLRect local = getLocalRect();
    const LLRect gutter(local.mLeft, local.mTop, local.mLeft + width, local.mBottom);
    gl_rect_2d(gutter, mGutterColor.get() % alpha);

    const LLFontGL* font   = getFont();
    const S32       row_h  = layout().rowHeight();
    const S32       ascent = llround(font->getAscenderHeight());
    const F32       right  = static_cast<F32>(gutter.mRight - GUTTER_PAD);
    const LLColor4  ink    = mLineNumberColor.get() % alpha;
    forEachVisibleRow(text, [&](S32 line, S32 row, S32 screen_top) {
        if (row != 0)
        {
            return;
        }
        font->renderUTF8(std::to_string(line + 1), 0, right, static_cast<F32>(screen_top - ascent), ink, LLFontGL::RIGHT, LLFontGL::BASELINE);
        const Mark mark = markAt(line);
        if (mark != Mark::None)
        {
            const S32 y = screen_top - (row_h - MARK_SIZE) / 2;
            gl_rect_2d(gutter.mLeft + MARK_INSET, y, gutter.mLeft + MARK_INSET + MARK_SIZE, y - MARK_SIZE,
                       mMarkColors[static_cast<size_t>(mark)].get() % alpha);
        }
    });
}

void ALCodeEditor::drawBeforeRows(const LLRect& text)
{
    const F32 alpha = getDrawContext().mAlpha;
    if (mHighlightCurrentLine && hasFocus() && !hasSelection())
    {
        S32 row;
        layout().xOf(caret().line, caret().column, &row);
        const S32 top = screenTopOf(text, caret().line, row);
        gl_rect_2d(text.mLeft, top, text.mRight, top - layout().rowHeight(), mCurrentLineColor.get() % alpha);
    }
    drawGutter(text, alpha);
}

// --- over the rows -------------------------------------------------------------

bool ALCodeEditor::spanOnRow(S32 line, S32 row, const ALTextRange& range_in, F32& x0, F32& x1)
{
    const ALTextRange range = range_in.normalised();
    if (line < range.begin.line || line > range.end.line)
    {
        return false;
    }
    const ALTextLayout::Line& laid = layout().line(line);
    if (row < 0 || row >= static_cast<S32>(laid.rows.size()))
    {
        return false;
    }
    const ALTextLayout::Row& r        = laid.rows[row];
    const S32                length   = document().lineLength(line);
    const bool               last_row = (row + 1 == static_cast<S32>(laid.rows.size()));
    const S32                lo       = llmax(range.begin.line < line ? 0 : range.begin.column, r.begin);
    const S32                hi       = llmin(range.end.line > line ? length + 1 : range.end.column, last_row ? length + 1 : r.end);
    if (lo > hi || (lo == hi && !range.empty()))
    {
        return false;
    }
    x0 = layout().xOf(line, lo);
    x1 = hi > length ? r.width + 6.f : (hi >= r.end && !last_row ? r.width : layout().xOf(line, hi));
    if (x1 <= x0)
    {
        x1 = x0 + 4.f;
    }
    return true;
}

void ALCodeEditor::drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color)
{
    mSquiggleScratch.clear();
    for (F32 x = x0; x <= x1; x += 1.f)
    {
        mSquiggleScratch.emplace_back(x, static_cast<F32>(y) + SQUIGGLE_AMPLITUDE * sinf((x - x0) * (2.f * F_PI / SQUIGGLE_WAVE)));
    }
    if (mSquiggleScratch.size() >= 2)
    {
        gl_polyline_2d(mSquiggleScratch, color, 1.f);
    }
}

void ALCodeEditor::drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha)
{
    const S32 row_h = layout().rowHeight();
    for (const Decoration& d : mDecorations)
    {
        F32 x0, x1;
        if (!spanOnRow(line, row, d.range, x0, x1))
        {
            continue;
        }
        if (d.style == Decoration::Style::Background)
        {
            gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, d.color % alpha);
        }
        else
        {
            drawSquiggle(left + x0, left + x1, screen_top - row_h + 2, d.color % alpha);
        }
    }
    if (mMatchBrackets && hasFocus())
    {
        ALTextPos open, close;
        if (matchingBrackets(open, close))
        {
            for (const ALTextPos& at : { open, close })
            {
                F32 x0, x1;
                if (spanOnRow(line, row, ALTextRange(at, document().nextCluster(at)), x0, x1))
                {
                    gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, mBracketMatchColor.get() % alpha, false);
                }
            }
        }
    }
}
