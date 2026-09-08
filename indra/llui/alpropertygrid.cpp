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

#include "llui.h"
#include "llaccordionctrl.h"
#include "llaccordionctrltab.h"
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

// Every row is a panel of its own, and this stacks them from its top: one
// row height each, full width, in the order they were added. Whatever the
// accordion decides this panel's height is, the rows begin at the top of it.
class ALPropertyGrid::Rows final : public LLPanel
{
public:
    AL_VIEW_TYPE(Rows, LLPanel);

    Rows(const LLPanel::Params& p, S32 row_height)
    :   LLPanel(p),
        mRowHeight(row_height)
    {
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override
    {
        LLPanel::reshape(width, height, called_from_parent);
        stack();
    }

    // The rows, oldest first: a view list is filled from the front.
    void stack()
    {
        const S32 width = getRect().getWidth();
        S32 top = getRect().getHeight();
        for (auto it = getChildList()->rbegin(); it != getChildList()->rend(); ++it)
        {
            LLView* row = *it;
            row->reshape(width, mRowHeight);
            row->setOrigin(0, top - mRowHeight);
            top -= mRowHeight;
        }
    }

private:
    const S32 mRowHeight;
};

ALPropertyGrid::Params::Params()
:   row_height("row_height", 22),
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
    LLAccordionCtrl::Params ap(LLUICtrlFactory::getDefaultParams<LLAccordionCtrl>());
    ap.name = "sections";
    ap.rect = getLocalRect();
    ap.follows.flags = FOLLOWS_ALL;
    ap.single_expansion = false;
    mAccordion = LLUICtrlFactory::create<LLAccordionCtrl>(ap);
    addChild(mAccordion);

    // An empty grid and a broken one look the same, so it says which. Over
    // the accordion rather than in it, because what it is saying is that
    // there are no sections.
    LLTextBox::Params tp;
    tp.name = "empty";
    tp.rect = LLRect(MARGIN, getRect().getHeight(), getRect().getWidth(), getRect().getHeight() - mRowHeight);
    tp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    tp.font_valign = LLFontGL::VCENTER;
    tp.use_ellipses = true;
    mEmpty = LLUICtrlFactory::create<LLTextBox>(tp);
    addChild(mEmpty);
}

ALPropertyGrid::~ALPropertyGrid() = default;

void ALPropertyGrid::setGroups(std::vector<std::string> groups)
{
    for (Section& section : mSections)
    {
        mAccordion->removeCollapsibleCtrl(section.tab);
        section.tab->die();
    }
    mSections.clear();
    mGroups = std::move(groups);

    for (const std::string& name : mGroups)
    {
        LLAccordionCtrlTab::Params tp(LLUICtrlFactory::getDefaultParams<LLAccordionCtrlTab>());
        tp.name = name;
        tp.title = name;
        tp.display_children = true;
        tp.rect = LLRect(0, mRowHeight, getRect().getWidth(), 0);

        LLAccordionCtrlTab* tab = LLUICtrlFactory::create<LLAccordionCtrlTab>(tp);

        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = name + "_rows";
        rp.rect = LLRect(0, mRowHeight, getRect().getWidth(), 0);
        rp.background_visible = false;
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
        Rows* rows = new Rows(rp, mRowHeight);
        rows->initFromParams(rp);
        tab->setAccordionView(rows);

        mAccordion->addCollapsibleCtrl(tab);
        mSections.push_back({ tab, rows });
    }
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

    // A section the file writes nothing in is every field the tag will take
    // and none that anyone chose, which is a page of grey to scroll past to
    // reach the next heading. So it arrives folded: everything is still
    // here, and what is in the file is what is in front of you. Choosing
    // another widget is a fresh view of a different thing, which is why
    // this is done here and not on every rebuild.
    for (size_t group = 0; group < mSections.size(); ++group)
    {
        const bool written = std::any_of(mFields.begin(), mFields.end(),
                                         [group](const Field& field)
                                         { return field.group == (S32)group && field.authored; });
        mSections[group].tab->setDisplayChildren(written);
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

void ALPropertyGrid::setNotices(std::string nothing_selected, std::string nothing_written,
                                std::string no_match)
{
    mNothingSelected = std::move(nothing_selected);
    mNothingWritten = std::move(nothing_written);
    mNoMatch = std::move(no_match);
    rebuild();
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

S32 ALPropertyGrid::countShown(S32 group) const
{
    S32 count = 0;
    for (const Field& field : mFields)
    {
        count += (field.group == group && shows(field)) ? 1 : 0;
    }
    return count;
}

// An accordion does not reserve room for its scrollbar: it lays its tabs
// out to its whole width and draws the scrollbar over the right edge of
// them. So a row laid out to the width its panel reports is a row with its
// last inch under a scrollbar, whenever there is one -- and whether there
// is one changes every time a section is folded, which is why it looked
// like a thing that happened at random.
//
// The width is taken from the accordion instead, with everything between
// it and the row subtracted: the margin a tab is inset by, the padding a
// tab keeps inside that, and the scrollbar's column whether or not the
// scrollbar is in it.
S32 ALPropertyGrid::rowWidth() const
{
    static LLUICachedControl<S32> scrollbar_size("UIScrollbarSize", 16);
    constexpr S32 TAB_MARGIN = 4;       // LLAccordionCtrl::arrangeMultiple
    const S32 padding = mSections.empty()
        ? 4
        : mSections.front().tab->getPaddingLeft() + mSections.front().tab->getPaddingRight();
    return llmax(120, mAccordion->getRect().getWidth() - TAB_MARGIN - padding - scrollbar_size);
}

S32 ALPropertyGrid::editorLeft() const
{
    return MARGIN + mLabelWidth + GUTTER;
}

S32 ALPropertyGrid::editorWidth(S32 width) const
{
    const S32 right = width - MARGIN - (mShowSource ? mSourceWidth + GUTTER : 0);
    return llmax(60, right - editorLeft());
}

// A row is anchored to the two edges its parts belong to, so a change of
// width widens the row rather than replacing it. Rebuilding here instead --
// which is what this did -- deleted and remade every widget in the grid on
// every frame of a drag, including the one under the pointer and any whose
// image was still arriving, which is what made a resize look like a fault.
void ALPropertyGrid::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
}

// Each section is filled and then sized to what it holds; the accordion
// arranges them, folds them and scrolls them. A section with nothing under
// it is hidden rather than shown empty, since a heading over nothing is
// worse than no heading.
void ALPropertyGrid::rebuild()
{
    if (!mAccordion)
    {
        return;
    }
    const S32 width = rowWidth();
    S32 shown = 0;
    for (size_t group = 0; group < mSections.size(); ++group)
    {
        Rows* rows = mSections[group].rows;
        rows->deleteAllChildren();

        const S32 count = countShown((S32)group);
        mSections[group].tab->setVisible(count > 0);
        if (!count)
        {
            continue;
        }
        shown += count;

        const S32 height = count * mRowHeight;
        rows->reshape(width, height, false);

        S32 within = 0;
        for (const Field& field : mFields)
        {
            if (field.group != (S32)group || !shows(field))
            {
                continue;
            }
            // Alternate rows are banded, because a name and the value
            // across from it are a hundred and fifty pixels apart.
            addRow(rows, field, (within++ & 1) != 0);
        }
        rows->stack();

        // A tab is as tall as it was told its panel is, once, when the panel
        // was put in it. Reshaping the panel afterwards says nothing: this
        // is what says it, and it is what every other growing thing inside
        // an accordion sends.
        rows->notifyParent(LLSD().with("action", "size_changes").with("height", height));
    }

    mAccordion->arrange();
    mAccordion->setVisible(shown > 0);
    mEmpty->setVisible(shown == 0);
    if (!shown)
    {
        mEmpty->setText(mFields.empty() ? mNothingSelected
                      : !mFilter.empty() ? mNoMatch
                                         : mNothingWritten);
    }
}

// The label, the editor its type asks for, and where the value came from.
// A field the file does not write is shown with what is in force anyway:
// an author changing it is writing it here for the first time, and the
// value to start from is the one on the screen.
// One panel per row, so a section can stack them and a row is a thing rather
// than four views that happen to share a Y.
void ALPropertyGrid::addRow(Rows* host, const Field& field, bool shaded)
{
    static const LLUIColor stripe = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::black);
    static const LLUIColor written = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor unwritten = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const S32 width = host->getRect().getWidth();
    const S32 left = editorLeft();
    const S32 editor_width = editorWidth(width);
    // Row-local: the section says where the row is, the row says where its
    // parts are.
    const S32 top = mRowHeight;
    const S32 bottom = 0;

    LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
    rp.name = field.name + "_row";
    rp.rect = LLRect(0, mRowHeight, width, 0);
    rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    rp.mouse_opaque = false;
    // Alternate rows are banded, because a name and the value across from it
    // are a hundred and fifty pixels apart. The row is its own band.
    rp.background_visible = shaded;
    if (shaded)
    {
        rp.bg_alpha_color = LLUIColor(LLColor4(stripe.get().mV[VRED], stripe.get().mV[VGREEN],
                                               stripe.get().mV[VBLUE], 0.25f));
    }
    LLPanel* row = LLUICtrlFactory::create<LLPanel>(rp);
    host->addChild(row);

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
        p.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        row->addChild(LLUICtrlFactory::create<LLTextBox>(p));
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
    // Everything between the two columns, so the row widens where the width
    // is. A spinner is the exception: it is as wide as a number needs.
    editor->setFollows(editor->as<LLSpinCtrl>() ? (FOLLOWS_LEFT | FOLLOWS_TOP)
                                                : (FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT));
    row->addChild(editor);

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
        p.follows.flags = FOLLOWS_RIGHT | FOLLOWS_TOP;
        row->addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }
}
