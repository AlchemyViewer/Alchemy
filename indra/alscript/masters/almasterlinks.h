/**
 * @file almasterlinks.h
 * @brief Which file on disk is the master of which script in the world, and the index of them.
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

#include "lldate.h"
#include "llsd.h"
#include "llstl.h"
#include "lluuid.h"
#include "stdtypes.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A file on disk the master of a script in the world: the script is the
// file's product, its envelope's source half the file's text, and saving
// the file uploads it. One link for each item, kept in the scripter's own
// account, so that an alt never uploads into another account's objects. A
// hint in the script -- an @file comment, an upload header -- only proposes
// a link; a link is made by the scripter, and is this record.
struct ALMasterLink
{
    // How the link was made: a file the scripter picked, the one the
    // script's own @file hint named, or one found by the item's name. Each
    // took a click; the last two are shown as such, since there the script
    // chose the file, not the scripter.
    enum class Made : U8
    {
        Picked,
        Hint,
        Name
    };
    // Active: a save of the master uploads it. Differing: the world was
    // changed from elsewhere since `base`. Pending: a send that did not go
    // -- held, out of reach, or not sent when asked -- waits to be sent by
    // hand. Suspended: the master moved or went, and nothing is sent until
    // it is found again or the link undone. Orphaned: the item left the
    // world, taken or taken a copy of, and the record is kept a while for
    // linking again when it is rezzed.
    enum class State : U8
    {
        Active,
        Differing,
        Pending,
        Suspended,
        Orphaned
    };

    // The object the item is in, null for an item of the agent's own
    // inventory; and the item. Together the link's key.
    LLUUID      object;
    LLUUID      item;
    // The master's path on disk, where it stands once its links are
    // followed: as ALDiskIncludes names what it admits.
    std::string master;
    Made        made = Made::Picked;
    bool        lua  = false;
    // A notecard's link rather than a script's: its text goes up as the
    // file has it, nothing expanded or compiled. A notecard that carries
    // items is never linked, since a file cannot hold them.
    bool        notecard = false;
    // The compile target last sent with: "mono", "lsl2" or "luau".
    std::string target;
    // The item's asset after the last upload made through the link, or as
    // it was when linked: the world moved where it holds another.
    LLUUID      base;
    // What last went up, as ALUploadHeader::hashOf hashes it.
    std::string hash;
    // The master's modification stamp at the last upload, as the disk
    // gives it.
    S64         stamp = 0;
    // The identities of the files on disk the last expansion read
    // (`disk:<path>`, ALIncludeIdentity::ofFile), its nested includes
    // among them: whose change sends the script again.
    std::vector<std::string> uses;
    // Whether the last expansion had a problem -- an include not found, say:
    // a file saved since may be the one it wanted.
    bool                     missed = false;
    // What the object, the item and the region were called, for showing
    // the link and for finding the item again after a copy or a take.
    std::string objectName;
    std::string itemName;
    std::string regionName;
    LLDate      linked;
    State       state = State::Active;
    // When it became Orphaned, which prune() counts from.
    LLDate      orphanedSince;
};

// Every link of an account, with what finds them fast among thousands: by
// the item, by the master file, and by each file the last expansions read.
// Kept on one thread: a lookup by a path may rebuild what it looks in, so
// not even two readers at once. A pointer or a reference into it is good
// until the next put(), remove() or prune().
class ALMasterLinks
{
public:
    // The form the LLSD is written in; read back from any, a record at a
    // time.
    static constexpr S32 VERSION = 1;

    // The link of an item, or none.
    const ALMasterLink* of(const LLUUID& object, const LLUUID& item) const;
    // The same, to change. Any field may be changed through it but the
    // object and the item, which are the link's key: an item linked again
    // is removed and put.
    ALMasterLink* find(const LLUUID& object, const LLUUID& item);

    // Every link a master file masters -- one file may master several
    // items -- and every link whose last expansion read a file: its
    // identity (`disk:<path>`) as `uses` holds it, or its path alone. Each
    // in the order the links were put; a path compared as paths are
    // (keyOf).
    std::vector<const ALMasterLink*> mastering(const std::string& master) const;
    std::vector<const ALMasterLink*> usersOf(const std::string& include) const;
    // The links a change to an include may send again: its users, and every
    // link whose last expansion missed something, which the include may be.
    // Each once, in the order put, and none suspended or orphaned, whose
    // master is not there to send.
    std::vector<const ALMasterLink*> affectedBy(const std::string& include) const;
    // The orphaned links of an item of this name, for a copy rezzed of what
    // was taken to be linked again: those of an object of the name given
    // first, then the latest orphaned first. An item with no name has none.
    std::vector<const ALMasterLink*> orphansNamed(const std::string& item_name, const std::string& object_name) const;

    // A file whose save sends something: a master, or an include of one.
    struct Watched
    {
        std::string path;
        bool        master = false;
    };
    // The files to watch: the master of every link that is live or
    // suspended -- a suspended link's, so that its file coming back is
    // heard -- and the files on disk the last expansions of the live ones
    // read. Each once, compared as paths are (keyOf); a file that is both a
    // master and an include is a master. In the order put, each link's
    // master before its uses.
    std::vector<Watched> watched() const;

    // A link put in, or put over the one the item already had.
    ALMasterLink& put(ALMasterLink link);
    // The item's link gone; false where it had none.
    bool remove(const LLUUID& object, const LLUUID& item);
    // The orphaned links orphaned more than `days` ago gone, and how many.
    // One orphaned with no date is given `now`, and so kept that long.
    size_t prune(const LLDate& now, F64 days = 90.0);

    const std::vector<ALMasterLink>& all() const { return mLinks; }
    size_t                           size() const { return mLinks.size(); }
    bool                             empty() const { return mLinks.empty(); }

    // The links as LLSD, `{"version": 1, "links": [...]}`, and back. Read
    // back tolerantly: a record with no item, or no master, or an object
    // that is not a key, is passed over, and the rest read; a field it does
    // not have, or cannot be read, is left as a new link has it; a way of
    // making or a state not known -- one a later version wrote -- is read
    // as the most careful there is, a hint's link, suspended.
    LLSD                 toLLSD() const;
    static ALMasterLinks fromLLSD(const LLSD& llsd);

    // A path in the form it is compared in. One written Windows's way -- a
    // drive, a share, from a root of `\` -- is the same in any case and
    // with either separator, as Windows finds it; any other is compared as
    // written, as ALDiskIncludes compares the paths of files once their
    // links are followed, which carry the case the disk keeps them in.
    static std::string keyOf(std::string_view path);
    // The same for a file's identity, `disk:<path>`: its path's form. Any
    // other identity as it is.
    static std::string useKeyOf(std::string_view identity);

private:
    using ItemKey = std::pair<LLUUID, LLUUID>;
    using ByPath  = boost::unordered_flat_map<std::string, std::vector<size_t>, ll::string_hash, std::equal_to<>>;

    void reindexItems();
    void reindexPaths() const;
    std::vector<const ALMasterLink*> linksAt(const ByPath& index, const std::string& key) const;

    std::vector<ALMasterLink>                       mLinks;
    boost::unordered_flat_map<ItemKey, size_t>      mByItem;
    // By the master's key and by each use's: rebuilt at the first lookup
    // after anything may have changed them, find() among those.
    mutable ByPath                                  mByMaster;
    mutable ByPath                                  mByUse;
    mutable bool                                    mPathsStale = false;
};
