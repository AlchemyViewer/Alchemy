/**
 * @file alscriptcontentsindex_test.cpp
 * @brief What each prim holds, as one index for every studio window, over a fake of the world.
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

#include "../alscriptcontentsindex.h"

#include "../test/lltut.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tut
{
    struct alscriptcontentsindex_data
    {
        typedef ALScriptContentsIndex Index;

        // The world: each object by its root, its prims the root first;
        // the prims whose object's copy is current; the asks not answered
        // yet, in order, and every ask made; the scripts asked whether they
        // run.
        std::map<LLUUID, std::vector<LLUUID>>                                            objects;
        std::set<LLUUID>                                                                 current;
        std::vector<std::pair<LLUUID, std::function<void(const Index::Contents&)>>>       pending;
        std::vector<std::pair<LLUUID, bool>>                                             asked;
        std::vector<ALScriptRef>                                                         askedRunning;
        // Each prim's serial as its object has it, which an answer carries.
        std::map<LLUUID, S32>                                                            serials;
        Index                                                                            index;

        alscriptcontentsindex_data() : index(world()) {}

        Index::World world()
        {
            Index::World out;
            out.linkset = [this](const LLUUID& any) {
                for (const auto& [root, prims] : objects)
                {
                    for (const LLUUID& prim : prims)
                    {
                        if (prim == any)
                        {
                            return prims;
                        }
                    }
                }
                return std::vector<LLUUID>();
            };
            out.current = [this](const LLUUID& prim) { return current.contains(prim); };
            out.serial  = [this](const LLUUID& prim) { return serials.contains(prim) ? serials[prim] : -1; };
            out.ask     = [this](const LLUUID& prim, bool from_region, std::function<void(const Index::Contents&)> told) {
                asked.emplace_back(prim, from_region);
                pending.emplace_back(prim, std::move(told));
            };
            out.askRunning = [this](const ALScriptRef& ref) { askedRunning.push_back(ref); };
            return out;
        }

        static LLUUID id(U32 n)
        {
            LLUUID out;
            out.mData[0]  = static_cast<U8>(n);
            out.mData[1]  = static_cast<U8>(n >> 8);
            out.mData[15] = 2;
            return out;
        }

        // An object of `prims` prims in sight, numbered from its root.
        LLUUID object(U32 root, size_t prims = 1)
        {
            std::vector<LLUUID> all;
            for (size_t i = 0; i < prims; ++i)
            {
                all.push_back(id(root + static_cast<U32>(i)));
            }
            objects[id(root)] = all;
            return id(root);
        }

        // The pending ask of a prim answered: what it holds, by names, or
        // no answer at all.
        bool answer(const LLUUID& prim, std::vector<std::string> names, bool fetched = true, const std::string& name = std::string())
        {
            for (auto at = pending.begin(); at != pending.end(); ++at)
            {
                if (at->first != prim)
                {
                    continue;
                }
                Index::Contents contents;
                contents.prim    = prim;
                contents.fetched = fetched;
                contents.name    = name;
                contents.serial  = fetched && serials.contains(prim) ? serials[prim] : -1;
                U32 n            = 500 + static_cast<U32>(prim.mData[0]) * 20;
                for (const std::string& one : names)
                {
                    Index::Item item;
                    item.id     = id(n++);
                    item.name   = one;
                    item.script = one.ends_with(".lsl");
                    contents.items.push_back(item);
                }
                auto told = std::move(at->second);
                pending.erase(at);
                told(contents);
                return true;
            }
            return false;
        }

        std::string names(const LLUUID& prim) const
        {
            std::string out;
            for (const Index::Item& item : index.items(prim))
            {
                out += (out.empty() ? "" : " ") + item.name;
            }
            return out;
        }
    };
    typedef test_group<alscriptcontentsindex_data> alscriptcontentsindex_group;
    typedef alscriptcontentsindex_group::object    alscriptcontentsindex_object;
    tut::alscriptcontentsindex_group               alscriptcontentsindex_instance("alscriptcontentsindex");

    template<> template<>
    void alscriptcontentsindex_object::test<1>()
    {
        set_test_name("a prim is asked once however many ask, every one hears the answer, and one known and current is not asked again");
        const LLUUID chair = object(10);
        S32          heard = 0;
        index.onHeard([&heard, chair](const Index::Contents& contents) { heard += contents.prim == chair; });
        ensure("asked", index.ask(chair));
        ensure("not again while it is not answered", !index.ask(chair));
        ensure("asking", index.asking(chair));
        ensure_equals(asked.size(), size_t(1));
        ensure("not known yet", !index.fetched(chair) && index.prim(chair) == nullptr);
        ensure("answered", answer(chair, { "sit.lsl", "readme" }, true, "Chair"));
        ensure_equals("heard once", heard, 1);
        ensure_equals(names(chair), std::string("sit.lsl readme"));
        ensure_equals("its name", index.prim(chair)->name, std::string("Chair"));
        ensure("no longer asking", !index.asking(chair));

        // Known: asked again only where the object's copy is not current,
        // where asked again by a person, or of the region.
        ensure("not current, asked", index.ask(chair) && answer(chair, { "sit.lsl" }));
        current.insert(chair);
        ensure("current, not asked", !index.ask(chair));
        ensure("a refetch is", index.ask(chair, true) && answer(chair, { "sit.lsl" }));
        ensure("and of the region", index.ask(chair, false, true) && asked.back().second);
        ensure("a refetch while asked asks again", index.ask(chair, true));
    }

    template<> template<>
    void alscriptcontentsindex_object::test<2>()
    {
        set_test_name("an answer that did not come leaves what was known; each script is asked once whether it runs, until let go of");
        const LLUUID chair = object(10);
        index.ask(chair);
        answer(chair, { "sit.lsl", "pose.lsl", "readme" });
        ensure_equals("each script asked whether it runs", askedRunning.size(), size_t(2));
        index.running(askedRunning[0], true);
        ensure("known", index.running(askedRunning[0]) == std::optional<bool>(true));
        ensure("not known", !index.running(askedRunning[1]));

        index.ask(chair, true);
        answer(chair, {}, false);
        ensure_equals("no answer is not an empty prim", names(chair), std::string("sit.lsl pose.lsl readme"));
        ensure("still fetched", index.fetched(chair));
        ensure_equals("only the one not known asked again", askedRunning.size(), size_t(3));

        index.forgetRunning({ chair });
        ensure("let go of", !index.running(askedRunning[0]));
        index.forget(chair);
        ensure("all of it", index.prim(chair) == nullptr && names(chair).empty());
    }

    template<> template<>
    void alscriptcontentsindex_object::test<3>()
    {
        set_test_name("an object listed whole: every prim asked, told once all have answered, and which could not be listed said");
        const LLUUID house = object(20, 4);
        // A prim known and current is not asked; one asked by someone else
        // is waited for.
        index.ask(id(21));
        answer(id(21), { "door.lsl" });
        current.insert(id(21));
        index.ask(id(22));
        const size_t before = asked.size();

        std::optional<Index::Listed> listed;
        index.ensureListed(id(23), [&listed](const Index::Listed& out) { listed = out; });
        ensure_equals("the root and the one never asked", asked.size() - before, size_t(2));
        ensure("not yet", !listed);
        answer(id(20), { "main.lsl" });
        answer(id(22), { "light.lsl" });
        ensure("not while one is out", !listed);
        answer(id(23), {}, false);
        ensure("told", listed.has_value());
        ensure("by its root", listed->root == house && listed->present);
        ensure_equals("every prim", listed->prims.size(), size_t(4));
        ensure("the one that did not answer said", listed->unlisted == std::vector<LLUUID>{ id(23) });

        // Everything known and current: told at once.
        for (U32 i = 20; i < 24; ++i)
        {
            current.insert(id(i));
        }
        index.ask(id(23), true);
        answer(id(23), {});
        listed.reset();
        index.ensureListed(house, [&listed](const Index::Listed& out) { listed = out; });
        ensure("at once", listed.has_value() && listed->unlisted.empty());

        // Not in sight: no prims, not present, told at once.
        listed.reset();
        index.ensureListed(id(99), [&listed](const Index::Listed& out) { listed = out; });
        ensure("away", listed.has_value() && !listed->present && listed->prims.empty());
    }

    template<> template<>
    void alscriptcontentsindex_object::test<4>()
    {
        set_test_name("a world that answers on the spot answers into the wait, and one told may list again");
        const LLUUID shed = object(30, 2);
        Index::World world;
        world.linkset = [this](const LLUUID& any) { return objects[any]; };
        world.current = [](const LLUUID&) { return false; };
        world.ask     = [](const LLUUID& prim, bool, std::function<void(const Index::Contents&)> told) {
            Index::Contents contents;
            contents.prim    = prim;
            contents.fetched = true;
            told(contents);
        };
        Index spot(std::move(world));
        S32   told = 0;
        spot.ensureListed(shed, [&](const Index::Listed& listed) {
            ++told;
            ensure("all listed", listed.unlisted.empty() && listed.prims.size() == 2);
            if (told == 1)
            {
                spot.ensureListed(shed, [&told](const Index::Listed&) { ++told; });
            }
        });
        ensure_equals("each told once", told, 2);
    }

    template<> template<>
    void alscriptcontentsindex_object::test<5>()
    {
        set_test_name("what a prim holds asked again as the studio draws once it changed -- its serial moved, or its object's copy was let "
                      "go of -- looking once a second, and a prim at most every few seconds");
        const LLUUID chair = object(40);
        current.insert(chair);
        serials[chair] = 3;
        index.ask(chair);
        answer(chair, { "sit.lsl" });
        index.refresh(10.0);
        ensure("unchanged: not asked", pending.empty());

        // A script added from the build tools.
        serials[chair] = 4;
        index.refresh(10.5);
        ensure("not looked at again within the second", pending.empty());
        index.refresh(11.0);
        ensure("its serial moved: asked", pending.size() == 1 && asked.back().first == chair);
        answer(chair, { "sit.lsl", "new.lsl" });
        ensure_equals("what it holds now", names(chair), std::string("sit.lsl new.lsl"));
        index.refresh(12.0);
        ensure("heard at its serial: not asked", pending.empty());

        // Its object's copy let go of, and no answer coming.
        current.erase(chair);
        index.refresh(13.0);
        ensure("not asked again so soon", pending.empty());
        index.refresh(16.0);
        ensure("asked", pending.size() == 1);
        index.refresh(17.0);
        ensure("not while it is asked", pending.size() == 1);
        answer(chair, {}, false);
        ensure_equals("no answer keeps what was known", names(chair), std::string("sit.lsl new.lsl"));
        index.refresh(18.0);
        ensure("not asked again at once", pending.empty());
        index.refresh(21.0);
        ensure("asked again a while after", pending.size() == 1);
        answer(chair, { "sit.lsl" });

        // Asked for directly, a prim that changed is asked though known.
        current.insert(chair);
        ensure("current: not asked", !index.ask(chair));
        serials[chair] = 5;
        ensure("its serial moved: asked", index.ask(chair));
    }
}
