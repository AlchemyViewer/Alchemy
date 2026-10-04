/**
 * @file aldiffview.cpp
 * @brief Two texts compared, side by side or inline.
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

#include "aldiffview.h"

#include "alcodeeditor.h"
#include "aldiffbar.h"
#include "altextdiff.h"
#include "alsaid.h"
#include "lllocalcliprect.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llrender2dutils.h"
#include "llwindow.h"

#include <algorithm>
#include <tuple>

static LLDefaultChildRegistry::Register<ALDiffView> r("diff_view");

namespace
{
    // The gap between the sides, where the line between them is drawn.
    constexpr S32 GAP = 3;

    LLColor4 colorOf(const char* name, const LLColor4& otherwise) { return LLUIColorTable::instance().getColor(name, otherwise).get(); }

    // One side's text as it is shown: its lines, each's number (0 for a
    // line put in to line the sides up), its tint, the words marked, which
    // lines are only there to line the sides up, which a copy leaves out,
    // and the mark each has on the ruler down the side, for what changed
    // there on either side.
    struct Shown
    {
        std::string                            text;
        std::vector<S32>                       numbers;
        std::vector<LLColor4>                  tints;
        std::vector<ALCodeEditor::Decoration>  words;
        std::vector<bool>                      spacers;
        std::vector<LLColor4>                  marks;

        S32 add(const std::string& line, S32 number, const LLColor4& tint, bool spacer = false, const LLColor4& mark = LLColor4::transparent)
        {
            if (!numbers.empty())
            {
                text += '\n';
            }
            text += line;
            numbers.push_back(number);
            tints.push_back(tint);
            spacers.push_back(spacer);
            marks.push_back(mark);
            return static_cast<S32>(numbers.size()) - 1;
        }

        void mark(S32 row, const ALTextDiff::spans_t& spans, const LLColor4& color)
        {
            for (const auto& [begin, end] : spans)
            {
                ALCodeEditor::Decoration d;
                d.range = ALTextRange(ALTextPos(row, begin), ALTextPos(row, end));
                d.style = ALCodeEditor::Decoration::Style::Background;
                d.color = color;
                words.push_back(d);
            }
        }

        void into(ALCodeEditor& editor)
        {
            // The text first: putting it in clears what is about lines.
            editor.setText(text);
            editor.setLineNumbers(std::move(numbers));
            editor.setLineTints(std::move(tints));
            editor.setSpacerLines(std::move(spacers));
            editor.setDecorations(std::move(words));
            editor.setRulerTints(std::move(marks));
        }
    };
}

ALDiffView::Params::Params()
:   syntax("syntax"),
    inline_view("inline_view", false),
    side("side")
{
}

ALDiffView::ALDiffView(const Params& p)
:   LLUICtrl(p),
    mInline(p.inline_view)
{
    ALDiffBar::Params bp(LLUICtrlFactory::getDefaultParams<ALDiffBar>());
    bp.name          = "bar";
    bp.rect          = LLRect(0, ALDiffBar::wantedHeight(), 10, 0);
    bp.follows.flags = FOLLOWS_NONE;
    mBar             = LLUICtrlFactory::create<ALDiffBar>(bp);
    addChild(mBar);
    mBar->onPrevious([this]() { stepFromBar(false); });
    mBar->onNext([this]() { stepFromBar(true); });
    mBar->onInline([this]() {
        setInline(!mInline);
        if (mOnInline)
        {
            mOnInline(mInline);
        }
    });
    mBar->onFold([this]() { setFoldSame(!mFoldSame); });
    mBar->onSwap([this]() { setSwapped(!mSwapped); });
    mBar->onDone([this]() {
        if (mEscape)
        {
            mEscape();
        }
    });
    mBar->setInline(mInline);
    mBar->setFolded(mFoldSame);
    mBar->setDoneShown(false);

    LLTextBox::Params head(LLUICtrlFactory::getDefaultParams<LLTextBox>());
    head.rect(LLRect(0, 16, 10, 0));
    head.follows.flags(FOLLOWS_NONE);
    head.font(LLFontGL::getFontSansSerifSmall());
    head.use_ellipses(true);
    head.name("left_title");
    mLeftHead = LLUICtrlFactory::create<LLTextBox>(head);
    addChild(mLeftHead);
    head.name("right_title");
    mRightHead = LLUICtrlFactory::create<LLTextBox>(head);
    addChild(mRightHead);

    const ALCodeEditor::Params side = p.side.isProvided() ? p.side() : LLUICtrlFactory::getDefaultParams<ALCodeEditor>();
    mLeft    = makeSide(side, "left");
    mRight   = makeSide(side, "right");
    mInlined = makeSide(side, "inline");
    if (p.syntax.isProvided())
    {
        setSyntax(p.syntax());
    }
    arrange();
    rebuild();
}

ALDiffView::~ALDiffView() = default;

ALCodeEditor* ALDiffView::makeSide(const ALCodeEditor::Params& side, const std::string& name)
{
    ALCodeEditor::Params p(side);
    p.name                   = name;
    p.rect                   = LLRect(0, 10, 10, 0);
    p.follows.flags          = FOLLOWS_NONE;
    p.read_only              = true;
    // Folding one side would undo the lining up, and a line lit on one
    // side and not the other says nothing.
    p.show_fold_markers      = false;
    p.highlight_current_line = false;
    // Escape is the comparison's: back to what it was made from.
    p.pass_escape            = true;
    ALCodeEditor* made       = LLUICtrlFactory::create<ALCodeEditor>(p);
    addChild(made);
    made->onCaretMoved([this]() { refreshBar(); });
    // Vim's ]c and [c, where the host puts vim over the sides.
    made->setChangeStepper([this](bool forward) { return goToChange(forward); });
    // The caret landing on a line folded away opens its fold, both sides
    // at once, rather than the one line on the one side.
    made->setLineRevealer([this, made](S32 line) {
        const S32 fold = foldOfRow(made, line, true);
        if (fold < 0 || mFolds[static_cast<size_t>(fold)].open)
        {
            return false;
        }
        openFold(fold);
        return true;
    });
    return made;
}

void ALDiffView::setTexts(std::string_view left, std::string_view right, const ALTextDiff::anchors_t& anchors)
{
    mLeftText  = std::string(left);
    mRightText = std::string(right);
    mAnchors   = anchors;
    rebuild();
}

void ALDiffView::setTitles(const std::string& left, const std::string& right)
{
    mLeftTitle  = left;
    mRightTitle = right;
    arrange();
}

void ALDiffView::setSyntax(const std::string& syntax)
{
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        side->setSyntax(syntax);
    }
}

void ALDiffView::setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        side->setGrammar(grammar);
    }
}

void ALDiffView::setFont(const LLFontGL* font)
{
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        side->setFont(font);
    }
}

void ALDiffView::setInline(bool inline_view)
{
    if (mInline == inline_view)
    {
        return;
    }
    const bool  had_keys = hasFocus();
    const Place place    = placeOfCaret();
    mInline              = inline_view;
    mBar->setInline(mInline);
    arrange();
    rebuild(foldsOpen());
    restorePlace(place);
    if (had_keys)
    {
        shown()->setFocus(true);
    }
}

void ALDiffView::setSwapped(bool swapped)
{
    if (mSwapped == swapped)
    {
        return;
    }
    // The keyboard left on the side it was on, which now shows the other
    // text: the place kept is the right's line, wherever that now is.
    const Place place = placeOfCaret();
    mSwapped          = swapped;
    mBar->setSwapped(mSwapped);
    arrange();
    rebuild(foldsOpen());
    restorePlace(place);
}

void ALDiffView::setOnEscape(std::function<void()> escape)
{
    mEscape = std::move(escape);
    mBar->setDoneShown(mEscape != nullptr);
}

ALDiffView::Place ALDiffView::placeOfCaret()
{
    Place               place;
    ALCodeEditor*       side = shown();
    std::tie(place.line, place.column) = rightAtCaret();
    place.belowTop = side->layout().lineTop(side->caret().line) - side->scrollY();
    place.fold     = foldOfRow(side, side->caret().line, false);
    return place;
}

void ALDiffView::restorePlace(const Place& place)
{
    // The row that now shows the right's line, on the side in front; or
    // the folded row the caret was on, which shows none.
    ALCodeEditor*           side = shown();
    const std::vector<S32>& rows = rightRowsOf(side);
    const auto              row  = std::find(rows.begin(), rows.end(), place.line + 1);
    if (row == rows.end() && (place.fold < 0 || place.fold >= foldCount()))
    {
        return;
    }
    const S32 at     = place.fold >= 0 && place.fold < foldCount() ? rowOfFold(side, place.fold) : static_cast<S32>(row - rows.begin());
    const S32 scroll = llmax(0, side->layout().lineTop(at) - place.belowTop);
    // Each side at the row and scrolled alike: lined up, side by side.
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* each : sides)
    {
        if (each)
        {
            each->goTo(ALTextPos(at, each == notRightSide() ? 0 : place.column));
            each->setScrollY(scroll);
        }
    }
    mScrolledY = side->scrollY();
}

void ALDiffView::rebuild(const std::vector<bool>& open)
{
    // What is shown on the left and on the right: the texts as given, or
    // swapped, and the pairs that line them up with them.
    const std::vector<std::string> left  = ALTextDiff::split(mSwapped ? mRightText : mLeftText);
    const std::vector<std::string> right = ALTextDiff::split(mSwapped ? mLeftText : mRightText);
    ALTextDiff::anchors_t          anchors(mAnchors);
    if (mSwapped)
    {
        for (auto& [from, to] : anchors)
        {
            std::swap(from, to);
        }
    }
    const std::vector<ALTextDiff::Run> runs = anchors.empty() ? ALTextDiff::lines(left, right) : ALTextDiff::lines(left, right, anchors);
    const LLColor4                      none(0.f, 0.f, 0.f, 0.f);
    const LLColor4                      out       = colorOf("CodeDiffRemovedColor", LLColor4(0.85f, 0.25f, 0.25f, 0.18f));
    const LLColor4                      in        = colorOf("CodeDiffAddedColor", LLColor4(0.25f, 0.75f, 0.35f, 0.18f));
    const LLColor4                      padding   = colorOf("CodeDiffPaddingColor", LLColor4(0.5f, 0.5f, 0.5f, 0.07f));
    const LLColor4                      folded    = colorOf("CodeDiffFoldColor", LLColor4(0.5f, 0.5f, 0.5f, 0.14f));
    const LLColor4                      out_words = colorOf("CodeDiffRemovedWordColor", LLColor4(0.9f, 0.25f, 0.25f, 0.4f));
    const LLColor4                      in_words  = colorOf("CodeDiffAddedWordColor", LLColor4(0.25f, 0.85f, 0.35f, 0.4f));
    // The ruler's: each side's own change, and beside a gap, the other's.
    const LLColor4                      out_mark  = colorOf("CodeDiffRemovedMarkColor", LLColor4(0.9f, 0.3f, 0.3f, 0.85f));
    const LLColor4                      in_mark   = colorOf("CodeDiffAddedMarkColor", LLColor4(0.3f, 0.8f, 0.4f, 0.85f));
    mChanges.clear();
    mChangeEnds.clear();
    mInlineLeftRows.clear();
    std::vector<Fold> folds;
    // The last run that is a change: the runs the same after it are at
    // the text's end. Where none is, nothing is folded.
    size_t last_change = runs.size();
    for (size_t i = runs.size(); i-- > 0;)
    {
        if (runs[i].kind != ALTextDiff::Kind::Same)
        {
            last_change = i;
            break;
        }
    }
    Shown ls;
    Shown rs;
    Shown is;
    for (size_t i = 0; i < runs.size();)
    {
        const ALTextDiff::Run& run = runs[i];
        if (run.kind == ALTextDiff::Kind::Same)
        {
            const auto same = [&](S32 n) {
                const std::string& line = right[static_cast<size_t>(run.right + n)];
                ls.add(left[static_cast<size_t>(run.left + n)], run.left + n + 1, none);
                rs.add(line, run.right + n + 1, none);
                is.add(line, run.right + n + 1, none);
                mInlineLeftRows.push_back(run.left + n + 1);
            };
            // Context kept beside a change, none at either end; what is
            // left folded away behind a row of its own, where it is enough.
            const S32 before = ls.numbers.empty() ? 0 : FOLD_CONTEXT;
            const S32 after  = i > last_change ? 0 : FOLD_CONTEXT;
            const S32 hidden = last_change == runs.size() ? 0 : run.count - before - after;
            S32       n      = 0;
            if (hidden >= FOLD_LEAST)
            {
                for (; n < before; ++n)
                {
                    same(n);
                }
                Fold fold;
                fold.row       = ls.add(std::string(), 0, folded, true);
                fold.inlineRow = is.add(std::string(), 0, folded, true);
                rs.add(std::string(), 0, folded, true);
                mInlineLeftRows.push_back(0);
                fold.count = hidden;
                fold.open  = !mFoldSame;
                folds.push_back(fold);
            }
            for (; n < run.count; ++n)
            {
                same(n);
            }
            ++i;
            continue;
        }
        // A change: the lines taken out and put in between two the same,
        // side by side, the first taken out beside the first put in.
        std::vector<S32> gone;
        std::vector<S32> made;
        for (; i < runs.size() && runs[i].kind != ALTextDiff::Kind::Same; ++i)
        {
            for (S32 n = 0; n < runs[i].count; ++n)
            {
                (runs[i].kind == ALTextDiff::Kind::Removed ? gone : made).push_back((runs[i].kind == ALTextDiff::Kind::Removed ? runs[i].left : runs[i].right) + n);
            }
        }
        mChanges.push_back(mInline ? static_cast<S32>(is.numbers.size()) : static_cast<S32>(ls.numbers.size()));
        const size_t rows = std::max(gone.size(), made.size());
        std::vector<S32> gone_rows;
        for (size_t n = 0; n < rows; ++n)
        {
            const bool      has_out = n < gone.size();
            const bool      has_in  = n < made.size();
            const S32       lrow = has_out ? ls.add(left[static_cast<size_t>(gone[n])], gone[n] + 1, out, false, out_mark) : ls.add(std::string(), 0, padding, true, in_mark);
            const S32       rrow = has_in ? rs.add(right[static_cast<size_t>(made[n])], made[n] + 1, in, false, in_mark) : rs.add(std::string(), 0, padding, true, out_mark);
            if (has_out && has_in)
            {
                ALTextDiff::spans_t lspans;
                ALTextDiff::spans_t rspans;
                ALTextDiff::words(left[static_cast<size_t>(gone[n])], right[static_cast<size_t>(made[n])], lspans, rspans);
                ls.mark(lrow, lspans, out_words);
                rs.mark(rrow, rspans, in_words);
            }
        }
        // Inline: what was taken out above what was put in.
        const S32 first_out = static_cast<S32>(is.numbers.size());
        for (const S32 line : gone)
        {
            is.add(left[static_cast<size_t>(line)], 0, out, false, out_mark);
            mInlineLeftRows.push_back(line + 1);
        }
        const S32 first_in = static_cast<S32>(is.numbers.size());
        for (const S32 line : made)
        {
            is.add(right[static_cast<size_t>(line)], line + 1, in, false, in_mark);
            mInlineLeftRows.push_back(0);
        }
        for (size_t n = 0; n < gone.size() && n < made.size(); ++n)
        {
            ALTextDiff::spans_t lspans;
            ALTextDiff::spans_t rspans;
            ALTextDiff::words(left[static_cast<size_t>(gone[n])], right[static_cast<size_t>(made[n])], lspans, rspans);
            is.mark(first_out + static_cast<S32>(n), lspans, out_words);
            is.mark(first_in + static_cast<S32>(n), rspans, in_words);
        }
        mChangeEnds.push_back(mInline ? static_cast<S32>(is.numbers.size()) : static_cast<S32>(ls.numbers.size()));
    }
    mLeftRows   = ls.numbers;
    mRightRows  = rs.numbers;
    mInlineRows = is.numbers;
    ls.into(*mLeft);
    rs.into(*mRight);
    is.into(*mInlined);
    mScrolledY = 0;
    mScrolledX = 0.f;
    if (open.size() == folds.size())
    {
        for (size_t n = 0; n < folds.size(); ++n)
        {
            folds[n].open = open[n];
        }
    }
    mFolds = std::move(folds);
    applyFolds();
    refreshBar();
}

// --- folds -----------------------------------------------------------------------------

void ALDiffView::setFoldSame(bool fold)
{
    mFoldSame = fold;
    mBar->setFolded(fold);
    if (mFolds.empty())
    {
        return;
    }
    // The caret's row kept as far down the view as it was; where its line
    // is folded away, the row that stands for it.
    ALCodeEditor* side      = shown();
    const S32     below_top = side->layout().lineTop(side->caret().line) - side->scrollY();
    for (Fold& each : mFolds)
    {
        each.open = !fold;
    }
    applyFolds();
    const S32 scroll = llmax(0, side->layout().lineTop(side->caret().line) - below_top);
    for (ALCodeEditor* each : { mLeft, mRight, mInlined })
    {
        each->setScrollY(scroll);
    }
    mScrolledY = mRight->scrollY();
}

S32 ALDiffView::foldedCount() const
{
    return static_cast<S32>(std::count_if(mFolds.begin(), mFolds.end(), [](const Fold& fold) { return !fold.open; }));
}

std::vector<bool> ALDiffView::foldsOpen() const
{
    std::vector<bool> open;
    open.reserve(mFolds.size());
    for (const Fold& fold : mFolds)
    {
        open.push_back(fold.open);
    }
    return open;
}

void ALDiffView::applyFolds()
{
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        ALTextLayout& layout = side->layout();
        for (size_t n = 0; n < mFolds.size(); ++n)
        {
            const Fold& fold = mFolds[n];
            const S32   row  = rowOfFold(side, static_cast<S32>(n));
            layout.setHidden(row, row, fold.open);
            layout.setHidden(row + 1, row + fold.count, !fold.open);
        }
        // A caret left on a line now hidden: on the row standing for its
        // run where that is folded, else on the run's first line.
        const S32 at   = side->caret().line;
        const S32 fold = foldOfRow(side, at, true);
        if (fold >= 0 && layout.hidden(at))
        {
            const S32 row = rowOfFold(side, fold);
            side->goTo(ALTextPos(mFolds[static_cast<size_t>(fold)].open ? row + 1 : row, 0));
        }
    }
}

S32 ALDiffView::rowOfFold(const ALCodeEditor* side, S32 fold) const
{
    const Fold& each = mFolds[static_cast<size_t>(fold)];
    return side == mInlined ? each.inlineRow : each.row;
}

S32 ALDiffView::foldOfRow(const ALCodeEditor* side, S32 row, bool lines) const
{
    // The last fold whose row is at or before it, in order of their rows.
    const bool inlined = side == mInlined;
    const auto after   = std::upper_bound(mFolds.begin(), mFolds.end(), row, [inlined](S32 at, const Fold& fold) {
        return at < (inlined ? fold.inlineRow : fold.row);
    });
    if (after == mFolds.begin())
    {
        return -1;
    }
    const S32 fold  = static_cast<S32>(after - mFolds.begin()) - 1;
    const S32 start = rowOfFold(side, fold);
    return row == start || (lines && row <= start + mFolds[static_cast<size_t>(fold)].count) ? fold : -1;
}

void ALDiffView::openFold(S32 fold)
{
    mFolds[static_cast<size_t>(fold)].open = true;
    applyFolds();
}

S32 ALDiffView::foldAtPoint(S32 x, S32 y, ALCodeEditor** under)
{
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* side : sides)
    {
        const LLRect frame = side ? side->getRect() : LLRect();
        if (!side || !frame.pointInRect(x, y))
        {
            continue;
        }
        const S32    local_x = x - frame.mLeft;
        const S32    local_y = y - frame.mBottom;
        const LLRect text    = side->textRect();
        if (local_y > text.mTop || local_y < text.mBottom)
        {
            return -1;
        }
        const S32 fold = foldOfRow(side, side->posAtLocal(llmax(local_x, text.mLeft), local_y, false).line, false);
        if (fold < 0 || mFolds[static_cast<size_t>(fold)].open)
        {
            return -1;
        }
        if (under)
        {
            *under = side;
        }
        return fold;
    }
    return -1;
}

bool ALDiffView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // A folded row opened, the caret put on the first line it hid, on the
    // side pressed, which takes the keyboard.
    ALCodeEditor* side = nullptr;
    const S32     fold = foldAtPoint(x, y, &side);
    if (fold < 0)
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    const S32 row = rowOfFold(side, fold);
    openFold(fold);
    side->goTo(ALTextPos(row + 1, 0));
    side->setFocus(true);
    return true;
}

bool ALDiffView::handleHover(S32 x, S32 y, MASK mask)
{
    if (foldAtPoint(x, y) >= 0)
    {
        if (LLWindow* window = getWindow())
        {
            window->setCursor(UI_CURSOR_HAND);
        }
        return true;
    }
    return LLUICtrl::handleHover(x, y, mask);
}

void ALDiffView::drawFoldRows()
{
    // Over each folded row, how many lines it stands for: the row is an
    // empty line, which the side draws tinted, and this is said over it.
    const F32           alpha       = getDrawContext().mAlpha;
    const LLFontGL*     font        = LLFontGL::getFontSansSerifSmall();
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* side : sides)
    {
        if (!side)
        {
            continue;
        }
        const LLRect   frame = side->getRect();
        const LLRect   text  = side->textRect();
        const S32      top   = frame.mBottom + text.mTop;
        const S32      left  = frame.mLeft + text.mLeft;
        LLLocalClipRect clip(LLRect(left, top, frame.mLeft + text.mRight, frame.mBottom + text.mBottom));
        const LLColor4 ink   = side->textColor() % (0.6f * alpha);
        ALTextLayout&  layout = side->layout();
        for (size_t n = 0; n < mFolds.size(); ++n)
        {
            const Fold& fold = mFolds[n];
            if (fold.open)
            {
                continue;
            }
            const S32 row    = rowOfFold(side, static_cast<S32>(n));
            const S32 row_t  = top - (layout.lineTop(row) - side->scrollY());
            const S32 height = layout.lineHeight(row);
            if (row_t - height > top || row_t < frame.mBottom + text.mBottom)
            {
                continue;
            }
            const std::string said = alSaidCount("DiffFoldedLines", fold.count, "\xE2\x8B\xAF 1 line the same \xE2\x8B\xAF",
                                                 "\xE2\x8B\xAF [COUNT] lines the same \xE2\x8B\xAF");
            font->renderUTF8(said, 0, left + 8, row_t - height / 2, ink, LLFontGL::LEFT, LLFontGL::VCENTER);
        }
    }
}

void ALDiffView::arrange()
{
    const S32    width  = getRect().getWidth();
    const S32    bar_h  = ALDiffBar::wantedHeight();
    const S32    height = llmax(0, getRect().getHeight() - bar_h);
    const S32    head_h = LLFontGL::getFontSansSerifSmall()->getLineHeight() + 6;
    const S32    body   = llmax(0, height - head_h);
    const S32    half   = (width - GAP) / 2;
    mBar->setShape(LLRect(0, height + bar_h, width, height));
    mLeft->setVisible(!mInline);
    mRight->setVisible(!mInline);
    mInlined->setVisible(mInline);
    mRightHead->setVisible(!mInline);
    if (mInline)
    {
        mInlined->setShape(LLRect(0, body, width, 0));
        mLeftHead->setShape(LLRect(4, height - 3, width - 4, body));
        const std::string& from = mSwapped ? mRightTitle : mLeftTitle;
        const std::string& to   = mSwapped ? mLeftTitle : mRightTitle;
        mLeftHead->setText(from.empty() && to.empty() ? std::string() : from + "  \xE2\x86\x92  " + to);
        return;
    }
    mLeft->setShape(LLRect(0, body, half, 0));
    mRight->setShape(LLRect(half + GAP, body, width, 0));
    mLeftHead->setShape(LLRect(4, height - 3, half - 4, body));
    mRightHead->setShape(LLRect(half + GAP + 4, height - 3, width - 4, body));
    mLeftHead->setText(mSwapped ? mRightTitle : mLeftTitle);
    mRightHead->setText(mSwapped ? mLeftTitle : mRightTitle);
}

void ALDiffView::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    if (mLeft)
    {
        arrange();
    }
}

ALCodeEditor* ALDiffView::shown() const
{
    if (mInline)
    {
        return mInlined;
    }
    return mLeft->hasFocus() ? mLeft : mRight;
}

S32 ALDiffView::changeAtCaret() const
{
    const S32  at   = shown()->caret().line;
    const auto next = std::upper_bound(mChanges.begin(), mChanges.end(), at);
    if (next == mChanges.begin())
    {
        return -1;
    }
    const size_t change = static_cast<size_t>(next - mChanges.begin()) - 1;
    return at < mChangeEnds[change] ? static_cast<S32>(change) : -1;
}

void ALDiffView::stepFromBar(bool forward)
{
    ALCodeEditor* side = shown();
    goToChange(forward);
    side->setFocus(true);
}

bool ALDiffView::goToChange(bool forward)
{
    ALCodeEditor* side = shown();
    const S32     at   = side->caret().line;
    S32           to   = -1;
    if (forward)
    {
        const auto next = std::upper_bound(mChanges.begin(), mChanges.end(), at);
        to              = next == mChanges.end() ? -1 : *next;
    }
    else
    {
        const auto next = std::lower_bound(mChanges.begin(), mChanges.end(), at);
        to              = next == mChanges.begin() ? -1 : *(next - 1);
    }
    if (to < 0)
    {
        return false;
    }
    // Both sides, which are lined up, at the change: the one in front
    // scrolled to it, and the other following it there as it is drawn.
    // (An array of their own: a list picked by ?: would be a temporary
    // gone before the loop reads it.)
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* each : sides)
    {
        if (each)
        {
            each->goTo(ALTextPos(to, 0));
        }
    }
    side->scrollToCaret();
    return true;
}

const std::vector<S32>& ALDiffView::rightRowsOf(const ALCodeEditor* side) const
{
    // The right's text is shown on the right, or on the left once swapped;
    // and inline, numbered as the side shown on the right, or else by the
    // lines taken out.
    if (side == mInlined)
    {
        return mSwapped ? mInlineLeftRows : mInlineRows;
    }
    return mSwapped ? mLeftRows : mRightRows;
}

const ALCodeEditor* ALDiffView::notRightSide() const
{
    return mInline ? nullptr : mSwapped ? mRight : mLeft;
}

std::pair<S32, S32> ALDiffView::rightAtCaret() const
{
    const ALCodeEditor*     side = shown();
    const std::vector<S32>& rows = rightRowsOf(side);
    const S32               row  = side->caret().line;
    if (rows.empty())
    {
        return { 0, 0 };
    }
    const S32 at = llclamp(row, 0, static_cast<S32>(rows.size()) - 1);
    if (rows[static_cast<size_t>(at)] > 0)
    {
        // The column where the caret stands in the right's own text: the
        // right side's, or a line inline that the right has.
        return { rows[static_cast<size_t>(at)] - 1, side == notRightSide() ? 0 : side->caret().column };
    }
    for (S32 n = at + 1; n < static_cast<S32>(rows.size()); ++n)
    {
        if (rows[static_cast<size_t>(n)] > 0)
        {
            return { rows[static_cast<size_t>(n)] - 1, 0 };
        }
    }
    for (S32 n = at - 1; n >= 0; --n)
    {
        if (rows[static_cast<size_t>(n)] > 0)
        {
            return { rows[static_cast<size_t>(n)] - 1, 0 };
        }
    }
    return { 0, 0 };
}

bool ALDiffView::handleUnicodeCharHere(llwchar uni_char)
{
    // A character the side in front would not take, being read only.
    if (mEdit && uni_char >= 0x20 && uni_char != 0x7F)
    {
        const auto [line, column] = rightAtCaret();
        if (LLView* to = mEdit(line, column))
        {
            return to->handleUnicodeChar(uni_char, true);
        }
    }
    return LLUICtrl::handleUnicodeCharHere(uni_char);
}

bool ALDiffView::handleKeyHere(KEY key, MASK mask)
{
    // Return on a folded row opens it, rather than going to the source.
    if (key == KEY_RETURN && mask == MASK_NONE)
    {
        ALCodeEditor* side = shown();
        const S32     fold = foldOfRow(side, side->caret().line, false);
        if (fold >= 0 && !mFolds[static_cast<size_t>(fold)].open)
        {
            openFold(fold);
            return true;
        }
    }
    // A key the side in front would not take, being read only, that
    // changes the text: a line broken or joined, a tab, a paste or a cut.
    const bool edits = ((key == KEY_RETURN || key == KEY_BACKSPACE || key == KEY_DELETE || key == KEY_TAB) && (mask == MASK_NONE || mask == MASK_SHIFT)) ||
                       ((key == 'V' || key == 'X') && mask == MASK_CONTROL);
    if (mEdit && edits)
    {
        const auto [line, column] = rightAtCaret();
        if (LLView* to = mEdit(line, column))
        {
            return to->handleKey(key, mask, true);
        }
    }
    if (key == KEY_F7 && (mask == MASK_NONE || mask == MASK_SHIFT))
    {
        goToChange(mask == MASK_NONE);
        return true;
    }
    // As the modern editors' comparisons have it; a side, which cannot be
    // changed, lets the lines it would move go.
    if ((key == KEY_DOWN || key == KEY_UP) && mask == MASK_ALT)
    {
        goToChange(key == KEY_DOWN);
        return true;
    }
    if (key == KEY_ESCAPE && mask == MASK_NONE)
    {
        // Kept either way: passed on, a panel would take the keyboard out
        // into the world.
        if (mEscape)
        {
            mEscape();
        }
        return true;
    }
    return LLUICtrl::handleKeyHere(key, mask);
}

void ALDiffView::refreshBar()
{
    // The change the caret is in, and the steps there are from it.
    mBar->setCount(changeAtCaret(), changeCount());
    const S32 at = shown()->caret().line;
    mBar->setSteps(!mChanges.empty() && mChanges.front() < at, !mChanges.empty() && mChanges.back() > at);
}

void ALDiffView::draw()
{
    // The bar as the side in front has it, which the keyboard may have
    // moved to; in the sides' colours as they are now.
    refreshBar();
    if (mBarColors != LLUIColorTable::instance().generation())
    {
        mBarColors = LLUIColorTable::instance().generation();
        mBar->setColors(mRight->backgroundColor(), mRight->textColor());
    }
    // The sides scrolled together: whichever moved since the last frame,
    // the other follows.
    if (!mInline)
    {
        const S32 ly = mLeft->scrollY();
        const S32 ry = mRight->scrollY();
        if (ly != mScrolledY)
        {
            mRight->setScrollY(ly);
        }
        else if (ry != mScrolledY)
        {
            mLeft->setScrollY(ry);
        }
        mScrolledY = mRight->scrollY();
        const F32 lx = mLeft->scrollX();
        const F32 rx = mRight->scrollX();
        if (lx != mScrolledX)
        {
            mRight->setScrollX(lx);
        }
        else if (rx != mScrolledX)
        {
            mLeft->setScrollX(rx);
        }
        mScrolledX = mRight->scrollX();
    }
    LLUICtrl::draw();
    drawFoldRows();
    if (!mInline)
    {
        const S32 half = (getRect().getWidth() - GAP) / 2;
        gl_rect_2d(half + 1, mLeft->getRect().mTop, half + GAP - 1, 0, colorOf("CodeDiffDividerColor", LLColor4(0.5f, 0.5f, 0.5f, 0.5f)) % getDrawContext().mAlpha);
    }
}
