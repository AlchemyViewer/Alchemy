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
    void setShowFoldMarkers(bool show);
    bool getShowFoldMarkers() const { return mShowFoldMarkers; }
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
    // Puts the chosen completion in place of the prefix. False with none
    // open.
    bool                           acceptCompletion();
    const std::vector<Completion>& completions() const { return mCompletions; }
    S32                            chosenCompletion() const;

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

private:
    void onEdit(const ALTextDocument::Edit& edit);
    void drawGutter(const LLRect& text, F32 alpha);
    void drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color);
    // The x span of a range on a row, if it touches the row; a range past
    // the line's end reaches a little past the last glyph.
    bool spanOnRow(S32 line, S32 row, const ALTextRange& range, F32& x0, F32& x1);

    void              ensureRegions();
    void              applyFolds();
    const FoldRegion* regionStartingAt(S32 line);
    const FoldRegion* regionAround(S32 line);
    // The box drawn after a folded block's first line, in local
    // coordinates, or an empty rect.
    LLRect foldBoxOf(S32 line, const LLRect& text);

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
    bool      mGutterColorSet      = false;
    bool      mLineNumberColorSet  = false;
    bool      mCurrentLineColorSet = false;
    bool      mFoldColorSet        = false;
    bool      mHighlightColorSet   = false;
    LLUIColor mMarkColors[static_cast<size_t>(Mark::COUNT)];

    boost::signals2::scoped_connection mEditConnection;
    boost::signals2::scoped_connection mChangedConnection;
    std::vector<Mark>                  mMarks;
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
    // The identifier the list is narrowing, which the choice replaces.
    ALTextRange             mCompletionRange;
    // Where the last request was made, so a late answer is known for
    // what it is about, and what was answered, kept through every
    // narrowing until the list closes.
    ALTextPos               mCompletionAsked{ -1, -1 };
    std::vector<Completion> mSupplied;
    ALTextRange             mHoverAsked;
    S32                     mMouseX = -1;
    S32                     mMouseY = -1;
    std::optional<Signature> mSignature;
    ALTextPos               mSignatureAt;
};
