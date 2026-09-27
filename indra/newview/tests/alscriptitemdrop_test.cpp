/**
 * @file alscriptitemdrop_test.cpp
 * @brief An inventory item dropped on a script: its name or its key, over the studio's own window.
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

#include "../alscriptitemdrop.h"

#include "alcodeeditor.h"
#include "llfontgl.h"
#include "llinventory.h"
#include "llpermissions.h"

#include "alscriptstudio_fixture.h"

#include "../test/lltut.h"

#include <string>

namespace tut
{
    struct alscriptitemdrop_data
    {
        al_studio_test::StudioWindow window;
        al_studio_test::FakeServices services{ window.floater };
        ALScriptStudioDoc*           doc = nullptr;
        // The key the viewer would show, or null.
        LLUUID                       shown;

        // A script's tab over an editor of its own, loaded, holding `text`.
        void make(const std::string& text)
        {
            if (!window.floater)
            {
                skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
            }
            LLUUID object, item;
            object.generate();
            item.generate();
            doc             = &services.addDoc("script", ALScriptRef(object, item), "Script");
            doc->loaded     = true;
            doc->modifiable = true;
            ALCodeEditor::Params p(LLUICtrlFactory::getDefaultParams<ALCodeEditor>());
            p.name      = "editor";
            p.rect      = LLRect(0, 200, 400, 0);
            p.syntax    = "lsl";
            doc->editor = LLUICtrlFactory::create<ALCodeEditor>(p);
            doc->editor->setFont(LLFontGL::getFontMonospace());
            window.floater->addChild(doc->editor);
            doc->editor->setText(text);
        }

        static LLPointer<LLInventoryItem> item(const std::string& name)
        {
            LLPointer<LLInventoryItem> one = new LLInventoryItem();
            LLUUID                     id;
            id.generate();
            one->setUUID(id);
            one->rename(name);
            one->setType(LLAssetType::AT_TEXTURE);
            one->setInventoryType(LLInventoryType::IT_TEXTURE);
            return one;
        }

        // A drop, or a drag over, at the text's start.
        EAcceptance drop(const LLPointer<LLInventoryItem>& what, bool dropping, MASK mask = MASK_NONE, std::string* tip = nullptr,
                         EDragAndDropType type = DAD_TEXTURE, bool* taken = nullptr)
        {
            EAcceptance accept = ACCEPT_NO;
            std::string tooltip;
            const LLRect text  = doc->editor->textRect();
            const bool   took  = ALScriptItemDrop::drop(*doc, services, [this](const LLInventoryItem&) { return shown; }, text.mLeft + 1,
                                                        text.mTop - 2, mask, dropping, type, what.get(), &accept, tooltip);
            if (tip)
            {
                *tip = tooltip;
            }
            if (taken)
            {
                *taken = took;
            }
            return accept;
        }
    };
    typedef test_group<alscriptitemdrop_data> alscriptitemdrop_group;
    typedef alscriptitemdrop_group::object    alscriptitemdrop_object;
    alscriptitemdrop_group                    alscriptitemdrop_instance("ALScriptItemDrop");

    template<> template<>
    void alscriptitemdrop_object::test<1>()
    {
        set_test_name("its name goes in where it lands, as a string, its quotes escaped; with Shift its key, where it may be seen");
        ensure_equals("quoted and escaped", ALScriptItemDrop::literal("a \"b\" \\ c"), std::string("\"a \\\"b\\\" \\\\ c\""));
        make("x;");
        const auto wood = item("Wood \"oak\"");
        std::string tip;
        ensure("offered", drop(wood, false, MASK_NONE, &tip) == ACCEPT_YES_SINGLE);
        ensure("the tip says what goes in", tip.find("\"Wood \\\"oak\\\"\"") != std::string::npos);
        ensure("nothing yet", doc->editor->text() == "x;");
        drop(wood, true);
        ensure_equals("in where it lands", doc->editor->text(), std::string("\"Wood \\\"oak\\\"\"x;"));
        ensure("the caret after it", doc->editor->caret() == ALTextPos(0, 14));

        doc->editor->setText("x;");
        ensure("its key not to be seen: refused", drop(wood, false, MASK_SHIFT, &tip) == ACCEPT_NO && !tip.empty());
        shown.generate();
        drop(wood, true, MASK_SHIFT);
        ensure_equals("its key", doc->editor->text(), "\"" + shown.asString() + "\"x;");
    }

    template<> template<>
    void alscriptitemdrop_object::test<2>()
    {
        set_test_name("a script that may not be changed refuses it; a folder, and a notecard, are left to someone else");
        make("x;");
        const auto wood = item("Wood");
        doc->modifiable = false;
        std::string tip;
        ensure("refused, and said why", drop(wood, true, MASK_NONE, &tip) == ACCEPT_NO && !tip.empty() && doc->editor->text() == "x;");
        doc->modifiable = true;
        bool taken = true;
        drop(wood, false, MASK_NONE, nullptr, DAD_CATEGORY, &taken);
        ensure("a folder: not taken", !taken);
        doc->notecard = true;
        drop(wood, false, MASK_NONE, nullptr, DAD_TEXTURE, &taken);
        ensure("a notecard: not taken", !taken);
    }
}
