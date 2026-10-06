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
#include "../luau/alluauconfigscript.h"
#include "../lint/alscriptlintpass.h"

#include "../test/lltut.h"

#include <chrono>

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

        // Script Studio's own aliases: what Luau takes, not self, not sl-*.
        ensure("a studio alias's name", ALLuauConfig::studioAliasName("lib") && ALLuauConfig::studioAliasName("My_Lib-2.x"));
        ensure("not self, in any case", !ALLuauConfig::studioAliasName("self") && !ALLuauConfig::studioAliasName("Self"));
        ensure("not sl-*", !ALLuauConfig::studioAliasName("sl-std") && !ALLuauConfig::studioAliasName("SL-x"));
        ensure("nothing Luau refuses", !ALLuauConfig::studioAliasName("") && !ALLuauConfig::studioAliasName("a/b") &&
                                           !ALLuauConfig::studioAliasName("a b") && !ALLuauConfig::studioAliasName(".."));
        ensure_equals("a folder's name made one", ALLuauConfig::studioAliasFor("My Library", {}), std::string("my-library"));
        ensure_equals("a number where it is taken, in any case", ALLuauConfig::studioAliasFor("lib", { "LIB", "lib2" }), std::string("lib3"));
        ensure_equals("one that cannot be: lib", ALLuauConfig::studioAliasFor("self", {}), std::string("lib"));
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

    template<> template<>
    void alluauconfig_object::test<8>()
    {
        set_test_name("a .config.luau is run and its luau table read as the .luaurc that says the same: aliases, mode, lints and globals, the studio's lints among them");
        const char* const source =
            "local root = './lib'\n"
            "local lints = { LocalUnused = false, SlCompoundAssign = false }\n"
            "return {\n"
            "    luau = {\n"
            "        languagemode = 'strict',\n"
            "        lint = lints,\n"
            "        linterrors = true,\n"
            "        globals = { 'Alchemy', 'grid' },\n"
            "        aliases = { Lib = root, ['shared'] = root .. '/shared' },\n"
            "        unknown = 'passed over, as Luau passes it over',\n"
            "    },\n"
            "    other = { tool = true },\n"
            "}\n";
        std::string json, error;
        ensure("ran: " + error, ALLuauConfigScript::asLuaurc(source, json, error));
        ALLuauConfig config;
        ensure("and parses as a .luaurc: " + error + " in " + json, ALLuauConfig::parse(json, config, error));
        ensure_equals("its mode", config.mode, std::string("strict"));
        ensure("its aliases, by name in lower case", config.aliases.size() == 2 && config.aliases["lib"] == "./lib" && config.aliases["shared"] == "./lib/shared");
        ensure("its globals in order", config.globals == std::vector<std::string>{ "Alchemy", "grid" });
        ensure("Luau's lint off", (config.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        ensure("and the studio's", (config.slLints & ALScriptLintPass::bit("SlCompoundAssign")) == 0);
        ensure("every lint an error", config.lintErrors);

        ensure("no luau table: a configuration that says nothing", ALLuauConfigScript::asLuaurc("return { other = 1 }", json, error) && json == "{}");
        ensure("an empty one the same", ALLuauConfigScript::asLuaurc("return { luau = {} }", json, error) &&
                                            ALLuauConfig::parse(json, config, error) && config.aliases.empty());
        ensure("no globals: an empty list", ALLuauConfigScript::asLuaurc("return { luau = { globals = {} } }", json, error) &&
                                                ALLuauConfig::parse(json, config, error) && config.globals.empty());
        // A .luaurc's strings are taken byte for byte: a backslash is kept
        // as it is, and what one cannot hold is refused.
        ensure("a backslash kept: " + error, ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'C:\\\\lua\\\\lib' } } }", json, error) &&
                                                 ALLuauConfig::parse(json, config, error) && config.aliases["q"] == "C:\\lua\\lib");
        ensure("not a quote", !ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'a\"b' } } }", json, error) &&
                                  error.find("may not hold a quote") != std::string::npos);
        ensure("nor a break", !ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'a\\nb' } } }", json, error));
        ensure("nor a backslash at the end", !ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'C:\\\\lua\\\\' } } }", json, error));
        ensure("nor in a name", !ALLuauConfigScript::asLuaurc("return { luau = { aliases = { ['a\"'] = 'x' } } }", json, error));
        // Luau's own judgement of what the values say.
        ensure("a mode that is none: Luau says so", ALLuauConfigScript::asLuaurc("return { luau = { languagemode = 'loose' } }", json, error) &&
                                                       !ALLuauConfig::parse(json, config, error));
    }

    template<> template<>
    void alluauconfig_object::test<9>()
    {
        set_test_name("a .config.luau that fails says why: not compiling, an error, a yield, the wrong return, a value no configuration holds, too long, too much memory; and it cannot reach Eris");
        std::string json, error;
        ensure("does not compile", !ALLuauConfigScript::asLuaurc("return {", json, error) && !error.empty());
        ensure("an error, in its words", !ALLuauConfigScript::asLuaurc("error('no config here')", json, error) && error.find("no config here") != std::string::npos);
        ensure_equals("a yield", (ALLuauConfigScript::asLuaurc("coroutine.yield()", json, error), error), std::string("configuration execution cannot yield"));
        ensure_equals("nothing returned", (ALLuauConfigScript::asLuaurc("local x = 1", json, error), error), std::string("configuration must return exactly one value"));
        ensure_equals("two returned", (ALLuauConfigScript::asLuaurc("return {}, {}", json, error), error), std::string("configuration must return exactly one value"));
        ensure_equals("not a table", (ALLuauConfigScript::asLuaurc("return 'x'", json, error), error), std::string("configuration did not return a table"));
        ensure_equals("luau not a table", (ALLuauConfigScript::asLuaurc("return { luau = 3 }", json, error), error),
                      std::string("configuration value for key \"luau\" must be a table"));
        ensure("a function where a value goes", !ALLuauConfigScript::asLuaurc("return { luau = { aliases = { x = print } } }", json, error) &&
                                                    error.find("must be strings, numbers, booleans") != std::string::npos);
        ensure("an array with a hole", !ALLuauConfigScript::asLuaurc("return { luau = { globals = { [1] = 'a', [3] = 'b' } } }", json, error) &&
                                           error.find("invalid numeric key") != std::string::npos);
        ensure("a table in itself, nesting forever", !ALLuauConfigScript::asLuaurc("local t = {} t.t = t return { luau = { aliases = t } }", json, error) &&
                                                         error.find("nest too deeply") != std::string::npos);
        // No metamethod runs as what it returned is read.
        ensure("an __index that would loop is never asked",
               ALLuauConfigScript::asLuaurc("return setmetatable({}, { __index = function() while true do end end })", json, error) && json == "{}");

        const auto started = std::chrono::steady_clock::now();
        ensure_equals("a loop that never ends, stopped", (ALLuauConfigScript::asLuaurc("while true do end", json, error), error),
                      std::string("configuration execution timed out"));
        const F64 took = std::chrono::duration<F64>(std::chrono::steady_clock::now() - started).count();
        ensure(llformat("stopped at its time, not long after: %.2f s", took), took >= ALLuauConfigScript::MOST_SECONDS && took < ALLuauConfigScript::MOST_SECONDS + 2.0);
        ensure("asked again, kept: no second wait", (ALLuauConfigScript::asLuaurc("while true do end", json, error),
                                                      std::chrono::duration<F64>(std::chrono::steady_clock::now() - started).count() < took + 0.1));
        ensure("memory past the most, refused",
               !ALLuauConfigScript::asLuaurc("local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 1000) .. i end return t", json, error) &&
                   error.find("memory") != std::string::npos);
        ensure("one string past it at once, refused", !ALLuauConfigScript::asLuaurc("return { luau = { x = string.rep('x', 64 * 1024 * 1024) } }", json, error) &&
                                                          error.find("memory") != std::string::npos);

        // Second Life's VM opens Eris as `ares`; a configuration is run
        // without it. The rest of Luau's libraries are there.
        ensure("no ares: " + error, ALLuauConfigScript::asLuaurc("return { luau = { languagemode = ares and 'strict' or 'nonstrict' } }", json, error) &&
                                        json.find("nonstrict") != std::string::npos);
        ensure("Luau's libraries: " + error,
               ALLuauConfigScript::asLuaurc("return { luau = { globals = { string.upper('a'), tostring(math.max(1, 2)), table.concat({ 'x', 'y' }), buffer and 'b' or 'none' } } }",
                                            json, error) &&
                   json == "{\"globals\": [\"A\", \"2\", \"xy\", \"b\"]}");
    }
}
