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
#include "alemptystate.h"
#include "alfollowscontrol.h"
#include "alfontfield.h"
#include "llbutton.h"
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
    // The metrics, in one place, because a pane where each editor decided
    // its own inset is a pane whose columns do not line up. Everything
    // between the left edge and the right is one of these; the row height
    // and the label column are the caller's, since only the caller knows
    // what its labels say.
    //
    // The left margin of the whole grid. A label written hard against the
    // edge of a scrolling container loses its first letter to the border,
    // which is what the container is drawn with.
    constexpr S32 MARGIN = 8;
    // The way back sits in the right margin of every row that has one.
    constexpr S32 REMOVE_WIDTH = 16;
    constexpr S32 GUTTER = 10;      // between the label and its editor
    // A control narrower than its column sits at the column's left rather
    // than being stretched across it: a number is as wide as a number.
    constexpr S32 NUMBER_WIDTH = 120;
    // A row that carries a picture rather than a value is as tall as the
    // picture, and its label sits in the ordinary row at the top of it.
    constexpr S32 PICTURE_HEIGHT = 84;
    // The gutter mark, in the margin the labels already leave.
    constexpr S32 MARK_WIDTH = 3;
    constexpr S32 MARK_INSET = 2;

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

    // A row's own words, out of the pattern the caller gave. A caller who
    // gave none gets the name of the field, which is the one thing this
    // library knows how to say about it.
    std::string say(const std::string& pattern, const ALPropertyGrid::Field& field)
    {
        if (pattern.empty())
        {
            return field.name;
        }
        std::string text = pattern;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = field.name;
        args["[TYPE]"] = field.type;
        args["[SOURCE]"] = field.source;
        LLStringUtil::format(text, args);
        return text;
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

// The strip beside a label that says this value is written somewhere else
// as well. It lives in the margin the labels already leave, so marking a row
// costs the columns nothing; it is drawn rather than imaged because it is
// one rectangle; and it takes a click, because seeing where else is the
// point of noticing it.
class ALPropertyGrid::Mark final : public LLUICtrl
{
public:
    AL_VIEW_TYPE(Mark, LLUICtrl);

    explicit Mark(const LLUICtrl::Params& p) : LLUICtrl(p) {}

    void draw() override
    {
        static const LLUIColor ink = LLUIColorTable::instance().getColor("EmphasisColor", LLColor4::yellow);
        gl_rect_2d(getLocalRect(), ink.get(), true);
        LLUICtrl::draw();
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        onCommit();
        return true;
    }
};

// Every row is a panel of its own, and this stacks them from its top: one
// row height each, full width, in the order they were added. Whatever the
// accordion decides this panel's height is, the rows begin at the top of it.
class ALPropertyGrid::Rows final : public LLPanel
{
public:
    AL_VIEW_TYPE(Rows, LLPanel);

    explicit Rows(const LLPanel::Params& p)
    :   LLPanel(p)
    {
    }

    void reshape(S32 width, S32 height, bool called_from_parent = true) override
    {
        LLPanel::reshape(width, height, called_from_parent);
        stack();
    }

    // The rows, oldest first: a view list is filled from the front. A row
    // keeps the height it was made at -- a row carrying a picture is taller
    // than one carrying a value -- so what is stacked here is heights and
    // not a count.
    void stack()
    {
        const S32 width = getRect().getWidth();
        S32 top = getRect().getHeight();
        for (auto it = getChildList()->rbegin(); it != getChildList()->rend(); ++it)
        {
            LLView* row = *it;
            const S32 height = row->getRect().getHeight();
            row->reshape(width, height);
            row->setOrigin(0, top - height);
            top -= height;
        }
    }
};

ALPropertyGrid::Params::Params()
:   row_height("row_height", 22),
    label_width("label_width", 150)
{
}

ALPropertyGrid::ALPropertyGrid(const Params& p)
:   LLPanel(p),
    mRowHeight(p.row_height),
    mLabelWidth(p.label_width)
{
    LLAccordionCtrl::Params ap(LLUICtrlFactory::getDefaultParams<LLAccordionCtrl>());
    ap.name = "sections";
    ap.rect = getLocalRect();
    ap.follows.flags = FOLLOWS_ALL;
    ap.single_expansion = false;
    mAccordion = LLUICtrlFactory::create<LLAccordionCtrl>(ap);
    addChild(mAccordion);

    // An empty grid and a broken one look the same, so it says which -- and
    // says what to do about it, since every one of the three reasons a grid
    // is empty has something the reader can do next. Over the accordion
    // rather than in it, because what it is saying is that there are no
    // sections at all.
    ALEmptyState::Params ep(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
    ep.name = "empty";
    ep.rect = getLocalRect();
    ep.follows.flags = FOLLOWS_ALL;
    ep.background_visible = false;
    mEmpty = LLUICtrlFactory::create<ALEmptyState>(ep);
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
        Rows* rows = new Rows(rp);
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

void ALPropertyGrid::setNotices(Notice nothing_selected, Notice nothing_written, Notice no_match)
{
    mNothingSelected = std::move(nothing_selected);
    mNothingWritten = std::move(nothing_written);
    mNoMatch = std::move(no_match);
    rebuild();
}

boost::signals2::connection ALPropertyGrid::onNoticeAction(const notice_signal_t::slot_type& cb)
{
    return mEmpty->onAction(cb);
}

void ALPropertyGrid::setTips(Tips tips)
{
    mTips = std::move(tips);
    rebuild();
}

void ALPropertyGrid::setEdgeTips(std::vector<std::string> tips)
{
    mEdgeTips = std::move(tips);
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

// What the row is, then where what is in force came from, then -- where
// nobody wrote it -- that nobody did. Three lines at most, and the second
// and third are the two things about this pane that have to be said rather
// than shown.
std::string ALPropertyGrid::tipFor(const Field& field) const
{
    std::string text = say(field.ignored      ? mTips.ignored
                         : field.unknown      ? mTips.unknown
                         : field.type.empty() ? mTips.field
                                              : mTips.fieldTyped, field);
    if (!field.source.empty() && !mTips.source.empty())
    {
        text += "\n" + say(mTips.source, field);
    }
    if (!field.authored && !mTips.unwritten.empty())
    {
        text += "\n" + say(mTips.unwritten, field);
    }
    return text;
}

S32 ALPropertyGrid::rowHeight(const Field& field) const
{
    return field.edges.size() == 4 ? llmax(mRowHeight, PICTURE_HEIGHT) : mRowHeight;
}

S32 ALPropertyGrid::sectionHeight(S32 group) const
{
    S32 height = 0;
    for (const Field& field : mFields)
    {
        if (field.group == group && shows(field) && !isPartnered(field))
        {
            height += rowHeight(field);
        }
    }
    return height;
}

S32 ALPropertyGrid::countShown(S32 group) const
{
    S32 count = 0;
    for (const Field& field : mFields)
    {
        // A pair is one row, and it is counted where the first of them is:
        // the section is as tall as the rows it will hold, not as the fields
        // it was given.
        count += (field.group == group && shows(field) && !isPartnered(field)) ? 1 : 0;
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
    // The right column ends short of the way back, where a row has one.
    // Every row leaves the room whether it has the button or not, so the
    // editors down the pane keep one right edge.
    const S32 right = width - MARGIN - REMOVE_WIDTH - GUTTER;
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

        const S32 height = sectionHeight((S32)group);
        rows->reshape(width, height, false);

        S32 within = 0;
        for (const Field& field : mFields)
        {
            if (field.group != (S32)group || !shows(field))
            {
                continue;
            }
            // A field on somebody else's row is not a row.
            if (isPartnered(field))
            {
                continue;
            }
            // Alternate rows are banded, because a name and the value
            // across from it are a hundred and fifty pixels apart.
            addRow(rows, field, partnerOf(field), (within++ & 1) != 0);
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
        const Notice& says = mFields.empty()  ? mNothingSelected
                           : !mFilter.empty() ? mNoMatch
                                              : mNothingWritten;
        mEmpty->say(says.headline, says.sentence, says.action);
    }
}

// The editor a field's type asks for, in the box it is given, wired to say
// which field it is when it commits. A row holds one of these, or two where
// two fields are the same thought: left and top are a position, and reading
// them on one line is how anybody says it.
LLUICtrl* ALPropertyGrid::makeEditor(const Field& field, const LLRect& box, LLPanel* row)
{
    const std::string name = field.name;
    LLUICtrl* editor = nullptr;

    if (field.kind == ALParamType::BOOLEAN)
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
        colour->setValue(field.value);
        editor = colour;
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
        for (const std::string& value : field.values)
        {
            combo->add(value);
        }
        combo->setValue(field.value);
        // Nobody wrote this one: what is shown is what is in force, not a
        // choice made here.
        combo->setUnset(!field.authored);
        editor = combo;
    }
    else if (field.kind == ALParamType::INTEGER || field.kind == ALParamType::UNSIGNED
          || field.kind == ALParamType::REAL)
    {
        const bool whole = field.kind != ALParamType::REAL;
        LLSpinCtrl::Params p;
        p.name = name;
        p.rect = LLRect(box.mLeft, box.mTop - 1, llmin(box.mRight, box.mLeft + NUMBER_WIDTH), box.mBottom + 1);
        p.label_width = 0;
        p.decimal_digits = whole ? 0 : 3;
        p.min_value = field.kind == ALParamType::UNSIGNED ? 0.f : -100000.f;
        p.max_value = 100000.f;
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
    // The row's own words, on the editor as well: a pointer resting on a
    // control is asking what a pointer resting on its label is asking, and
    // on a paired row the two halves are two different questions.
    editor->setToolTip(tipFor(field));
    // Everything between the two columns, so the row widens where the width
    // is. A spinner is the exception: it is as wide as a number needs.
    editor->setFollows(editor->as<LLSpinCtrl>() ? (FOLLOWS_LEFT | FOLLOWS_TOP)
                                                : (FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT));
    row->addChild(editor);
    return editor;
}

const ALPropertyGrid::Field* ALPropertyGrid::partnerOf(const Field& field) const
{
    if (field.pairWith.empty())
    {
        return nullptr;
    }
    for (const Field& other : mFields)
    {
        // The same section and shown by the same rules: a filter that matches
        // one half of a pair does not drag the other half in with it.
        if (other.name == field.pairWith && other.group == field.group && shows(other))
        {
            return &other;
        }
    }
    return nullptr;
}

bool ALPropertyGrid::isPartnered(const Field& field) const
{
    for (const Field& other : mFields)
    {
        if (other.pairWith == field.name && other.group == field.group
            && shows(other) && !other.name.empty())
        {
            return true;
        }
    }
    return false;
}

// The label, the editor its type asks for, and where the value came from.
// A field the file does not write is shown with what is in force anyway:
// an author changing it is writing it here for the first time, and the
// value to start from is the one on the screen.
// One panel per row, so a section can stack them and a row is a thing rather
// than four views that happen to share a Y.
void ALPropertyGrid::addRow(Rows* host, const Field& field, const Field* partner, bool shaded)
{
    static const LLUIColor stripe = LLUIColorTable::instance().getColor("PanelDefaultBackgroundColor", LLColor4::black);
    static const LLUIColor written = LLUIColorTable::instance().getColor("LabelTextColor", LLColor4::white);
    static const LLUIColor unwritten = LLUIColorTable::instance().getColor("LabelDisabledColor", LLColor4::grey);

    const S32 width = host->getRect().getWidth();
    // Row-local: the section says where the row is, the row says where its
    // parts are. A row carrying a picture is taller than one carrying a
    // value, and everything but the picture sits in the ordinary row at the
    // top of it, so the columns still line up down the pane.
    const S32 height = rowHeight(field);
    const S32 top = height;
    const S32 bottom = height - mRowHeight;

    LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
    rp.name = field.name + "_row";
    rp.rect = LLRect(0, height, width, 0);
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

    // What this row is, in the caller's words, said the same way wherever
    // the pointer is along it. Which layer wrote what is in force was a
    // column of its own, saying one word on almost every row; it is part of
    // this sentence now, and the rows that disagree are the marked ones.
    const std::string tip = tipFor(field);

    {
        // A field nothing writes is shown in the quieter ink, and so is one
        // that is written and does nothing: what is on the row is what the
        // widget is doing, not what the file says.
        LLTextBox::Params p;
        p.name = field.name + "_label";
        p.rect = LLRect(MARGIN, top - 2, MARGIN + mLabelWidth, bottom);
        p.initial_value = partner ? field.name + ", " + partner->name : field.name;
        p.tool_tip = tip;
        p.font_valign = LLFontGL::VCENTER;
        p.text_color = (field.authored && !field.ignored) ? written : unwritten;
        p.use_ellipses = true;
        p.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        row->addChild(LLUICtrlFactory::create<LLTextBox>(p));
    }

    // The editor, or two of them where this field carries a partner. A
    // picture takes the whole of the row it made tall.
    const S32 left = editorLeft();
    const S32 editor_width = editorWidth(width);
    if (field.edges.size() == 4)
    {
        makeEditor(field, LLRect(left, top, left + editor_width, 0), row);
    }
    else if (partner)
    {
        const S32 half = (editor_width - GUTTER) / 2;
        makeEditor(field, LLRect(left, top, left + half, bottom), row);
        makeEditor(*partner, LLRect(left + half + GUTTER, top, left + editor_width, bottom), row);
    }
    else
    {
        makeEditor(field, LLRect(left, top, left + editor_width, bottom), row);
    }

    // The way back, on the rows that have one. A field this file writes can
    // be taken out again, and what was in force before is in force after --
    // which the document has always been able to do and this pane has never
    // had a way to ask for.
    if (field.authored && !field.ignored)
    {
        LLButton::Params p(LLUICtrlFactory::getDefaultParams<LLButton>());
        p.name = field.name + "_remove";
        p.label = std::string("x");
        p.rect = LLRect(width - MARGIN - REMOVE_WIDTH, top - 2, width - MARGIN, bottom);
        p.tool_tip = say(mTips.remove, field);
        p.follows.flags = FOLLOWS_RIGHT | FOLLOWS_TOP;
        LLButton* remove = LLUICtrlFactory::create<LLButton>(p);
        const std::string removed = field.name;
        remove->setCommitCallback([this, removed](LLUICtrl*, const LLSD&) { mFieldRemove(removed); });
        row->addChild(remove);
    }

    // The gutter: a value some other skin or language disagrees about, said
    // once beside the row rather than as a column repeating the same word
    // all the way down the pane.
    if (!field.alsoWritten.empty())
    {
        LLUICtrl::Params p;
        p.name = field.name + "_gutter";
        p.rect = LLRect(MARK_INSET, top - 4, MARK_INSET + MARK_WIDTH, bottom + 4);
        p.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP;
        std::string says;
        for (const std::string& line : field.alsoWritten)
        {
            says += says.empty() ? line : "\n" + line;
        }
        p.tool_tip = says;
        Mark* mark = new Mark(p);
        mark->initFromParams(p);
        const std::string marked = field.name;
        mark->setCommitCallback([this, marked](LLUICtrl*, const LLSD&) { mFieldGutter(marked); });
        row->addChild(mark);
    }
}
