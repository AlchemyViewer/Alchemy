/**
 * @file alcodeeditor.h
 * @brief The text view as a code editor: a gutter, marks, a matched bracket, squiggles.
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

#include <vector>

// The text view configured for code: a gutter with line numbers and a mark
// per line for what an analyzer said about it, a band under the caret's
// row, the bracket the caret is at and its match boxed, and decorations
// over ranges -- a squiggle under a problem, a wash behind a find. Folding
// and the completion popover come later.
class ALCodeEditor : public ALTextView
{
public:
    AL_VIEW_TYPE(ALCodeEditor, ALTextView);

    struct Params : public LLInitParam::Block<Params, ALTextView::Params>
    {
        Optional<bool>      show_line_numbers;
        Optional<bool>      highlight_current_line;
        Optional<bool>      match_brackets;
        Optional<LLUIColor> gutter_color;
        Optional<LLUIColor> line_number_color;
        Optional<LLUIColor> current_line_color;
        Optional<LLUIColor> bracket_match_color;

        Params();
    };

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
    S32  gutterWidth() const;

    // Zero-based; the caret goes to the start of the line and the line
    // comes into view.
    void goToLine(S32 line);

protected:
    friend class LLUICtrlFactory;
    ALCodeEditor(const Params& p);

    S32  leftInset() const override { return gutterWidth(); }
    void drawBeforeRows(const LLRect& text) override;
    void drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha) override;

private:
    void onEdit(const ALTextDocument::Edit& edit);
    void drawGutter(const LLRect& text, F32 alpha);
    void drawSquiggle(F32 x0, F32 x1, S32 y, const LLColor4& color);
    // The x span of a range on a row, if it touches the row; a range past
    // the line's end reaches a little past the last glyph.
    bool spanOnRow(S32 line, S32 row, const ALTextRange& range, F32& x0, F32& x1);

    bool mShowLineNumbers      = true;
    bool mHighlightCurrentLine = true;
    bool mMatchBrackets        = true;

    LLUIColor mGutterColor;
    LLUIColor mLineNumberColor;
    LLUIColor mCurrentLineColor;
    LLUIColor mBracketMatchColor;
    LLUIColor mMarkColors[static_cast<size_t>(Mark::COUNT)];

    boost::signals2::scoped_connection mEditConnection;
    std::vector<Mark>                  mMarks;
    std::vector<Decoration>            mDecorations;
    std::vector<LLVector2>             mSquiggleScratch;
};
