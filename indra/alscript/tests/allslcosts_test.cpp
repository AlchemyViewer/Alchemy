/**
 * @file tests/allslcosts_test.cpp
 * @brief What LSL forms cost on each target: every answer ALLSLCosts gives, measured.
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

// Each answer ALLSLCosts gives, compiled for each target with the grid's
// builtins loaded: a pair of snippets in a handler with a local of every
// type to work on, weighed once and twice over, so that what a second use
// adds is told from what the first did. A failure here is a compiler that
// changed, and the answer to change with it -- or a form that is no longer
// what the optimizer takes it to be.
//
// Set AL_COSTS_PRINT to have every pair's numbers printed.

#include "linden_common.h"

#include "../allslcosts.h"

#include "../allslservice.h"
#include "../alscriptweight.h"

#include "../test/lltut.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace tut
{
    struct allslcosts_data
    {
        using Target = ALLSLOptimizer::Target;

        ALLSLService lsl;
        std::string  error;
        bool         lslLoaded = false;
        const bool   print     = getenv("AL_COSTS_PRINT") != nullptr;

        allslcosts_data() { lslLoaded = lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error); }

        static constexpr Target TARGETS[] = { Target::LSO, Target::Mono, Target::Luau };

        static const char* nameOf(Target t) { return t == Target::LSO ? "LSO" : t == Target::Mono ? "Mono" : "Luau"; }

        // Globals the snippet needs, and its statements.
        struct Snippet
        {
            std::string globals;
            std::string body;
        };

        // The body `copies` times, each in braces of its own, so that its
        // locals can be declared again.
        static std::string script(const Snippet& s, int copies)
        {
            std::string body;
            for (int i = 0; i < copies; ++i)
            {
                body += "{ " + s.body + " }\n";
            }
            return "integer gi = 3; float gf = 2.5; string gs = \"a\"; key gk; vector gv = <1, 2, 3>; list gl = [1];\n" + s.globals +
                   "\ndefault { state_entry() {\n"
                   "integer i = gi; integer j = gi; float f = gf; string s = gs; key k = gk; list l = gl; vector v = gv;\n" +
                   body + "llOwnerSay((string)[i, j, f, s, k, v] + (string)l);\n} }\n";
        }

        S32 size(Target t, const Snippet& s, int copies)
        {
            const std::string    text   = script(s, copies);
            const ALScriptWeight weight = t == Target::LSO    ? ALScriptWeigh::lso(text)
                                          : t == Target::Mono ? ALScriptWeigh::mono(text)
                                                              : ALScriptWeigh::lslLuau(text);
            ensure("compiles for " + std::string(nameOf(t)) + ": " + weight.error + "\n" + text, weight.compiled);
            return S32(weight.total);
        }

        // What `b` costs against `a`: where each is once, and what a second
        // of it adds against a second of the other. Where one of them is
        // only globals, the first alone.
        struct Delta
        {
            S32 first = 0;
            S32 each  = 0;
        };
        Delta delta(Target t, const Snippet& a, const Snippet& b)
        {
            const S32 a1 = size(t, a, 1);
            const S32 b1 = size(t, b, 1);
            Delta     d;
            d.first = b1 - a1;
            d.each  = !a.body.empty() && !b.body.empty() ? (size(t, b, 2) - b1) - (size(t, a, 2) - a1) : d.first;
            return d;
        }

        // Smaller where a use is smaller, or no larger and the first is.
        static char verdict(const Delta& d)
        {
            if (d.each < 0 || (d.each == 0 && d.first < 0))
            {
                return '<';
            }
            if (d.each > 0 || (d.each == 0 && d.first > 0))
            {
                return '>';
            }
            return '=';
        }

        // A float as the optimizer writes it for a target: a whole value as
        // an integer where that is smaller.
        static std::string number(Target t, double v)
        {
            char buffer[32];
            if (ALLSLCosts::of(t).integerForFloat && v == double(S32(v)))
            {
                snprintf(buffer, sizeof(buffer), "%d", S32(v));
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%g", v);
                if (!strchr(buffer, '.'))
                {
                    strcat(buffer, ".0");
                }
            }
            return buffer;
        }

        void said(const std::string& what, Target t, const Delta& d, S32 expected) const
        {
            if (print)
            {
                fprintf(stderr, "%-44s %-5s first %+5d each %+5d  (taken as %+d)\n", what.c_str(), nameOf(t), d.first, d.each, expected);
            }
        }
    };
    typedef test_group<allslcosts_data> allslcosts_group;
    typedef allslcosts_group::object    allslcosts_object;
    allslcosts_group                    allslcosts_instance("ALLSLCosts");

    // The rewrites: each pair's verdict on LSO, Mono and Luau in turn --
    // '<' where the second form is smaller -- and the flag that stands for
    // it where the optimizer has one. The rest are what rewrites still to
    // come will read: here so that what was measured stays measured.
    template<> template<>
    void allslcosts_object::test<1>()
    {
        ensure("builtins: " + error, lslLoaded);
        using Flag = bool ALLSLCosts::*;
        struct Fact
        {
            const char* what;
            Snippet     a;
            Snippet     b;
            const char* verdicts;
            Flag        flag;
            // Told by one use alone: where the first carries what a script
            // pays once -- Luau's import of its bit library for ~ -- more
            // than a few uses after it save.
            bool        once = false;
        };
        const Fact facts[] = {
            // optfloats
            { "2.0 as 2", { "", "f = 2.0;" }, { "", "f = 2;" }, "><=", &ALLSLCosts::integerForFloat },
            { "200.0 as 200", { "", "f = 200.0;" }, { "", "f = 200;" }, "><=", &ALLSLCosts::integerForFloat },
            { "an argument 1.0 as 1", { "", "llSetText(s, v, 1.0);" }, { "", "llSetText(s, v, 1);" }, "><=", &ALLSLCosts::integerForFloat },
            { "a vector's parts", { "", "v = <1.0, 2.0, 3.0>;" }, { "", "v = <1, 2, 3>;" }, "><=", &ALLSLCosts::integerForFloat },
            { "some of a vector's parts", { "", "v = <1.5, 2.0, 3.0>;" }, { "", "v = <1.5, 2, 3>;" }, "><=", &ALLSLCosts::integerForFloat },
            // listlength
            { "llGetListLength(l) as l != []", { "", "i = llGetListLength(l);" }, { "", "i = l != [];" }, "<>>", &ALLSLCosts::lengthAsNotEqual },
            // listadd
            { "[a] as (list)a", { "", "l = [i];" }, { "", "l = (list)i;" }, "<<>", &ALLSLCosts::listAsSum },
            { "[a, b] as (list)a + b", { "", "l = [i, j];" }, { "", "l = (list)i + j;" }, "<<>", &ALLSLCosts::listAsSum },
            { "[a, b, c] as (list)a + b + c", { "", "l = [i, j, f];" }, { "", "l = (list)i + j + f;" }, "<<>", &ALLSLCosts::listAsSum },
            // The -1 idioms, where only truth counts, and of what is never below -1.
            { "x != -1 as ~x", { "", "if (j != -1) i = 2;" }, { "", "if (~j) i = 2;" }, "<<>", &ALLSLCosts::complementForNotMinusOne, true },
            { "a find != -1 as ~find", { "", "if (llListFindList(l, [i]) != -1) i = 2;" }, { "", "if (~llListFindList(l, [i])) i = 2;" }, "<<>",
              &ALLSLCosts::complementForNotMinusOne, true },
            { "a find >= 0 as ~find", { "", "if (llSubStringIndex(s, \"a\") >= 0) i = 2;" }, { "", "if (~llSubStringIndex(s, \"a\")) i = 2;" }, "<<>",
              &ALLSLCosts::complementForNotMinusOne, true },
            { "a find > -1 as ~find", { "", "if (llSubStringIndex(s, \"a\") > -1) i = 2;" }, { "", "if (~llSubStringIndex(s, \"a\")) i = 2;" }, "<<>",
              &ALLSLCosts::complementForNotMinusOne, true },
            { "x == -1 as !~x", { "", "i = j == -1;" }, { "", "i = !~j;" }, "<=>", &ALLSLCosts::notComplementForMinusOne },
            { "x == -1 as !~x, as a condition", { "", "if (j == -1) i = 2;" }, { "", "if (!~j) i = 2;" }, "<=>", &ALLSLCosts::notComplementForMinusOne },
            // Luau's answer turns on the operand -- a call here, a local above
            // -- and the flag keeps to the local's.
            { "a find < 0 as !~find", { "", "if (llSubStringIndex(s, \"a\") < 0) i = 2;" }, { "", "if (!~llSubStringIndex(s, \"a\")) i = 2;" }, "<><",
              nullptr },
            // One and two either way.
            { "x + 1 as -~x", { "", "i = j + 1;" }, { "", "i = -~j;" }, "<=>", &ALLSLCosts::negateComplementForIncrement },
            { "x + 2 as -~-~x", { "", "i = j + 2;" }, { "", "i = -~-~j;" }, "<>>", &ALLSLCosts::negateComplementForIncrement },
            { "x - 1 as ~-x", { "", "i = j - 1;" }, { "", "i = ~-j;" }, "<<>", &ALLSLCosts::complementNegateForDecrement },
            { "x - 2 as ~-~-x", { "", "i = j - 2;" }, { "", "i = ~-~-j;" }, "<<>", &ALLSLCosts::complementNegateForDecrement },
            // An increment whose value goes unused.
            { "x++ as ++x", { "", "i++;" }, { "", "++i;" }, "<<=", &ALLSLCosts::preForPost },
            { "x-- as --x", { "", "i--;" }, { "", "--i;" }, "<<=", &ALLSLCosts::preForPost },
            { "a for's x++ as ++x", { "", "for (i = 0; i < 3; i++) j = i;" }, { "", "for (i = 0; i < 3; ++i) j = i;" }, "<<=", &ALLSLCosts::preForPost },
            // A list's element bare.
            { "l + [a] as l + a", { "", "l = l + [i];" }, { "", "l = l + i;" }, "<<<", &ALLSLCosts::elementForList },
            { "l + [a, b] as l + a + b", { "", "l = l + [i, j];" }, { "", "l = l + i + j;" }, "<<<", &ALLSLCosts::elementForList },
            { "[a] + l as a + l", { "", "l = [s] + l;" }, { "", "l = s + l;" }, "<<<", &ALLSLCosts::elementForList },
            { "l + (list)x as l + x", { "", "l = l + (list)s;" }, { "", "l = l + s;" }, "<<<", &ALLSLCosts::elementForList },
            { "l += [a] as l += a", { "", "l += [i];" }, { "", "l += i;" }, "<<<", &ALLSLCosts::elementForList },
            // To come.
            // Comparisons.
            { "!(a <= b) as a > b", { "", "if (!(j <= i)) i = 2;" }, { "", "if (j > i) i = 2;" }, "<<<", &ALLSLCosts::comparisonForNot },
            { "!(a >= b) as a < b", { "", "if (!(j >= i)) i = 2;" }, { "", "if (j < i) i = 2;" }, "<<<", &ALLSLCosts::comparisonForNot },
            { "!(a != b) as a == b", { "", "if (!(j != i)) i = 2;" }, { "", "if (j == i) i = 2;" }, "<<<", &ALLSLCosts::comparisonForNot },
            // Never larger, where the flag is not for these alone.
            { "!(a > b) as a <= b", { "", "if (!(j > i)) i = 2;" }, { "", "if (j <= i) i = 2;" }, "<=<", nullptr },
            { "!(a == b) as a != b", { "", "if (!(j == i)) i = 2;" }, { "", "if (j != i) i = 2;" }, "<=<", nullptr },
            { "!(a < b) as a >= b, as a value", { "", "i = !(j < i);" }, { "", "i = j >= i;" }, "<==", nullptr },
            { "a != b as a ^ b, a loop's", { "", "while (j != i) i++;" }, { "", "while (j ^ i) i++;" }, "<<>", &ALLSLCosts::xorForNotEqual },
            { "if (a == b) A else B as if (a ^ b) B else A", { "", "if (j == i) i = 2; else i = 3;" }, { "", "if (j ^ i) i = 3; else i = 2;" }, "<<>",
              &ALLSLCosts::xorForNotEqual },
            { "a >= 5 as a > 4", { "", "if (j >= 5) i = 2;" }, { "", "if (j > 4) i = 2;" }, "=<=", &ALLSLCosts::strictForInclusive },
            { "a <= 5 as a < 6", { "", "if (j <= 5) i = 2;" }, { "", "if (j < 6) i = 2;" }, "=<=", &ALLSLCosts::strictForInclusive },
            { "a || b as a | b", { "", "while (i || j) i--;" }, { "", "while (i | j) i--;" }, "=<>", &ALLSLCosts::bitOrForOr },
            { "a <= b as a < b + 1, of a variable", { "", "i = j <= i;" }, { "", "i = j < i + 1;" }, "><=", nullptr },
            // The library as casts.
            { "llDumpList2String(l, \"\") as (string)l", { "", "s = llDumpList2String(l, \"\");" }, { "", "s = (string)l;" }, "<<<",
              &ALLSLCosts::castForDump },
            { "llList2String(llGetObjectDetails(k, [X]), 0) as (string)", { "", "s = llList2String(llGetObjectDetails(k, [OBJECT_NAME]), 0);" },
              { "", "s = (string)llGetObjectDetails(k, [OBJECT_NAME]);" }, "<<<", &ALLSLCosts::castForDetail },
            // Not taken: a key's is NULL_KEY from an empty list, an integer's no 0 of a key.
            { "llList2Key(llGetObjectDetails(k, [X]), 0) as (key)(string)", { "", "k = llList2Key(llGetObjectDetails(k, [OBJECT_OWNER]), 0);" },
              { "", "k = (key)((string)llGetObjectDetails(k, [OBJECT_OWNER]));" }, "<>>", nullptr },
            { "a whole float in a list as ((float)2)", { "", "l = [2.0];" }, { "", "l = [(float)2];" }, "><=", &ALLSLCosts::castForWholeFloat },
            { "a whole float cast as ((float)2)", { "", "s = (string)2.0;" }, { "", "s = (string)((float)2);" }, "><=", &ALLSLCosts::castForWholeFloat },
            // To come.
            { "i = i + 1 as ++i", { "", "i = i + 1;" }, { "", "++i;" }, "===", nullptr },
            { "-5 as ((integer)-5)", { "", "i = -5;" }, { "", "i = ((integer)-5);" }, "===", nullptr },
            { "-5.5 as ((float)-5.5)", { "", "f = -5.5;" }, { "", "f = ((float)-5.5);" }, "=<>", nullptr },
            { "integer x = 0 as integer x", { "", "integer z = 0; i = z;" }, { "", "integer z; i = z;" }, "===", nullptr },
            { "a && b as !(!a | !b)", { "", "if (i && j) i = 2;" }, { "", "if (!(!i | !j)) i = 2;" }, ">=>", nullptr },
            { "NULL_KEY as \"\"", { "", "k = llGetOwnerKey(NULL_KEY);" }, { "", "k = llGetOwnerKey(\"\");" }, "<<<", &ALLSLCosts::emptyForNullKey },
            { "llStringLength(s) as s != \"\"", { "", "if (llStringLength(s)) i = 2;" }, { "", "if (s != \"\") i = 2;" }, "<>>", &ALLSLCosts::emptyForLength },
            { "llDumpList2String(l, \"\") as (string)l", { "", "s = llDumpList2String(l, \"\");" }, { "", "s = (string)l;" }, "<<<", nullptr },
            { "if (!a) A else B as if (a) B else A", { "", "if (!j) i = 2; else i = 3;" }, { "", "if (j) i = 3; else i = 2;" }, "<<<", nullptr },
            { "a trailing return;", { "integer i0; f1() { i0 = 1; return; }", "f1();" }, { "integer i0; f1() { i0 = 1; }", "f1();" }, "===", nullptr },
        };
        for (const Fact& fact : facts)
        {
            for (size_t n = 0; n < 3; ++n)
            {
                const Target t = TARGETS[n];
                const Delta  d = delta(t, fact.a, fact.b);
                said(fact.what, t, d, 0);
                const char v = fact.once ? verdict({ d.first, d.first }) : verdict(d);
                ensure_equals(std::string(fact.what) + " on " + nameOf(t), v, fact.verdicts[n]);
                // A flag on is a form smaller there; one off may be for what
                // a first use costs a script that the pair does not show
                // (allslcosts.cpp says where).
                if (fact.flag && ALLSLCosts::of(t).*fact.flag)
                {
                    ensure(std::string(fact.what) + ": the flag on " + nameOf(t) + ", and the form not smaller", v == '<');
                }
            }
        }
    }

    // What a function costs, and a call of it, a local and a jump.
    template<> template<>
    void allslcosts_object::test<2>()
    {
        ensure("builtins: " + error, lslLoaded);
        const Snippet none{ "", "" };
        const Snippet two{ "f1() { }", "" };
        const Snippet eighteen{ "a_much_longer_name() { }", "" };
        const Snippet param{ "f1(integer a) { }", "" };
        for (const Target t : TARGETS)
        {
            const ALLSLCosts& c     = ALLSLCosts::of(t);
            const std::string where = std::string(" on ") + nameOf(t);

            const Delta local = delta(t, { "", "integer z = i; j = z;" }, { "", "j = i;" });
            said("a local", t, local, -c.local);
            ensure_equals("a local" + where, -local.each, c.local);

            const Delta jump = delta(t, { "", "jump L; @L;" }, { "", "" });
            said("a jump", t, jump, -c.jump);
            // What Luau's compiler makes of a jump is a byte more in some
            // runs than in others, the same build and the same text.
            ensure("a jump" + where + ": " + std::to_string(-jump.first), t == Target::Luau ? std::abs(-jump.first - c.jump) <= 1 : -jump.first == c.jump);

            const Delta f2  = delta(t, none, two);
            const Delta f18 = delta(t, none, eighteen);
            said("a function", t, f2, c.function + 2 * c.functionChar);
            ensure_equals("a function's name, by the character" + where, (f18.first - f2.first) / 16, c.functionChar);
            ensure_equals("a function" + where, f2.first - 2 * c.functionChar, c.function);

            const Delta p = delta(t, two, param);
            said("a parameter", t, p, c.parameter);
            ensure_equals("a parameter" + where, p.first, c.parameter);

            const Delta call = delta(t, { "f1() { }", "f1();" }, { "f1() { }", "f1(); f1();" });
            said("a call", t, call, c.call);
            ensure_equals("a call" + where, call.each, c.call);

            const Delta argued = delta(t, { "f1(integer a) { }", "f1(i);" }, { "f1(integer a) { }", "f1(i); f1(i);" });
            said("a call with an argument", t, argued, c.call + c.argument);
            ensure_equals("an argument" + where, argued.each - c.call, c.argument);

            for (const char* text : { "hello", "hello world, hello" })
            {
                const S32   length = S32(strlen(text));
                const Delta string = delta(t, { "", "s = s;" }, { "", "s = \"" + std::string(text) + "\";" });
                said("a string of " + std::to_string(length) + ", held once", t, string, c.stringHeld + length * c.stringHeldChar);
                ensure_equals("a string of " + std::to_string(length) + ", held once" + where, string.first - string.each,
                              c.stringHeld + length * c.stringHeldChar);
            }

            const Delta first2  = delta(t, two, { "f1() { }", "f1();" });
            const Delta first18 = delta(t, eighteen, { "a_much_longer_name() { }", "a_much_longer_name();" });
            said("a first call", t, first2, c.call + c.reference + 2 * c.referenceChar);
            ensure_equals("a first call's reference, by the character" + where, (first18.first - first2.first) / 16, c.referenceChar);
            ensure_equals("a first call's reference" + where, first2.first - c.call - 2 * c.referenceChar, c.reference);

            // The most elements of a literal added to a list bare: at it
            // smaller, one past it larger; with none, eight smaller.
            const auto bare = [&](S32 n) {
                std::string literal, sum;
                for (S32 k = 0; k < n; ++k)
                {
                    const char* one = k % 2 ? "j" : "s";
                    literal += (k ? ", " : "") + std::string(one);
                    sum += std::string(" + ") + one;
                }
                return delta(t, { "", "l = l + [" + literal + "];" }, { "", "l = l" + sum + ";" });
            };
            if (c.elementsForListMost > 0)
            {
                ensure("the most added bare, smaller" + where, verdict(bare(c.elementsForListMost)) == '<');
                ensure("one more, larger" + where, verdict(bare(c.elementsForListMost + 1)) == '>');
            }
            else
            {
                ensure("eight added bare, smaller" + where, verdict(bare(8)) == '<');
            }
        }
    }

    // A global never assigned, against its value written where it is read.
    template<> template<>
    void allslcosts_object::test<3>()
    {
        ensure("builtins: " + error, lslLoaded);
        struct Kind
        {
            const char*                  what;
            ALLSLCosts::Held ALLSLCosts::*held;
            const char*                  type;
            std::vector<double>          parts;
            const char*                  read;
        };
        const Kind kinds[] = {
            { "an integer", &ALLSLCosts::integer, "integer", {}, "i = @;" },
            { "a float", &ALLSLCosts::floating, "float", { 2.5 }, "f = @;" },
            { "a whole float", &ALLSLCosts::wholeFloating, "float", { 2.0 }, "f = @;" },
            { "a vector", &ALLSLCosts::vector, "vector", { 1.5, 2.5, 3.5 }, "v = @;" },
            { "a whole vector", &ALLSLCosts::wholeVector, "vector", { 1, 2, 3 }, "v = @;" },
            { "a rotation", &ALLSLCosts::rotation, "rotation", { .5, .5, .5, .5 }, "v = v * @;" },
            { "a whole rotation", &ALLSLCosts::wholeRotation, "rotation", { 1, 2, 3, 4 }, "v = v * @;" },
        };
        const auto put = [](std::string text, const std::string& what) { return text.replace(text.find('@'), 1, what); };
        for (const Target t : TARGETS)
        {
            const ALLSLCosts& c     = ALLSLCosts::of(t);
            const std::string where = std::string(" on ") + nameOf(t);
            for (const Kind& kind : kinds)
            {
                std::string value;
                for (double part : kind.parts)
                {
                    value += (value.empty() ? "" : ", ") + number(t, part);
                }
                value = kind.parts.empty() ? std::string("1234567") : kind.parts.size() > 1 ? "<" + value + ">" : value;
                const std::string       global = std::string(kind.type) + " gw = " + value + ";";
                const Delta             own    = delta(t, { "", "" }, { global, "" });
                const Delta             out    = delta(t, { global, put(kind.read, "gw") }, { global, put(kind.read, value) });
                const ALLSLCosts::Held& held   = c.*kind.held;
                said(std::string(kind.what) + " global", t, own, held.global);
                said(std::string(kind.what) + " written out", t, out, held.first);
                ensure_equals(std::string(kind.what) + " global" + where, own.first, held.global);
                ensure_equals(std::string(kind.what) + " written out, first" + where, out.first, held.first);
                ensure_equals(std::string(kind.what) + " written out, each" + where, out.each, held.each);
            }
            // A string at two lengths, what a character adds the same.
            for (const char* text : { "hello", "hello world, hello" })
            {
                const S32              length = S32(strlen(text));
                const std::string      quoted = std::string("\"") + text + "\"";
                const std::string      global = "string gw = " + quoted + ";";
                const Delta            own    = delta(t, { "", "" }, { global, "" });
                const Delta            out    = delta(t, { global, "s = gw;" }, { global, "s = " + quoted + ";" });
                const ALLSLCosts::Held held   = c.stringOf(length);
                const std::string      what   = "a string of " + std::to_string(length);
                said(what + " global", t, own, held.global);
                said(what + " written out", t, out, held.first);
                ensure_equals(what + " global" + where, own.first, held.global);
                ensure_equals(what + " written out, first" + where, out.first, held.first);
                ensure_equals(what + " written out, each" + where, out.each, held.each);
            }
        }
    }

    // What putting a function in place is estimated to come to, against the
    // script written both ways and weighed: the same side of nothing, and
    // near it.
    template<> template<>
    void allslcosts_object::test<4>()
    {
        ensure("builtins: " + error, lslLoaded);
        struct Case
        {
            const char* what;
            Snippet     called;
            Snippet     inlined;
            const char* name;
            ALLSLCosts::Inlining f;
        };
        const Case cases[] = {
            { "an expression, three times", { "integer sq(integer x) { return x * x; }", "i = sq(j); j = sq(i); i = sq(j);" },
              { "", "i = j * j; j = i * i; i = j * j;" }, "sq", { 0, 3, 1, 0, 0, 0, 0, 2 } },
            { "a statement, twice", { "say(string m) { llOwnerSay(\"[\" + m + \"]\"); }", "say(s); say(gs);" },
              { "", "{ string m = s; llOwnerSay(\"[\" + m + \"]\"); } { string m = gs; llOwnerSay(\"[\" + m + \"]\"); }" }, "say", { 0, 2, 1, 1, 0, 2, 2, 3 } },
            { "a longer body, five times",
              { "tell(integer n) { if (n > 2) llOwnerSay(\"many \" + (string)n); else llOwnerSay(\"few\"); llSetText((string)n, gv, 1.0); }",
                "tell(i); tell(j); tell(i + j); tell(2); tell(i * 2);" },
              { "", "{ integer n = i; if (n > 2) llOwnerSay(\"many \" + (string)n); else llOwnerSay(\"few\"); llSetText((string)n, gv, 1.0); }"
                    "{ integer n = j; if (n > 2) llOwnerSay(\"many \" + (string)n); else llOwnerSay(\"few\"); llSetText((string)n, gv, 1.0); }"
                    "{ integer n = i + j; if (n > 2) llOwnerSay(\"many \" + (string)n); else llOwnerSay(\"few\"); llSetText((string)n, gv, 1.0); }"
                    "{ integer n = 2; if (n > 2) llOwnerSay(\"many \" + (string)n); else llOwnerSay(\"few\"); llSetText((string)n, gv, 1.0); }"
                    "{ integer n = i * 2; if (n > 2) llOwnerSay(\"many \" + (string)n); else llOwnerSay(\"few\"); llSetText((string)n, gv, 1.0); }" },
              "tell", { 0, 5, 1, 1, 0, 2, 8, 4 } },
        };
        for (const Case& one : cases)
        {
            for (const Target t : TARGETS)
            {
                const std::string    text  = script(one.called, 1);
                const ALScriptWeight whole = t == Target::LSO ? ALScriptWeigh::lso(text) : t == Target::Mono ? ALScriptWeigh::mono(text) : ALScriptWeigh::lslLuau(text);
                S32                  bytes = -1;
                for (const ALScriptWeight::Part& part : whole.parts)
                {
                    bytes = part.name == one.name ? S32(part.bytes) : bytes;
                }
                ensure(std::string(one.what) + ": the function weighed on " + nameOf(t) + ": " + whole.error, bytes > 0);
                const S32 measured = size(t, one.inlined, 1) - size(t, one.called, 1);
                ALLSLCosts::Inlining f = one.f;
                f.bytes                = bytes;
                const S32 estimate     = ALLSLCosts::of(t).inlined(f);
                said(std::string(one.what) + " (estimated)", t, { measured, measured }, estimate);
                ensure(std::string(one.what) + " on " + nameOf(t) + ": estimated " + std::to_string(estimate) + ", measured " + std::to_string(measured),
                       (estimate < 0) == (measured < 0) && std::abs(estimate - measured) <= std::max(12, std::abs(measured) / 4));
            }
        }
    }

    // Where a list's helpers are held once for the script: the least a
    // list's shape saves at a place, and the most a helper new to the
    // script costs.
    template<> template<>
    void allslcosts_object::test<5>()
    {
        ensure("builtins: " + error, lslLoaded);
        for (const Target t : TARGETS)
        {
            const ALLSLCosts& c     = ALLSLCosts::of(t);
            const std::string where = std::string(" on ") + nameOf(t);
            if (c.listShapeLeast == 0)
            {
                continue;
            }
            const std::pair<Snippet, Snippet> shapes[] = {
                { { "", "l = [i];" }, { "", "l = (list)i;" } },
                { { "", "l = [i, j];" }, { "", "l = (list)i + j;" } },
                { { "", "l = l + [i];" }, { "", "l = l + i;" } },
                { { "", "l = l + [i, j];" }, { "", "l = l + i + j;" } },
                { { "", "l = [s] + l;" }, { "", "l = s + l;" } },
                { { "", "l = l + (list)s;" }, { "", "l = l + s;" } },
                { { "", "l += [i];" }, { "", "l += i;" } },
            };
            S32 least = std::numeric_limits<S32>::max();
            for (const auto& [a, b] : shapes)
            {
                least = std::min(least, -delta(t, a, b).each);
            }
            said("the least a list's shape saves", t, { least, least }, c.listShapeLeast);
            ensure_equals("the least a list's shape saves at a place" + where, least, c.listShapeLeast);
            // A helper each: a cast to a list, something added after one,
            // something added before one.
            const Snippet had{ "", "l = l + [i];" };
            S32           most = 0;
            for (const char* more : { "l = (list)s;", "l = l + v;", "l = v + l;" })
            {
                const Delta d = delta(t, had, { "", std::string("l = l + [i]; ") + more });
                most          = std::max(most, d.first - d.each);
            }
            said("the most a helper costs", t, { most, most }, c.listHelperMost);
            ensure_equals("the most a list's helper new to the script costs" + where, most, c.listHelperMost);
        }
    }
}
