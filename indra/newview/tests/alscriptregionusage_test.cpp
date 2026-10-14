/**
 * @file alscriptregionusage_test.cpp
 * @brief Tests for ALScriptRegionUsage: answers read, objects asked once a minute.
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

#include "../alscriptregionusage.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>

namespace
{
    struct alscriptregionusage_data
    {
        typedef ALScriptRegionUsage       Usage;
        typedef Usage::World::Kind        Kind;
        typedef std::function<void(const LLSD&)> Told;

        // The world: what each object is; the askings not answered yet,
        // the attachments' and each object's on land.
        std::map<LLUUID, Kind>          kinds;
        std::vector<Told>               attachmentAsks;
        std::vector<std::pair<LLUUID, Told>> landAsks;
        // Time: whether the agent manages the estate, each object's owner,
        // and the askings of Top Scripts not answered yet.
        bool                                 estateManager = false;
        std::map<LLUUID, std::string>        owners;
        std::vector<std::pair<std::string, std::function<void(const Usage::times_t&)>>> timeAsks;
        Usage                           usage;
        S32                             heard = 0;
        boost::signals2::scoped_connection heardConnection;

        alscriptregionusage_data() : usage(fake())
        {
            heardConnection = usage.onHeard([this]() { ++heard; });
        }

        Usage::World fake()
        {
            Usage::World out;
            out.kindOf = [this](const LLUUID& root) {
                const auto found = kinds.find(root);
                return found != kinds.end() ? found->second : Kind::None;
            };
            out.askAttachments = [this](Told told) { attachmentAsks.push_back(std::move(told)); };
            out.askLand        = [this](const LLUUID& root, Told told) { landAsks.emplace_back(root, std::move(told)); };
            out.mayAskTime     = [this]() { return estateManager; };
            out.ownerOf        = [this](const LLUUID& root) {
                const auto found = owners.find(root);
                return found != owners.end() ? found->second : std::string();
            };
            out.askTime = [this](const std::string& owner, std::function<void(const Usage::times_t&)> told) {
                timeAsks.emplace_back(owner, std::move(told));
            };
            return out;
        }

        static LLUUID id(U32 n)
        {
            LLUUID out;
            out.mData[0]  = static_cast<U8>(n);
            out.mData[15] = 2;
            return out;
        }
        // An object as an answer lists it.
        static LLSD object(const LLUUID& id, S64 memory, S32 urls)
        {
            LLSD out;
            out["id"]                  = id;
            out["name"]                = "Thing";
            out["resources"]["memory"] = static_cast<LLSD::Real>(memory);
            out["resources"]["urls"]   = urls;
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptregionusage_data, "ALScriptRegionUsage: readAttachments and readDetails read each listed object's memory and URLs", "[ALScriptRegionUsage]")
    {
        LLSD attachments;
        attachments["attachments"][0]["location"] = "Chest";
        attachments["attachments"][0]["objects"].append(object(id(1), 65536, 1));
        attachments["attachments"][1]["location"] = "HUD Center";
        attachments["attachments"][1]["objects"].append(object(id(2), 131072, 0));
        attachments["summary"]["used"][0]["type"]   = "memory";
        attachments["summary"]["used"][0]["amount"] = 196608;
        const LLDate          when(1.8e9);
        Usage::usages_t       read;
        Usage::readAttachments(attachments, when, read);
        REQUIRE(read.size() == 2U);
        {
            INFO("the chest's");
            CHECK(read[id(1)].memory == 65536);
            CHECK(read[id(1)].urls == 1);
            CHECK(read[id(1)].when == when);
        }
        {
            INFO("the HUD's");
            CHECK(read[id(2)].memory == 131072);
            CHECK(read[id(2)].urls == 0);
        }

        LLSD details;
        details["parcels"][0]["name"]     = "Home";
        details["parcels"][0]["local_id"] = 12;
        details["parcels"][0]["objects"].append(object(id(3), 16384, 0));
        details["parcels"][0]["objects"].append(object(LLUUID::null, 1, 1));
        read.clear();
        Usage::readDetails(details, when, read);
        {
            INFO("a parcel's, one without an id passed over");
            REQUIRE(read.size() == 1);
            CHECK(read[id(3)].memory == 16384);
        }
        read.clear();
        Usage::readDetails(LLSD(), when, read);
        Usage::readAttachments(LLSD("nonsense"), when, read);
        UNSCOPED_INFO("nothing from nothing");
        REQUIRE(read.empty());
    }

    TEST_CASE_METHOD(alscriptregionusage_data, "ALScriptRegionUsage: ask asks once for all attachments and once per object on land", "[ALScriptRegionUsage]")
    {
        kinds[id(1)] = Kind::Attachment;
        kinds[id(2)] = Kind::Attachment;
        kinds[id(3)] = Kind::Land;
        kinds[id(4)] = Kind::Land;
        usage.ask({ id(1), id(2), id(3), id(4), id(5) }, 100.0);
        UNSCOPED_INFO("one asking for the attachments");
        REQUIRE(attachmentAsks.size() == 1U);
        {
            INFO("each on land");
            REQUIRE(landAsks.size() == 2);
            CHECK(landAsks[0].first == id(3));
            CHECK(landAsks[1].first == id(4));
        }
        {
            INFO("nothing known yet");
            REQUIRE_FALSE(usage.usageOf(id(1)));
            CHECK(heard == 0);
        }

        LLSD answer;
        answer["attachments"][0]["objects"].append(object(id(1), 65536, 2));
        answer["attachments"][0]["objects"].append(object(id(2), 16384, 0));
        attachmentAsks[0](answer);
        CHECK(heard == 1);
        REQUIRE(usage.usageOf(id(1)));
        REQUIRE(usage.usageOf(id(1))->urls == 2);
        REQUIRE(usage.usageOf(id(2)));
        CHECK(usage.usageOf(id(2))->memory == 16384);
        LLSD parcel;
        parcel["parcels"][0]["objects"].append(object(id(3), 131072, 0));
        parcel["parcels"][0]["objects"].append(object(id(4), 65536, 0));
        landAsks[0].second(parcel);
        {
            INFO("a parcel's answer is every object on it");
            CHECK(heard == 2);
            REQUIRE(usage.usageOf(id(4)));
            CHECK(usage.usageOf(id(4))->memory == 65536);
        }
    }

    TEST_CASE_METHOD(alscriptregionusage_data, "ALScriptRegionUsage: an object is asked about at most once a minute counted from the answer", "[ALScriptRegionUsage]")
    {
        kinds[id(3)] = Kind::Land;
        kinds[id(4)] = Kind::Land;
        usage.ask({ id(3) }, 100.0);
        REQUIRE(landAsks.size() == 1U);
        usage.ask({ id(3) }, 130.0);
        UNSCOPED_INFO("not again within the minute");
        REQUIRE(landAsks.size() == 1U);
        LLSD parcel;
        parcel["parcels"][0]["objects"].append(object(id(3), 1, 0));
        parcel["parcels"][0]["objects"].append(object(id(4), 2, 0));
        landAsks[0].second(parcel);
        usage.ask({ id(4) }, 140.0);
        UNSCOPED_INFO("one the answer named is answered already");
        REQUIRE(landAsks.size() == 1U);
        usage.ask({ id(3), id(4) }, 161.0);
        UNSCOPED_INFO("a minute from the answer, not the asking");
        REQUIRE(landAsks.size() == 1U);
        usage.ask({ id(3), id(4) }, 191.0);
        UNSCOPED_INFO("after the minute, each again");
        REQUIRE(landAsks.size() == 3U);

        kinds[id(1)] = Kind::Attachment;
        kinds[id(2)] = Kind::Attachment;
        usage.ask({ id(1) }, 200.0);
        usage.ask({ id(2) }, 210.0);
        UNSCOPED_INFO("the attachments once a minute, whichever asks");
        REQUIRE(attachmentAsks.size() == 1U);

        // An answer after its asker is gone goes nowhere, and passes by
        // returning; one to an asker still here is heard.
        Told late = landAsks.back().second;
        std::optional<Usage> other;
        other.emplace(fake());
        other->ask({ id(3) }, 300.0);
        UNSCOPED_INFO("the other asks for itself");
        REQUIRE(landAsks.size() == 4U);
        Told orphaned = landAsks.back().second;
        other.reset();
        const S32 before = heard;
        orphaned(parcel);
        late(parcel);
        UNSCOPED_INFO("the one still here heard");
        CHECK(heard == before + 1);
    }

    TEST_CASE_METHOD(alscriptregionusage_data, "ALScriptRegionUsage: an estate manager asks each owner's script time once a minute", "[ALScriptRegionUsage]")
    {
        kinds[id(1)] = Kind::Land;
        kinds[id(2)] = Kind::Land;
        kinds[id(3)] = Kind::Attachment;
        kinds[id(4)] = Kind::Land;
        owners[id(1)] = "Ann Resident";
        owners[id(2)] = "Ann Resident";
        owners[id(3)] = "Bo Smith";
        usage.ask({ id(1), id(2), id(3), id(4) }, 100.0);
        UNSCOPED_INFO("not an estate manager: not asked");
        REQUIRE(timeAsks.empty());

        estateManager = true;
        attachmentAsks.clear();
        landAsks.clear();
        usage.ask({ id(1), id(2), id(3), id(4) }, 100.0);
        UNSCOPED_INFO("an asking an owner");
        REQUIRE(timeAsks.size() == 2U);
        {
            INFO("each owner once, none for one unknown");
            CHECK(timeAsks[0].first == "Ann Resident");
            CHECK(timeAsks[1].first == "Bo Smith");
        }
        usage.ask({ id(1) }, 130.0);
        UNSCOPED_INFO("not again within a minute");
        REQUIRE(timeAsks.size() == 2);
        usage.ask({ id(1) }, 161.0);
        {
            INFO("again after it");
            REQUIRE(timeAsks.size() == 3);
            CHECK(timeAsks[2].first == "Ann Resident");
        }

        // Memory first, then time: both kept.
        LLSD details;
        details["parcels"][0]["objects"].append(object(id(1), 65536, 0));
        REQUIRE_FALSE(landAsks.empty());
        landAsks[0].second(details);
        Usage::times_t times;
        times[id(1)] = 0.125f;
        times[id(5)] = 2.0f;
        const S32 before = heard;
        timeAsks[0].second(times);
        CHECK(heard == before + 1);
        const ALScriptRegionUsage::Usage* one = usage.usageOf(id(1));
        REQUIRE(one);
        REQUIRE(one->hasTime());
        REQUIRE(one->time == 0.125f);
        REQUIRE(one->hasMemory());
        CHECK(one->memory == 65536);
        {
            INFO("an object not asked about kept too, time alone");
            REQUIRE(usage.usageOf(id(5)));
            CHECK_FALSE(usage.usageOf(id(5))->hasMemory());
        }
        // Memory again: the time stays.
        landAsks[0].second(details);
        REQUIRE(usage.usageOf(id(1)));
        UNSCOPED_INFO("time kept through memory");
        CHECK(usage.usageOf(id(1))->time == 0.125f);

        // A later answer for the owner that leaves one of theirs out: it
        // runs no scripts now. One as full as Top Scripts gives proves
        // nothing of those it leaves out.
        Usage::times_t later;
        later[id(2)] = 0.5f;
        timeAsks[0].second(later);
        {
            INFO("left out: no time now, its memory kept");
            REQUIRE(usage.usageOf(id(1)));
            REQUIRE_FALSE(usage.usageOf(id(1))->hasTime());
            CHECK(usage.usageOf(id(1))->hasMemory());
        }
        REQUIRE(usage.usageOf(id(2)));
        UNSCOPED_INFO("named: its time");
        CHECK(usage.usageOf(id(2))->time == 0.5f);
        Usage::times_t full;
        for (U32 n = 0; n < ALScriptRegionUsage::TOP_SCRIPTS_MOST; ++n)
        {
            full[id(100 + n)] = 0.01f;
        }
        timeAsks[0].second(full);
        REQUIRE(usage.usageOf(id(2)));
        UNSCOPED_INFO("a full answer leaving it out: its time stands");
        CHECK(usage.usageOf(id(2))->time == 0.5f);
    }

    TEST_CASE_METHOD(alscriptregionusage_data, "ALScriptRegionUsage: ownerKey and answering match a Top Scripts answer to the right ask", "[ALScriptRegionUsage]")
    {
        typedef std::optional<size_t> Whose;
        typedef std::optional<std::string> Theirs;
        UNSCOPED_INFO("a name in either form, lowered");
        CHECK(ALScriptRegionUsage::ownerKey("Ann Resident  ") == std::string("ann"));
        UNSCOPED_INFO("a last name kept");
        CHECK(ALScriptRegionUsage::ownerKey("Bo Smith") == std::string("bo.smith"));
        UNSCOPED_INFO("a username as it is");
        CHECK(ALScriptRegionUsage::ownerKey("Bo.Smith") == std::string("bo.smith"));
        const std::vector<std::string> waiting = { "Ann Resident", "Bo Smith", "Ann Resident" };
        const Theirs                   none;
        UNSCOPED_INFO("the oldest it names");
        CHECK(ALScriptRegionUsage::answering(waiting, { "ann ", "ann " }, none) == Whose(0));
        UNSCOPED_INFO("not the oldest where it names a later one");
        CHECK(ALScriptRegionUsage::answering(waiting, { "Bo Smith" }, none) == Whose(1));
        UNSCOPED_INFO("no rows, Top Objects not waiting: the oldest's");
        CHECK(ALScriptRegionUsage::answering(waiting, {}, none) == Whose(0));
        UNSCOPED_INFO("named otherwise, Top Objects not waiting: the oldest's still");
        CHECK(ALScriptRegionUsage::answering(waiting, { "Cy Resident" }, none) == Whose(0));
        UNSCOPED_INFO("none waiting: none");
        REQUIRE_FALSE(ALScriptRegionUsage::answering({}, { "Ann Resident" }, none));
        const Theirs cy = std::string("cy");
        UNSCOPED_INFO("Top Objects waiting: one naming the studio's owner alone is the studio's");
        CHECK(ALScriptRegionUsage::answering(waiting, { "Bo Smith" }, cy) == Whose(1));
        UNSCOPED_INFO("one naming its own owner: Top Objects'");
        REQUIRE_FALSE(ALScriptRegionUsage::answering(waiting, { "Cy Resident" }, cy));
        UNSCOPED_INFO("no rows: Top Objects'");
        REQUIRE_FALSE(ALScriptRegionUsage::answering(waiting, {}, cy));
        UNSCOPED_INFO("an owner both asked about: Top Objects', the studio's answer to come");
        REQUIRE_FALSE(ALScriptRegionUsage::answering(waiting, { "Bo Smith" }, Theirs(std::string("Bo Smith"))));
    }
}
