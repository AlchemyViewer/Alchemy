/**
 * @file alluauconfig_test.cpp
 * @brief A .luaurc read for its aliases and its mode.
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

#include "../luau/alluauconfig.h"
#include "../lint/alscriptlintpass.h"

#include "../test/lltut.h"

namespace tut
{
    struct alluauconfig_data
    {
    };

    typedef test_group<alluauconfig_data> alluauconfig_group;
    typedef alluauconfig_group::object    alluauconfig_object;
    alluauconfig_group                    alluauconfig_instance("alluauconfig");

    template<> template<>
    void alluauconfig_object::test<1>()
    {
        set_test_name("aliases come out by lower-case name with their paths, and the mode where it is said");
        ALLuauConfig config;
        std::string  error;
        ensure("parses: " + error, ALLuauConfig::parse("{\n  \"languageMode\": \"strict\",\n  \"aliases\": { \"Lib\": \"./lib\", \"shared\": \"../shared\", \"abs\": \"/opt/lua\" }\n}\n", config, error));
        ensure_equals("three aliases", config.aliases.size(), size_t(3));
        ensure_equals("by lower-case name", config.aliases["lib"], std::string("./lib"));
        ensure_equals("a path up", config.aliases["shared"], std::string("../shared"));
        ensure_equals("an absolute one", config.aliases["abs"], std::string("/opt/lua"));
        ensure_equals("the mode", config.mode, std::string("strict"));
        ensure("no mode where none is said", ALLuauConfig::parse("{ \"aliases\": {} }", config, error) && config.mode.empty());
        ensure("not a configuration", !ALLuauConfig::parse("{ \"aliases\": 3 }", config, error) && !error.empty());
        ensure("nor an alias Luau refuses", !ALLuauConfig::parse("{ \"aliases\": { \"bad name\": \"x\" } }", config, error));
    }

    template<> template<>
    void alluauconfig_object::test<2>()
    {
        set_test_name("a require name's alias is what follows the @ up to the slash, and absolute paths are known on both platforms");
        std::string alias, rest;
        ensure("found", ALLuauConfig::aliasOf("@Lib/util/strings", alias, rest));
        ensure_equals("lower case", alias, std::string("lib"));
        ensure_equals("the rest", rest, std::string("util/strings"));
        ensure("the alias alone", ALLuauConfig::aliasOf("@lib", alias, rest) && alias == "lib" && rest.empty());
        ensure("not without the @", !ALLuauConfig::aliasOf("lib/util", alias, rest));
        ensure("not an empty one", !ALLuauConfig::aliasOf("@/util", alias, rest));
        ensure("absolute on posix", ALLuauConfig::absolute("/opt/lua"));
        ensure("absolute on windows", ALLuauConfig::absolute("C:\\lua") && ALLuauConfig::absolute("c:/lua"));
        ensure("relative", !ALLuauConfig::absolute("./lib") && !ALLuauConfig::absolute("lib"));
    }
    template<> template<>
    void alluauconfig_object::test<3>()
    {
        set_test_name("the lints and the globals come out as Luau reads them, the lints as its defaults where the file says nothing");
        ALLuauConfig config;
        std::string  error;
        const ALLuauConfig defaults;
        ensure("parses: " + error, ALLuauConfig::parse("{\n  \"lint\": { \"LocalUnused\": false, \"LocalShadow\": true },\n  \"globals\": [\"Alchemy\", \"grid\"]\n}\n", config, error));
        ensure_equals("two globals", config.globals.size(), size_t(2));
        ensure_equals("in order", config.globals[0], std::string("Alchemy"));
        // LocalUnused is code 7 and LocalShadow code 4 in Luau's numbering;
        // the second is off by default.
        ensure("LocalUnused off", (config.lints & (1ull << 7)) == 0);
        ensure("LocalShadow on", (config.lints & (1ull << 4)) != 0);
        ensure("the rest as they were", (config.lints & ~((1ull << 7) | (1ull << 4))) == (defaults.lints & ~((1ull << 7) | (1ull << 4))));
        ensure("none fatal, not every lint an error", config.fatalLints == 0 && !config.lintErrors);
        ensure("said nothing: the defaults", ALLuauConfig::parse("{}", config, error) && config.lints == defaults.lints && config.fatalLints == 0 && config.globals.empty());
        ensure("every lint an error where asked", ALLuauConfig::parse("{ \"lintErrors\": true }", config, error) && config.lintErrors);
    }

    template<> template<>
    void alluauconfig_object::test<4>()
    {
        set_test_name("a .luaurc over a scripter's own lints and mode overrides only what it says");
        const uint64_t unused   = ALLuauConfig::lintBit("LocalUnused");
        const uint64_t function = ALLuauConfig::lintBit("FunctionUnused");
        const uint64_t shadow   = ALLuauConfig::lintBit("LocalShadow");
        ensure("the lints have bits", unused && function && shadow && unused != function);
        ensure("an unknown lint has none", ALLuauConfig::lintBit("NoSuchLint") == 0);
        ensure("every lint is named", ALLuauConfig::lintNames().size() > 20 && ALLuauConfig::lintNames().front() == "UnknownGlobal");

        ALLuauConfig base;
        base.lints &= ~unused;          // the scripter turned it off
        base.fatalLints |= shadow;      // and made this one an error
        base.mode = "strict";
        ALLuauConfig config;
        std::string  error;
        ensure("an empty file", ALLuauConfig::parse("{}", config, error, &base));
        ensure("keeps the lints off", (config.lints & unused) == 0 && (config.lints & function) != 0);
        ensure("and the errors", (config.fatalLints & shadow) != 0);
        ensure_equals("and the mode", config.mode, std::string("strict"));

        ensure("a file saying one lint", ALLuauConfig::parse("{ \"lint\": { \"LocalUnused\": true, \"FunctionUnused\": false }, \"languageMode\": \"nonstrict\" }", config, error, &base));
        ensure("turns that one on", (config.lints & unused) != 0);
        ensure("and that one off", (config.lints & function) == 0);
        ensure("the rest as the scripter had them", (config.fatalLints & shadow) != 0);
        ensure_equals("its mode over theirs", config.mode, std::string("nonstrict"));

        ensure("without a base, Luau's defaults", ALLuauConfig::parse("{}", config, error) && (config.lints & unused) != 0 && config.mode.empty());
    }

    template<> template<>
    void alluauconfig_object::test<5>()
    {
        set_test_name("a file read over another adds its globals to the other's, and its aliases over the other's, as Luau reads a chain of them");
        ALLuauConfig outer;
        std::string  error;
        ensure("the outer file", ALLuauConfig::parse("{ \"globals\": [\"shared\"], \"aliases\": { \"lib\": \"./lib\", \"util\": \"./util\" } }", outer, error));
        ALLuauConfig inner;
        ensure("the inner over it", ALLuauConfig::parse("{ \"globals\": [\"mine\"], \"aliases\": { \"lib\": \"./mylib\" } }", inner, error, &outer));
        ensure("both globals", inner.globals == std::vector<std::string>{ "shared", "mine" });
        ensure_equals("the nearer alias", inner.aliases["lib"], std::string("./mylib"));
        ensure_equals("the outer's kept", inner.aliases["util"], std::string("./util"));
        ALLuauConfig quiet;
        ensure("one saying nothing of them", ALLuauConfig::parse("{}", quiet, error, &outer));
        ensure("keeps them", quiet.globals == std::vector<std::string>{ "shared" });
    }

    template<> template<>
    void alluauconfig_object::test<6>()
    {
        set_test_name("a chain of files, nearest first, read from the top down: the nearest to say a thing wins, and globals add up");
        const std::string root   = "{ \"languageMode\": \"strict\", \"globals\": [\"a\"], \"lint\": { \"LocalUnused\": false }, "
                                   "\"aliases\": { \"lib\": \"./rootlib\", \"root\": \"./r\" } }";
        const std::string broken = "{ not a configuration";
        const std::string middle = "{ \"globals\": [\"b\"], \"lint\": { \"FunctionUnused\": false }, \"aliases\": { \"lib\": \"./midlib\" } }";
        const std::string nearby = "{ \"languageMode\": \"nonstrict\", \"globals\": [\"c\"], \"lint\": { \"LocalUnused\": true } }";
        const std::vector<std::string_view> chain{ nearby, middle, broken, root };

        ALLuauConfig merged;
        ensure("parsed", ALLuauConfig::parseChain(chain, merged));
        ensure_equals("the nearest mode", merged.mode, std::string("nonstrict"));
        ensure("globals from every file, the furthest first", merged.globals == std::vector<std::string>{ "a", "b", "c" });
        const uint64_t local_unused    = ALLuauConfig::lintBit("LocalUnused");
        const uint64_t function_unused = ALLuauConfig::lintBit("FunctionUnused");
        ensure("a lint the root turned off and the nearest on", (merged.lints & local_unused) != 0);
        ensure("one the middle turned off, off", (merged.lints & function_unused) == 0);
        ensure_equals("an alias the middle says over the root's", merged.aliases["lib"], std::string("./midlib"));
        ensure_equals("the root's own kept", merged.aliases["root"], std::string("./r"));


        ALLuauConfig base;
        base.mode = "strict";
        ALLuauConfig none;
        ensure("none that parse is no configuration", !ALLuauConfig::parseChain({ broken }, none, &base));
        ensure_equals("and leaves the base", none.mode, std::string("strict"));
    }

    template<> template<>
    void alluauconfig_object::test<7>()
    {
        set_test_name("the studio's own lints in a .luaurc: taken out for Luau and said for the studio, first, last or alone, with the rest read whole; * says for them too");
        const uint64_t compound = ALScriptLintPass::bit("SlCompoundAssign");
        ensure("a rule of the table", compound != 0 && (ALScriptLintPass::defaults() & compound) != 0);
        ALLuauConfig out;
        std::string  error;
        ensure("beside Luau's: " + error,
               ALLuauConfig::parse("{ \"languageMode\": \"strict\", \"lint\": { \"LocalUnused\": false, \"SlCompoundAssign\": false } }", out, error));
        ensure("the rest read", out.mode == "strict" && (out.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        ensure("ours off", (out.slLints & compound) == 0);
        ensure("alone: " + error, ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": false } }", out, error) && (out.slLints & compound) == 0);
        ensure("first: " + error, ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": false, \"LocalUnused\": false }, \"globals\": [\"g\"] }", out, error) &&
                                      (out.slLints & compound) == 0 && out.globals == std::vector<std::string>{ "g" } &&
                                      (out.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        ensure("with // comments, over lines: " + error,
               ALLuauConfig::parse("{\n  // c style\n  \"lint\": {\n    \"LocalUnused\": true,\n    \"SlCompoundAssign\": false\n  }\n}", out, error) &&
                   (out.slLints & compound) == 0);
        ensure("* for ours too: " + error, ALLuauConfig::parse("{ \"lint\": { \"*\": false } }", out, error) && out.slLints == 0 &&
                                               (out.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        ensure("then one on: " + error,
               ALLuauConfig::parse("{ \"lint\": { \"*\": false, \"SlCompoundAssign\": true } }", out, error) && out.slLints == compound);
        ensure("an Sl name that is none", !ALLuauConfig::parse("{ \"lint\": { \"SlNope\": true } }", out, error) && error == "Unknown lint SlNope");
        ensure("a value that is none", !ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": \"fatal\" } }", out, error) &&
                                           error == "Bad setting 'fatal'.  Valid options are true and false");
        ensure("Luau's words on the rest keep their lines: " + error,
               !ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": false },\n  \"globals\": 3 }", out, error) && error.find("line 2") != std::string::npos);

        ALLuauConfig base;
        base.slLints      = 0;
        base.slFatalLints = compound;
        ensure("the base's where the file does not say: " + error,
               ALLuauConfig::parse("{ \"languageMode\": \"strict\" }", out, error, &base) && out.slLints == 0 && out.slFatalLints == compound);
    }
}
