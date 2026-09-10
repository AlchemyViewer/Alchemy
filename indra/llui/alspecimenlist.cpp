/**
 * @file alspecimenlist.cpp
 * @brief A list whose rows are the widgets themselves rather than words about them.
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

#include "alspecimenlist.h"

#include "alemptystate.h"
#include "llfontgl.h"
#include "llscrollcontainer.h"
#include "lltextbox.h"
#include "lluicolortable.h"
#include "lluictrlfactory.h"

#include <algorithm>
#include <functional>

static LLDefaultChildRegistry::Register<ALSpecimenList> r("specimen_list");

namespace
{
    constexpr S32 HEADING_HEIGHT = 20;
    constexpr S32 INSET = 6;
    constexpr S32 GAP = 8;

    bool holds(std::string_view haystack, std::string_view needle)
    {
        const auto same = [](char a, char b)
        {
            return LLStringOps::toLower(a) == LLStringOps::toLower(b);
        };
        return std::search(haystack.begin(), haystack.end(),
                           needle.begin(), needle.end(), same) != haystack.end();
    }
}

// One row: a label, and the thing itself beside it.
//
// The click is taken here rather than passed down. A specimen is a picture of
// a widget: a list of buttons is not a list of things to press, and a person
// choosing the row means the row.
class ALSpecimenList::Row final : public LLPanel
{
public:
    AL_VIEW_TYPE(ALSpecimenList::Row, LLPanel);

    Row(const LLPanel::Params& p, std::string value, std::function<void(const std::string&)> chose)
    :   LLPanel(p),
        mValue(std::move(value)),
        mChose(std::move(chose))
    {
    }

    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        // Not LLPanel::handleMouseDown, which would offer it to the specimen
        // first and let a button in the list be pressed.
        if (mChose)
        {
            mChose(mValue);
        }
        return true;
    }

    void draw() override
    {
        if (mMarked)
        {
            static const LLUIColor mark =
                LLUIColorTable::instance().getColor("ScrollSelectedBGColor", LLColor4::blue);
            gl_rect_2d(getLocalRect(), mark.get(), true);
        }
        LLPanel::draw();
    }

    void setMarked(bool marked) { mMarked = marked; }
    const std::string& value() const { return mValue; }

private:
    std::string                             mValue;
    std::function<void(const std::string&)> mChose;
    bool                                    mMarked = false;
};

ALSpecimenList::Params::Params()
:   row_height("row_height", 30),
    label_width("label_width", 120),
    empty_headline("empty_headline"),
    empty_sentence("empty_sentence")
{
}

ALSpecimenList::ALSpecimenList(const Params& p)
:   LLPanel(p),
    mEmptyHeadline(p.empty_headline),
    mEmptySentence(p.empty_sentence),
    mRowHeight(p.row_height),
    mLabelWidth(p.label_width)
{
    LLScrollContainer::Params sp(LLUICtrlFactory::getDefaultParams<LLScrollContainer>());
    sp.name = "scroller";
    sp.rect = getLocalRect();
    sp.follows.flags = FOLLOWS_ALL;
    mScroller = LLUICtrlFactory::create<LLScrollContainer>(sp);
    addChild(mScroller);

    LLPanel::Params cp(LLUICtrlFactory::getDefaultParams<LLPanel>());
    cp.name = "content";
    cp.rect = LLRect(0, getRect().getHeight(), getRect().getWidth(), 0);
    cp.background_visible = false;
    cp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
    mContent = LLUICtrlFactory::create<LLPanel>(cp);
    mScroller->addChild(mContent);

    ALEmptyState::Params ep(LLUICtrlFactory::getDefaultParams<ALEmptyState>());
    ep.name = "empty";
    ep.rect = getLocalRect();
    ep.follows.flags = FOLLOWS_ALL;
    ep.background_visible = false;
    mEmpty = LLUICtrlFactory::create<ALEmptyState>(ep);
    addChild(mEmpty);
    mEmpty->say(mEmptyHeadline, mEmptySentence);
    mEmpty->setVisible(false);
}

ALSpecimenList::~ALSpecimenList() = default;

void ALSpecimenList::setSpecimens(std::vector<Specimen> specimens)
{
    for (Row* row : mRows)
    {
        mContent->removeChild(row);
        delete row;
    }
    mRows.clear();
    for (LLTextBox* heading : mHeadings)
    {
        mContent->removeChild(heading);
        delete heading;
    }
    mHeadings.clear();
    mGroups.clear();
    mSpecimens = std::move(specimens);

    // The headings, in the order they first appear: which things belong
    // together is a fact about them and not about their names, so they are
    // not sorted.
    for (const Specimen& specimen : mSpecimens)
    {
        if (!specimen.group.empty()
            && std::find(mGroups.begin(), mGroups.end(), specimen.group) == mGroups.end())
        {
            mGroups.push_back(specimen.group);
        }
    }
    for (const std::string& group : mGroups)
    {
        LLTextBox::Params tp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        tp.name = "heading_" + group;
        tp.initial_value = group;
        tp.rect = LLRect(0, HEADING_HEIGHT, getRect().getWidth(), 0);
        tp.font = LLFontGL::getFontSansSerifSmallBold();
        LLTextBox* made = LLUICtrlFactory::create<LLTextBox>(tp);
        mContent->addChild(made);
        mHeadings.push_back(made);
    }

    for (Specimen& specimen : mSpecimens)
    {
        LLPanel::Params rp(LLUICtrlFactory::getDefaultParams<LLPanel>());
        rp.name = "row_" + specimen.value;
        rp.rect = LLRect(0, mRowHeight, getRect().getWidth(), 0);
        rp.background_visible = false;
        rp.follows.flags = FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_RIGHT;
        rp.tool_tip = specimen.toolTip;
        const std::string value = specimen.value;
        Row* row = new Row(rp, value, [this](const std::string& chosen) { choose(chosen); });
        row->initFromParams(rp);

        LLTextBox::Params lp(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        lp.name = "label";
        lp.initial_value = specimen.label;
        lp.rect = LLRect(INSET, mRowHeight - 8, INSET + mLabelWidth, 8);
        lp.font = LLFontGL::getFontSansSerifSmall();
        lp.use_ellipses = true;
        row->addChild(LLUICtrlFactory::create<LLTextBox>(lp));

        if (specimen.view)
        {
            row->addChild(specimen.view);
        }
        mContent->addChild(row);
        mRows.push_back(row);
    }
    layout();
}

bool ALSpecimenList::passes(const Specimen& specimen) const
{
    return mFilter.empty() || holds(specimen.label, mFilter) || holds(specimen.group, mFilter);
}

void ALSpecimenList::filter(const std::string& text)
{
    mFilter = text;
    layout();
}

void ALSpecimenList::choose(const std::string& value)
{
    mChosen = value;
    for (Row* row : mRows)
    {
        row->setMarked(row->value() == value);
    }
    mChose(value);
}

// Down the list: rows under no heading first, then each heading and what is
// under it. A heading the filter has left with nothing under it goes with
// what it held, and a row the filter has narrowed away is hidden rather than
// destroyed -- building a widget is the expensive part, and a filter is typed
// a letter at a time.
void ALSpecimenList::layout()
{
    if (!mContent || !mScroller)
    {
        return;
    }
    const S32 width = mScroller->getContentWindowRect().getWidth();

    // What is shown, in the order it is shown, before anything is placed:
    // the content panel has to be as tall as the whole of it, and a row is
    // placed from the content's top.
    struct Placed { LLView* view = nullptr; S32 height = 0; };
    std::vector<Placed> order;
    mShown = 0;

    const auto sweep = [&](const std::string& group, LLTextBox* heading)
    {
        bool any = false;
        for (size_t i = 0; i < mSpecimens.size() && i < mRows.size(); ++i)
        {
            if (mSpecimens[i].group != group)
            {
                continue;
            }
            const bool keep = passes(mSpecimens[i]);
            mRows[i]->setVisible(keep);
            if (!keep)
            {
                continue;
            }
            if (heading && !any)
            {
                order.push_back({ heading, HEADING_HEIGHT });
            }
            any = true;
            order.push_back({ mRows[i], mRowHeight });
            ++mShown;
        }
        if (heading)
        {
            heading->setVisible(any);
        }
    };

    sweep(std::string(), nullptr);
    for (size_t g = 0; g < mGroups.size(); ++g)
    {
        sweep(mGroups[g], g < mHeadings.size() ? mHeadings[g] : nullptr);
    }

    S32 total = GAP;
    for (const Placed& one : order)
    {
        total += one.height;
    }
    const S32 tall = llmax(total, mScroller->getContentWindowRect().getHeight());
    mContent->reshape(width, tall);
    mContent->setOrigin(0, 0);

    S32 top = tall;
    for (const Placed& one : order)
    {
        const S32 left = one.height == HEADING_HEIGHT ? INSET : 0;
        one.view->setShape(LLRect(left, top, width, top - one.height));
        top -= one.height;
    }

    mEmpty->setVisible(mShown == 0);
    mScroller->setVisible(mShown > 0);
}

void ALSpecimenList::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);
    if (mScroller)
    {
        mScroller->reshape(width, height);
    }
    if (mEmpty)
    {
        mEmpty->reshape(width, height);
    }
    layout();
}
