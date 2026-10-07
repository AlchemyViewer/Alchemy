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

#include "../test/lltut.h"

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

namespace tut
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

    typedef test_group<allinelabel_data> allinelabel_group;
    typedef allinelabel_group::object    allinelabel_object;
    allinelabel_group                    allinelabel_instance("allinelabel");

    template<> template<>
    void allinelabel_object::test<1>()
    {
        set_test_name("a path from a folder on a POSIX root: down with `/`, up with `..`, none to the folder itself or above it, and case tells parts apart");
        ensure_equals("beside", ALLineLabel::relative("/home/me/proj", "/home/me/proj/util.luau"), std::string("util.luau"));
        ensure_equals("down", ALLineLabel::relative("/home/me/proj", "/home/me/proj/lib/util.luau"), std::string("lib/util.luau"));
        ensure_equals("up and across", ALLineLabel::relative("/home/me/proj", "/home/me/shared/util.luau"), std::string("../shared/util.luau"));
        ensure_equals("up to the root", ALLineLabel::relative("/home/me/proj", "/opt/lib/util.lsl"), std::string("../../../opt/lib/util.lsl"));
        ensure_equals("a trailing slash and `.` passed over, `..` taken back", ALLineLabel::relative("/home/me/proj/./", "/home/me/x/../proj/lib//util.luau"),
                      std::string("lib/util.luau"));
        ensure_equals("a name it only begins with is no folder of it", ALLineLabel::relative("/home/me/proj", "/home/me/project/util.luau"),
                      std::string("../project/util.luau"));
        ensure_equals("another case another folder", ALLineLabel::relative("/home/me/proj", "/home/me/Proj/util.luau"), std::string("../Proj/util.luau"));
        ensure_equals("a backslash no separator", ALLineLabel::relative("/home/me", "/home/me/a\\b.lsl"), std::string("a\\b.lsl"));
        ensure("the folder itself", ALLineLabel::relative("/home/me/proj", "/home/me/proj/").empty());
        ensure("a folder it is in", ALLineLabel::relative("/home/me/proj", "/home/me").empty());
        ensure("not from a root", ALLineLabel::relative("proj", "proj/util.luau").empty() && ALLineLabel::relative("/home/me", "util.luau").empty());
        ensure("nothing", ALLineLabel::relative("", "").empty());
    }

    template<> template<>
    void allinelabel_object::test<2>()
    {
        set_test_name("a path from a folder as Windows writes it: either separator, any case, the long forms read as the paths they stand for; none to another drive or share");
        ensure_equals("down", ALLineLabel::relative("C:\\Users\\me\\proj", "C:\\Users\\me\\proj\\lib\\util.luau"), std::string("lib/util.luau"));
        ensure_equals("up and across", ALLineLabel::relative("C:\\Users\\me\\proj", "C:\\Users\\me\\shared\\util.luau"), std::string("../shared/util.luau"));
        ensure_equals("any case, the file's parts kept as written", ALLineLabel::relative("c:\\users\\ME\\Proj", "C:\\Users\\me\\proj\\Lib\\Util.luau"),
                      std::string("Lib/Util.luau"));
        ensure_equals("either separator", ALLineLabel::relative("C:/Users/me/proj/", "C:\\Users\\me\\proj/lib\\util.lsl"), std::string("lib/util.lsl"));
        ensure_equals("a long path", ALLineLabel::relative("C:\\Users\\me\\proj", "\\\\?\\C:\\Users\\me\\proj\\lib\\util.luau"), std::string("lib/util.luau"));
        ensure_equals("the NT namespace's", ALLineLabel::relative("\\??\\C:\\proj", "C:\\proj\\util.luau"), std::string("util.luau"));
        ensure_equals("a share", ALLineLabel::relative("\\\\host\\share\\proj", "\\\\HOST\\Share\\proj\\lib\\util.lsl"), std::string("lib/util.lsl"));
        ensure_equals("a share's long path", ALLineLabel::relative("\\\\?\\UNC\\host\\share\\proj", "//host/share/shared/util.lsl"),
                      std::string("../shared/util.lsl"));
        ensure("another drive", ALLineLabel::relative("C:\\proj", "D:\\proj\\util.luau").empty());
        ensure("another share", ALLineLabel::relative("\\\\host\\one\\proj", "\\\\host\\two\\proj\\util.luau").empty());
        ensure("a drive and a share", ALLineLabel::relative("C:\\proj", "\\\\host\\share\\proj\\util.luau").empty());
        ensure("a share with no share", ALLineLabel::relative("\\\\host", "\\\\host\\share\\util.luau").empty());
        ensure("a device", ALLineLabel::relative("\\\\.\\pipe", "\\\\.\\pipe\\util.luau").empty());
        ensure("Windows's and a POSIX root", ALLineLabel::relative("/proj", "C:\\proj\\util.luau").empty() &&
                                                 ALLineLabel::relative("C:\\proj", "/proj/util.luau").empty());
        ensure("no further up than the root", ALLineLabel::relative("C:\\proj", "C:\\..\\..\\proj\\util.luau") == "util.luau");
    }

    template<> template<>
    void allinelabel_object::test<3>()
    {
        set_test_name("a label quotable: a quote, a backslash and a line break made what a reader with no escapes reads whole");
        ensure_equals("each", ALLineLabel::quotable("a\"b\\c\nd\re\tf\x7fg"), std::string("a'b/c_d_e_f_g"));
        ensure_equals("the rest as it was", ALLineLabel::quotable("lib/my util (2).luau \xc3\xa9"), std::string("lib/my util (2).luau \xc3\xa9"));
    }

    template<> template<>
    void allinelabel_object::test<4>()
    {
        set_test_name("a script on disk: itself by its base name, every file on disk from its folder however reached, the world's by name, nothing from a root");
        const ALLineLabel label("disk:/home/me/proj/main.luau", { "/home/me/includes", "/home/me/proj/lib", "/home/me/shared" });
        ensure_equals("itself, by its own name on disk", label.of(fileOf("Main script", ""), true), std::string("main.luau"));
        ensure_equals("beside it", label.of(fileOf("util.luau", "disk:/home/me/proj/util.luau"), false), std::string("util.luau"));
        ensure_equals("under it", label.of(fileOf("util.luau", "disk:/home/me/proj/lib/util.luau"), false), std::string("lib/util.luau"));
        ensure_equals("an alias's, from its folder all the same", label.of(fileOf("util.luau", "disk:/home/me/shared/util.luau"), false),
                      std::string("../shared/util.luau"));
        ensure_equals("an include folder's", label.of(fileOf("util.lsl", "disk:/home/me/includes/net/util.lsl"), false),
                      std::string("../includes/net/util.lsl"));
        ensure_equals("one under no blessed folder", label.of(fileOf("x.luau", "disk:/opt/x.luau"), false), std::string("../../../opt/x.luau"));
        ensure_equals("one from the world, by its name", label.of(fileOf("Util \"v2\"", "object:5d2c6a5b-8a5e-4d1e-9b8a-3c2a1f0e9d8c:1a2b3c4d-1111-2222-3333-444455556666"), false),
                      std::string("Util 'v2'"));
        ensure_equals("one named no other way", label.of(fileOf("lib", "lib"), false), std::string("lib"));
        ensure_equals("a call", label(fileOf("util.luau", "disk:/home/me/proj/lib/util.luau"), false), std::string("lib/util.luau"));
        for (const std::string& path : { "disk:/home/me/proj/lib/util.luau", "disk:/home/me/shared/util.luau", "disk:/opt/x.luau" })
        {
            ensure("never from a root: " + path, !fromRoot(label.of(fileOf("x", path), false)));
        }
    }

    template<> template<>
    void allinelabel_object::test<5>()
    {
        set_test_name("a script not on disk, or a file on another drive: from the outermost blessed folder that holds it, as an #include names it; under none, by its name");
        const ALLineLabel world("object:5d2c6a5b-8a5e-4d1e-9b8a-3c2a1f0e9d8c:1a2b3c4d-1111-2222-3333-444455556666",
                                { "C:\\Users\\me\\includes\\net", "C:\\Users\\me\\includes", "D:\\lib" });
        ensure_equals("itself, by the name it is given", world.of(fileOf("Door \\ main", ""), true), std::string("Door / main"));
        ensure_equals("the outermost folder's", world.of(fileOf("util.lsl", "disk:C:\\Users\\me\\includes\\net\\util.lsl"), false),
                      std::string("net/util.lsl"));
        ensure_equals("one folder's", world.of(fileOf("util.lsl", "disk:c:\\users\\me\\Includes\\util.lsl"), false), std::string("util.lsl"));
        ensure_equals("another drive's", world.of(fileOf("util.luau", "disk:D:\\lib\\x\\util.luau"), false), std::string("x/util.luau"));
        ensure_equals("under none, by its name", world.of(fileOf("util.luau", "disk:E:\\util.luau"), false), std::string("util.luau"));
        const ALLineLabel nothing("", {});
        ensure_equals("no identity at all: itself by its name", nothing.of(fileOf("main.lsl", ""), true), std::string("main.lsl"));
        ensure_equals("and a file by its name", nothing.of(fileOf("util.lsl", "disk:/x/util.lsl"), false), std::string("util.lsl"));

        // On disk, but its include on another drive: from the folder that
        // holds that.
        const ALLineLabel drive("disk:C:\\Users\\me\\proj\\main.lsl", { "D:\\includes" });
        ensure_equals("itself", drive.of(fileOf("main.lsl", ""), true), std::string("main.lsl"));
        ensure_equals("the same drive's from its folder", drive.of(fileOf("a.lsl", "disk:C:\\Users\\me\\lib\\a.lsl"), false), std::string("../lib/a.lsl"));
        ensure_equals("another drive's from its blessed folder", drive.of(fileOf("a.lsl", "disk:D:\\includes\\net\\a.lsl"), false), std::string("net/a.lsl"));
        const ALLineLabel share("disk:\\\\host\\share\\proj\\main.lsl", { "C:\\includes" });
        ensure_equals("a script on a share: the share's from its folder", share.of(fileOf("a.lsl", "disk:\\\\host\\share\\proj\\lib\\a.lsl"), false),
                      std::string("lib/a.lsl"));
        ensure_equals("and a drive's from its blessed folder", share.of(fileOf("a.lsl", "disk:C:\\includes\\a.lsl"), false), std::string("a.lsl"));
    }

    template<> template<>
    void allinelabel_object::test<6>()
    {
        set_test_name("through a run: two SLua modules of one name in different folders read apart, the script by its name, and what the map calls each is as before");
        add("lib/util", "util.luau", "disk:/home/me/proj/lib/util.luau", "return { a = 1 }\n");
        add("@shared/util", "util.luau", "disk:/home/me/shared/util.luau", "return { b = 2 }\n");
        ALPreprocessor::Options lua = options(true);
        lua.lineLabel               = ALLineLabel("disk:/home/me/proj/main.luau", { "/home/me/shared" });
        const std::string      script = "local a = require(\"lib/util\")\nlocal b = require(\"@shared/util\")\nprint(a.a, b.b)\n";
        ALPreprocessor::Result r      = ALPreprocessor::run(script, lua);
        ensure("no errors: " + r.text, !r.hasErrors());
        ensure("the one under it: " + r.text, r.text.find("-- @line 1 \"lib/util.luau\"\nreturn { a = 1 }\n") != std::string::npos);
        ensure("the alias's, from its folder: " + r.text, r.text.find("-- @line 1 \"../shared/util.luau\"\nreturn { b = 2 }\n") != std::string::npos);
        ensure("the script's: " + r.text, r.text.find("-- @line 1 \"main.luau\"\nlocal a = require(1)\n") != std::string::npos);
        ensure_equals("three files", r.map.files().size(), size_t(3));
        ensure("each still called its __SHORTFILE__", r.map.files()[0].name == "main.luau" && r.map.files()[1].name == "util.luau" &&
                                                         r.map.files()[2].name == "util.luau");

        // Unset, each by its name, as before.
        ALPreprocessor::Options plain = options(true);
        r                             = ALPreprocessor::run(script, plain);
        ensure("unset, by name: " + r.text, r.text.find("-- @line 1 \"util.luau\"\nreturn { a = 1 }\n") != std::string::npos &&
                                               r.text.find("-- @line 1 \"main.luau\"\n") != std::string::npos);
    }

    template<> template<>
    void allinelabel_object::test<7>()
    {
        set_test_name("through a run: an LSL include from the world keeps its name, one on disk is from the script's folder, and whatever a label holds is made quotable");
        add("net/util.lsl", "util.lsl", "disk:C:\\Users\\me\\includes\\net\\util.lsl", "integer x;\n");
        add("World lib", "World \"lib\"", "inventory:1a2b3c4d-1111-2222-3333-444455556666", "integer y;\n");
        ALPreprocessor::Options lsl = options(false);
        lsl.lineLabel               = ALLineLabel("", { "C:\\Users\\me\\includes" });
        const std::string      script = "integer a;\n#include \"net/util.lsl\"\n#include \"World lib\"\ndefault { state_entry() { } }\n";
        ALPreprocessor::Result r      = ALPreprocessor::run(script, lsl);
        ensure("no errors: " + r.text, !r.hasErrors());
        ensure("the disk's, as an #include names it: " + r.text, r.text.find("// @line 1 \"net/util.lsl\"\ninteger x;\n") != std::string::npos);
        ensure("the world's, by its name: " + r.text, r.text.find("// @line 1 \"World 'lib'\"\ninteger y;\n") != std::string::npos);
        ensure("the script's after: " + r.text, r.text.find("// @line 4 \"main.lsl\"\ndefault") != std::string::npos);

        // A caller's own, quote and all, made quotable.
        lsl.lineLabel = [](const ALSourceMap::File& file, bool self) { return self ? std::string("my \"main\"") : "C:\\" + file.name; };
        r             = ALPreprocessor::run(script, lsl);
        ensure("quotable: " + r.text, r.text.find("// @line 1 \"C:/util.lsl\"\n") != std::string::npos &&
                                         r.text.find("// @line 4 \"my 'main'\"\n") != std::string::npos);
    }
}
