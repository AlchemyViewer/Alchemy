/**
 * @file alcolorpicker.cpp
 * @brief A colour chosen by eye: a hue ring, a shade square, and the channels.
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

#include "alcolorpicker.h"

#include "llfontgl.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "lluictrlfactory.h"
#include "lluicolortable.h"

#include <fmt/format.h>

#include <array>
#include <cmath>

static LLDefaultChildRegistry::Register<ALColorPicker> r("color_picker");

namespace
{
    constexpr S32 GAP = 8;
    constexpr S32 SLIDER_GAP = 4;
    // What the sliders need before the ring may have any of the width, and
    // what a slider is worth reading at. Everything else here grows with
    // the widget, because a colour picker is looked at rather than read and
    // the whole point of making one resizable is that it gets bigger.
    constexpr S32 CHANNELS_MIN_WIDTH = 210;
    constexpr S32 SLIDER_MIN_HEIGHT = 18;
    constexpr S32 SLIDER_MAX_HEIGHT = 34;
    constexpr S32 LABEL_WIDTH = 14;
    constexpr S32 HARMONY_ROWS = 2;
    constexpr S32 HARMONY_COLUMNS = 11;
    constexpr S32 RING_STEPS = 96;
    constexpr S32 TRACK_STEPS = 48;

    LLColor4 hsvColor(F32 h, F32 s, F32 v, F32 a)
    {
        h = h - std::floor(h);
        const F32 sector = h * 6.f;
        const S32 i = (S32)std::floor(sector) % 6;
        const F32 f = sector - std::floor(sector);
        const F32 p = v * (1.f - s);
        const F32 q = v * (1.f - s * f);
        const F32 t = v * (1.f - s * (1.f - f));
        switch (i)
        {
        case 0:  return LLColor4(v, t, p, a);
        case 1:  return LLColor4(q, v, p, a);
        case 2:  return LLColor4(p, v, t, a);
        case 3:  return LLColor4(p, q, v, a);
        case 4:  return LLColor4(t, p, v, a);
        default: return LLColor4(v, p, q, a);
        }
    }

    void toHSVOf(const LLColor4& color, F32& h, F32& s, F32& v)
    {
        const F32 r = color.mV[VRED];
        const F32 g = color.mV[VGREEN];
        const F32 b = color.mV[VBLUE];
        const F32 high = llmax(r, g, b);
        const F32 low = llmin(r, llmin(g, b));
        const F32 span = high - low;

        v = high;
        s = high > 0.f ? span / high : 0.f;
        if (span <= 0.f)
        {
            // Grey has no hue of its own, so it keeps the one it was given.
            return;
        }
        if (high == r)      { h = (g - b) / span / 6.f; }
        else if (high == g) { h = (2.f + (b - r) / span) / 6.f; }
        else                { h = (4.f + (r - g) / span) / 6.f; }
        h = h - std::floor(h);
    }

    // Written into the caller's buffer, since it is drawn every frame and
    // the font takes a view.
    typedef std::array<char, 8> hex_buf_t;
    std::string_view hexOf(hex_buf_t& buf, const LLColor4& color)
    {
        const auto byte = [](F32 v) { return llclamp((S32)llround(v * 255.f), 0, 255); };
        const auto result = fmt::format_to_n(buf.data(), buf.size(), "#{:02x}{:02x}{:02x}",
                                             byte(color.mV[VRED]), byte(color.mV[VGREEN]), byte(color.mV[VBLUE]));
        return std::string_view(buf.data(), llmin(result.size, buf.size()));
    }
}

ALColorPicker::Params::Params()
:   show_alpha("show_alpha", true)
{
}

ALColorPicker::ALColorPicker(const Params& p)
:   LLUICtrl(p),
    mShowAlpha(p.show_alpha)
{
    setColor(LLColor4::white);
    layout();
}

ALColorPicker::~ALColorPicker() = default;

void ALColorPicker::setColor(const LLColor4& color)
{
    mColor = color;
    toHSV();
}

void ALColorPicker::toHSV()
{
    toHSVOf(mColor, mHue, mSat, mVal);
}

void ALColorPicker::fromHSV()
{
    mColor = hsvColor(mHue, mSat, mVal, mColor.mV[VALPHA]);
}

void ALColorPicker::setValue(const LLSD& value)
{
    if (value.isString())
    {
        LLColor4 parsed;
        LLColor4::parseColor4(value.asString(), &parsed);
        setColor(parsed);
        return;
    }
    setColor(LLColor4(value));
}

LLSD ALColorPicker::getValue() const
{
    return fmt::format("{:.3f}, {:.3f}, {:.3f}, {:.3f}", mColor.mV[VRED], mColor.mV[VGREEN],
                       mColor.mV[VBLUE], mColor.mV[VALPHA]);
}

void ALColorPicker::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
}

// The ring takes a square of the left, as tall as there is room for; the
// channels take what is left of the width, and the strip of what goes with
// the colour sits above them.
void ALColorPicker::layout()
{
    const S32 width = getRect().getWidth();
    const S32 height = getRect().getHeight();
    // The ring takes what is left once the sliders have what they need,
    // rather than a fixed share: widening the widget widens the ring.
    const S32 side = llmax(60, llmin(height, width - CHANNELS_MIN_WIDTH - GAP));

    mRingBox = LLRect(0, height, side, height - side);

    // A band in proportion to the ring, so that a big ring is not a hairline
    // around a big hole.
    mRingWidth = llclamp(side / 7, 12, 40);

    // The shades of the hue fill the circle the ring encloses.
    const S32 inner = side / 2 - mRingWidth;
    const S32 square = (S32)(inner * 1.414f) - 2;
    const S32 cx = mRingBox.mLeft + side / 2;
    const S32 cy = mRingBox.mBottom + side / 2;
    mSquare = LLRect(cx - square / 2, cy + square / 2, cx + square / 2, cy - square / 2);

    const S32 right_left = side + GAP;
    const S32 rows = mShowAlpha ? 7 : 6;
    // The strip of what goes with the colour keeps a fixed share of the
    // height; the sliders share what is left, so they grow with the widget
    // instead of leaving a field of nothing under them.
    const S32 harmony_height = llclamp(height / 6, 30, 64);
    mHarmonies = LLRect(right_left, height, width, height - harmony_height);
    mChannels = LLRect(right_left, mHarmonies.mBottom - GAP, width, 0);

    const S32 room = mChannels.getHeight() - GAP - (rows - 1) * SLIDER_GAP - 16;
    mSliderHeight = llclamp(room / llmax(1, rows), SLIDER_MIN_HEIGHT, SLIDER_MAX_HEIGHT);
}

void ALColorPicker::draw()
{
    drawRing();
    drawSquare();
    drawHarmonies();
    drawChannels();
    LLUICtrl::draw();
}

// The hue ring: one strip of segments, each pair of vertices the hue at
// that angle. Zero is to the right and it turns the way a clock does not,
// which is the way every colour wheel is drawn.
void ALColorPicker::drawRing() const
{
    const F32 outer = mRingBox.getWidth() * 0.5f;
    const F32 inner = outer - (F32)mRingWidth;
    if (inner <= 2.f)
    {
        return;
    }
    const F32 cx = (F32)(mRingBox.mLeft + mRingBox.getWidth() / 2);
    const F32 cy = (F32)(mRingBox.mBottom + mRingBox.getHeight() / 2);

    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLE_STRIP);
    for (S32 i = 0; i <= RING_STEPS; ++i)
    {
        const F32 fraction = (F32)i / RING_STEPS;
        const F32 angle = fraction * F_TWO_PI;
        const LLColor4 hue = hsvColor(fraction, 1.f, 1.f, 1.f);
        gGL.color4fv(hue.mV);
        gGL.vertex2f(cx + outer * cosf(angle), cy + outer * sinf(angle));
        gGL.vertex2f(cx + inner * cosf(angle), cy + inner * sinf(angle));
    }
    gGL.end();

    // Where the hue sits, on the middle of the band.
    const F32 angle = mHue * F_TWO_PI;
    const F32 radius = (outer + inner) * 0.5f;
    gl_circle_2d(cx + radius * cosf(angle), cy + radius * sinf(angle), 5.f, 12, false);
}

// The shades of one hue, as one quad: white and the hue along the top,
// black along the bottom, which interpolates to exactly the square every
// picker draws.
void ALColorPicker::drawSquare() const
{
    if (mSquare.getWidth() <= 0)
    {
        return;
    }
    const LLColor4 hue = hsvColor(mHue, 1.f, 1.f, 1.f);

    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLES);
    {
        const auto corner = [](const LLColor4& c, S32 x, S32 y)
        {
            gGL.color4fv(c.mV);
            gGL.vertex2i(x, y);
        };
        corner(LLColor4::white, mSquare.mLeft,  mSquare.mTop);
        corner(LLColor4::black, mSquare.mLeft,  mSquare.mBottom);
        corner(LLColor4::black, mSquare.mRight, mSquare.mBottom);

        corner(LLColor4::white, mSquare.mLeft,  mSquare.mTop);
        corner(LLColor4::black, mSquare.mRight, mSquare.mBottom);
        corner(hue,             mSquare.mRight, mSquare.mTop);
    }
    gGL.end();

    gl_rect_2d(mSquare, LLColor4(0.f, 0.f, 0.f, 0.5f), false);

    const S32 x = mSquare.mLeft + (S32)(mSat * mSquare.getWidth());
    const S32 y = mSquare.mBottom + (S32)(mVal * mSquare.getHeight());
    gl_circle_2d((F32)x, (F32)y, 5.f, 12, false);
}

// What goes with the colour: the hues around it on one row, and the same
// hue lighter and darker on the other. The first cell is what is picked.
LLColor4 ALColorPicker::harmony(S32 index) const
{
    if (index < HARMONY_COLUMNS)
    {
        const F32 step = 1.f / HARMONY_COLUMNS;
        return hsvColor(mHue + step * (index + 1), mSat, mVal, mColor.mV[VALPHA]);
    }
    const S32 i = index - HARMONY_COLUMNS;
    const F32 fraction = (F32)(i + 1) / (HARMONY_COLUMNS + 1);
    return hsvColor(mHue, llclamp(mSat * (0.4f + fraction), 0.f, 1.f), fraction, mColor.mV[VALPHA]);
}

void ALColorPicker::drawHarmonies() const
{
    if (mHarmonies.getWidth() <= 0)
    {
        return;
    }
    const S32 cell_height = mHarmonies.getHeight() / HARMONY_ROWS;
    const S32 current = cell_height * 2;

    gl_rect_2d(LLRect(mHarmonies.mLeft, mHarmonies.mTop, mHarmonies.mLeft + current,
                      mHarmonies.mTop - mHarmonies.getHeight()), mColor, true);

    const S32 left = mHarmonies.mLeft + current + 4;
    const S32 cell_width = llmax(1, (mHarmonies.mRight - left) / HARMONY_COLUMNS);
    for (S32 row = 0; row < HARMONY_ROWS; ++row)
    {
        for (S32 column = 0; column < HARMONY_COLUMNS; ++column)
        {
            const LLRect cell(left + column * cell_width,
                              mHarmonies.mTop - row * cell_height,
                              left + (column + 1) * cell_width - 1,
                              mHarmonies.mTop - (row + 1) * cell_height + 1);
            gl_rect_2d(cell, harmony(row * HARMONY_COLUMNS + column), true);
        }
    }
}

// One channel: a track that shows what the value would be all the way
// along it, so the slider says what it does rather than only where it is.
void ALColorPicker::drawSlider(const LLRect& track, F32 fraction, const LLColor4& from,
                               const LLColor4& to, bool hue_track, const std::string& label) const
{
    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    font->renderUTF8(label, 0, track.mLeft - LABEL_WIDTH, track.mBottom + 4, ink.get(),
                     LLFontGL::LEFT, LLFontGL::BOTTOM);

    gGL.getTextureSlot(0)->unbind();
    gGL.begin(LLRender::TRIANGLE_STRIP);
    for (S32 i = 0; i <= TRACK_STEPS; ++i)
    {
        const F32 at = (F32)i / TRACK_STEPS;
        const LLColor4 color = hue_track ? hsvColor(at, 1.f, 1.f, 1.f)
                                         : LLColor4(from + (to - from) * at);
        const F32 x = (F32)track.mLeft + at * track.getWidth();
        gGL.color4fv(color.mV);
        gGL.vertex2f(x, (F32)track.mTop);
        gGL.vertex2f(x, (F32)track.mBottom);
    }
    gGL.end();

    const S32 at = track.mLeft + (S32)(fraction * track.getWidth());
    gl_rect_2d(LLRect(at - 1, track.mTop + 2, at + 2, track.mBottom - 2), LLColor4::white, true);
    gl_rect_2d(track, LLColor4(0.f, 0.f, 0.f, 0.5f), false);
}

void ALColorPicker::drawChannels() const
{
    const S32 left = mChannels.mLeft + LABEL_WIDTH;
    const S32 width = mChannels.mRight - left;
    if (width <= 0)
    {
        return;
    }

    struct Row { const char* label; F32 fraction; LLColor4 from; LLColor4 to; bool hue; };
    const LLColor4 opaque(mColor.mV[VRED], mColor.mV[VGREEN], mColor.mV[VBLUE], 1.f);
    const Row rows[] = {
        { "H", mHue, LLColor4::black, LLColor4::white, true },
        { "S", mSat, hsvColor(mHue, 0.f, mVal, 1.f), hsvColor(mHue, 1.f, mVal, 1.f), false },
        { "V", mVal, hsvColor(mHue, mSat, 0.f, 1.f), hsvColor(mHue, mSat, 1.f, 1.f), false },
        { "R", mColor.mV[VRED],   LLColor4(0.f, opaque.mV[VGREEN], opaque.mV[VBLUE], 1.f),
                                  LLColor4(1.f, opaque.mV[VGREEN], opaque.mV[VBLUE], 1.f), false },
        { "G", mColor.mV[VGREEN], LLColor4(opaque.mV[VRED], 0.f, opaque.mV[VBLUE], 1.f),
                                  LLColor4(opaque.mV[VRED], 1.f, opaque.mV[VBLUE], 1.f), false },
        { "B", mColor.mV[VBLUE],  LLColor4(opaque.mV[VRED], opaque.mV[VGREEN], 0.f, 1.f),
                                  LLColor4(opaque.mV[VRED], opaque.mV[VGREEN], 1.f, 1.f), false },
        { "A", mColor.mV[VALPHA], LLColor4(opaque.mV[VRED], opaque.mV[VGREEN], opaque.mV[VBLUE], 0.f),
                                  opaque, false },
    };

    const S32 count = mShowAlpha ? 7 : 6;
    S32 top = mChannels.mTop;
    for (S32 i = 0; i < count; ++i)
    {
        // A gap between the two ways of saying the same colour.
        if (i == 3)
        {
            top -= GAP;
        }
        const LLRect track(left, top, mChannels.mRight, top - mSliderHeight);
        if (track.mBottom < mChannels.mBottom)
        {
            break;
        }
        drawSlider(track, rows[i].fraction, rows[i].from, rows[i].to, rows[i].hue, rows[i].label);
        top -= mSliderHeight + SLIDER_GAP;
    }

    static const LLUIColor ink = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    const LLFontGL* font = LLFontGL::getFontMonospace();
    hex_buf_t hex;
    font->renderUTF8(hexOf(hex, mColor), 0, mChannels.mRight, mChannels.mBottom + 2, ink.get(),
                     LLFontGL::RIGHT, LLFontGL::BOTTOM);
}

ALColorPicker::Grab ALColorPicker::grabAt(S32 x, S32 y) const
{
    if (mSquare.pointInRect(x, y))
    {
        return Grab::Square;
    }
    if (mRingBox.pointInRect(x, y))
    {
        const F32 cx = (F32)(mRingBox.mLeft + mRingBox.getWidth() / 2);
        const F32 cy = (F32)(mRingBox.mBottom + mRingBox.getHeight() / 2);
        const F32 distance = std::hypot(x - cx, y - cy);
        const F32 outer = mRingBox.getWidth() * 0.5f;
        if (distance <= outer && distance >= outer - (F32)mRingWidth)
        {
            return Grab::Ring;
        }
        return Grab::None;
    }

    const S32 left = mChannels.mLeft + LABEL_WIDTH;
    const S32 count = mShowAlpha ? 7 : 6;
    S32 top = mChannels.mTop;
    for (S32 i = 0; i < count; ++i)
    {
        if (i == 3)
        {
            top -= GAP;
        }
        const LLRect track(left, top, mChannels.mRight, top - mSliderHeight);
        if (track.pointInRect(x, y))
        {
            return (Grab)((S32)Grab::Hue + i);
        }
        top -= mSliderHeight + SLIDER_GAP;
    }
    return Grab::None;
}

void ALColorPicker::apply(Grab grab, S32 x, S32 y)
{
    const S32 left = mChannels.mLeft + LABEL_WIDTH;
    const F32 along = mChannels.mRight > left
        ? llclamp((F32)(x - left) / (F32)(mChannels.mRight - left), 0.f, 1.f) : 0.f;

    switch (grab)
    {
    case Grab::Ring:
    {
        const F32 cx = (F32)(mRingBox.mLeft + mRingBox.getWidth() / 2);
        const F32 cy = (F32)(mRingBox.mBottom + mRingBox.getHeight() / 2);
        F32 angle = std::atan2((F32)y - cy, (F32)x - cx);
        if (angle < 0.f)
        {
            angle += F_TWO_PI;
        }
        mHue = angle / F_TWO_PI;
        fromHSV();
        break;
    }
    case Grab::Square:
        mSat = llclamp((F32)(x - mSquare.mLeft) / (F32)llmax(1, mSquare.getWidth()), 0.f, 1.f);
        mVal = llclamp((F32)(y - mSquare.mBottom) / (F32)llmax(1, mSquare.getHeight()), 0.f, 1.f);
        fromHSV();
        break;

    case Grab::Hue:         mHue = along; fromHSV(); break;
    case Grab::Saturation:  mSat = along; fromHSV(); break;
    case Grab::Value:       mVal = along; fromHSV(); break;

    // A channel of the other three moves the colour itself, and the hue,
    // saturation and value follow from where that put it.
    case Grab::Red:     mColor.mV[VRED] = along;    toHSV(); break;
    case Grab::Green:   mColor.mV[VGREEN] = along;  toHSV(); break;
    case Grab::Blue:    mColor.mV[VBLUE] = along;   toHSV(); break;
    case Grab::Alpha:   mColor.mV[VALPHA] = along;  break;

    default:
        return;
    }
    onCommit();
}

bool ALColorPicker::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // The strip is a choice rather than a drag: one of those is the colour.
    if (mHarmonies.pointInRect(x, y))
    {
        const S32 cell_height = mHarmonies.getHeight() / HARMONY_ROWS;
        const S32 left = mHarmonies.mLeft + cell_height * 2 + 4;
        if (x >= left)
        {
            const S32 cell_width = llmax(1, (mHarmonies.mRight - left) / HARMONY_COLUMNS);
            const S32 column = llclamp((x - left) / cell_width, 0, HARMONY_COLUMNS - 1);
            const S32 row = llclamp((mHarmonies.mTop - y) / llmax(1, cell_height), 0, HARMONY_ROWS - 1);
            setColor(harmony(row * HARMONY_COLUMNS + column));
            onCommit();
        }
        return true;
    }

    mGrab = grabAt(x, y);
    if (mGrab != Grab::None)
    {
        gFocusMgr.setMouseCapture(this);
        apply(mGrab, x, y);
        return true;
    }
    return LLUICtrl::handleMouseDown(x, y, mask);
}

bool ALColorPicker::handleHover(S32 x, S32 y, MASK mask)
{
    if (mGrab != Grab::None && hasMouseCapture())
    {
        apply(mGrab, x, y);
        return true;
    }
    return LLUICtrl::handleHover(x, y, mask);
}

bool ALColorPicker::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mGrab != Grab::None && hasMouseCapture())
    {
        apply(mGrab, x, y);
        mGrab = Grab::None;
        gFocusMgr.setMouseCapture(nullptr);
        return true;
    }
    return LLUICtrl::handleMouseUp(x, y, mask);
}
