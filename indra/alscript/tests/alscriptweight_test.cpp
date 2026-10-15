/**
 * @file tests/alscriptweight_test.cpp
 * @brief What a script weighs: SLua by its bytecode read back, LSO by its image.
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

#include "../core/alscriptweight.h"

#include "../lsl/allslservice.h"
#include "../luau/alluauservice.h"
#include "../luau/alluausharedstart.h"
#include "../preprocessor/alsourcemap.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <numeric>
#include <sstream>

namespace
{
    struct alscriptweight_data
    {
        ALLSLService lsl;
        std::string  error;
        bool         lslLoaded = false;

        alscriptweight_data() { lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error); }

        static const ALScriptWeight::Part* named(const ALScriptWeight& weight, const std::string& name)
        {
            for (const ALScriptWeight::Part& part : weight.parts)
            {
                if (part.name == name)
                {
                    return &part;
                }
            }
            return nullptr;
        }

        static size_t at(const ALScriptWeight& weight, S32 line)
        {
            return within(weight, line, line);
        }

        static size_t within(const ALScriptWeight& weight, S32 from, S32 to)
        {
            size_t bytes = 0;
            for (const ALScriptWeight::Line& line : weight.lines)
            {
                bytes += line.line >= from && line.line <= to ? line.bytes : 0;
            }
            return bytes;
        }

        static std::string lined(const ALScriptWeight& weight)
        {
            std::string out;
            for (const ALScriptWeight::Line& line : weight.lines)
            {
                out += " " + std::to_string(line.line) + "=" + std::to_string(line.bytes);
            }
            return out;
        }

        // A global string, a function, and a handler with a loop, a long
        // string and a short one, and no return of its own.
        static const std::string& byLine()
        {
            static const std::string script = "integer gCount = 0;\n"
                                              "string gName = \"a name a little longer than a word\";\n"
                                              "integer twice(integer n)\n"
                                              "{\n"
                                              "    return n * 2;\n"
                                              "}\n"
                                              "default\n"
                                              "{\n"
                                              "    state_entry()\n"
                                              "    {\n"
                                              "        integer i;\n"
                                              "        for (i = 0; i < 3; ++i)\n"
                                              "        {\n"
                                              "            gCount = twice(gCount);\n"
                                              "        }\n"
                                              "        llOwnerSay(\"a sentence of some length, said to the owner on every start\");\n"
                                              "        llOwnerSay(\"x\");\n"
                                              "    }\n"
                                              "}\n";
            return script;
        }

        static std::string listed(const ALScriptWeight& weight)
        {
            std::string out;
            for (const ALScriptWeight::Part& part : weight.parts)
            {
                out += " " + part.within + (part.within.empty() ? "" : ".") + part.name + "=" + std::to_string(part.bytes);
            }
            return out;
        }

        // A shared start's edits made from the end back, as an editor's
        // replaceAll does.
        static std::string require_applied(const std::string& script, const ALLuauSharedStart::Rewrite& rewrite)
        {
            std::vector<std::string> lines;
            for (size_t from = 0; from <= script.size();)
            {
                const size_t cut = std::min(script.find('\n', from), script.size());
                lines.push_back(script.substr(from, cut - from));
                from = cut + 1;
            }
            for (auto edit = rewrite.edits.rbegin(); edit != rewrite.edits.rend(); ++edit)
            {
                UNSCOPED_INFO("one line each");
                REQUIRE(edit->line == edit->endLine);
                std::string& line = lines[static_cast<size_t>(edit->line)];
                line.replace(static_cast<size_t>(edit->column), static_cast<size_t>(edit->endColumn - edit->column), edit->text);
            }
            std::string after;
            for (size_t i = 0; i < lines.size(); ++i)
            {
                after += lines[i] + (i + 1 < lines.size() ? "\n" : "");
            }
            return after;
        }

        // The texts of a shared start's strings.
        static std::vector<std::string> textsOf(const ALScriptWeight& weight, const ALScriptWeight::SharedStart& shared)
        {
            std::vector<std::string> out;
            for (const size_t i : shared.strings)
            {
                out.push_back(weight.strings[i].text);
            }
            return out;
        }
    };

    // SLua weighs what the server charges, the bytecode; each function is
    // its prototype, where its lines are, and each line the instructions
    // that came of it.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: SLua weighs the bytecode the server charges; each function a part", "[ALScriptWeight]")
    {
        const std::string script = "local function greet(name: string)\n"
                                   "    ll.OwnerSay(\"hello \" .. name)\n"
                                   "end\n"
                                   "\n"
                                   "local function count(n: number)\n"
                                   "    local total = 0\n"
                                   "    for i = 1, n do\n"
                                   "        total += i\n"
                                   "    end\n"
                                   "    return total\n"
                                   "end\n"
                                   "local function nothing() end\n"
                                   "nothing()\n"
                                   "greet(tostring(count(10)))\n";
        const ALScriptWeight weight = ALScriptWeigh::slua(script);
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        UNSCOPED_INFO("against SLua's limit");
        CHECK(weight.limit == size_t(131072));
        CHECK_FALSE(weight.estimate);
        CHECK(weight.total > 50);
        CHECK(weight.total < 2000);
        const ALScriptWeight::Part* greet = named(weight, "greet");
        const ALScriptWeight::Part* count = named(weight, "count");
        {
            INFO("each function a part:" + listed(weight));
            REQUIRE(greet);
            REQUIRE(count);
        }
        {
            INFO("where each is declared");
            CHECK(greet->line == 0);
            CHECK(count->line == 4);
        }
        UNSCOPED_INFO("and where count ends: the line of its end");
        CHECK(count->endLine == 10);
        const ALScriptWeight::Part* nothing = named(weight, "nothing");
        {
            INFO("a function that does nothing weighs least:" + listed(weight));
            REQUIRE(nothing);
            CHECK(nothing->bytes < greet->bytes);
            CHECK(nothing->bytes < count->bytes);
        }
        CHECK(named(weight, "script"));
        CHECK(named(weight, "strings"));
        const size_t parts = std::accumulate(weight.parts.begin(), weight.parts.end(), size_t(0),
                                             [](size_t sum, const ALScriptWeight::Part& p) { return sum + p.bytes; });
        {
            INFO("the parts are nearly the whole: " << parts << " of " << weight.total);
            CHECK(parts <= weight.total);
            CHECK(weight.total - parts < 16);
        }
        bool body = false;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            body = body || (line.line == 7 && line.bytes > 0);
        }
        UNSCOPED_INFO("the loop's body has bytes of its own");
        CHECK(body);
    }

    // A long string is weight as much as code is; and a script that does
    // not compile says so.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: a long string weighs as code does; a script that fails to compile says so", "[ALScriptWeight]")
    {
        const std::string    words(2000, 'x');
        const ALScriptWeight heavy = ALScriptWeigh::slua("local s = \"" + words + "\"\nprint(s)\n");
        const ALScriptWeight light = ALScriptWeigh::slua("local s = \"x\"\nprint(s)\n");
        CHECK(heavy.compiled);
        CHECK(light.compiled);
        CHECK(heavy.total >= light.total + 2000);
        // Heavy, it is a part of its own, and no longer the table of
        // strings'.
        const ALScriptWeight::Part* constant = nullptr;
        for (const ALScriptWeight::Part& one : heavy.parts)
        {
            constant = one.kind == ALScriptWeight::Part::Kind::Constant && one.name != "strings" ? &one : constant;
        }
        // At the line that loads it: a local never set again is folded into
        // where it is used, print(s), as -O1 compiles it.
        {
            INFO("a part of its own:" + listed(heavy));
            REQUIRE(constant);
            CHECK(constant->bytes >= 2000);
            CHECK(constant->line == 1);
        }
        {
            INFO("named by its start: " + (constant ? constant->name : std::string()));
            REQUIRE(constant);
            CHECK(constant->name.rfind("\"xxxx", 0) == 0);
        }
        {
            INFO("not the strings' too");
            REQUIRE(named(heavy, "strings"));
            CHECK(named(heavy, "strings")->bytes < 100);
        }
        UNSCOPED_INFO("a light one stays the strings'");
        CHECK(std::none_of(light.parts.begin(), light.parts.end(), [](const ALScriptWeight::Part& one) {
                   return one.kind == ALScriptWeight::Part::Kind::Constant && one.name != "strings";
               }));
        // And for the LSL targets: LSO's in its code, Mono's in its strings,
        // LSL on Luau's in its table; each a part of its own, out of the
        // handler that held it, the whole as it was.
        const std::string lsl = "default { state_entry() { llOwnerSay(\"" + words + "\"); } }\n";
        for (const ALScriptWeight& one : { ALScriptWeigh::lso(lsl), ALScriptWeigh::mono(lsl), ALScriptWeigh::lslLuau(lsl) })
        {
            const ALScriptWeight::Part* found = nullptr;
            size_t                      sum   = 0;
            for (const ALScriptWeight::Part& part : one.parts)
            {
                found = part.kind == ALScriptWeight::Part::Kind::Constant && part.name != "strings" ? &part : found;
                sum += part.bytes;
            }
            const std::string target = ALScriptWeight::nameOf(one.target);
            {
                INFO(target + ": a part of its own:" + listed(one));
                REQUIRE(one.compiled);
                REQUIRE(found);
                REQUIRE(found->bytes >= 2000);
                REQUIRE(found->line == 0);
            }
            UNSCOPED_INFO(target + ": counted once");
            REQUIRE(sum <= one.total);
        }
        // A table's values written in it, which no instruction loads -- the
        // template carries them: heavy ones are parts of their own too, at
        // the table's line.
        const ALScriptWeight table = ALScriptWeigh::slua("local MSG = {\n    welcome = \"" + std::string(300, 'w') + "\",\n    help = \"" +
                                                         std::string(300, 'h') + "\",\n}\nprint(MSG.welcome, MSG.help)\n");
        size_t values = 0;
        for (const ALScriptWeight::Part& one : table.parts)
        {
            values += one.kind == ALScriptWeight::Part::Kind::Constant && one.name != "strings" && one.bytes >= 300 && one.line >= 0 && one.line <= 3;
        }
        UNSCOPED_INFO("each value a part of its own, at the table:" + listed(table));
        CHECK(values == size_t(2));
        // Named by whole characters: the cut, 32 bytes in, falls inside
        // one, or just after one.
        const auto whole = [](const std::string& name) {
            for (size_t i = 0; i < name.size();)
            {
                const unsigned char lead = static_cast<unsigned char>(name[i]);
                const size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
                if (length == 0 || i + length > name.size())
                {
                    return false;
                }
                for (size_t k = 1; k < length; ++k)
                {
                    if ((static_cast<unsigned char>(name[i + k]) & 0xC0) != 0x80)
                    {
                        return false;
                    }
                }
                i += length;
            }
            return true;
        };
        std::string cyrillic, cjk = "a";
        for (int i = 0; i < 200; ++i)
        {
            cyrillic += "\xD0\x96";
            cjk += "\xE3\x81\x82";
        }
        for (const std::string& text : { cyrillic, cjk })
        {
            const ALScriptWeight wide = ALScriptWeigh::slua("local s = \"" + text + "\"\nprint(s)\n");
            const ALScriptWeight::Part* found = nullptr;
            for (const ALScriptWeight::Part& one : wide.parts)
            {
                found = one.kind == ALScriptWeight::Part::Kind::Constant && one.name != "strings" ? &one : found;
            }
            {
                INFO("a wide one named: " + listed(wide));
                REQUIRE(found);
                REQUIRE(whole(found->name));
                REQUIRE(found->name.find("\xE2\x80\xA6") != std::string::npos);
            }
        }

        const ALScriptWeight broken = ALScriptWeigh::slua("local function (\n");
        CHECK_FALSE(broken.compiled);
        CHECK_FALSE(broken.error.empty());
    }

    // LSO weighs its image to the top of its heap: the registers, the
    // globals, the functions, the states with their handlers, and the heap;
    // each function, state, handler and global a part, where it is.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: LSO weighs its image to the top of its heap", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::string script = "integer gCount = 0;\n"
                                   "string gName = \"a name a little longer than a word\";\n"
                                   "integer twice(integer n)\n"
                                   "{\n"
                                   "    return n * 2;\n"
                                   "}\n"
                                   "default\n"
                                   "{\n"
                                   "    state_entry()\n"
                                   "    {\n"
                                   "        gCount = twice(gCount);\n"
                                   "    }\n"
                                   "    touch_start(integer n)\n"
                                   "    {\n"
                                   "        llOwnerSay(gName + \" \" + (string)gCount + \" \" + (string)n);\n"
                                   "    }\n"
                                   "}\n";
        const ALScriptWeight weight = ALScriptWeigh::lso(script);
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        UNSCOPED_INFO("against LSO's limit");
        CHECK(weight.limit == size_t(16384));
        CHECK(weight.total > 0);
        CHECK(weight.total < 4096);
        const ALScriptWeight::Part* twice = named(weight, "twice");
        const ALScriptWeight::Part* state = named(weight, "default");
        const ALScriptWeight::Part* touch = named(weight, "touch_start");
        {
            INFO("each a part:" + listed(weight));
            REQUIRE(twice);
            REQUIRE(state);
            REQUIRE(touch);
            CHECK(named(weight, "state_entry"));
            CHECK(named(weight, "registers"));
        }
        UNSCOPED_INFO("a function where it is");
        CHECK(twice->line == 2);
        UNSCOPED_INFO("a handler in its state");
        CHECK(touch->within == std::string("default"));
        // Where its handlers begin and a jump table entry for each; each
        // handler, from where it begins to where it ends, its own part.
        UNSCOPED_INFO("a state is its table, its handlers apart:" + listed(weight));
        CHECK(state->bytes == size_t(5 + 8 * 2));
        const ALScriptWeight::Part* count = named(weight, "gCount");
        const ALScriptWeight::Part* name  = named(weight, "gName");
        REQUIRE(count);
        REQUIRE(name);
        UNSCOPED_INFO("a string global weighs its string");
        CHECK(name->bytes > count->bytes);
    }

    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: what does not fit in 16 KB says so", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        std::string script = "list gWords = [";
        for (int i = 0; i < 600; ++i)
        {
            script += std::string(i ? ", " : "") + "\"" + std::string(40, 'a' + i % 26) + "\"";
        }
        script += "];\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay(llList2String(gWords, 0));\n    }\n}\n";
        const ALScriptWeight weight = ALScriptWeigh::lso(script);
        {
            INFO("does not fit: " << weight.error << " " << weight.total);
            CHECK_FALSE(weight.compiled);
            CHECK_FALSE(weight.error.empty());
        }
        UNSCOPED_INFO("and weighs more than there is");
        CHECK(weight.total > weight.limit);
        const ALScriptWeight broken = ALScriptWeigh::lso("default { state_entry() { integer x = ; } }\n");
        CHECK_FALSE(broken.compiled);
        CHECK_FALSE(broken.error.empty());
    }

    // Mono is an estimate: the IL sized by the opcode table with what it
    // declares, over what an assembly costs; each function and handler a
    // part where it is, and the parts are the whole.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: Mono is an estimate: the IL sized by the opcode table with what it declares", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::string script = "integer gCount = 0;\n"
                                   "integer twice(integer n)\n"
                                   "{\n"
                                   "    return n * 2;\n"
                                   "}\n"
                                   "default\n"
                                   "{\n"
                                   "    state_entry()\n"
                                   "    {\n"
                                   "        gCount = twice(gCount);\n"
                                   "    }\n"
                                   "    touch_start(integer n)\n"
                                   "    {\n"
                                   "        llOwnerSay(\"a sentence of some length, said to the owner on every touch\");\n"
                                   "    }\n"
                                   "}\n";
        const ALScriptWeight weight = ALScriptWeigh::mono(script);
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        CHECK(weight.estimate);
        UNSCOPED_INFO("against Mono's limit");
        CHECK(weight.limit == size_t(65536));
        CHECK(weight.total > ALScriptWeigh::MONO_BASE_BYTES);
        CHECK(weight.total < 8192);
        const ALScriptWeight::Part* twice = named(weight, "twice");
        const ALScriptWeight::Part* touch = named(weight, "touch_start");
        const ALScriptWeight::Part* entry = named(weight, "state_entry");
        {
            INFO("each a part:" + listed(weight));
            REQUIRE(twice);
            REQUIRE(touch);
            REQUIRE(entry);
            CHECK(named(weight, "globals"));
            CHECK(named(weight, "assembly"));
        }
        UNSCOPED_INFO("a function where it is");
        CHECK(twice->line == 1);
        UNSCOPED_INFO("a handler in its state");
        CHECK(touch->within == std::string("default"));
        CHECK(touch->line == 11);
        UNSCOPED_INFO("a long string weighs:" + listed(weight));
        CHECK(touch->bytes > entry->bytes);
        const size_t parts = std::accumulate(weight.parts.begin(), weight.parts.end(), size_t(0),
                                             [](size_t sum, const ALScriptWeight::Part& p) { return sum + p.bytes; });
        CHECK(parts == weight.total);
    }

    // LSL on Luau: the fork's own compiler, the asset as the server makes
    // it, the bytecode read back as SLua's is; and again with its lines,
    // for what each comes to.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: LSL on Luau: the asset as the server makes it with the fork's compiler", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::string script = "integer gCount = 0;\n"
                                   "integer twice(integer n)\n"
                                   "{\n"
                                   "    return n * 2;\n"
                                   "}\n"
                                   "default\n"
                                   "{\n"
                                   "    state_entry()\n"
                                   "    {\n"
                                   "        gCount = twice(gCount);\n"
                                   "    }\n"
                                   "    touch_start(integer n)\n"
                                   "    {\n"
                                   "        llOwnerSay((string)gCount + \" \" + (string)n);\n"
                                   "    }\n"
                                   "}\n";
        const ALScriptWeight weight = ALScriptWeigh::lslLuau(script);
        UNSCOPED_INFO("compiled: " + weight.error + listed(weight));
        CHECK(weight.compiled);
        UNSCOPED_INFO("against Luau's limit");
        CHECK(weight.limit == size_t(131072));
        CHECK_FALSE(weight.estimate);
        {
            INFO("parts:" + listed(weight));
            CHECK(weight.total > 50);
            CHECK(weight.total < 4096);
        }
        const ALScriptWeight::Part* twice = named(weight, "twice");
        UNSCOPED_INFO("a function a part:" + listed(weight));
        REQUIRE(twice != nullptr);
        CHECK(twice->line == 1);
        const ALScriptWeight::Part* touch = named(weight, "touch_start");
        {
            INFO("a handler in its state:" + listed(weight));
            REQUIRE(touch);
            CHECK(touch->within == "default");
            CHECK(touch->kind == ALScriptWeight::Part::Kind::Handler);
        }
        CHECK(touch->line == 11);
        // Its lines, read off the compiler's lined bytecode, which the
        // server's is not: the total stays what the server charges.
        {
            INFO("each line weighed:" + lined(weight));
            CHECK(at(weight, 9) > 0);
            CHECK(at(weight, 13) > 0);
            CHECK(at(weight, 3) > 0);
        }
        UNSCOPED_INFO("the handler's long line its own:" + lined(weight));
        CHECK(at(weight, 13) > at(weight, 9));
        UNSCOPED_INFO("nothing past the script's lines:" + lined(weight));
        CHECK(within(weight, 16, 1000) == 0);
        const ALScriptWeight broken = ALScriptWeigh::lslLuau("default { state_entry() { integer x = ; } }\n");
        CHECK_FALSE(broken.compiled);
        CHECK_FALSE(broken.error.empty());
    }

    // LSO by line: what the compiler wrote while it stood at each
    // statement and expression, a function's missing return at its closing
    // brace, and a global's value on its line. A function's lines are its
    // code, which is the function less its header.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: LSO by line: what the compiler wrote at each statement and expression", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const ALScriptWeight weight = ALScriptWeigh::lso(byLine());
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        CHECK_FALSE(weight.lines.empty());
        UNSCOPED_INFO("the loop's body" + lined(weight));
        CHECK(at(weight, 13) > 0);
        UNSCOPED_INFO("the loop itself: its test, its step and its jump back" + lined(weight));
        CHECK(at(weight, 11) > 0);
        UNSCOPED_INFO("a long string weighs more than a short one" + lined(weight));
        CHECK(at(weight, 15) > at(weight, 16) + 40);
        UNSCOPED_INFO("the return it does not write, at its closing brace" + lined(weight));
        CHECK(at(weight, 17) > 0);
        UNSCOPED_INFO("a function's return" + lined(weight));
        CHECK(at(weight, 4) > 0);
        UNSCOPED_INFO("a global string's value" + lined(weight));
        CHECK(at(weight, 1) >= 34);
        {
            INFO("a brace that makes nothing" + lined(weight));
            CHECK(at(weight, 7) == 0);
            CHECK(at(weight, 3) == 0);
        }
        const ALScriptWeight::Part* entry = named(weight, "state_entry");
        const ALScriptWeight::Part* twice = named(weight, "twice");
        {
            INFO("both parts:" + listed(weight));
            REQUIRE(entry);
            REQUIRE(twice);
        }
        UNSCOPED_INFO("a handler's lines are all of it but its header, where its code begins");
        CHECK(entry->bytes - within(weight, entry->line, entry->endLine) ==
                      size_t(5));
        UNSCOPED_INFO("a function's lines are all of it but its header: where its code begins, its type and its parameter's");
        CHECK(twice->bytes - within(weight, twice->line, twice->endLine) == size_t(9));
    }

    // Mono by line, the same way: each instruction, with its string, is
    // the line it was written for; a global's initialiser is its line's.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: Mono by line: each instruction with its string is its line's", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const ALScriptWeight weight = ALScriptWeigh::mono(byLine());
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        CHECK_FALSE(weight.lines.empty());
        UNSCOPED_INFO("the loop's body" + lined(weight));
        CHECK(at(weight, 13) > 0);
        UNSCOPED_INFO("the loop itself" + lined(weight));
        CHECK(at(weight, 11) > 0);
        UNSCOPED_INFO("a long string weighs more than a short one" + lined(weight));
        CHECK(at(weight, 15) > at(weight, 16) + 40);
        UNSCOPED_INFO("the return it does not write, at its closing brace" + lined(weight));
        CHECK(at(weight, 17) > 0);
        UNSCOPED_INFO("a global string's initialiser" + lined(weight));
        CHECK(at(weight, 1) >= 34);
        {
            INFO("a brace that makes nothing" + lined(weight));
            CHECK(at(weight, 7) == 0);
            CHECK(at(weight, 3) == 0);
        }
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            if (part.line >= 0)
            {
                UNSCOPED_INFO(part.name + "'s lines are no more than it" + lined(weight));
                REQUIRE(within(weight, part.line, part.endLine) <= part.bytes);
            }
        }
        size_t all = 0;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            all += line.bytes;
        }
        CHECK(all > 0);
        CHECK(all < weight.total);
    }

    // In the source's places: a part in an include is in the include, at
    // its line there, and a line's bytes are the source line's; what the
    // map has no origin for keeps its part without a place.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: in the source's places: a part in an include is at its line there", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const std::vector<std::string> lines = { "integer twice(integer n)",
                                                 "{",
                                                 "    return n * 2;",
                                                 "}",
                                                 "default",
                                                 "{",
                                                 "    state_entry()",
                                                 "    {",
                                                 "        llOwnerSay((string)twice(2));",
                                                 "    }",
                                                 "}" };
        std::string expanded;
        for (const std::string& line : lines)
        {
            expanded += line + "\n";
        }
        // The first four lines are the include's, the rest the script's from
        // its second line on: its first is the #include.
        const auto mapped = [&lines](bool with_include) {
            ALSourceMap map;
            map.addFile("script", "script");
            map.addFile("lib.lsl", "disk:/scripts/lib.lsl");
            for (S32 out = with_include ? 0 : 4; out < S32(lines.size()); ++out)
            {
                ALSourceMap::Segment segment;
                segment.outLine = out;
                segment.length  = S32(lines[out].size());
                segment.file    = out < 4 ? 1 : 0;
                segment.line    = out < 4 ? out : out - 3;
                map.add(segment);
            }
            map.finish();
            return map;
        };
        const ALScriptWeight weight = ALScriptWeigh::lso(expanded);
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        const ALScriptWeight placed = weight.inSource(mapped(true));
        CHECK(placed.total == weight.total);
        const ALScriptWeight::Part* twice = named(placed, "twice");
        const ALScriptWeight::Part* entry = named(placed, "state_entry");
        {
            INFO("both:" + listed(placed));
            REQUIRE(twice);
            REQUIRE(entry);
        }
        UNSCOPED_INFO("a function in the include");
        CHECK(twice->file == std::string("disk:/scripts/lib.lsl"));
        UNSCOPED_INFO("at its line there");
        CHECK(twice->line == 0);
        UNSCOPED_INFO("to its end there");
        CHECK(twice->endLine == 3);
        UNSCOPED_INFO("a handler in the script");
        CHECK(entry->file.empty());
        UNSCOPED_INFO("at the script's line");
        CHECK(entry->line == 3);
        size_t in_include = 0, in_script = 0;
        for (const ALScriptWeight::Line& line : placed.lines)
        {
            UNSCOPED_INFO("a line of one of the two" + lined(placed));
            REQUIRE((line.file.empty() || line.file == "disk:/scripts/lib.lsl"));
            UNSCOPED_INFO("not the #include's" + lined(placed));
            REQUIRE_FALSE((line.file.empty() && line.line == 0));
            (line.file.empty() ? in_script : in_include) += line.bytes;
        }
        UNSCOPED_INFO("the include's return is its line's");
        CHECK(in_include > 0);
        UNSCOPED_INFO("the call is the script's");
        CHECK(in_script > 0);
        size_t all = 0;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            all += line.bytes;
        }
        UNSCOPED_INFO("every line's bytes somewhere");
        CHECK(in_include + in_script == all);

        const ALScriptWeight lost = weight.inSource(mapped(false));
        UNSCOPED_INFO("a part the map has no origin for is kept" + listed(lost));
        REQUIRE(named(lost, "twice") != nullptr);
        UNSCOPED_INFO("without a place");
        CHECK(named(lost, "twice")->line == -1);
        const ALScriptWeight::Part* lost_entry = named(lost, "state_entry");
        {
            INFO("the handler still placed" + listed(lost));
            REQUIRE(lost_entry);
            CHECK(lost_entry->line == 3);
        }
    }

    // A long list parses on every platform: each element nests the parse two
    // states deeper, and the parser's limit is the same everywhere.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: a long list parses on every platform: each element nests two states deeper", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        std::string script = "list gNumbers = [";
        for (int i = 0; i < 3000; ++i)
        {
            script += (i ? ", " : "") + std::to_string(i);
        }
        script += "];\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay((string)llGetListLength(gNumbers));\n    }\n}\n";
        lsl.check(script);
        CHECK(lsl.parsed());
        const ALScriptWeight weight = ALScriptWeigh::lso(script);
        UNSCOPED_INFO("and is weighed: " + weight.error);
        CHECK(weight.total > 0);
    }

    // Mono's user string heap holds a string once, however many loads of
    // it there are: a second load costs the instruction alone, on its line.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: Mono's user string heap holds a string once however often it is loaded", "[ALScriptWeight]")
    {
        UNSCOPED_INFO("builtins: " + error);
        CHECK(lslLoaded);
        const auto script = [](int says) {
            std::string text = "default\n{\n    state_entry()\n    {\n";
            for (int i = 0; i < says; ++i)
            {
                text += "        llOwnerSay(\"a sentence of some length, said more than once\");\n";
            }
            return text + "    }\n}\n";
        };
        const ALScriptWeight one   = ALScriptWeigh::mono(script(1));
        const ALScriptWeight two   = ALScriptWeigh::mono(script(2));
        const ALScriptWeight three = ALScriptWeigh::mono(script(3));
        {
            INFO("compiled: " + one.error + two.error + three.error);
            CHECK(one.compiled);
            CHECK(two.compiled);
            CHECK(three.compiled);
        }
        UNSCOPED_INFO("the third load as the second");
        CHECK(three.total - two.total == two.total - one.total);
        UNSCOPED_INFO("the second less than the string" + lined(two));
        CHECK(two.total - one.total < 20);
        UNSCOPED_INFO("the string is the first line's" + lined(two));
        CHECK(at(two, 4) > at(two, 5) + 80);
    }

    // SLua's handlers and callbacks are named as the script means them, a
    // state's with its state, each part from its function to its end; and
    // a line carries the strings and constants it is first to name.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: SLua's handlers and callbacks are named as the script means them", "[ALScriptWeight]")
    {
        const std::string script = "local greeting = \"a sentence of some length, said to the owner as the script starts up\"\n"
                                   "local function twice(n: number): number\n"
                                   "    return n * 2\n"
                                   "end\n"
                                   "local states = {}\n"
                                   "states.default = {\n"
                                   "    state_entry = function()\n"
                                   "        ll.OwnerSay(greeting)\n"
                                   "    end,\n"
                                   "}\n"
                                   "states[\"other place\"] = { touch_start = function(events) print(twice(1)) end }\n"
                                   "LLEvents:on(\"touch_start\", function(events)\n"
                                   "    print(\"touched\")\n"
                                   "end)\n"
                                   "LLTimers:every(1, function()\n"
                                   "    print(\"tick\")\n"
                                   "end)\n";
        const ALScriptWeight weight = ALScriptWeigh::slua(script);
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        const auto find = [&](const std::string& name, const std::string& within) -> const ALScriptWeight::Part* {
            for (const ALScriptWeight::Part& part : weight.parts)
            {
                if (part.name == name && part.within == within)
                {
                    return &part;
                }
            }
            return nullptr;
        };
        const ALScriptWeight::Part* twice = find("twice", "");
        {
            INFO("a local function, from its function to its end:" + listed(weight));
            REQUIRE(twice);
            CHECK(twice->line == 1);
            CHECK(twice->endLine == 3);
            CHECK(twice->endColumn == 3);
        }
        UNSCOPED_INFO("not a handler");
        CHECK(twice->kind == ALScriptWeight::Part::Kind::Function);
        const ALScriptWeight::Part* entry = find("state_entry", "default");
        {
            INFO("a state's handler, with its state:" + listed(weight));
            REQUIRE(entry);
            CHECK(entry->kind == ALScriptWeight::Part::Kind::Handler);
        }
        {
            INFO("from its function to its end");
            CHECK(entry->line == 6);
            CHECK(entry->column == 18);
            CHECK(entry->endLine == 8);
        }
        UNSCOPED_INFO("a state named by a string:" + listed(weight));
        CHECK(find("touch_start", "other place") != nullptr);
        const ALScriptWeight::Part* touched = find("touch_start", "");
        {
            INFO("LLEvents' handler by its event:" + listed(weight));
            REQUIRE(touched);
            CHECK(touched->kind == ALScriptWeight::Part::Kind::Handler);
            CHECK(touched->line == 11);
        }
        const ALScriptWeight::Part* tick = find("LLTimers:every", "");
        {
            INFO("what LLTimers calls, by how it is set going:" + listed(weight));
            REQUIRE(tick);
            CHECK(tick->kind == ALScriptWeight::Part::Kind::Function);
        }

        // The greeting, a constant the compiler puts where it is used, is
        // that line's; the strings every line names are counted on some
        // line, so that the lines come to most of the whole.
        UNSCOPED_INFO("the long string on the line that says it:" + lined(weight));
        CHECK(at(weight, 7) > 80);
        UNSCOPED_INFO("a short one on its own line:" + lined(weight));
        CHECK(at(weight, 12) > 4 + std::string("touched").size());
        const size_t lines = within(weight, 0, 100);
        UNSCOPED_INFO("most of the whole on some line: " << lines << " of " << weight.total);
        CHECK(lines * 3 > weight.total * 2);
    }

    // Each string the table keeps: what it takes up, how many instructions
    // name it and how many load it as a value, the first line naming it,
    // and a function's name as such. They come to the table, its count
    // before them.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: each string the table keeps: its bytes and uses and loads and first line", "[ALScriptWeight]")
    {
        const std::string script = "local function greet(name: string)\n"           // 0
                                   "    ll.OwnerSay(\"hello \" .. name)\n"             // 1
                                   "end\n"                                            // 2
                                   "local t = {}\n"                                   // 3
                                   "t.colour = \"red\"\n"                             // 4
                                   "if t.colour == \"blue\" then greet(\"x\") end\n"  // 5
                                   "greet(\"hello again\")\n"                         // 6
                                   "greet(\"hello again\")\n";                        // 7
        const ALScriptWeight w = ALScriptWeigh::slua(script);
        UNSCOPED_INFO("compiled: " + w.error);
        CHECK(w.compiled);
        const auto of = [&w](const std::string& text) -> const ALScriptWeight::String* {
            for (const ALScriptWeight::String& one : w.strings)
            {
                if (one.text == text)
                {
                    return &one;
                }
            }
            return nullptr;
        };
        const ALScriptWeight::String* again = of("hello again");
        {
            INFO("a value loaded twice");
            REQUIRE(again);
            CHECK(again->uses == 2);
            CHECK(again->loads == 2);
            CHECK(again->line == 6);
            CHECK(again->bytes == 12);
        }
        const ALScriptWeight::String* colour = of("colour");
        {
            INFO("a field's name: named, never loaded");
            REQUIRE(colour);
            CHECK(colour->uses >= 2);
            CHECK(colour->loads == 0);
            CHECK(colour->line == 4);
        }
        const ALScriptWeight::String* blue = of("blue");
        {
            INFO("a comparison's: named, never loaded");
            REQUIRE(blue);
            CHECK(blue->uses == 1);
            CHECK(blue->loads == 0);
        }
        const ALScriptWeight::String* say = of("OwnerSay");
        {
            INFO("an import's path");
            REQUIRE(say);
            CHECK(say->uses >= 1);
            CHECK(say->loads == 0);
            CHECK(say->line == 1);
        }
        const ALScriptWeight::String* greet = of("greet");
        {
            INFO("a function's name, at the line it starts on");
            REQUIRE(greet);
            CHECK(greet->name);
            CHECK(greet->uses == 0);
            CHECK(greet->line == 0);
        }
        size_t bytes = 0;
        for (const ALScriptWeight::String& one : w.strings)
        {
            bytes += one.bytes;
        }
        const ALScriptWeight::Part* table = named(w, "strings");
        {
            INFO("they come to the table, with its count");
            REQUIRE(table);
            CHECK(table->bytes == bytes + 1);
        }

        // LSL compiled for Luau's VM: its strings, at the lines the lined
        // compile says.
        UNSCOPED_INFO("the builtins: " + error);
        CHECK(lslLoaded);
        const ALScriptWeight lsl = ALScriptWeigh::lslLuau(byLine());
        bool found = false;
        for (const ALScriptWeight::String& one : lsl.strings)
        {
            found = found || (one.text == "a sentence of some length, said to the owner on every start" && one.line == 15);
        }
        {
            INFO("LSL on Luau's, placed");
            CHECK(lsl.compiled);
            CHECK(found);
        }
    }

    // Strings that start alike: where keeping the start once and joining it
    // back on would weigh less, what it would save -- the table's bytes
    // before less after, less the join at each load and the local once.
    // Only strings loaded as values; a short start saves nothing.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: strings that start alike: where keeping the start once would weigh less", "[ALScriptWeight]")
    {
        const std::string start = "You have touched the object of the day: ";
        const std::vector<std::string> rests = { "alpha", "bravo", "charlie", "delta" };
        std::string script = "local t = {}\n";
        for (const std::string& rest : rests)
        {
            script += "ll.OwnerSay(\"" + start + rest + "\")\n";
        }
        // Sharing a start with them, but a field's name too: never two.
        script += "t[\"" + start + "zulu\"] = 1\nt.x = t[\"" + start + "zulu\"]\n";
        // Two sharing a start too short to be worth it.
        script += "ll.OwnerSay(\"short one\")\nll.OwnerSay(\"short two\")\n";
        const ALScriptWeight w = ALScriptWeigh::slua(script);
        UNSCOPED_INFO("compiled: " + w.error);
        CHECK(w.compiled);
        UNSCOPED_INFO("one start worth keeping");
        REQUIRE(w.sharedStarts.size() == size_t(1));
        const ALScriptWeight::SharedStart& shared = w.sharedStarts.front();
        UNSCOPED_INFO("the start the four share");
        CHECK(shared.start == start);
        UNSCOPED_INFO("the four, not the field's");
        CHECK(shared.strings.size() == rests.size());
        size_t before = 0;
        size_t after  = 1 + start.size();
        for (const std::string& rest : rests)
        {
            before += 1 + start.size() + rest.size();
            after += 1 + rest.size();
        }
        const size_t cost = after + rests.size() * ALScriptWeigh::SHARED_START_PER_LOAD + ALScriptWeigh::SHARED_START_ONCE;
        UNSCOPED_INFO("what it saves");
        CHECK(shared.saved == before - cost);
        for (const size_t i : shared.strings)
        {
            REQUIRE(w.strings[i].text.rfind(start, 0) == 0);
            REQUIRE(w.strings[i].text != start + "zulu");
        }

        // The costs against the compiler's own: the four written whole, and
        // written with the start kept apart and joined back on, weigh about
        // what was said apart.
        std::string whole;
        std::string apart = "local start\nstart = \"" + start + "\"\n";
        for (const std::string& rest : rests)
        {
            whole += "ll.OwnerSay(\"" + start + rest + "\")\n";
            apart += "ll.OwnerSay(start .. \"" + rest + "\")\n";
        }
        const ALScriptWeight as_whole = ALScriptWeigh::slua(whole);
        const ALScriptWeight as_apart = ALScriptWeigh::slua(apart);
        CHECK(as_whole.compiled);
        CHECK(as_apart.compiled);
        REQUIRE(as_whole.sharedStarts.size() == 1);
        const S64 really = S64(as_whole.total) - S64(as_apart.total);
        UNSCOPED_INFO("saves about what was said: " << really << " against " << as_whole.sharedStarts[0].saved);
        CHECK(std::abs(really - S64(as_whole.sharedStarts[0].saved)) <= 8);

        // Nothing where only two share a start: two loads cost more than
        // one start saves, until the start is long.
        UNSCOPED_INFO("two with a middling start: nothing");
        CHECK(ALScriptWeigh::slua("ll.OwnerSay(\"twenty bytes of start, then one\")\nll.OwnerSay(\"twenty bytes of start, then two\")\n").sharedStarts.empty());
        UNSCOPED_INFO("two with a long one: worth it");
        CHECK(ALScriptWeigh::slua("ll.OwnerSay(\"a start long enough to be worth keeping once, then one\")\n"
                                 "ll.OwnerSay(\"a start long enough to be worth keeping once, then two\")\n")
                     .sharedStarts.size() == 1);
        // Never inside a character.
        const ALScriptWeight cut = ALScriptWeigh::slua("ll.OwnerSay(\"a start long enough to be worth keeping once \xC3\xA9t\xC3\xA9\")\n"
                                                       "ll.OwnerSay(\"a start long enough to be worth keeping once \xC3\xA8re\")\n");
        {
            INFO("cut before the character");
            REQUIRE(cut.sharedStarts.size() == 1);
            CHECK(cut.sharedStarts[0].start == "a start long enough to be worth keeping once ");
        }
    }

    // A shared start kept once in the script itself: each literal of its
    // strings the start's local and the rest joined on, the local declared
    // at the top, past the --! comments, apart from its value; in brackets
    // where `..` would bind otherwise; constant strings joined with the
    // pieces after the first bracketed with its rest, which Luau then
    // still folds into one; a require's path, a table's key, a length Luau
    // works out as it compiles and a string not of them left alone. What it
    // weighs after is what was said apart, with nothing left to keep once;
    // and it checks, strict as well.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: a shared start kept once in the script itself", "[ALScriptWeight]")
    {
        const std::string start = "You have touched the object of the day: ";
        const std::string script = "--!strict\n"                                                           // 0
                                   "local util = require(\"" + start + "x\")\n"                           // 1
                                   "ll.OwnerSay(\"" + start + "alpha\")\n"                                   // 2
                                   "ll.OwnerSay(\"" + start + "bravo\" .. \"!\")\n"                          // 3
                                   "local said = { [\"" + start + "key\"] = \"" + start + "charlie\" }\n"    // 4
                                   "print(#\"" + start + "delta\")\n"                                         // 5
                                   "print \"" + start + "delta\"\n"                                           // 6
                                   "ll.OwnerSay(\"other\")\n";                                                // 7
        const ALScriptWeight before = ALScriptWeigh::slua(script);
        UNSCOPED_INFO("weighed: " + before.error);
        CHECK(before.compiled);
        UNSCOPED_INFO("one start");
        REQUIRE(before.sharedStarts.size() == size_t(1));
        const std::vector<std::string> strings = textsOf(before, before.sharedStarts[0]);
        UNSCOPED_INFO("not the require's path, which is no string to keep once");
        CHECK(std::find(strings.begin(), strings.end(), start + "x") == strings.end());
        ALLuauSharedStart::Rewrite rewrite;
        std::string                error;
        const bool                 done = ALLuauSharedStart::rewrite(script, before.sharedStarts[0].start, strings, rewrite, error);
        UNSCOPED_INFO("rewritten: " + error);
        CHECK(done);
        CHECK(rewrite.name == std::string("sharedStart"));
        const std::string after = require_applied(script, rewrite);
        const auto has = [&after](const std::string& text) { return after.find(text) != std::string::npos; };
        UNSCOPED_INFO("the local past the --! comment: " + after);
        CHECK(after.rfind("--!strict\nlocal sharedStart\nsharedStart = \"" + start + "\"\n\n", 0) == 0);
        UNSCOPED_INFO("a call's: " + after);
        CHECK(has("ll.OwnerSay(sharedStart .. \"alpha\")"));
        UNSCOPED_INFO("constant strings joined, still folded: " + after);
        CHECK(has("ll.OwnerSay(sharedStart .. (\"bravo\" .. \"!\"))"));
        UNSCOPED_INFO("a table's value, not its key: " + after);
        CHECK(has("[\"" + start + "key\"] = sharedStart .. \"charlie\""));
        UNSCOPED_INFO("a length Luau works out, left: " + after);
        CHECK(has("print(#\"" + start + "delta\")"));
        UNSCOPED_INFO("and a call with no brackets: " + after);
        CHECK(has("print (sharedStart .. \"delta\")"));
        UNSCOPED_INFO("a require's path left: " + after);
        CHECK(has("require(\"" + start + "x\")"));
        UNSCOPED_INFO("another string left: " + after);
        CHECK(has("ll.OwnerSay(\"other\")"));

        // What it weighs now, and that it checks.
        const ALScriptWeight weighed = ALScriptWeigh::slua(after);
        UNSCOPED_INFO("weighed after: " + weighed.error);
        CHECK(weighed.compiled);
        // Nothing left to keep once: the start is the local's one value,
        // kept once already, and the require's path no literal to keep.
        UNSCOPED_INFO("nothing offered again");
        CHECK(weighed.sharedStarts.size() == size_t(0));
        CHECK(weighed.total < before.total);
        ALLuauService     service;
        llifstream        in(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau", std::ios::binary);
        std::stringstream definitions;
        definitions << in.rdbuf();
        const bool loaded = service.loadDefinitions(definitions.str(), error);
        UNSCOPED_INFO("definitions: " + error);
        REQUIRE(loaded);
        std::string said;
        for (const ALScriptProblem& p : service.check(after))
        {
            said += p.severity == ALScriptProblem::Severity::Error && p.message.find("require") == std::string::npos ? p.message + "\n" : std::string();
        }
        UNSCOPED_INFO("checks, strict:\n" + said + "---\n" + after);
        CHECK(said.empty());

        {
            INFO("nothing to make of a script without them");
            CHECK_FALSE(ALLuauSharedStart::rewrite("print(\"x\")\n", start, strings, rewrite, error));
            CHECK_FALSE(error.empty());
        }
        const bool taken = ALLuauSharedStart::rewrite("local sharedStart = 1\nprint(\"" + start + "alpha\")\n", start, { start + "alpha" }, rewrite, error);
        UNSCOPED_INFO("rewritten with a name taken: " + error);
        CHECK(taken);
        UNSCOPED_INFO("a name it has taken, another");
        CHECK(rewrite.name == std::string("sharedStart2"));
        UNSCOPED_INFO("quoted as SLua reads it");
        CHECK(ALLuauSharedStart::quoted("a\"b\\c\n\x01" "2") == std::string("\"a\\\"b\\\\c\\n\\0012\""));
    }

    // Strings loaded in closures: each function that loads one, and each
    // holding such a function, would capture the start's local, and the
    // script's body close it over as it ends, which the saving counts.
    // What it says is about what the compiler makes: a closure shared
    // before frees its constant made anew, a byte or two each.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: strings loaded in closures: the saving counts each closure's capture", "[ALScriptWeight]")
    {
        const std::string              start  = "You have touched the object of the day: ";
        const std::vector<std::string> rests  = { "alpha", "bravo", "charlie" };
        const std::vector<std::string> events = { "touch_start", "touch_end", "collision_start" };
        std::string                    whole;
        std::string                    apart  = "local start\nstart = \"" + start + "\"\n";
        for (size_t i = 0; i < rests.size(); ++i)
        {
            whole += "LLEvents:on(\"" + events[i] + "\", function()\n    ll.OwnerSay(\"" + start + rests[i] + "\")\nend)\n";
            apart += "LLEvents:on(\"" + events[i] + "\", function()\n    ll.OwnerSay(start .. \"" + rests[i] + "\")\nend)\n";
        }
        const ALScriptWeight as_whole = ALScriptWeigh::slua(whole);
        const ALScriptWeight as_apart = ALScriptWeigh::slua(apart);
        {
            INFO("both compiled: " + as_whole.error + as_apart.error);
            CHECK(as_whole.compiled);
            CHECK(as_apart.compiled);
        }
        UNSCOPED_INFO("one start");
        REQUIRE(as_whole.sharedStarts.size() == size_t(1));
        const ALScriptWeight::SharedStart& shared = as_whole.sharedStarts.front();
        UNSCOPED_INFO("the start the three share");
        CHECK(shared.start == start);
        for (const size_t i : shared.strings)
        {
            UNSCOPED_INFO("each loaded in a function of its own");
            REQUIRE(as_whole.strings[i].functions.size() == size_t(1));
        }
        size_t before = 0;
        size_t after  = 1 + start.size();
        for (const std::string& rest : rests)
        {
            before += 1 + start.size() + rest.size();
            after += 1 + rest.size();
        }
        const size_t cost = after + rests.size() * (ALScriptWeigh::SHARED_START_PER_LOAD + ALScriptWeigh::SHARED_START_PER_FUNCTION) +
                            ALScriptWeigh::SHARED_START_ONCE + ALScriptWeigh::SHARED_START_CAPTURED;
        UNSCOPED_INFO("what it saves, a capture for each closure");
        CHECK(shared.saved == before - cost);
        const S64 really = S64(as_whole.total) - S64(as_apart.total);
        UNSCOPED_INFO("saves about what was said: " << really << " against " << shared.saved);
        CHECK(std::abs(really - S64(shared.saved)) <= S64(8 + 2 * rests.size()));
    }

    // Where the start's local goes: before the first of the script that is
    // not a comment, since Luau reads a `--!` comment as the script's own
    // only before that -- past an ordinary comment before them, a blank
    // line, `--!` comments apart -- at the start of its line, or after a
    // comment on it. What would not parse is refused, as is a start not
    // every string of which is written out; and a byte of no character is
    // quoted in hex.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: where the start's local goes: before the first thing that is not a comment", "[ALScriptWeight]")
    {
        const std::string              start   = "You have touched the object of the day: ";
        const std::vector<std::string> strings = { start + "alpha", start + "bravo" };
        const std::string              said    = "ll.OwnerSay(\"" + start + "alpha\")\nll.OwnerSay(\"" + start + "bravo\")\n";
        const std::string              local   = "local sharedStart\nsharedStart = \"" + start + "\"\n\n";
        const auto kept = [&](const std::string& script) {
            ALLuauSharedStart::Rewrite rewrite;
            std::string                why;
            const bool                 done = ALLuauSharedStart::rewrite(script, start, strings, rewrite, why);
            UNSCOPED_INFO("rewritten: " + why + "\n" + script);
            REQUIRE(done);
            return alscriptweight_data::require_applied(script, rewrite);
        };
        const auto heads = [&](const std::string& head, const std::string& text) { return text.rfind(head + local, 0) == 0; };
        std::string after = kept("-- Door script\n--!strict\n" + said);
        UNSCOPED_INFO("past an ordinary comment and the --! one: " + after);
        CHECK(heads("-- Door script\n--!strict\n", after));
        UNSCOPED_INFO("and the strings kept: " + after);
        CHECK(after.find("ll.OwnerSay(sharedStart .. \"alpha\")") != std::string::npos);
        after = kept("\n--!strict\n" + said);
        UNSCOPED_INFO("past a blank first line: " + after);
        CHECK(heads("\n--!strict\n", after));
        after = kept("--!strict\n\n--!optimize 2\n" + said);
        UNSCOPED_INFO("past --! comments apart: " + after);
        CHECK(heads("--!strict\n\n--!optimize 2\n", after));
        after = kept("--!strict\n    " + said);
        {
            INFO("at the start of an indented line: " + after);
            CHECK(heads("--!strict\n", after));
            CHECK(after.find(local + "    ll.OwnerSay") != std::string::npos);
        }
        after = kept("--[[ The door ]] " + said);
        UNSCOPED_INFO("after a comment on the same line: " + after);
        CHECK(heads("--[[ The door ]] ", after));

        ALLuauSharedStart::Rewrite rewrite;
        std::string                error;
        {
            INFO("a call's string on a line of its own would be ambiguous bracketed");
            CHECK_FALSE(ALLuauSharedStart::rewrite("ll.OwnerSay\n\"" + start + "alpha\"\nll.OwnerSay(\"" + start + "bravo\")\n", start, strings, rewrite, error));
            CHECK(error.find("does not parse") != std::string::npos);
        }
        UNSCOPED_INFO("nothing made of it");
        CHECK(rewrite.edits.empty());
        {
            const bool made = ALLuauSharedStart::rewrite("ll.OwnerSay(\"" + start + "alpha\")\n", start, strings, rewrite, error);
            INFO("a string not written out: " + error);
            CHECK_FALSE(made);
            CHECK(error.find("not every one") != std::string::npos);
        }
        {
            const bool made = ALLuauSharedStart::rewrite("local greeting = \"" + start + "\"\nll.OwnerSay(greeting .. \"alpha\")\nll.OwnerSay(greeting .. \"bravo\")\n",
                                                         start, strings, rewrite, error);
            INFO("none written out, a local joined on: " + error);
            CHECK_FALSE(made);
            CHECK(error.find("none of these strings") != std::string::npos);
        }

        UNSCOPED_INFO("a byte of no character in hex, a whole one as it is");
        CHECK(ALLuauSharedStart::quoted("\xFF" "a\xC3\xA9\xC3") ==
                      std::string("\"\\xFFa\xC3\xA9\\xC3\""));
        UNSCOPED_INFO("an overlong form is no character");
        CHECK(ALLuauSharedStart::quoted("\xC0\x80") == std::string("\"\\xC0\\x80\""));
        UNSCOPED_INFO("four bytes, whole");
        CHECK(ALLuauSharedStart::quoted("\xF0\x9F\x98\x80") == std::string("\"\xF0\x9F\x98\x80\""));
    }

    // What is not offered: strings Luau makes as it compiles -- an
    // interpolated string's pattern, strings joined on a local's constant
    // value -- one the script's types name as a singleton, which `string`
    // is not, the start kept once already, and a module's strings in the
    // source's places. What is: strings a `..` that is not folded goes on
    // to, each a literal of its own, kept once in place.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: what is not offered: strings Luau makes as it compiles", "[ALScriptWeight]")
    {
        const std::string start = "You have touched the object of the day: ";
        UNSCOPED_INFO("an interpolated string's pattern");
        CHECK(ALScriptWeigh::slua("local function said(name: string)\n"
                                 "    ll.OwnerSay(`" + start + "{name}, first`)\n"
                                 "    ll.OwnerSay(`" + start + "{name}, second`)\n"
                                 "    ll.OwnerSay(`" + start + "{name}, third`)\n"
                                 "end\n"
                                 "said(\"x\")\n")
                 .sharedStarts.empty());
        UNSCOPED_INFO("strings joined on a local's constant value");
        CHECK(ALScriptWeigh::slua("local greeting = \"" + start + "\"\n"
                                 "ll.OwnerSay(greeting .. \"alpha\")\n"
                                 "ll.OwnerSay(greeting .. \"bravo\")\n"
                                 "ll.OwnerSay(greeting .. \"charlie\")\n")
                 .sharedStarts.empty());
        const auto feelings = [&start](const std::string& type) {
            return "type Mood = " + type + "\nlocal function feel(mood: Mood) end\n"
                   "feel(\"" + start + "happy\")\nfeel(\"" + start + "sad\")\nfeel(\"" + start + "bored\")\n";
        };
        UNSCOPED_INFO("given for a string");
        CHECK(ALScriptWeigh::slua(feelings("string")).sharedStarts.size() == size_t(1));
        UNSCOPED_INFO("given for a singleton");
        CHECK(ALScriptWeigh::slua(feelings("\"" + start + "happy\" | \"" + start + "sad\" | \"" + start + "bored\"")).sharedStarts.empty());

        // Kept once, with an interpolated string sharing the start beside
        // them: neither it nor the start's local is offered again.
        const std::string kept_script = "local function said(name: string)\n"
                                        "    ll.OwnerSay(`" + start + "{name}`)\n"
                                        "end\n"
                                        "ll.OwnerSay(\"" + start + "alpha\")\n"
                                        "ll.OwnerSay(\"" + start + "bravo\")\n"
                                        "ll.OwnerSay(\"" + start + "charlie\")\n"
                                        "said(\"x\")\n";
        const ALScriptWeight kept = ALScriptWeigh::slua(kept_script);
        {
            INFO("weighed: " + kept.error);
            CHECK(kept.compiled);
            REQUIRE(kept.sharedStarts.size() == 1);
        }
        UNSCOPED_INFO("the three written out, not the pattern");
        CHECK(kept.sharedStarts[0].strings.size() == size_t(3));
        ALLuauSharedStart::Rewrite rewrite;
        std::string                error;
        bool                       done = ALLuauSharedStart::rewrite(kept_script, start, textsOf(kept, kept.sharedStarts[0]), rewrite, error);
        UNSCOPED_INFO("rewritten: " + error);
        CHECK(done);
        const ALScriptWeight again = ALScriptWeigh::slua(require_applied(kept_script, rewrite));
        UNSCOPED_INFO("weighed again: " + again.error);
        CHECK(again.compiled);
        UNSCOPED_INFO("nothing offered again");
        CHECK(again.sharedStarts.size() == size_t(0));

        // Joined on to a name, each piece is loaded on its own.
        const std::string joined = "local function touched(name: string)\n"
                                   "    ll.OwnerSay(name .. \"" + start + "alpha\" .. \"!\")\n"
                                   "    ll.OwnerSay(name .. \"" + start + "bravo\" .. \"!\")\n"
                                   "    ll.OwnerSay(name .. \"" + start + "charlie\" .. \"!\")\n"
                                   "end\n"
                                   "touched(\"x\")\n";
        const ALScriptWeight pieces = ALScriptWeigh::slua(joined);
        {
            INFO("weighed: " + pieces.error);
            CHECK(pieces.compiled);
            REQUIRE(pieces.sharedStarts.size() == 1);
            CHECK(pieces.sharedStarts[0].start == start);
        }
        done = ALLuauSharedStart::rewrite(joined, start, textsOf(pieces, pieces.sharedStarts[0]), rewrite, error);
        UNSCOPED_INFO("rewritten: " + error);
        CHECK(done);
        const std::string pieces_after = require_applied(joined, rewrite);
        UNSCOPED_INFO("each piece kept in place: " + pieces_after);
        CHECK(pieces_after.find("ll.OwnerSay(name .. sharedStart .. \"alpha\" .. \"!\")") != std::string::npos);
        const ALScriptWeight lighter = ALScriptWeigh::slua(pieces_after);
        CHECK(lighter.compiled);
        CHECK(lighter.total < pieces.total);

        // A module's strings, in the source's places, are not the script's
        // to keep once: the first three lines are a module's.
        const std::vector<std::string> lines = { "local m = {}",
                                                 "function m.a() ll.OwnerSay(\"" + start + "alpha\") end",
                                                 "function m.b() ll.OwnerSay(\"" + start + "bravo\") end",
                                                 "ll.OwnerSay(\"" + start + "charlie\")",
                                                 "m.a() m.b()" };
        std::string bundle;
        for (const std::string& line : lines)
        {
            bundle += line + "\n";
        }
        const auto mapped = [&lines](S32 module_lines) {
            ALSourceMap map;
            map.addFile("script", "script");
            map.addFile("mod", "inventory:/mod");
            for (S32 out = 0; out < S32(lines.size()); ++out)
            {
                ALSourceMap::Segment segment;
                segment.outLine = out;
                segment.length  = S32(lines[out].size());
                segment.file    = out < module_lines ? 1 : 0;
                segment.line    = out < module_lines ? out : out - module_lines;
                map.add(segment);
            }
            map.finish();
            return map;
        };
        const ALScriptWeight bundled = ALScriptWeigh::slua(bundle);
        {
            INFO("weighed: " + bundled.error);
            CHECK(bundled.compiled);
            CHECK(bundled.sharedStarts.size() == 1);
        }
        UNSCOPED_INFO("all the script's: kept");
        CHECK(bundled.inSource(mapped(0)).sharedStarts.size() == size_t(1));
        UNSCOPED_INFO("two a module's: nothing");
        CHECK(bundled.inSource(mapped(3)).sharedStarts.size() == size_t(0));
    }

    // LSL's constants are folded as the grid's compiler folds them: a
    // constant read is its value in the bytecode, as if written out, and
    // not a global looked up by its name.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: LSL's constants are folded as the grid's compiler folds them", "[ALScriptWeight]")
    {
        const ALScriptWeight named   = ALScriptWeigh::slua("local x = PRIM_SIZE\nprint(x)\n");
        const ALScriptWeight written = ALScriptWeigh::slua("local x = 7\nprint(x)\n");
        CHECK(named.compiled);
        CHECK(written.compiled);
        UNSCOPED_INFO("the same as its value written out");
        CHECK(named.total == written.total);
        bool looked_up = false;
        for (const ALScriptWeight::String& one : named.strings)
        {
            looked_up = looked_up || one.text == "PRIM_SIZE";
        }
        UNSCOPED_INFO("not its name among the strings");
        CHECK_FALSE(looked_up);
    }

    // A heavy string is named on one line as a string of the table is, a
    // return in it written out as a break and a tab are.
    TEST_CASE_METHOD(alscriptweight_data, "ALScriptWeight: a heavy string is named on one line as a string of the table is", "[ALScriptWeight]")
    {
        const ALScriptWeight weight = ALScriptWeigh::slua("local s = \"a\\rb\\tc" + std::string(300, 'x') + "\"\nprint(s)\n");
        UNSCOPED_INFO("compiled: " + weight.error);
        CHECK(weight.compiled);
        const ALScriptWeight::Part* found = nullptr;
        for (const ALScriptWeight::Part& one : weight.parts)
        {
            found = one.kind == ALScriptWeight::Part::Kind::Constant && one.name != "strings" ? &one : found;
        }
        UNSCOPED_INFO("a part of its own:" + listed(weight));
        REQUIRE(found != nullptr);
        {
            INFO("named on one line: " + found->name);
            CHECK(found->name.rfind("\"a\\rb\\tc", 0) == 0);
            CHECK(found->name.find('\r') == std::string::npos);
        }
    }
}
