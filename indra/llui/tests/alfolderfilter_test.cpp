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

#include "../test/lltut.h"

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

namespace tut
{
    struct alfolderfilter_data
    {
    };
    typedef test_group<alfolderfilter_data> alfolderfilter_group;
    typedef alfolderfilter_group::object    alfolderfilter_object;
    alfolderfilter_group                    alfolderfilter_test_group("alfolderfilter");

    template<> template<>
    void alfolderfilter_object::test<1>()
    {
        set_test_name("the words lowercased, whatever script they are in; a change a new generation, the same words none");
        TestFilter filter;
        ensure("nothing asked: not active, the default", !filter.isActive() && filter.isDefault() && !filter.isNotDefault());
        const S32 first = filter.getCurrentGeneration();
        filter.setWords("Panel ÄRGER");
        ensure_equals("lowercased", filter.words(), std::string("panel ärger"));
        ensure("a new generation, every one of them", filter.getCurrentGeneration() == first + 1
                                                          && filter.getFirstRequiredGeneration() == first + 1
                                                          && filter.getFirstSuccessGeneration() == first + 1);
        ensure("modified", filter.isModified());
        ensure("active", filter.isActive() && filter.isNotDefault());
        filter.clearModified();
        filter.setWords("PANEL ärger");
        ensure("the same words: nothing new", filter.getCurrentGeneration() == first + 1 && !filter.isModified());
        ensure_equals("said as it is", filter.getFilterText(), std::string("panel ärger"));
        ensure_equals("named", filter.getName(), std::string("test"));
    }

    template<> template<>
    void alfolderfilter_object::test<2>()
    {
        set_test_name("where the words are in a label: its bytes as they are, found again past a letter lowercasing changes the length of");
        LLFolderViewFilter::Match plain = ALFolderFilter::spanIn("close_btn", 6, "btn");
        ensure("in plain letters, where found", plain.mOffset == 6 && plain.mLength == 3);
        // The Kelvin sign is three bytes and lowercases to a k of one: the
        // words after it are two bytes further on in the label than in its
        // lowercased copy.
        const std::string label = "\xe2\x84\xaa panel";
        const std::string lower = utf8str_tolower(label);
        ensure("lowercasing moves the bytes", lower.find("panel") != label.find("panel"));
        const LLFolderViewFilter::Match past = ALFolderFilter::spanIn(label, lower.find("panel"), "panel");
        ensure_equals("past it, in the label's bytes", label.substr(past.mOffset, past.mLength), std::string("panel"));
        const LLFolderViewFilter::Match on = ALFolderFilter::spanIn(label, lower.find("k p"), "k p");
        ensure_equals("over it, the letter as it stands", label.substr(on.mOffset, on.mLength), std::string("\xe2\x84\xaa p"));
        ensure("not found: nothing lit", ALFolderFilter::spanIn(label, std::string::npos, "x").mLength == 0);
        ensure("no words: nothing lit", ALFolderFilter::spanIn(label, 0, "").mLength == 0);
    }
}
