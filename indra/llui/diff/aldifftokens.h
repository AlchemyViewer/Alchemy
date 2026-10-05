/**
 * @file aldifftokens.h
 * @brief A line cut into the words two lines are compared by.
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

#ifndef AL_ALDIFFTOKENS_H
#define AL_ALDIFFTOKENS_H

#include "altextdiff.h"

#include <string_view>

// A line cut into the words a comparison weighs and marks: a run of word
// bytes (identifiers and numbers), a run of blanks, each other byte.
namespace ALDiffTokens
{
    bool blank(char c);
    // Its words, each as [begin, end) in it, covering it.
    ALTextDiff::spans_t words(std::string_view line);
}

#endif // AL_ALDIFFTOKENS_H
