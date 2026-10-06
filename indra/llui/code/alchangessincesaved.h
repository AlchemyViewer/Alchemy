/**
 * @file alchangessincesaved.h
 * @brief A text's changes since it was saved, kept while neither it nor its history moves.
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


#ifndef AL_ALCHANGESSINCESAVED_H
#define AL_ALCHANGESSINCESAVED_H

#include "stdtypes.h"

#include <memory>
#include <string>
#include <vector>

class ALTextDocument;
class ALTextUndo;

// A text's changes since it was saved, as a merge reads them
// (ALTextMerge::changesOf), with the lines of the saved text and of the
// text: worked out when asked, and kept while neither the text nor its
// history moves. ]c and [c pressed again and again, and the peek shown at
// each change stepped to, ask the same of the same text -- the saved text
// worked back from the undo journal and the two compared whole, each time.
class ALChangesSinceSaved
{
public:
    // A stretch changed: its first line now and how many, and the saved
    // lines it stands for.
    struct Change
    {
        S32 now        = 0;
        S32 nowCount   = 0;
        S32 saved      = 0;
        S32 savedCount = 0;

        bool operator==(const Change& other) const = default;
    };
    // What is known of a text as it stood: the saved text's lines, the
    // text's, and the changes between them, in order; and the text's
    // version (ALTextDocument::version) it is of.
    struct Known
    {
        std::vector<std::string> saved;
        std::vector<std::string> now;
        std::vector<Change>      changes;
        U32                      version = 0;
    };

    static std::vector<Change> between(const std::vector<std::string>& saved, const std::vector<std::string>& now);
    // As a text and its journal stand, worked out again only where either
    // has moved since it last was; none where the journal knows no saved
    // text. Held by whoever asked for as long as they like.
    std::shared_ptr<const Known> of(const ALTextDocument& document, const ALTextUndo& journal);

private:
    std::shared_ptr<const Known> mKnown;
    U32                          mVersion  = 0;
    U32                          mRevision = 0;
    bool                         mAsked    = false;
};

#endif // AL_ALCHANGESSINCESAVED_H
