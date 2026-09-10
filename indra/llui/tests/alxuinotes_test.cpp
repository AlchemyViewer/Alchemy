/**
 * @file alxuinotes_test.cpp
 * @brief What a person knows about a tag, read from the skin file that says it.
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

#include "../alxuinotes.h"

#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include "../test/lltut.h"

class LLAvatarName;
const std::string gXUINotesTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gXUINotesTestAnonName;
}

namespace tut
{
    struct alxuinotes_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
    };

    typedef test_group<alxuinotes_data> alxuinotes_test;
    typedef alxuinotes_test::object     alxuinotes_object;
    tut::alxuinotes_test alxuinotes_testgroup("alxuinotes");

    // The file is read, and a tag somebody has written a sentence for has
    // one; a tag nobody has falls back to nothing, not to a complaint.
    template<> template<>
    void alxuinotes_object::test<1>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const ALXUINotes& notes = ALXUINotes::get();
        ensure("the file was read: " + std::to_string(notes.count()), notes.count() > 100);
        ensure("a button has a sentence", !notes.note("button").empty());
        ensure("a tag nobody wrote about has none", notes.note("no_such_tag").empty());
        ensure("and asking twice is the same file",
               &ALXUINotes::get() == &notes);
    }

    // A correction written once, under no tag, answers for every tag; one
    // written under a tag answers only there.
    template<> template<>
    void alxuinotes_object::test<2>()
    {
        if (!ui.ok())
        {
            skip("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const ALXUINotes& notes = ALXUINotes::get();
        ensure("there are corrections: " + std::to_string(notes.attributeCount()), notes.attributeCount() > 10);

        const ALXUINotes::Attribute* watermark = notes.attribute("line_editor", "watermark_text");
        ensure("watermark_text is a mistake under a line editor", watermark != nullptr);
        ensure("and is deprecated", watermark->deprecated);
        ensure_equals("for label", watermark->instead, std::string("label"));
        ensure("and the same mistake under a text editor",
               notes.attribute("text_editor", "watermark_text") == watermark);

        const ALXUINotes::Attribute* min_width = notes.attribute("layout_panel", "min_width");
        ensure("min_width is a mistake on a layout panel", min_width != nullptr);
        ensure_equals("written min_dim instead", min_width->instead, std::string("min_dim"));
        ensure("and is not one on a panel, which has no min_dim to write",
               notes.attribute("panel", "min_width") == nullptr);

        ensure("a name nobody corrected has no line",
               notes.attribute("button", "label") == nullptr);
    }
}
