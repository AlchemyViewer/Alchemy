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

#include "llsd.h"
#include "lluuid.h"

#include <optional>
#include <vector>

// How the viewer keeps its inventory in step with the server's: what a
// fetch's reply lists of a folder, and what that leaves out. Rules over
// plain values, which AIS3's updates and the background fetch apply to the
// inventory model.
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
};
