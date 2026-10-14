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

#include "alxuinotes.h"

#include "../lluictrlfactory.h"

#include "alheadlessui_fixture.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alxuinotes_data
    {
        ll_test::HeadlessUI& ui = ll_test::HeadlessUI::get();
    };

    // The file is read, and a tag somebody has written a sentence for has
    // one; a tag nobody has falls back to nothing, not to a complaint.
    TEST_CASE_METHOD(alxuinotes_data, "alxuinotes: the notes file is read and only a tag written about has a note", "[alxuinotes]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const ALXUINotes& notes = ALXUINotes::get();
        CHECK(notes.count() > 100);
        CHECK_FALSE(notes.note("button").empty());
        CHECK(notes.note("no_such_tag").empty());
        UNSCOPED_INFO("and asking twice is the same file");
        CHECK(&ALXUINotes::get() == &notes);
    }

    // A correction written once, under no tag, answers for every tag; one
    // written under a tag answers only there.
    TEST_CASE_METHOD(alxuinotes_data, "alxuinotes: an untagged correction answers for every tag and a tagged one only there", "[alxuinotes]")
    {
        if (!ui.ok())
        {
            SKIP("no UI: LLUI_TEST_APP_DIR does not point at the source tree");
        }
        const ALXUINotes& notes = ALXUINotes::get();
        CHECK(notes.attributeCount() > 10);

        const ALXUINotes::Attribute* watermark = notes.attribute("line_editor", "watermark_text");
        UNSCOPED_INFO("watermark_text is a mistake under a line editor");
        REQUIRE(watermark != nullptr);
        CHECK(watermark->deprecated);
        CHECK(watermark->instead == std::string("label"));
        UNSCOPED_INFO("and the same mistake under a text editor");
        CHECK(notes.attribute("text_editor", "watermark_text") == watermark);

        const ALXUINotes::Attribute* min_width = notes.attribute("layout_panel", "min_width");
        UNSCOPED_INFO("min_width is a mistake on a layout panel");
        REQUIRE(min_width != nullptr);
        CHECK(min_width->instead == std::string("min_dim"));
        UNSCOPED_INFO("and is not one on a panel, which has no min_dim to write");
        CHECK(notes.attribute("panel", "min_width") == nullptr);

        UNSCOPED_INFO("a name nobody corrected has no line");
        CHECK(notes.attribute("button", "label") == nullptr);
    }
}
