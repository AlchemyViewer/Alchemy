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
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llrender2dutils.h"

#include <algorithm>
#include <tuple>

static LLDefaultChildRegistry::Register<ALDiffView> r("diff_view");

namespace
{
    // The gap between the sides, where the line between them is drawn.
    constexpr S32 GAP = 3;

    LLColor4 colorOf(const char* name, const LLColor4& otherwise) { return LLUIColorTable::instance().getColor(name, otherwise).get(); }

    // One side's text as it is shown: its lines, each's number (0 for a
    // line put in to line the sides up), its tint, the words marked, and
    // which lines are only there to line the sides up, which a copy leaves
    // out.
    struct Shown
    {
        std::string                            text;
        std::vector<S32>                       numbers;
        std::vector<LLColor4>                  tints;
        std::vector<ALCodeEditor::Decoration>  words;
        std::vector<bool>                      spacers;

        S32 add(const std::string& line, S32 number, const LLColor4& tint, bool spacer = false)
        {
            if (!numbers.empty())
            {
                text += '\n';
            }
            text += line;
            numbers.push_back(number);
            tints.push_back(tint);
            spacers.push_back(spacer);
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
    mBar->onSwap([this]() { setSwapped(!mSwapped); });
    mBar->onDone([this]() {
        if (mEscape)
        {
            mEscape();
        }
    });
    mBar->setInline(mInline);
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
    rebuild();
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
    rebuild();
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
    return place;
}

void ALDiffView::restorePlace(const Place& place)
{
    // The row that now shows the right's line, on the side in front.
    ALCodeEditor*           side = shown();
    const std::vector<S32>& rows = rightRowsOf(side);
    const auto              row  = std::find(rows.begin(), rows.end(), place.line + 1);
    if (row == rows.end())
    {
        return;
    }
    const S32 at     = static_cast<S32>(row - rows.begin());
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

void ALDiffView::rebuild()
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
    const LLColor4                      out_words = colorOf("CodeDiffRemovedWordColor", LLColor4(0.9f, 0.25f, 0.25f, 0.4f));
    const LLColor4                      in_words  = colorOf("CodeDiffAddedWordColor", LLColor4(0.25f, 0.85f, 0.35f, 0.4f));
    mChanges.clear();
    mChangeEnds.clear();
    mInlineLeftRows.clear();
    Shown ls;
    Shown rs;
    Shown is;
    for (size_t i = 0; i < runs.size();)
    {
        const ALTextDiff::Run& run = runs[i];
        if (run.kind == ALTextDiff::Kind::Same)
        {
            for (S32 n = 0; n < run.count; ++n)
            {
                const std::string& line = right[static_cast<size_t>(run.right + n)];
                ls.add(left[static_cast<size_t>(run.left + n)], run.left + n + 1, none);
                rs.add(line, run.right + n + 1, none);
                is.add(line, run.right + n + 1, none);
                mInlineLeftRows.push_back(run.left + n + 1);
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
            const S32       lrow = has_out ? ls.add(left[static_cast<size_t>(gone[n])], gone[n] + 1, out) : ls.add(std::string(), 0, padding, true);
            const S32       rrow = has_in ? rs.add(right[static_cast<size_t>(made[n])], made[n] + 1, in) : rs.add(std::string(), 0, padding, true);
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
            is.add(left[static_cast<size_t>(line)], 0, out);
            mInlineLeftRows.push_back(line + 1);
        }
        const S32 first_in = static_cast<S32>(is.numbers.size());
        for (const S32 line : made)
        {
            is.add(right[static_cast<size_t>(line)], line + 1, in);
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
    refreshBar();
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
    if (!mInline)
    {
        const S32 half = (getRect().getWidth() - GAP) / 2;
        gl_rect_2d(half + 1, mLeft->getRect().mTop, half + GAP - 1, 0, colorOf("CodeDiffDividerColor", LLColor4(0.5f, 0.5f, 0.5f, 0.5f)) % getDrawContext().mAlpha);
    }
}
