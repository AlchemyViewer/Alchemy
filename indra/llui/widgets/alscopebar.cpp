/**
 * @file alscopebar.cpp
 * @brief A query said as a sentence: fixed words, dropdowns, and what is looked for.
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

#include "alscopebar.h"

#include "llcombobox.h"
#include "llfontgl.h"
#include "lllineeditor.h"
#include "lltextbox.h"
#include "lluictrl.h"
#include "lluictrlfactory.h"

static LLDefaultChildRegistry::Register<ALScopeBar> r("scope_bar");

namespace
{
    // What a dropdown spends on its arrow and its two insets, over the
    // widest label in it.
    constexpr S32 CHOICE_CHROME = 30;
    constexpr S32 WORD_PAD = 4;
    // A field narrower than this is not a field, it is a gap: a sentence
    // whose parts do not fit gives the field its floor and runs on.
    constexpr S32 MIN_FIELD = 60;
}

ALScopeBar::Params::Params()
:   control_height("control_height", 22),
    gap("gap", 4)
{
}

ALScopeBar::ALScopeBar(const Params& p)
:   LLPanel(p),
    mControlHeight(p.control_height),
    mGap(p.gap),
    mRebuild([this]()
    {
        mSegments = std::move(mNext);
        mNext.clear();
        build();
    })
{
}

ALScopeBar::~ALScopeBar() = default;

void ALScopeBar::setSentence(std::vector<Segment> segments)
{
    // What is already chosen stays chosen, so that a caller rewriting the
    // sentence as one answer changes does not undo the others.
    for (Segment& segment : segments)
    {
        if (segment.name.empty() || !segment.value.empty())
        {
            continue;
        }
        const std::string had = valueOf(segment.name);
        if (!had.empty())
        {
            segment.value = had;
        }
    }
    // The parts answer for the sentence they were built from until they
    // are built again, so a sentence that has to wait waits whole.
    mNext = std::move(segments);
    mRebuild.request();
}

void ALScopeBar::build()
{
    for (LLView* part : mParts)
    {
        removeChild(part);
        delete part;
    }
    mParts.clear();

    for (Segment& segment : mSegments)
    {
        switch (segment.kind)
        {
        case Segment::Kind::Word:
        {
            LLTextBox::Params p(LLUICtrlFactory::getDefaultParams<LLTextBox>());
            p.name = segment.name.empty() ? segment.text : segment.name;
            p.initial_value = segment.text;
            p.rect = LLRect(0, mControlHeight, 10, 0);
            LLTextBox* word = LLUICtrlFactory::create<LLTextBox>(p);
            addChild(word);
            mParts.push_back(word);
            break;
        }

        case Segment::Kind::Choice:
        {
            LLComboBox::Params p(LLUICtrlFactory::getDefaultParams<LLComboBox>());
            p.name = segment.name;
            p.rect = LLRect(0, mControlHeight, 10, 0);
            p.tool_tip = segment.toolTip;
            LLComboBox* choice = LLUICtrlFactory::create<LLComboBox>(p);
            for (const auto& [label, value] : segment.choices)
            {
                choice->add(label, LLSD(value));
            }
            if (segment.value.empty() || !choice->setSelectedByValue(LLSD(segment.value), true))
            {
                choice->selectFirstItem();
                segment.value = choice->getValue().asString();
            }
            choice->setCommitCallback([this](LLUICtrl*, const LLSD&) { mRebuild.around([this] { mChanged(); }); });
            addChild(choice);
            mParts.push_back(choice);
            break;
        }

        case Segment::Kind::Field:
        {
            LLLineEditor::Params p(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
            p.name = segment.name;
            p.rect = LLRect(0, mControlHeight, 10, 0);
            p.tool_tip = segment.toolTip;
            // The placeholder, which is what a field with nothing in it
            // should be saying rather than nothing.
            p.label = segment.text;
            LLLineEditor* field = LLUICtrlFactory::create<LLLineEditor>(p);
            field->setText(segment.value);
            field->setCommitCallback([this](LLUICtrl*, const LLSD&) { mRebuild.around([this] { mRun(); }); });
            field->setKeystrokeCallback([this](LLLineEditor*, void*) { mRebuild.around([this] { mChanged(); }); }, nullptr);
            addChild(field);
            mParts.push_back(field);
            break;
        }
        }
    }
    layout();
}

S32 ALScopeBar::widthOf(const Segment& segment) const
{
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    switch (segment.kind)
    {
    case Segment::Kind::Word:
        return font->getWidth(segment.text) + WORD_PAD;

    case Segment::Kind::Choice:
    {
        S32 widest = 0;
        for (const auto& [label, value] : segment.choices)
        {
            widest = llmax(widest, font->getWidth(label));
        }
        return widest + CHOICE_CHROME;
    }

    case Segment::Kind::Field:
        break;
    }
    return MIN_FIELD;
}

// Every part as wide as what is in it, and the field with whatever is left:
// that is the difference between a sentence and a row of columns.
void ALScopeBar::layout()
{
    const S32 height = getRect().getHeight();
    const S32 top = height - (height - mControlHeight) / 2;
    const S32 bottom = top - mControlHeight;

    S32 adorned = 0;
    if (mAdornment)
    {
        adorned = mAdornment->getRect().getWidth() + mGap;
    }

    S32 fixed = 0;
    S32 fields = 0;
    for (size_t i = 0; i < mSegments.size(); ++i)
    {
        if (mSegments[i].kind == Segment::Kind::Field)
        {
            ++fields;
        }
        else
        {
            fixed += widthOf(mSegments[i]);
        }
        fixed += i + 1 < mSegments.size() ? mGap : 0;
    }

    const S32 room = getRect().getWidth() - adorned - fixed;
    const S32 each = fields > 0 ? llmax(MIN_FIELD, room / fields) : 0;

    S32 left = 0;
    for (size_t i = 0; i < mParts.size() && i < mSegments.size(); ++i)
    {
        const S32 width = mSegments[i].kind == Segment::Kind::Field
                        ? each : widthOf(mSegments[i]);
        mParts[i]->setShape(LLRect(left, top, left + width, bottom));
        left += width + mGap;
    }

    if (mAdornment)
    {
        const S32 width = mAdornment->getRect().getWidth();
        mAdornment->setShape(LLRect(getRect().getWidth() - width, top,
                                    getRect().getWidth(), bottom));
    }
}

void ALScopeBar::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    layout();
}

std::string ALScopeBar::valueOf(std::string_view name) const
{
    for (size_t i = 0; i < mSegments.size() && i < mParts.size(); ++i)
    {
        if (mSegments[i].name != name || mSegments[i].kind == Segment::Kind::Word)
        {
            continue;
        }
        if (const LLUICtrl* ctrl = mParts[i]->as<LLUICtrl>())
        {
            return ctrl->getValue().asString();
        }
    }
    return std::string();
}

void ALScopeBar::setValue(std::string_view name, const std::string& value)
{
    for (size_t i = 0; i < mSegments.size() && i < mParts.size(); ++i)
    {
        if (mSegments[i].name != name || mSegments[i].kind == Segment::Kind::Word)
        {
            continue;
        }
        mSegments[i].value = value;
        if (LLComboBox* choice = mParts[i]->as<LLComboBox>())
        {
            choice->setSelectedByValue(LLSD(value), true);
        }
        else if (LLLineEditor* field = mParts[i]->as<LLLineEditor>())
        {
            field->setText(value);
        }
        return;
    }
}

LLSD ALScopeBar::query() const
{
    LLSD said;
    for (const Segment& segment : mSegments)
    {
        if (segment.kind == Segment::Kind::Word || segment.name.empty())
        {
            continue;
        }
        said[segment.name] = valueOf(segment.name);
    }
    return said;
}

void ALScopeBar::setAdornment(LLView* view)
{
    if (mAdornment)
    {
        removeChild(mAdornment);
        delete mAdornment;
    }
    mAdornment = view;
    if (mAdornment)
    {
        addChild(mAdornment);
    }
    layout();
}
