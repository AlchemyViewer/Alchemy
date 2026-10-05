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

#include "aldifflexer.h"

#include "alcodeeditor.h"
#include "aldiffbar.h"
#include "aldiffmodel.h"
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
    // The gap between the sides, where the line between them is drawn; and
    // as wide as it is where it holds the arrows that take a change back.
    constexpr S32 GAP         = 3;
    constexpr S32 ARROW_GAP   = 18;
    constexpr S32 BRACKET_GAP = 24;

    LLColor4 colorOf(const char* name, const LLColor4& otherwise) { return LLUIColorTable::instance().getColor(name, otherwise).get(); }
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
    mBar->onFold([this]() { setFoldSame(!mModel.foldsSame()); });
    mBar->setIgnores({ [this](const std::string& what) { return ignores(what); }, [this](const std::string& what) { return offersIgnore(what); },
                       [this](const std::string& what) { setIgnore(what, !ignores(what)); } });
    mBar->onSwap([this]() { setSwapped(!mModel.swapped()); });
    mBar->onTakeBack([this]() {
        ALCodeEditor* side = shown();
        takeBack(changeAtCaret());
        side->setFocus(true);
    });
    mBar->onSettle([this](ALTextMerge::Take take) {
        ALCodeEditor* side = shown();
        settle(changeAtCaret(), take);
        side->setFocus(true);
    });
    mBar->onDone([this]() {
        if (mEscape)
        {
            mEscape();
        }
    });
    mBar->setInline(mInline);
    mBar->setFolded(mModel.foldsSame());
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
    fill();
}

ALDiffView::~ALDiffView() = default;

ALCodeEditor* ALDiffView::makeSide(const ALCodeEditor::Params& side, const std::string& name)
{
    ALCodeEditor::Params p(side);
    p.name                   = name;
    p.rect                   = LLRect(0, 10, 10, 0);
    p.follows.flags          = FOLLOWS_NONE;
    p.read_only              = true;
    // Folding one side would undo the lining up, and so would a line
    // wrapped on one side and not the other; a line lit on one side and
    // not the other says nothing. Whatever the host's editors do.
    p.show_fold_markers      = false;
    p.word_wrap              = false;
    p.highlight_current_line = false;
    // Escape is the comparison's: back to what it was made from.
    p.pass_escape            = true;
    ALCodeEditor* made       = LLUICtrlFactory::create<ALCodeEditor>(p);
    addChild(made);
    made->setFoldable(false);
    mConnections.emplace_back(made->onCaretMoved([this]() { refreshBar(); }));
    // The bar is of the side the keyboard is in.
    mConnections.emplace_back(made->setFocusChangedCallback([this](LLFocusableElement*) { refreshBar(); }));
    // The sides scrolled together: whichever moves, the other follows.
    mConnections.emplace_back(made->onScrolled([this, made]() { followScroll(made); }));
    // Vim's ]c and [c, where the host puts vim over the sides.
    made->setChangeStepper([this](bool forward) { return goToChange(forward); });
    // The caret landing on a line folded away opens its fold, both sides
    // at once, rather than the one line on the one side.
    made->setLineRevealer([this, made](S32 line) {
        if (const S32 fold = mModel.foldOfRow(layoutOf(made), mModel.rowOfLine(columnOf(made), line), true); fold >= 0 && !mModel.foldOpen(fold))
        {
            openFold(fold);
        }
    });
    return made;
}


void ALDiffView::setTexts(std::string_view left, std::string_view right, const ALTextDiff::ranges_t& ranges)
{
    mModel.setTexts(left, right, ranges);
    mBar->setMerging(false);
    fill();
}

void ALDiffView::setMergeBase(std::optional<std::string_view> base)
{
    mModel.setMergeBase(base);
    mBar->setMerging(mModel.merging());
    refreshBar();
}

void ALDiffView::setRightText(std::string_view right)
{
    if (right == mModel.rightText())
    {
        return;
    }
    // The caret on its line of the right wherever that went: at its column
    // where the line is as it was, else at its start.
    Place                      place = placeOfCaret();
    const ALDiffModel::LineMap moved = mModel.setRightText(right);
    if (!moved.kept(place.line))
    {
        place.column = 0;
    }
    place.line = moved.line(place.line);
    fill();
    restorePlace(place);
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
    compareBy(mLeft->highlighter().grammar());
}

void ALDiffView::setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        side->setGrammar(grammar);
    }
    compareBy(grammar);
}

void ALDiffView::compareBy(const std::shared_ptr<const ALSyntaxGrammar>& grammar)
{
    // Prose, or none: words by their bytes.
    const std::shared_ptr<const ALSyntaxGrammar> code = grammar && !grammar->prose() ? grammar : nullptr;
    if (code == mLexedBy)
    {
        return;
    }
    mLexedBy = code;
    if (!code && mModel.likeness().ignoreComments)
    {
        ALTextDiff::Likeness like = mModel.likeness();
        like.ignoreComments       = false;
        mModel.setLikeness(like);
        mBar->setIgnoring(like.any());
    }
    if (mModel.leftText().empty() && mModel.rightText().empty())
    {
        mModel.setLexer(code ? ALDiffLexer::lexerOf(std::make_shared<ALDiffLexer>(code)) : ALTextDiff::lexer_t());
        return;
    }
    const Place place = placeOfCaret();
    mModel.setLexer(code ? ALDiffLexer::lexerOf(std::make_shared<ALDiffLexer>(code)) : ALTextDiff::lexer_t());
    fill();
    restorePlace(place);
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
    // Both ways are laid out already: the other shown, at the same place.
    const bool  had_keys = hasFocus();
    const Place place    = placeOfCaret();
    mInline              = inline_view;
    mBar->setInline(mInline);
    arrange();
    refreshBar();
    restorePlace(place);
    if (had_keys)
    {
        shown()->setFocus(true);
    }
}

void ALDiffView::setSwapped(bool swapped)
{
    if (mModel.swapped() == swapped)
    {
        return;
    }
    // The keyboard left on the side it was on, which now shows the other
    // text: the place kept is the right's line, wherever that now is.
    const Place place = placeOfCaret();
    mModel.setSwapped(swapped);
    mBar->setSwapped(swapped);
    arrange();
    fill();
    restorePlace(place);
}

void ALDiffView::setIgnoreWhitespace(bool ignore)
{
    if (ignoresWhitespace() != ignore)
    {
        ALTextDiff::Likeness like = mModel.likeness();
        like.ignoreWhitespace     = ignore;
        setLikeness(like);
    }
}

void ALDiffView::setIgnoreCase(bool ignore)
{
    if (ignoresCase() != ignore)
    {
        ALTextDiff::Likeness like = mModel.likeness();
        like.ignoreCase           = ignore;
        setLikeness(like);
    }
}

void ALDiffView::setOffersIgnoreCase(bool offers)
{
    mOffersCase = offers;
    if (!offers)
    {
        setIgnoreCase(false);
    }
}

bool ALDiffView::ignores(const std::string& what) const
{
    const ALTextDiff::Likeness& like = mModel.likeness();
    return what == "whitespace"    ? like.ignoreWhitespace
           : what == "trailing"    ? like.ignoreTrailing
           : what == "blank_lines" ? like.ignoreBlankLines
           : what == "comments"    ? like.ignoreComments
           : what == "case"        ? like.ignoreCase
                                   : false;
}

bool ALDiffView::offersIgnore(const std::string& what) const
{
    // Comments where a grammar says where they are; case where the host
    // offers it; the rest always.
    return what == "comments" ? mLexedBy != nullptr : what == "case" ? mOffersCase : true;
}

void ALDiffView::setIgnore(const std::string& what, bool ignore)
{
    if (!offersIgnore(what) && ignore)
    {
        return;
    }
    ALTextDiff::Likeness like = mModel.likeness();
    bool* flag = what == "whitespace"    ? &like.ignoreWhitespace
                 : what == "trailing"    ? &like.ignoreTrailing
                 : what == "blank_lines" ? &like.ignoreBlankLines
                 : what == "comments"    ? &like.ignoreComments
                 : what == "case"        ? &like.ignoreCase
                                         : nullptr;
    if (flag && *flag != ignore)
    {
        *flag = ignore;
        setLikeness(like);
    }
}

void ALDiffView::setSame(ALTextDiff::same_t same)
{
    const Place place = placeOfCaret();
    mModel.setSame(std::move(same));
    fill();
    restorePlace(place);
}

void ALDiffView::setAlgorithm(ALTextDiff::Algorithm algorithm)
{
    if (algorithm == mModel.options().algorithm)
    {
        return;
    }
    // The runs folded are others now: folded or not as asked.
    const Place place = placeOfCaret();
    mModel.setAlgorithm(algorithm);
    fill();
    restorePlace(place);
}

void ALDiffView::setLikeness(const ALTextDiff::Likeness& like)
{
    mBar->setIgnoring(like.any());
    // The runs folded are others now: folded or not as asked.
    const Place place = placeOfCaret();
    mModel.setLikeness(like);
    fill();
    restorePlace(place);
}

void ALDiffView::setOnEscape(std::function<void()> escape)
{
    mEscape = std::move(escape);
    mBar->setDoneShown(mEscape != nullptr);
}

ALDiffView::Column ALDiffView::columnOf(const ALCodeEditor* side) const
{
    return side == mInlined ? Column::Inline : side == mLeft ? Column::Left : Column::Right;
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
    // The row that now shows the right's line; each side's caret there,
    // or under the gap a side has there, at the column where it shows the
    // right's own text, and scrolled alike, lined up, the line in front as
    // far down the view as it was.
    ALCodeEditor* side = shown();
    const S32     at   = mModel.rowOfRightLine(layoutOf(side), place.line);
    if (at < 0)
    {
        return;
    }
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* each : sides)
    {
        if (each)
        {
            const Column column = columnOf(each);
            const bool   own    = column == Column::Inline || column == mModel.rightColumn();
            each->goTo(ALTextPos(mModel.caretLineOfRow(column, at), own ? place.column : 0));
        }
    }
    side->setScrollY(llmax(0, side->layout().lineTop(side->caret().line) - place.belowTop));
}

void ALDiffView::fill()
{
    const LLColor4 out       = colorOf("CodeDiffRemovedColor", LLColor4(0.85f, 0.25f, 0.25f, 0.18f));
    const LLColor4 in        = colorOf("CodeDiffAddedColor", LLColor4(0.25f, 0.75f, 0.35f, 0.18f));
    const LLColor4 padding   = colorOf("CodeDiffPaddingColor", LLColor4(0.5f, 0.5f, 0.5f, 0.07f));
    const LLColor4 out_words = colorOf("CodeDiffRemovedWordColor", LLColor4(0.9f, 0.25f, 0.25f, 0.4f));
    const LLColor4 in_words  = colorOf("CodeDiffAddedWordColor", LLColor4(0.25f, 0.85f, 0.35f, 0.4f));
    // The ruler's: each side's own change, and beside a gap, the other's.
    const LLColor4 out_mark  = colorOf("CodeDiffRemovedMarkColor", LLColor4(0.9f, 0.3f, 0.3f, 0.85f));
    const LLColor4 in_mark   = colorOf("CodeDiffAddedMarkColor", LLColor4(0.3f, 0.8f, 0.4f, 0.85f));
    // A block moved, at either end: neither red nor green.
    const LLColor4 moved      = colorOf("CodeDiffMovedColor", LLColor4(0.45f, 0.45f, 0.95f, 0.18f));
    const LLColor4 moved_mark = colorOf("CodeDiffMovedMarkColor", LLColor4(0.5f, 0.5f, 1.f, 0.85f));
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        // What the editor is told of each line of the column, and of the
        // rows below its last: its number, where the text is not its own;
        // its tint, its mark on the ruler and its sign, as it was taken
        // out or put in; the rows of nothing above it, beside lines the
        // other side has, marked as theirs; and the words that changed.
        const Column                            column = columnOf(side);
        const S32                               count  = mModel.lineCount(column);
        const LLColor4&                         beside = column == Column::Left ? in_mark : out_mark;
        std::vector<ALTextView::LineAnnotation> said(static_cast<size_t>(count) + 1);
        std::vector<ALCodeEditor::Decoration>   words;
        for (S32 l = 0; l < count; ++l)
        {
            const ALDiffModel::Line&    line = mModel.line(column, l);
            ALTextView::LineAnnotation& each = said[static_cast<size_t>(l)];
            const bool                  gone = line.kind == ALDiffModel::Kind::Removed;
            const bool                  made = line.kind == ALDiffModel::Kind::Added;
            if (column == Column::Inline)
            {
                each.number = line.number;
            }
            each.sign      = line.sign;
            each.tint      = line.move >= 0 ? moved : gone ? out : made ? in : LLColor4::transparent;
            each.rulerTint = line.move >= 0 ? moved_mark : gone ? out_mark : made ? in_mark : LLColor4::transparent;
            each.gap       = line.padding;
            if (line.padding > 0)
            {
                each.gapTint      = padding;
                each.gapRulerTint = beside;
            }
            for (const auto& [begin, end] : line.words)
            {
                ALCodeEditor::Decoration d;
                d.range = ALTextRange(ALTextPos(l, begin), ALTextPos(l, end));
                d.style = ALCodeEditor::Decoration::Style::Background;
                d.color = gone ? out_words : in_words;
                words.push_back(d);
            }
        }
        ALTextView::LineAnnotation& below = said.back();
        below.gap                         = mModel.endPadding(column);
        if (below.gap > 0)
        {
            below.gapTint      = padding;
            below.gapRulerTint = beside;
        }
        // The text first: a new text clears what is said of its lines.
        side->setText(mModel.text(column));
        side->setLineAnnotations(std::move(said));
        side->setDecorations(std::move(words));
        applyNotes(side);
    }
    // The bands, and the gap as wide as they need, or not.
    const S32 was = gap();
    mBands.clear();
    for (S32 n = 0; n < static_cast<S32>(mModel.ranges().size()); ++n)
    {
        const auto [lf, le] = mModel.rangeRows(n, Column::Left);
        const auto [rf, re] = mModel.rangeRows(n, Column::Right);
        if (mModel.rangeBracketed(n) && !(lf == rf && le == lf + 1 && re == rf + 1))
        {
            mBands.push_back(n);
        }
    }
    if (gap() != was)
    {
        arrange();
    }
    mFoldSaid.clear();
    for (S32 n = 0; n < foldCount(); ++n)
    {
        mFoldSaid.push_back(
            alSaidCount("DiffFoldedLines", mModel.foldLines(n), "\xE2\x8B\xAF 1 line the same \xE2\x8B\xAF", "\xE2\x8B\xAF [COUNT] lines the same \xE2\x8B\xAF"));
    }
    applyFolds();
    refreshBar();
}

void ALDiffView::setNotes(std::vector<ALDiffModel::Note> notes)
{
    mModel.setNotes(std::move(notes));
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        applyNotes(side);
    }
}

void ALDiffView::applyNotes(ALCodeEditor* side)
{
    std::vector<ALCodeEditor::LineNote> said;
    for (const ALDiffModel::Note& note : mModel.notesIn(columnOf(side)))
    {
        said.push_back(ALCodeEditor::LineNote{ note.line, note.text, note.tip });
    }
    side->setLineNotes(said);
}

void ALDiffView::refreshLinked()
{
    // The range the caret of the side in front is in, side by side.
    const ALCodeEditor* side = shown();
    mLinked                  = mInline ? -1 : mModel.rangeAt(columnOf(side), side->caret().line);
}

void ALDiffView::drawLinked()
{
    // Each side's rows of it washed, an edge down their left: what the
    // line the caret is on became, or was made from, and where it stands.
    if (mLinked < 0 || mInline)
    {
        return;
    }
    const F32 alpha = getDrawContext().mAlpha;
    for (ALCodeEditor* side : { mLeft, mRight })
    {
        const auto [first, end] = mModel.rangeRows(mLinked, columnOf(side));
        if (end <= first)
        {
            continue;
        }
        const LLRect    frame = side->getRect();
        const LLRect    text  = side->textRect();
        const S32       top   = frame.mBottom + text.mTop;
        const S32       from  = top - (topOfRow(side, first) - side->scrollY());
        const S32       to    = top - (topOfRow(side, end) - side->scrollY());
        LLLocalClipRect clip(LLRect(frame.mLeft + text.mLeft, top, frame.mLeft + text.mRight, frame.mBottom + text.mBottom));
        // The edge inside the change's outline, which may be over it.
        const S32 edge = frame.mLeft + text.mLeft + LINKED_INSET;
        gl_rect_2d(frame.mLeft + text.mLeft, from, frame.mLeft + text.mRight, to, mLinkedColor % (0.1f * alpha), true);
        gl_rect_2d(edge, from - LINKED_INSET, edge + LINKED_EDGE, to + LINKED_INSET, mLinkedColor % (0.8f * alpha), true);
    }
}

S32 ALDiffView::topOfRow(ALCodeEditor* side, S32 row) const
{
    // A row with a line: the line's top. A row of nothing: up the gap
    // above the next line by as many rows of it as are drawn from it on.
    const Column column = columnOf(side);
    const Layout layout = layoutOf(side);
    const S32    rows   = mModel.rowCount(layout);
    S32          at     = llclamp(row, 0, rows);
    S32          drawn  = 0;
    for (; at < rows && mModel.lineOfRow(column, at) < 0; ++at)
    {
        drawn += mModel.rowDrawn(layout, at) ? 1 : 0;
    }
    ALTextLayout& lines = side->layout();
    const S32     line  = at < rows ? mModel.lineOfRow(column, at) : side->document().lineCount();
    return lines.lineTop(line) - drawn * lines.rowHeight();
}

S32 ALDiffView::arrowAtPoint(S32 x, S32 y)
{
    // In the gap, beside a change's first row.
    if (!mTakeBack || mInline || changeCount() == 0)
    {
        return -1;
    }
    const S32    half  = (getRect().getWidth() - gap()) / 2;
    const LLRect frame = mRight->getRect();
    const LLRect text  = mRight->textRect();
    if (x < half || x >= half + gap() || y > frame.mBottom + text.mTop || y < frame.mBottom + text.mBottom)
    {
        return -1;
    }
    const S32 doc_y = (frame.mBottom + text.mTop - y) + mRight->scrollY();
    const S32 row_h = mRight->layout().rowHeight();
    for (S32 n = 0; n < changeCount(); ++n)
    {
        const S32 top = topOfRow(mRight, mModel.changeFirst(Layout::Sides, n));
        if (doc_y >= top && doc_y < top + row_h)
        {
            return n;
        }
    }
    return -1;
}

void ALDiffView::drawArrows()
{
    // An arrow at each change's first row in sight, pointing from the side
    // whose lines it puts back to the side they go in.
    if (!mTakeBack || mInline)
    {
        return;
    }
    const F32       alpha  = getDrawContext().mAlpha;
    const LLFontGL* font   = LLFontGL::getFontSansSerifSmall();
    const S32       half   = (getRect().getWidth() - gap()) / 2;
    const LLRect    frame  = mRight->getRect();
    const LLRect    text   = mRight->textRect();
    const S32       top    = frame.mBottom + text.mTop;
    const S32       height = mRight->layout().rowHeight();
    const LLColor4  ink    = mRight->textColor() % (0.7f * alpha);
    const LLColor4  lit    = mRight->cursorColor() % alpha;
    const char*     arrow  = mModel.swapped() ? "\xE2\x86\x90" : "\xE2\x86\x92";
    // A change's first row partly out of sight cut at the sides' edge, not
    // drawn over the titles or the bar.
    LLLocalClipRect clip(LLRect(half, top, half + gap(), frame.mBottom + text.mBottom));
    for (S32 n = 0; n < changeCount(); ++n)
    {
        const S32 row_t = top - (topOfRow(mRight, mModel.changeFirst(Layout::Sides, n)) - mRight->scrollY());
        if (height <= 0 || row_t - height > top || row_t < frame.mBottom + text.mBottom)
        {
            continue;
        }
        const bool hover = n == mArrowHover;
        if (hover)
        {
            gl_rect_2d(half, row_t, half + gap(), row_t - height, mRight->textColor() % (0.15f * alpha));
        }
        font->renderUTF8(arrow, 0, half + gap() / 2, row_t - height / 2, hover ? lit : ink, LLFontGL::HCENTER, LLFontGL::VCENTER);
    }
    mArrowHover = -1;
}

void ALDiffView::drawCurrentChange()
{
    // Its rows, the same on both sides, which are lined up: from the top
    // of its first to the top of the row after its last, across the text,
    // its lines and its gap alike.
    const S32 change = changeAtCaret();
    if (change < 0)
    {
        return;
    }
    const F32           alpha   = getDrawContext().mAlpha;
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* side : sides)
    {
        const S32 first = side ? mModel.changeFirst(layoutOf(side), change) : 0;
        const S32 end   = side ? mModel.changeEnd(layoutOf(side), change) : 0;
        if (!side || end <= first)
        {
            continue;
        }
        const LLRect    frame = side->getRect();
        const LLRect    text  = side->textRect();
        const S32       top   = frame.mBottom + text.mTop;
        const S32       from  = top - (topOfRow(side, first) - side->scrollY());
        const S32       to    = top - (topOfRow(side, end) - side->scrollY());
        LLLocalClipRect clip(LLRect(frame.mLeft + text.mLeft, top, frame.mLeft + text.mRight, frame.mBottom + text.mBottom));
        gl_rect_2d(frame.mLeft + text.mLeft, from, frame.mLeft + text.mRight - 1, to, mCurrentColor % (0.8f * alpha), false);
    }
}

void ALDiffView::drawConflicts()
{
    // Down the text's left edge, from the top of its first row to the top
    // of the row after its last, its gap alike: on each side, so that it
    // reads whichever is looked at.
    if (!mModel.merging() || mModel.conflictCount() == 0)
    {
        return;
    }
    const F32           alpha   = getDrawContext().mAlpha;
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* side : sides)
    {
        if (!side)
        {
            continue;
        }
        const LLRect    frame = side->getRect();
        const LLRect    text  = side->textRect();
        const S32       top   = frame.mBottom + text.mTop;
        LLLocalClipRect clip(LLRect(frame.mLeft + text.mLeft, top, frame.mLeft + text.mRight, frame.mBottom + text.mBottom));
        for (S32 change = 0; change < changeCount(); ++change)
        {
            const S32 first = mModel.changeFirst(layoutOf(side), change);
            const S32 end   = mModel.changeEnd(layoutOf(side), change);
            if (!mModel.changeConflicts(change) || end <= first)
            {
                continue;
            }
            const S32 from = top - (topOfRow(side, first) - side->scrollY());
            const S32 to   = top - (topOfRow(side, end) - side->scrollY());
            if (to >= top || from <= frame.mBottom + text.mBottom)
            {
                continue;
            }
            gl_rect_2d(frame.mLeft + text.mLeft, from, frame.mLeft + text.mLeft + CONFLICT_EDGE, to, mConflictColor % alpha);
        }
    }
}

// --- folds -----------------------------------------------------------------------------

void ALDiffView::setFoldSame(bool fold)
{
    mModel.setFoldSame(fold);
    mBar->setFolded(fold);
    if (foldCount() == 0)
    {
        return;
    }
    // The caret's line kept as far down the view as it was; where its line
    // is folded away, the line beside the run.
    ALCodeEditor* side      = shown();
    const S32     below_top = side->layout().lineTop(side->caret().line) - side->scrollY();
    applyFolds();
    const S32 scroll = llmax(0, side->layout().lineTop(side->caret().line) - below_top);
    for (ALCodeEditor* each : { mLeft, mRight, mInlined })
    {
        each->setScrollY(scroll);
    }
}

void ALDiffView::applyFolds()
{
    const LLColor4 folded = colorOf("CodeDiffFoldColor", LLColor4(0.5f, 0.5f, 0.5f, 0.14f));
    for (ALCodeEditor* side : { mLeft, mRight, mInlined })
    {
        const Column  column = columnOf(side);
        ALTextLayout& layout = side->layout();
        for (S32 n = 0; n < foldCount(); ++n)
        {
            // Its lines, which are the same lines on every side and so a
            // line each there; its own row the gap above the line after,
            // a stop: the caret stops on it going up or down, and Return
            // there opens it.
            const bool open  = mModel.foldOpen(n);
            const S32  first = mModel.foldFirstLine(column, n);
            layout.setHidden(ALTextLayout::HiddenBy::Host, first, first + mModel.foldLines(n) - 1, !open);
            const S32                  under = mModel.foldGapLine(column, n);
            ALTextView::LineAnnotation said  = side->lineAnnotation(under);
            said.gap                         = open ? 0 : 1;
            said.gapTint                     = folded;
            said.gapStop                     = true;
            side->setLineAnnotation(under, said);
        }
        // A caret left on a line now hidden: under the run's row, or at
        // the text's end on the line before the run.
        const S32 at   = side->caret().line;
        const S32 fold = mModel.foldOfRow(ALDiffModel::layoutOf(column), mModel.rowOfLine(column, at), true);
        if (fold >= 0 && layout.hidden(at))
        {
            const S32 under = mModel.foldGapLine(column, fold);
            const S32 last  = side->document().lineCount() - 1;
            side->goTo(ALTextPos(under <= last ? under : llmax(0, mModel.foldFirstLine(column, fold) - 1), 0));
        }
    }
}

void ALDiffView::openFold(S32 fold)
{
    mModel.setFoldOpen(fold, true);
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
        const S32    local_y = y - frame.mBottom;
        const LLRect text    = side->textRect();
        if (local_y > text.mTop || local_y < text.mBottom)
        {
            return -1;
        }
        const S32 fold = mModel.foldOfGap(columnOf(side), side->gapAtLocal(local_y));
        if (fold < 0)
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

bool ALDiffView::moveAtPoint(S32 x, S32 y, ALCodeEditor** under, S32* at_line)
{
    // In a side's gutter, beside a line of a block moved.
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
        if (local_x >= text.mLeft || local_y > text.mTop || local_y < text.mBottom || side->gapAtLocal(local_y) >= 0)
        {
            return false;
        }
        const S32 line = side->posAtLocal(text.mLeft, local_y, false).line;
        if (mModel.line(columnOf(side), line).move < 0)
        {
            return false;
        }
        *under   = side;
        *at_line = line;
        return true;
    }
    return false;
}

void ALDiffView::goToMoved(ALCodeEditor* side, S32 line)
{
    const auto [column, other] = mModel.moveOtherEnd(columnOf(side), line);
    if (other < 0)
    {
        return;
    }
    ALCodeEditor* to = column == Column::Left ? mLeft : column == Column::Right ? mRight : mInlined;
    to->goTo(ALTextPos(other, 0));
    to->setFocus(true);
}

bool ALDiffView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // A folded row opened, the caret put on the first line it hid, on the
    // side pressed, which takes the keyboard.
    if (const S32 change = arrowAtPoint(x, y); change >= 0)
    {
        takeBack(change);
        return true;
    }
    // A moved line's sign: to the other end of its block.
    ALCodeEditor* moved_side = nullptr;
    S32           moved_line = -1;
    if (moveAtPoint(x, y, &moved_side, &moved_line))
    {
        goToMoved(moved_side, moved_line);
        return true;
    }
    ALCodeEditor* side = nullptr;
    const S32     fold = foldAtPoint(x, y, &side);
    if (fold < 0)
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }
    const S32 first = mModel.foldFirstLine(columnOf(side), fold);
    openFold(fold);
    side->goTo(ALTextPos(first, 0));
    side->setFocus(true);
    return true;
}

bool ALDiffView::handleHover(S32 x, S32 y, MASK mask)
{
    mArrowHover = arrowAtPoint(x, y);
    ALCodeEditor* moved_side = nullptr;
    S32           moved_line = -1;
    if (mArrowHover >= 0 || foldAtPoint(x, y) >= 0 || moveAtPoint(x, y, &moved_side, &moved_line))
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
    // Over each folded run's row, how many lines it stands for: the row is
    // a gap, which the side draws tinted, and this is said over it.
    const F32           alpha   = getDrawContext().mAlpha;
    const LLFontGL*     font    = LLFontGL::getFontSansSerifSmall();
    ALCodeEditor* const sides[] = { mInline ? mInlined : mLeft, mInline ? nullptr : mRight };
    for (ALCodeEditor* side : sides)
    {
        if (!side)
        {
            continue;
        }
        const Column    column = columnOf(side);
        const LLRect    frame  = side->getRect();
        const LLRect    text   = side->textRect();
        const S32       top    = frame.mBottom + text.mTop;
        const S32       left   = frame.mLeft + text.mLeft;
        LLLocalClipRect clip(LLRect(left, top, frame.mLeft + text.mRight, frame.mBottom + text.mBottom));
        const LLColor4  ink    = side->textColor() % (0.6f * alpha);
        const S32       height = side->layout().rowHeight();
        const S32       stood  = side->hasFocus() ? mModel.foldOfGap(column, side->caretGap()) : -1;
        for (S32 n = 0; n < foldCount(); ++n)
        {
            if (mModel.foldOpen(n))
            {
                continue;
            }
            const S32 row_t = top - (topOfRow(side, mModel.foldRow(layoutOf(side), n)) - side->scrollY());
            if (row_t - height > top || row_t < frame.mBottom + text.mBottom)
            {
                continue;
            }
            // The one the caret stands on, where the keyboard is: lit, as a
            // line the caret is on would be.
            if (n == stood)
            {
                gl_rect_2d(left, row_t, frame.mLeft + text.mRight, row_t - height, side->textColor() % (0.1f * alpha));
                gl_rect_2d(left, row_t, frame.mLeft + text.mRight - 1, row_t - height + 1, side->cursorColor() % (0.8f * alpha), false);
            }
            font->renderUTF8(mFoldSaid[static_cast<size_t>(n)], 0, left + 8, row_t - height / 2, ink, LLFontGL::LEFT, LLFontGL::VCENTER);
        }
    }
}

void ALDiffView::arrange()
{
    // The titles' row only where there is a title: else the sides start
    // under the bar.
    const bool   titled = !mLeftTitle.empty() || !mRightTitle.empty();
    const S32    width  = getRect().getWidth();
    const S32    bar_h  = ALDiffBar::wantedHeight();
    const S32    height = llmax(0, getRect().getHeight() - bar_h);
    const S32    head_h = titled ? LLFontGL::getFontSansSerifSmall()->getLineHeight() + 6 : 0;
    const S32    body   = llmax(0, height - head_h);
    const S32    half   = (width - gap()) / 2;
    mBar->setShape(LLRect(0, height + bar_h, width, height));
    mLeft->setVisible(!mInline);
    mRight->setVisible(!mInline);
    mInlined->setVisible(mInline);
    mLeftHead->setVisible(titled);
    mRightHead->setVisible(titled && !mInline);
    if (mInline)
    {
        mInlined->setShape(LLRect(0, body, width, 0));
        mLeftHead->setShape(LLRect(4, height - 3, width - 4, body));
        const std::string& from = mModel.swapped() ? mRightTitle : mLeftTitle;
        const std::string& to   = mModel.swapped() ? mLeftTitle : mRightTitle;
        mLeftHead->setText(from.empty() && to.empty() ? std::string() : from + "  \xE2\x86\x92  " + to);
        return;
    }
    mLeft->setShape(LLRect(0, body, half, 0));
    mRight->setShape(LLRect(half + gap(), body, width, 0));
    mLeftHead->setShape(LLRect(4, height - 3, half - 4, body));
    mRightHead->setShape(LLRect(half + gap() + 4, height - 3, width - 4, body));
    mLeftHead->setText(mModel.swapped() ? mRightTitle : mLeftTitle);
    mRightHead->setText(mModel.swapped() ? mLeftTitle : mRightTitle);
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

void ALDiffView::setOnTakeBack(take_back_t take)
{
    const bool had = mTakeBack != nullptr;
    mTakeBack      = std::move(take);
    if (had != (mTakeBack != nullptr))
    {
        arrange();
    }
    refreshBar();
}

S32 ALDiffView::gap() const
{
    if (mInline)
    {
        return GAP;
    }
    return !mBands.empty() ? BRACKET_GAP : mTakeBack ? ARROW_GAP : GAP;
}

void ALDiffView::drawRanges()
{
    if (mInline || mBands.empty())
    {
        return;
    }
    const F32    alpha  = getDrawContext().mAlpha;
    const S32    half   = (getRect().getWidth() - gap()) / 2;
    const S32    x0     = half + 1;
    const S32    x1     = half + gap() - 2;
    const LLRect frame  = mRight->getRect();
    const LLRect text   = mRight->textRect();
    const S32    top    = frame.mBottom + text.mTop;
    const S32    bottom = frame.mBottom + text.mBottom;
    const auto   yOf    = [&](ALCodeEditor* side, S32 row) { return top - (topOfRow(side, row) - side->scrollY()); };
    LLLocalClipRect clip(LLRect(half, top, half + gap(), bottom));
    for (const S32 n : mBands)
    {
        // The one the caret is in, as its rows are, brighter.
        const bool     linked = n == mLinked;
        const LLColor4 band   = linked ? mLinkedColor % (0.2f * alpha) : mRight->textColor() % (0.08f * alpha);
        const LLColor4 edge   = linked ? mLinkedColor % (0.8f * alpha) : mRight->textColor() % (0.35f * alpha);
        const auto [lf, le] = mModel.rangeRows(n, Column::Left);
        const auto [rf, re] = mModel.rangeRows(n, Column::Right);
        const S32 lt = yOf(mLeft, lf);
        const S32 lb = yOf(mLeft, le);
        const S32 rt = yOf(mRight, rf);
        const S32 rb = yOf(mRight, re);
        if ((lb > top && rb > top) || (lt < bottom && rt < bottom) || (lt == lb && rt == rb))
        {
            continue;
        }
        // The band, and its edges: a bracket on each side over its rows,
        // the two joined top and bottom.
        gl_triangle_2d(x0, lt, x1, rt, x1, rb, band, true);
        gl_triangle_2d(x0, lt, x1, rb, x0, lb, band, true);
        gl_line_2d(x0, lt, x1, rt, edge);
        gl_line_2d(x0, lb, x1, rb, edge);
        gl_line_2d(x0, lt, x0, lb, edge);
        gl_line_2d(x1, rt, x1, rb, edge);
    }
}

bool ALDiffView::takeBack(S32 change)
{
    // The edit worked out by the model, made by whoever shows it, and the
    // comparison made again from the right as it then is.
    ALTextRange range;
    std::string text;
    std::string made;
    if (!mTakeBack || !mModel.takeBack(change, range, text, made) || !mTakeBack(range, text))
    {
        return false;
    }
    setRightText(made);
    return true;
}

bool ALDiffView::settle(S32 change, ALTextMerge::Take take)
{
    // The edit, where it needs one, made by whoever shows it; the base
    // taken as theirs there either way, and the conflicts found again.
    const std::optional<ALDiffMerge::Settling> settling = mModel.settle(change, take);
    if (!settling || (settling->edits && (!mTakeBack || !mTakeBack(settling->range, settling->text))))
    {
        return false;
    }
    mModel.settled(settling->base);
    if (settling->edits)
    {
        setRightText(settling->made);
    }
    refreshBar();
    return true;
}

S32 ALDiffView::changeAtCaret() const
{
    const ALCodeEditor* side = shown();
    return mModel.changeOfLine(columnOf(side), side->caret().line);
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
    const S32     to   = mModel.changeStep(columnOf(side), side->caret().line, forward);
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
            each->goTo(ALTextPos(mModel.caretLineOfRow(columnOf(each), mModel.changeFirst(layoutOf(each), to)), 0));
        }
    }
    side->scrollToCaret();
    return true;
}

std::pair<S32, S32> ALDiffView::rightAtCaret() const
{
    const ALCodeEditor* side = shown();
    return mModel.rightAt(columnOf(side), side->caret().line, side->caret().column);
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
    // Return on a folded row opens it, rather than going to the source:
    // the caret on the first line it hid.
    if (key == KEY_RETURN && mask == MASK_NONE)
    {
        ALCodeEditor* side = shown();
        if (const S32 fold = mModel.foldOfGap(columnOf(side), side->caretGap()); fold >= 0)
        {
            const S32 first = mModel.foldFirstLine(columnOf(side), fold);
            openFold(fold);
            side->goTo(ALTextPos(first, 0));
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
    refreshLinked();
    // The change the caret is in, and the steps there are from it.
    mBar->setFellBack(mModel.fellBack());
    mBar->setCount(changeAtCaret(), changeCount());
    mBar->setTakeBackShown(mTakeBack != nullptr);
    mBar->setTakeBackEnabled(mTakeBack && changeAtCaret() >= 0);
    if (mModel.merging())
    {
        mBar->setConflicts(mModel.conflictCount(), mTakeBack && mModel.changeConflicts(changeAtCaret()));
    }
    const ALCodeEditor* side = shown();
    mBar->setSteps(mModel.changeStep(columnOf(side), side->caret().line, false) >= 0, mModel.changeStep(columnOf(side), side->caret().line, true) >= 0);
}

void ALDiffView::followScroll(ALCodeEditor* from)
{
    if (mInline || mFollowing || from == mInlined)
    {
        return;
    }
    ALCodeEditor* other = from == mLeft ? mRight : mLeft;
    mFollowing          = true;
    other->setScrollY(from->scrollY());
    other->setScrollX(from->scrollX());
    mFollowing = false;
}

void ALDiffView::refreshColors()
{
    if (mColorsGeneration == LLUIColorTable::instance().generation())
    {
        return;
    }
    mColorsGeneration = LLUIColorTable::instance().generation();
    mBar->setColors(mRight->backgroundColor(), mRight->textColor());
    mCurrentColor  = colorOf("CodeDiffCurrentColor", mRight->cursorColor());
    mDividerColor  = colorOf("CodeDiffDividerColor", LLColor4(0.5f, 0.5f, 0.5f, 0.5f));
    mLinkedColor   = colorOf("CodeDiffLinkedColor", LLColor4(0.45f, 0.65f, 1.f, 1.f));
    mConflictColor = colorOf("CodeDiffConflictColor", LLColor4(1.f, 0.6f, 0.1f, 1.f));
}

void ALDiffView::draw()
{
    refreshColors();
    LLUICtrl::draw();
    drawFoldRows();
    drawLinked();
    drawConflicts();
    drawCurrentChange();
    if (!mInline)
    {
        // The line between the sides down the middle of the gap, and the
        // arrows beside it where there are any.
        const S32 half   = (getRect().getWidth() - gap()) / 2;
        const S32 middle = half + gap() / 2;
        if (mBands.empty())
        {
            gl_rect_2d(middle - GAP / 2 + 1, mLeft->getRect().mTop, middle - GAP / 2 + GAP - 1, 0, mDividerColor % getDrawContext().mAlpha);
        }
        drawRanges();
        drawArrows();
    }
}
