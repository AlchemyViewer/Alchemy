/**
 * @file alinventorysync.h
 * @brief How the viewer keeps its inventory in step with the server's.
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

#include "llerror.h"
#include "llsd.h"
#include "lluuid.h"

#include <boost/unordered/unordered_flat_map.hpp>

#include <deque>
#include <optional>
#include <vector>

// How the viewer keeps its inventory in step with the server's: rules over
// plain values, and over any folder type with LLViewerInventoryCategory's
// accessors, which the inventory model, AIS3's updates and the background
// fetch apply.
class ALInventorySync
{
public:
    // A folder's version where the viewer doesn't know it, as
    // LLViewerInventoryCategory::VERSION_UNKNOWN.
    static constexpr S32 VERSION_UNKNOWN = -1;

    // What a fetch's reply lists of one folder.
    enum class Listing
    {
        Whole,     // every folder, item and link the folder holds
        LinksOnly, // its links alone, as a fetch of an outfit's links has them
    };

    // What an AIS3 reply's `_embedded` lists of the folder it is under: the
    // whole folder where it has categories, links and items; the links of
    // a folder of outfit links (the current outfit or an outfit) where a
    // fetch has them alone; otherwise nothing of any kind in full.
    static std::optional<Listing> embeddedListing(const LLSD& embedded, bool outfit_folder, bool fetch);

    // What an AIS3 reply's `_embedded` names of the kinds `listing` covers.
    static uuid_set_t embeddedIds(const LLSD& embedded, Listing listing);

    // One of a folder's children, as the model holds it.
    struct Child
    {
        LLUUID id;
        bool   link = false;
    };

    // The children of a folder held at `held_version` that a reply's
    // listing of it at `listed_version` leaves out, which the folder no
    // longer holds: each one `listed` doesn't name, or under a listing of
    // links alone each such link. None where the listing's version is
    // unknown or older than the folder held, which a change the viewer
    // made since may have outrun.
    static uuid_vec_t unlisted(const std::vector<Child>& children, Listing listing, const uuid_set_t& listed,
                               S32 listed_version, S32 held_version);

    // The changes made to what folders hold, in all: an object made in a
    // folder, moved into or out of it, or removed from it, by the viewer
    // or by a reply. The inventory model notes each one.
    class ContentChanges
    {
    public:
        // How far the changes have come, which a request notes as it goes.
        U64 stamp() const { return mCount; }

        // Notes that what `folder_id` holds changed.
        void note(const LLUUID& folder_id);

        // Whether what `folder_id` holds changed after `stamp`.
        bool changedSince(const LLUUID& folder_id, U64 stamp) const;

        // Forgets a folder that has gone.
        void forget(const LLUUID& folder_id);

        // Forgets every folder, as the model empties.
        void clear();

    private:
        U64                                    mCount = 0;
        boost::unordered_flat_map<LLUUID, U64> mLastChange;
    };

    // A request whose reply may list folders whole. The reply is written
    // before any change made after the request went, so it doesn't list
    // what such a change put in a folder, and the version it gives such a
    // folder is not the folder's as the viewer holds it, which a change
    // the viewer made need not have raised.
    class Request
    {
    public:
        // As the request goes.
        explicit Request(const ContentChanges& changes);

        // As the reply comes, before any of it is applied, since applying
        // it changes the folders it lists too: spares each of
        // `listed_folders` whose contents changed after the request went.
        void spareChanged(const ContentChanges& changes, const uuid_vec_t& listed_folders);

        // Whether the reply leaves `folder_id` holding whatever it doesn't
        // list, rather than removing it as unlisted() would.
        bool spares(const LLUUID& folder_id) const;

        // Once the reply is applied: has each folder it spared fetched
        // again, by forgetting the version the reply gave it, as
        // markOutOfStep does. `find(folder_id)` is a pointer to the folder,
        // or null where it has gone.
        template <typename FindFolder>
        void forgetSpared(FindFolder&& find) const;

    private:
        U64        mSent = 0;
        uuid_set_t mSpared;
    };

    // The folders an inventory's cache keeps, from its root down through
    // the folders of the root's owner, in the order met: each complete
    // folder, the root among them only where it is complete, whose record
    // and items the cache holds. An incomplete folder is kept out, and the
    // complete ones below it kept. `is_complete(folder)` says whether a
    // folder is; `for_each_child(folder, visit)` calls `visit(child)` for
    // each folder directly under one.
    template <typename Folder, typename ForEachChild, typename IsComplete>
    static std::vector<Folder*> cachedFolders(Folder* root, ForEachChild&& for_each_child, IsComplete&& is_complete);

    // The owner of a folder an AIS3 reply makes: the one the reply names,
    // or where it names none, its parent's, as `parent_owner(parent_id)`
    // finds it, else `fallback`.
    template <typename ParentOwner>
    static LLUUID newFolderOwner(const LLSD& category_map, ParentOwner&& parent_owner, const LLUUID& fallback);

    // Whether a folder of `owner` is the library's, whose owner the login
    // names as `library_owner`. A login that names none has no library.
    static bool isLibraryFolder(const LLUUID& owner, const LLUUID& library_owner);

    // Has a folder found out of step with the server's fetched when next
    // asked for, by forgetting its version. One whose version is already
    // unknown is asked for now, as `fetch()` asks for it.
    template <typename Folder>
    static void markOutOfStep(Folder& folder);
};

template <typename Folder>
void ALInventorySync::markOutOfStep(Folder& folder)
{
    if (folder.getVersion() == VERSION_UNKNOWN)
    {
        folder.fetch();
    }
    else
    {
        folder.setVersion(VERSION_UNKNOWN);
    }
}

template <typename FindFolder>
void ALInventorySync::Request::forgetSpared(FindFolder&& find) const
{
    for (const LLUUID& folder_id : mSpared)
    {
        if (auto* folder = find(folder_id))
        {
            markOutOfStep(*folder);
        }
    }
}

template <typename ParentOwner>
LLUUID ALInventorySync::newFolderOwner(const LLSD& category_map, ParentOwner&& parent_owner, const LLUUID& fallback)
{
    if (category_map.has("agent_id"))
    {
        return category_map["agent_id"].asUUID();
    }
    if (const std::optional<LLUUID> owner = parent_owner(category_map["parent_id"].asUUID()))
    {
        return *owner;
    }
    return fallback;
}

template <typename Folder, typename ForEachChild, typename IsComplete>
std::vector<Folder*> ALInventorySync::cachedFolders(Folder* root, ForEachChild&& for_each_child, IsComplete&& is_complete)
{
    std::vector<Folder*> kept;
    if (!root)
    {
        return kept;
    }
    if (is_complete(root))
    {
        kept.push_back(root);
    }

    uuid_set_t met = { root->getUUID() };
    std::deque<Folder*> walk = { root };
    while (!walk.empty())
    {
        Folder* folder = walk.front();
        walk.pop_front();
        for_each_child(folder, [&](Folder* child)
        {
            if (!child)
            {
                return;
            }
            // The library and the agent's inventory are cached apart.
            if (child->getOwnerID() != root->getOwnerID())
            {
                LL_WARNS("Inventory") << "Owner mismatch in category tree: expected " << root->getOwnerID() << " got "
                                      << child->getOwnerID() << " for category " << child->getUUID() << LL_ENDL;
                return;
            }
            if (!met.insert(child->getUUID()).second)
            {
                return;
            }
            if (is_complete(child))
            {
                kept.push_back(child);
            }
            walk.push_back(child);
        });
    }
    return kept;
}
