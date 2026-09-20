/**
 * @file aloutputlist.h
 * @brief A bounded, filterable log list: what something said, the last so many of them.
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

#pragma once

#include "llscrolllistctrl.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>

// A log a pane shows: the last so many things something said, each with
// when, who and what kind, kept in the order they came and shown through
// a filter. The list follows its tail while the tail is what is being
// looked at, and lets go of the oldest once it holds its fill, which is
// what a log that runs for a session has to do.
//
// The columns are the list's own, declared as any scroll list's are, by
// the names "time", "source", "kind" and "text"; an entry fills the ones
// there are.
class ALOutputList : public LLScrollListCtrl
{
public:
    AL_VIEW_TYPE(ALOutputList, LLScrollListCtrl);

    struct Params : public LLInitParam::Block<Params, LLScrollListCtrl::Params>
    {
        // How many entries are kept; the oldest go past it.
        Optional<S32> capacity;
        Params();
    };

    struct Entry
    {
        std::string             time;
        std::string             source;
        std::string             kind;
        std::string             text;
        // The row's ink, where it is not the list's own.
        std::optional<LLColor4> color;
        // What the caller wants back when the row is chosen.
        LLSD                    value;
        // What a filter may go by beside the columns: the caller's own
        // key, an object's id say.
        LLSD                    key;
    };

    void                     append(Entry entry);
    void                     clearEntries();
    const std::deque<Entry>& entries() const { return mEntries; }
    S32                      capacity() const { return mCapacity; }
    void                     setCapacity(S32 capacity);

    // Which entries are shown: all of them, or the ones the filter takes.
    typedef std::function<bool(const Entry&)> filter_t;
    void setFilter(filter_t filter);

    // The entry of the row chosen, or null.
    const Entry* chosen() const;

protected:
    friend class LLUICtrlFactory;
    ALOutputList(const Params& p);

private:
    void refill();
    void show(const Entry& entry, U32 serial);
    bool passes(const Entry& entry) const { return !mFilter || mFilter(entry); }
    // Whether the last row is in sight, which is when a new one should be.
    bool atTail() const;

    std::deque<Entry> mEntries;
    std::deque<U32>   mSerials;
    U32               mNextSerial = 1;
    filter_t          mFilter;
    S32               mCapacity = 500;
};
