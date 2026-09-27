/**
 * @file alscriptsearch.cpp
 * @brief What a search across Script Studio's scripts found, and what Replace All would make of it.
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

#include "llviewerprecompiledheaders.h"

#include "alscriptsearch.h"

#include <algorithm>

void ALScriptSearch::begin(const std::string& query, const ALTextSearchOptions& options)
{
    ++mGeneration;
    mQuery      = query;
    mOptions    = options;
    mPending    = 0;
    mHits       = 0;
    mFiles      = 0;
    mBadPattern = false;
    mPatternError.clear();
    mFound.clear();
    mTyped.clear();
    mDue = 0.0;
    mObject.setNull();
    mRoots.clear();
}

bool ALScriptSearch::answered(U32 generation)
{
    if (generation != mGeneration)
    {
        return false;
    }
    --mPending;
    return true;
}

ALScriptSearch::Kept ALScriptSearch::search(const ALScriptRef& ref, const std::string& name, const std::string& where, const ALTextDocument& text,
                                            U32 version, const std::string& doc_id, bool keep_text, bool notecard, const std::string& file)
{
    std::string                    error;
    const std::vector<ALTextRange> matches = ALTextSearch::matches(text, mQuery, mOptions, nullptr, &error);
    if (!error.empty())
    {
        mBadPattern   = true;
        mPatternError = error;
        return Kept::Nothing;
    }
    // The places kept, with the text they were found in, for a replace,
    // and each one's line as the row lists it.
    Found found;
    found.ref      = ref;
    found.doc      = doc_id;
    found.name     = name;
    found.where    = where.empty() ? name : where + ": " + name;
    found.version  = version;
    found.places   = matches;
    found.notecard = notecard;
    found.file     = file;
    if (keep_text && !matches.empty())
    {
        found.text = text.text();
    }
    for (const ALTextRange& match : matches)
    {
        const std::string& line  = text.line(match.begin.line);
        const size_t       first = line.find_first_not_of(" \t");
        const size_t       last  = line.find_last_not_of(" \t\r");
        found.lines.push_back(first == std::string::npos ? std::string() : line.substr(first, last - first + 1));
        found.at.push_back(first == std::string::npos ? -1 : match.begin.column - static_cast<S32>(first));
    }
    // A script searched again as it stands: in its place among the rest.
    for (Found& one : mFound)
    {
        if ((!doc_id.empty() && one.doc == doc_id) || (doc_id.empty() && !ref.isNull() && one.ref == ref) || (!file.empty() && one.file == file))
        {
            one = std::move(found);
            return Kept::Again;
        }
    }
    if (matches.empty())
    {
        return Kept::Nothing;
    }
    ++mFiles;
    mHits += static_cast<S32>(matches.size());
    mFound.push_back(std::move(found));
    return Kept::Added;
}

void ALScriptSearch::dropEmpty()
{
    mFound.erase(std::remove_if(mFound.begin(), mFound.end(), [](const Found& one) { return one.places.empty(); }), mFound.end());
}

void ALScriptSearch::recount()
{
    mHits  = 0;
    mFiles = static_cast<S32>(mFound.size());
    for (const Found& one : mFound)
    {
        mHits += static_cast<S32>(one.places.size());
    }
}

bool ALScriptSearch::isOver(const LLUUID& root) const
{
    return root.notNull() && std::find(mRoots.begin(), mRoots.end(), root) != mRoots.end();
}

void ALScriptSearch::typedIn(const std::string& id, F64 now)
{
    if (std::find(mTyped.begin(), mTyped.end(), id) == mTyped.end())
    {
        mTyped.push_back(id);
    }
    constexpr F64 SEARCH_AGAIN = 0.6;
    mDue                       = now + SEARCH_AGAIN;
}

bool ALScriptSearch::due(F64 now)
{
    if (mDue <= 0.0 || now < mDue)
    {
        return false;
    }
    if (mPending > 0)
    {
        // Still fetching the rest: once they are in.
        mDue = now + 0.5;
        return false;
    }
    mDue = 0.0;
    return true;
}

std::vector<std::string> ALScriptSearch::takeTyped()
{
    std::vector<std::string> typed = std::move(mTyped);
    mTyped.clear();
    return typed;
}

void ALScriptSearch::rekey(const std::string& from, const std::string& to)
{
    for (Found& one : mFound)
    {
        if (one.doc == from)
        {
            one.doc = to;
        }
    }
    for (std::string& typed : mTyped)
    {
        if (typed == from)
        {
            typed = to;
        }
    }
}

// static
ALScriptSearch::Step ALScriptSearch::step(const Found& one, const Now& now)
{
    if (one.places.empty() || one.notecard)
    {
        return Step::Skip;
    }
    if (!one.file.empty())
    {
        // Included: every script's that includes it, left to be changed
        // by hand.
        return Step::Leave;
    }
    switch (now.at)
    {
        case Now::At::Elsewhere:
            // Changed there, as a tab here would be, where it reads as it
            // did when it was searched.
            if (now.notecard)
            {
                return Step::Skip;
            }
            return now.loaded && now.modifiable && !one.text.empty() && now.text && now.text->text() == one.text ? Step::Replace : Step::Leave;
        case Now::At::Here:
        {
            if (now.notecard)
            {
                return Step::Skip;
            }
            if (!now.modifiable)
            {
                // May be read and not changed: said among what was left.
                return Step::Leave;
            }
            // Searched as it stood in its tab, by the version it was at;
            // searched as the region had it and opened since, by its text.
            const bool same = now.loaded && now.text && (one.version != 0 ? now.text->version() == one.version : now.text->text() == one.text);
            return same ? Step::Replace : Step::Leave;
        }
        case Now::At::Closed:
        default:
            // A file on disk, closed since it was searched, has no item to
            // open it by; a script needs the text it was searched in.
            return one.ref.isNull() || one.text.empty() ? Step::Leave : Step::Open;
    }
}

std::vector<std::pair<ALTextRange, std::string>> ALScriptSearch::replacements(const Found& one, const ALTextDocument& text, const std::string& with) const
{
    std::vector<std::pair<ALTextRange, std::string>> edits;
    for (const ALTextRange& place : one.places)
    {
        edits.emplace_back(place, ALTextSearch::replacement(text, place, mQuery, mOptions, with));
    }
    return edits;
}

std::vector<ALScriptStudioDoc::PendingEdit> ALScriptSearch::pendingEdits(const Found& one, const std::string& with) const
{
    // Worked out over the text it was searched in, and made once it has
    // loaded where each place still reads as it did.
    const ALTextDocument                        searched(one.text);
    std::vector<ALScriptStudioDoc::PendingEdit> edits;
    for (const ALTextRange& place : one.places)
    {
        ALScriptStudioDoc::PendingEdit edit;
        edit.span.line      = place.begin.line;
        edit.span.column    = place.begin.column;
        edit.span.endLine   = place.end.line;
        edit.span.endColumn = place.end.column;
        edit.was            = searched.text(place);
        edit.now            = ALTextSearch::replacement(searched, place, mQuery, mOptions, with);
        edit.replace        = true;
        edits.push_back(std::move(edit));
    }
    return edits;
}
