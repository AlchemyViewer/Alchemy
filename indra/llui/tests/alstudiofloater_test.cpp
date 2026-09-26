/**
 * @file alstudiofloater_test.cpp
 * @brief A studio's quick open, asked and answered, and the shape a studio keeps.
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

#include "../alstudiofloater.h"

#include "../alpopover.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../lluictrlfactory.h"

#include "llcontrol.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <string>
#include <vector>

class LLAvatarName;
const std::string gStudioFloaterTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gStudioFloaterTestAnonName;
}

namespace
{
    // A studio with nothing in it but what every studio has.
    class TestStudio : public ALStudioFloater
    {
    public:
        explicit TestStudio(const std::string& setting) : ALStudioFloater(LLSD(), setting) {}

        using ALStudioFloater::quickOpen;
        using ALStudioFloater::saveState;
    };

    std::vector<ALQuickOpen::Candidate> candidates(const std::vector<std::string>& values)
    {
        std::vector<ALQuickOpen::Candidate> out;
        for (const std::string& value : values)
        {
            ALQuickOpen::Candidate one;
            one.label = value;
            one.value = value;
            out.push_back(one);
        }
        return out;
    }

    // Return, as the field gives it: the best answer taken.
    void choose(ALQuickOpen* quick)
    {
        quick->findChild<LLLineEditor>("query")->onCommit();
    }
}

namespace tut
{
    struct alstudiofloater_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static TestStudio* studio(const std::string& setting = std::string())
        {
            TestStudio* made = new TestStudio(setting);
            made->setRect(LLRect(100, 700, 900, 100));
            made->openFloater();
            return made;
        }
    };

    typedef test_group<alstudiofloater_data> alstudiofloater_test;
    typedef alstudiofloater_test::object     alstudiofloater_object;
    tut::alstudiofloater_test alstudiofloater_testgroup("alstudiofloater");

    // Another question while one is up is asked afresh: the one up is put
    // away, escaped, and what is picked from the new list goes to whoever
    // asked the new question -- a command picked from the palette is the
    // command, not a jump to the first symbol.
    template<> template<>
    void alstudiofloater_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        std::vector<std::string> symbols, commands;
        S32 symbols_escaped = 0;
        ALQuickOpen* first = window->quickOpen(candidates({ "state_entry", "touch_start" }), "Type a symbol", "Go to Symbol",
                                               [&](const std::string& v) { symbols.push_back(v); }, nullptr, 0, 0,
                                               [&]() { ++symbols_escaped; });
        ensure("a quick open", first != nullptr);
        const LLHandle<LLFloater> first_up = first->getParentByType<LLFloater>()->getHandle();

        ALQuickOpen* second = window->quickOpen(candidates({ "save", "compile" }), "Type a command", "Commands",
                                                [&](const std::string& v) { commands.push_back(v); });
        ensure("another", second != nullptr && second != first);
        ensure("the first put away", first_up.isDead() || first_up.get()->isDead() || !first_up.get()->getVisible());
        ensure_equals("escaped, for whoever asked it", symbols_escaped, 1);

        choose(second);
        ensure_equals("the pick goes to whoever asked for commands", commands.size(), (size_t)1);
        ensure_equals("the best of them", commands.front(), std::string("save"));
        ensure("and nothing to the symbols", symbols.empty());
        ensure_equals("which were told once", symbols_escaped, 1);
        window->closeFloater();
    }

    // The same question again while it is up keeps what was typed, and
    // its answer goes to the latest asking; the first asker hears nothing.
    template<> template<>
    void alstudiofloater_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        std::vector<std::string> first_heard, second_heard;
        ALQuickOpen* first = window->quickOpen(candidates({ "alpha", "beta" }), "Type a name", "Tabs",
                                               [&](const std::string& v) { first_heard.push_back(v); });
        first->setQuery("be");
        ALQuickOpen* again = window->quickOpen(candidates({ "alpha", "beta", "beetle" }), "Type a name", "Tabs",
                                               [&](const std::string& v) { second_heard.push_back(v); });
        ensure("the same one", again == first);
        ensure_equals("what was typed kept", again->query(), std::string("be"));
        choose(again);
        ensure("the latest asking hears the answer", second_heard.size() == 1 && second_heard.front() == "beta");
        ensure("the first does not", first_heard.empty());
        window->closeFloater();
    }

    // Three ways out, told apart: Escape puts back what was previewed,
    // looking away with nothing chosen leaves it -- the look away is a
    // click, and a view put back under it lands it on another line -- and
    // a choice is neither.
    template<> template<>
    void alstudiofloater_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        S32 escaped = 0;
        S32 left    = 0;
        std::vector<std::string> chosen;
        const auto ask = [&]() {
            return window->quickOpen(candidates({ "one" }), "Type", "Ways out", [&](const std::string& v) { chosen.push_back(v); },
                                     nullptr, 0, 0, [&]() { ++escaped; }, {}, [&]() { ++left; });
        };
        const auto popoverOf = [](ALQuickOpen* quick) { return ALViewType::as<ALPopover>(quick->getParentByType<LLFloater>()); };

        ALQuickOpen* quick = ask();
        ensure("in a popover", popoverOf(quick) != nullptr);
        popoverOf(quick)->onFocusLost();
        ensure_equals("looked away from, it was left", left, 1);
        ensure_equals("not escaped", escaped, 0);
        ensure("and nothing chosen", chosen.empty());

        quick = ask();
        popoverOf(quick)->escape();
        ensure_equals("escaped", escaped, 1);
        ensure_equals("not left", left, 1);

        quick = ask();
        choose(quick);
        ensure_equals("chosen", chosen.size(), (size_t)1);
        ensure("and neither of the others", escaped == 1 && left == 1);
        window->closeFloater();
    }

    // The state keeps the window's shape as it opens: closed while it is
    // minimized, what is written is the window, not the title bar it is
    // drawn as then.
    template<> template<>
    void alstudiofloater_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLControlGroup* config = LLUI::getInstance()->getSettingGroup("config");
        ensure("a config group", config != nullptr);
        const std::string setting = "ALStudioFloaterTestState";
        if (!config->controlExists(setting))
        {
            config->declareLLSD(setting, LLSD(), "a test studio's state", LLControlVariable::PERSIST_NO);
        }
        TestStudio* window = studio(setting);
        window->setCanMinimize(true);
        const LLRect whole = window->getRect();
        window->setMinimized(true);
        ensure("minimized", window->isMinimized());
        ensure("drawn as its title bar", window->getRect().getHeight() < whole.getHeight());

        window->saveState();
        const LLSD rect = config->getLLSD(setting)["rect"];
        ensure_equals("four numbers", rect.size(), 4);
        ensure_equals("as wide as the window", rect[2].asInteger() - rect[0].asInteger(), whole.getWidth());
        ensure_equals("and as tall", rect[3].asInteger() - rect[1].asInteger(), whole.getHeight());
        window->setMinimized(false);
        window->closeFloater();
    }

    template<> template<>
    void alstudiofloater_object::test<5>()
    {
        set_test_name("a step chosen in a history gone to a step at a time either way, stopped where a step will not go or at the bound");
        size_t      in_force = 5;
        size_t      refuse   = 0;
        std::string taken;
        const auto  count    = [&]() { return in_force; };
        S32         refused  = 0;
        const auto  back     = [&]() {
            if (in_force == refuse)
            {
                ++refused;
                return false;
            }
            --in_force;
            taken += "u";
            return true;
        };
        const auto forward = [&]() {
            ++in_force;
            taken += "r";
            return true;
        };
        ALStudioFloater::goToStep(2, count, back, forward, 100);
        ensure("back three", in_force == 2 && taken == "uuu");
        taken.clear();
        ALStudioFloater::goToStep(4, count, back, forward, 100);
        ensure("forward two", in_force == 4 && taken == "rr");
        taken.clear();
        ALStudioFloater::goToStep(4, count, back, forward, 100);
        ensure("there already: nothing", taken.empty());
        refuse = 3;
        ALStudioFloater::goToStep(0, count, back, forward, 100);
        ensure("a step that will not go: stopped, and not turned round", in_force == 3 && taken == "u");
        ensure_equals("asked once", refused, 1);
        taken.clear();
        refuse = 99;
        ALStudioFloater::goToStep(0, count, back, forward, 2);
        ensure("the bound", in_force == 1 && taken == "uu");
        taken.clear();
        ALStudioFloater::goToStep(9, count, back, forward, 3);
        ensure("the bound forward too", in_force == 4 && taken == "rrr");
    }
}
