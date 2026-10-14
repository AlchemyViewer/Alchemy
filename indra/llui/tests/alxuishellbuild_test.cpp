/**
 * @file alxuishellbuild_test.cpp
 * @brief A shell build leaves registered panel classes unbuilt.
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

#include "alxuishellbuild.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>

namespace
{
        // A registered panel class that counts its constructions, standing in
        // for the viewer classes whose constructors have side effects.
        int gProbeClassBuilt = 0;

        struct ProbeClassPanel : public LLPanel
        {
            ProbeClassPanel() { ++gProbeClassBuilt; }
        };

        struct TestPanel : public LLPanel
        {
            TestPanel(const LLPanel::Params& p) : LLPanel(p) {}
        };

    struct alxuishellbuild_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static constexpr const char* XUI =
            "<panel name=\"outer\" width=\"50\" height=\"50\">"
            "<panel name=\"inner\" class=\"shell_probe_class\" width=\"10\" height=\"10\"/>"
            "</panel>";

        alxuishellbuild_data()
        {
            LLRegisterPanelClass::instance().addPanelClass("shell_probe_class",
                &LLRegisterPanelClass::defaultPanelClassBuilder<ProbeClassPanel>);
        }

        static LLPanel* build()
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseBuffer(XUI, std::strlen(XUI), root))
            {
                return nullptr;
            }
            const LLPanel::Params& defaults = LLUICtrlFactory::getDefaultParams<LLPanel>();
            LLPanel* panel = new TestPanel(defaults);
            if (!panel->initPanelXML(root, nullptr, defaults))
            {
                delete panel;
                return nullptr;
            }
            return panel;
        }
    };

    // Without the switch, the class attribute builds the registered class.
    TEST_CASE_METHOD(alxuishellbuild_data, "alxuishellbuild: without the switch the class attribute builds the registered class", "[alxuishellbuild]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        gProbeClassBuilt = 0;
        std::unique_ptr<LLPanel> panel(build());
        REQUIRE(panel != nullptr);
        UNSCOPED_INFO("the registered class was constructed");
        CHECK(gProbeClassBuilt == 1);
        CHECK(panel->findChild<LLPanel>("inner") != nullptr);
    }

    // With it, the same file builds a plain panel in the class's place, and
    // the switch is gone again once its holder is.
    TEST_CASE_METHOD(alxuishellbuild_data, "alxuishellbuild: a shell build puts a plain panel in the class's place until it ends", "[alxuishellbuild]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        gProbeClassBuilt = 0;
        {
            ALXUIShellBuild shell;
            UNSCOPED_INFO("the switch is active while its holder lives");
            CHECK(ALXUIShellBuild::active());
            std::unique_ptr<LLPanel> panel(build());
            REQUIRE(panel != nullptr);
            UNSCOPED_INFO("the registered class was not constructed");
            CHECK(gProbeClassBuilt == 0);
            UNSCOPED_INFO("the inner panel exists as a plain panel");
            CHECK(panel->findChild<LLPanel>("inner") != nullptr);
        }
        UNSCOPED_INFO("the switch is gone with its holder");
        CHECK_FALSE(ALXUIShellBuild::active());
    }
}
