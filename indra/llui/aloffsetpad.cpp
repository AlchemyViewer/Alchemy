/**
 * @file aloffsetpad.cpp
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

#include "linden_common.h"
#include "aloffsetpad.h"

#include "llrender.h"
#include "llrender2dutils.h"
#include "llspinctrl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <fmt/format.h>

static LLDefaultChildRegistry::Register<ALOffsetPad> r("offset_pad");

namespace
{
    constexpr S32 BOX_WIDTH = 78;
    constexpr S32 BOX_HEIGHT = 20;
    constexpr S32 GAP = 6;
}

ALOffsetPad::Params::Params()
{
}

ALOffsetPad::ALOffsetPad(const Params& p) : ALPictureField(p)
{
    for (LLSpinCtrl** spin : { &mX, &mY })
    {
        LLSpinCtrl::Params sp;

        sp.name = spin == &mX ? "x" : "y";
        sp.label = spin == &mX ? "X" : "Y";
        sp.label_width = 14;
        sp.rect = LLRect(0, BOX_HEIGHT, BOX_WIDTH, 0);
        sp.decimal_digits = 1;
        sp.increment = 0.5f;
        sp.min_value = -512.f;
        sp.max_value = 512.f;
        *spin = adoptBox(LLUICtrlFactory::create<LLSpinCtrl>(sp));
    }

    layout();
}

void ALOffsetPad::placeBoxes(const LLRect& picture, S32 width, S32 height)
{
    const S32 left = picture.mRight + GAP;
    const S32 top = height - 2;

    mX->setShape(LLRect(left, top, left + BOX_WIDTH, top - BOX_HEIGHT));
    mY->setShape(LLRect(left, top - BOX_HEIGHT - GAP, left + BOX_WIDTH, top - 2 * BOX_HEIGHT - GAP));
}

void ALOffsetPad::boxTyped(size_t box)
{
    mOffset[0] = (F32)mX->getValue().asReal();
    mOffset[1] = (F32)mY->getValue().asReal();
}

void ALOffsetPad::setRange(F32 reach, F32 step, S32 decimals)
{
    mReach = llmax(1.f, reach);

    for (LLSpinCtrl* spin : { mX, mY })
    {
        spin->setMinValue(-mReach);
        spin->setMaxValue(mReach);
        spin->setIncrement(step);
        spin->setPrecision(decimals);
    }
}

void ALOffsetPad::take(const std::vector<F32>& numbers)
{
    mOffset[0] = numbers.size() > 0 ? numbers[0] : 0.f;
    mOffset[1] = numbers.size() > 1 ? numbers[1] : 0.f;
}

std::string ALOffsetPad::say() const
{
    return fmt::format("{} {}", mOffset[0], mOffset[1]);
}

void ALOffsetPad::showNumbers()
{
    mX->setValue(LLSD(mOffset[0]));
    mY->setValue(LLSD(mOffset[1]));
}

// Where the pointer is, as an offset: the pad's centre is none and its
// edge is the reach, in both directions.
void ALOffsetPad::pointAt(S32 x, S32 y)
{
    const LLRect& pad = picture();
    const F32 half = llmax(1.f, F32(pad.getWidth()) * 0.5f);
    const F32 cx = F32(pad.mLeft + pad.mRight) * 0.5f;
    const F32 cy = F32(pad.mTop + pad.mBottom) * 0.5f;

    mOffset[0] = llclamp((x - cx) / half * mReach, -mReach, mReach);
    mOffset[1] = llclamp((y - cy) / half * mReach, -mReach, mReach);
    // Half steps, which is what the boxes show.
    mOffset[0] = ll_round(mOffset[0] * 2.f) * 0.5f;
    mOffset[1] = ll_round(mOffset[1] * 2.f) * 0.5f;
    showNumbers();
}

void ALOffsetPad::drawPicture()
{
    static const LLUIColor well = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    static const LLUIColor grid = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
    static const LLUIColor dot = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);

    const LLRect& pad = picture();
    gl_rect_2d(pad, well.get() % 0.6f, true);
    gl_rect_2d(pad, grid.get() % 0.6f, false);

    const S32 cx = (pad.mLeft + pad.mRight) / 2;
    const S32 cy = (pad.mTop + pad.mBottom) / 2;

    gl_line_2d(pad.mLeft, cy, pad.mRight, cy, grid.get() % 0.4f);
    gl_line_2d(cx, pad.mBottom, cx, pad.mTop, grid.get() % 0.4f);

    const F32 half = F32(pad.getWidth()) * 0.5f;
    const S32 x = cx + ll_round(mOffset[0] / mReach * half);
    const S32 y = cy + ll_round(mOffset[1] / mReach * half);

    gl_line_2d(cx, cy, x, y, dot.get() % 0.6f);
    gl_rect_2d(LLRect(x - 3, y + 3, x + 3, y - 3), dot.get(), true);
}
