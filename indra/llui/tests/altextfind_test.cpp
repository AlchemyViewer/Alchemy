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

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{
    struct altextfind_data
    {
        // The worker a long text is looked through on, closed and waited
        // for as each test ends: nothing of a test's own left running.
        ~altextfind_data() { ALTextFind::closeWorker(); }
    };

    TEST_CASE_METHOD(altextfind_data, "altextfind: matches slide with edits and a find in a selection keeps to its stretch", "[altextfind]")
    {
        ALTextDocument doc("one two one\nthree one\n");
        ALTextFind     find;
        doc.onChanged([&find](const ALTextDocument::Edit& edit) { find.edited(edit); });
        const U32 was = find.generation();
        find.search(doc, "one", ALTextSearchOptions(), false, ALTextRange(ALTextPos(0, 8), ALTextPos(0, 11)));
        REQUIRE(find.count() == size_t(3));
        UNSCOPED_INFO("the selection the current one");
        CHECK(find.current() == 1);
        UNSCOPED_INFO("a generation on");
        CHECK(find.generation() != was);
        const U32 found = find.generation();
        doc.insert(ALTextPos(0, 0), "x ");
        {
            INFO("slid");
            CHECK(find.matches()[0] == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 5)));
            CHECK(find.generation() != found);
        }
        UNSCOPED_INFO("the nearest after a place");
        CHECK(find.nearest(ALTextPos(0, 6), true) == 1);
        UNSCOPED_INFO("round the end");
        CHECK(find.nearest(ALTextPos(1, 9), true) == 0);

        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 13)));
        UNSCOPED_INFO("in the selection only");
        CHECK(find.count() == size_t(2));
        doc.insert(ALTextPos(0, 5), " one");
        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange());
        UNSCOPED_INFO("kept to it as it grew, not to the selection now");
        CHECK(find.count() == size_t(3));
        find.search(doc, "one", ALTextSearchOptions(), false, ALTextRange());
        UNSCOPED_INFO("let go of");
        CHECK(find.count() == size_t(4));
    }

    TEST_CASE_METHOD(altextfind_data, "altextfind: matches stop at LIMIT but not for a replace and can be taken back", "[altextfind]")
    {
        std::string text;
        for (size_t i = 0; i < ALTextFind::LIMIT + 2; ++i)
        {
            text += "z\n";
        }
        const ALTextDocument doc(text);
        ALTextFind           find;
        find.search(doc, "z", ALTextSearchOptions(), false, ALTextRange());
        {
            INFO("capped");
            CHECK(find.capped());
            CHECK(find.count() == ALTextFind::LIMIT);
        }
        UNSCOPED_INFO("every one for a replace");
        CHECK(find.replacements(doc, "z", ALTextSearchOptions(), "y").size() == ALTextFind::LIMIT + 2);
        std::vector<ALTextRange> taken = find.take();
        {
            INFO("taken");
            CHECK(find.count() == 0);
            CHECK(taken.size() == ALTextFind::LIMIT);
        }
        find.restore(std::move(taken), 3);
        {
            INFO("put back");
            CHECK(find.count() == ALTextFind::LIMIT);
            CHECK(find.current() == 3);
        }
        find.stale();
        {
            INFO("to be looked through again, not yet");
            CHECK(find.isStale());
            CHECK_FALSE(find.due());
        }
        find.clear();
        {
            INFO("put away");
            CHECK(find.count() == 0);
            CHECK_FALSE(find.isStale());
            CHECK(find.current() == -1);
        }
    }

    TEST_CASE_METHOD(altextfind_data, "altextfind: a long text is searched on a worker and again if it changed meanwhile", "[altextfind]")
    {
        std::string text;
        while (text.size() < ALTextFind::ON_A_WORKER + 1024)
        {
            text += "needle in a haystack of words\n";
        }
        ALTextDocument doc(text);
        ALTextFind     find;
        doc.onChanged([&find](const ALTextDocument::Edit& edit) { find.edited(edit); });
        find.search(doc, "needle", ALTextSearchOptions(), false, ALTextRange());
        UNSCOPED_INFO("on a worker");
        CHECK(find.searching());
        {
            INFO("taken, waited for");
            CHECK(find.collect(doc, ALTextRange(), true));
            CHECK_FALSE(find.searching());
        }
        const size_t lines = static_cast<size_t>(doc.lineCount() - 1);
        UNSCOPED_INFO("every line's");
        CHECK(find.count() == llmin(lines, ALTextFind::LIMIT));

        find.search(doc, "haystack", ALTextSearchOptions(), false, ALTextRange());
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "haystack ");
        UNSCOPED_INFO("of the text as it is now");
        CHECK(find.collect(doc, ALTextRange(), true));
        {
            INFO("the one typed meanwhile among them");
            REQUIRE_FALSE(find.matches().empty());
            CHECK(find.matches()[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 8)));
        }
        find.search(doc, "needle", ALTextSearchOptions(), false, ALTextRange());
        find.clear();
        {
            INFO("put away with a worker out: let go of");
            CHECK_FALSE(find.searching());
            CHECK(find.count() == 0);
        }
    }

    TEST_CASE_METHOD(altextfind_data, "altextfind: a worker's stale matches are let go when a search for a new query is due", "[altextfind]")
    {
        std::string text;
        while (text.size() < ALTextFind::ON_A_WORKER + 1024)
        {
            text += "needle in a haystack of words\n";
        }
        ALTextDocument doc(text);
        ALTextFind     find;
        doc.onChanged([&find](const ALTextDocument::Edit& edit) { find.edited(edit); });
        find.search(doc, "needle", ALTextSearchOptions(), false, ALTextRange());
        UNSCOPED_INFO("on a worker");
        CHECK(find.searching());
        // The text changes while it looks, and the query after it, which a
        // long text looks for once it settles.
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "x");
        find.stale();
        find.collect(doc, ALTextRange(), true);
        UNSCOPED_INFO("the search with the query as it is now still due");
        CHECK(find.isStale());
        UNSCOPED_INFO("and the old one not looked for again meanwhile");
        CHECK_FALSE(find.searching());

        // With nothing due, a changed text is looked through again as before.
        find.search(doc, "haystack", ALTextSearchOptions(), false, ALTextRange());
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "haystack ");
        {
            INFO("looked for again");
            CHECK(find.collect(doc, ALTextRange(), true));
            CHECK_FALSE(find.isStale());
        }
        {
            INFO("the one typed meanwhile among them");
            REQUIRE_FALSE(find.matches().empty());
            CHECK(find.matches()[0] == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 8)));
        }
    }

    TEST_CASE_METHOD(altextfind_data, "altextfind: a text that never settles is searched again after waiting long enough", "[altextfind]")
    {
        ALTextFind find;
        LLFrameTimer::updateFrameTime();
        find.stale();
        UNSCOPED_INFO("not at the change");
        CHECK_FALSE(find.due());
        // A change every 20 ms, well within the settle, for up to 2 s.
        bool due = false;
        for (S32 step = 0; step < 100 && !due; ++step)
        {
            ms_sleep(20);
            LLFrameTimer::updateFrameTime();
            find.stale();
            due = find.due();
        }
        UNSCOPED_INFO("due though it never settled");
        CHECK(due);

        // Looked through, the wait starts again at the next change.
        const ALTextDocument doc("x");
        find.search(doc, "x", ALTextSearchOptions(), false, ALTextRange());
        find.stale();
        UNSCOPED_INFO("not due at the next change");
        CHECK_FALSE(find.due());
    }

    TEST_CASE_METHOD(altextfind_data, "altextfind: a find in a selection cleared and asked again keeps to the new selection", "[altextfind]")
    {
        const ALTextDocument doc("one two\none two\none two\n");
        ALTextFind           find;
        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange(ALTextPos(0, 0), ALTextPos(0, 7)));
        UNSCOPED_INFO("in the first line's selection");
        CHECK(find.count() == size_t(1));
        find.clear();
        find.search(doc, "one", ALTextSearchOptions(), true, ALTextRange(ALTextPos(1, 0), ALTextPos(2, 7)));
        UNSCOPED_INFO("in the selection made since");
        REQUIRE(find.count() == size_t(2));
        {
            INFO("on its lines");
            CHECK(find.matches()[0].begin.line == 1);
            CHECK(find.matches()[1].begin.line == 2);
        }
    }
}
