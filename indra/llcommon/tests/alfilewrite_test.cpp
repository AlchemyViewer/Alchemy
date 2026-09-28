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

#include "../test/lltut.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#if !LL_WINDOWS
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tut
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

    typedef test_group<alfilewrite_data> alfilewrite_group;
    typedef alfilewrite_group::object    alfilewrite_object;
    alfilewrite_group                    alfilewrite_instance("alfilewrite");

    template<> template<>
    void alfilewrite_object::test<1>()
    {
        set_test_name("a file is written whole, a new one or over an old, and nothing is left beside it");
        const std::string file = in("script.lsl");
        ensure("a new file", ALFileWrite::whole(file, "default {}\n"));
        ensure_equals("its text", read(file), std::string("default {}\n"));
        ensure("over it", ALFileWrite::whole(file, "x"));
        ensure_equals("the new text, all of the old gone", read(file), std::string("x"));
        ensure("empty is a text too", ALFileWrite::whole(file, ""));
        ensure_equals("and the file is empty", read(file), std::string());
        std::error_code ec;
        ensure("nothing left beside it", !fs::exists(fsyspath(ALFileWrite::besideOf(file)), ec));
    }

    template<> template<>
    void alfilewrite_object::test<2>()
    {
        set_test_name("a file that could not be written in place is left as it was");
        if (root())
        {
            skip("root writes what it likes");
        }
        const std::string file = in("readonly.lsl");
        put(file, "what was there");
        std::error_code ec;
        fs::permissions(fsyspath(file), fs::perms::owner_read, fs::perm_options::replace, ec);
        ensure("refused", !ALFileWrite::whole(file, "new text"));
        ensure_equals("the old text, whole", read(file), std::string("what was there"));
        ensure("nothing left beside it", !fs::exists(fsyspath(ALFileWrite::besideOf(file)), ec));
    }

    template<> template<>
    void alfilewrite_object::test<3>()
    {
        set_test_name("where nothing can be put beside a file, it is written in place, and what is there by that name stays");
        const std::string file = in("keep.lsl");
        put(file, "what was there");
        // Where the text would go beside it is a folder, which no file can
        // be written as.
        std::error_code ec;
        fs::create_directories(fsyspath(ALFileWrite::besideOf(file)), ec);
        ensure("written", ALFileWrite::whole(file, "new text"));
        ensure_equals("in place", read(file), std::string("new text"));
        ensure("the folder by that name left", fs::is_directory(fsyspath(ALFileWrite::besideOf(file)), ec));
    }

    template<> template<>
    void alfilewrite_object::test<4>()
    {
        set_test_name("a file keeps its permissions, and a link is written through and stays a link");
#if LL_WINDOWS
        skip("permissions and links are the Posix file systems' to test");
#else
        const std::string file = in("mine.lsl");
        put(file, "old");
        std::error_code ec;
        fs::permissions(fsyspath(file), fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        ensure("written", ALFileWrite::whole(file, "new"));
        ensure_equals("the new text", read(file), std::string("new"));
        ensure("its permissions as they were", fs::status(fsyspath(file)).permissions() == (fs::perms::owner_read | fs::perms::owner_write));

        const std::string link = in("link.lsl");
        fs::create_symlink(fsyspath(file), fsyspath(link), ec);
        if (ec)
        {
            skip("no links here");
        }
        ensure("through the link", ALFileWrite::whole(link, "through"));
        ensure("still a link", fs::is_symlink(fsyspath(link)));
        ensure_equals("the file it names written", read(file), std::string("through"));

        const std::string dangling = in("dangling.lsl");
        fs::create_symlink(fsyspath(in("nowhere.lsl")), fsyspath(dangling), ec);
        ensure("a link to nothing has nowhere to write", !ALFileWrite::whole(dangling, "lost"));
        ensure("and is left a link", fs::is_symlink(fsyspath(dangling)));
#endif
    }

    template<> template<>
    void alfilewrite_object::test<5>()
    {
        set_test_name("a file in a folder only its files may be written in is written in place");
#if LL_WINDOWS
        skip("a folder's permissions are the Posix file systems' to test");
#else
        if (root())
        {
            skip("root writes what it likes");
        }
        const std::string sub = in("closed");
        std::error_code   ec;
        fs::create_directories(fsyspath(sub), ec);
        const std::string file = fsyspath(fsyspath(sub) / fsyspath("inside.lsl")).string();
        put(file, "old");
        fs::permissions(fsyspath(sub), fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace, ec);
        const bool written = ALFileWrite::whole(file, "new");
        fs::permissions(fsyspath(sub), fs::perms::owner_all, fs::perm_options::replace, ec);
        ensure("written", written);
        ensure_equals("in place", read(file), std::string("new"));
#endif
    }

    template<> template<>
    void alfilewrite_object::test<7>()
    {
        set_test_name("the one reader: an ordinary file whole, up to a limit; not a folder, and not a pipe, which it does not wait on");
        const std::string file = in("include.lsl");
        put(file, "integer x;\n");
        std::string text = "left over";
        ensure("read", ALFileRead::whole(file, text, 64));
        ensure_equals("whole", text, std::string("integer x;\n"));
        ensure("at the limit", ALFileRead::whole(file, text, 11) && text.size() == 11);
        ensure("past it", !ALFileRead::whole(file, text, 10) && text.empty());
        ensure("a folder is not read", !ALFileRead::whole(folder, text, 64));
        ensure("nothing is not read", !ALFileRead::whole(in("gone.lsl"), text, 64));
#if !LL_WINDOWS
        const std::string pipe = in("pipe");
        if (mkfifo(pipe.c_str(), 0600) == 0)
        {
            ensure("a pipe is not read", !ALFileRead::whole(pipe, text, 64));
        }
#endif
    }

    template<> template<>
    void alfilewrite_object::test<8>()
    {
        set_test_name("a temp file is written in place, the user's alone, and never through a link");
        const std::string file = in("sl_script_x_id.lsl");
        ensure("written", ALFileWrite::temp(file, "default {}"));
        ensure_equals("its text", read(file), std::string("default {}"));
        ensure("again, shorter", ALFileWrite::temp(file, "x"));
        ensure_equals("all of the old gone", read(file), std::string("x"));
#if !LL_WINDOWS
        ensure("the user's alone", fs::status(fsyspath(file)).permissions() == (fs::perms::owner_read | fs::perms::owner_write));
        const std::string target = in("elsewhere.txt");
        put(target, "somebody's");
        const std::string link = in("sl_script_y_id.lsl");
        std::error_code   ec;
        fs::create_symlink(fsyspath(target), fsyspath(link), ec);
        if (!ec)
        {
            ensure("not through a link", !ALFileWrite::temp(link, "mine"));
            ensure_equals("what it names untouched", read(target), std::string("somebody's"));
        }
#endif
    }
}
