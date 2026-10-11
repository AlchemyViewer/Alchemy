/**
 * @file altextfind_test.cpp
 * @brief What a find over a text found, kept in step with its edits.
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

#include "altextfind.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    struct altextfind_data
    {
        // The worker a long text is looked through on, closed and waited
        // for as each test ends: nothing of a test's own left running.
        ~altextfind_data() { ALTextFind::closeWorker(); }
    };
    typedef test_group<altextfind_data> altextfind_group;
    typedef altextfind_group::object    altextfind_object;
    altextfind_group                    altextfind_instance("altextfind");

    template<> template<>
    void altextfind_object::test<1>()
    {
        set_test_name("found, the current one the selection; slid with an edit, a new generation each time; in a selection kept to it as it grows");
        ALTextDocument doc("one two one\nthree one\n");
        ALTextFind     find;
        doc.onChanged([&find](const ALTextDocument::Edit& edit) { find.edited(edit); });
        const U32 was = find.generation();
        find.search(doc, "one", ALTextSearchOptions(), false, ALTextRange(ALTextPos(0, 8), ALTextPos(0, 11)));
        ensure_equals("three", find.count(), size_t(3));
        ensure_equals("the selection the current one", find.current(), 1);
        ensure("a generation on", find.generation() != was);
        const U32 found = find.generation();
        doc.insert(ALTextPos(0, 0), "x ");
        ensure("slid", find.matches()[0] == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 5)) && find.generation() != found);
        ensure_equals("the nearest after a place", find.nearest(ALTextPos(0, 6), true), 1);
        ensure_equals("round the end", find.nearest(ALTextPos(1, 9), true), 0);

        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 13)));
        ensure_equals("in the selection only", find.count(), size_t(2));
        doc.insert(ALTextPos(0, 5), " one");
        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange());
        ensure_equals("kept to it as it grew, not to the selection now", find.count(), size_t(3));
        find.search(doc, "one", ALTextSearchOptions(), false, ALTextRange());
        ensure_equals("let go of", find.count(), size_t(4));
    }

    template<> template<>
    void altextfind_object::test<2>()
    {
        set_test_name("no more than a list's worth; all of them for a replace; taken and put back; put away");
        std::string text;
        for (size_t i = 0; i < ALTextFind::LIMIT + 2; ++i)
        {
            text += "z\n";
        }
        const ALTextDocument doc(text);
        ALTextFind           find;
        find.search(doc, "z", ALTextSearchOptions(), false, ALTextRange());
        ensure("capped", find.capped() && find.count() == ALTextFind::LIMIT);
        ensure_equals("every one for a replace", find.replacements(doc, "z", ALTextSearchOptions(), "y").size(), ALTextFind::LIMIT + 2);
        std::vector<ALTextRange> taken = find.take();
        ensure("taken", find.count() == 0 && taken.size() == ALTextFind::LIMIT);
        find.restore(std::move(taken), 3);
        ensure("put back", find.count() == ALTextFind::LIMIT && find.current() == 3);
        find.stale();
        ensure("to be looked through again, not yet", find.isStale() && !find.due());
        find.clear();
        ensure("put away", find.count() == 0 && !find.isStale() && find.current() == -1);
    }

    template<> template<>
    void altextfind_object::test<3>()
    {
        set_test_name("a long text looked through on a worker, its matches taken as they come; of a text changed meanwhile, looked for again");
        std::string text;
        while (text.size() < ALTextFind::ON_A_WORKER + 1024)
        {
            text += "needle in a haystack of words\n";
        }
        ALTextDocument doc(text);
        ALTextFind     find;
        doc.onChanged([&find](const ALTextDocument::Edit& edit) { find.edited(edit); });
        find.search(doc, "needle", ALTextSearchOptions(), false, ALTextRange());
        ensure("on a worker", find.searching());
        ensure("taken, waited for", find.collect(doc, ALTextRange(), true) && !find.searching());
        const size_t lines = static_cast<size_t>(doc.lineCount() - 1);
        ensure_equals("every line's", find.count(), llmin(lines, ALTextFind::LIMIT));

        find.search(doc, "haystack", ALTextSearchOptions(), false, ALTextRange());
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "haystack ");
        ensure("of the text as it is now", find.collect(doc, ALTextRange(), true));
        ensure("the one typed meanwhile among them", !find.matches().empty() && find.matches()[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 8)));
        find.search(doc, "needle", ALTextSearchOptions(), false, ALTextRange());
        find.clear();
        ensure("put away with a worker out: let go of", !find.searching() && find.count() == 0);
    }

    template<> template<>
    void altextfind_object::test<4>()
    {
        set_test_name("a worker's matches of a text changed meanwhile, where a search is due with another query, are let go of rather than looked for again by the old one");
        std::string text;
        while (text.size() < ALTextFind::ON_A_WORKER + 1024)
        {
            text += "needle in a haystack of words\n";
        }
        ALTextDocument doc(text);
        ALTextFind     find;
        doc.onChanged([&find](const ALTextDocument::Edit& edit) { find.edited(edit); });
        find.search(doc, "needle", ALTextSearchOptions(), false, ALTextRange());
        ensure("on a worker", find.searching());
        // The text changes while it looks, and the query after it, which a
        // long text looks for once it settles.
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "x");
        find.stale();
        find.collect(doc, ALTextRange(), true);
        ensure("the search with the query as it is now still due", find.isStale());
        ensure("and the old one not looked for again meanwhile", !find.searching());

        // With nothing due, a changed text is looked through again as before.
        find.search(doc, "haystack", ALTextSearchOptions(), false, ALTextRange());
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "haystack ");
        ensure("looked for again", find.collect(doc, ALTextRange(), true) && !find.isStale());
        ensure("the one typed meanwhile among them", !find.matches().empty() && find.matches()[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 8)));
    }

    template<> template<>
    void altextfind_object::test<5>()
    {
        set_test_name("a text that changes too often to settle -- a log taking entries -- is looked through again all the same, once it has waited long enough");
        ALTextFind find;
        LLFrameTimer::updateFrameTime();
        find.stale();
        ensure("not at the change", !find.due());
        // A change every 20 ms, well within the settle, for up to 2 s.
        bool due = false;
        for (S32 step = 0; step < 100 && !due; ++step)
        {
            ms_sleep(20);
            LLFrameTimer::updateFrameTime();
            find.stale();
            due = find.due();
        }
        ensure("due though it never settled", due);

        // Looked through, the wait starts again at the next change.
        const ALTextDocument doc("x");
        find.search(doc, "x", ALTextSearchOptions(), false, ALTextRange());
        find.stale();
        ensure("not due at the next change", !find.due());
    }

    template<> template<>
    void altextfind_object::test<6>()
    {
        set_test_name("put away, a find in a selection lets go of the stretch it kept to: asked for again, it keeps to the selection then");
        const ALTextDocument doc("one two\none two\none two\n");
        ALTextFind           find;
        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 7)));
        ensure_equals("in the first line's selection", find.count(), size_t(1));
        find.clear();
        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange(ALTextPos(1, 0), ALTextPos(2, 7)));
        ensure_equals("in the selection made since", find.count(), size_t(2));
        ensure("on its lines", find.matches()[0].begin.line == 1 && find.matches()[1].begin.line == 2);
    }
}
