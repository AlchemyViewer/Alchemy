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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<alquickopen_data> alquickopen_test;
    typedef alquickopen_test::object     alquickopen_object;
    tut::alquickopen_test alquickopen_testgroup("alquickopen");

    // The four degrees of meaning it, in the order they mean it. These are
    // kinds and not amounts: no number of scattered letters adds up to a name
    // that starts with what was typed.
    template<> template<>
    void alquickopen_object::test<1>()
    {
        ensure("the whole of it beats a prefix",
               ALQuickOpen::score("button", "button") > ALQuickOpen::score("button_bar", "button"));
        ensure("a prefix beats initials",
               ALQuickOpen::score("floater_buy.xml", "floater")
               > ALQuickOpen::score("floater_buy.xml", "fb"));
        ensure("initials beat a word inside it",
               ALQuickOpen::score("floater_buy.xml", "fb")
               > ALQuickOpen::score("floater_buy.xml", "buy"));
        ensure("a word inside it beats a run in the middle of one",
               ALQuickOpen::score("floater_buy.xml", "buy")
               > ALQuickOpen::score("floater_buy.xml", "oater"));
        ensure("and a run beats letters merely in order",
               ALQuickOpen::score("floater_buy.xml", "oater")
               > ALQuickOpen::score("floater_buy.xml", "fby"));
        ensure("letters not in order are not a match at all",
               ALQuickOpen::score("floater_buy.xml", "yubx") == 0);
        ensure("nor are letters that are not there",
               ALQuickOpen::score("floater_buy.xml", "zzz") == 0);
    }

    // A shorter name answering the same query answered it better, which is
    // what makes typing three letters land on the thing you meant.
    template<> template<>
    void alquickopen_object::test<2>()
    {
        ensure("the shorter of two prefixes",
               ALQuickOpen::score("panel_people.xml", "panel")
               > ALQuickOpen::score("panel_preferences_advanced.xml", "panel"));
        ensure_equals("and that is what comes back first",
                      best("panel"), std::string("panel_people.xml"));

        // `flbuy` is initials-and-more; the file that is only about buying
        // wins over the one that is about buying currency.
        ensure_equals("the tighter of two", best("floater_buy"), std::string("floater_buy.xml"));
    }

    // Nothing typed answers everything, in the order it was given, because
    // the caller's order is its own idea of what matters.
    template<> template<>
    void alquickopen_object::test<3>()
    {
        const std::vector<ALQuickOpen::Candidate> all = files();
        const std::vector<size_t> order = ALQuickOpen::rank(all, std::string());
        ensure_equals("everything", order.size(), all.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
            ensure_equals("in the order it was given", order[i], i);
        }

        // And a query nothing answers comes back empty rather than as
        // everything, which is the difference between a rank and a filter
        // that gave up.
        ensure("nothing matched", ALQuickOpen::rank(all, "qqzzxwvu").empty());
    }

    // Case is not something anybody typing quickly is thinking about.
    template<> template<>
    void alquickopen_object::test<4>()
    {
        ensure("upper", ALQuickOpen::score("floater_buy.xml", "FLOATER") > 0);
        ensure_equals("and it ranks the same as lower",
                      ALQuickOpen::score("floater_buy.xml", "FLOATER"),
                      ALQuickOpen::score("floater_buy.xml", "floater"));
        ensure_equals("whichever way round",
                      ALQuickOpen::score("Floater_Buy.xml", "floater"),
                      ALQuickOpen::score("floater_buy.xml", "floater"));
    }

    // A query longer than what it is asked about cannot be in it.
    template<> template<>
    void alquickopen_object::test<5>()
    {
        ensure("too long", ALQuickOpen::score("btn", "button") == 0);
        ensure("and nothing has nothing to say", ALQuickOpen::score("", "b") == 0);
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
    template<> template<>
    void alquickopen_object::test<6>()
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the field is there", field != nullptr);
        quick->setQuery("pann");
        quick->takeFocus();
        ensure("and has the keyboard", field->hasFocus());
        ensure("end of the line", field->handleKeyHere(KEY_END, MASK_NONE));
        ensure("one letter back", field->handleKeyHere(KEY_BACKSPACE, MASK_NONE));
        ensure_equals("the field says what was left", field->getText(), std::string("pan"));

        gFocusMgr.setKeyboardFocus(nullptr);
        ensure_equals("the keyboard leaving chooses nothing", chosen.size(), (size_t)0);

        // Return reaches the field first and climbs from there, the way the
        // window hands a key to whatever has the keyboard.
        quick->takeFocus();
        ensure("return is taken", field->handleKey(KEY_RETURN, MASK_NONE, false));
        ensure_equals("and chooses once", chosen.size(), (size_t)1);
        ensure_equals("the best answer to what was typed", chosen.front(), std::string("panel_people.xml"));

        gFocusMgr.setKeyboardFocus(nullptr);
        quick->die();
    }

    // The other words a candidate answers to: typing them finds it, below
    // anything whose label answers.
    template<> template<>
    void alquickopen_object::test<7>()
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
        ensure_equals("the words alone find it", order.size(), size_t(1));
        ensure_equals("the square", shapes[order.front()].label, std::string("Rounded_Square"));

        // The same match on a label outranks it on the words; a better
        // match on the words outranks a poor one on a label.
        order = ALQuickOpen::rank(shapes, "round");
        ensure_equals("both answer", order.size(), size_t(2));
        ensure_equals("the label first", shapes[order.front()].label, std::string("Rounded_Square"));
        ensure_equals("the words after", shapes[order.back()].label, std::string("Circle"));

        ALQuickOpen::Candidate scattered;
        scattered.label = "pxuxsxh";
        scattered.value = "scattered";
        shapes.push_back(scattered);
        order = ALQuickOpen::rank(shapes, "push");
        ensure_equals("both answer", order.size(), size_t(2));
        ensure_equals("the words' whole word before the label's scattered letters",
                      shapes[order.front()].label, std::string("PushButton_Off"));
    }

    template<> template<>
    void alquickopen_object::test<8>()
    {
        set_test_name("freeform, the one row says what return does, and return sends what was typed");
        if (!ll_test::HeadlessUI::get().ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("freeform", quick->freeform());
        ensure("one row, the hint", list && list->count() == 1 && list->choices()[0].text == "Type a line number.");
        // Not chosen: return takes what was typed whatever the row says.
        ensure("and not chosen", quick->chosenRow() == -1);
        quick->setQuery("12");
        ensure("the query is told", typed.size() == 1 && typed[0] == "12");
        ensure("the candidates stay aside", list->count() == 1);
        quick->setHint("Go to line 12.");
        ensure_equals("the row says what return does now", list->choices()[0].text, std::string("Go to line 12."));
        quick->findChild<LLLineEditor>("query")->onCommit();
        ensure("return sends what was typed", chosen.size() == 1 && chosen[0] == "12");
        quick->die();
    }

    template<> template<>
    void alquickopen_object::test<9>()
    {
        set_test_name("with nothing typed, the first few to browse; typed, the best few; none, said so");
        if (!ll_test::HeadlessUI::get().ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("nothing typed: the first as many as were asked for", list->count(), 3);
        ensure("in the order given", quick->listed().front() == "llFunction00");
        quick->setQuery("func");
        ensure_equals("typed: as many as were asked for", list->count(), 3);
        ensure("the best chosen", quick->chosenRow() == 0 && quick->listed().size() == 3);
        ensure("and nothing to say about it", quick->findChild<ALChoiceList>("matches")->placeholder().empty());
        quick->setQuery("zzz");
        ensure_equals("nothing answers", list->count(), 0);
        ensure("and the list says so", !list->placeholder().empty());
        ensure("nothing chosen", quick->chosenRow() == -1);
        quick->setQuery(std::string());
        ensure("the saying goes with a list to show", list->placeholder().empty());
        quick->die();
    }
    template<> template<>
    void alquickopen_object::test<10>()
    {
        set_test_name("a long label is still an answer, and each degree of meaning it stays in its own tier however far in");
        const std::string scattered = std::string(300, 'x') + "a_b_c";
        ensure("letters scattered far into a long line still match", ALQuickOpen::score(scattered, "abc") > 0);
        const std::string run = std::string(1200, 'y') + "abc";
        ensure("a run far into a longer one still matches", ALQuickOpen::score(run, "abc") > 0);
        ensure("and still outranks the best scattered letters",
               ALQuickOpen::score(run, "abc") > ALQuickOpen::score("aXbXc", "abc"));
        ensure("which outrank the worst", ALQuickOpen::score("aXbXc", "abc") >= ALQuickOpen::score(scattered, "abc"));
        ensure("a word start outranks the best run", ALQuickOpen::score("x_abc", "abc") > ALQuickOpen::score("xabc", "abc"));

        std::vector<ALQuickOpen::Candidate> history;
        for (const std::string& line : { scattered, run })
        {
            ALQuickOpen::Candidate one;
            one.label = line;
            one.value = line;
            history.push_back(one);
        }
        const std::vector<size_t> order = ALQuickOpen::rank(history, "abc");
        ensure_equals("both are offered", order.size(), size_t(2));
        ensure_equals("the run first", order.front(), size_t(1));
    }

    // A prefix says which list is asked of and is not matched: `>sav`
    // finds what `sav` would, `>` alone lists everything, and a query
    // without it is matched whole.
    template<> template<>
    void alquickopen_object::test<11>()
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(100, 400, 400, 200);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        gFloaterView->addChild(quick);
        quick->setCandidates(files());
        quick->setPrefix(">");
        ensure("the list", quick->findChild<ALChoiceList>("matches") != nullptr);
        const auto first = [quick]() { return quick->listed().empty() ? std::string() : quick->listed().front(); };
        quick->setQuery(">people");
        ensure_equals("matched without it", first(), std::string("panel_people.xml"));
        quick->setQuery("> people");
        ensure_equals("nor the blanks after it", first(), std::string("panel_people.xml"));
        quick->setQuery(">");
        ensure_equals("alone, everything to browse", quick->listed().size(), files().size());
        quick->setQuery("people");
        ensure_equals("without it, the query whole", first(), std::string("panel_people.xml"));
        quick->setQuery(">>people");
        ensure("with it twice, the second is matched", quick->listed().empty() || first() != "panel_people.xml");
        quick->die();
    }

    template<> template<>
    void alquickopen_object::test<12>()
    {
        set_test_name("escape in the field is the popover's: said as escaped, not as looked away, and the keyboard back where it was");
        if (!ll_test::HeadlessUI::get().ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ALPopover*   popover = ALPopover::showOver(window, quick, "Go to line");
        ensure("shown", popover != nullptr);
        std::vector<bool> said;
        popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
        quick->takeFocus();
        LLLineEditor* field = quick->findChild<LLLineEditor>("query");
        ensure("the field has the keyboard", gFocusMgr.getKeyboardFocus() == field);
        field->handleKey(KEY_ESCAPE, MASK_NONE, false);
        ensure("closed", popover->isDead() || !popover->getVisible());
        ensure_equals("once", said.size(), 1u);
        ensure("as escaped", said.front());
        ensure("the keyboard back where it was", gFocusMgr.getKeyboardFocus() == typed);
        gFocusMgr.setKeyboardFocus(nullptr);
        window->closeFloater();
    }

    template<> template<>
    void alquickopen_object::test<13>()
    {
        set_test_name("the arrows move the choice and return takes it, a double-click picks the row under it; tall enough for so many rows");
        if (!ll_test::HeadlessUI::get().ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALQuickOpen::Params p(LLUICtrlFactory::getDefaultParams<ALQuickOpen>());
        p.name = "quick";
        p.rect = LLRect(0, 200, 300, 0);
        ALQuickOpen* quick = LLUICtrlFactory::create<ALQuickOpen>(p);
        std::vector<std::string> chosen;
        quick->onChose([&chosen](const std::string& v) { chosen.push_back(v); });
        quick->setCandidates(files());
        quick->setQuery("floater");
        ensure("rows to move along", quick->listed().size() >= 3 && quick->chosenRow() == 0);
        ensure("down", quick->handleKeyHere(KEY_DOWN, MASK_NONE) && quick->chosenRow() == 1);
        ensure("down again", quick->handleKeyHere(KEY_DOWN, MASK_NONE) && quick->chosenRow() == 2);
        ensure("up", quick->handleKeyHere(KEY_UP, MASK_NONE) && quick->chosenRow() == 1);
        quick->findChild<LLLineEditor>("query")->onCommit();
        ensure("return takes the one chosen", chosen.size() == 1 && chosen[0] == quick->listed()[1]);

        ALChoiceList* list = quick->findChild<ALChoiceList>("matches");
        const LLRect  text = list->textRect();
        const S32     y    = text.mTop - list->layout().lineTop(2) - list->layout().rowHeightOf(2, 0) / 2 + list->scrollY();
        ensure("a double-click is taken", list->handleDoubleClick(text.mLeft + 4, y, MASK_NONE));
        ensure("the row under it picked", chosen.size() == 2 && chosen[1] == quick->listed()[2]);
        quick->die();

        ensure("taller for more rows", ALQuickOpen::heightForRows(3) > ALQuickOpen::heightForRows(1));
        const S32 line = LLFontGL::getFontSansSerifSmall()->getLineHeight();
        ensure("by the rows", ALQuickOpen::heightForRows(3) - ALQuickOpen::heightForRows(1) >= 2 * line);
    }
}
