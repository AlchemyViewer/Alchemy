/**
 * @file alpropertygrid.cpp
 * @brief One row per field of a widget, with the editor its type asks for.
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

#include "alpropertygrid.h"

#include "alcolorfield.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALPropertyGrid> r("property_grid");

namespace
{
    // A number written the way a file writes one, which is not the way a
    // spinner holds it: three point zero is 3, and a value that carries a
    // C float suffix keeps it out of the way of the parse.
    std::string numberText(F32 value, bool whole)
    {
        if (whole)
        {
            return std::to_string((S32)llround(value));
        }
        std::string text = llformat("%.4f", value);
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
}

ALPropertyGrid::Params::Params()
:   row_height("row_height", 20),
    label_width("label_width", 150),
    source_width("source_width", 90)
{
}

ALPropertyGrid::ALPropertyGrid(const Params& p)
:   LLPanel(p),
    mRowHeight(p.row_height),
    mLabelWidth(p.label_width),
    mSourceWidth(p.source_width)
{
}

ALPropertyGrid::~ALPropertyGrid() = default;

void ALPropertyGrid::setFields(std::vector<Field> fields)
{
    mFields = std::move(fields);
    std::sort(mFields.begin(), mFields.end(), [](const Field& a, const Field& b)
    {
        // What the file writes first, since that is what is being worked
        // on, and the rest alphabetically under it.
        if (a.authored != b.authored)
        {
            return a.authored;
        }
        return a.name < b.name;
    });
    rebuild();
}

void ALPropertyGrid::clearFields()
{
    mFields.clear();
    rebuild();
}

void ALPropertyGrid::setAuthoredOnly(bool only)
{
    if (mAuthoredOnly != only)
    {
        mAuthoredOnly = only;
        rebuild();
    }
}

S32 ALPropertyGrid::contentHeight() const
{
    S32 rows = 0;
    for (const Field& field : mFields)
    {
        rows += !mAuthoredOnly || field.authored;
    }
    return llmax(rows, 1) * mRowHeight;
}

void ALPropertyGrid::rebuild()
{
    deleteAllChildren();

    const S32 height = contentHeight();
    reshape(getRect().getWidth(), height, false);

    S32 top = height;
    for (const Field& field : mFields)
    {
        if (mAuthoredOnly && !field.authored)
        {
            continue;
        }
        addRow(field, top);
        top -= mRowHeight;
    }
}

// The label, the editor its type asks for, and where the value came from.
// A field the file does not write is shown with what is in force anyway:
// an author changing it is writing it here for the first time, and the
// value to start from is the one on the screen.
void ALPropertyGrid::addRow(const Field& field, S32 top)
{
    const S32 width = getRect().getWidth();
    const S32 editor_left = mLabelWidth + 4;
    const S32 editor_width = llmax(60, width - mLabelWidth - mSourceWidth - 12);
    const S32 bottom = top - mRowHeight;

    {
        LLTextBox::Params p;
        p.name = field.name + "_label";
        p.rect = LLRect(0, top - 2, mLabelWidth, bottom);
        p.initial_value = field.name;
        p.tool_tip = field.type.empty() ? field.name : field.name + " : " + field.type;
        p.font_valign = LLFontGL::VCENTER;
        p.use_ellipses = true;
        addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }

    const std::string name = field.name;
    LLUICtrl* editor = nullptr;

    if (field.kind == ALParamType::BOOLEAN)
    {
        LLCheckBoxCtrl::Params p;
        p.name = name;
        p.rect = LLRect(editor_left, top - 2, editor_left + editor_width, bottom);
        p.label = LLStringUtil::null;
        p.initial_value = field.value == "true" || field.value == "1";
        editor = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
    }
    else if (!field.values.empty())
    {
        LLComboBox::Params p;
        p.name = name;
        p.rect = LLRect(editor_left, top - 1, editor_left + editor_width, bottom + 1);
        p.allow_text_entry = false;
        LLComboBox* combo = LLUICtrlFactory::create<LLComboBox>(p);
        for (const std::string& value : field.values)
        {
            combo->add(value);
        }
        combo->setValue(field.value);
        editor = combo;
    }
    else if (field.kind == ALParamType::INTEGER || field.kind == ALParamType::UNSIGNED
          || field.kind == ALParamType::REAL)
    {
        const bool whole = field.kind != ALParamType::REAL;
        LLSpinCtrl::Params p;
        p.name = name;
        p.rect = LLRect(editor_left, top - 1, editor_left + llmin(editor_width, 140), bottom + 1);
        p.label_width = 0;
        p.decimal_digits = whole ? 0 : 3;
        p.min_value = field.kind == ALParamType::UNSIGNED ? 0.f : -100000.f;
        p.max_value = 100000.f;
        p.initial_value = numberOf(field.value);
        editor = LLUICtrlFactory::create<LLSpinCtrl>(p);
    }
    else if (field.type == "LLUIColor")
    {
        // The one type with a vocabulary worth showing rather than typing.
        ALColorField::Params p;
        p.name = name;
        p.rect = LLRect(editor_left, top - 1, editor_left + editor_width, bottom + 1);
        ALColorField* colour = LLUICtrlFactory::create<ALColorField>(p);
        colour->setValue(field.value);
        editor = colour;
    }
    else
    {
        LLLineEditor::Params p;
        p.name = name;
        p.rect = LLRect(editor_left, top - 1, editor_left + editor_width, bottom + 1);
        p.initial_value = field.value;
        p.commit_on_focus_lost = true;
        editor = LLUICtrlFactory::create<LLLineEditor>(p);
    }

    // Every editor answers the same way: the row says which field it is,
    // and what a file would write is made from the value here.
    const bool whole = field.kind != ALParamType::REAL;
    const ALParamType::EValue kind = field.kind;
    editor->setCommitCallback([this, name, kind, whole](LLUICtrl* ctrl, const LLSD&)
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
        mFieldCommit(name, text);
    });
    addChild(editor);

    {
        LLTextBox::Params p;
        p.name = field.name + "_source";
        p.rect = LLRect(width - mSourceWidth, top - 2, width, bottom);
        p.initial_value = field.source;
        p.tool_tip = field.source;
        p.font_valign = LLFontGL::VCENTER;
        p.use_ellipses = true;
        addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }
}
