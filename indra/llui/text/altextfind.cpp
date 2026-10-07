/**
 * @file altextfind.cpp
 * @brief What a find over a text found, kept in step with its edits.
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

#include "altextfind.h"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

struct ALTextFind::Working
{
    std::mutex               lock;
    std::condition_variable  done;
    bool                     finished = false;
    std::vector<ALTextRange> found;
    std::string              error;
    // What it was asked, to ask it again where the text has moved on.
    U32                      version = 0;
    std::string              query;
    ALTextSearchOptions      options;
};

void ALTextFind::search(const ALTextDocument& doc, const std::string& query, const ALTextSearchOptions& options, bool in_selection,
                        const ALTextRange& selection)
{
    ++mGeneration;
    mStale = false;
    if (in_selection)
    {
        if (!mInSelection)
        {
            mScope       = selection;
            mInSelection = true;
        }
    }
    else
    {
        mInSelection = false;
    }
    // No more than a list of them is any use as: a letter over a long
    // text is otherwise every place it stands.
    ALTextSearchOptions capped = options;
    capped.limit               = LIMIT;
    // A long text, or a pattern across lines, on a worker over a copy: the
    // matches there were stand until its come.
    if (!query.empty() && (doc.byteCount() > ON_A_WORKER || (options.regex && options.acrossLines)))
    {
        auto working     = std::make_shared<Working>();
        working->version = doc.version();
        working->query   = query;
        working->options = options;
        mWorking         = working;
        std::optional<ALTextRange> scope;
        if (mInSelection)
        {
            scope = mScope;
        }
        std::thread([working, text = doc.text(), query, capped, scope]() {
            const ALTextDocument     copy(text);
            std::string              error;
            std::vector<ALTextRange> found = ALTextSearch::matches(copy, query, capped, scope ? &*scope : nullptr, &error);
            std::lock_guard<std::mutex> guard(working->lock);
            working->found    = std::move(found);
            working->error    = std::move(error);
            working->finished = true;
            working->done.notify_all();
        }).detach();
        return;
    }
    mWorking.reset();
    std::vector<ALTextRange> found = ALTextSearch::matches(doc, query, capped, mInSelection ? &mScope : nullptr, &mError);
    take(doc, std::move(found), mError, selection);
}

void ALTextFind::take(const ALTextDocument& doc, std::vector<ALTextRange> found, const std::string& error, const ALTextRange& selection)
{
    ++mGeneration;
    mError = error;
    mMatches.assign(std::move(found));
    // The current one is the match the selection is.
    mCurrent = -1;
    for (size_t i = 0; i < mMatches.size(); ++i)
    {
        if (mMatches[i] == selection)
        {
            mCurrent = static_cast<S32>(i);
            break;
        }
    }
}

bool ALTextFind::collect(const ALTextDocument& doc, const ALTextRange& selection, bool wait)
{
    if (!mWorking)
    {
        return false;
    }
    std::shared_ptr<Working> working = mWorking;
    std::unique_lock<std::mutex> guard(working->lock);
    if (wait)
    {
        working->done.wait(guard, [&working]() { return working->finished; });
    }
    if (!working->finished)
    {
        return false;
    }
    mWorking.reset();
    if (working->version != doc.version())
    {
        // Of a text since changed. Where a search is due -- the query
        // changed meanwhile -- that one is left to be made, with what is
        // asked for then; else looked for again, as the text is now.
        if (mStale)
        {
            return false;
        }
        const std::string         query   = working->query;
        const ALTextSearchOptions options = working->options;
        guard.unlock();
        search(doc, query, options, mInSelection, selection);
        // Looked through at once, or on a worker again.
        return !mWorking || (wait && collect(doc, selection, true));
    }
    std::vector<ALTextRange> found = std::move(working->found);
    const std::string        error = working->error;
    guard.unlock();
    take(doc, std::move(found), error, selection);
    return true;
}

void ALTextFind::clear()
{
    ++mGeneration;
    // A worker still looking is let go of; what it finds, nobody takes.
    mWorking.reset();
    mMatches.clear();
    mCurrent = -1;
    mStale   = false;
    // And the stretch a find in a selection kept to: the next one asked
    // for keeps to the selection then.
    mInSelection = false;
    mScope       = ALTextRange();
}

void ALTextFind::edited(const ALTextDocument::Edit& edit)
{
    ++mGeneration;
    if (mInSelection)
    {
        mScope = edit.stretched(mScope);
    }
    // Its matches slide with the text, those the edit cut through going,
    // until the text is looked through again.
    mMatches.apply(edit, &mCurrent);
}

void ALTextFind::stale()
{
    if (!mStale)
    {
        mStaleFor.reset();
    }
    mStale = true;
    mSettle.reset();
}

bool ALTextFind::due() const
{
    constexpr F32 SETTLE = 0.2f;
    // No longer than this between looks however often the text changes: a
    // log taking entries faster than it settles is looked through all the
    // same.
    constexpr F32 LONGEST = 1.f;
    return mStale && (mSettle.getElapsedTimeF32() >= SETTLE || mStaleFor.getElapsedTimeF32() >= LONGEST);
}

S32 ALTextFind::nearest(const ALTextPos& from, bool forward) const
{
    return ALTextSearch::nearest(mMatches.items(), from, forward);
}

std::vector<std::pair<ALTextRange, std::string>> ALTextFind::replacements(const ALTextDocument& doc, const std::string& query,
                                                                          const ALTextSearchOptions& options, const std::string& with) const
{
    if (capped())
    {
        ALTextSearchOptions every = options;
        every.limit               = 0;
        return ALTextSearch::replacements(doc, query, every, with, mInSelection ? &mScope : nullptr);
    }
    std::vector<std::pair<ALTextRange, std::string>> out;
    out.reserve(mMatches.size());
    for (const ALTextRange& match : mMatches.items())
    {
        out.emplace_back(match, ALTextSearch::replacement(doc, match, query, options, with));
    }
    return out;
}

std::vector<ALTextRange> ALTextFind::take()
{
    ++mGeneration;
    std::vector<ALTextRange> out = mMatches.take();
    mMatches.clear();
    mCurrent = -1;
    return out;
}

void ALTextFind::restore(std::vector<ALTextRange> matches, S32 current)
{
    ++mGeneration;
    mMatches.assign(std::move(matches));
    mCurrent = current;
}
