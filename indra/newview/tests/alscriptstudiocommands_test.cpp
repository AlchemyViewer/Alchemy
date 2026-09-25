/**
 * @file alscriptstudiocommands_test.cpp
 * @brief Script Studio's commands by name: what each does, whether it can now, and whether it is on.
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

#include "../alscriptstudiocommands.h"

#include "../test/lltut.h"

#include <algorithm>

namespace tut
{
    struct alscriptstudiocommands_data
    {
        ALScriptStudioCommands commands;
        // What the commands did, in order.
        std::vector<std::string> done;
    };

    typedef test_group<alscriptstudiocommands_data> alscriptstudiocommands_group;
    typedef alscriptstudiocommands_group::object    alscriptstudiocommands_object;
    alscriptstudiocommands_group                    alscriptstudiocommands_instance("alscriptstudiocommands");

    template<> template<>
    void alscriptstudiocommands_object::test<1>()
    {
        set_test_name("a command run by name, enabled where it says so or says nothing, and on where it is a toggle that is");
        bool can  = false;
        bool wrap = true;
        ensure("registered", commands.add("save", [this]() { done.push_back("save"); }, [&can]() { return can; }));
        ensure("and a toggle", commands.add("word_wrap", [&wrap]() { wrap = !wrap; }, nullptr, [&wrap]() { return wrap; }));
        ensure("and one saying nothing of either", commands.add("close", [this]() { done.push_back("close"); }));

        ensure("known", commands.has("save") && commands.has("word_wrap") && commands.has("close"));
        ensure("not enabled while it says it cannot", !commands.enabled("save"));
        can = true;
        ensure("enabled once it can", commands.enabled("save"));
        ensure("one saying nothing, always", commands.enabled("close"));
        commands.run("save");
        commands.run("close");
        ensure("each run", done == std::vector<std::string>{ "save", "close" });

        ensure("the toggle on", commands.checked("word_wrap"));
        commands.run("word_wrap");
        ensure("and off", !commands.checked("word_wrap") && !wrap);
        ensure("no toggle is off", !commands.checked("save"));
        ensure("nothing unknown asked", commands.unknownAsked().empty());
    }

    template<> template<>
    void alscriptstudiocommands_object::test<2>()
    {
        set_test_name("a name is registered once, and keeps what it was registered with first");
        ensure("the first", commands.add("save", [this]() { done.push_back("first"); }));
        ensure("the second refused", !commands.add("save", [this]() { done.push_back("second"); }));
        ensure("unlisted the same", !commands.addUnlisted("save", [this]() { done.push_back("third"); }));
        commands.run("save");
        ensure("the first's", done == std::vector<std::string>{ "first" });
        ensure("an unlisted one is a command like any", commands.addUnlisted("reveal", [this]() { done.push_back("reveal"); }));
        commands.run("reveal");
        ensure_equals("run", done.back(), std::string("reveal"));
        std::vector<std::string> names = commands.names();
        std::sort(names.begin(), names.end());
        ensure("every name, once", names == std::vector<std::string>{ "reveal", "save" });
    }

    template<> template<>
    void alscriptstudiocommands_object::test<3>()
    {
        set_test_name("a name nothing registered does nothing, cannot be done, is off, and is said once");
        commands.run("no_such");
        ensure("not enabled", !commands.enabled("no_such"));
        ensure("off", !commands.checked("no_such"));
        ensure("not done by a key", !commands.runIfEnabled("no_such"));
        ensure("nor known", !commands.has("no_such"));
        ensure("said once however often asked", commands.unknownAsked() == std::vector<std::string>{ "no_such" });
        commands.enabled("another");
        ensure("and another once more", commands.unknownAsked() == std::vector<std::string>{ "no_such", "another" });
    }

    template<> template<>
    void alscriptstudiocommands_object::test<4>()
    {
        set_test_name("a key or an ex command does a command only where it can be done now");
        bool can = false;
        commands.add("format", [this]() { done.push_back("format"); }, [&can]() { return can; });
        ensure("not while it cannot", !commands.runIfEnabled("format") && done.empty());
        can = true;
        ensure("once it can", commands.runIfEnabled("format") && done == std::vector<std::string>{ "format" });
    }

    template<> template<>
    void alscriptstudiocommands_object::test<5>()
    {
        set_test_name("a command may register others as it runs");
        commands.add("first", [this]() {
            // Enough to move the table's own storage about.
            for (int i = 0; i < 64; ++i)
            {
                commands.add("made_" + std::to_string(i), [this]() { done.push_back("made"); });
            }
            done.push_back("first");
        });
        commands.run("first");
        ensure("ran to its end", done == std::vector<std::string>{ "first" });
        commands.run("made_63");
        ensure_equals("and what it made runs", done.back(), std::string("made"));
    }
}
