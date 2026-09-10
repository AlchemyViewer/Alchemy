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

#include "../alscopebar.h"

#include "../llcombobox.h"
#include "../lllineeditor.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "llcallbacklist.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gScopeTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gScopeTestAnonName;
}

namespace tut
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

    typedef test_group<alscopebar_data> alscopebar_test;
    typedef alscopebar_test::object     alscopebar_object;
    tut::alscopebar_test alscopebar_testgroup("alscopebar");

    // The whole of it, read back by the names its parts were given. A word
    // chooses nothing and is not in the query.
    template<> template<>
    void alscopebar_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make();
        bar->setSentence(sentence());

        const LLSD said = bar->query();
        ensure_equals("four parts choose something", said.size(), 4);
        ensure_equals("the first option of each, unless told otherwise",
                      said["field"].asString(), std::string("any"));
        ensure_equals("and so on", said["how"].asString(), std::string("containing"));
        ensure_equals("and so on", said["scope"].asString(), std::string("all"));
        ensure("the field starts empty", said["query"].asString().empty());
        ensure("and a word is not in it", !said.has("Find"));

        bar->setValue("query", "close");
        bar->setValue("scope", "skin");
        ensure_equals("what was typed", bar->valueOf("query"), std::string("close"));
        ensure_equals("and what was chosen", bar->valueOf("scope"), std::string("skin"));
        delete bar;
    }

    // Every part as wide as what is in it, and the field with whatever is
    // left: that is the difference between a sentence and a row of columns.
    template<> template<>
    void alscopebar_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make(500);
        bar->setSentence(sentence());

        const LLLineEditor* field = bar->getChild<LLLineEditor>("query");
        const LLComboBox* scope = bar->getChild<LLComboBox>("scope");
        ensure("the field is there", field != nullptr);
        ensure("and so is the last part", scope != nullptr);

        // In the order the sentence reads, left to right, none overlapping.
        ensure("the field comes before the last choice",
               field->getRect().mRight <= scope->getRect().mLeft);
        ensure("and the field is the wide one",
               field->getRect().getWidth() > scope->getRect().getWidth());
        ensure("everything fits", scope->getRect().mRight <= 500);

        // Narrower, and the field is what gives up the room.
        const S32 was = field->getRect().getWidth();
        bar->reshape(360, 30);
        ensure("the field gave up the room", field->getRect().getWidth() < was);
        ensure("and the parts that are words did not",
               scope->getRect().getWidth() > 0);
        delete bar;
    }

    // A caller may rewrite the sentence as one answer changes; what is
    // already chosen in the parts that are still there stays chosen.
    template<> template<>
    void alscopebar_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make();
        bar->setSentence(sentence());
        bar->setValue("query", "close");
        bar->setValue("scope", "skin");

        std::vector<ALScopeBar::Segment> shorter = sentence();
        shorter.pop_back();     // the scope goes
        bar->setSentence(shorter);

        ensure_equals("what was typed is still there", bar->valueOf("query"), std::string("close"));
        ensure("and the part that went says nothing", bar->valueOf("scope").empty());
        delete bar;
    }

    // Chosen or typed is one thing; committed is another, because a caller
    // that searches every file cannot do it on every keystroke.
    template<> template<>
    void alscopebar_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALScopeBar* bar = make();
        bar->setSentence(sentence());

        S32 changed = 0;
        S32 ran = 0;
        bar->onChanged([&changed]() { ++changed; });
        bar->onRun([&ran]() { ++ran; });

        bar->getChild<LLComboBox>("scope")->setSelectedByValue(LLSD("skin"), true);
        bar->getChild<LLComboBox>("scope")->onCommit();
        ensure_equals("choosing is a change", changed, 1);
        ensure_equals("and is not a run", ran, 0);

        bar->getChild<LLLineEditor>("query")->onCommit();
        ensure_equals("committing the field runs it", ran, 1);
        delete bar;
    }

    // The slot at the right end, for whatever the caller wants beside the
    // sentence: it keeps its width and the field gives up the room.
    template<> template<>
    void alscopebar_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the slot is filled", count != nullptr);
        ensure_equals("at the right end", count->getRect().mRight, 500);
        ensure_equals("keeping its width", count->getRect().getWidth(), 90);
        ensure("and the field gave up the room",
               bar->getChild<LLLineEditor>("query")->getRect().getWidth() < was);
        delete bar;
    }

    // A caller answering one part by rewriting the sentence does it from
    // inside that part's own callback. The part is still on the stack, so
    // the sentence is built again once it has finished, and until then the
    // old parts go on answering for the old sentence.
    template<> template<>
    void alscopebar_object::test<6>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the change was heard", changed, 1);
        ensure("the part that was pressed is still there", bar->findChild<LLComboBox>("how") == how);
        ensure("and so is the one the new sentence drops", bar->findChild<LLComboBox>("scope") != nullptr);
        ensure_equals("and the query still answers", bar->valueOf("query"), std::string("close"));

        gIdleCallbacks.callFunctions();
        ensure("once the part has finished, the sentence is the new one",
               bar->findChild<LLComboBox>("scope") == nullptr);
        ensure_equals("with what was chosen kept", bar->valueOf("how"), std::string("starting"));
        ensure_equals("and what was typed", bar->valueOf("query"), std::string("close"));
        ensure_equals("and nothing heard twice", changed, 1);
        delete bar;
    }
}
