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

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    struct alscriptstudiocommands_data
    {
        ALScriptStudioCommands commands;
        // What the commands did, in order.
        std::vector<std::string> done;
    };

    TEST_CASE_METHOD(alscriptstudiocommands_data, "alscriptstudiocommands: a command runs by name and reports enabled and checked state", "[alscriptstudiocommands]")
    {
        bool can  = false;
        bool wrap = true;
        UNSCOPED_INFO("registered");
        CHECK(commands.add("save", [this]() { done.push_back("save"); }, [&can]() { return can; }));
        UNSCOPED_INFO("and a toggle");
        CHECK(commands.add("word_wrap", [&wrap]() { wrap = !wrap; }, nullptr, [&wrap]() { return wrap; }));
        UNSCOPED_INFO("and one saying nothing of either");
        CHECK(commands.add("close", [this]() { done.push_back("close"); }));

        REQUIRE(commands.has("save"));
        REQUIRE(commands.has("word_wrap"));
        REQUIRE(commands.has("close"));
        UNSCOPED_INFO("not enabled while it says it cannot");
        CHECK_FALSE(commands.enabled("save"));
        can = true;
        UNSCOPED_INFO("enabled once it can");
        CHECK(commands.enabled("save"));
        UNSCOPED_INFO("one saying nothing, always");
        CHECK(commands.enabled("close"));
        commands.run("save");
        commands.run("close");
        CHECK(done == std::vector<std::string>{ "save", "close" });

        UNSCOPED_INFO("the toggle on");
        CHECK(commands.checked("word_wrap"));
        commands.run("word_wrap");
        {
            INFO("and off");
            CHECK_FALSE(commands.checked("word_wrap"));
            CHECK_FALSE(wrap);
        }
        UNSCOPED_INFO("no toggle is off");
        CHECK_FALSE(commands.checked("save"));
        UNSCOPED_INFO("nothing unknown asked");
        REQUIRE(commands.unknownAsked().empty());
    }

    TEST_CASE_METHOD(alscriptstudiocommands_data, "alscriptstudiocommands: a name registers once and keeps its first command", "[alscriptstudiocommands]")
    {
        CHECK(commands.add("save", [this]() { done.push_back("first"); }));
        UNSCOPED_INFO("the second refused");
        CHECK_FALSE(commands.add("save", [this]() { done.push_back("second"); }));
        UNSCOPED_INFO("unlisted the same");
        CHECK_FALSE(commands.addUnlisted("save", [this]() { done.push_back("third"); }));
        commands.run("save");
        UNSCOPED_INFO("the first's");
        REQUIRE(done == std::vector<std::string>{ "first" });
        UNSCOPED_INFO("an unlisted one is a command like any");
        CHECK(commands.addUnlisted("reveal", [this]() { done.push_back("reveal"); }));
        commands.run("reveal");
        CHECK(done.back() == std::string("reveal"));
        std::vector<std::string> names = commands.names();
        std::sort(names.begin(), names.end());
        UNSCOPED_INFO("every name, once");
        CHECK(names == std::vector<std::string>{ "reveal", "save" });
    }

    TEST_CASE_METHOD(alscriptstudiocommands_data, "alscriptstudiocommands: an unknown name does nothing and is reported once", "[alscriptstudiocommands]")
    {
        commands.run("no_such");
        CHECK_FALSE(commands.enabled("no_such"));
        CHECK_FALSE(commands.checked("no_such"));
        UNSCOPED_INFO("not done by a key");
        CHECK_FALSE(commands.runIfEnabled("no_such"));
        REQUIRE_FALSE(commands.has("no_such"));
        UNSCOPED_INFO("said once however often asked");
        CHECK(commands.unknownAsked() == std::vector<std::string>{ "no_such" });
        commands.enabled("another");
        UNSCOPED_INFO("and another once more");
        CHECK(commands.unknownAsked() == std::vector<std::string>{ "no_such", "another" });
    }

    TEST_CASE_METHOD(alscriptstudiocommands_data, "alscriptstudiocommands: a key or an ex command does a command only where it can be done now", "[alscriptstudiocommands]")
    {
        bool can = false;
        commands.add("format", [this]() { done.push_back("format"); }, [&can]() { return can; });
        {
            INFO("not while it cannot");
            CHECK_FALSE(commands.runIfEnabled("format"));
            REQUIRE(done.empty());
        }
        can = true;
        {
            INFO("once it can");
            CHECK(commands.runIfEnabled("format"));
            CHECK(done == std::vector<std::string>{ "format" });
        }
    }

    TEST_CASE_METHOD(alscriptstudiocommands_data, "alscriptstudiocommands: a command may register others as it runs", "[alscriptstudiocommands]")
    {
        commands.add("first", [this]() {
            // Enough to move the table's own storage about.
            for (int i = 0; i < 64; ++i)
            {
                commands.add("made_" + std::to_string(i), [this]() { done.push_back("made"); });
            }
            done.push_back("first");
        });
        commands.run("first");
        UNSCOPED_INFO("ran to its end");
        REQUIRE(done == std::vector<std::string>{ "first" });
        commands.run("made_63");
        UNSCOPED_INFO("and what it made runs");
        CHECK(done.back() == std::string("made"));
    }
}
