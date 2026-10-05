/**
 * @file alfolderfilter.cpp
 * @brief What our folder trees share of filtering: the words, where they match a row, and the walk.
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

#include "alfolderfilter.h"

#include "llstring.h"

void ALFolderFilter::setWords(const std::string& text)
{
    std::string lower = utf8str_tolower(text);
    if (lower == mWords)
    {
        return;
    }
    mWords = std::move(lower);
    setModified();
}

void ALFolderFilter::setModified(EFilterModified behavior)
{
    // Every change starts over.
    ++mGeneration;
    mModified = true;
}

// static
LLFolderViewFilter::Match ALFolderFilter::spanIn(const std::string& label, std::string::size_type at, const std::string& words)
{
    Match match;
    if (at == std::string::npos || words.empty())
    {
        return match;
    }
    if (utf8str_is_ascii(label) && utf8str_is_ascii(words))
    {
        match.mOffset = at;
        match.mLength = words.size();
        return match;
    }
    match.mOffset    = utf8str_bytes_from_cased_bytes(label, at, false);
    const size_t end = utf8str_bytes_from_cased_bytes(label, at + words.size(), false);
    match.mLength    = end > match.mOffset ? end - match.mOffset : 0;
    return match;
}

LLFolderViewFilter::Match ALFolderFilter::getFilterMatch(LLFolderViewModelItem* item) const
{
    if (mWords.empty() || !item)
    {
        return Match();
    }
    const std::string& label = item->getDisplayName();
    return spanIn(label, utf8str_tolower(label).find(mWords), mWords);
}

bool ALFilteredItem::filter(LLFolderViewFilter& filter)
{
    const S32 generation = filter.getCurrentGeneration();
    const S32 required   = filter.getFirstRequiredGeneration();

    if (getLastFilterGeneration() >= required && getLastFolderFilterGeneration() >= required && !passedFilter(required))
    {
        // Already failed a filter at least as strict as this one.
        setPassedFilter(false, generation);
        setPassedFolderFilter(false, generation);
        return true;
    }

    setPassedFolderFilter(filter.checkFolder(this), generation);

    bool going = true;
    if (!mChildren.empty() && (getLastFilterGeneration() < required || descendantsPassedFilter(required)))
    {
        for (auto& childp : mChildren)
        {
            ALFilteredItem* child = static_cast<ALFilteredItem*>(childp.get());
            if (child->getLastFilterGeneration() < generation)
            {
                // False where the pass has spent its time: stop here, and go
                // on from this row the next.
                going = child->filter(filter);
            }
            if (child->passedFilter())
            {
                for (ALFilteredItem* up = this; up && up->mMostFilteredDescendantGeneration < generation;
                     up = static_cast<ALFilteredItem*>(up->mParent))
                {
                    up->mMostFilteredDescendantGeneration = generation;
                }
            }
            if (!going)
            {
                return false;
            }
        }
    }

    // The words lit where they are, for the folder view to draw.
    const LLFolderViewFilter::Match match = filter.getFilterMatch(this);
    setPassedFilter(filter.check(this), generation, match.mOffset, match.mLength);
    return !filter.isTimedOut();
}
