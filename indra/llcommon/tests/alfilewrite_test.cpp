/**
 * @file alfilewrite_test.cpp
 * @brief A file written whole or not at all, through a link, keeping its permissions.
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

#include "../alfilewrite.h"

#include "../fsyspath.h"
#include "../llfile.h"
#include "../lluuid.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

#if !LL_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
    namespace fs = std::filesystem;

    struct alfilewrite_data
    {
        std::string folder;

        alfilewrite_data()
        {
            folder = fsyspath(fs::temp_directory_path() / fsyspath("alfilewrite_" + LLUUID::generateNewID().asString())).string();
            std::error_code ec;
            fs::create_directories(fsyspath(folder), ec);
        }
        ~alfilewrite_data()
        {
            // Whatever a test made read-only made writable again, so that
            // it can go.
            std::error_code ec;
            fs::permissions(fsyspath(folder), fs::perms::owner_all, fs::perm_options::add, ec);
            for (const auto& entry : fs::recursive_directory_iterator(fsyspath(folder), ec))
            {
                fs::permissions(entry.path(), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add, ec);
            }
            fs::remove_all(fsyspath(folder), ec);
        }

        std::string in(const char* name) const { return fsyspath(fsyspath(folder) / fsyspath(name)).string(); }

        static std::string read(const std::string& file)
        {
            llifstream        in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        static void put(const std::string& file, const std::string& text)
        {
            llofstream out(file, std::ios::binary);
            out << text;
        }

        static bool root()
        {
#if LL_WINDOWS
            return false;
#else
            return geteuid() == 0;
#endif
        }
    };

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: whole writes a new file or over an old and leaves nothing beside it", "[alfilewrite]")
    {
        const std::string file = in("script.lsl");
        CHECK(ALFileWrite::whole(file, "default {}\n"));
        CHECK(read(file) == std::string("default {}\n"));
        CHECK(ALFileWrite::whole(file, "x"));
        UNSCOPED_INFO("the new text, all of the old gone");
        CHECK(read(file) == std::string("x"));
        UNSCOPED_INFO("empty is a text too");
        CHECK(ALFileWrite::whole(file, ""));
        CHECK(read(file) == std::string());
        std::error_code ec;
        CHECK_FALSE(fs::exists(fsyspath(ALFileWrite::besideOf(file)), ec));
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: a file that could not be written in place is left as it was", "[alfilewrite]")
    {
        if (root())
        {
            SKIP("root writes what it likes");
        }
        const std::string file = in("readonly.lsl");
        put(file, "what was there");
        std::error_code ec;
        fs::permissions(fsyspath(file), fs::perms::owner_read, fs::perm_options::replace, ec);
        CHECK_FALSE(ALFileWrite::whole(file, "new text"));
        UNSCOPED_INFO("the old text, whole");
        CHECK(read(file) == std::string("what was there"));
        CHECK_FALSE(fs::exists(fsyspath(ALFileWrite::besideOf(file)), ec));
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: a file with no room beside it is written in place and the folder stays", "[alfilewrite]")
    {
        const std::string file = in("keep.lsl");
        put(file, "what was there");
        // Where the text would go beside it is a folder, which no file can
        // be written as.
        std::error_code ec;
        fs::create_directories(fsyspath(ALFileWrite::besideOf(file)), ec);
        CHECK(ALFileWrite::whole(file, "new text"));
        UNSCOPED_INFO("in place");
        CHECK(read(file) == std::string("new text"));
        CHECK(fs::is_directory(fsyspath(ALFileWrite::besideOf(file)), ec));
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: a file keeps its permissions and a link is written through", "[alfilewrite]")
    {
#if LL_WINDOWS
        SKIP("permissions and links are the Posix file systems' to test");
#else
        const std::string file = in("mine.lsl");
        put(file, "old");
        std::error_code ec;
        fs::permissions(fsyspath(file), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        CHECK(ALFileWrite::whole(file, "new"));
        CHECK(read(file) == std::string("new"));
        UNSCOPED_INFO("its permissions as they were");
        CHECK(fs::status(fsyspath(file)).permissions() == (fs::perms::owner_read | fs::perms::owner_write));

        const std::string link = in("link.lsl");
        fs::create_symlink(fsyspath(file), fsyspath(link), ec);
        if (ec)
        {
            SKIP("no links here");
        }
        CHECK(ALFileWrite::whole(link, "through"));
        CHECK(fs::is_symlink(fsyspath(link)));
        UNSCOPED_INFO("the file it names written");
        CHECK(read(file) == std::string("through"));

        const std::string dangling = in("dangling.lsl");
        fs::create_symlink(fsyspath(in("nowhere.lsl")), fsyspath(dangling), ec);
        UNSCOPED_INFO("a link to nothing has nowhere to write");
        CHECK_FALSE(ALFileWrite::whole(dangling, "lost"));
        CHECK(fs::is_symlink(fsyspath(dangling)));
#endif
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: a file in a folder only its files may be written in is written in place", "[alfilewrite]")
    {
#if LL_WINDOWS
        SKIP("a folder's permissions are the Posix file systems' to test");
#else
        if (root())
        {
            SKIP("root writes what it likes");
        }
        const std::string sub = in("closed");
        std::error_code   ec;
        fs::create_directories(fsyspath(sub), ec);
        const std::string file = fsyspath(fsyspath(sub) / fsyspath("inside.lsl")).string();
        put(file, "old");
        fs::permissions(fsyspath(sub), fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
        const bool written = ALFileWrite::whole(file, "new");
        fs::permissions(fsyspath(sub), fs::perms::owner_all, fs::perm_options::replace, ec);
        CHECK(written);
        UNSCOPED_INFO("in place");
        CHECK(read(file) == std::string("new"));
#endif
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: a durable write is whole or nothing and never in place", "[alfilewrite]")
    {
        const std::string file = in("journal.llsd");
        CHECK(ALFileWrite::whole(file, "one", /*durable*/ true));
        CHECK(ALFileWrite::whole(file, "two", /*durable*/ true));
        CHECK(read(file) == std::string("two"));
        std::error_code ec;
        REQUIRE_FALSE(fs::exists(fsyspath(ALFileWrite::besideOf(file)), ec));
        fs::create_directories(fsyspath(ALFileWrite::besideOf(file)), ec);
        CHECK_FALSE(ALFileWrite::whole(file, "three", /*durable*/ true));
        UNSCOPED_INFO("the old text, whole");
        CHECK(read(file) == std::string("two"));
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: ALFileRead reads an ordinary file up to a limit and no folder or pipe", "[alfilewrite]")
    {
        const std::string file = in("include.lsl");
        put(file, "integer x;\n");
        std::string text = "left over";
        CHECK(ALFileRead::whole(file, text, 64));
        CHECK(text == std::string("integer x;\n"));
        {
            INFO("at the limit");
            CHECK(ALFileRead::whole(file, text, 11));
            REQUIRE(text.size() == 11);
        }
        {
            INFO("past it");
            CHECK_FALSE(ALFileRead::whole(file, text, 10));
            REQUIRE(text.empty());
        }
        CHECK_FALSE(ALFileRead::whole(folder, text, 64));
        UNSCOPED_INFO("a missing file is not read");
        CHECK_FALSE(ALFileRead::whole(in("gone.lsl"), text, 64));
#if !LL_WINDOWS
        const std::string pipe = in("pipe");
        if (mkfifo(pipe.c_str(), 0600) == 0)
        {
            CHECK_FALSE(ALFileRead::whole(pipe, text, 64));
        }
#endif
    }

    TEST_CASE_METHOD(alfilewrite_data, "alfilewrite: temp writes in place for the user alone and never through a link", "[alfilewrite]")
    {
        const std::string file = in("sl_script_x_id.lsl");
        CHECK(ALFileWrite::temp(file, "default {}"));
        CHECK(read(file) == std::string("default {}"));
        CHECK(ALFileWrite::temp(file, "x"));
        UNSCOPED_INFO("all of the old gone");
        CHECK(read(file) == std::string("x"));
#if !LL_WINDOWS
        UNSCOPED_INFO("the user's alone");
        CHECK(fs::status(fsyspath(file)).permissions() == (fs::perms::owner_read | fs::perms::owner_write));
        const std::string target = in("elsewhere.txt");
        put(target, "somebody's");
        const std::string link = in("sl_script_y_id.lsl");
        std::error_code   ec;
        fs::create_symlink(fsyspath(target), fsyspath(link), ec);
        if (!ec)
        {
            CHECK_FALSE(ALFileWrite::temp(link, "mine"));
            UNSCOPED_INFO("what it names untouched");
            CHECK(read(target) == std::string("somebody's"));
        }
#endif
    }
}
