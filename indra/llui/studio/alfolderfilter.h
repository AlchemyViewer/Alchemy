/**
 * @file alfolderfilter.h
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

#pragma once

#include "llfolderviewmodel.h"
#include "lltimer.h"

#include <string>

// The filter every folder tree of ours has: XUI Studio's outline, the scene
// explorer, Script Studio's explorer. The words typed, lowercased, and where
// they are in a row's label, for the folder view to light; one generation
// for every change, the tree being small enough that each change starts over;
// and a budget for a pass, which a large tree is refiltered within. What
// passes is the subclass's: check(), and isActive() for whether anything is
// being asked beyond the words. A filter with a finer generation scheme --
// the scene explorer's -- overrides the generations and setModified.
class ALFolderFilter : public LLFolderViewFilter
{
public:
    explicit ALFolderFilter(std::string name) : mName(std::move(name)) {}

    // The words, lowercased; a change is a new generation.
    void               setWords(const std::string& text);
    const std::string& words() const { return mWords; }

    // Where lowercased words found `at` in a lowercased copy of `label` are
    // in the label itself: lowercasing moves no byte of plain ASCII, and of
    // anything else the span is found again in the words as they stand.
    static Match spanIn(const std::string& label, std::string::size_type at, const std::string& words);

    // The words in the row's label, lit.
    Match getFilterMatch(LLFolderViewModelItem* item) const override;

    bool checkFolder(const LLFolderViewModelItem* folder) const override { return true; }
    void setEmptyLookupMessage(const std::string& message) override { mEmptyLookupMessage = message; }
    std::string getEmptyLookupMessage(bool is_empty_folder = false) const override { return mEmptyLookupMessage; }
    bool showAllResults() const override { return false; }

    bool isActive() const override { return !mWords.empty(); }
    bool isModified() const override { return mModified; }
    void clearModified() override { mModified = false; }
    const std::string& getName() const override { return mName; }
    const std::string& getFilterText() override { return mWords; }
    void setModified(EFilterModified behavior = FILTER_RESTART) override;

    // A pass's budget, which a large tree is refiltered a slice at a time
    // within, the rest resumed on the next idle. A real-time LLTimer: an
    // LLFrameTimer's clock only moves once a frame, so it could never run
    // out inside one pass.
    void resetTime(S32 timeout) override
    {
        mFilterTime.reset();
        mFilterTime.setTimerExpirySec((F32)timeout / 1000.f);
    }
    bool isTimedOut() override { return mFilterTime.hasExpired(); }

    bool isDefault() const override { return !isActive(); }
    bool isNotDefault() const override { return isActive(); }
    void markDefault() override {}
    void resetDefault() override {}

    S32 getCurrentGeneration() const override { return mGeneration; }
    S32 getFirstSuccessGeneration() const override { return mGeneration; }
    S32 getFirstRequiredGeneration() const override { return mGeneration; }

protected:
    bool mModified = false;

private:
    std::string mName;
    std::string mEmptyLookupMessage;
    std::string mWords;
    LLTimer     mFilterTime;
    S32         mGeneration = 1;
};

// A row of one of those trees, filtered the way every folder view is: a row
// that failed a filter at least as strict fails again without asking; the
// rows under it first, while the pass has time, each that passes lighting
// the rows above it; then the row itself, and the words lit in it. False
// where the pass ran out of time, to go on from here the next.
class ALFilteredItem : public LLFolderViewModelItemCommon
{
public:
    explicit ALFilteredItem(LLFolderViewModelInterface& root_view_model) : LLFolderViewModelItemCommon(root_view_model) {}

    bool filter(LLFolderViewFilter& filter) override;
};
