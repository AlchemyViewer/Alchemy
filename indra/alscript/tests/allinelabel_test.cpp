/**
 * @file allinelabel_test.cpp
 * @brief Tests for ALLineLabel: what a `@line` comment names each file by, from the script's own folder.
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

#include "../preprocessor/allinelabel.h"
#include "../preprocessor/alpreprocessor.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace
{
    ALSourceMap::File fileOf(const std::string& name, const std::string& path)
    {
        ALSourceMap::File f;
        f.name = name;
        f.path = path;
        return f;
    }

    // Whether a label reads as a path from a root: `/`, a drive or a share.
    bool fromRoot(const std::string& label)
    {
        return (!label.empty() && (label[0] == '/' || label[0] == '\\')) || label.find(':') != std::string::npos;
    }
}

namespace
{
    struct allinelabel_data
    {
        // A resolver over a table of named texts, each with its identity.
        std::map<std::string, ALPreprocessor::Include> files;

        void add(const std::string& ask, const std::string& name, const std::string& path, const std::string& text)
        {
            ALPreprocessor::Include inc;
            inc.text   = text;
            inc.name   = name;
            inc.path   = path;
            files[ask] = inc;
        }

        ALPreprocessor::Options options(bool lua)
        {
            ALPreprocessor::Options o;
            o.lua          = lua;
            o.fileName     = lua ? "main.luau" : "main.lsl";
            o.unixTime     = 1234567890;
            o.lineComments = true;
            o.resolve      = [this](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
                const auto it = files.find(ask.name);
                if (it == files.end())
                {
                    return ALPreprocessor::Found::No;
                }
                out = it->second;
                return ALPreprocessor::Found::Yes;
            };
            return o;
        }
    };

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: a POSIX path from a folder goes down with `/` and up with `..`; case counts", "[allinelabel]")
    {
        UNSCOPED_INFO("beside");
        CHECK(ALLineLabel::relative("/home/me/proj", "/home/me/proj/util.luau") == std::string("util.luau"));
        UNSCOPED_INFO("down");
        CHECK(ALLineLabel::relative("/home/me/proj", "/home/me/proj/lib/util.luau") == std::string("lib/util.luau"));
        UNSCOPED_INFO("up and across");
        CHECK(ALLineLabel::relative("/home/me/proj", "/home/me/shared/util.luau") == std::string("../shared/util.luau"));
        UNSCOPED_INFO("up to the root");
        CHECK(ALLineLabel::relative("/home/me/proj", "/opt/lib/util.lsl") == std::string("../../../opt/lib/util.lsl"));
        UNSCOPED_INFO("a trailing slash and `.` passed over, `..` taken back");
        CHECK(ALLineLabel::relative("/home/me/proj/./", "/home/me/x/../proj/lib//util.luau") ==
                      std::string("lib/util.luau"));
        UNSCOPED_INFO("a name it only begins with is no folder of it");
        CHECK(ALLineLabel::relative("/home/me/proj", "/home/me/project/util.luau") ==
                      std::string("../project/util.luau"));
        UNSCOPED_INFO("another case another folder");
        CHECK(ALLineLabel::relative("/home/me/proj", "/home/me/Proj/util.luau") == std::string("../Proj/util.luau"));
        UNSCOPED_INFO("a backslash no separator");
        CHECK(ALLineLabel::relative("/home/me", "/home/me/a\\b.lsl") == std::string("a\\b.lsl"));
        UNSCOPED_INFO("the folder itself");
        REQUIRE(ALLineLabel::relative("/home/me/proj", "/home/me/proj/").empty());
        UNSCOPED_INFO("a folder it is in");
        REQUIRE(ALLineLabel::relative("/home/me/proj", "/home/me").empty());
        {
            INFO("not from a root");
            REQUIRE(ALLineLabel::relative("proj", "proj/util.luau").empty());
            REQUIRE(ALLineLabel::relative("/home/me", "util.luau").empty());
        }
        REQUIRE(ALLineLabel::relative("", "").empty());
    }

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: a Windows path from a folder takes either separator and case and long forms", "[allinelabel]")
    {
        UNSCOPED_INFO("down");
        CHECK(ALLineLabel::relative("C:\\Users\\me\\proj", "C:\\Users\\me\\proj\\lib\\util.luau") == std::string("lib/util.luau"));
        UNSCOPED_INFO("up and across");
        CHECK(ALLineLabel::relative("C:\\Users\\me\\proj", "C:\\Users\\me\\shared\\util.luau") == std::string("../shared/util.luau"));
        UNSCOPED_INFO("any case, the file's parts kept as written");
        CHECK(ALLineLabel::relative("c:\\users\\ME\\Proj", "C:\\Users\\me\\proj\\Lib\\Util.luau") ==
                      std::string("Lib/Util.luau"));
        UNSCOPED_INFO("either separator");
        CHECK(ALLineLabel::relative("C:/Users/me/proj/", "C:\\Users\\me\\proj/lib\\util.lsl") == std::string("lib/util.lsl"));
        UNSCOPED_INFO("a long path");
        CHECK(ALLineLabel::relative("C:\\Users\\me\\proj", "\\\\?\\C:\\Users\\me\\proj\\lib\\util.luau") == std::string("lib/util.luau"));
        UNSCOPED_INFO("the NT namespace's");
        CHECK(ALLineLabel::relative("\\??\\C:\\proj", "C:\\proj\\util.luau") == std::string("util.luau"));
        UNSCOPED_INFO("a share");
        CHECK(ALLineLabel::relative("\\\\host\\share\\proj", "\\\\HOST\\Share\\proj\\lib\\util.lsl") == std::string("lib/util.lsl"));
        UNSCOPED_INFO("a share's long path");
        CHECK(ALLineLabel::relative("\\\\?\\UNC\\host\\share\\proj", "//host/share/shared/util.lsl") ==
                      std::string("../shared/util.lsl"));
        UNSCOPED_INFO("another drive");
        REQUIRE(ALLineLabel::relative("C:\\proj", "D:\\proj\\util.luau").empty());
        UNSCOPED_INFO("another share");
        REQUIRE(ALLineLabel::relative("\\\\host\\one\\proj", "\\\\host\\two\\proj\\util.luau").empty());
        UNSCOPED_INFO("a drive and a share");
        REQUIRE(ALLineLabel::relative("C:\\proj", "\\\\host\\share\\proj\\util.luau").empty());
        UNSCOPED_INFO("a share with no share");
        REQUIRE(ALLineLabel::relative("\\\\host", "\\\\host\\share\\util.luau").empty());
        UNSCOPED_INFO("a device");
        REQUIRE(ALLineLabel::relative("\\\\.\\pipe", "\\\\.\\pipe\\util.luau").empty());
        {
            INFO("Windows's and a POSIX root");
            REQUIRE(ALLineLabel::relative("/proj", "C:\\proj\\util.luau").empty());
            REQUIRE(ALLineLabel::relative("C:\\proj", "/proj/util.luau").empty());
        }
        UNSCOPED_INFO("no further up than the root");
        CHECK(ALLineLabel::relative("C:\\proj", "C:\\..\\..\\proj\\util.luau") == "util.luau");
    }

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: quotable makes a label safe for a reader with no escapes", "[allinelabel]")
    {
        CHECK(ALLineLabel::quotable("a\"b\\c\nd\re\tf\x7fg") == std::string("a'b/c_d_e_f_g"));
        UNSCOPED_INFO("the rest as it was");
        CHECK(ALLineLabel::quotable("lib/my util (2).luau \xc3\xa9") == std::string("lib/my util (2).luau \xc3\xa9"));
    }

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: a script on disk labels files from its folder and the world's by name", "[allinelabel]")
    {
        const ALLineLabel label("disk:/home/me/proj/main.luau", { "/home/me/includes", "/home/me/proj/lib", "/home/me/shared" });
        UNSCOPED_INFO("itself, by its own name on disk");
        CHECK(label.of(fileOf("Main script", ""), true) == std::string("main.luau"));
        UNSCOPED_INFO("beside it");
        CHECK(label.of(fileOf("util.luau", "disk:/home/me/proj/util.luau"), false) == std::string("util.luau"));
        UNSCOPED_INFO("under it");
        CHECK(label.of(fileOf("util.luau", "disk:/home/me/proj/lib/util.luau"), false) == std::string("lib/util.luau"));
        UNSCOPED_INFO("an alias's, from its folder all the same");
        CHECK(label.of(fileOf("util.luau", "disk:/home/me/shared/util.luau"), false) ==
                      std::string("../shared/util.luau"));
        UNSCOPED_INFO("an include folder's");
        CHECK(label.of(fileOf("util.lsl", "disk:/home/me/includes/net/util.lsl"), false) ==
                      std::string("../includes/net/util.lsl"));
        UNSCOPED_INFO("one under no blessed folder");
        CHECK(label.of(fileOf("x.luau", "disk:/opt/x.luau"), false) == std::string("../../../opt/x.luau"));
        UNSCOPED_INFO("one from the world, by its name");
        CHECK(label.of(fileOf("Util \"v2\"", "object:5d2c6a5b-8a5e-4d1e-9b8a-3c2a1f0e9d8c:1a2b3c4d-1111-2222-3333-444455556666"), false) ==
                      std::string("Util 'v2'"));
        UNSCOPED_INFO("one named no other way");
        CHECK(label.of(fileOf("lib", "lib"), false) == std::string("lib"));
        UNSCOPED_INFO("a call");
        CHECK(label(fileOf("util.luau", "disk:/home/me/proj/lib/util.luau"), false) == std::string("lib/util.luau"));
        for (const char* path : { "disk:/home/me/proj/lib/util.luau", "disk:/home/me/shared/util.luau", "disk:/opt/x.luau" })
        {
            CAPTURE(path);
            REQUIRE_FALSE(fromRoot(label.of(fileOf("x", path), false)));
        }
    }

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: off disk a file is labelled from the outermost blessed folder holding it", "[allinelabel]")
    {
        const ALLineLabel world("object:5d2c6a5b-8a5e-4d1e-9b8a-3c2a1f0e9d8c:1a2b3c4d-1111-2222-3333-444455556666",
                                { "C:\\Users\\me\\includes\\net", "C:\\Users\\me\\includes", "D:\\lib" });
        UNSCOPED_INFO("itself, by the name it is given");
        CHECK(world.of(fileOf("Door \\ main", ""), true) == std::string("Door / main"));
        UNSCOPED_INFO("the outermost folder's");
        CHECK(world.of(fileOf("util.lsl", "disk:C:\\Users\\me\\includes\\net\\util.lsl"), false) ==
                      std::string("net/util.lsl"));
        UNSCOPED_INFO("one folder's");
        CHECK(world.of(fileOf("util.lsl", "disk:c:\\users\\me\\Includes\\util.lsl"), false) == std::string("util.lsl"));
        UNSCOPED_INFO("another drive's");
        CHECK(world.of(fileOf("util.luau", "disk:D:\\lib\\x\\util.luau"), false) == std::string("x/util.luau"));
        UNSCOPED_INFO("under none, by its name");
        CHECK(world.of(fileOf("util.luau", "disk:E:\\util.luau"), false) == std::string("util.luau"));
        const ALLineLabel nothing("", {});
        UNSCOPED_INFO("no identity at all: itself by its name");
        CHECK(nothing.of(fileOf("main.lsl", ""), true) == std::string("main.lsl"));
        UNSCOPED_INFO("and a file by its name");
        CHECK(nothing.of(fileOf("util.lsl", "disk:/x/util.lsl"), false) == std::string("util.lsl"));

        // On disk, but its include on another drive: from the folder that
        // holds that.
        const ALLineLabel drive("disk:C:\\Users\\me\\proj\\main.lsl", { "D:\\includes" });
        UNSCOPED_INFO("itself");
        CHECK(drive.of(fileOf("main.lsl", ""), true) == std::string("main.lsl"));
        UNSCOPED_INFO("the same drive's from its folder");
        CHECK(drive.of(fileOf("a.lsl", "disk:C:\\Users\\me\\lib\\a.lsl"), false) == std::string("../lib/a.lsl"));
        UNSCOPED_INFO("another drive's from its blessed folder");
        CHECK(drive.of(fileOf("a.lsl", "disk:D:\\includes\\net\\a.lsl"), false) == std::string("net/a.lsl"));
        const ALLineLabel share("disk:\\\\host\\share\\proj\\main.lsl", { "C:\\includes" });
        UNSCOPED_INFO("a script on a share: the share's from its folder");
        CHECK(share.of(fileOf("a.lsl", "disk:\\\\host\\share\\proj\\lib\\a.lsl"), false) ==
                      std::string("lib/a.lsl"));
        UNSCOPED_INFO("and a drive's from its blessed folder");
        CHECK(share.of(fileOf("a.lsl", "disk:C:\\includes\\a.lsl"), false) == std::string("a.lsl"));
    }

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: through a run: two SLua modules of one name read apart by their folders", "[allinelabel]")
    {
        add("lib/util", "util.luau", "disk:/home/me/proj/lib/util.luau", "return { a = 1 }\n");
        add("@shared/util", "util.luau", "disk:/home/me/shared/util.luau", "return { b = 2 }\n");
        ALPreprocessor::Options lua = options(true);
        lua.lineLabel               = ALLineLabel("disk:/home/me/proj/main.luau", { "/home/me/shared" });
        const std::string      script = "local a = require(\"lib/util\")\nlocal b = require(\"@shared/util\")\nprint(a.a, b.b)\n";
        ALPreprocessor::Result r      = ALPreprocessor::run(script, lua);
        UNSCOPED_INFO("no errors: " + r.text);
        CHECK_FALSE(r.hasErrors());
        UNSCOPED_INFO("the one under it: " + r.text);
        REQUIRE(r.text.find("-- @line 1 \"lib/util.luau\"\nreturn { a = 1 }\n") != std::string::npos);
        UNSCOPED_INFO("the alias's, from its folder: " + r.text);
        REQUIRE(r.text.find("-- @line 1 \"../shared/util.luau\"\nreturn { b = 2 }\n") != std::string::npos);
        UNSCOPED_INFO("the script's: " + r.text);
        REQUIRE(r.text.find("-- @line 1 \"main.luau\"\nlocal a = require(1)\n") != std::string::npos);
        REQUIRE(r.map.files().size() == size_t(3));
        {
            INFO("each still called its __SHORTFILE__");
            CHECK(r.map.files()[0].name == "main.luau");
            CHECK(r.map.files()[1].name == "util.luau");
            CHECK(r.map.files()[2].name == "util.luau");
        }

        // Unset, each by its name, as before.
        ALPreprocessor::Options plain = options(true);
        r                             = ALPreprocessor::run(script, plain);
        {
            INFO("unset, by name: " + r.text);
            REQUIRE(r.text.find("-- @line 1 \"util.luau\"\nreturn { a = 1 }\n") != std::string::npos);
            REQUIRE(r.text.find("-- @line 1 \"main.luau\"\n") != std::string::npos);
        }
    }

    TEST_CASE_METHOD(allinelabel_data, "allinelabel: through a run: LSL includes labelled from disk or by name and made quotable", "[allinelabel]")
    {
        add("net/util.lsl", "util.lsl", "disk:C:\\Users\\me\\includes\\net\\util.lsl", "integer x;\n");
        add("World lib", "World \"lib\"", "inventory:1a2b3c4d-1111-2222-3333-444455556666", "integer y;\n");
        ALPreprocessor::Options lsl = options(false);
        lsl.lineLabel               = ALLineLabel("", { "C:\\Users\\me\\includes" });
        const std::string      script = "integer a;\n#include \"net/util.lsl\"\n#include \"World lib\"\ndefault { state_entry() { } }\n";
        ALPreprocessor::Result r      = ALPreprocessor::run(script, lsl);
        UNSCOPED_INFO("no errors: " + r.text);
        CHECK_FALSE(r.hasErrors());
        UNSCOPED_INFO("the disk's, as an #include names it: " + r.text);
        REQUIRE(r.text.find("// @line 1 \"net/util.lsl\"\ninteger x;\n") != std::string::npos);
        UNSCOPED_INFO("the world's, by its name: " + r.text);
        REQUIRE(r.text.find("// @line 1 \"World 'lib'\"\ninteger y;\n") != std::string::npos);
        UNSCOPED_INFO("the script's after: " + r.text);
        REQUIRE(r.text.find("// @line 4 \"main.lsl\"\ndefault") != std::string::npos);

        // A caller's own, quote and all, made quotable.
        lsl.lineLabel = [](const ALSourceMap::File& file, bool self) { return self ? std::string("my \"main\"") : "C:\\" + file.name; };
        r             = ALPreprocessor::run(script, lsl);
        {
            INFO("quotable: " + r.text);
            REQUIRE(r.text.find("// @line 1 \"C:/util.lsl\"\n") != std::string::npos);
            REQUIRE(r.text.find("// @line 4 \"my 'main'\"\n") != std::string::npos);
        }
    }
}
