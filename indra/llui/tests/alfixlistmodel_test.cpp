/**
 * @file alfixlistmodel_test.cpp
 * @brief What a code editor's quick-fix list offers, in what order, and as refactors come in.
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

#include "../alfixlistmodel.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

namespace tut
{
    struct alfixlistmodel_data
    {
        ALFixListModel model;

        static ALCodeFix fix(const std::string& title, bool preferred = false, bool suppress = false)
        {
            ALCodeFix one;
            one.title     = title;
            one.preferred = preferred;
            one.suppress  = suppress;
            one.edits.emplace_back(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), title);
            return one;
        }
        static std::string titles(const std::vector<ALCodeFix>& fixes)
        {
            std::string out;
            for (const ALCodeFix& one : fixes)
            {
                out += (out.empty() ? "" : "|") + one.title;
            }
            return out;
        }
    };
    typedef test_group<alfixlistmodel_data> alfixlistmodel_group;
    typedef alfixlistmodel_group::object    alfixlistmodel_object;
    alfixlistmodel_group                    alfixlistmodel_instance("ALFixListModel");

    template<> template<>
    void alfixlistmodel_object::test<1>()
    {
        set_test_name("the preferred first, then the other fixes, the refactors, and a suppression last, each kind as given");
        ALCodeFix refactor = fix("Extract");
        refactor.refactor  = true;
        std::vector<ALCodeFix> fixes = { fix("Suppress", false, true), refactor, fix("Cast"), fix("Declare", true), fix("Rename") };
        ALFixListModel::rank(fixes);
        ensure_equals("ranked", titles(fixes), std::string("Declare|Cast|Rename|Extract|Suppress"));
    }

    template<> template<>
    void alfixlistmodel_object::test<2>()
    {
        set_test_name("each showing counted; notes put after the fixes of the showing they are for, one each, and nothing otherwise");
        const U32 first = model.show(4, { fix("A"), fix("B") });
        ensure("the line", model.line() == 4);
        const U32 second = model.show(4, { fix("A"), fix("B") });
        ensure("counted", second == first + 1);
        ensure("an earlier showing's notes dropped", !model.note(first, { "1", "2" }));
        ensure("not one each", !model.note(second, { "1" }));
        ensure("this showing's", model.note(second, { "-12 bytes", "" }) && model.fixes()[0].note == "-12 bytes");
        model.close();
        ensure("closed", model.fixes().empty() && model.line() == -1);
    }

    template<> template<>
    void alfixlistmodel_object::test<3>()
    {
        set_test_name("refactors join the list they were asked over, keeping the choice; alone where it had none; a word where there is nothing");
        const ALTextRange at(ALTextPos(2, 1), ALTextPos(2, 5));
        const ALTextPos   caret(2, 5);
        model.show(2, { fix("Declare", true), fix("Suppress", false, true) });
        ensure("not asked for: nothing", !model.join(at, caret, { fix("Extract") }, true, 1));
        model.ask(at, caret);
        ensure("awaited", model.awaited());
        ensure("another place's: nothing", !model.join(ALTextRange(ALTextPos(3, 0), ALTextPos(3, 0)), caret, { fix("Extract") }, true, 1));
        ensure("the caret moved: nothing", !model.join(at, ALTextPos(2, 4), { fix("Extract") }, true, 1));
        std::optional<ALFixListModel::Joined> joined = model.join(at, caret, { fix("Extract") }, true, 1);
        ensure("joined", joined.has_value());
        ensure_equals("after the fixes, before the suppression", titles(joined->fixes), std::string("Declare|Extract|Suppress"));
        ensure("a refactor", joined->fixes[1].refactor);
        ensure("the suppression still chosen", joined->chosen == 2);
        ensure("no longer awaited", !model.awaited());

        model.show(2, { fix("Declare", true) });
        model.ask(at, caret);
        ensure("nothing new for an open list: left as it is", !model.join(at, caret, {}, true, 0));
        model.close();
        model.ask(at, caret);
        joined = model.join(at, caret, { fix("Extract") }, false, -1);
        ensure_equals("alone, the list having closed", titles(joined->fixes), std::string("Extract"));
        model.ask(at, caret);
        joined = model.join(at, caret, {}, false, -1);
        ensure("nothing at all: a word that says so, which makes nothing",
               joined && joined->fixes.size() == 1 && ALFixListModel::isNothing(joined->fixes[0]) && !joined->fixes[0].title.empty());
        model.ask(at, caret);
        model.close();
        ensure("closed: not awaited", !model.awaited());
    }
}
