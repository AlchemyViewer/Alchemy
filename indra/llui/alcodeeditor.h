/**
 * @file alcodeeditor.h
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

#pragma once

#include "altextview.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class LLScrollListCtrl;

// The text view configured for code: a gutter with line numbers, a mark
// per line for what an analyzer said about it and a marker per block that
// folds, a band under the caret's row, the bracket the caret is at and its
// match boxed, decorations over ranges -- a squiggle under a problem, a
// wash behind a find -- the places a name stands lit, and a list of
// completions under the caret.
//
// Blocks are found by indentation, which every language here writes by,
// with a brace or an `end` on the line after a block taken as part of it;
// an analyzer that knows better will hand over regions in phase 2.
// Completions come from whoever is set as the provider, and until someone
// is, from the grammar's vocabulary and the words in the document.
class ALCodeEditor : public ALTextView
{
public:
    AL_VIEW_TYPE(ALCodeEditor, ALTextView);

    struct Params : public LLInitParam::Block<Params, ALTextView::Params>
    {
        Optional<bool>      show_line_numbers;
        Optional<bool>      show_fold_markers;
        Optional<bool>      highlight_current_line;
        Optional<bool>      match_brackets;
        // The list opens on its own once an identifier is two letters in.
        Optional<bool>      auto_complete;
        // The gutter, its numbers, the band under the caret's row and the
        // fold markers are mixed from the text's own background and
        // colour unless a skin names them, so they follow the theme and
        // the focus as the text does.
        Optional<LLUIColor> gutter_color;
        Optional<LLUIColor> line_number_color;
        Optional<LLUIColor> current_line_color;
        Optional<LLUIColor> bracket_match_color;
        Optional<LLUIColor> fold_color;
        // Behind every place a name stands, once its references were
        // asked for; mixed from the text's unless a skin names it.
        Optional<LLUIColor> highlight_color;
        // The bar beside a line changed since the text was last saved.
        Optional<LLUIColor> changed_color;
        // Brackets by depth, one colour per level, round again after the
        // last; none given leaves brackets in the punctuation colour.
        Optional<LLUIColor> bracket_color_1, bracket_color_2, bracket_color_3;

        Params();
    };

    ~ALCodeEditor() override;

    // What the gutter says about a line: the worst of what is on it.
    enum class Mark : U8
    {
        None,
        Note,
        Warning,
        Error,
        Runtime,
        COUNT
    };

    void setMark(S32 line, Mark mark);
    Mark markAt(S32 line) const;
    void clearMarks();

    // Something drawn over a range: a squiggle under it, or a wash behind
    // it. Replaced whole by whoever computes them; an edit slides the ones
    // below it and drops the ones it cuts through.
    struct Decoration
    {
        enum class Style : U8
        {
            Squiggle,
            Background
        };
        ALTextRange range;
        Style       style = Style::Squiggle;
        LLColor4    color;
        // Shown when the mouse rests on it, where there is something to say.
        std::string message;
    };
    void                           setDecorations(std::vector<Decoration> decorations);
    const std::vector<Decoration>& decorations() const { return mDecorations; }

    // The places a name stands, washed over until they are cleared or
    // the caret leaves them all; an edit slides them as it does the
    // decorations.
    void                            setHighlights(std::vector<ALTextRange> ranges);
    void                            clearHighlights() { mHighlights.clear(); }
    const std::vector<ALTextRange>& highlights() const { return mHighlights; }
    // Whether a position is on one of them.
    bool                            highlighted(const ALTextPos& at) const;

    // The bracket the caret is at -- just before it, or under it -- and
    // its match, skipping what is inside strings and comments. False where
    // the caret is at no bracket, or the bracket has no match.
    bool matchingBrackets(ALTextPos& open, ALTextPos& close);

    void setShowLineNumbers(bool show);
    bool getShowLineNumbers() const { return mShowLineNumbers; }
    // Whether a line was changed since the text was last saved: the
    // gutter bars it, and a save clears them all.
    bool lineChanged(S32 line) const;
    void resetDirty() override;
    void setShowFoldMarkers(bool show);
    bool getShowFoldMarkers() const { return mShowFoldMarkers; }
    // A faint line down each level of indentation, so that a block's
    // extent is seen without counting spaces.
    void setShowIndentGuides(bool show) { mShowIndentGuides = show; }
    bool getShowIndentGuides() const { return mShowIndentGuides; }
    // Line numbers counted from the caret's line, which is numbered as
    // itself: what a jump of so many lines reads off.
    void setRelativeLineNumbers(bool relative) { mRelativeLineNumbers = relative; }
    bool getRelativeLineNumbers() const { return mRelativeLineNumbers; }
    // Each pair of brackets in the colour of its depth, so that which
    // closes which is read by colour.
    void setColorBrackets(bool color) { mColorBrackets = color; }
    bool getColorBrackets() const { return mColorBrackets; }
    // The line that opens the block the top of the view is inside,
    // pinned at the top while the block runs on below: the function, the
    // state, the event, the loop, outermost first, up to a few.
    void setStickyHeaders(bool sticky) { mStickyHeaders = sticky; }
    bool getStickyHeaders() const { return mStickyHeaders; }
    S32  gutterWidth() const;

    // Zero-based; the caret goes to the start of the line and the line
    // comes into view.
    void goToLine(S32 line);

    // --- folding ---------------------------------------------------------------

    // A block: the line it starts on stays in sight, the lines through
    // `end` go when it is folded.
    struct FoldRegion
    {
        S32 start = 0;
        S32 end   = 0;
    };
    // Every block in the text, by start line, found again when the text
    // has changed.
    const std::vector<FoldRegion>& foldRegions();
    // The block that starts at the line, else the innermost one around
    // it. False where there is none, or it is already that way.
    bool foldAt(S32 line);
    bool unfoldAt(S32 line);
    void foldAll();
    void unfoldAll();
    // Whether a folded block starts at the line.
    bool isFolded(S32 line) const;

    // --- completion --------------------------------------------------------------

    struct Completion
    {
        std::string  text;
        std::string  detail;
        ALSyntaxKind kind = ALSyntaxKind::Text;
        // A snippet rather than a word: accepting it puts this body in
        // place of the prefix, with its placeholders to tab through.
        std::string  snippet;
    };
    // Asked for what could go at a position, given the identifier typed
    // so far; answers into `out`, already narrowed to the prefix. The
    // words of the document itself are added after whatever it answers,
    // and the whole is ordered: the case typed first, then the alphabet.
    typedef std::function<void(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out)> completion_provider_t;
    void setCompletionProvider(completion_provider_t provider) { mProvider = std::move(provider); }
    // Told when the list opens at a new identifier, for whoever answers
    // later: an analyzer on another thread. What it answers comes back
    // through supplyCompletions, and is merged in if the list is still
    // about the same identifier.
    typedef std::function<void(const ALTextPos& at, std::string_view prefix)> completion_request_t;
    void setCompletionRequest(completion_request_t request) { mCompletionRequest = std::move(request); }
    void supplyCompletions(const ALTextPos& at, std::vector<Completion> more);
    bool completionOpen() const;
    void closeCompletion();
    // Puts the chosen completion in place of the prefix; a function comes
    // with its brackets, the caret between them where it takes anything,
    // and the call's signature asked for. False with none open.
    bool                           acceptCompletion();
    const std::vector<Completion>& completions() const { return mCompletions; }
    S32                            chosenCompletion() const;
    // What accepting does, for whoever has a completion in hand without
    // the list: the completion in place of the range.
    void complete(const Completion& chosen, const ALTextRange& range);

    // --- snippets --------------------------------------------------------------------

    // A body in place of the selection, or at the caret: its lines after
    // the first indented as the caret's line is; `${1:text}`, `${1}` and
    // `$1` its placeholders, tabbed through in order of their numbers,
    // and `$0` where the caret lands past the last; `$$` a dollar.
    void insertSnippet(std::string_view body);

    // --- the name at the caret -----------------------------------------------------

    // Asked, on F12, shift-F12 or F2, or the menu, to find the definition
    // or the references of the identifier the caret is on, or to rename
    // it: the command, and the identifier's range. Whoever answers goes
    // there, lights the places, or asks for the new name and calls
    // replaceAll. Nothing is asked with the caret on no identifier.
    typedef std::function<void(ALEditorCommand command, const ALTextRange& word)> symbol_request_t;
    void setSymbolRequest(symbol_request_t request) { mSymbolRequest = std::move(request); }
    // The identifier at a position -- letters, digits and underscores,
    // which is narrower than a word to the document, where a dot between
    // letters joins them as it does in "e.g." -- or an empty range. The
    // caret's is the one it is on or at the end of.
    ALTextRange identifierAt(const ALTextPos& pos) const;
    ALTextRange identifierAtCaret() const;

    // --- placeholders ----------------------------------------------------------------

    // The parameters of a call just completed, each a stretch to type
    // over: the first is selected, tab selects the next and shift-tab the
    // one before, past the last the caret lands after the call, and
    // escape or a click lets them go. Typing over one keeps it as what
    // was typed until tab leaves it.
    const std::vector<ALTextRange>& placeholders() const { return mPlaceholders; }
    S32                             placeholderAt() const { return mPlaceholderAt; }
    void                            setPlaceholders(std::vector<ALTextRange> ranges, const ALTextPos& after);
    bool                            nextPlaceholder(S32 direction);
    void                            clearPlaceholders();
    // The names of a signature's parameters, from how a completion's
    // detail reads: "integer llSay(integer channel, string msg)" or
    // "(channel: number, msg: string) -> ()". The list is the bracket
    // after the name where the detail has the name -- a return type may
    // have brackets of its own before it -- else the first.
    static std::vector<std::string> parameterNames(std::string_view detail, std::string_view name = std::string_view());
    // Where that list opens in the detail, or npos.
    static size_t parameterListAt(std::string_view detail, std::string_view name);

    // --- hover -------------------------------------------------------------------

    // Asked what to say about the word the mouse rests on; answers into
    // `text` and true where there is something. A decoration's message
    // comes first where the mouse is on one.
    typedef std::function<bool(const ALTextPos& at, std::string_view word, std::string& text)> hover_provider_t;
    void setHoverProvider(hover_provider_t provider) { mHover = std::move(provider); }
    // Asked when the provider had nothing to say, for an answer that
    // comes later through supplyHover; shown if the mouse is still there.
    typedef std::function<void(const ALTextPos& at, std::string_view word)> hover_request_t;
    void setHoverRequest(hover_request_t request) { mHoverRequest = std::move(request); }
    void supplyHover(const ALTextPos& at, const std::string& text);

    // --- signature help ------------------------------------------------------------

    // The call the caret is in, drawn above its row: the label with the
    // parameter the caret is at picked out, and a line of documentation.
    struct Signature
    {
        std::string                          label;
        // Each parameter's span in the label, in bytes.
        std::vector<std::pair<S32, S32>>     parameters;
        S32                                  active = 0;
        std::string                          documentation;
    };
    // Asked for the call at the caret when an opening bracket or a comma
    // is typed, and again at every change while one is shown; the answer
    // comes through showSignature, or hideSignature for none.
    typedef std::function<void(const ALTextPos& caret)> signature_request_t;
    void setSignatureRequest(signature_request_t request) { mSignatureRequest = std::move(request); }
    void showSignature(const ALTextPos& caret, Signature signature);
    void hideSignature();
    // Shown, and still about the call the caret is in: on its line, and
    // not before where it began.
    bool signatureShown() const;
    const Signature* signature() const { return mSignature ? &*mSignature : nullptr; }

    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void onMouseLeave(S32 x, S32 y, MASK mask) override;
    bool handleToolTip(S32 x, S32 y, MASK mask) override;
    void onFocusLost() override;

protected:
    friend class LLUICtrlFactory;
    ALCodeEditor(const Params& p);

    S32  leftInset() const override { return gutterWidth(); }
    void drawBeforeRows(const LLRect& text) override;
    void drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha) override;
    void revealLine(S32 line) override;
    bool performFold(ALEditorCommand command) override;
    bool canFold(ALEditorCommand command) const override;
    bool complete() override;
    bool performSymbol(ALEditorCommand command) override;
    bool canSymbol(ALEditorCommand command) const override;
    bool mapMark(S32 line, LLColor4& color) const override;

private:
    void onEdit(const ALTextDocument::Edit& edit);
    void drawGutter(const LLRect& text, F32 alpha);
    void drawAfterRows(const LLRect& text) override;
    void tintRow(S32 line, const ALTextLayout::Line& laid, const ALTextLayout::Row& row, F32 alpha, std::vector<LLColor4U>& colors) override;
    // The depth of brackets open at a line's start, found from the top
    // and kept until an edit above it.
    S32  bracketDepthBefore(S32 line);
    // The lines pinned at the top for the view as scrolled now, outer to
    // inner; and the number of rows they take.
    std::vector<S32> stickyLines();
    S32              stickyRows();
    void drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color);

    void              ensureRegions();
    void              applyFolds();
    const FoldRegion* regionStartingAt(S32 line);
    const FoldRegion* regionAround(S32 line);
    // The box drawn after a folded block's first line, in local
    // coordinates, or an empty rect.
    LLRect foldBoxOf(S32 line, const LLRect& text);
    std::string foldBoxText(S32 line);
    // How far a line is indented, in columns; a blank line as the next
    // line that is not, so that the guides run through it.
    S32 indentOf(S32 line) const;

    void openCompletion();
    void refreshCompletion();
    void placeCompletion();
    void listCompletions();
    // Out of sight, but still about the word it was asked about, for an
    // answer that may yet come.
    void hideCompletionList();
    void drawSignature(const LLRect& text);
    void showTip(const ALTextRange& about, const std::string& says);
    void vocabularyCompletions(std::string_view prefix, std::vector<Completion>& out);
    void documentCompletions(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out);

    // The colours as drawn now: a skin's where it gave one, else mixed
    // from the text's.
    LLColor4 gutterColor() const;
    LLColor4 lineNumberColor() const;
    LLColor4 currentLineColor() const;
    LLColor4 foldColor() const;
    LLColor4 highlightColor() const;
    LLColor4 changedColor() const;

    bool mShowLineNumbers      = true;
    bool mShowFoldMarkers      = true;
    bool mHighlightCurrentLine = true;
    bool mMatchBrackets        = true;
    bool mAutoComplete         = true;

    LLUIColor mGutterColor;
    LLUIColor mLineNumberColor;
    LLUIColor mCurrentLineColor;
    LLUIColor mBracketMatchColor;
    LLUIColor mFoldColor;
    LLUIColor mHighlightColor;
    LLUIColor mChangedColor;
    bool      mShowIndentGuides    = true;
    bool      mRelativeLineNumbers = false;
    bool      mColorBrackets       = true;
    bool      mStickyHeaders       = true;
    LLUIColor mBracketColors[3];
    bool      mBracketColorsSet    = false;
    // Depth entering each line, valid for the first mDepthValid lines.
    std::vector<S32> mDepthBefore;
    S32              mDepthValid = 0;
    bool      mGutterColorSet      = false;
    bool      mLineNumberColorSet  = false;
    bool      mCurrentLineColorSet = false;
    bool      mFoldColorSet        = false;
    bool      mHighlightColorSet   = false;
    bool      mChangedColorSet     = false;
    LLUIColor mMarkColors[static_cast<size_t>(Mark::COUNT)];

    boost::signals2::scoped_connection mEditConnection;
    boost::signals2::scoped_connection mChangedConnection;
    std::vector<Mark>                  mMarks;
    // One per line: changed since the last save.
    std::vector<U8>                    mChanged;
    // The mouse over the gutter, and the line it is on there: the fold
    // markers of open blocks show while it is, and the block under it
    // shows its extent.
    bool                               mGutterHover     = false;
    S32                                mGutterHoverLine = -1;
    std::vector<Decoration>            mDecorations;
    std::vector<ALTextRange>           mHighlights;
    std::vector<LLVector2>             mSquiggleScratch;

    std::vector<FoldRegion> mRegions;
    U32                     mRegionsVersion = 0;
    bool                    mRegionsValid   = false;
    // The start lines of the blocks that are folded, in order.
    std::vector<S32>        mFolded;

    completion_provider_t   mProvider;
    completion_request_t    mCompletionRequest;
    hover_provider_t        mHover;
    hover_request_t         mHoverRequest;
    signature_request_t     mSignatureRequest;
    symbol_request_t        mSymbolRequest;
    LLScrollListCtrl*       mCompletionList = nullptr;
    std::vector<Completion> mCompletions;
    // The identifier the list is narrowing, which the choice replaces;
    // and the name before the dot before it, where there is one, whose
    // members are what is being asked for.
    ALTextRange             mCompletionRange;
    std::string             mCompletionHead;
    std::vector<ALTextRange> mPlaceholders;
    S32                      mPlaceholderAt = -1;
    ALTextPos                mPlaceholdersAfter;
    // Where the last request was made, so a late answer is known for
    // what it is about, and what was answered, kept through every
    // narrowing until the list closes.
    ALTextPos               mCompletionAsked{ -1, -1 };
    std::vector<Completion> mSupplied;
    // The word the analyzer was last asked about, and what it answered,
    // kept for the text at the version it was answered for, so that the
    // mouse coming back to the word finds the answer waiting.
    ALTextRange             mHoverAsked;
    U32                     mHoverAskedVersion = 0;
    std::string             mHoverAnswer;
    S32                     mMouseX = -1;
    S32                     mMouseY = -1;
    std::optional<Signature> mSignature;
    ALTextPos               mSignatureAt;
};
