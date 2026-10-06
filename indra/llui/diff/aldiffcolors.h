/**
 * @file aldiffcolors.h
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

#ifndef AL_ALDIFFCOLORS_H
#define AL_ALDIFFCOLORS_H

#include "lluicolor.h"

// The colours a comparison is drawn in, by the names the colour table
// holds them under, each with the one it is where the skin names none:
// one place, so that a comparison (ALDiffView), the peek at a change from
// the editor (ALChangePeek) and the editor's own marks of what changed
// agree.
namespace ALDiffColors
{
    enum class Name : U8
    {
        // Lines taken out and put in; the words within them that changed;
        // their marks on the ruler.
        Removed,
        Added,
        RemovedWord,
        AddedWord,
        RemovedMark,
        AddedMark,
        // A block moved, at either end, and its mark.
        Moved,
        MovedMark,
        // The rows of nothing beside the other side's lines; a folded run's
        // row; the line between the sides.
        Padding,
        Fold,
        Divider,
        // The range the caret is in, and a change in a merge's conflict.
        Linked,
        Conflict
    };

    // As the table has it now, and after: a skin changed changes it.
    LLUIColor get(Name name);
}

#endif // AL_ALDIFFCOLORS_H
