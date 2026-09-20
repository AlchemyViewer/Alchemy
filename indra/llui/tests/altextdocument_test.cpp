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

#include "../altextdocument.h"

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
    }

    template<> template<>
    void altextdocument_object::test<11>()
    {
        set_test_name("find: forward, wrapping, backwards, whole words, and without regard to case");
        ALTextDocument doc("Foo foobar\n\xC3\x89ric foo\nfoo");
        ALTextDocument::FindOptions options;

        auto found = doc.find("foo", ALTextPos(0, 0), options);
        ensure("found", found.has_value());
        ensure("the first exact one", *found == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)));

        found = doc.find("foo", ALTextPos(0, 5), options);
        ensure("the next, on the next line", found && *found == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));

        found = doc.find("foo", ALTextPos(2, 1), options);
        ensure("wraps round to the first", found && *found == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 7)));

        options.wrap = false;
        ensure("or not", !doc.find("foo", ALTextPos(2, 1), options));
        options.wrap = true;

        options.caseInsensitive = true;
        found = doc.find("foo", ALTextPos(0, 0), options);
        ensure("case: the capital one", found && *found == ALTextRange(ALTextPos(0, 0), ALTextPos(0, 3)));
        found = doc.find("\xC3\xA9ric", ALTextPos(0, 0), options);
        ensure("case above ASCII", found && *found == ALTextRange(ALTextPos(1, 0), ALTextPos(1, 5)));
        options.caseInsensitive = false;

        options.wholeWord = true;
        found = doc.find("foo", ALTextPos(0, 0), options);
        ensure("a whole word, not the start of foobar", found && *found == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));
        options.wholeWord = false;

        options.backwards = true;
        found = doc.find("foo", ALTextPos(2, 0), options);
        ensure("back: the one before", found && *found == ALTextRange(ALTextPos(1, 6), ALTextPos(1, 9)));
        found = doc.find("foo", ALTextPos(0, 4), options);
        ensure("back from the first wraps to the last", found && *found == ALTextRange(ALTextPos(2, 0), ALTextPos(2, 3)));

        ensure("nothing for nothing", !doc.find("", ALTextPos(0, 0)));
        ensure("nothing across lines", !doc.find("bar\n", ALTextPos(0, 0)));
    }

    template<> template<>
    void altextdocument_object::test<12>()
    {
        set_test_name("a log: text arrives at the end and the oldest lines go");
        ALTextDocument doc;
        doc.append("one\ntwo");
        doc.append("\nthree");
        ensure_equals("three lines", doc.lineCount(), 3);
        ALTextDocument::Edit gone = doc.removeFirstLines(2);
        ensure_equals("one left", doc.text(), std::string("three"));
        ensure_equals("what went", gone.removed, std::string("one\ntwo\n"));
        ensure("everything can go", doc.removeFirstLines(5).removed == "three" && doc.empty());
        ensure("nothing to go", doc.removeFirstLines(0).nothing());
    }
}
