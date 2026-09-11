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

#include "../alfontfield.h"

#include "../alpopover.h"
#include "../llcheckboxctrl.h"
#include "../llcombobox.h"
#include "../lllineeditor.h"
#include "../lluictrlfactory.h"

#include "llfontgl.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gFontFieldTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFontFieldTestAnonName;
}

namespace tut
{
    struct alfontfield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

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

    typedef test_group<alfontfield_data> alfontfield_test;
    typedef alfontfield_test::object     alfontfield_object;
    tut::alfontfield_test alfontfield_testgroup("alfontfield");

    // The value is the name, and the other two are their own; the specimen
    // is drawn in the face the three name, found when they change and not
    // again.
    template<> template<>
    void alfontfield_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFontField* field = make();
        ensure("a field given nothing draws in a face all the same", field->font() != nullptr);

        field->setValue("SansSerif");
        ensure_equals("the value is the name", field->getValue().asString(), std::string("SansSerif"));
        ensure_equals("and the line says it", field->getChild<LLLineEditor>("text")->getText(), std::string("SansSerif"));
        const LLFontGL* plain = field->font();

        field->setStyle("BOLD");
        ensure_equals("style is its own", field->style(), std::string("BOLD"));
        ensure_equals("and not the value", field->getValue().asString(), std::string("SansSerif"));
        ensure("a style is a different face", field->font() != plain);
        ensure("and the bold one", (field->font()->getFontDesc().getStyle() & LLFontGL::BOLD) != 0);

        field->setSize("Large");
        ensure_equals("size is its own", field->size(), std::string("Large"));

        field->setValue("NoSuchFace");
        ensure("a name nobody declared still draws in something", field->font() != nullptr);
        field->die();
    }

    // Typing a name in the line commits it, and says which part changed.
    template<> template<>
    void alfontfield_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure_equals("the field committed", commits, 1);
        ensure_equals("one part changed", said.size(), 1u);
        ensure("the name, which is the part with no name", said.front().part.empty());
        ensure_equals("to what was typed", said.front().value, std::string("Monospace"));
        ensure_equals("and the value says so", field->getValue().asString(), std::string("Monospace"));

        line->onCommit();
        ensure_equals("committing the same name again is a commit", commits, 2);
        ensure_equals("but not a change", said.size(), 1u);
        field->die();
    }

    // The popover: escaped, nothing is written; closed any other way, only
    // the parts that changed are, each as its own attribute.
    template<> template<>
    void alfontfield_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
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
        ensure("the popover opened", popover != nullptr);
        ensure("with the sizes in it", popover->findChild<LLComboBox>("size", true) != nullptr);

        // Escaped: what was picked goes nowhere.
        LLCheckBoxCtrl* bold = popover->findChild<LLCheckBoxCtrl>("bold", true);
        ensure("and the styles", bold != nullptr);
        bold->setValue(true);
        bold->onCommit();
        ensure("escape is taken", popover->handleKeyHere(KEY_ESCAPE, MASK_NONE));
        ensure("nothing was written", said.empty());
        ensure_equals("and nothing committed", commits, 0);
        ensure_equals("the style is what it was", field->style(), std::string("NORMAL"));

        // Returned: what was picked is written, one part per attribute that
        // changed and nothing for the ones that did not.
        field->handleMouseDown(4, 10, MASK_NONE);
        popover = popoverOpen();
        ensure("opened again", popover != nullptr);
        bold = popover->findChild<LLCheckBoxCtrl>("bold", true);
        bold->setValue(true);
        bold->onCommit();
        LLComboBox* sizes = popover->findChild<LLComboBox>("size", true);
        sizes->setValue("Large");
        sizes->onCommit();
        ensure("return is taken", popover->handleKeyHere(KEY_RETURN, MASK_NONE));
        ensure_equals("two parts written", said.size(), 2u);
        ensure_equals("the size", said[0].part, std::string("size"));
        ensure_equals("as picked", said[0].value, std::string("Large"));
        ensure_equals("and the style", said[1].part, std::string("style"));
        ensure_equals("as a file writes it", said[1].value, std::string("BOLD"));
        ensure_equals("the name did not change and was not written",
                      field->getValue().asString(), std::string("SansSerif"));
        ensure_equals("and the field committed once for the answer", commits, 1);
        field->die();
    }
}
