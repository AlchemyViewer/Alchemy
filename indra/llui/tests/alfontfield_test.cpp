/**
 * @file alfontfield_test.cpp
 * @brief A font as the three attributes a file writes for one, and the popover that picks them.
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

#include "alfontfield.h"

#include "alpopover.h"
#include "alspecimenlist.h"
#include "../llcheckboxctrl.h"
#include "../llcombobox.h"
#include "../llfocusmgr.h"
#include "../lllineeditor.h"
#include "../lltextbox.h"
#include "../lluictrlfactory.h"

#include "llfontgl.h"
#include "llmortician.h"

#include "alheadlessui_fixture.h"
#include "aluistatescope.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alfontfield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
        // Focus and capture as the test found them, whatever it leaves
        // them on.
        ll_test::FocusScope  focus;

        // Each test's popovers escaped and its dead views reaped as it ends,
        // as the next frame would: a test that empties the graveyard itself
        // then finds only its own views there, and a popover left open
        // holds the keys and is what the next test would find showing.
        ~alfontfield_data()
        {
            ll_test::escapePopovers();
            LLMortician::updateClass();
        }

        static ALFontField* make()
        {
            ALFontField::Params p(LLUICtrlFactory::getDefaultParams<ALFontField>());
            p.name = "font";
            p.rect = LLRect(100, 322, 300, 300);
            ALFontField* field = LLUICtrlFactory::create<ALFontField>(p);
            gFloaterView->addChild(field);
            return field;
        }

        // The popover is a floater of its own over the floater view, and
        // the newest one there is the one just opened.
        static ALPopover* popoverOpen()
        {
            ALPopover* found = nullptr;
            for (LLView* child : *gFloaterView->getChildList())
            {
                if (ALPopover* popover = child->as<ALPopover>(); popover && popover->getVisible())
                {
                    found = popover;
                }
            }
            return found;
        }

        struct Said
        {
            std::string part;
            std::string value;
        };
    };

    // The value is the name, and the other two are their own; the specimen
    // is drawn in the face the three name, found when they change and not
    // again.
    TEST_CASE_METHOD(alfontfield_data, "alfontfield: the value is the name; style and size are attributes of their own", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFontField* field = make();
        UNSCOPED_INFO("a field given nothing draws in a face all the same");
        REQUIRE(field->font() != nullptr);

        field->setValue("SansSerif");
        UNSCOPED_INFO("the value is the name");
        CHECK(field->getValue().asString() == std::string("SansSerif"));
        UNSCOPED_INFO("and the line says it");
        CHECK(field->getChild<LLLineEditor>("text")->getText() == std::string("SansSerif"));
        const LLFontGL* plain = field->font();

        field->setStyle("BOLD");
        UNSCOPED_INFO("style is its own");
        CHECK(field->style() == std::string("BOLD"));
        UNSCOPED_INFO("and not the value");
        CHECK(field->getValue().asString() == std::string("SansSerif"));
        UNSCOPED_INFO("a style is a different face");
        CHECK(field->font() != plain);
        UNSCOPED_INFO("and the bold one");
        CHECK((field->font()->getFontDesc().getStyle() & LLFontGL::BOLD) != 0);

        field->setSize("Large");
        UNSCOPED_INFO("size is its own");
        CHECK(field->size() == std::string("Large"));

        field->setValue("NoSuchFace");
        UNSCOPED_INFO("a name nobody declared still draws in something");
        REQUIRE(field->font() != nullptr);
        field->die();
    }

    // Typing a name in the line commits it, and says which part changed.
    TEST_CASE_METHOD(alfontfield_data, "alfontfield: a name typed in the line commits it as the name part", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFontField* field = make();
        field->setValue("SansSerif");
        std::vector<Said> said;
        field->onPartCommit([&said](const std::string& part, const std::string& value)
        {
            said.push_back({ part, value });
        });
        S32 commits = 0;
        field->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        LLLineEditor* line = field->getChild<LLLineEditor>("text");
        line->setText(std::string("Monospace"));
        line->onCommit();
        UNSCOPED_INFO("the field committed");
        CHECK(commits == 1);
        UNSCOPED_INFO("one part changed");
        REQUIRE(said.size() == 1u);
        UNSCOPED_INFO("the name, which is the part with no name");
        REQUIRE(said.front().part.empty());
        UNSCOPED_INFO("to what was typed");
        CHECK(said.front().value == std::string("Monospace"));
        UNSCOPED_INFO("and the value says so");
        CHECK(field->getValue().asString() == std::string("Monospace"));

        line->onCommit();
        UNSCOPED_INFO("committing the same name again is a commit");
        CHECK(commits == 2);
        UNSCOPED_INFO("but not a change");
        REQUIRE(said.size() == 1u);
        field->die();
    }

    // The popover: escaped, nothing is written; closed any other way, only
    // the parts that changed are, each as its own attribute.
    TEST_CASE_METHOD(alfontfield_data, "alfontfield: an escaped popover writes nothing and return writes what changed", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFontField* field = make();
        field->setValue("SansSerif");
        field->setStyle("NORMAL");
        std::vector<Said> said;
        field->onPartCommit([&said](const std::string& part, const std::string& value)
        {
            said.push_back({ part, value });
        });
        S32 commits = 0;
        field->setCommitCallback([&commits](LLUICtrl*, const LLSD&) { ++commits; });

        // Pressing the specimen opens it.
        field->handleMouseDown(4, 10, MASK_NONE);
        ALPopover* popover = popoverOpen();
        UNSCOPED_INFO("the popover opened");
        REQUIRE(popover != nullptr);
        UNSCOPED_INFO("with the sizes in it");
        REQUIRE(popover->findChild<LLComboBox>("size", true) != nullptr);

        // Escaped: what was picked goes nowhere.
        LLCheckBoxCtrl* bold = popover->findChild<LLCheckBoxCtrl>("bold", true);
        UNSCOPED_INFO("and the styles");
        REQUIRE(bold != nullptr);
        bold->setValue(true);
        bold->onCommit();
        UNSCOPED_INFO("escape is taken");
        CHECK(popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        UNSCOPED_INFO("nothing was written");
        REQUIRE(said.empty());
        UNSCOPED_INFO("and nothing committed");
        CHECK(commits == 0);
        UNSCOPED_INFO("the style is what it was");
        CHECK(field->style() == std::string("NORMAL"));

        // Returned: what was picked is written, one part per attribute that
        // changed and nothing for the ones that did not.
        field->handleMouseDown(4, 10, MASK_NONE);
        popover = popoverOpen();
        UNSCOPED_INFO("opened again");
        REQUIRE(popover != nullptr);
        bold = popover->findChild<LLCheckBoxCtrl>("bold", true);
        bold->setValue(true);
        bold->onCommit();
        LLComboBox* sizes = popover->findChild<LLComboBox>("size", true);
        sizes->setValue("Large");
        sizes->onCommit();
        UNSCOPED_INFO("return is taken");
        CHECK(popover->handleKeyHere(KEY_RETURN, MASK_NONE));
        UNSCOPED_INFO("two parts written");
        REQUIRE(said.size() == 2u);
        UNSCOPED_INFO("the size");
        CHECK(said[0].part == std::string("size"));
        UNSCOPED_INFO("as picked");
        CHECK(said[0].value == std::string("Large"));
        UNSCOPED_INFO("and the style");
        CHECK(said[1].part == std::string("style"));
        UNSCOPED_INFO("as a file writes it");
        CHECK(said[1].value == std::string("BOLD"));
        UNSCOPED_INFO("the name did not change and was not written");
        CHECK(field->getValue().asString() == std::string("SansSerif"));
        UNSCOPED_INFO("and the field committed once for the answer");
        CHECK(commits == 1);
        field->die();
    }

    TEST_CASE_METHOD(alfontfield_data, "alfontfield: a field dying with its popover up escapes it and gives the keyboard back", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLLineEditor::Params lp(LLUICtrlFactory::getDefaultParams<LLLineEditor>());
        lp.name                 = "elsewhere";
        lp.rect                 = LLRect(400, 322, 500, 300);
        LLLineEditor* elsewhere = LLUICtrlFactory::create<LLLineEditor>(lp);
        gFloaterView->addChild(elsewhere);
        LLMortician::updateClass();
        ALFontField* field = make();
        field->setValue("SansSerif");
        bool written = false;
        field->onPartCommit([&written](const std::string&, const std::string&) { written = true; });
        elsewhere->setFocus(true);
        field->handleMouseDown(4, 10, MASK_NONE);
        ALPopover* popover = popoverOpen();
        {
            INFO("the popover opened, with the keyboard");
            REQUIRE(popover != nullptr);
            CHECK(gFocusMgr.getKeyboardFocus() != elsewhere);
        }
        LLCheckBoxCtrl* bold = popover->findChild<LLCheckBoxCtrl>("bold", true);
        bold->setValue(true);
        bold->onCommit();
        field->die();
        LLMortician::updateClass();
        UNSCOPED_INFO("nothing written");
        CHECK_FALSE(written);
        UNSCOPED_INFO("the keyboard back where it was");
        CHECK(gFocusMgr.getKeyboardFocus() == elsewhere);
        gFocusMgr.setKeyboardFocus(nullptr);
        elsewhere->die();
    }

    TEST_CASE_METHOD(alfontfield_data, "alfontfield: a click on a family shows it and a double-click commits it", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLMortician::updateClass();
        ALFontField* field = make();
        field->setValue("SansSerif");
        std::vector<Said> said;
        field->onPartCommit([&said](const std::string& part, const std::string& value) { said.push_back({ part, value }); });
        field->handleMouseDown(4, 10, MASK_NONE);
        ALPopover* popover = popoverOpen();
        UNSCOPED_INFO("open");
        REQUIRE(popover != nullptr);
        ALSpecimenList* fonts = popover->findChild<ALSpecimenList>("fonts", true);
        {
            INFO("the families listed");
            REQUIRE(fonts);
            REQUIRE(fonts->count() > 1);
        }
        UNSCOPED_INFO("the field's chosen");
        CHECK(fonts->chosen() == std::string("SansSerif"));
        UNSCOPED_INFO("each beside a specimen");
        REQUIRE(popover->findChild<LLTextBox>("sample", true) != nullptr);
        fonts->choose("Monospace");
        {
            INFO("a click shows it and writes nothing");
            REQUIRE(said.empty());
            CHECK(popoverOpen() == popover);
        }
        LLView* row = fonts->findChild<LLView>("row_Monospace", true);
        UNSCOPED_INFO("its row");
        REQUIRE(row != nullptr);
        row->handleDoubleClick(10, 10, MASK_NONE);
        UNSCOPED_INFO("a double-click settles");
        REQUIRE(popoverOpen() == nullptr);
        {
            INFO("and writes the name");
            REQUIRE(said.size() == 1);
            REQUIRE(said[0].part.empty());
            CHECK(said[0].value == "Monospace");
        }
        field->die();
    }

    TEST_CASE_METHOD(alfontfield_data, "alfontfield: a monospace-only field keeps the name it has and previews by label", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLMortician::updateClass();
        ALFontField::Params p(LLUICtrlFactory::getDefaultParams<ALFontField>());
        p.name     = "font";
        p.rect     = LLRect(100, 322, 300, 300);
        p.families = "monospace";
        ALFontField* field = LLUICtrlFactory::create<ALFontField>(p);
        gFloaterView->addChild(field);
        field->setValue("Inter");
        field->handleMouseDown(4, 10, MASK_NONE);
        ALPopover* popover = popoverOpen();
        UNSCOPED_INFO("open");
        REQUIRE(popover != nullptr);
        ALSpecimenList* fonts = popover->findChild<ALSpecimenList>("fonts", true);
        {
            INFO("the name it has, kept though it is not monospace");
            REQUIRE(fonts);
            REQUIRE(fonts->findChild<LLView>("row_Inter", true) != nullptr);
        }
        UNSCOPED_INFO("a monospace family listed");
        REQUIRE(fonts->findChild<LLView>("row_CascadiaCode", true) != nullptr);
        UNSCOPED_INFO("a proportional one not");
        REQUIRE(fonts->findChild<LLView>("row_DejaVu", true) == nullptr);
        fonts->choose("CascadiaCode");
        const LLTextBox* preview = popover->findChild<LLTextBox>("preview", true);
        {
            INFO("the preview says its label");
            REQUIRE(preview);
            CHECK(preview->getText().rfind("Cascadia Code", 0) == 0);
        }
        popover->escape();
        field->die();
    }

    TEST_CASE_METHOD(alfontfield_data, "alfontfield: Alt-Down and F4 open the popover as for a combo box but Down does not", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFontField* field = make();
        {
            INFO("Down alone: not taken, nothing opened");
            CHECK_FALSE(field->handleKeyHere(KEY_DOWN, MASK_NONE));
            REQUIRE_FALSE(popoverOpen());
        }
        UNSCOPED_INFO("Alt-Down");
        CHECK(field->handleKeyHere(KEY_DOWN, MASK_ALT));
        ALPopover* up = popoverOpen();
        UNSCOPED_INFO("opened");
        REQUIRE(up != nullptr);
        up->escape();
        {
            INFO("F4");
            CHECK(field->handleKeyHere(KEY_F4, MASK_NONE));
            REQUIRE(popoverOpen() != nullptr);
        }
        popoverOpen()->escape();
        field->die();
    }

    TEST_CASE_METHOD(alfontfield_data, "alfontfield: sizes show their points and go by name; a zoom's size is not offered", "[alfontfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        LLFontGL::getFontAtPoints("SansSerif", 23.f, LLFontGL::NORMAL);
        ALFontField* field = make();
        field->handleMouseDown(4, 10, MASK_NONE);
        ALPopover*  popover = popoverOpen();
        LLComboBox* sizes   = popover ? popover->findChild<LLComboBox>("size", true) : nullptr;
        UNSCOPED_INFO("the sizes");
        REQUIRE(sizes != nullptr);
        sizes->setValue("Large");
        const F32 large = LLFontGL::pointsOf("SansSerif", "Large");
        UNSCOPED_INFO("fonts.xml gives Large");
        CHECK(large > 0.f);
        UNSCOPED_INFO("said in points");
        CHECK(sizes->getSelectedItemLabel() == "Large" + llformat(" (%g pt)", large));
        UNSCOPED_INFO("chosen by name");
        CHECK(sizes->getValue().asString() == std::string("Large"));
        UNSCOPED_INFO("the zoom's size not among them");
        CHECK_FALSE(sizes->selectByValue(LLSD("23.0pt")));
        popover->escape();
        field->die();
    }
}
