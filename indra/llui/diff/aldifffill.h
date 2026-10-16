/**
 * @file aldifffill.h
 * @brief A comparison's editor told of a column of it, or of what changed.
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

#ifndef AL_ALDIFFFILL_H
#define AL_ALDIFFFILL_H

#include "aldiffmodel.h"

class ALCodeEditor;

// What an editor of a comparison (ALDiffView) is told of the column of its
// model it shows: the column's text, and what is said of each of its lines
// -- its number where the text is not its own, its tint, its mark on the
// ruler, its sign, the rows of nothing above it -- and of the rows below
// the last, and the words that changed. The folds and the notes are the
// view's, applied after.
namespace ALDiffFill
{
    // All of it; the text put in only where it is another, so that a side
    // as it was keeps its place, but none of its lines hidden.
    void whole(ALCodeEditor& side, const ALDiffModel& model, ALDiffModel::Column column);
    // Only what the model's last rebuild laid out again (relaid()), the
    // editor holding the column as the layout before it had it: the text
    // of the lines there where it is another, a stretch put in in place of
    // a stretch, and what is said of them, their words and the numbers of
    // the lines after moved along -- a script of tens of thousands of lines
    // is not put in again, nor each of its lines said again, a keystroke at
    // a time. Those said again are from the line before the stretch at
    // most to the line after it, or below the last. False where the
    // rebuild laid out all of it, or the editor has not the lines the
    // layout before had: whole() then.
    bool again(ALCodeEditor& side, const ALDiffModel& model, ALDiffModel::Column column);
}

#endif // AL_ALDIFFFILL_H
