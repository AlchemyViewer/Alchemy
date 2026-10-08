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

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct altextdocument_data
    {
        std::vector<ALTextDocument::Edit> heard;

        void listen(ALTextDocument& doc)
        {
            doc.onChanged([this](const ALTextDocument::Edit& edit) { heard.push_back(edit); });
        }
    };

    typedef test_group<altextdocument_data> altextdocument_group;
    typedef altextdocument_group::object    altextdocument_object;
    altextdocument_group                    altextdocument_instance("altextdocument");

    template<> template<>
    void altextdocument_object::test<1>()
    {
        set_test_name("an empty document is one empty line");
        ALTextDocument doc;
        ensure_equals("one line", doc.lineCount(), 1);
        ensure("empty", doc.empty());
        ensure_equals("no text", doc.text(), std::string());
        ensure_equals("no bytes", doc.byteCount(), size_t(0));
        ensure("end is start", doc.end() == doc.start());
    }

    template<> template<>
    void altextdocument_object::test<2>()
    {
        set_test_name("line endings arrive as anything and leave as LF");
        ALTextDocument doc("one\r\ntwo\rthree\nfour");
        ensure_equals("four lines", doc.lineCount(), 4);
        ensure_equals("second", doc.line(1), std::string("two"));
        ensure_equals("third", doc.line(2), std::string("three"));
        ensure_equals("joined with LF", doc.text(), std::string("one\ntwo\nthree\nfour"));
        ensure_equals("bytes", doc.byteCount(), doc.text().size());
        ensure("end", doc.end() == ALTextPos(3, 4));
        ensure_equals("a line past the end is empty", doc.line(9), std::string());
    }

    template<> template<>
    void altextdocument_object::test<3>()
    {
        set_test_name("an insert in a line is one edit, heard once");
        ALTextDocument doc("hello world");
        listen(doc);
        const U32 before = doc.version();
        ALTextDocument::Edit edit = doc.insert(ALTextPos(0, 5), ",");
        ensure_equals("text", doc.text(), std::string("hello, world"));
        ensure("range is where it went", edit.range == ALTextRange(ALTextPos(0, 5), ALTextPos(0, 5)));
        ensure_equals("nothing removed", edit.removed, std::string());
        ensure_equals("inserted", edit.inserted, std::string(","));
        ensure("ends after the comma", edit.endAfter() == ALTextPos(0, 6));
        ensure("version moved", doc.version() > before);
        ensure_equals("heard once", heard.size(), size_t(1));
        ensure_equals("heard the same", heard.front().inserted, std::string(","));
    }

    template<> template<>
    void altextdocument_object::test<4>()
    {
        set_test_name("inserting line breaks splits, removing across lines joins, and the inverse puts it back");
        ALTextDocument doc("abcd");
        ALTextDocument::Edit split = doc.insert(ALTextPos(0, 2), "1\n22\n");
        ensure_equals("three lines", doc.lineCount(), 3);
        ensure_equals("text", doc.text(), std::string("ab1\n22\ncd"));
        ensure("ends at the start of the last line", split.endAfter() == ALTextPos(2, 0));
        ensure("range after", split.rangeAfter() == ALTextRange(ALTextPos(0, 2), ALTextPos(2, 0)));

        ALTextDocument::Edit joined = doc.remove(ALTextRange(ALTextPos(0, 3), ALTextPos(2, 1)));
        ensure_equals("joined", doc.text(), std::string("ab1d"));
        ensure_equals("removed what spanned the lines", joined.removed, std::string("\n22\nc"));

        doc.replace(joined.inverse().range, joined.inverse().inserted);
        ensure_equals("put back", doc.text(), std::string("ab1\n22\ncd"));
        doc.replace(split.inverse().range, split.inverse().inserted);
        ensure_equals("and back again", doc.text(), std::string("abcd"));
    }

    template<> template<>
    void altextdocument_object::test<5>()
    {
        set_test_name("a replacement over several lines, and a range given backwards");
        ALTextDocument doc("one\ntwo\nthree\nfour");
        ALTextDocument::Edit edit = doc.replace(ALTextRange(ALTextPos(2, 3), ALTextPos(0, 2)), "X\nY");
        ensure_equals("text", doc.text(), std::string("onX\nYee\nfour"));
        ensure("range normalised", edit.range == ALTextRange(ALTextPos(0, 2), ALTextPos(2, 3)));
        ensure_equals("removed", edit.removed, std::string("e\ntwo\nthr"));
        ensure_equals("text of a range", doc.text(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 2))), std::string("nX\nYe"));
    }

    template<> template<>
    void altextdocument_object::test<6>()
    {
        set_test_name("an edit that changes nothing moves nothing");
        ALTextDocument doc("abc");
        listen(doc);
        const U32 before = doc.version();
        ALTextDocument::Edit edit = doc.remove(ALTextRange(ALTextPos(0, 1), ALTextPos(0, 1)));
        ensure("nothing", edit.nothing());
        ensure_equals("version still", doc.version(), before);
        ensure("nobody told", heard.empty());
        ensure("clamped past the end", doc.insert(ALTextPos(7, 7), "").nothing());
    }

    template<> template<>
    void altextdocument_object::test<7>()
    {
        set_test_name("offsets and positions agree");
        ALTextDocument doc("ab\n\ncde");
        ensure_equals("start", doc.offsetOf(ALTextPos(0, 0)), size_t(0));
        ensure_equals("past the first break", doc.offsetOf(ALTextPos(1, 0)), size_t(3));
        ensure_equals("into the last", doc.offsetOf(ALTextPos(2, 2)), size_t(6));
        ensure("back", doc.posAt(6) == ALTextPos(2, 2));
        ensure("the empty line", doc.posAt(3) == ALTextPos(1, 0));
        ensure("a break's own offset is the line end before it", doc.posAt(2) == ALTextPos(0, 2));
        ensure("past everything is the end", doc.posAt(99) == doc.end());
        // The whole text is kept between edits and follows them.
        ensure_equals("the whole", doc.wholeText(), std::string("ab\n\ncde"));
        ensure("the line starts", doc.lineStarts() == std::vector<size_t>({ 0, 3, 4 }));
        doc.insert(ALTextPos(0, 2), "X\nY");
        ensure_equals("the whole after an edit", doc.wholeText(), std::string("abX\nY\n\ncde"));
        ensure("the starts after it", doc.lineStarts() == std::vector<size_t>({ 0, 4, 6, 7 }));
        ensure("and the offsets", doc.offsetOf(ALTextPos(3, 1)) == 8 && doc.posAt(8) == ALTextPos(3, 1));
        // Patched in place, whatever the edit's shape: lines taken out,
        // put in, and the end of the text.
        doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(2, 0)), "");
        ensure_equals("lines taken out", doc.wholeText(), std::string("a\ncde"));
        ensure("the starts follow", doc.lineStarts() == std::vector<size_t>({ 0, 2 }));
        doc.append("\nf");
        ensure_equals("appended", doc.wholeText(), std::string("a\ncde\nf"));
        ensure("the starts again", doc.lineStarts() == std::vector<size_t>({ 0, 2, 6 }));
        ensure_equals("as made afresh", doc.wholeText(), doc.text());
    }

    template<> template<>
    void altextdocument_object::test<8>()
    {
        set_test_name("clusters: a combining mark stays with its base, and lines join up");
        // e + combining acute, then z.
        ALTextDocument doc("e\xCC\x81z\nq");
        ensure("next from the start clears the cluster", doc.nextCluster(ALTextPos(0, 0)) == ALTextPos(0, 3));
        ensure("clamp inside the cluster goes back", doc.clamp(ALTextPos(0, 2)) == ALTextPos(0, 0));
        ensure("prev from after z", doc.prevCluster(ALTextPos(0, 4)) == ALTextPos(0, 3));
        ensure("prev from the cluster's end", doc.prevCluster(ALTextPos(0, 3)) == ALTextPos(0, 0));
        ensure("next at a line end is the next line", doc.nextCluster(ALTextPos(0, 4)) == ALTextPos(1, 0));
        ensure("prev at a line start is the line before's end", doc.prevCluster(ALTextPos(1, 0)) == ALTextPos(0, 4));
        ensure("nowhere past the end", doc.nextCluster(ALTextPos(1, 1)) == ALTextPos(1, 1));
        ensure("nowhere before the start", doc.prevCluster(ALTextPos(0, 0)) == ALTextPos(0, 0));
    }

    template<> template<>
    void altextdocument_object::test<9>()
    {
        set_test_name("words: along a line and over its end");
        ALTextDocument doc("hello world\nnext");
        const ALTextPos one = doc.nextWord(ALTextPos(0, 0));
        ensure("forward moves", one > ALTextPos(0, 0) && one <= ALTextPos(0, 6));
        ensure("the word under the caret", doc.wordAt(ALTextPos(0, 2)) == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 5)));
        ensure("forward at the line end crosses", doc.nextWord(ALTextPos(0, 11)) == ALTextPos(1, 0));
        ensure("back at a line start crosses", doc.prevWord(ALTextPos(1, 0)) == ALTextPos(0, 11));
        const ALTextPos back = doc.prevWord(ALTextPos(0, 11));
        ensure("back moves", back < ALTextPos(0, 11) && back >= ALTextPos(0, 5));
        ensure("forward at the very end stays", doc.nextWord(ALTextPos(1, 4)) == ALTextPos(1, 4));
    }

    template<> template<>
    void altextdocument_object::test<10>()
    {
        set_test_name("display columns count graphemes and reach tab stops");
        ALTextDocument doc("\tab\te\xCC\x81");
        ensure_equals("a tab is the next stop", doc.displayColumn(ALTextPos(0, 1), 4), 4);
        ensure_equals("then one per grapheme", doc.displayColumn(ALTextPos(0, 3), 4), 6);
        ensure_equals("a second tab", doc.displayColumn(ALTextPos(0, 4), 4), 8);
        ensure_equals("a cluster is one column", doc.displayColumn(ALTextPos(0, 7), 4), 9);
        ensure("back to the byte", doc.posAtDisplayColumn(0, 4, 4) == ALTextPos(0, 1));
        ensure("into a tab is the tab", doc.posAtDisplayColumn(0, 2, 4) == ALTextPos(0, 0));
        ensure("past the end is the end", doc.posAtDisplayColumn(0, 40, 4) == ALTextPos(0, 7));
        ensure("column six", doc.posAtDisplayColumn(0, 6, 4) == ALTextPos(0, 3));
        // ASCII alone, read a byte a column.
        ALTextDocument ascii("\tab\tc");
        ensure_equals("ascii: past the second tab", ascii.displayColumn(ALTextPos(0, 5), 4), 9);
        ensure_equals("ascii: before it", ascii.displayColumn(ALTextPos(0, 3), 4), 6);
        ensure("ascii: back to the byte", ascii.posAtDisplayColumn(0, 5, 4) == ALTextPos(0, 2));
        ensure("ascii: into a tab is the tab", ascii.posAtDisplayColumn(0, 7, 4) == ALTextPos(0, 3));
        ensure("ascii: past the end is the end", ascii.posAtDisplayColumn(0, 40, 4) == ALTextPos(0, 5));
    }

    template<> template<>
    void altextdocument_object::test<11>()
    {
        set_test_name("a stretch replaced by the same text changes nothing: nothing heard, the version still, answered as nothing");
        ALTextDocument doc("one two");
        listen(doc);
        const U32            before = doc.version();
        ALTextDocument::Edit same   = doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "two");
        ensure("nothing", same.nothing());
        ensure_equals("the version still", doc.version(), before);
        ensure("nobody told", heard.empty());
        ensure_equals("the text as it was", doc.text(), std::string("one two"));
        ensure("and a different text is an edit", !doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "Two").nothing() && heard.size() == 1);
    }

    template<> template<>
    void altextdocument_object::test<12>()
    {
        set_test_name("a log: text arrives at the end");
        ALTextDocument doc;
        doc.append("one\ntwo");
        doc.append("\nthree");
        ensure_equals("three lines", doc.lineCount(), 3);
        ensure_equals("in the order it came", doc.text(), std::string("one\ntwo\nthree"));
    }

    template<> template<>
    void altextdocument_object::test<13>()
    {
        set_test_name("a stretch an edit lands in grows and shrinks with it, rather than going as a link would");
        const ALTextRange scope(ALTextPos(0, 4), ALTextPos(1, 3));
        ALTextDocument    doc("one two three\nfour five");
        // Inside it, longer: the end moves on by what grew.
        ALTextDocument::Edit grew = doc.replace(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)), "TWO!!");
        ensure("grown", grew.stretched(scope) == ALTextRange(ALTextPos(0, 4), ALTextPos(1, 3)));
        ensure("its end on the next line as it was", grew.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 13))) == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 15)));
        // Before it: both ends move along.
        doc.setText("one two three");
        ALTextDocument::Edit before = doc.insert(ALTextPos(0, 0), "zero ");
        ensure("moved along", before.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 9), ALTextPos(0, 12)));
        // At its start: within it; at its end: not.
        doc.setText("one two three");
        ALTextDocument::Edit at_start = doc.insert(ALTextPos(0, 4), "x");
        ensure("put at its start, within it", at_start.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 8)));
        doc.setText("one two three");
        ALTextDocument::Edit at_end = doc.insert(ALTextPos(0, 7), "x");
        ensure("put at its end, not", at_end.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)));
        // Over its end: its end past what went in.
        doc.setText("one two three");
        ALTextDocument::Edit over = doc.replace(ALTextRange(ALTextPos(0, 6), ALTextPos(0, 9)), "O\nTH");
        ensure("its end past what went in", over.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 4), ALTextPos(1, 2)));
        // Over its start: its start where what was replaced began.
        doc.setText("one two three");
        ALTextDocument::Edit cut = doc.remove(ALTextRange(ALTextPos(0, 2), ALTextPos(0, 5)));
        ensure("its start where the cut began", cut.stretched(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7))) == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 4)));
    }

    template<> template<>
    void altextdocument_object::test<14>()
    {
        set_test_name("tabs to the stops from a column, one column a character; and a replacement in the case of what it replaces, past ASCII");
        S32 column = 0;
        ensure_equals("to the first stop", alExpandTabs("\tx", column, 4), std::string("    x"));
        ensure_equals("counted on", column, 5);
        ensure_equals("from where it had reached", alExpandTabs("\ty", column, 4), std::string("   y"));
        column = 1;
        ensure_equals("a character of two bytes is one column", alExpandTabs("\xC3\xA9\t|", column, 4), std::string("\xC3\xA9  |"));

        ensure_equals("all capitals", alInCaseOf("\xC3\x89LAN", "hello"), std::string("HELLO"));
        ensure_equals("capitalised", alInCaseOf("\xC3\x89lan", "hello"), std::string("Hello"));
        ensure_equals("all small, and what goes in lowered past ASCII too", alInCaseOf("world", "\xC3\x89LAN"), std::string("\xC3\xA9lan"));
        ensure_equals("raised past ASCII", alInCaseOf("WORLD", "\xC3\xA9lan"), std::string("\xC3\x89LAN"));
        ensure_equals("mixed stays as written", alInCaseOf("wOrLd", "Hello"), std::string("Hello"));
        ensure_equals("no letters stays as written", alInCaseOf("123", "Hello"), std::string("Hello"));
    }

    template<> template<>
    void altextdocument_object::test<15>()
    {
        set_test_name("an edit knows where it ends as it is made, and where it is told of it says the same as one worked out");
        ALTextDocument doc("ab\ncd");
        listen(doc);
        doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(1, 1)), "x\r\nyz\r\n");
        doc.insert(ALTextPos(0, 0), "q");
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), std::string_view());
        ensure_equals("three heard", heard.size(), size_t(3));
        for (const ALTextDocument::Edit& edit : heard)
        {
            ensure("kept as it was made", edit.endKept);
            ALTextDocument::Edit by_hand{ edit.range, edit.removed, edit.inserted };
            ensure("by hand, not", !by_hand.endKept);
            ensure("the same end", edit.endAfter() == by_hand.endAfter());
            ensure_equals("the same breaks", edit.breaksInserted(), by_hand.breaksInserted());
        }
        ensure("across the line endings put in, as LF", heard[0].endAfter() == ALTextPos(2, 0));
        ensure_equals("two breaks", heard[0].breaksInserted(), 2);
        ensure("one character on", heard[1].endAfter() == ALTextPos(0, 1));
        ensure("nothing put in ends where it began", heard[2].endAfter() == ALTextPos(0, 0));
    }

    template<> template<>
    void altextdocument_object::test<16>()
    {
        set_test_name("several stretches replaced as one edit: one notification carrying each, positions between them moved, and back again");
        ALTextDocument doc("one two\nthree four\nfive six\nseven");
        listen(doc);
        typedef ALTextRange R;
        typedef ALTextPos   P;
        const ALTextDocument::Edit edit =
            doc.replaceMany({ { R(P(0, 4), P(0, 7)), "2" }, { R(P(1, 0), P(1, 5)), "3\nTHREE" }, { R(P(2, 5), P(2, 8)), "6" }, { R(P(2, 0), P(2, 4)), "5" } });
        ensure_equals("the text", doc.text(), std::string("one 2\n3\nTHREE four\n5 6\nseven"));
        ensure("one notification", heard.size() == 1);
        ensure("from the first to the last", edit.range == R(P(0, 4), P(2, 8)));
        const std::vector<ALTextDocument::Edit::Part>& parts = edit.parts;
        ensure("each in order", parts.size() == 4 && parts[2].before == R(P(2, 0), P(2, 4)));
        ensure("each as it is after", parts[0].after == R(P(0, 4), P(0, 5)) && parts[1].after == R(P(1, 0), P(2, 5)) && parts[2].after == R(P(3, 0), P(3, 1)) &&
                                          parts[3].after == R(P(3, 2), P(3, 3)));
        const std::vector<ALTextDocument::Edit::LineSpan> spans = edit.lineSpans();
        ensure("a run of lines each, the two sharing one line as one", spans.size() == 3 && spans[1].first == 1 && spans[1].made == 2 && spans[2].first == 2 &&
                                                                          spans[2].last == 2 && spans[2].made == 1);
        ensure("a position between them moved", edit.slidPast(P(1, 6)) == P(2, 6));
        R between(P(2, 4), P(2, 5));
        ensure("a range between two on one line kept, and moved", edit.slide(between) && between == R(P(3, 1), P(3, 2)));
        R cut(P(0, 3), P(0, 5));
        ensure("one a stretch cut through goes", !edit.slide(cut));
        ensure("the find scope stretched round them", edit.stretched(R(P(0, 5), P(3, 2))) == R(P(0, 4), P(4, 2)));
        ensure("a mark inside a stretch where it began", edit.placed(P(1, 2)) == P(1, 0) && edit.placed(P(2, 6)) == P(3, 2));
        ensure("one after them all moved along", edit.placed(P(3, 2)) == P(4, 2));
        ensure("a line replaced gone, one after them moved", edit.lineAfter(0) == -1 && edit.lineAfter(2) == -1 && edit.lineAfter(3) == 4);

        const ALTextDocument::Edit back = edit.inverse();
        doc.replace(back.range, back.inserted, back.parts);
        ensure_equals("put back", doc.text(), std::string("one two\nthree four\nfive six\nseven"));
        ensure("the stretches with it, the other way round", heard.size() == 2 && heard[1].parts.size() == 4 && heard[1].parts[1].after == R(P(1, 0), P(1, 5)));

        doc.replaceMany({ { R(P(0, 0), P(0, 3)), "a" }, { R(P(0, 2), P(0, 4)), "b" } });
        ensure_equals("one over another left out", doc.text(), std::string("a two\nthree four\nfive six\nseven"));
        heard.clear();
        doc.replaceMany({ { R(P(3, 0), P(3, 0)), "x" } });
        ensure("one stretch is a plain edit", heard.size() == 1 && heard[0].parts.empty());
    }

    template<> template<>
    void altextdocument_object::test<17>()
    {
        set_test_name("the next tab stop, in columns and in pixels; and how wide a text's leading blanks are, and how many bytes");
        ensure_equals("from a stop to the next", alNextTabStop(0, 4), 4);
        ensure_equals("from inside to the next", alNextTabStop(5, 4), 8);
        ensure_equals("a width of none taken as one", alNextTabStop(3, 0), 4);
        ensure_equals("in pixels", alNextTabStop(13.f, 8.f), 16.f);
        ensure_equals("in pixels, from a stop", alNextTabStop(16.f, 8.f), 24.f);
        size_t bytes = 0;
        ensure_equals("spaces, then a tab to its stop", alBlanksWidth("  \tx", 4, &bytes), 4);
        ensure_equals("three bytes of them", bytes, size_t(3));
        ensure_equals("a tab, then spaces", alBlanksWidth("\t  x", 4), 6);
        ensure_equals("nothing blank", alBlanksWidth("x  ", 4, &bytes), 0);
        ensure_equals("no bytes", bytes, size_t(0));
        ensure_equals("all blank", alBlanksWidth(" \t", 2, &bytes), 2);
        ensure_equals("all of it", bytes, size_t(2));
    }

    template<> template<>
    void altextdocument_object::test<18>()
    {
        set_test_name("words as code reads them: a name's run, a run of marks, the blanks after; and a name's parts");
        const ALTextDocument doc("ll.Say(0, llSetPos)\n  PRIM_POSITION");
        std::string stops;
        for (ALTextPos at(0, 0); at.line == 0 && at.column < 19;)
        {
            at = doc.nextCodeWord(at);
            stops += llformat("%d ", at.column);
        }
        ensure_equals("forward, each run's end past its blanks", stops, std::string("2 3 6 7 8 10 18 19 "));
        ensure("across the line end", doc.nextCodeWord(ALTextPos(0, 19)) == ALTextPos(1, 0));
        ensure("over the next line's blanks from its start", doc.nextCodeWord(ALTextPos(1, 0)) == ALTextPos(1, 2));
        stops.clear();
        for (ALTextPos at(0, 19); at.column > 0;)
        {
            at = doc.prevCodeWord(at);
            stops += llformat("%d ", at.column);
        }
        ensure_equals("back, each run's start", stops, std::string("18 10 8 7 6 3 2 0 "));
        ensure("back across the line start", doc.prevCodeWord(ALTextPos(1, 0)) == ALTextPos(0, 19));
        ensure("parts forward", doc.nextCodeWord(ALTextPos(0, 10), true) == ALTextPos(0, 12) && doc.nextCodeWord(ALTextPos(0, 12), true) == ALTextPos(0, 15));
        ensure("parts back", doc.prevCodeWord(ALTextPos(0, 17), true) == ALTextPos(0, 15) && doc.prevCodeWord(ALTextPos(0, 15), true) == ALTextPos(0, 12));
        ensure("past an underscore", doc.nextCodeWord(ALTextPos(1, 2), true) == ALTextPos(1, 7) && doc.prevCodeWord(ALTextPos(1, 15), true) == ALTextPos(1, 7));
        const ALTextDocument wide("x\xC3\xA9y + 1");
        ensure("a character past ASCII kept whole", wide.nextCodeWord(ALTextPos(0, 0)) == ALTextPos(0, 5));
    }

    template<> template<>
    void altextdocument_object::test<19>()
    {
        set_test_name("a character stepped over forward and back, at every place of a line, is the cluster ICU says, and a byte of a line clamped is the boundary it says, ASCII taken the quick way");
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
                ensure_equals("forward in \"" + line + "\" at " + std::to_string(c), d.nextCluster(ALTextPos(l, static_cast<S32>(c))).column,
                              static_cast<S32>(utf8str_step_grapheme_forward(line, c)));
            }
            stops.push_back(line.size());
            for (size_t c : stops)
            {
                if (c > 0)
                {
                    ensure_equals("back in \"" + line + "\" at " + std::to_string(c), d.prevCluster(ALTextPos(l, static_cast<S32>(c))).column,
                                  static_cast<S32>(utf8str_step_grapheme_backward(line, c)));
                }
            }
            for (size_t c = 0; c <= line.size(); ++c)
            {
                ensure_equals("clamped in \"" + line + "\" at " + std::to_string(c), d.clamp(ALTextPos(l, static_cast<S32>(c))).column,
                              static_cast<S32>(utf8str_grapheme_align_backward(line, c)));
            }
        }
        ensure("off a line's end to the next", d.nextCluster(ALTextPos(0, static_cast<S32>(lines[0].size()))) == ALTextPos(1, 0));
        ensure("back off its start to the one before", d.prevCluster(ALTextPos(1, 0)) == ALTextPos(0, static_cast<S32>(lines[0].size())));
    }

    template<> template<>
    void altextdocument_object::test<20>()
    {
        set_test_name("a place moved by an edit, pushed by text put in at it or not; and the stretch a place stood in, a batch's own, not the text between");
        ALTextDocument             doc("abcdef");
        const ALTextDocument::Edit typed = doc.insert(ALTextPos(0, 2), "XY");
        ensure("pushed", typed.placed(ALTextPos(0, 2)) == ALTextPos(0, 4));
        ensure("not pushed", typed.placed(ALTextPos(0, 2), false) == ALTextPos(0, 2));
        ensure("after it moved either way", typed.placed(ALTextPos(0, 3), false) == ALTextPos(0, 5));
        ensure("nothing stood in what was put in", !typed.replacedAround(ALTextPos(0, 2)));

        // "abXYcdef": "XY" and "ef" replaced at once.
        const ALTextDocument::Edit batch =
            doc.replaceMany({ { ALTextRange(ALTextPos(0, 2), ALTextPos(0, 4)), "-" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)), "" } });
        ensure_equals("made", doc.text(), std::string("ab-cd"));
        ensure("in the first", batch.replacedAround(ALTextPos(0, 3)) == ALTextRange(ALTextPos(0, 2), ALTextPos(0, 4)));
        ensure("at the second's start", batch.replacedAround(ALTextPos(0, 6)) == ALTextRange(ALTextPos(0, 6), ALTextPos(0, 8)));
        ensure("not between them", !batch.replacedAround(ALTextPos(0, 4)) && !batch.replacedAround(ALTextPos(0, 5)));
        ensure("nor at an end", !batch.replacedAround(ALTextPos(0, 8)));
        ensure("between them, moved by the first", batch.placed(ALTextPos(0, 5)) == ALTextPos(0, 4));
    }
    template<> template<>
    void altextdocument_object::test<21>()
    {
        set_test_name("a text's lines, whatever its endings, as a character at a time reads them: CRLF and a lone CR as LF, at the ends too, and a CR far on");
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
            ensure("the lines of a text of " + std::to_string(text.size()) + " bytes", got == want);
            ALTextDocument replaced("something else\nfirst");
            replaced.setText(text);
            ensure_equals("the same put in over another", replaced.text(), doc.text());
        }
    }

    template<> template<>
    void altextdocument_object::test<22>()
    {
        set_test_name("the bytes of the text, and of a stretch of it, as every kind of edit leaves them");
        ALTextDocument doc("one\r\ntwo\rthree");
        const auto agrees = [&doc](const std::string& what) {
            ensure_equals(what + ": the whole", doc.byteCount(), doc.text().size());
            const S32         last     = doc.lineCount() - 1;
            const ALTextRange ranges[] = { ALTextRange(doc.start(), doc.end()), ALTextRange(ALTextPos(0, 1), ALTextPos(0, 2)),
                                           ALTextRange(ALTextPos(0, 2), ALTextPos(last, 1)), ALTextRange(doc.end(), ALTextPos(0, 1)),
                                           ALTextRange(ALTextPos(-3, 9), ALTextPos(99, 99)) };
            for (const ALTextRange& range : ranges)
            {
                ensure_equals(what + ": a stretch", doc.byteCount(range), doc.text(range).size());
            }
        };
        agrees("as read");
        doc.insert(ALTextPos(1, 1), "X\r\nY");
        agrees("a break put in");
        doc.replace(ALTextRange(ALTextPos(0, 1), ALTextPos(2, 1)), "z");
        agrees("lines taken out");
        doc.replaceMany({ { ALTextRange(ALTextPos(0, 0), ALTextPos(0, 1)), "ab\ncd" }, { ALTextRange(doc.end(), doc.end()), "\r\n\n" } });
        agrees("a batch");
        doc.remove(ALTextRange(doc.start(), doc.end()));
        agrees("everything gone");
        doc.setText("again\nand again");
        agrees("a text put in whole");
    }

    template<> template<>
    void altextdocument_object::test<23>()
    {
        set_test_name("words as code reads them never stop inside a character: a keycap's mark goes with what joins it, and so does a blank a mark is put on");
        // x, a blank, # with U+FE0F and U+20E3 on it, a blank, y.
        const ALTextDocument keycap("x #\xEF\xB8\x8F\xE2\x83\xA3 y");
        ensure("from the mark, past the keycap and the blank after it", keycap.nextCodeWord(ALTextPos(0, 2)) == ALTextPos(0, 10));
        ensure("back over it whole", keycap.prevCodeWord(ALTextPos(0, 10)) == ALTextPos(0, 2));
        ensure("up to it as before", keycap.nextCodeWord(ALTextPos(0, 0)) == ALTextPos(0, 2));
        // a, a blank with a combining acute on it, z.
        const ALTextDocument accent("a \xCC\x81z");
        ensure("forward past the blank and its mark", accent.nextCodeWord(ALTextPos(0, 0)) == ALTextPos(0, 4));
        ensure("and from the blank itself", accent.nextCodeWord(ALTextPos(0, 1)) == ALTextPos(0, 4));
        ensure("back to the blank the mark is on", accent.prevCodeWord(ALTextPos(0, 4)) == ALTextPos(0, 1));
    }
}
