/**
 * @file alxuifields_test.cpp
 * @brief A XUI attribute's section in the attribute grid, and the words it
 * may take.
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

#include "alxuifields.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

namespace
{
    struct alxuifields_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();

        static ALPropertyGrid::Field field(const std::string& name)
        {
            ALPropertyGrid::Field out;
            out.name = name;
            return out;
        }
    };

    TEST_CASE_METHOD(alxuifields_data, "alxuifields: sectionOf files a name by its words and a leaf under its block's", "[alxuifields]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        UNSCOPED_INFO("a name names");
        CHECK(ALXUIFields::sectionOf("name") == (S32)ALXUIFields::IDENTITY);
        UNSCOPED_INFO("a tool tip too");
        CHECK(ALXUIFields::sectionOf("tool_tip") == (S32)ALXUIFields::IDENTITY);
        UNSCOPED_INFO("an edge is where");
        CHECK(ALXUIFields::sectionOf("left") == (S32)ALXUIFields::GEOMETRY);
        UNSCOPED_INFO("a rect's leaf is where its rect is");
        CHECK(ALXUIFields::sectionOf("rect.left") == (S32)ALXUIFields::GEOMETRY);
        UNSCOPED_INFO("a padding is where, by its words");
        CHECK(ALXUIFields::sectionOf("pad_bottom") == (S32)ALXUIFields::GEOMETRY);
        UNSCOPED_INFO("a colour's leaf is a colour");
        CHECK(ALXUIFields::sectionOf("bg_alpha_color.alpha") == (S32)ALXUIFields::APPEARANCE);
        UNSCOPED_INFO("a callback is what it does");
        CHECK(ALXUIFields::sectionOf("commit_callback.function") == (S32)ALXUIFields::BEHAVIOUR);
        UNSCOPED_INFO("a name nothing recognises is left for last");
        CHECK(ALXUIFields::sectionOf("wibble") == (S32)ALXUIFields::OTHER);
    }

    TEST_CASE_METHOD(alxuifields_data, "alxuifields: the notes' section for a name wins over the guess from its words", "[alxuifields]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // "chrome" is on the list of names that say what a thing does; the
        // notes say it is how a thing looks, and they are asked first.
        UNSCOPED_INFO("the notes' section");
        CHECK(ALXUIFields::sectionOf("chrome") == (S32)ALXUIFields::APPEARANCE);
        UNSCOPED_INFO("and one the notes agree with stands");
        CHECK(ALXUIFields::sectionOf("follows") == (S32)ALXUIFields::GEOMETRY);
    }

    TEST_CASE_METHOD(alxuifields_data, "alxuifields: vocabularyFor gives a field the viewer's words and leaves others alone", "[alxuifields]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid::Field font = field("font");
        ALXUIFields::vocabularyFor(font);
        UNSCOPED_INFO("the fonts fonts.xml declares");
        CHECK_FALSE(font.values.empty());
        UNSCOPED_INFO("the sans among them");
        CHECK(std::find(font.values.begin(), font.values.end(), "SansSerif") != font.values.end());

        ALPropertyGrid::Field size = field("font.size");
        ALXUIFields::vocabularyFor(size);
        UNSCOPED_INFO("and their sizes");
        CHECK_FALSE(size.values.empty());

        ALPropertyGrid::Field style = field("font.style");
        ALXUIFields::vocabularyFor(style);
        UNSCOPED_INFO("a style is flags");
        CHECK(style.flags);
        UNSCOPED_INFO("with a word for none of them");
        CHECK(style.noneWord == std::string("NORMAL"));

        ALPropertyGrid::Field follows = field("follows");
        ALXUIFields::vocabularyFor(follows);
        UNSCOPED_INFO("four edges");
        CHECK(follows.values.size() == (size_t)4);
        UNSCOPED_INFO("drawn in the picture's order");
        CHECK(follows.edges.size() == (size_t)4);
        UNSCOPED_INFO("a word for all");
        CHECK(follows.allWord == std::string("all"));
        UNSCOPED_INFO("and for none");
        CHECK(follows.noneWord == std::string("none"));

        ALPropertyGrid::Field held = field("layout");
        held.values = { "mine" };
        ALXUIFields::vocabularyFor(held);
        UNSCOPED_INFO("a field with its values keeps them");
        CHECK(held.values.size() == (size_t)1);

        ALPropertyGrid::Field other = field("label");
        ALXUIFields::vocabularyFor(other);
        UNSCOPED_INFO("a field the viewer holds no words for gets none");
        CHECK(other.values.empty());
    }
}
