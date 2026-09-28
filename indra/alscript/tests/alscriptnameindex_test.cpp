/**
 * @file alscriptnameindex_test.cpp
 * @brief Tests for ALScriptNameIndex: built once, then kept from each item said to change.
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

#include "../alscriptnameindex.h"

#include "../test/lltut.h"

#include <map>

namespace tut
{
    struct alscriptnameindex_data
    {
        static LLUUID id(U32 n)
        {
            LLUUID out;
            out.mData[15] = static_cast<U8>(n);
            return out;
        }
    };

    typedef test_group<alscriptnameindex_data> alscriptnameindex_group;
    typedef alscriptnameindex_group::object    alscriptnameindex_object;
    alscriptnameindex_group                    alscriptnameindex_instance("alscriptnameindex");

    template<> template<>
    void alscriptnameindex_object::test<1>()
    {
        set_test_name("built once; an item renamed, added, taken away or moved out of reach changes what its names give, and the generation with it");
        ALScriptNameIndex index;
        ensure("not built, nothing given", !index.built() && index.named("lib").empty());
        index.changed(id(1), std::string("lib"));
        ensure("nothing kept before it is built", index.named("lib").empty());

        index.build({ { id(1), "lib" }, { id(2), "lib" }, { id(3), "door" } });
        ensure("two of a name, in order", index.named("lib") == std::vector<LLUUID>({ id(1), id(2) }) && index.size() == 3);
        U32 generation = index.generation();

        index.changed(id(3), std::string("door"));
        ensure("the same said again: nothing moves", index.generation() == generation);
        index.changed(id(9), std::nullopt);
        ensure("an item never listed gone: nothing moves", index.generation() == generation);

        index.changed(id(2), std::string("util"));
        ensure("renamed: under its new name only", index.named("lib") == std::vector<LLUUID>({ id(1) }) && index.named("util") == std::vector<LLUUID>({ id(2) }));
        ensure("and the generation moved", index.generation() != generation);
        generation = index.generation();

        index.changed(id(4), std::string("lib"));
        ensure("added, after the rest", index.named("lib") == std::vector<LLUUID>({ id(1), id(4) }));
        index.changed(id(1), std::nullopt);
        ensure("taken away, or into the trash", index.named("lib") == std::vector<LLUUID>({ id(4) }) && index.size() == 3);
        index.changed(id(3), std::nullopt);
        ensure("a name with nothing left gives nothing", index.named("door").empty());

        generation = index.generation();
        index.forget();
        ensure("forgotten: built again at the next question", !index.built() && index.generation() != generation);
    }

    template<> template<>
    void alscriptnameindex_object::test<2>()
    {
        set_test_name("every item listed is visited once with its name, as it is now");
        ALScriptNameIndex index;
        index.build({ { id(1), "lib" }, { id(2), "lib" }, { id(3), "door" } });
        index.changed(id(2), std::string("util"));
        index.changed(id(3), std::nullopt);
        std::map<LLUUID, std::string> seen;
        index.forEach([&seen](const LLUUID& item, const std::string& name) { ensure("once each", seen.emplace(item, name).second); });
        ensure_equals("what is listed", seen.size(), size_t(2));
        ensure("by its name now", seen[id(1)] == "lib" && seen[id(2)] == "util");
    }
}
