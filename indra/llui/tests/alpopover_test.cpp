/**
 * @file alpopover_test.cpp
 * @brief Where it lands, and the three ways out of it.
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

#include "alpopover.h"

#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
    struct alpopover_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        struct TestPanel : public LLPanel
        {
            TestPanel(const LLPanel::Params& p) : LLPanel(p) {}
        };

        // Something to hang one off, placed well inside the screen.
        static LLPanel* anchor(S32 left = 200, S32 bottom = 300)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "anchor";
            p.rect = LLRect(left, bottom + 20, left + 120, bottom);
            LLPanel* view = LLUICtrlFactory::create<TestPanel>(p);
            gFloaterView->addChild(view);
            return view;
        }

        // One made and not yet opened, as a field makes its own.
        struct TestPopover : public ALPopover
        {
            TestPopover() : ALPopover(ALPopover::paramsFor(120, 60)) {}
        };

        static LLPanel* content(S32 width = 160, S32 height = 90)
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "content";
            p.rect = LLRect(0, height, width, 0);
            return LLUICtrlFactory::create<TestPanel>(p);
        }
    };

    // Under the control, its left edge with the control's, and the size the
    // content asked for: what is in it decides how big it is.
    TEST_CASE_METHOD(alpopover_data, "alpopover: a popover hangs under its control left-aligned at its content's size", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel* over = anchor();
        LLPanel* body = content(160, 90);
        ALPopover* popover = ALPopover::show(over, body);
        REQUIRE(popover != nullptr);
        CHECK(popover->getVisible());

        const LLRect screen = over->calcScreenRect();
        const LLRect where = popover->getRect();
        {
            INFO("the size the content asked for");
            CHECK(where.getWidth() == 160);
            CHECK(where.getHeight() == 90);
        }
        UNSCOPED_INFO("left edges line up");
        CHECK(where.mLeft == screen.mLeft);
        UNSCOPED_INFO("and it hangs from the bottom of the control");
        CHECK(where.mTop == screen.mBottom);

        UNSCOPED_INFO("the content is in it");
        CHECK(popover->findChild<LLPanel>("content", true) == body);
        popover->closeFloater();
        over->die();
    }

    // A popover that would hang off the bottom of the screen goes above the
    // control instead: one a person cannot see the whole of has not opened.
    TEST_CASE_METHOD(alpopover_data, "alpopover: a popover that would hang off the screen's bottom goes above instead", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel* over = anchor(200, 10);
        ALPopover* popover = ALPopover::show(over, content(160, 90));
        REQUIRE(popover != nullptr);
        UNSCOPED_INFO("and is on the screen");
        CHECK(popover->getRect().mBottom >= 0);
        UNSCOPED_INFO("above the control rather than below it");
        CHECK(popover->getRect().mBottom >= over->calcScreenRect().mTop);
        popover->closeFloater();
        over->die();
    }

    // Three ways out, and a caller applying on close has to tell them apart.
    TEST_CASE_METHOD(alpopover_data, "alpopover: each of the three ways out says whether the popover was escaped", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // Settled: what was chosen stands.
        {
            LLPanel* over = anchor();
            ALPopover* popover = ALPopover::show(over, content());
            REQUIRE(popover != nullptr);
            std::vector<bool> said;
            popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
            popover->settle();
            REQUIRE(said.size() == 1u);
            UNSCOPED_INFO("and said it was not escaped");
            CHECK_FALSE(said.front());
            over->die();
        }

        // Escaped: the caller keeps what it had.
        {
            LLPanel* over = anchor();
            ALPopover* popover = ALPopover::show(over, content());
            REQUIRE(popover != nullptr);
            std::vector<bool> said;
            popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
            CHECK(popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
            REQUIRE(said.size() == 1u);
            UNSCOPED_INFO("and said it was escaped");
            CHECK(said.front());
            over->die();
        }

        // Looking somewhere else is the usual way out, and it settles: a
        // person who has chosen and moved on has finished.
        {
            LLPanel* over = anchor();
            ALPopover* popover = ALPopover::show(over, content());
            REQUIRE(popover != nullptr);
            std::vector<bool> said;
            popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
            popover->onFocusLost();
            REQUIRE(said.size() == 1u);
            UNSCOPED_INFO("and said it was not escaped");
            CHECK_FALSE(said.front());
            over->die();
        }
    }

    // Nothing to hang it off is nothing to open, and the content given is not
    // leaked over it.
    TEST_CASE_METHOD(alpopover_data, "alpopover: with no anchor or no content nothing opens", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel*                orphan = content();
        const LLHandle<LLPanel> given  = orphan->getHandle();
        UNSCOPED_INFO("no anchor, no popover");
        CHECK(ALPopover::show(nullptr, orphan) == nullptr);
        UNSCOPED_INFO("and the content given deleted, not leaked");
        CHECK(given.isDead());
        LLPanel* over = anchor();
        UNSCOPED_INFO("and no content, none either");
        CHECK(ALPopover::show(over, nullptr) == nullptr);
        over->die();
    }

    TEST_CASE_METHOD(alpopover_data, "alpopover: a control at the screen's right edge opens its popover on the screen", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const S32 screen_width = gFloaterView->getRect().getWidth();
        LLPanel* over = anchor(screen_width - 40, 300);
        ALPopover* popover = ALPopover::show(over, content(160, 90));
        REQUIRE(popover != nullptr);
        UNSCOPED_INFO("on the screen, not off its right edge");
        CHECK(popover->getRect().mRight <= screen_width);
        UNSCOPED_INFO("and no narrower for it");
        CHECK(popover->getRect().getWidth() == 160);
        popover->closeFloater();
        over->die();
    }

    // A popover is a window of its own, so a key it does not take goes home
    // to the window its anchor is in, and it asks before the viewer's menu
    // does -- otherwise a window's own shortcuts stop working the moment one
    // of its popovers has the keyboard.
    TEST_CASE_METHOD(alpopover_data, "alpopover: a key the popover does not take goes home to its anchor's window", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        struct Home : public LLFloater
        {
            Home(const LLFloater::Params& p) : LLFloater(LLSD(), p) {}
            bool handleKeyHere(KEY key, MASK mask) override
            {
                heard.push_back(key);
                return key == 'F' && mask == MASK_CONTROL;
            }
            std::vector<KEY> heard;
        };
        LLFloater::Params fp(LLFloater::getDefaultParams());
        fp.name = "home";
        fp.rect = LLRect(100, 500, 500, 100);
        fp.save_rect = false;
        fp.save_visibility = false;
        Home* home = new Home(fp);
        LLPanel* over = anchor();
        home->addChild(over);

        ALPopover* popover = ALPopover::show(over, content());
        REQUIRE(popover != nullptr);
        UNSCOPED_INFO("it knows where home is");
        CHECK(popover->home() == home);
        UNSCOPED_INFO("and asks before the viewer's own menu does");
        CHECK(popover->hasAccelerators());
        UNSCOPED_INFO("a key home takes is taken");
        CHECK(popover->handleKeyHere('F', MASK_CONTROL));
        UNSCOPED_INFO("and home heard it");
        CHECK(home->heard.size() == 1u);
        UNSCOPED_INFO("a key home declines is declined");
        CHECK_FALSE(popover->handleKeyHere('Q', MASK_CONTROL));
        UNSCOPED_INFO("having been asked");
        CHECK(home->heard.size() == 2u);
        UNSCOPED_INFO("escape is still the popover's own");
        CHECK(popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        UNSCOPED_INFO("and never reaches home");
        CHECK(home->heard.size() == 2u);

        // Hung off nothing in a window, it has no home and no claim on the
        // menu's keys.
        LLPanel* loose = anchor();
        ALPopover* alone = ALPopover::show(loose, content());
        REQUIRE(alone != nullptr);
        UNSCOPED_INFO("no home");
        CHECK(alone->home() == nullptr);
        UNSCOPED_INFO("no claim");
        CHECK_FALSE(alone->hasAccelerators());
        UNSCOPED_INFO("and a key it does not take is declined");
        CHECK_FALSE(alone->handleKeyHere('Q', MASK_CONTROL));
        alone->closeFloater();
        loose->die();
        home->die();
    }

    // A popover built to remember its size opens at the size the last of
    // its kind was left at, and one of another kind does not.
    TEST_CASE_METHOD(alpopover_data, "alpopover: a remembering popover opens at the size the last of its kind was left at", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        // As a popover with more to it than a panel is made: derived, and
        // opened with openBeside.
        struct Remembering : public ALPopover
        {
            Remembering(const LLFloater::Params& p) : ALPopover(p) {}
        };
        // Kinds of its own each time it runs: the sizes are kept for the
        // process, and a kind seen before would open at what it was left at.
        static S32        runs       = 0;
        const std::string kind       = "popover_test_kind_" + std::to_string(++runs);
        const std::string other_kind = "popover_test_other_" + std::to_string(runs);
        LLPanel* over = anchor();
        ALPopover* first = new Remembering(ALPopover::paramsRemembered(kind, 200, 120));
        first->openBeside(over);
        UNSCOPED_INFO("opens at the size given");
        CHECK(first->getRect().getWidth() == 200);
        first->reshape(260, 150);
        first->closeFloater();

        ALPopover* second = new Remembering(ALPopover::paramsRemembered(kind, 200, 120));
        second->openBeside(over);
        UNSCOPED_INFO("the next of its kind opens as wide as the last was left");
        CHECK(second->getRect().getWidth() == 260);
        UNSCOPED_INFO("and as tall");
        CHECK(second->getRect().getHeight() == 150);
        second->closeFloater();

        ALPopover* other = new Remembering(ALPopover::paramsRemembered(other_kind, 200, 120));
        other->openBeside(over);
        UNSCOPED_INFO("another kind is not told");
        CHECK(other->getRect().getWidth() == 200);
        other->closeFloater();
        over->die();
    }
    // A popover is its window's dependent: the window closing takes it,
    // however the window went, and one that has already gone is not
    // touched when the window goes after it.
    TEST_CASE_METHOD(alpopover_data, "alpopover: a popover goes with its window and one gone first is left alone", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const auto home = []() {
            LLFloater::Params fp(LLFloater::getDefaultParams());
            fp.name            = "home";
            fp.rect            = LLRect(100, 500, 500, 100);
            fp.save_rect       = false;
            fp.save_visibility = false;
            LLFloater* made    = new LLFloater(LLSD(), fp);
            made->openFloater();
            return made;
        };

        LLFloater* window = home();
        LLPanel*   over   = anchor();
        window->addChild(over);
        ALPopover*        popover = ALPopover::show(over, content());
        REQUIRE(popover != nullptr);
        std::vector<bool> said;
        popover->onClosed([&said](bool escaped) { said.push_back(escaped); });
        UNSCOPED_INFO("its window's dependent");
        CHECK(popover->getDependee() == window);
        window->closeFloater();
        UNSCOPED_INFO("gone with its window");
        CHECK(popover->isDead());
        REQUIRE(said.size() == 1u);
        UNSCOPED_INFO("and not as escaped: nobody pressed escape");
        CHECK_FALSE(said.front());

        // Gone first: the window closing after it finds nothing of it.
        LLFloater* again = home();
        LLPanel*   under = anchor();
        again->addChild(under);
        ALPopover* first = ALPopover::show(under, content());
        REQUIRE(first != nullptr);
        first->escape();
        CHECK(first->isDead());
        again->closeFloater();
        UNSCOPED_INFO("and the window closed after it");
        CHECK(again->isDead());
    }

    // Beside a rect of the screen given for it -- where something on a
    // zoomed canvas is drawn -- rather than beside the anchor's own rect,
    // and still the dependent of the anchor's window.
    TEST_CASE_METHOD(alpopover_data, "alpopover: showAt hangs a popover from a screen rect given for it", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel*     over   = anchor();
        const LLRect screen(300, 260, 380, 240);
        ALPopover*   popover = ALPopover::showAt(screen, over, content(160, 90));
        REQUIRE(popover != nullptr);
        CHECK(popover->getVisible());
        const LLRect where = popover->getRect();
        UNSCOPED_INFO("its left with the rect's");
        CHECK(where.mLeft == screen.mLeft);
        UNSCOPED_INFO("hung from the rect's bottom");
        CHECK(where.mTop == screen.mBottom);
        UNSCOPED_INFO("not the anchor's");
        CHECK(where.mLeft != over->calcScreenRect().mLeft);
        popover->closeFloater();
        UNSCOPED_INFO("no anchor, no popover");
        CHECK(ALPopover::showAt(screen, nullptr, content()) == nullptr);
        over->die();
    }

    // Quiet as a menu is, and closed from the keyboard with nothing else
    // taking it -- no window over which to hand it back -- it gives the
    // keyboard back to what had it, not to the world.
    TEST_CASE_METHOD(alpopover_data, "alpopover: a popover is silent and escaped gives the keyboard back to what had it", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel*              over = anchor();
        LLLineEditor::Params  lp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
        lp.name               = "field";
        lp.rect               = LLRect(0, 20, 120, 0);
        LLLineEditor* field   = LLUICtrlFactory::create<LLLineEditor>(lp);
        over->addChild(field);
        field->setFocus(true);
        UNSCOPED_INFO("the field has the keyboard");
        CHECK(gFocusMgr.getKeyboardFocus() == field);
        ALPopover* popover = ALPopover::showOver(over, content(), "Go to line");
        REQUIRE(popover != nullptr);
        CHECK(popover->getSoundFlags() == LLView::SILENT);
        UNSCOPED_INFO("the popover has the keyboard");
        CHECK(gFocusMgr.getKeyboardFocus() != field);
        popover->escape();
        UNSCOPED_INFO("the keyboard back in the field");
        CHECK(gFocusMgr.getKeyboardFocus() == field);
        gFocusMgr.setKeyboardFocus(nullptr);
        over->die();
    }

    // Over a window with more in it than what had the keyboard -- a field
    // before it in the tab order, say -- escape and return each give the
    // keyboard back to what had it: the window taking it as its dependent
    // closes puts it on its first control, which is not where the keys were,
    // and the arrows there move the avatar.
    TEST_CASE_METHOD(alpopover_data, "alpopover: escape and return over a window give the keyboard back to what had it", "[alpopover]")
    {
        if (!ui.ok())
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
        const auto field = [window](const char* name, S32 bottom) {
            LLLineEditor::Params lp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
            lp.name           = name;
            lp.rect           = LLRect(10, bottom + 20, 130, bottom);
            LLLineEditor* one = LLUICtrlFactory::create<LLLineEditor>(lp);
            window->addChild(one);
            return one;
        };
        LLLineEditor* first = field("first", 300);
        LLLineEditor* typed = field("typed", 200);
        (void)first;
        for (const bool escaping : { true, false })
        {
            CAPTURE(escaping);
            typed->setFocus(true);
            UNSCOPED_INFO("the second field has the keyboard");
            REQUIRE(gFocusMgr.getKeyboardFocus() == typed);
            ALPopover* popover = ALPopover::showOver(window, content(), "Go to line");
            REQUIRE(popover != nullptr);
            UNSCOPED_INFO("the popover has the keyboard");
            REQUIRE(gFocusMgr.getKeyboardFocus() != typed);
            if (escaping)
            {
                popover->escape();
            }
            else
            {
                popover->settle();
            }
            UNSCOPED_INFO("the keyboard back where it was");
            REQUIRE(gFocusMgr.getKeyboardFocus() == typed);
        }
        gFocusMgr.setKeyboardFocus(nullptr);
        window->closeFloater();
    }

    TEST_CASE_METHOD(alpopover_data, "alpopover: a slot holds one popover and says how it went; a new one escapes it", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel*                          over = anchor();
        ALPopoverSlot                     slot;
        std::vector<std::pair<int, bool>> said;
        const auto                        tell = [&said](int which) {
            return [&said, which](bool escaped) { said.emplace_back(which, escaped); };
        };
        CHECK_FALSE(slot.isOpen());
        CHECK(slot.get() == nullptr);

        ALPopover* first = ALPopover::show(over, content());
        slot.hold(first, tell(1));
        CHECK(slot.isOpen());
        CHECK(slot.get() == first);
        slot.close(false);
        UNSCOPED_INFO("settled, and told so");
        CHECK(said == std::vector<std::pair<int, bool>>{ { 1, false } });
        {
            INFO("empty again");
            CHECK_FALSE(slot.isOpen());
            CHECK(slot.get() == nullptr);
        }
        slot.close();
        UNSCOPED_INFO("nothing to close, nobody told");
        CHECK(said.size() == 1u);

        slot.hold(ALPopover::show(over, content()), tell(2));
        slot.close();
        {
            INFO("escaped, and told so");
            REQUIRE(said.size() == 2);
            CHECK(said[1] == std::make_pair(2, true));
        }

        slot.hold(ALPopover::show(over, content()), tell(3));
        TestPopover* fourth = new TestPopover();
        slot.hold(fourth, tell(4));
        {
            INFO("the one it held escaped for the next");
            REQUIRE(said.size() == 3);
            CHECK(said[2] == std::make_pair(3, true));
        }
        UNSCOPED_INFO("and the next held");
        CHECK(slot.get() == fourth);
        fourth->openBeside(over);
        fourth->onFocusLost();
        {
            INFO("one looked away from is settled, and told so");
            REQUIRE(said.size() == 4);
            CHECK(said[3] == std::make_pair(4, false));
        }
        UNSCOPED_INFO("and the slot empty");
        CHECK_FALSE(slot.isOpen());
        over->die();
    }

    TEST_CASE_METHOD(alpopover_data, "alpopover: a popover held again tells only the new listener; one let go tells none", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel*         over = anchor();
        ALPopoverSlot    slot;
        std::vector<int> said;
        ALPopover*       popover = ALPopover::show(over, content());
        REQUIRE(popover != nullptr);
        slot.hold(popover, [&said](bool) { said.push_back(1); });
        slot.hold(popover, [&said](bool) { said.push_back(2); });
        {
            INFO("still up, held");
            CHECK(slot.get() == popover);
            CHECK(popover->getVisible());
            CHECK(said.empty());
        }
        slot.close();
        UNSCOPED_INFO("the new listener told, the old not");
        CHECK(said == std::vector<int>{ 2 });

        const auto dead = [](const LLHandle<ALPopover>& handle) { return !handle.get() || handle.get()->isDead(); };
        said.clear();
        ALPopover*                dropped = ALPopover::show(over, content());
        REQUIRE(dropped != nullptr);
        const LLHandle<ALPopover> handle  = dropped->getDerivedHandle<ALPopover>();
        slot.hold(dropped, [&said](bool) { said.push_back(3); });
        slot.drop();
        UNSCOPED_INFO("dropped: it dies");
        CHECK(dead(handle));
        UNSCOPED_INFO("and the slot is empty");
        CHECK_FALSE(slot.isOpen());
        if (ALPopover* late = handle.get())
        {
            late->closeFloater();
        }
        UNSCOPED_INFO("nobody told, even as it closes after");
        CHECK(said.empty());

        LLHandle<ALPopover> scoped;
        {
            ALPopoverSlot gone;
            ALPopover*    held = ALPopover::show(over, content());
            REQUIRE(held != nullptr);
            scoped             = held->getDerivedHandle<ALPopover>();
            gone.hold(held, [&said](bool) { said.push_back(4); });
        }
        UNSCOPED_INFO("a slot that goes: it dies");
        CHECK(dead(scoped));
        UNSCOPED_INFO("nobody told");
        CHECK(said.empty());
        over->die();
    }

    TEST_CASE_METHOD(alpopover_data, "alpopover: one told its popover went may open the next and the slot holds it", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel*      over = anchor();
        ALPopoverSlot slot;
        TestPopover*  next = nullptr;
        bool          next_told = false;
        slot.hold(ALPopover::show(over, content()), [&](bool) {
            next = new TestPopover();
            slot.hold(next, [&next_told](bool) { next_told = true; });
        });
        slot.close();
        {
            INFO("the next held");
            REQUIRE(next != nullptr);
            CHECK(slot.get() == next);
        }
        next->openBeside(over);
        slot.close();
        {
            INFO("and told of as it goes");
            CHECK(next_told);
            CHECK_FALSE(slot.isOpen());
        }
        over->die();
    }

    TEST_CASE_METHOD(alpopover_data, "alpopover: an owner told its popover went may delete the slot that told it", "[alpopover]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLPanel* over  = anchor();
        auto*    owner = new ALPopoverSlot();
        bool     told  = false;
        owner->hold(ALPopover::show(over, content()), [&owner, &told](bool) {
            told = true;
            delete owner;
            owner = nullptr;
        });
        owner->close();
        {
            INFO("told, and gone");
            CHECK(told);
            CHECK(owner == nullptr);
        }
        over->die();
    }
}
