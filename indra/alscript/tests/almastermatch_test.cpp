/**
 * @file almastermatch_test.cpp
 * @brief Which file on disk a script in the world may have as its master, against real folders.
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

#include "../masters/almastermatch.h"

#include "altestlinks.h"
#include "fsyspath.h"
#include "llfile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
            root             = fs::temp_directory_path() / ("almastermatch_" + std::to_string(stamp));
            fs::create_directories(root);
            // Where links are followed to: the answers are in those terms.
            root = fs::canonical(root);
        }
        ~Scratch()
        {
            std::error_code ec;
            fs::remove_all(root, ec);
        }

        fs::path under(const std::string& relative) const { return (root / relative).make_preferred(); }

        std::string write(const std::string& relative, const std::string& text = "default { state_entry() { } }\n") const
        {
            const fs::path path = under(relative);
            fs::create_directories(path.parent_path());
            llofstream out(path, std::ios::binary);
            out << text;
            return fsyspath(path).string();
        }
        std::string at(const std::string& relative) const { return fsyspath(under(relative)).string(); }
    };

    using Aliases = std::vector<std::pair<std::string, std::string>>;
}

namespace
{
    struct almastermatch_data
    {
        // What a hint resolves to, or `-` and why.
        static std::string resolved(const std::string& hint, const ALDiskIncludes& blessed, bool lua, const Aliases& aliases = {})
        {
            std::string                      why;
            const std::optional<std::string> file = ALMasterMatch::resolve(hint, blessed, aliases, lua, why);
            if (file)
            {
                return why.empty() ? *file : "said why for a file found: " + why;
            }
            return why.empty() ? std::string("- and nothing said") : "- " + why;
        }

        static std::string joined(const std::vector<std::string>& list)
        {
            std::string out;
            for (const std::string& one : list)
            {
                out += (out.empty() ? "" : ", ") + one;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: a hint is an @file comment in the first ten lines or an upload header's", "[almastermatch]")
    {
        UNSCOPED_INFO("LSL's");
        CHECK(ALMasterMatch::hintOf("// @file net/door.lsl\ndefault {}\n", "", false).value_or("-") == std::string("net/door.lsl"));
        UNSCOPED_INFO("SLua's");
        CHECK(ALMasterMatch::hintOf("-- @file net/door.luau\nprint(1)\n", "", true).value_or("-") == std::string("net/door.luau"));
        UNSCOPED_INFO("indented, close up, in any case, the line's end trimmed");
        CHECK(ALMasterMatch::hintOf("  //@FILE\tnet/door.lsl  \r\n", "", false).value_or("-") == std::string("net/door.lsl"));
        UNSCOPED_INFO("quoted for its spaces");
        CHECK(ALMasterMatch::hintOf("// @file \"my scripts/door.lsl\"\n", "", false).value_or("-") ==
                      std::string("my scripts/door.lsl"));
        UNSCOPED_INFO("past a byte order mark");
        CHECK(ALMasterMatch::hintOf("\xEF\xBB\xBF// @file door.lsl\n", "", false).value_or("-") ==
                      std::string("door.lsl"));

        std::string ninth;
        for (int line = 0; line < 9; ++line)
        {
            ninth += "integer x" + std::to_string(line) + ";\n";
        }
        UNSCOPED_INFO("the tenth line");
        CHECK(ALMasterMatch::hintOf(ninth + "// @file late.lsl\n", "", false).value_or("-") == std::string("late.lsl"));
        UNSCOPED_INFO("not the eleventh");
        REQUIRE_FALSE(ALMasterMatch::hintOf(ninth + "\n// @file late.lsl\n", "", false));
        UNSCOPED_INFO("not the other language's comment");
        REQUIRE_FALSE(ALMasterMatch::hintOf("// @file door.luau\n", "", true));
        UNSCOPED_INFO("nor code");
        REQUIRE_FALSE(ALMasterMatch::hintOf("string f = \"// @file door.lsl\";\n", "", false));
        UNSCOPED_INFO("nor a word that only begins so");
        REQUIRE_FALSE(ALMasterMatch::hintOf("// @filename door.lsl\n", "", false));
        UNSCOPED_INFO("nor a doc comment's third dash");
        REQUIRE_FALSE(ALMasterMatch::hintOf("--- @file door.luau\n", "", true));
        UNSCOPED_INFO("nor one naming nothing");
        REQUIRE_FALSE(ALMasterMatch::hintOf("// @file   \n", "", false));
        UNSCOPED_INFO("passing one naming nothing for the next");
        CHECK(ALMasterMatch::hintOf("// @file\t\n// @file door.lsl\n", "", false).value_or("-") == std::string("door.lsl"));

        const std::string ours = "// ================ alchemy meta ================\n"
                                 "// @file net/door.lsl\n"
                                 "// @hash xxh128:00\n"
                                 "// ==============================================\n";
        const std::string theirs = "-- ============== sl-vscode-plugin meta ==============\n"
                                   "-- @file src/door.luau\n"
                                   "-- @hash 00\n"
                                   "-- ==================================================\n";
        UNSCOPED_INFO("our header in the compiled half");
        CHECK(ALMasterMatch::hintOf("default {}\n", ours + "default {}\n", false).value_or("-") ==
                      std::string("net/door.lsl"));
        UNSCOPED_INFO("past the target line");
        CHECK(ALMasterMatch::hintOf("default {}\n", "//mono\n" + ours + "default {}\n", false).value_or("-") ==
                      std::string("net/door.lsl"));
        UNSCOPED_INFO("the plugin's");
        CHECK(ALMasterMatch::hintOf("print(1)\n", "--luau\n" + theirs + "print(1)\n", true).value_or("-") ==
                      std::string("src/door.luau"));
        UNSCOPED_INFO("the source's own first");
        CHECK(ALMasterMatch::hintOf("// @file mine.lsl\n", ours, false).value_or("-") == std::string("mine.lsl"));
        UNSCOPED_INFO("not a header further down");
        REQUIRE_FALSE(ALMasterMatch::hintOf("default {}\n", "default {}\n" + ours, false));
        UNSCOPED_INFO("nor one with no @file");
        REQUIRE_FALSE(ALMasterMatch::hintOf("x\n", "// ================ alchemy meta ================\n// @hash xxh128:00\n// ====\n", false));
        UNSCOPED_INFO("nothing where nothing says");
        REQUIRE_FALSE(ALMasterMatch::hintOf("default {}\n", "default {}\n", false));
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: a relative hint resolves in the first blessed folder that has it", "[almastermatch]")
    {
        Scratch           s;
        const std::string first = s.write("a/net/door.lsl");
        const std::string only  = s.write("b/only.lsl");
        s.write("b/net/door.lsl");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("a"));
        blessed.bless(s.at("b"));

        UNSCOPED_INFO("in both, the first folder's");
        CHECK(almastermatch_data::resolved("net/door.lsl", blessed, false) == first);
        UNSCOPED_INFO("with Windows's separators");
        CHECK(almastermatch_data::resolved("net\\door.lsl", blessed, false) == first);
        UNSCOPED_INFO("in the second only, the second's");
        CHECK(almastermatch_data::resolved("only.lsl", blessed, false) == only);
        UNSCOPED_INFO("a `.` on the way");
        CHECK(almastermatch_data::resolved("./net/./door.lsl", blessed, false) == first);
        UNSCOPED_INFO("in neither");
        CHECK(almastermatch_data::resolved("missing.lsl", blessed, false) ==
                      std::string("- 'missing.lsl' is not a file in the include folders, or not an ordinary file of at most 4 MB"));
        fs::create_directories(s.under("a/folder.lsl"));
        UNSCOPED_INFO("a folder is no file, whatever it is called");
        CHECK(almastermatch_data::resolved("folder.lsl", blessed, false).substr(0, 1) ==
                      std::string("-"));

        ALDiskIncludes none;
        UNSCOPED_INFO("no folder blessed, looked for nowhere");
        CHECK(almastermatch_data::resolved("only.lsl", none, false) ==
                      std::string("- no include folders are set, so 'only.lsl' is looked for nowhere"));
        UNSCOPED_INFO("a hint naming nothing");
        CHECK(almastermatch_data::resolved("  ", blessed, false) == std::string("- the @file hint names no file"));
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: no climb or root or link or share reaches outside the blessed folders", "[almastermatch]")
    {
        Scratch           s;
        const std::string secret = s.write("private/secret.lsl", "// the author's own\n");
        const std::string inside = s.write("a/net/door.lsl");
        const std::string other  = s.write("b/other.lsl");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("a"));
        blessed.bless(s.at("b"));

        UNSCOPED_INFO("climbing out of every blessed folder");
        CHECK(almastermatch_data::resolved("../private/secret.lsl", blessed, false) ==
                      std::string("- '../private/secret.lsl' is outside the include folders"));
        UNSCOPED_INFO("however deep it starts");
        CHECK(almastermatch_data::resolved("net/../../private/secret.lsl", blessed, false) ==
                      std::string("- 'net/../../private/secret.lsl' is outside the include folders"));
        UNSCOPED_INFO("climbing within one is no climb");
        CHECK(almastermatch_data::resolved("net/../net/door.lsl", blessed, false) == inside);
        UNSCOPED_INFO("nor into another blessed folder, as an include could read it");
        CHECK(almastermatch_data::resolved("../b/other.lsl", blessed, false) == other);

        UNSCOPED_INFO("from a root, under a blessed folder");
        CHECK(almastermatch_data::resolved(inside, blessed, false) == inside);
        UNSCOPED_INFO("from a root, outside");
        CHECK(almastermatch_data::resolved(secret, blessed, false) ==
                      "- '" + secret + "' is outside the include folders");
        UNSCOPED_INFO("from a root, climbing out");
        CHECK(almastermatch_data::resolved(s.at("a/../private/secret.lsl"), blessed, false).substr(0, 1) ==
                      std::string("-"));
        for (const char* elsewhere : { "\\\\host\\share\\door.lsl", "//host/share/door.lsl", "\\\\?\\UNC\\host\\share\\door.lsl", "C:\\door.lsl",
                                       "c:door.lsl" })
        {
            CAPTURE(elsewhere);
            REQUIRE(almastermatch_data::resolved(elsewhere, blessed, false) ==
                          "- '" + std::string(elsewhere) + "' is outside the include folders");
        }
        UNSCOPED_INFO("nor a home folder");
        CHECK(almastermatch_data::resolved("~/door.lsl", blessed, false) ==
                      std::string("- '~/door.lsl' is from a home folder, which a hint is not followed to"));

#if LL_WINDOWS
        // Windows' own link to a folder, which any user may make, so it is
        // tried on every Windows machine, where a symlink may not be.
        UNSCOPED_INFO("a junction to a folder outside");
        REQUIRE(!ll_test::createDirectoryJunction(s.root / "private", s.root / "a" / "hatch"));
        CHECK(almastermatch_data::resolved("hatch/secret.lsl", blessed, false).substr(0, 1) == std::string("-"));
        UNSCOPED_INFO("a junction to another blessed folder is followed");
        REQUIRE(!ll_test::createDirectoryJunction(s.root / "b", s.root / "a" / "within"));
        CHECK(almastermatch_data::resolved("within/other.lsl", blessed, false) == other);
#endif
        std::error_code ec;
        fs::create_symlink(fsyspath(secret), s.root / "a" / "innocent.lsl", ec);
        if (ec)
        {
            SKIP("no symlinks here: " << ec.message());
        }
        UNSCOPED_INFO("a link out of the folder");
        CHECK(almastermatch_data::resolved("innocent.lsl", blessed, false).substr(0, 1) == std::string("-"));
        fs::create_directory_symlink(s.root / "private", s.root / "a" / "door", ec);
        REQUIRE(!ec);
        UNSCOPED_INFO("nor a file through a linked folder");
        CHECK(almastermatch_data::resolved("door/secret.lsl", blessed, false).substr(0, 1) ==
                      std::string("-"));
        fs::create_symlink(fsyspath(other), s.root / "a" / "inward.lsl", ec);
        REQUIRE(!ec);
        UNSCOPED_INFO("a link to another blessed folder is followed");
        CHECK(almastermatch_data::resolved("inward.lsl", blessed, false) == other);
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: only a script of the item's language and a sensible size", "[almastermatch]")
    {
        Scratch           s;
        const std::string notes  = s.write("a/notes.txt", "keys\n");
        const std::string door   = s.write("a/door.lsl");
        const std::string header = s.write("a/lib.lslh", "integer f() { return 1; }\n");
        const std::string util   = s.write("a/util.luau", "return {}\n");
        const std::string old    = s.write("a/old.lua", "return {}\n");
        s.write("a/big.lsl", std::string(ALDiskIncludes::MAX_BYTES + 1, ' '));
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));

        UNSCOPED_INFO("not another kind of file");
        CHECK(almastermatch_data::resolved("notes.txt", blessed, false) ==
                      std::string("- 'notes.txt' is not an LSL script (.lsl, .lslh or .lsli)"));
        UNSCOPED_INFO("nor a name with no extension");
        CHECK(almastermatch_data::resolved("door", blessed, false).substr(0, 1) == std::string("-"));
        UNSCOPED_INFO("LSL's own");
        CHECK(almastermatch_data::resolved("door.lsl", blessed, false) == door);
        // As the disk finds it: where it is the same file in any case, by
        // the case the disk keeps it in.
        const std::string upper = almastermatch_data::resolved("door.LSL", blessed, false);
        UNSCOPED_INFO("an extension in any case");
        CHECK((fs::exists(s.under("a/door.LSL")) ? upper == door : upper.substr(0, 1) == "-"));
        UNSCOPED_INFO("and its includes'");
        CHECK(almastermatch_data::resolved("lib.lslh", blessed, false) == header);
        UNSCOPED_INFO("not SLua's for LSL");
        CHECK(almastermatch_data::resolved("util.luau", blessed, false) ==
                      std::string("- 'util.luau' is not an LSL script (.lsl, .lslh or .lsli)"));
        UNSCOPED_INFO("nor LSL's for SLua");
        CHECK(almastermatch_data::resolved("door.lsl", blessed, true) ==
                      std::string("- 'door.lsl' is not an SLua script (.luau or .lua)"));
        UNSCOPED_INFO("SLua's own");
        CHECK(almastermatch_data::resolved("util.luau", blessed, true) == util);
        UNSCOPED_INFO("and its older");
        CHECK(almastermatch_data::resolved("old.lua", blessed, true) == old);
        UNSCOPED_INFO("not one too big");
        CHECK(almastermatch_data::resolved("big.lsl", blessed, false) ==
                      std::string("- 'big.lsl' is not a file in the include folders, or not an ordinary file of at most 4 MB"));

        std::error_code ec;
        fs::create_symlink(fsyspath(notes), s.root / "a" / "fake.lsl", ec);
        if (ec)
        {
            SKIP("no symlinks here: " << ec.message());
        }
        UNSCOPED_INFO("a script's name for another kind of file");
        CHECK(almastermatch_data::resolved("fake.lsl", blessed, false) ==
                      std::string("- 'fake.lsl' leads to a file that is not an LSL script (.lsl, .lslh or .lsli)"));
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: an alias names a folder but is held to the blessed folders all the same", "[almastermatch]")
    {
        Scratch           s;
        const std::string util = s.write("a/lib/util.luau", "return {}\n");
        s.write("private/x.luau", "return {}\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));
        const Aliases aliases{ { "lib", s.at("a/lib") }, { "@out", s.at("private") }, { "loose", "lib" } };

        UNSCOPED_INFO("through the alias");
        CHECK(almastermatch_data::resolved("@lib/util.luau", blessed, true, aliases) == util);
        UNSCOPED_INFO("named in any case");
        CHECK(almastermatch_data::resolved("@LIB/util.luau", blessed, true, aliases) == util);
        UNSCOPED_INFO("one not set");
        CHECK(almastermatch_data::resolved("@nope/util.luau", blessed, true, aliases) ==
                      std::string("- '@nope/util.luau' names the alias @nope, which is not set"));
        UNSCOPED_INFO("one whose folder is not blessed");
        CHECK(almastermatch_data::resolved("@out/x.luau", blessed, true, aliases) ==
                      std::string("- '@out/x.luau' is outside the include folders"));
        UNSCOPED_INFO("one climbed out of");
        CHECK(almastermatch_data::resolved("@lib/../../private/x.luau", blessed, true, aliases) ==
                      std::string("- '@lib/../../private/x.luau' is outside the include folders"));
        UNSCOPED_INFO("one that is no folder on disk");
        CHECK(almastermatch_data::resolved("@loose/util.luau", blessed, true, aliases) ==
                      std::string("- the alias @loose is no folder on disk"));
        UNSCOPED_INFO("the alias alone is no file");
        CHECK(almastermatch_data::resolved("@lib", blessed, true, aliases).substr(0, 1) == std::string("-"));
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: byName lists <name>.<ext> then the plugin's loose forms best first", "[almastermatch]")
    {
        Scratch           s;
        const std::string door     = s.write("a/door.lsl");
        const std::string net_door = s.write("a/net/door.lsl");
        const std::string b_door   = s.write("b/door.lsl");
        const std::string gate     = s.write("a/net/gate.lsl");
        const std::string flat     = s.write("a/net_gate.lsl");
        const std::string lamp     = s.write("a/net/lamp.lsl");
        const std::string big_lamp = s.write("a/Lamp.lsl");
        s.write("a/.git/door.lsl");
        s.write("a/util.lslh");
        s.write("a/door.txt");
        s.write("private/door.lsl");
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));
        blessed.bless(s.at("b"));

        UNSCOPED_INFO("the folder blessed first, then the nearer its top; none hidden, none not blessed");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("door", blessed, false)) ==
                      almastermatch_data::joined({ door, net_door, b_door }));
        UNSCOPED_INFO("a name with a folder in it, by its last parts");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("net/door", blessed, false)) ==
                      net_door);
        UNSCOPED_INFO("a name with its extension already");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("door.lsl", blessed, false)) ==
                      almastermatch_data::joined({ door, net_door, b_door }));
        UNSCOPED_INFO("a name, then its folders run into it with _");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("net_gate", blessed, false)) ==
                      almastermatch_data::joined({ flat, gate }));
        UNSCOPED_INFO("or with nothing");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("netgate", blessed, false)) == gate);
        UNSCOPED_INFO("or with a space");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("net gate", blessed, false)) == gate);
        UNSCOPED_INFO("its own case first, then any");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("lamp", blessed, false)) ==
                      almastermatch_data::joined({ lamp, big_lamp }));
        UNSCOPED_INFO("not an include's header");
        REQUIRE(ALMasterMatch::byName("util", blessed, false).empty());
        UNSCOPED_INFO("nor for no name");
        REQUIRE(ALMasterMatch::byName("", blessed, false).empty());
        UNSCOPED_INFO("nor in no folder");
        REQUIRE(ALMasterMatch::byName("door", ALDiskIncludes(), false).empty());

        const std::vector<ALDiskIncludes::Listed> listed = ALMasterMatch::listing(blessed, false);
        UNSCOPED_INFO("listed once for many names, the same");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("door", listed, false)) ==
                      almastermatch_data::joined(ALMasterMatch::byName("door", blessed, false)));
        UNSCOPED_INFO("and given up where asked");
        REQUIRE(ALMasterMatch::listing(blessed, false, [] { return true; }).empty());
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: by name in SLua: .luau then .lua", "[almastermatch]")
    {
        Scratch           s;
        const std::string luau = s.write("a/util.luau", "return {}\n");
        const std::string lua  = s.write("a/util.lua", "return {}\n");
        const std::string deep = s.write("a/net/util.luau", "return {}\n");
        s.write("a/util.lsl");
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));
        UNSCOPED_INFO("the modern first, nearer the top first among those");
        CHECK(almastermatch_data::joined(ALMasterMatch::byName("util", blessed, true)) == almastermatch_data::joined({ luau, deep, lua }));
        UNSCOPED_INFO("and LSL's for LSL");
        REQUIRE(ALMasterMatch::byName("util", blessed, false).size() == size_t(1));
    }

    TEST_CASE_METHOD(almastermatch_data, "almastermatch: a notecard's master is text or notecard or JSON in any case; never a script", "[almastermatch]")
    {
        const std::vector<std::string>& notecards = ALMasterMatch::notecardExtensions();
        CHECK(ALDiskIncludes::extensionOf("readme.txt", notecards) != 0);
        CHECK(ALDiskIncludes::extensionOf("Config.NOTECARD", notecards) != 0);
        CHECK(ALDiskIncludes::extensionOf("data/settings.json", notecards) != 0);
        CHECK(ALDiskIncludes::extensionOf("door.lsl", notecards) == 0);
        CHECK(ALDiskIncludes::extensionOf("door.luau", notecards) == 0);
    }
}
