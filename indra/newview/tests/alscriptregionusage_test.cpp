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

#include "../test/lltut.h"

#include <map>
#include <optional>

namespace tut
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

    typedef test_group<alscriptregionusage_data> alscriptregionusage_group;
    typedef alscriptregionusage_group::object    alscriptregionusage_object;
    tut::alscriptregionusage_group               alscriptregionusage_test("ALScriptRegionUsage");

    template<> template<>
    void alscriptregionusage_object::test<1>()
    {
        set_test_name("each object an answer lists, by id, with its memory and URLs: the attachments' by point, a parcel's details by parcel");
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
        ensure_equals("both", read.size(), 2U);
        ensure("the chest's", read[id(1)].memory == 65536 && read[id(1)].urls == 1 && read[id(1)].when == when);
        ensure("the HUD's", read[id(2)].memory == 131072 && read[id(2)].urls == 0);

        LLSD details;
        details["parcels"][0]["name"]     = "Home";
        details["parcels"][0]["local_id"] = 12;
        details["parcels"][0]["objects"].append(object(id(3), 16384, 0));
        details["parcels"][0]["objects"].append(object(LLUUID::null, 1, 1));
        read.clear();
        Usage::readDetails(details, when, read);
        ensure("a parcel's, one without an id passed over", read.size() == 1 && read[id(3)].memory == 16384);
        read.clear();
        Usage::readDetails(LLSD(), when, read);
        Usage::readAttachments(LLSD("nonsense"), when, read);
        ensure("nothing from nothing", read.empty());
    }

    template<> template<>
    void alscriptregionusage_object::test<2>()
    {
        set_test_name("asked: the attachments once for all of them, each object on land by itself, none out of sight; heard and kept");
        kinds[id(1)] = Kind::Attachment;
        kinds[id(2)] = Kind::Attachment;
        kinds[id(3)] = Kind::Land;
        kinds[id(4)] = Kind::Land;
        usage.ask({ id(1), id(2), id(3), id(4), id(5) }, 100.0);
        ensure_equals("one asking for the attachments", attachmentAsks.size(), 1U);
        ensure("each on land", landAsks.size() == 2 && landAsks[0].first == id(3) && landAsks[1].first == id(4));
        ensure("nothing known yet", !usage.usageOf(id(1)) && heard == 0);

        LLSD answer;
        answer["attachments"][0]["objects"].append(object(id(1), 65536, 2));
        answer["attachments"][0]["objects"].append(object(id(2), 16384, 0));
        attachmentAsks[0](answer);
        ensure("heard", heard == 1 && usage.usageOf(id(1)) && usage.usageOf(id(1))->urls == 2 && usage.usageOf(id(2))->memory == 16384);
        LLSD parcel;
        parcel["parcels"][0]["objects"].append(object(id(3), 131072, 0));
        parcel["parcels"][0]["objects"].append(object(id(4), 65536, 0));
        landAsks[0].second(parcel);
        ensure("a parcel's answer is every object on it", heard == 2 && usage.usageOf(id(4)) && usage.usageOf(id(4))->memory == 65536);
    }

    template<> template<>
    void alscriptregionusage_object::test<3>()
    {
        set_test_name("once a minute: an object asked again only after, one another answer named counted as asked, and none after it is gone");
        kinds[id(3)] = Kind::Land;
        kinds[id(4)] = Kind::Land;
        usage.ask({ id(3) }, 100.0);
        ensure_equals("asked", landAsks.size(), 1U);
        usage.ask({ id(3) }, 130.0);
        ensure_equals("not again within the minute", landAsks.size(), 1U);
        LLSD parcel;
        parcel["parcels"][0]["objects"].append(object(id(3), 1, 0));
        parcel["parcels"][0]["objects"].append(object(id(4), 2, 0));
        landAsks[0].second(parcel);
        usage.ask({ id(4) }, 140.0);
        ensure_equals("one the answer named is answered already", landAsks.size(), 1U);
        usage.ask({ id(3), id(4) }, 161.0);
        ensure_equals("a minute from the answer, not the asking", landAsks.size(), 1U);
        usage.ask({ id(3), id(4) }, 191.0);
        ensure_equals("after the minute, each again", landAsks.size(), 3U);

        kinds[id(1)] = Kind::Attachment;
        kinds[id(2)] = Kind::Attachment;
        usage.ask({ id(1) }, 200.0);
        usage.ask({ id(2) }, 210.0);
        ensure_equals("the attachments once a minute, whichever asks", attachmentAsks.size(), 1U);

        // An answer after it is gone goes nowhere.
        Told late = landAsks.back().second;
        {
            Usage gone(fake());
        }
        std::optional<Usage> other;
        other.emplace(fake());
        other->ask({ id(3) }, 300.0);
        Told orphaned = landAsks.back().second;
        other.reset();
        orphaned(parcel);
        late(parcel);
        ensure("the one still here heard", usage.usageOf(id(3)) != nullptr);
    }

    template<> template<>
    void alscriptregionusage_object::test<4>()
    {
        set_test_name("time, of an estate manager only: each owner's objects asked for at once, once a minute, an owner unknown not at all; kept with what memory said, and memory kept with it");
        kinds[id(1)] = Kind::Land;
        kinds[id(2)] = Kind::Land;
        kinds[id(3)] = Kind::Attachment;
        kinds[id(4)] = Kind::Land;
        owners[id(1)] = "Ann Resident";
        owners[id(2)] = "Ann Resident";
        owners[id(3)] = "Bo Smith";
        usage.ask({ id(1), id(2), id(3), id(4) }, 100.0);
        ensure("not an estate manager: not asked", timeAsks.empty());

        estateManager = true;
        attachmentAsks.clear();
        landAsks.clear();
        usage.ask({ id(1), id(2), id(3), id(4) }, 100.0);
        ensure_equals("an asking an owner", timeAsks.size(), 2U);
        ensure("each owner once, none for one unknown", timeAsks[0].first == "Ann Resident" && timeAsks[1].first == "Bo Smith");
        usage.ask({ id(1) }, 130.0);
        ensure("not again within a minute", timeAsks.size() == 2);
        usage.ask({ id(1) }, 161.0);
        ensure("again after it", timeAsks.size() == 3 && timeAsks[2].first == "Ann Resident");

        // Memory first, then time: both kept.
        LLSD details;
        details["parcels"][0]["objects"].append(object(id(1), 65536, 0));
        landAsks[0].second(details);
        Usage::times_t times;
        times[id(1)] = 0.125f;
        times[id(5)] = 2.0f;
        const S32 before = heard;
        timeAsks[0].second(times);
        ensure("heard", heard == before + 1);
        const ALScriptRegionUsage::Usage* one = usage.usageOf(id(1));
        ensure("its time and its memory", one && one->hasTime() && one->time == 0.125f && one->hasMemory() && one->memory == 65536);
        ensure("an object not asked about kept too, time alone", usage.usageOf(id(5)) && !usage.usageOf(id(5))->hasMemory());
        // Memory again: the time stays.
        landAsks[0].second(details);
        ensure("time kept through memory", usage.usageOf(id(1))->time == 0.125f);

        // A later answer for the owner that leaves one of theirs out: it
        // runs no scripts now. One as full as Top Scripts gives proves
        // nothing of those it leaves out.
        Usage::times_t later;
        later[id(2)] = 0.5f;
        timeAsks[0].second(later);
        ensure("left out: no time now, its memory kept", !usage.usageOf(id(1))->hasTime() && usage.usageOf(id(1))->hasMemory());
        ensure("named: its time", usage.usageOf(id(2))->time == 0.5f);
        Usage::times_t full;
        for (U32 n = 0; n < ALScriptRegionUsage::TOP_SCRIPTS_MOST; ++n)
        {
            full[id(100 + n)] = 0.01f;
        }
        timeAsks[0].second(full);
        ensure("a full answer leaving it out: its time stands", usage.usageOf(id(2))->time == 0.5f);
    }

    template<> template<>
    void alscriptregionusage_object::test<5>()
    {
        set_test_name("an answer of Top Scripts by owner: with Top Objects not waiting on one, the oldest ask's whose owner its rows name, else the oldest's; with it waiting, its own but for one naming an owner only the studio asked about");
        typedef std::optional<size_t> Whose;
        typedef std::optional<std::string> Theirs;
        ensure_equals("a name in either form, lowered", ALScriptRegionUsage::ownerKey("Ann Resident  "), std::string("ann"));
        ensure_equals("a last name kept", ALScriptRegionUsage::ownerKey("Bo Smith"), std::string("bo.smith"));
        ensure_equals("a username as it is", ALScriptRegionUsage::ownerKey("Bo.Smith"), std::string("bo.smith"));
        const std::vector<std::string> waiting = { "Ann Resident", "Bo Smith", "Ann Resident" };
        const Theirs                   none;
        ensure("the oldest it names", ALScriptRegionUsage::answering(waiting, { "ann ", "ann " }, none) == Whose(0));
        ensure("not the oldest where it names a later one", ALScriptRegionUsage::answering(waiting, { "Bo Smith" }, none) == Whose(1));
        ensure("no rows, Top Objects not waiting: the oldest's", ALScriptRegionUsage::answering(waiting, {}, none) == Whose(0));
        ensure("named otherwise, Top Objects not waiting: the oldest's still", ALScriptRegionUsage::answering(waiting, { "Cy Resident" }, none) == Whose(0));
        ensure("none waiting: none", !ALScriptRegionUsage::answering({}, { "Ann Resident" }, none));
        const Theirs cy = std::string("cy");
        ensure("Top Objects waiting: one naming the studio's owner alone is the studio's", ALScriptRegionUsage::answering(waiting, { "Bo Smith" }, cy) == Whose(1));
        ensure("one naming its own owner: Top Objects'", !ALScriptRegionUsage::answering(waiting, { "Cy Resident" }, cy));
        ensure("no rows: Top Objects'", !ALScriptRegionUsage::answering(waiting, {}, cy));
        ensure("an owner both asked about: Top Objects', the studio's answer to come",
               !ALScriptRegionUsage::answering(waiting, { "Bo Smith" }, Theirs(std::string("Bo Smith"))));
    }
}
