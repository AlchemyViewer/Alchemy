/**
 * @file tests/altextcarets_test.cpp
 * @brief The selections a text view has besides its main one.
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

#include "altextcarets.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    ALTextRange caret(S32 line, S32 column)
    {
        return ALTextRange(ALTextPos(line, column), ALTextPos(line, column));
    }

    // From an anchor to a caret, on one line.
    ALTextRange sel(S32 line, S32 anchor, S32 caret_column)
    {
        return ALTextRange(ALTextPos(line, anchor), ALTextPos(line, caret_column));
    }

    std::string said(const ALTextRange& r)
    {
        return std::to_string(r.begin.line) + ":" + std::to_string(r.begin.column) + "-" + std::to_string(r.end.line) + ":" +
               std::to_string(r.end.column);
    }

    std::string said(const std::vector<ALTextRange>& all)
    {
        std::string out;
        for (const ALTextRange& r : all)
        {
            out += (out.empty() ? "" : " ") + said(r);
        }
        return out;
    }
}

namespace
{
    TEST_CASE("altextcarets: a caret is pushed past typing; typing at a selection's edge goes outside", "[altextcarets]")
    {
        ALTextDocument doc("abcdef");
        // Typed at 2.
        ALTextDocument::Edit typed = doc.insert(ALTextPos(0, 2), "XY");
        UNSCOPED_INFO("a caret at it pushed past");
        CHECK(said(ALTextCarets::slid(caret(0, 2), typed)) == said(caret(0, 4)));
        UNSCOPED_INFO("one before it stays");
        CHECK(said(ALTextCarets::slid(caret(0, 1), typed)) == said(caret(0, 1)));
        UNSCOPED_INFO("one after it moves along");
        CHECK(said(ALTextCarets::slid(caret(0, 5), typed)) == said(caret(0, 7)));
        UNSCOPED_INFO("a selection that begins at it is pushed whole");
        CHECK(said(ALTextCarets::slid(sel(0, 2, 4), typed)) == said(sel(0, 4, 6)));
        UNSCOPED_INFO("one that ends at it stays, what went in after it");
        CHECK(said(ALTextCarets::slid(sel(0, 0, 2), typed)) == said(sel(0, 0, 2)));
        UNSCOPED_INFO("and backwards the same, the caret still at its start");
        CHECK(said(ALTextCarets::slid(sel(0, 2, 0), typed)) == said(sel(0, 2, 0)));
        UNSCOPED_INFO("one over it grows by it");
        CHECK(said(ALTextCarets::slid(sel(0, 1, 3), typed)) == said(sel(0, 1, 5)));
        UNSCOPED_INFO("and keeps its way");
        CHECK(said(ALTextCarets::slid(sel(0, 3, 1), typed)) == said(sel(0, 5, 1)));

        // "abXYcdef": "Ycd" taken.
        ALTextDocument::Edit taken = doc.remove(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 6)));
        UNSCOPED_INFO("a caret in what was taken goes to where it began");
        CHECK(said(ALTextCarets::slid(caret(0, 5), taken)) == said(caret(0, 3)));
        UNSCOPED_INFO("a selection over its start is cut back to it");
        CHECK(said(ALTextCarets::slid(sel(0, 1, 4), taken)) == said(sel(0, 1, 3)));
        UNSCOPED_INFO("one over its end loses what it took");
        CHECK(said(ALTextCarets::slid(sel(0, 7, 4), taken)) == said(sel(0, 4, 3)));
        UNSCOPED_INFO("one inside it a caret where it began");
        CHECK(said(ALTextCarets::slid(sel(0, 4, 5), taken)) == said(caret(0, 3)));
    }

    TEST_CASE("altextcarets: overlapping selections merge and a caret merges with what it touches", "[altextcarets]")
    {
        ALTextCarets carets;
        carets.assign({ sel(0, 0, 3), sel(0, 3, 6) });
        UNSCOPED_INFO("two side by side stay two");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ sel(0, 0, 3), sel(0, 3, 6) }));
        carets.assign({ sel(0, 0, 4), sel(0, 3, 6) });
        UNSCOPED_INFO("two over each other one");
        CHECK(said(carets.selections()) == said(sel(0, 0, 6)));
        carets.assign({ sel(0, 0, 3), caret(0, 3) });
        UNSCOPED_INFO("a caret at a selection's end in it");
        CHECK(said(carets.selections()) == said(sel(0, 0, 3)));
        carets.assign({ caret(1, 2), sel(1, 6, 2) });
        UNSCOPED_INFO("one at its start too, the selection's way kept");
        CHECK(said(carets.selections()) == said(sel(1, 6, 2)));
        carets.assign({ caret(2, 1), caret(2, 1), caret(0, 1) });
        UNSCOPED_INFO("two carets at one place one, in order");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ caret(0, 1), caret(2, 1) }));
        carets.assign({ sel(0, 0, 2), sel(0, 1, 4), sel(0, 3, 8), caret(0, 10) });
        UNSCOPED_INFO("a run of overlaps one, the one after them not");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ sel(0, 0, 8), caret(0, 10) }));
        carets.add(caret(0, 8));
        UNSCOPED_INFO("one more added merges as it goes in");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ sel(0, 0, 8), caret(0, 10) }));
        carets.add(caret(0, 9));
        UNSCOPED_INFO("and one in a gap stays");
        REQUIRE(carets.size() == static_cast<size_t>(3));
    }

    TEST_CASE("altextcarets: merge folds what the main selection meets into it and keeps its way", "[altextcarets]")
    {
        ALTextCarets carets;
        carets.assign({ caret(0, 2), sel(0, 5, 8), caret(3, 0) });
        ALTextRange main = sel(0, 5, 0);
        UNSCOPED_INFO("the main one as it was");
        CHECK_FALSE(carets.merge(main));
        CHECK(said(main) == said(sel(0, 5, 0)));
        UNSCOPED_INFO("the caret it covers taken in, not the selection it only touches");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ sel(0, 5, 8), caret(3, 0) }));

        main = sel(0, 2, 6);
        UNSCOPED_INFO("changed again");
        CHECK(carets.merge(main));
        UNSCOPED_INFO("over the selection now, its own way kept");
        CHECK(said(main) == said(sel(0, 2, 8)));
        UNSCOPED_INFO("the others left");
        CHECK(said(carets.selections()) == said(caret(3, 0)));

        carets.assign({ sel(1, 9, 3) });
        main = caret(1, 9);
        UNSCOPED_INFO("a caret at a selection's end changed");
        CHECK(carets.merge(main));
        UNSCOPED_INFO("it is the selection, run its way");
        CHECK(said(main) == said(sel(1, 9, 3)));
        UNSCOPED_INFO("nothing else");
        REQUIRE(carets.empty());

        carets.assign({ caret(5, 0) });
        main = caret(0, 0);
        UNSCOPED_INFO("apart, nothing changes");
        CHECK_FALSE(carets.merge(main));
        UNSCOPED_INFO("and nothing goes");
        REQUIRE(carets.size() == static_cast<size_t>(1));
    }

    TEST_CASE("altextcarets: apply slides carets along an edit and merges those it brings together", "[altextcarets]")
    {
        ALTextDocument doc("one two three\nfour five\nsix");
        ALTextCarets   carets;
        carets.assign({ caret(0, 1), caret(0, 5), sel(0, 8, 11), caret(1, 2), caret(2, 1) });
        // "two thr" taken: the caret in it and the selection's start go to
        // where it began, and meet.
        carets.apply(doc.remove(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 11))));
        UNSCOPED_INFO("cut back and merged");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), caret(1, 2), caret(2, 1) }));
        // A line put in above the second: those below move down a line.
        carets.apply(doc.insert(ALTextPos(1, 0), "new\n"));
        UNSCOPED_INFO("moved down");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), caret(2, 2), caret(3, 1) }));
        // A line joined to the one before: the caret on it goes onto that one.
        carets.apply(doc.remove(ALTextRange(ALTextPos(2, 4), ALTextPos(3, 0))));
        UNSCOPED_INFO("onto the line it was joined to");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), caret(2, 2), caret(2, 5) }));
        UNSCOPED_INFO("the text as the carets have it");
        CHECK(doc.text() == std::string("one ee\nnew\nfoursix"));
    }

    TEST_CASE("altextcarets: a batch slides each caret by the stretches before it", "[altextcarets]")
    {
        ALTextDocument doc("a b c d e");
        ALTextCarets   carets;
        carets.assign({ caret(0, 1), caret(0, 3), sel(0, 4, 5), caret(0, 9) });
        // "b" made "BB", and "d" made "" -- the caret at 3 is right after b.
        carets.apply(doc.replaceMany({ { ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)), "BB" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 7)), "" } }));
        UNSCOPED_INFO("each along by what went before it");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), sel(0, 5, 6), caret(0, 9) }));
        CHECK(doc.text() == std::string("a BB c  e"));
        // Text put in at the selection's start and at its end: it keeps
        // what it held; the caret at the end of the text pushed.
        carets.apply(doc.replaceMany({ { ALTextRange(ALTextPos(0, 5), ALTextPos(0, 5)), "<" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 6)), ">" },
                                       { ALTextRange(ALTextPos(0, 9), ALTextPos(0, 9)), "!" } }));
        UNSCOPED_INFO("the selection still over c alone, the last caret past the !");
        CHECK(said(carets.selections()) == said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), sel(0, 6, 7), caret(0, 12) }));
        CHECK(doc.text() == std::string("a BB <c>  e!"));
    }

    TEST_CASE("altextcarets: onLine gives the carets that begin on a line or reach it from above", "[altextcarets]")
    {
        ALTextCarets carets;
        carets.assign({ caret(0, 0), ALTextRange(ALTextPos(4, 2), ALTextPos(1, 3)), caret(2, 0), caret(2, 4), caret(3, 1) });
        const auto on = [&](S32 line) {
            std::vector<ALTextRange> out;
            for (auto [it, end] = carets.onLine(line); it != end; ++it)
            {
                const ALTextRange r = it->normalised();
                if (r.begin.line <= line && line <= r.end.line)
                {
                    out.push_back(*it);
                }
            }
            return said(out);
        };
        // The long one takes in what it covers.
        UNSCOPED_INFO("all, merged");
        REQUIRE(carets.size() == static_cast<size_t>(2));
        CHECK(on(0) == said(caret(0, 0)));
        UNSCOPED_INFO("line 2 is the long one's");
        CHECK(on(2) == said(ALTextRange(ALTextPos(4, 2), ALTextPos(1, 3))));
        CHECK(on(5) == std::string());
    }
}
