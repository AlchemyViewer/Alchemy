/**
 * @file alsnippetsession_test.cpp
 * @brief A snippet or a call being filled in: its body read, its stops moving with the text, its mirrors.
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

#include "alsnippetsession.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct alsnippetsession_data
    {
        ALSnippetSession session;

        static ALTextRange range(S32 line, S32 from, S32 to) { return ALTextRange(ALTextPos(line, from), ALTextPos(line, to)); }
    };
    typedef test_group<alsnippetsession_data> alsnippetsession_group;
    typedef alsnippetsession_group::object    alsnippetsession_object;
    alsnippetsession_group                    alsnippetsession_instance("ALSnippetSession");

    template<> template<>
    void alsnippetsession_object::test<1>()
    {
        set_test_name("a body read: its stops in number order, a number again a mirror, its later lines indented, and where the caret lands");
        const ALSnippetSession::Expansion x = ALSnippetSession::expand("for (${1:i} = 0; $1 < ${2:n}; ++$1)\n{\n$0\n}", ALTextPos(0, 4), "    ");
        ensure_equals("the text", x.text, std::string("for (i = 0; i < n; ++i)\n    {\n    \n    }"));
        const ALTextDocument doc("    " + x.text);
        ensure("two stops", x.stops.size() == 2 && doc.text(x.stops[0]) == "i" && doc.text(x.stops[1]) == "n");
        ensure("two mirrors of the first", x.mirrors.size() == 2 && x.mirrors[0].of == 0 && x.mirrors[1].of == 0 && doc.text(x.mirrors[1].range) == "i");
        ensure("$0 on the indented line", x.landing == range(2, 4, 4));

        const ALSnippetSession::Expansion nested = ALSnippetSession::expand("${1:a ${2:b}} ${0:done}", ALTextPos(0, 0), "");
        const ALTextDocument                nd(nested.text);
        ensure("one inside another", nested.stops.size() == 2 && nd.text(nested.stops[0]) == "a b" && nd.text(nested.stops[1]) == "b");
        ensure("$0's text", nd.text(nested.landing) == "done");

        const ALSnippetSession::Expansion plain = ALSnippetSession::expand("\\$1 costs $$5 \\}", ALTextPos(0, 0), "");
        ensure_equals("escaped", plain.text, std::string("$1 costs $5 }"));
        ensure("no stops, landing at the end", plain.stops.empty() && plain.landing == range(0, 13, 13));
    }

    template<> template<>
    void alsnippetsession_object::test<2>()
    {
        set_test_name("the stop being typed over becomes what was typed; the rest move with the text; one an edit cuts into goes; none being typed over, it is over");
        ALTextDocument doc("f(a, b)\n");
        session.start({ range(0, 2, 3), range(0, 5, 6) }, ALTextPos(0, 7));
        ensure("the first being typed over", session.active() && session.at() == 0);
        session.slide(doc.replace(range(0, 2, 3), "alpha"));
        ensure("typed over", doc.text(session.stops()[0]) == "alpha" && doc.text(session.stops()[1]) == "b");
        ensure("the end moved along", session.after() == ALTextPos(0, 11));
        session.slide(doc.replace(range(0, 0, 0), "x"));
        ensure("moved with the text", doc.text(session.stops()[0]) == "alpha" && doc.text(session.stops()[1]) == "b");
        session.slide(doc.replace(range(0, 9, 11), ""));
        ensure("the one cut into goes", session.stops().size() == 1 && session.at() == 0);
        session.slide(doc.replace(range(0, 2, 4), ""));
        ensure("the one being typed over cut into from outside: over", !session.active() && session.at() == -1);
    }

    template<> template<>
    void alsnippetsession_object::test<3>()
    {
        set_test_name("mirrors: those that no longer read as their stop; brought up as one batch, as the editor does, each is what the batch put in its place");
        ALTextDocument doc("i = i + i;\n");
        session.start({ range(0, 0, 1) }, ALTextPos(0, 10), { { 0, range(0, 4, 5) }, { 0, range(0, 8, 9) } });
        session.slide(doc.replace(range(0, 0, 1), "count"));
        std::string            wanted;
        const std::vector<S32> stale = session.staleMirrors(0, doc, wanted);
        ensure_equals("what the stop holds", wanted, std::string("count"));
        ensure("both", stale == std::vector<S32>({ 0, 1 }));
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (const S32 k : stale)
        {
            edits.emplace_back(session.mirrors()[static_cast<size_t>(k)].range, wanted);
        }
        session.setSyncing(true);
        session.slide(doc.replaceMany(std::move(edits)));
        session.setSyncing(false);
        ensure("each brought up whole", doc.text(session.mirrors()[0].range) == "count" && doc.text(session.mirrors()[1].range) == "count");
        ensure_equals("the line", doc.line(0), std::string("count = count + count;"));
        ensure("none left", session.staleMirrors(0, doc, wanted).empty());
        ensure("none for another stop", session.staleMirrors(3, doc, wanted).empty());
    }

    template<> template<>
    void alsnippetsession_object::test<4>()
    {
        set_test_name("filled in from the first stop's line to where the call or the snippet ends");
        session.start({ range(1, 4, 5), range(3, 2, 3) }, ALTextPos(4, 0));
        ensure("above: no", !session.reaches(0));
        ensure("the first stop's line", session.reaches(1));
        ensure("between", session.reaches(2));
        ensure("where it ends", session.reaches(4));
        ensure("below: no", !session.reaches(5));
        session.clear();
        ensure("cleared", !session.active() && session.mirrors().empty() && session.landing() == 0);
    }

    template<> template<>
    void alsnippetsession_object::test<5>()
    {
        set_test_name("a body's own levels, four spaces or a tab, made again in the text's unit; what is left over as spaces; the first line as it is");
        const ALSnippetSession::Expansion tabs = ALSnippetSession::expand("f()\n{\n    a;\n        b;\n\tc;\n      d;\n}", ALTextPos(0, 1), "\t", "\t");
        ensure_equals("tabs a level, under the line's own", tabs.text, std::string("f()\n\t{\n\t\ta;\n\t\t\tb;\n\t\tc;\n\t\t  d;\n\t}"));
        const ALSnippetSession::Expansion two = ALSnippetSession::expand("if x then\n    $0\nend", ALTextPos(0, 0), "", "  ");
        ensure_equals("two spaces a level", two.text, std::string("if x then\n  \nend"));
        ensure("$0 where its level is now", two.landing == range(1, 2, 2));
        const ALSnippetSession::Expansion same = ALSnippetSession::expand("    lead\n    x", ALTextPos(0, 0), "");
        ensure_equals("no unit: as written", same.text, std::string("    lead\n    x"));
    }

    template<> template<>
    void alsnippetsession_object::test<6>()
    {
        set_test_name("a mirror that is a later stop, or inside one, made again as the batch the editor makes: the stop holds what went in, and is still there to go to");
        // Every stale mirror of a stop made again at once, as the editor does
        // on leaving it.
        const auto sync = [this](ALTextDocument& doc, S32 stop) {
            std::string                                      wanted;
            std::vector<std::pair<ALTextRange, std::string>> edits;
            for (const S32 k : session.staleMirrors(stop, doc, wanted))
            {
                edits.emplace_back(session.mirrors()[static_cast<size_t>(k)].range, wanted);
            }
            session.setSyncing(true);
            session.slide(doc.replaceMany(std::move(edits)));
            session.setSyncing(false);
        };

        const ALSnippetSession::Expansion whole = ALSnippetSession::expand("local ${1:name} = require(\"${2:$1}\")", ALTextPos(0, 0), "");
        ALTextDocument                    doc(whole.text);
        ensure("the second stop the first's mirror", whole.stops.size() == 2 && whole.mirrors.size() == 1 && whole.stops[1] == whole.mirrors[0].range);
        session.start(whole.stops, whole.landing.begin, whole.mirrors);
        session.slide(doc.replace(session.stops()[0], "json"));
        sync(doc, 0);
        ensure_equals("brought up", doc.text(), std::string("local json = require(\"json\")"));
        ensure("both stops kept", session.stops().size() == 2 && session.at() == 0);
        ensure_equals("the second over what its mirror holds now", doc.text(session.stops()[1]), std::string("json"));

        const ALSnippetSession::Expansion around = ALSnippetSession::expand("${1:x} ${2:the $1 thing}", ALTextPos(0, 0), "");
        ALTextDocument                    text(around.text);
        session.start(around.stops, around.landing.begin, around.mirrors);
        session.slide(text.replace(session.stops()[0], "abc"));
        sync(text, 0);
        ensure_equals("brought up inside", text.text(), std::string("abc the abc thing"));
        ensure("the stop around it kept", session.stops().size() == 2);
        ensure_equals("grown with it", text.text(session.stops()[1]), std::string("the abc thing"));
    }

    template<> template<>
    void alsnippetsession_object::test<7>()
    {
        set_test_name("a number's first place written bare, its default given after: every place shows the default, and leaving the stop takes nothing from its mirror");
        const ALSnippetSession::Expansion x = ALSnippetSession::expand("$1 = ${1:value};", ALTextPos(0, 0), "");
        ensure_equals("the default where it is first", x.text, std::string("value = value;"));
        ensure("the stop the first place, holding it", x.stops.size() == 1 && x.stops[0] == range(0, 0, 5));
        ensure("its mirror the second", x.mirrors.size() == 1 && x.mirrors[0].of == 0 && x.mirrors[0].range == range(0, 8, 13));
        const ALTextDocument doc(x.text);
        session.start(x.stops, x.landing.begin, x.mirrors);
        std::string wanted;
        ensure("nothing to bring up as the stop is left", session.staleMirrors(0, doc, wanted).empty() && wanted == "value");

        const ALSnippetSession::Expansion braced = ALSnippetSession::expand("${1} + $1 + ${1:n}", ALTextPos(0, 0), "");
        ensure_equals("braced without one, and a mirror between, the same", braced.text, std::string("n + n + n"));
        const ALSnippetSession::Expansion empty = ALSnippetSession::expand("${1:} + ${1:n}", ALTextPos(0, 0), "");
        ensure_equals("a first given an empty default keeps it", empty.text, std::string(" + n"));
    }
}
