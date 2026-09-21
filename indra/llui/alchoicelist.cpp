/**
 * @file alchoicelist.cpp
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

#include "linden_common.h"

#include "alchoicelist.h"

#include "llrender.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <cmath>

static LLDefaultChildRegistry::Register<ALChoiceList> r("choice_list");

namespace
{
    // The notes' column starts this many spaces past the widest text,
    // and never past this share of the list's width.
    const S32 NOTE_GAP        = 2;
    const F32 NOTE_COLUMN_MAX = 0.6f;
    // A mark's square is inset this far from the row's edges, and the
    // text starts this far past the square.
    const S32 MARK_INSET = 2;
    const S32 MARK_GAP   = 4;
}

ALChoiceList::Params::Params()
:   note_font("note_font"),
    note_color("note_color"),
    border_color("border_color")
{
    read_only   = true;
    takes_focus = false;
    tab_stop    = false;
    word_wrap   = false;
}

ALChoiceList::ALChoiceList(const Params& p)
:   ALTextView(p),
    mNoteFont(p.note_font.isProvided() ? p.note_font() : LLFontGL::getFontSansSerif()),
    mNoteColor(p.note_color),
    mNoteColorSet(p.note_color.isProvided()),
    mBorderColor(p.border_color.isProvided() ? p.border_color() : LLUIColorTable::instance().getColor("CodeCompletionBorderColor", LLColor4::grey))
{
    setReadOnly(true);
    // The marks' column: room before every line's text while any choice
    // has a mark, which drawRowExtras fills.
    layout().setInlayProvider([this](S32 line, std::vector<ALTextLayout::Inlay>& out) {
        if (mMarks && line < count())
        {
            ALTextLayout::Inlay mark;
            mark.column = 0;
            mark.width  = mMarkWidth;
            mark.before = true;
            mark.id     = line;
            out.push_back(mark);
        }
    });
}

// --- the choices --------------------------------------------------------------

void ALChoiceList::setChoices(std::vector<Choice> choices, S32 chosen)
{
    mChoices = std::move(choices);
    // The marks' column, a row's height square and a gap, while any
    // choice has one.
    mMarks     = std::any_of(mChoices.begin(), mChoices.end(), [](const Choice& c) { return c.icon || !c.badge.empty(); });
    mMarkWidth = mMarks ? static_cast<F32>(layout().rowHeight() + MARK_GAP) : 0.f;
    // The text: each choice on a line, its note after a tab. A choice
    // without a note has a space in the note's face there, so that every
    // row is as tall as one with.
    std::string text;
    F32         widest = 0.f;
    for (size_t i = 0; i < mChoices.size(); ++i)
    {
        const Choice& choice = mChoices[i];
        if (i)
        {
            text += '\n';
        }
        text += choice.text;
        text += '\t';
        text += choice.note.empty() ? std::string(" ") : choice.note;
        if (getFont())
        {
            widest = llmax(widest, getFont()->getWidthF32(choice.text));
        }
    }
    // The notes' column: one tab stop, past the widest text up to a
    // share of the width. A text wider still runs its note on to the
    // next stop.
    const F32 cell  = llmax(1.f, layout().columnWidth());
    const F32 limit = static_cast<F32>(getLocalRect().getWidth()) * NOTE_COLUMN_MAX;
    setTabWidth(llmax(1, static_cast<S32>(std::ceil((llmin(widest, limit) + mMarkWidth) / cell)) + NOTE_GAP));
    setText(text);
    // The styles: the text in its own ink where it has one, the note in
    // the reading face and its colour.
    LLColor4 note_ink = mNoteColorSet ? mNoteColor.get() : textColor();
    if (!mNoteColorSet)
    {
        note_ink.mV[VALPHA] *= 0.6f;
    }
    std::vector<Style> styles;
    styles.reserve(mChoices.size() * 2);
    for (size_t i = 0; i < mChoices.size(); ++i)
    {
        const Choice& choice = mChoices[i];
        const S32     line   = static_cast<S32>(i);
        const S32     split  = static_cast<S32>(choice.text.size());
        if (choice.color && split > 0)
        {
            Style ink;
            ink.range = ALTextRange(ALTextPos(line, 0), ALTextPos(line, split));
            ink.color = *choice.color;
            styles.push_back(ink);
        }
        Style note;
        note.range = ALTextRange(ALTextPos(line, split + 1), document().lineEnd(line));
        note.font  = mNoteFont;
        note.color = note_ink;
        styles.push_back(note);
    }
    setStyles(std::move(styles));
    mChosen = -1;
    choose(chosen);
}

void ALChoiceList::choose(S32 index)
{
    if (mChoices.empty())
    {
        mChosen = -1;
        return;
    }
    mChosen = llclamp(index, 0, count() - 1);
    // The caret is the chosen line: what the ruler marks, and what is
    // kept in sight.
    setCaret(ALTextPos(mChosen, 0));
}

void ALChoiceList::moveChoice(S32 by, bool wrap)
{
    const S32 n = count();
    if (n == 0)
    {
        return;
    }
    const S32 at = llmax(0, mChosen);
    choose(wrap ? ((at + by) % n + n) % n : llclamp(at + by, 0, n - 1));
}

S32 ALChoiceList::heightFor(S32 rows)
{
    S32 height = 0;
    for (S32 line = 0; line < llmin(rows, count()); ++line)
    {
        height += layout().rowHeightOf(line, 0);
    }
    return height + (getLocalRect().getHeight() - textRect().getHeight());
}

// --- drawing --------------------------------------------------------------------

void ALChoiceList::drawBeforeRows(const LLRect& text)
{
    if (mChosen < 0 || mChosen >= count())
    {
        return;
    }
    const F32    alpha = getDrawContext().mAlpha;
    const LLRect local = getLocalRect();
    const S32    top   = screenTopOf(text, mChosen, 0);
    const S32    h     = layout().rowHeightOf(mChosen, 0);
    gl_rect_2d(local.mLeft + 1, top, local.mRight - 1, top - h, selectionColor() % alpha);
}

void ALChoiceList::drawRowExtras(S32 line, S32 row, const LLRect& text, S32 screen_top, F32 left, F32 alpha)
{
    if (!mMarks || row != 0 || line < 0 || line >= count())
    {
        return;
    }
    const Choice& choice = mChoices[static_cast<size_t>(line)];
    if (!choice.icon && choice.badge.empty())
    {
        return;
    }
    // The square: a row's height less the inset, at the row's top left,
    // in the gap the layout left before the text.
    const S32    top  = screenTopOf(text, line, 0);
    const S32    side = layout().rowHeight() - 2 * MARK_INSET;
    const S32    x0   = static_cast<S32>(left) + MARK_INSET;
    const LLRect box(x0, top - MARK_INSET, x0 + side, top - MARK_INSET - side);
    if (choice.icon)
    {
        choice.icon->draw(box, LLColor4::white % alpha);
        return;
    }
    const LLColor4 ink = choice.color ? *choice.color : textColor();
    gl_rect_2d(box, lerp(backgroundColor(), ink, 0.25f) % alpha);
    if (const LLFontGL* font = getFont())
    {
        const F32 x = static_cast<F32>(box.mLeft + box.mRight) * 0.5f;
        const F32 y = static_cast<F32>(box.mBottom) + (static_cast<F32>(side) - font->getLineHeight()) * 0.5f + font->getDescenderHeight();
        font->renderUTF8(choice.badge, 0, x, y, ink % alpha, LLFontGL::HCENTER, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW);
    }
}

void ALChoiceList::draw()
{
    ALTextView::draw();
    gl_rect_2d(getLocalRect(), mBorderColor.get() % getDrawContext().mAlpha, false);
}

// --- the mouse ------------------------------------------------------------------

S32 ALChoiceList::lineAtLocal(S32 x, S32 y)
{
    if (mChoices.empty() || !textRect().pointInRect(x, y))
    {
        return -1;
    }
    const S32 line = posAtLocal(x, y, false).line;
    return line >= 0 && line < count() ? line : -1;
}

bool ALChoiceList::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (const S32 line = lineAtLocal(x, y); line >= 0)
    {
        choose(line);
        return true;
    }
    return ALTextView::handleMouseDown(x, y, mask);
}

bool ALChoiceList::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    if (const S32 line = lineAtLocal(x, y); line >= 0)
    {
        choose(line);
        mPicked(line);
        return true;
    }
    return ALTextView::handleDoubleClick(x, y, mask);
}
