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

#include "alfixlistmodel.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
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

    TEST_CASE_METHOD(alfixlistmodel_data, "ALFixListModel: rank puts preferred then fixes then refactors then a suppression", "[ALFixListModel]")
    {
        ALCodeFix refactor = fix("Extract");
        refactor.refactor  = true;
        std::vector<ALCodeFix> fixes = { fix("Suppress", false, true), refactor, fix("Cast"), fix("Declare", true), fix("Rename") };
        ALFixListModel::rank(fixes);
        CHECK(titles(fixes) == std::string("Declare|Cast|Rename|Extract|Suppress"));
    }

    TEST_CASE_METHOD(alfixlistmodel_data, "ALFixListModel: notes are taken one per fix and only for the latest showing", "[ALFixListModel]")
    {
        const U32 first = model.show(4, { fix("A"), fix("B") });
        CHECK(model.line() == 4);
        const U32 second = model.show(4, { fix("A"), fix("B") });
        CHECK(second == first + 1);
        UNSCOPED_INFO("an earlier showing's notes dropped");
        CHECK_FALSE(model.note(first, { "1", "2" }));
        UNSCOPED_INFO("not one each");
        CHECK_FALSE(model.note(second, { "1" }));
        {
            INFO("this showing's");
            CHECK(model.note(second, { "-12 bytes", "" }));
            REQUIRE(model.fixes().size() == 2);
            CHECK(model.fixes()[0].note == "-12 bytes");
        }
        model.close();
        CHECK(model.fixes().empty());
        CHECK(model.line() == -1);
    }

    TEST_CASE_METHOD(alfixlistmodel_data, "ALFixListModel: refactors join the asked-for list keeping the choice or stand alone", "[ALFixListModel]")
    {
        const ALTextRange at(ALTextPos(2, 1), ALTextPos(2, 5));
        const ALTextPos   caret(2, 5);
        model.show(2, { fix("Declare", true), fix("Suppress", false, true) });
        UNSCOPED_INFO("not asked for: nothing");
        CHECK_FALSE(model.join(at, caret, { fix("Extract") }, true, 1));
        model.ask(at, caret);
        CHECK(model.awaited());
        UNSCOPED_INFO("another place's: nothing");
        CHECK_FALSE(model.join(ALTextRange(ALTextPos(3, 0), ALTextPos(3, 0)), caret, { fix("Extract") }, true, 1));
        UNSCOPED_INFO("the caret moved: nothing");
        CHECK_FALSE(model.join(at, ALTextPos(2, 4), { fix("Extract") }, true, 1));
        std::optional<ALFixListModel::Joined> joined = model.join(at, caret, { fix("Extract") }, true, 1);
        REQUIRE(joined.has_value());
        UNSCOPED_INFO("after the fixes, before the suppression");
        REQUIRE(titles(joined->fixes) == std::string("Declare|Extract|Suppress"));
        CHECK(joined->fixes[1].refactor);
        UNSCOPED_INFO("the suppression still chosen");
        CHECK(joined->chosen == 2);
        CHECK_FALSE(model.awaited());

        model.show(2, { fix("Declare", true) });
        model.ask(at, caret);
        UNSCOPED_INFO("nothing new for an open list: left as it is");
        CHECK_FALSE(model.join(at, caret, {}, true, 0));
        model.close();
        model.ask(at, caret);
        joined = model.join(at, caret, { fix("Extract") }, false, -1);
        REQUIRE(joined.has_value());
        UNSCOPED_INFO("alone, the list having closed");
        CHECK(titles(joined->fixes) == std::string("Extract"));
        model.ask(at, caret);
        joined = model.join(at, caret, {}, false, -1);
        {
            INFO("nothing at all: a word that says so, which makes nothing");
            REQUIRE(joined);
            REQUIRE(joined->fixes.size() == 1);
            CHECK(ALFixListModel::isNothing(joined->fixes[0]));
            CHECK_FALSE(joined->fixes[0].title.empty());
        }
        model.ask(at, caret);
        model.close();
        UNSCOPED_INFO("closed: not awaited");
        CHECK_FALSE(model.awaited());
    }
}
