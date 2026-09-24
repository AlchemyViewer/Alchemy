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

#include "../alscriptweight.h"

#include "../allslservice.h"

#include "../test/lltut.h"

#include <numeric>

namespace tut
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

        static std::string listed(const ALScriptWeight& weight)
        {
            std::string out;
            for (const ALScriptWeight::Part& part : weight.parts)
            {
                out += " " + part.within + (part.within.empty() ? "" : ".") + part.name + "=" + std::to_string(part.bytes);
            }
            return out;
        }
    };
    typedef test_group<alscriptweight_data> alscriptweight_group;
    typedef alscriptweight_group::object    alscriptweight_object;
    alscriptweight_group                    alscriptweight_instance("ALScriptWeight");

    // SLua weighs what the server charges, the bytecode; each function is
    // its prototype, where its lines are, and each line the instructions
    // that came of it.
    template<> template<>
    void alscriptweight_object::test<1>()
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
        ensure("compiled: " + weight.error, weight.compiled);
        ensure_equals("against SLua's limit", weight.limit, size_t(131072));
        ensure("not an estimate", !weight.estimate);
        ensure("something, and not much", weight.total > 50 && weight.total < 2000);
        const ALScriptWeight::Part* greet = named(weight, "greet");
        const ALScriptWeight::Part* count = named(weight, "count");
        ensure("each function a part:" + listed(weight), greet && count);
        ensure_equals("where it is declared", greet->line, 0);
        ensure_equals("and where it ends", count->line, 4);
        ensure("to its last line", count->endLine >= 8);
        const ALScriptWeight::Part* nothing = named(weight, "nothing");
        ensure("a function that does nothing weighs least:" + listed(weight),
               nothing && nothing->bytes < greet->bytes && nothing->bytes < count->bytes);
        ensure("the script itself, and its strings", named(weight, "(the script)") && named(weight, "strings"));
        const size_t parts = std::accumulate(weight.parts.begin(), weight.parts.end(), size_t(0),
                                             [](size_t sum, const ALScriptWeight::Part& p) { return sum + p.bytes; });
        ensure("the parts are nearly the whole: " + std::to_string(parts) + " of " + std::to_string(weight.total),
               parts <= weight.total && weight.total - parts < 16);
        bool body = false;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            body = body || (line.line == 7 && line.bytes > 0);
        }
        ensure("the loop's body has bytes of its own", body);
    }

    // A long string is weight as much as code is; and a script that does
    // not compile says so.
    template<> template<>
    void alscriptweight_object::test<2>()
    {
        const std::string    words(2000, 'x');
        const ALScriptWeight heavy = ALScriptWeigh::slua("local s = \"" + words + "\"\nprint(s)\n");
        const ALScriptWeight light = ALScriptWeigh::slua("local s = \"x\"\nprint(s)\n");
        ensure("both compiled", heavy.compiled && light.compiled);
        ensure("the string weighs", heavy.total >= light.total + 2000);
        ensure("in the strings", named(heavy, "strings") && named(heavy, "strings")->bytes >= 2000);
        const ALScriptWeight broken = ALScriptWeigh::slua("local function (\n");
        ensure("a script that does not compile", !broken.compiled && !broken.error.empty());
    }

    // LSO weighs its image to the top of its heap: the registers, the
    // globals, the functions, the states with their handlers, and the heap;
    // each function, state, handler and global a part, where it is.
    template<> template<>
    void alscriptweight_object::test<3>()
    {
        ensure("builtins: " + error, lslLoaded);
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
        ensure("compiled: " + weight.error, weight.compiled);
        ensure_equals("against LSO's limit", weight.limit, size_t(16384));
        ensure("well under it", weight.total > 0 && weight.total < 4096);
        const ALScriptWeight::Part* twice = named(weight, "twice");
        const ALScriptWeight::Part* state = named(weight, "default");
        const ALScriptWeight::Part* touch = named(weight, "touch_start");
        ensure("each a part:" + listed(weight), twice && state && touch && named(weight, "state_entry") && named(weight, "registers"));
        ensure_equals("a function where it is", twice->line, 2);
        ensure_equals("a handler in its state", touch->within, std::string("default"));
        ensure("the state holds its handlers", state->bytes > touch->bytes);
        const ALScriptWeight::Part* count = named(weight, "gCount");
        const ALScriptWeight::Part* name  = named(weight, "gName");
        ensure("the globals too", count && name);
        ensure("a string global weighs its string", name->bytes > count->bytes);
    }

    // What does not fit in 16 KB says so.
    template<> template<>
    void alscriptweight_object::test<4>()
    {
        ensure("builtins: " + error, lslLoaded);
        std::string script = "list gWords = [";
        for (int i = 0; i < 600; ++i)
        {
            script += std::string(i ? ", " : "") + "\"" + std::string(40, 'a' + i % 26) + "\"";
        }
        script += "];\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay(llList2String(gWords, 0));\n    }\n}\n";
        const ALScriptWeight weight = ALScriptWeigh::lso(script);
        ensure("does not fit: " + weight.error + " " + std::to_string(weight.total), !weight.compiled && !weight.error.empty());
        ensure("and weighs more than there is", weight.total > weight.limit);
        const ALScriptWeight broken = ALScriptWeigh::lso("default { state_entry() { integer x = ; } }\n");
        ensure("a script that does not parse", !broken.compiled && !broken.error.empty());
    }

    // Mono is an estimate: the IL sized by the opcode table with what it
    // declares, over what an assembly costs; each function and handler a
    // part where it is, and the parts are the whole.
    template<> template<>
    void alscriptweight_object::test<5>()
    {
        ensure("builtins: " + error, lslLoaded);
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
        ensure("compiled: " + weight.error, weight.compiled);
        ensure("an estimate", weight.estimate);
        ensure_equals("against Mono's limit", weight.limit, size_t(65536));
        ensure("over what an assembly costs, and under the limit", weight.total > ALScriptWeigh::MONO_BASE_BYTES && weight.total < 8192);
        const ALScriptWeight::Part* twice = named(weight, "twice");
        const ALScriptWeight::Part* touch = named(weight, "touch_start");
        const ALScriptWeight::Part* entry = named(weight, "state_entry");
        ensure("each a part:" + listed(weight), twice && touch && entry && named(weight, "globals") && named(weight, "assembly"));
        ensure_equals("a function where it is", twice->line, 1);
        ensure_equals("a handler in its state", touch->within, std::string("default"));
        ensure_equals("where it is", touch->line, 11);
        ensure("a long string weighs:" + listed(weight), touch->bytes > entry->bytes);
        const size_t parts = std::accumulate(weight.parts.begin(), weight.parts.end(), size_t(0),
                                             [](size_t sum, const ALScriptWeight::Part& p) { return sum + p.bytes; });
        ensure_equals("the parts are the whole", parts, weight.total);
    }
}
