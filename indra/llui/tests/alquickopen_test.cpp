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
}
