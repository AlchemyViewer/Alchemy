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

#include "../alxuifields.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

#include <algorithm>

class LLAvatarName;
const std::string gXUIFieldsTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gXUIFieldsTestAnonName;
}

namespace tut
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

    typedef test_group<alxuifields_data> alxuifields_test;
    typedef alxuifields_test::object     alxuifields_object;
    tut::alxuifields_test alxuifields_testgroup("alxuifields");

    template<> template<>
    void alxuifields_object::test<1>()
    {
        set_test_name("a name goes under the section its words say, a nested leaf under its block's");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ensure_equals("a name names", ALXUIFields::sectionOf("name"), (S32)ALXUIFields::IDENTITY);
        ensure_equals("a tool tip too", ALXUIFields::sectionOf("tool_tip"), (S32)ALXUIFields::IDENTITY);
        ensure_equals("an edge is where", ALXUIFields::sectionOf("left"), (S32)ALXUIFields::GEOMETRY);
        ensure_equals("a rect's leaf is where its rect is", ALXUIFields::sectionOf("rect.left"), (S32)ALXUIFields::GEOMETRY);
        ensure_equals("a padding is where, by its words", ALXUIFields::sectionOf("pad_bottom"), (S32)ALXUIFields::GEOMETRY);
        ensure_equals("a colour's leaf is a colour", ALXUIFields::sectionOf("bg_alpha_color.alpha"), (S32)ALXUIFields::APPEARANCE);
        ensure_equals("a callback is what it does", ALXUIFields::sectionOf("commit_callback.function"), (S32)ALXUIFields::BEHAVIOUR);
        ensure_equals("a name nothing recognises is left for last", ALXUIFields::sectionOf("wibble"), (S32)ALXUIFields::OTHER);
    }

    template<> template<>
    void alxuifields_object::test<2>()
    {
        set_test_name("where the notes put a name, the guess from its words gives way");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        // "chrome" is on the list of names that say what a thing does; the
        // notes say it is how a thing looks, and they are asked first.
        ensure_equals("the notes' section", ALXUIFields::sectionOf("chrome"), (S32)ALXUIFields::APPEARANCE);
        ensure_equals("and one the notes agree with stands", ALXUIFields::sectionOf("follows"), (S32)ALXUIFields::GEOMETRY);
    }

    template<> template<>
    void alxuifields_object::test<3>()
    {
        set_test_name("a field whose words the viewer holds is given them, and no other field is touched");
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        ALPropertyGrid::Field font = field("font");
        ALXUIFields::vocabularyFor(font);
        ensure("the fonts fonts.xml declares", !font.values.empty());
        ensure("the sans among them",
               std::find(font.values.begin(), font.values.end(), "SansSerif") != font.values.end());

        ALPropertyGrid::Field size = field("font.size");
        ALXUIFields::vocabularyFor(size);
        ensure("and their sizes", !size.values.empty());

        ALPropertyGrid::Field style = field("font.style");
        ALXUIFields::vocabularyFor(style);
        ensure("a style is flags", style.flags);
        ensure_equals("with a word for none of them", style.noneWord, std::string("NORMAL"));

        ALPropertyGrid::Field follows = field("follows");
        ALXUIFields::vocabularyFor(follows);
        ensure_equals("four edges", follows.values.size(), (size_t)4);
        ensure_equals("drawn in the picture's order", follows.edges.size(), (size_t)4);
        ensure_equals("a word for all", follows.allWord, std::string("all"));
        ensure_equals("and for none", follows.noneWord, std::string("none"));

        ALPropertyGrid::Field held = field("layout");
        held.values = { "mine" };
        ALXUIFields::vocabularyFor(held);
        ensure_equals("a field with its values keeps them", held.values.size(), (size_t)1);

        ALPropertyGrid::Field other = field("label");
        ALXUIFields::vocabularyFor(other);
        ensure("a field the viewer holds no words for gets none", other.values.empty());
    }
}
