/**
 * @file alscriptsnapshot.cpp
 * @brief What the preprocessor needs to expand one script, taken ahead of the run: run on any thread.
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

#include "linden_common.h"

#include "alscriptsnapshot.h"

#include <algorithm>

void ALScriptSnapshot::answer(const ALPreprocessor::Ask& ask, ALPreprocessor::Found found, ALPreprocessor::Include include)
{
    Answer answer;
    answer.found   = found;
    answer.include = std::move(include);
    mAnswers.emplace(keyOf(ask), std::move(answer));
}

// static
std::string ALScriptSnapshot::keyOf(const ALPreprocessor::Ask& ask)
{
    // What a name stands for is decided by the name, who is asking, and
    // whether it is a require or an include of either kind.
    std::string key = ask.from;
    key += ask.require ? "\x01r" : ask.angled ? "\x01<" : "\x01\"";
    key += ask.name;
    return key;
}

ALPreprocessor::Result ALScriptSnapshot::run(std::string_view source)
{
    mMissed.clear();
    ALPreprocessor::Options options = mOptions;
    options.resolve                 = [this](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
        const std::string key    = keyOf(ask);
        const auto        answer = mAnswers.find(key);
        if (answer == mAnswers.end())
        {
            // Nobody has looked this name up yet -- a fresh script, or
            // one whose includes the author has just changed. Noted for
            // the main thread, which is the only one that may look
            // anything up, and pending, so that the run goes on and
            // says what it wanted.
            if (std::none_of(mMissed.begin(), mMissed.end(), [&key](const ALPreprocessor::Ask& was) { return keyOf(was) == key; }))
            {
                mMissed.push_back(ask);
            }
            return ALPreprocessor::Found::Pending;
        }
        // Found, or not found and what the search before found instead.
        if (answer->second.found != ALPreprocessor::Found::Pending)
        {
            out = answer->second.include;
        }
        return answer->second.found;
    };
    return ALPreprocessor::run(source, options);
}
