/**
 * @file almasterplan.h
 * @brief What a send of a master file's script does, given the world's and the file's state.
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

#include "lluuid.h"
#include "stdtypes.h"

#include <vector>

// Whether a linked script is sent, and how, as a small table: the kind of
// send, against whether the world changed since the link's base, whether
// the world already holds what would go up, and whether that is what went
// up last.
//
// A save of the master is the scripter asking for an upload, so it always
// uploads: where the world was changed from elsewhere to something else,
// the world's text is kept in history first, and said. A send the studio
// makes of its own accord -- an include's users sent again, Link all, Send
// from Files -- never overwrites a change made in the world: it is held
// for the scripter to look at. A world changed to just what we would send
// -- a recompile, the VS Code plugin sending the same file -- is no
// conflict at all: the base moves to it, and a derived send has nothing
// to do. Nor has a derived send of what went up last, while the scripter
// leaves skipping on.
struct ALMasterPlan
{
    // Direct: a save of the master itself, in the studio or outside it.
    // Derived: the studio's own -- an include's users, Link all, Send from
    // Files.
    enum class Send : U8
    {
        Direct,
        Derived
    };
    // Send: upload it. Skip: upload nothing, and say so; the base moves to
    // the world's where that moved. SendKeepingTheirs: keep the world's
    // text in history, then upload. Hold: upload nothing, and offer the
    // scripter the choice.
    enum class Do : U8
    {
        Send,
        Skip,
        SendKeepingTheirs,
        Hold
    };

    // world_moved: the world's asset is not the link's base. world_same: the
    // world's text, its hash worked out again from its halves, is what would
    // go up. unchanged: what would go up hashes as the link's last hash.
    // skip_unchanged: the scripter's setting, on unless they turn it off.
    static Do decide(Send kind, bool world_moved, bool world_same, bool unchanged, bool skip_unchanged);

    // Whether the scripter is asked once before an include's users go up
    // again: where more than `ask_over` scripts would change
    // (ALScriptMastersAskOver), or they are in two objects or more, the
    // agent's own inventory counted as one. Only the scripts that would
    // change are counted, those whose text going up is what went up last
    // left out; with none, there is nothing to ask.
    static bool askFirst(size_t changing, size_t objects, size_t ask_over);
    // How many objects the scripts are in, by the object of each: the null
    // key, the agent's inventory, counted once like any other.
    static size_t objectsOf(const std::vector<LLUUID>& objects);
};
