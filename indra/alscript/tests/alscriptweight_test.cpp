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
#include "../alsourcemap.h"

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
        ensure("the script itself, and its strings", named(weight, "script") && named(weight, "strings"));
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
        // Heavy, it is a part of its own, and no longer the table of
        // strings'.
        const ALScriptWeight::Part* constant = nullptr;
        for (const ALScriptWeight::Part& one : heavy.parts)
        {
            constant = one.kind == ALScriptWeight::Part::Kind::Constant && one.name != "strings" ? &one : constant;
        }
        // At the line that loads it: a local never set again is folded into
        // where it is used, print(s), as -O1 compiles it.
        ensure("a part of its own:" + listed(heavy), constant && constant->bytes >= 2000 && constant->line == 1);
        ensure("named by its start: " + (constant ? constant->name : std::string()), constant && constant->name.rfind("\"xxxx", 0) == 0);
        ensure("not the strings' too", named(heavy, "strings") && named(heavy, "strings")->bytes < 100);
        ensure("a light one stays the strings'", std::none_of(light.parts.begin(), light.parts.end(), [](const ALScriptWeight::Part& one) {
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
            ensure(target + ": a part of its own:" + listed(one), one.compiled && found && found->bytes >= 2000 && found->line == 0);
            ensure(target + ": counted once", sum <= one.total);
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
        ensure_equals("each value a part of its own, at the table:" + listed(table), values, size_t(2));
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
            ensure("a wide one named: " + listed(wide), found && whole(found->name) && found->name.find("\xE2\x80\xA6") != std::string::npos);
        }

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
        // Where its handlers begin and a jump table entry for each; each
        // handler, from where it begins to where it ends, its own part.
        ensure_equals("a state is its table, its handlers apart:" + listed(weight), state->bytes, size_t(5 + 8 * 2));
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

    // A long list parses on every platform: each element nests the parse two
    // states deeper, and the parser's limit is the same everywhere.
    template<> template<>
    void alscriptweight_object::test<10>()
    {
        ensure("builtins: " + error, lslLoaded);
        std::string script = "list gNumbers = [";
        for (int i = 0; i < 3000; ++i)
        {
            script += (i ? ", " : "") + std::to_string(i);
        }
        script += "];\ndefault\n{\n    state_entry()\n    {\n        llOwnerSay((string)llGetListLength(gNumbers));\n    }\n}\n";
        lsl.check(script);
        ensure("it parses", lsl.parsed());
        const ALScriptWeight weight = ALScriptWeigh::lso(script);
        ensure("and is weighed: " + weight.error, weight.total > 0);
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

    // LSL on Luau: the fork's own compiler, the asset as the server makes
    // it, the bytecode read back as SLua's is; and again with its lines,
    // for what each comes to.
    template<> template<>
    void alscriptweight_object::test<6>()
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
                                   "        llOwnerSay((string)gCount + \" \" + (string)n);\n"
                                   "    }\n"
                                   "}\n";
        const ALScriptWeight weight = ALScriptWeigh::lslLuau(script);
        ensure("compiled: " + weight.error + listed(weight), weight.compiled);
        ensure_equals("against Luau's limit", weight.limit, size_t(131072));
        ensure("not an estimate", !weight.estimate);
        ensure("something, and not much: " + std::to_string(weight.total) + listed(weight), weight.total > 50 && weight.total < 4096);
        const ALScriptWeight::Part* twice = named(weight, "twice");
        ensure("a function a part:" + listed(weight), twice != nullptr);
        ensure("where it is: " + std::to_string(twice->line), twice->line == 1);
        const ALScriptWeight::Part* touch = named(weight, "touch_start");
        ensure("a handler in its state:" + listed(weight), touch && touch->within == "default" && touch->kind == ALScriptWeight::Part::Kind::Handler);
        ensure_equals("where it is", touch->line, 11);
        // Its lines, read off the compiler's lined bytecode, which the
        // server's is not: the total stays what the server charges.
        ensure("each line weighed:" + lined(weight), at(weight, 9) > 0 && at(weight, 13) > 0 && at(weight, 3) > 0);
        ensure("the handler's long line its own:" + lined(weight), at(weight, 13) > at(weight, 9));
        ensure("nothing past the script's lines:" + lined(weight), within(weight, 16, 1000) == 0);
        const ALScriptWeight broken = ALScriptWeigh::lslLuau("default { state_entry() { integer x = ; } }\n");
        ensure("a script that does not compile", !broken.compiled && !broken.error.empty());
    }

    // LSO by line: what the compiler wrote while it stood at each
    // statement and expression, a function's missing return at its closing
    // brace, and a global's value on its line. A function's lines are its
    // code, which is the function less its header.
    template<> template<>
    void alscriptweight_object::test<7>()
    {
        ensure("builtins: " + error, lslLoaded);
        const ALScriptWeight weight = ALScriptWeigh::lso(byLine());
        ensure("compiled: " + weight.error, weight.compiled);
        ensure("by line:" + lined(weight), !weight.lines.empty());
        ensure("the loop's body" + lined(weight), at(weight, 13) > 0);
        ensure("the loop itself: its test, its step and its jump back" + lined(weight), at(weight, 11) > 0);
        ensure("a long string weighs more than a short one" + lined(weight), at(weight, 15) > at(weight, 16) + 40);
        ensure("the return it does not write, at its closing brace" + lined(weight), at(weight, 17) > 0);
        ensure("a function's return" + lined(weight), at(weight, 4) > 0);
        ensure("a global string's value" + lined(weight), at(weight, 1) >= 34);
        ensure("a brace that makes nothing" + lined(weight), at(weight, 7) == 0 && at(weight, 3) == 0);
        const ALScriptWeight::Part* entry = named(weight, "state_entry");
        const ALScriptWeight::Part* twice = named(weight, "twice");
        ensure("both parts:" + listed(weight), entry && twice);
        ensure_equals("a handler's lines are all of it but its header, where its code begins", entry->bytes - within(weight, entry->line, entry->endLine),
                      size_t(5));
        ensure_equals("a function's lines are all of it but its header: where its code begins, its type and its parameter's",
                      twice->bytes - within(weight, twice->line, twice->endLine), size_t(9));
    }

    // Mono by line, the same way: each instruction, with its string, is
    // the line it was written for; a global's initialiser is its line's.
    template<> template<>
    void alscriptweight_object::test<8>()
    {
        ensure("builtins: " + error, lslLoaded);
        const ALScriptWeight weight = ALScriptWeigh::mono(byLine());
        ensure("compiled: " + weight.error, weight.compiled);
        ensure("by line:" + lined(weight), !weight.lines.empty());
        ensure("the loop's body" + lined(weight), at(weight, 13) > 0);
        ensure("the loop itself" + lined(weight), at(weight, 11) > 0);
        ensure("a long string weighs more than a short one" + lined(weight), at(weight, 15) > at(weight, 16) + 40);
        ensure("the return it does not write, at its closing brace" + lined(weight), at(weight, 17) > 0);
        ensure("a global string's initialiser" + lined(weight), at(weight, 1) >= 34);
        ensure("a brace that makes nothing" + lined(weight), at(weight, 7) == 0 && at(weight, 3) == 0);
        for (const ALScriptWeight::Part& part : weight.parts)
        {
            if (part.line >= 0)
            {
                ensure(part.name + "'s lines are no more than it" + lined(weight), within(weight, part.line, part.endLine) <= part.bytes);
            }
        }
        size_t all = 0;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            all += line.bytes;
        }
        ensure("the lines are less than the whole", all > 0 && all < weight.total);
    }

    // In the source's places: a part in an include is in the include, at
    // its line there, and a line's bytes are the source line's; what the
    // map has no origin for keeps its part without a place.
    template<> template<>
    void alscriptweight_object::test<9>()
    {
        ensure("builtins: " + error, lslLoaded);
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
        ensure("compiled: " + weight.error, weight.compiled);
        const ALScriptWeight placed = weight.inSource(mapped(true));
        ensure_equals("the same weight", placed.total, weight.total);
        const ALScriptWeight::Part* twice = named(placed, "twice");
        const ALScriptWeight::Part* entry = named(placed, "state_entry");
        ensure("both:" + listed(placed), twice && entry);
        ensure_equals("a function in the include", twice->file, std::string("disk:/scripts/lib.lsl"));
        ensure_equals("at its line there", twice->line, 0);
        ensure_equals("to its end there", twice->endLine, 3);
        ensure("a handler in the script", entry->file.empty());
        ensure_equals("at the script's line", entry->line, 3);
        size_t in_include = 0, in_script = 0;
        for (const ALScriptWeight::Line& line : placed.lines)
        {
            ensure("a line of one of the two" + lined(placed), line.file.empty() || line.file == "disk:/scripts/lib.lsl");
            ensure("not the #include's" + lined(placed), !(line.file.empty() && line.line == 0));
            (line.file.empty() ? in_script : in_include) += line.bytes;
        }
        ensure("the include's return is its line's", in_include > 0);
        ensure("the call is the script's", in_script > 0);
        size_t all = 0;
        for (const ALScriptWeight::Line& line : weight.lines)
        {
            all += line.bytes;
        }
        ensure_equals("every line's bytes somewhere", in_include + in_script, all);

        const ALScriptWeight lost = weight.inSource(mapped(false));
        ensure("a part the map has no origin for is kept" + listed(lost), named(lost, "twice") != nullptr);
        ensure_equals("without a place", named(lost, "twice")->line, -1);
        ensure_equals("the handler still placed", named(lost, "state_entry")->line, 3);
    }

    // Mono's user string heap holds a string once, however many loads of
    // it there are: a second load costs the instruction alone, on its line.
    template<> template<>
    void alscriptweight_object::test<11>()
    {
        ensure("builtins: " + error, lslLoaded);
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
        ensure("compiled: " + one.error + two.error + three.error, one.compiled && two.compiled && three.compiled);
        ensure_equals("the third load as the second", three.total - two.total, two.total - one.total);
        ensure("the second less than the string" + lined(two), two.total - one.total < 20);
        ensure("the string is the first line's" + lined(two), at(two, 4) > at(two, 5) + 80);
    }

    // SLua's handlers and callbacks are named as the script means them, a
    // state's with its state, each part from its function to its end; and
    // a line carries the strings and constants it is first to name.
    template<> template<>
    void alscriptweight_object::test<12>()
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
        ensure("compiled: " + weight.error, weight.compiled);
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
        ensure("a local function, from its function to its end:" + listed(weight), twice && twice->line == 1 && twice->endLine == 3 && twice->endColumn == 3);
        ensure("not a handler", twice->kind == ALScriptWeight::Part::Kind::Function);
        const ALScriptWeight::Part* entry = find("state_entry", "default");
        ensure("a state's handler, with its state:" + listed(weight), entry && entry->kind == ALScriptWeight::Part::Kind::Handler);
        ensure("from its function to its end", entry->line == 6 && entry->column == 18 && entry->endLine == 8);
        ensure("a state named by a string:" + listed(weight), find("touch_start", "other place") != nullptr);
        const ALScriptWeight::Part* touched = find("touch_start", "");
        ensure("LLEvents' handler by its event:" + listed(weight), touched && touched->kind == ALScriptWeight::Part::Kind::Handler && touched->line == 11);
        const ALScriptWeight::Part* tick = find("LLTimers:every", "");
        ensure("what LLTimers calls, by how it is set going:" + listed(weight), tick && tick->kind == ALScriptWeight::Part::Kind::Function);

        // The greeting, a constant the compiler puts where it is used, is
        // that line's; the strings every line names are counted on some
        // line, so that the lines come to most of the whole.
        ensure("the long string on the line that says it:" + lined(weight), at(weight, 7) > 80);
        ensure("a short one on its own line:" + lined(weight), at(weight, 12) > 4 + std::string("touched").size());
        const size_t lines = within(weight, 0, 100);
        ensure("most of the whole on some line: " + std::to_string(lines) + " of " + std::to_string(weight.total), lines * 3 > weight.total * 2);
    }
}
