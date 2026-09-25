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

#include "../test/lltut.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gExplorerTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gExplorerTestAnonName;
}

namespace tut
{
    struct alscriptexplorermodel_data
    {
        typedef ALScriptExplorerModel Model;

        Model model;
        // The world: each object in sight by its root, its prims the root
        // first; what is selected; the prims of the scripts open.
        std::map<LLUUID, Model::Seen> world;
        std::vector<LLUUID>           selected;
        std::vector<LLUUID>           open;

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
            ALScriptWorkspace::Contents contents;
            contents.prim    = prim;
            contents.fetched = fetched;
            U32 n            = 1000;
            for (const std::string& name : names)
            {
                ALScriptWorkspace::Item item;
                item.id     = id(n++ + static_cast<U32>(prim.mData[0]) * 50);
                item.name   = name;
                item.script = name.ends_with(".lsl") || name.ends_with(".luau");
                item.lua    = name.ends_with(".luau");
                contents.items.push_back(item);
            }
            model.contents(contents);
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
    };
    typedef test_group<alscriptexplorermodel_data> alscriptexplorermodel_group;
    typedef alscriptexplorermodel_group::object    alscriptexplorermodel_object;
    tut::alscriptexplorermodel_group               alscriptexplorermodel_instance("alscriptexplorermodel");

    template<> template<>
    void alscriptexplorermodel_object::test<1>()
    {
        set_test_name("the objects in hand, each once: the pinned first, one not in sight by its pin's name, then the selected, then the open scripts' objects");
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
        ensure_equals(names(), std::string("Lamp* Gone(away)* (unnamed)(away)* Chair Table"));
        ensure("an object by a prim of it, listed by its root", model.objects().back().root == table && model.objects().back().prims.size() == 3);
        ensure("listed prims", model.listed(id(22)) && !model.listed(id(90)));

        // Across sessions, the pins as they were read.
        LLSD kept;
        model.saveState(kept);
        ensure_equals(kept["pinned"].size(), size_t(3));
        ensure_equals(kept["pinned"][1]["name"].asString(), std::string("Gone"));
    }

    template<> template<>
    void alscriptexplorermodel_object::test<2>()
    {
        set_test_name("what a prim was known to hold is kept through a listing again, and a listing that did not come leaves it");
        const LLUUID chair = object(10, "Chair");
        selected           = { chair };
        list();
        ensure("what a prim holds not known yet, a folder of it may still be opened", !model.rows(std::string()).front().known);
        holds(chair, { "sit.lsl", "readme" });
        ensure("known", model.rows(std::string()).front().known);
        ensure_equals(rows(), std::string("O:Chair I:sit.lsl I:readme"));
        list();
        ensure_equals("kept through a listing", rows(), std::string("O:Chair I:sit.lsl I:readme"));
        holds(chair, {}, false);
        ensure_equals("no answer is not an empty prim", rows(), std::string("O:Chair I:sit.lsl I:readme"));
        holds(chair, { "sit.lsl" });
        ensure_equals("an answer is", rows(), std::string("O:Chair I:sit.lsl"));
    }

    template<> template<>
    void alscriptexplorermodel_object::test<3>()
    {
        set_test_name("a linkset's prims holding nothing go under one row after the rest -- two or more, never the root -- folded until opened, which takes no drop");
        const LLUUID house = object(10, "House", 5);
        selected           = { house };
        list();
        holds(id(10), {});
        holds(id(11), { "door.lsl" });
        holds(id(12), {});
        holds(id(13), {});
        ensure_equals("the root, empty, stays first; the two others holding nothing go under one row, folded; one not answered stays", rows(),
                      std::string("O:House P:House P:House.1 I:door.lsl P:House.4 E:2+ P:House.2 P:House.3"));
        const std::vector<Model::Row> all   = model.rows(std::string());
        const Model::Row&             group = all[all.size() - 3];
        ensure("the row of them is of no prim to act on or drop into", !Model::Choice::of(group.value) && Model::primOf(group.value).isNull());
        ensure("opened, it asks only to be filled again", model.foldRow(group.value) == Model::Refold::Refill);
        ensure_equals(rows(), std::string("O:House P:House P:House.1 I:door.lsl P:House.4 E:2 P:House.2 P:House.3"));
        ensure("folded again", model.foldEmpties(house, true) == Model::Refold::Refill && model.foldEmpties(house, true) == Model::Refold::None);

        holds(id(14), { "light.lsl" });
        holds(id(13), { "bell.lsl" });
        ensure_equals("one alone holding nothing is not put under a row", rows(),
                      std::string("O:House P:House P:House.1 I:door.lsl P:House.2 P:House.3 I:bell.lsl P:House.4 I:light.lsl"));
    }

    template<> template<>
    void alscriptexplorermodel_object::test<4>()
    {
        set_test_name("a filter shows an item and what holds it, an object or a prim by its name with all it holds, whatever is folded, and no row of prims holding nothing");
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
        ensure_equals("folded, what the object holds is still listed, for the tree to show opened", rows(),
                      std::string("O:House+ P:House I:main.lsl P:House.1 I:door.lsl I:notes E:2+ P:House.2 P:House.3 O:Chair I:sit.lsl"));
        ensure_equals("an item by its name, and what holds it, unfolded", rows("DOOR"), std::string("O:House P:House.1 I:door.lsl"));
        ensure_equals("a prim by its name, with all it holds", rows("house.1"), std::string("O:House P:House.1 I:door.lsl I:notes"));
        ensure_equals("an object by its name, with every prim, the empty ones by name", rows("house"),
                      std::string("O:House P:House I:main.lsl P:House.1 I:door.lsl I:notes P:House.2 P:House.3"));
        ensure_equals("nothing", rows("zzz"), std::string());
    }

    template<> template<>
    void alscriptexplorermodel_object::test<5>()
    {
        set_test_name("a large linkset's prims are folded when first listed, but the root and any with a script open; folded, they are not asked what they hold unless a filter looks");
        const size_t count = Model::LARGE_LINKSET + 4;
        const LLUUID big   = object(100, "Big", count);
        selected           = { big };
        open               = { id(105) };
        list();
        const auto always = [](const LLUUID&) { return true; };
        std::vector<LLUUID> asked = model.toAsk(false, false, always);
        ensure_equals("the root and the one open", asked.size(), size_t(2));
        const std::vector<Model::Row> all = model.rows(std::string());
        const Model::Row*             one = rowNamed(all, "Big.1");
        ensure("a folded prim is listed, folded, what it holds not known", one && one->folded && !one->known);
        ensure("the linkset's own row is known: its prims are", all.front().known);
        ensure("which", asked[0] == id(100) && asked[1] == id(105));
        ensure("asked, not again until answered", model.toAsk(false, false, always).empty());
        ensure_equals("a filter looks through the rest", model.toAsk(false, true, always).size(), count - 2);
        ensure_equals("a refetch asks them all again", model.toAsk(true, true, always).size(), count);

        holds(id(100), { "core.lsl" });
        ensure("answered and current: not asked", model.toAsk(false, false, always).empty());
        const auto changed = [](const LLUUID& prim) { return prim != id(100); };
        std::vector<LLUUID> again = model.toAsk(false, false, changed);
        ensure("answered, but changed since: asked again", again.size() == 1 && again[0] == id(100));

        // Opened, a folded prim asks to be listed again, for what it holds.
        ensure("unfolded", model.fold(id(101), true, false) == Model::Refold::Relist);
        holds(id(101), {});
        list();
        ensure("through a listing again, one opened stays open", model.fold(id(101), true, false) == Model::Refold::None);
        ensure("and the rest stay folded", model.fold(id(102), true, true) == Model::Refold::None);
    }

    template<> template<>
    void alscriptexplorermodel_object::test<6>()
    {
        set_test_name("a new item opens once its prim lists it, by id or by name, with its text; and a script not known to run is asked about once");
        const LLUUID chair = object(10, "Chair");
        selected           = { chair };
        list();
        model.openWhenListed(chair, LLUUID::null, "new.lsl", std::string("default {}"));
        ALScriptWorkspace::Contents contents;
        contents.prim    = chair;
        contents.fetched = true;
        contents.items.push_back({ id(500), "old.lsl", true, false });
        Model::Heard heard = model.contents(contents);
        ensure("not listed yet, nothing opens", heard.listed && heard.opening.empty());
        ensure("the script asked whether it runs", heard.askRunning.size() == 1 && heard.askRunning[0] == ALScriptRef(chair, id(500)));
        model.running(ALScriptRef(chair, id(500)), true);
        contents.items.push_back({ id(501), "new.lsl", true, false });
        heard = model.contents(contents);
        ensure("listed, opened with its text", heard.opening.size() == 1 && heard.opening[0].ref == ALScriptRef(chair, id(501)) &&
                                                   heard.opening[0].text == std::optional<std::string>("default {}"));
        ensure("the one known to run not asked again", heard.askRunning.size() == 1 && heard.askRunning[0].item == id(501));
        heard = model.contents(contents);
        ensure("once", heard.opening.empty());
        ensure("a prim not listed is no answer", !model.contents(ALScriptWorkspace::Contents{ id(77) }).listed);
        ensure("what runs, known", model.knownRunning(ALScriptRef(chair, id(500))) == std::optional<bool>(true) &&
                                       !model.knownRunning(ALScriptRef(chair, id(501))));
        selected.clear();
        list();
        ensure("let go of with its prim", !model.knownRunning(ALScriptRef(chair, id(500))));
    }

    template<> template<>
    void alscriptexplorermodel_object::test<7>()
    {
        set_test_name("what rows chosen reach: an object every prim, a prim itself; a script with its prim is the queue's; each script counted once");
        const LLUUID house = object(10, "House", 3);
        selected           = { house };
        list();
        holds(id(10), { "a.lsl", "b.lsl", "notes" });
        holds(id(11), { "c.lsl" });
        holds(id(12), {});
        const std::vector<Model::Row> all = model.rows(std::string());
        Model::Choice whole = *Model::Choice::of(all.front().value);
        ensure("an object row", !whole.isItem() && !whole.primRow && whole.prim == house);
        ensure_equals("every prim of it", model.containerPrims({ whole }).size(), size_t(3));
        ensure_equals("each script once", model.scriptsReached({ whole }), 3);

        Model::Choice prim = *Model::Choice::of(rowNamed(all, "House.1")->value);
        Model::Choice c    = *Model::Choice::of(rowNamed(all, "c.lsl")->value);
        Model::Choice a    = *Model::Choice::of(rowNamed(all, "a.lsl")->value);
        ensure("a prim row", prim.primRow && prim.prim == id(11));
        const auto prims = model.containerPrims({ prim, c, a });
        ensure("the prim itself", prims.size() == 1 && prims[0].first == id(11) && prims[0].second == "House.1");
        ensure("its script is the queue's", Model::walkedByQueue(c, prims) && !Model::walkedByQueue(a, prims));
        ensure_equals("counted once", model.scriptsReached({ prim, c, a }), 2);
        ensure("a notecard chosen reaches no script", model.scriptsReached({ *Model::Choice::of(rowNamed(all, "notes")->value) }) == 0);
    }

    template<> template<>
    void alscriptexplorermodel_object::test<8>()
    {
        set_test_name("a prim renamed here is called so at once, the root its object and its pin too; names heard change them the same way");
        const LLUUID house = object(10, "House", 2);
        selected           = { house };
        model.togglePinned(house, "House");
        list();
        model.renamed(id(11), "Door");
        ensure_equals(model.objects().front().prims[1].name, std::string("Door"));
        ensure("a prim is not its object", model.objects().front().name == "House" && !model.takePinsChanged());
        model.renamed(house, "Home");
        ensure("the root is", model.objects().front().name == "Home" && model.takePinsChanged() && model.pins().front().name == "Home");
        ensure("told once", !model.takePinsChanged());
        ensure("heard as it is, nothing changes", !model.rereadNames([](const LLUUID& prim) { return prim == id(10) ? std::string("Home") : std::string(); }));
        ensure("heard otherwise, it does", model.rereadNames([](const LLUUID& prim) { return prim == id(10) ? std::string("Villa") : std::string(); }));
        ensure("the pin with it", model.nameOf(house) == "Villa" && model.takePinsChanged() && model.pins().front().name == "Villa");
        ensure("a name asked once while listed", model.askName(id(11)) && !model.askName(id(11)) && !model.askName(id(99)));
        model.renamed(id(11), "Gate");
        ensure("renamed, asked again", model.askName(id(11)));
    }

    template<> template<>
    void alscriptexplorermodel_object::test<9>()
    {
        set_test_name("an object folded is a folded row, opened asks to be listed again, and a prim revealed unfolds what holds it; pins from the rows");
        const LLUUID house = object(10, "House", 2);
        const LLUUID chair = object(20, "Chair");
        selected           = { house, chair };
        list();
        holds(id(10), { "a.lsl" });
        holds(id(11), { "b.lsl" });
        holds(chair, {});
        ensure("folded", model.fold(house, false) == Model::Refold::Refill && model.folded(house));
        ensure_equals(rows(), std::string("O:House+ P:House I:a.lsl P:House.1 I:b.lsl O:Chair"));
        ensure("a prim revealed unfolds what holds it, and was asked what it holds", !model.unfoldTo(id(11)) && !model.folded(house));
        ensure("opened", model.fold(house, false, false) == Model::Refold::None);
        model.fold(house, false, true);
        ensure("opened again asks to be listed", model.fold(house, false) == Model::Refold::Relist);

        // Pinned from the rows: every object among them, as the first is not.
        const std::vector<Model::Row> all = model.rows(std::string());
        model.pin({ *Model::Choice::of(all[0].value), *Model::Choice::of(rowNamed(all, "b.lsl")->value), *Model::Choice::of(rowNamed(all, "Chair")->value) });
        ensure("both pinned, each once", model.pins().size() == 2 && model.isPinned(house) && model.isPinned(chair));
        model.pin({ *Model::Choice::of(rowNamed(all, "Chair")->value) });
        ensure("let go", model.pins().size() == 1 && !model.isPinned(chair));
    }
}
