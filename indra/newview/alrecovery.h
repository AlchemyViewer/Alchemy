/**
 * @file alrecovery.h
 * @brief The account's store of unsaved texts, where what each editor kept goes back to, and what is offered back at login.
 *
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

#include "alrecoverystore.h"

#include <functional>
#include <string>
#include <vector>

class ALScriptStudioRecovery;

// The account's store of unsaved texts (ALRecoveryStore), which the
// viewer's editors keep what they hold unsaved in against the viewer going
// before it was saved -- a crash, a lost connection, a quit: Script
// Studio's tabs (ALScriptStudioRecovery), the notecard window, the legacy
// script editors. Where an entry goes back to when it is taken up, and
// what is offered back at login, are the same for all of them.
class ALRecovery
{
public:
    typedef ALRecoveryEntry Entry;

    // One for the account logged in, under its own folder, made again where
    // another has logged in since; null before one has. Every entry this
    // process writes is under one session, which is how the next session
    // tells what this one left. A test's in its place, where one is given.
    static ALRecoveryStore* store();
    static void             useStore(ALRecoveryStore* store);
    // Where a text one of the viewer's own windows kept
    // (ALRecoveryStore::isWindowKey) goes back to: the notecard
    // window, a legacy script editor, as the viewer has them -- false where
    // it cannot have the item, and the text is taken up in the studio as
    // any other is. None where none is given, as in a test.
    typedef std::function<bool(const Entry& entry)> window_t;
    static void takeWindowsTo(window_t window);
    // An entry one of the viewer's own windows kept, taken up there: false
    // where it is not such a window's, or the window cannot have its item.
    static bool toOwnWindow(const Entry& entry);
    // What an entry is called in a list: its name, or its file's.
    static std::string nameOf(const Entry& entry);

    // At login: what a session that ended before saving left, offered, to
    // take up now, later, or not at all; `studio` opens the studio to take
    // it up in. What was kept on purpose at a quit opens with the studio,
    // and where the studio is not open already, is offered to open it now.
    static void offer(std::function<ALScriptStudioRecovery*()> studio, bool studio_open);
    // What the login offers: what sessions that ended left unsaved, and
    // what was kept on purpose, where the studio is not open to have
    // opened it.
    struct Offers
    {
        std::vector<Entry> unsaved;
        std::vector<Entry> kept;
    };
    static Offers offersAt(const ALRecoveryStore& store, bool studio_open);
};
