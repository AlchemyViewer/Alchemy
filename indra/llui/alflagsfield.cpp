/**
 * @file alflagsfield.cpp
 * @brief A set of flags as a XUI file writes one: names with bars between them.
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

#include "alflagsfield.h"

#include "llcheckboxctrl.h"
#include "lluictrlfactory.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<ALFlagsField> r("flags_field");

namespace
{
    bool sameWord(std::string_view a, std::string_view b)
    {
        return a.size() == b.size()
            && std::equal(a.begin(), a.end(), b.begin(),
                          [](char x, char y) { return LLStringOps::toLower(x) == LLStringOps::toLower(y); });
    }
}

ALFlagsField::ALFlagsField(const Params& p)
:   LLUICtrl(p)
{
}

void ALFlagsField::setFlags(std::vector<std::string> names, std::string all_word, std::string none_word)
{
    mNames = std::move(names);
    mAll = std::move(all_word);
    mNone = std::move(none_word);
    mSet.assign(mNames.size(), false);
    rebuild();
}

void ALFlagsField::setValue(const LLSD& value)
{
    const std::string text = value.asString();
    mSet.assign(mNames.size(), false);

    for (size_t start = 0; start <= text.size(); )
    {
        const size_t bar = text.find('|', start);
        const std::string_view token(text.data() + start,
                                     (bar == std::string::npos ? text.size() : bar) - start);
        if (!mAll.empty() && sameWord(token, mAll))
        {
            mSet.assign(mNames.size(), true);
        }
        else
        {
            for (size_t i = 0; i < mNames.size(); ++i)
            {
                if (sameWord(token, mNames[i]))
                {
                    mSet[i] = true;
                }
            }
        }
        if (bar == std::string::npos)
        {
            break;
        }
        start = bar + 1;
    }

    for (size_t i = 0; i < mBoxes.size() && i < mSet.size(); ++i)
    {
        mBoxes[i]->setValue((bool)mSet[i]);
    }
}

LLSD ALFlagsField::getValue() const
{
    const bool every = !mSet.empty() && std::all_of(mSet.begin(), mSet.end(), [](bool on) { return on; });
    const bool any = std::any_of(mSet.begin(), mSet.end(), [](bool on) { return on; });
    if (every && !mAll.empty())
    {
        return mAll;
    }
    if (!any)
    {
        return mNone;
    }
    std::string out;
    for (size_t i = 0; i < mNames.size(); ++i)
    {
        if (mSet[i])
        {
            out += out.empty() ? mNames[i] : "|" + mNames[i];
        }
    }
    return out;
}

// One box per name, sharing the width evenly. The names are short -- four
// edges, three styles -- so the label is the name itself rather than an
// abbreviation of it that would have to be learned.
void ALFlagsField::rebuild()
{
    deleteAllChildren();
    mBoxes.clear();
    if (mNames.empty())
    {
        return;
    }

    const S32 width = getRect().getWidth() / (S32)mNames.size();
    for (size_t i = 0; i < mNames.size(); ++i)
    {
        LLCheckBoxCtrl::Params p;
        p.name = mNames[i];
        p.label = mNames[i];
        p.font = LLFontGL::getFontSansSerifSmall();
        p.rect = LLRect((S32)i * width, getRect().getHeight(), (S32)(i + 1) * width, 0);
        p.initial_value = (bool)mSet[i];
        LLCheckBoxCtrl* box = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
        const size_t which = i;
        box->setCommitCallback([this, which](LLUICtrl* ctrl, const LLSD&)
        {
            mSet[which] = ctrl->getValue().asBoolean();
            onToggle();
        });
        addChild(box);
        mBoxes.push_back(box);
    }
}

void ALFlagsField::onToggle()
{
    onCommit();
}
