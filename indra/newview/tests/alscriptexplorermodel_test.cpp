/**
 * @file alscriptexplorermodel_test.cpp
 * @brief What Script Studio's explorer lists, and the rows it makes of them.
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

#include "../alscriptexplorermodel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    struct alscriptexplorermodel_data
    {
        typedef ALScriptExplorerModel Model;
        typedef std::function<void(const ALScriptContents&)> Told;

        // The world: each object in sight by its root, its prims the root
        // first; what is selected; the prims of the scripts open; the prims
        // whose object's copy is not current. The index's asks not answered
        // yet, and every ask made, of the region or not; the scripts it
        // asked whether they run.
        std::map<LLUUID, Model::Seen>        world;
        std::vector<LLUUID>                  selected;
        std::vector<LLUUID>                  open;
        std::set<LLUUID>                     stale;
        std::map<LLUUID, std::vector<Told>>  pending;
        std::vector<std::pair<LLUUID, bool>> asks;
        std::vector<ALScriptRef>             askedRunning;
        ALScriptContentsIndex                index;
        Model                                model;
        // What the model made of the last answer, heard as the pane hears it.
        Model::Heard                         lastHeard;
        boost::signals2::scoped_connection   heardConnection;

        alscriptexplorermodel_data() : index(fake()), model(index)
        {
            heardConnection = index.onHeard([this](const ALScriptContents& contents) { lastHeard = model.contents(contents); });
        }

        ALScriptContentsIndex::World fake()
        {
            ALScriptContentsIndex::World out;
            out.current = [this](const LLUUID& prim) { return !stale.contains(prim); };
            out.ask     = [this](const LLUUID& prim, bool from_region, Told told) {
                asks.emplace_back(prim, from_region);
                pending[prim].push_back(std::move(told));
            };
            out.askRunning = [this](const ALScriptRef& ref) { askedRunning.push_back(ref); };
            return out;
        }

        // What the pane asks of the index after a listing: the prims asked
        // now.
        std::vector<LLUUID> ask(bool refetch, bool filtering)
        {
            const size_t before = asks.size();
            for (const LLUUID& prim : model.wanted(filtering))
            {
                index.ask(prim, refetch, model.asksRegion(prim));
            }
            std::vector<LLUUID> out;
            for (size_t i = before; i < asks.size(); ++i)
            {
                out.push_back(asks[i].first);
            }
            return out;
        }

        // An answer, through the index: to every ask of its prim not
        // answered yet, or to one made for it.
        Model::Heard feed(const ALScriptContents& contents)
        {
            if (pending[contents.prim].empty())
            {
                index.ask(contents.prim, true);
            }
            std::vector<Told> told = std::move(pending[contents.prim]);
            pending.erase(contents.prim);
            lastHeard = Model::Heard();
            for (Told& one : told)
            {
                one(contents);
            }
            return lastHeard;
        }

        static LLUUID id(U32 n)
        {
            LLUUID out;
            out.mData[0] = static_cast<U8>(n);
            out.mData[1] = static_cast<U8>(n >> 8);
            out.mData[15] = 1;
            return out;
        }

        // An object of `prims` prims in sight, named "name" and "name.1" on.
        LLUUID object(U32 root, const std::string& name, size_t prims = 1)
        {
            Model::Seen seen;
            for (size_t i = 0; i < prims; ++i)
            {
                seen.prims.push_back({ id(root + static_cast<U32>(i)), i == 0 ? name : name + "." + std::to_string(i) });
            }
            world[id(root)] = seen;
            return id(root);
        }

        void list()
        {
            Model::Listing listing;
            listing.seen = [this](const LLUUID& any) -> std::optional<Model::Seen> {
                for (const auto& [root, seen] : world)
                {
                    for (const Model::Seen::Part& part : seen.prims)
                    {
                        if (part.id == any)
                        {
                            return seen;
                        }
                    }
                }
                return std::nullopt;
            };
            listing.nameless = [](const LLUUID&) { return std::string("..."); };
            listing.selected = selected;
            listing.open     = open;
            listing.unnamed  = "(unnamed)";
            model.list(listing);
        }

        // What a prim says it holds.
        void holds(const LLUUID& prim, std::vector<std::string> names, bool fetched = true)
        {
            ALScriptContents contents;
            contents.prim    = prim;
            contents.fetched = fetched;
            U32 n            = 1000;
            for (const std::string& name : names)
            {
                ALScriptContents::Item item;
                item.id     = id(n++ + static_cast<U32>(prim.mData[0]) * 50);
                item.name   = name;
                item.script = name.ends_with(".lsl") || name.ends_with(".luau");
                item.lua    = name.ends_with(".luau");
                contents.items.push_back(item);
            }
            feed(contents);
        }

        // The rows as words: an object "O:name", a prim "P:name", the prims
        // holding nothing "E:count", an item "I:name"; a folded one with a +.
        std::string rows(const std::string& filter = std::string()) const
        {
            std::string out;
            for (const Model::Row& row : model.rows(filter))
            {
                std::string one;
                switch (row.kind)
                {
                    case Model::Row::Kind::Object: one = "O:" + row.name; break;
                    case Model::Row::Kind::Prim: one = "P:" + row.name; break;
                    case Model::Row::Kind::Empties: one = "E:" + std::to_string(row.empties); break;
                    case Model::Row::Kind::Item: one = "I:" + row.name; break;
                }
                out += (out.empty() ? "" : " ") + one + (row.folded ? "+" : "");
            }
            return out;
        }

        std::string names() const
        {
            std::string out;
            for (const Model::Object& object : model.objects())
            {
                out += (out.empty() ? "" : " ") + object.name + (object.present ? "" : "(away)") + (object.pinned ? "*" : "");
            }
            return out;
        }

        const Model::Row* rowNamed(const std::vector<Model::Row>& all, const std::string& name) const
        {
            const auto found = std::find_if(all.begin(), all.end(), [&name](const Model::Row& row) { return row.name == name; });
            return found != all.end() ? &*found : nullptr;
        }

        // What the first row of that name stands for, as a click on it
        // would choose it.
        Model::Choice require_choice(const std::vector<Model::Row>& all, const std::string& name) const
        {
            CAPTURE(name);
            const Model::Row* row = rowNamed(all, name);
            REQUIRE(row != nullptr);
            const std::optional<Model::Choice> choice = Model::Choice::of(row->value);
            REQUIRE(choice.has_value());
            return *choice;
        }
    };

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: pins come first then the selection then the open scripts' objects", "[alscriptexplorermodel]")
    {
        const LLUUID chair = object(10, "Chair");
        const LLUUID table = object(20, "Table", 3);
        const LLUUID lamp  = object(30, "Lamp");
        LLSD         state;
        state["pinned"][0]["id"]   = lamp;
        state["pinned"][0]["name"] = "Lamp";
        state["pinned"][1]["id"]   = id(90);
        state["pinned"][1]["name"] = "Gone";
        state["pinned"][2]["id"]   = id(91);
        model.readState(state);
        selected = { chair, lamp };
        open     = { id(21) };
        list();
        REQUIRE(names() == std::string("Lamp* Gone(away)* (unnamed)(away)* Chair Table"));
        {
            INFO("an object by a prim of it, listed by its root");
            CHECK(model.objects().back().root == table);
            CHECK(model.objects().back().prims.size() == 3);
        }
        CHECK(model.listed(id(22)));
        CHECK_FALSE(model.listed(id(90)));
        {
            INFO("what coming or going concerns: a listed prim, a pin away");
            CHECK(model.concerns(id(22)));
            CHECK(model.concerns(id(90)));
        }
        UNSCOPED_INFO("not what it neither lists nor asked for");
        CHECK_FALSE(model.concerns(id(55)));

        // Across sessions, the pins as they were read.
        LLSD kept;
        model.saveState(kept);
        REQUIRE(kept["pinned"].size() == size_t(3));
        CHECK(kept["pinned"][1]["name"].asString() == std::string("Gone"));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: what a prim holds is kept through a relisting and an unfetched answer", "[alscriptexplorermodel]")
    {
        const LLUUID chair = object(10, "Chair");
        selected           = { chair };
        list();
        const std::vector<Model::Row> unknown = model.rows(std::string());
        REQUIRE_FALSE(unknown.empty());
        UNSCOPED_INFO("what a prim holds not known yet, a folder of it may still be opened");
        CHECK_FALSE(unknown.front().known);
        holds(chair, { "sit.lsl", "readme" });
        const std::vector<Model::Row> known = model.rows(std::string());
        REQUIRE_FALSE(known.empty());
        CHECK(known.front().known);
        CHECK(rows() == std::string("O:Chair I:sit.lsl I:readme"));
        list();
        UNSCOPED_INFO("kept through a listing");
        CHECK(rows() == std::string("O:Chair I:sit.lsl I:readme"));
        holds(chair, {}, false);
        UNSCOPED_INFO("no answer is not an empty prim");
        CHECK(rows() == std::string("O:Chair I:sit.lsl I:readme"));
        holds(chair, { "sit.lsl" });
        UNSCOPED_INFO("an answer is");
        CHECK(rows() == std::string("O:Chair I:sit.lsl"));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: two or more empty prims besides the root fold under one row at the end", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 5);
        selected           = { house };
        list();
        holds(id(10), {});
        holds(id(11), { "door.lsl" });
        holds(id(12), {});
        holds(id(13), {});
        UNSCOPED_INFO("the root, empty, stays first; the two others holding nothing go under one row, folded; one not answered stays");
        REQUIRE(rows() ==
                      std::string("O:House P:House P:House.1 I:door.lsl P:House.4 E:2+ P:House.2 P:House.3"));
        const std::vector<Model::Row> all   = model.rows(std::string());
        const Model::Row&             group = all[all.size() - 3];
        {
            INFO("the row of them is of no prim to act on or drop into");
            CHECK_FALSE(Model::Choice::of(group.value));
            CHECK(Model::primOf(group.value).isNull());
        }
        UNSCOPED_INFO("opened, it asks only to be filled again");
        CHECK(model.foldRow(group.value) == Model::Refold::Refill);
        CHECK(rows() == std::string("O:House P:House P:House.1 I:door.lsl P:House.4 E:2 P:House.2 P:House.3"));
        {
            INFO("folded again");
            CHECK(model.foldEmpties(house, true) == Model::Refold::Refill);
            CHECK(model.foldEmpties(house, true) == Model::Refold::None);
        }

        holds(id(14), { "light.lsl" });
        holds(id(13), { "bell.lsl" });
        UNSCOPED_INFO("one alone holding nothing is not put under a row");
        CHECK(rows() ==
                      std::string("O:House P:House P:House.1 I:door.lsl P:House.2 P:House.3 I:bell.lsl P:House.4 I:light.lsl"));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: a filter shows matches with what holds them and no row of empty prims", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 4);
        const LLUUID chair = object(20, "Chair");
        selected           = { house, chair };
        list();
        holds(id(10), { "main.lsl" });
        holds(id(11), { "door.lsl", "notes" });
        holds(id(12), {});
        holds(id(13), {});
        holds(chair, { "sit.lsl" });
        model.fold(house, false, true);
        UNSCOPED_INFO("folded, what the object holds is still listed, for the tree to show opened");
        CHECK(rows() ==
                      std::string("O:House+ P:House I:main.lsl P:House.1 I:door.lsl I:notes E:2+ P:House.2 P:House.3 O:Chair I:sit.lsl"));
        UNSCOPED_INFO("an item by its name, and what holds it, unfolded");
        CHECK(rows("DOOR") == std::string("O:House P:House.1 I:door.lsl"));
        UNSCOPED_INFO("a prim by its name, with all it holds");
        CHECK(rows("house.1") == std::string("O:House P:House.1 I:door.lsl I:notes"));
        UNSCOPED_INFO("an object by its name, with every prim, the empty ones by name");
        CHECK(rows("house") ==
                      std::string("O:House P:House I:main.lsl P:House.1 I:door.lsl I:notes P:House.2 P:House.3"));
        CHECK(rows("zzz") == std::string());
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: a large linkset's prims start folded and are asked only when filtered", "[alscriptexplorermodel]")
    {
        const size_t count = Model::LARGE_LINKSET + 4;
        const LLUUID big   = object(100, "Big", count);
        selected           = { big };
        open               = { id(105) };
        list();
        std::vector<LLUUID> asked = ask(false, false);
        UNSCOPED_INFO("the root and the one open");
        REQUIRE(asked.size() == size_t(2));
        const std::vector<Model::Row> all = model.rows(std::string());
        const Model::Row*             one = rowNamed(all, "Big.1");
        {
            INFO("a folded prim is listed, folded, what it holds not known");
            REQUIRE(one);
            CHECK(one->folded);
            CHECK_FALSE(one->known);
        }
        UNSCOPED_INFO("the linkset's own row is known: its prims are");
        CHECK(all.front().known);
        CHECK(asked[0] == id(100));
        CHECK(asked[1] == id(105));
        UNSCOPED_INFO("asked, not again until answered");
        CHECK(ask(false, false).empty());
        UNSCOPED_INFO("a filter looks through the rest");
        CHECK(ask(false, true).size() == count - 2);
        UNSCOPED_INFO("a refetch asks them all again");
        CHECK(ask(true, true).size() == count);

        holds(id(100), { "core.lsl" });
        UNSCOPED_INFO("answered and current: not asked");
        CHECK(ask(false, false).empty());
        stale.insert(id(100));
        std::vector<LLUUID> again = ask(false, false);
        {
            INFO("answered, but changed since: asked again");
            REQUIRE(again.size() == 1);
            CHECK(again[0] == id(100));
        }

        // Opened, a folded prim asks to be listed again, for what it holds.
        UNSCOPED_INFO("unfolded");
        CHECK(model.fold(id(101), true, false) == Model::Refold::Relist);
        holds(id(101), {});
        list();
        UNSCOPED_INFO("through a listing again, one opened stays open");
        CHECK(model.fold(id(101), true, false) == Model::Refold::None);
        UNSCOPED_INFO("and the rest stay folded");
        CHECK(model.fold(id(102), true, true) == Model::Refold::None);
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: a new item opens once listed and whether a script runs is asked once", "[alscriptexplorermodel]")
    {
        const LLUUID chair = object(10, "Chair");
        selected           = { chair };
        list();
        model.openWhenListed(chair, LLUUID::null, "new.lsl", std::string("default {}"));
        ALScriptContents contents;
        contents.prim    = chair;
        contents.fetched = true;
        contents.items.push_back({ id(500), "old.lsl", true, false });
        Model::Heard heard = feed(contents);
        {
            INFO("not listed yet, nothing opens");
            CHECK(heard.listed);
            CHECK(heard.opening.empty());
        }
        UNSCOPED_INFO("the script asked whether it runs");
        CHECK(askedRunning == std::vector<ALScriptRef>{ ALScriptRef(chair, id(500)) });
        index.running(ALScriptRef(chair, id(500)), true);
        contents.items.push_back({ id(501), "new.lsl", true, false });
        heard = feed(contents);
        {
            INFO("listed, opened with its text");
            REQUIRE(heard.opening.size() == 1);
            CHECK(heard.opening[0].ref == ALScriptRef(chair, id(501)));
            CHECK(heard.opening[0].text == std::optional<std::string>("default {}"));
        }
        {
            INFO("the one known to run not asked again");
            REQUIRE(askedRunning.size() == 2);
            CHECK(askedRunning[1].item == id(501));
        }
        heard = feed(contents);
        UNSCOPED_INFO("opened once only");
        CHECK(heard.opening.empty());
        UNSCOPED_INFO("a prim not listed is no answer");
        CHECK_FALSE(model.contents(ALScriptContents{ id(77) }).listed);
        {
            INFO("what runs, known");
            CHECK(model.knownRunning(ALScriptRef(chair, id(500))) == std::optional<bool>(true));
            CHECK_FALSE(model.knownRunning(ALScriptRef(chair, id(501))));
        }
        model.forgetRunning();
        UNSCOPED_INFO("let go of for the listed prims, to be asked again");
        CHECK_FALSE(model.knownRunning(ALScriptRef(chair, id(500))));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: chosen rows reach their prims and scripts and count each script once", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 3);
        selected           = { house };
        list();
        holds(id(10), { "a.lsl", "b.lsl", "notes" });
        holds(id(11), { "c.lsl" });
        holds(id(12), {});
        const std::vector<Model::Row> all   = model.rows(std::string());
        const Model::Choice           whole = require_choice(all, "House");
        {
            INFO("an object row");
            CHECK_FALSE(whole.isItem());
            CHECK_FALSE(whole.primRow);
            CHECK(whole.prim == house);
        }
        UNSCOPED_INFO("every prim of it");
        CHECK(model.containerPrims({ whole }).size() == size_t(3));
        UNSCOPED_INFO("each script once");
        CHECK(model.scriptsReached({ whole }) == 3);

        const Model::Choice prim = require_choice(all, "House.1");
        const Model::Choice c    = require_choice(all, "c.lsl");
        const Model::Choice a    = require_choice(all, "a.lsl");
        {
            INFO("a prim row");
            CHECK(prim.primRow);
            CHECK(prim.prim == id(11));
        }
        const auto prims = model.containerPrims({ prim, c, a });
        {
            INFO("the prim itself");
            REQUIRE(prims.size() == 1);
            CHECK(prims[0].first == id(11));
            CHECK(prims[0].second == "House.1");
        }
        {
            INFO("its script is the queue's");
            CHECK(Model::walkedByQueue(c, prims));
            CHECK_FALSE(Model::walkedByQueue(a, prims));
        }
        UNSCOPED_INFO("counted once");
        CHECK(model.scriptsReached({ prim, c, a }) == 2);
        const Model::Choice notes = require_choice(all, "notes");
        UNSCOPED_INFO("a notecard chosen reaches no script");
        CHECK(model.scriptsReached({ notes }) == 0);
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: a rename shows at once and a root's new name reaches its object's pin", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 2);
        selected           = { house };
        model.togglePinned(house, "House");
        list();
        model.renamed(id(11), "Door");
        REQUIRE(model.objects().size() == size_t(1));
        REQUIRE(model.objects().front().prims.size() == size_t(2));
        CHECK(model.objects().front().prims[1].name == std::string("Door"));
        {
            INFO("a prim is not its object");
            CHECK(model.objects().front().name == "House");
            CHECK_FALSE(model.takePinsChanged());
        }
        model.renamed(house, "Home");
        {
            INFO("the root is");
            CHECK(model.objects().front().name == "Home");
            CHECK(model.takePinsChanged());
            REQUIRE(model.pins().size() == size_t(1));
            CHECK(model.pins().front().name == "Home");
        }
        UNSCOPED_INFO("told once");
        CHECK_FALSE(model.takePinsChanged());
        UNSCOPED_INFO("heard as it is, nothing changes");
        CHECK_FALSE(model.rereadNames([](const LLUUID& prim) { return prim == alscriptexplorermodel_data::id(10) ? std::string("Home") : std::string(); }));
        UNSCOPED_INFO("heard otherwise, it does");
        CHECK(model.rereadNames([](const LLUUID& prim) { return prim == alscriptexplorermodel_data::id(10) ? std::string("Villa") : std::string(); }));
        {
            INFO("the pin with it");
            CHECK(model.nameOf(house) == "Villa");
            CHECK(model.takePinsChanged());
            CHECK(model.pins().front().name == "Villa");
        }
        {
            INFO("a name asked once while listed");
            CHECK(model.askName(id(11)));
            CHECK_FALSE(model.askName(id(11)));
            CHECK_FALSE(model.askName(id(99)));
        }
        model.renamed(id(11), "Gate");
        UNSCOPED_INFO("renamed, asked again");
        CHECK(model.askName(id(11)));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: folding objects and revealing prims and pinning from the rows", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 2);
        const LLUUID chair = object(20, "Chair");
        selected           = { house, chair };
        list();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        holds(chair, {});
        CHECK(model.fold(house, false) == Model::Refold::Refill);
        CHECK(model.folded(house));
        CHECK(rows() == std::string("O:House+ P:House I:a.lsl P:House.1 I:b.lsl O:Chair"));
        {
            INFO("a prim revealed unfolds what holds it, and was asked what it holds");
            CHECK_FALSE(model.unfoldTo(id(11)));
            CHECK_FALSE(model.folded(house));
        }
        UNSCOPED_INFO("opened");
        CHECK(model.fold(house, false, false) == Model::Refold::None);
        model.fold(house, false, true);
        UNSCOPED_INFO("opened again asks to be listed");
        CHECK(model.fold(house, false) == Model::Refold::Relist);

        // Pinned from the rows: every object among them, as the first is not.
        const std::vector<Model::Row> all = model.rows(std::string());
        model.pin({ require_choice(all, "House"), require_choice(all, "b.lsl"), require_choice(all, "Chair") });
        {
            INFO("both pinned, each once");
            CHECK(model.pins().size() == 2);
            CHECK(model.isPinned(house));
            CHECK(model.isPinned(chair));
        }
        model.pin({ require_choice(all, "Chair") });
        {
            INFO("let go");
            CHECK(model.pins().size() == 1);
            CHECK_FALSE(model.isPinned(chair));
        }
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: a prim dropped into is asked again of the region until let go of", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 2);
        selected           = { house };
        list();
        UNSCOPED_INFO("both asked first");
        CHECK(ask(false, false).size() == size_t(2));
        holds(id(10), { "a.lsl" });
        holds(id(11), {});
        UNSCOPED_INFO("known and current: nothing asked");
        CHECK(ask(false, false).empty());
        model.askRegion(id(11));
        UNSCOPED_INFO("the one dropped into, though its object says current");
        REQUIRE(ask(false, false) == std::vector<LLUUID>({ id(11) }));
        {
            INFO("and of the region");
            CHECK(model.asksRegion(id(11)));
            CHECK_FALSE(model.asksRegion(id(10)));
            CHECK(asks.back().second);
        }
        holds(id(11), { "b.lsl" });
        UNSCOPED_INFO("still, for the ask a moment later");
        CHECK(ask(false, false) == std::vector<LLUUID>({ id(11) }));
        holds(id(11), { "b.lsl" });
        model.doneAskingRegion();
        {
            INFO("let go of: as any other");
            CHECK(ask(false, false).empty());
            CHECK_FALSE(model.asksRegion(id(11)));
        }
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: folds of an object no longer listed reset and prims are found by id", "[alscriptexplorermodel]")
    {
        const size_t count = Model::LARGE_LINKSET + 2;
        const LLUUID big   = object(200, "Big", count);
        selected           = { big };
        list();
        UNSCOPED_INFO("a prim of a large linkset folded at first");
        CHECK(model.fold(id(201), true, true) == Model::Refold::None);
        UNSCOPED_INFO("opened");
        CHECK(model.fold(id(201), true, false) == Model::Refold::Relist);
        UNSCOPED_INFO("folded shut");
        CHECK(model.fold(big, false, true) == Model::Refold::Refill);
        list();
        UNSCOPED_INFO("open through a listing again");
        CHECK(model.fold(id(201), true, false) == Model::Refold::None);
        UNSCOPED_INFO("and the object shut");
        CHECK(model.folded(big));

        selected.clear();
        list();
        UNSCOPED_INFO("not listed: nothing folded");
        CHECK_FALSE(model.folded(big));
        selected = { big };
        list();
        UNSCOPED_INFO("back: its prims folded as at first");
        CHECK(model.fold(id(201), true, true) == Model::Refold::None);
        UNSCOPED_INFO("the object open");
        CHECK_FALSE(model.folded(big));

        // By id, wherever it is in the list.
        UNSCOPED_INFO("a prim revealed, never asked what it holds, is to be asked");
        CHECK(model.unfoldTo(id(205)));
        UNSCOPED_INFO("unfolded to");
        CHECK(model.fold(id(205), true, false) == Model::Refold::None);
        model.renamed(id(203), "Middle");
        UNSCOPED_INFO("renamed in place");
        CHECK(rowNamed(model.rows(std::string()), "Middle") != nullptr);
        UNSCOPED_INFO("named by its root");
        CHECK(model.nameOf(big) == std::string("Big"));
        CHECK(model.present(big));
        CHECK_FALSE(model.present(id(203)));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: link numbers and the permissions of items and prims", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House", 3);
        world[house].prims[2].modify = false;
        const LLUUID lone = object(40, "Lone");
        selected = { house, lone };
        list();
        ask(false, false);
        ALScriptContents contents;
        contents.prim    = id(11);
        contents.fetched = true;
        ALScriptContents::Item locked;
        locked.id     = id(2000);
        locked.name   = "locked.lsl";
        locked.modify = false;
        locked.copy   = false;
        contents.items.push_back(locked);
        feed(contents);

        const std::vector<Model::Row> all = model.rows(std::string());
        const Model::Row*             hinge = rowNamed(all, "House.1");
        {
            INFO("the second prim is link 2");
            REQUIRE(hinge);
            CHECK(hinge->kind == Model::Row::Kind::Prim);
            CHECK(hinge->link == 2);
        }
        const Model::Row* latch = rowNamed(all, "House.2");
        {
            INFO("the third, 3");
            REQUIRE(latch);
            CHECK(latch->link == 3);
        }
        UNSCOPED_INFO("the root is link 1");
        CHECK(model.linkNumber(house) == 1);
        UNSCOPED_INFO("a prim alone is 0");
        CHECK(model.linkNumber(lone) == 0);
        const Model::Row* item = rowNamed(all, "locked.lsl");
        {
            INFO("no modify, no copy");
            REQUIRE(item);
            CHECK(item->noModify);
            CHECK(item->noCopy);
        }
        {
            INFO("found by its ref");
            REQUIRE(model.itemAt(ALScriptRef(id(11), id(2000))).has_value());
            CHECK_FALSE(model.itemAt(ALScriptRef(id(11), id(2000)))->modify);
        }
        {
            INFO("a prim the agent may not change");
            CHECK_FALSE(model.primModifiable(id(12)));
            CHECK(model.primModifiable(id(11)));
        }
        UNSCOPED_INFO("one not seen, yes");
        CHECK(model.primModifiable(id(99)));
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: a pin keeps its object's last place and region across sessions", "[alscriptexplorermodel]")
    {
        const LLUUID house = object(10, "House");
        world[house].position = LLVector3d(256000.0 + 10.0, 256000.0 + 20.0, 30.0);
        world[house].region   = "Ahern";
        selected              = { house };
        list();
        std::vector<Model::Choice> rows(1);
        rows[0].root = house;
        rows[0].prim = house;
        model.pin(rows);
        const Model::Pin* pin = model.pinOf(house);
        {
            INFO("pinned with its place");
            REQUIRE(pin);
            REQUIRE(pin->position);
            CHECK(pin->position->mdV[VZ] == 30.0);
            CHECK(pin->region == "Ahern");
        }

        // Moved a little: kept as it was; far, or to another region: kept anew.
        world[house].position = LLVector3d(256000.0 + 12.0, 256000.0 + 20.0, 30.0);
        list();
        {
            INFO("a step is no new place");
            CHECK_FALSE(model.takePinsChanged());
            const Model::Pin* stepped = model.pinOf(house);
            REQUIRE(stepped);
            REQUIRE(stepped->position);
            CHECK(stepped->position->mdV[VX] == 256010.0);
        }
        world[house].position = LLVector3d(256000.0 + 100.0, 256000.0 + 20.0, 30.0);
        world[house].region   = "Morris";
        list();
        {
            INFO("far, and elsewhere: kept, and said");
            CHECK(model.takePinsChanged());
            const Model::Pin* moved = model.pinOf(house);
            REQUIRE(moved);
            CHECK(moved->region == "Morris");
        }

        // Across sessions, and away.
        LLSD state;
        model.saveState(state);
        Model again(index);
        again.readState(state);
        {
            INFO("read back");
            REQUIRE(again.pinOf(house));
            CHECK(again.pinOf(house)->position);
            CHECK(again.pinOf(house)->region == "Morris");
        }
        world.clear();
        selected.clear();
        Model::Listing listing;
        listing.seen    = [](const LLUUID&) -> std::optional<Model::Seen> { return std::nullopt; };
        listing.unnamed = "(unnamed)";
        again.list(listing);
        const std::vector<Model::Row> all = again.rows(std::string());
        {
            INFO("listed away, with its region");
            REQUIRE_FALSE(all.empty());
            CHECK_FALSE(all[0].present);
            CHECK(all[0].region == "Morris");
        }
    }

    TEST_CASE_METHOD(alscriptexplorermodel_data, "alscriptexplorermodel: two items of one kind compare and the candidates list same names first", "[alscriptexplorermodel]")
    {
        const LLUUID door  = object(10, "Door", 2);
        const LLUUID lamp  = object(20, "Lamp");
        selected           = { door, lamp };
        list();
        ask(false, false);
        holds(id(10), { "open.lsl", "notes" });
        holds(id(11), { "open.lsl" });
        holds(id(20), { "blink.lsl", "open.lsl", "notes" });
        const auto choiceOf = [this](const LLUUID& prim, const std::string& name) {
            Model::Choice one;
            for (const auto& object : model.objects())
            {
                for (const auto& part : object.prims)
                {
                    if (part.id == prim)
                    {
                        one.root = object.root;
                    }
                }
            }
            one.prim = prim;
            for (const ALScriptContents::Item& item : index.items(prim))
            {
                if (item.name == name)
                {
                    one.item   = item.id;
                    one.name   = item.name;
                    one.script = item.script;
                }
            }
            return one;
        };
        const Model::Choice here   = choiceOf(id(10), "open.lsl");
        const Model::Choice child  = choiceOf(id(11), "open.lsl");
        const Model::Choice notes  = choiceOf(id(10), "notes");
        const Model::Choice there  = choiceOf(id(20), "open.lsl");
        const Model::Choice blink  = choiceOf(id(20), "blink.lsl");
        Model::Choice       prim;
        prim.root = door;
        prim.prim = id(11);

        const auto pair = Model::comparing({ here, there });
        {
            INFO("two scripts, in their order");
            REQUIRE(pair);
            CHECK(pair->first.ref() == here.ref());
            CHECK(pair->second.ref() == there.ref());
        }
        UNSCOPED_INFO("not a script and a notecard");
        CHECK_FALSE(Model::comparing({ here, notes }));
        UNSCOPED_INFO("not one");
        CHECK_FALSE(Model::comparing({ here }));
        UNSCOPED_INFO("not three");
        CHECK_FALSE(Model::comparing({ here, there, blink }));
        UNSCOPED_INFO("not a prim");
        CHECK_FALSE(Model::comparing({ here, prim }));
        UNSCOPED_INFO("not the same twice");
        CHECK_FALSE(Model::comparing({ here, here }));

        const std::vector<Model::Choice> others = model.comparableWith(here);
        std::string                      said;
        for (const Model::Choice& one : others)
        {
            said += (said.empty() ? "" : " ") + model.placeOf(one) + "/" + one.name;
        }
        UNSCOPED_INFO("scripts only, its name first, not itself");
        CHECK(said ==
                      "Door" + std::string(Model::PLACE_SEPARATOR) + "Door.1/open.lsl Lamp/open.lsl Lamp/blink.lsl");
        UNSCOPED_INFO("a notecard's are notecards");
        CHECK(model.comparableWith(notes).size() == 1U);
        UNSCOPED_INFO("a prim compares with nothing");
        CHECK(model.comparableWith(prim).empty());
        UNSCOPED_INFO("a root's place is its object");
        CHECK(model.placeOf(here) == std::string("Door"));
        UNSCOPED_INFO("a child's has its prim");
        CHECK(model.placeOf(child) == "Door" + std::string(Model::PLACE_SEPARATOR) + "Door.1");
    }
}
