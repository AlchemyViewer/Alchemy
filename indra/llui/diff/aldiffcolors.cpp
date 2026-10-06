/**
 * @file aldiffcolors.cpp
 * @brief The colours a comparison is drawn in, wherever it is drawn.
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

#include "aldiffcolors.h"

#include "lluicolortable.h"

#include <iterator>

namespace
{
    struct Entry
    {
        const char* name;
        LLColor4    otherwise;
    };
    // In the order of ALDiffColors::Name.
    const Entry ENTRIES[] = {
        { "CodeDiffRemovedColor", LLColor4(0.85f, 0.25f, 0.25f, 0.18f) },
        { "CodeDiffAddedColor", LLColor4(0.25f, 0.75f, 0.35f, 0.18f) },
        { "CodeDiffRemovedWordColor", LLColor4(0.9f, 0.25f, 0.25f, 0.4f) },
        { "CodeDiffAddedWordColor", LLColor4(0.25f, 0.85f, 0.35f, 0.4f) },
        { "CodeDiffRemovedMarkColor", LLColor4(0.9f, 0.3f, 0.3f, 0.85f) },
        { "CodeDiffAddedMarkColor", LLColor4(0.3f, 0.8f, 0.4f, 0.85f) },
        { "CodeDiffMovedColor", LLColor4(0.45f, 0.45f, 0.95f, 0.18f) },
        { "CodeDiffMovedMarkColor", LLColor4(0.5f, 0.5f, 1.f, 0.85f) },
        { "CodeDiffPaddingColor", LLColor4(0.5f, 0.5f, 0.5f, 0.07f) },
        { "CodeDiffFoldColor", LLColor4(0.5f, 0.5f, 0.5f, 0.14f) },
        { "CodeDiffDividerColor", LLColor4(0.5f, 0.5f, 0.5f, 0.5f) },
        { "CodeDiffLinkedColor", LLColor4(0.45f, 0.65f, 1.f, 1.f) },
        { "CodeDiffConflictColor", LLColor4(1.f, 0.6f, 0.1f, 1.f) },
    };
    static_assert(std::size(ENTRIES) == static_cast<size_t>(ALDiffColors::Name::Conflict) + 1, "a colour for every name");
}

LLUIColor ALDiffColors::get(Name name)
{
    const Entry& entry = ENTRIES[static_cast<size_t>(name)];
    return LLUIColorTable::instance().getColor(entry.name, entry.otherwise);
}
