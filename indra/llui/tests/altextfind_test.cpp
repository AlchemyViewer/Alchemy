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

#include "../altextfind.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    struct altextfind_data
    {
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
        ensure_equals("every one for a replace", find.all(doc, "z", ALTextSearchOptions()).size(), ALTextFind::LIMIT + 2);
        std::vector<ALTextRange> taken = find.take();
        ensure("taken", find.count() == 0 && taken.size() == ALTextFind::LIMIT);
        find.restore(std::move(taken), 3);
        ensure("put back", find.count() == ALTextFind::LIMIT && find.current() == 3);
        find.stale();
        ensure("to be looked through again, not yet", find.isStale() && !find.due());
        find.clear();
        ensure("put away", find.count() == 0 && !find.isStale() && find.current() == -1);
    }
}
