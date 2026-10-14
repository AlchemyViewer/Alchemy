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

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
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

    TEST_CASE_METHOD(alscriptcontentsindex_data, "alscriptcontentsindex: a prim is asked once for all who ask and not again while current", "[alscriptcontentsindex]")
    {
        const LLUUID chair = object(10);
        S32          heard = 0;
        index.onHeard([&heard, chair](const Index::Contents& contents) { heard += contents.prim == chair; });
        CHECK(index.ask(chair));
        UNSCOPED_INFO("not again while it is not answered");
        CHECK_FALSE(index.ask(chair));
        CHECK(index.asking(chair));
        CHECK(asked.size() == size_t(1));
        CHECK_FALSE(index.fetched(chair));
        CHECK(index.prim(chair) == nullptr);
        CHECK(answer(chair, { "sit.lsl", "readme" }, true, "Chair"));
        CHECK(heard == 1);
        CHECK(names(chair) == std::string("sit.lsl readme"));
        REQUIRE(index.prim(chair) != nullptr);
        CHECK(index.prim(chair)->name == std::string("Chair"));
        CHECK_FALSE(index.asking(chair));

        // Known: asked again only where the object's copy is not current,
        // where asked again by a person, or of the region.
        {
            INFO("not current, asked");
            CHECK(index.ask(chair));
            CHECK(answer(chair, { "sit.lsl" }));
        }
        current.insert(chair);
        UNSCOPED_INFO("current, not asked");
        CHECK_FALSE(index.ask(chair));
        {
            INFO("a refetch is asked");
            CHECK(index.ask(chair, true));
            CHECK(answer(chair, { "sit.lsl" }));
        }
        {
            INFO("so is one of the region");
            CHECK(index.ask(chair, false, true));
            CHECK(asked.back().second);
        }
        UNSCOPED_INFO("a refetch while asked asks again");
        CHECK(index.ask(chair, true));
    }

    TEST_CASE_METHOD(alscriptcontentsindex_data, "alscriptcontentsindex: a missed answer keeps the contents and a script's running is asked once", "[alscriptcontentsindex]")
    {
        const LLUUID chair = object(10);
        index.ask(chair);
        answer(chair, { "sit.lsl", "pose.lsl", "readme" });
        UNSCOPED_INFO("each script asked whether it runs");
        REQUIRE(askedRunning.size() == size_t(2));
        index.running(askedRunning[0], true);
        CHECK(index.running(askedRunning[0]) == std::optional<bool>(true));
        UNSCOPED_INFO("one not answered is not known");
        CHECK_FALSE(index.running(askedRunning[1]));

        index.ask(chair, true);
        answer(chair, {}, false);
        UNSCOPED_INFO("no answer is not an empty prim");
        CHECK(names(chair) == std::string("sit.lsl pose.lsl readme"));
        UNSCOPED_INFO("still fetched");
        CHECK(index.fetched(chair));
        UNSCOPED_INFO("only the one not known asked again");
        CHECK(askedRunning.size() == size_t(3));

        index.forgetRunning({ chair });
        UNSCOPED_INFO("whether it runs let go of");
        CHECK_FALSE(index.running(askedRunning[0]));
        index.forget(chair);
        {
            INFO("all of the prim let go of");
            CHECK(index.prim(chair) == nullptr);
            CHECK(names(chair).empty());
        }
    }

    TEST_CASE_METHOD(alscriptcontentsindex_data, "alscriptcontentsindex: ensureListed tells once every prim has answered and names the unlisted", "[alscriptcontentsindex]")
    {
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
        UNSCOPED_INFO("the root and the one never asked");
        CHECK(asked.size() - before == size_t(2));
        UNSCOPED_INFO("not told yet");
        CHECK_FALSE(listed);
        answer(id(20), { "main.lsl" });
        answer(id(22), { "light.lsl" });
        UNSCOPED_INFO("not while one is out");
        CHECK_FALSE(listed);
        answer(id(23), {}, false);
        REQUIRE(listed.has_value());
        {
            INFO("by its root");
            CHECK(listed->root == house);
            CHECK(listed->present);
        }
        UNSCOPED_INFO("every prim");
        CHECK(listed->prims.size() == size_t(4));
        UNSCOPED_INFO("the one that did not answer said");
        CHECK(listed->unlisted == std::vector<LLUUID>{ id(23) });

        // Everything known and current: told at once.
        for (U32 i = 20; i < 24; ++i)
        {
            current.insert(id(i));
        }
        index.ask(id(23), true);
        answer(id(23), {});
        listed.reset();
        index.ensureListed(house, [&listed](const Index::Listed& out) { listed = out; });
        {
            INFO("at once");
            REQUIRE(listed.has_value());
            CHECK(listed->unlisted.empty());
        }

        // Not in sight: no prims, not present, told at once.
        listed.reset();
        index.ensureListed(id(99), [&listed](const Index::Listed& out) { listed = out; });
        {
            INFO("not in sight");
            REQUIRE(listed.has_value());
            CHECK_FALSE(listed->present);
            CHECK(listed->prims.empty());
        }
    }

    TEST_CASE_METHOD(alscriptcontentsindex_data, "alscriptcontentsindex: an answer on the spot reaches the wait and a listener may list again", "[alscriptcontentsindex]")
    {
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
            {
                INFO("all listed");
                REQUIRE(listed.unlisted.empty());
                REQUIRE(listed.prims.size() == 2);
            }
            if (told == 1)
            {
                spot.ensureListed(shed, [&told](const Index::Listed&) { ++told; });
            }
        });
        UNSCOPED_INFO("each told once");
        CHECK(told == 2);
    }

    // The studio's draw looks once a second, and asks a prim at most every
    // few seconds.
    TEST_CASE_METHOD(alscriptcontentsindex_data, "alscriptcontentsindex: refresh asks a prim again once its serial moved or its copy went", "[alscriptcontentsindex]")
    {
        const LLUUID chair = object(40);
        current.insert(chair);
        serials[chair] = 3;
        index.ask(chair);
        answer(chair, { "sit.lsl" });
        index.refresh(10.0);
        UNSCOPED_INFO("unchanged: not asked");
        CHECK(pending.empty());

        // A script added from the build tools.
        serials[chair] = 4;
        index.refresh(10.5);
        UNSCOPED_INFO("not looked at again within the second");
        CHECK(pending.empty());
        index.refresh(11.0);
        {
            INFO("its serial moved: asked");
            REQUIRE(pending.size() == 1);
            CHECK(asked.back().first == chair);
        }
        answer(chair, { "sit.lsl", "new.lsl" });
        UNSCOPED_INFO("what it holds now");
        CHECK(names(chair) == std::string("sit.lsl new.lsl"));
        index.refresh(12.0);
        UNSCOPED_INFO("heard at its serial: not asked");
        CHECK(pending.empty());

        // Its object's copy let go of, and no answer coming.
        current.erase(chair);
        index.refresh(13.0);
        UNSCOPED_INFO("not asked again so soon");
        CHECK(pending.empty());
        index.refresh(16.0);
        UNSCOPED_INFO("asked");
        CHECK(pending.size() == 1);
        index.refresh(17.0);
        UNSCOPED_INFO("not while it is asked");
        CHECK(pending.size() == 1);
        answer(chair, {}, false);
        UNSCOPED_INFO("no answer keeps what was known");
        CHECK(names(chair) == std::string("sit.lsl new.lsl"));
        index.refresh(18.0);
        UNSCOPED_INFO("not asked again at once");
        CHECK(pending.empty());
        index.refresh(21.0);
        UNSCOPED_INFO("asked again a while after");
        CHECK(pending.size() == 1);
        answer(chair, { "sit.lsl" });

        // Asked for directly, a prim that changed is asked though known.
        current.insert(chair);
        UNSCOPED_INFO("current: not asked");
        CHECK_FALSE(index.ask(chair));
        serials[chair] = 5;
        UNSCOPED_INFO("its serial moved: asked");
        CHECK(index.ask(chair));
    }
}
