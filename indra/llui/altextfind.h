/**
 * @file altextfind.h
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


#pragma once

#include "alanchoredranges.h"
#include "altextdocument.h"
#include "altextsearch.h"
#include "llframetimer.h"

#include <memory>
#include <string>
#include <vector>

// What a find over a text found, kept in step with the text: each match,
// in order, which of them is current, the stretch a find in a selection
// keeps to, and what was wrong with a pattern. Looked through again a
// moment after the text stops changing, not at every edit. Nothing here
// draws or asks anything of a view: the view's find bar says what to look
// for, and the view draws what was found.
//
// A long text, or a pattern that reads across lines, is looked through on
// a worker over a copy of it, so that a slow one holds nothing up: the
// matches there were stand until the worker's come in (collect); one of a
// text changed since is looked for again. Whatever acts on the matches
// waits for them (settle), so that it acts on the text as it is.
class ALTextFind
{
public:
    // The text looked through now, for the query as the options say, in
    // the whole text or -- asked for -- the selection, kept to from the
    // first time it is asked for until it is not. The current match is
    // the one the selection is.
    void search(const ALTextDocument& doc, const std::string& query, const ALTextSearchOptions& options, bool in_selection,
                const ALTextRange& selection);
    // A worker's matches, where they have come in: taken where the text is
    // as it was looked through, and true; looked for again where it is
    // not. With `wait`, waited for.
    bool collect(const ALTextDocument& doc, const ALTextRange& selection, bool wait = false);
    bool searching() const { return mWorking != nullptr; }
    // Past this many bytes, a text is looked through on a worker.
    static constexpr size_t ON_A_WORKER = 256 * 1024;
    // Nothing found, and nothing to look through again: the find put away.
    void clear();
    // An edit of the text: the matches after it slide, those it cut
    // through go, and a selection kept to grows or shrinks with what is
    // done within it.
    void edited(const ALTextDocument::Edit& edit);
    // To be looked through again once the text has stopped changing for a
    // moment; and whether that moment has come.
    void stale();
    bool isStale() const { return mStale; }
    bool due() const;

    const std::vector<ALTextRange>& matches() const { return mMatches.items(); }
    size_t                          count() const { return mMatches.size(); }
    S32                             current() const { return mCurrent; }
    void                            setCurrent(S32 index) { mCurrent = index; }
    const std::string&              error() const { return mError; }
    // Whether the matches stopped at as many as a find lists (LIMIT),
    // with more in the text.
    bool                            capped() const { return mMatches.size() >= LIMIT; }
    static constexpr size_t         LIMIT = 10000;
    // The match nearest a place, after it or before it, round the ends;
    // -1 with none.
    S32 nearest(const ALTextPos& from, bool forward) const;
    // Every match, for a replace of every one: those listed, or where the
    // list stopped short, all there are, looked through again without end.
    std::vector<ALTextRange> all(const ALTextDocument& doc, const std::string& query, const ALTextSearchOptions& options) const;
    // The matches taken out, before a replace of every one; and put back,
    // where the replace did nothing.
    std::vector<ALTextRange> take();
    void                     restore(std::vector<ALTextRange> matches, S32 current);
    // Moved on whenever the matches change, found or slid.
    U32                      generation() const { return mGeneration; }

private:
    ALAnchoredRanges<ALTextRange> mMatches;
    S32                           mCurrent = -1;
    bool                          mStale   = false;
    LLFrameTimer                  mSettle;
    bool                          mInSelection = false;
    ALTextRange                   mScope;
    std::string                   mError;
    U32                           mGeneration = 0;
    // The worker's search on its way: what it was asked, and what it
    // found, which it hands over through the lock.
    struct Working;
    std::shared_ptr<Working>      mWorking;
    void                          take(const ALTextDocument& doc, std::vector<ALTextRange> found, const std::string& error, const ALTextRange& selection);
};
