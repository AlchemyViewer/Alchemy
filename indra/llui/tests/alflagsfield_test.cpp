/**
 * @file alflagsfield_test.cpp
 * @brief A set of flags as a XUI file writes one: names with bars between them.
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

#include "../alflagsfield.h"

#include "../llcheckboxctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gFlagsFieldTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gFlagsFieldTestAnonName;
}

namespace tut
{
    struct alflagsfield_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static const std::vector<std::string>& styles()
        {
            static const std::vector<std::string> names = { "BOLD", "ITALIC", "UNDERLINE" };
            return names;
        }

        static ALFlagsField* make(S32 width = 300)
        {
            ALFlagsField::Params p(LLUICtrlFactory::getDefaultParams<ALFlagsField>());
            p.name = "style";
            p.rect = LLRect(0, 20, width, 0);
            ALFlagsField* field = LLUICtrlFactory::create<ALFlagsField>(p);
            field->setFlags(styles(), LLStringUtil::null, "NORMAL");
            return field;
        }
    };

    typedef test_group<alflagsfield_data> alflagsfield_test;
    typedef alflagsfield_test::object     alflagsfield_object;
    tut::alflagsfield_test alflagsfield_testgroup("alflagsfield");

    // The form, read: each name sets its bit, the word for all sets every
    // bit, a name the list does not carry is passed over, and case is not
    // what the parsers compare.
    template<> template<>
    void alflagsfield_object::test<1>()
    {
        const std::vector<std::string> edges = { "left", "bottom", "right", "top" };
        ensure_equals("one name", ALFlagsField::read("left", edges, "all"), 1u);
        ensure_equals("two with a bar", ALFlagsField::read("left|top", edges, "all"), 1u | 8u);
        ensure_equals("the word for all", ALFlagsField::read("all", edges, "all"), 15u);
        ensure_equals("and it may be spelt how it likes", ALFlagsField::read("ALL", edges, "all"), 15u);
        ensure_equals("as may a name", ALFlagsField::read("Left|TOP", edges, "all"), 1u | 8u);
        ensure_equals("a name not carried is passed over", ALFlagsField::read("left|middle", edges, "all"), 1u);
        ensure_equals("nothing is nothing", ALFlagsField::read("", edges, "all"), 0u);
        ensure_equals("and so is the word for none", ALFlagsField::read("none", edges, "all"), 0u);
        ensure_equals("no word for all leaves the word a name",
                      ALFlagsField::read("all", edges, LLStringUtil::null), 0u);
    }

    // The form, written: the word for all where every bit is set and there
    // is one, the word for none where no bit is, and the names between bars
    // otherwise.
    template<> template<>
    void alflagsfield_object::test<2>()
    {
        const std::vector<std::string> edges = { "left", "bottom", "right", "top" };
        ensure_equals("every bit is the word", ALFlagsField::write(15u, edges, "all", "none"), std::string("all"));
        ensure_equals("no bit is the other word", ALFlagsField::write(0u, edges, "all", "none"), std::string("none"));
        ensure_equals("some bits are the names", ALFlagsField::write(1u | 8u, edges, "all", "none"),
                      std::string("left|top"));
        ensure_equals("every bit with no word for it is the names",
                      ALFlagsField::write(15u, edges, LLStringUtil::null, "none"),
                      std::string("left|bottom|right|top"));
        ensure_equals("a bit past the names is not a name", ALFlagsField::write(16u | 1u, edges, "all", "none"),
                      std::string("left"));
        ensure("the two agree", ALFlagsField::read(ALFlagsField::write(5u, edges, "all", "none"), edges, "all") == 5u);
    }

    // The field: what it is given comes back the way a file writes it, and
    // the boxes say the same.
    template<> template<>
    void alflagsfield_object::test<3>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFlagsField* field = make();
        ensure_equals("nothing set is the word for none", field->getValue().asString(), std::string("NORMAL"));

        field->setValue("BOLD|ITALIC");
        ensure_equals("read back as written", field->getValue().asString(), std::string("BOLD|ITALIC"));
        ensure("bold is ticked", field->getChild<LLCheckBoxCtrl>("BOLD")->getValue().asBoolean());
        ensure("italic is ticked", field->getChild<LLCheckBoxCtrl>("ITALIC")->getValue().asBoolean());
        ensure("underline is not", !field->getChild<LLCheckBoxCtrl>("UNDERLINE")->getValue().asBoolean());

        field->setValue("NORMAL");
        ensure_equals("the word for none clears them", field->getValue().asString(), std::string("NORMAL"));
        ensure("and the box with it", !field->getChild<LLCheckBoxCtrl>("BOLD")->getValue().asBoolean());
        delete field;
    }

    // A box ticked is the field committing what the file should now say.
    template<> template<>
    void alflagsfield_object::test<4>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFlagsField* field = make();
        std::vector<std::string> said;
        field->setCommitCallback([&said](LLUICtrl* ctrl, const LLSD&) { said.push_back(ctrl->getValue().asString()); });

        LLCheckBoxCtrl* underline = field->getChild<LLCheckBoxCtrl>("UNDERLINE");
        underline->setValue(true);
        underline->onCommit();
        ensure_equals("one commit", said.size(), 1u);
        ensure_equals("saying the name ticked", said.back(), std::string("UNDERLINE"));

        LLCheckBoxCtrl* bold = field->getChild<LLCheckBoxCtrl>("BOLD");
        bold->setValue(true);
        bold->onCommit();
        ensure_equals("in the order the names were given, not the order they were ticked",
                      said.back(), std::string("BOLD|UNDERLINE"));

        underline->setValue(false);
        underline->onCommit();
        ensure_equals("and unticked goes out again", said.back(), std::string("BOLD"));
        delete field;
    }

    // The boxes share the width, and go on sharing it when there is more.
    template<> template<>
    void alflagsfield_object::test<5>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFlagsField* field = make(300);
        LLCheckBoxCtrl* last = field->getChild<LLCheckBoxCtrl>("UNDERLINE");
        ensure_equals("a third each", last->getRect().mLeft, 200);
        ensure_equals("to the end", last->getRect().mRight, 300);

        field->reshape(600, 20);
        ensure_equals("a third of the new width", last->getRect().mLeft, 400);
        ensure_equals("to the new end", last->getRect().mRight, 600);
        delete field;
    }
}
