/**
 * @file tests/altextdocument_test.cpp
 * @brief The document behind the text view.
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

#include "altextdocument.h"

#include "altextchars.h"
#include "llstring.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct altextdocument_data
    {
        std::vector<ALTextDocument::Edit> heard;

        void listen(ALTextDocument& doc)
        {
            doc.onChanged([this](const ALTextDocument::Edit& edit) { heard.push_back(edit); });
        }
    };

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: an empty document is one empty line", "[altextdocument]")
    {
        ALTextDocument doc;
        CHECK(doc.lineCount() == 1);
        CHECK(doc.empty());
        CHECK(doc.text() == std::string());
        CHECK(doc.byteCount() == size_t(0));
        CHECK(doc.end() == doc.start());
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: line endings arrive as anything and leave as LF", "[altextdocument]")
    {
        ALTextDocument doc("one\r\ntwo\rthree\nfour");
        CHECK(doc.lineCount() == 4);
        CHECK(doc.line(1) == std::string("two"));
        CHECK(doc.line(2) == std::string("three"));
        CHECK(doc.text() == std::string("one\ntwo\nthree\nfour"));
        CHECK(doc.byteCount() == doc.text().size());
        CHECK(doc.end() == ALTextPos(3, 4));
        UNSCOPED_INFO("a line past the end is empty");
        CHECK(doc.line(9) == std::string());
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: an insert in a line is one edit heard once", "[altextdocument]")
    {
        ALTextDocument doc("hello world");
        listen(doc);
        const U32 before = doc.version();
        ALTextDocument::Edit edit = doc.insert(ALTextPos(0, 5), ",");
        CHECK(doc.text() == std::string("hello, world"));
        UNSCOPED_INFO("range is where it went");
        CHECK(edit.range == ALTextRange(ALTextPos(0, 5), ALTextPos(0, 5)));
        CHECK(edit.removed == std::string());
        CHECK(edit.inserted == std::string(","));
        UNSCOPED_INFO("ends after the comma");
        CHECK(edit.endAfter() == ALTextPos(0, 6));
        CHECK(doc.version() > before);
        REQUIRE(heard.size() == size_t(1));
        UNSCOPED_INFO("heard the same");
        CHECK(heard.front().inserted == std::string(","));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: breaks put in split a line and taken out join it and inverses undo both", "[altextdocument]")
    {
        ALTextDocument doc("abcd");
        ALTextDocument::Edit split = doc.insert(ALTextPos(0, 2), "1\n22\n");
        CHECK(doc.lineCount() == 3);
        CHECK(doc.text() == std::string("ab1\n22\ncd"));
        UNSCOPED_INFO("ends at the start of the last line");
        CHECK(split.endAfter() == ALTextPos(2, 0));
        CHECK(split.rangeAfter() == ALTextRange(ALTextPos(0, 2), ALTextPos(2, 0)));

        ALTextDocument::Edit joined = doc.remove(ALTextRange(ALTextPos(0, 3), ALTextPos(2, 1)));
        UNSCOPED_INFO("joined");
        CHECK(doc.text() == std::string("ab1d"));
        UNSCOPED_INFO("removed what spanned the lines");
        CHECK(joined.removed == std::string("\n22\nc"));

        doc.replace(joined.inverse().range, joined.inverse().inserted);
        UNSCOPED_INFO("put back");
        CHECK(doc.text() == std::string("ab1\n22\ncd"));
        doc.replace(split.inverse().range, split.inverse().inserted);
        UNSCOPED_INFO("and back again");
        CHECK(doc.text() == std::string("abcd"));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: a replacement over several lines takes a range given backwards", "[altextdocument]")
    {
        ALTextDocument doc("one\ntwo\nthree\nfour");
        ALTextDocument::Edit edit = doc.replace(ALTextRange(ALTextPos(2, 3), ALTextPos(0, 2)), "X\nY");
        CHECK(doc.text() == std::string("onX\nYee\nfour"));
        UNSCOPED_INFO("range normalised");
        CHECK(edit.range == ALTextRange(ALTextPos(0, 2), ALTextPos(2, 3)));
        CHECK(edit.removed == std::string("e\ntwo\nthr"));
        CHECK(doc.text(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 2))) == std::string("nX\nYe"));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: an edit that changes nothing moves nothing", "[altextdocument]")
    {
        ALTextDocument doc("abc");
        listen(doc);
        const U32 before = doc.version();
        ALTextDocument::Edit edit = doc.remove(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)));
        CHECK(edit.nothing());
        CHECK(doc.version() == before);
        UNSCOPED_INFO("nobody told");
        CHECK(heard.empty());
        const ALTextDocument::Edit past = doc.insert(ALTextPos(7, 7), "");
        CHECK(past.nothing());
        UNSCOPED_INFO("answered where it was asked for, clamped to the end");
        CHECK(past.range == ALTextRange(ALTextPos(0, 3), ALTextPos(0, 3)));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: offsets and positions agree and the whole text kept follows edits", "[altextdocument]")
    {
        ALTextDocument doc("ab\n\ncde");
        CHECK(doc.offsetOf(ALTextPos(0, 0)) == size_t(0));
        UNSCOPED_INFO("past the first break");
        CHECK(doc.offsetOf(ALTextPos(1, 0)) == size_t(3));
        UNSCOPED_INFO("into the last");
        CHECK(doc.offsetOf(ALTextPos(2, 2)) == size_t(6));
        CHECK(doc.posAt(6) == ALTextPos(2, 2));
        UNSCOPED_INFO("the empty line");
        CHECK(doc.posAt(3) == ALTextPos(1, 0));
        UNSCOPED_INFO("a break's own offset is the line end before it");
        CHECK(doc.posAt(2) == ALTextPos(0, 2));
        UNSCOPED_INFO("past everything is the end");
        CHECK(doc.posAt(99) == doc.end());
        // The whole text is kept between edits and follows them.
        CHECK(doc.wholeText() == std::string("ab\n\ncde"));
        CHECK(doc.lineStarts() == std::vector<size_t>({ 0, 3, 4 }));
        doc.insert(ALTextPos(0, 2), "X\nY");
        UNSCOPED_INFO("the whole after an edit");
        CHECK(doc.wholeText() == std::string("abX\nY\n\ncde"));
        UNSCOPED_INFO("the starts after it");
        CHECK(doc.lineStarts() == std::vector<size_t>({ 0, 4, 6, 7 }));
        {
            INFO("and the offsets");
            CHECK(doc.offsetOf(ALTextPos(3, 1)) == 8);
            CHECK(doc.posAt(8) == ALTextPos(3, 1));
        }
        // Patched in place, whatever the edit's shape: lines taken out,
        // put in, and the end of the text.
        doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(2, 0)), "");
        UNSCOPED_INFO("lines taken out");
        CHECK(doc.wholeText() == std::string("a\ncde"));
        UNSCOPED_INFO("the starts follow");
        CHECK(doc.lineStarts() == std::vector<size_t>({ 0, 2 }));
        doc.append("\nf");
        UNSCOPED_INFO("appended");
        CHECK(doc.wholeText() == std::string("a\ncde\nf"));
        UNSCOPED_INFO("the starts again");
        CHECK(doc.lineStarts() == std::vector<size_t>({ 0, 2, 6 }));
        UNSCOPED_INFO("as made afresh");
        CHECK(doc.wholeText() == doc.text());
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: a combining mark stays with its base and clusters step across lines", "[altextdocument]")
    {
        // e + combining acute, then z.
        ALTextDocument doc("e\xCC\x81z\nq");
        UNSCOPED_INFO("next from the start clears the cluster");
        CHECK(doc.nextCluster(ALTextPos(0, 0)) == ALTextPos(0, 3));
        UNSCOPED_INFO("clamp inside the cluster goes back");
        CHECK(doc.clamp(ALTextPos(0, 2)) == ALTextPos(0, 0));
        UNSCOPED_INFO("prev from after z");
        CHECK(doc.prevCluster(ALTextPos(0, 4)) == ALTextPos(0, 3));
        UNSCOPED_INFO("prev from the cluster's end");
        CHECK(doc.prevCluster(ALTextPos(0, 3)) == ALTextPos(0, 0));
        UNSCOPED_INFO("next at a line end is the next line");
        CHECK(doc.nextCluster(ALTextPos(0, 4)) == ALTextPos(1, 0));
        UNSCOPED_INFO("prev at a line start is the line before's end");
        CHECK(doc.prevCluster(ALTextPos(1, 0)) == ALTextPos(0, 4));
        UNSCOPED_INFO("nowhere past the end");
        CHECK(doc.nextCluster(ALTextPos(1, 1)) == ALTextPos(1, 1));
        UNSCOPED_INFO("nowhere before the start");
        CHECK(doc.prevCluster(ALTextPos(0, 0)) == ALTextPos(0, 0));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: word steps go along a line and over its end", "[altextdocument]")
    {
        ALTextDocument doc("hello world\nnext");
        const ALTextPos one = doc.nextWord(ALTextPos(0, 0));
        {
            INFO("forward moves");
            CHECK(one > ALTextPos(0, 0));
            CHECK(one <= ALTextPos(0, 6));
        }
        UNSCOPED_INFO("the word under the caret");
        CHECK(doc.wordAt(ALTextPos(0, 2)) == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        UNSCOPED_INFO("forward at the line end crosses");
        CHECK(doc.nextWord(ALTextPos(0, 11)) == ALTextPos(1, 0));
        UNSCOPED_INFO("back at a line start crosses");
        CHECK(doc.prevWord(ALTextPos(1, 0)) == ALTextPos(0, 11));
        const ALTextPos back = doc.prevWord(ALTextPos(0, 11));
        {
            INFO("back moves");
            CHECK(back < ALTextPos(0, 11));
            CHECK(back >= ALTextPos(0, 5));
        }
        UNSCOPED_INFO("forward at the very end stays");
        CHECK(doc.nextWord(ALTextPos(1, 4)) == ALTextPos(1, 4));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: display columns count graphemes and reach tab stops", "[altextdocument]")
    {
        ALTextDocument doc("\tab\te\xCC\x81");
        UNSCOPED_INFO("a tab is the next stop");
        CHECK(doc.displayColumn(ALTextPos(0, 1), 4) == 4);
        UNSCOPED_INFO("then one per grapheme");
        CHECK(doc.displayColumn(ALTextPos(0, 3), 4) == 6);
        UNSCOPED_INFO("a second tab");
        CHECK(doc.displayColumn(ALTextPos(0, 4), 4) == 8);
        UNSCOPED_INFO("a cluster is one column");
        CHECK(doc.displayColumn(ALTextPos(0, 7), 4) == 9);
        UNSCOPED_INFO("back to the byte");
        CHECK(doc.posAtDisplayColumn(0, 4, 4) == ALTextPos(0, 1));
        UNSCOPED_INFO("into a tab is the tab");
        CHECK(doc.posAtDisplayColumn(0, 2, 4) == ALTextPos(0, 0));
        UNSCOPED_INFO("past the end is the end");
        CHECK(doc.posAtDisplayColumn(0, 40, 4) == ALTextPos(0, 7));
        CHECK(doc.posAtDisplayColumn(0, 6, 4) == ALTextPos(0, 3));
        // ASCII alone, read a byte a column.
        ALTextDocument ascii("\tab\tc");
        UNSCOPED_INFO("ascii: past the second tab");
        CHECK(ascii.displayColumn(ALTextPos(0, 5), 4) == 9);
        UNSCOPED_INFO("ascii: before it");
        CHECK(ascii.displayColumn(ALTextPos(0, 3), 4) == 6);
        UNSCOPED_INFO("ascii: back to the byte");
        CHECK(ascii.posAtDisplayColumn(0, 5, 4) == ALTextPos(0, 2));
        UNSCOPED_INFO("ascii: into a tab is the tab");
        CHECK(ascii.posAtDisplayColumn(0, 7, 4) == ALTextPos(0, 3));
        UNSCOPED_INFO("ascii: past the end is the end");
        CHECK(ascii.posAtDisplayColumn(0, 40, 4) == ALTextPos(0, 5));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: a stretch replaced by the same text is no edit and nobody is told", "[altextdocument]")
    {
        ALTextDocument doc("one two");
        listen(doc);
        const U32            before = doc.version();
        ALTextDocument::Edit same   = doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "two");
        CHECK(same.nothing());
        CHECK(doc.version() == before);
        CHECK(heard.empty());
        UNSCOPED_INFO("the text as it was");
        CHECK(doc.text() == std::string("one two"));
        {
            INFO("and a different text is an edit");
            CHECK_FALSE(doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "Two").nothing());
            CHECK(heard.size() == 1);
        }
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: text appended arrives at the end in the order it came", "[altextdocument]")
    {
        ALTextDocument doc;
        doc.append("one\ntwo");
        doc.append("\nthree");
        CHECK(doc.lineCount() == 3);
        CHECK(doc.text() == std::string("one\ntwo\nthree"));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: a stretch an edit lands in grows and shrinks with it and is not dropped", "[altextdocument]")
    {
        const ALTextRange scope(ALTextPos(0, 4), ALTextPos(1, 3));
        ALTextDocument    doc("one two three\nfour five");
        // Inside it, longer: the end moves on by what grew.
        ALTextDocument::Edit grew = doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "TWO!!");
        UNSCOPED_INFO("its end on the next line as it was");
        CHECK(grew.stretched(scope) == ALTextRange(ALTextPos(0, 4), ALTextPos(1, 3)));
        UNSCOPED_INFO("grown");
        CHECK(grew.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 13))) == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 15)));
        // Before it: both ends move along.
        doc.setText("one two three");
        ALTextDocument::Edit before = doc.insert(ALTextPos(0, 0), "zero ");
        UNSCOPED_INFO("moved along");
        CHECK(before.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 9), ALTextPos(0, 12)));
        // At its start: within it; at its end: not.
        doc.setText("one two three");
        ALTextDocument::Edit at_start = doc.insert(ALTextPos(0, 4), "x");
        UNSCOPED_INFO("put at its start, within it");
        CHECK(at_start.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 8)));
        doc.setText("one two three");
        ALTextDocument::Edit at_end = doc.insert(ALTextPos(0, 7), "x");
        UNSCOPED_INFO("put at its end, not");
        CHECK(at_end.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)));
        // Over its end: its end past what went in.
        doc.setText("one two three");
        ALTextDocument::Edit over = doc.replace(ALTextRange(ALTextPos(0, 6), ALTextPos(0, 9)), "O\nTH");
        UNSCOPED_INFO("its end past what went in");
        CHECK(over.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 4), ALTextPos(1, 2)));
        // Over its start: its start where what was replaced began.
        doc.setText("one two three");
        ALTextDocument::Edit cut = doc.remove(ALTextRange(ALTextPos(0, 2), ALTextPos(0, 5)));
        UNSCOPED_INFO("its start where the cut began");
        CHECK(cut.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 4)));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: alExpandTabs counts from a column and alInCaseOf keeps case past ASCII", "[altextdocument]")
    {
        S32 column = 0;
        UNSCOPED_INFO("to the first stop");
        CHECK(alExpandTabs("\tx", column, 4) == std::string("    x"));
        UNSCOPED_INFO("counted on");
        CHECK(column == 5);
        UNSCOPED_INFO("from where it had reached");
        CHECK(alExpandTabs("\ty", column, 4) == std::string("   y"));
        column = 1;
        UNSCOPED_INFO("a character of two bytes is one column");
        CHECK(alExpandTabs("\xC3\xA9\t|", column, 4) == std::string("\xC3\xA9  |"));

        UNSCOPED_INFO("all capitals");
        CHECK(alInCaseOf("\xC3\x89LAN", "hello") == std::string("HELLO"));
        UNSCOPED_INFO("capitalised");
        CHECK(alInCaseOf("\xC3\x89lan", "hello") == std::string("Hello"));
        UNSCOPED_INFO("all small, and what goes in lowered past ASCII too");
        CHECK(alInCaseOf("world", "\xC3\x89LAN") == std::string("\xC3\xA9lan"));
        UNSCOPED_INFO("raised past ASCII");
        CHECK(alInCaseOf("WORLD", "\xC3\xA9lan") == std::string("\xC3\x89LAN"));
        UNSCOPED_INFO("mixed stays as written");
        CHECK(alInCaseOf("wOrLd", "Hello") == std::string("Hello"));
        UNSCOPED_INFO("no letters stays as written");
        CHECK(alInCaseOf("123", "Hello") == std::string("Hello"));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: an edit's end kept as it is made agrees with one worked out by hand", "[altextdocument]")
    {
        ALTextDocument doc("ab\ncd");
        listen(doc);
        doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 1)), "x\r\nyz\r\n");
        doc.insert(ALTextPos(0, 0), "q");
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), std::string_view());
        REQUIRE(heard.size() == size_t(3));
        for (const ALTextDocument::Edit& edit : heard)
        {
            UNSCOPED_INFO("kept as it was made");
            REQUIRE(edit.endKept);
            ALTextDocument::Edit by_hand{ edit.range, edit.removed, edit.inserted };
            UNSCOPED_INFO("by hand, not");
            REQUIRE_FALSE(by_hand.endKept);
            REQUIRE(edit.endAfter() == by_hand.endAfter());
            REQUIRE(edit.breaksInserted() == by_hand.breaksInserted());
        }
        UNSCOPED_INFO("across the line endings put in, as LF");
        CHECK(heard[0].endAfter() == ALTextPos(2, 0));
        CHECK(heard[0].breaksInserted() == 2);
        UNSCOPED_INFO("one character on");
        CHECK(heard[1].endAfter() == ALTextPos(0, 1));
        UNSCOPED_INFO("nothing put in ends where it began");
        CHECK(heard[2].endAfter() == ALTextPos(0, 0));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: several stretches replaced as one edit are heard once and undone whole", "[altextdocument]")
    {
        ALTextDocument doc("one two\nthree four\nfive six\nseven");
        listen(doc);
        typedef ALTextRange R;
        typedef ALTextPos   P;
        const ALTextDocument::Edit edit =
            doc.replaceMany({ { R(P(0, 4), P(0, 7)), "2" }, { R(P(1, 0), P(1, 5)), "3\nTHREE" }, { R(P(2, 5), P(2, 8)), "6" }, { R(P(2, 0), P(2, 4)), "5" } });
        CHECK(doc.text() == std::string("one 2\n3\nTHREE four\n5 6\nseven"));
        UNSCOPED_INFO("one notification");
        CHECK(heard.size() == 1);
        UNSCOPED_INFO("from the first to the last");
        CHECK(edit.range == R(P(0, 4), P(2, 8)));
        const std::vector<ALTextDocument::Edit::Part>& parts = edit.parts;
        {
            INFO("each in order");
            REQUIRE(parts.size() == 4);
            CHECK(parts[2].before == R(P(2, 0), P(2, 4)));
        }
        {
            INFO("each as it is after");
            CHECK(parts[0].after == R(P(0, 4), P(0, 5)));
            CHECK(parts[1].after == R(P(1, 0), P(2, 5)));
            CHECK(parts[2].after == R(P(3, 0), P(3, 1)));
            CHECK(parts[3].after == R(P(3, 2), P(3, 3)));
        }
        const std::vector<ALTextDocument::Edit::LineSpan> spans = edit.lineSpans();
        {
            INFO("a run of lines each, the two sharing one line as one");
            REQUIRE(spans.size() == 3);
            CHECK(spans[1].first == 1);
            CHECK(spans[1].made == 2);
            CHECK(spans[2].first == 2);
            CHECK(spans[2].last == 2);
            CHECK(spans[2].made == 1);
        }
        UNSCOPED_INFO("a position between them moved");
        CHECK(edit.slidPast(P(1, 6)) == P(2, 6));
        R between(P(2, 4), P(2, 5));
        {
            INFO("a range between two on one line kept, and moved");
            CHECK(edit.slide(between));
            CHECK(between == R(P(3, 1), P(3, 2)));
        }
        R cut(P(0, 3), P(0, 5));
        UNSCOPED_INFO("one a stretch cut through goes");
        CHECK_FALSE(edit.slide(cut));
        UNSCOPED_INFO("the find scope stretched round them");
        CHECK(edit.stretched(R(P(0, 5), P(3, 2))) == R(P(0, 4), P(4, 2)));
        {
            INFO("a mark inside a stretch where it began");
            CHECK(edit.placed(P(1, 2)) == P(1, 0));
            CHECK(edit.placed(P(2, 6)) == P(3, 2));
        }
        UNSCOPED_INFO("one after them all moved along");
        CHECK(edit.placed(P(3, 2)) == P(4, 2));
        {
            INFO("a line replaced gone, one after them moved");
            CHECK(edit.lineAfter(0) == -1);
            CHECK(edit.lineAfter(2) == -1);
            CHECK(edit.lineAfter(3) == 4);
        }

        const ALTextDocument::Edit back = edit.inverse();
        doc.replace(back.range, back.inserted, back.parts);
        UNSCOPED_INFO("put back");
        CHECK(doc.text() == std::string("one two\nthree four\nfive six\nseven"));
        {
            INFO("the stretches with it, the other way round");
            REQUIRE(heard.size() == 2);
            REQUIRE(heard[1].parts.size() == 4);
            CHECK(heard[1].parts[1].after == R(P(1, 0), P(1, 5)));
        }

        doc.replaceMany({ { R(P(0, 0), P(0, 3)), "a" }, { R(P(0, 2), P(0, 4)), "b" } });
        UNSCOPED_INFO("one over another left out");
        CHECK(doc.text() == std::string("a two\nthree four\nfive six\nseven"));
        heard.clear();
        doc.replaceMany({ { R(P(3, 0), P(3, 0)), "x" } });
        {
            INFO("one stretch is a plain edit");
            REQUIRE(heard.size() == 1);
            CHECK(heard[0].parts.empty());
        }
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: alNextTabStop in columns and pixels and alBlanksWidth in width and bytes", "[altextdocument]")
    {
        UNSCOPED_INFO("from a stop to the next");
        CHECK(alNextTabStop(0, 4) == 4);
        UNSCOPED_INFO("from inside to the next");
        CHECK(alNextTabStop(5, 4) == 8);
        UNSCOPED_INFO("a width of none taken as one");
        CHECK(alNextTabStop(3, 0) == 4);
        UNSCOPED_INFO("in pixels");
        CHECK(alNextTabStop(13.f, 8.f) == 16.f);
        UNSCOPED_INFO("in pixels, from a stop");
        CHECK(alNextTabStop(16.f, 8.f) == 24.f);
        size_t bytes = 0;
        UNSCOPED_INFO("spaces, then a tab to its stop");
        CHECK(alBlanksWidth("  \tx", 4, &bytes) == 4);
        UNSCOPED_INFO("three bytes of them");
        CHECK(bytes == size_t(3));
        UNSCOPED_INFO("a tab, then spaces");
        CHECK(alBlanksWidth("\t  x", 4) == 6);
        UNSCOPED_INFO("nothing blank");
        CHECK(alBlanksWidth("x  ", 4, &bytes) == 0);
        CHECK(bytes == size_t(0));
        UNSCOPED_INFO("all blank");
        CHECK(alBlanksWidth(" \t", 2, &bytes) == 2);
        UNSCOPED_INFO("all of it");
        CHECK(bytes == size_t(2));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: code word steps take a run and the blanks after it or a part of a name", "[altextdocument]")
    {
        const ALTextDocument doc("ll.Say(0, llSetPos)\n  PRIM_POSITION");
        std::string stops;
        for (ALTextPos at(0, 0); at.line == 0 && at.column < 19;)
        {
            at = doc.nextCodeWord(at);
            stops += llformat("%d ", at.column);
        }
        UNSCOPED_INFO("forward, each run's end past its blanks");
        CHECK(stops == std::string("2 3 6 7 8 10 18 19 "));
        UNSCOPED_INFO("across the line end");
        CHECK(doc.nextCodeWord(ALTextPos(0, 19)) == ALTextPos(1, 0));
        UNSCOPED_INFO("over the next line's blanks from its start");
        CHECK(doc.nextCodeWord(ALTextPos(1, 0)) == ALTextPos(1, 2));
        stops.clear();
        for (ALTextPos at(0, 19); at.column > 0;)
        {
            at = doc.prevCodeWord(at);
            stops += llformat("%d ", at.column);
        }
        UNSCOPED_INFO("back, each run's start");
        CHECK(stops == std::string("18 10 8 7 6 3 2 0 "));
        UNSCOPED_INFO("back across the line start");
        CHECK(doc.prevCodeWord(ALTextPos(1, 0)) == ALTextPos(0, 19));
        {
            INFO("parts forward");
            CHECK(doc.nextCodeWord(ALTextPos(0, 10), true) == ALTextPos(0, 12));
            CHECK(doc.nextCodeWord(ALTextPos(0, 12), true) == ALTextPos(0, 15));
        }
        {
            INFO("parts back");
            CHECK(doc.prevCodeWord(ALTextPos(0, 17), true) == ALTextPos(0, 15));
            CHECK(doc.prevCodeWord(ALTextPos(0, 15), true) == ALTextPos(0, 12));
        }
        {
            INFO("past an underscore");
            CHECK(doc.nextCodeWord(ALTextPos(1, 2), true) == ALTextPos(1, 7));
            CHECK(doc.prevCodeWord(ALTextPos(1, 15), true) == ALTextPos(1, 7));
        }
        const ALTextDocument wide("x\xC3\xA9y + 1");
        UNSCOPED_INFO("a character past ASCII kept whole");
        CHECK(wide.nextCodeWord(ALTextPos(0, 0)) == ALTextPos(0, 5));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: cluster steps and clamps agree with ICU at every place of every line", "[altextdocument]")
    {
        const std::vector<std::string> lines = {
            "plain ascii, all of it",
            "e\xCC\x81 an accent joined to the e before it",
            "caf\xC3\xA9 na\xC3\xAFve",
            "1\xEF\xB8\x8F\xE2\x83\xA3 a keycap after a digit",
            "flags \xF0\x9F\x87\xBA\xF0\x9F\x87\xB8 and a\xE2\x80\x8D joiner",
            "a tab\tin a line",
            "x",
            "",
        };
        std::string text;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            text += (i ? "\n" : "") + lines[i];
        }
        const ALTextDocument d(text);
        for (S32 l = 0; l < static_cast<S32>(lines.size()); ++l)
        {
            // From every place a caret stands: each cluster's start, and
            // the line's end.
            const std::string& line  = lines[static_cast<size_t>(l)];
            std::vector<size_t> stops = utf8str_grapheme_starts(line, line.size());
            for (size_t c : stops)
            {
                CAPTURE(line, c);
                REQUIRE(d.nextCluster(ALTextPos(l, static_cast<S32>(c))).column ==
                              static_cast<S32>(utf8str_step_grapheme_forward(line, c)));
            }
            stops.push_back(line.size());
            for (size_t c : stops)
            {
                if (c > 0)
                {
                    CAPTURE(line, c);
                    REQUIRE(d.prevCluster(ALTextPos(l, static_cast<S32>(c))).column ==
                                  static_cast<S32>(utf8str_step_grapheme_backward(line, c)));
                }
            }
            for (size_t c = 0; c <= line.size(); ++c)
            {
                CAPTURE(line, c);
                REQUIRE(d.clamp(ALTextPos(l, static_cast<S32>(c))).column ==
                              static_cast<S32>(utf8str_grapheme_align_backward(line, c)));
            }
        }
        UNSCOPED_INFO("off a line's end to the next");
        CHECK(d.nextCluster(ALTextPos(0, static_cast<S32>(lines[0].size()))) == ALTextPos(1, 0));
        UNSCOPED_INFO("back off its start to the one before");
        CHECK(d.prevCluster(ALTextPos(1, 0)) == ALTextPos(0, static_cast<S32>(lines[0].size())));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: an edit moves a place pushed or not and finds the stretch it stood in", "[altextdocument]")
    {
        ALTextDocument             doc("abcdef");
        const ALTextDocument::Edit typed = doc.insert(ALTextPos(0, 2), "XY");
        UNSCOPED_INFO("pushed");
        CHECK(typed.placed(ALTextPos(0, 2)) == ALTextPos(0, 4));
        UNSCOPED_INFO("not pushed");
        CHECK(typed.placed(ALTextPos(0, 2), false) == ALTextPos(0, 2));
        UNSCOPED_INFO("after it moved either way");
        CHECK(typed.placed(ALTextPos(0, 3), false) == ALTextPos(0, 5));
        UNSCOPED_INFO("nothing stood in what was put in");
        CHECK_FALSE(typed.replacedAround(ALTextPos(0, 2)));

        // "abXYcdef": "XY" and "ef" replaced at once.
        const ALTextDocument::Edit batch =
            doc.replaceMany({ { ALTextRange(ALTextPos(0, 2), ALTextPos(0, 4)), "-" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)), "" } });
        CHECK(doc.text() == std::string("ab-cd"));
        UNSCOPED_INFO("in the first");
        CHECK(batch.replacedAround(ALTextPos(0, 3)) == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 4)));
        UNSCOPED_INFO("at the second's start");
        CHECK(batch.replacedAround(ALTextPos(0, 6)) == ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)));
        {
            INFO("not between them");
            CHECK_FALSE(batch.replacedAround(ALTextPos(0, 4)));
            CHECK_FALSE(batch.replacedAround(ALTextPos(0, 5)));
        }
        UNSCOPED_INFO("nor at an end");
        CHECK_FALSE(batch.replacedAround(ALTextPos(0, 8)));
        UNSCOPED_INFO("between them, moved by the first");
        CHECK(batch.placed(ALTextPos(0, 5)) == ALTextPos(0, 4));
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: CRLF and a lone CR split lines as reading a character at a time does", "[altextdocument]")
    {
        // Each character read in turn: what the lines are.
        const auto reference = [](const std::string& text) {
            std::vector<std::string> out(1);
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '\r' || text[i] == '\n')
                {
                    if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                    {
                        ++i;
                    }
                    out.emplace_back();
                }
                else
                {
                    out.back().push_back(text[i]);
                }
            }
            return out;
        };
        std::vector<std::string> texts = { "", "\r", "\n", "\r\n", "\n\r", "\r\r\n", "a\r", "a\r\n", "\ra", "a\rb\r\nc\nd\r\re",
                                           std::string(500, 'x') + "\n" + std::string(30, 'y') + "\r" };
        std::string many;
        for (S32 n = 0; n < 2000; ++n)
        {
            many += "line " + std::to_string(n) + "\n";
        }
        texts.push_back(many + "a lone CR far on\rand after it");
        // And texts of the three endings and letters, at random but the same
        // each run.
        U32 seed = 12345;
        for (S32 t = 0; t < 300; ++t)
        {
            std::string text;
            const S32   length = static_cast<S32>((seed = seed * 1103515245u + 12345u) >> 16) % 40;
            for (S32 c = 0; c < length; ++c)
            {
                seed = seed * 1103515245u + 12345u;
                text.push_back("ab\r\n"[(seed >> 16) % 4]);
            }
            texts.push_back(text);
        }
        for (const std::string& text : texts)
        {
            const ALTextDocument           doc(text);
            const std::vector<std::string> want = reference(text);
            std::vector<std::string>       got;
            for (S32 l = 0; l < doc.lineCount(); ++l)
            {
                got.push_back(doc.line(l));
            }
            CAPTURE(text.size());
            REQUIRE(got == want);
            ALTextDocument replaced("something else\nfirst");
            replaced.setText(text);
            UNSCOPED_INFO("the same put in over another");
            REQUIRE(replaced.text() == doc.text());
        }
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: the bytes of the text and of a stretch agree after every kind of edit", "[altextdocument]")
    {
        ALTextDocument doc("one\r\ntwo\rthree");
        const auto require_bytes_agree = [&doc](const std::string& what) {
            INFO(what);
            REQUIRE(doc.byteCount() == doc.text().size());
            const S32         last     = doc.lineCount() - 1;
            const ALTextRange ranges[] = { ALTextRange(doc.start(), doc.end()), ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2)),
                                           ALTextRange(ALTextPos(0, 2), ALTextPos(last, 1)), ALTextRange(doc.end(), ALTextPos(0, 1)),
                                           ALTextRange(ALTextPos(-3, 9), ALTextPos(99, 99)) };
            for (const ALTextRange& range : ranges)
            {
                REQUIRE(doc.byteCount(range) == doc.text(range).size());
            }
        };
        require_bytes_agree("as read");
        doc.insert(ALTextPos(1, 1), "X\r\nY");
        require_bytes_agree("a break put in");
        doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(2, 1)), "z");
        require_bytes_agree("lines taken out");
        doc.replaceMany({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), "ab\ncd" }, { ALTextRange(doc.end(), doc.end()), "\r\n\n" } });
        require_bytes_agree("a batch");
        doc.remove(ALTextRange(doc.start(), doc.end()));
        require_bytes_agree("everything gone");
        doc.setText("again\nand again");
        require_bytes_agree("a text put in whole");
    }

    TEST_CASE_METHOD(altextdocument_data, "altextdocument: code word steps never stop inside a keycap or a blank a mark is on", "[altextdocument]")
    {
        // x, a blank, # with U+FE0F and U+20E3 on it, a blank, y.
        const ALTextDocument keycap("x #\xEF\xB8\x8F\xE2\x83\xA3 y");
        UNSCOPED_INFO("from the mark, past the keycap and the blank after it");
        CHECK(keycap.nextCodeWord(ALTextPos(0, 2)) == ALTextPos(0, 10));
        UNSCOPED_INFO("back over it whole");
        CHECK(keycap.prevCodeWord(ALTextPos(0, 10)) == ALTextPos(0, 2));
        UNSCOPED_INFO("up to it as before");
        CHECK(keycap.nextCodeWord(ALTextPos(0, 0)) == ALTextPos(0, 2));
        // a, a blank with a combining acute on it, z.
        const ALTextDocument accent("a \xCC\x81z");
        UNSCOPED_INFO("forward past the blank and its mark");
        CHECK(accent.nextCodeWord(ALTextPos(0, 0)) == ALTextPos(0, 4));
        UNSCOPED_INFO("and from the blank itself");
        CHECK(accent.nextCodeWord(ALTextPos(0, 1)) == ALTextPos(0, 4));
        UNSCOPED_INFO("back to the blank the mark is on");
        CHECK(accent.prevCodeWord(ALTextPos(0, 4)) == ALTextPos(0, 1));
    }
}
