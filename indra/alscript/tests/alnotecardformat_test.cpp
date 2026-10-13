/**
 * @file alnotecardformat_test.cpp
 * @brief Tests of what a notecard's text is taken for, the lines a script cannot read whole, and a JSON notecard's outline.
 *
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

#include "../core/alnotecardformat.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct alnotecardformat_data
    {
        // An outline as one line a test can read: each entry as its depth
        // in dots, its name, its detail after an equals sign.
        static std::string said(const std::vector<ALScriptOutlineEntry>& entries)
        {
            std::string out;
            for (const ALScriptOutlineEntry& entry : entries)
            {
                out += (out.empty() ? "" : " ") + std::string(static_cast<size_t>(entry.depth), '.') + entry.name + "=" + entry.detail;
            }
            return out;
        }
    };

    TEST_CASE_METHOD(alnotecardformat_data, "ALNotecardFormat: guess: JSON by brace or bracket; settings by mostly keys; text otherwise", "[ALNotecardFormat]")
    {
        UNSCOPED_INFO("an object");
        CHECK(ALNotecardFormat::guess("  \n{ \"a\": 1 }\n") == std::string("json"));
        UNSCOPED_INFO("an array");
        CHECK(ALNotecardFormat::guess("[1, 2]") == std::string("json"));
        UNSCOPED_INFO("settings");
        CHECK(ALNotecardFormat::guess("# the door\n[Door]\nspeed = 2\nsound: creak\n\nlocked=false\n") == std::string("config"));
        UNSCOPED_INFO("one key is not enough");
        CHECK(ALNotecardFormat::guess("title: A Story\nOnce upon a time.\nThe end.\n") == std::string("text"));
        UNSCOPED_INFO("prose with a colon or two");
        CHECK(ALNotecardFormat::guess("Note: read this.\nWelcome to the store\nAsk for help\nHave fun\n") ==
                      std::string("text"));
        CHECK(ALNotecardFormat::guess("") == std::string("text"));
    }

    TEST_CASE_METHOD(alnotecardformat_data, "ALNotecardFormat: linesPast finds lines longer than llGetNotecardLine returns by bytes", "[ALNotecardFormat]")
    {
        const std::string one_line_too_long(ALNotecardFormat::READ_LINE_BYTES + 1, 'x');
        const std::string exactly(ALNotecardFormat::READ_LINE_BYTES, 'y');
        const std::vector<S32> past = ALNotecardFormat::linesPast("short\n" + one_line_too_long + "\n" + exactly + "\n" + one_line_too_long);
        UNSCOPED_INFO("the second and the fourth");
        CHECK(past == std::vector<S32>({ 1, 3 }));
        // 513 characters of two bytes each: 1026 bytes.
        std::string wide;
        for (int i = 0; i < 513; ++i)
        {
            wide += "\xC3\xA9";
        }
        UNSCOPED_INFO("by bytes, not characters");
        CHECK(ALNotecardFormat::linesPast(wide) == std::vector<S32>({ 0 }));
    }

    TEST_CASE_METHOD(alnotecardformat_data, "ALNotecardFormat: a JSON outline nests keys and places array objects and spans each value", "[ALNotecardFormat]")
    {
        const std::string text = "{\n"
                                 "  \"name\": \"Door\",\n"
                                 "  \"speed\": 2.5,\n"
                                 "  \"sounds\": { \"open\": \"creak\", \"shut\": \"bang\" },\n"
                                 "  \"stops\": [1, 2, { \"at\": 3 }],\n"
                                 "  \"on\": true\n"
                                 "}\n";
        const std::vector<ALScriptOutlineEntry> entries = ALNotecardFormat::outline(text);
        REQUIRE(said(entries) ==
                      std::string("name=\"Door\" speed=2.5 sounds={...} .open=\"creak\" .shut=\"bang\" stops=[...] .[2]={...} ..at=3 on=true"));
        const ALScriptOutlineEntry& sounds = entries[2];
        {
            INFO("sounds named where it is written");
            CHECK(sounds.nameSpan.line == 3);
            CHECK(sounds.nameSpan.column == 2);
            CHECK(sounds.nameSpan.endColumn == 10);
        }
        {
            INFO("and spanning its object");
            CHECK(sounds.span.line == 3);
            CHECK(sounds.span.endLine == 3);
            CHECK(sounds.span.endColumn == 47);
        }
        {
            INFO("a scalar spans its value");
            CHECK(entries[1].span.line == 2);
            CHECK(entries[1].span.endColumn == 14);
        }
    }

    TEST_CASE_METHOD(alnotecardformat_data, "ALNotecardFormat: a JSON outline of a top array or broken text or too many entries", "[ALNotecardFormat]")
    {
        UNSCOPED_INFO("an array's objects");
        CHECK(said(ALNotecardFormat::outline("[{\"a\": 1}, {\"b\": [ {\"c\": null} ]}]")) ==
                      std::string("[0]={...} .a=1 [1]={...} .b=[...] ..[0]={...} ...c=null"));
        UNSCOPED_INFO("broken off: as far as it goes");
        CHECK(said(ALNotecardFormat::outline("{\"a\": {\"b\": 1, \"c\"")) == std::string("a={...} .b=1"));
        UNSCOPED_INFO("not JSON at all: nothing");
        CHECK(said(ALNotecardFormat::outline("just words: here")) == std::string());
        UNSCOPED_INFO("no more than asked");
        REQUIRE(ALNotecardFormat::outline("{\"a\":1,\"b\":2,\"c\":3}", 2).size() == size_t(2));
    }

    TEST_CASE_METHOD(alnotecardformat_data, "ALNotecardFormat: namedAt finds the notecard named first in llGetNotecardLine and its kin", "[ALNotecardFormat]")
    {
        const std::string lsl = "    key q = llGetNotecardLine(\"config\", line);";
        std::optional<ALNotecardFormat::Named> named = ALNotecardFormat::namedAt(lsl, 34);
        {
            INFO("inside the string");
            REQUIRE(named);
            CHECK(named->name == "config");
            CHECK(named->begin == 30);
            CHECK(named->end == 38);
        }
        UNSCOPED_INFO("on its quote");
        REQUIRE(ALNotecardFormat::namedAt(lsl, 30).has_value());
        UNSCOPED_INFO("not on the call's name");
        REQUIRE_FALSE(ALNotecardFormat::namedAt(lsl, 16));
        named = ALNotecardFormat::namedAt("ll.GetNumberOfNotecardLines ( 'dialog' )", 32);
        {
            INFO("SLua's, single quotes, blanks about the bracket");
            REQUIRE(named);
            CHECK(named->name == "dialog");
        }
        named = ALNotecardFormat::namedAt("llGetNotecardLineSync(\"say \\\"hi\\\"\", 0)", 25);
        {
            INFO("an escape taken out");
            REQUIRE(named);
            CHECK(named->name == "say \"hi\"");
        }
        UNSCOPED_INFO("a string another call is given, not");
        REQUIRE_FALSE(ALNotecardFormat::namedAt("llSay(0, \"config\");", 12));
        UNSCOPED_INFO("the second argument, not");
        REQUIRE_FALSE(ALNotecardFormat::namedAt("llGetNotecardLine(name, \"x\")", 25));

        const std::string script = "default {\n"
                                   "  state_entry() { llGetNotecardLine(\"config\", 0); llSay(0, \"config\"); }\n"
                                   "  touch_start(integer n) { llGetNumberOfNotecardLines(\"config\"); llGetNotecardLine(\"other\", 0); }\n"
                                   "}\n";
        const std::vector<ALScriptSpan> read = ALNotecardFormat::readersOf(script, "config");
        REQUIRE(read.size() == size_t(2));
        CHECK(read[0].line == 1);
        CHECK(read[0].column == 36);
        CHECK(read[0].endColumn == 44);
        CHECK(read[1].line == 2);
        CHECK(read[1].column == 54);
        UNSCOPED_INFO("none of another");
        REQUIRE(ALNotecardFormat::readersOf(script, "missing").empty());
    }

    TEST_CASE_METHOD(alnotecardformat_data, "ALNotecardFormat: readersOf counts a lone CR as a line break as LF is and CRLF as one", "[ALNotecardFormat]")
    {
        const std::string script = "default {\r"
                                   "  state_entry() { llGetNotecardLine(\"config\", 0); }\r\n"
                                   "  touch_start(integer n) { llGetNumberOfNotecardLines(\"config\"); }\n"
                                   "}\r";
        const std::vector<ALScriptSpan> read = ALNotecardFormat::readersOf(script, "config");
        REQUIRE(read.size() == size_t(2));
        {
            INFO("the first on the second line");
            CHECK(read[0].line == 1);
            CHECK(read[0].column == 36);
            CHECK(read[0].endColumn == 44);
        }
        {
            INFO("the second on the third");
            CHECK(read[1].line == 2);
            CHECK(read[1].column == 54);
        }
    }
}
