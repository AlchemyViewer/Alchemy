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

#include "alflagsfield.h"

#include "../llcheckboxctrl.h"
#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
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

    // The form, read: each name sets its bit, the word for all sets every
    // bit, a name the list does not carry is passed over, and case is not
    // what the parsers compare.
    TEST_CASE_METHOD(alflagsfield_data, "alflagsfield: read sets a bit for each name and every bit for the word for all", "[alflagsfield]")
    {
        const std::vector<std::string> edges = { "left", "bottom", "right", "top" };
        CHECK(ALFlagsField::read("left", edges, "all") == 1u);
        CHECK(ALFlagsField::read("left|top", edges, "all") == (1u | 8u));
        UNSCOPED_INFO("the word for all");
        CHECK(ALFlagsField::read("all", edges, "all") == 15u);
        UNSCOPED_INFO("and it may be spelt how it likes");
        CHECK(ALFlagsField::read("ALL", edges, "all") == 15u);
        UNSCOPED_INFO("a word with a name in it is not the name");
        CHECK(ALFlagsField::read("lefty|top", edges, "all") == 8u);
        UNSCOPED_INFO("nor with the word for all in it");
        CHECK(ALFlagsField::read("allow", edges, "all") == 0u);
        UNSCOPED_INFO("a name may be spelt how it likes too");
        CHECK(ALFlagsField::read("Left|TOP", edges, "all") == (1u | 8u));
        UNSCOPED_INFO("a name not carried is passed over");
        CHECK(ALFlagsField::read("left|middle", edges, "all") == 1u);
        UNSCOPED_INFO("nothing is nothing");
        CHECK(ALFlagsField::read("", edges, "all") == 0u);
        UNSCOPED_INFO("and so is the word for none");
        CHECK(ALFlagsField::read("none", edges, "all") == 0u);
        UNSCOPED_INFO("no word for all leaves the word a name");
        CHECK(ALFlagsField::read("all", edges, LLStringUtil::null) == 0u);
    }

    // The form, written: the word for all where every bit is set and there
    // is one, the word for none where no bit is, and the names between bars
    // otherwise.
    TEST_CASE_METHOD(alflagsfield_data, "alflagsfield: write gives the word for all or for none or the names between bars", "[alflagsfield]")
    {
        const std::vector<std::string> edges = { "left", "bottom", "right", "top" };
        UNSCOPED_INFO("every bit is the word");
        CHECK(ALFlagsField::write(15u, edges, "all", "none") == std::string("all"));
        UNSCOPED_INFO("no bit is the other word");
        CHECK(ALFlagsField::write(0u, edges, "all", "none") == std::string("none"));
        UNSCOPED_INFO("some bits are the names");
        CHECK(ALFlagsField::write(1u | 8u, edges, "all", "none") ==
                      std::string("left|top"));
        UNSCOPED_INFO("every bit with no word for it is the names");
        CHECK(ALFlagsField::write(15u, edges, LLStringUtil::null, "none") ==
                      std::string("left|bottom|right|top"));
        UNSCOPED_INFO("a bit past the names is not a name");
        CHECK(ALFlagsField::write(16u | 1u, edges, "all", "none") ==
                      std::string("left"));
        UNSCOPED_INFO("the two agree");
        CHECK(ALFlagsField::read(ALFlagsField::write(5u, edges, "all", "none"), edges, "all") == 5u);
    }

    TEST_CASE_METHOD(alflagsfield_data, "alflagsfield: the field gives back its value as a file writes it and ticks the boxes", "[alflagsfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFlagsField* field = make();
        UNSCOPED_INFO("nothing set is the word for none");
        CHECK(field->getValue().asString() == std::string("NORMAL"));

        field->setValue("BOLD|ITALIC");
        UNSCOPED_INFO("read back as written");
        CHECK(field->getValue().asString() == std::string("BOLD|ITALIC"));
        CHECK(field->getChild<LLCheckBoxCtrl>("BOLD")->getValue().asBoolean());
        CHECK(field->getChild<LLCheckBoxCtrl>("ITALIC")->getValue().asBoolean());
        CHECK_FALSE(field->getChild<LLCheckBoxCtrl>("UNDERLINE")->getValue().asBoolean());

        field->setValue("NORMAL");
        UNSCOPED_INFO("the word for none clears them");
        CHECK(field->getValue().asString() == std::string("NORMAL"));
        UNSCOPED_INFO("and the box with it");
        CHECK_FALSE(field->getChild<LLCheckBoxCtrl>("BOLD")->getValue().asBoolean());
        delete field;
    }

    // A box ticked is the field committing what the file should now say.
    TEST_CASE_METHOD(alflagsfield_data, "alflagsfield: a box ticked commits the flags in the order the names were given", "[alflagsfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFlagsField* field = make();
        std::vector<std::string> said;
        field->setCommitCallback([&said](LLUICtrl* ctrl, const LLSD&) { said.push_back(ctrl->getValue().asString()); });

        LLCheckBoxCtrl* underline = field->getChild<LLCheckBoxCtrl>("UNDERLINE");
        underline->setValue(true);
        underline->onCommit();
        REQUIRE(said.size() == 1u);
        UNSCOPED_INFO("saying the name ticked");
        CHECK(said.back() == std::string("UNDERLINE"));

        LLCheckBoxCtrl* bold = field->getChild<LLCheckBoxCtrl>("BOLD");
        bold->setValue(true);
        bold->onCommit();
        UNSCOPED_INFO("in the order the names were given, not the order they were ticked");
        CHECK(said.back() == std::string("BOLD|UNDERLINE"));

        underline->setValue(false);
        underline->onCommit();
        UNSCOPED_INFO("and unticked goes out again");
        CHECK(said.back() == std::string("BOLD"));
        delete field;
    }

    TEST_CASE_METHOD(alflagsfield_data, "alflagsfield: the boxes share the width and share it again when it grows", "[alflagsfield]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALFlagsField* field = make(300);
        LLCheckBoxCtrl* last = field->getChild<LLCheckBoxCtrl>("UNDERLINE");
        UNSCOPED_INFO("a third each");
        CHECK(last->getRect().mLeft == 200);
        UNSCOPED_INFO("to the end");
        CHECK(last->getRect().mRight == 300);

        field->reshape(600, 20);
        UNSCOPED_INFO("a third of the new width");
        CHECK(last->getRect().mLeft == 400);
        UNSCOPED_INFO("to the new end");
        CHECK(last->getRect().mRight == 600);
        delete field;
    }
}
