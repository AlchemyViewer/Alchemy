/**
 * @file alchoicepopup.h
 * @brief A list of choices floated over a view, and the box beside it about the chosen one.
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

#include "alchoicelist.h"
#include "llview.h"

#include <string>
#include <vector>

class ALTextView;

// A list of choices floated over a view -- what a code editor completes a
// word from, the fixes it offers at a problem -- and the box beside it that
// says more about the chosen one: a completion's documentation, a fix's
// preview. The list is made with the popup; the box the first time it is
// wanted, most lists never having anything to say beside them.
//
// A child of the view it floats over, as large as the view and following
// it, so that the list and the box are placed in the view's own
// coordinates; and seen through, so that only they take the mouse
// (ALTextView::overlayAt looks through a child that is not mouse-opaque to
// its children). Hidden while its list is. The view finds it by its name
// among its children, as anything looking at the view would.
class ALChoicePopup final : public LLView
{
public:
    AL_VIEW_TYPE(ALChoicePopup, LLView);

    struct Params : public LLInitParam::Block<Params, LLView::Params>
    {
        // The list's name, and the box's, among the view's descendants;
        // and whether the list is a menu (ALChoiceList::setMenuLike).
        Mandatory<std::string> list_name;
        Mandatory<std::string> side_name;
        Optional<bool>         menu_like;
        // The list's face.
        Optional<const LLFontGL*> font;
        Params();
    };

    ALChoiceList&       list() { return *mList; }
    const ALChoiceList& list() const { return *mList; }
    // Whether the list is up, and whether a point of the view is on it.
    bool                listShown() const { return getVisible(); }
    bool                onList(S32 x, S32 y) const;

    // The list and the box in the colours of the view they float over, and
    // the list in its face: a script theme recolours the view, which the
    // skin's colours know nothing of.
    void setLook(const LLFontGL* font, const LLColor4& ground, const LLColor4& ink, const LLColor4& chosen, const LLColor4& border);
    // The list given its choices, one of them chosen, laid out `width`
    // wide -- to be measured (ALChoiceList::widthFor) before it is placed.
    void fill(std::vector<ALChoiceList::Choice> choices, S32 chosen, S32 width);
    // The list put under an anchor in the view -- or over it where under
    // would run off the bottom -- so many rows tall and so wide, within the
    // view; and shown.
    void showUnder(const LLRect& anchor, S32 rows, S32 width);
    // The list hidden and its choices let go of, the box with it.
    void hideList();

    // The box beside the list, made the first time it is wanted, with the
    // tints it last showed taken off for whoever fills it now.
    ALTextView& side();
    // Put beside the list where there is room, on its right or else its
    // left, else under or over it; as tall as what it says, up to a limit,
    // the rest cut. And hidden.
    void        placeSide();
    void        hideSide();
    bool        sideShown() const;
    // Whether the box, shown, says this already, beside the list where it
    // is now: so that a list made again with the same row chosen leaves
    // the box as it stands rather than lexing and reading it again. And
    // what it says, told once it is filled and placed.
    bool        sideSays(const std::string& says) const;
    void        sideSaid(const std::string& says);

    // The box's frame over the box, as a card's is.
    void draw() override;

protected:
    friend class LLUICtrlFactory;
    ALChoicePopup(const Params& p);

private:
    ALChoiceList* mList = nullptr;
    ALTextView*   mSide = nullptr;
    std::string   mSideName;
    LLColor4      mGround;
    LLColor4      mInk;
    LLColor4      mBorder;
    // What the box says, and beside where the list was when it said it.
    std::string   mSideSays;
    LLRect        mSideBeside;
};
