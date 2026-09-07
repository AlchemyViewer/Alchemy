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

#include "../alxuishellbuild.h"
#include "../llpanel.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <cstring>

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gShellTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gShellTestAnonName;
}

namespace tut
{
    // A registered panel class that counts its constructions, standing in
    // for the viewer classes whose constructors have side effects.
    static int gProbeClassBuilt = 0;

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

    typedef test_group<alxuishellbuild_data> alxuishellbuild_test;
    typedef alxuishellbuild_test::object     alxuishellbuild_object;
    tut::alxuishellbuild_test alxuishellbuild_testgroup("alxuishellbuild");

    // Without the switch, the class attribute builds the registered class.
    template<> template<>
    void alxuishellbuild_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        gProbeClassBuilt = 0;
        std::unique_ptr<LLPanel> panel(build());
        ensure("built", panel != nullptr);
        ensure_equals("the registered class was constructed", gProbeClassBuilt, 1);
        ensure("the inner panel exists", panel->findChild<LLPanel>("inner") != nullptr);
    }

    // With it, the same file builds a plain panel in the class's place, and
    // the switch is gone again once its holder is.
    template<> template<>
    void alxuishellbuild_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }

        gProbeClassBuilt = 0;
        {
            ALXUIShellBuild shell;
            ensure("the switch is active while its holder lives", ALXUIShellBuild::active());
            std::unique_ptr<LLPanel> panel(build());
            ensure("built", panel != nullptr);
            ensure_equals("the registered class was not constructed", gProbeClassBuilt, 0);
            ensure("the inner panel exists as a plain panel", panel->findChild<LLPanel>("inner") != nullptr);
        }
        ensure("the switch is gone with its holder", !ALXUIShellBuild::active());
    }
}
