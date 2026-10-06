/**
 * @file aldiskincludes_test.cpp
 * @brief Which files on disk a script's includes may be read from.
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

#include "../preprocessor/aldiskincludes.h"

#include "fsyspath.h"

#include "../test/lltut.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>

#if LL_DARWIN || LL_LINUX
#include <sys/stat.h>
#endif

namespace
{
    namespace fs = std::filesystem;

    // A folder of its own for each test, gone with it.
    struct Scratch
    {
        fs::path root;

        Scratch()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            root             = fs::temp_directory_path() / ("aldiskincludes_" + std::to_string(stamp));
            fs::create_directories(root);
            // Where links are followed to: the answers are in those terms.
            root = fs::canonical(root);
        }
        ~Scratch()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        // In the system's own separators, as a path that has been followed
        // is put.
        fs::path under(const std::string& relative) const { return (root / relative).make_preferred(); }

        std::string write(const std::string& relative, const std::string& text) const
        {
            const fs::path path = under(relative);
            fs::create_directories(path.parent_path());
            llofstream out(path, std::ios::binary);
            out << text;
            return fsyspath(path).string();
        }
        std::string at(const std::string& relative) const { return fsyspath(under(relative)).string(); }
    };
}

namespace tut
{
    struct aldiskincludes_data
    {
    };

    typedef test_group<aldiskincludes_data> aldiskincludes_group;
    typedef aldiskincludes_group::object    aldiskincludes_object;
    aldiskincludes_group                    aldiskincludes_instance("aldiskincludes");

    template<> template<>
    void aldiskincludes_object::test<1>()
    {
        set_test_name("nothing on the disk is read but under a blessed folder, however the path is put");
        Scratch            s;
        const std::string  lib    = s.write("lib/util.lsl", "integer util() { return 1; }\n");
        const std::string  secret = s.write("private/secret.txt", "the author's own\n");
        s.write("lib2/other.lsl", "x\n");
        ALDiskIncludes none;
        ensure("nothing blessed, nothing read", !none.blessed() && !none.admits(lib));

        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));
        ensure("blessed", blessed.blessed() && blessed.folders().size() == 1);
        ensure_equals("a file under it is read, as it stands", blessed.admits(lib).value_or(""), lib);
        ensure("a path from a root elsewhere is not", !blessed.admits(secret));
        ensure("nor one climbing out of the blessed folder", !blessed.admits(s.at("lib/../private/secret.txt")));
        ensure("nor one in a folder whose name only begins with it", !blessed.admits(s.at("lib2/other.lsl")));
        ensure("nor the folder itself", !blessed.admits(s.at("lib")));
        ensure("nor what is not there", !blessed.admits(s.at("lib/missing.lsl")));

        blessed.bless(s.at("nowhere"));
        blessed.bless(lib);
        ensure_equals("what is not a folder blesses nothing", blessed.folders().size(), size_t(1));
        blessed.bless(s.at("lib/../lib"));
        ensure_equals("and one folder is blessed once however it is named", blessed.folders().size(), size_t(1));
    }

    template<> template<>
    void aldiskincludes_object::test<2>()
    {
        set_test_name("a link is followed before it is judged, and only an ordinary file of a sensible size is read");
        Scratch           s;
        const std::string secret = s.write("private/secret.txt", "the author's own\n");
        s.write("lib/util.lsl", "x\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));

        std::error_code ec;
        fs::create_symlink(fsyspath(secret), s.root / "lib" / "innocent.lsl", ec);
        if (!ec)
        {
            ensure("a link out of the folder is not read", !blessed.admits(s.at("lib/innocent.lsl")));
        }
        fs::create_directory_symlink(s.root / "private", s.root / "lib" / "door", ec);
        if (!ec)
        {
            ensure("nor a file through a linked folder", !blessed.admits(s.at("lib/door/secret.txt")));
        }

        const std::string big = s.write("lib/big.lsl", std::string(static_cast<size_t>(ALDiskIncludes::MAX_BYTES) + 1, 'x'));
        ensure("a file past the limit is not read", !blessed.admits(big));
        std::string text;
        ensure("nor read by the reader", !ALDiskIncludes::readOrdinary(big, text) && text.empty());
        ensure("a folder is not read", !ALDiskIncludes::readOrdinary(s.at("lib"), text));
        ensure("an ordinary one is", ALDiskIncludes::readOrdinary(s.at("lib/util.lsl"), text) && text == "x\n");
        ensure("and held at its own size, not the limit's", text.capacity() < 1024);
        const std::string whole(100000, 'y');
        const std::string mid = s.write("lib/mid.lsl", whole);
        ensure("one of some size, whole", ALDiskIncludes::readOrdinary(mid, text) && text == whole);
        ensure("held at about its size", text.capacity() < whole.size() + 1024);
        const std::string at_limit = s.write("lib/limit.lsl", std::string(static_cast<size_t>(ALDiskIncludes::MAX_BYTES), 'z'));
        ensure("one at the limit, whole", ALDiskIncludes::readOrdinary(at_limit, text) && text.size() == ALDiskIncludes::MAX_BYTES);

#if LL_DARWIN || LL_LINUX
        // What would be read for ever, or never answer: refused at once.
        ALDiskIncludes devices;
        devices.bless("/dev");
        ensure("a device is not an include", !devices.admits("/dev/zero"));
        ensure("nor read", !ALDiskIncludes::readOrdinary("/dev/zero", text));
        const std::string pipe = s.at("lib/pipe");
        if (mkfifo(pipe.c_str(), 0600) == 0)
        {
            ensure("a pipe is not an include", !blessed.admits(pipe));
            ensure("nor read, and the reading does not wait on it", !ALDiskIncludes::readOrdinary(pipe, text));
        }
#endif
    }

    template<> template<>
    void aldiskincludes_object::test<3>()
    {
        set_test_name("a .lslrc lists folders from beside it, and the nearest up from a folder is found");
        Scratch s;
        s.write("project/.lslrc", "{\"include\": [\"../lib\", \"shared/\"]}");
        s.write("project/src/deep/main.lsl", "x\n");
        const std::vector<std::string> listed = ALDiskIncludes::lslrcFolders(s.at("project"));
        ensure_equals("both", listed.size(), size_t(2));
        ensure_equals("the one above, from beside the file", listed[0], s.at("lib"));
        ensure_equals("the one below, without its slash", listed[1], s.at("project/shared"));
        std::string found_in;
        ensure("the nearest up", ALDiskIncludes::nearestLslrcFolders(s.at("project/src/deep"), &found_in) == listed);
        ensure_equals("and where it is", found_in, s.at("project"));
        ensure("none where there is none", ALDiskIncludes::lslrcFolders(s.at("project/src")).empty());
        s.write("broken/.lslrc", "{not json");
        ensure("nor where it is not a configuration", ALDiskIncludes::lslrcFolders(s.at("broken")).empty());
    }

    template<> template<>
    void aldiskincludes_object::test<4>()
    {
        set_test_name("a blessed folder's files are listed through its folders, by their paths from it, and no further than asked");
        Scratch s;
        s.write("lib/util.luau", "return {}\n");
        s.write("lib/net/http.luau", "return {}\n");
        s.write("lib/net/deep/deeper/far.luau", "return {}\n");
        s.write("lib/.git/hooks.luau", "return {}\n");
        s.write("lib/notes.txt", "not a module\n");
        s.write("elsewhere/secret.luau", "return {}\n");
        const auto listed = [](const std::vector<ALDiskIncludes::Listed>& found) {
            std::vector<std::string> out;
            for (const ALDiskIncludes::Listed& one : found)
            {
                out.push_back(one.relative);
            }
            std::sort(out.begin(), out.end());
            std::string said;
            for (const std::string& one : out)
            {
                said += (said.empty() ? "" : " ") + one;
            }
            return said;
        };
        ALDiskIncludes blessed;
        ensure("nothing of a folder not blessed", blessed.filesUnder(s.at("lib"), { ".luau" }, 3, 100, 100).empty());
        blessed.bless(s.at("lib"));
        const std::vector<ALDiskIncludes::Listed> found = blessed.filesUnder(s.at("lib"), { ".luau", ".lua" }, 2, 100, 100);
        ensure_equals("two folders down, no hidden one, only what is asked for", listed(found), std::string("net/http.luau util.luau"));
        for (const ALDiskIncludes::Listed& one : found)
        {
            ensure_equals("each where it stands", one.file, s.at("lib/" + one.relative));
        }
        ensure_equals("deeper where asked", listed(blessed.filesUnder(s.at("lib"), { ".luau" }, 4, 100, 100)),
                      std::string("net/deep/deeper/far.luau net/http.luau util.luau"));
        ensure_equals("a folder under a blessed one, from itself", listed(blessed.filesUnder(s.at("lib/net"), { ".luau" }, 1, 100, 100)),
                      std::string("http.luau"));
        ensure_equals("no more than asked for", blessed.filesUnder(s.at("lib"), { ".luau" }, 4, 100, 1).size(), size_t(1));
        ensure("nothing outside", blessed.filesUnder(s.at("elsewhere"), { ".luau" }, 4, 100, 100).empty());
#if LL_DARWIN || LL_LINUX
        // A link to a folder outside is not followed down.
        std::error_code ec;
        fs::create_directory_symlink(s.under("elsewhere"), s.under("lib/linked"), ec);
        ensure("linked", !ec);
        ensure_equals("not through a link", listed(blessed.filesUnder(s.at("lib"), { ".luau" }, 4, 100, 100)),
                      std::string("net/deep/deeper/far.luau net/http.luau util.luau"));
#endif
    }

    template<> template<>
    void aldiskincludes_object::test<5>()
    {
        set_test_name("a configuration blesses a folder under its own, or under one the scripter blessed, and no further");
        Scratch s;
        s.write("project/shared/a.lsl", "a\n");
        s.write("lib/b.lsl", "b\n");
        s.write("secret/key.txt", "k\n");
        ALDiskIncludes none;
        ensure("under its own", none.mayFromConfig(s.at("project/shared"), s.at("project")));
        ensure("its own", none.mayFromConfig(s.at("project"), s.at("project")));
        ensure("not a sibling", !none.mayFromConfig(s.at("lib"), s.at("project")));
        ensure("not a root", !none.mayFromConfig(fsyspath(s.root.root_path()).string(), s.at("project")));
        ensure("not by going up", !none.mayFromConfig(s.at("project/../secret"), s.at("project")));
        ensure("not what is not there", !none.mayFromConfig(s.at("project/missing"), s.at("project")));
        // The sibling, where the scripter's own folders hold it.
        ALDiskIncludes own;
        own.bless(fsyspath(s.root).string());
        ensure("under the scripter's", own.mayFromConfig(s.at("lib"), s.at("project")));
        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));
        ensure("blessed from it", blessed.blessFromConfig(s.at("lib"), s.at("project")));
        ensure("and refused past it", !blessed.blessFromConfig(s.at("secret"), s.at("project")) && !blessed.admits(s.at("secret/key.txt")));
#if LL_DARWIN || LL_LINUX
        // A link inside that leads outside is where it leads.
        std::error_code ec;
        fs::create_directory_symlink(s.under("secret"), s.under("project/escape"), ec);
        ensure("linked: " + ec.message(), !ec);
        ensure("not through a link out", !none.mayFromConfig(s.at("project/escape"), s.at("project")));
#endif
    }

    template<> template<>
    void aldiskincludes_object::test<6>()
    {
        set_test_name("a require of a name with no extension finds a folder's init.luau after the name's own files");
        typedef std::vector<std::string> names;
        ensure("SLua's require", ALDiskIncludes::namesFor("lib", true, true) ==
                                     names{ "lib", "lib.luau", "lib.lua", "lib/init.luau", "lib/init.lua" });
        const names folder = ALDiskIncludes::namesFor("./shared/lib/", true, true);
        ensure("a path's last folder, however it ends",
               folder.size() == 5 && folder[3] == "./shared/lib/init.luau" && folder[4] == "./shared/lib/init.lua");
        ensure("a name with its extension is that file", ALDiskIncludes::namesFor("lib.Luau", true, true) ==
                                                              names{ "lib.Luau", "lib.Luau.luau", "lib.Luau.lua" });
        ensure("an #include in SLua is a file", ALDiskIncludes::namesFor("lib", true, false) == names{ "lib", "lib.luau", "lib.lua" });
        ensure("an LSL include", ALDiskIncludes::namesFor("lib", false, false) == names{ "lib", "lib.lsl" });

        // As a folder is looked in: the first of the names it admits.
        Scratch           s;
        const std::string init = s.write("modules/lib/init.luau", "return {}\n");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("modules"));
        const auto first = [&](const std::string& name) {
            for (const std::string& candidate : ALDiskIncludes::namesFor(name, true, true))
            {
                if (const std::optional<std::string> real = blessed.admits(s.at("modules/" + candidate)))
                {
                    return *real;
                }
            }
            return std::string();
        };
        ensure_equals("the folder's module", first("lib"), init);
        const std::string own = s.write("modules/lib.luau", "return 1\n");
        ensure_equals("a file of the name before the folder", first("lib"), own);
        ensure_equals("init by its own name too", first("lib/init"), init);
    }

    template<> template<>
    void aldiskincludes_object::test<7>()
    {
        set_test_name("the configuration at the top of each blessed folder that has one, in their order, read as any file under it is");
        Scratch        s;
        ALDiskIncludes none;
        ensure("nothing blessed, nothing found", none.atTop(".luaurc").empty());

        s.write("first/util.luau", "return 1\n");
        const std::string second = s.write("second/.luaurc", "{\"aliases\": {\"lib\": \"./lib\"}}\n");
        const std::string third  = s.write("third/.luaurc", "{}\n");
        s.write("first/deeper/.luaurc", "{}\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("first"));
        blessed.bless(s.at("third"));
        blessed.bless(s.at("second"));
        typedef std::vector<std::string> files;
        ensure("each folder's own, in the order blessed, not one further down", blessed.atTop(".luaurc") == files{ third, second });

        // A folder so named is no file.
        fs::create_directories(s.under("first/.luaurc"));
        ensure("a folder so named passed over", blessed.atTop(".luaurc") == files{ third, second });
#if LL_DARWIN || LL_LINUX
        // A link out of the blessed folders is where it leads, and not read.
        s.write("elsewhere/.luaurc", "{}\n");
        std::error_code ec;
        fs::remove(s.under("first/.luaurc"), ec);
        fs::create_symlink(s.under("elsewhere/.luaurc"), s.under("first/.luaurc"), ec);
        ensure("linked: " + ec.message(), !ec);
        ensure("not through a link out", blessed.atTop(".luaurc") == files{ third, second });
#endif
        ensure("nor a name no folder has", blessed.atTop(".lslrc").empty());
    }

    template<> template<>
    void aldiskincludes_object::test<8>()
    {
        set_test_name("the scripts of a language under folders: each folder blessed for the look, each script once, no more than asked, nothing outside");
        Scratch s;
        s.write("lib/util.luau", "return {}\n");
        s.write("lib/old.lua", "return {}\n");
        s.write("lib/net/http.luau", "return {}\n");
        s.write("lib/door.lsl", "default {}\n");
        s.write("lib/door.lslh", "integer x;\n");
        s.write("lib/notes.txt", "words\n");
        s.write("tools/t.luau", "return {}\n");
        s.write("elsewhere/secret.luau", "return {}\n");
        const auto names = [&s](std::vector<std::string> found) {
            std::sort(found.begin(), found.end());
            std::string said;
            for (const std::string& one : found)
            {
                said += (said.empty() ? "" : " ") + fsyspath(fs::relative(fsyspath(one), fsyspath(s.at("")))).generic_string();
            }
            return said;
        };
        ensure_equals("SLua's, both folders, through their folders",
                      names(ALDiskIncludes::scriptsUnder({ s.at("lib"), s.at("tools") }, true, 4, 100)),
                      std::string("lib/net/http.luau lib/old.lua lib/util.luau tools/t.luau"));
        ensure_equals("LSL's and its includes'", names(ALDiskIncludes::scriptsUnder({ s.at("lib") }, false, 4, 100)),
                      std::string("lib/door.lsl lib/door.lslh"));
        ensure_equals("a folder given twice, or under another, each once",
                      ALDiskIncludes::scriptsUnder({ s.at("lib"), s.at("lib/net"), s.at("lib") }, true, 4, 100).size(), size_t(3));
        ensure_equals("no more than asked", ALDiskIncludes::scriptsUnder({ s.at("lib"), s.at("tools") }, true, 4, 2).size(), size_t(2));
        ensure_equals("not deeper than asked", names(ALDiskIncludes::scriptsUnder({ s.at("lib") }, true, 0, 100)),
                      std::string("lib/old.lua lib/util.luau"));
        ensure("nothing of a folder not there", ALDiskIncludes::scriptsUnder({ s.at("nowhere") }, true, 4, 100).empty());
    }
}
