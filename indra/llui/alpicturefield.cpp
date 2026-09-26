/**
 * @file alpicturefield.cpp
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

#include "linden_common.h"

#include "alpicturefield.h"

#include "llfocusmgr.h"
#include "llspinctrl.h"

#include <sstream>

ALPictureField::ALPictureField(const LLUICtrl::Params& p) : LLUICtrl(p)
{
}

LLSpinCtrl* ALPictureField::adoptBox(LLSpinCtrl* box)
{
    const size_t index = mBoxes.size();
    box->setCommitCallback([this, index](LLUICtrl*, const LLSD&)
        {
            boxTyped(index);
            onCommit();
        });
    addChild(box);
    mBoxes.push_back(box);
    return box;
}

void ALPictureField::layout()
{
    const S32 width  = getRect().getWidth();
    const S32 height = getRect().getHeight();
    mPicture         = pictureIn(width, height);
    placeBoxes(mPicture, width, height);
}

LLRect ALPictureField::pictureIn(S32 width, S32 height) const
{
    const S32 side = llmax(24, height - 4);
    return LLRect(2, height - 2, 2 + side, height - 2 - side);
}

void ALPictureField::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
}

void ALPictureField::setValue(const LLSD& value)
{
    std::istringstream in(value.asString());
    std::vector<F32>   numbers;
    for (F32 number; in >> number;)
    {
        numbers.push_back(number);
    }
    take(numbers);
    showNumbers();
}

LLSD ALPictureField::getValue() const
{
    return say();
}

bool ALPictureField::resetToDefault()
{
    if (mDefault.isUndefined())
    {
        return false;
    }
    const std::string was = say();
    setValue(mDefault);
    if (say() != was)
    {
        onCommit();
    }
    return true;
}

void ALPictureField::draw()
{
    drawPicture();
    LLUICtrl::draw();
}

bool ALPictureField::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (!pointable() || !mPicture.pointInRect(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    mDrag.press(x, y);
    gFocusMgr.setMouseCapture(this);
    pointAt(x, y);

    return true;
}

bool ALPictureField::handleHover(S32 x, S32 y, MASK mask)
{
    if (mDrag.pressed())
    {
        // Followed only where the pointer went somewhere: the captor is
        // hovered on every frame, moved or not.
        if (mDrag.moved(x, y))
        {
            pointAt(x, y);
        }

        return true;
    }

    return LLUICtrl::handleHover(x, y, mask);
}

void ALPictureField::onMouseCaptureLost()
{
    mDrag.cancel();
    LLUICtrl::onMouseCaptureLost();
}

bool ALPictureField::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (!mDrag.release())
    {
        return LLUICtrl::handleMouseUp(x, y, mask);
    }

    gFocusMgr.setMouseCapture(nullptr);
    pointAt(x, y);
    onCommit();

    return true;
}
