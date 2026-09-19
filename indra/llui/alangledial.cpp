/**
 * @file alangledial.cpp
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

#include "linden_common.h"
#include "alangledial.h"

#include "llfocusmgr.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llspinctrl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <cmath>
#include <fmt/format.h>
#include <sstream>

static LLDefaultChildRegistry::Register<ALAngleDial> r("angle_dial");

namespace
{
    constexpr S32 BOX_WIDTH = 84;
    constexpr S32 BOX_HEIGHT = 20;
    constexpr S32 GAP = 6;
    constexpr F32 DEG = 3.14159265f / 180.f;
}

ALAngleDial::Params::Params()
{
}

ALAngleDial::ALAngleDial(const Params& p) : LLUICtrl(p)
{
    LLSpinCtrl::Params sp;

    sp.name = "degrees";
    sp.label = "deg";
    sp.label_width = 26;
    sp.rect = LLRect(0, BOX_HEIGHT, BOX_WIDTH, 0);
    sp.decimal_digits = 0;
    sp.increment = 5.f;
    sp.min_value = 0.f;
    sp.max_value = 360.f;
    mDegrees = LLUICtrlFactory::create<LLSpinCtrl>(sp);
    mDegrees->setCommitCallback([this](LLUICtrl*, const LLSD&)
        {
            mAngle = (F32)mDegrees->getValue().asReal();
            onCommit();
        });
    addChild(mDegrees);
    layout();
}

void ALAngleDial::layout()
{
    const S32 height = getRect().getHeight();
    const S32 side = llmax(24, height - 4);

    mDial = LLRect(2, height - 2, 2 + side, height - 2 - side);

    const S32 left = mDial.mRight + GAP;
    const S32 top = height - 2;

    mDegrees->setShape(LLRect(left, top, left + BOX_WIDTH, top - BOX_HEIGHT));
}

void ALAngleDial::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
}

void ALAngleDial::setValue(const LLSD& value)
{
    std::istringstream in(value.asString());
    F32 x = -1.f;
    F32 y = 1.f;

    in >> x >> y;

    if (x != 0.f || y != 0.f)
    {
        mAngle = std::atan2(y, x) / DEG;

        if (mAngle < 0.f)
        {
            mAngle += 360.f;
        }
    }

    showDegrees();
}

LLSD ALAngleDial::getValue() const
{
    return fmt::format("{:.4g} {:.4g}", std::cos(mAngle * DEG), std::sin(mAngle * DEG));
}

void ALAngleDial::showDegrees()
{
    mDegrees->setValue(LLSD(ll_round(mAngle)));
}

void ALAngleDial::turn(S32 x, S32 y)
{
    const F32 cx = F32(mDial.mLeft + mDial.mRight) * 0.5f;
    const F32 cy = F32(mDial.mTop + mDial.mBottom) * 0.5f;

    if (x == cx && y == cy)
    {
        return;
    }

    mAngle = std::atan2(F32(y) - cy, F32(x) - cx) / DEG;

    if (mAngle < 0.f)
    {
        mAngle += 360.f;
    }

    // Whole degrees, in steps of five while dragging: a light direction
    // is chosen by eye.
    mAngle = F32(ll_round(mAngle / 5.f) * 5) ;

    if (mAngle >= 360.f)
    {
        mAngle -= 360.f;
    }

    showDegrees();
}

void ALAngleDial::draw()
{
    static const LLUIColor well = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);
    static const LLUIColor rim = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);
    static const LLUIColor handle = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);

    const F32 cx = F32(mDial.mLeft + mDial.mRight) * 0.5f;
    const F32 cy = F32(mDial.mTop + mDial.mBottom) * 0.5f;
    const F32 radius = F32(mDial.getWidth()) * 0.5f - 2.f;

    gGL.color4fv((well.get() % 0.6f).mV);
    gl_circle_2d(cx, cy, radius, 32, true);
    gGL.color4fv((rim.get() % 0.7f).mV);
    gl_circle_2d(cx, cy, radius, 32, false);

    const S32 hx = ll_round(cx + std::cos(mAngle * DEG) * (radius - 3.f));
    const S32 hy = ll_round(cy + std::sin(mAngle * DEG) * (radius - 3.f));

    gl_line_2d(ll_round(cx), ll_round(cy), hx, hy, handle.get() % 0.7f);
    gl_rect_2d(LLRect(hx - 3, hy + 3, hx + 3, hy - 3), handle.get(), true);

    LLUICtrl::draw();
}

bool ALAngleDial::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (!mDial.pointInRect(x, y))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    mDragging = true;
    gFocusMgr.setMouseCapture(this);
    turn(x, y);

    return true;
}

bool ALAngleDial::handleHover(S32 x, S32 y, MASK mask)
{
    if (mDragging)
    {
        turn(x, y);

        return true;
    }

    return LLUICtrl::handleHover(x, y, mask);
}

bool ALAngleDial::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (!mDragging)
    {
        return LLUICtrl::handleMouseUp(x, y, mask);
    }

    mDragging = false;
    gFocusMgr.setMouseCapture(nullptr);
    turn(x, y);
    onCommit();

    return true;
}
