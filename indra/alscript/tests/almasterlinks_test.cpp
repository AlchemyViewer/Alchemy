/**
 * @file almasterlinks_test.cpp
 * @brief The links of disk masters to scripts in the world, their index and their LLSD.
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

#include "../masters/almasterlinks.h"

#include "llsdserialize.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct almasterlinks_data
    {
        static LLUUID key(int n)
        {
            char text[37];
            snprintf(text, sizeof(text), "%08x-0000-4000-8000-000000000000", n);
            return LLUUID(text);
        }

        static ALMasterLink link(int object, int item, const std::string& master)
        {
            ALMasterLink out;
            out.object = object ? key(object) : LLUUID::null;
            out.item   = key(item);
            out.master = master;
            return out;
        }

        // The items of links, in the order given, by the numbers they were
        // made from.
        static std::string items(const std::vector<const ALMasterLink*>& links)
        {
            std::string out;
            for (const ALMasterLink* one : links)
            {
                out += (out.empty() ? "" : " ") + std::to_string(std::stoul(one->item.asString().substr(0, 8), nullptr, 16));
            }
            return out;
        }

        static std::string joined(const std::vector<std::string>& list)
        {
            std::string out;
            for (const std::string& one : list)
            {
                out += (out.empty() ? "" : ", ") + one;
            }
            return out;
        }

        // The files watched, in order, a master's marked with a star.
        static std::string watched(const std::vector<ALMasterLinks::Watched>& list)
        {
            std::string out;
            for (const ALMasterLinks::Watched& one : list)
            {
                out += (out.empty() ? "" : " ") + one.path + (one.master ? "*" : "");
            }
            return out;
        }
    };

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: a link is found by object and item then replaced in place and removed", "[almasterlinks]")
    {
        ALMasterLinks links;
        {
            INFO("none at first");
            REQUIRE(links.empty());
            REQUIRE_FALSE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        }

        links.put(almasterlinks_data::link(1, 2, "/s/door.lsl"));
        links.put(almasterlinks_data::link(0, 2, "/s/inventory.lsl"));
        links.put(almasterlinks_data::link(3, 4, "/s/lamp.lsl"));
        REQUIRE(links.size() == size_t(3));
        const ALMasterLink* const door = links.of(almasterlinks_data::key(1), almasterlinks_data::key(2));
        REQUIRE(door);
        UNSCOPED_INFO("by its object and item");
        CHECK(door->master == std::string("/s/door.lsl"));
        const ALMasterLink* const inventory = links.of(LLUUID::null, almasterlinks_data::key(2));
        REQUIRE(inventory);
        UNSCOPED_INFO("the same item in the inventory is another");
        CHECK(inventory->master == std::string("/s/inventory.lsl"));
        UNSCOPED_INFO("not by the item in another object");
        REQUIRE_FALSE(links.of(almasterlinks_data::key(3), almasterlinks_data::key(2)));

        ALMasterLink again = almasterlinks_data::link(1, 2, "/s/door2.lsl");
        again.made         = ALMasterLink::Made::Hint;
        ALMasterLink& put  = links.put(again);
        UNSCOPED_INFO("put over, not beside");
        REQUIRE(links.size() == size_t(3));
        UNSCOPED_INFO("what put answers is the one kept");
        CHECK(put.master == std::string("/s/door2.lsl"));
        const ALMasterLink* const replaced = links.of(almasterlinks_data::key(1), almasterlinks_data::key(2));
        REQUIRE(replaced);
        UNSCOPED_INFO("and found so");
        CHECK(replaced->made == ALMasterLink::Made::Hint);
        UNSCOPED_INFO("in its place");
        CHECK(links.all()[0].master == std::string("/s/door2.lsl"));

        ALMasterLink* found = links.find(almasterlinks_data::key(3), almasterlinks_data::key(4));
        UNSCOPED_INFO("found to change");
        REQUIRE(found != nullptr);
        found->hash = "xxh128:00";
        const ALMasterLink* const lamp = links.of(almasterlinks_data::key(3), almasterlinks_data::key(4));
        REQUIRE(lamp);
        UNSCOPED_INFO("changed where it is kept");
        CHECK(lamp->hash == std::string("xxh128:00"));
        UNSCOPED_INFO("nothing to change of an item not linked");
        REQUIRE_FALSE(links.find(almasterlinks_data::key(9), almasterlinks_data::key(9)));

        CHECK(links.remove(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        UNSCOPED_INFO("and gone");
        REQUIRE_FALSE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        UNSCOPED_INFO("not twice");
        CHECK_FALSE(links.remove(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        {
            INFO("the rest kept, in order");
            REQUIRE(links.size() == size_t(2));
            CHECK(links.all()[0].master == std::string("/s/inventory.lsl"));
        }
        const ALMasterLink* const moved = links.of(almasterlinks_data::key(3), almasterlinks_data::key(4));
        REQUIRE(moved);
        UNSCOPED_INFO("and found where they now are");
        CHECK(moved->master == std::string("/s/lamp.lsl"));
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: mastering lists a file's items comparing paths as their platform does", "[almasterlinks]")
    {
        ALMasterLinks links;
        links.put(almasterlinks_data::link(1, 10, "/s/net/door.lsl"));
        links.put(almasterlinks_data::link(2, 11, "/s/net/door.lsl"));
        links.put(almasterlinks_data::link(3, 12, "/s/net/lamp.lsl"));
        links.put(almasterlinks_data::link(4, 13, "C:\\Scripts\\Door.lsl"));
        links.put(almasterlinks_data::link(5, 14, "\\\\host\\share\\door.lsl"));

        UNSCOPED_INFO("one file masters several items, in the order put");
        CHECK(almasterlinks_data::items(links.mastering("/s/net/door.lsl")) ==
                      std::string("10 11"));
        UNSCOPED_INFO("a path from a root of /, as written: the disk's own case");
        REQUIRE(links.mastering("/s/net/Door.lsl").empty());
        UNSCOPED_INFO("nor another file");
        REQUIRE(links.mastering("/s/net/window.lsl").empty());
        UNSCOPED_INFO("Windows's, in any case and with either separator");
        CHECK(almasterlinks_data::items(links.mastering("c:/scripts/door.LSL")) == std::string("13"));
        UNSCOPED_INFO("a share's the same");
        CHECK(almasterlinks_data::items(links.mastering("//HOST/share/door.lsl")) ==
                      std::string("14"));
        UNSCOPED_INFO("the key of Windows's");
        CHECK(ALMasterLinks::keyOf("C:/Scripts/Door.lsl") == std::string("c:\\scripts\\door.lsl"));
        UNSCOPED_INFO("and of any other");
        CHECK(ALMasterLinks::keyOf("/S/Door.lsl") == std::string("/S/Door.lsl"));

        // Changed through find(): the index follows.
        ALMasterLink* const remastered = links.find(almasterlinks_data::key(2), almasterlinks_data::key(11));
        REQUIRE(remastered);
        remastered->master = "/s/net/lamp.lsl";
        UNSCOPED_INFO("a master changed through find");
        CHECK(almasterlinks_data::items(links.mastering("/s/net/door.lsl")) ==
                      std::string("10"));
        UNSCOPED_INFO("is found under the new");
        CHECK(almasterlinks_data::items(links.mastering("/s/net/lamp.lsl")) ==
                      std::string("11 12"));
        links.remove(almasterlinks_data::key(1), almasterlinks_data::key(10));
        UNSCOPED_INFO("and one removed is not");
        REQUIRE(links.mastering("/s/net/door.lsl").empty());
        UNSCOPED_INFO("nor are the others lost by it");
        CHECK(almasterlinks_data::items(links.mastering("/s/net/lamp.lsl")) ==
                      std::string("11 12"));
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: usersOf finds links that last read a file by its identity or its path", "[almasterlinks]")
    {
        ALMasterLinks links;
        ALMasterLink  door = almasterlinks_data::link(1, 10, "/s/door.lsl");
        door.uses          = { "disk:/s/lib/util.lsl", "disk:/s/lib/net.lsl", "disk:/s/lib/util.lsl" };
        ALMasterLink lamp  = almasterlinks_data::link(1, 11, "/s/lamp.lsl");
        lamp.uses          = { "disk:/s/lib/util.lsl" };
        ALMasterLink win   = almasterlinks_data::link(2, 12, "C:\\s\\win.lsl");
        win.uses           = { "disk:C:\\S\\Lib\\Util.lsl" };
        links.put(door);
        links.put(lamp);
        links.put(win);

        UNSCOPED_INFO("by its identity, each user once");
        CHECK(almasterlinks_data::items(links.usersOf("disk:/s/lib/util.lsl")) ==
                      std::string("10 11"));
        UNSCOPED_INFO("or by its path alone");
        CHECK(almasterlinks_data::items(links.usersOf("/s/lib/net.lsl")) ==
                      std::string("10"));
        UNSCOPED_INFO("a master is not a use of itself");
        REQUIRE(links.usersOf("/s/door.lsl").empty());
        UNSCOPED_INFO("Windows's in any case");
        CHECK(almasterlinks_data::items(links.usersOf("disk:c:/s/lib/util.lsl")) ==
                      std::string("12"));

        // The uses of the next expansion, written through find().
        ALMasterLink* const expanded = links.find(almasterlinks_data::key(1), almasterlinks_data::key(10));
        REQUIRE(expanded);
        expanded->uses = { "disk:/s/lib/net.lsl" };
        UNSCOPED_INFO("uses changed through find");
        CHECK(almasterlinks_data::items(links.usersOf("disk:/s/lib/util.lsl")) ==
                      std::string("11"));
        ALMasterLink again = lamp;
        again.uses.clear();
        links.put(again);
        UNSCOPED_INFO("and through put");
        REQUIRE(links.usersOf("disk:/s/lib/util.lsl").empty());
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: orphaned links are kept ninety days and then pruned; no other is", "[almasterlinks]")
    {
        const LLDate  now(1800000000.0);
        const F64     day = 24.0 * 60.0 * 60.0;
        ALMasterLinks links;

        ALMasterLink old      = almasterlinks_data::link(1, 10, "/s/a.lsl");
        old.state             = ALMasterLink::State::Orphaned;
        old.orphanedSince     = LLDate(now.secondsSinceEpoch() - 91 * day);
        ALMasterLink young    = almasterlinks_data::link(1, 11, "/s/b.lsl");
        young.state           = ALMasterLink::State::Orphaned;
        young.orphanedSince   = LLDate(now.secondsSinceEpoch() - 89 * day);
        ALMasterLink active   = almasterlinks_data::link(1, 12, "/s/c.lsl");
        active.linked         = LLDate(now.secondsSinceEpoch() - 400 * day);
        ALMasterLink held     = almasterlinks_data::link(1, 13, "/s/d.lsl");
        held.state            = ALMasterLink::State::Suspended;
        held.orphanedSince    = LLDate(now.secondsSinceEpoch() - 400 * day);
        ALMasterLink undated  = almasterlinks_data::link(1, 14, "/s/e.lsl");
        undated.state         = ALMasterLink::State::Orphaned;
        for (const ALMasterLink& one : { old, young, active, held, undated })
        {
            links.put(one);
        }

        CHECK(links.prune(now) == size_t(1));
        UNSCOPED_INFO("the old orphan");
        REQUIRE_FALSE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(10)));
        UNSCOPED_INFO("not the young one");
        REQUIRE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(11)) != nullptr);
        {
            INFO("nor any not orphaned, however old");
            REQUIRE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(12)));
            REQUIRE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(13)));
        }
        const ALMasterLink* dated = links.of(almasterlinks_data::key(1), almasterlinks_data::key(14));
        UNSCOPED_INFO("one with no date is kept");
        REQUIRE(dated != nullptr);
        UNSCOPED_INFO("and dated now");
        CHECK(dated->orphanedSince == now);
        UNSCOPED_INFO("found by its master still");
        REQUIRE(links.mastering("/s/b.lsl").size() == size_t(1));

        UNSCOPED_INFO("fewer days where asked");
        CHECK(links.prune(now, 30.0) == size_t(1));
        UNSCOPED_INFO("the young one now");
        REQUIRE_FALSE(links.of(almasterlinks_data::key(1), almasterlinks_data::key(11)));
        UNSCOPED_INFO("the one dated now goes in its time");
        CHECK(links.prune(LLDate(now.secondsSinceEpoch() + 91 * day)) == size_t(1));
        UNSCOPED_INFO("and nothing else");
        REQUIRE(links.size() == size_t(2));
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: the LLSD is versioned and every field round-trips through XML", "[almasterlinks]")
    {
        ALMasterLinks links;
        ALMasterLink  full;
        full.object        = almasterlinks_data::key(1);
        full.item          = almasterlinks_data::key(2);
        full.master        = "/s/net/door.luau";
        full.made          = ALMasterLink::Made::Name;
        full.lua           = true;
        full.target        = "luau";
        full.base          = almasterlinks_data::key(3);
        full.hash          = "xxh128:0123456789abcdef0123456789abcdef";
        // Nanoseconds now: past what a real holds whole.
        full.stamp         = 1791234567123456789LL;
        full.uses          = { "disk:/s/lib/a.luau", "disk:/s/lib/b.luau" };
        full.objectName    = "Door";
        full.itemName      = "door";
        full.regionName    = "Ahern";
        full.linked        = LLDate(1791234567.0);
        full.state         = ALMasterLink::State::Orphaned;
        full.orphanedSince = LLDate(1791234999.0);
        links.put(full);
        ALMasterLink inventory = almasterlinks_data::link(0, 4, "/s/plain.lsl");
        inventory.state        = ALMasterLink::State::Differing;
        links.put(inventory);
        for (const ALMasterLink::State state : { ALMasterLink::State::Active, ALMasterLink::State::Pending, ALMasterLink::State::Suspended })
        {
            ALMasterLink one = almasterlinks_data::link(5, 10 + static_cast<int>(state), "/s/x.lsl");
            one.state        = state;
            one.made         = ALMasterLink::Made::Hint;
            links.put(one);
        }

        const LLSD llsd = links.toLLSD();
        CHECK(llsd["version"].asInteger() == 1);
        UNSCOPED_INFO("every link");
        REQUIRE(llsd["links"].size() == 5);

        std::ostringstream out;
        LLSDSerialize::toPrettyXML(llsd, out);
        std::istringstream in(out.str());
        LLSD               back;
        REQUIRE(LLSDSerialize::fromXML(back, in) > 0);

        const ALMasterLinks read = ALMasterLinks::fromLLSD(back);
        UNSCOPED_INFO("all read");
        REQUIRE(read.size() == size_t(5));
        const ALMasterLink* got = read.of(full.object, full.item);
        UNSCOPED_INFO("the full one");
        REQUIRE(got != nullptr);
        CHECK(got->master == full.master);
        CHECK(got->made == full.made);
        CHECK(got->lua);
        CHECK(got->target == full.target);
        CHECK(got->base == full.base);
        CHECK(got->hash == full.hash);
        UNSCOPED_INFO("stamp, to the nanosecond");
        CHECK(got->stamp == full.stamp);
        CHECK(almasterlinks_data::joined(got->uses) == almasterlinks_data::joined(full.uses));
        CHECK(got->objectName == full.objectName);
        CHECK(got->itemName == full.itemName);
        CHECK(got->regionName == full.regionName);
        CHECK(got->linked == full.linked);
        CHECK(got->state == full.state);
        CHECK(got->orphanedSince == full.orphanedSince);
        const ALMasterLink* plain = read.of(LLUUID::null, inventory.item);
        {
            INFO("the inventory's, with no object");
            REQUIRE(plain);
            CHECK(plain->object.isNull());
            CHECK(plain->state == ALMasterLink::State::Differing);
        }
        {
            INFO("no date where none was");
            CHECK(plain->orphanedSince.isNull());
            REQUIRE_FALSE(back["links"][1].has("orphaned_since"));
        }
        for (const ALMasterLink::State state : { ALMasterLink::State::Active, ALMasterLink::State::Pending, ALMasterLink::State::Suspended })
        {
            const ALMasterLink* one = read.of(almasterlinks_data::key(5), almasterlinks_data::key(10 + static_cast<int>(state)));
            CAPTURE(state);
            REQUIRE(one);
            REQUIRE(one->state == state);
            REQUIRE(one->made == ALMasterLink::Made::Hint);
        }
        UNSCOPED_INFO("found by their masters");
        REQUIRE(read.mastering("/s/x.lsl").size() == size_t(3));
        UNSCOPED_INFO("and their uses");
        REQUIRE(read.usersOf("disk:/s/lib/b.luau").size() == size_t(1));
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: malformed records are skipped and unknown values are read carefully", "[almasterlinks]")
    {
        UNSCOPED_INFO("not a map");
        REQUIRE(ALMasterLinks::fromLLSD(LLSD("links")).empty());
        UNSCOPED_INFO("no links");
        REQUIRE(ALMasterLinks::fromLLSD(LLSD::emptyMap()).empty());
        LLSD wrong;
        wrong["links"] = "none";
        UNSCOPED_INFO("links not a list");
        REQUIRE(ALMasterLinks::fromLLSD(wrong).empty());

        const auto record = [](const std::string& object, const std::string& item, const LLSD& master) {
            LLSD out;
            if (!object.empty())
            {
                out["object"] = object;
            }
            out["item"]   = item;
            out["master"] = master;
            return out;
        };
        const std::string object = almasterlinks_data::key(1).asString();
        LLSD              llsd;
        llsd["version"] = 1;
        LLSD& links     = llsd["links"];
        links.append("not a record");
        links.append(record(object, "", "/s/no-item.lsl"));
        links.append(record(object, almasterlinks_data::key(2).asString(), ""));
        links.append(record(object, almasterlinks_data::key(3).asString(), LLSD(42)));
        links.append(record("not a key", almasterlinks_data::key(4).asString(), "/s/bad-object.lsl"));
        LLSD good = record(object, almasterlinks_data::key(5).asString(), "/s/good.lsl");
        good["made"]  = "guessed";
        good["state"] = "dormant";
        good["stamp"] = "soon";
        good["base"]  = "not a key";
        LLSD uses;
        uses.append("disk:/s/lib/a.lsl");
        uses.append(7);
        uses.append("");
        good["uses"]  = uses;
        links.append(good);
        // With no object: the agent's inventory.
        LLSD bare = record("", almasterlinks_data::key(6).asString(), "/s/bare.lsl");
        bare["stamp"] = 12345;
        links.append(bare);
        // An item twice: the later wins.
        links.append(record(object, almasterlinks_data::key(5).asString(), "/s/later.lsl"));

        const ALMasterLinks read = ALMasterLinks::fromLLSD(llsd);
        UNSCOPED_INFO("the two whole records");
        REQUIRE(read.size() == size_t(2));
        UNSCOPED_INFO("not one whose object is no key, read as the inventory's");
        REQUIRE_FALSE(read.of(LLUUID::null, almasterlinks_data::key(4)));
        const ALMasterLink* later = read.of(almasterlinks_data::key(1), almasterlinks_data::key(5));
        {
            INFO("an item twice is read once, the later");
            REQUIRE(later);
            CHECK(later->master == "/s/later.lsl");
        }
        const ALMasterLink* plain = read.of(LLUUID::null, almasterlinks_data::key(6));
        UNSCOPED_INFO("one with no object is the inventory's");
        REQUIRE(plain != nullptr);
        {
            INFO("what it does not say is as a new link has it");
            CHECK(plain->made == ALMasterLink::Made::Picked);
            CHECK(plain->state == ALMasterLink::State::Active);
        }
        UNSCOPED_INFO("a stamp written as an integer");
        CHECK(plain->stamp == S64(12345));

        // The earlier of the item twice, alone.
        LLSD once;
        once["links"].append(good);
        const ALMasterLinks careful = ALMasterLinks::fromLLSD(once);
        const ALMasterLink* odd     = careful.of(almasterlinks_data::key(1), almasterlinks_data::key(5));
        REQUIRE(odd != nullptr);
        UNSCOPED_INFO("a way of making not known is a hint's");
        CHECK(odd->made == ALMasterLink::Made::Hint);
        UNSCOPED_INFO("a state not known is suspended");
        CHECK(odd->state == ALMasterLink::State::Suspended);
        UNSCOPED_INFO("a stamp not a number is none");
        CHECK(odd->stamp == 0);
        UNSCOPED_INFO("a base not a key is none");
        CHECK(odd->base.isNull());
        UNSCOPED_INFO("only the uses that are names");
        CHECK(almasterlinks_data::joined(odd->uses) == std::string("disk:/s/lib/a.lsl"));
        UNSCOPED_INFO("a version not given is read all the same");
        REQUIRE(careful.size() == 1);
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: thousands of links over a few masters and includes are found by each", "[almasterlinks]")
    {
        ALMasterLinks links;
        constexpr int LINKS   = 5000;
        constexpr int MASTERS = 50;
        for (int n = 0; n < LINKS; ++n)
        {
            ALMasterLink one = almasterlinks_data::link(1 + n / 100, 100000 + n, "/s/master" + std::to_string(n % MASTERS) + ".lsl");
            one.uses         = { "disk:/s/lib/common.lsl", "disk:/s/lib/part" + std::to_string(n % 7) + ".lsl" };
            links.put(one);
        }
        UNSCOPED_INFO("all kept");
        REQUIRE(links.size() == size_t(LINKS));
        for (int m = 0; m < MASTERS; ++m)
        {
            const std::vector<const ALMasterLink*> found = links.mastering("/s/master" + std::to_string(m) + ".lsl");
            CAPTURE(m);
            REQUIRE(found.size() == size_t(LINKS / MASTERS));
            REQUIRE(found.front()->master == "/s/master" + std::to_string(m) + ".lsl");
        }
        UNSCOPED_INFO("every user of the include they share");
        REQUIRE(links.usersOf("disk:/s/lib/common.lsl").size() == size_t(LINKS));
        UNSCOPED_INFO("and of one a seventh read");
        REQUIRE(links.usersOf("/s/lib/part3.lsl").size() == size_t((LINKS - 3 + 6) / 7));
        for (int n = 0; n < LINKS; n += 2)
        {
            links.remove(almasterlinks_data::key(1 + n / 100), almasterlinks_data::key(100000 + n));
        }
        UNSCOPED_INFO("half removed");
        REQUIRE(links.size() == size_t(LINKS / 2));
        UNSCOPED_INFO("the rest found by their master");
        REQUIRE(links.mastering("/s/master1.lsl").size() == size_t(LINKS / MASTERS));
        UNSCOPED_INFO("none removed found");
        REQUIRE(links.mastering("/s/master0.lsl").empty());
        UNSCOPED_INFO("each found by its item");
        REQUIRE(links.of(almasterlinks_data::key(1 + 4999 / 100), almasterlinks_data::key(104999)) != nullptr);
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: the missed flag round-trips through XML and is written only where set", "[almasterlinks]")
    {
        ALMasterLinks links;
        ALMasterLink  missing = almasterlinks_data::link(1, 10, "/s/door.lsl");
        missing.missed        = true;
        links.put(missing);
        links.put(almasterlinks_data::link(1, 11, "/s/lamp.lsl"));

        const LLSD llsd = links.toLLSD();
        UNSCOPED_INFO("still version 1");
        CHECK(llsd["version"].asInteger() == 1);
        UNSCOPED_INFO("written where set");
        CHECK(llsd["links"][0]["missed"].asBoolean());
        UNSCOPED_INFO("and not where not");
        REQUIRE_FALSE(llsd["links"][1].has("missed"));

        std::ostringstream out;
        LLSDSerialize::toPrettyXML(llsd, out);
        std::istringstream in(out.str());
        LLSD               back;
        REQUIRE(LLSDSerialize::fromXML(back, in) > 0);
        const ALMasterLinks       read  = ALMasterLinks::fromLLSD(back);
        const ALMasterLink* const set   = read.of(almasterlinks_data::key(1), almasterlinks_data::key(10));
        const ALMasterLink* const unset = read.of(almasterlinks_data::key(1), almasterlinks_data::key(11));
        REQUIRE(set);
        REQUIRE(unset);
        UNSCOPED_INFO("read back set");
        CHECK(set->missed);
        UNSCOPED_INFO("and not");
        CHECK_FALSE(unset->missed);

        // As an older version wrote it, with no such field, and as it might
        // be written otherwise.
        LLSD older;
        LLSD record;
        record["item"]   = almasterlinks_data::key(12);
        record["master"] = "/s/old.lsl";
        older["links"].append(record);
        record["item"]   = almasterlinks_data::key(13);
        record["missed"] = 1;
        older["links"].append(record);
        record["item"]   = almasterlinks_data::key(14);
        record["missed"] = LLSD::emptyMap();
        older["links"].append(record);
        const ALMasterLinks old = ALMasterLinks::fromLLSD(older);
        UNSCOPED_INFO("all read");
        REQUIRE(old.size() == size_t(3));
        const ALMasterLink* const unsaid   = old.of(LLUUID::null, almasterlinks_data::key(12));
        const ALMasterLink* const number   = old.of(LLUUID::null, almasterlinks_data::key(13));
        const ALMasterLink* const no_value = old.of(LLUUID::null, almasterlinks_data::key(14));
        REQUIRE(unsaid);
        REQUIRE(number);
        REQUIRE(no_value);
        UNSCOPED_INFO("none said is none missed");
        CHECK_FALSE(unsaid->missed);
        UNSCOPED_INFO("a number is read as one");
        CHECK(number->missed);
        UNSCOPED_INFO("what is no value is none");
        CHECK_FALSE(no_value->missed);
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: affectedBy lists users and links that missed but none suspended or orphaned", "[almasterlinks]")
    {
        ALMasterLinks links;
        const auto    with = [&links](int item, const std::vector<std::string>& uses, bool missed, ALMasterLink::State state) {
            ALMasterLink one = almasterlinks_data::link(1, item, "/s/master" + std::to_string(item) + ".lsl");
            one.uses         = uses;
            one.missed       = missed;
            one.state        = state;
            links.put(one);
        };
        using State = ALMasterLink::State;
        with(10, { "disk:/s/lib/util.lsl" }, false, State::Active);
        with(11, { "disk:/s/lib/net.lsl" }, true, State::Active);
        with(12, { "disk:/s/lib/util.lsl" }, false, State::Suspended);
        with(13, { "disk:/s/lib/util.lsl" }, true, State::Orphaned);
        with(14, { "disk:/s/lib/util.lsl" }, true, State::Pending);
        with(15, { "disk:/s/lib/net.lsl" }, false, State::Active);
        with(16, { "disk:/s/lib/util.lsl" }, false, State::Differing);
        with(17, {}, true, State::Suspended);

        UNSCOPED_INFO("its users and those that missed, in the order put");
        CHECK(almasterlinks_data::items(links.affectedBy("disk:/s/lib/util.lsl")) == std::string("10 11 14 16"));
        UNSCOPED_INFO("by its path alone the same");
        CHECK(almasterlinks_data::items(links.affectedBy("/s/lib/util.lsl")) ==
                      std::string("10 11 14 16"));
        UNSCOPED_INFO("a file nobody read: those that missed");
        CHECK(almasterlinks_data::items(links.affectedBy("/s/lib/new.lsl")) ==
                      std::string("11 14"));
        UNSCOPED_INFO("one both a user and missing is one");
        CHECK(almasterlinks_data::items(links.affectedBy("/s/lib/net.lsl")) ==
                      std::string("11 14 15"));

        // Expanded again, and nothing missed now.
        ALMasterLink* const expanded = links.find(almasterlinks_data::key(1), almasterlinks_data::key(14));
        REQUIRE(expanded);
        expanded->missed = false;
        UNSCOPED_INFO("no longer");
        CHECK(almasterlinks_data::items(links.affectedBy("/s/lib/new.lsl")) == std::string("11"));
        UNSCOPED_INFO("nothing at all with nothing linked");
        REQUIRE(ALMasterLinks().affectedBy("/s/lib/util.lsl").empty());
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: watched lists live and suspended masters and live disk includes once each", "[almasterlinks]")
    {
        ALMasterLinks links;
        const auto    with = [&links](int item, const std::string& master, const std::vector<std::string>& uses, ALMasterLink::State state) {
            ALMasterLink one = almasterlinks_data::link(1, item, master);
            one.uses         = uses;
            one.state        = state;
            links.put(one);
        };
        using State = ALMasterLink::State;
        const std::string in_world = "object:" + almasterlinks_data::key(1).asString() + ":" + almasterlinks_data::key(99).asString();
        with(10, "/s/a.lsl", { "disk:/s/lib/u.lsl", in_world, "disk:/s/lib/v.lsl", "disk:/s/lib/u.lsl" }, State::Active);
        with(11, "/s/b.lsl", { "disk:/s/lib/u.lsl", "inventory:" + almasterlinks_data::key(98).asString() }, State::Differing);
        // An include here that a link later masters.
        with(12, "/s/c.lsl", { "disk:/s/c2.lsl" }, State::Pending);
        with(13, "/s/c2.lsl", {}, State::Active);
        // A master gone: its file watched, to hear it come back; not what
        // it last read.
        with(14, "/s/e.lsl", { "disk:/s/lib/w.lsl" }, State::Suspended);
        with(15, "/s/f.lsl", { "disk:/s/lib/x.lsl" }, State::Orphaned);
        // A master of two links, and a master another link reads.
        with(16, "C:\\S\\G.lsl", { "disk:/s/a.lsl" }, State::Active);
        with(17, "c:/s/g.lsl", {}, State::Active);

        UNSCOPED_INFO("each once, in the order met");
        CHECK(almasterlinks_data::watched(links.watched()) ==
                      std::string("/s/a.lsl* /s/lib/u.lsl /s/lib/v.lsl /s/b.lsl* /s/c.lsl* /s/c2.lsl* /s/e.lsl* C:\\S\\G.lsl*"));
        UNSCOPED_INFO("nothing with nothing linked");
        REQUIRE(ALMasterLinks().watched().empty());

        // Orphaned, the file it masters is watched no longer.
        ALMasterLink* const gone = links.find(almasterlinks_data::key(1), almasterlinks_data::key(14));
        REQUIRE(gone);
        gone->state = State::Orphaned;
        ALMasterLink* const include = links.find(almasterlinks_data::key(1), almasterlinks_data::key(13));
        REQUIRE(include);
        include->state = State::Orphaned;
        UNSCOPED_INFO("an include again where it no longer masters");
        CHECK(almasterlinks_data::watched(links.watched()) ==
                      std::string("/s/a.lsl* /s/lib/u.lsl /s/lib/v.lsl /s/b.lsl* /s/c.lsl* /s/c2.lsl C:\\S\\G.lsl*"));
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: orphansNamed lists an item name's orphans by object name then latest", "[almasterlinks]")
    {
        ALMasterLinks links;
        const auto    orphan = [&links](int item, const std::string& item_name, const std::string& object_name, F64 since) {
            ALMasterLink one  = almasterlinks_data::link(1, item, "/s/" + std::to_string(item) + ".lsl");
            one.itemName      = item_name;
            one.objectName    = object_name;
            one.state         = ALMasterLink::State::Orphaned;
            one.orphanedSince = LLDate(since);
            links.put(one);
        };
        orphan(10, "door", "Gate", 1000.0);
        orphan(11, "door", "Door", 500.0);
        orphan(12, "door", "Door", 2000.0);
        orphan(13, "door", "Shed", 3000.0);
        orphan(14, "lamp", "Door", 4000.0);
        orphan(15, "Door", "Door", 5000.0);
        // The same name, but not orphaned.
        ALMasterLink live = almasterlinks_data::link(2, 16, "/s/live.lsl");
        live.itemName     = "door";
        live.objectName   = "Door";
        links.put(live);
        orphan(17, "door", "Shed", 3000.0);

        UNSCOPED_INFO("the object's first, then the latest, as put among equals");
        CHECK(almasterlinks_data::items(links.orphansNamed("door", "Door")) == std::string("12 11 13 17 10"));
        UNSCOPED_INFO("an object of no such name: the latest first");
        CHECK(almasterlinks_data::items(links.orphansNamed("door", "Window")) == std::string("13 17 12 10 11"));
        UNSCOPED_INFO("names compared as written");
        CHECK(almasterlinks_data::items(links.orphansNamed("Door", "door")) == std::string("15"));
        UNSCOPED_INFO("an item of no such name has none");
        REQUIRE(links.orphansNamed("window", "Door").empty());
        UNSCOPED_INFO("nor an item with no name");
        REQUIRE(links.orphansNamed("", "Door").empty());
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: a notecard link round-trips through the LLSD and a script's omits the flag", "[almasterlinks]")
    {
        ALMasterLinks links;
        ALMasterLink  note = almasterlinks_data::link(1, 10, "/s/readme.txt");
        note.notecard      = true;
        links.put(note);
        links.put(almasterlinks_data::link(1, 11, "/s/lamp.lsl"));
        const LLSD llsd = links.toLLSD();
        UNSCOPED_INFO("written where set");
        CHECK(llsd["links"][0]["notecard"].asBoolean());
        UNSCOPED_INFO("and not where not");
        REQUIRE_FALSE(llsd["links"][1].has("notecard"));
        const ALMasterLinks       read   = ALMasterLinks::fromLLSD(llsd);
        const ALMasterLink* const card   = read.of(almasterlinks_data::key(1), almasterlinks_data::key(10));
        const ALMasterLink* const script = read.of(almasterlinks_data::key(1), almasterlinks_data::key(11));
        REQUIRE(card);
        REQUIRE(script);
        UNSCOPED_INFO("read back a notecard's");
        CHECK(card->notecard);
        UNSCOPED_INFO("and a script's");
        CHECK_FALSE(script->notecard);
    }

    TEST_CASE_METHOD(almasterlinks_data, "almasterlinks: a name XML would not carry round-trips through XML as its bytes", "[almasterlinks]")
    {
        // A control character in a name -- anyone may give an object one --
        // written as it is would leave the file unreadable at the next
        // start, and every link lost with it.
        ALMasterLinks     links;
        const std::string odd("a\x01" "b\x1f" "c", 5);
        ALMasterLink      link = almasterlinks_data::link(1, 10, "/s/door.lsl");
        link.objectName        = odd;
        link.itemName          = "tab\tnewline\nfine";
        link.regionName        = "return\rback";
        link.uses              = { "disk:/s/lib/" + odd + ".lsl" };
        links.put(link);
        ALMasterLink plain = almasterlinks_data::link(1, 11, "/s/lamp.lsl");
        plain.objectName   = "Lamp <&> \"it's\"";
        links.put(plain);

        const LLSD llsd = links.toLLSD();
        {
            INFO("as bytes only where XML would not carry it");
            CHECK(llsd["links"][0]["object_name"].isBinary());
            CHECK(llsd["links"][0]["item_name"].isString());
            CHECK(llsd["links"][0]["region_name"].isBinary());
            CHECK(llsd["links"][0]["uses"][0].isBinary());
            CHECK(llsd["links"][1]["object_name"].isString());
        }
        std::ostringstream out;
        LLSDSerialize::toPrettyXML(llsd, out);
        std::istringstream in(out.str());
        LLSD               back;
        UNSCOPED_INFO("read back whole");
        REQUIRE(LLSDSerialize::fromXML(back, in) > 0);
        const ALMasterLinks       read   = ALMasterLinks::fromLLSD(back);
        const ALMasterLink* const got    = read.of(almasterlinks_data::key(1), almasterlinks_data::key(10));
        const ALMasterLink* const simple = read.of(almasterlinks_data::key(1), almasterlinks_data::key(11));
        REQUIRE(got);
        REQUIRE(simple);
        {
            INFO("as they were");
            CHECK(got->objectName == odd);
            CHECK(got->itemName == std::string("tab\tnewline\nfine"));
            CHECK(got->regionName == std::string("return\rback"));
            CHECK(almasterlinks_data::joined(got->uses) == "disk:/s/lib/" + odd + ".lsl");
            CHECK(simple->objectName == std::string("Lamp <&> \"it's\""));
        }
        UNSCOPED_INFO("found by the use still");
        REQUIRE(read.usersOf("disk:/s/lib/" + odd + ".lsl").size() == size_t(1));
    }
}
