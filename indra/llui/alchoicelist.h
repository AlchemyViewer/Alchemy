/**
 * @file alchoicelist.h
 * @brief A list to choose from, on the text engine: one line a choice, one of them chosen.
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
#include "lluiimage.h"

#include <optional>
#include <string>
#include <vector>

// A list to choose from, on the text engine: one line a choice, in the
// view's face -- the editor's, where the list is its completions -- with
// a note after it in the reading face, the notes in a column of their
// own, and a mark before it where the choices have them: an image, or
// a badge, a letter or two on a square in the choice's colour, which
// is what a kind looks like until it has an icon. One of the choices
// is chosen, drawn on a band. The list is read-only and never has the
// keyboard: whoever shows it moves the choice by the keys it is given,
// and the mouse chooses by a press and picks by a double click. The
// chosen line is kept in sight.
class ALChoiceList : public ALTextView
{
public:
    AL_VIEW_TYPE(ALChoiceList, ALTextView);

    struct Params : public LLInitParam::Block<Params, ALTextView::Params>
    {
        // The notes' face and colour: the reading face, and the text's
        // colour faded, unless a skin says.
        Optional<const LLFontGL*> note_font;
        Optional<LLUIColor>       note_color;
        // Around the list.
        Optional<LLUIColor>       border_color;
        Params();
    };

    struct Choice
    {
        std::string             text;
        std::string             note;
        // The text's ink, where it is not the view's own: a completion in
        // the colour its kind is drawn in.
        std::optional<LLColor4> color;
        // The mark before the text: an image, or failing one a badge --
        // a letter or two in the ink on a square of it. Nothing for none;
        // the column is there while any choice has one.
        LLUIImagePtr            icon;
        std::string             badge;
    };

    // The choices, replaced whole, with one of them chosen.
    void                       setChoices(std::vector<Choice> choices, S32 chosen = 0);
    const std::vector<Choice>& choices() const { return mChoices; }
    S32                        count() const { return static_cast<S32>(mChoices.size()); }
    // Which is chosen, or -1 with none.
    S32                        chosen() const { return mChosen; }
    void                       choose(S32 index);
    // The choice moved by so many: round the ends where `wrap`, else
    // stopping at them.
    void                       moveChoice(S32 by, bool wrap);
    // How tall the list is with so many rows in sight: what to shape it
    // to, once the choices are in.
    S32                        heightFor(S32 rows);

    // The line around the list, where a view is themed after it was made:
    // the list floats over an editor and wears that editor's colours, not
    // the skin's, which a script theme has nothing to do with.
    void                       setBorderColor(const LLUIColor& color) { mBorderColor = color; }

    typedef boost::signals2::signal<void(S32 index)> choice_signal_t;
    // A choice picked: double-clicked.
    boost::signals2::connection onPicked(const choice_signal_t::slot_type& slot) { return mPicked.connect(slot); }

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleDoubleClick(S32 x, S32 y, MASK mask) override;

protected:
    friend class LLUICtrlFactory;
    ALChoiceList(const Params& p);

    void drawBeforeRows(const LLRect& text) override;
    void drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha) override;

private:
    // The line under a local point, or -1 off the lines.
    S32  lineAtLocal(S32 x, S32 y);
    // A mark's square's side: the taller face's height less the inset.
    S32  markSide() const;

    std::vector<Choice> mChoices;
    S32                 mChosen = -1;
    // Whether any choice has a mark, and how wide the marks' column is.
    bool                mMarks     = false;
    F32                 mMarkWidth = 0.f;
    const LLFontGL*     mNoteFont = nullptr;
    LLUIColor           mNoteColor;
    bool                mNoteColorSet = false;
    LLUIColor           mBorderColor;
    choice_signal_t     mPicked;
};
