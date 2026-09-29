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

#include "../alnotecardformat.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
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
    typedef test_group<alnotecardformat_data> alnotecardformat_group;
    typedef alnotecardformat_group::object    alnotecardformat_object;
    alnotecardformat_group                    alnotecardformat_instance("ALNotecardFormat");

    template<> template<>
    void alnotecardformat_object::test<1>()
    {
        set_test_name("guessed: JSON by its brace or bracket; settings where most lines are keys or sections, two at least; plain text otherwise");
        ensure_equals("an object", ALNotecardFormat::guess("  \n{ \"a\": 1 }\n"), std::string("json"));
        ensure_equals("an array", ALNotecardFormat::guess("[1, 2]"), std::string("json"));
        ensure_equals("settings", ALNotecardFormat::guess("# the door\n[Door]\nspeed = 2\nsound: creak\n\nlocked=false\n"), std::string("config"));
        ensure_equals("one key is not enough", ALNotecardFormat::guess("title: A Story\nOnce upon a time.\nThe end.\n"), std::string("text"));
        ensure_equals("prose with a colon or two", ALNotecardFormat::guess("Note: read this.\nWelcome to the store\nAsk for help\nHave fun\n"),
                      std::string("text"));
        ensure_equals("nothing", ALNotecardFormat::guess(""), std::string("text"));
    }

    template<> template<>
    void alnotecardformat_object::test<2>()
    {
        set_test_name("the lines, counted from 0, longer than llGetNotecardLine returns: by bytes");
        const std::string one_line_too_long(ALNotecardFormat::READ_LINE_BYTES + 1, 'x');
        const std::string exactly(ALNotecardFormat::READ_LINE_BYTES, 'y');
        const std::vector<S32> past = ALNotecardFormat::linesPast("short\n" + one_line_too_long + "\n" + exactly + "\n" + one_line_too_long);
        ensure("the second and the fourth", past == std::vector<S32>({ 1, 3 }));
        // 513 characters of two bytes each: 1026 bytes.
        std::string wide;
        for (int i = 0; i < 513; ++i)
        {
            wide += "\xC3\xA9";
        }
        ensure("by bytes, not characters", ALNotecardFormat::linesPast(wide) == std::vector<S32>({ 0 }));
    }

    template<> template<>
    void alnotecardformat_object::test<3>()
    {
        set_test_name("a JSON outline: every key nested under the one it is in, an array's objects by their places, each spanning its value");
        const std::string text = "{\n"
                                 "  \"name\": \"Door\",\n"
                                 "  \"speed\": 2.5,\n"
                                 "  \"sounds\": { \"open\": \"creak\", \"shut\": \"bang\" },\n"
                                 "  \"stops\": [1, 2, { \"at\": 3 }],\n"
                                 "  \"on\": true\n"
                                 "}\n";
        const std::vector<ALScriptOutlineEntry> entries = ALNotecardFormat::outline(text);
        ensure_equals("the keys", said(entries),
                      std::string("name=\"Door\" speed=2.5 sounds={...} .open=\"creak\" .shut=\"bang\" stops=[...] .[2]={...} ..at=3 on=true"));
        const ALScriptOutlineEntry& sounds = entries[2];
        ensure("sounds named where it is written", sounds.nameSpan.line == 3 && sounds.nameSpan.column == 2 && sounds.nameSpan.endColumn == 10);
        ensure("and spanning its object", sounds.span.line == 3 && sounds.span.endLine == 3 && sounds.span.endColumn == 47);
        ensure("a scalar spans its value", entries[1].span.line == 2 && entries[1].span.endColumn == 14);
    }

    template<> template<>
    void alnotecardformat_object::test<4>()
    {
        set_test_name("a JSON outline of an array at the top, of text that is not JSON all through, and of more entries than asked for");
        ensure_equals("an array's objects", said(ALNotecardFormat::outline("[{\"a\": 1}, {\"b\": [ {\"c\": null} ]}]")),
                      std::string("[0]={...} .a=1 [1]={...} .b=[...] ..[0]={...} ...c=null"));
        ensure_equals("broken off: as far as it goes", said(ALNotecardFormat::outline("{\"a\": {\"b\": 1, \"c\"")), std::string("a={...} .b=1"));
        ensure_equals("not JSON at all: nothing", said(ALNotecardFormat::outline("just words: here")), std::string());
        ensure_equals("no more than asked", ALNotecardFormat::outline("{\"a\":1,\"b\":2,\"c\":3}", 2).size(), size_t(2));
    }

    template<> template<>
    void alnotecardformat_object::test<5>()
    {
        set_test_name("a notecard a script names where it reads one: the string first in llGetNotecardLine and its kin, LSL's or SLua's, in either quote");
        const std::string lsl = "    key q = llGetNotecardLine(\"config\", line);";
        std::optional<ALNotecardFormat::Named> named = ALNotecardFormat::namedAt(lsl, 34);
        ensure("inside the string", named && named->name == "config" && named->begin == 30 && named->end == 38);
        ensure("on its quote", ALNotecardFormat::namedAt(lsl, 30).has_value());
        ensure("not on the call's name", !ALNotecardFormat::namedAt(lsl, 16));
        ensure("SLua's, single quotes, blanks about the bracket", ALNotecardFormat::namedAt("ll.GetNumberOfNotecardLines ( 'dialog' )", 32)->name == "dialog");
        ensure("an escape taken out", ALNotecardFormat::namedAt("llGetNotecardLineSync(\"say \\\"hi\\\"\", 0)", 25)->name == "say \"hi\"");
        ensure("a string another call is given, not", !ALNotecardFormat::namedAt("llSay(0, \"config\");", 12));
        ensure("the second argument, not", !ALNotecardFormat::namedAt("llGetNotecardLine(name, \"x\")", 25));

        const std::string script = "default {\n"
                                   "  state_entry() { llGetNotecardLine(\"config\", 0); llSay(0, \"config\"); }\n"
                                   "  touch_start(integer n) { llGetNumberOfNotecardLines(\"config\"); llGetNotecardLine(\"other\", 0); }\n"
                                   "}\n";
        const std::vector<ALScriptSpan> read = ALNotecardFormat::readersOf(script, "config");
        ensure_equals("two readers of it", read.size(), size_t(2));
        ensure("the first", read[0].line == 1 && read[0].column == 36 && read[0].endColumn == 44);
        ensure("the second", read[1].line == 2 && read[1].column == 54);
        ensure("none of another", ALNotecardFormat::readersOf(script, "missing").empty());
    }
}
