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

#include "llclipboard.h"
#include "lldir.h"
#include "llfocusmgr.h"
#include "lllocalcliprect.h"
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
            case ALEditorCommand::ToggleComment:
                return true;
            default:
                return false;
        }
    }
}

ALTextView::Params::Params()
:   text_color("text_color"),
    text_readonly_color("text_readonly_color"),
    bg_color("bg_color"),
    bg_readonly_color("bg_readonly_color"),
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
    default_text("default_text")
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
    mCursorColor(p.cursor_color),
    mSelectionColor(p.selection_color),
    mBgVisible(p.bg_visible),
    mReadOnly(p.read_only),
    mWordWrap(p.word_wrap),
    mSoftTabs(p.soft_tabs),
    mHPad(p.h_pad),
    mVPad(p.v_pad)
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
    LLScrollbar::Params           bar;
    bar.name("scrollbar");
    bar.rect(LLRect(getRect().getWidth() - scrollbar_size, getRect().getHeight(), getRect().getWidth(), 0));
    bar.orientation(LLScrollbar::VERTICAL);
    bar.doc_size(0);
    bar.doc_pos(0);
    bar.page_size(1);
    bar.change_callback(boost::bind(&ALTextView::onScrollChange, this, _1, _2));
    bar.follows.flags(FOLLOWS_RIGHT | FOLLOWS_TOP | FOLLOWS_BOTTOM);
    bar.visible(false);
    mScrollbar = LLUICtrlFactory::create<LLScrollbar>(bar);
    addChild(mScrollbar);

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
    if (gEditMenuHandler == this)
    {
        gEditMenuHandler = nullptr;
    }
}

// --- the text ----------------------------------------------------------------

void ALTextView::setText(std::string_view text)
{
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

void ALTextView::syncScrollbar()
{
    if (!mScrollbar)
    {
        return;
    }
    const S32  page   = llmax(1, textRect().getHeight());
    const S32  total  = mLayout.totalHeight();
    const bool needed = total > page;
    if (mScrollbar->getVisible() != needed)
    {
        mScrollbar->setVisible(needed);
        if (mWordWrap)
        {
            mLayout.setWrapWidth(textRect().getWidth());
        }
    }
    mScrollY = llclamp(mScrollY, 0, llmax(0, mLayout.totalHeight() - page));
    mScrollbar->setDocSize(mLayout.totalHeight());
    mScrollbar->setPageSize(page);
    mScrollbar->setDocPos(mScrollY);
}

void ALTextView::onScrollChange(S32 pos, LLScrollbar*)
{
    mScrollY = pos;
}

void ALTextView::setScrollY(S32 y)
{
    mScrollY = llmax(0, y);
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
        const F32 width  = static_cast<F32>(llmax(1, text.getWidth()));
        const F32 margin = 8.f;
        if (x < mScrollX + margin)
        {
            mScrollX = llmax(0.f, x - margin);
        }
        else if (x + margin > mScrollX + width)
        {
            mScrollX = x + margin - width;
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
    mAnchor   = mDocument.clamp(range.begin);
    mCaret    = mDocument.clamp(range.end);
    mDesiredX = -1.f;
    mBlink.reset();
    scrollToCaret();
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
        else if (line > 0)
        {
            --line;
            row = mLayout.rowCount(line) - 1;
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
        else if (line + 1 < mDocument.lineCount())
        {
            ++line;
            row = 0;
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
            if (mReadOnly)
            {
                return false;
            }
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
            if (mReadOnly)
            {
                return false;
            }
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
            if (mReadOnly)
            {
                return false;
            }
            deleteRange(hasSelection() ? selection() : ALTextRange(mDocument.prevWord(mCaret), mCaret));
            return true;
        case C::DeleteWordRight:
            if (mReadOnly)
            {
                return false;
            }
            deleteRange(hasSelection() ? selection() : ALTextRange(mCaret, mDocument.nextWord(mCaret)));
            return true;
        case C::NewLine:
        {
            if (mReadOnly)
            {
                return false;
            }
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
            if (mReadOnly)
            {
                return false;
            }
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
            if (mReadOnly)
            {
                return false;
            }
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
        case C::ToggleComment:
            return toggleComment();
    }
    return false;
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

// --- drawing -------------------------------------------------------------------

const LLColor4& ALTextView::colorForKind(ALSyntaxKind kind) const
{
    return mKindColors[static_cast<size_t>(kind)].get();
}

void ALTextView::colorRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha)
{
    const size_t count = row.glyphEnd - row.glyphBegin;
    mColorScratch.resize(count);
    const LLColor4                    base   = (mReadOnly ? mTextReadOnlyColor : mTextColor).get() % alpha;
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
        gl_rect_2d(getLocalRect(), (mReadOnly ? mBgReadOnlyColor : mBgColor).get() % alpha);
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
    mScrollX = llmax(0.f, mScrollX + static_cast<F32>(delta.mClicks * WHEEL_ROWS * mLayout.rowHeight()));
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
    }
    else if (gEditMenuHandler == this)
    {
        gEditMenuHandler = nullptr;
    }
}

void ALTextView::onFocusLost()
{
    if (mChangedSinceFocus)
    {
        mChangedSinceFocus = false;
        onCommit();
    }
    LLUICtrl::onFocusLost();
}
