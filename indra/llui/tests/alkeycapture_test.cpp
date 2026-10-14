/**
 * @file alkeycapture_test.cpp
 * @brief Keys pressed to be given to a command, in a popover of their own.
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

#include "alkeycapture.h"

#include "alkeychord.h"
#include "alpopover.h"
#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "llkeyboard.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace
{
    struct alkeycapture_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        struct TestPanel : public LLPanel
        {
            TestPanel(const LLPanel::Params& p) : LLPanel(p) {}
        };

        std::vector<ALKeyChord> chosen;
        std::vector<ALKeyChord> asked;
        // The keys named as they are, for these tests and no others.
        ll_test::KeyNamesScope  key_names{ [](std::string_view name) { return std::string(name); } };
        std::vector<LLHandle<LLView>> anchors;

        ~alkeycapture_data()
        {
            ALKeyChords::stop();
            gFocusMgr.setKeyboardFocus(nullptr);
            // What each capture was shown over, out of the floater view
            // every later test builds its floaters in.
            for (const LLHandle<LLView>& handle : anchors)
            {
                if (LLView* view = handle.get())
                {
                    gFloaterView->removeChild(view);
                    view->die();
                }
            }
        }

        LLPanel* anchor()
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "anchor";
            p.rect = LLRect(200, 320, 320, 300);
            LLPanel* view = LLUICtrlFactory::create<TestPanel>(p);
            gFloaterView->addChild(view);
            anchors.push_back(view->getHandle());
            return view;
        }

        ALKeyCapture* show(bool two_keys)
        {
            if (!ui.ok())
            {
                SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            ALKeyCapture::Words words;
            words.title   = "Keys for Save";
            words.prompt  = "Press the keys";
            words.nothing = "(nothing yet)";
            words.set     = "Set";
            words.cancel  = "Cancel";
            return ALKeyCapture::show(
                anchor(), words, two_keys,
                [this](const ALKeyChord& chord) {
                    asked.push_back(chord);
                    return "said of " + chord.describe();
                },
                [this](const ALKeyChord& chord) { chosen.push_back(chord); });
        }

        static std::string text(ALKeyCapture* capture, const char* name) { return capture->getChild<LLTextBox>(name)->getText(); }
    };

    TEST_CASE_METHOD(alkeycapture_data, "alkeycapture: a two-key capture takes every key first and Return gives the two pressed", "[alkeycapture]")
    {
        ALKeyCapture* capture = show(true);
        REQUIRE(capture != nullptr);
        CHECK(ALKeyChords::waiting());
        CHECK(text(capture, "keys") == std::string("(nothing yet)"));
        UNSCOPED_INFO("Control-S, which the viewer's menus would take");
        CHECK(ALKeyChords::takeKey('S', MASK_CONTROL));
        CHECK(capture->pressed() == (ALKeyChord{ 'S', MASK_CONTROL }));
        CHECK(text(capture, "said") == "said of " + ALKeyChord{ 'S', MASK_CONTROL }.describe());
        UNSCOPED_INFO("still waiting for the second");
        CHECK(ALKeyChords::waiting());
        {
            INFO("a modifier alone is nothing yet");
            CHECK_FALSE(ALKeyChords::takeKey(KEY_SHIFT, MASK_SHIFT));
            CHECK(capture->pressed() == (ALKeyChord{ 'S', MASK_CONTROL }));
        }
        UNSCOPED_INFO("then a plain K, which a text would take");
        CHECK(ALKeyChords::takeKey('K', MASK_NONE));
        UNSCOPED_INFO("its character not typed");
        CHECK(ALKeyChords::takeChar('k'));
        const ALKeyChord two{ 'K', MASK_NONE, 'S', MASK_CONTROL };
        UNSCOPED_INFO("two in turn");
        CHECK(capture->pressed() == two);
        CHECK(text(capture, "keys") == two.describe());
        {
            INFO("a third starts again");
            CHECK(ALKeyChords::takeKey(KEY_F5, MASK_NONE));
            CHECK(capture->pressed() == (ALKeyChord{ KEY_F5, MASK_NONE }));
        }
        ALKeyChords::takeKey('D', MASK_CONTROL);
        UNSCOPED_INFO("Return gives them");
        CHECK(ALKeyChords::takeKey(KEY_RETURN, MASK_NONE));
        {
            INFO("once, as pressed");
            REQUIRE(chosen.size() == 1);
            CHECK(chosen[0] == (ALKeyChord{ 'D', MASK_CONTROL, KEY_F5, MASK_NONE }));
        }
        CHECK_FALSE(ALKeyChords::waiting());
    }

    TEST_CASE_METHOD(alkeycapture_data, "alkeycapture: a one-key capture keeps the last key and Escape gives nothing", "[alkeycapture]")
    {
        ALKeyCapture* capture = show(false);
        REQUIRE(capture != nullptr);
        {
            INFO("Return with nothing pressed gives nothing");
            CHECK(ALKeyChords::takeKey(KEY_RETURN, MASK_NONE));
            CHECK(chosen.empty());
            CHECK(ALKeyChords::waiting());
        }
        ALKeyChords::takeKey('K', MASK_CONTROL);
        ALKeyChords::takeKey('S', MASK_NONE);
        UNSCOPED_INFO("the second in place of the first");
        CHECK(capture->pressed() == (ALKeyChord{ 'S', MASK_NONE }));
        ALKeyChords::takeChar('s');
        ALPopover* up = capture->popover();
        REQUIRE(up != nullptr);
        CHECK(ALKeyChords::takeKey(KEY_ESCAPE, MASK_NONE));
        CHECK(chosen.empty());
        {
            INFO("gone, escaped");
            CHECK(up->escaped());
            CHECK_FALSE(ALKeyChords::waiting());
        }
    }
}
