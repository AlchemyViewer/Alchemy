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

#include "../alquickopen.h"

#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gQuickTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gQuickTestAnonName;
}

namespace tut
{
    struct alquickopen_data
    {
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
}
