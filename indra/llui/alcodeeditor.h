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
#include <string>
#include <string_view>
#include <vector>

class LLScrollListCtrl;

// The text view configured for code: a gutter with line numbers, a mark
// per line for what an analyzer said about it and a marker per block that
// folds, a band under the caret's row, the bracket the caret is at and its
// match boxed, decorations over ranges -- a squiggle under a problem, a
// wash behind a find -- and a list of completions under the caret.
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
        Optional<LLUIColor> gutter_color;
        Optional<LLUIColor> line_number_color;
        Optional<LLUIColor> current_line_color;
        Optional<LLUIColor> bracket_match_color;
        Optional<LLUIColor> fold_color;

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
    };
    void                           setDecorations(std::vector<Decoration> decorations);
    const std::vector<Decoration>& decorations() const { return mDecorations; }

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
    bool completionOpen() const;
    void closeCompletion();
    // Puts the chosen completion in place of the prefix. False with none
    // open.
    bool                           acceptCompletion();
    const std::vector<Completion>& completions() const { return mCompletions; }
    S32                            chosenCompletion() const;

    bool handleKeyHere(KEY key, MASK mask) override;
    bool handleUnicodeCharHere(llwchar uni_char) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
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
    void vocabularyCompletions(std::string_view prefix, std::vector<Completion>& out);
    void documentCompletions(const ALTextPos& at, std::string_view prefix, std::vector<Completion>& out);

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
    LLUIColor mMarkColors[static_cast<size_t>(Mark::COUNT)];

    boost::signals2::scoped_connection mEditConnection;
    boost::signals2::scoped_connection mChangedConnection;
    std::vector<Mark>                  mMarks;
    std::vector<Decoration>            mDecorations;
    std::vector<LLVector2>             mSquiggleScratch;

    std::vector<FoldRegion> mRegions;
    U32                     mRegionsVersion = 0;
    bool                    mRegionsValid   = false;
    // The start lines of the blocks that are folded, in order.
    std::vector<S32>        mFolded;

    completion_provider_t   mProvider;
    LLScrollListCtrl*       mCompletionList = nullptr;
    std::vector<Completion> mCompletions;
    // The identifier the list is narrowing, which the choice replaces.
    ALTextRange             mCompletionRange;
};
