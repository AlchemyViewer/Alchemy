/**
 * @file alcolorwheelctrl.cpp
 * @brief Colourist's wheel: hue ring, draggable puck, master slider, fields
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
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

#include "llviewerprecompiledheaders.h"

#include "alcolorwheelctrl.h"

#include "llbutton.h"
#include "llfocusmgr.h"
#include "lllineeditor.h"
#include "lllocalcliprect.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llsliderctrl.h"
#include "lltextbox.h"
#include "lltextvalidate.h"
#include "lluicolortable.h"
#include "llwindow.h"

#include <cmath>

static LLDefaultChildRegistry::Register<ALColorWheelCtrl> r("color_wheel");

namespace
{
constexpr S32 LABEL_HEIGHT = 15;
constexpr S32 ROW_HEIGHT = 16;
constexpr S32 CAPTION_HEIGHT = 11;
constexpr S32 GAP = 3;
constexpr S32 RESET_SIZE = 15;
/// Extra pixels around the puck that still count as grabbing it.
constexpr S32 GRAB_SLOP = 4;

/// Move a child and let it re-derive whatever it computes from its own size.
///
/// setRect() alone is not enough: it writes the rectangle and nothing else, so
/// anything a widget works out in reshape() keeps its old answer.
/// LLLineEditor is the one that bites here -- updateTextPadding() derives the
/// drawable text span from getRect().getWidth() and runs only from reshape(),
/// so an editor built at a placeholder width and then setRect() into place
/// draws its background across the new rect and its text into the old
/// one-pixel window. It reads as an editor that accepts typing and displays
/// nothing.
///
/// called_from_parent must be true, and it is: we are the parent, laying the
/// child out. Pass false and LLView::reshape tells mParentView to reshape in
/// turn (llview.cpp:1498) -- straight back into our own reshape, updateLayout
/// and this function. That notify sits outside the zero-delta guard, so the
/// recursion does not even need a size change to run away; it overflows the
/// stack the moment the widget is first laid out.
void placeChild(LLView* child, const LLRect& rect)
{
    if (!child)
    {
        return;
    }
    child->setRect(rect);
    child->reshape(rect.getWidth(), rect.getHeight(), true);
}
}

ALColorWheelCtrl::Params::Params()
:   label("label"),
    centre("centre", 0.f),
    min_value("min_value", -0.5f),
    max_value("max_value", 0.5f),
    lock_master("lock_master", false),
    ring_thickness("ring_thickness", 10),
    ring_steps("ring_steps", 72),
    puck_radius("puck_radius", 4),
    decimal_digits("decimal_digits", 2),
    border_color("border_color", LLUIColorTable::instance().getColor("DefaultShadowLight")),
    face_color("face_color", LLUIColorTable::instance().getColor("MenuDefaultBgColor")),
    crosshair_color("crosshair_color", LLUIColorTable::instance().getColor("DefaultShadowDark"))
{
    changeDefault(mouse_opaque, true);
}

ALColorWheelCtrl::ALColorWheelCtrl(const ALColorWheelCtrl::Params& p)
:   LLUICtrl(p),
    mBorderColor(p.border_color),
    mFaceColor(p.face_color),
    mCrosshairColor(p.crosshair_color),
    mRingThickness(llmax(3, p.ring_thickness())),
    mRingSteps(llclamp(p.ring_steps(), 12, 360)),
    mPuckRadius(llmax(2, p.puck_radius())),
    mDecimalDigits(llclamp(p.decimal_digits(), 0, 4))
{
    mModel.configure(p.centre, p.min_value, p.max_value, p.lock_master);
    mModel.reset();

    // Ring colours depend only on the angle, so they are built once here
    // rather than per frame. ALColorWheelModel::ringColor is the same function
    // the puck goes through, which is what stops the ring and the puck
    // disagreeing about where a hue is.
    mRingColors.reserve(mRingSteps);
    for (S32 i = 0; i < mRingSteps; ++i)
    {
        const LLVector3 c = ALColorWheelModel::ringColor(F_TWO_PI * (F32)i / (F32)mRingSteps);
        mRingColors.emplace_back(c.mV[VX], c.mV[VY], c.mV[VZ], 1.f);
    }

    // Every child below seeds its Params from getDefaultParams<T>() first.
    // LLUICtrlFactory::defaultBuilder does exactly that before it reads XUI,
    // so the skin's widgets/*.xml supplies fonts, text colours, borders and
    // background images. A bare Params misses all of it -- which renders as a
    // line editor that accepts typing and displays nothing, because it has no
    // text colour rather than no text.
    if (!p.label().empty())
    {
        LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        tp.name("wheel_label");
        tp.initial_value(p.label());
        tp.rect(LLRect(0, LABEL_HEIGHT, 1, 0));
        mLabel = LLUICtrlFactory::create<LLTextBox>(tp);
        addChild(mLabel);
    }

    {
        LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
        bp.name("wheel_reset");
        bp.rect(LLRect(0, RESET_SIZE, RESET_SIZE, 0));
        bp.scale_image(true);
        bp.tool_tip("Reset to default");
        bp.click_callback.function([this](LLUICtrl*, const LLSD&) { onReset(); });
        mReset = LLUICtrlFactory::create<LLButton>(bp);
        // Set after construction: the Params overlay slot wants an LLUIImage*,
        // whereas this resolves the skin's name for us.
        mReset->setImageOverlay("Refresh_Off", LLFontGL::HCENTER);
        addChild(mReset);
    }

    if (!mModel.isMasterLocked())
    {
        LLSliderCtrl::Params sp(LLUICtrlFactory::getDefaultParams<LLSliderCtrl>());
        sp.name("wheel_master");
        sp.rect(LLRect(0, ROW_HEIGHT, 1, 0));
        sp.min_value(mModel.getMin());
        sp.max_value(mModel.getMax());
        sp.increment(0.01f);
        sp.decimal_digits(mDecimalDigits);
        sp.show_text(true);
        sp.can_edit_text(true);
        sp.label_width(0);
        sp.text_width(40);
        sp.tool_tip("Master: moves all three channels together");
        sp.commit_callback.function([this](LLUICtrl*, const LLSD&) { onMasterCommit(); });
        mMaster = LLUICtrlFactory::create<LLSliderCtrl>(sp);
        addChild(mMaster);
    }

    static const char* const CHANNEL_NAME[3] = { "wheel_r", "wheel_g", "wheel_b" };
    static const char* const CHANNEL_LABEL[3] = { "R", "G", "B" };
    static const char* const CHANNEL_TIP[3] = { "Red channel", "Green channel", "Blue channel" };

    for (S32 i = 0; i < 3; ++i)
    {
        LLLineEditor::Params lp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
        lp.name(CHANNEL_NAME[i]);
        lp.rect(LLRect(0, ROW_HEIGHT, 1, 0));
        lp.max_length.chars(8);
        lp.prevalidator(&LLTextValidate::validateFloat);
        lp.commit_on_focus_lost(true);
        lp.tool_tip(CHANNEL_TIP[i]);
        lp.commit_callback.function([this, i](LLUICtrl*, const LLSD&) { onChannelCommit(i); });
        mFields[i] = LLUICtrlFactory::create<LLLineEditor>(lp);
        addChild(mFields[i]);

        // One caption per field rather than a single centred row: LLTextBase
        // has no horizontal alignment param, so alignment here is a matter of
        // where the rect is put, and a letter per column is clearer anyway.
        LLTextBox::Params cp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        cp.name(std::string("wheel_caption_") + CHANNEL_LABEL[i]);
        cp.initial_value(std::string(CHANNEL_LABEL[i]));
        cp.font(LLFontGL::getFontSansSerifSmall());
        cp.rect(LLRect(0, CAPTION_HEIGHT, 1, 0));
        mCaptions[i] = LLUICtrlFactory::create<LLTextBox>(cp);
        addChild(mCaptions[i]);
    }

    updateLayout();
    syncChildren();
}

void ALColorWheelCtrl::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    updateLayout();
}

void ALColorWheelCtrl::setEnabled(bool enabled)
{
    LLUICtrl::setEnabled(enabled);
    if (mMaster)
    {
        mMaster->setEnabled(enabled);
    }
    if (mReset)
    {
        mReset->setEnabled(enabled);
    }
    for (LLLineEditor* field : mFields)
    {
        if (field)
        {
            field->setEnabled(enabled);
        }
    }
    // The captions too, or a greyed-out wheel keeps three bright R G B labels
    // under it and reads as half disabled.
    for (LLTextBox* caption : mCaptions)
    {
        if (caption)
        {
            caption->setEnabled(enabled);
        }
    }
    if (mLabel)
    {
        mLabel->setEnabled(enabled);
    }
}

void ALColorWheelCtrl::updateLayout()
{
    const LLRect r = getLocalRect();
    if (r.getWidth() <= 0 || r.getHeight() <= 0)
    {
        return;
    }

    S32 top = r.mTop;
    placeChild(mLabel, LLRect(r.mLeft, top, r.mRight - RESET_SIZE - 2, top - LABEL_HEIGHT));
    placeChild(mReset, LLRect(r.mRight - RESET_SIZE, top, r.mRight, top - RESET_SIZE));
    top -= llmax(LABEL_HEIGHT, RESET_SIZE) + GAP;

    // Fixed rows are measured up from the bottom; whatever is left in the
    // middle becomes the ring, so a shorter widget shrinks the wheel instead
    // of clipping its controls.
    S32 bottom = r.mBottom;
    const S32 field_gap = 2;
    const S32 field_width = (r.getWidth() - field_gap * 2) / 3;

    for (S32 i = 0; i < 3; ++i)
    {
        const S32 left = r.mLeft + i * (field_width + field_gap);
        placeChild(mCaptions[i], LLRect(left, bottom + CAPTION_HEIGHT, left + field_width, bottom));
    }
    bottom += CAPTION_HEIGHT + 1;

    for (S32 i = 0; i < 3; ++i)
    {
        const S32 left = r.mLeft + i * (field_width + field_gap);
        placeChild(mFields[i], LLRect(left, bottom + ROW_HEIGHT, left + field_width, bottom));
    }
    bottom += ROW_HEIGHT + GAP;

    if (mMaster)
    {
        placeChild(mMaster, LLRect(r.mLeft, bottom + ROW_HEIGHT, r.mRight, bottom));
        bottom += ROW_HEIGHT + GAP;
    }

    // Square, centred in what remains.
    const S32 avail_h = llmax(0, top - bottom);
    const S32 side = llmin(r.getWidth(), avail_h);
    const S32 left = r.mLeft + (r.getWidth() - side) / 2;
    const S32 wheel_bottom = bottom + (avail_h - side) / 2;
    mWheelRect = LLRect(left, wheel_bottom + side, left + side, wheel_bottom);
}

void ALColorWheelCtrl::wheelGeometry(F32& cx, F32& cy, F32& radius) const
{
    cx = (F32)mWheelRect.mLeft + (F32)mWheelRect.getWidth() * 0.5f;
    cy = (F32)mWheelRect.mBottom + (F32)mWheelRect.getHeight() * 0.5f;
    // The puck rides the inner edge of the ring, so the draggable radius stops
    // where the colour band begins.
    radius = llmax(1.f, (F32)llmin(mWheelRect.getWidth(), mWheelRect.getHeight()) * 0.5f
                        - (F32)mRingThickness);
}

bool ALColorWheelCtrl::pointToPolar(S32 x, S32 y, F32& hue_out, F32& sat_out) const
{
    F32 cx, cy, radius;
    wheelGeometry(cx, cy, radius);

    const F32 dx = (F32)x - cx;
    const F32 dy = (F32)y - cy;
    const F32 dist = sqrtf(dx * dx + dy * dy);

    if (dist < 1e-3f)
    {
        // Dead centre has no direction. Reporting atan2(0,0) here is what
        // makes a puck dragged through the middle flick to red on the way out.
        hue_out = mLastHue;
        sat_out = 0.f;
        return true;
    }

    hue_out = atan2f(dy, dx);
    if (hue_out < 0.f)
    {
        hue_out += F_TWO_PI;
    }
    // Clamp to the rim rather than refusing to move: pushing past maximum is a
    // normal thing to do with a grading wheel.
    sat_out = llmin(dist / radius, 1.f) * mModel.getMaxSat();
    return true;
}

bool ALColorWheelCtrl::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (!getEnabled())
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    F32 cx, cy, radius;
    wheelGeometry(cx, cy, radius);

    const F32 dx = (F32)x - cx;
    const F32 dy = (F32)y - cy;
    if (sqrtf(dx * dx + dy * dy) > radius + (F32)(mPuckRadius + GRAB_SLOP))
    {
        return LLUICtrl::handleMouseDown(x, y, mask);
    }

    mDragging = true;
    gFocusMgr.setMouseCapture(this);
    setFocus(true);

    F32 hue, sat;
    pointToPolar(x, y, hue, sat);
    mLastHue = hue;
    mModel.setPolar(hue, sat);
    publish();
    return true;
}

bool ALColorWheelCtrl::handleHover(S32 x, S32 y, MASK mask)
{
    if (gFocusMgr.getMouseCapture() != this || !mDragging)
    {
        getWindow()->setCursor(UI_CURSOR_ARROW);
        return true;
    }

    F32 hue, sat;
    pointToPolar(x, y, hue, sat);
    mLastHue = hue;
    mModel.setPolar(hue, sat);
    publish();

    getWindow()->setCursor(UI_CURSOR_ARROW);
    return true;
}

bool ALColorWheelCtrl::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (gFocusMgr.getMouseCapture() != this)
    {
        return LLUICtrl::handleMouseUp(x, y, mask);
    }
    gFocusMgr.setMouseCapture(nullptr);
    mDragging = false;
    return true;
}

bool ALColorWheelCtrl::handleDoubleClick(S32 x, S32 y, MASK mask)
{
    // Belt and braces, as handleMouseDown's enabled test is: parent dispatch
    // already skips disabled children, so this cannot fire disabled today,
    // but a reset that writes the setting must never ride on that staying so.
    if (!getEnabled())
    {
        return LLUICtrl::handleDoubleClick(x, y, mask);
    }

    F32 cx, cy, radius;
    wheelGeometry(cx, cy, radius);
    const F32 dx = (F32)x - cx;
    const F32 dy = (F32)y - cy;
    if (sqrtf(dx * dx + dy * dy) <= radius)
    {
        // Double-clicking the face to neutralise is the gesture every grading
        // tool uses, and it beats hunting for the reset glyph mid-drag.
        onReset();
        return true;
    }
    return LLUICtrl::handleDoubleClick(x, y, mask);
}

void ALColorWheelCtrl::setValue(const LLSD& value)
{
    if (!value.isArray() || value.size() < 3)
    {
        return;
    }
    mModel.setRGB(LLVector3((F32)value[0].asReal(),
                            (F32)value[1].asReal(),
                            (F32)value[2].asReal()));
    if (mModel.getSat() > 0.f)
    {
        mLastHue = mModel.getHue();
    }
    syncChildren();
}

LLSD ALColorWheelCtrl::getValue() const
{
    LLSD out = LLSD::emptyArray();
    for (S32 i = 0; i < 3; ++i)
    {
        out.append(LLSD::Real(mModel.getRGB().mV[i]));
    }
    return out;
}

void ALColorWheelCtrl::publish()
{
    syncChildren();
    setControlValue(getValue());
    onCommit();
}

void ALColorWheelCtrl::syncChildren()
{
    // The children's commit handlers write back into the model, so a plain
    // setValue here would loop.
    if (mUpdating)
    {
        return;
    }
    mUpdating = true;

    if (mMaster)
    {
        mMaster->setValue(mModel.getMaster());
    }
    for (S32 i = 0; i < 3; ++i)
    {
        if (mFields[i])
        {
            mFields[i]->setText(llformat("%.*f", mDecimalDigits, mModel.getRGB().mV[i]));
        }
    }

    mUpdating = false;
}

void ALColorWheelCtrl::onMasterCommit()
{
    if (mUpdating || !mMaster)
    {
        return;
    }
    mModel.setMaster((F32)mMaster->getValue().asReal());
    publish();
}

void ALColorWheelCtrl::onChannelCommit(S32 index)
{
    if (mUpdating || index < 0 || index > 2 || !mFields[index])
    {
        return;
    }
    mModel.setChannel(index, (F32)atof(mFields[index]->getText().c_str()));
    if (mModel.getSat() > 0.f)
    {
        mLastHue = mModel.getHue();
    }
    publish();
}

void ALColorWheelCtrl::onReset()
{
    mModel.reset();
    mLastHue = 0.f;
    publish();
}

void ALColorWheelCtrl::draw()
{
    F32 cx, cy, radius;
    wheelGeometry(cx, cy, radius);

    if (radius > 1.f)
    {
        const F32 outer = radius + (F32)mRingThickness;

        // The hue ring. Washers draw around the current origin, so the
        // translate is ours to do.
        //
        // It MUST be translateUI, paired with pushUIMatrix/popUIMatrix. Those
        // save and restore the UI offset stack; translatef writes the modelview
        // matrix, which only pushMatrix/popMatrix restore. Mixing the two
        // leaves the translate applied forever, and since the UI draws in one
        // pass everything after this widget -- the rest of the floater, the
        // toolbars, the top-right buttons -- slides off screen. gl_circle_2d
        // is the reference for this pairing; ALPanelMusicTicker uses the other
        // one, pushMatrix with translatef, which is why it pre-scales by
        // getUIScale() and this does not: vertex3f already applies
        // (vertex + mUIOffset) * mUIScale.
        gGL.pushUIMatrix();
        {
            gGL.translateUI(cx, cy, 0.f);
            gl_washer_angular_2d(outer, radius, mRingColors);
        }
        gGL.popUIMatrix();

        // Face, then the neutral crosshair, then the puck on top.
        gGL.color4fv(mFaceColor.get().mV);
        gl_circle_2d(cx, cy, radius, 48, true);

        const LLColor4 cross = mCrosshairColor.get();
        gl_line_2d(ll_round(cx - radius), ll_round(cy), ll_round(cx + radius), ll_round(cy), cross);
        gl_line_2d(ll_round(cx), ll_round(cy - radius), ll_round(cx), ll_round(cy + radius), cross);

        std::vector<LLVector2> rim;
        rim.reserve(48);
        for (S32 i = 0; i < 48; ++i)
        {
            const F32 a = F_TWO_PI * (F32)i / 48.f;
            rim.emplace_back(cx + cosf(a) * radius, cy + sinf(a) * radius);
        }
        gl_polyline_2d(rim, mBorderColor.get(), 1.f, true);

        // Puck position comes back out of the stored value, never from the
        // pointer, so it can only ever sit where the setting actually is.
        const F32 sat_max = mModel.getMaxSat();
        const F32 frac = (sat_max > 0.f) ? mModel.getSat() / sat_max : 0.f;
        const F32 hue = (mModel.getSat() > 0.f) ? mModel.getHue() : mLastHue;
        const F32 px = cx + cosf(hue) * frac * radius;
        const F32 py = cy + sinf(hue) * frac * radius;

        const LLVector3 tint = ALColorWheelModel::ringColor(hue);
        const LLColor4 puck_fill(tint.mV[VX], tint.mV[VY], tint.mV[VZ], 1.f);

        gGL.color4fv(puck_fill.mV);
        gl_circle_2d(px, py, (F32)mPuckRadius, 16, true);

        std::vector<LLVector2> ring;
        ring.reserve(20);
        for (S32 i = 0; i < 20; ++i)
        {
            const F32 a = F_TWO_PI * (F32)i / 20.f;
            ring.emplace_back(px + cosf(a) * (F32)mPuckRadius, py + sinf(a) * (F32)mPuckRadius);
        }
        gl_polyline_2d(ring, mDragging ? LLColor4::white : mBorderColor.get(), 1.5f, true);
    }

    LLUICtrl::draw();
}
