/**
 * @file alluaunavigation_test.cpp
 * @brief Where a name in an SLua script is declared and used, across the modules it requires.
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

#include "../luau/alluauservice.h"
#include "../luau/alluauconfig.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

namespace
{
    const char* const UTIL = "disk:/lib/util.luau";
    const char* const SHAPES = "disk:/lib/shapes.luau";

    // A module with a function field, a field assigned, a type exported and
    // used: the table it returns made at its second line.
    const char* const UTIL_TEXT =
        "--!strict\n"                                  // 0
        "local M = {}\n"                               // 1
        "export type Point = { x: number, y: number }\n" // 2
        "function M.twice(n: number): number\n"        // 3
        "    return n * 2\n"                           // 4
        "end\n"                                        // 5
        "M.limit = 10\n"                               // 6
        "function M.origin(): Point\n"                 // 7
        "    return { x = 0, y = 0 }\n"                // 8
        "end\n"                                        // 9
        "return M\n";                                  // 10

    // A module that requires the first and uses two of its fields.
    const char* const SHAPES_TEXT =
        "local util = require(\"util\")\n"                // 0
        "local S = {}\n"                                  // 1
        "function S.double(n: number): number\n"          // 2
        "    return util.twice(n) + util.limit\n"         // 3
        "end\n"                                           // 4
        "return S\n";                                     // 5

    // A script requiring both, with a table of its own that has a field
    // of the same name as one of the module's.
    const char* const SCRIPT_A =
        "--!strict\n"                                                         // 0
        "local util = require(\"util\")\n"                                    // 1
        "local shapes = require(\"shapes\")\n"                                // 2
        "local other = { twice = 1 }\n"                                       // 3
        "local p: util.Point = util.origin()\n"                               // 4
        "print(util.twice(2), shapes.double(3), other.twice, p.x)\n"          // 5
        "print(util.limit)\n";                                                // 6

    // Another script requiring the first module alone, not strict: under
    // the old solver its questions read autocomplete's module.
    const char* const SCRIPT_B =
        "local util = require(\"util\")\n"   // 0
        "print(util.twice(4))\n";            // 1

    std::string where(const ALScriptSpan& span) { return std::to_string(span.line) + ":" + std::to_string(span.column); }

    std::string placesOf(const ALScriptReferences& refs)
    {
        std::string out;
        for (const ALScriptSpan& span : refs.references)
        {
            out += (out.empty() ? "" : " ") + where(span);
        }
        for (const ALScriptReferences::Elsewhere& place : refs.elsewhere)
        {
            out += (out.empty() ? "" : " ") + place.file + "@" + where(place.span);
        }
        return out;
    }
}

namespace
{
    struct alluaunavigation_data
    {
        ALLuauService service;
        std::string   error;
        bool          loaded = false;
        // Luau's new type solver, where the run asks for it: CTest runs
        // these twice, the second time with AL_TEST_LUAU_SOLVER=new.
        const bool    newSolver = getenv("AL_TEST_LUAU_SOLVER") && std::string(getenv("AL_TEST_LUAU_SOLVER")) == "new";

        alluaunavigation_data()
        {
            llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            service.setNewSolver(newSolver, error);
            loaded = service.loadDefinitions(text.str(), error);
        }

        // A script of its own, asked about next, requiring the two modules
        // -- the second requiring the first -- or the first alone.
        void open(const std::string& id, bool both)
        {
            service.setDocument(id);
            service.setConfig(ALLuauConfig());
            ALLuauService::Modules modules;
            modules.modules.push_back({ UTIL, UTIL_TEXT });
            modules.reaches.push_back({ "", "util", UTIL });
            if (both)
            {
                modules.modules.push_back({ SHAPES, SHAPES_TEXT });
                modules.reaches.push_back({ "", "shapes", SHAPES });
                modules.reaches.push_back({ SHAPES, "util", UTIL });
            }
            service.setModules(modules);
        }
    };

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: hover and references place a module's field in the module's own lines", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        open("a", true);
        ALScriptReferences refs = service.references(SCRIPT_A, 5, 12); // `twice` in `util.twice(2)`
        REQUIRE(refs.found);
        CHECK(refs.name == "twice");
        CHECK(refs.kind == ALScriptSymbolKind::Function);
        UNSCOPED_INFO("declared in the module");
        CHECK(refs.definitionFile == std::string(UTIL));
        UNSCOPED_INFO("at `function M.twice`");
        CHECK(where(refs.definition) == std::string("3:11"));
        {
            INFO("the module's to rename");
            CHECK(refs.hasDefinition);
            CHECK(refs.renamable);
        }

        const ALScriptHover hover = service.hover(SCRIPT_A, 5, 12);
        {
            INFO("hover: the module's");
            REQUIRE(hover.found);
            CHECK(hover.hasDefinition);
            CHECK(hover.definitionFile == UTIL);
        }
        UNSCOPED_INFO("hover: the same place");
        CHECK(llformat("%d:%d", hover.definitionLine, hover.definitionColumn) == std::string("3:11"));

        refs = service.references(SCRIPT_A, 6, 12); // `limit`, assigned
        UNSCOPED_INFO("a field assigned: declared where it is assigned");
        CHECK(refs.definitionFile + "@" + where(refs.definition) == std::string(UTIL) + "@6:2");
        refs = service.references(SCRIPT_A, 5, 29); // `double`, a module that requires the other
        UNSCOPED_INFO("each module its own key");
        CHECK(refs.definitionFile + "@" + where(refs.definition) == std::string(SHAPES) + "@2:11");

        const ALScriptHover local = service.hover(SCRIPT_A, 5, 7); // `util` itself
        {
            INFO("a local the script binds: the script's");
            CHECK(local.hasDefinition);
            REQUIRE(local.definitionFile.empty());
            CHECK(local.definitionLine == 1);
        }
        const ALScriptHover say = service.hover("ll.Say(0, \"x\")\n", 0, 4);
        {
            INFO("what the definitions declare: nowhere to go");
            REQUIRE(say.found);
            CHECK_FALSE(say.hasDefinition);
        }
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: a module's exported type is found through its local and declared there", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        open("a", true);
        const ALScriptReferences refs = service.references(SCRIPT_A, 4, 15); // `Point` in `util.Point`
        REQUIRE(refs.found);
        CHECK(refs.name == "Point");
        CHECK(refs.kind == ALScriptSymbolKind::Type);
        UNSCOPED_INFO("declared in the module");
        CHECK(refs.definitionFile + "@" + where(refs.definition) == std::string(UTIL) + "@2:12");
        UNSCOPED_INFO("each place: the script's, the module's");
        CHECK(placesOf(refs) == std::string("4:14 ") + UTIL + "@2:12 " + UTIL + "@7:21");
        CHECK(refs.renamable);

        // The local the module was required as is named in a type's prefix
        // too, which a rename of it renames.
        const ALScriptReferences local = service.references(SCRIPT_A, 1, 7);
        UNSCOPED_INFO("the local, the prefix among its places");
        CHECK(placesOf(local) == std::string("1:6 4:9 4:22 5:6 6:6"));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: a module's field is found everywhere it stands but not another table's", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        open("a", true);
        ALScriptReferences refs = service.references(SCRIPT_A, 5, 12);
        UNSCOPED_INFO("the script's, the module's own, and the other module's");
        CHECK(placesOf(refs) == std::string("5:11 ") + UTIL + "@3:11 " + SHAPES + "@3:16");
        refs = service.references(SCRIPT_A, 6, 12);
        UNSCOPED_INFO("an assigned one the same");
        CHECK(placesOf(refs) == std::string("6:11 ") + UTIL + "@6:2 " + SHAPES + "@3:32");

        // The script's own table's field of the same name: the script's alone.
        refs = service.references(SCRIPT_A, 5, 46);
        REQUIRE(refs.found);
        CHECK(refs.name == "twice");
        REQUIRE(refs.definitionFile.empty());
        UNSCOPED_INFO("declared in the script's constructor");
        CHECK(where(refs.definition) == std::string("3:16"));
        UNSCOPED_INFO("and used once; none of the module's");
        CHECK(placesOf(refs) == std::string("3:16 5:45"));

        // Asked at the declaration in the constructor: the same.
        UNSCOPED_INFO("from the declaration");
        CHECK(placesOf(service.references(SCRIPT_A, 3, 17)) == std::string("3:16 5:45"));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: scripts sharing a module find their places and the module's from either end", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        open("a", true);
        const ALScriptReferences a = service.references(SCRIPT_A, 5, 12);
        open("b", false);
        const ALScriptReferences b = service.references(SCRIPT_B, 1, 12);
        UNSCOPED_INFO("the other script's: its own and the module's");
        CHECK(placesOf(b) == std::string("1:11 ") + UTIL + "@3:11");
        UNSCOPED_INFO("the same declaration");
        CHECK(b.definitionFile + "@" + where(b.definition) == a.definitionFile + "@" + where(a.definition));

        // What the lookup across scripts asks of each: the script read
        // apart with its modules, as the checker reads it, asked at the
        // module's declaration in the module's own lines.
        open("lookup", false);
        const ALScriptReferences from_module = service.references(SCRIPT_B, 3, 12, UTIL); // `twice` in `function M.twice`
        {
            INFO("found from the module");
            REQUIRE(from_module.found);
            CHECK(from_module.name == "twice");
            CHECK(from_module.definitionFile == UTIL);
        }
        UNSCOPED_INFO("the script's use, through the require, and the module's declaration");
        CHECK(placesOf(from_module) == std::string("1:11 ") + UTIL + "@3:11");
        // A module's local is its own alone.
        const ALScriptReferences local = service.references(SCRIPT_B, 1, 6, UTIL); // `M` in `local M = {}`
        REQUIRE(local.found);
        CHECK(local.name == "M");
        UNSCOPED_INFO("the module's places alone");
        CHECK(placesOf(local) == std::string(UTIL) + "@1:6 " + UTIL + "@3:9 " + UTIL + "@6:0 " + UTIL + "@7:9 " + UTIL + "@10:7");
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: hover on a type's name says it as declared and where it was declared", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        open("a", true);
        ALScriptHover hover = service.hover(SCRIPT_A, 4, 15); // `Point` in `util.Point`
        REQUIRE(hover.found);
        {
            INFO("the module's, by its name and fields: " + hover.label);
            CHECK(hover.label.rfind("type util.Point = ", 0) == 0);
            REQUIRE(hover.label.find("x: number") != std::string::npos);
        }
        UNSCOPED_INFO("declared in the module");
        CHECK(hover.definitionFile + "@" + llformat("%d:%d", hover.definitionLine, hover.definitionColumn) == std::string(UTIL) + "@2:12");

        const std::string own = "--!strict\n"                                   // 0
                                "type Pair<T> = { first: T, second: T }\n"      // 1
                                "local p: Pair<number> = { first = 1, second = 2 }\n" // 2
                                "local n: number = p.first\n";                  // 3
        open("own", false);
        hover = service.hover(own, 2, 10); // `Pair`
        {
            INFO("the script's own, generic: " + hover.label);
            REQUIRE(hover.found);
            CHECK(hover.label.rfind("type Pair<T> = ", 0) == 0);
            REQUIRE(hover.label.find("first: T") != std::string::npos);
        }
        {
            INFO("declared in the script");
            CHECK(hover.hasDefinition);
            REQUIRE(hover.definitionFile.empty());
            CHECK(hover.definitionLine == 1);
        }
        hover = service.hover(own, 3, 10); // `number`
        UNSCOPED_INFO("a built-in one, only itself");
        CHECK(hover.label == std::string("type number"));
        hover = service.hover(own, 3, 6); // `n`, the local, as ever
        UNSCOPED_INFO("a local still a local: " + hover.label);
        CHECK(hover.label.rfind("local n", 0) == 0);
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: a field is found by a string key in brackets too with its quotes left out", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string text = "--!strict\n"                                  // 0
                                 "local t = { size = 1, [\"name\"] = \"a\" }\n" // 1
                                 "t[\"size\"] = t.size + 1\n"                   // 2
                                 "print(t.name, t[\"name\"], t['size'])\n";     // 3
        open("keys", false);
        ALScriptReferences refs = service.references(text, 2, 4); // `size` in `t["size"]`
        REQUIRE(refs.found);
        CHECK(refs.name == "size");
        UNSCOPED_INFO("each place: the record's key, the strings' insides, the name");
        CHECK(placesOf(refs) == std::string("1:12 2:3 2:14 3:28"));
        UNSCOPED_INFO("declared by the record");
        CHECK(where(refs.definition) == std::string("1:12"));
        CHECK(refs.hasDefinition);
        CHECK(refs.renamable);

        refs = service.references(text, 3, 9); // `name` in `t.name`
        UNSCOPED_INFO("a key in brackets declares it");
        CHECK(placesOf(refs) == std::string("1:24 3:8 3:17"));
        UNSCOPED_INFO("there, inside its quotes");
        CHECK(where(refs.definition) == std::string("1:24"));
        CHECK(refs.renamable);
        UNSCOPED_INFO("asked at that key: the same");
        CHECK(placesOf(service.references(text, 1, 25)) == std::string("1:24 3:8 3:17"));
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: a method is its class's through __index; a function __index stops a rename", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string account = "--!strict\n"                                    // 0
                                    "local Account = {}\n"                           // 1
                                    "Account.__index = Account\n"                    // 2
                                    "function Account.new()\n"                       // 3
                                    "    return setmetatable({}, Account)\n"         // 4
                                    "end\n"                                          // 5
                                    "function Account:deposit(n: number): number\n"  // 6
                                    "    return n\n"                                 // 7
                                    "end\n"                                          // 8
                                    "local a = Account.new()\n"                      // 9
                                    "print(a:deposit(5))\n";                         // 10
        open("account", false);
        ALScriptReferences refs = service.references(account, 6, 18); // `deposit` in `function Account:deposit`
        REQUIRE(refs.found);
        CHECK(refs.name == "deposit");
        UNSCOPED_INFO("the declaration and the object's call");
        CHECK(placesOf(refs) == std::string("6:17 10:8"));
        CHECK(refs.hasDefinition);
        CHECK(refs.renamable);
        refs = service.references(account, 10, 9); // `deposit` in `a:deposit(5)`
        UNSCOPED_INFO("from the call: the same");
        CHECK(placesOf(refs) == std::string("6:17 10:8"));
        UNSCOPED_INFO("declared in the class");
        CHECK(where(refs.definition) == std::string("6:17"));

        const std::string proxy = "--!strict\n"                                                             // 0
                                  "local Base = {}\n"                                                       // 1
                                  "function Base.greet(): string\n"                                         // 2
                                  "    return \"hi\"\n"                                                     // 3
                                  "end\n"                                                                   // 4
                                  "local o = setmetatable({}, { __index = function(_, k) return nil end })\n" // 5
                                  "print(Base.greet(), o.greet)\n";                                         // 6
        open("proxy", false);
        refs = service.references(proxy, 2, 15); // `greet` in `function Base.greet`
        REQUIRE(refs.found);
        CHECK(refs.hasDefinition);
        UNSCOPED_INFO("an object's field of the name, its __index a function: not to rename");
        CHECK_FALSE(refs.renamable);
        service.setDocument("");
    }

    TEST_CASE_METHOD(alluaunavigation_data, "alluaunavigation: a forward-declared local function is defined where its function is", "[alluaunavigation]")
    {
        UNSCOPED_INFO("definitions loaded: " + error);
        CHECK(loaded);
        const std::string text = "local count, f, g\n"                  // 0
                                 "local function h() return f() end\n" // 1
                                 "function f() return 1 end\n"         // 2
                                 "g = function() return 2 end\n"       // 3
                                 "count = 0\n"                         // 4
                                 "print(h(), g(), count)\n";           // 5
        open("forward", false);
        ALScriptReferences refs = service.references(text, 1, 26); // `f` in `return f()`
        REQUIRE(refs.found);
        CHECK(refs.name == "f");
        UNSCOPED_INFO("Tapple's: at `function f`, not `local f`");
        CHECK(where(refs.definition) == std::string("2:9"));
        CHECK(refs.hasDefinition);
        CHECK(refs.kind == ALScriptSymbolKind::Function);
        UNSCOPED_INFO("every place, the declaration among them");
        CHECK(placesOf(refs) == std::string("0:13 1:26 2:9"));

        refs = service.references(text, 5, 11); // `g` in `g()`
        UNSCOPED_INFO("given a function by an assignment: there");
        CHECK(where(refs.definition) == std::string("3:0"));
        CHECK(refs.kind == ALScriptSymbolKind::Function);

        refs = service.references(text, 5, 16); // `count`
        UNSCOPED_INFO("given a number: where it is declared");
        CHECK(where(refs.definition) == std::string("0:6"));
        CHECK(refs.kind == ALScriptSymbolKind::Variable);
        service.setDocument("");
    }
}
