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

#include "../alsnippetsession.h"

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
        set_test_name("mirrors: those that no longer read as their stop, the last first; the one being brought up is what the edit put in");
        ALTextDocument doc("i = i + i;\n");
        session.start({ range(0, 0, 1) }, ALTextPos(0, 10), { { 0, range(0, 4, 5) }, { 0, range(0, 8, 9) } });
        session.slide(doc.replace(range(0, 0, 1), "count"));
        std::string            wanted;
        const std::vector<S32> stale = session.staleMirrors(0, doc, wanted);
        ensure_equals("what the stop holds", wanted, std::string("count"));
        ensure("both, the last in the text first", stale.size() == 2 && stale[0] == 1 && stale[1] == 0);
        session.syncing(1);
        session.slide(doc.replace(session.mirrors()[1].range, wanted));
        session.syncing(-1);
        ensure("brought up whole", doc.text(session.mirrors()[1].range) == "count");
        ensure("one left", session.staleMirrors(0, doc, wanted).size() == 1);
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
}
