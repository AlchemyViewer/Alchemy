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

#include "../alkeychord.h"
#include "../alpopover.h"
#include "../alsurface.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llmenugl.h"
#include "../lluictrlfactory.h"

#include "llframetimer.h"
#include "llkeyboard.h"

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

    template<> template<>
    void alstudiofloater_object::test<6>()
    {
        set_test_name("a key runs the first command it is the key of that can run; one that cannot passes it on; a person's key stands for the standard");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*              window = studio();
        std::vector<std::string> ran;
        bool                     can    = false;
        window->addCommand({ "find", 'F', MASK_CONTROL, false }, [&]() { ran.push_back("find"); return can; });
        window->addCommand({ "filter", 'F', MASK_CONTROL, false }, [&]() { ran.push_back("filter"); return true; });
        window->addCommand({ "save", 'S', MASK_CONTROL }, [&]() { ran.push_back("save"); return true; });
        window->addCommand({ "save", 'S', MASK_CONTROL | MASK_SHIFT, false }, [&]() { ran.push_back("save again"); return true; });

        ensure("a key none has is not taken", !window->runCommandKey('Q', MASK_CONTROL) && ran.empty());
        window->addCommand({ "unbound", KEY_NONE, MASK_NONE }, [&]() { ran.push_back("unbound"); return true; });
        ensure("nor no key at all, a command with none", !window->runCommandKey(KEY_NONE, MASK_NONE) && ran.empty());
        ensure("taken", window->handleKeyHere('F', MASK_CONTROL));
        ensure("by the second, the first unable", ran == std::vector<std::string>({ "find", "filter" }));
        ran.clear();
        can = true;
        ensure("taken", window->runCommandKey('F', MASK_CONTROL));
        ensure("by the first, able now", ran == std::vector<std::string>({ "find" }));

        ran.clear();
        window->rebound     = "save";
        window->reboundKey  = 'K';
        window->reboundMask = MASK_ALT;
        ensure("a person's key stands for the standard", window->runCommandKey('K', MASK_ALT) && ran == std::vector<std::string>({ "save" }));
        ensure("which it no longer answers to", !window->runCommandKey('S', MASK_CONTROL));
        ensure("a second key, not theirs to give, still does",
               window->runCommandKey('S', MASK_CONTROL | MASK_SHIFT) && ran.back() == "save again");
        window->closeFloater();
    }

    template<> template<>
    void alstudiofloater_object::test<7>()
    {
        set_test_name("a window keeping its chords takes those nothing in it had a use for; Quit and Control-Tab go on; one that does not keep them lets them go");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*   window = studio();
        LLLineEditor* typing = field(window, "text");
        S32           found  = 0;
        window->addCommand({ "find", 'F', MASK_CONTROL, false }, [&]() { ++found; return true; });
        ensure("not kept, Control-D goes on to the viewer", !typing->handleKey('D', MASK_CONTROL, false));

        window->keepChords(true);
        ensure("Control-D, which duplicates what is selected in the world", typing->handleKey('D', MASK_CONTROL, false));
        ensure("Control-L, which links it", typing->handleKey('L', MASK_CONTROL, false));
        ensure("Control-Shift-H, which goes home", typing->handleKey('H', MASK_CONTROL | MASK_SHIFT, false));
        ensure("an Alt chord", typing->handleKey('R', MASK_ALT, false));
        ensure("the Mac's own Control key", typing->handleKey('D', MASK_MAC_CONTROL, false));
        ensure("the window's own key still its own", typing->handleKey('F', MASK_CONTROL, false) && found == 1);
        ensure("Quit goes on", !typing->handleKey('Q', MASK_CONTROL, false));
        ensure("and moving between windows",
               !typing->handleKey(KEY_TAB, MASK_CONTROL, false) && !typing->handleKey(KEY_TAB, MASK_CONTROL | MASK_SHIFT, false));
        ensure("a key held with nothing goes on", !typing->handleKey(KEY_F2, MASK_NONE, false));
        ensure("and a modifier on its own", !typing->handleKey(KEY_ALT, MASK_ALT, false));
        ensure_equals("what was typed untouched", typing->getText(), std::string("text"));
        window->closeFloater();
    }

    template<> template<>
    void alstudiofloater_object::test<8>()
    {
        set_test_name("kept chords: cut, copy, paste and select all go to the text with the keyboard, here or in a window of its own");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        window->keepChords(true);
        LLLineEditor* typing = field(window, "hello");
        ensure("select all", typing->handleKey('A', MASK_CONTROL, false) && typing->hasSelection());
        ensure("cut", typing->handleKey('X', MASK_CONTROL, false));
        ensure_equals("gone", typing->getText(), std::string());
        ensure("paste", typing->handleKey('V', MASK_CONTROL, false));
        ensure_equals("back", typing->getText(), std::string("hello"));

        // A popover's field, whose keys come home to the window.
        ALQuickOpen* quick = window->quickOpen(candidates({ "one" }), "Type", "Popover", [](const std::string&) {});
        LLLineEditor* query = quick->findChild<LLLineEditor>("query");
        query->setFocus(true);
        quick->setQuery("world");
        ensure("select all there", query->handleKey('A', MASK_CONTROL, false) && query->hasSelection());
        ensure("copy there", query->handleKey('C', MASK_CONTROL, false));
        ensure("a chord from there kept too", query->handleKey('D', MASK_CONTROL, false));
        typing->setFocus(true);
        typing->setText(LLStringUtil::null);
        ensure("paste here", typing->handleKey('V', MASK_CONTROL, false));
        ensure_equals("what was copied there", typing->getText(), std::string("world"));
        window->closeFloater();
    }

    template<> template<>
    void alstudiofloater_object::test<9>()
    {
        set_test_name("two keys in turn: the first waits, the second goes to the window before the keyboard's, runs the two's command, and takes its character");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio* window = studio();
        S32         saved  = 0;
        S32         found  = 0;
        window->addCommand({ "save_all", 'S', MASK_NONE, true, 'K', MASK_CONTROL }, [&]() { ++saved; return true; });
        window->addCommand({ "find", 'F', MASK_CONTROL, false }, [&]() { ++found; return true; });
        LLLineEditor* typing = field(window, "text");
        ensure("nobody waits: a key goes where it goes", !ALKeyChords::takeKey('S', MASK_NONE) && !ALKeyChords::takeChar('s'));

        ensure("the first taken", typing->handleKey('K', MASK_CONTROL, false));
        ensure("and waited on", ALKeyChords::waiting());
        ensure("a modifier on its own is no second key", !ALKeyChords::takeKey(KEY_CONTROL, MASK_CONTROL) && ALKeyChords::waiting());
        ensure("the second taken", ALKeyChords::takeKey('S', MASK_NONE));
        ensure_equals("the command the two are", saved, 1);
        ensure("waiting no longer", !ALKeyChords::waiting());
        ensure("its character taken with it", ALKeyChords::takeChar('s'));
        ensure("and only its own", !ALKeyChords::takeChar('a'));
        ensure_equals("nothing typed", typing->getText(), std::string("text"));

        typing->handleKey('K', MASK_CONTROL, false);
        ALKeyChords::takeKey('S', MASK_NONE);
        LLFrameTimer::updateFrameCount();
        ensure("a character a frame later is typing", !ALKeyChords::takeChar('s'));

        typing->handleKey('K', MASK_CONTROL, false);
        ensure("Control held down through both", ALKeyChords::takeKey('S', MASK_CONTROL) && saved == 3);
        ensure("no character to take", !ALKeyChords::takeChar('s'));

        typing->handleKey('K', MASK_CONTROL, false);
        ensure("a second key the text or the window would take: the wait's", ALKeyChords::takeKey('F', MASK_CONTROL));
        ensure("and no command", found == 0 && saved == 3 && !ALKeyChords::waiting());
        typing->handleKey('K', MASK_CONTROL, false);
        ensure("Escape: no command, the wait over", ALKeyChords::takeKey(KEY_ESCAPE, MASK_NONE) && !ALKeyChords::waiting() && saved == 3);

        typing->handleKey('K', MASK_CONTROL, false);
        LLLineEditor* other = field(window, "other");
        ensure("the keyboard gone elsewhere: the wait is over", !ALKeyChords::waiting() && !ALKeyChords::takeKey('S', MASK_NONE));
        ensure_equals("nothing run", saved, 3);
        other->setFocus(false);
        window->closeFloater();
    }

    template<> template<>
    void alstudiofloater_object::test<10>()
    {
        set_test_name("two keys written apart; a menu item shows keys it does not answer to, and a key it answers to in their place");
        LLKeyboard::setStringTranslatorFunc([](std::string_view name) { return std::string(name); });
        const ALKeyChord two{ 'S', MASK_NONE, 'K', MASK_CONTROL };
        ensure_equals("the two", two.describe(), LLKeyboard::stringFromAccelerator(MASK_CONTROL, 'K') + " " + LLKeyboard::stringFromAccelerator(MASK_NONE, 'S'));
        ensure_equals("one", ALKeyChord{ 'S', MASK_CONTROL }.describe(), LLKeyboard::stringFromAccelerator(MASK_CONTROL, 'S'));
        ensure("none", ALKeyChord{}.describe().empty());
        ensure("led by the first", two.ledBy('K', MASK_CONTROL) && !two.ledBy('K', MASK_NONE) && !ALKeyChord{ 'K', MASK_CONTROL }.ledBy('K', MASK_CONTROL));
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLMenuItemCallGL::Params p;
        p.name  = "save_all";
        p.label = "Save All";
        LLMenuItemCallGL* item = LLUICtrlFactory::create<LLMenuItemCallGL>(p);
        item->setShownKeys(two.describe());
        ensure_equals("shown", item->getAcceleratorString(), two.describe());
        ensure("and answered to by nothing", item->getAcceleratorKey() == KEY_NONE);
        item->setShownAccelerator('S', MASK_CONTROL);
        ensure_equals("a key of its own in their place", item->getAcceleratorString(), LLKeyboard::stringFromAccelerator(MASK_CONTROL, 'S'));
        item->die();
    }

    template<> template<>
    void alstudiofloater_object::test<11>()
    {
        set_test_name("a command knows it was run by its keys -- one, or two in turn -- and not chosen with the mouse");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("by its key, by two, and not by a menu", heard == std::vector<bool>({ true, true, false }));
        ensure("and not after", !window->byKeys());
        window->closeFloater();
    }

    template<> template<>
    void alstudiofloater_object::test<12>()
    {
        set_test_name("the status line goes quiet and stays legible against the window; where even the text does not read, it stays the text");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        TestStudio*    window = studio();
        const LLColor4 text   = ALSurface::text().get();
        for (const LLColor4& ground : { LLColor4(0.1f, 0.1f, 0.1f, 1.f), LLColor4(0.95f, 0.95f, 0.95f, 1.f), text })
        {
            window->setBackgroundColor(ground);
            const LLColor4 quiet = window->quietStatusColor();
            ensure("no louder than the text", ALSurface::contrast(quiet, ground) <= ALSurface::contrast(text, ground) + 0.001f);
            if (ALSurface::contrast(text, ground) >= ALSurface::LEGIBLE)
            {
                ensure("legible", ALSurface::contrast(quiet, ground) >= ALSurface::LEGIBLE - 0.01f);
            }
            else
            {
                ensure("the text itself", quiet == text);
            }
        }
        window->closeFloater();
    }
}
