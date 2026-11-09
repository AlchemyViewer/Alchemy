/**
 * @file alinventorysync_test.cpp
 * @brief Tests of how the viewer keeps its inventory in step with the server's.
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

#include "linden_common.h"

#include "../alinventorysync.h"

#include "indra_constants.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>

namespace
{
    using Sync = ALInventorySync;

    const LLUUID FOLDER("10000000-0000-4000-8000-000000000001");
    const LLUUID ITEM("10000000-0000-4000-8000-000000000002");
    const LLUUID LINK("10000000-0000-4000-8000-000000000003");
    const LLUUID OTHER_FOLDER("10000000-0000-4000-8000-000000000004");
    const LLUUID OTHER_ITEM("10000000-0000-4000-8000-000000000005");
    const LLUUID OTHER_LINK("10000000-0000-4000-8000-000000000006");

    // A folder holding a folder, an item and a link of each pair.
    const std::vector<Sync::Child> CHILDREN = {
        { FOLDER, false }, { ITEM, false }, { LINK, true },
        { OTHER_FOLDER, false }, { OTHER_ITEM, false }, { OTHER_LINK, true },
    };

    // An AIS3 `_embedded` map of these ids, as a reply keys its folders,
    // items or links.
    LLSD keyed(std::initializer_list<LLUUID> ids)
    {
        LLSD map = LLSD::emptyMap();
        for (const LLUUID& id : ids)
        {
            map[id.asString()] = LLSD::emptyMap();
        }
        return map;
    }

    TEST_CASE("alinventorysync: a whole listing leaves out each child it doesn't name", "[alinventorysync]")
    {
        const uuid_set_t listed = { FOLDER, ITEM, LINK };
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, listed, 7, 6)
              == uuid_vec_t{ OTHER_FOLDER, OTHER_ITEM, OTHER_LINK });
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, {}, 7, 6)
              == uuid_vec_t{ FOLDER, ITEM, LINK, OTHER_FOLDER, OTHER_ITEM, OTHER_LINK });
    }

    TEST_CASE("alinventorysync: a listing of links alone leaves out only the links it doesn't name", "[alinventorysync]")
    {
        const uuid_set_t listed = { LINK };
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::LinksOnly, listed, 7, 6) == uuid_vec_t{ OTHER_LINK });
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::LinksOnly, {}, 7, 6) == uuid_vec_t{ LINK, OTHER_LINK });
    }

    TEST_CASE("alinventorysync: a listing as new as the folder held or newer leaves out what it doesn't name", "[alinventorysync]")
    {
        const uuid_set_t listed = { FOLDER, ITEM, LINK, OTHER_FOLDER, OTHER_ITEM };
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, listed, 6, 6) == uuid_vec_t{ OTHER_LINK });
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, listed, 9, 6) == uuid_vec_t{ OTHER_LINK });
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, listed, 1, Sync::VERSION_UNKNOWN) == uuid_vec_t{ OTHER_LINK });
    }

    TEST_CASE("alinventorysync: a listing older than the folder held or of no version leaves nothing out", "[alinventorysync]")
    {
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, {}, 5, 6).empty());
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::LinksOnly, {}, 5, 6).empty());
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, {}, Sync::VERSION_UNKNOWN, 6).empty());
        CHECK(Sync::unlisted(CHILDREN, Sync::Listing::Whole, {}, Sync::VERSION_UNKNOWN, Sync::VERSION_UNKNOWN).empty());
    }

    TEST_CASE("alinventorysync: an AIS3 reply lists the whole folder where its embedded has every kind", "[alinventorysync]")
    {
        LLSD embedded;
        embedded["categories"] = keyed({});
        embedded["links"] = keyed({});
        embedded["items"] = keyed({});
        for (const bool outfit : { false, true })
        {
            for (const bool fetch : { false, true })
            {
                INFO("outfit folder " << outfit << " fetch " << fetch);
                CHECK(Sync::embeddedListing(embedded, outfit, fetch) == Sync::Listing::Whole);
            }
        }
    }

    TEST_CASE("alinventorysync: an AIS3 fetch lists an outfit folder by its links alone", "[alinventorysync]")
    {
        LLSD links;
        links["links"] = keyed({ LINK });
        CHECK(Sync::embeddedListing(links, true, true) == Sync::Listing::LinksOnly);
        // Not a fetch, or not a folder of outfit links: the links are not
        // all it holds.
        CHECK_FALSE(Sync::embeddedListing(links, true, false).has_value());
        CHECK_FALSE(Sync::embeddedListing(links, false, true).has_value());
    }

    TEST_CASE("alinventorysync: an AIS3 reply missing a kind lists nothing in full", "[alinventorysync]")
    {
        LLSD categories;
        categories["categories"] = keyed({ FOLDER });
        CHECK_FALSE(Sync::embeddedListing(categories, false, true).has_value());
        CHECK_FALSE(Sync::embeddedListing(categories, true, true).has_value());

        LLSD no_links;
        no_links["categories"] = keyed({ FOLDER });
        no_links["items"] = keyed({ ITEM });
        CHECK_FALSE(Sync::embeddedListing(no_links, false, true).has_value());

        CHECK_FALSE(Sync::embeddedListing(LLSD(), true, true).has_value());
    }

    TEST_CASE("alinventorysync: an AIS3 reply names the keys of the kinds its listing covers", "[alinventorysync]")
    {
        LLSD embedded;
        embedded["categories"] = keyed({ FOLDER });
        embedded["links"] = keyed({ LINK, OTHER_LINK });
        embedded["items"] = keyed({ ITEM });
        CHECK(Sync::embeddedIds(embedded, Sync::Listing::Whole) == uuid_set_t{ FOLDER, ITEM, LINK, OTHER_LINK });
        CHECK(Sync::embeddedIds(embedded, Sync::Listing::LinksOnly) == uuid_set_t{ LINK, OTHER_LINK });
    }

    const LLUUID AGENT("20000000-0000-4000-8000-000000000001");
    const LLUUID LIBRARY("20000000-0000-4000-8000-000000000002");

    // A folder as the cache's walk reads one.
    struct CacheFolder
    {
        LLUUID id;
        LLUUID owner;
        bool complete = true;
        std::vector<CacheFolder*> children = {};

        const LLUUID& getUUID() const { return id; }
        const LLUUID& getOwnerID() const { return owner; }
    };

    std::vector<CacheFolder*> cached(CacheFolder& root)
    {
        return Sync::cachedFolders(
            &root,
            [](CacheFolder* folder, const auto& visit)
            {
                for (CacheFolder* child : folder->children)
                {
                    visit(child);
                }
            },
            [](CacheFolder* folder) { return folder->complete; });
    }

    TEST_CASE("alinventorysync: the cache keeps each complete folder from a complete root down", "[alinventorysync]")
    {
        CacheFolder leaf{ LINK, AGENT };
        CacheFolder child{ ITEM, AGENT, true, { &leaf } };
        CacheFolder root{ FOLDER, AGENT, true, { &child } };
        CHECK(cached(root) == std::vector<CacheFolder*>{ &root, &child, &leaf });
    }

    TEST_CASE("alinventorysync: the cache leaves out a root whose count disagrees and keeps what is complete below it", "[alinventorysync]")
    {
        CacheFolder below_incomplete{ OTHER_ITEM, AGENT };
        CacheFolder incomplete{ OTHER_FOLDER, AGENT, false, { &below_incomplete } };
        CacheFolder complete{ ITEM, AGENT };
        CacheFolder root{ FOLDER, AGENT, false, { &complete, &incomplete } };
        CHECK(cached(root) == std::vector<CacheFolder*>{ &complete, &below_incomplete });

        CacheFolder lone_root{ FOLDER, AGENT, false };
        CHECK(cached(lone_root).empty());
    }

    TEST_CASE("alinventorysync: the cache keeps no folder of another owner nor any folder below one", "[alinventorysync]")
    {
        CacheFolder below{ OTHER_ITEM, AGENT };
        CacheFolder library{ OTHER_FOLDER, LIBRARY, true, { &below } };
        CacheFolder root{ FOLDER, AGENT, true, { &library } };
        CHECK(cached(root) == std::vector<CacheFolder*>{ &root });
    }

    TEST_CASE("alinventorysync: the cache keeps a folder met twice once", "[alinventorysync]")
    {
        CacheFolder shared{ LINK, AGENT };
        CacheFolder first{ ITEM, AGENT, true, { &shared } };
        CacheFolder second{ OTHER_ITEM, AGENT, true, { &shared } };
        CacheFolder root{ FOLDER, AGENT, true, { &first, &second, &root } };
        CHECK(cached(root) == std::vector<CacheFolder*>{ &root, &first, &second, &shared });
    }

    // A folder as an AIS3 reply has it, under `parent`, with `agent_id`
    // where it names an owner.
    LLSD replyFolder(const LLUUID& parent, const std::optional<LLUUID>& owner = std::nullopt)
    {
        LLSD map;
        map["category_id"] = OTHER_FOLDER;
        map["parent_id"] = parent;
        if (owner)
        {
            map["agent_id"] = *owner;
        }
        return map;
    }

    // The owner of each folder the viewer holds: the library's root, FOLDER.
    std::optional<LLUUID> heldOwner(const LLUUID& folder_id)
    {
        return folder_id == FOLDER ? std::optional<LLUUID>(LIBRARY) : std::nullopt;
    }

    TEST_CASE("alinventorysync: a folder an AIS3 reply names no owner of is its parent's", "[alinventorysync]")
    {
        CHECK(Sync::newFolderOwner(replyFolder(FOLDER), heldOwner, AGENT) == LIBRARY);
    }

    TEST_CASE("alinventorysync: a folder an AIS3 reply names no owner of under a folder the viewer doesn't hold is the fallback's", "[alinventorysync]")
    {
        CHECK(Sync::newFolderOwner(replyFolder(ITEM), heldOwner, AGENT) == AGENT);
    }

    TEST_CASE("alinventorysync: a folder an AIS3 reply names the owner of is that owner's", "[alinventorysync]")
    {
        CHECK(Sync::newFolderOwner(replyFolder(FOLDER, AGENT), heldOwner, LIBRARY) == AGENT);
        CHECK(Sync::newFolderOwner(replyFolder(ITEM, LIBRARY), heldOwner, AGENT) == LIBRARY);
    }

    TEST_CASE("alinventorysync: a folder is the library's where its owner is the one the login names", "[alinventorysync]")
    {
        // A grid whose library is not Linden Lab's library account's.
        CHECK(Sync::isLibraryFolder(LIBRARY, LIBRARY));
        CHECK_FALSE(Sync::isLibraryFolder(AGENT, LIBRARY));
        CHECK_FALSE(Sync::isLibraryFolder(ALEXANDRIA_LINDEN_ID, LIBRARY));

        // Linden Lab's grids, whose logins name that account.
        CHECK(Sync::isLibraryFolder(ALEXANDRIA_LINDEN_ID, ALEXANDRIA_LINDEN_ID));
        CHECK_FALSE(Sync::isLibraryFolder(AGENT, ALEXANDRIA_LINDEN_ID));
    }

    TEST_CASE("alinventorysync: a login that names no library owner has no library folder", "[alinventorysync]")
    {
        CHECK_FALSE(Sync::isLibraryFolder(LLUUID::null, LLUUID::null));
        CHECK_FALSE(Sync::isLibraryFolder(AGENT, LLUUID::null));
    }

    // A folder as one found out of step is: its version, and the fetches
    // asked for of it, which LLViewerInventoryCategory::fetch sends only for
    // a folder of unknown version.
    struct StepFolder
    {
        S32 version = Sync::VERSION_UNKNOWN;
        S32 fetches = 0;

        S32 getVersion() const { return version; }
        void setVersion(S32 to) { version = to; }
        bool fetch()
        {
            if (version != Sync::VERSION_UNKNOWN)
            {
                return false;
            }
            ++fetches;
            return true;
        }
    };

    TEST_CASE("alinventorysync: a folder out of step forgets its version and is fetched when next asked for", "[alinventorysync]")
    {
        StepFolder folder{ 7 };
        Sync::markOutOfStep(folder);
        CHECK(folder.version == Sync::VERSION_UNKNOWN);
        // Nothing is asked for yet.
        CHECK(folder.fetches == 0);

        CHECK(folder.fetch());
        CHECK(folder.fetches == 1);
    }

    TEST_CASE("alinventorysync: a folder out of step of unknown version is asked for now", "[alinventorysync]")
    {
        StepFolder folder;
        Sync::markOutOfStep(folder);
        CHECK(folder.version == Sync::VERSION_UNKNOWN);
        CHECK(folder.fetches == 1);
    }
}
