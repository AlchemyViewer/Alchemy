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

#include "fsyspath.h"
#include "llfile.h"

#include "../test/lltut.h"

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

namespace tut
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

    typedef test_group<almastermatch_data> almastermatch_group;
    typedef almastermatch_group::object    almastermatch_object;
    almastermatch_group                    almastermatch_instance("almastermatch");

    template<> template<>
    void almastermatch_object::test<1>()
    {
        set_test_name("a hint is an @file comment among the first ten lines of the source, else an upload header's");
        ensure_equals("LSL's", ALMasterMatch::hintOf("// @file net/door.lsl\ndefault {}\n", "", false).value_or("-"), std::string("net/door.lsl"));
        ensure_equals("SLua's", ALMasterMatch::hintOf("-- @file net/door.luau\nprint(1)\n", "", true).value_or("-"), std::string("net/door.luau"));
        ensure_equals("indented, close up, in any case, the line's end trimmed",
                      ALMasterMatch::hintOf("  //@FILE\tnet/door.lsl  \r\n", "", false).value_or("-"), std::string("net/door.lsl"));
        ensure_equals("quoted for its spaces", ALMasterMatch::hintOf("// @file \"my scripts/door.lsl\"\n", "", false).value_or("-"),
                      std::string("my scripts/door.lsl"));
        ensure_equals("past a byte order mark", ALMasterMatch::hintOf("\xEF\xBB\xBF// @file door.lsl\n", "", false).value_or("-"),
                      std::string("door.lsl"));

        std::string ninth;
        for (int line = 0; line < 9; ++line)
        {
            ninth += "integer x" + std::to_string(line) + ";\n";
        }
        ensure_equals("the tenth line", ALMasterMatch::hintOf(ninth + "// @file late.lsl\n", "", false).value_or("-"), std::string("late.lsl"));
        ensure("not the eleventh", !ALMasterMatch::hintOf(ninth + "\n// @file late.lsl\n", "", false));
        ensure("not the other language's comment", !ALMasterMatch::hintOf("// @file door.luau\n", "", true));
        ensure("nor code", !ALMasterMatch::hintOf("string f = \"// @file door.lsl\";\n", "", false));
        ensure("nor a word that only begins so", !ALMasterMatch::hintOf("// @filename door.lsl\n", "", false));
        ensure("nor a doc comment's third dash", !ALMasterMatch::hintOf("--- @file door.luau\n", "", true));
        ensure("nor one naming nothing", !ALMasterMatch::hintOf("// @file   \n", "", false));
        ensure_equals("passing one naming nothing for the next",
                      ALMasterMatch::hintOf("// @file\t\n// @file door.lsl\n", "", false).value_or("-"), std::string("door.lsl"));

        const std::string ours = "// ================ alchemy meta ================\n"
                                 "// @file net/door.lsl\n"
                                 "// @hash xxh128:00\n"
                                 "// ==============================================\n";
        const std::string theirs = "-- ============== sl-vscode-plugin meta ==============\n"
                                   "-- @file src/door.luau\n"
                                   "-- @hash 00\n"
                                   "-- ==================================================\n";
        ensure_equals("our header in the compiled half", ALMasterMatch::hintOf("default {}\n", ours + "default {}\n", false).value_or("-"),
                      std::string("net/door.lsl"));
        ensure_equals("past the target line", ALMasterMatch::hintOf("default {}\n", "//mono\n" + ours + "default {}\n", false).value_or("-"),
                      std::string("net/door.lsl"));
        ensure_equals("the plugin's", ALMasterMatch::hintOf("print(1)\n", "--luau\n" + theirs + "print(1)\n", true).value_or("-"),
                      std::string("src/door.luau"));
        ensure_equals("the source's own first", ALMasterMatch::hintOf("// @file mine.lsl\n", ours, false).value_or("-"), std::string("mine.lsl"));
        ensure("not a header further down", !ALMasterMatch::hintOf("default {}\n", "default {}\n" + ours, false));
        ensure("nor one with no @file",
               !ALMasterMatch::hintOf("x\n", "// ================ alchemy meta ================\n// @hash xxh128:00\n// ====\n", false));
        ensure("nothing where nothing says", !ALMasterMatch::hintOf("default {}\n", "default {}\n", false));
    }

    template<> template<>
    void almastermatch_object::test<2>()
    {
        set_test_name("a relative hint is looked for in each blessed folder in turn, and the first found wins");
        Scratch           s;
        const std::string first = s.write("a/net/door.lsl");
        const std::string only  = s.write("b/only.lsl");
        s.write("b/net/door.lsl");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("a"));
        blessed.bless(s.at("b"));

        ensure_equals("in both, the first folder's", almastermatch_data::resolved("net/door.lsl", blessed, false), first);
        ensure_equals("with Windows's separators", almastermatch_data::resolved("net\\door.lsl", blessed, false), first);
        ensure_equals("in the second only, the second's", almastermatch_data::resolved("only.lsl", blessed, false), only);
        ensure_equals("a `.` on the way", almastermatch_data::resolved("./net/./door.lsl", blessed, false), first);
        ensure_equals("in neither", almastermatch_data::resolved("missing.lsl", blessed, false),
                      std::string("- 'missing.lsl' is not a file in the include folders, or not an ordinary file of at most 4 MB"));
        fs::create_directories(s.under("a/folder.lsl"));
        ensure_equals("a folder is no file, whatever it is called", almastermatch_data::resolved("folder.lsl", blessed, false).substr(0, 1),
                      std::string("-"));

        ALDiskIncludes none;
        ensure_equals("no folder blessed, looked for nowhere", almastermatch_data::resolved("only.lsl", none, false),
                      std::string("- no include folders are set, so 'only.lsl' is looked for nowhere"));
        ensure_equals("a hint naming nothing", almastermatch_data::resolved("  ", blessed, false), std::string("- the @file hint names no file"));
    }

    template<> template<>
    void almastermatch_object::test<3>()
    {
        set_test_name("nothing is reached outside the blessed folders: not by climbing, from a root, through a link, nor a share");
        Scratch           s;
        const std::string secret = s.write("private/secret.lsl", "// the author's own\n");
        const std::string inside = s.write("a/net/door.lsl");
        const std::string other  = s.write("b/other.lsl");
        ALDiskIncludes    blessed;
        blessed.bless(s.at("a"));
        blessed.bless(s.at("b"));

        ensure_equals("climbing out of every blessed folder", almastermatch_data::resolved("../private/secret.lsl", blessed, false),
                      std::string("- '../private/secret.lsl' is outside the include folders"));
        ensure_equals("however deep it starts", almastermatch_data::resolved("net/../../private/secret.lsl", blessed, false),
                      std::string("- 'net/../../private/secret.lsl' is outside the include folders"));
        ensure_equals("climbing within one is no climb", almastermatch_data::resolved("net/../net/door.lsl", blessed, false), inside);
        ensure_equals("nor into another blessed folder, as an include could read it",
                      almastermatch_data::resolved("../b/other.lsl", blessed, false), other);

        ensure_equals("from a root, under a blessed folder", almastermatch_data::resolved(inside, blessed, false), inside);
        ensure_equals("from a root, outside", almastermatch_data::resolved(secret, blessed, false),
                      "- '" + secret + "' is outside the include folders");
        ensure_equals("from a root, climbing out", almastermatch_data::resolved(s.at("a/../private/secret.lsl"), blessed, false).substr(0, 1),
                      std::string("-"));
        for (const char* elsewhere : { "\\\\host\\share\\door.lsl", "//host/share/door.lsl", "\\\\?\\UNC\\host\\share\\door.lsl", "C:\\door.lsl",
                                       "c:door.lsl" })
        {
            ensure_equals(std::string("never asked of: ") + elsewhere, almastermatch_data::resolved(elsewhere, blessed, false),
                          "- '" + std::string(elsewhere) + "' is outside the include folders");
        }
        ensure_equals("nor a home folder", almastermatch_data::resolved("~/door.lsl", blessed, false),
                      std::string("- '~/door.lsl' is from a home folder, which a hint is not followed to"));

        std::error_code ec;
        fs::create_symlink(fsyspath(secret), s.root / "a" / "innocent.lsl", ec);
        if (!ec)
        {
            ensure_equals("a link out of the folder", almastermatch_data::resolved("innocent.lsl", blessed, false).substr(0, 1), std::string("-"));
        }
        fs::create_directory_symlink(s.root / "private", s.root / "a" / "door", ec);
        if (!ec)
        {
            ensure_equals("nor a file through a linked folder", almastermatch_data::resolved("door/secret.lsl", blessed, false).substr(0, 1),
                          std::string("-"));
        }
        fs::create_symlink(fsyspath(other), s.root / "a" / "inward.lsl", ec);
        if (!ec)
        {
            ensure_equals("a link to another blessed folder is followed", almastermatch_data::resolved("inward.lsl", blessed, false), other);
        }
    }

    template<> template<>
    void almastermatch_object::test<4>()
    {
        set_test_name("only a script of the item's language, of a sensible size");
        Scratch           s;
        const std::string notes  = s.write("a/notes.txt", "keys\n");
        const std::string door   = s.write("a/door.lsl");
        const std::string header = s.write("a/lib.lslh", "integer f() { return 1; }\n");
        const std::string util   = s.write("a/util.luau", "return {}\n");
        const std::string old    = s.write("a/old.lua", "return {}\n");
        s.write("a/big.lsl", std::string(ALDiskIncludes::MAX_BYTES + 1, ' '));
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));

        ensure_equals("not another kind of file", almastermatch_data::resolved("notes.txt", blessed, false),
                      std::string("- 'notes.txt' is not an LSL script (.lsl, .lslh or .lsli)"));
        ensure_equals("nor a name with no extension", almastermatch_data::resolved("door", blessed, false).substr(0, 1), std::string("-"));
        ensure_equals("LSL's own", almastermatch_data::resolved("door.lsl", blessed, false), door);
        // As the disk finds it: where it is the same file in any case, by
        // the case the disk keeps it in.
        const std::string upper = almastermatch_data::resolved("door.LSL", blessed, false);
        ensure("an extension in any case", fs::exists(s.under("a/door.LSL")) ? upper == door : upper.substr(0, 1) == "-");
        ensure_equals("and its includes'", almastermatch_data::resolved("lib.lslh", blessed, false), header);
        ensure_equals("not SLua's for LSL", almastermatch_data::resolved("util.luau", blessed, false),
                      std::string("- 'util.luau' is not an LSL script (.lsl, .lslh or .lsli)"));
        ensure_equals("nor LSL's for SLua", almastermatch_data::resolved("door.lsl", blessed, true),
                      std::string("- 'door.lsl' is not an SLua script (.luau or .lua)"));
        ensure_equals("SLua's own", almastermatch_data::resolved("util.luau", blessed, true), util);
        ensure_equals("and its older", almastermatch_data::resolved("old.lua", blessed, true), old);
        ensure_equals("not one too big", almastermatch_data::resolved("big.lsl", blessed, false),
                      std::string("- 'big.lsl' is not a file in the include folders, or not an ordinary file of at most 4 MB"));

        std::error_code ec;
        fs::create_symlink(fsyspath(notes), s.root / "a" / "fake.lsl", ec);
        if (!ec)
        {
            ensure_equals("a script's name for another kind of file", almastermatch_data::resolved("fake.lsl", blessed, false),
                          std::string("- 'fake.lsl' leads to a file that is not an LSL script (.lsl, .lslh or .lsli)"));
        }
    }

    template<> template<>
    void almastermatch_object::test<5>()
    {
        set_test_name("an alias names a folder of the studio's or a configuration's, and is held to the blessed folders all the same");
        Scratch           s;
        const std::string util = s.write("a/lib/util.luau", "return {}\n");
        s.write("private/x.luau", "return {}\n");
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));
        const Aliases aliases{ { "lib", s.at("a/lib") }, { "@out", s.at("private") }, { "loose", "lib" } };

        ensure_equals("through the alias", almastermatch_data::resolved("@lib/util.luau", blessed, true, aliases), util);
        ensure_equals("named in any case", almastermatch_data::resolved("@LIB/util.luau", blessed, true, aliases), util);
        ensure_equals("one not set", almastermatch_data::resolved("@nope/util.luau", blessed, true, aliases),
                      std::string("- '@nope/util.luau' names the alias @nope, which is not set"));
        ensure_equals("one whose folder is not blessed", almastermatch_data::resolved("@out/x.luau", blessed, true, aliases),
                      std::string("- '@out/x.luau' is outside the include folders"));
        ensure_equals("one climbed out of", almastermatch_data::resolved("@lib/../../private/x.luau", blessed, true, aliases),
                      std::string("- '@lib/../../private/x.luau' is outside the include folders"));
        ensure_equals("one that is no folder on disk", almastermatch_data::resolved("@loose/util.luau", blessed, true, aliases),
                      std::string("- the alias @loose is no folder on disk"));
        ensure_equals("the alias alone is no file", almastermatch_data::resolved("@lib", blessed, true, aliases).substr(0, 1), std::string("-"));
    }

    template<> template<>
    void almastermatch_object::test<6>()
    {
        set_test_name("by name: <name>.<ext> anywhere under a blessed folder, then the plugin's loose forms, every one best first");
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

        ensure_equals("the folder blessed first, then the nearer its top; none hidden, none not blessed",
                      almastermatch_data::joined(ALMasterMatch::byName("door", blessed, false)),
                      almastermatch_data::joined({ door, net_door, b_door }));
        ensure_equals("a name with a folder in it, by its last parts", almastermatch_data::joined(ALMasterMatch::byName("net/door", blessed, false)),
                      net_door);
        ensure_equals("a name with its extension already", almastermatch_data::joined(ALMasterMatch::byName("door.lsl", blessed, false)),
                      almastermatch_data::joined({ door, net_door, b_door }));
        ensure_equals("a name, then its folders run into it with _", almastermatch_data::joined(ALMasterMatch::byName("net_gate", blessed, false)),
                      almastermatch_data::joined({ flat, gate }));
        ensure_equals("or with nothing", almastermatch_data::joined(ALMasterMatch::byName("netgate", blessed, false)), gate);
        ensure_equals("or with a space", almastermatch_data::joined(ALMasterMatch::byName("net gate", blessed, false)), gate);
        ensure_equals("its own case first, then any", almastermatch_data::joined(ALMasterMatch::byName("lamp", blessed, false)),
                      almastermatch_data::joined({ lamp, big_lamp }));
        ensure("not an include's header", ALMasterMatch::byName("util", blessed, false).empty());
        ensure("nor for no name", ALMasterMatch::byName("", blessed, false).empty());
        ensure("nor in no folder", ALMasterMatch::byName("door", ALDiskIncludes(), false).empty());

        const std::vector<ALDiskIncludes::Listed> listed = ALMasterMatch::listing(blessed, false);
        ensure_equals("listed once for many names, the same",
                      almastermatch_data::joined(ALMasterMatch::byName("door", listed, false)),
                      almastermatch_data::joined(ALMasterMatch::byName("door", blessed, false)));
        ensure("and given up where asked", ALMasterMatch::listing(blessed, false, [] { return true; }).empty());
    }

    template<> template<>
    void almastermatch_object::test<7>()
    {
        set_test_name("by name in SLua: .luau, then .lua");
        Scratch           s;
        const std::string luau = s.write("a/util.luau", "return {}\n");
        const std::string lua  = s.write("a/util.lua", "return {}\n");
        const std::string deep = s.write("a/net/util.luau", "return {}\n");
        s.write("a/util.lsl");
        ALDiskIncludes blessed;
        blessed.bless(s.at("a"));
        ensure_equals("the modern first, nearer the top first among those",
                      almastermatch_data::joined(ALMasterMatch::byName("util", blessed, true)), almastermatch_data::joined({ luau, deep, lua }));
        ensure_equals("and LSL's for LSL", ALMasterMatch::byName("util", blessed, false).size(), size_t(1));
    }

    template<> template<>
    void almastermatch_object::test<8>()
    {
        set_test_name("a notecard's master is text, a notecard's own, or JSON, in any case, and never a script");
        const std::vector<std::string>& notecards = ALMasterMatch::notecardExtensions();
        ensure("text", ALDiskIncludes::extensionOf("readme.txt", notecards) != 0);
        ensure("a notecard", ALDiskIncludes::extensionOf("Config.NOTECARD", notecards) != 0);
        ensure("JSON", ALDiskIncludes::extensionOf("data/settings.json", notecards) != 0);
        ensure("not a script", ALDiskIncludes::extensionOf("door.lsl", notecards) == 0);
        ensure("nor SLua", ALDiskIncludes::extensionOf("door.luau", notecards) == 0);
    }
}
