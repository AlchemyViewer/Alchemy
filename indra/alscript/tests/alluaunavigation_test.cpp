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

#include "../test/lltut.h"

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

namespace tut
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

    typedef test_group<alluaunavigation_data> alluaunavigation_group;
    typedef alluaunavigation_group::object    alluaunavigation_object;
    alluaunavigation_group                    alluaunavigation_instance("alluaunavigation");

    template<> template<>
    void alluaunavigation_object::test<1>()
    {
        set_test_name("a field a module declares is declared there: hover and references say the module's key and its place in the module's lines");
        ensure("definitions loaded: " + error, loaded);
        open("a", true);
        ALScriptReferences refs = service.references(SCRIPT_A, 5, 12); // `twice` in `util.twice(2)`
        ensure("found", refs.found && refs.name == "twice");
        ensure(llformat("a function, not kind %d", (int)refs.kind), refs.kind == ALScriptSymbolKind::Function);
        ensure_equals("declared in the module", refs.definitionFile, std::string(UTIL));
        ensure_equals("at `function M.twice`", where(refs.definition), std::string("3:11"));
        ensure("the module's to rename", refs.hasDefinition && refs.renamable);

        const ALScriptHover hover = service.hover(SCRIPT_A, 5, 12);
        ensure("hover: the module's", hover.found && hover.hasDefinition && hover.definitionFile == UTIL);
        ensure_equals("hover: the same place", llformat("%d:%d", hover.definitionLine, hover.definitionColumn), std::string("3:11"));

        refs = service.references(SCRIPT_A, 6, 12); // `limit`, assigned
        ensure_equals("a field assigned: declared where it is assigned", refs.definitionFile + "@" + where(refs.definition),
                      std::string(UTIL) + "@6:2");
        refs = service.references(SCRIPT_A, 5, 29); // `double`, a module that requires the other
        ensure_equals("each module its own key", refs.definitionFile + "@" + where(refs.definition), std::string(SHAPES) + "@2:11");

        const ALScriptHover local = service.hover(SCRIPT_A, 5, 7); // `util` itself
        ensure("a local the script binds: the script's", local.hasDefinition && local.definitionFile.empty() && local.definitionLine == 1);
        const ALScriptHover say = service.hover("ll.Say(0, \"x\")\n", 0, 4);
        ensure("what the definitions declare: nowhere to go", say.found && !say.hasDefinition);
        service.setDocument("");
    }

    template<> template<>
    void alluaunavigation_object::test<2>()
    {
        set_test_name("a type a module exports is declared there, found through the local the module was required as; its uses in the module too");
        ensure("definitions loaded: " + error, loaded);
        open("a", true);
        const ALScriptReferences refs = service.references(SCRIPT_A, 4, 15); // `Point` in `util.Point`
        ensure("found", refs.found && refs.name == "Point");
        ensure("a type", refs.kind == ALScriptSymbolKind::Type);
        ensure_equals("declared in the module", refs.definitionFile + "@" + where(refs.definition), std::string(UTIL) + "@2:12");
        ensure_equals("each place: the script's, the module's", placesOf(refs), std::string("4:14 ") + UTIL + "@2:12 " + UTIL + "@7:21");
        ensure("renamable", refs.renamable);

        // The local the module was required as is named in a type's prefix
        // too, which a rename of it renames.
        const ALScriptReferences local = service.references(SCRIPT_A, 1, 7);
        ensure_equals("the local, the prefix among its places", placesOf(local), std::string("1:6 4:9 4:22 5:6 6:6"));
        service.setDocument("");
    }

    template<> template<>
    void alluaunavigation_object::test<3>()
    {
        set_test_name("a module's field is found wherever it stands, in the script and every module it requires; a field of the same name on another table is left alone");
        ensure("definitions loaded: " + error, loaded);
        open("a", true);
        ALScriptReferences refs = service.references(SCRIPT_A, 5, 12);
        ensure_equals("the script's, the module's own, and the other module's", placesOf(refs),
                      std::string("5:11 ") + UTIL + "@3:11 " + SHAPES + "@3:16");
        refs = service.references(SCRIPT_A, 6, 12);
        ensure_equals("an assigned one the same", placesOf(refs), std::string("6:11 ") + UTIL + "@6:2 " + SHAPES + "@3:32");

        // The script's own table's field of the same name: the script's alone.
        refs = service.references(SCRIPT_A, 5, 46);
        ensure("found", refs.found && refs.name == "twice" && refs.definitionFile.empty());
        ensure_equals("declared in the script's constructor", where(refs.definition), std::string("3:16"));
        ensure_equals("and used once; none of the module's", placesOf(refs), std::string("3:16 5:45"));

        // Asked at the declaration in the constructor: the same.
        ensure_equals("from the declaration", placesOf(service.references(SCRIPT_A, 3, 17)), std::string("3:16 5:45"));
        service.setDocument("");
    }

    template<> template<>
    void alluaunavigation_object::test<4>()
    {
        set_test_name("two scripts requiring one module: each finds its own places and the module's; and a script read whole, its modules in it, finds them from the module's declaration");
        ensure("definitions loaded: " + error, loaded);
        open("a", true);
        const ALScriptReferences a = service.references(SCRIPT_A, 5, 12);
        open("b", false);
        const ALScriptReferences b = service.references(SCRIPT_B, 1, 12);
        ensure_equals("the other script's: its own and the module's", placesOf(b), std::string("1:11 ") + UTIL + "@3:11");
        ensure_equals("the same declaration", b.definitionFile + "@" + where(b.definition), a.definitionFile + "@" + where(a.definition));

        // What the lookup across an object's scripts asks of each: the
        // script expanded as a save sends it, its modules ahead of it in a
        // table, asked at the module's declaration there.
        std::string bundle = "local __modules = {}\n__modules[\"disk:/lib/util.luau\"] = (function()\n";
        bundle += UTIL_TEXT;
        bundle += "end)()\nlocal util = __modules[\"disk:/lib/util.luau\"]\nprint(util.twice(4))\n";
        service.setDocument("bundle");
        service.setConfig(ALLuauConfig());
        service.setModules({});
        const ALScriptReferences whole = service.references(bundle, 5, 12); // `twice` in `function M.twice`, two lines down
        ensure("found in the bundle", whole.found && whole.name == "twice" && whole.definitionFile.empty());
        ensure_equals("the module's declaration and the script's use", placesOf(whole), std::string("5:11 15:11"));
        service.setDocument("");
    }

    template<> template<>
    void alluaunavigation_object::test<5>()
    {
        set_test_name("a hover on a type's name says the type as its declaration reads, and where it was declared: a module's through its local, the script's own, a generic one, a built-in one");
        ensure("definitions loaded: " + error, loaded);
        open("a", true);
        ALScriptHover hover = service.hover(SCRIPT_A, 4, 15); // `Point` in `util.Point`
        ensure("found", hover.found);
        ensure("the module's, by its name and fields: " + hover.label,
               hover.label.rfind("type util.Point = ", 0) == 0 && hover.label.find("x: number") != std::string::npos);
        ensure_equals("declared in the module", hover.definitionFile + "@" + llformat("%d:%d", hover.definitionLine, hover.definitionColumn),
                      std::string(UTIL) + "@2:12");

        const std::string own = "--!strict\n"                                   // 0
                                "type Pair<T> = { first: T, second: T }\n"      // 1
                                "local p: Pair<number> = { first = 1, second = 2 }\n" // 2
                                "local n: number = p.first\n";                  // 3
        open("own", false);
        hover = service.hover(own, 2, 10); // `Pair`
        ensure("the script's own, generic: " + hover.label, hover.found && hover.label.rfind("type Pair<T> = ", 0) == 0 &&
                                                             hover.label.find("first: T") != std::string::npos);
        ensure("declared in the script", hover.hasDefinition && hover.definitionFile.empty() && hover.definitionLine == 1);
        hover = service.hover(own, 3, 10); // `number`
        ensure_equals("a built-in one, only itself", hover.label, std::string("type number"));
        hover = service.hover(own, 3, 6); // `n`, the local, as ever
        ensure("a local still a local: " + hover.label, hover.label.rfind("local n", 0) == 0);
        service.setDocument("");
    }
}
