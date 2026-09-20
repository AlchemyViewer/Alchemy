/**
 * @file alangledial.h
 * @brief A direction chosen by turning a dial
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

// A circle with a handle on its rim where the direction points, turned by
// dragging, and the angle in degrees beside it. The value is the unit
// vector, x and y with a space between; a drag commits when it ends.
class ALAngleDial : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALAngleDial, LLUICtrl);

    static constexpr S32 HEIGHT = 64;

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Params();
    };

    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    void draw() override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    // A drag ends with the button, or with the mouse taken away: either
    // way the pointer stops turning the dial.
    void onMouseCaptureLost() override;

protected:
    friend class LLUICtrlFactory;
    ALAngleDial(const Params& p);

private:
    void layout();
    void turn(S32 x, S32 y);
    void showDegrees();

    LLSpinCtrl* mDegrees = nullptr;
    LLRect mDial;
    // Degrees anticlockwise from the right, as the value's x and y say.
    F32 mAngle = 135.f;
    bool mDragging = false;
};
