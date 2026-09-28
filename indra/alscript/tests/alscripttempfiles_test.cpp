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

#include "../alscripttempfiles.h"

#include "fsyspath.h"
#include "llfile.h"
#include "lluuid.h"

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>

namespace tut
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

    typedef test_group<alscripttempfiles_data> alscripttempfiles_group;
    typedef alscripttempfiles_group::object    alscripttempfiles_object;
    alscripttempfiles_group                    alscripttempfiles_instance("alscripttempfiles");

    template<> template<>
    void alscripttempfiles_object::test<1>()
    {
        set_test_name("a copy's name: the script's without what a file system refuses, the id, the language's extension");
        ensure_equals("LSL", ALScriptTempFiles::nameFor("/tmp", "My Script", "abc", false), std::string("/tmp/sl_script_My Script_abc.lsl"));
        ensure_equals("SLua, the name cleaned", ALScriptTempFiles::nameFor("/tmp/", "a/b:c?", "abc", true),
                      std::string("/tmp/sl_script_abc_abc.luau"));
        ensure_equals("nothing left of the name", ALScriptTempFiles::nameFor("/tmp/", "<>*", "abc", false), std::string("/tmp/sl_script_abc.lsl"));
        ensure("a copy", ALScriptTempFiles::isCopy("/tmp/sl_script_x_abc.lsl.log"));
        ensure("a notecard's", ALScriptTempFiles::isCopy("/tmp/sl_notecard_n_abc.txt"));
        ensure("not one", !ALScriptTempFiles::isCopy("/tmp/notes_sl_script_.txt"));
    }

    template<> template<>
    void alscripttempfiles_object::test<2>()
    {
        set_test_name("two editors of one script hold one copy, and it goes with the last of them, whichever that is");
        ALScriptTempFiles files(lists, "one");
        const std::string copy = ALScriptTempFiles::nameFor(temp, "s", "id", false);
        std::shared_ptr<ALScriptTempFiles::Claim> studio = files.claim(copy);
        std::shared_ptr<ALScriptTempFiles::Claim> window = files.claim(copy);
        ensure("one hold", studio == window);
        put(copy);
        studio.reset();
        ensure("the other still has it", there(copy));
        window.reset();
        ensure("gone with the last", !there(copy));

        // Held again after it went: written again, and listed once.
        std::shared_ptr<ALScriptTempFiles::Claim> again = files.claim(copy);
        put(copy);
        ensure("there", there(copy));
    }

    template<> template<>
    void alscripttempfiles_object::test<3>()
    {
        set_test_name("a sweep takes what sessions that are over left, and leaves what a running one lists, and what is no copy");
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
        ensure_equals("three went", sweeper.sweep(), size_t(3));
        ensure("the crashed one's copy", !there(left));
        ensure("and its log", !there(left_log));
        ensure("the older one's", !there(older));
        ensure("one a running session lists stays", there(shared));
        ensure("what is no copy stays", there(notes));
        ensure("the crashed one's list and lock gone", !there(lists + "crashed.list") && !there(lists + "crashed.lock"));
        ensure("and the older one's list", !there(lists + "older.list"));
        ensure("the running one's kept", there(lists + "running.list") && there(lists + "running.lock"));

        ensure_equals("swept again, nothing", sweeper.sweep(), size_t(0));
        ensure("the running one's still", there(shared));
    }

    template<> template<>
    void alscripttempfiles_object::test<4>()
    {
        set_test_name("a session that ends letting go of everything leaves no list, no lock, and no copy");
        const std::string copy = ALScriptTempFiles::nameFor(temp, "s", "id", false);
        {
            ALScriptTempFiles                         files(lists, "clean");
            std::shared_ptr<ALScriptTempFiles::Claim> held = files.claim(copy);
            put(copy);
            ensure("listed", there(lists + "clean.list") && there(lists + "clean.lock"));
        }
        ensure("no copy", !there(copy));
        ensure("no list, no lock", !there(lists + "clean.list") && !there(lists + "clean.lock"));

        // One that let go of the store first, a hold outliving it: the
        // lock is kept until the hold goes.
        std::shared_ptr<ALScriptTempFiles::Claim> held;
        {
            ALScriptTempFiles files(lists, "late");
            held = files.claim(copy);
            put(copy);
        }
        ALScriptTempFiles sweeper(lists, "now");
        ensure_equals("still running, as far as a sweep can tell", sweeper.sweep(), size_t(0));
        ensure("its copy kept", there(copy));
        held.reset();
        ensure("gone with the hold", !there(copy) && !there(lists + "late.list") && !there(lists + "late.lock"));
    }
}
