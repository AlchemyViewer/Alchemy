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

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct alsnippetsession_data
    {
        ALSnippetSession session;

        static ALTextRange range(S32 line, S32 from, S32 to) { return ALTextRange(ALTextPos(line, from), ALTextPos(line, to)); }
    };

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: expand reads stops and mirrors and escapes and indents a body", "[ALSnippetSession]")
    {
        const ALSnippetSession::Expansion x = ALSnippetSession::expand("for (${1:i} = 0; $1 < ${2:n}; ++$1)\n{\n$0\n}", ALTextPos(0, 4), "    ");
        CHECK(x.text == std::string("for (i = 0; i < n; ++i)\n    {\n    \n    }"));
        const ALTextDocument doc("    " + x.text);
        REQUIRE(x.stops.size() == 2);
        CHECK(doc.text(x.stops[0]) == "i");
        CHECK(doc.text(x.stops[1]) == "n");
        REQUIRE(x.mirrors.size() == 2);
        CHECK(x.mirrors[0].of == 0);
        CHECK(x.mirrors[1].of == 0);
        CHECK(doc.text(x.mirrors[1].range) == "i");
        UNSCOPED_INFO("$0 on the indented line");
        CHECK(x.landing == range(2, 4, 4));

        const ALSnippetSession::Expansion nested = ALSnippetSession::expand("${1:a ${2:b}} ${0:done}", ALTextPos(0, 0), "");
        const ALTextDocument                nd(nested.text);
        {
            INFO("one inside another");
            REQUIRE(nested.stops.size() == 2);
            CHECK(nd.text(nested.stops[0]) == "a b");
            CHECK(nd.text(nested.stops[1]) == "b");
        }
        UNSCOPED_INFO("$0's text");
        CHECK(nd.text(nested.landing) == "done");

        const ALSnippetSession::Expansion plain = ALSnippetSession::expand("\\$1 costs $$5 \\}", ALTextPos(0, 0), "");
        UNSCOPED_INFO("escaped");
        CHECK(plain.text == std::string("$1 costs $5 }"));
        CHECK(plain.stops.empty());
        CHECK(plain.landing == range(0, 13, 13));
    }

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: stops follow edits and one an edit cuts into is dropped", "[ALSnippetSession]")
    {
        ALTextDocument doc("f(a, b)\n");
        session.start({ range(0, 2, 3), range(0, 5, 6) }, ALTextPos(0, 7));
        {
            INFO("the first being typed over");
            CHECK(session.active());
            CHECK(session.at() == 0);
        }
        session.slide(doc.replace(range(0, 2, 3), "alpha"));
        {
            INFO("typed over");
            REQUIRE(session.stops().size() == 2);
            CHECK(doc.text(session.stops()[0]) == "alpha");
            CHECK(doc.text(session.stops()[1]) == "b");
        }
        UNSCOPED_INFO("the end moved along");
        CHECK(session.after() == ALTextPos(0, 11));
        session.slide(doc.replace(range(0, 0, 0), "x"));
        {
            INFO("moved with the text");
            REQUIRE(session.stops().size() == 2);
            CHECK(doc.text(session.stops()[0]) == "alpha");
            CHECK(doc.text(session.stops()[1]) == "b");
        }
        session.slide(doc.replace(range(0, 9, 11), ""));
        {
            INFO("the one cut into goes");
            CHECK(session.stops().size() == 1);
            CHECK(session.at() == 0);
        }
        session.slide(doc.replace(range(0, 2, 4), ""));
        {
            INFO("the one being typed over cut into from outside: over");
            CHECK_FALSE(session.active());
            CHECK(session.at() == -1);
        }
    }

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: staleMirrors lists out-of-date mirrors and one batch fixes them", "[ALSnippetSession]")
    {
        ALTextDocument doc("i = i + i;\n");
        session.start({ range(0, 0, 1) }, ALTextPos(0, 10), { { 0, range(0, 4, 5) }, { 0, range(0, 8, 9) } });
        session.slide(doc.replace(range(0, 0, 1), "count"));
        std::string            wanted;
        const std::vector<S32> stale = session.staleMirrors(0, doc, wanted);
        UNSCOPED_INFO("what the stop holds");
        CHECK(wanted == std::string("count"));
        UNSCOPED_INFO("both mirrors are stale");
        CHECK(stale == std::vector<S32>({ 0, 1 }));
        std::vector<std::pair<ALTextRange, std::string>> edits;
        for (const S32 k : stale)
        {
            edits.emplace_back(session.mirrors()[static_cast<size_t>(k)].range, wanted);
        }
        session.setSyncing(true);
        session.slide(doc.replaceMany(std::move(edits)));
        session.setSyncing(false);
        {
            INFO("each brought up whole");
            REQUIRE(session.mirrors().size() == 2);
            CHECK(doc.text(session.mirrors()[0].range) == "count");
            CHECK(doc.text(session.mirrors()[1].range) == "count");
        }
        CHECK(doc.line(0) == std::string("count = count + count;"));
        CHECK(session.staleMirrors(0, doc, wanted).empty());
        UNSCOPED_INFO("none for another stop");
        CHECK(session.staleMirrors(3, doc, wanted).empty());
    }

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: reaches spans from the first stop's line to where it ends", "[ALSnippetSession]")
    {
        session.start({ range(1, 4, 5), range(3, 2, 3) }, ALTextPos(4, 0));
        UNSCOPED_INFO("above: no");
        CHECK_FALSE(session.reaches(0));
        UNSCOPED_INFO("the first stop's line");
        CHECK(session.reaches(1));
        UNSCOPED_INFO("between");
        CHECK(session.reaches(2));
        UNSCOPED_INFO("where it ends");
        CHECK(session.reaches(4));
        UNSCOPED_INFO("below: no");
        CHECK_FALSE(session.reaches(5));
        session.clear();
        {
            INFO("cleared");
            CHECK_FALSE(session.active());
            CHECK(session.mirrors().empty());
            CHECK(session.landing() == 0);
        }
    }

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: a body's indent levels are redone in the text's own unit", "[ALSnippetSession]")
    {
        const ALSnippetSession::Expansion tabs = ALSnippetSession::expand("f()\n{\n    a;\n        b;\n\tc;\n      d;\n}", ALTextPos(0, 1), "\t", "\t");
        UNSCOPED_INFO("tabs a level, under the line's own");
        CHECK(tabs.text == std::string("f()\n\t{\n\t\ta;\n\t\t\tb;\n\t\tc;\n\t\t  d;\n\t}"));
        const ALSnippetSession::Expansion two = ALSnippetSession::expand("if x then\n    $0\nend", ALTextPos(0, 0), "", "  ");
        UNSCOPED_INFO("two spaces a level");
        CHECK(two.text == std::string("if x then\n  \nend"));
        UNSCOPED_INFO("$0 where its level is now");
        CHECK(two.landing == range(1, 2, 2));
        const ALSnippetSession::Expansion same = ALSnippetSession::expand("    lead\n    x", ALTextPos(0, 0), "");
        UNSCOPED_INFO("no unit: as written");
        CHECK(same.text == std::string("    lead\n    x"));
    }

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: syncing a mirror that is or sits in a later stop keeps that stop", "[ALSnippetSession]")
    {
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
        {
            INFO("the second stop the first's mirror");
            REQUIRE(whole.stops.size() == 2);
            REQUIRE(whole.mirrors.size() == 1);
            CHECK(whole.stops[1] == whole.mirrors[0].range);
        }
        session.start(whole.stops, whole.landing.begin, whole.mirrors);
        REQUIRE(session.stops().size() == 2);
        session.slide(doc.replace(session.stops()[0], "json"));
        sync(doc, 0);
        CHECK(doc.text() == std::string("local json = require(\"json\")"));
        {
            INFO("both stops kept");
            REQUIRE(session.stops().size() == 2);
            CHECK(session.at() == 0);
        }
        UNSCOPED_INFO("the second over what its mirror holds now");
        CHECK(doc.text(session.stops()[1]) == std::string("json"));

        const ALSnippetSession::Expansion around = ALSnippetSession::expand("${1:x} ${2:the $1 thing}", ALTextPos(0, 0), "");
        ALTextDocument                    text(around.text);
        REQUIRE(around.stops.size() == 2);
        session.start(around.stops, around.landing.begin, around.mirrors);
        session.slide(text.replace(session.stops()[0], "abc"));
        sync(text, 0);
        CHECK(text.text() == std::string("abc the abc thing"));
        UNSCOPED_INFO("the stop around it kept");
        REQUIRE(session.stops().size() == 2);
        UNSCOPED_INFO("grown with it");
        CHECK(text.text(session.stops()[1]) == std::string("the abc thing"));
    }

    TEST_CASE_METHOD(alsnippetsession_data, "ALSnippetSession: a default given after a bare first place fills every place", "[ALSnippetSession]")
    {
        const ALSnippetSession::Expansion x = ALSnippetSession::expand("$1 = ${1:value};", ALTextPos(0, 0), "");
        UNSCOPED_INFO("the default where it is first");
        CHECK(x.text == std::string("value = value;"));
        {
            INFO("the stop the first place, holding it");
            REQUIRE(x.stops.size() == 1);
            CHECK(x.stops[0] == range(0, 0, 5));
        }
        {
            INFO("its mirror the second");
            REQUIRE(x.mirrors.size() == 1);
            CHECK(x.mirrors[0].of == 0);
            CHECK(x.mirrors[0].range == range(0, 8, 13));
        }
        const ALTextDocument doc(x.text);
        session.start(x.stops, x.landing.begin, x.mirrors);
        std::string wanted;
        {
            INFO("nothing to bring up as the stop is left");
            CHECK(session.staleMirrors(0, doc, wanted).empty());
            CHECK(wanted == "value");
        }

        const ALSnippetSession::Expansion braced = ALSnippetSession::expand("${1} + $1 + ${1:n}", ALTextPos(0, 0), "");
        UNSCOPED_INFO("braced without one, and a mirror between, the same");
        CHECK(braced.text == std::string("n + n + n"));
        const ALSnippetSession::Expansion empty = ALSnippetSession::expand("${1:} + ${1:n}", ALTextPos(0, 0), "");
        UNSCOPED_INFO("a first given an empty default keeps it");
        CHECK(empty.text == std::string(" + n"));
    }
}
