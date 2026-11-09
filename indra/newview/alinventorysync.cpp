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
uuid_vec_t ALInventorySync::unlisted(const std::vector<Child>& children, Listing listing, const uuid_set_t& listed,
                                     S32 listed_version, S32 held_version)
{
    uuid_vec_t left_out;
    if (listed_version == VERSION_UNKNOWN || (held_version != VERSION_UNKNOWN && listed_version < held_version))
    {
        return left_out;
    }
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
bool ALInventorySync::isLibraryFolder(const LLUUID& owner, const LLUUID& library_owner)
{
    return library_owner.notNull() && owner == library_owner;
}
