/**
 * @file alcornerfield.cpp
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

#include "linden_common.h"
#include "alcornerfield.h"

#include "llcheckboxctrl.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llspinctrl.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <cmath>
#include <fmt/format.h>
#include <sstream>

static LLDefaultChildRegistry::Register<ALCornerField> r("corner_field");

namespace
{
    constexpr S32 PICTURE_WIDTH = 72;
    constexpr S32 BOX_WIDTH = 62;
    constexpr S32 BOX_HEIGHT = 20;
    constexpr S32 GAP = 4;

}

ALCornerField::Params::Params()
{
}

ALCornerField::ALCornerField(const Params& p) : LLUICtrl(p)
{
    // Top left and top right on the top row, bottom left and bottom right
    // under them: the boxes sit where the corners are.
    static const char* NAMES[4] = { "tl", "tr", "br", "bl" };

    for (size_t i = 0; i < 4; ++i)
    {
        LLSpinCtrl::Params sp;

        sp.name = NAMES[i];
        sp.rect = LLRect(0, BOX_HEIGHT, BOX_WIDTH, 0);
        sp.label_width = 0;
        sp.decimal_digits = 1;
        sp.increment = 0.5f;
        sp.min_value = 0.f;
        sp.max_value = 512.f;
        mSpin[i] = LLUICtrlFactory::create<LLSpinCtrl>(sp);
        mSpin[i]->setCommitCallback([this, i](LLUICtrl*, const LLSD&) { onSpin(i); });
        addChild(mSpin[i]);
    }

    LLCheckBoxCtrl::Params cp;

    cp.name = "link";
    cp.rect = LLRect(0, BOX_HEIGHT, 70, 0);
    cp.label = "Same";
    cp.initial_value = true;
    mLink = LLUICtrlFactory::create<LLCheckBoxCtrl>(cp);
    addChild(mLink);
    layout();
}

void ALCornerField::layout()
{
    const S32 height = getRect().getHeight();
    const S32 width = getRect().getWidth();

    mPicture = LLRect(0, height, llmin(PICTURE_WIDTH, width), 0);

    const S32 left = mPicture.mRight + GAP;
    const S32 top = height - 2;
    const S32 bottom_row = top - BOX_HEIGHT - GAP;

    mSpin[0]->setShape(LLRect(left, top, left + BOX_WIDTH, top - BOX_HEIGHT));
    mSpin[1]->setShape(LLRect(left + BOX_WIDTH + GAP, top, left + 2 * BOX_WIDTH + GAP, top - BOX_HEIGHT));
    mSpin[3]->setShape(LLRect(left, bottom_row, left + BOX_WIDTH, bottom_row - BOX_HEIGHT));
    mSpin[2]->setShape(LLRect(left + BOX_WIDTH + GAP, bottom_row, left + 2 * BOX_WIDTH + GAP, bottom_row - BOX_HEIGHT));

    const S32 link_left = left + 2 * BOX_WIDTH + 2 * GAP + 4;

    mLink->setShape(LLRect(link_left, top, link_left + 70, top - BOX_HEIGHT));
    mLink->setVisible(link_left + 40 <= width);
}

void ALCornerField::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
}

void ALCornerField::setRange(F32 minimum, F32 maximum, F32 step, S32 decimals)
{
    for (LLSpinCtrl* spin : mSpin)
    {
        spin->setMinValue(minimum);
        spin->setMaxValue(maximum);
        spin->setIncrement(step);
        spin->setPrecision(decimals);
    }
}

void ALCornerField::setValue(const LLSD& value)
{
    std::istringstream in(value.asString());
    std::array<F32, 4> radii{};
    size_t count = 0;

    for (F32 number; count < 4 && in >> number; ++count)
    {
        radii[count] = number;
    }

    switch (count)
    {
        case 1: mRadii = { radii[0], radii[0], radii[0], radii[0] }; break;
        case 2: mRadii = { radii[0], radii[1], radii[0], radii[1] }; break;
        default: mRadii = radii; break;
    }

    const bool same = mRadii[0] == mRadii[1] && mRadii[1] == mRadii[2] && mRadii[2] == mRadii[3];

    mLink->set(same);
    showRadii();
}

LLSD ALCornerField::getValue() const
{
    return fmt::format("{} {} {} {}", mRadii[0], mRadii[1], mRadii[2], mRadii[3]);
}

void ALCornerField::showRadii()
{
    for (size_t i = 0; i < 4; ++i)
    {
        mSpin[i]->setValue(LLSD(mRadii[i]));
    }
}

void ALCornerField::onSpin(size_t corner)
{
    const F32 value = (F32)mSpin[corner]->getValue().asReal();

    if (mLink->get())
    {
        mRadii.fill(value);
        showRadii();
    }
    else
    {
        mRadii[corner] = value;
    }

    onCommit();
}

// The rectangle with its corners rounded as the numbers say, scaled so the
// largest radius is a quarter of the picture: what is being chosen is the
// proportion, and the picture says it before the number does.
void ALCornerField::draw()
{
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor edge = LLUIColorTable::instance().getColor("DefaultShadowLight", LLColor4::black);

    const LLRect box(mPicture.mLeft + 6, mPicture.mTop - 6, mPicture.mRight - 6, mPicture.mBottom + 6);
    const F32 largest = llmax(1.f, *std::max_element(mRadii.begin(), mRadii.end()));
    const F32 scale = llmin(F32(box.getWidth()), F32(box.getHeight())) * 0.45f / largest;
    const std::array<F32, 4> r = { llmin(mRadii[0] * scale, box.getHeight() * 0.5f), llmin(mRadii[1] * scale, box.getHeight() * 0.5f),
                                   llmin(mRadii[2] * scale, box.getHeight() * 0.5f), llmin(mRadii[3] * scale, box.getHeight() * 0.5f) };

    gl_rect_2d(mPicture, edge.get() % 0.5f, true);

    // Each corner is an arc from one straight edge to the next; the four
    // arcs and the straights between them are one outline.
    const auto arc = [&](F32 cx, F32 cy, F32 radius, F32 from, F32 to)
    {
        constexpr S32 STEPS = 8;
        F32 px = cx + radius * std::cos(from);
        F32 py = cy + radius * std::sin(from);

        for (S32 i = 1; i <= STEPS; ++i)
        {
            const F32 angle = from + (to - from) * F32(i) / STEPS;
            const F32 x = cx + radius * std::cos(angle);
            const F32 y = cy + radius * std::sin(angle);

            gl_line_2d(ll_round(px), ll_round(py), ll_round(x), ll_round(y), ink.get());
            px = x;
            py = y;
        }
    };
    constexpr F32 PI_F = 3.14159265f;

    arc(box.mLeft + r[0], box.mTop - r[0], r[0], PI_F * 0.5f, PI_F);
    arc(box.mRight - r[1], box.mTop - r[1], r[1], 0.f, PI_F * 0.5f);
    arc(box.mRight - r[2], box.mBottom + r[2], r[2], -PI_F * 0.5f, 0.f);
    arc(box.mLeft + r[3], box.mBottom + r[3], r[3], PI_F, PI_F * 1.5f);
    gl_line_2d(ll_round(box.mLeft + r[0]), box.mTop, ll_round(box.mRight - r[1]), box.mTop, ink.get());
    gl_line_2d(box.mRight, ll_round(box.mTop - r[1]), box.mRight, ll_round(box.mBottom + r[2]), ink.get());
    gl_line_2d(ll_round(box.mRight - r[2]), box.mBottom, ll_round(box.mLeft + r[3]), box.mBottom, ink.get());
    gl_line_2d(box.mLeft, ll_round(box.mBottom + r[3]), box.mLeft, ll_round(box.mTop - r[0]), ink.get());

    LLUICtrl::draw();
}
