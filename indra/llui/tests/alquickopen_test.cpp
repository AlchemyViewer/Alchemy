/**
 * @file alquickopen_test.cpp
 * @brief The ranking, which is the whole of it.
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

#include "alquickopen.h"

#include "alchoicelist.h"
#include "llfontgl.h"
#include "alpopover.h"

#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llscrolllistctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alquickopen_data
    {
        // Focus and capture as the test found them, whatever it leaves them
        // on.
        ll_test::FocusScope focus;

        static std::vector<ALQuickOpen::Candidate> files()
        {
            std::vector<ALQuickOpen::Candidate> made;
            for (const char* name : { "floater_buy.xml", "floater_about.xml", "panel_people.xml",
                                      "panel_preferences_advanced.xml", "menu_viewer.xml",
                                      "floater_buy_currency.xml", "widgets/button.xml" })
            {
                ALQuickOpen::Candidate one;
                one.label = name;
                one.value = name;
                made.push_back(one);
            }
            return made;
        }

        static std::string best(const std::string& query)
        {
            const std::vector<ALQuickOpen::Candidate> all = files();
            const std::vector<size_t> order = ALQuickOpen::rank(all, query);
            return order.empty() ? std::string() : all[order.front()].label;
        }
    };

    // The four degrees of meaning it, in the order they mean it. These are
    // kinds and not amounts: no number of scattered letters adds up to a name
    // that starts with what was typed.
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: score ranks whole over prefix over initials over word over run", "[alquickopen]")
    {
        UNSCOPED_INFO("the whole of it beats a prefix");
        CHECK(ALQuickOpen::score("button", "button") > ALQuickOpen::score("button_bar", "button"));
        UNSCOPED_INFO("a prefix beats initials");
        CHECK(ALQuickOpen::score("floater_buy.xml", "floater")
               > ALQuickOpen::score("floater_buy.xml", "fb"));
        UNSCOPED_INFO("initials beat a word inside it");
        CHECK(ALQuickOpen::score("floater_buy.xml", "fb")
               > ALQuickOpen::score("floater_buy.xml", "buy"));
        UNSCOPED_INFO("a word inside it beats a run in the middle of one");
        CHECK(ALQuickOpen::score("floater_buy.xml", "buy")
               > ALQuickOpen::score("floater_buy.xml", "oater"));
        UNSCOPED_INFO("and a run beats letters merely in order");
        CHECK(ALQuickOpen::score("floater_buy.xml", "oater")
               > ALQuickOpen::score("floater_buy.xml", "fby"));
        UNSCOPED_INFO("letters not in order are not a match at all");
        CHECK(ALQuickOpen::score("floater_buy.xml", "yubx") == 0);
        UNSCOPED_INFO("nor are letters that are not there");
        CHECK(ALQuickOpen::score("floater_buy.xml", "zzz") == 0);
    }

    // A shorter name answering the same query answered it better, which is
    // what makes typing three letters land on the thing you meant.
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: a shorter name answering the same query ranks higher", "[alquickopen]")
    {
        UNSCOPED_INFO("the shorter of two prefixes");
        CHECK(ALQuickOpen::score("panel_people.xml", "panel")
               > ALQuickOpen::score("panel_preferences_advanced.xml", "panel"));
        UNSCOPED_INFO("and that is what comes back first");
        CHECK(best("panel") == std::string("panel_people.xml"));

        // `floater_buy` begins both, and `flbuy` is initials-and-more of
        // both; either way the file that is only about buying wins over the
        // one that is about buying currency.
        UNSCOPED_INFO("the tighter of two prefixes");
        CHECK(best("floater_buy") == std::string("floater_buy.xml"));
        UNSCOPED_INFO("the tighter of two by initials");
        CHECK(best("flbuy") == std::string("floater_buy.xml"));
    }

    // Nothing typed answers everything, in the order it was given, because
    // the caller's order is its own idea of what matters.
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: rank of nothing lists everything in order and of junk nothing", "[alquickopen]")
    {
        const std::vector<ALQuickOpen::Candidate> all = files();
        const std::vector<size_t> order = ALQuickOpen::rank(all, std::string());
        REQUIRE(order.size() == all.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
            REQUIRE(order[i] == i);
        }

        // And a query nothing answers comes back empty rather than as
        // everything, which is the difference between a rank and a filter
        // that gave up.
        CHECK(ALQuickOpen::rank(all, "qqzzxwvu").empty());
    }

    // Case is not something anybody typing quickly is thinking about.
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: score ignores case in the query and in the name", "[alquickopen]")
    {
        CHECK(ALQuickOpen::score("floater_buy.xml", "FLOATER") > 0);
        CHECK(ALQuickOpen::score("floater_buy.xml", "FLOATER") ==
                      ALQuickOpen::score("floater_buy.xml", "floater"));
        CHECK(ALQuickOpen::score("Floater_Buy.xml", "floater") ==
                      ALQuickOpen::score("floater_buy.xml", "floater"));
    }

    TEST_CASE_METHOD(alquickopen_data, "alquickopen: a query longer than the name scores nothing", "[alquickopen]")
    {
        CHECK(ALQuickOpen::score("btn", "button") == 0);
        CHECK(ALQuickOpen::score("", "b") == 0);
    }

    // A row is chosen when it is asked for -- Return, or a double-click -- and
    // not when the keyboard merely leaves the field. The field committed on
    // losing focus, so clicking a row further down (the list takes the
    // keyboard before it takes the click) or clicking away after typing chose
    // whatever was selected already: the top row, never the one clicked.
    //
    // The query has to be *edited* in the field, not set, because setQuery
    // puts its text in the way that counts as committed and only an edit
    // leaves it pending. Typed characters would need the window's keyboard,
    // which a headless test has none of, so the edit is a backspace.
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: return chooses a row but focus leaving the field does not", "[alquickopen]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(100, 400, 400, 200);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        gFloaterView->addChild(quick);
        quick->setCandidates(files());

        std::vector<std::string> chosen;
        quick->onChose([&chosen](const std::string& value) { chosen.push_back(value); });

        LLLineEditor* field = quick->findChild<LLLineEditor>("query");
        REQUIRE(field != nullptr);
        quick->setQuery("pann");
        quick->takeFocus();
        CHECK(field->hasFocus());
        CHECK(field->handleKeyHere(KEY_END, MASK_NONE));
        CHECK(field->handleKeyHere(KEY_BACKSPACE, MASK_NONE));
        CHECK(field->getText() == std::string("pan"));

        gFocusMgr.setKeyboardFocus(nullptr);
        UNSCOPED_INFO("the keyboard leaving chooses nothing");
        CHECK(chosen.size() == (size_t)0);

        // Return reaches the field first and climbs from there, the way the
        // window hands a key to whatever has the keyboard.
        quick->takeFocus();
        CHECK(field->handleKey(KEY_RETURN, MASK_NONE, false));
        REQUIRE(chosen.size() == (size_t)1);
        UNSCOPED_INFO("the best answer to what was typed");
        CHECK(chosen.front() == std::string("panel_people.xml"));

        gFocusMgr.setKeyboardFocus(nullptr);
        quick->die();
    }

    TEST_CASE_METHOD(alquickopen_data, "alquickopen: a candidate's other words find it below an equal label match", "[alquickopen]")
    {
        std::vector<ALQuickOpen::Candidate> shapes;
        for (const auto& [name, words] : { std::pair<const char*, const char*>{ "PushButton_Off", "Push button buttons" },
                                           { "Rounded_Square", "Rounded square basic" },
                                           { "Circle", "round disc" } })
        {
            ALQuickOpen::Candidate one;
            one.label = name;
            one.also = words;
            one.value = name;
            shapes.push_back(one);
        }

        std::vector<size_t> order = ALQuickOpen::rank(shapes, "basic");
        UNSCOPED_INFO("the words alone find it");
        REQUIRE(order.size() == size_t(1));
        CHECK(shapes[order.front()].label == std::string("Rounded_Square"));

        // The same match on a label outranks it on the words; a better
        // match on the words outranks a poor one on a label.
        order = ALQuickOpen::rank(shapes, "round");
        REQUIRE(order.size() == size_t(2));
        UNSCOPED_INFO("the label first");
        CHECK(shapes[order.front()].label == std::string("Rounded_Square"));
        UNSCOPED_INFO("the words after");
        CHECK(shapes[order.back()].label == std::string("Circle"));

        ALQuickOpen::Candidate scattered;
        scattered.label = "pxuxsxh";
        scattered.value = "scattered";
        shapes.push_back(scattered);
        order = ALQuickOpen::rank(shapes, "push");
        REQUIRE(order.size() == size_t(2));
        UNSCOPED_INFO("the words' whole word before the label's scattered letters");
        CHECK(shapes[order.front()].label == std::string("PushButton_Off"));
    }

    TEST_CASE_METHOD(alquickopen_data, "alquickopen: in freeform the one row is the hint and return sends the query", "[alquickopen]")
    {
        if (!ll_test::HeadlessUI::get().ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(0, 60, 300, 0);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        std::vector<std::string> typed, chosen;
        quick->onQueryChanged([&](const std::string& q) { typed.push_back(q); });
        quick->onChose([&](const std::string& v) { chosen.push_back(v); });
        quick->setCandidates(files());
        quick->setHint("Type a line number.");
        const ALChoiceList* list = quick->findChild<ALChoiceList>("matches");
        CHECK(quick->freeform());
        REQUIRE(list);
        REQUIRE(list->count() == 1);
        CHECK(list->choices()[0].text == "Type a line number.");
        // Not chosen: return takes what was typed whatever the row says.
        CHECK(quick->chosenRow() == -1);
        quick->setQuery("12");
        {
            INFO("the query is told");
            REQUIRE(typed.size() == 1);
            CHECK(typed[0] == "12");
        }
        UNSCOPED_INFO("the candidates stay aside");
        REQUIRE(list->count() == 1);
        quick->setHint("Go to line 12.");
        CHECK(list->choices()[0].text == std::string("Go to line 12."));
        LLLineEditor* field = quick->findChild<LLLineEditor>("query");
        REQUIRE(field != nullptr);
        field->onCommit();
        REQUIRE(chosen.size() == 1);
        CHECK(chosen[0] == "12");
        quick->die();
    }

    TEST_CASE_METHOD(alquickopen_data, "alquickopen: the list shows the first or the best few or says nothing matches", "[alquickopen]")
    {
        if (!ll_test::HeadlessUI::get().ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(0, 200, 300, 0);
        p.rows = 3;
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        std::vector<ALQuickOpen::Candidate> many;
        for (S32 i = 0; i < 20; ++i)
        {
            ALQuickOpen::Candidate one;
            one.label = llformat("llFunction%02d", i);
            one.value = one.label;
            many.push_back(one);
        }
        quick->setCandidates(many);
        const ALChoiceList* list = quick->findChild<ALChoiceList>("matches");
        REQUIRE(list != nullptr);
        UNSCOPED_INFO("nothing typed: the first as many as were asked for");
        REQUIRE(list->count() == 3);
        UNSCOPED_INFO("in the order given");
        CHECK(quick->listed() == std::vector<std::string>({ "llFunction00", "llFunction01", "llFunction02" }));
        quick->setQuery("func");
        UNSCOPED_INFO("typed: as many as were asked for");
        CHECK(list->count() == 3);
        {
            INFO("the best chosen");
            CHECK(quick->chosenRow() == 0);
            CHECK(quick->listed().size() == 3);
        }
        UNSCOPED_INFO("and nothing to say about it");
        CHECK(list->placeholder().empty());
        quick->setQuery("zzz");
        CHECK(list->count() == 0);
        UNSCOPED_INFO("and the list says so");
        CHECK_FALSE(list->placeholder().empty());
        CHECK(quick->chosenRow() == -1);
        quick->setQuery(std::string());
        UNSCOPED_INFO("the saying goes with a list to show");
        CHECK(list->placeholder().empty());
        quick->die();
    }
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: matches far into a long label still score and keep their tiers", "[alquickopen]")
    {
        const std::string scattered = std::string(300, 'x') + "a_b_c";
        UNSCOPED_INFO("letters scattered far into a long line still match");
        CHECK(ALQuickOpen::score(scattered, "abc") > 0);
        const std::string run = std::string(1200, 'y') + "abc";
        UNSCOPED_INFO("a run far into a longer one still matches");
        CHECK(ALQuickOpen::score(run, "abc") > 0);
        UNSCOPED_INFO("and still outranks the best scattered letters");
        CHECK(ALQuickOpen::score(run, "abc") > ALQuickOpen::score("aXbXc", "abc"));
        UNSCOPED_INFO("which outrank the worst");
        CHECK(ALQuickOpen::score("aXbXc", "abc") >= ALQuickOpen::score(scattered, "abc"));
        UNSCOPED_INFO("a word start outranks the best run");
        CHECK(ALQuickOpen::score("x_abc", "abc") > ALQuickOpen::score("xabc", "abc"));

        std::vector<ALQuickOpen::Candidate> history;
        for (const std::string& line : { scattered, run })
        {
            ALQuickOpen::Candidate one;
            one.label = line;
            one.value = line;
            history.push_back(one);
        }
        const std::vector<size_t> order = ALQuickOpen::rank(history, "abc");
        REQUIRE(order.size() == size_t(2));
        UNSCOPED_INFO("the run first");
        CHECK(order.front() == size_t(1));
    }

    // A prefix says which list is asked of and is not matched: `>sav`
    // finds what `sav` would, `>` alone lists everything, and a query
    // without it is matched whole.
    TEST_CASE_METHOD(alquickopen_data, "alquickopen: a set prefix picks the list and is left out of the match", "[alquickopen]")
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(100, 400, 400, 200);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        gFloaterView->addChild(quick);
        quick->setCandidates(files());
        quick->setPrefix(">");
        CHECK(quick->findChild<ALChoiceList>("matches") != nullptr);
        const auto first = [quick]() { return quick->listed().empty() ? std::string() : quick->listed().front(); };
        quick->setQuery(">people");
        UNSCOPED_INFO("matched without it");
        CHECK(first() == std::string("panel_people.xml"));
        quick->setQuery("> people");
        UNSCOPED_INFO("nor the blanks after it");
        CHECK(first() == std::string("panel_people.xml"));
        quick->setQuery(">");
        UNSCOPED_INFO("alone, everything to browse");
        CHECK(quick->listed().size() == files().size());
        quick->setQuery("people");
        UNSCOPED_INFO("without it, the query whole");
        CHECK(first() == std::string("panel_people.xml"));
        quick->setQuery(">>people");
        UNSCOPED_INFO("with it twice, the second is matched");
        CHECK((quick->listed().empty() || first() != "panel_people.xml"));
        quick->die();
    }

    TEST_CASE_METHOD(alquickopen_data, "alquickopen: escape in the field closes the popover as escaped and restores focus", "[alquickopen]")
    {
        if (!ll_test::HeadlessUI::get().ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLFloater::Params fp(LLFloater::getDefaultParams());
        fp.name            = "home";
        fp.rect            = LLRect(100, 500, 500, 100);
        fp.save_rect       = false;
        fp.save_visibility = false;
        LLFloater* window  = new LLFloater(LLSD(), fp);
        window->openFloater();
        LLLineEditor::Params lp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
        lp.name             = "typed";
        lp.rect             = LLRect(10, 220, 130, 200);
        LLLineEditor* typed = LLUICtrlFactory::create<LLLineEditor>(lp);
        window->addChild(typed);
        typed->setFocus(true);

        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name             = "quick_open";
        p.rect             = LLRect(0, 60, 300, 0);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        // Found before the popover is up: one left up by a failed REQUIRE
        // would be escaped after the case, into a callback holding `said`.
        LLLineEditor* field = quick->findChild<LLLineEditor>("query");
        REQUIRE(field != nullptr);
        ALPopover*   popover = ALPopover::showOver(window, quick, "Go to line");
        REQUIRE(popover != nullptr);
        std::vector<bool> said;
        popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
        quick->takeFocus();
        CHECK(gFocusMgr.getKeyboardFocus() == field);
        field->handleKey(KEY_ESCAPE, MASK_NONE, false);
        CHECK((popover->isDead() || !popover->getVisible()));
        REQUIRE(said.size() == 1u);
        UNSCOPED_INFO("closed as escaped and not as looked away");
        CHECK(said.front());
        CHECK(gFocusMgr.getKeyboardFocus() == typed);
        gFocusMgr.setKeyboardFocus(nullptr);
        window->closeFloater();
    }

    TEST_CASE_METHOD(alquickopen_data, "alquickopen: keys and double-clicks choose rows and heightForRows grows with rows", "[alquickopen]")
    {
        if (!ll_test::HeadlessUI::get().ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(0, 200, 300, 0);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        std::vector<std::string> chosen;
        quick->onChose([&chosen](const std::string& v) { chosen.push_back(v); });
        quick->setCandidates(files());
        quick->setQuery("floater");
        {
            INFO("rows to move along");
            REQUIRE(quick->listed().size() >= 3);
            CHECK(quick->chosenRow() == 0);
        }
        {
            INFO("down");
            CHECK(quick->handleKeyHere(KEY_DOWN, MASK_NONE));
            CHECK(quick->chosenRow() == 1);
        }
        {
            INFO("down again");
            CHECK(quick->handleKeyHere(KEY_DOWN, MASK_NONE));
            CHECK(quick->chosenRow() == 2);
        }
        {
            INFO("up");
            CHECK(quick->handleKeyHere(KEY_UP, MASK_NONE));
            CHECK(quick->chosenRow() == 1);
        }
        LLLineEditor* field = quick->findChild<LLLineEditor>("query");
        REQUIRE(field != nullptr);
        field->onCommit();
        {
            INFO("return takes the one chosen");
            REQUIRE(chosen.size() == 1);
            CHECK(chosen[0] == quick->listed()[1]);
        }

        ALChoiceList* list = quick->findChild<ALChoiceList>("matches");
        REQUIRE(list != nullptr);
        const LLRect  text = list->textRect();
        const S32     y    = text.mTop - list->layout().lineTop(2) - list->layout().rowHeightOf(2, 0) / 2 + list->scrollY();
        CHECK(list->handleDoubleClick(text.mLeft + 4, y, MASK_NONE));
        {
            INFO("the row under it picked");
            REQUIRE(chosen.size() == 2);
            CHECK(chosen[1] == quick->listed()[2]);
        }
        quick->die();

        CHECK(ALQuickOpen::heightForRows(3) > ALQuickOpen::heightForRows(1));
        const S32 line = LLFontGL::getFontSansSerifSmall()->getLineHeight();
        UNSCOPED_INFO("at least a line for each row more");
        CHECK(ALQuickOpen::heightForRows(3) - ALQuickOpen::heightForRows(1) >= 2 * line);
    }
}
