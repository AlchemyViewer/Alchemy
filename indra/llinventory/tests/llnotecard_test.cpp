/**
 * @file llnotecard_test.cpp
 * @brief A notecard written by LLNotecard reads back with the items and text it was written with.
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

#include "../llinventory.h"
#include "../llnotecard.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

namespace
{
    LLPointer<LLInventoryItem> make_item(const char* item_id, const char* name)
    {
        LLPermissions perm;
        perm.init(LLUUID("8c0b5e4e-27a4-4b1d-9a0e-1f6a2d3c4b5a"), LLUUID("1d2c3b4a-5e6f-4a7b-8c9d-0e1f2a3b4c5d"),
                  LLUUID::null, LLUUID::null);
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_COPY | PERM_TRANSFER);
        return new LLInventoryItem(LLUUID(item_id), LLUUID("5a4b3c2d-1e0f-4a9b-8c7d-6e5f4a3b2c1d"), perm,
                                   LLUUID("0f1e2d3c-4b5a-4968-8776-a5b4c3d2e1f0"), LLAssetType::AT_LANDMARK,
                                   LLInventoryType::IT_LANDMARK, std::string(name), std::string("For the notecard"),
                                   LLSaleInfo::DEFAULT, 0, 1700000000);
    }

    std::string export_notecard(const std::vector<LLPointer<LLInventoryItem>>& items, const std::string& text)
    {
        LLNotecard notecard;
        notecard.setItems(items);
        notecard.setText(text);
        std::ostringstream out;
        REQUIRE(notecard.exportStream(out));
        return out.str();
    }

    TEST_CASE("llnotecard: a notecard with embedded items reads back with them and its text", "[llnotecard]")
    {
        const std::string text   = "See the landmarks.";
        const std::string buffer = export_notecard({ make_item("aaaaaaaa-1111-4111-8111-111111111111", "First"),
                                                     make_item("bbbbbbbb-2222-4222-8222-222222222222", "Second") },
                                                   text);
        CHECK(buffer.find("count 2\n") != std::string::npos);

        LLNotecard read;
        std::istringstream in(buffer);
        REQUIRE(read.importStream(in));
        REQUIRE(read.getItems().size() == 2);
        CHECK(read.getItems()[0]->getUUID() == LLUUID("aaaaaaaa-1111-4111-8111-111111111111"));
        CHECK(read.getItems()[1]->getUUID() == LLUUID("bbbbbbbb-2222-4222-8222-222222222222"));
        CHECK(read.getItems()[1]->getName() == "Second");
        CHECK(read.getText() == text);
    }

    TEST_CASE("llnotecard: a null embedded item is left out of the count as it is of the items", "[llnotecard]")
    {
        const std::string text   = "Two landmarks and a gap.";
        const std::string buffer = export_notecard({ make_item("aaaaaaaa-1111-4111-8111-111111111111", "First"),
                                                     LLPointer<LLInventoryItem>(),
                                                     make_item("bbbbbbbb-2222-4222-8222-222222222222", "Second") },
                                                   text);
        CHECK(buffer.find("count 2\n") != std::string::npos);

        LLNotecard read;
        std::istringstream in(buffer);
        REQUIRE(read.importStream(in));
        REQUIRE(read.getItems().size() == 2);
        CHECK(read.getItems()[0]->getUUID() == LLUUID("aaaaaaaa-1111-4111-8111-111111111111"));
        CHECK(read.getItems()[1]->getUUID() == LLUUID("bbbbbbbb-2222-4222-8222-222222222222"));
        CHECK(read.getText() == text);
    }

    TEST_CASE("llnotecard: a notecard whose items are all null is written with none", "[llnotecard]")
    {
        const std::string buffer =
            export_notecard({ LLPointer<LLInventoryItem>(), LLPointer<LLInventoryItem>() }, "No items.");
        CHECK(buffer.find("count 0\n") != std::string::npos);

        LLNotecard read;
        std::istringstream in(buffer);
        REQUIRE(read.importStream(in));
        CHECK(read.getItems().empty());
        CHECK(read.getText() == "No items.");
    }
}
