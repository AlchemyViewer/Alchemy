/**
 * @file alrecoverykeeper.h
 * @brief One window's unsaved text kept in the account's recovery store, as Script Studio keeps its tabs'.
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

#include "lluuid.h"

#include <functional>
#include <optional>
#include <string>

// One window's unsaved text kept against the viewer going before it was
// saved -- the notecard window's -- in the
// account's store (ALRecovery::store), as Script Studio keeps its tabs'
// (ALScriptStudioRecovery): written a moment after it changes, whatever is
// typed meanwhile; forgotten once it is saved or changed back; set aside a
// while when it is thrown away. What an earlier session left of the same
// item is the window's to offer as it opens, and one it takes up is let go
// of once the window's own is written.
class ALRecoveryKeeper
{
public:
    typedef ALRecoveryEntry Entry;

    // How long after the first change since it was last written the text
    // is written again: as the studio writes its tabs.
    static constexpr F64 DELAY = 1.5;

    // What it asks of the window.
    struct Holder
    {
        // The window's text as an entry, and what goes with it; the key
        // and the state are filled in here.
        std::function<Entry()> entry;
        // Whether there is anything to keep: loaded, changeable, and
        // changed since it was saved.
        std::function<bool()> unsaved;
        // A write that could not be made, said once until the text is
        // saved.
        std::function<void()> failed;
    };

    explicit ALRecoveryKeeper(Holder holder);

    // The item the window holds, whose entry it keeps under the key of
    // the viewer's own windows (ALRecoveryStore::windowKeyOf); another
    // where a save made the item another. A null item keeps nothing.
    void               setItem(const LLUUID& object, const LLUUID& item);
    const std::string& key() const { return mKey; }

    // Each frame, or as often as the window likes: `version` is anything
    // that moves as the text does. An unsaved text written a moment after
    // it first moved; nothing unsaved, this session's entry forgotten.
    void pump(U64 version, F64 now);
    // Saved, or the item deleted: nothing of this session's to keep.
    void forget();
    // Thrown away -- Don't Save -- and set aside a while all the same.
    // False where it could not be.
    bool setAside();
    // What sessions that ended left of the item, whole, to offer as it
    // opens.
    std::optional<Entry> left() const;
    // One of those taken up into the text, which now stands at `version`:
    // the window's own written at once, and then the one taken up let go
    // of. Or turned down: moved among the discarded.
    void took(const Entry& taken, U64 version);
    void turnDown(const Entry& offered);

private:
    Entry entryNow(Entry::State state) const;

    Holder             mHolder;
    std::string        mKey;
    // The version last seen, and last written; when the next write is due.
    std::optional<U64> mSeen;
    std::optional<U64> mWritten;
    F64                mDue    = 0.0;
    bool               mFailed = false;
};
