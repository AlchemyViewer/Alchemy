/**
 * @file alcornerfield.h
 * @brief Four corner radii, drawn as the corners they round
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

#include "alpicturefield.h"

#include <array>

class LLCheckBoxCtrl;
class LLSpinCtrl;

// A rounded rectangle drawn with the radii it is given, and a box at each
// of its corners to change one. Linked, which is how they start when they
// agree, a change to any corner is a change to all four; unlinked, each is
// its own. The value is the four numbers clockwise from the top left with
// spaces between.
class ALCornerField : public ALPictureField
{
public:
    AL_VIEW_TYPE(ALCornerField, ALPictureField);

    static constexpr S32 HEIGHT = 74;

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Params();
    };

    void setRange(F32 minimum, F32 maximum, F32 step, S32 decimals);

protected:
    friend class LLUICtrlFactory;
    ALCornerField(const Params& p);

    LLRect      pictureIn(S32 width, S32 height) const override;
    void        placeBoxes(const LLRect& picture, S32 width, S32 height) override;
    void        take(const std::vector<F32>& numbers) override;
    std::string say() const override;
    void        showNumbers() override;
    void        boxTyped(size_t corner) override;
    void        drawPicture() override;

private:
    std::array<LLSpinCtrl*, 4> mSpin{};
    LLCheckBoxCtrl* mLink = nullptr;
    // Top left, top right, bottom right, bottom left.
    std::array<F32, 4> mRadii{};
};
