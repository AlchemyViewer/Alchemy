/**
 * @file alsettingrow.cpp
 * @brief One setting on one line: its name, a slider, its value, and the way back to its default.
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

#include "linden_common.h"

#include "alsettingrow.h"

#include "alfieldeditors.h"
#include "llbutton.h"
#include "llcontrol.h"
#include "llfontgl.h"
#include "llsdutil.h"
#include "llsliderctrl.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluictrlfactory.h"

#include <fmt/format.h>

#include <cmath>
#include <sstream>

static LLDefaultChildRegistry::Register<ALSettingRow> r("setting_row");

namespace
{
// How many digits the whole part of a number takes, at least one.
S32 wholeDigits(F32 value)
{
    const F32 magnitude = std::fabs(value);
    return magnitude < 1.f ? 1 : (S32)std::log10(magnitude) + 1;
}

// A value box wide enough for the widest number between min and max, at the
// decimals shown: the digits of the wider end, the point, a sign where the
// range goes below zero, and the slider's own padding.
S32 valueBoxWidth(const LLFontGL* font, F32 min_value, F32 max_value, S32 decimal_digits)
{
    S32 width = font->getWidth("0") * (llmax(wholeDigits(min_value), wholeDigits(max_value)) + decimal_digits);
    if (decimal_digits > 0)
    {
        width += font->getWidth(".");
    }
    if (min_value < 0.f)
    {
        width += font->getWidth("-");
    }
    return width + 8;
}
} // namespace

ALSettingRow::Params::Params()
:   label_width("label_width", 140),
    min_value("min_val", 0.f),
    max_value("max_val", 1.f),
    increment("increment", 0.1f),
    decimal_digits("decimal_digits", 3),
    text_width("text_width"),
    show_text("show_text", true),
    can_edit_text("can_edit_text", true),
    show_reset("show_reset", true),
    components("components"),
    part("part", -1),
    mouse_down_callback("mouse_down_callback"),
    mouse_up_callback("mouse_up_callback")
{
    changeDefault(follows.flags, FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT);
    // The slider's value box and the reset button take the keyboard; the row
    // around them is nowhere to stop.
    changeDefault(tab_stop, false);
}

ALSettingRow::ALSettingRow(const Params& p)
:   LLUICtrl(p),
    mLabel(p.label()),
    mDecimalDigits(p.decimal_digits),
    mPart(p.part),
    mShowReset(p.show_reset)
{
    const S32 width = getRect().getWidth();
    const S32 height = getRect().getHeight();

    std::istringstream letters(p.components());
    for (std::string letter; letters >> letter;)
    {
        mLetters.push_back(letter);
    }
    if (!mLetters.empty())
    {
        // The caption where a slider's label would be, and a box per part
        // after it, as tall as the row.
        mPart = -1;
        LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        tp.name = "setting_row_caption";
        tp.rect = LLRect(0, height - 1, p.label_width(), height - 16);
        tp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        tp.initial_value = p.label();
        mCaption = LLUICtrlFactory::create<LLTextBox>(tp);
        addChild(mCaption);

        ALFieldEditors::Field field;
        field.name = getName();
        field.kind = ALParamType::REAL;
        field.components = mLetters;
        field.lettered = true;
        field.authored = true;
        field.bounded = true;
        field.minimum = p.min_value();
        field.maximum = p.max_value();
        field.step = p.increment();
        field.decimals = p.decimal_digits();
        ALFieldEditors().makeComponents(field, LLRect(p.label_width(), height + 1, width - RESET_SIZE - RESET_GAP, -1), this,
                                        [this](const std::string&, const std::string& text) { onPartsCommit(text); });
        for (const std::string& letter : mLetters)
        {
            mBoxes.push_back(getChild<LLSpinCtrl>(getName() + "." + letter));
        }
    }
    else
    {
        // From the skin's slider, so it has the fonts, colours and images every
        // other slider has. It keeps its own label, which is what keeps the gap
        // between label and track, and the label's grey when disabled, exactly
        // as a slider has them.
        LLSliderCtrl::Params sp(LLUICtrlFactory::getDefaultParams<LLSliderCtrl>());
        sp.name = "setting_row_slider";
        // A pixel in at the top and bottom: the row is as tall as its reset
        // button, and that is where a slider sat beside one.
        sp.rect = LLRect(0, height - 1, width - RESET_SIZE - RESET_GAP, 1);
        sp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
        // Values, not params: a param copied from one block to another carries
        // whether it was provided, and a row that left one out would hand the
        // slider an unprovided param -- which the slider fills in its own way,
        // measuring its label for a width instead of taking 140, and following
        // the skin's template for whether its value can be typed.
        sp.label = p.label();
        sp.label_width = p.label_width();
        sp.min_value = p.min_value();
        sp.max_value = p.max_value();
        sp.increment = p.increment();
        sp.decimal_digits = p.decimal_digits();
        sp.show_text = p.show_text();
        sp.can_edit_text = p.can_edit_text();
        if (p.text_width.isProvided())
        {
            sp.text_width = p.text_width();
        }
        else if (p.show_text && (p.max_value < 1.f || -p.min_value > p.max_value))
        {
            // The slider sizes its value box from the maximum's logarithm, which
            // has no room for the leading zero under 1, nothing at all at 0 or
            // below, and nothing for a minimum wider than the maximum. Anywhere
            // else it gets it right, and is left to.
            sp.text_width = valueBoxWidth(sp.font(), p.min_value, p.max_value, p.decimal_digits);
        }
        sp.commit_callback.function([this](LLUICtrl*, const LLSD&) { onSliderCommit(); });
        // Handed on as params, and only when said: a callback a file names is
        // resolved by the slider that fires it, against the registrar in scope
        // while the row is being built -- which is this constructor.
        if (p.mouse_down_callback.isProvided())
        {
            sp.mouse_down_callback(p.mouse_down_callback);
        }
        if (p.mouse_up_callback.isProvided())
        {
            sp.mouse_up_callback(p.mouse_up_callback);
        }
        mSlider = LLUICtrlFactory::create<LLSliderCtrl>(sp);
        addChild(mSlider);
    }

    LLButton::Params bp(LLUICtrlFactory::getDefaultParams<LLButton>());
    bp.name = "setting_row_reset";
    bp.rect = LLRect(width - RESET_SIZE, height, width, height - RESET_SIZE);
    bp.follows.flags = FOLLOWS_TOP | FOLLOWS_RIGHT;
    bp.label = LLStringUtil::null;
    bp.scale_image = true;
    bp.visible = false;
    bp.click_callback.function([this](LLUICtrl*, const LLSD&) { onResetClicked(); });
    mReset = LLUICtrlFactory::create<LLButton>(bp);
    // By name after construction: the overlay param wants an image, and this
    // resolves the skin's.
    mReset->setImageOverlay("Refresh_Off", LLFontGL::HCENTER);
    addChild(mReset);
}

void ALSettingRow::setValue(const LLSD& value)
{
    // What the setting says, from wherever it was changed: the slider or the
    // boxes show it without committing, and the reset button follows it. A
    // box being typed in keeps what the hand is doing.
    if (!mLetters.empty())
    {
        ALFieldEditors::Field field;
        field.name = getName();
        field.components = mLetters;
        field.authored = true;
        for (size_t i = 0; i < mLetters.size(); ++i)
        {
            field.value += (i ? " " : "") + fmt::format("{}", value[(S32)i].asReal());
        }
        ALFieldEditors::refresh(this, field);
    }
    else
    {
        mSlider->setValue((F32)(mPart >= 0 ? value[mPart].asReal() : value.asReal()));
    }
    refreshReset();
}

LLSD ALSettingRow::getValue() const
{
    if (!mLetters.empty())
    {
        LLSD parts = LLSD::emptyArray();
        for (const LLSpinCtrl* box : mBoxes)
        {
            parts.append(LLSD::Real(box->getValue().asReal()));
        }
        return parts;
    }
    return LLSD(mSlider->getValueF32());
}

void ALSettingRow::setEnabled(bool enabled)
{
    LLUICtrl::setEnabled(enabled);
    if (mSlider)
    {
        mSlider->setEnabled(enabled);
    }
    for (LLSpinCtrl* box : mBoxes)
    {
        box->setEnabled(enabled);
    }
    mReset->setEnabled(enabled);
}

void ALSettingRow::setFocus(bool focus)
{
    if (mSlider)
    {
        mSlider->setFocus(focus);
    }
    else if (!mBoxes.empty())
    {
        mBoxes.front()->setFocus(focus);
    }
}

LLTextBox* ALSettingRow::getLabelBox() const
{
    // The slider made it from the label the row was given; a vector's row
    // made its own.
    if (mLabel.empty())
    {
        return nullptr;
    }
    return mSlider ? mSlider->getLabelBox() : mCaption;
}

bool ALSettingRow::isModified()
{
    LLControlVariable* control = getControlVariable();
    if (!control || control->isDefault())
    {
        return false;
    }
    const LLSD value = control->getValue();
    const LLSD fallback = control->getDefault();
    // As shown: a value that rounds to the default's digits is the default
    // as far as anyone looking can tell.
    const F64  scale  = std::pow(10.0, (F64)llmax(mDecimalDigits, 0));
    const auto differ = [scale](const LLSD& a, const LLSD& b)
    {
        return std::llround(a.asReal() * scale) != std::llround(b.asReal() * scale);
    };
    if (mPart >= 0)
    {
        return differ(value[mPart], fallback[mPart]);
    }
    if (!mLetters.empty())
    {
        for (S32 i = 0; i < (S32)mLetters.size(); ++i)
        {
            if (differ(value[i], fallback[i]))
            {
                return true;
            }
        }
        return false;
    }
    if ((value.isReal() || value.isInteger()) && (fallback.isReal() || fallback.isInteger()))
    {
        return differ(value, fallback);
    }
    return !llsd_equals(value, fallback);
}

void ALSettingRow::resetToDefault()
{
    LLControlVariable* control = getControlVariable();
    if (!control)
    {
        return;
    }
    if (mPart >= 0)
    {
        // The default's part put back, the others as they are: the row
        // beside this one is another part, and its reset is its own.
        control->set(withParts((size_t)mPart, { control->getDefault()[mPart].asReal() }));
        return;
    }
    // The setting's own reset, which fires the setting's signal: the slider
    // and the button come back through setValue like any other change, and
    // whatever listens to the setting hears it.
    control->resetToDefault(true);
}

LLSD ALSettingRow::withParts(size_t first, const std::vector<F64>& numbers)
{
    // Written whole, since a setting is one value: the parts no box here
    // shows go back as they were.
    LLControlVariable* control = getControlVariable();
    const LLSD         current = control ? control->getValue() : LLSD();
    LLSD               updated = LLSD::emptyArray();
    for (S32 i = 0; i < current.size(); ++i)
    {
        updated.append(LLSD::Real(current[i].asReal()));
    }
    for (size_t i = 0; i < numbers.size(); ++i)
    {
        updated[(S32)(first + i)] = LLSD::Real(numbers[i]);
    }
    return updated;
}

std::string ALSettingRow::shown(const LLSD& number) const
{
    return fmt::format("{:.{}f}", number.asReal(), llmax(mDecimalDigits, 0));
}

std::string ALSettingRow::_getSearchText() const
{
    return mLabel + getToolTip();
}

void ALSettingRow::onSetHighlight() const
{
    // The slider lights its label, which is the row's name; a vector's
    // caption is lit here.
    if (mSlider)
    {
        mSlider->setHighlighted(getHighlighted());
    }
    else if (mCaption)
    {
        mCaption->ll::ui::SearchableControl::setHighlighted(getHighlighted());
    }
}

void ALSettingRow::onSliderCommit()
{
    // The row is what is bound, so the row writes the setting -- once -- and
    // then says it changed, as any control does. A part writes the whole.
    const F32 value = mSlider->getValueF32();
    setControlValue(mPart >= 0 ? withParts((size_t)mPart, { value }) : LLSD(value));
    refreshReset();
    onCommit();
}

void ALSettingRow::onPartsCommit(const std::string& text)
{
    // Every box's number, as the one typed in says them all.
    std::istringstream  in(text);
    std::vector<F64>    numbers;
    for (F64 number; in >> number;)
    {
        numbers.push_back(number);
    }
    setControlValue(withParts(0, numbers));
    refreshReset();
    onCommit();
}

void ALSettingRow::onResetClicked()
{
    if (mResetHandler)
    {
        mResetHandler(this);
        return;
    }
    resetToDefault();
}

void ALSettingRow::refreshReset()
{
    const bool show = mShowReset && getControlVariable() && isModified();
    if (show == mResetShown)
    {
        return;
    }
    mResetShown = show;

    if (show)
    {
        const LLSD  fallback = getControlVariable()->getDefault();
        std::string said;
        if (mPart >= 0)
        {
            said = shown(fallback[mPart]);
        }
        else if (!mLetters.empty())
        {
            for (S32 i = 0; i < (S32)mLetters.size(); ++i)
            {
                said += (i ? ", " : "") + shown(fallback[i]);
            }
        }
        else
        {
            said = shown(fallback);
        }
        LLStringUtil::format_map_t args;
        args["[VALUE]"] = said;
        mReset->setToolTip(LLTrans::getString("SettingRowReset", args));
    }
    else if (mReset->hasFocus())
    {
        // A button that goes away with the keyboard in it would leave the
        // keyboard nowhere; the slider beside it is where it was pointing.
        setFocus(true);
    }
    mReset->setVisible(show);
}
