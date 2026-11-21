/**
 * @file alinventorysync.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alinventorysync.h"

namespace
{
    void addKeys(const LLSD& map, uuid_set_t& ids)
    {
        for (LLSD::map_const_iterator it = map.beginMap(), end = map.endMap(); it != end; ++it)
        {
            ids.insert(LLUUID(it->first));
        }
    }
}

// static
std::optional<ALInventorySync::Listing> ALInventorySync::embeddedListing(const LLSD& embedded, bool outfit_folder,
                                                                         bool fetch)
{
    if (embedded.has("categories") && embedded.has("links") && embedded.has("items"))
    {
        return Listing::Whole;
    }
    if (fetch && outfit_folder && embedded.has("links"))
    {
        return Listing::LinksOnly;
    }
    return std::nullopt;
}

// static
uuid_set_t ALInventorySync::embeddedIds(const LLSD& embedded, Listing listing)
{
    uuid_set_t ids;
    addKeys(embedded["links"], ids);
    if (listing == Listing::Whole)
    {
        addKeys(embedded["categories"], ids);
        addKeys(embedded["items"], ids);
    }
    return ids;
}

// static
uuid_vec_t ALInventorySync::unnamed(const std::vector<Child>& children, Listing listing, const uuid_set_t& listed)
{
    uuid_vec_t left_out;
    for (const Child& child : children)
    {
        if ((listing == Listing::Whole || child.link) && !listed.contains(child.id))
        {
            left_out.push_back(child.id);
        }
    }
    return left_out;
}

// static
uuid_vec_t ALInventorySync::unlisted(const std::vector<Child>& children, Listing listing, const uuid_set_t& listed,
                                     S32 listed_version, S32 held_version)
{
    if (listed_version == VERSION_UNKNOWN || (held_version != VERSION_UNKNOWN && listed_version < held_version))
    {
        return {};
    }
    return unnamed(children, listing, listed);
}

// static
ALInventorySync::FolderListing ALInventorySync::readListing(const LLUUID& folder_id, const std::vector<Child>& children,
                                                            Listing listing, const uuid_set_t& listed,
                                                            S32 listed_version, S32 held_version)
{
    FolderListing read;
    read.folder_id = folder_id;
    read.unlisted  = unlisted(children, listing, listed, listed_version, held_version);
    read.unnamed   = !unnamed(children, listing, listed).empty();
    return read;
}

void ALInventorySync::ContentChanges::note(const LLUUID& folder_id, F64 now)
{
    mLastChange[folder_id] = { ++mCount, now };
}

bool ALInventorySync::ContentChanges::changedSince(const LLUUID& folder_id, U64 stamp, F64 since) const
{
    const auto found = mLastChange.find(folder_id);
    return found != mLastChange.end() && (found->second.count > stamp || found->second.at > since);
}

void ALInventorySync::ContentChanges::forget(const LLUUID& folder_id)
{
    mLastChange.erase(folder_id);
}

void ALInventorySync::ContentChanges::clear()
{
    mLastChange.clear();
}

ALInventorySync::Request::Request(const ContentChanges& changes, F64 now)
: mSent(changes.stamp())
, mSentAt(now)
{
}

void ALInventorySync::Request::spareChanged(const ContentChanges& changes, const uuid_vec_t& listed_folders)
{
    for (const LLUUID& folder_id : listed_folders)
    {
        if (changes.changedSince(folder_id, mSent, mSentAt - GRACE_SECONDS))
        {
            mSpared.insert(folder_id);
        }
    }
}

bool ALInventorySync::Request::spares(const LLUUID& folder_id) const
{
    return mSpared.contains(folder_id);
}

ALInventorySync::Request::Applied ALInventorySync::Request::apply(const FolderListing& listing) const
{
    Applied applied;
    if (spares(listing.folder_id))
    {
        applied.out_of_step = listing.unnamed;
    }
    else
    {
        applied.removed = listing.unlisted;
    }
    return applied;
}

// static
bool ALInventorySync::isLibraryOwner(const LLUUID& owner, const LLUUID& library_owner)
{
    return library_owner.notNull() && owner == library_owner;
}
