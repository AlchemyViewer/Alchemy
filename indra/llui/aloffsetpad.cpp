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

#include "llfocusmgr.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llspinctrl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <fmt/format.h>
#include <sstream>

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

ALOffsetPad::ALOffsetPad(const Params& p) : LLUICtrl(p)
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
        *spin = LLUICtrlFactory::create<LLSpinCtrl>(sp);
        (*spin)->setCommitCallback([this](LLUICtrl*, const LLSD&)
            {
                mOffset[0] = (F32)mX->getValue().asReal();
                mOffset[1] = (F32)mY->getValue().asReal();
                onCommit();
            });
        addChild(*spin);
    }

    layout();
}

void ALOffsetPad::layout()
{
    const S32 height = getRect().getHeight();
    const S32 side = llmax(24, height - 4);

    mPad = LLRect(2, height - 2, 2 + side, height - 2 - side);

    const S32 left = mPad.mRight + GAP;
    const S32 top = height - 2;

    mX->setShape(LLRect(left, top, left + BOX_WIDTH, top - BOX_HEIGHT));
    mY->setShape(LLRect(left, top - BOX_HEIGHT - GAP, left + BOX_WIDTH, top - 2 * BOX_HEIGHT - GAP));
}

void ALOffsetPad::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
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

void ALOffsetPad::setValue(const LLSD& value)
{
    std::istringstream in(value.asString());
    F32 x = 0.f;
    F32 y = 0.f;

    in >> x >> y;
    mOffset[0] = x;
    mOffset[1] = y;
    showNumbers();
}

LLSD ALOffsetPad::getValue() const
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
void ALOffsetPad::place(S32 x, S32 y)
{
    const F32 half = llmax(1.f, F32(mPad.getWidth()) * 0.5f);
    const F32 cx = F32(mPad.mLeft + mPad.mRight) * 0.5f;
    const F32 cy = F32(mPad.mTop + mPad.mBottom) * 0.5f;

    mOffset[0] = llclamp((x - cx) / half * mReach, -mReach, mReach);
    mOffset[1] = llclamp((y - cy) / half * mReach, -mReach, mReach);
    // Half steps, which is what the boxes show.
    mOffset[0] = ll_round(mOffset[0] * 2.f) * 0.5f;
    mOffset[1] = ll_round(mOffset[1] * 2.f) * 0.5f;
    showNumbers();
}

void ALOffsetPad::draw()
{
    static const LLUIColor well = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    static const LLUIColor grid = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
    static const LLUIColor dot = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);

    gl_rect_2d(mPad, well.get() % 0.6f, true);
    gl_rect_2d(mPad, grid.get() % 0.6f, false);

    const S32 cx = (mPad.mLeft + mPad.mRight) / 2;
    const S32 cy = (mPad.mTop + mPad.mBottom) / 2;

    gl_line_2d(mPad.mLeft, cy, mPad.mRight, cy, grid.get() % 0.4f);
    gl_line_2d(cx, mPad.mBottom, cx, mPad.mTop, grid.get() % 0.4f);

    const F32 half = F32(mPad.getWidth()) * 0.5f;
    const S32 x = cx + ll_round(mOffset[0] / mReach * half);
    const S32 y = cy + ll_round(mOffset[1] / mReach * half);

    gl_line_2d(cx, cy, x, y, dot.get() % 0.6f);
    gl_rect_2d(LLRect(x - 3, y + 3, x + 3, y - 3), dot.get(), true);

    LLUICtrl::draw();
}

bool ALOffsetPad::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (!mPad.pointInRect(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    mDragging = true;
    gFocusMgr.setMouseCapture(this);
    place(x, y);

    return true;
}

bool ALOffsetPad::handleHover(S32 x, S32 y, MASK mask)
{
    if (mDragging)
    {
        place(x, y);

        return true;
    }

    return LLUICtrl::handleHover(x, y, mask);
}

bool ALOffsetPad::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (!mDragging)
    {
        return LLUICtrl::handleMouseUp(x, y, mask);
    }

    mDragging = false;
    gFocusMgr.setMouseCapture(nullptr);
    place(x, y);
    onCommit();

    return true;
}
