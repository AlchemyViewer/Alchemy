/**
 * @file alcodeeditor.cpp
 * @brief The text view as a code editor: a gutter, marks, a matched bracket, squiggles, folds, completion.
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

#include "llfocusmgr.h"
#include "llrender2dutils.h"
#include "llscrolllistctrl.h"
#include "llstring.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <boost/unordered/unordered_flat_set.hpp>

#include <algorithm>
#include <cmath>

static LLDefaultChildRegistry::Register<ALCodeEditor> r("code_editor");

namespace
{
    const S32 GUTTER_PAD  = 6;
    const S32 MARK_SIZE   = 6;
    const S32 MARK_INSET  = 3;
    const S32 FOLD_COLUMN = 12;
    const S32 FOLD_MARKER = 7;
    const S32 FOLD_BOX_GAP = 6;
    const F32 SQUIGGLE_AMPLITUDE = 1.5f;
    const F32 SQUIGGLE_WAVE      = 6.f;
    const S32 COMPLETION_WIDTH   = 360;
    const S32 COMPLETION_ROWS    = 8;
    const S32 COMPLETION_AUTO_AT = 2;
    const size_t COMPLETION_CAP  = 200;

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

    bool identifierByte(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    // A line with nothing but whitespace, or nothing.
    std::string_view trimmed(const std::string& line)
    {
        size_t begin = line.find_first_not_of(" \t\r");
        if (begin == std::string::npos)
        {
            return std::string_view();
        }
        size_t end = line.find_last_not_of(" \t\r");
        return std::string_view(line).substr(begin, end - begin + 1);
    }

    // What closes a block on a line of its own, and so belongs to the
    // block above it.
    bool closesBlock(std::string_view text)
    {
        static const char* const CLOSERS[] = { "}", "};", "})", "});", "end", "end)", "end,", "end);", ")", ");", "]", "],", "];" };
        for (const char* closer : CLOSERS)
        {
            if (text == closer)
            {
                return true;
            }
        }
        return false;
    }

    ALSyntaxKind kindOfTable(std::string_view table)
    {
        if (table == "function") return ALSyntaxKind::Function;
        if (table == "event") return ALSyntaxKind::Event;
        if (table == "type") return ALSyntaxKind::Type;
        if (table == "constant") return ALSyntaxKind::Constant;
        if (table == "control") return ALSyntaxKind::Control;
        if (table == "keyword") return ALSyntaxKind::Keyword;
        if (table == "deprecated") return ALSyntaxKind::Deprecated;
        return ALSyntaxKind::Text;
    }
}

ALCodeEditor::Params::Params()
:   show_line_numbers("show_line_numbers", true),
    show_fold_markers("show_fold_markers", true),
    highlight_current_line("highlight_current_line", true),
    match_brackets("match_brackets", true),
    auto_complete("auto_complete", true),
    gutter_color("gutter_color"),
    line_number_color("line_number_color"),
    current_line_color("current_line_color"),
    bracket_match_color("bracket_match_color"),
    fold_color("fold_color")
{
}

ALCodeEditor::ALCodeEditor(const Params& p)
:   ALTextView(p),
    mShowLineNumbers(p.show_line_numbers),
    mShowFoldMarkers(p.show_fold_markers),
    mHighlightCurrentLine(p.highlight_current_line),
    mMatchBrackets(p.match_brackets),
    mAutoComplete(p.auto_complete),
    mGutterColor(p.gutter_color),
    mLineNumberColor(p.line_number_color),
    mCurrentLineColor(p.current_line_color),
    mBracketMatchColor(p.bracket_match_color),
    mFoldColor(p.fold_color)
{
    for (size_t mark = 0; mark < static_cast<size_t>(Mark::COUNT); ++mark)
    {
        mMarkColors[mark] = LLUIColorTable::instance().getColor(MARK_COLOR_NAMES[mark], LLColor4::red);
    }
    mMarks.assign(document().lineCount(), Mark::None);
    mEditConnection    = document().onChanged([this](const ALTextDocument::Edit& edit) { onEdit(edit); });
    mChangedConnection = onTextChanged([this]() {
        if (completionOpen())
        {
            refreshCompletion();
        }
    });

    // The list of completions, made once and shown when there is
    // something to choose; a child, so it draws over the text and goes
    // where the view goes.
    LLScrollListCtrl::Params list(LLUICtrlFactory::getDefaultParams<LLScrollListCtrl>());
    list.name("completions");
    list.rect(LLRect(0, 10, 10, 0));
    list.visible(false);
    list.tab_stop(false);
    list.follows.flags(FOLLOWS_NONE);
    list.multi_select(false);
    list.commit_on_selection_change(false);
    list.commit_on_keyboard_movement(false);
    list.draw_heading(false);
    list.has_border(true);
    list.background_visible(true);
    list.can_sort(false);
    LLScrollListColumn::Params text_column;
    text_column.name("text");
    text_column.width.pixel_width(150);
    list.contents.columns.add(text_column);
    LLScrollListColumn::Params detail_column;
    detail_column.name("detail");
    detail_column.width.dynamic_width(true);
    list.contents.columns.add(detail_column);
    mCompletionList = LLUICtrlFactory::create<LLScrollListCtrl>(list);
    mCompletionList->setDoubleClickCallback([this]() { acceptCompletion(); });
    addChild(mCompletionList);
}

ALCodeEditor::~ALCodeEditor() = default;

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

    // Folds slide the same way. One that starts on the edit's first line
    // stays: typing on a block's first line is not opening the block.
    mFolded.erase(std::remove_if(mFolded.begin(), mFolded.end(), [&](S32 start) { return start > first && start <= last; }), mFolded.end());
    for (S32& start : mFolded)
    {
        if (start > last)
        {
            start += delta;
        }
    }
    mRegionsValid = false;
    if (!mFolded.empty())
    {
        applyFolds();
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

void ALCodeEditor::setShowFoldMarkers(bool show)
{
    mShowFoldMarkers = show;
    reshape(getRect().getWidth(), getRect().getHeight());
}

S32 ALCodeEditor::gutterWidth() const
{
    if (!getFont())
    {
        return 0;
    }
    S32 width = 0;
    if (mShowLineNumbers)
    {
        S32 digits = 1;
        for (S32 n = document().lineCount(); n >= 10; n /= 10)
        {
            ++digits;
        }
        digits = llmax(digits, 3);
        width += MARK_INSET + MARK_SIZE + GUTTER_PAD + digits * getFont()->getWidth("8") + GUTTER_PAD;
    }
    if (mShowFoldMarkers)
    {
        width += FOLD_COLUMN;
    }
    return width;
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
    const S32       numbers_right = gutter.mRight - (mShowFoldMarkers ? FOLD_COLUMN : 0) - GUTTER_PAD;
    const LLColor4  ink    = mLineNumberColor.get() % alpha;
    const LLColor4  fold   = mFoldColor.get() % alpha;
    if (mShowFoldMarkers)
    {
        ensureRegions();
    }
    forEachVisibleRow(text, [&](S32 line, S32 row, S32 screen_top) {
        if (row != 0)
        {
            return;
        }
        if (mShowLineNumbers)
        {
            font->renderUTF8(std::to_string(line + 1), 0, static_cast<F32>(numbers_right), static_cast<F32>(screen_top - ascent), ink, LLFontGL::RIGHT, LLFontGL::BASELINE);
            const Mark mark = markAt(line);
            if (mark != Mark::None)
            {
                const S32 y = screen_top - (row_h - MARK_SIZE) / 2;
                gl_rect_2d(gutter.mLeft + MARK_INSET, y, gutter.mLeft + MARK_INSET + MARK_SIZE, y - MARK_SIZE,
                           mMarkColors[static_cast<size_t>(mark)].get() % alpha);
            }
        }
        if (mShowFoldMarkers && regionStartingAt(line))
        {
            // A triangle: pointing down at an open block, right at a
            // folded one.
            const S32 cx = gutter.mRight - FOLD_COLUMN / 2;
            const S32 cy = screen_top - row_h / 2;
            const S32 h  = FOLD_MARKER / 2;
            if (isFolded(line))
            {
                gl_triangle_2d(cx - h + 1, cy + h, cx - h + 1, cy - h, cx + h - 1, cy, fold, true);
            }
            else
            {
                gl_triangle_2d(cx - h, cy + h - 1, cx + h, cy + h - 1, cx, cy - h + 1, fold, true);
            }
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

LLRect ALCodeEditor::foldBoxOf(S32 line, const LLRect& text)
{
    if (!isFolded(line))
    {
        return LLRect();
    }
    const ALTextLayout::Line& laid = layout().line(line);
    if (laid.rows.empty())
    {
        return LLRect();
    }
    const ALTextLayout::Row& last  = laid.rows.back();
    const S32                row   = static_cast<S32>(laid.rows.size()) - 1;
    const S32                top   = screenTopOf(text, line, row);
    const S32                row_h = layout().rowHeight();
    const S32                x0    = static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + last.xStart + last.width) + FOLD_BOX_GAP;
    const S32                w     = getFont()->getWidth("...") + 8;
    return LLRect(x0, top - 1, x0 + w, top - row_h + 1);
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
    // A folded block says so after its first line.
    if (row + 1 == layout().rowCount(line) && isFolded(line))
    {
        const LLRect box = foldBoxOf(line, text);
        if (box.notEmpty())
        {
            const LLColor4 ink = mFoldColor.get() % alpha;
            gl_rect_2d(box, ink, false);
            getFont()->renderUTF8("...", 0, static_cast<F32>(box.mLeft + 4), static_cast<F32>(screen_top - llround(getFont()->getAscenderHeight())), ink, LLFontGL::LEFT, LLFontGL::BASELINE);
        }
    }
}

// --- folding -----------------------------------------------------------------

void ALCodeEditor::ensureRegions()
{
    if (mRegionsValid && mRegionsVersion == document().version())
    {
        return;
    }
    mRegions.clear();
    const S32        count = document().lineCount();
    const S32        tab   = getTabWidth();
    std::vector<S32> indent(count, -1);
    for (S32 l = 0; l < count; ++l)
    {
        const std::string& line  = document().line(l);
        S32                n     = 0;
        bool               blank = true;
        for (char c : line)
        {
            if (c == ' ')
            {
                ++n;
            }
            else if (c == '\t')
            {
                n = (n / tab + 1) * tab;
            }
            else if (c != '\r')
            {
                blank = false;
                break;
            }
        }
        indent[l] = blank ? -1 : n;
    }
    // A block is a line and the deeper lines after it, with a line of
    // nothing going with whichever side keeps the block whole, and the
    // closer on the line after -- a brace, an `end` -- taken as part of it.
    std::vector<S32> end_of(count, -1);
    for (S32 l = 0; l < count; ++l)
    {
        if (indent[l] < 0)
        {
            continue;
        }
        S32 end = l;
        S32 k   = l + 1;
        for (; k < count; ++k)
        {
            if (indent[k] < 0)
            {
                continue;
            }
            if (indent[k] > indent[l])
            {
                end = k;
            }
            else
            {
                break;
            }
        }
        if (end == l)
        {
            continue;
        }
        if (k < count && indent[k] == indent[l] && closesBlock(trimmed(document().line(k))))
        {
            end = k;
        }
        end_of[l] = end;
    }
    // A brace on a line of its own is its header's: `default` and the
    // `{` under it fold as one block, from the header.
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[l] < 0 || trimmed(document().line(l)) != "{")
        {
            continue;
        }
        S32 header = l - 1;
        while (header >= 0 && indent[header] < 0)
        {
            --header;
        }
        if (header >= 0 && indent[header] == indent[l] && end_of[header] < 0)
        {
            end_of[header] = end_of[l];
            end_of[l]      = -1;
        }
    }
    for (S32 l = 0; l < count; ++l)
    {
        if (end_of[l] > l)
        {
            mRegions.push_back(FoldRegion{ l, end_of[l] });
        }
    }
    mRegionsVersion = document().version();
    mRegionsValid   = true;
}

const std::vector<ALCodeEditor::FoldRegion>& ALCodeEditor::foldRegions()
{
    ensureRegions();
    return mRegions;
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionStartingAt(S32 line)
{
    ensureRegions();
    const auto it = std::lower_bound(mRegions.begin(), mRegions.end(), line,
                                     [](const FoldRegion& region, S32 l) { return region.start < l; });
    return (it != mRegions.end() && it->start == line) ? &*it : nullptr;
}

const ALCodeEditor::FoldRegion* ALCodeEditor::regionAround(S32 line)
{
    ensureRegions();
    // The innermost: of those that hold the line, the one that starts last.
    const FoldRegion* found = nullptr;
    for (const FoldRegion& region : mRegions)
    {
        if (region.start >= line)
        {
            break;
        }
        if (region.end >= line)
        {
            found = &region;
        }
    }
    return found;
}

bool ALCodeEditor::isFolded(S32 line) const
{
    return std::binary_search(mFolded.begin(), mFolded.end(), line);
}

void ALCodeEditor::applyFolds()
{
    ensureRegions();
    layout().setHidden(0, document().lineCount() - 1, false);
    // A fold whose block is gone is gone with it.
    mFolded.erase(std::remove_if(mFolded.begin(), mFolded.end(), [&](S32 start) { return regionStartingAt(start) == nullptr; }), mFolded.end());
    for (S32 start : mFolded)
    {
        const FoldRegion* region = regionStartingAt(start);
        layout().setHidden(region->start + 1, region->end, true);
    }
}

bool ALCodeEditor::foldAt(S32 line)
{
    const FoldRegion* region = regionStartingAt(line);
    if (!region)
    {
        region = regionAround(line);
    }
    if (!region || isFolded(region->start))
    {
        return false;
    }
    const FoldRegion chosen = *region;
    mFolded.insert(std::upper_bound(mFolded.begin(), mFolded.end(), chosen.start), chosen.start);
    // The caret cannot stay in what is folded away.
    const ALTextRange sel = selection().normalised();
    if ((sel.begin.line > chosen.start && sel.begin.line <= chosen.end) || (sel.end.line > chosen.start && sel.end.line <= chosen.end))
    {
        setCaret(document().lineEnd(chosen.start));
    }
    applyFolds();
    return true;
}

bool ALCodeEditor::unfoldAt(S32 line)
{
    S32 start = -1;
    if (isFolded(line))
    {
        start = line;
    }
    else
    {
        // The innermost folded block around the line -- which, since a
        // folded block's lines are hidden, is the one whose first line
        // this is a descendant of.
        for (S32 folded : mFolded)
        {
            const FoldRegion* region = regionStartingAt(folded);
            if (region && region->start < line && line <= region->end)
            {
                start = folded;
            }
        }
    }
    if (start < 0)
    {
        return false;
    }
    mFolded.erase(std::remove(mFolded.begin(), mFolded.end(), start), mFolded.end());
    applyFolds();
    return true;
}

void ALCodeEditor::foldAll()
{
    ensureRegions();
    mFolded.clear();
    for (const FoldRegion& region : mRegions)
    {
        mFolded.push_back(region.start);
    }
    applyFolds();
    if (layout().hidden(caret().line))
    {
        setCaret(document().lineEnd(layout().visibleFrom(caret().line, -1)));
    }
}

void ALCodeEditor::unfoldAll()
{
    mFolded.clear();
    applyFolds();
}

void ALCodeEditor::revealLine(S32 line)
{
    // Every folded block the line is inside opens.
    bool changed = false;
    for (S32 i = static_cast<S32>(mFolded.size()) - 1; i >= 0; --i)
    {
        const FoldRegion* region = regionStartingAt(mFolded[i]);
        if (!region || (region->start < line && line <= region->end))
        {
            mFolded.erase(mFolded.begin() + i);
            changed = true;
        }
    }
    if (changed)
    {
        applyFolds();
    }
    else
    {
        ALTextView::revealLine(line);
    }
}

bool ALCodeEditor::performFold(ALEditorCommand command)
{
    switch (command)
    {
        case ALEditorCommand::Fold:
            return foldAt(caret().line);
        case ALEditorCommand::Unfold:
            return unfoldAt(caret().line);
        case ALEditorCommand::FoldAll:
            foldAll();
            return true;
        case ALEditorCommand::UnfoldAll:
            unfoldAll();
            return true;
        default:
            return false;
    }
}

bool ALCodeEditor::canFold(ALEditorCommand command) const
{
    ALCodeEditor* self = const_cast<ALCodeEditor*>(this);
    switch (command)
    {
        case ALEditorCommand::Fold:
        {
            const FoldRegion* region = self->regionStartingAt(caret().line);
            if (!region)
            {
                region = self->regionAround(caret().line);
            }
            return region && !isFolded(region->start);
        }
        case ALEditorCommand::Unfold:
            return !mFolded.empty();
        case ALEditorCommand::FoldAll:
            return mFolded.size() < self->foldRegions().size();
        case ALEditorCommand::UnfoldAll:
            return !mFolded.empty();
        default:
            return false;
    }
}

// --- completion --------------------------------------------------------------

bool ALCodeEditor::completionOpen() const
{
    return mCompletionList && mCompletionList->getVisible();
}

void ALCodeEditor::closeCompletion()
{
    if (mCompletionList)
    {
        mCompletionList->setVisible(false);
        mCompletionList->deleteAllItems();
    }
    mCompletions.clear();
}

S32 ALCodeEditor::chosenCompletion() const
{
    return completionOpen() ? mCompletionList->getFirstSelectedIndex() : -1;
}

void ALCodeEditor::vocabularyCompletions(std::string_view prefix, std::vector<Completion>& out)
{
    std::vector<std::pair<std::string, std::string>> words;
    if (highlighter().grammar())
    {
        highlighter().grammar()->collectWords(prefix, words);
    }
    highlighter().words().collect(prefix, words);
    for (auto& [word, table] : words)
    {
        Completion c;
        c.text   = word;
        c.kind   = kindOfTable(table);
        c.detail = alSyntaxKindName(c.kind);
        out.push_back(std::move(c));
    }
}

void ALCodeEditor::documentCompletions(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out)
{
    boost::unordered_flat_set<std::string, ll::string_hash, std::equal_to<>> seen;
    for (const Completion& c : out)
    {
        seen.insert(c.text);
    }
    auto begins = [&](std::string_view word) {
        if (word.size() < prefix.size())
        {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            if (LLStringOps::toLower(word[i]) != LLStringOps::toLower(prefix[i]))
            {
                return false;
            }
        }
        return true;
    };
    // The document's own words, other than the one being typed.
    const S32 count = document().lineCount();
    for (S32 l = 0; l < count && out.size() < COMPLETION_CAP; ++l)
    {
        const std::string& line = document().line(l);
        size_t             i    = 0;
        while (i < line.size())
        {
            if (!identifierByte(line[i]))
            {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < line.size() && identifierByte(line[j]))
            {
                ++j;
            }
            const bool typing = (l == at.line && static_cast<S32>(j) == at.column);
            if (!typing && (line[i] < '0' || line[i] > '9'))
            {
                std::string_view word(line.data() + i, j - i);
                if (word.size() > prefix.size() && begins(word) && seen.insert(std::string(word)).second)
                {
                    Completion c;
                    c.text = std::string(word);
                    out.push_back(std::move(c));
                }
            }
            i = j;
        }
    }
}

void ALCodeEditor::refreshCompletion()
{
    const std::string prefix = wordBeforeCaret();
    const ALTextPos   at     = caret();
    if (prefix.empty() || hasSelection())
    {
        closeCompletion();
        return;
    }
    mCompletions.clear();
    if (mProvider)
    {
        mProvider(at, prefix, mCompletions);
    }
    else
    {
        vocabularyCompletions(prefix, mCompletions);
    }
    documentCompletions(at, prefix, mCompletions);
    // What matches the case typed comes first; then the alphabet.
    std::stable_sort(mCompletions.begin(), mCompletions.end(), [&](const Completion& a, const Completion& b) {
        const bool a_exact = a.text.compare(0, prefix.size(), prefix) == 0;
        const bool b_exact = b.text.compare(0, prefix.size(), prefix) == 0;
        if (a_exact != b_exact)
        {
            return a_exact;
        }
        return a.text < b.text;
    });
    if (mCompletions.size() > COMPLETION_CAP)
    {
        mCompletions.resize(COMPLETION_CAP);
    }
    if (mCompletions.empty())
    {
        closeCompletion();
        return;
    }
    mCompletionRange = ALTextRange(ALTextPos(at.line, at.column - static_cast<S32>(prefix.size())), at);

    const S32 was = mCompletionList->getFirstSelectedIndex();
    mCompletionList->deleteAllItems();
    for (size_t i = 0; i < mCompletions.size(); ++i)
    {
        const Completion& c = mCompletions[i];
        LLSD              row;
        row["id"]                     = static_cast<S32>(i);
        row["columns"][0]["column"]   = "text";
        row["columns"][0]["value"]    = c.text;
        row["columns"][1]["column"]   = "detail";
        row["columns"][1]["value"]    = c.detail;
        mCompletionList->addElement(row);
    }
    mCompletionList->selectNthItem(llclamp(was, 0, static_cast<S32>(mCompletions.size()) - 1));
    placeCompletion();
    mCompletionList->setVisible(true);
}

void ALCodeEditor::placeCompletion()
{
    const LLRect text  = textRect();
    const LLRect local = getLocalRect();
    const S32    row_h = layout().rowHeight();
    S32          row;
    const F32    x      = layout().xOf(mCompletionRange.begin.line, mCompletionRange.begin.column, &row);
    const S32    top    = screenTopOf(text, mCompletionRange.begin.line, row);
    const S32    rows   = llmin(static_cast<S32>(mCompletions.size()), COMPLETION_ROWS);
    const S32    height = rows * (getFont()->getLineHeight() + 2) + 6;
    const S32    width  = llmin(COMPLETION_WIDTH, llmax(60, local.getWidth() - 8));
    S32          left   = static_cast<S32>(static_cast<F32>(text.mLeft) - scrollX() + x);
    left                = llclamp(left, local.mLeft, llmax(local.mLeft, local.mRight - width));
    LLRect rect;
    if (top - row_h - height >= local.mBottom || top + height > local.mTop)
    {
        // Under the row, or over it where under would run off the bottom.
        rect = LLRect(left, top - row_h, left + width, top - row_h - height);
    }
    else
    {
        rect = LLRect(left, top + height, left + width, top);
    }
    mCompletionList->setShape(rect);
}

void ALCodeEditor::openCompletion()
{
    refreshCompletion();
}

bool ALCodeEditor::complete()
{
    if (isReadOnly())
    {
        return false;
    }
    openCompletion();
    return true;
}

bool ALCodeEditor::acceptCompletion()
{
    if (!completionOpen())
    {
        return false;
    }
    const S32 index = mCompletionList->getFirstSelectedIndex();
    if (index < 0 || index >= static_cast<S32>(mCompletions.size()))
    {
        closeCompletion();
        return false;
    }
    const std::string chosen = mCompletions[index].text;
    const ALTextRange range  = mCompletionRange;
    closeCompletion();
    setSelection(range);
    insertText(chosen);
    setFocus(true);
    return true;
}

// --- input -------------------------------------------------------------------

bool ALCodeEditor::handleKeyHere(KEY key, MASK mask)
{
    if (completionOpen() && mask == MASK_NONE)
    {
        const S32 count = static_cast<S32>(mCompletions.size());
        const S32 at    = mCompletionList->getFirstSelectedIndex();
        switch (key)
        {
            case KEY_ESCAPE:
                closeCompletion();
                return true;
            case KEY_UP:
                mCompletionList->selectNthItem((at - 1 + count) % count);
                mCompletionList->scrollToShowSelected();
                return true;
            case KEY_DOWN:
                mCompletionList->selectNthItem((at + 1) % count);
                mCompletionList->scrollToShowSelected();
                return true;
            case KEY_PAGE_UP:
                mCompletionList->selectNthItem(llmax(0, at - COMPLETION_ROWS));
                mCompletionList->scrollToShowSelected();
                return true;
            case KEY_PAGE_DOWN:
                mCompletionList->selectNthItem(llmin(count - 1, at + COMPLETION_ROWS));
                mCompletionList->scrollToShowSelected();
                return true;
            case KEY_RETURN:
            case KEY_TAB:
                acceptCompletion();
                return true;
            case KEY_LEFT:
            case KEY_RIGHT:
            case KEY_HOME:
            case KEY_END:
                closeCompletion();
                break;
            default:
                break;
        }
    }
    else if (completionOpen() && key != KEY_BACKSPACE)
    {
        closeCompletion();
    }
    return ALTextView::handleKeyHere(key, mask);
}

bool ALCodeEditor::handleUnicodeCharHere(llwchar uni_char)
{
    const bool was_open = completionOpen();
    if (!ALTextView::handleUnicodeCharHere(uni_char))
    {
        return false;
    }
    const bool identifier = uni_char < 0x80 && identifierByte(static_cast<char>(uni_char));
    if (!identifier)
    {
        closeCompletion();
    }
    else if (!was_open && mAutoComplete && static_cast<S32>(wordBeforeCaret().size()) >= COMPLETION_AUTO_AT)
    {
        openCompletion();
    }
    return true;
}

bool ALCodeEditor::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (mCompletionList && mCompletionList->getVisible() && mCompletionList->getRect().pointInRect(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    closeCompletion();
    const LLRect text         = textRect();
    const S32    gutter_right = getLocalRect().mLeft + gutterWidth();
    if (mShowFoldMarkers && x < gutter_right && x >= gutter_right - FOLD_COLUMN)
    {
        const S32 line = posAtLocal(text.mLeft, y, false).line;
        if (regionStartingAt(line))
        {
            if (isFolded(line))
            {
                unfoldAt(line);
            }
            else
            {
                foldAt(line);
            }
            return true;
        }
    }
    if (!mFolded.empty() && text.pointInRect(x, y))
    {
        const S32 line = posAtLocal(x, y, false).line;
        if (isFolded(line) && foldBoxOf(line, text).pointInRect(x, y))
        {
            unfoldAt(line);
            return true;
        }
    }
    return ALTextView::handleMouseDown(x, y, mask);
}

void ALCodeEditor::onFocusLost()
{
    // The list is a child: choosing from it with the mouse takes the
    // keyboard for a moment, and that is not looking away.
    if (!(mCompletionList && mCompletionList->hasFocus()))
    {
        closeCompletion();
    }
    ALTextView::onFocusLost();
}
