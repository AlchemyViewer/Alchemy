/**
 * @file alrecovery.h
 * @brief The account's store of unsaved texts, where what each editor kept goes back to, and what is offered back at login.
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
    // (ALRecoveryStore::isWindowKey) goes back to: the notecard window, as
    // the viewer has it -- false where
    // it cannot have the item, and the text is taken up in the studio as
    // any other is. None where none is given, as in a test.
    typedef std::function<bool(const Entry& entry)> window_t;
    static void takeWindowsTo(window_t window);
    // An entry one of the viewer's own windows kept, taken up there: false
    // where it is not such a window's, or the window cannot have its item.
    static bool toOwnWindow(const Entry& entry);
    // What an entry is called in a list: its name, or its file's.
    static std::string nameOf(const Entry& entry);

    // What the legacy editors' backups left in a folder -- the temp
    // folder, where they wrote a changed script (.lslbackup) or notecard
    // (.ncbackup) every minute -- taken into the store as a session of
    // their own that ended, to be offered at login as any other, and the
    // files gone once written. They name no item, only a name, so each is
    // taken up in a tab of its own. How many were taken.
    static S32 importLegacyBackups(const std::string& folder);

    // At login: what a session that ended before saving left, offered, to
    // take up now, later, or not at all -- each where it was kept: a
    // window of the viewer's own, or Script Studio, which `studio` opens
    // and takes up what it is given in (none, to open it alone). What was
    // kept on purpose at a quit opens with the studio, and where the studio
    // is not open already, is offered to open it now.
    typedef std::function<void(const std::vector<Entry>& entries)> studio_t;
    static void offer(studio_t studio, bool studio_open);
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
