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

#include "altestlinks.h"
#include "fsyspath.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
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

namespace
{
    TEST_CASE("aldiskincludes: nothing is read but under a blessed folder however the path is put", "[aldiskincludes]")
    {
        Scratch            s;
        const std::string  lib    = s.write("lib/util.lsl", "integer util() { return 1; }\n");
        const std::string  secret = s.write("private/secret.txt", "the author's own\n");
        s.write("lib2/other.lsl", "x\n");
        ALDiskIncludes none;
        {
            INFO("nothing blessed, nothing read");
            CHECK_FALSE(none.blessed());
            REQUIRE_FALSE(none.admits(lib));
        }

        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));
        CHECK(blessed.blessed());
        REQUIRE(blessed.folders().size() == 1);
        UNSCOPED_INFO("a file under it is read, as it stands");
        CHECK(blessed.admits(lib).value_or("") == lib);
        UNSCOPED_INFO("a path from a root elsewhere is not");
        REQUIRE_FALSE(blessed.admits(secret));
        UNSCOPED_INFO("nor one climbing out of the blessed folder");
        REQUIRE_FALSE(blessed.admits(s.at("lib/../private/secret.txt")));
        UNSCOPED_INFO("nor one in a folder whose name only begins with it");
        REQUIRE_FALSE(blessed.admits(s.at("lib2/other.lsl")));
        UNSCOPED_INFO("nor the folder itself");
        REQUIRE_FALSE(blessed.admits(s.at("lib")));
        UNSCOPED_INFO("nor what is not there");
        REQUIRE_FALSE(blessed.admits(s.at("lib/missing.lsl")));

        blessed.bless(s.at("nowhere"));
        blessed.bless(lib);
        UNSCOPED_INFO("what is not a folder blesses no folder");
        REQUIRE(blessed.folders().size() == size_t(1));
        blessed.bless(s.at("lib/../lib"));
        UNSCOPED_INFO("and one folder is blessed once however it is named");
        REQUIRE(blessed.folders().size() == size_t(1));
    }

    TEST_CASE("aldiskincludes: a link is followed before judging, so one out of the folder is not read", "[aldiskincludes]")
    {
        Scratch           s;
        const std::string secret = s.write("private/secret.txt", "the author's own\n");
        s.write("lib/util.lsl", "x\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));

#if LL_WINDOWS
        // Windows' own link to a folder, which any user may make, so it is
        // tried on every Windows machine, where a symlink may not be.
        UNSCOPED_INFO("nor a file through a junction to a folder outside");
        REQUIRE(!ll_test::createDirectoryJunction(s.root / "private", s.root / "lib" / "hatch"));
        REQUIRE_FALSE(blessed.admits(s.at("lib/hatch/secret.txt")));
#endif
        std::error_code ec;
        fs::create_symlink(fsyspath(secret), s.root / "lib" / "innocent.lsl", ec);
        if (ec)
        {
            SKIP("no symlinks here: " << ec.message());
        }
        UNSCOPED_INFO("a link out of the folder is not read");
        REQUIRE_FALSE(blessed.admits(s.at("lib/innocent.lsl")));
        fs::create_directory_symlink(s.root / "private", s.root / "lib" / "door", ec);
        REQUIRE(!ec);
        UNSCOPED_INFO("nor a file through a linked folder");
        REQUIRE_FALSE(blessed.admits(s.at("lib/door/secret.txt")));
    }

    TEST_CASE("aldiskincludes: only sensibly sized ordinary files are read", "[aldiskincludes]")
    {
        Scratch s;
        s.write("lib/util.lsl", "x\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));

        const std::string big = s.write("lib/big.lsl", std::string(static_cast<size_t>(ALDiskIncludes::MAX_BYTES) + 1, 'x'));
        UNSCOPED_INFO("a file past the limit is not read");
        REQUIRE_FALSE(blessed.admits(big));
        std::string text;
        {
            INFO("nor read by the reader");
            REQUIRE_FALSE(ALDiskIncludes::readOrdinary(big, text));
            REQUIRE(text.empty());
        }
        UNSCOPED_INFO("a folder is not read");
        CHECK_FALSE(ALDiskIncludes::readOrdinary(s.at("lib"), text));
        {
            INFO("an ordinary one is");
            CHECK(ALDiskIncludes::readOrdinary(s.at("lib/util.lsl"), text));
            CHECK(text == "x\n");
        }
        UNSCOPED_INFO("and held at its own size, not the limit's");
        CHECK(text.capacity() < 1024);
        const std::string whole(100000, 'y');
        const std::string mid = s.write("lib/mid.lsl", whole);
        {
            INFO("one of some size, whole");
            CHECK(ALDiskIncludes::readOrdinary(mid, text));
            CHECK(text == whole);
        }
        UNSCOPED_INFO("held at about its size");
        REQUIRE(text.capacity() < whole.size() + 1024);
        const std::string at_limit = s.write("lib/limit.lsl", std::string(static_cast<size_t>(ALDiskIncludes::MAX_BYTES), 'z'));
        {
            INFO("one at the limit, whole");
            CHECK(ALDiskIncludes::readOrdinary(at_limit, text));
            REQUIRE(text.size() == ALDiskIncludes::MAX_BYTES);
        }

#if LL_DARWIN || LL_LINUX
        // What would be read for ever, or never answer: refused at once.
        ALDiskIncludes devices;
        devices.bless("/dev");
        UNSCOPED_INFO("a device is not an include");
        REQUIRE_FALSE(devices.admits("/dev/zero"));
        UNSCOPED_INFO("nor read");
        CHECK_FALSE(ALDiskIncludes::readOrdinary("/dev/zero", text));
        const std::string pipe = s.at("lib/pipe");
        if (mkfifo(pipe.c_str(), 0600) == 0)
        {
            UNSCOPED_INFO("a pipe is not an include");
            REQUIRE_FALSE(blessed.admits(pipe));
            UNSCOPED_INFO("nor read, and the reading does not wait on it");
            CHECK_FALSE(ALDiskIncludes::readOrdinary(pipe, text));
        }
#endif
    }

    TEST_CASE("aldiskincludes: a .lslrc lists folders from beside it and the nearest up is found", "[aldiskincludes]")
    {
        Scratch s;
        s.write("project/.lslrc", "{\"include\": [\"../lib\", \"shared/\"]}");
        s.write("project/src/deep/main.lsl", "x\n");
        const std::vector<std::string> listed = ALDiskIncludes::lslrcFolders(s.at("project"));
        REQUIRE(listed.size() == size_t(2));
        UNSCOPED_INFO("the one above, from beside the file");
        CHECK(listed[0] == s.at("lib"));
        UNSCOPED_INFO("the one below, without its slash");
        CHECK(listed[1] == s.at("project/shared"));
        std::string found_in;
        UNSCOPED_INFO("the nearest up");
        CHECK(ALDiskIncludes::nearestLslrcFolders(s.at("project/src/deep"), &found_in) == listed);
        UNSCOPED_INFO("and where it is");
        CHECK(found_in == s.at("project"));
        UNSCOPED_INFO("none where there is none");
        REQUIRE(ALDiskIncludes::lslrcFolders(s.at("project/src")).empty());
        s.write("broken/.lslrc", "{not json");
        UNSCOPED_INFO("nor where it is not a configuration");
        REQUIRE(ALDiskIncludes::lslrcFolders(s.at("broken")).empty());
    }

    TEST_CASE("aldiskincludes: a blessed folder's files listed by path from it no deeper than asked", "[aldiskincludes]")
    {
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
        UNSCOPED_INFO("nothing of a folder not blessed");
        REQUIRE(blessed.filesUnder(s.at("lib"), { ".luau" }, 3, 100, 100).empty());
        blessed.bless(s.at("lib"));
        const std::vector<ALDiskIncludes::Listed> found = blessed.filesUnder(s.at("lib"), { ".luau", ".lua" }, 2, 100, 100);
        UNSCOPED_INFO("two folders down, no hidden one, only what is asked for");
        CHECK(listed(found) == std::string("net/http.luau util.luau"));
        for (const ALDiskIncludes::Listed& one : found)
        {
            UNSCOPED_INFO("each where it stands");
            REQUIRE(one.file == s.at("lib/" + one.relative));
        }
        UNSCOPED_INFO("deeper where asked");
        CHECK(listed(blessed.filesUnder(s.at("lib"), { ".luau" }, 4, 100, 100)) ==
                      std::string("net/deep/deeper/far.luau net/http.luau util.luau"));
        UNSCOPED_INFO("a folder under a blessed one, from itself");
        CHECK(listed(blessed.filesUnder(s.at("lib/net"), { ".luau" }, 1, 100, 100)) ==
                      std::string("http.luau"));
        UNSCOPED_INFO("no more than asked for");
        REQUIRE(blessed.filesUnder(s.at("lib"), { ".luau" }, 4, 100, 1).size() == size_t(1));
        UNSCOPED_INFO("nothing outside");
        REQUIRE(blessed.filesUnder(s.at("elsewhere"), { ".luau" }, 4, 100, 100).empty());
#if LL_DARWIN || LL_LINUX
        // A link to a folder outside is not followed down.
        std::error_code ec;
        fs::create_directory_symlink(s.under("elsewhere"), s.under("lib/linked"), ec);
        UNSCOPED_INFO("linked");
        REQUIRE_FALSE(ec);
        UNSCOPED_INFO("not through a link");
        CHECK(listed(blessed.filesUnder(s.at("lib"), { ".luau" }, 4, 100, 100)) ==
                      std::string("net/deep/deeper/far.luau net/http.luau util.luau"));
#endif
    }

    TEST_CASE("aldiskincludes: a configuration blesses only under its own folder or the scripter's", "[aldiskincludes]")
    {
        Scratch s;
        s.write("project/shared/a.lsl", "a\n");
        s.write("lib/b.lsl", "b\n");
        s.write("secret/key.txt", "k\n");
        ALDiskIncludes none;
        UNSCOPED_INFO("under its own");
        CHECK(none.mayFromConfig(s.at("project/shared"), s.at("project")));
        UNSCOPED_INFO("its own");
        CHECK(none.mayFromConfig(s.at("project"), s.at("project")));
        UNSCOPED_INFO("not a sibling");
        CHECK_FALSE(none.mayFromConfig(s.at("lib"), s.at("project")));
        UNSCOPED_INFO("not a root");
        CHECK_FALSE(none.mayFromConfig(fsyspath(s.root.root_path()).string(), s.at("project")));
        UNSCOPED_INFO("not by going up");
        CHECK_FALSE(none.mayFromConfig(s.at("project/../secret"), s.at("project")));
        UNSCOPED_INFO("not what is not there");
        CHECK_FALSE(none.mayFromConfig(s.at("project/missing"), s.at("project")));
        // The sibling, where the scripter's own folders hold it.
        ALDiskIncludes own;
        own.bless(fsyspath(s.root).string());
        UNSCOPED_INFO("under the scripter's");
        CHECK(own.mayFromConfig(s.at("lib"), s.at("project")));
        ALDiskIncludes blessed;
        blessed.bless(s.at("lib"));
        UNSCOPED_INFO("blessed from it");
        CHECK(blessed.blessFromConfig(s.at("lib"), s.at("project")));
        {
            INFO("and refused past it");
            CHECK_FALSE(blessed.blessFromConfig(s.at("secret"), s.at("project")));
            REQUIRE_FALSE(blessed.admits(s.at("secret/key.txt")));
        }
#if LL_DARWIN || LL_LINUX
        // A link inside that leads outside is where it leads.
        std::error_code ec;
        fs::create_directory_symlink(s.under("secret"), s.under("project/escape"), ec);
        UNSCOPED_INFO("linked: " << ec.message());
        REQUIRE_FALSE(ec);
        UNSCOPED_INFO("not through a link out");
        CHECK_FALSE(none.mayFromConfig(s.at("project/escape"), s.at("project")));
#endif
    }

    TEST_CASE("aldiskincludes: a require with no extension takes its own files before a folder's init", "[aldiskincludes]")
    {
        typedef std::vector<std::string> names;
        UNSCOPED_INFO("SLua's require");
        CHECK(ALDiskIncludes::namesFor("lib", true, true) ==
                                     names{ "lib", "lib.luau", "lib.lua", "lib/init.luau", "lib/init.lua" });
        const names folder = ALDiskIncludes::namesFor("./shared/lib/", true, true);
        {
            INFO("a path's last folder, however it ends");
            REQUIRE(folder.size() == 5);
            CHECK(folder[3] == "./shared/lib/init.luau");
            CHECK(folder[4] == "./shared/lib/init.lua");
        }
        UNSCOPED_INFO("a name with its extension is that file");
        CHECK(ALDiskIncludes::namesFor("lib.Luau", true, true) ==
                                                              names{ "lib.Luau", "lib.Luau.luau", "lib.Luau.lua" });
        UNSCOPED_INFO("an #include in SLua is a file");
        CHECK(ALDiskIncludes::namesFor("lib", true, false) == names{ "lib", "lib.luau", "lib.lua" });
        UNSCOPED_INFO("an LSL include");
        CHECK(ALDiskIncludes::namesFor("lib", false, false) == names{ "lib", "lib.lsl" });

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
        UNSCOPED_INFO("the folder's module");
        CHECK(first("lib") == init);
        const std::string own = s.write("modules/lib.luau", "return 1\n");
        UNSCOPED_INFO("a file of the name before the folder");
        CHECK(first("lib") == own);
        UNSCOPED_INFO("init by its own name too");
        CHECK(first("lib/init") == init);
    }

    TEST_CASE("aldiskincludes: atTop finds each blessed folder's own configuration in blessing order", "[aldiskincludes]")
    {
        Scratch        s;
        ALDiskIncludes none;
        UNSCOPED_INFO("nothing blessed, nothing found");
        REQUIRE(none.atTop(".luaurc").empty());

        s.write("first/util.luau", "return 1\n");
        const std::string second = s.write("second/.luaurc", "{\"aliases\": {\"lib\": \"./lib\"}}\n");
        const std::string third  = s.write("third/.luaurc", "{}\n");
        s.write("first/deeper/.luaurc", "{}\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("first"));
        blessed.bless(s.at("third"));
        blessed.bless(s.at("second"));
        typedef std::vector<std::string> files;
        UNSCOPED_INFO("each folder's own, in the order blessed, not one further down");
        CHECK(blessed.atTop(".luaurc") == files{ third, second });

        // A folder so named is no file.
        fs::create_directories(s.under("first/.luaurc"));
        UNSCOPED_INFO("a folder so named passed over");
        CHECK(blessed.atTop(".luaurc") == files{ third, second });
#if LL_DARWIN || LL_LINUX
        // A link out of the blessed folders is where it leads, and not read.
        s.write("elsewhere/.luaurc", "{}\n");
        std::error_code ec;
        fs::remove(s.under("first/.luaurc"), ec);
        fs::create_symlink(s.under("elsewhere/.luaurc"), s.under("first/.luaurc"), ec);
        UNSCOPED_INFO("linked: " << ec.message());
        REQUIRE_FALSE(ec);
        UNSCOPED_INFO("not through a link out");
        CHECK(blessed.atTop(".luaurc") == files{ third, second });
#endif
        UNSCOPED_INFO("nor a name no folder has");
        REQUIRE(blessed.atTop(".lslrc").empty());
    }

    TEST_CASE("aldiskincludes: scriptsUnder lists each script of a language once within the limits", "[aldiskincludes]")
    {
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
        UNSCOPED_INFO("SLua's, both folders, through their folders");
        CHECK(names(ALDiskIncludes::scriptsUnder({ s.at("lib"), s.at("tools") }, true, 4, 100)) ==
                      std::string("lib/net/http.luau lib/old.lua lib/util.luau tools/t.luau"));
        UNSCOPED_INFO("LSL's and its includes'");
        CHECK(names(ALDiskIncludes::scriptsUnder({ s.at("lib") }, false, 4, 100)) ==
                      std::string("lib/door.lsl lib/door.lslh"));
        UNSCOPED_INFO("a folder given twice, or under another, each once");
        REQUIRE(ALDiskIncludes::scriptsUnder({ s.at("lib"), s.at("lib/net"), s.at("lib") }, true, 4, 100).size() == size_t(3));
        UNSCOPED_INFO("no more than asked");
        REQUIRE(ALDiskIncludes::scriptsUnder({ s.at("lib"), s.at("tools") }, true, 4, 2).size() == size_t(2));
        UNSCOPED_INFO("not deeper than asked");
        CHECK(names(ALDiskIncludes::scriptsUnder({ s.at("lib") }, true, 0, 100)) ==
                      std::string("lib/old.lua lib/util.luau"));
        UNSCOPED_INFO("nothing of a folder not there");
        REQUIRE(ALDiskIncludes::scriptsUnder({ s.at("nowhere") }, true, 4, 100).empty());
    }

    TEST_CASE("aldiskincludes: extensions in any case; a file blessed alone; paths judged lexically", "[aldiskincludes]")
    {
        typedef std::vector<std::string> names;
        const names&                     lua = ALDiskIncludes::scriptExtensions(true);
        UNSCOPED_INFO("SLua's, the modern first");
        CHECK(lua == names{ ".luau", ".lua" });
        UNSCOPED_INFO("LSL's and its includes'");
        CHECK(ALDiskIncludes::scriptExtensions(false) == names{ ".lsl", ".lslh", ".lsli" });
        UNSCOPED_INFO("in any case");
        CHECK(ALDiskIncludes::extensionOf("Util.LUAU", lua) == size_t(5));
        UNSCOPED_INFO("the old one");
        CHECK(ALDiskIncludes::extensionOf("old.Lua", lua) == size_t(4));
        UNSCOPED_INFO("an include's");
        CHECK(ALDiskIncludes::extensionOf("defs.LSLH", ALDiskIncludes::scriptExtensions(false)) == size_t(5));
        UNSCOPED_INFO("past a name of its own");
        CHECK(ALDiskIncludes::extensionOf(".luau", lua) == size_t(0));
        CHECK(ALDiskIncludes::extensionOf("notes.txt", lua) == size_t(0));
        UNSCOPED_INFO("a require of one in any case is that file");
        CHECK(ALDiskIncludes::namesFor("Util.LUAU", true, true) ==
                                                              names{ "Util.LUAU", "Util.LUAU.luau", "Util.LUAU.lua" });

        Scratch           s;
        const std::string shout  = s.write("lib/Shout.LUAU", "return {}\n");
        const std::string module = s.write("vendor/json.luau", "return {}\n");
        const std::string beside = s.write("vendor/other.luau", "return {}\n");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("lib"));
        const std::vector<ALDiskIncludes::Listed> listed = blessed.filesUnder(s.at("lib"), lua, 1, 100, 100);
        {
            INFO("listed in any case");
            REQUIRE(listed.size() == 1);
            CHECK(listed[0].file == shout);
        }
        blessed.bless(module);
        UNSCOPED_INFO("a file blessed alone");
        CHECK(blessed.admits(module).value_or("") == module);
        UNSCOPED_INFO("nothing beside it");
        REQUIRE_FALSE(blessed.admits(beside));
        UNSCOPED_INFO("and no folder");
        REQUIRE(blessed.folders().size() == size_t(1));

        const std::vector<std::string> folders{ s.at("lib") };
        std::string                    upper = s.at("lib/net/x.luau");
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        UNSCOPED_INFO("under, as written");
        CHECK(ALDiskIncludes::lexicallyUnder(s.at("lib/net/x.luau"), folders));
        UNSCOPED_INFO("in any case");
        CHECK(ALDiskIncludes::lexicallyUnder(upper, folders));
        UNSCOPED_INFO("whatever is there");
        CHECK(ALDiskIncludes::lexicallyUnder(s.at("lib/missing/x.luau"), folders));
        UNSCOPED_INFO("not climbing out");
        CHECK_FALSE(ALDiskIncludes::lexicallyUnder(s.at("lib/../vendor/json.luau"), folders));
        UNSCOPED_INFO("not a folder whose name it only begins with");
        CHECK_FALSE(ALDiskIncludes::lexicallyUnder(s.at("lib2/x.luau"), folders));
        {
            INFO("not another machine's share");
            CHECK_FALSE(ALDiskIncludes::lexicallyUnder("//alincludes.invalid/share/x", folders));
            CHECK_FALSE(ALDiskIncludes::lexicallyUnder("\\\\alincludes.invalid\\share\\x", folders));
        }
        UNSCOPED_INFO("nor a device's path to one under it");
        CHECK_FALSE(ALDiskIncludes::lexicallyUnder("\\\\?\\" + s.at("lib/x.luau"), folders));
        {
            INFO("nor under nothing");
            CHECK_FALSE(ALDiskIncludes::lexicallyUnder(s.at("lib/x.luau"), {}));
            CHECK_FALSE(ALDiskIncludes::lexicallyUnder(std::string(), folders));
        }
        // What a configuration lists of another machine's share: not
        // blessed, and not asked after.
        {
            INFO("a configuration's share");
            CHECK_FALSE(blessed.mayFromConfig("//alincludes.invalid/share", s.at("lib")));
            CHECK_FALSE(blessed.blessFromConfig("\\\\alincludes.invalid\\share", s.at("lib")));
        }
        // Nor by the NT namespace's names, which Windows passes on as they
        // are: a share's, and even a blessed folder's own.
        {
            INFO("the NT namespace's share");
            CHECK_FALSE(blessed.mayFromConfig("\\??\\UNC\\alincludes.invalid\\share", s.at("lib")));
            CHECK_FALSE(blessed.blessFromConfig("/?\?/UNC/alincludes.invalid/share", s.at("lib")));
        }
        fs::create_directories(s.under("lib/net"));
        {
            INFO("its name for a folder under the configuration's");
            CHECK_FALSE(blessed.mayFromConfig("\\??\\" + s.at("lib/net"), s.at("lib")));
            CHECK_FALSE(ALDiskIncludes::lexicallyUnder("\\??\\" + s.at("lib/net"), folders));
        }
        UNSCOPED_INFO("which, as itself, it may name");
        CHECK(blessed.mayFromConfig(s.at("lib/net"), s.at("lib")));
    }

    TEST_CASE("aldiskincludes: a DOS device's name under a blessed folder is refused before the disk is asked", "[aldiskincludes]")
    {
#if LL_WINDOWS
        // Windows opens CON, PRN, AUX, NUL, COM1 to COM9 and LPT1 to LPT9,
        // and COM and LPT with a superscript one, two or three, as the
        // device wherever the name stands in a path, extension or not, so
        // even asking how such a path stands opens it. Nothing here can
        // see that the disk was not asked -- NUL answers, and COM3 may
        // wait on a port -- so this says only what is answered.
        Scratch           s;
        const std::string script = s.write("lib/a.lsl", "integer a;\n");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("lib"));
        REQUIRE_FALSE(blessed.admits(s.at("lib/NUL")));
        REQUIRE_FALSE(blessed.admits(s.at("lib/nul.lsl")));
        REQUIRE_FALSE(blessed.admits(s.at("lib/COM3.lsl")));
        REQUIRE_FALSE(blessed.admits(s.at("lib/lpt1")));
        REQUIRE_FALSE(blessed.admits(s.at("lib/con/a.lsl")));
        REQUIRE_FALSE(blessed.mayFromConfig(s.at("lib/aux"), s.at("lib")));
        // ¹, ² and ³ in UTF-8, as a path's string is.
        for (const char* name : { "COM\xC2\xB9", "com\xC2\xB2.lsl", "COM\xC2\xB3", "LPT\xC2\xB9.lsl", "lpt\xC2\xB2", "LPT\xC2\xB3" })
        {
            INFO(name);
            CHECK_FALSE(blessed.admits(s.at(std::string("lib/") + name)));
            CHECK_FALSE(blessed.mayFromConfig(s.at(std::string("lib/") + name), s.at("lib")));
        }
        UNSCOPED_INFO("a file is still a file");
        CHECK(blessed.admits(script).value_or("") == script);
        // A name that only begins like one is a file.
        const std::string console = s.write("lib/console.lsl", "integer c;\n");
        CHECK(blessed.admits(console).value_or("") == console);
        const std::string com = s.write("lib/com10.lsl", "integer c;\n");
        CHECK(blessed.admits(com).value_or("") == com);
        // Nor is a superscript past three a digit to Windows.
        const std::string four = s.write("lib/com\xE2\x81\xB4.lsl", "integer c;\n");
        CHECK(blessed.admits(four).value_or("") == four);
#endif
    }
}
