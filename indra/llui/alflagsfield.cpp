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
    // As many names as the bits of the word they are read into.
    constexpr size_t MOST_NAMES = 32;

    bool sameWord(std::string_view a, std::string_view b)
    {
        return a.size() == b.size()
            && std::equal(a.begin(), a.end(), b.begin(),
                          [](char x, char y) { return LLStringOps::toLower(x) == LLStringOps::toLower(y); });
    }
}

// static
U32 ALFlagsField::read(std::string_view text, std::span<const std::string> names, std::string_view all_word)
{
    const size_t count = llmin(names.size(), MOST_NAMES);
    const U32 every = count >= MOST_NAMES ? ~0u : (1u << count) - 1u;
    U32 bits = 0;
    for (size_t start = 0; start <= text.size(); )
    {
        const size_t bar = text.find('|', start);
        const std::string_view token = text.substr(start, bar == std::string_view::npos ? std::string_view::npos : bar - start);
        if (!all_word.empty() && sameWord(token, all_word))
        {
            bits = every;
        }
        else
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (!names[i].empty() && sameWord(token, names[i]))
                {
                    bits |= 1u << i;
                }
            }
        }
        if (bar == std::string_view::npos)
        {
            break;
        }
        start = bar + 1;
    }
    return bits;
}

// static
std::string ALFlagsField::write(U32 bits, std::span<const std::string> names,
                                const std::string& all_word, const std::string& none_word)
{
    const size_t count = llmin(names.size(), MOST_NAMES);
    const U32 every = count >= MOST_NAMES ? ~0u : (1u << count) - 1u;
    bits &= every;
    if (count > 0 && bits == every && !all_word.empty())
    {
        return all_word;
    }
    if (bits == 0)
    {
        return none_word;
    }
    std::string out;
    for (size_t i = 0; i < count; ++i)
    {
        if ((bits & (1u << i)) && !names[i].empty())
        {
            out += out.empty() ? names[i] : "|" + names[i];
        }
    }
    return out;
}

ALFlagsField::ALFlagsField(const Params& p)
:   LLUICtrl(p)
{
}

void ALFlagsField::setFlags(std::vector<std::string> names, std::string all_word, std::string none_word)
{
    mNames = std::move(names);
    if (mNames.size() > MOST_NAMES)
    {
        mNames.resize(MOST_NAMES);
    }
    mAll = std::move(all_word);
    mNone = std::move(none_word);
    mBits = 0;
    rebuild();
}

void ALFlagsField::setValue(const LLSD& value)
{
    mBits = read(value.asString(), mNames, mAll);
    for (size_t i = 0; i < mBoxes.size() && i < mNames.size(); ++i)
    {
        mBoxes[i]->setValue((mBits & (1u << i)) != 0);
    }
}

LLSD ALFlagsField::getValue() const
{
    return write(mBits, mNames, mAll, mNone);
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

    for (size_t i = 0; i < mNames.size(); ++i)
    {
        LLCheckBoxCtrl::Params p;
        p.name = mNames[i];
        p.label = mNames[i];
        p.font = LLFontGL::getFontSansSerifSmall();
        p.rect = LLRect(0, getRect().getHeight(), 10, 0);
        p.initial_value = (mBits & (1u << i)) != 0;
        LLCheckBoxCtrl* box = LLUICtrlFactory::create<LLCheckBoxCtrl>(p);
        const U32 bit = 1u << i;
        box->setCommitCallback([this, bit](LLUICtrl* ctrl, const LLSD&)
        {
            mBits = ctrl->getValue().asBoolean() ? (mBits | bit) : (mBits & ~bit);
            onToggle();
        });
        addChild(box);
        mBoxes.push_back(box);
    }
    layout();
}

void ALFlagsField::layout()
{
    if (mBoxes.empty())
    {
        return;
    }
    const S32 width = getRect().getWidth() / (S32)mBoxes.size();
    for (size_t i = 0; i < mBoxes.size(); ++i)
    {
        mBoxes[i]->setShape(LLRect((S32)i * width, getRect().getHeight(), (S32)(i + 1) * width, 0));
    }
}

void ALFlagsField::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLUICtrl::reshape(width, height, called_from_parent);
    layout();
}

void ALFlagsField::onToggle()
{
    onCommit();
}
