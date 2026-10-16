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

#include "lluuid.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

namespace
{
    TEST_CASE("alluauconfig: aliases by lower-case name with their paths and the mode where it is said", "[alluauconfig]")
    {
        ALLuauConfig config;
        std::string  error;
        const bool   parsed = ALLuauConfig::parse("{\n  \"languageMode\": \"strict\",\n  \"aliases\": { \"Lib\": \"./lib\", \"shared\": \"../shared\", \"abs\": \"/opt/lua\" }\n}\n", config, error);
        UNSCOPED_INFO("parses: " + error);
        REQUIRE(parsed);
        REQUIRE(config.aliases.size() == size_t(3));
        UNSCOPED_INFO("by lower-case name");
        CHECK(config.aliases["lib"] == std::string("./lib"));
        UNSCOPED_INFO("a path up");
        CHECK(config.aliases["shared"] == std::string("../shared"));
        UNSCOPED_INFO("an absolute one");
        CHECK(config.aliases["abs"] == std::string("/opt/lua"));
        CHECK(config.mode == std::string("strict"));
        {
            INFO("no mode where none is said");
            CHECK(ALLuauConfig::parse("{ \"aliases\": {} }", config, error));
            REQUIRE(config.mode.empty());
        }
        {
            INFO("not a configuration");
            CHECK_FALSE(ALLuauConfig::parse("{ \"aliases\": 3 }", config, error));
            REQUIRE_FALSE(error.empty());
        }
        UNSCOPED_INFO("nor an alias Luau refuses");
        CHECK_FALSE(ALLuauConfig::parse("{ \"aliases\": { \"bad name\": \"x\" } }", config, error));
    }

    TEST_CASE("alluauconfig: aliasOf reads the @ name up to the slash; absolute paths and studio aliases", "[alluauconfig]")
    {
        std::string alias, rest;
        REQUIRE(ALLuauConfig::aliasOf("@Lib/util/strings", alias, rest));
        UNSCOPED_INFO("lower case");
        CHECK(alias == std::string("lib"));
        CHECK(rest == std::string("util/strings"));
        {
            INFO("the alias alone");
            CHECK(ALLuauConfig::aliasOf("@lib", alias, rest));
            CHECK(alias == "lib");
            REQUIRE(rest.empty());
        }
        UNSCOPED_INFO("not without the @");
        CHECK_FALSE(ALLuauConfig::aliasOf("lib/util", alias, rest));
        UNSCOPED_INFO("not an empty one");
        CHECK_FALSE(ALLuauConfig::aliasOf("@/util", alias, rest));
        UNSCOPED_INFO("absolute on posix");
        CHECK(ALLuauConfig::absolute("/opt/lua"));
        {
            INFO("absolute on windows");
            CHECK(ALLuauConfig::absolute("C:\\lua"));
            CHECK(ALLuauConfig::absolute("c:/lua"));
        }
        {
            INFO("relative");
            CHECK_FALSE(ALLuauConfig::absolute("./lib"));
            CHECK_FALSE(ALLuauConfig::absolute("lib"));
        }

        // Script Studio's own aliases: what Luau takes, not self, not sl-*.
        {
            INFO("a studio alias's name");
            CHECK(ALLuauConfig::studioAliasName("lib"));
            CHECK(ALLuauConfig::studioAliasName("My_Lib-2.x"));
        }
        {
            INFO("not self, in any case");
            CHECK_FALSE(ALLuauConfig::studioAliasName("self"));
            CHECK_FALSE(ALLuauConfig::studioAliasName("Self"));
        }
        {
            INFO("not sl-*");
            CHECK_FALSE(ALLuauConfig::studioAliasName("sl-std"));
            CHECK_FALSE(ALLuauConfig::studioAliasName("SL-x"));
        }
        {
            INFO("nothing Luau refuses");
            CHECK_FALSE(ALLuauConfig::studioAliasName(""));
            CHECK_FALSE(ALLuauConfig::studioAliasName("a/b"));
            CHECK_FALSE(ALLuauConfig::studioAliasName("a b"));
            CHECK_FALSE(ALLuauConfig::studioAliasName(".."));
        }
        UNSCOPED_INFO("a folder's name made one");
        CHECK(ALLuauConfig::studioAliasFor("My Library", {}) == std::string("my-library"));
        UNSCOPED_INFO("a number where it is taken, in any case");
        CHECK(ALLuauConfig::studioAliasFor("lib", { "LIB", "lib2" }) == std::string("lib3"));
        UNSCOPED_INFO("one that cannot be: lib");
        CHECK(ALLuauConfig::studioAliasFor("self", {}) == std::string("lib"));
    }

    TEST_CASE("alluauconfig: lints and globals come out as Luau reads them with its default lints", "[alluauconfig]")
    {
        ALLuauConfig config;
        std::string  error;
        const ALLuauConfig defaults;
        const bool         parsed = ALLuauConfig::parse("{\n  \"lint\": { \"LocalUnused\": false, \"LocalShadow\": true },\n  \"globals\": [\"Alchemy\", \"grid\"]\n}\n", config, error);
        UNSCOPED_INFO("parses: " + error);
        REQUIRE(parsed);
        REQUIRE(config.globals.size() == size_t(2));
        UNSCOPED_INFO("in order");
        CHECK(config.globals[0] == std::string("Alchemy"));
        // LocalUnused is code 7 and LocalShadow code 4 in Luau's numbering;
        // the second is off by default.
        UNSCOPED_INFO("LocalUnused off");
        CHECK((config.lints & (1ull << 7)) == 0);
        UNSCOPED_INFO("LocalShadow on");
        CHECK((config.lints & (1ull << 4)) != 0);
        UNSCOPED_INFO("the rest as they were");
        CHECK((config.lints & ~((1ull << 7) | (1ull << 4))) == (defaults.lints & ~((1ull << 7) | (1ull << 4))));
        {
            INFO("none fatal, not every lint an error");
            CHECK(config.fatalLints == 0);
            CHECK_FALSE(config.lintErrors);
        }
        {
            INFO("said nothing: the defaults");
            CHECK(ALLuauConfig::parse("{}", config, error));
            CHECK(config.lints == defaults.lints);
            CHECK(config.fatalLints == 0);
            REQUIRE(config.globals.empty());
        }
        {
            INFO("every lint an error where asked");
            CHECK(ALLuauConfig::parse("{ \"lintErrors\": true }", config, error));
            CHECK(config.lintErrors);
        }
    }

    TEST_CASE("alluauconfig: a .luaurc over a scripter's own lints and mode overrides only what it says", "[alluauconfig]")
    {
        const uint64_t unused   = ALLuauConfig::lintBit("LocalUnused");
        const uint64_t function = ALLuauConfig::lintBit("FunctionUnused");
        const uint64_t shadow   = ALLuauConfig::lintBit("LocalShadow");
        {
            INFO("the lints have bits");
            CHECK(unused);
            CHECK(function);
            CHECK(shadow);
            CHECK(unused != function);
        }
        UNSCOPED_INFO("an unknown lint has none");
        CHECK(ALLuauConfig::lintBit("NoSuchLint") == 0);
        {
            INFO("every lint is named");
            REQUIRE(ALLuauConfig::lintNames().size() > 20);
            CHECK(ALLuauConfig::lintNames().front() == "UnknownGlobal");
        }

        ALLuauConfig base;
        base.lints &= ~unused;          // the scripter turned it off
        base.fatalLints |= shadow;      // and made this one an error
        base.mode = "strict";
        ALLuauConfig config;
        std::string  error;
        UNSCOPED_INFO("an empty file");
        REQUIRE(ALLuauConfig::parse("{}", config, error, &base));
        {
            INFO("keeps the lints off");
            CHECK((config.lints & unused) == 0);
            CHECK((config.lints & function) != 0);
        }
        UNSCOPED_INFO("and the errors");
        CHECK((config.fatalLints & shadow) != 0);
        UNSCOPED_INFO("and the mode");
        CHECK(config.mode == std::string("strict"));

        UNSCOPED_INFO("a file saying one lint");
        CHECK(ALLuauConfig::parse("{ \"lint\": { \"LocalUnused\": true, \"FunctionUnused\": false }, \"languageMode\": \"nonstrict\" }", config, error, &base));
        UNSCOPED_INFO("turns that one on");
        CHECK((config.lints & unused) != 0);
        UNSCOPED_INFO("and that one off");
        CHECK((config.lints & function) == 0);
        UNSCOPED_INFO("the rest as the scripter had them");
        CHECK((config.fatalLints & shadow) != 0);
        UNSCOPED_INFO("its mode over theirs");
        CHECK(config.mode == std::string("nonstrict"));

        {
            INFO("without a base, Luau's defaults");
            CHECK(ALLuauConfig::parse("{}", config, error));
            CHECK((config.lints & unused) != 0);
            REQUIRE(config.mode.empty());
        }
    }

    TEST_CASE("alluauconfig: a file read over another adds to the globals and overrides the aliases", "[alluauconfig]")
    {
        ALLuauConfig outer;
        std::string  error;
        UNSCOPED_INFO("the outer file");
        REQUIRE(ALLuauConfig::parse("{ \"globals\": [\"shared\"], \"aliases\": { \"lib\": \"./lib\", \"util\": \"./util\" } }", outer, error));
        ALLuauConfig inner;
        UNSCOPED_INFO("the inner over it");
        REQUIRE(ALLuauConfig::parse("{ \"globals\": [\"mine\"], \"aliases\": { \"lib\": \"./mylib\" } }", inner, error, &outer));
        UNSCOPED_INFO("both globals");
        CHECK(inner.globals == std::vector<std::string>{ "shared", "mine" });
        UNSCOPED_INFO("the nearer alias");
        CHECK(inner.aliases["lib"] == std::string("./mylib"));
        UNSCOPED_INFO("the outer's kept");
        CHECK(inner.aliases["util"] == std::string("./util"));
        ALLuauConfig quiet;
        UNSCOPED_INFO("one saying nothing of them");
        REQUIRE(ALLuauConfig::parse("{}", quiet, error, &outer));
        UNSCOPED_INFO("keeps them");
        CHECK(quiet.globals == std::vector<std::string>{ "shared" });
    }

    TEST_CASE("alluauconfig: parseChain: the nearest file to say a thing wins and globals add up", "[alluauconfig]")
    {
        const std::string root   = "{ \"languageMode\": \"strict\", \"globals\": [\"a\"], \"lint\": { \"LocalUnused\": false }, "
                                   "\"aliases\": { \"lib\": \"./rootlib\", \"root\": \"./r\" } }";
        const std::string broken = "{ not a configuration";
        const std::string middle = "{ \"globals\": [\"b\"], \"lint\": { \"FunctionUnused\": false }, \"aliases\": { \"lib\": \"./midlib\" } }";
        const std::string nearby = "{ \"languageMode\": \"nonstrict\", \"globals\": [\"c\"], \"lint\": { \"LocalUnused\": true } }";
        const std::vector<std::string_view> chain{ nearby, middle, broken, root };

        ALLuauConfig merged;
        REQUIRE(ALLuauConfig::parseChain(chain, merged));
        UNSCOPED_INFO("the nearest mode");
        CHECK(merged.mode == std::string("nonstrict"));
        UNSCOPED_INFO("globals from every file, the furthest first");
        CHECK(merged.globals == std::vector<std::string>{ "a", "b", "c" });
        const uint64_t local_unused    = ALLuauConfig::lintBit("LocalUnused");
        const uint64_t function_unused = ALLuauConfig::lintBit("FunctionUnused");
        UNSCOPED_INFO("a lint the root turned off and the nearest on");
        CHECK((merged.lints & local_unused) != 0);
        UNSCOPED_INFO("one the middle turned off, off");
        CHECK((merged.lints & function_unused) == 0);
        UNSCOPED_INFO("an alias the middle says over the root's");
        CHECK(merged.aliases["lib"] == std::string("./midlib"));
        UNSCOPED_INFO("the root's own kept");
        CHECK(merged.aliases["root"] == std::string("./r"));


        ALLuauConfig base;
        base.mode = "strict";
        ALLuauConfig none;
        UNSCOPED_INFO("none that parse is no configuration");
        REQUIRE_FALSE(ALLuauConfig::parseChain({ broken }, none, &base));
        UNSCOPED_INFO("and leaves the base");
        CHECK(none.mode == std::string("strict"));
    }

    TEST_CASE("alluauconfig: the studio's lints in a .luaurc are read apart from Luau's and * covers them", "[alluauconfig]")
    {
        const uint64_t compound = ALScriptLintPass::bit("SlCompoundAssign");
        {
            INFO("a rule of the table");
            CHECK(compound != 0);
            CHECK((ALScriptLintPass::defaults() & compound) != 0);
        }
        ALLuauConfig out;
        std::string  error;
        const bool   beside = ALLuauConfig::parse("{ \"languageMode\": \"strict\", \"lint\": { \"LocalUnused\": false, \"SlCompoundAssign\": false } }", out, error);
        UNSCOPED_INFO("beside Luau's: " + error);
        REQUIRE(beside);
        {
            INFO("the rest read");
            CHECK(out.mode == "strict");
            CHECK((out.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        }
        UNSCOPED_INFO("ours off");
        CHECK((out.slLints & compound) == 0);
        {
            const bool parsed = ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": false } }", out, error);
            INFO("alone: " + error);
            CHECK(parsed);
            CHECK((out.slLints & compound) == 0);
        }
        {
            const bool parsed = ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": false, \"LocalUnused\": false }, \"globals\": [\"g\"] }", out, error);
            INFO("first: " + error);
            CHECK(parsed);
            CHECK((out.slLints & compound) == 0);
            CHECK(out.globals == std::vector<std::string>{ "g" });
            CHECK((out.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        }
        {
            const bool parsed = ALLuauConfig::parse("{\n  // c style\n  \"lint\": {\n    \"LocalUnused\": true,\n    \"SlCompoundAssign\": false\n  }\n}", out, error);
            INFO("with // comments, over lines: " + error);
            CHECK(parsed);
            CHECK((out.slLints & compound) == 0);
        }
        {
            const bool parsed = ALLuauConfig::parse("{ \"lint\": { \"*\": false } }", out, error);
            INFO("* for ours too: " + error);
            CHECK(parsed);
            CHECK(out.slLints == 0);
            CHECK((out.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        }
        {
            const bool parsed = ALLuauConfig::parse("{ \"lint\": { \"*\": false, \"SlCompoundAssign\": true } }", out, error);
            INFO("then one on: " + error);
            CHECK(parsed);
            CHECK(out.slLints == compound);
        }
        {
            INFO("an Sl name that is none");
            CHECK_FALSE(ALLuauConfig::parse("{ \"lint\": { \"SlNope\": true } }", out, error));
            CHECK(error == "Unknown lint SlNope");
        }
        {
            INFO("a value that is none");
            CHECK_FALSE(ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": \"fatal\" } }", out, error));
            CHECK(error == "Bad setting 'fatal'.  Valid options are true and false");
        }
        {
            const bool parsed = ALLuauConfig::parse("{ \"lint\": { \"SlCompoundAssign\": false },\n  \"globals\": 3 }", out, error);
            INFO("Luau's words on the rest keep their lines: " + error);
            CHECK_FALSE(parsed);
            REQUIRE(error.find("line 2") != std::string::npos);
        }

        ALLuauConfig base;
        base.slLints      = 0;
        base.slFatalLints = compound;
        {
            const bool parsed = ALLuauConfig::parse("{ \"languageMode\": \"strict\" }", out, error, &base);
            INFO("the base's where the file does not say: " + error);
            CHECK(parsed);
            CHECK(out.slLints == 0);
            CHECK(out.slFatalLints == compound);
        }
    }

    TEST_CASE("alluauconfig: a .config.luau's luau table reads as the .luaurc that says the same", "[alluauconfig]")
    {
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
        const bool  ran = ALLuauConfigScript::asLuaurc(source, json, error);
        UNSCOPED_INFO("ran: " + error);
        REQUIRE(ran);
        ALLuauConfig config;
        const bool   parsed = ALLuauConfig::parse(json, config, error);
        UNSCOPED_INFO("and parses as a .luaurc: " + error + " in " + json);
        REQUIRE(parsed);
        CHECK(config.mode == std::string("strict"));
        {
            INFO("its aliases, by name in lower case");
            REQUIRE(config.aliases.size() == 2);
            CHECK(config.aliases["lib"] == "./lib");
            CHECK(config.aliases["shared"] == "./lib/shared");
        }
        UNSCOPED_INFO("its globals in order");
        CHECK(config.globals == std::vector<std::string>{ "Alchemy", "grid" });
        UNSCOPED_INFO("Luau's lint off");
        CHECK((config.lints & ALLuauConfig::lintBit("LocalUnused")) == 0);
        UNSCOPED_INFO("and the studio's");
        CHECK((config.slLints & ALScriptLintPass::bit("SlCompoundAssign")) == 0);
        UNSCOPED_INFO("every lint an error");
        CHECK(config.lintErrors);

        {
            INFO("no luau table: a configuration that says nothing");
            CHECK(ALLuauConfigScript::asLuaurc("return { other = 1 }", json, error));
            CHECK(json == "{}");
        }
        {
            INFO("an empty one the same");
            CHECK(ALLuauConfigScript::asLuaurc("return { luau = {} }", json, error));
            CHECK(ALLuauConfig::parse(json, config, error));
            REQUIRE(config.aliases.empty());
        }
        {
            INFO("no globals: an empty list");
            CHECK(ALLuauConfigScript::asLuaurc("return { luau = { globals = {} } }", json, error));
            CHECK(ALLuauConfig::parse(json, config, error));
            REQUIRE(config.globals.empty());
        }
        // A .luaurc's strings are taken byte for byte: a backslash is kept
        // as it is, and what one cannot hold is refused.
        {
            INFO("a backslash kept");
            const bool written = ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'C:\\\\lua\\\\lib' } } }", json, error);
            UNSCOPED_INFO("ran: " + error);
            CHECK(written);
            const bool read = ALLuauConfig::parse(json, config, error);
            UNSCOPED_INFO("parses: " + error);
            CHECK(read);
            CHECK(config.aliases["q"] == "C:\\lua\\lib");
        }
        {
            INFO("not a quote");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'a\"b' } } }", json, error));
            REQUIRE(error.find("may not hold a quote") != std::string::npos);
        }
        UNSCOPED_INFO("nor a break");
        CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'a\\nb' } } }", json, error));
        UNSCOPED_INFO("nor a backslash at the end");
        CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { aliases = { q = 'C:\\\\lua\\\\' } } }", json, error));
        UNSCOPED_INFO("nor in a name");
        CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { aliases = { ['a\"'] = 'x' } } }", json, error));
        // Luau's own judgement of what the values say.
        {
            INFO("a mode that is none: Luau says so");
            CHECK(ALLuauConfigScript::asLuaurc("return { luau = { languagemode = 'loose' } }", json, error));
            CHECK_FALSE(ALLuauConfig::parse(json, config, error));
        }
    }

    TEST_CASE("alluauconfig: a .config.luau that fails says why; and it cannot reach Eris", "[alluauconfig]")
    {
        std::string json, error;
        {
            INFO("does not compile");
            REQUIRE_FALSE(ALLuauConfigScript::asLuaurc("return {", json, error));
            REQUIRE_FALSE(error.empty());
        }
        {
            INFO("an error, in its words");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("error('no config here')", json, error));
            REQUIRE(error.find("no config here") != std::string::npos);
        }
        UNSCOPED_INFO("a yield");
        CHECK((ALLuauConfigScript::asLuaurc("coroutine.yield()", json, error), error) == std::string("configuration execution cannot yield"));
        UNSCOPED_INFO("nothing returned");
        CHECK((ALLuauConfigScript::asLuaurc("local x = 1", json, error), error) == std::string("configuration must return exactly one value"));
        UNSCOPED_INFO("two returned");
        CHECK((ALLuauConfigScript::asLuaurc("return {}, {}", json, error), error) == std::string("configuration must return exactly one value"));
        UNSCOPED_INFO("not a table");
        CHECK((ALLuauConfigScript::asLuaurc("return 'x'", json, error), error) == std::string("configuration did not return a table"));
        UNSCOPED_INFO("luau not a table");
        CHECK((ALLuauConfigScript::asLuaurc("return { luau = 3 }", json, error), error) == std::string("configuration value for key \"luau\" must be a table"));
        {
            INFO("a function where a value goes");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { aliases = { x = print } } }", json, error));
            REQUIRE(error.find("must be strings, booleans") != std::string::npos);
        }
        // A .luaurc holds no numbers, and Luau's reading of one would stop
        // at it saying nothing of where: refused here, by its key.
        UNSCOPED_INFO("a number, by its key");
        CHECK((ALLuauConfigScript::asLuaurc("return { luau = { lint = { LocalUnused = 0 } } }", json, error), error) ==
                      std::string("configuration value for key \"LocalUnused\" must be a string or a boolean, not a number"));
        {
            INFO("in an array, by its place");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { globals = { 'a', 2 } } }", json, error));
            REQUIRE(error.find("\"globals[2]\"") != std::string::npos);
        }
        {
            INFO("at the top");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { linterrors = 1 } }", json, error));
            REQUIRE(error.find("\"linterrors\"") != std::string::npos);
        }
        {
            INFO("an array with a hole");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { globals = { [1] = 'a', [3] = 'b' } } }", json, error));
            REQUIRE(error.find("invalid numeric key") != std::string::npos);
        }
        {
            INFO("a table in itself, nesting forever");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("local t = {} t.t = t return { luau = { aliases = t } }", json, error));
            REQUIRE(error.find("nest too deeply") != std::string::npos);
        }
        // No metamethod runs as what it returned is read.
        {
            INFO("an __index that would loop is never asked");
            CHECK(ALLuauConfigScript::asLuaurc("return setmetatable({}, { __index = function() while true do end end })", json, error));
            CHECK(json == "{}");
        }

        // A run stopped by the deadline is kept a while, by its text: a text
        // of this run's own is run, not answered from what another test, or
        // this one before, asked.
        const std::string forever = "while true do end -- " + LLUUID::generateNewID().asString();
        const auto        started = std::chrono::steady_clock::now();
        UNSCOPED_INFO("a loop that never ends, stopped");
        CHECK((ALLuauConfigScript::asLuaurc(forever, json, error), error) == std::string("configuration execution timed out"));
        const F64 took = std::chrono::duration<F64>(std::chrono::steady_clock::now() - started).count();
        {
            INFO("stopped at its time, not long after");
            CHECK(took >= ALLuauConfigScript::MOST_SECONDS);
            CHECK(took < ALLuauConfigScript::MOST_SECONDS + 2.0);
        }
        UNSCOPED_INFO("asked again at once, kept for a while: no second wait");
        CHECK((ALLuauConfigScript::asLuaurc(forever, json, error),
               std::chrono::duration<F64>(std::chrono::steady_clock::now() - started).count() < took + 0.1));
        // table.move's copy is a loop in C the deadline does not reach, and
        // nils moved into a table that holds none take no memory: held to so
        // many elements, at once.
        const auto moving = std::chrono::steady_clock::now();
        {
            const bool ran = ALLuauConfigScript::asLuaurc("table.move({}, 1, 2147483646, 2) return {}", json, error);
            INFO("two billion nils moved, refused at once: " + error);
            CHECK_FALSE(ran);
            REQUIRE(error.find("table.move") != std::string::npos);
            CHECK(std::chrono::duration<F64>(std::chrono::steady_clock::now() - moving).count() < ALLuauConfigScript::MOST_SECONDS);
        }
        {
            const bool ran = ALLuauConfigScript::asLuaurc("return { luau = { globals = table.move({ 'a', 'b' }, 1, 2, 2, { 'z' }) } }", json, error);
            INFO("a move within it, as Luau's: " + error);
            CHECK(ran);
            CHECK(json == "{\"globals\": [\"z\", \"a\", \"b\"]}");
        }
        {
            INFO("memory past the most, refused");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 1000) .. i end return t", json, error));
            REQUIRE(error.find("memory") != std::string::npos);
        }
        {
            INFO("one string past it at once, refused");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc("return { luau = { x = string.rep('x', 64 * 1024 * 1024) } }", json, error));
            REQUIRE(error.find("memory") != std::string::npos);
        }

        // Second Life's VM opens Eris as `ares`; a configuration is run
        // without it. The rest of Luau's libraries are there.
        {
            const bool ran = ALLuauConfigScript::asLuaurc("return { luau = { languagemode = ares and 'strict' or 'nonstrict' } }", json, error);
            INFO("no ares: " + error);
            CHECK(ran);
            REQUIRE(json.find("nonstrict") != std::string::npos);
        }
        {
            const bool ran = ALLuauConfigScript::asLuaurc(
                "return { luau = { globals = { string.upper('a'), tostring(math.max(1, 2)), table.concat({ 'x', 'y' }), buffer and 'b' or 'none' } } }", json, error);
            INFO("Luau's libraries: " + error);
            CHECK(ran);
            CHECK(json == "{\"globals\": [\"A\", \"2\", \"xy\", \"b\"]}");
        }
    }

    TEST_CASE("alluauconfig: a .config.luau's run leaves no JSON on failure and no error on success", "[alluauconfig]")
    {
        const char* const failing = "return { luau = { languagemode = 'strict', lint = { LocalUnused = 0 } } }";
        std::string       json = "from before", error;
        {
            INFO("a failure part way through the writing");
            REQUIRE_FALSE(ALLuauConfigScript::asLuaurc(failing, json, error));
            REQUIRE_FALSE(error.empty());
        }
        UNSCOPED_INFO("leaves no JSON");
        CHECK(json == std::string());
        json = "from before";
        {
            INFO("asked again, kept");
            CHECK_FALSE(ALLuauConfigScript::asLuaurc(failing, json, error));
            REQUIRE_FALSE(error.empty());
        }
        UNSCOPED_INFO("and no JSON still");
        CHECK(json == std::string());

        const char* const working = "return { luau = { languagemode = 'strict' } }";
        error = "from before";
        {
            INFO("a success");
            CHECK(ALLuauConfigScript::asLuaurc(working, json, error));
            CHECK(json == "{\"languageMode\": \"strict\"}");
        }
        UNSCOPED_INFO("leaves no error");
        CHECK(error == std::string());
        error = "from before";
        {
            INFO("asked again, kept");
            CHECK(ALLuauConfigScript::asLuaurc(working, json, error));
            CHECK(json == "{\"languageMode\": \"strict\"}");
        }
        UNSCOPED_INFO("and no error still");
        CHECK(error == std::string());
    }
}
