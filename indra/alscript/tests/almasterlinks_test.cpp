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

#include "../test/lltut.h"

#include <sstream>
#include <string>
#include <vector>

namespace tut
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

    typedef test_group<almasterlinks_data> almasterlinks_group;
    typedef almasterlinks_group::object    almasterlinks_object;
    almasterlinks_group                    almasterlinks_instance("almasterlinks");

    template<> template<>
    void almasterlinks_object::test<1>()
    {
        set_test_name("a link is found by its object and item, put over, and removed");
        ALMasterLinks links;
        ensure("none at first", links.empty() && !links.of(almasterlinks_data::key(1), almasterlinks_data::key(2)));

        links.put(almasterlinks_data::link(1, 2, "/s/door.lsl"));
        links.put(almasterlinks_data::link(0, 2, "/s/inventory.lsl"));
        links.put(almasterlinks_data::link(3, 4, "/s/lamp.lsl"));
        ensure_equals("three", links.size(), size_t(3));
        ensure_equals("by its object and item", links.of(almasterlinks_data::key(1), almasterlinks_data::key(2))->master, std::string("/s/door.lsl"));
        ensure_equals("the same item in the inventory is another", links.of(LLUUID::null, almasterlinks_data::key(2))->master,
                      std::string("/s/inventory.lsl"));
        ensure("not by the item in another object", !links.of(almasterlinks_data::key(3), almasterlinks_data::key(2)));

        ALMasterLink again = almasterlinks_data::link(1, 2, "/s/door2.lsl");
        again.made         = ALMasterLink::Made::Hint;
        ALMasterLink& put  = links.put(again);
        ensure_equals("put over, not beside", links.size(), size_t(3));
        ensure_equals("what put answers is the one kept", put.master, std::string("/s/door2.lsl"));
        ensure("and found so", links.of(almasterlinks_data::key(1), almasterlinks_data::key(2))->made == ALMasterLink::Made::Hint);
        ensure_equals("in its place", links.all()[0].master, std::string("/s/door2.lsl"));

        ALMasterLink* found = links.find(almasterlinks_data::key(3), almasterlinks_data::key(4));
        ensure("found to change", found != nullptr);
        found->hash = "xxh128:00";
        ensure_equals("changed where it is kept", links.of(almasterlinks_data::key(3), almasterlinks_data::key(4))->hash, std::string("xxh128:00"));
        ensure("nothing to change of an item not linked", !links.find(almasterlinks_data::key(9), almasterlinks_data::key(9)));

        ensure("removed", links.remove(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        ensure("and gone", !links.of(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        ensure("not twice", !links.remove(almasterlinks_data::key(1), almasterlinks_data::key(2)));
        ensure_equals("the rest kept, in order", links.all()[0].master, std::string("/s/inventory.lsl"));
        ensure_equals("and found where they now are", links.of(almasterlinks_data::key(3), almasterlinks_data::key(4))->master,
                      std::string("/s/lamp.lsl"));
    }

    template<> template<>
    void almasterlinks_object::test<2>()
    {
        set_test_name("every item a master file masters, a path compared as its platform compares it");
        ALMasterLinks links;
        links.put(almasterlinks_data::link(1, 10, "/s/net/door.lsl"));
        links.put(almasterlinks_data::link(2, 11, "/s/net/door.lsl"));
        links.put(almasterlinks_data::link(3, 12, "/s/net/lamp.lsl"));
        links.put(almasterlinks_data::link(4, 13, "C:\\Scripts\\Door.lsl"));
        links.put(almasterlinks_data::link(5, 14, "\\\\host\\share\\door.lsl"));

        ensure_equals("one file masters several items, in the order put", almasterlinks_data::items(links.mastering("/s/net/door.lsl")),
                      std::string("10 11"));
        ensure("a path from a root of /, as written: the disk's own case", links.mastering("/s/net/Door.lsl").empty());
        ensure("nor another file", links.mastering("/s/net/window.lsl").empty());
        ensure_equals("Windows's, in any case and with either separator",
                      almasterlinks_data::items(links.mastering("c:/scripts/door.LSL")), std::string("13"));
        ensure_equals("a share's the same", almasterlinks_data::items(links.mastering("//HOST/share/door.lsl")),
                      std::string("14"));
        ensure_equals("the key of Windows's", ALMasterLinks::keyOf("C:/Scripts/Door.lsl"), std::string("c:\\scripts\\door.lsl"));
        ensure_equals("and of any other", ALMasterLinks::keyOf("/S/Door.lsl"), std::string("/S/Door.lsl"));

        // Changed through find(): the index follows.
        links.find(almasterlinks_data::key(2), almasterlinks_data::key(11))->master = "/s/net/lamp.lsl";
        ensure_equals("a master changed through find", almasterlinks_data::items(links.mastering("/s/net/door.lsl")),
                      std::string("10"));
        ensure_equals("is found under the new", almasterlinks_data::items(links.mastering("/s/net/lamp.lsl")),
                      std::string("11 12"));
        links.remove(almasterlinks_data::key(1), almasterlinks_data::key(10));
        ensure("and one removed is not", links.mastering("/s/net/door.lsl").empty());
        ensure_equals("nor are the others lost by it", almasterlinks_data::items(links.mastering("/s/net/lamp.lsl")),
                      std::string("11 12"));
    }

    template<> template<>
    void almasterlinks_object::test<3>()
    {
        set_test_name("every link whose last expansion read a file, by the file's identity or its path");
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

        ensure_equals("by its identity, each user once", almasterlinks_data::items(links.usersOf("disk:/s/lib/util.lsl")),
                      std::string("10 11"));
        ensure_equals("or by its path alone", almasterlinks_data::items(links.usersOf("/s/lib/net.lsl")),
                      std::string("10"));
        ensure("a master is not a use of itself", links.usersOf("/s/door.lsl").empty());
        ensure_equals("Windows's in any case", almasterlinks_data::items(links.usersOf("disk:c:/s/lib/util.lsl")),
                      std::string("12"));

        // The uses of the next expansion, written through find().
        links.find(almasterlinks_data::key(1), almasterlinks_data::key(10))->uses = { "disk:/s/lib/net.lsl" };
        ensure_equals("uses changed through find", almasterlinks_data::items(links.usersOf("disk:/s/lib/util.lsl")),
                      std::string("11"));
        ALMasterLink again = lamp;
        again.uses.clear();
        links.put(again);
        ensure("and through put", links.usersOf("disk:/s/lib/util.lsl").empty());
    }

    template<> template<>
    void almasterlinks_object::test<4>()
    {
        set_test_name("orphaned links are kept ninety days and then pruned; no other is");
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

        ensure_equals("one pruned", links.prune(now), size_t(1));
        ensure("the old orphan", !links.of(almasterlinks_data::key(1), almasterlinks_data::key(10)));
        ensure("not the young one", links.of(almasterlinks_data::key(1), almasterlinks_data::key(11)) != nullptr);
        ensure("nor any not orphaned, however old",
               links.of(almasterlinks_data::key(1), almasterlinks_data::key(12)) && links.of(almasterlinks_data::key(1), almasterlinks_data::key(13)));
        const ALMasterLink* dated = links.of(almasterlinks_data::key(1), almasterlinks_data::key(14));
        ensure("one with no date is kept", dated != nullptr);
        ensure("and dated now", dated->orphanedSince == now);
        ensure_equals("found by its master still", links.mastering("/s/b.lsl").size(), size_t(1));

        ensure_equals("fewer days where asked", links.prune(now, 30.0), size_t(1));
        ensure("the young one now", !links.of(almasterlinks_data::key(1), almasterlinks_data::key(11)));
        ensure_equals("the one dated now goes in its time", links.prune(LLDate(now.secondsSinceEpoch() + 91 * day)), size_t(1));
        ensure_equals("and nothing else", links.size(), size_t(2));
    }

    template<> template<>
    void almasterlinks_object::test<5>()
    {
        set_test_name("the LLSD is versioned, and every field comes back, through notation too");
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
        ensure_equals("version 1", llsd["version"].asInteger(), 1);
        ensure_equals("every link", llsd["links"].size(), 5);

        std::ostringstream out;
        LLSDSerialize::toNotation(llsd, out);
        std::istringstream in(out.str());
        LLSD               back;
        ensure("as notation", LLSDSerialize::fromNotation(back, in, static_cast<llssize>(out.str().size())) > 0);

        const ALMasterLinks read = ALMasterLinks::fromLLSD(back);
        ensure_equals("all read", read.size(), size_t(5));
        const ALMasterLink* got = read.of(full.object, full.item);
        ensure("the full one", got != nullptr);
        ensure_equals("master", got->master, full.master);
        ensure("made", got->made == full.made);
        ensure("lua", got->lua);
        ensure_equals("target", got->target, full.target);
        ensure_equals("base", got->base, full.base);
        ensure_equals("hash", got->hash, full.hash);
        ensure_equals("stamp, to the nanosecond", got->stamp, full.stamp);
        ensure_equals("uses", almasterlinks_data::joined(got->uses), almasterlinks_data::joined(full.uses));
        ensure_equals("object name", got->objectName, full.objectName);
        ensure_equals("item name", got->itemName, full.itemName);
        ensure_equals("region name", got->regionName, full.regionName);
        ensure("linked", got->linked == full.linked);
        ensure("state", got->state == full.state);
        ensure("orphaned since", got->orphanedSince == full.orphanedSince);
        const ALMasterLink* plain = read.of(LLUUID::null, inventory.item);
        ensure("the inventory's, with no object", plain && plain->object.isNull() && plain->state == ALMasterLink::State::Differing);
        ensure("no date where none was", plain->orphanedSince.isNull() && !back["links"][1].has("orphaned_since"));
        for (const ALMasterLink::State state : { ALMasterLink::State::Active, ALMasterLink::State::Pending, ALMasterLink::State::Suspended })
        {
            const ALMasterLink* one = read.of(almasterlinks_data::key(5), almasterlinks_data::key(10 + static_cast<int>(state)));
            ensure("each state", one && one->state == state && one->made == ALMasterLink::Made::Hint);
        }
        ensure_equals("found by their masters", read.mastering("/s/x.lsl").size(), size_t(3));
        ensure_equals("and their uses", read.usersOf("disk:/s/lib/b.luau").size(), size_t(1));
    }

    template<> template<>
    void almasterlinks_object::test<6>()
    {
        set_test_name("a malformed record is passed over and the rest read; what is not known is read carefully");
        ensure("not a map", ALMasterLinks::fromLLSD(LLSD("links")).empty());
        ensure("no links", ALMasterLinks::fromLLSD(LLSD::emptyMap()).empty());
        LLSD wrong;
        wrong["links"] = "none";
        ensure("links not a list", ALMasterLinks::fromLLSD(wrong).empty());

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
        ensure_equals("the two whole records", read.size(), size_t(2));
        ensure("not one whose object is no key, read as the inventory's", !read.of(LLUUID::null, almasterlinks_data::key(4)));
        const ALMasterLink* later = read.of(almasterlinks_data::key(1), almasterlinks_data::key(5));
        ensure("an item twice is read once, the later", later && later->master == "/s/later.lsl");
        const ALMasterLink* plain = read.of(LLUUID::null, almasterlinks_data::key(6));
        ensure("one with no object is the inventory's", plain != nullptr);
        ensure("what it does not say is as a new link has it", plain->made == ALMasterLink::Made::Picked && plain->state == ALMasterLink::State::Active);
        ensure_equals("a stamp written as an integer", plain->stamp, S64(12345));

        // The earlier of the item twice, alone.
        LLSD once;
        once["links"].append(good);
        const ALMasterLinks careful = ALMasterLinks::fromLLSD(once);
        const ALMasterLink* odd     = careful.of(almasterlinks_data::key(1), almasterlinks_data::key(5));
        ensure("read", odd != nullptr);
        ensure("a way of making not known is a hint's", odd->made == ALMasterLink::Made::Hint);
        ensure("a state not known is suspended", odd->state == ALMasterLink::State::Suspended);
        ensure("a stamp not a number is none", odd->stamp == 0);
        ensure("a base not a key is none", odd->base.isNull());
        ensure_equals("only the uses that are names", almasterlinks_data::joined(odd->uses), std::string("disk:/s/lib/a.lsl"));
        ensure("a version not given is read all the same", careful.size() == 1);
    }

    template<> template<>
    void almasterlinks_object::test<7>()
    {
        set_test_name("thousands of links over a few masters and includes are found by each");
        ALMasterLinks links;
        constexpr int LINKS   = 5000;
        constexpr int MASTERS = 50;
        for (int n = 0; n < LINKS; ++n)
        {
            ALMasterLink one = almasterlinks_data::link(1 + n / 100, 100000 + n, "/s/master" + std::to_string(n % MASTERS) + ".lsl");
            one.uses         = { "disk:/s/lib/common.lsl", "disk:/s/lib/part" + std::to_string(n % 7) + ".lsl" };
            links.put(one);
        }
        ensure_equals("all kept", links.size(), size_t(LINKS));
        for (int m = 0; m < MASTERS; ++m)
        {
            const std::vector<const ALMasterLink*> found = links.mastering("/s/master" + std::to_string(m) + ".lsl");
            ensure_equals("each master's", found.size(), size_t(LINKS / MASTERS));
            ensure("each the master's", found.front()->master == "/s/master" + std::to_string(m) + ".lsl");
        }
        ensure_equals("every user of the include they share", links.usersOf("disk:/s/lib/common.lsl").size(), size_t(LINKS));
        ensure_equals("and of one a seventh read", links.usersOf("/s/lib/part3.lsl").size(), size_t((LINKS - 3 + 6) / 7));
        for (int n = 0; n < LINKS; n += 2)
        {
            links.remove(almasterlinks_data::key(1 + n / 100), almasterlinks_data::key(100000 + n));
        }
        ensure_equals("half removed", links.size(), size_t(LINKS / 2));
        ensure_equals("the rest found by their master", links.mastering("/s/master1.lsl").size(), size_t(LINKS / MASTERS));
        ensure("none removed found", links.mastering("/s/master0.lsl").empty());
        ensure("each found by its item", links.of(almasterlinks_data::key(1 + 4999 / 100), almasterlinks_data::key(104999)) != nullptr);
    }

    template<> template<>
    void almasterlinks_object::test<8>()
    {
        set_test_name("whether the last expansion missed something comes back through the LLSD, and is written only where set");
        ALMasterLinks links;
        ALMasterLink  missing = almasterlinks_data::link(1, 10, "/s/door.lsl");
        missing.missed        = true;
        links.put(missing);
        links.put(almasterlinks_data::link(1, 11, "/s/lamp.lsl"));

        const LLSD llsd = links.toLLSD();
        ensure_equals("still version 1", llsd["version"].asInteger(), 1);
        ensure("written where set", llsd["links"][0]["missed"].asBoolean());
        ensure("and not where not", !llsd["links"][1].has("missed"));

        std::ostringstream out;
        LLSDSerialize::toNotation(llsd, out);
        std::istringstream in(out.str());
        LLSD               back;
        ensure("as notation", LLSDSerialize::fromNotation(back, in, static_cast<llssize>(out.str().size())) > 0);
        const ALMasterLinks read = ALMasterLinks::fromLLSD(back);
        ensure("read back set", read.of(almasterlinks_data::key(1), almasterlinks_data::key(10))->missed);
        ensure("and not", !read.of(almasterlinks_data::key(1), almasterlinks_data::key(11))->missed);

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
        ensure_equals("all read", old.size(), size_t(3));
        ensure("none said is none missed", !old.of(LLUUID::null, almasterlinks_data::key(12))->missed);
        ensure("a number is read as one", old.of(LLUUID::null, almasterlinks_data::key(13))->missed);
        ensure("what is no value is none", !old.of(LLUUID::null, almasterlinks_data::key(14))->missed);
    }

    template<> template<>
    void almasterlinks_object::test<9>()
    {
        set_test_name("an include's change may send its users and every link that missed something, each once, none suspended or orphaned");
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

        ensure_equals("its users and those that missed, in the order put",
                      almasterlinks_data::items(links.affectedBy("disk:/s/lib/util.lsl")), std::string("10 11 14 16"));
        ensure_equals("by its path alone the same", almasterlinks_data::items(links.affectedBy("/s/lib/util.lsl")),
                      std::string("10 11 14 16"));
        ensure_equals("a file nobody read: those that missed", almasterlinks_data::items(links.affectedBy("/s/lib/new.lsl")),
                      std::string("11 14"));
        ensure_equals("one both a user and missing is one", almasterlinks_data::items(links.affectedBy("/s/lib/net.lsl")),
                      std::string("11 14 15"));

        // Expanded again, and nothing missed now.
        links.find(almasterlinks_data::key(1), almasterlinks_data::key(14))->missed = false;
        ensure_equals("no longer", almasterlinks_data::items(links.affectedBy("/s/lib/new.lsl")), std::string("11"));
        ensure("nothing at all with nothing linked", ALMasterLinks().affectedBy("/s/lib/util.lsl").empty());
    }

    template<> template<>
    void almasterlinks_object::test<10>()
    {
        set_test_name("the files to watch: live and suspended links' masters, live ones' disk includes, each once, a master as a master");
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

        ensure_equals("each once, in the order met", almasterlinks_data::watched(links.watched()),
                      std::string("/s/a.lsl* /s/lib/u.lsl /s/lib/v.lsl /s/b.lsl* /s/c.lsl* /s/c2.lsl* /s/e.lsl* C:\\S\\G.lsl*"));
        ensure("nothing with nothing linked", ALMasterLinks().watched().empty());

        // Orphaned, the file it masters is watched no longer.
        links.find(almasterlinks_data::key(1), almasterlinks_data::key(14))->state = State::Orphaned;
        links.find(almasterlinks_data::key(1), almasterlinks_data::key(13))->state = State::Orphaned;
        ensure_equals("an include again where it no longer masters", almasterlinks_data::watched(links.watched()),
                      std::string("/s/a.lsl* /s/lib/u.lsl /s/lib/v.lsl /s/b.lsl* /s/c.lsl* /s/c2.lsl C:\\S\\G.lsl*"));
    }

    template<> template<>
    void almasterlinks_object::test<11>()
    {
        set_test_name("the orphaned links of an item's name, those of the object's name first, then the latest");
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

        ensure_equals("the object's first, then the latest, as put among equals",
                      almasterlinks_data::items(links.orphansNamed("door", "Door")), std::string("12 11 13 17 10"));
        ensure_equals("an object of no such name: the latest first",
                      almasterlinks_data::items(links.orphansNamed("door", "Window")), std::string("13 17 12 10 11"));
        ensure_equals("names compared as written", almasterlinks_data::items(links.orphansNamed("Door", "door")), std::string("15"));
        ensure("an item of no such name has none", links.orphansNamed("window", "Door").empty());
        ensure("nor an item with no name", links.orphansNamed("", "Door").empty());
    }
}
