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

void ALTextFind::search(const ALTextDocument& doc, const std::string& query, const ALTextSearchOptions& options, bool in_selection,
                        const ALTextRange& selection)
{
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
    mMatches.assign(ALTextSearch::matches(doc, query, options, mInSelection ? &mScope : nullptr, &mError));
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

void ALTextFind::clear()
{
    mMatches.clear();
    mCurrent = -1;
    mStale   = false;
}

void ALTextFind::edited(const ALTextDocument::Edit& edit)
{
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
    mStale = true;
    mSettle.reset();
}

bool ALTextFind::due() const
{
    constexpr F32 SETTLE = 0.2f;
    return mStale && mSettle.getElapsedTimeF32() >= SETTLE;
}

S32 ALTextFind::nearest(const ALTextPos& from, bool forward) const
{
    return ALTextSearch::nearest(mMatches.items(), from, forward);
}

std::vector<ALTextRange> ALTextFind::take()
{
    std::vector<ALTextRange> out = mMatches.take();
    mMatches.clear();
    mCurrent = -1;
    return out;
}

void ALTextFind::restore(std::vector<ALTextRange> matches, S32 current)
{
    mMatches.assign(std::move(matches));
    mCurrent = current;
}
