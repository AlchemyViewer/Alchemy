/**
 * @file altextview.cpp
 * @brief A text view: a document laid out and drawn, with a caret in it.
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

#include "altextview.h"

#include "alviewtype.h"
#include "llclipboard.h"
#include "lldir.h"
#include "llfocusmgr.h"
#include "llkeyboard.h"
#include "lllocalcliprect.h"
#include "llmenugl.h"
#include "llrender2dutils.h"
#include "llscrollbar.h"
#include "llstring.h"
#include "lltimer.h"
#include "llui.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"
#include "llwindow.h"

static LLDefaultChildRegistry::Register<ALTextView> r("text_view");

namespace
{
    const F32 BLINK_DELAY           = 1.f;
    const S32 CARET_WIDTH           = 2;
    const F32 TRIPLE_CLICK_INTERVAL = 0.3f;
    const S32 WHEEL_ROWS            = 3;
    // Room past the widest line before a horizontal scrollbar is needed,
    // and what the caret keeps between itself and an edge.
    const S32 H_MARGIN              = 16;
    const F32 CARET_MARGIN          = 8.f;

    // The colour each kind is drawn in, by the name in the colour table;
    // Text is the view's own text colour.
    const char* const KIND_COLOR_NAMES[] = {
        "TextFgColor",       "SyntaxComment",  "SyntaxDocComment", "SyntaxString",   "SyntaxEscape",      "SyntaxNumber",
        "SyntaxKeyword",     "SyntaxControl",  "SyntaxType",       "SyntaxConstant", "SyntaxFunction",    "SyntaxEvent",
        "SyntaxLabel",       "SyntaxOperator", "SyntaxPunctuation", "SyntaxPreprocessor", "SyntaxTag",     "SyntaxAttribute",
        "SyntaxAttributeValue", "SyntaxEntity", "SyntaxVariable",   "SyntaxParameter", "SyntaxProperty",   "SyntaxDeprecated",
        "SyntaxInvalid",
    };
    static_assert(sizeof(KIND_COLOR_NAMES) / sizeof(KIND_COLOR_NAMES[0]) == static_cast<size_t>(ALSyntaxKind::COUNT), "every kind has a colour");

    bool editsText(ALEditorCommand command)
    {
        switch (command)
        {
            case ALEditorCommand::DeleteLeft:
            case ALEditorCommand::DeleteRight:
            case ALEditorCommand::DeleteWordLeft:
            case ALEditorCommand::DeleteWordRight:
            case ALEditorCommand::NewLine:
            case ALEditorCommand::Indent:
            case ALEditorCommand::Unindent:
            case ALEditorCommand::Undo:
            case ALEditorCommand::Redo:
            case ALEditorCommand::Cut:
            case ALEditorCommand::Paste:
            case ALEditorCommand::Delete:
            case ALEditorCommand::ToggleComment:
            case ALEditorCommand::DuplicateLine:
            case ALEditorCommand::MoveLineUp:
            case ALEditorCommand::MoveLineDown:
            case ALEditorCommand::DeleteLine:
            case ALEditorCommand::Complete:
                return true;
            default:
                return false;
        }
    }

    bool identifierByte(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }
}

ALTextView::Params::Params()
:   text_color("text_color"),
    text_readonly_color("text_readonly_color"),
    bg_color("bg_color"),
    bg_readonly_color("bg_readonly_color"),
    bg_focus_color("bg_focus_color"),
    cursor_color("cursor_color"),
    selection_color("selection_color"),
    bg_visible("bg_visible", true),
    read_only("read_only", false),
    word_wrap("word_wrap", false),
    soft_tabs("soft_tabs", false),
    tab_width("tab_width", 4),
    h_pad("h_pad", 4),
    v_pad("v_pad", 2),
    syntax("syntax"),
    default_text("default_text"),
    context_menu("context_menu")
{
}

ALTextView::ALTextView(const Params& p)
:   LLUICtrl(p),
    mUndo(mDocument),
    mKeymap(ALKeymap::standard()),
    mFont(p.font),
    mTextColor(p.text_color),
    mTextReadOnlyColor(p.text_readonly_color),
    mBgColor(p.bg_color),
    mBgReadOnlyColor(p.bg_readonly_color),
    mBgFocusColor(p.bg_focus_color.isProvided() ? p.bg_focus_color() : p.bg_color()),
    mCursorColor(p.cursor_color),
    mSelectionColor(p.selection_color),
    mBgVisible(p.bg_visible),
    mReadOnly(p.read_only),
    mWordWrap(p.word_wrap),
    mSoftTabs(p.soft_tabs),
    mHPad(p.h_pad),
    mVPad(p.v_pad),
    mContextMenuFile(p.context_menu.isProvided() ? p.context_menu() : std::string())
{
    const S32 tab_width = p.tab_width;
    mTabWidth           = llmax(1, tab_width);
    for (size_t kind = 0; kind < mKindColors.size(); ++kind)
    {
        mKindColors[kind] = LLUIColorTable::instance().getColor(KIND_COLOR_NAMES[kind], mTextColor.get());
    }

    mHighlighter.attach(&mDocument);
    mLayout.attach(&mDocument);
    mLayout.setFont(mFont);
    mLayout.setTabWidth(mTabWidth);

    static LLUICachedControl<S32> scrollbar_size("UIScrollbarSize", 0);
    const S32                     size = scrollbar_size;
    LLScrollbar::Params           bar;
    bar.name("scrollbar");
    bar.rect(LLRect(getRect().getWidth() - size, getRect().getHeight(), getRect().getWidth(), 0));
    bar.orientation(LLScrollbar::VERTICAL);
    bar.doc_size(0);
    bar.doc_pos(0);
    bar.page_size(1);
    bar.change_callback(boost::bind(&ALTextView::onScrollChange, this, _1, _2));
    bar.follows.flags(FOLLOWS_RIGHT | FOLLOWS_TOP | FOLLOWS_BOTTOM);
    bar.visible(false);
    mScrollbar = LLUICtrlFactory::create<LLScrollbar>(bar);
    addChild(mScrollbar);

    LLScrollbar::Params hbar;
    hbar.name("h_scrollbar");
    hbar.rect(LLRect(0, size, getRect().getWidth() - size, 0));
    hbar.orientation(LLScrollbar::HORIZONTAL);
    hbar.doc_size(0);
    hbar.doc_pos(0);
    hbar.page_size(1);
    hbar.change_callback(boost::bind(&ALTextView::onScrollChange, this, _1, _2));
    hbar.follows.flags(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_BOTTOM);
    hbar.visible(false);
    mHScrollbar = LLUICtrlFactory::create<LLScrollbar>(hbar);
    addChild(mHScrollbar);

    if (p.syntax.isProvided())
    {
        const std::string& grammar_name = p.syntax;
        setSyntax(grammar_name);
    }
    if (p.default_text.isProvided())
    {
        const std::string& initial = p.default_text;
        setText(initial);
    }
    if (mWordWrap)
    {
        mLayout.setWrapWidth(textRect().getWidth());
    }
}

ALTextView::~ALTextView()
{
    if (LLContextMenu* menu = mContextMenuHandle.get())
    {
        menu->die();
        mContextMenuHandle.markDead();
    }
    if (LLWindow* window = getWindow())
    {
        window->allowLanguageTextInput(this, false);
    }
    if (gEditMenuHandler == this)
    {
        gEditMenuHandler = nullptr;
    }
}

// --- the text ----------------------------------------------------------------

void ALTextView::setText(std::string_view text)
{
    mPreeditLength = 0;
    mPreeditSegmentEnds.clear();
    mPreeditStandouts.clear();
    mPreeditOverwritten.clear();
    mDocument.setText(text);
    mUndo.clear();
    mUndo.markSaved();
    mCaret = mAnchor = mDocument.start();
    mDesiredX         = -1.f;
    mScrollY          = 0;
    mScrollX          = 0.f;
    mChangedSinceFocus = false;
    syncScrollbar();
    mChanged();
}

void ALTextView::setValue(const LLSD& value)
{
    setText(value.asString());
}

LLSD ALTextView::getValue() const
{
    return LLSD(text());
}

ALSyntaxLibrary& ALTextView::syntaxLibrary()
{
    static ALSyntaxLibrary library;
    static bool            loaded = false;
    if (!loaded)
    {
        loaded = true;
        if (gDirUtilp)
        {
            library.loadDirectory(gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "syntax"));
        }
    }
    return library;
}

void ALTextView::setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar)
{
    mHighlighter.setGrammar(std::move(grammar));
}

void ALTextView::setSyntax(std::string_view grammar_name)
{
    setGrammar(syntaxLibrary().find(grammar_name));
}

void ALTextView::setFont(const LLFontGL* font)
{
    mFont = font ? font : LLFontGL::getFontMonospace();
    mLayout.setFont(mFont);
    syncScrollbar();
}

void ALTextView::setReadOnly(bool read_only)
{
    mReadOnly = read_only;
    if (hasFocus())
    {
        allowLanguageInput(!read_only);
    }
}

void ALTextView::setWordWrap(bool wrap)
{
    mWordWrap = wrap;
    mScrollX  = 0.f;
    mLayout.setWrapWidth(wrap ? textRect().getWidth() : 0);
    syncScrollbar();
}

void ALTextView::setTabWidth(S32 spaces)
{
    mTabWidth = llmax(1, spaces);
    mLayout.setTabWidth(mTabWidth);
}

// --- geometry ----------------------------------------------------------------

LLRect ALTextView::textRect() const
{
    LLRect rect = getLocalRect();
    rect.mLeft += mHPad + leftInset();
    rect.mRight -= mHPad;
    rect.mTop -= mVPad;
    rect.mBottom += mVPad;
    if (mScrollbar && mScrollbar->getVisible())
    {
        rect.mRight -= mScrollbar->getRect().getWidth();
    }
    if (mHScrollbar && mHScrollbar->getVisible())
    {
        rect.mBottom += mHScrollbar->getRect().getHeight();
    }
    return rect;
}

S32 ALTextView::rowsPerPage() const
{
    const S32 row = mLayout.rowHeight();
    return row > 0 ? llmax(1, textRect().getHeight() / row) : 1;
}

void ALTextView::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    if (mWordWrap)
    {
        mLayout.setWrapWidth(textRect().getWidth());
    }
    syncScrollbar();
}

ALTextPos ALTextView::posAtLocal(S32 x, S32 y, bool round)
{
    const LLRect text  = textRect();
    const S32    row_h = mLayout.rowHeight();
    if (row_h <= 0 || mDocument.lineCount() == 0)
    {
        return mDocument.start();
    }
    const S32 doc_y = (text.mTop - y) + mScrollY;
    const S32 line  = mLayout.lineAtY(llmax(0, doc_y));
    const S32 top   = mLayout.lineTop(line);
    const S32 row   = llclamp((doc_y - top) / row_h, 0, mLayout.rowCount(line) - 1);
    const F32 x_rel = static_cast<F32>(x - text.mLeft) + mScrollX;
    return mDocument.clamp(ALTextPos(line, mLayout.columnAt(line, row, x_rel, round)));
}

// --- scrolling -----------------------------------------------------------------

bool ALTextView::hasHorizontalScrollbar() const
{
    return mHScrollbar && mHScrollbar->getVisible();
}

void ALTextView::syncScrollbar()
{
    if (!mScrollbar || !mHScrollbar)
    {
        return;
    }
    // Each bar takes room the other's decision depends on, so both are
    // decided again once the first decision has been applied.
    for (S32 pass = 0; pass < 2; ++pass)
    {
        const LLRect text   = textRect();
        const bool   need_v = mLayout.totalHeight() > llmax(1, text.getHeight());
        const bool   need_h = !mWordWrap && mLayout.contentWidth() + static_cast<F32>(H_MARGIN) > static_cast<F32>(text.getWidth());
        bool         changed = false;
        if (mScrollbar->getVisible() != need_v)
        {
            mScrollbar->setVisible(need_v);
            changed = true;
        }
        if (mHScrollbar->getVisible() != need_h)
        {
            mHScrollbar->setVisible(need_h);
            changed = true;
        }
        if (!changed)
        {
            break;
        }
    }
    const LLRect local = getLocalRect();
    const S32    v_w   = mScrollbar->getRect().getWidth();
    const S32    h_h   = mHScrollbar->getRect().getHeight();
    if (mScrollbar->getVisible())
    {
        mScrollbar->setShape(LLRect(local.mRight - v_w, local.mTop, local.mRight, mHScrollbar->getVisible() ? h_h : 0));
    }
    if (mHScrollbar->getVisible())
    {
        mHScrollbar->setShape(LLRect(0, h_h, mScrollbar->getVisible() ? local.mRight - v_w : local.mRight, 0));
    }

    const LLRect text = textRect();
    if (mWordWrap)
    {
        mLayout.setWrapWidth(text.getWidth());
    }
    const S32 page = llmax(1, text.getHeight());
    mScrollY       = llclamp(mScrollY, 0, llmax(0, mLayout.totalHeight() - page));
    mScrollbar->setDocSize(mLayout.totalHeight());
    mScrollbar->setPageSize(page);
    mScrollbar->setDocPos(mScrollY);

    const S32 width   = llmax(1, text.getWidth());
    const S32 content = mWordWrap ? 0 : static_cast<S32>(ceilf(mLayout.contentWidth())) + H_MARGIN;
    mScrollX          = llclamp(mScrollX, 0.f, static_cast<F32>(llmax(0, content - width)));
    mHScrollbar->setDocSize(content);
    mHScrollbar->setPageSize(width);
    mHScrollbar->setDocPos(static_cast<S32>(mScrollX));
}

void ALTextView::onScrollChange(S32 pos, LLScrollbar* bar)
{
    if (bar == mHScrollbar)
    {
        mScrollX = static_cast<F32>(pos);
    }
    else
    {
        mScrollY = pos;
    }
}

void ALTextView::setScrollY(S32 y)
{
    mScrollY = llmax(0, y);
    syncScrollbar();
}

void ALTextView::setScrollX(F32 x)
{
    mScrollX = llmax(0.f, x);
    syncScrollbar();
}

void ALTextView::scrollToCaret()
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    S32       row;
    const F32 x    = mLayout.xOf(mCaret.line, mCaret.column, &row);
    const S32 top  = mLayout.lineTop(mCaret.line) + row * row_h;
    const LLRect text = textRect();
    const S32 page = llmax(row_h, text.getHeight());
    if (top < mScrollY)
    {
        mScrollY = top;
    }
    else if (top + row_h > mScrollY + page)
    {
        mScrollY = top + row_h - page;
    }
    mScrollY = llmax(0, mScrollY);
    if (mWordWrap)
    {
        mScrollX = 0.f;
    }
    else
    {
        const F32 width = static_cast<F32>(llmax(1, text.getWidth()));
        if (x < mScrollX + CARET_MARGIN)
        {
            mScrollX = llmax(0.f, x - CARET_MARGIN);
        }
        else if (x + CARET_MARGIN > mScrollX + width)
        {
            mScrollX = x + CARET_MARGIN - width;
        }
    }
    syncScrollbar();
}

void ALTextView::scrollToLine(S32 line)
{
    mScrollY = mLayout.lineTop(line);
    syncScrollbar();
}

S32 ALTextView::firstVisibleLine()
{
    return mLayout.lineAtY(mScrollY);
}

// --- the caret ---------------------------------------------------------------

void ALTextView::placeCaret(const ALTextPos& pos, bool extend)
{
    mCaret = mDocument.clamp(pos);
    if (!extend)
    {
        mAnchor = mCaret;
    }
    if (mLayout.hidden(mCaret.line))
    {
        revealLine(mCaret.line);
    }
    mBlink.reset();
}

void ALTextView::setCaret(ALTextPos pos, bool extend)
{
    placeCaret(pos, extend);
    mDesiredX = -1.f;
    scrollToCaret();
}

void ALTextView::setSelection(const ALTextRange& range)
{
    mAnchor = mDocument.clamp(range.begin);
    placeCaret(range.end, true);
    mDesiredX = -1.f;
    scrollToCaret();
}

std::string ALTextView::wordBeforeCaret() const
{
    const std::string& line  = mDocument.line(mCaret.line);
    S32                begin = llmin(mCaret.column, static_cast<S32>(line.size()));
    while (begin > 0 && identifierByte(line[begin - 1]))
    {
        --begin;
    }
    return line.substr(begin, mCaret.column - begin);
}

void ALTextView::moveVertically(S32 rows, bool extend)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    S32       row;
    const F32 x = mLayout.xOf(mCaret.line, mCaret.column, &row);
    if (mDesiredX < 0.f)
    {
        mDesiredX = x;
    }
    S32 line = mCaret.line;
    for (; rows < 0; ++rows)
    {
        if (row > 0)
        {
            --row;
        }
        else if (const S32 above = mLayout.visibleFrom(line - 1, -1); above >= 0)
        {
            line = above;
            row  = mLayout.rowCount(line) - 1;
        }
        else
        {
            placeCaret(mDocument.start(), extend);
            mDesiredX = -1.f;
            scrollToCaret();
            return;
        }
    }
    for (; rows > 0; --rows)
    {
        if (row + 1 < mLayout.rowCount(line))
        {
            ++row;
        }
        else if (const S32 below = mLayout.visibleFrom(line + 1, 1); below >= 0)
        {
            line = below;
            row  = 0;
        }
        else
        {
            placeCaret(mDocument.end(), extend);
            mDesiredX = -1.f;
            scrollToCaret();
            return;
        }
    }
    placeCaret(ALTextPos(line, mLayout.columnAt(line, row, mDesiredX, true)), extend);
    scrollToCaret();
}

// --- editing -------------------------------------------------------------------

ALTextDocument::Edit ALTextView::edit(const ALTextRange& range, std::string_view text)
{
    const ALTextPos      before = mCaret;
    ALTextDocument::Edit done   = mDocument.replace(range, text);
    if (done.nothing())
    {
        return done;
    }
    const ALTextPos after = mDocument.clamp(done.endAfter());
    mUndo.record(done, before, after, LLTimer::getElapsedSeconds());
    placeCaret(after, false);
    return done;
}

void ALTextView::afterEdit()
{
    mDesiredX          = -1.f;
    mChangedSinceFocus = true;
    mBlink.reset();
    scrollToCaret();
    mChanged();
}

void ALTextView::insertText(std::string_view text)
{
    if (mReadOnly)
    {
        return;
    }
    if (!edit(selection(), text).nothing())
    {
        afterEdit();
    }
}

void ALTextView::deleteRange(const ALTextRange& range)
{
    if (mReadOnly)
    {
        return;
    }
    if (!edit(range, std::string_view()).nothing())
    {
        afterEdit();
    }
}

std::string ALTextView::tabText(const ALTextPos& at) const
{
    if (!mSoftTabs)
    {
        return "\t";
    }
    const S32 column = mDocument.displayColumn(at, mTabWidth);
    return std::string(mTabWidth - (column % mTabWidth), ' ');
}

std::pair<S32, S32> ALTextView::selectedLines() const
{
    const ALTextRange range = selection().normalised();
    S32               last  = range.end.line;
    if (last > range.begin.line && range.end.column == 0)
    {
        --last;
    }
    return { range.begin.line, last };
}

void ALTextView::indentLines(bool in)
{
    const auto [first, last] = selectedLines();
    mUndo.beginGroup();
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = mDocument.line(l);
        if (in)
        {
            if (!line.empty())
            {
                edit(ALTextRange(ALTextPos(l, 0), ALTextPos(l, 0)), mSoftTabs ? std::string(mTabWidth, ' ') : std::string("\t"));
            }
        }
        else
        {
            S32 taken = 0;
            if (!line.empty() && line[0] == '\t')
            {
                taken = 1;
            }
            else
            {
                while (taken < mTabWidth && taken < static_cast<S32>(line.size()) && line[taken] == ' ')
                {
                    ++taken;
                }
            }
            if (taken)
            {
                edit(ALTextRange(ALTextPos(l, 0), ALTextPos(l, taken)), std::string_view());
            }
        }
    }
    mUndo.endGroup();
    // The lines, whole, stay selected.
    mAnchor = ALTextPos(first, 0);
    mCaret  = last + 1 < mDocument.lineCount() ? ALTextPos(last + 1, 0) : mDocument.lineEnd(last);
    afterEdit();
}

void ALTextView::duplicateLines()
{
    const auto [first, last] = selectedLines();
    const std::string block  = mDocument.text(ALTextRange(mDocument.lineStart(first), mDocument.lineEnd(last)));
    const ALTextPos   caret  = mCaret;
    const ALTextPos   anchor = mAnchor;
    const S32         count  = last - first + 1;
    mUndo.beginGroup();
    edit(ALTextRange(mDocument.lineEnd(last), mDocument.lineEnd(last)), "\n" + block);
    mUndo.endGroup();
    // The caret and the selection go with the copy.
    mAnchor = ALTextPos(anchor.line + count, anchor.column);
    placeCaret(ALTextPos(caret.line + count, caret.column), true);
    afterEdit();
}

void ALTextView::moveLines(S32 direction)
{
    const auto [first, last] = selectedLines();
    if ((direction < 0 && first == 0) || (direction > 0 && last + 1 >= mDocument.lineCount()))
    {
        return;
    }
    const std::string block  = mDocument.text(ALTextRange(mDocument.lineStart(first), mDocument.lineEnd(last)));
    const ALTextPos   caret  = mCaret;
    const ALTextPos   anchor = mAnchor;
    mUndo.beginGroup();
    if (direction < 0)
    {
        const std::string above = mDocument.line(first - 1);
        edit(ALTextRange(mDocument.lineStart(first - 1), mDocument.lineEnd(last)), block + "\n" + above);
    }
    else
    {
        const std::string below = mDocument.line(last + 1);
        edit(ALTextRange(mDocument.lineStart(first), mDocument.lineEnd(last + 1)), below + "\n" + block);
    }
    mUndo.endGroup();
    mAnchor = ALTextPos(anchor.line + direction, anchor.column);
    placeCaret(ALTextPos(caret.line + direction, caret.column), true);
    afterEdit();
}

void ALTextView::deleteLines()
{
    const auto [first, last] = selectedLines();
    ALTextRange range(mDocument.lineStart(first), last + 1 < mDocument.lineCount() ? mDocument.lineStart(last + 1) : mDocument.lineEnd(last));
    if (last + 1 >= mDocument.lineCount() && first > 0)
    {
        // The last line goes with the newline before it.
        range.begin = mDocument.lineEnd(first - 1);
    }
    const S32 column = mCaret.column;
    mUndo.beginGroup();
    edit(range, std::string_view());
    mUndo.endGroup();
    const S32 line = llmin(first, mDocument.lineCount() - 1);
    placeCaret(ALTextPos(line, llmin(column, mDocument.lineLength(line))), false);
    afterEdit();
}

bool ALTextView::perform(ALEditorCommand command)
{
    typedef ALEditorCommand C;
    const bool extend = command >= C::SelectLeft && command <= C::SelectPageDown;
    auto move = [&](const ALTextPos& to) {
        placeCaret(to, extend);
        mDesiredX = -1.f;
        scrollToCaret();
        return true;
    };
    // An arrow with a selection and no shift collapses it to that end.
    auto collapse = [&](bool to_begin) {
        if (!hasSelection() || extend)
        {
            return false;
        }
        const ALTextRange range = selection().normalised();
        return move(to_begin ? range.begin : range.end);
    };
    if (mReadOnly && editsText(command))
    {
        return false;
    }
    switch (command)
    {
        case C::None:
        case C::COUNT:
            return false;
        case C::MoveLeft:
        case C::SelectLeft:
            return collapse(true) || move(mDocument.prevCluster(mCaret));
        case C::MoveRight:
        case C::SelectRight:
            return collapse(false) || move(mDocument.nextCluster(mCaret));
        case C::MoveUp:
        case C::SelectUp:
            moveVertically(-1, extend);
            return true;
        case C::MoveDown:
        case C::SelectDown:
            moveVertically(1, extend);
            return true;
        case C::MoveWordLeft:
        case C::SelectWordLeft:
            return move(mDocument.prevWord(mCaret));
        case C::MoveWordRight:
        case C::SelectWordRight:
            return move(mDocument.nextWord(mCaret));
        case C::MoveLineStart:
        case C::SelectLineStart:
        {
            // To the first thing on the line, or to the line's start from
            // there.
            const std::string& line   = mDocument.line(mCaret.line);
            S32                indent = 0;
            while (indent < static_cast<S32>(line.size()) && (line[indent] == ' ' || line[indent] == '\t'))
            {
                ++indent;
            }
            return move(ALTextPos(mCaret.line, mCaret.column == indent ? 0 : indent));
        }
        case C::MoveLineEnd:
        case C::SelectLineEnd:
            return move(mDocument.lineEnd(mCaret.line));
        case C::MoveDocStart:
        case C::SelectDocStart:
            return move(mDocument.start());
        case C::MoveDocEnd:
        case C::SelectDocEnd:
            return move(mDocument.end());
        case C::MovePageUp:
        case C::SelectPageUp:
            moveVertically(-rowsPerPage(), extend);
            return true;
        case C::MovePageDown:
        case C::SelectPageDown:
            moveVertically(rowsPerPage(), extend);
            return true;
        case C::SelectAll:
            selectAll();
            return true;
        case C::DeleteLeft:
            if (hasSelection())
            {
                deleteRange(selection());
            }
            else if (mCaret != mDocument.start())
            {
                deleteRange(ALTextRange(mDocument.prevCluster(mCaret), mCaret));
            }
            return true;
        case C::DeleteRight:
            if (hasSelection())
            {
                deleteRange(selection());
            }
            else if (mCaret != mDocument.end())
            {
                deleteRange(ALTextRange(mCaret, mDocument.nextCluster(mCaret)));
            }
            return true;
        case C::DeleteWordLeft:
            deleteRange(hasSelection() ? selection() : ALTextRange(mDocument.prevWord(mCaret), mCaret));
            return true;
        case C::DeleteWordRight:
            deleteRange(hasSelection() ? selection() : ALTextRange(mCaret, mDocument.nextWord(mCaret)));
            return true;
        case C::NewLine:
        {
            // The new line starts with the indentation of the one it leaves.
            const ALTextPos    at   = selection().normalised().begin;
            const std::string& line = mDocument.line(at.line);
            std::string        text = "\n";
            S32                n    = 0;
            while (n < at.column && n < static_cast<S32>(line.size()) && (line[n] == ' ' || line[n] == '\t'))
            {
                ++n;
            }
            text.append(line, 0, n);
            insertText(text);
            return true;
        }
        case C::Indent:
        {
            const auto [first, last] = selectedLines();
            if (last > first)
            {
                indentLines(true);
            }
            else
            {
                insertText(tabText(selection().normalised().begin));
            }
            return true;
        }
        case C::Unindent:
            indentLines(false);
            return true;
        case C::Undo:
            undo();
            return true;
        case C::Redo:
            redo();
            return true;
        case C::Cut:
            cut();
            return true;
        case C::Copy:
            copy();
            return true;
        case C::Paste:
            paste();
            return true;
        case C::Delete:
            doDelete();
            return true;
        case C::ToggleComment:
            return toggleComment();
        case C::DuplicateLine:
            duplicateLines();
            return true;
        case C::MoveLineUp:
            moveLines(-1);
            return true;
        case C::MoveLineDown:
            moveLines(1);
            return true;
        case C::DeleteLine:
            deleteLines();
            return true;
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
            return performFold(command);
        case C::Complete:
            return complete();
    }
    return false;
}

bool ALTextView::canPerform(ALEditorCommand command) const
{
    typedef ALEditorCommand C;
    switch (command)
    {
        case C::Undo:
            return canUndo();
        case C::Redo:
            return canRedo();
        case C::Cut:
            return canCut();
        case C::Copy:
            return canCopy();
        case C::Paste:
            return canPaste();
        case C::Delete:
            return canDoDelete();
        case C::SelectAll:
            return canSelectAll();
        case C::ToggleComment:
            return !mReadOnly && mHighlighter.grammar() && !mHighlighter.grammar()->lineComment().empty();
        case C::Fold:
        case C::Unfold:
        case C::FoldAll:
        case C::UnfoldAll:
            return canFold(command);
        default:
            return !(mReadOnly && editsText(command));
    }
}

bool ALTextView::toggleComment()
{
    if (mReadOnly || !mHighlighter.grammar() || mHighlighter.grammar()->lineComment().empty())
    {
        return false;
    }
    const std::string& token   = mHighlighter.grammar()->lineComment();
    const auto [first, last]   = selectedLines();
    const bool had_selection   = hasSelection();
    const ALTextPos caret_was  = mCaret;

    // Out where every line that says anything is commented; in otherwise.
    auto indentation = [&](const std::string& line) {
        S32 n = 0;
        while (n < static_cast<S32>(line.size()) && (line[n] == ' ' || line[n] == '\t'))
        {
            ++n;
        }
        return n;
    };
    bool all_commented = true;
    bool any_text      = false;
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = mDocument.line(l);
        const S32          n    = indentation(line);
        if (n == static_cast<S32>(line.size()))
        {
            continue;
        }
        any_text = true;
        if (line.compare(n, token.size(), token) != 0)
        {
            all_commented = false;
        }
    }
    if (!any_text)
    {
        return true;
    }
    S32 caret_shift = 0;
    mUndo.beginGroup();
    for (S32 l = first; l <= last; ++l)
    {
        const std::string& line = mDocument.line(l);
        const S32          n    = indentation(line);
        if (n == static_cast<S32>(line.size()))
        {
            continue;
        }
        if (all_commented)
        {
            S32 taken = static_cast<S32>(token.size());
            if (n + taken < static_cast<S32>(line.size()) && line[n + taken] == ' ')
            {
                ++taken;
            }
            edit(ALTextRange(ALTextPos(l, n), ALTextPos(l, n + taken)), std::string_view());
            if (l == caret_was.line)
            {
                caret_shift = caret_was.column >= n + taken ? -taken : (caret_was.column > n ? n - caret_was.column : 0);
            }
        }
        else
        {
            edit(ALTextRange(ALTextPos(l, n), ALTextPos(l, n)), token + " ");
            if (l == caret_was.line && caret_was.column >= n)
            {
                caret_shift = static_cast<S32>(token.size()) + 1;
            }
        }
    }
    mUndo.endGroup();
    if (had_selection)
    {
        mAnchor = ALTextPos(first, 0);
        mCaret  = last + 1 < mDocument.lineCount() ? ALTextPos(last + 1, 0) : mDocument.lineEnd(last);
    }
    else
    {
        placeCaret(ALTextPos(caret_was.line, caret_was.column + caret_shift), false);
    }
    afterEdit();
    return true;
}

S32 ALTextView::screenTopOf(const LLRect& text, S32 line, S32 row)
{
    return text.mTop - (mLayout.lineTop(line) + row * mLayout.rowHeight() - mScrollY);
}

void ALTextView::forEachVisibleRow(const LLRect& text, const std::function<void(S32, S32, S32)>& visit)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return;
    }
    const S32 count    = mDocument.lineCount();
    const S32 bottom_y = mScrollY + text.getHeight();
    for (S32 line = mLayout.lineAtY(mScrollY); line < count; ++line)
    {
        if (mLayout.hidden(line))
        {
            continue;
        }
        const S32 rows = mLayout.rowCount(line);
        const S32 top  = mLayout.lineTop(line);
        if (top >= bottom_y)
        {
            break;
        }
        for (S32 r = 0; r < rows; ++r)
        {
            const S32 row_top = top + r * row_h;
            if (row_top + row_h <= mScrollY)
            {
                continue;
            }
            if (row_top >= bottom_y)
            {
                break;
            }
            visit(line, r, text.mTop - (row_top - mScrollY));
        }
    }
}

// --- LLEditMenuHandler ---------------------------------------------------------

void ALTextView::undo()
{
    if (mReadOnly)
    {
        return;
    }
    if (std::optional<ALTextPos> caret = mUndo.undo())
    {
        placeCaret(*caret, false);
        afterEdit();
    }
}

void ALTextView::redo()
{
    if (mReadOnly)
    {
        return;
    }
    if (std::optional<ALTextPos> caret = mUndo.redo())
    {
        placeCaret(*caret, false);
        afterEdit();
    }
}

void ALTextView::cut()
{
    if (!canCut())
    {
        return;
    }
    copy();
    deleteRange(selection());
}

void ALTextView::copy()
{
    if (!hasSelection())
    {
        return;
    }
    const std::string text = selectedText();
    LLClipboard::instance().copyToClipboard(text, 0, static_cast<S32>(text.size()));
}

bool ALTextView::canPaste() const
{
    return !mReadOnly && LLClipboard::instance().isTextAvailable();
}

void ALTextView::paste()
{
    if (!canPaste())
    {
        return;
    }
    std::string text;
    if (!LLClipboard::instance().pasteFromClipboard(text))
    {
        return;
    }
    mUndo.beginGroup();
    insertText(text);
    mUndo.endGroup();
}

void ALTextView::doDelete()
{
    if (canDoDelete())
    {
        deleteRange(selection());
    }
}

void ALTextView::selectAll()
{
    mAnchor   = mDocument.start();
    mCaret    = mDocument.end();
    mDesiredX = -1.f;
    mBlink.reset();
}

void ALTextView::deselect()
{
    mAnchor = mCaret;
}

// --- the input method ----------------------------------------------------------

void ALTextView::allowLanguageInput(bool allow)
{
    if (LLWindow* window = getWindow())
    {
        window->allowLanguageTextInput(this, allow);
    }
}

ALTextRange ALTextView::preeditRange() const
{
    return ALTextRange(mPreeditBegin, ALTextPos(mPreeditBegin.line, mPreeditBegin.column + mPreeditLength));
}

void ALTextView::resetPreedit()
{
    if (hasSelection() && !hasPreedit())
    {
        deleteRange(selection());
    }
    if (!hasPreedit())
    {
        return;
    }
    // Put back what was there: nothing, or what an overwrite took. None of
    // this is an edit the journal sees; the composition was never text.
    mDocument.replace(preeditRange(), mPreeditOverwritten);
    const ALTextPos begin = mPreeditBegin;
    mPreeditLength        = 0;
    mPreeditSegmentEnds.clear();
    mPreeditStandouts.clear();
    mPreeditOverwritten.clear();
    placeCaret(begin, false);
    mChanged();
}

void ALTextView::updatePreedit(std::string_view preedit_string, const segment_lengths_t& preedit_segment_lengths,
                               const standouts_t& preedit_standouts, S32 caret_position)
{
    if (mReadOnly)
    {
        return;
    }
    if (LLWindow* window = getWindow())
    {
        window->hideCursorUntilMouseMove();
    }
    resetPreedit();

    // A composition is one line of text; a newline in one would put the
    // rest of it where this cannot count it.
    std::string composed(preedit_string);
    std::replace(composed.begin(), composed.end(), '\n', ' ');

    const ALTextPos at = mCaret;
    ALTextPos       end = at;
    if (gKeyboard && LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
    {
        // As much of the line as the composition covers, in whole
        // characters, so the overwrite never ends inside one.
        const size_t want = composed.size();
        while (end.line == at.line && static_cast<size_t>(end.column - at.column) < want && end.column < mDocument.lineLength(at.line))
        {
            const ALTextPos next = mDocument.nextCluster(end);
            if (next.line != at.line || static_cast<size_t>(next.column - at.column) > want)
            {
                break;
            }
            end = next;
        }
        mPreeditOverwritten = mDocument.text(ALTextRange(at, end));
    }
    else
    {
        mPreeditOverwritten.clear();
    }
    mDocument.replace(ALTextRange(at, end), composed);

    mPreeditBegin  = at;
    mPreeditLength = static_cast<S32>(composed.size());
    mPreeditSegmentEnds.clear();
    S32 sum = 0;
    for (S32 length : preedit_segment_lengths)
    {
        sum += llmax(0, length);
        mPreeditSegmentEnds.push_back(llmin(sum, mPreeditLength));
    }
    if (mPreeditSegmentEnds.empty() || mPreeditSegmentEnds.back() < mPreeditLength)
    {
        mPreeditSegmentEnds.push_back(mPreeditLength);
    }
    mPreeditStandouts = preedit_standouts;
    while (mPreeditStandouts.size() < mPreeditSegmentEnds.size())
    {
        mPreeditStandouts.push_back(false);
    }

    placeCaret(ALTextPos(at.line, at.column + llclamp(caret_position, 0, mPreeditLength)), false);
    mDesiredX = -1.f;
    scrollToCaret();
    mChanged();
}

void ALTextView::markAsPreedit(S32 position, S32 length)
{
    if (hasPreedit())
    {
        LL_WARNS() << "markAsPreedit with a composition in progress" << LL_ENDL;
    }
    const ALTextPos begin = mDocument.posAt(static_cast<size_t>(llmax(0, position)));
    ALTextPos       end   = mDocument.posAt(static_cast<size_t>(llmax(0, position + length)));
    if (end.line != begin.line)
    {
        end = mDocument.lineEnd(begin.line);
    }
    deselect();
    placeCaret(begin, false);
    mPreeditSegmentEnds.clear();
    mPreeditStandouts.clear();
    mPreeditOverwritten.clear();
    mPreeditBegin  = begin;
    mPreeditLength = llmax(0, end.column - begin.column);
    if (mPreeditLength > 0)
    {
        mPreeditSegmentEnds.push_back(mPreeditLength);
        mPreeditStandouts.push_back(false);
        if (gKeyboard && LL_KIM_OVERWRITE == gKeyboard->getInsertMode())
        {
            mPreeditOverwritten = mDocument.text(preeditRange());
        }
    }
}

void ALTextView::getPreeditRange(S32* position, S32* length) const
{
    if (hasPreedit())
    {
        *position = static_cast<S32>(mDocument.offsetOf(mPreeditBegin));
        *length   = mPreeditLength;
    }
    else
    {
        *position = static_cast<S32>(mDocument.offsetOf(mCaret));
        *length   = 0;
    }
}

void ALTextView::getSelectionRange(S32* position, S32* length) const
{
    const ALTextRange range = selection().normalised();
    *position               = static_cast<S32>(mDocument.offsetOf(range.begin));
    *length                 = static_cast<S32>(mDocument.offsetOf(range.end)) - *position;
}

bool ALTextView::getPreeditLocation(S32 query_offset, LLCoordGL* coord, LLRect* bounds, LLRect* control) const
{
    const LLRect text = textRect();
    if (control)
    {
        LLRect screen;
        localRectToScreen(text, &screen);
        LLUI::getInstance()->screenRectToGL(screen, control);
    }
    const ALTextPos begin = hasPreedit() ? mPreeditBegin : mCaret;
    const ALTextPos query = query_offset >= 0 ? ALTextPos(begin.line, begin.column + query_offset) : mCaret;
    if (query.line != begin.line || query.column < begin.column || query.column > begin.column + mPreeditLength)
    {
        return false;
    }
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0)
    {
        return false;
    }
    S32       row;
    const F32 qx  = lay().xOf(query.line, query.column, &row);
    const S32 top = text.mTop - (lay().lineTop(query.line) + row * row_h - mScrollY);
    if (top > text.mTop || top - row_h < text.mBottom)
    {
        return false;
    }
    const F32 left = static_cast<F32>(text.mLeft) - mScrollX;
    if (coord)
    {
        S32 sx, sy;
        localPointToScreen(static_cast<S32>(left + qx), top - row_h / 2, &sx, &sy);
        LLUI::getInstance()->screenPointToGL(sx, sy, &coord->mX, &coord->mY);
    }
    if (bounds)
    {
        S32       row_begin, row_end;
        const F32 x0 = lay().xOf(begin.line, begin.column, &row_begin);
        F32       x1 = lay().xOf(begin.line, begin.column + mPreeditLength, &row_end);
        if (row_end != row_begin)
        {
            x1 = lay().line(begin.line).rows[row_begin].width;
        }
        LLRect local(static_cast<S32>(left + x0), top, static_cast<S32>(left + x1), top - row_h);
        LLRect screen;
        localRectToScreen(local, &screen);
        LLUI::getInstance()->screenRectToGL(screen, bounds);
    }
    return true;
}

S32 ALTextView::getPreeditFontSize() const
{
    return mFont ? ll_round(mFont->getLineHeight() * LLUI::getScaleFactor().mV[VY]) : 0;
}

const std::string& ALTextView::getPreeditStringUtf8() const
{
    if (!mWholeTextValid || mWholeTextVersion != mDocument.version())
    {
        mWholeText        = mDocument.text();
        mWholeTextVersion = mDocument.version();
        mWholeTextValid   = true;
    }
    return mWholeText;
}

// --- the context menu ------------------------------------------------------------

void ALTextView::showContextMenu(S32 x, S32 y)
{
    if (mContextMenuFile.empty() || !LLMenuGL::sMenuContainer)
    {
        return;
    }
    LLContextMenu* menu = mContextMenuHandle.get();
    if (!menu)
    {
        // The menu's actions name commands, and come back to whichever view
        // showed it; the view may be gone by then, so they hold a handle.
        const LLHandle<ALTextView>                          self = getDerivedHandle<ALTextView>();
        LLUICtrl::CommitCallbackRegistry::ScopedRegistrar   commit;
        LLUICtrl::EnableCallbackRegistry::ScopedRegistrar   enable;
        commit.add("TextView.Perform", [self](LLUICtrl*, const LLSD& param) {
            ALTextView* view = self.get();
            if (std::optional<ALEditorCommand> command = alEditorCommandFromName(param.asStringRef()); view && command)
            {
                view->perform(*command);
            }
        });
        enable.add("TextView.Enable", [self](LLUICtrl*, const LLSD& param) {
            ALTextView* view = self.get();
            if (std::optional<ALEditorCommand> command = alEditorCommandFromName(param.asStringRef()); view && command)
            {
                return view->canPerform(*command);
            }
            return false;
        });
        menu = LLUICtrlFactory::createFromFile<LLContextMenu>(mContextMenuFile, LLMenuGL::sMenuContainer,
                                                              LLMenuHolderGL::child_registry_t::instance());
        if (!menu)
        {
            LL_WARNS() << "No context menu from " << mContextMenuFile << " for " << getName() << LL_ENDL;
            return;
        }
        mContextMenuHandle = menu->getHandle();
    }
    gEditMenuHandler = this;
    S32 screen_x, screen_y;
    localPointToScreen(x, y, &screen_x, &screen_y);
    menu->show(screen_x, screen_y, this);
}

// --- drawing -------------------------------------------------------------------

const LLColor4& ALTextView::colorForKind(ALSyntaxKind kind) const
{
    return mKindColors[static_cast<size_t>(kind)].get();
}

const LLColor4& ALTextView::backgroundColor() const
{
    return mReadOnly ? mBgReadOnlyColor.get() : hasFocus() ? mBgFocusColor.get() : mBgColor.get();
}

void ALTextView::colorRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha)
{
    const size_t count = row.glyphEnd - row.glyphBegin;
    mColorScratch.resize(count);
    const LLColor4                    base   = textColor() % alpha;
    const std::vector<ALSyntaxToken>& tokens = mHighlighter.tokens(line);
    size_t                            t      = 0;
    for (size_t k = 0; k < count; ++k)
    {
        const S32 cluster = laid.glyphs[row.glyphBegin + k].cluster;
        while (t < tokens.size() && tokens[t].end <= cluster)
        {
            ++t;
        }
        if (t < tokens.size() && tokens[t].begin <= cluster && tokens[t].kind != ALSyntaxKind::Text)
        {
            mColorScratch[k] = LLColor4U(colorForKind(tokens[t].kind) % alpha);
        }
        else
        {
            mColorScratch[k] = LLColor4U(base);
        }
    }
}

void ALTextView::drawPreedit(S32 line, const ALTextLayout::Row& row, S32 screen_top, F32 left, F32 alpha)
{
    static LLUICachedControl<S32> marker_thickness("UIPreeditMarkerThickness", 1);
    static LLUICachedControl<S32> standout_thickness("UIPreeditStandoutThickness", 2);
    const S32      row_h = mLayout.rowHeight();
    const LLColor4 ink   = textColor() % alpha;
    S32            from  = mPreeditBegin.column;
    for (size_t i = 0; i < mPreeditSegmentEnds.size(); ++i)
    {
        const S32 to = mPreeditBegin.column + mPreeditSegmentEnds[i];
        const S32 lo = llmax(from, row.begin);
        const S32 hi = llmin(to, row.end);
        from         = to;
        if (lo >= hi)
        {
            continue;
        }
        const F32 x0        = mLayout.xOf(line, lo);
        const F32 x1        = mLayout.xOf(line, hi);
        const S32 thickness = llmax(1, static_cast<S32>(i < mPreeditStandouts.size() && mPreeditStandouts[i] ? standout_thickness : marker_thickness));
        const S32 y         = screen_top - row_h + 1;
        gl_rect_2d(static_cast<S32>(left + x0), y + thickness, static_cast<S32>(left + x1), y, ink);
    }
}

void ALTextView::drawRows(const LLRect& text)
{
    const S32 row_h = mLayout.rowHeight();
    if (row_h <= 0 || !mFont)
    {
        return;
    }
    const F32  alpha       = getDrawContext().mAlpha;
    const S32  ascent      = llround(mFont->getAscenderHeight());
    const S32  count       = mDocument.lineCount();
    const S32  bottom_y    = mScrollY + text.getHeight();
    const bool show_caret  = hasFocus() && gFocusMgr.getAppHasFocus() && !mReadOnly;
    const F32  blink       = mBlink.getElapsedTimeF32();
    const bool caret_on    = show_caret && (blink < BLINK_DELAY || (static_cast<S32>(blink * 2.f) & 1));
    const ALTextRange sel  = selection().normalised();
    const bool has_sel     = !sel.empty();
    S32        caret_row   = 0;
    const F32  caret_x     = mLayout.xOf(mCaret.line, mCaret.column, &caret_row);
    const F32  space       = mLayout.xOf(0, 0) + 6.f;  // what a selected line end is drawn as

    for (S32 line = mLayout.lineAtY(mScrollY); line < count; ++line)
    {
        if (mLayout.hidden(line))
        {
            continue;
        }
        const ALTextLayout::Line& laid = mLayout.line(line);
        const S32                 top  = mLayout.lineTop(line);
        if (top >= bottom_y)
        {
            break;
        }
        const S32 length = mDocument.lineLength(line);
        for (size_t r = 0; r < laid.rows.size(); ++r)
        {
            const S32 row_top = top + static_cast<S32>(r) * row_h;
            if (row_top + row_h <= mScrollY)
            {
                continue;
            }
            if (row_top >= bottom_y)
            {
                break;
            }
            const ALTextLayout::Row& row        = laid.rows[r];
            const S32                screen_top = text.mTop - (row_top - mScrollY);
            const F32                left       = static_cast<F32>(text.mLeft) - mScrollX;

            // The selection behind the row.
            if (has_sel && sel.begin.line <= line && line <= sel.end.line)
            {
                const S32 sel_begin = sel.begin.line < line ? 0 : sel.begin.column;
                const S32 sel_end   = sel.end.line > line ? length + 1 : sel.end.column;
                const S32 lo        = llmax(sel_begin, row.begin);
                const bool last_row = (r + 1 == laid.rows.size());
                const S32 hi        = llmin(sel_end, last_row ? length + 1 : row.end);
                if (lo < hi)
                {
                    const F32 x0 = mLayout.xOf(line, lo) ;
                    const F32 x1 = hi > length ? row.width + space : (hi >= row.end && !last_row ? row.width : mLayout.xOf(line, hi));
                    gl_rect_2d(static_cast<S32>(left + x0), screen_top, static_cast<S32>(left + x1), screen_top - row_h, mSelectionColor.get() % alpha);
                }
            }

            // The glyphs.
            const size_t glyph_count = row.glyphEnd - row.glyphBegin;
            if (glyph_count)
            {
                colorRow(line, laid, row, alpha);
                mFont->renderGlyphs(&laid.placed[row.glyphBegin], mColorScratch.data(), glyph_count,
                                    left - row.xStart, static_cast<F32>(screen_top - ascent));
            }

            if (hasPreedit() && line == mPreeditBegin.line)
            {
                drawPreedit(line, row, screen_top, left, alpha);
            }

            drawRowExtras(line, static_cast<S32>(r), text, screen_top, left, alpha);

            // The caret.
            if (caret_on && line == mCaret.line && static_cast<S32>(r) == caret_row)
            {
                const S32 x = static_cast<S32>(left + caret_x);
                gl_rect_2d(x, screen_top, x + CARET_WIDTH, screen_top - row_h, mCursorColor.get() % alpha);
            }
        }
    }
}

void ALTextView::draw()
{
    syncScrollbar();
    const F32 alpha = getDrawContext().mAlpha;
    if (mBgVisible)
    {
        gl_rect_2d(getLocalRect(), backgroundColor() % alpha);
    }
    const LLRect text = textRect();
    drawBeforeRows(text);
    {
        LLLocalClipRect clip(text);
        drawRows(text);
    }
    LLUICtrl::draw();
}

// --- input ---------------------------------------------------------------------

bool ALTextView::handleKeyHere(KEY key, MASK mask)
{
    const ALEditorCommand command = mKeymap.lookup(key, mask);
    if (command == ALEditorCommand::None)
    {
        return false;
    }
    if (mReadOnly && editsText(command))
    {
        // Somebody else's: a Tab moves on, a Return closes.
        return false;
    }
    if (!perform(command))
    {
        return false;
    }
    mBlink.reset();
    return true;
}

bool ALTextView::handleUnicodeCharHere(llwchar uni_char)
{
    if (mReadOnly || uni_char < 0x20 || uni_char == 0x7F)
    {
        return false;
    }
    insertText(utf8str_from_cp(uni_char));
    if (LLWindow* window = getWindow())
    {
        window->hideCursorUntilMouseMove();
    }
    return true;
}

bool ALTextView::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (LLUICtrl::handleMouseDown(x, y, mask))
    {
        return true;
    }
    setFocus(true);
    if (!mTripleClick.hasExpired())
    {
        // The third click takes the line.
        mAnchor    = mDocument.lineStart(mCaret.line);
        mCaret     = mCaret.line + 1 < mDocument.lineCount() ? mDocument.lineStart(mCaret.line + 1) : mDocument.lineEnd(mCaret.line);
        mSelecting = false;
        return true;
    }
    placeCaret(posAtLocal(x, y, true), (mask & MASK_SHIFT) != 0);
    mDesiredX  = -1.f;
    mSelecting = true;
    gFocusMgr.setMouseCapture(this);
    return true;
}

bool ALTextView::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    if (mContextMenuFile.empty() || !textRect().pointInRect(x, y))
    {
        return LLUICtrl::handleRightMouseDown(x, y, mask);
    }
    setFocus(true);
    // The click puts the caret where it landed, unless it landed in the
    // selection, which is what the menu is then about.
    const ALTextPos   at  = posAtLocal(x, y, true);
    const ALTextRange sel = selection().normalised();
    if (!hasSelection() || at < sel.begin || sel.end < at)
    {
        placeCaret(at, false);
        mDesiredX = -1.f;
    }
    showContextMenu(x, y);
    return true;
}

bool ALTextView::handleHover(S32 x, S32 y, MASK mask)
{
    if (mSelecting && hasMouseCapture())
    {
        const LLRect text  = textRect();
        const S32    row_h = mLayout.rowHeight();
        if (y > text.mTop)
        {
            setScrollY(mScrollY - row_h);
        }
        else if (y < text.mBottom)
        {
            setScrollY(mScrollY + row_h);
        }
        placeCaret(posAtLocal(x, y, true), true);
        mDesiredX = -1.f;
        return true;
    }
    if (textRect().pointInRect(x, y))
    {
        if (LLWindow* window = getWindow())
        {
            window->setCursor(UI_CURSOR_IBEAM);
        }
        return true;
    }
    return LLUICtrl::handleHover(x, y, mask);
}

bool ALTextView::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mSelecting)
    {
        mSelecting = false;
        gFocusMgr.setMouseCapture(nullptr);
        return true;
    }
    return LLUICtrl::handleMouseUp(x, y, mask);
}

bool ALTextView::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    if (LLUICtrl::handleDoubleClick(x, y, mask))
    {
        return true;
    }
    setFocus(true);
    const ALTextRange word = mDocument.wordAt(posAtLocal(x, y, false));
    mAnchor                = word.begin;
    mCaret                 = word.end;
    mDesiredX              = -1.f;
    mSelecting             = false;
    mTripleClick.setTimerExpirySec(TRIPLE_CLICK_INTERVAL);
    return true;
}

bool ALTextView::handleScrollWheel(S32 x, S32 y, LLScrollDelta delta)
{
    setScrollY(mScrollY + delta.mClicks * WHEEL_ROWS * mLayout.rowHeight());
    return true;
}

bool ALTextView::handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta)
{
    if (mWordWrap)
    {
        return false;
    }
    setScrollX(mScrollX + static_cast<F32>(delta.mClicks * WHEEL_ROWS * mLayout.rowHeight()));
    return true;
}

void ALTextView::onMouseCaptureLost()
{
    mSelecting = false;
}

void ALTextView::setFocus(bool focus)
{
    LLUICtrl::setFocus(focus);
    if (focus)
    {
        gEditMenuHandler   = this;
        mChangedSinceFocus = false;
        mBlink.reset();
        allowLanguageInput(!mReadOnly);
    }
    else
    {
        allowLanguageInput(false);
        if (gEditMenuHandler == this)
        {
            gEditMenuHandler = nullptr;
        }
    }
}

void ALTextView::onFocusLost()
{
    allowLanguageInput(false);
    if (mChangedSinceFocus)
    {
        mChangedSinceFocus = false;
        onCommit();
    }
    LLUICtrl::onFocusLost();
}
