/**
 * @file altextview.h
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

#pragma once

#include "alkeymap.h"
#include "alsyntaxhighlighter.h"
#include "altextdocument.h"
#include "altextlayout.h"
#include "altextundo.h"
#include "lleditmenuhandler.h"
#include "llframetimer.h"
#include "llpreeditor.h"
#include "lluicolor.h"
#include "lluictrl.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class LLContextMenu;
class LLScrollbar;

// A view of a document: the lines laid out and drawn, only the ones in
// sight, a caret and a selection in them, a keymap that turns keys into
// commands, and a grammar that colours what it shows. Text is drawn from
// glyph runs the layout shaped once, so a frame costs the rows on screen
// and nothing that is not. The document, the undo journal, the highlighter
// and the layout are its own, and reachable, for whatever is built over it
// -- the code editor first.
//
// An input method composes into it through LLPreeditor, as the legacy
// editors do; a right click shows the menu its file names; lines a
// subclass hides (folding) take no room and the caret passes over them.
//
// What is not here yet: spell check, atoms and display substitutions.
class ALTextView : public LLUICtrl, public LLEditMenuHandler, protected LLPreeditor
{
public:
    AL_VIEW_TYPE(ALTextView, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<LLUIColor>   text_color;
        Optional<LLUIColor>   text_readonly_color;
        Optional<LLUIColor>   bg_color;
        Optional<LLUIColor>   bg_readonly_color;
        Optional<LLUIColor>   cursor_color;
        Optional<LLUIColor>   selection_color;
        Optional<bool>        bg_visible;
        Optional<bool>        read_only;
        Optional<bool>        word_wrap;
        // A tab typed is spaces to the next stop rather than a tab.
        Optional<bool>        soft_tabs;
        Optional<S32>         tab_width;
        Optional<S32>         h_pad;
        Optional<S32>         v_pad;
        // The grammar to colour by, by the name in its file.
        Optional<std::string> syntax;
        Optional<std::string> default_text;
        // The file the right-click menu is built from; none for no menu.
        Optional<std::string> context_menu;

        Params();
    };

    ~ALTextView() override;

    // --- the text ------------------------------------------------------------

    void        setText(std::string_view text);
    std::string text() const { return mDocument.text(); }
    void        setValue(const LLSD& value) override;
    LLSD        getValue() const override;

    ALTextDocument&            document() { return mDocument; }
    const ALTextDocument&      document() const { return mDocument; }
    ALTextLayout&              layout() { return mLayout; }
    ALSyntaxHighlighter&       highlighter() { return mHighlighter; }
    const ALSyntaxHighlighter& highlighter() const { return mHighlighter; }
    ALTextUndo&                undoJournal() { return mUndo; }

    // The grammars on disk, read the first time anything asks.
    static ALSyntaxLibrary& syntaxLibrary();
    void                    setGrammar(std::shared_ptr<const ALSyntaxGrammar> grammar);
    void                    setSyntax(std::string_view grammar_name);

    void            setFont(const LLFontGL* font);
    const LLFontGL* getFont() const override { return mFont; }
    void            setReadOnly(bool read_only);
    bool            isReadOnly() const { return mReadOnly; }
    void            setWordWrap(bool wrap);
    bool            getWordWrap() const { return mWordWrap; }
    void            setTabWidth(S32 spaces);
    S32             getTabWidth() const { return mTabWidth; }
    void            setSoftTabs(bool soft) { mSoftTabs = soft; }
    // Whether the text has changed since it was set or saved.
    bool            isDirty() const override { return !mUndo.isPristine(); }
    void            resetDirty() override { mUndo.markSaved(); }

    // --- the caret and the selection -----------------------------------------

    const ALTextPos& caret() const { return mCaret; }
    // The selection runs from the anchor to the caret; extending keeps the
    // anchor where it is.
    void        setCaret(ALTextPos pos, bool extend = false);
    ALTextRange selection() const { return ALTextRange(mAnchor, mCaret); }
    bool        hasSelection() const { return mAnchor != mCaret; }
    void        setSelection(const ALTextRange& range);
    std::string selectedText() const { return mDocument.text(selection()); }
    // The identifier the caret is at the end of: letters, digits and
    // underscores back from the caret. Empty at anything else.
    std::string wordBeforeCaret() const;

    // --- editing, through the undo journal -----------------------------------

    // In place of the selection, or at the caret.
    void insertText(std::string_view text);
    void deleteRange(const ALTextRange& range);
    bool perform(ALEditorCommand command);
    // Whether a command would do anything now: what a menu asks.
    bool canPerform(ALEditorCommand command) const;
    ALKeymap&       keymap() { return mKeymap; }
    const ALKeymap& keymap() const { return mKeymap; }

    // --- scrolling -----------------------------------------------------------

    void scrollToCaret();
    void scrollToLine(S32 line);
    S32  firstVisibleLine();
    S32  scrollY() const { return mScrollY; }
    void setScrollY(S32 y);
    F32  scrollX() const { return mScrollX; }
    void setScrollX(F32 x);
    bool hasHorizontalScrollbar() const;

    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onTextChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

    // --- the input method ------------------------------------------------------

    bool        hasPreedit() const { return mPreeditLength > 0; }
    ALTextRange preeditRange() const;
    // The view as what a window composes into. The window is handed it
    // when the view takes focus; a test hands itself.
    LLPreeditor& preeditor() { return *this; }

    // --- the context menu ------------------------------------------------------

    void showContextMenu(S32 x, S32 y);

    // --- LLEditMenuHandler ---------------------------------------------------

    LLView* asView() override { return this; }
    void    undo() override;
    bool    canUndo() const override { return !mReadOnly && mUndo.canUndo(); }
    void    redo() override;
    bool    canRedo() const override { return !mReadOnly && mUndo.canRedo(); }
    void    cut() override;
    bool    canCut() const override { return !mReadOnly && hasSelection(); }
    void    copy() override;
    bool    canCopy() const override { return hasSelection(); }
    void    paste() override;
    bool    canPaste() const override;
    void    doDelete() override;
    bool    canDoDelete() const override { return !mReadOnly && hasSelection(); }
    void    selectAll() override;
    bool    canSelectAll() const override { return !mDocument.empty(); }
    void    deselect() override;
    bool    canDeselect() const override { return hasSelection(); }

    // --- LLView --------------------------------------------------------------

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleRightMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleScrollWheel(S32 x, S32 y, LLScrollDelta delta) override;
    bool handleScrollHWheel(S32 x, S32 y, LLScrollDelta delta) override;
    void onMouseCaptureLost() override;
    void setFocus(bool focus) override;
    void onFocusLost() override;
    bool acceptsTextInput() const override { return !mReadOnly; }

    // The rows a page holds, and the rect the text is drawn in.
    S32    rowsPerPage() const;
    LLRect textRect() const;
    // The position under a point of the view, on a cluster boundary.
    ALTextPos posAtLocal(S32 x, S32 y, bool round);
    // Comments the selected lines out with the grammar's line comment, or
    // back in where they all are. False without a grammar that has one.
    bool toggleComment();

protected:
    friend class LLUICtrlFactory;
    ALTextView(const Params& p);

    // What a subclass adds to the picture: room at the left of the text
    // for a gutter, whatever it draws there and under the rows before they
    // are drawn, and whatever it draws over each row after its glyphs.
    virtual S32  leftInset() const { return 0; }
    virtual void drawBeforeRows(const LLRect& text) {}
    virtual void drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha) {}
    // What a subclass does about folding: the caret has landed on a
    // hidden line and it must be seen; a fold command was given; whether
    // one could be.
    virtual void revealLine(S32 line) { mLayout.setHidden(line, line, false); }
    virtual bool performFold(ALEditorCommand command) { return false; }
    virtual bool canFold(ALEditorCommand command) const { return false; }
    // Completion was asked for.
    virtual bool complete() { return false; }
    // The screen y of the top of a line's row, and every row on screen in
    // turn, for a subclass drawing beside them.
    S32  screenTopOf(const LLRect& text, S32 line, S32 row);
    void forEachVisibleRow(const LLRect& text, const std::function<void(S32 line, S32 row, S32 screen_top)>& visit);
    // The lines a selection covers, as commands over whole lines count them.
    std::pair<S32, S32>  selectedLines() const;
    // Every change goes through here: the document, the journal, the
    // caret, and whoever is listening.
    ALTextDocument::Edit edit(const ALTextRange& range, std::string_view text);
    void                 afterEdit();
    void                 placeCaret(const ALTextPos& pos, bool extend);

    // --- LLPreeditor ---------------------------------------------------------

    void resetPreedit() override;
    void updatePreedit(std::string_view preedit_string, const segment_lengths_t& preedit_segment_lengths,
                       const standouts_t& preedit_standouts, S32 caret_position) override;
    void markAsPreedit(S32 position, S32 length) override;
    void getPreeditRange(S32* position, S32* length) const override;
    void getSelectionRange(S32* position, S32* length) const override;
    bool getPreeditLocation(S32 query_offset, LLCoordGL* coord, LLRect* bounds, LLRect* control) const override;
    S32  getPreeditFontSize() const override;
    const std::string& getPreeditStringUtf8() const override;

private:
    void                 moveVertically(S32 rows, bool extend);
    std::string          tabText(const ALTextPos& at) const;
    void                 indentLines(bool in);
    void                 duplicateLines();
    void                 moveLines(S32 direction);
    void                 deleteLines();
    void                 allowLanguageInput(bool allow);

    void syncScrollbar();
    void onScrollChange(S32 pos, LLScrollbar* bar);
    void drawRows(const LLRect& text);
    void drawPreedit(S32 line, const ALTextLayout::Row& row, S32 screen_top, F32 left, F32 alpha);
    void colorRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha);
    const LLColor4& colorForKind(ALSyntaxKind kind) const;
    // Laying out is a cache fill, which a const query may cause.
    ALTextLayout& lay() const { return const_cast<ALTextLayout&>(mLayout); }

    ALTextDocument      mDocument;
    ALTextUndo          mUndo;
    ALSyntaxHighlighter mHighlighter;
    ALTextLayout        mLayout;
    ALKeymap            mKeymap;
    LLScrollbar*        mScrollbar  = nullptr;
    LLScrollbar*        mHScrollbar = nullptr;
    const LLFontGL*     mFont       = nullptr;

    LLUIColor mTextColor;
    LLUIColor mTextReadOnlyColor;
    LLUIColor mBgColor;
    LLUIColor mBgReadOnlyColor;
    LLUIColor mCursorColor;
    LLUIColor mSelectionColor;
    std::array<LLUIColor, static_cast<size_t>(ALSyntaxKind::COUNT)> mKindColors;

    bool mBgVisible = true;
    bool mReadOnly  = false;
    bool mWordWrap  = false;
    bool mSoftTabs  = false;
    S32  mTabWidth  = 4;
    S32  mHPad      = 4;
    S32  mVPad      = 2;

    ALTextPos mCaret;
    ALTextPos mAnchor;
    // The x the caret wants when it moves between rows, or negative.
    F32          mDesiredX = -1.f;
    S32          mScrollY  = 0;
    F32          mScrollX  = 0.f;
    bool         mSelecting = false;
    LLFrameTimer mBlink;
    LLFrameTimer mTripleClick;
    bool         mChangedSinceFocus = false;

    // The composition, where one is in progress: where it starts, how many
    // bytes of it there are, where each clause ends counted from its
    // start, which clauses stand out, and what it overwrote.
    ALTextPos        mPreeditBegin;
    S32              mPreeditLength = 0;
    std::vector<S32> mPreeditSegmentEnds;
    standouts_t      mPreeditStandouts;
    std::string      mPreeditOverwritten;
    // The whole text, joined for the input method when it asks.
    mutable std::string mWholeText;
    mutable U32         mWholeTextVersion = 0;
    mutable bool        mWholeTextValid   = false;

    std::string             mContextMenuFile;
    LLHandle<LLContextMenu> mContextMenuHandle;

    std::vector<LLColor4U> mColorScratch;
    changed_signal_t       mChanged;
};
