/**
 * @file aloutputlist.cpp
 * @brief A bounded, filterable log list.
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

#include "aloutputlist.h"

static LLDefaultChildRegistry::Register<ALOutputList> r("output_list");

ALOutputList::Params::Params()
:   capacity("capacity", 500)
{
}

ALOutputList::ALOutputList(const Params& p)
:   LLScrollListCtrl(p),
    mCapacity(llmax(1, p.capacity()))
{
}

bool ALOutputList::atTail() const
{
    return getScrollPos() + getPageLines() >= getItemCount();
}

void ALOutputList::show(const Entry& entry, U32 serial)
{
    LLSD row;
    row["value"] = static_cast<S32>(serial);
    S32 n = 0;
    auto cell = [&](const char* column, const std::string& text) {
        if (!getColumn(column))
        {
            return;
        }
        row["columns"][n]["column"] = column;
        row["columns"][n]["value"]  = text;
        if (entry.color)
        {
            row["columns"][n]["color"] = entry.color->getValue();
        }
        ++n;
    };
    cell("time", entry.time);
    cell("source", entry.source);
    cell("kind", entry.kind);
    cell("text", entry.text);
    addElement(row, ADD_BOTTOM);
}

void ALOutputList::append(Entry entry)
{
    const bool follow = atTail();
    const U32  serial = mNextSerial++;
    mEntries.push_back(std::move(entry));
    mSerials.push_back(serial);
    while (static_cast<S32>(mEntries.size()) > mCapacity)
    {
        const U32 oldest = mSerials.front();
        mEntries.pop_front();
        mSerials.pop_front();
        for (LLScrollListItem* item : getItemList())
        {
            if (static_cast<U32>(item->getValue().asInteger()) == oldest)
            {
                deleteSingleItem(item);
                break;
            }
        }
    }
    if (passes(mEntries.back()))
    {
        show(mEntries.back(), serial);
        if (follow)
        {
            setScrollPos(getItemCount());
        }
    }
}

void ALOutputList::clearEntries()
{
    mEntries.clear();
    mSerials.clear();
    deleteAllItems();
}

void ALOutputList::setCapacity(S32 capacity)
{
    mCapacity = llmax(1, capacity);
    if (static_cast<S32>(mEntries.size()) > mCapacity)
    {
        while (static_cast<S32>(mEntries.size()) > mCapacity)
        {
            mEntries.pop_front();
            mSerials.pop_front();
        }
        refill();
    }
}

void ALOutputList::setFilter(filter_t filter)
{
    mFilter = std::move(filter);
    refill();
}

void ALOutputList::refill()
{
    const bool follow = atTail();
    deleteAllItems();
    for (size_t i = 0; i < mEntries.size(); ++i)
    {
        if (passes(mEntries[i]))
        {
            show(mEntries[i], mSerials[i]);
        }
    }
    if (follow)
    {
        setScrollPos(getItemCount());
    }
}

const ALOutputList::Entry* ALOutputList::chosen() const
{
    const LLScrollListItem* item = getFirstSelected();
    if (!item)
    {
        return nullptr;
    }
    const U32 serial = static_cast<U32>(item->getValue().asInteger());
    for (size_t i = 0; i < mSerials.size(); ++i)
    {
        if (mSerials[i] == serial)
        {
            return &mEntries[i];
        }
    }
    return nullptr;
}
