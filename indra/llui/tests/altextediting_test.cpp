/**
 * @file altextediting_test.cpp
 * @brief A text view's commands over whole lines, over a document with nothing drawn.
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

#include "../altextediting.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    using namespace ALTextEditing;

    struct altextediting_data
    {
        // The text a change leaves, its replacements made in order.
        static std::string applied(const std::string& text, const Change& change)
        {
            ALTextDocument doc(text);
            for (const Replacement& one : change.replacements)
            {
                doc.replace(one.range, one.text);
            }
            return doc.text();
        }
    };

    typedef test_group<altextediting_data> altextediting_group;
    typedef altextediting_group::object    altextediting_object;
    altextediting_group                    altextediting_instance("altextediting");

    template<> template<>
    void altextediting_object::test<1>()
    {
        set_test_name("the lines a selection covers, a selection ending at a line's start not taking it");
        ensure("a caret's line", selectedLines(ALTextRange(ALTextPos(2, 3), ALTextPos(2, 3))) == std::pair<S32, S32>(2, 2));
        ensure("backwards, and not the line it ends at the start of",
               selectedLines(ALTextRange(ALTextPos(4, 0), ALTextPos(1, 2))) == std::pair<S32, S32>(1, 3));
        ensure("ending inside a line takes it", selectedLines(ALTextRange(ALTextPos(1, 0), ALTextPos(3, 1))) == std::pair<S32, S32>(1, 3));
    }

    template<> template<>
    void altextediting_object::test<2>()
    {
        set_test_name("lines duplicated, moved and deleted, the selection going with them");
        const std::string    text = "one\ntwo\nthree";
        const ALTextDocument doc(text);

        const Change copy = duplicateLines(doc, ALTextPos(0, 1), ALTextPos(1, 2));
        ensure_equals("the lines again under them", applied(text, copy), std::string("one\ntwo\none\ntwo\nthree"));
        ensure("the selection on the copy", copy.selects && copy.anchor == ALTextPos(2, 1) && copy.caret == ALTextPos(3, 2));

        const std::optional<Change> up = moveLines(doc, ALTextPos(1, 1), ALTextPos(1, 1), -1);
        ensure("up", up.has_value());
        ensure_equals("past the one above", applied(text, *up), std::string("two\none\nthree"));
        ensure("the caret with it", up->caret == ALTextPos(0, 1));
        const std::optional<Change> down = moveLines(doc, ALTextPos(0, 0), ALTextPos(1, 0), 1);
        ensure("a selection ending at a line's start takes one line", down.has_value());
        ensure_equals("past the one below", applied(text, *down), std::string("two\none\nthree"));
        ensure("not past the top", !moveLines(doc, ALTextPos(0, 2), ALTextPos(0, 2), -1));
        ensure("nor the bottom", !moveLines(doc, ALTextPos(2, 2), ALTextPos(2, 2), 1));

        const Change middle = deleteLines(doc, ALTextPos(1, 3), ALTextPos(1, 3));
        ensure_equals("gone", applied(text, middle), std::string("one\nthree"));
        ensure("the caret on the line that took its place", !middle.selects && middle.caret == ALTextPos(1, 3));
        const Change last = deleteLines(doc, ALTextPos(2, 5), ALTextPos(2, 5));
        ensure_equals("the last with the break before it", applied(text, last), std::string("one\ntwo"));
        ensure("the caret on the line above, no further than it goes", last.caret == ALTextPos(1, 3));
        const Change all = deleteLines(doc, ALTextPos(0, 2), ALTextPos(2, 1));
        ensure_equals("all of them", applied(text, all), std::string());
        ensure("the caret at the start", all.caret == ALTextPos(0, 0));
    }

    template<> template<>
    void altextediting_object::test<3>()
    {
        set_test_name("lines commented out where any is not, back in where they all are, and a caret kept in its text");
        const std::string    text = "  a\n\n  // b\nc";
        const ALTextDocument doc(text);
        const std::optional<Change> out = toggleComment(doc, ALTextPos(0, 0), ALTextPos(3, 1), "//");
        ensure("commented", out.has_value());
        ensure_equals("every line saying anything, at its text", applied(text, *out), std::string("  // a\n\n  // // b\n// c"));
        ensure("the lines whole, to the end of the last", out->selects && out->anchor == ALTextPos(0, 0) && out->caret == ALTextPos(3, 4));

        const std::string    commented = "\t// x\n\t//y\n";
        const ALTextDocument commented_doc(commented);
        const std::optional<Change> back = toggleComment(commented_doc, ALTextPos(0, 5), ALTextPos(0, 5), "//");
        ensure("uncommented", back.has_value());
        ensure_equals("with the space after, where there is one", applied(commented, *back), std::string("\tx\n\t//y\n"));
        ensure("the caret kept in its text", !back->selects && back->caret == ALTextPos(0, 2));
        const std::optional<Change> both = toggleComment(commented_doc, ALTextPos(0, 0), ALTextPos(2, 0), "//");
        ensure_equals("all of them back in", applied(commented, *both), std::string("\tx\n\ty\n"));
        ensure("to the start of the line after", both->caret == ALTextPos(2, 0));

        const ALTextDocument blank("   \n");
        ensure("nothing where no line says anything", !toggleComment(blank, ALTextPos(0, 1), ALTextPos(0, 1), "//"));
    }

    template<> template<>
    void altextediting_object::test<4>()
    {
        set_test_name("lines joined as vim's J joins them, or with their blanks kept as its gJ");
        const std::string text = "call(a,\n    b\n    )\n\nend";
        const ALTextDocument doc(text);
        const std::optional<Change> j = joinLines(doc, 0, 2, false);
        ensure("joined", j.has_value());
        ensure_equals("one space, none before the )", applied(text, *j), std::string("call(a, b)\n\nend"));
        ensure("the caret where the first join is", j->caret == ALTextPos(0, 7));
        ensure_equals("a blank line takes no space", applied(text, *joinLines(doc, 2, 4, false)), std::string("call(a,\n    b\n    ) end"));
        ensure_equals("the blanks kept", applied(text, *joinLines(doc, 0, 1, true)), std::string("call(a,    b\n    )\n\nend"));
        ensure("one line is nothing to join", !joinLines(doc, 4, 4, false) && !joinLines(doc, 4, 9, false));
    }
}
