/**
 * @file llui_test_checks.cpp
 * @brief What llui's test binaries set up for the process once -- the headless
 *        UI and the colour table -- what they put back after every case as a
 *        frame of the viewer would, and what they give the state guard to
 *        check: the floater view, the menu container, the key names, the root
 *        view and the colour table's colours.
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

#include "../llclipboard.h"
#include "../llfloater.h"
#include "../llfocusmgr.h"
#include "../llmenugl.h"
#include "../llui.h"
#include "../lluicolortable.h"

#include "llkeyboard.h"
#include "llmortician.h"

#include "alheadlessui_fixture.h"
#include "alteststateguard.h"
#include "aluistatescope.h"
#if LLUI_TEST_GL
#include "aluigl_fixture.h"
#endif

#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    // A pointer as text: which object, or none. Only ever printed, never
    // followed, since what it points at may be gone.
    std::string which(const void* p)
    {
        return p ? llformat("%p", p) : std::string("none");
    }

    // LLKeyboard keeps the function it names keys with to itself and its
    // subclasses.
    struct KeyNames : LLKeyboard
    {
        static const void* translator() { return reinterpret_cast<const void*>(mStringTranslator); }
    };

    // The colour table's colours, the loaded and the user's, each name with
    // its value, sorted: what a case that sets one, or loads sheets of its
    // own, puts back. Not its generation, which a colour set and set back
    // still moves.
    std::string colourTable()
    {
        if (!LLUIColorTable::instanceExists())
        {
            return "none";
        }
        const LLUIColorTable& table = LLUIColorTable::instance();
        const auto list = [](const auto& colours)
        {
            std::vector<std::string> lines;
            for (const auto& [name, colour] : colours)
            {
                const LLColor4 c = colour.get();
                lines.push_back(llformat("%s=%g,%g,%g,%g", name.c_str(), c.mV[0], c.mV[1], c.mV[2], c.mV[3]));
            }
            std::sort(lines.begin(), lines.end());
            std::string text;
            for (const std::string& line : lines)
            {
                text += (text.empty() ? "" : " ") + line;
            }
            return text;
        };
        return llformat("%zu loaded, %zu set by the user: ", table.getLoadedColors().size(),
                        table.getUserColors().size())
            + list(table.getUserColors()) + " | " + list(table.getLoadedColors());
    }

    class LLUIChecks : public Catch::EventListenerBase
    {
    public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(Catch::TestRunInfo const&) override
        {
            // The UI up before the first case, as the viewer has it before it
            // builds a view, so that no case is the one that made it. The GL
            // tests' fonts put their glyphs in textures, in the shared
            // context.
#if LLUI_TEST_GL
            ll_test::uiGL();
            ll_test::HeadlessUI::get(/*gl_textures=*/true);
#else
            ll_test::HeadlessUI::get();
#endif
            // The colour table every widget reads, made before the first case
            // and loaded by none: whichever case first asked for a colour
            // would otherwise be the one that made it.
            LLUIColorTable::getInstance();

            // Every floater built adds itself to it, and every menu shown
            // shows in its container: a case that sets one of its own puts
            // the one before back.
            ll_test::StateGuard::add("gFloaterView", [] { return which(gFloaterView); });
            ll_test::StateGuard::add("LLMenuGL's menu container", [] { return which(LLMenuGL::sMenuContainer); });
            ll_test::StateGuard::add("LLKeyboard's key names", [] { return which(KeyNames::translator()); });
            ll_test::StateGuard::add("the UI's root view", []
            {
                return LLUI::instanceExists() ? which(LLUI::getInstance()->getRootView()) : std::string("no UI");
            });
            // A case that sets a colour sets it back; one that loads sheets
            // of its own loads what was there again.
            ll_test::StateGuard::add("the colour table", colourTable);
        }

        // What a frame of the viewer clears between one thing and the next,
        // cleared between cases: no popover left up to hold the keys, the
        // keyboard, the mouse and the top let go of, nothing on the
        // clipboard, and the views that asked to die gone.
        void testCaseEnded(Catch::TestCaseStats const&) override
        {
            ll_test::escapePopovers();
            gFocusMgr.setMouseCapture(nullptr);
            gFocusMgr.setKeyboardFocus(nullptr);
            gFocusMgr.setTopCtrl(nullptr);
            if (LLClipboard::instanceExists())
            {
                LLClipboard::instance().reset();
                LLClipboard::instance().copyToClipboard(std::string_view(), 0, 0, /*use_primary=*/true);
            }
            LLMortician::updateClass();
        }
    };
}

CATCH_REGISTER_LISTENER(LLUIChecks)
