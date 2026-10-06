/**
 * @file alchangessincesaved.cpp
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


#include "linden_common.h"

#include "alchangessincesaved.h"

#include "altextdiff.h"
#include "altextdocument.h"
#include "altextmerge.h"
#include "altextundo.h"

// static
std::vector<ALChangesSinceSaved::Change> ALChangesSinceSaved::between(const std::vector<std::string>& saved, const std::vector<std::string>& now)
{
    std::vector<Change> out;
    for (const ALTextMerge::Change& c : ALTextMerge::changesOf(saved, now, ALTextDiff::Options()))
    {
        out.push_back(Change{ c.at, c.atEnd - c.at, c.base, c.baseEnd - c.base });
    }
    return out;
}

std::shared_ptr<const ALChangesSinceSaved::Known> ALChangesSinceSaved::of(const ALTextDocument& document, const ALTextUndo& journal)
{
    if (mAsked && mVersion == document.version() && mRevision == journal.revision())
    {
        return mKnown;
    }
    mAsked    = true;
    mVersion  = document.version();
    mRevision = journal.revision();
    mKnown.reset();
    if (const std::optional<std::string> saved = journal.savedText())
    {
        auto known     = std::make_shared<Known>();
        known->saved   = ALTextDiff::split(*saved);
        known->now     = ALTextDiff::split(document.wholeText());
        known->changes = between(known->saved, known->now);
        known->version = mVersion;
        mKnown         = std::move(known);
    }
    return mKnown;
}
