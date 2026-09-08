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
#include "alflagsfield.h"
#include "alfontfield.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALPropertyGrid> r("property_grid");

namespace
{
    // The left margin of the whole grid. A label written hard against the
    // edge of a scrolling container loses its first letter to the border,
    // which is what the container is drawn with.
    constexpr S32 MARGIN = 8;
    constexpr S32 GUTTER = 10;      // between the label and its editor
    constexpr S32 HEADING_EXTRA = 6;

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

    // The types worth a swatch rather than a spelling. A colour is written
    // as a name out of the colour table or as its four parts, and neither
    // is something to type from memory.
    bool isColorType(std::string_view type)
    {
        return type == "LLUIColor" || type == "LLColor4" || type == "LLColor3" || type == "LLColor4U";
    }

    // A font is a name, a size and three flags, and all three vocabularies
    // live in fonts.xml. The type is the pointer the block holds.
    bool isFontType(std::string_view type)
    {
        return type.find("LLFontGL") != std::string_view::npos;
    }

    bool carries(std::string_view haystack, std::string_view needle)
    {
        if (needle.empty())
        {
            return true;
        }
        const auto at = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                    [](char a, char b) { return LLStringOps::toLower(a) == LLStringOps::toLower(b); });
        return at != haystack.end();
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

void ALPropertyGrid::setGroups(std::vector<std::string> groups)
{
    mGroups = std::move(groups);
    mFolded.assign(llmax<size_t>(mGroups.size(), 1), false);
    rebuild();
}

void ALPropertyGrid::setFields(std::vector<Field> fields)
{
    mFields = std::move(fields);
    std::sort(mFields.begin(), mFields.end(), [](const Field& a, const Field& b)
    {
        // The headings first, in the order they were named; what the file
        // writes at the top of each, since that is what is being worked
        // on; and the rest alphabetically under it.
        if (a.group != b.group)
        {
            return a.group < b.group;
        }
        if (a.authored != b.authored)
        {
            return a.authored;
        }
        return a.name < b.name;
    });

    // A column that says the same word on every row is a column of one
    // repeated word, so it is only drawn where the rows disagree.
    mShowSource = false;
    for (const Field& field : mFields)
    {
        if (!field.source.empty() && field.source != mFields.front().source)
        {
            mShowSource = true;
            break;
        }
    }
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

void ALPropertyGrid::setNested(bool nested)
{
    if (mNested != nested)
    {
        mNested = nested;
        rebuild();
    }
}

void ALPropertyGrid::setFilter(const std::string& text)
{
    if (mFilter != text)
    {
        mFilter = text;
        rebuild();
    }
}

// What another field of this same widget is in force at. A composite
// editor -- a font is three attributes -- needs the ones that are not the
// row it is on, and the grid is what has them.
std::string ALPropertyGrid::valueOf(const std::string& name) const
{
    const auto it = std::find_if(mFields.begin(), mFields.end(),
                                 [&name](const Field& field) { return field.name == name; });
    return it == mFields.end() ? std::string() : it->value;
}

bool ALPropertyGrid::shows(const Field& field) const
{
    if (mAuthoredOnly && !field.authored)
    {
        return false;
    }
    // A leaf of a block the widget carries. Hidden by default because it
    // multiplies the list several times over -- but never hidden when the
    // file writes it, since the tool must not be quiet about what is there.
    if (!mNested && !field.authored && field.name.find('.') != std::string::npos)
    {
        return false;
    }
    return carries(field.name, mFilter);
}

bool ALPropertyGrid::anyShown(S32 group) const
{
    return std::any_of(mFields.begin(), mFields.end(), [&](const Field& field)
    {
        return field.group == group && shows(field);
    });
}

bool ALPropertyGrid::open(S32 group) const
{
    return group < 0 || (size_t)group >= mFolded.size() || !mFolded[group];
}

S32 ALPropertyGrid::editorLeft() const
{
    return MARGIN + mLabelWidth + GUTTER;
}

S32 ALPropertyGrid::editorWidth() const
{
    const S32 right = getRect().getWidth() - MARGIN - (mShowSource ? mSourceWidth + GUTTER : 0);
    return llmax(60, right - editorLeft());
}

S32 ALPropertyGrid::contentHeight() const
{
    const S32 headings = (S32)llmax<size_t>(mGroups.size(), 1);
    S32 height = 0;
    for (S32 group = 0; group < headings; ++group)
    {
        if (!anyShown(group))
        {
            continue;
        }
        // An unnamed section has no heading to draw and nothing to fold.
        height += mGroups.empty() ? 0 : mRowHeight + HEADING_EXTRA;
        if (!open(group))
        {
            continue;
        }
        for (const Field& field : mFields)
        {
            height += (field.group == group && shows(field)) ? mRowHeight : 0;
        }
    }
    // Never nothing: an empty grid still has a line saying it is empty.
    return llmax(height, mRowHeight);
}

void ALPropertyGrid::reshape(S32 width, S32 height, bool called_from_parent)
{
    const bool changed = width != getRect().getWidth();
    LLPanel::reshape(width, height, called_from_parent);
    if (changed && !mRebuilding)
    {
        rebuild();
    }
}

void ALPropertyGrid::rebuild()
{
    mRebuilding = true;
    deleteAllChildren();

    // A scrolled document grows downward from a top that stays put: reshape
    // alone keeps the bottom edge, which would walk the rows off the bottom
    // of the container every time the list got shorter.
    const S32 was_top = getRect().mTop;
    const S32 height = contentHeight();
    reshape(getRect().getWidth(), height, false);
    translate(0, was_top - getRect().mTop);

    const S32 headings = (S32)llmax<size_t>(mGroups.size(), 1);
    S32 top = height;
    S32 rows = 0;
    for (S32 group = 0; group < headings; ++group)
    {
        if (!anyShown(group))
        {
            continue;
        }
        S32 count = 0;
        for (const Field& field : mFields)
        {
            count += field.group == group && shows(field);
        }
        if (!mGroups.empty())
        {
            addHeading(group, count, top);
            top -= mRowHeight + HEADING_EXTRA;
        }
        if (!open(group))
        {
            continue;
        }
        // Alternate rows are shaded, because a name and the value across
        // from it are a hundred and fifty pixels apart.
        S32 within = 0;
        for (const Field& field : mFields)
        {
            if (field.group != group || !shows(field))
            {
                continue;
            }
            addRow(field, top, (within++ & 1) != 0);
            top -= mRowHeight;
            ++rows;
        }
    }

    if (!rows)
    {
        // An empty grid and a broken one look the same, so it says which.
        LLTextBox::Params p;
        p.name = "empty";
        p.rect = LLRect(MARGIN, height, getRect().getWidth(), height - mRowHeight);
        p.initial_value = mFields.empty()
            ? "Nothing selected."
            : (!mFilter.empty() ? "No field of that name."
                                : "This element writes nothing; take the switch off to see every field.");
        p.font_valign = LLFontGL::VCENTER;
        addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }
    mRebuilding = false;
}

// The name of a section and how many fields are under it, over a band of
// its own so that the eye can find where one subject ends. Clicking it
// folds the section away, and the arrow says which way that will go.
void ALPropertyGrid::addHeading(S32 group, S32 count, S32 top)
{
    static const LLUIColor band = LLUIColorTable::instance().getColor("MenuItemHighlightBgColor", LLColor4::grey4);
    static const LLUIColor ink = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);

    const std::string& name = mGroups[llclamp(group, 0, (S32)mGroups.size() - 1)];
    const bool folded = !open(group);

    LLTextBox::Params p;
    p.name = "heading_" + name;
    p.rect = LLRect(0, top - HEADING_EXTRA / 2, getRect().getWidth(), top - mRowHeight - HEADING_EXTRA / 2);
    p.initial_value = (folded ? "\xE2\x96\xB6  " : "\xE2\x96\xBC  ") + name
                      + "   (" + std::to_string(count) + ")";
    p.font = LLFontGL::getFontSansSerifSmallBold();
    p.font_valign = LLFontGL::VCENTER;
    p.h_pad = MARGIN;
    p.text_color = ink;
    p.bg_visible = true;
    p.bg_readonly_color = band;
    p.mouse_opaque = true;
    p.tool_tip = folded ? "Show these fields" : "Hide these fields";
    LLTextBox* heading = LLUICtrlFactory::create<LLTextBox>(p);
    heading->setClickedCallback([this, group](void*)
    {
        if ((size_t)group < mFolded.size())
        {
            mFolded[group] = !mFolded[group];
            rebuild();
        }
    });
    addChild(heading);
}

// The label, the editor its type asks for, and where the value came from.
// A field the file does not write is shown with what is in force anyway:
// an author changing it is writing it here for the first time, and the
// value to start from is the one on the screen.
void ALPropertyGrid::addRow(const Field& field, S32 top, bool shaded)
{
    static const LLUIColor stripe = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::black);
    static const LLUIColor written = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor unwritten = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const S32 width = getRect().getWidth();
    const S32 left = editorLeft();
    const S32 editor_width = editorWidth();
    const S32 bottom = top - mRowHeight;

    if (shaded)
    {
        LLPanel::Params band;
        band.name = "stripe";
        band.rect = LLRect(0, top, width, bottom);
        band.background_visible = true;
        band.bg_alpha_color = LLUIColor(LLColor4(stripe.get().mV[VRED], stripe.get().mV[VGREEN],
                                                 stripe.get().mV[VBLUE], 0.25f));
        band.mouse_opaque = false;
        addChild(LLUICtrlFactory::create<LLPanel>(band));
    }

    {
        // A field nothing writes is shown in the quieter ink, and so is one
        // that is written and does nothing: what is on the row is what the
        // widget is doing, not what the file says.
        LLTextBox::Params p;
        p.name = field.name + "_label";
        p.rect = LLRect(MARGIN, top - 2, MARGIN + mLabelWidth, bottom);
        p.initial_value = field.name;
        p.tool_tip = field.ignored ? field.name + " is read off every widget and thrown away"
                   : field.unknown ? field.name + " is written here and declared by nothing"
                   : field.type.empty() ? field.name
                                        : field.name + " : " + field.type;
        p.font_valign = LLFontGL::VCENTER;
        p.text_color = (field.authored && !field.ignored) ? written : unwritten;
        p.use_ellipses = true;
        addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }

    const std::string name = field.name;
    LLUICtrl* editor = nullptr;

    if (field.kind == ALParamType::BOOLEAN)
    {
        LLCheckBoxCtrl::Params p;
        p.name = name;
        p.rect = LLRect(left, top - 2, left + editor_width, bottom);
        p.label = LLStringUtil::null;
        p.initial_value = field.value == "true" || field.value == "1";
        editor = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
    }
    else if (isColorType(field.type))
    {
        // The one type with a vocabulary worth showing rather than typing.
        ALColorField::Params p;
        p.name = name;
        p.rect = LLRect(left, top - 1, left + editor_width, bottom + 1);
        ALColorField* colour = LLUICtrlFactory::create<ALColorField>(p);
        colour->setValue(field.value);
        editor = colour;
    }
    else if (field.flags)
    {
        // Four independent answers written as one word, so four boxes.
        ALFlagsField::Params p;
        p.name = name;
        p.rect = LLRect(left, top - 2, left + editor_width, bottom);
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
        p.rect = LLRect(left, top - 1, left + editor_width, bottom + 1);
        ALFontField* font = LLUICtrlFactory::create<ALFontField>(p);
        font->setValue(field.value);
        font->setSize(valueOf(name + ".size"));
        font->setStyle(valueOf(name + ".style"));
        font->onPartCommit([this, name](const std::string& part, const std::string& value)
        {
            mFieldCommit(part.empty() ? name : name + "." + part, value);
        });
        editor = font;
    }
    else if (!field.values.empty())
    {
        LLComboBox::Params p;
        p.name = name;
        p.rect = LLRect(left, top - 1, left + editor_width, bottom + 1);
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
        p.rect = LLRect(left, top - 1, left + llmin(editor_width, 120), bottom + 1);
        p.label_width = 0;
        p.decimal_digits = whole ? 0 : 3;
        p.min_value = field.kind == ALParamType::UNSIGNED ? 0.f : -100000.f;
        p.max_value = 100000.f;
        p.initial_value = numberOf(field.value);
        editor = LLUICtrlFactory::create<LLSpinCtrl>(p);
    }
    else
    {
        LLLineEditor::Params p;
        p.name = name;
        p.rect = LLRect(left, top - 1, left + editor_width, bottom + 1);
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

    if (mShowSource)
    {
        LLTextBox::Params p;
        p.name = field.name + "_source";
        p.rect = LLRect(width - MARGIN - mSourceWidth, top - 2, width - MARGIN, bottom);
        p.initial_value = field.source;
        p.tool_tip = field.source;
        p.font_valign = LLFontGL::VCENTER;
        p.font_halign = LLFontGL::RIGHT;
        p.text_color = unwritten;
        p.use_ellipses = true;
        addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }
}
