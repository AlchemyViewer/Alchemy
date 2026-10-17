/**
 * @file alscripttempfiles_test.cpp
 * @brief The copies of scripts an external editor is given: one name, held not owned, swept after a crash.
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

#include "../core/alscripttempfiles.h"

#include "fsyspath.h"
#include "llfile.h"
#include "lluuid.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    struct alscripttempfiles_data
    {
        // A folder of its own for each test, standing for the temp folder,
        // and one under it for the sessions' lists.
        std::string temp;
        std::string lists;

        alscripttempfiles_data()
        {
            temp = fsyspath(std::filesystem::temp_directory_path() / fsyspath("alscripttempfiles_" + LLUUID::generateNewID().asString())).string() + "/";
            lists = temp + "lists/";
            std::error_code ec;
            std::filesystem::create_directories(fsyspath(lists), ec);
        }

        ~alscripttempfiles_data()
        {
            std::error_code ec;
            std::filesystem::remove_all(fsyspath(temp), ec);
        }

        static void put(const std::string& path, const std::string& text = "default {}\n")
        {
            llofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }

        static bool there(const std::string& path) { return LLFile::isfile(path); }
    };

    TEST_CASE_METHOD(alscripttempfiles_data, "alscripttempfiles: a copy's name is the cleaned script name and id and language's extension", "[alscripttempfiles]")
    {
        CHECK(ALScriptTempFiles::nameFor("/tmp", "My Script", "abc", false) == std::string("/tmp/sl_script_My Script_abc.lsl"));
        UNSCOPED_INFO("SLua, the name cleaned");
        CHECK(ALScriptTempFiles::nameFor("/tmp/", "a/b:c?", "abc", true) ==
                      std::string("/tmp/sl_script_abc_abc.luau"));
        UNSCOPED_INFO("nothing left of the name");
        CHECK(ALScriptTempFiles::nameFor("/tmp/", "<>*", "abc", false) == std::string("/tmp/sl_script_abc.lsl"));
        UNSCOPED_INFO("a copy");
        CHECK(ALScriptTempFiles::isCopy("/tmp/sl_script_x_abc.lsl.log"));
        UNSCOPED_INFO("a notecard's");
        CHECK(ALScriptTempFiles::isCopy("/tmp/sl_notecard_n_abc.txt"));
        UNSCOPED_INFO("not one");
        CHECK_FALSE(ALScriptTempFiles::isCopy("/tmp/notes_sl_script_.txt"));
    }

    TEST_CASE_METHOD(alscripttempfiles_data, "alscripttempfiles: two editors of one script share a copy that goes with the last of them", "[alscripttempfiles]")
    {
        ALScriptTempFiles files(lists, "one");
        const std::string copy = ALScriptTempFiles::nameFor(temp, "s", "id", false);
        std::shared_ptr<ALScriptTempFiles::Claim> studio = files.claim(copy);
        std::shared_ptr<ALScriptTempFiles::Claim> window = files.claim(copy);
        UNSCOPED_INFO("one hold");
        CHECK(studio == window);
        put(copy);
        studio.reset();
        UNSCOPED_INFO("the other still has it");
        CHECK(there(copy));
        window.reset();
        UNSCOPED_INFO("gone with the last");
        CHECK_FALSE(there(copy));

        // Held again after it went: written again, and listed once.
        std::shared_ptr<ALScriptTempFiles::Claim> again = files.claim(copy);
        put(copy);
        CHECK(there(copy));
    }

    TEST_CASE_METHOD(alscripttempfiles_data, "alscripttempfiles: a sweep takes what ended sessions left but not what a running one lists", "[alscripttempfiles]")
    {
        // A session that crashed: its list, and its lock that nobody holds.
        const std::string left     = ALScriptTempFiles::nameFor(temp, "left", "1", false);
        const std::string left_log = left + ".log";
        const std::string shared   = ALScriptTempFiles::nameFor(temp, "shared", "2", true);
        const std::string notes    = temp + "notes.txt";
        for (const std::string& path : { left, left_log, shared, notes })
        {
            put(path);
        }
        put(lists + "crashed.list", left + "\n" + left_log + "\r\n" + shared + "\n" + notes + "\n");
        put(lists + "crashed.lock", "");
        // Another, whose lock has gone with it too.
        const std::string older = ALScriptTempFiles::nameFor(temp, "older", "3", false);
        put(older);
        put(lists + "older.list", older + "\n");

        // A session still running, which lists one the crashed one did.
        ALScriptTempFiles                        running(lists, "running");
        std::shared_ptr<ALScriptTempFiles::Claim> held = running.claim(shared);

        ALScriptTempFiles sweeper(lists, "now");
        CHECK(sweeper.sweep() == size_t(3));
        UNSCOPED_INFO("the crashed one's copy");
        CHECK_FALSE(there(left));
        UNSCOPED_INFO("and its log");
        CHECK_FALSE(there(left_log));
        UNSCOPED_INFO("the older one's");
        CHECK_FALSE(there(older));
        UNSCOPED_INFO("one a running session lists stays");
        CHECK(there(shared));
        UNSCOPED_INFO("what is no copy stays");
        CHECK(there(notes));
        {
            INFO("the crashed one's list and lock gone");
            CHECK_FALSE(there(lists + "crashed.list"));
            CHECK_FALSE(there(lists + "crashed.lock"));
        }
        UNSCOPED_INFO("and the older one's list");
        CHECK_FALSE(there(lists + "older.list"));
        {
            INFO("the running one's kept");
            CHECK(there(lists + "running.list"));
            CHECK(there(lists + "running.lock"));
        }

        UNSCOPED_INFO("swept again, nothing");
        CHECK(sweeper.sweep() == size_t(0));
        UNSCOPED_INFO("the running one's still");
        CHECK(there(shared));
    }

    TEST_CASE_METHOD(alscripttempfiles_data, "alscripttempfiles: a session that lets go of everything leaves no list or lock or copy", "[alscripttempfiles]")
    {
        const std::string copy = ALScriptTempFiles::nameFor(temp, "s", "id", false);
        {
            ALScriptTempFiles                         files(lists, "clean");
            std::shared_ptr<ALScriptTempFiles::Claim> held = files.claim(copy);
            put(copy);
            {
                INFO("listed");
                CHECK(there(lists + "clean.list"));
                CHECK(there(lists + "clean.lock"));
            }
        }
        CHECK_FALSE(there(copy));
        CHECK_FALSE(there(lists + "clean.list"));
        CHECK_FALSE(there(lists + "clean.lock"));

        // One that let go of the store first, a hold outliving it: the
        // lock is kept until the hold goes.
        std::shared_ptr<ALScriptTempFiles::Claim> held;
        {
            ALScriptTempFiles files(lists, "late");
            held = files.claim(copy);
            put(copy);
        }
        ALScriptTempFiles sweeper(lists, "now");
        UNSCOPED_INFO("still running, as far as a sweep can tell");
        CHECK(sweeper.sweep() == size_t(0));
        UNSCOPED_INFO("its copy kept");
        CHECK(there(copy));
        held.reset();
        {
            INFO("gone with the hold");
            CHECK_FALSE(there(copy));
            CHECK_FALSE(there(lists + "late.list"));
            CHECK_FALSE(there(lists + "late.lock"));
        }
    }

    TEST_CASE_METHOD(alscripttempfiles_data, "alscripttempfiles: a session whose lock cannot be taken lists nothing for a sweep to take", "[alscripttempfiles]")
    {
        // Its lock's path taken by a folder: the lock cannot be opened, so
        // a list would be one no lock says is a running session's, and
        // another session's sweep would take the copy while it is held.
        std::error_code ec;
        std::filesystem::create_directories(fsyspath(lists + "stuck.lock"), ec);
        const std::string copy = ALScriptTempFiles::nameFor(temp, "Script", "1", false);
        put(copy);
        ALScriptTempFiles                         stuck(lists, "stuck");
        std::shared_ptr<ALScriptTempFiles::Claim> held = stuck.claim(copy);
        UNSCOPED_INFO("not listed");
        CHECK_FALSE(there(lists + "stuck.list"));
        ALScriptTempFiles sweeper(lists, "now");
        UNSCOPED_INFO("so no sweep takes it");
        CHECK(sweeper.sweep() == size_t(0));
        CHECK(there(copy));
        held.reset();
        UNSCOPED_INFO("and it still goes with the last hold");
        CHECK_FALSE(there(copy));
    }
}
