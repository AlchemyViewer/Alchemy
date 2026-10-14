/**
 * @file alfolderfilter_test.cpp
 * @brief A folder tree's filter: its words, where they are in a label, and its generations.
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

#include "alfolderfilter.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    // As a tree of ours has one: whatever it asks of a row.
    class TestFilter final : public ALFolderFilter
    {
    public:
        TestFilter() : ALFolderFilter("test") {}
        bool check(const LLFolderViewModelItem* item) override { return true; }
    };
}

namespace
{
    TEST_CASE("alfolderfilter: setWords lowercases any script and only new words start a generation", "[alfolderfilter]")
    {
        TestFilter filter;
        {
            INFO("nothing asked: not active, the default");
            CHECK_FALSE(filter.isActive());
            CHECK(filter.isDefault());
            CHECK_FALSE(filter.isNotDefault());
        }
        const S32 first = filter.getCurrentGeneration();
        filter.setWords("Panel ÄRGER");
        UNSCOPED_INFO("lowercased");
        CHECK(filter.words() == std::string("panel ärger"));
        {
            INFO("a new generation, every one of them");
            CHECK(filter.getCurrentGeneration() == first + 1);
            CHECK(filter.getFirstRequiredGeneration() == first + 1);
            CHECK(filter.getFirstSuccessGeneration() == first + 1);
        }
        CHECK(filter.isModified());
        CHECK(filter.isActive());
        CHECK(filter.isNotDefault());
        filter.clearModified();
        filter.setWords("PANEL ärger");
        {
            INFO("the same words: nothing new");
            CHECK(filter.getCurrentGeneration() == first + 1);
            CHECK_FALSE(filter.isModified());
        }
        CHECK(filter.getFilterText() == std::string("panel ärger"));
        CHECK(filter.getName() == std::string("test"));
    }

    TEST_CASE("alfolderfilter: spanIn gives the label's own bytes past a letter lowercasing shrinks", "[alfolderfilter]")
    {
        LLFolderViewFilter::Match plain = ALFolderFilter::spanIn("close_btn", 6, "btn");
        {
            INFO("in plain letters, where found");
            CHECK(plain.mOffset == 6);
            CHECK(plain.mLength == 3);
        }
        // The Kelvin sign is three bytes and lowercases to a k of one: the
        // words after it are two bytes further on in the label than in its
        // lowercased copy.
        const std::string label = "\xe2\x84\xaa panel";
        const std::string lower = utf8str_tolower(label);
        UNSCOPED_INFO("lowercasing moves the bytes");
        CHECK(lower.find("panel") != label.find("panel"));
        const LLFolderViewFilter::Match past = ALFolderFilter::spanIn(label, lower.find("panel"), "panel");
        UNSCOPED_INFO("past it, in the label's bytes");
        CHECK(label.substr(past.mOffset, past.mLength) == std::string("panel"));
        const LLFolderViewFilter::Match on = ALFolderFilter::spanIn(label, lower.find("k p"), "k p");
        UNSCOPED_INFO("over it, the letter as it stands");
        CHECK(label.substr(on.mOffset, on.mLength) == std::string("\xe2\x84\xaa p"));
        UNSCOPED_INFO("not found: nothing lit");
        CHECK(ALFolderFilter::spanIn(label, std::string::npos, "x").mLength == 0);
        UNSCOPED_INFO("no words: nothing lit");
        CHECK(ALFolderFilter::spanIn(label, 0, "").mLength == 0);
    }
}
