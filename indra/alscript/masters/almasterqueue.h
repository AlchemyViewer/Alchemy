/**
 * @file almasterqueue.h
 * @brief The sends of master files to their scripts underway, and those waiting their turn.
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

#include "almasterplan.h"

#include "llstl.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Which sends of master files to their scripts go now, and which wait. A
// script has one send at a time: a send asked while one is on its way goes
// again after it, once, so that the file's newest text goes up last, and
// however often the file was saved meanwhile. And no more than `limit`
// scripts are sent at once -- an include saved may send a hundred -- the
// rest waiting their turn, first come, first served.
//
// A send asked again while it waits, or while it is on its way, is still
// one send, of the kind that matters most: a save of the master outranks
// the studio's own send, since a save always uploads where the studio's
// own may be held.
//
// Scripts are named by a key, ALScriptRef::id(). Nothing is sent from
// here: what starts is handed back, for the caller to start, and the
// caller says when each has finished, however it ended.
class ALMasterQueue
{
public:
    // At least one at a time, however few are asked for.
    explicit ALMasterQueue(size_t limit = 4);

    // A send asked: true where it starts now, for the caller to start;
    // false where it waits -- behind a send to the same script, or for a
    // free place.
    bool ask(const std::string& key, ALMasterPlan::Send kind);
    // A send ended, however it did: the sends to start now, in order, each
    // with its kind, for the caller to start. A send asked again while it
    // was on its way waits behind every other, so that what is asked first
    // goes first.
    std::vector<std::pair<std::string, ALMasterPlan::Send>> finished(const std::string& key);
    // A script not to send after all -- its link undone, say -- whether it
    // waits or is to go again; a send already on its way goes on.
    void drop(const std::string& key);

    // Whether a send to the script is on its way; whether another waits to
    // start, its turn not yet come or to go again after the one on its way.
    bool underway(const std::string& key) const;
    bool waiting(const std::string& key) const;
    // The sends on their way now, and the most there may be.
    size_t busy() const { return mUnderway.size(); }
    size_t limit() const { return mLimit; }
    void   clear();

private:
    using Send = ALMasterPlan::Send;

    // The kind a send asked twice is: a save of the master's if either is.
    static Send outranking(Send had, Send asked);
    void        line(const std::string& key, Send kind);

    struct Waiting
    {
        U64  order = 0;
        Send kind  = Send::Derived;
    };

    size_t mLimit;
    U64    mNextOrder = 0;
    // The scripts a send is on its way to, and, where another is asked
    // after it, of which kind.
    boost::unordered_flat_map<std::string, std::optional<Send>, ll::string_hash, std::equal_to<>> mUnderway;
    // Those waiting their turn, by key, and in the order of it.
    boost::unordered_flat_map<std::string, Waiting, ll::string_hash, std::equal_to<>> mWaiting;
    std::map<U64, std::string>                                                         mLine;
};
