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

#include "../test/lltut.h"

#include <string>
#include <vector>

class LLAvatarName;
const std::string gKeyCaptureTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gKeyCaptureTestAnonName;
}

namespace tut
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

        alkeycapture_data() { LLKeyboard::setStringTranslatorFunc([](std::string_view name) { return std::string(name); }); }
        ~alkeycapture_data()
        {
            ALKeyChords::stop();
            gFocusMgr.setKeyboardFocus(nullptr);
        }

        LLPanel* anchor()
        {
            LLPanel::Params p(LLUICtrlFactory::getDefaultParams<LLPanel>());
            p.name = "anchor";
            p.rect = LLRect(200, 320, 320, 300);
            LLPanel* view = LLUICtrlFactory::create<TestPanel>(p);
            gFloaterView->addChild(view);
            return view;
        }

        ALKeyCapture* show(bool two_keys)
        {
            if (!ui.ok())
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
    typedef test_group<alkeycapture_data> alkeycapture_group;
    typedef alkeycapture_group::object    alkeycapture_object;
    alkeycapture_group                    alkeycapture_instance("alkeycapture");

    template<> template<>
    void alkeycapture_object::test<1>()
    {
        set_test_name("every key taken before anything else would: the viewer's menus' Control-S, a text's plain S; two in turn; Return gives them");
        ALKeyCapture* capture = show(true);
        ensure("shown, and waiting", capture != nullptr && ALKeyChords::waiting());
        ensure_equals("nothing yet", text(capture, "keys"), std::string("(nothing yet)"));
        ensure("Control-S, which the viewer's menus would take", ALKeyChords::takeKey('S', MASK_CONTROL));
        ensure("pressed", capture->pressed() == (ALKeyChord{ 'S', MASK_CONTROL }));
        ensure_equals("said", text(capture, "said"), "said of " + ALKeyChord{ 'S', MASK_CONTROL }.describe());
        ensure("waiting still", ALKeyChords::waiting());
        ensure("a modifier alone is nothing yet", !ALKeyChords::takeKey(KEY_SHIFT, MASK_SHIFT) && capture->pressed() == (ALKeyChord{ 'S', MASK_CONTROL }));
        ensure("then a plain K", ALKeyChords::takeKey('K', MASK_NONE));
        ensure("its character not typed", ALKeyChords::takeChar('k'));
        const ALKeyChord two{ 'K', MASK_NONE, 'S', MASK_CONTROL };
        ensure("two in turn", capture->pressed() == two);
        ensure_equals("shown", text(capture, "keys"), two.describe());
        ensure("a third starts again", ALKeyChords::takeKey(KEY_F5, MASK_NONE) && capture->pressed() == (ALKeyChord{ KEY_F5, MASK_NONE }));
        ALKeyChords::takeKey('D', MASK_CONTROL);
        ensure("Return gives them", ALKeyChords::takeKey(KEY_RETURN, MASK_NONE));
        ensure("once, as pressed", chosen.size() == 1 && chosen[0] == (ALKeyChord{ 'D', MASK_CONTROL, KEY_F5, MASK_NONE }));
        ensure("and the wait over", !ALKeyChords::waiting());
    }

    template<> template<>
    void alkeycapture_object::test<2>()
    {
        set_test_name("one key where one is all there may be; Return with nothing pressed gives nothing; Escape keeps what was");
        ALKeyCapture* capture = show(false);
        ensure("Return with nothing", ALKeyChords::takeKey(KEY_RETURN, MASK_NONE) && chosen.empty() && ALKeyChords::waiting());
        ALKeyChords::takeKey('K', MASK_CONTROL);
        ALKeyChords::takeKey('S', MASK_NONE);
        ensure("the second in place of the first", capture->pressed() == (ALKeyChord{ 'S', MASK_NONE }));
        ALKeyChords::takeChar('s');
        ALPopover* up = capture->popover();
        ensure("Escape", ALKeyChords::takeKey(KEY_ESCAPE, MASK_NONE));
        ensure("nothing given", chosen.empty());
        ensure("gone, escaped", up->escaped() && !ALKeyChords::waiting());
    }
}
