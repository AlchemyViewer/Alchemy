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

#include "alsurface.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llspinctrl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <cmath>
#include <fmt/format.h>

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

ALAngleDial::ALAngleDial(const Params& p) : ALPictureField(p)
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
    mDegrees = adoptBox(LLUICtrlFactory::create<LLSpinCtrl>(sp));
    layout();
}

void ALAngleDial::placeBoxes(const LLRect& picture, S32 width, S32 height)
{
    const S32 left = picture.mRight + GAP;
    const S32 top = height - 2;

    mDegrees->setShape(LLRect(left, top, left + BOX_WIDTH, top - BOX_HEIGHT));
}

void ALAngleDial::boxTyped(size_t box)
{
    mAngle = (F32)mDegrees->getValue().asReal();
}

void ALAngleDial::take(const std::vector<F32>& numbers)
{
    const F32 x = numbers.size() > 0 ? numbers[0] : -1.f;
    const F32 y = numbers.size() > 1 ? numbers[1] : 1.f;

    if (x != 0.f || y != 0.f)
    {
        mAngle = std::atan2(y, x) / DEG;

        if (mAngle < 0.f)
        {
            mAngle += 360.f;
        }
    }
}

std::string ALAngleDial::say() const
{
    return fmt::format("{:.4g} {:.4g}", std::cos(mAngle * DEG), std::sin(mAngle * DEG));
}

void ALAngleDial::showNumbers()
{
    mDegrees->setValue(LLSD(ll_round(mAngle)));
}

void ALAngleDial::pointAt(S32 x, S32 y)
{
    const LLRect& dial = picture();
    const F32 cx = F32(dial.mLeft + dial.mRight) * 0.5f;
    const F32 cy = F32(dial.mTop + dial.mBottom) * 0.5f;

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

    showNumbers();
}

void ALAngleDial::drawPicture()
{
    const LLUIColor& well = ALSurface::well();
    const LLUIColor& rim = ALSurface::rim();
    const LLUIColor& handle = ALSurface::handle();

    const LLRect& dial = picture();
    const F32 cx = F32(dial.mLeft + dial.mRight) * 0.5f;
    const F32 cy = F32(dial.mTop + dial.mBottom) * 0.5f;
    const F32 radius = F32(dial.getWidth()) * 0.5f - 2.f;

    gGL.color4fv((well.get() % 0.6f).mV);
    gl_circle_2d(cx, cy, radius, 32, true);
    gGL.color4fv((rim.get() % 0.7f).mV);
    gl_circle_2d(cx, cy, radius, 32, false);

    const S32 hx = ll_round(cx + std::cos(mAngle * DEG) * (radius - 3.f));
    const S32 hy = ll_round(cy + std::sin(mAngle * DEG) * (radius - 3.f));

    gl_line_2d(ll_round(cx), ll_round(cy), hx, hy, handle.get() % 0.7f);
    gl_rect_2d(LLRect(hx - 3, hy + 3, hx + 3, hy - 3), handle.get(), true);
}
