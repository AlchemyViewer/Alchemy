/**
 * @file alscriptsnapshot.h
 * @brief What the preprocessor needs to expand one script, taken ahead of the run: run on any thread.
 *
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
 */

#pragma once

#include "alpreprocessor.h"
#include "llstl.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <string>
#include <string_view>
#include <vector>

// What the preprocessor needs of the viewer to expand one script, taken on
// the main thread and read on any other: the options as values, and every
// include the script asked for when it was last expanded, already resolved
// to its text. So a run over a snapshot touches nothing of the viewer's --
// no inventory, no object, no setting, no cache -- and belongs on a thread
// of its own while the main one draws. What a run asks for that the
// snapshot does not hold is noted rather than looked up: the main thread
// resolves those, fetches what is in the world, and takes another snapshot.
class ALScriptSnapshot
{
public:
    // What the run is made with; its `resolve` is the snapshot's own.
    ALPreprocessor::Options&       options() { return mOptions; }
    const ALPreprocessor::Options& options() const { return mOptions; }
    // What a name stands for, looked up ahead of the run: Yes, with what
    // it found, or No. One left Pending is not answered at all.
    void answer(const ALPreprocessor::Ask& ask, ALPreprocessor::Found found, ALPreprocessor::Include include = ALPreprocessor::Include());

    // Expands a script with what the snapshot holds. Any thread.
    ALPreprocessor::Result run(std::string_view source);
    // What the last run asked for and the snapshot could not answer,
    // each once, in the order asked; taken away by the asking.
    std::vector<ALPreprocessor::Ask> missed() { return std::move(mMissed); }

    // One key for the three things that decide what a name stands for.
    static std::string keyOf(const ALPreprocessor::Ask& ask);

private:
    struct Answer
    {
        ALPreprocessor::Found   found = ALPreprocessor::Found::No;
        ALPreprocessor::Include include;
    };

    ALPreprocessor::Options                                                          mOptions;
    boost::unordered_flat_map<std::string, Answer, ll::string_hash, std::equal_to<>> mAnswers;
    std::vector<ALPreprocessor::Ask>                                                 mMissed;
};
