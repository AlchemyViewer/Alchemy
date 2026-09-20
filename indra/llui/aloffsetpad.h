/**
 * @file aloffsetpad.h
 * @brief An offset chosen by dragging a dot
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

#include "lluictrl.h"

class LLSpinCtrl;

// A square pad with a dot where the offset is, dragged to move it, and the
// two numbers beside it for anyone who knows the number they want. The
// value is x and y with a space between; a drag commits when it ends.
class ALOffsetPad : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALOffsetPad, LLUICtrl);

    static constexpr S32 HEIGHT = 70;

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Params();
    };

    void setValue(const LLSD& value) override;
    LLSD getValue() const override;
    // How far the pad reaches from its centre, in the value's units.
    void setRange(F32 reach, F32 step, S32 decimals);

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    // A drag ends with the button, or with the mouse taken away: either
    // way the pointer stops moving the dot.
    void onMouseCaptureLost() override;

protected:
    friend class LLUICtrlFactory;
    ALOffsetPad(const Params& p);

private:
    void layout();
    void place(S32 x, S32 y);
    void showNumbers();

    LLSpinCtrl* mX = nullptr;
    LLSpinCtrl* mY = nullptr;
    LLRect mPad;
    F32 mOffset[2] = { 0.f, 0.f };
    F32 mReach = 32.f;
    bool mDragging = false;
};
