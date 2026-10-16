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

#include "alstudiofloater.h"

#include "alkeychord.h"
#include "alpopover.h"
#include "alsurface.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llmenugl.h"
#include "../lluictrlfactory.h"

#include "llframetimer.h"
#include "llkeyboard.h"

#include "llcontrol.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    // A studio with nothing in it but what every studio has.
    class TestStudio : public ALStudioFloater
    {
    public:
        explicit TestStudio(const std::string& setting) : ALStudioFloater(LLSD(), setting) {}

        using ALStudioFloater::quickOpen;
        using ALStudioFloater::saveState;
        using ALStudioFloater::addCommand;
        using ALStudioFloater::runCommandKey;
        using ALStudioFloater::keepChords;
        using ALStudioFloater::byKeys;

        // A person's key for a command, as a studio with a keymap keeps it.
        std::string rebound;
        KEY         reboundKey = KEY_NONE;
        MASK        reboundMask = MASK_NONE;

    protected:
        std::vector<ALKeyChord> keysOf(const KeyedCommand& command) const override
        {
            if (command.rebindable && rebound == command.id)
            {
                return { ALKeyChord{ reboundKey, reboundMask } };
            }
            return ALStudioFloater::keysOf(command);
        }
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

    // A field in the window, with the keyboard.
    LLLineEditor* field(LLFloater* window, const std::string& text)
    {
        LLLineEditor::Params p;
        p.name = "field";
        p.rect = LLRect(10, 40, 300, 20);
        LLLineEditor* made = LLUICtrlFactory::create<LLLineEditor>(p);
        window->addChild(made);
        made->setText(text);
        made->setFocus(true);
        return made;
    }

    // Every popover still up escaped while what its callbacks count is still
    // there: a REQUIRE that ends a case with one up would otherwise leave the
    // harness to escape it after the case, into a frame that has gone.
    struct EscapeOnExit
    {
        ~EscapeOnExit() { ll_test::escapePopovers(); }
    };

    // Return, as the field gives it: the best answer taken.
    void choose(ALQuickOpen* quick)
    {
        LLLineEditor* query = quick->findChild<LLLineEditor>("query");
        REQUIRE(query != nullptr);
        query->onCommit();
    }
}

namespace
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

    // Another question while one is up is asked afresh: the one up is put
    // away, escaped, and what is picked from the new list goes to whoever
    // asked the new question -- a command picked from the palette is the
    // command, not a jump to the first symbol.
    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: a new quick open replaces the one up and escapes it", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        std::vector<std::string> symbols, commands;
        S32 symbols_escaped = 0;
        EscapeOnExit escape_on_exit;
        ALQuickOpen* first = window->quickOpen(candidates({ "state_entry", "touch_start" }), "Type a symbol", "Go to Symbol",
                                               [&](const std::string& v) { symbols.push_back(v); }, nullptr, 0, 0,
                                               [&]() { ++symbols_escaped; });
        REQUIRE(first != nullptr);
        LLFloater* first_popover = first->getParentByType<LLFloater>();
        REQUIRE(first_popover != nullptr);
        const LLHandle<LLFloater> first_up = first_popover->getHandle();

        ALQuickOpen* second = window->quickOpen(candidates({ "save", "compile" }), "Type a command", "Commands",
                                                [&](const std::string& v) { commands.push_back(v); });
        REQUIRE(second != nullptr);
        CHECK(second != first);
        UNSCOPED_INFO("the first put away");
        CHECK((first_up.isDead() || first_up.get()->isDead() || !first_up.get()->getVisible()));
        UNSCOPED_INFO("escaped, for whoever asked it");
        CHECK(symbols_escaped == 1);

        choose(second);
        UNSCOPED_INFO("the pick goes to whoever asked for commands");
        REQUIRE(commands.size() == (size_t)1);
        UNSCOPED_INFO("the best of them");
        CHECK(commands.front() == std::string("save"));
        UNSCOPED_INFO("and nothing to the symbols");
        CHECK(symbols.empty());
        UNSCOPED_INFO("which were told once");
        CHECK(symbols_escaped == 1);
        window->closeFloater();
    }

    // The same question again while it is up keeps what was typed, and
    // its answer goes to the latest asking; the first asker hears nothing.
    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: the same quick open again keeps its query and answers the latest", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        std::vector<std::string> first_heard, second_heard;
        ALQuickOpen* first = window->quickOpen(candidates({ "alpha", "beta" }), "Type a name", "Tabs",
                                               [&](const std::string& v) { first_heard.push_back(v); });
        REQUIRE(first != nullptr);
        first->setQuery("be");
        ALQuickOpen* again = window->quickOpen(candidates({ "alpha", "beta", "beetle" }), "Type a name", "Tabs",
                                               [&](const std::string& v) { second_heard.push_back(v); });
        REQUIRE(again == first);
        CHECK(again->query() == std::string("be"));
        choose(again);
        REQUIRE(second_heard.size() == 1);
        CHECK(second_heard.front() == "beta");
        UNSCOPED_INFO("the first does not");
        CHECK(first_heard.empty());
        window->closeFloater();
    }

    // Three ways out, told apart: Escape puts back what was previewed,
    // looking away with nothing chosen leaves it -- the look away is a
    // click, and a view put back under it lands it on another line -- and
    // a choice is neither.
    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: escaping and looking away and choosing are told apart", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        S32 escaped = 0;
        S32 left    = 0;
        std::vector<std::string> chosen;
        EscapeOnExit escape_on_exit;
        const auto ask = [&]() {
            return window->quickOpen(candidates({ "one" }), "Type", "Ways out", [&](const std::string& v) { chosen.push_back(v); },
                                     nullptr, 0, 0, [&]() { ++escaped; }, {}, [&]() { ++left; });
        };
        const auto popoverOf = [](ALQuickOpen* quick)
        {
            return quick ? ALViewType::as<ALPopover>(quick->getParentByType<LLFloater>()) : nullptr;
        };

        ALQuickOpen* quick = ask();
        REQUIRE(popoverOf(quick) != nullptr);
        popoverOf(quick)->onFocusLost();
        UNSCOPED_INFO("looked away from, it was left");
        CHECK(left == 1);
        CHECK(escaped == 0);
        CHECK(chosen.empty());

        quick = ask();
        REQUIRE(popoverOf(quick) != nullptr);
        popoverOf(quick)->escape();
        CHECK(escaped == 1);
        UNSCOPED_INFO("escaping is not leaving");
        CHECK(left == 1);

        quick = ask();
        REQUIRE(quick != nullptr);
        choose(quick);
        CHECK(chosen.size() == (size_t)1);
        {
            INFO("and neither of the others");
            CHECK(escaped == 1);
            CHECK(left == 1);
        }
        window->closeFloater();
    }

    // The state keeps the window's shape as it opens: closed while it is
    // minimized, what is written is the window, not the title bar it is
    // drawn as then.
    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: saveState of a minimized window writes its full rect", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLControlGroup* config = LLUI::getInstance()->getSettingGroup("config");
        REQUIRE(config != nullptr);
        const std::string setting = "ALStudioFloaterTestState";
        if (!config->controlExists(setting))
        {
            config->declareLLSD(setting, LLSD(), "a test studio's state", LLControlVariable::PERSIST_NO);
        }
        // Emptied when the test ends, so a studio made later under the same
        // setting does not open where this one was saved.
        ll_test::SettingsScope settings;
        settings.keep(config->getControl(setting).get());
        TestStudio* window = studio(setting);
        window->setCanMinimize(true);
        const LLRect whole = window->getRect();
        window->setMinimized(true);
        CHECK(window->isMinimized());
        UNSCOPED_INFO("drawn as its title bar");
        CHECK(window->getRect().getHeight() < whole.getHeight());

        window->saveState();
        const LLSD rect = config->getLLSD(setting)["rect"];
        REQUIRE(rect.size() == 4);
        CHECK(rect[2].asInteger() - rect[0].asInteger() == whole.getWidth());
        CHECK(rect[3].asInteger() - rect[1].asInteger() == whole.getHeight());
        window->setMinimized(false);
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: goToStep walks back or forward and stops at a refusal or the bound", "[alstudiofloater]")
    {
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
        CHECK(in_force == 2);
        CHECK(taken == "uuu");
        taken.clear();
        ALStudioFloater::goToStep(4, count, back, forward, 100);
        CHECK(in_force == 4);
        CHECK(taken == "rr");
        taken.clear();
        ALStudioFloater::goToStep(4, count, back, forward, 100);
        UNSCOPED_INFO("there already: nothing");
        CHECK(taken.empty());
        refuse = 3;
        ALStudioFloater::goToStep(0, count, back, forward, 100);
        {
            INFO("a step that will not go: stopped, and not turned round");
            CHECK(in_force == 3);
            CHECK(taken == "u");
        }
        UNSCOPED_INFO("asked once");
        CHECK(refused == 1);
        taken.clear();
        refuse = 99;
        ALStudioFloater::goToStep(0, count, back, forward, 2);
        {
            INFO("the bound");
            CHECK(in_force == 1);
            CHECK(taken == "uu");
        }
        taken.clear();
        ALStudioFloater::goToStep(9, count, back, forward, 3);
        {
            INFO("the bound forward too");
            CHECK(in_force == 4);
            CHECK(taken == "rrr");
        }
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: a key runs the first able command and a rebinding replaces it", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*              window = studio();
        std::vector<std::string> ran;
        bool                     can    = false;
        window->addCommand({ "find", 'F', MASK_CONTROL, false }, [&]() { ran.push_back("find"); return can; });
        window->addCommand({ "filter", 'F', MASK_CONTROL, false }, [&]() { ran.push_back("filter"); return true; });
        window->addCommand({ "save", 'S', MASK_CONTROL }, [&]() { ran.push_back("save"); return true; });
        window->addCommand({ "save", 'S', MASK_CONTROL | MASK_SHIFT, false }, [&]() { ran.push_back("save again"); return true; });

        {
            INFO("a key none has is not taken");
            CHECK_FALSE(window->runCommandKey('Q', MASK_CONTROL));
            CHECK(ran.empty());
        }
        window->addCommand({ "unbound", KEY_NONE, MASK_NONE }, [&]() { ran.push_back("unbound"); return true; });
        {
            INFO("nor no key at all, a command with none");
            CHECK_FALSE(window->runCommandKey(KEY_NONE, MASK_NONE));
            CHECK(ran.empty());
        }
        CHECK(window->handleKeyHere('F', MASK_CONTROL));
        UNSCOPED_INFO("by the second, the first unable");
        CHECK(ran == std::vector<std::string>({ "find", "filter" }));
        ran.clear();
        can = true;
        CHECK(window->runCommandKey('F', MASK_CONTROL));
        UNSCOPED_INFO("by the first, able now");
        CHECK(ran == std::vector<std::string>({ "find" }));

        ran.clear();
        window->rebound     = "save";
        window->reboundKey  = 'K';
        window->reboundMask = MASK_ALT;
        {
            INFO("a person's key stands for the standard");
            CHECK(window->runCommandKey('K', MASK_ALT));
            CHECK(ran == std::vector<std::string>({ "save" }));
        }
        UNSCOPED_INFO("which it no longer answers to");
        CHECK_FALSE(window->runCommandKey('S', MASK_CONTROL));
        {
            INFO("a second key, not theirs to give, still does");
            CHECK(window->runCommandKey('S', MASK_CONTROL | MASK_SHIFT));
            REQUIRE_FALSE(ran.empty());
            CHECK(ran.back() == "save again");
        }
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: keepChords swallows unused chords but lets Quit and Control-Tab go", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*   window = studio();
        LLLineEditor* typing = field(window, "text");
        S32           found  = 0;
        window->addCommand({ "find", 'F', MASK_CONTROL, false }, [&]() { ++found; return true; });
        UNSCOPED_INFO("not kept, Control-D goes on to the viewer");
        CHECK_FALSE(typing->handleKey('D', MASK_CONTROL, false));

        window->keepChords(true);
        UNSCOPED_INFO("Control-D, which duplicates what is selected in the world");
        CHECK(typing->handleKey('D', MASK_CONTROL, false));
        UNSCOPED_INFO("Control-L, which links it");
        CHECK(typing->handleKey('L', MASK_CONTROL, false));
        UNSCOPED_INFO("Control-Shift-H, which goes home");
        CHECK(typing->handleKey('H', MASK_CONTROL | MASK_SHIFT, false));
        CHECK(typing->handleKey('R', MASK_ALT, false));
        UNSCOPED_INFO("the Mac's own Control key");
        CHECK(typing->handleKey('D', MASK_MAC_CONTROL, false));
        {
            INFO("the window's own key still its own");
            CHECK(typing->handleKey('F', MASK_CONTROL, false));
            CHECK(found == 1);
        }
        UNSCOPED_INFO("Quit goes on");
        CHECK_FALSE(typing->handleKey('Q', MASK_CONTROL, false));
        {
            INFO("and moving between windows");
            CHECK_FALSE(typing->handleKey(KEY_TAB, MASK_CONTROL, false));
            CHECK_FALSE(typing->handleKey(KEY_TAB, MASK_CONTROL | MASK_SHIFT, false));
        }
        UNSCOPED_INFO("a key held with nothing goes on");
        CHECK_FALSE(typing->handleKey(KEY_F2, MASK_NONE, false));
        UNSCOPED_INFO("and a modifier on its own");
        CHECK_FALSE(typing->handleKey(KEY_ALT, MASK_ALT, false));
        UNSCOPED_INFO("what was typed untouched");
        CHECK(typing->getText() == std::string("text"));
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: kept chords leave select all and the clipboard keys to the text", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        window->keepChords(true);
        LLLineEditor* typing = field(window, "hello");
        {
            INFO("select all");
            CHECK(typing->handleKey('A', MASK_CONTROL, false));
            CHECK(typing->hasSelection());
        }
        UNSCOPED_INFO("cut");
        CHECK(typing->handleKey('X', MASK_CONTROL, false));
        CHECK(typing->getText() == std::string());
        UNSCOPED_INFO("paste");
        CHECK(typing->handleKey('V', MASK_CONTROL, false));
        CHECK(typing->getText() == std::string("hello"));

        // A popover's field, whose keys come home to the window.
        ALQuickOpen* quick = window->quickOpen(candidates({ "one" }), "Type", "Popover", [](const std::string&) {});
        REQUIRE(quick != nullptr);
        LLLineEditor* query = quick->findChild<LLLineEditor>("query");
        REQUIRE(query != nullptr);
        query->setFocus(true);
        quick->setQuery("world");
        {
            INFO("select all there");
            CHECK(query->handleKey('A', MASK_CONTROL, false));
            CHECK(query->hasSelection());
        }
        UNSCOPED_INFO("copy there");
        CHECK(query->handleKey('C', MASK_CONTROL, false));
        UNSCOPED_INFO("a chord from there kept too");
        CHECK(query->handleKey('D', MASK_CONTROL, false));
        typing->setFocus(true);
        typing->setText(LLStringUtil::null);
        UNSCOPED_INFO("paste here");
        CHECK(typing->handleKey('V', MASK_CONTROL, false));
        UNSCOPED_INFO("what was copied there");
        CHECK(typing->getText() == std::string("world"));
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: a two-key chord waits then runs and eats its character", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        S32         saved  = 0;
        S32         found  = 0;
        window->addCommand({ "save_all", 'S', MASK_NONE, true, 'K', MASK_CONTROL }, [&]() { ++saved; return true; });
        window->addCommand({ "find", 'F', MASK_CONTROL, false }, [&]() { ++found; return true; });
        LLLineEditor* typing = field(window, "text");
        {
            INFO("nobody waits: a key goes where it goes");
            CHECK_FALSE(ALKeyChords::takeKey('S', MASK_NONE));
            CHECK_FALSE(ALKeyChords::takeChar('s'));
        }

        UNSCOPED_INFO("the first taken");
        CHECK(typing->handleKey('K', MASK_CONTROL, false));
        CHECK(ALKeyChords::waiting());
        {
            INFO("a modifier on its own is no second key");
            CHECK_FALSE(ALKeyChords::takeKey(KEY_CONTROL, MASK_CONTROL));
            CHECK(ALKeyChords::waiting());
        }
        UNSCOPED_INFO("the second taken");
        CHECK(ALKeyChords::takeKey('S', MASK_NONE));
        UNSCOPED_INFO("the command the two are");
        CHECK(saved == 1);
        CHECK_FALSE(ALKeyChords::waiting());
        UNSCOPED_INFO("its character taken with it");
        CHECK(ALKeyChords::takeChar('s'));
        UNSCOPED_INFO("and only its own");
        CHECK_FALSE(ALKeyChords::takeChar('a'));
        UNSCOPED_INFO("nothing typed");
        CHECK(typing->getText() == std::string("text"));

        typing->handleKey('K', MASK_CONTROL, false);
        ALKeyChords::takeKey('S', MASK_NONE);
        LLFrameTimer::updateFrameCount();
        UNSCOPED_INFO("a character a frame later is typing");
        CHECK_FALSE(ALKeyChords::takeChar('s'));

        typing->handleKey('K', MASK_CONTROL, false);
        {
            INFO("Control held down through both");
            CHECK(ALKeyChords::takeKey('S', MASK_CONTROL));
            CHECK(saved == 3);
        }
        UNSCOPED_INFO("no character to take");
        CHECK_FALSE(ALKeyChords::takeChar('s'));

        typing->handleKey('K', MASK_CONTROL, false);
        UNSCOPED_INFO("a second key the text or the window would take: the wait's");
        CHECK(ALKeyChords::takeKey('F', MASK_CONTROL));
        {
            INFO("and no command");
            CHECK(found == 0);
            CHECK(saved == 3);
            CHECK_FALSE(ALKeyChords::waiting());
        }
        typing->handleKey('K', MASK_CONTROL, false);
        {
            INFO("Escape: no command, the wait over");
            CHECK(ALKeyChords::takeKey(KEY_ESCAPE, MASK_NONE));
            CHECK_FALSE(ALKeyChords::waiting());
            CHECK(saved == 3);
        }

        typing->handleKey('K', MASK_CONTROL, false);
        LLLineEditor* other = field(window, "other");
        {
            INFO("the keyboard gone elsewhere: the wait is over");
            CHECK_FALSE(ALKeyChords::waiting());
            CHECK_FALSE(ALKeyChords::takeKey('S', MASK_NONE));
        }
        UNSCOPED_INFO("nothing run");
        CHECK(saved == 3);
        other->setFocus(false);
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: describe writes both keys and a menu item shows keys it ignores", "[alstudiofloater]")
    {
        ll_test::KeyNamesScope key_names([](std::string_view name) { return std::string(name); });
        const ALKeyChord two{ 'S', MASK_NONE, 'K', MASK_CONTROL };
        CHECK(two.describe() == LLKeyboard::stringFromAccelerator(MASK_CONTROL, 'K') + " " + LLKeyboard::stringFromAccelerator(MASK_NONE, 'S'));
        CHECK(ALKeyChord{ 'S', MASK_CONTROL }.describe() == LLKeyboard::stringFromAccelerator(MASK_CONTROL, 'S'));
        CHECK(ALKeyChord{}.describe().empty());
        {
            INFO("led by the first");
            CHECK(two.ledBy('K', MASK_CONTROL));
            CHECK_FALSE(two.ledBy('K', MASK_NONE));
            CHECK_FALSE(ALKeyChord{ 'K', MASK_CONTROL }.ledBy('K', MASK_CONTROL));
        }
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLMenuItemCallGL::Params p;
        p.name  = "save_all";
        p.label = "Save All";
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setShownKeys(two.describe());
        CHECK(item->getAcceleratorString() == two.describe());
        UNSCOPED_INFO("and answered to by nothing");
        CHECK(item->getAcceleratorKey() == KEY_NONE);
        item->setShownAccelerator('S', MASK_CONTROL);
        UNSCOPED_INFO("a key of its own in their place");
        CHECK(item->getAcceleratorString() == LLKeyboard::stringFromAccelerator(MASK_CONTROL, 'S'));
        item->die();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: byKeys is true only while a key or chord runs the command", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*       window = studio();
        std::vector<bool> heard;
        const auto        run    = [&]() { heard.push_back(window->byKeys()); return true; };
        window->addCommand({ "one", KEY_F6, MASK_NONE }, run);
        window->addCommand({ "two", 'S', MASK_NONE, true, 'K', MASK_CONTROL }, run);
        window->handleKeyHere(KEY_F6, MASK_NONE);
        window->handleKeyHere('K', MASK_CONTROL);
        ALKeyChords::takeKey('S', MASK_NONE);
        ALKeyChords::takeChar('s');
        run();
        UNSCOPED_INFO("by its key, by two, and not by a menu");
        CHECK(heard == std::vector<bool>({ true, true, false }));
        UNSCOPED_INFO("and not after");
        CHECK_FALSE(window->byKeys());
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: the quiet status colour stays legible or falls back to the text", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*    window = studio();
        const LLColor4 text   = ALSurface::text().get();
        for (const LLColor4& ground : { LLColor4(0.1f, 0.1f, 0.1f, 1.f), LLColor4(0.95f, 0.95f, 0.95f, 1.f), text })
        {
            CAPTURE(ground);
            window->setBackgroundColor(ground);
            const LLColor4 quiet = window->quietStatusColor();
            UNSCOPED_INFO("no louder than the text");
            REQUIRE(ALSurface::contrast(quiet, ground) <= ALSurface::contrast(text, ground) + 0.001f);
            if (ALSurface::contrast(text, ground) >= ALSurface::LEGIBLE)
            {
                UNSCOPED_INFO("legible");
                REQUIRE(ALSurface::contrast(quiet, ground) >= ALSurface::LEGIBLE - 0.01f);
            }
            else
            {
                UNSCOPED_INFO("the text itself");
                REQUIRE(quiet == text);
            }
        }
        window->closeFloater();
    }

    TEST_CASE_METHOD(alstudiofloater_data, "alstudiofloater: Caps Lock between a chord's two keys is not its second key", "[alstudiofloater]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        S32         saved  = 0;
        window->addCommand({ "save_all", 'S', MASK_NONE, true, 'K', MASK_CONTROL }, [&]() { ++saved; return true; });
        LLLineEditor* typing = field(window, "text");
        UNSCOPED_INFO("the first taken");
        CHECK(typing->handleKey('K', MASK_CONTROL, false));
        {
            INFO("Caps Lock on its own is no second key");
            CHECK_FALSE(ALKeyChords::takeKey(KEY_CAPSLOCK, MASK_NONE));
            CHECK(ALKeyChords::waiting());
        }
        {
            INFO("the key after it the second");
            CHECK(ALKeyChords::takeKey('S', MASK_NONE));
            CHECK(saved == 1);
            CHECK_FALSE(ALKeyChords::waiting());
        }
        UNSCOPED_INFO("its character taken with it");
        CHECK(ALKeyChords::takeChar('S'));
        UNSCOPED_INFO("nothing typed");
        CHECK(typing->getText() == std::string("text"));
        window->closeFloater();
    }
}
