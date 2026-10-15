/**
 * @file alscopebar_test.cpp
 * @brief A sentence: what it composes, and how it lays itself out.
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

#include "alscopebar.h"

#include "../llcombobox.h"
#include "../lllineeditor.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "llcallbacklist.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alscopebar_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALScopeBar* make(S32 width = 400)
        {
            ALScopeBar::Params p(LLUICtrlFactory::getDefaultParams<ALScopeBar>());
            p.rect = LLRect(0, 30, width, 0);
            return LLUICtrlFactory::create<ALScopeBar>(p);
        }

        // Find [field] [how] `query` in [where]
        static std::vector<ALScopeBar::Segment> sentence()
        {
            std::vector<ALScopeBar::Segment> said;

            ALScopeBar::Segment find;
            find.kind = ALScopeBar::Segment::Kind::Word;
            find.text = "Find";
            said.push_back(find);

            ALScopeBar::Segment what;
            what.kind = ALScopeBar::Segment::Kind::Choice;
            what.name = "field";
            what.choices = { { "Anything", "any" }, { "Attribute name", "attribute" } };
            said.push_back(what);

            ALScopeBar::Segment how;
            how.kind = ALScopeBar::Segment::Kind::Choice;
            how.name = "how";
            how.choices = { { "containing", "containing" }, { "starting with", "starting" } };
            said.push_back(how);

            ALScopeBar::Segment query;
            query.kind = ALScopeBar::Segment::Kind::Field;
            query.name = "query";
            query.text = "what to look for";
            said.push_back(query);

            ALScopeBar::Segment in;
            in.kind = ALScopeBar::Segment::Kind::Word;
            in.text = "in";
            said.push_back(in);

            ALScopeBar::Segment where;
            where.kind = ALScopeBar::Segment::Kind::Choice;
            where.name = "scope";
            where.choices = { { "every skin", "all" }, { "this skin", "skin" } };
            said.push_back(where);

            return said;
        }
    };

    TEST_CASE_METHOD(alscopebar_data, "alscopebar: query reads back each choosing part by name and leaves words out", "[alscopebar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make();
        bar->setSentence(sentence());

        const LLSD said = bar->query();
        UNSCOPED_INFO("four parts choose something");
        REQUIRE(said.size() == 4);
        {
            INFO("each choice starts at its first option");
            CHECK(said["field"].asString() == std::string("any"));
            CHECK(said["how"].asString() == std::string("containing"));
            CHECK(said["scope"].asString() == std::string("all"));
        }
        CHECK(said["query"].asString().empty());
        CHECK_FALSE(said.has("Find"));

        bar->setValue("query", "close");
        bar->setValue("scope", "skin");
        CHECK(bar->valueOf("query") == std::string("close"));
        CHECK(bar->valueOf("scope") == std::string("skin"));
        delete bar;
    }

    // Every part as wide as what is in it, and the field with whatever is
    // left: that is the difference between a sentence and a row of columns.
    TEST_CASE_METHOD(alscopebar_data, "alscopebar: each part is as wide as its content and the field takes the rest", "[alscopebar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make(500);
        bar->setSentence(sentence());

        const LLLineEditor* field = bar->getChild<LLLineEditor>("query");
        const LLComboBox* scope = bar->getChild<LLComboBox>("scope");
        REQUIRE(field != nullptr);
        REQUIRE(scope != nullptr);

        // In the order the sentence reads, left to right, none overlapping.
        CHECK(field->getRect().mRight <= scope->getRect().mLeft);
        CHECK(field->getRect().getWidth() > scope->getRect().getWidth());
        UNSCOPED_INFO("everything fits in the bar");
        CHECK(scope->getRect().mRight <= 500);

        // Narrower, and the field is what gives up the room: the choices keep
        // theirs.
        const LLComboBox* what = bar->getChild<LLComboBox>("field");
        const LLComboBox* how = bar->getChild<LLComboBox>("how");
        const S32 was = field->getRect().getWidth();
        const S32 what_was = what->getRect().getWidth();
        const S32 how_was = how->getRect().getWidth();
        const S32 scope_was = scope->getRect().getWidth();
        bar->reshape(360, 30);
        CHECK(field->getRect().getWidth() < was);
        CHECK(what->getRect().getWidth() == what_was);
        CHECK(how->getRect().getWidth() == how_was);
        CHECK(scope->getRect().getWidth() == scope_was);
        delete bar;
    }

    // A caller may rewrite the sentence as one answer changes; what is
    // already chosen in the parts that are still there stays chosen.
    TEST_CASE_METHOD(alscopebar_data, "alscopebar: a rewritten sentence keeps what its remaining parts hold", "[alscopebar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make();
        bar->setSentence(sentence());
        bar->setValue("query", "close");
        bar->setValue("scope", "skin");

        std::vector<ALScopeBar::Segment> shorter = sentence();
        shorter.pop_back();     // the scope goes
        bar->setSentence(shorter);

        CHECK(bar->valueOf("query") == std::string("close"));
        UNSCOPED_INFO("and the part that went says nothing");
        CHECK(bar->valueOf("scope").empty());
        delete bar;
    }

    // Chosen or typed is one thing; committed is another, because a caller
    // that searches every file cannot do it on every keystroke.
    TEST_CASE_METHOD(alscopebar_data, "alscopebar: a choice is a change and only committing the field runs", "[alscopebar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make();
        bar->setSentence(sentence());

        S32 changed = 0;
        S32 ran = 0;
        bar->onChanged([&changed]() { ++changed; });
        bar->onRun([&ran]() { ++ran; });

        bar->getChild<LLComboBox>("scope")->setSelectedByValue(LLSD("skin"), true);
        bar->getChild<LLComboBox>("scope")->onCommit();
        CHECK(changed == 1);
        CHECK(ran == 0);

        bar->getChild<LLLineEditor>("query")->onCommit();
        CHECK(ran == 1);
        delete bar;
    }

    // The slot at the right end, for whatever the caller wants beside the
    // sentence: it keeps its width and the field gives up the room.
    TEST_CASE_METHOD(alscopebar_data, "alscopebar: the adornment sits at the right end at its own width", "[alscopebar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make(500);
        bar->setSentence(sentence());
        const S32 was = bar->getChild<LLLineEditor>("query")->getRect().getWidth();

        LLTextBox::Params p(LLUICtrlFactory::getDefaultParams<LLTextBox>());
        p.name = "count";
        p.rect = LLRect(0, 22, 90, 0);
        p.initial_value = "3 results";
        bar->setAdornment(LLUICtrlFactory::create<LLTextBox>(p));

        const LLView* count = bar->getChild<LLView>("count");
        REQUIRE(count != nullptr);
        CHECK(count->getRect().mRight == 500);
        CHECK(count->getRect().getWidth() == 90);
        CHECK(bar->getChild<LLLineEditor>("query")->getRect().getWidth() < was);
        delete bar;
    }

    // A caller answering one part by rewriting the sentence does it from
    // inside that part's own callback. The part is still on the stack, so
    // the sentence is built again once it has finished, and until then the
    // old parts go on answering for the old sentence.
    TEST_CASE_METHOD(alscopebar_data, "alscopebar: a sentence rewritten from a part's own callback waits for idle", "[alscopebar]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make(500);
        bar->setSentence(sentence());
        bar->setValue("query", "close");

        // The answer to a change: the same sentence without its last part.
        S32 changed = 0;
        bar->onChanged([bar, &changed]()
        {
            ++changed;
            std::vector<ALScopeBar::Segment> shorter = alscopebar_data::sentence();
            shorter.pop_back();
            shorter.pop_back();
            bar->setSentence(std::move(shorter));
        });

        LLComboBox* how = bar->getChild<LLComboBox>("how");
        how->setValue("starting");
        how->onCommit();
        CHECK(changed == 1);
        UNSCOPED_INFO("the part that was pressed is still there");
        CHECK(bar->findChild<LLComboBox>("how") == how);
        UNSCOPED_INFO("and so is the one the new sentence drops");
        CHECK(bar->findChild<LLComboBox>("scope") != nullptr);
        UNSCOPED_INFO("and the query still answers");
        CHECK(bar->valueOf("query") == std::string("close"));

        gIdleCallbacks.callFunctions();
        UNSCOPED_INFO("once the part has finished, the sentence is the new one");
        CHECK(bar->findChild<LLComboBox>("scope") == nullptr);
        UNSCOPED_INFO("with what was chosen kept");
        CHECK(bar->valueOf("how") == std::string("starting"));
        UNSCOPED_INFO("and what was typed");
        CHECK(bar->valueOf("query") == std::string("close"));
        UNSCOPED_INFO("and nothing heard twice");
        CHECK(changed == 1);
        delete bar;
    }
}
