/**
 * @file alpicturefield.h
 * @brief A picture beside boxes of numbers: what the dial, the pad and the corner field share.
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

#include "aldraggesture.h"
#include "lluictrl.h"

#include <string>
#include <vector>

class LLSpinCtrl;

// A picture beside boxes of numbers: a dial, a pad, a rounded rectangle.
// The picture says what the numbers are before the boxes do; on a kind
// with a pointer, a press or a drag on it moves them, and the boxes are
// for anyone who knows the number they want. The value is the numbers as
// words. A box commits as it is typed in, a drag when it ends. Each kind
// says where its picture goes and draws it, lays out its boxes, and reads
// its numbers to and from the value; this has the rest.
class ALPictureField : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALPictureField, LLUICtrl);

    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    // What a reset goes back to: nothing, until one is given.
    void        setDefault(const LLSD& value) { mDefault = value; }
    const LLSD& defaultValue() const { return mDefault; }
    // Back to the default, committed where that changed what it says.
    // False where it has none.
    bool resetToDefault();

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    // A drag ends with the button, or with the mouse taken away: either
    // way the pointer stops moving the picture.
    void onMouseCaptureLost() override;

protected:
    ALPictureField(const LLUICtrl::Params& p);

    // A box made by the kind, put beside the picture: typed into, it is
    // read by boxTyped() and the field commits.
    LLSpinCtrl*   adoptBox(LLSpinCtrl* box);
    const LLRect& picture() const { return mPicture; }
    // The picture and the boxes put where they go: once the kind has made
    // its boxes, and again at each reshape.
    void layout();

    // Where the picture goes in a field so big: a square at the left, as
    // tall as the field less a border, unless the kind says otherwise.
    virtual LLRect pictureIn(S32 width, S32 height) const;
    // The boxes put beside it.
    virtual void placeBoxes(const LLRect& picture, S32 width, S32 height) = 0;
    // The numbers a value gives, taken; and the value they make.
    virtual void        take(const std::vector<F32>& numbers) = 0;
    virtual std::string say() const = 0;
    // The boxes shown what the numbers are.
    virtual void showNumbers() = 0;
    // A box typed into, read into the numbers.
    virtual void boxTyped(size_t box) = 0;
    // Whether the picture is pointed at, and the numbers moved to where
    // it is: not for a kind whose picture only shows them.
    virtual bool pointable() const { return false; }
    virtual void pointAt(S32 x, S32 y) {}
    virtual void drawPicture() = 0;

private:
    std::vector<LLSpinCtrl*> mBoxes;
    LLRect                   mPicture;
    LLSD                     mDefault;
    // Moved at once, taken on release.
    ALDragGesture            mDrag;
};
