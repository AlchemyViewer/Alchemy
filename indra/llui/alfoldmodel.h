/**
 * @file alfoldmodel.h
 * @brief The blocks of a code editor's text that fold, and which of them are folded.
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

#include "altextdocument.h"

#include <optional>
#include <utility>
#include <vector>

// The blocks of a code editor's text that fold, and which are folded. A
// block is a line and the deeper lines after it, by indentation alone --
// any language, and a line of nothing going with whichever side keeps the
// block whole -- with the closer on the line after it, a brace or an `end`,
// taken as part of it, and a brace on a line of its own folding with the
// header above it. The blocks are found again when the text changes; the
// folds slide with its edits, and one whose block is gone goes.
//
// Worked out over a document alone -- one document's, whose version says
// when its blocks are found again -- the editor hides the lines a fold
// takes from its layout, and moves the caret out of what is folded away.
class ALFoldModel
{
public:
    // A block: the line it starts on stays in sight, the lines through
    // `end` go when it is folded.
    struct Region
    {
        S32 start = 0;
        S32 end   = 0;
    };

    // Every block in the text, by start line, found again where the
    // document has changed since, or `invalidate` said so. Tabs are as
    // wide as `tab_width` says.
    const std::vector<Region>& regions(const ALTextDocument& doc, S32 tab_width);
    void                       invalidate() { mValid = false; }
    // The block that starts at a line; and the innermost one around it --
    // of those that hold it, the one that starts last.
    const Region* startingAt(const ALTextDocument& doc, S32 tab_width, S32 line);
    const Region* around(const ALTextDocument& doc, S32 tab_width, S32 line);

    // Whether a folded block starts at the line; and the start lines of
    // those that are, in order.
    bool                    isFolded(S32 line) const;
    const std::vector<S32>& folded() const { return mFolded; }

    // The block that starts at the line, else the innermost one around it,
    // folded: the block, or nothing where there is none, or it is folded.
    std::optional<Region> fold(const ALTextDocument& doc, S32 tab_width, S32 line);
    // The folded block that starts at the line, else the innermost folded
    // one around it, opened; false where there is none.
    bool unfold(const ALTextDocument& doc, S32 tab_width, S32 line);
    void foldAll(const ALTextDocument& doc, S32 tab_width);
    void unfoldAll() { mFolded.clear(); }
    // Every folded block a line is inside opened, and every fold whose
    // block is gone; false where there was none of either.
    bool reveal(const ALTextDocument& doc, S32 tab_width, S32 line);
    // The lines hidden, each folded block's after its first; a fold whose
    // block is gone let go of first.
    std::vector<std::pair<S32, S32>> hidden(const ALTextDocument& doc, S32 tab_width);

    // An edit of the text, over lines `first` through `last` of it as it
    // was, which the edit made into `made` lines. A fold that starts on
    // the edit's first line stays: typing on a block's first line is not
    // opening the block. One on its last line stays too where the edit
    // ended at that line's start and left it a line of its own -- whole
    // lines taken from above a folded block -- and moves with it; the rest
    // inside go, and those after move along. The blocks are found again.
    // A batch's runs of lines each so, one after another.
    void edited(const ALTextDocument::Edit& edit);

private:
    std::vector<Region> mRegions;
    U32                 mVersion  = 0;
    // Tabs are measured by it where a line mixes them with spaces.
    S32                 mTabWidth = 0;
    bool                mValid    = false;
    std::vector<S32>    mFolded;
};
