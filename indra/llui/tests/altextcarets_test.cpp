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

#include "../test/lltut.h"

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

namespace tut
{
    struct altextcarets_data
    {
    };
    typedef test_group<altextcarets_data> altextcarets_group;
    typedef altextcarets_group::object    altextcarets_object;
    altextcarets_group                    altextcarets_instance("altextcarets");

    template<> template<>
    void altextcarets_object::test<1>()
    {
        set_test_name("a caret is pushed past what is typed at it; a selection keeps what it held, what goes in at either edge outside it");
        ALTextDocument doc("abcdef");
        // Typed at 2.
        ALTextDocument::Edit typed = doc.insert(ALTextPos(0, 2), "XY");
        ensure_equals("a caret at it pushed past", said(ALTextCarets::slid(caret(0, 2), typed)), said(caret(0, 4)));
        ensure_equals("one before it stays", said(ALTextCarets::slid(caret(0, 1), typed)), said(caret(0, 1)));
        ensure_equals("one after it moves along", said(ALTextCarets::slid(caret(0, 5), typed)), said(caret(0, 7)));
        ensure_equals("a selection that begins at it is pushed whole", said(ALTextCarets::slid(sel(0, 2, 4), typed)), said(sel(0, 4, 6)));
        ensure_equals("one that ends at it stays, what went in after it", said(ALTextCarets::slid(sel(0, 0, 2), typed)), said(sel(0, 0, 2)));
        ensure_equals("and backwards the same, the caret still at its start", said(ALTextCarets::slid(sel(0, 2, 0), typed)), said(sel(0, 2, 0)));
        ensure_equals("one over it grows by it", said(ALTextCarets::slid(sel(0, 1, 3), typed)), said(sel(0, 1, 5)));
        ensure_equals("and keeps its way", said(ALTextCarets::slid(sel(0, 3, 1), typed)), said(sel(0, 5, 1)));

        // "abXYcdef": "Ycd" taken.
        ALTextDocument::Edit taken = doc.remove(ALTextRange(ALTextPos(0, 3), ALTextPos(0, 6)));
        ensure_equals("a caret in what was taken goes to where it began", said(ALTextCarets::slid(caret(0, 5), taken)), said(caret(0, 3)));
        ensure_equals("a selection over its start is cut back to it", said(ALTextCarets::slid(sel(0, 1, 4), taken)), said(sel(0, 1, 3)));
        ensure_equals("one over its end loses what it took", said(ALTextCarets::slid(sel(0, 7, 4), taken)), said(sel(0, 4, 3)));
        ensure_equals("one inside it a caret where it began", said(ALTextCarets::slid(sel(0, 4, 5), taken)), said(caret(0, 3)));
    }

    template<> template<>
    void altextcarets_object::test<2>()
    {
        set_test_name("those that overlap become one, and so do a caret and what it touches; two selections that only touch stay two");
        ALTextCarets carets;
        carets.assign({ sel(0, 0, 3), sel(0, 3, 6) });
        ensure_equals("two side by side stay two", said(carets.selections()), said(std::vector<ALTextRange>{ sel(0, 0, 3), sel(0, 3, 6) }));
        carets.assign({ sel(0, 0, 4), sel(0, 3, 6) });
        ensure_equals("two over each other one", said(carets.selections()), said(sel(0, 0, 6)));
        carets.assign({ sel(0, 0, 3), caret(0, 3) });
        ensure_equals("a caret at a selection's end in it", said(carets.selections()), said(sel(0, 0, 3)));
        carets.assign({ caret(1, 2), sel(1, 6, 2) });
        ensure_equals("one at its start too, the selection's way kept", said(carets.selections()), said(sel(1, 6, 2)));
        carets.assign({ caret(2, 1), caret(2, 1), caret(0, 1) });
        ensure_equals("two carets at one place one, in order", said(carets.selections()), said(std::vector<ALTextRange>{ caret(0, 1), caret(2, 1) }));
        carets.assign({ sel(0, 0, 2), sel(0, 1, 4), sel(0, 3, 8), caret(0, 10) });
        ensure_equals("a run of overlaps one, the one after them not", said(carets.selections()),
                      said(std::vector<ALTextRange>{ sel(0, 0, 8), caret(0, 10) }));
        carets.add(caret(0, 8));
        ensure_equals("one more added merges as it goes in", said(carets.selections()), said(std::vector<ALTextRange>{ sel(0, 0, 8), caret(0, 10) }));
        carets.add(caret(0, 9));
        ensure_equals("and one in a gap stays", carets.size(), static_cast<size_t>(3));
    }

    template<> template<>
    void altextcarets_object::test<3>()
    {
        set_test_name("the main selection takes in any it meets, and keeps its way; a caret as main takes the way of what it took in");
        ALTextCarets carets;
        carets.assign({ caret(0, 2), sel(0, 5, 8), caret(3, 0) });
        ALTextRange main = sel(0, 5, 0);
        ensure("the main one as it was", !carets.merge(main));
        ensure_equals("and still so", said(main), said(sel(0, 5, 0)));
        ensure_equals("the caret it covers taken in, not the selection it only touches", said(carets.selections()),
                      said(std::vector<ALTextRange>{ sel(0, 5, 8), caret(3, 0) }));

        main = sel(0, 2, 6);
        ensure("changed again", carets.merge(main));
        ensure_equals("over the selection now, its own way kept", said(main), said(sel(0, 2, 8)));
        ensure_equals("the others left", said(carets.selections()), said(caret(3, 0)));

        carets.assign({ sel(1, 9, 3) });
        main = caret(1, 9);
        ensure("a caret at a selection's end changed", carets.merge(main));
        ensure_equals("it is the selection, run its way", said(main), said(sel(1, 9, 3)));
        ensure("nothing else", carets.empty());

        carets.assign({ caret(5, 0) });
        main = caret(0, 0);
        ensure("apart, nothing changes", !carets.merge(main));
        ensure_equals("and nothing goes", carets.size(), static_cast<size_t>(1));
    }

    template<> template<>
    void altextcarets_object::test<4>()
    {
        set_test_name("an edit slides each along, those it brought together merged; those before it and below it untouched");
        ALTextDocument doc("one two three\nfour five\nsix");
        ALTextCarets   carets;
        carets.assign({ caret(0, 1), caret(0, 5), sel(0, 8, 11), caret(1, 2), caret(2, 1) });
        // "two thr" taken: the caret in it and the selection's start go to
        // where it began, and meet.
        carets.apply(doc.remove(ALTextRange(ALTextPos(0, 4), ALTextPos(0, 11))));
        ensure_equals("cut back and merged", said(carets.selections()),
                      said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), caret(1, 2), caret(2, 1) }));
        // A line put in above the second: those below move down a line.
        carets.apply(doc.insert(ALTextPos(1, 0), "new\n"));
        ensure_equals("moved down", said(carets.selections()),
                      said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), caret(2, 2), caret(3, 1) }));
        // A line joined to the one before: the caret on it goes onto that one.
        carets.apply(doc.remove(ALTextRange(ALTextPos(2, 4), ALTextPos(3, 0))));
        ensure_equals("onto the line it was joined to", said(carets.selections()),
                      said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), caret(2, 2), caret(2, 5) }));
        ensure_equals("the text as the carets have it", doc.text(), std::string("one ee\nnew\nfoursix"));
    }

    template<> template<>
    void altextcarets_object::test<5>()
    {
        set_test_name("a batch's stretches slide each by the stretches before it; a caret in one goes to where it begins");
        ALTextDocument doc("a b c d e");
        ALTextCarets   carets;
        carets.assign({ caret(0, 1), caret(0, 3), sel(0, 4, 5), caret(0, 9) });
        // "b" made "BB", and "d" made "" -- the caret at 3 is right after b.
        carets.apply(doc.replaceMany({ { ALTextRange(ALTextPos(0, 2), ALTextPos(0, 3)), "BB" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 7)), "" } }));
        ensure_equals("each along by what went before it", said(carets.selections()),
                      said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), sel(0, 5, 6), caret(0, 9) }));
        ensure_equals("the text", doc.text(), std::string("a BB c  e"));
        // Text put in at the selection's start and at its end: it keeps
        // what it held; the caret at the end of the text pushed.
        carets.apply(doc.replaceMany({ { ALTextRange(ALTextPos(0, 5), ALTextPos(0, 5)), "<" }, { ALTextRange(ALTextPos(0, 6), ALTextPos(0, 6)), ">" },
                                       { ALTextRange(ALTextPos(0, 9), ALTextPos(0, 9)), "!" } }));
        ensure_equals("the selection still over c alone, the last caret past the !", said(carets.selections()),
                      said(std::vector<ALTextRange>{ caret(0, 1), caret(0, 4), sel(0, 6, 7), caret(0, 12) }));
        ensure_equals("the text again", doc.text(), std::string("a BB <c>  e!"));
    }

    template<> template<>
    void altextcarets_object::test<6>()
    {
        set_test_name("those that may lie on a line: one that began above and reaches it, and those that begin on it");
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
        ensure_equals("all, merged", carets.size(), static_cast<size_t>(2));
        ensure_equals("line 0", on(0), said(caret(0, 0)));
        ensure_equals("line 2 is the long one's", on(2), said(ALTextRange(ALTextPos(4, 2), ALTextPos(1, 3))));
        ensure_equals("line 5 nothing", on(5), std::string());
    }
}
