/**
 * @file allsovm_test.cpp
 * @brief LL's LSO VM run on scripts that reach each kind of its operations
 *
 * Copyright (c) 2026, Alchemy Viewer Project.
 *
 * The source code in this file is provided to you under the terms of the
 * GNU Lesser General Public License, version 2.1, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. Terms of the LGPL can be found in doc/LGPL-licence.txt
 * in this distribution, or online at http://www.gnu.org/licenses/lgpl-2.1.txt
 *
 */

// The reference's VM is what the optimizer and the weigher are checked
// against, so it is run here on scripts that reach its opcodes, its typed
// operations, its heap and its faults, as the sanitized builds run every
// test: what the scripts say through llSay is what the grid's LSO says.

#include "linden_common.h"

#include "allsovmrun.h"
#include "altestsanitizer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using ll_test_lso::Event;
    using ll_test_lso::EventArgs;
    using ll_test_lso::runLSO;
    using Said = std::vector<std::string>;

    // A library function, by its name.
    LLScriptLibraryFunction& libraryFunction(const char* name)
    {
        LLScriptLibraryFunction* function = ll_test_lso::libraryFunction(name);
        REQUIRE(function);
        return *function;
    }

    char* copied(const char* text)
    {
        char* out = new char[std::strlen(text) + 1];
        std::strcpy(out, text);
        return out;
    }

    // Library functions answering with a value of each type, as the
    // simulator's would: the VM makes the value's type from the function's
    // and deletes it after.
    void answerTime(LLScriptLibData* retval, LLScriptLibData*, const LLUUID&) { retval->mInteger = 1234567890; }
    void answerHalf(LLScriptLibData* retval, LLScriptLibData* args, const LLUUID&) { retval->mFP = args[0].mFP / 2; }
    void answerName(LLScriptLibData* retval, LLScriptLibData*, const LLUUID&) { retval->mString = copied("box"); }
    void answerKey(LLScriptLibData* retval, LLScriptLibData*, const LLUUID&) { retval->mKey = copied("00000000-0000-0000-0000-0000000000ff"); }
    void answerPos(LLScriptLibData* retval, LLScriptLibData*, const LLUUID&) { retval->mVec = LLVector3(1.f, 2.f, 3.f); }
    void answerRot(LLScriptLibData* retval, LLScriptLibData*, const LLUUID&) { retval->mQuat = LLQuaternion(0.f, 0.f, 0.f, 1.f); }
    // How many it was given, and "end": a list each way.
    void answerParams(LLScriptLibData* retval, LLScriptLibData* args, const LLUUID&)
    {
        S32 given = 0;
        for (const LLScriptLibData* item = args[0].mListp; item; item = item->mListp)
        {
            ++given;
        }
        retval->mListp          = new LLScriptLibData(given);
        retval->mListp->mListp = new LLScriptLibData("end");
    }

    // A script whose state_entry is the body given.
    std::string entry(const std::string& body, const std::string& globals = std::string())
    {
        return globals + "default { state_entry() {\n" + body + "\n} }\n";
    }

    TEST_CASE("allsovm: integers wrap and divide toward nothing and shift by their count's low five bits", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            integer big = 2147483647;
            integer least = -2147483648;
            integer none = -1;
            llSay(0, (string)(big + 1) + " " + (string)(least - 1) + " " + (string)(big * 2));
            llSay(0, (string)(least * none) + " " + (string)(least / none) + " " + (string)(least % none));
            llSay(0, (string)(7 / 2) + " " + (string)(-7 / 2) + " " + (string)(7 % 3) + " " + (string)(-7 % 3));
            llSay(0, (string)(1 << 33) + " " + (string)(-16 >> 2) + " " + (string)(1 << -1) + " " + (string)(-1 >> 40));
            llSay(0, (string)(5 & 3) + " " + (string)(5 | 3) + " " + (string)(5 ^ 3) + " " + (string)(~5) + " " + (string)(-big));
            llSay(0, (string)(3 < 4) + (string)(3 > 4) + (string)(3 <= 3) + (string)(3 >= 4) + (string)(3 == 3) + (string)(3 != 3));
            llSay(0, (string)(!0) + (string)(!7) + (string)(0 && 1) + (string)(2 && 3) + (string)(0 || 2) + (string)(0 || 0));
            integer b = 5;
            b += 3; b -= 1; b *= 2; b /= 3; b %= 3;
            integer c = 1;
            c++; ++c; c--; --c; ++c;
            llSay(0, (string)b + " " + (string)c);
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "-2147483648 2147483647 -2",
                                "-2147483648 -2147483648 0",
                                "3 -3 1 -1",
                                "2 -4 -2147483648 -1",
                                "1 7 6 -6 -2147483647",
                                "101010",
                                "100110",
                                "1 2" });
    }

    TEST_CASE("allsovm: floats compute and compare and cast to and from integers and strings", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            float f = 1.5;
            llSay(0, (string)(f * 2.0 + 0.25) + " " + (string)(f - 4) + " " + (string)(f / 4) + " " + (string)(-f));
            llSay(0, (string)((integer)3.99) + " " + (string)((integer)-3.99) + " " + (string)((float)7 / 2) + " " + (string)(2 + 0.5));
            llSay(0, (string)(f < 2) + (string)(f > 2) + (string)(f == 1.5) + (string)(f != 1.5) + (string)(f <= 1.5) + (string)(f >= 1.6));
            f += 1; f -= 0.5; f *= 4; f /= 2;
            llSay(0, (string)f + " " + (string)((float)"1.5e1") + " " + (string)((integer)"42abc") + " " + (string)((integer)"0x1F"));
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "3.250000 -2.500000 0.375000 -1.500000",
                                "3 -3 3.500000 2.500000",
                                "101010",
                                "4.000000 15.000000 42 31" });
    }

    TEST_CASE("allsovm: strings and keys join and compare and cast", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            string s = "ab";
            s += "cd";
            key k = "00000000-0000-0000-0000-000000000001";
            string t = (string)k;
            llSay(0, s + " " + (string)(s == "abcd") + (string)(s != "abcd") + (string)(s == "ab"));
            llSay(0, t + " " + (string)(k == t) + " " + (string)((key)"" == ""));
            llSay(0, (string)((integer)"  -12") + " " + (string)7 + (string)-7 + " " + (string)"");
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "abcd 100",
                                "00000000-0000-0000-0000-000000000001 1 1",
                                "-12 7-7 " });
    }

    // A rotation's results are said in thousandths, rounded: what is left of
    // a sum that should be nothing has the sign the compiler's order of the
    // arithmetic gives it, which LSO would print as 0.00000 or -0.00000.
    const char* const THOUSANDTHS = R"lsl(
            integer milli(float f) { if (f < 0) return -(integer)(-f * 1000 + 0.5); return (integer)(f * 1000 + 0.5); }
            string v3(vector v) { return (string)milli(v.x) + "," + (string)milli(v.y) + "," + (string)milli(v.z); }
            string q4(rotation r) { return (string)milli(r.x) + "," + (string)milli(r.y) + "," + (string)milli(r.z) + "," + (string)milli(r.s); }
        )lsl";

    TEST_CASE("allsovm: vectors and rotations compute and compare and cast", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            vector v = <1, 2, 3>;
            vector w = <4, 5, 6>;
            rotation none = <0, 0, 0, 1>;
            rotation quarter = <0, 0, 0.70710678, 0.70710678>;
            llSay(0, (string)(v + w) + " " + (string)(v - w) + " " + (string)(v * 2) + " " + (string)(v / 2));
            llSay(0, (string)(v * w) + " " + (string)(v % w) + " " + (string)(-v));
            llSay(0, (string)(v.y) + " " + (string)(v == <1, 2, 3>) + (string)(v != w) + " " + (string)(v * none));
            llSay(0, v3(<1, 0, 0> * quarter) + " " + q4(none * quarter) + " " + q4(quarter / quarter));
            llSay(0, (string)(quarter.s) + " " + (string)(quarter == quarter) + (string)(quarter != none) + " " + (string)(-<1, 2, 3, 4>));
            v.z = 9;
            llSay(0, (string)v + " " + (string)((vector)"<7, 8, 9>") + " " + (string)((rotation)"<1, 2, 3, 4>"));
        )lsl", THOUSANDTHS));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "<5.00000, 7.00000, 9.00000> <-3.00000, -3.00000, -3.00000> <2.00000, 4.00000, 6.00000> <0.50000, 1.00000, 1.50000>",
                                "32.000000 <-3.00000, 6.00000, -3.00000> <-1.00000, -2.00000, -3.00000>",
                                "2.000000 11 <1.00000, 2.00000, 3.00000>",
                                "0,1000,0 0,0,707,707 0,0,0,1000",
                                "0.707107 11 <-1.00000, -2.00000, -3.00000, -4.00000>",
                                "<1.00000, 2.00000, 9.00000> <7.00000, 8.00000, 9.00000> <1.00000, 2.00000, 3.00000, 4.00000>" });
    }

    TEST_CASE("allsovm: lists build and join and compare by their lengths", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            list l = [1, 2.5, "x", <1, 2, 3>];
            l += [7];
            l = 0 + l;
            l = l + "end";
            key k = "00000000-0000-0000-0000-000000000002";
            list m = [k, <0, 0, 0, 1>];
            llSay(0, (string)l);
            llSay(0, (string)(l == [1, 2, 3, 4, 5, 6, 7]) + " " + (string)(l != [1]) + " " + (string)([] == []));
            llSay(0, (string)(m + l));
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "012.500000x<1.000000, 2.000000, 3.000000>7end",
                                "1 6 1",
                                "00000000-0000-0000-0000-000000000002<0.000000, 0.000000, 0.000000, 1.000000>012.500000x<1.000000, 2.000000, 3.000000>7end" });
    }

    TEST_CASE("allsovm: functions take and return each type and recurse and loop", "[allsovm]")
    {
        const std::string globals = R"lsl(
            integer gCount = 0;
            float gScale = 2.0;
            string gText = "g";
            vector gV = <1, 1, 1>;
            rotation gR = <0, 0, 0, 1>;
            list gL = [1];
            key gK = "00000000-0000-0000-0000-000000000003";
            integer fact(integer n) { if (n <= 1) return 1; return n * fact(n - 1); }
            string repeat(string s, integer n) { string out; integer i; for (i = 0; i < n; ++i) out += s; return out; }
            vector scale(vector v, float k) { ++gCount; return v * k * gScale; }
            rotation turn(rotation r) { return r * gR; }
            list more(list l, key k) { gL += l; return l + [k]; }
            float half(float f) { return f / 2; }
            key same(key k) { return k; }
            nothing() { gText += "!"; }
        )lsl";
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            llSay(0, (string)fact(10) + " " + repeat("ab", 3) + " " + (string)scale(gV, 1.5) + " " + (string)half(5));
            llSay(0, (string)turn(<0, 0, 1, 0>) + " " + (string)more([2], gK) + " " + (string)same(gK));
            nothing();
            integer i = 0;
            while (i < 5) ++i;
            do { i += 10; } while (i < 30);
            if (i == 35) llSay(0, "loops " + (string)i); else llSay(0, "loops wrong");
            if (i < 0) llSay(0, "no"); else if (i > 100) llSay(0, "no"); else llSay(0, gText + (string)gCount + (string)gL);
            jump over;
            llSay(0, "jumped over");
            @over;
            llSay(0, "after");
        )lsl", globals));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "3628800 ababab <3.00000, 3.00000, 3.00000> 2.500000",
                                "<0.00000, 0.00000, 1.00000, 0.00000> 200000000-0000-0000-0000-000000000003 00000000-0000-0000-0000-000000000003",
                                "loops 35",
                                "g!112",
                                "after" });
    }

    // LSO patches only the last of several jumps to one label: the others are
    // left as they were compiled, a jump of nothing, and fall through.
    // Scripts on the grid see this, and the reference keeps it.
    TEST_CASE("allsovm: of several jumps to one label only the last jumps as on the grid", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            integer n = 0;
            if (n == 0) jump done;
            llSay(0, "the first jump fell through");
            jump done;
            llSay(0, "the last jump did not jump");
            @done;
            llSay(0, "done");
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "the first jump fell through", "done" });
    }

    TEST_CASE("allsovm: events of each argument type are handled and states change", "[allsovm]")
    {
        const LLUUID who("00000000-0000-0000-0000-00000000000a");
        const ll_test_lso::Run run = runLSO(R"lsl(
            default
            {
                state_entry() { llSay(0, "default entry"); }
                touch_start(integer n) { llSay(0, "touched " + (string)n); state second; }
                state_exit() { llSay(0, "default exit"); }
            }
            state second
            {
                state_entry() { llSay(0, "second entry"); }
                listen(integer channel, string name, key id, string message) { llSay(0, (string)channel + name + (string)id + message); }
                link_message(integer sender, integer num, string str, key id) { llSay(0, (string)(sender + num) + str + (string)id); }
                at_target(integer handle, vector target, vector pos) { llSay(0, (string)handle + (string)target + (string)(pos - target)); }
                timer() { llSay(0, "tick"); state default; }
            }
        )lsl",
                                            {
                                                Event{ LSTT_TOUCH_START, EventArgs().integer(3) },
                                                Event{ LSTT_CHAT, EventArgs().integer(5).string("Bob").key(who).string("hi") },
                                                Event{ LSTT_LINK_MESSAGE, EventArgs().integer(1).integer(41).string("str").key(who) },
                                                Event{ LSTT_AT_TARGET, EventArgs().integer(2).vector(LLVector3(1.f, 2.f, 3.f)).vector(LLVector3(1.f, 2.f, 4.f)) },
                                                Event{ LSTT_TIMER, EventArgs() },
                                            });
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "default entry",
                                "touched 3",
                                "default exit",
                                "second entry",
                                "5Bob00000000-0000-0000-0000-00000000000ahi",
                                "42str00000000-0000-0000-0000-00000000000a",
                                "2<1.00000, 2.00000, 3.00000><0.00000, 0.00000, 1.00000>",
                                "tick",
                                "default entry" });
    }

    // Strings and lists made and let go over and over, of sizes that leave
    // the heap's blocks to be split, merged and reused.
    TEST_CASE("allsovm: the heap keeps up with strings and lists made and let go", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            string s;
            string last;
            list keep;
            integer i;
            for (i = 0; i < 300; ++i)
            {
                list l = [i, (string)i, <i, i, i>, (float)i];
                last = (string)l;
                s += "x";
                if (i % 7 == 0) keep += [i];
                if (i % 50 == 0) s = "";
            }
            llSay(0, last + " " + s + " " + (string)keep);
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "299299<299.000000, 299.000000, 299.000000>299.000000 "
                                "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx "
                                "0714212835424956637077849198105112119126133140147154161168175182189196203210217224231238245252259266273280287294" });
    }

    TEST_CASE("allsovm: dividing by nothing is a math error that stops the script", "[allsovm]")
    {
        const ll_test_lso::Run integers = runLSO(entry(R"lsl(
            integer none = 0;
            llSay(0, "before");
            llSay(0, (string)(1 / none));
            llSay(0, "after");
        )lsl"));
        CHECK(integers.error == "Math Error");
        CHECK(integers.said == Said{ "before" });

        const ll_test_lso::Run remainder = runLSO(entry(R"lsl(
            integer none = 0;
            llSay(0, (string)(1 % none));
        )lsl"));
        CHECK(remainder.error == "Math Error");

        const ll_test_lso::Run floats = runLSO(entry(R"lsl(
            float none = 0;
            llSay(0, (string)(1.0 / none));
        )lsl"));
        CHECK(floats.error == "Math Error");
    }

    // What a regression script's header says comes of it: the fault on its
    // "// expect fault: " line, none without one, and what it says on its
    // "// expect said: " lines, in order; or, with an "// expect refused: "
    // line, that LL's compiler refuses it, saying that. A "// deeper than
    // ThreadSanitizer follows" line says LL's compiler recurses on it past
    // ll_test::THREAD_SANITIZER_FRAMES, so that a ThreadSanitizer build
    // passes it over.
    struct Expected
    {
        std::string fault;
        Said        said;
        std::string refused;
        bool        tooDeepForTsan = false;
    };

    Expected expectedOf(const std::string& text)
    {
        static const std::string FAULT   = "// expect fault:";
        static const std::string SAID    = "// expect said:";
        static const std::string REFUSED = "// expect refused:";
        Expected                 expected;
        size_t                   at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            end        = end == std::string::npos ? text.size() : end;
            std::string line = text.substr(at, end - at);
            at               = end + 1;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            auto value = [&line](const std::string& prefix) { return line.substr(line.size() > prefix.size() ? prefix.size() + 1 : prefix.size()); };
            if (line.starts_with(FAULT))
            {
                expected.fault = value(FAULT);
            }
            else if (line.starts_with(SAID))
            {
                expected.said.push_back(value(SAID));
            }
            else if (line.starts_with(REFUSED))
            {
                expected.refused = value(REFUSED);
            }
            else if (line == "// deeper than ThreadSanitizer follows")
            {
                expected.tooDeepForTsan = true;
            }
        }
        return expected;
    }

    // Each script in tests/regressions: a fault the sanitizers found in LL's
    // compiler or VM, most by the fuzzers in fuzz/, kept as the script that
    // shows it. Read as bytes, since some hold what no editor would write.
    TEST_CASE("allsovm: each regression script runs as its header says", "[allsovm]")
    {
        std::vector<std::filesystem::path> scripts;
        for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(AL_LSCRIPT_REGRESSION_DIR))
        {
            if (item.path().extension() == ".lsl")
            {
                scripts.push_back(item.path());
            }
        }
        std::sort(scripts.begin(), scripts.end());
        REQUIRE_FALSE(scripts.empty());
        for (const std::filesystem::path& path : scripts)
        {
            INFO(path.filename().string());
            std::ifstream     in(path, std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            REQUIRE_FALSE(text.empty());
            const Expected expected = expectedOf(text);
            if (ll_test::THREAD_SANITIZER && expected.tooDeepForTsan)
            {
                continue;
            }
            if (!expected.refused.empty())
            {
                const ALLSLReference::Result lso = ALLSLReference::compile(text, ALLSLReference::Target::LSO);
                INFO(lso.messages);
                CHECK_FALSE(lso.ok);
                CHECK(lso.messages.find(expected.refused) != std::string::npos);
                continue;
            }
            const ll_test_lso::Run run = runLSO(text);
            CHECK(run.error == expected.fault);
            CHECK(run.said == expected.said);
        }
    }

    TEST_CASE("allsovm: running out of memory is a stack-heap collision that stops the script", "[allsovm]")
    {
        const ll_test_lso::Run grown = runLSO(entry(R"lsl(
            list l = ["0123456789"];
            while (TRUE) l += l;
        )lsl"));
        CHECK(grown.error == "Stack-Heap Collision");

        const ll_test_lso::Run deep = runLSO(entry(R"lsl(
            llSay(0, (string)deeper(0));
        )lsl", "integer deeper(integer n) { return deeper(n + 1) + 1; }\n"));
        CHECK(deep.error == "Stack-Heap Collision");
    }

    TEST_CASE("allsovm: mixed types multiply and compare and join lists either way", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            vector v = <1, 2, 3>;
            key k = "00000000-0000-0000-0000-000000000004";
            key same = "00000000-0000-0000-0000-000000000004";
            llSay(0, (string)(2 * v) + " " + (string)(0.5 * v) + " " + (string)(v * 0.5));
            llSay(0, (string)(k == same) + (string)(k != same) + (string)("00000000-0000-0000-0000-000000000004" == k) + (string)(k == "x"));
            list l = [1];
            l = 2.5 + l; l = "s" + l; l = k + l; l = <1, 2, 3> + l; l = <0, 0, 0, 1> + l;
            l = l + 3.5; l = l + k; l = l + <4, 5, 6>; l = l + <0, 0, 1, 0>; l = l + 7;
            llSay(0, (string)l);
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "<2.00000, 4.00000, 6.00000> <0.50000, 1.00000, 1.50000> <0.50000, 1.00000, 1.50000>",
                                "1010",
                                "<0.000000, 0.000000, 0.000000, 1.000000><1.000000, 2.000000, 3.000000>"
                                "00000000-0000-0000-0000-000000000004s2.50000013.500000"
                                "00000000-0000-0000-0000-000000000004<4.000000, 5.000000, 6.000000><0.000000, 0.000000, 1.000000, 0.000000>7" });
    }

    TEST_CASE("allsovm: assignments chain for each type in locals and globals", "[allsovm]")
    {
        const std::string globals = R"lsl(
            vector gV;
            rotation gR;
            string gS;
            setGlobals() { gV = <1, 2, 3>; gR = <0, 0, 0, 1>; gS = gS = "w"; }
            vector through(vector v, rotation r) { vector here = v; rotation turn = r; return here * turn; }
        )lsl";
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            integer a; integer b; a = b = 4;
            float f; float g; f = g = 1.5;
            string s; string t; s = t = "u";
            key k1; key k2; k1 = k2 = "00000000-0000-0000-0000-000000000005";
            vector v1; vector v2; v1 = v2 = <1, 1, 2>;
            rotation r1; rotation r2; r1 = r2 = <0, 0, 0, 2>;
            list l1; list l2; l1 = l2 = [9];
            setGlobals();
            llSay(0, (string)(a + b) + " " + (string)(f + g) + " " + s + t + " " + (string)k1 + (string)k2);
            llSay(0, (string)(v1 + v2) + " " + (string)(r1 + r2) + " " + (string)(l1 + l2) + " " + (string)gV + (string)gR + gS);
            llSay(0, (string)through(v1, <0, 0, 0, 1>));
        )lsl", globals));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "8 3.000000 uu 00000000-0000-0000-0000-00000000000500000000-0000-0000-0000-000000000005",
                                "<2.00000, 2.00000, 4.00000> <0.00000, 0.00000, 0.00000, 4.00000> 99 <1.00000, 2.00000, 3.00000><0.00000, 0.00000, 0.00000, 1.00000>w",
                                "<1.00000, 1.00000, 2.00000>" });
    }

    TEST_CASE("allsovm: library functions answer with each type and take and give lists", "[allsovm]")
    {
        using ll_test_lso::StandIn;
        const ll_test_lso::TestLibrary library;
        const StandIn time(libraryFunction("llGetUnixTime"), answerTime);
        const StandIn half(libraryFunction("llFrand"), answerHalf);
        const StandIn name(libraryFunction("llGetObjectName"), answerName);
        const StandIn key(libraryFunction("llGetKey"), answerKey);
        const StandIn pos(libraryFunction("llGetPos"), answerPos);
        const StandIn rot(libraryFunction("llGetRot"), answerRot);
        const StandIn params(libraryFunction("llGetPrimitiveParams"), answerParams);
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            integer t = llGetUnixTime();
            float f = llFrand(5.0);
            string n = llGetObjectName();
            key k = llGetKey();
            vector p = llGetPos();
            rotation r = llGetRot();
            list l = llGetPrimitiveParams([1, "two", 3.0]);
            llSay(0, (string)t + " " + (string)f + " " + n + " " + (string)k);
            llSay(0, (string)p + " " + (string)r + " " + (string)l);
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "1234567890 2.500000 box 00000000-0000-0000-0000-0000000000ff",
                                "<1.00000, 2.00000, 3.00000> <0.00000, 0.00000, 0.00000, 1.00000> 3end" });
    }

    // What the simulator does with a script across a save: its state written
    // out, read into a VM made again from the bytecode, and run on; and a
    // reset, which starts it over.
    TEST_CASE("allsovm: a script's state written and read back runs on and a reset starts it over", "[allsovm]")
    {
        const std::vector<U8> image = ll_test_lso::compileLSO(R"lsl(
            integer gCount = 0;
            string gText = "start";
            list gL;
            default
            {
                state_entry() { gCount = 5; gText += "ed"; gL = [gCount, gText, <1, 2, 3>]; }
                touch_start(integer n) { gCount += n; llSay(0, (string)gCount + gText + (string)gL); }
            }
        )lsl");

        ll_test_lso::gSaid.clear();
        const ll_test_lso::TestLibrary library;
        U8*         saved  = nullptr;
        const char* error  = nullptr;
        U32         events = 0;
        {
            LLScriptExecuteLSL2 first(image.data(), static_cast<U32>(image.size()));
            ll_test_lso::settle(first, error, events);
            REQUIRE_FALSE(error);
            REQUIRE(first.writeState(&saved, 0, 0) > 0);
        }
        LLScriptExecuteLSL2 again(image.data(), static_cast<U32>(image.size()));
        CHECK(again.readState(saved) >= 0);
        delete[] saved;
        again.addEvent(EventArgs().integer(2).make(LSTT_TOUCH_START));
        ll_test_lso::settle(again, error, events);

        again.reset();
        ll_test_lso::settle(again, error, events);
        again.addEvent(EventArgs().integer(1).make(LSTT_TOUCH_START));
        ll_test_lso::settle(again, error, events);

        CHECK_FALSE(error);
        CHECK(ll_test_lso::gSaid == Said{ "7started5started<1.000000, 2.000000, 3.000000>",
                                          "6started5started<1.000000, 2.000000, 3.000000>" });
        ll_test_lso::gSaid.clear();
    }

    // The library as scripts reach it, answering as SLua's builtins do (see
    // allsolibrary.cpp): lists through the VM's heap both ways, strings by
    // the character, and the math at its edges.
    TEST_CASE("allsovm: library functions work lists and strings and numbers through the VM", "[allsovm]")
    {
        const ll_test_lso::Run run = runLSO(entry(R"lsl(
            list l = [1, 2, 3, 4, 5];
            llSay(0, llDumpList2String(llList2List(l, 1, 3), ",") + " " + llDumpList2String(llList2List(l, 3, 1), ",") + " " + llDumpList2String(llList2List(l, -2, -1), ","));
            llSay(0, llDumpList2String(llDeleteSubList(l, 1, 3), ",") + " " + llDumpList2String(llDeleteSubList(l, 3, 1), ","));
            llSay(0, llDumpList2String(llListInsertList(l, ["a", "b"], 2), ",") + " " + llDumpList2String(llListInsertList(l, ["z"], -1), ","));
            llSay(0, llDumpList2String(llListReplaceList(l, ["x"], 1, 2), ",") + " " + llDumpList2String(llListReplaceList(l, ["x"], 3, 1), ",") + " " + llDumpList2String(llListReplaceList(l, ["x"], 9, 9), ","));
            llSay(0, llDumpList2String(llListSort([3, 1, 2], 1, TRUE), ",") + " " + llDumpList2String(llListSort(["b", 2, "a", 1], 2, FALSE), ","));
            llSay(0, (string)llGetListLength(llListRandomize(l, 1)) + " " + llDumpList2String(llListSort(llListRandomize(l, 1), 1, TRUE), ","));
            llSay(0, (string)llList2Integer(["0x1F"], 0) + " " + (string)llList2Integer([" 12"], 0) + " " + (string)llList2Key([5], 0) + "|" + (string)llList2Float(["2.5"], 0) + " " + (string)llGetListEntryType([<1, 2, 3>], 0));
            llSay(0, llDumpList2String([-0.0, 1.5], ";") + " " + llList2CSV([1, "two", <1, 2, 3>]) + " " + (string)llCSV2List("a, b,c") + " " + (string)llParseString2List("a,b;;c", [","], [";"]));
            llSay(0, (string)llListFindList([1, "a", 2], ["a", 2]) + " " + (string)llListFindList([1], [2]));
            llSay(0, (string)llStringLength("héllo") + " " + llGetSubString("héllo", 1, 2) + " " + llGetSubString("héllo", 3, 1) + " " + (string)llSubStringIndex("héllo", "llo") + " " + llInsertString("héllo", 1, "X") + " " + llDeleteSubString("héllo", 1, 1));
            llSay(0, (string)llAbs(-2147483648) + " " + (string)llLog(-1) + " " + (string)llLog10(100) + " " + (string)llPow(2, 10) + " " + (string)llRound(2.5) + " " + (string)llFloor(-1.5) + " " + (string)llCeil(1.2) + " " + llToUpper("abc"));
            llSay(0, (string)llVecMag(<3, 4, 0>) + " " + (string)llVecNorm(<0, 0, 2>) + " " + (string)llModPow(3, 4, 5) + " " + llChar(233) + (string)llOrd("é", 0));
        )lsl"));
        CHECK(run.error.empty());
        CHECK(run.said == Said{ "2,3,4 1,2,4,5 4,5",
                                "1,5 3",
                                "1,2,a,b,3,4,5 1,2,3,4,z,5",
                                "1,x,4,5 3,x 1,2,3,4,5,x",
                                "1,2,3 b,2,a,1",
                                "5 1,2,3,4,5",
                                "31 12 |2.500000 5",
                                "0.000000;1.500000 1, two, <1.000000, 2.000000, 3.000000> abc ab;;c",
                                "1 -1",
                                "5 él hélo 2 hXéllo hllo",
                                "-2147483648 0.000000 2.000000 1024.000000 3 -2 2 ABC",
                                "5.000000 <0.00000, 0.00000, 1.00000> 1 é233" });
    }
}
