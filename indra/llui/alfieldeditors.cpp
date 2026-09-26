/**
 * @file alfieldeditors.cpp
 * @brief The editor a field's type asks for: made, kept up to date, and committed as a file writes it.
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

#include "alfieldeditors.h"

#include "alangledial.h"
#include "alcolorfield.h"
#include "alcornerfield.h"
#include "alflagsfield.h"
#include "alfollowscontrol.h"
#include "alfontfield.h"
#include "alimagefield.h"
#include "aloffsetpad.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfocusmgr.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llsliderctrl.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <fmt/format.h>

#include <algorithm>
#include <memory>

namespace
{
    // Between the parts of a value that is several numbers, where there is
    // room for it.
    constexpr S32 GUTTER = 10;
    // A control narrower than its column sits at the column's left rather
    // than being stretched across it: a number is as wide as a number.
    constexpr S32 NUMBER_WIDTH = 120;
    // A part of a value that is several numbers is narrower than a number on
    // its own, since three or four of them share the column -- but never so
    // narrow that the number in it cannot be read.
    constexpr S32 NUMBER_MIN_WIDTH = 44;

    // A number written the way a file writes one, which is not the way a
    // spinner holds it: three point zero is 3, and a value that carries a
    // C float suffix keeps it out of the way of the parse.
    std::string numberText(F32 value, bool whole)
    {
        if (whole)
        {
            return std::to_string((S32)llround(value));
        }
        std::string text = fmt::format("{:.4f}", value);
        while (text.size() > 1 && text.back() == '0')
        {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.')
        {
            text.pop_back();
        }
        return text;
    }

    F32 numberOf(const std::string& text)
    {
        return (F32)atof(text.c_str());
    }

    // How a number's box behaves: what the vocabulary said where it said
    // anything, and the old rule of thumb where it did not.
    S32 decimalsOf(const ALFieldEditors::Field& field, bool whole)
    {
        return field.decimals >= 0 ? field.decimals : whole ? 0 : 3;
    }

    F32 stepOf(const ALFieldEditors::Field& field, bool whole)
    {
        return field.step > 0.f ? field.step : whole ? 1.f : 0.1f;
    }

    F32 minimumOf(const ALFieldEditors::Field& field)
    {
        return field.bounded ? field.minimum : field.kind == ALParamType::UNSIGNED ? 0.f : -100000.f;
    }

    F32 maximumOf(const ALFieldEditors::Field& field)
    {
        return field.bounded ? field.maximum : 100000.f;
    }

    // A value that is several numbers, as the numbers it is. A file writes
    // them with spaces between them and an LLSD one arrives with commas, so
    // both separate.
    std::vector<std::string> numbersOf(const std::string& text)
    {
        std::vector<std::string> parts;
        size_t at = 0;
        while (at < text.size())
        {
            while (at < text.size() && (text[at] == ' ' || text[at] == ',' || text[at] == '	'))
            {
                ++at;
            }
            const size_t start = at;
            while (at < text.size() && text[at] != ' ' && text[at] != ',' && text[at] != '	')
            {
                ++at;
            }
            if (at > start)
            {
                parts.push_back(text.substr(start, at - start));
            }
        }
        return parts;
    }

    // A font is a name, a size and three flags, and all three vocabularies
    // live in fonts.xml. The type is the pointer the block holds.
    bool isFontType(std::string_view type)
    {
        return type.find("LLFontGL") != std::string_view::npos;
    }

    // A picture is a name out of the skin, and is known by sight: the
    // type is the pointer the block holds.
    bool isImageType(std::string_view type)
    {
        return type.find("LLUIImage") != std::string_view::npos;
    }
}

// The types worth a swatch rather than a spelling. A colour is written
// as a name out of the colour table or as its four parts, and neither
// is something to type from memory.
// static
bool ALFieldEditors::isColorType(std::string_view type)
{
    return type == "LLUIColor" || type == "LLColor4" || type == "LLColor3" || type == "LLColor4U";
}


// static
void ALFieldEditors::refresh(LLView* within, const Field& field)
{
    LLUICtrl* editor = within->findChild<LLUICtrl>(field.name, true);
    // Being dragged or typed in: what it shows is what the hand is doing,
    // and what the hand is doing is what the field is about to say.
    if (editor && (gFocusMgr.childHasKeyboardFocus(editor) || gFocusMgr.childHasMouseCapture(editor)
                   || editor->hasFocus() || editor->hasMouseCapture()))
    {
        return;
    }
    if (!field.components.empty())
    {
        const std::vector<std::string> parts = numbersOf(field.value);
        for (size_t i = 0; i < field.components.size(); ++i)
        {
            LLSpinCtrl* spin = within->findChild<LLSpinCtrl>(field.name + "." + field.components[i], true);
            // The same test as the whole editor's: one of the three being
            // scrubbed keeps what the hand is doing.
            if (spin && !gFocusMgr.childHasKeyboardFocus(spin) && !gFocusMgr.childHasMouseCapture(spin)
                && !spin->hasFocus() && !spin->hasMouseCapture())
            {
                spin->setValue(i < parts.size() ? numberOf(parts[i]) : 0.f);
                spin->setUnset(!field.authored);
            }
        }
        return;
    }
    if (!editor)
    {
        return;
    }
    if (LLSpinCtrl* spin = editor->as<LLSpinCtrl>())
    {
        spin->setValue(numberOf(field.value));
        spin->setUnset(!field.authored);
    }
    else if (LLSliderCtrl* slider = editor->as<LLSliderCtrl>())
    {
        slider->setValue(numberOf(field.value));
    }
    else if (LLComboBox* combo = editor->as<LLComboBox>())
    {
        combo->setValue(field.value);
        combo->setUnset(!field.authored);
    }
    else if (LLCheckBoxCtrl* check = editor->as<LLCheckBoxCtrl>())
    {
        check->setValue(field.value == "true" || field.value == "1");
    }
    else if (LLLineEditor* line = editor->as<LLLineEditor>())
    {
        line->setText(field.authored ? field.value : LLStringUtil::null);
        line->setLabel(field.authored ? LLStringUtil::null : field.value);
    }
    else
    {
        editor->setValue(field.value);
    }
}

void ALFieldEditors::makeComponents(const Field& field, const LLRect& box, LLPanel* row, const commit_t& commit) const
{
    const size_t count = field.components.size();
    if (count == 0)
    {
        return;
    }
    static const LLUIColor caption =
        LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const std::vector<std::string> parts = numbersOf(field.value);
    const bool whole = field.kind != ALParamType::REAL;
    // Every part inside the column they share. A floor wider than the room
    // divided by the count is a row that overflows and is clipped, which
    // reads as a row with nothing on it -- so the gutter closes up first,
    // and then the boxes take what is left however narrow that is.
    const S32 room = box.getWidth();
    const S32 gutter = (room - GUTTER * (S32)(count - 1)) / (S32)count >= NUMBER_MIN_WIDTH
                     ? GUTTER : 2;
    const S32 each = llmax(1, (room - gutter * (S32)(count - 1)) / (S32)count);

    // Held so that any one of them can read all of them: a part committed on
    // its own would say nothing about the other three.
    auto boxes = std::make_shared<std::vector<LLSpinCtrl*> >();

    for (size_t i = 0; i < count; ++i)
    {
        const S32 left = box.mLeft + (S32)i * (each + gutter);

        LLSpinCtrl::Params p;
        p.name = field.name + "." + field.components[i];
        p.rect = LLRect(left, box.mTop - 1, left + each, box.mBottom + 1 + CAPTION_HEIGHT);
        p.label_width = 0;
        p.decimal_digits = decimalsOf(field, whole);
        p.increment = stepOf(field, whole);
        p.min_value = minimumOf(field);
        p.max_value = maximumOf(field);
        p.initial_value = i < parts.size() ? numberOf(parts[i]) : 0.f;
        LLSpinCtrl* spin = LLUICtrlFactory::create<LLSpinCtrl>(p);
        spin->setUnset(!field.authored);
        row->addChild(spin);
        boxes->push_back(spin);

        // What the part is called, under it: X and Y over two boxes is the
        // difference between a position and two numbers.
        LLTextBox::Params cp;
        cp.name = p.name() + "_caption";
        cp.rect = LLRect(left, box.mBottom + CAPTION_HEIGHT, left + each, box.mBottom);
        cp.initial_value = field.components[i];
        cp.font = LLFontGL::getFontSansSerifSmall();
        cp.font_halign = LLFontGL::HCENTER;
        cp.text_color = caption;
        row->addChild(LLUICtrlFactory::create<LLTextBox>(cp));
    }

    const std::string name = field.name;
    for (LLSpinCtrl* spin : *boxes)
    {
        spin->setCommitCallback([commit, name, whole, boxes](LLUICtrl*, const LLSD&)
        {
            std::string joined;
            for (const LLSpinCtrl* one : *boxes)
            {
                joined += joined.empty() ? "" : " ";
                joined += numberText((F32)one->getValue().asReal(), whole);
            }
            commit(name, joined);
        });
    }
}

LLUICtrl* ALFieldEditors::make(const Field& field, const LLRect& box, LLPanel* row, const std::string& tip, const commit_t& commit,
                               const value_of_t& value_of) const
{
    const std::string name = field.name;
    LLUICtrl* editor = nullptr;
    const bool picture = field.corners || field.pad || field.dial;

    if (field.corners)
    {
        ALCornerField::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        ALCornerField* corners = LLUICtrlFactory::create<ALCornerField>(p);
        corners->setRange(minimumOf(field), maximumOf(field), stepOf(field, false), decimalsOf(field, false));
        corners->setValue(field.value);
        editor = corners;
    }
    else if (field.pad)
    {
        ALOffsetPad::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        ALOffsetPad* pad = LLUICtrlFactory::create<ALOffsetPad>(p);
        pad->setRange(field.bounded ? llmax(1.f, field.maximum) : 32.f, stepOf(field, false), decimalsOf(field, false));
        pad->setValue(field.value);
        editor = pad;
    }
    else if (field.dial)
    {
        ALAngleDial::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        ALAngleDial* dial = LLUICtrlFactory::create<ALAngleDial>(p);
        dial->setValue(field.value);
        editor = dial;
    }
    else if (field.kind == ALParamType::BOOLEAN)
    {
        LLCheckBoxCtrl::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 2, box.mRight, box.mBottom);
        p.label = LLStringUtil::null;
        p.initial_value = field.value == "true" || field.value == "1";
        editor = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
    }
    else if (isColorType(field.type))
    {
        // The one type with a vocabulary worth showing rather than typing.
        ALColorField::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        ALColorField* colour = LLUICtrlFactory::create<ALColorField>(p);
        if (mColorResolver)
        {
            colour->setResolver(mColorResolver);
        }
        if (mColorChoices)
        {
            colour->setChoices(mColorChoices);
        }
        colour->setValue(field.value);
        editor = colour;
    }
    else if (isImageType(field.type))
    {
        // The other type known by sight: the picture beside its name, and
        // the skin's pictures to choose from.
        ALImageField::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        ALImageField* image = LLUICtrlFactory::create<ALImageField>(p);
        if (mImageChoices)
        {
            image->setChoices(mImageChoices);
        }
        if (mImageEditor)
        {
            image->setEditor(mImageEditor, mImageEditLabel);
        }
        image->setValue(field.value);
        editor = image;
    }
    else if (field.edges.size() == 4)
    {
        // Which edges of its parent a thing is tied to, drawn as what that
        // does to it: four struts, two springs, and the same element in a
        // parent that is larger and one that is smaller.
        ALFollowsControl::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 2, box.mRight, box.mBottom + 2);
        ALFollowsControl* follows = LLUICtrlFactory::create<ALFollowsControl>(p);
        follows->setEdges(field.edges[0], field.edges[1], field.edges[2], field.edges[3],
                          field.allWord, field.noneWord);
        follows->setTips(mEdgeTips);
        if (!field.subjectParent.isEmpty())
        {
            follows->setSubject(field.subject, field.subjectParent);
        }
        follows->setValue(field.value);
        editor = follows;
    }
    else if (field.flags)
    {
        // Four independent answers written as one word, so four boxes.
        ALFlagsField::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 2, box.mRight, box.mBottom);
        ALFlagsField* flags = LLUICtrlFactory::create<ALFlagsField>(p);
        flags->setFlags(field.values, field.allWord, field.noneWord);
        flags->setValue(field.value);
        editor = flags;
    }
    else if (isFontType(field.type))
    {
        // The other two attributes of the same font are edited here too,
        // because they are what a font is: this row is where they are,
        // whatever the file calls them.
        ALFontField::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        ALFontField* font = LLUICtrlFactory::create<ALFontField>(p);
        font->setValue(field.value);
        font->setSize(value_of(name + ".size"));
        font->setStyle(value_of(name + ".style"));
        font->onPartCommit([commit, name](const std::string& part, const std::string& value)
        {
            commit(part.empty() ? name : name + "." + part, value);
        });
        editor = font;
    }
    else if (!field.values.empty())
    {
        LLComboBox::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        // The list is what the vocabulary offers, and typing is still
        // allowed because a value already in the file that the list does
        // not have must not be lost by looking at it.
        p.allow_text_entry = true;
        LLComboBox* combo = LLUICtrlFactory::create<LLComboBox>(p);
        if (!field.value.empty()
            && std::find(field.values.begin(), field.values.end(), field.value) == field.values.end())
        {
            combo->add(field.value);
        }
        // Shown by the label where there is one, chosen by the value
        // either way: what commits is what a file writes.
        const bool labelled = field.valueLabels.size() == field.values.size();
        for (size_t i = 0; i < field.values.size(); ++i)
        {
            if (labelled)
            {
                combo->add(field.valueLabels[i], LLSD(field.values[i]));
            }
            else
            {
                combo->add(field.values[i]);
            }
        }
        combo->setValue(field.value);
        // Nobody wrote this one: what is shown is what is in force, not a
        // choice made here.
        combo->setUnset(!field.authored);
        editor = combo;
    }
    else if (field.slider && field.bounded && field.kind == ALParamType::REAL)
    {
        // A number chosen out of a range rather than typed: the slider is
        // the range and the box beside it is the number.
        LLSliderCtrl::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        p.label_width = 0;
        p.show_text = true;
        p.can_edit_text = true;
        p.text_width = 52;
        p.decimal_digits = decimalsOf(field, false);
        p.increment = stepOf(field, false);
        p.min_value = field.minimum;
        p.max_value = field.maximum;
        p.initial_value = llclamp(numberOf(field.value), field.minimum, field.maximum);
        editor = LLUICtrlFactory::create<LLSliderCtrl>(p);
    }
    else if (field.kind == ALParamType::INTEGER || field.kind == ALParamType::UNSIGNED
          || field.kind == ALParamType::REAL)
    {
        const bool whole = field.kind != ALParamType::REAL;
        LLSpinCtrl::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, llmin(box.mRight, box.mLeft + NUMBER_WIDTH), box.mBottom + 1);
        p.label_width = 0;
        p.decimal_digits = decimalsOf(field, whole);
        // A whole number steps by one. The step is a tenth unless it is said
        // otherwise, and a field carrying no decimals rounds a tenth straight
        // back to the number it started at -- so every press of an arrow and
        // every notch of the wheel wrote back the number that was there, and
        // only the keyboard could change one.
        p.increment = stepOf(field, whole);
        p.min_value = minimumOf(field);
        p.max_value = maximumOf(field);
        p.initial_value = numberOf(field.value);
        LLSpinCtrl* spin = LLUICtrlFactory::create<LLSpinCtrl>(p);
        // Nobody wrote this one, so the number in force goes behind the box
        // rather than in it: an unset number and a chosen zero read exactly
        // alike otherwise, which is the oldest complaint about this pane.
        spin->setUnset(!field.authored);
        editor = spin;
    }
    else
    {
        LLLineEditor::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, box.mRight, box.mBottom + 1);
        // Nobody wrote this one, so what the element carries goes behind the
        // box rather than in it: a value in the box is one the file writes,
        // and reading it as one is reading the file wrong.
        p.initial_value = field.authored ? field.value : LLStringUtil::null;
        p.label = field.authored ? LLStringUtil::null : field.value;
        p.commit_on_focus_lost = true;
        editor = LLUICtrlFactory::create<LLLineEditor>(p);
    }

    // Every editor answers the same way: the row says which field it is,
    // and what a file would write is made from the value here. Except a
    // font, which answers by its parts above, since it is three fields and
    // a commit of the whole would write the name a second time.
    const bool whole = field.kind != ALParamType::REAL;
    // A picture answers with the numbers already spelled the way a file
    // writes them.
    const ALParamType::EValue kind = picture ? ALParamType::OTHER : field.kind;
    if (!editor->as<ALFontField>())
    {
        editor->setCommitCallback([commit, name, kind, whole](LLUICtrl* ctrl, const LLSD&)
        {
            std::string text;
            switch (kind)
            {
            case ALParamType::BOOLEAN:
                text = ctrl->getValue().asBoolean() ? "true" : "false";
                break;
            case ALParamType::INTEGER:
            case ALParamType::UNSIGNED:
            case ALParamType::REAL:
                text = numberText((F32)ctrl->getValue().asReal(), whole);
                break;
            default:
                text = ctrl->getValue().asString();
                break;
            }
            commit(name, text);
        });
    }
    // The row's own words, on the editor as well: a pointer resting on a
    // control is asking what a pointer resting on its label is asking, and
    // on a paired row the two halves are two different questions.
    editor->setToolTip(tip);
    // Everything between the two columns, so the row widens where the width
    // is. A spinner is the exception: it is as wide as a number needs.
    editor->setFollows(editor->as<LLSpinCtrl>() ? (FOLLOWS_LEFT | FOLLOWS_TOP)
                                                : (FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT));
    row->addChild(editor);
    return editor;
}

