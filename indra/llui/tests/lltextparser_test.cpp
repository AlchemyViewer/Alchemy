/**
 * @file lltextparser_test.cpp
 * @brief Chat highlights: a line split at the keywords it holds, and a line
 *        highlighted whole.
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

#include "../lltextparser.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct lltextparser_data
    {
        LLTextParser& parser = LLTextParser::instance();

        lltextparser_data()
        {
            // The parser is the process's; each test starts it empty.
            std::vector<LLUUID> ids;
            for (const LLHighlightEntry& entry : parser.getHighlights())
            {
                ids.push_back(entry.getId());
            }
            for (const LLUUID& id : ids)
            {
                parser.removeHighlight(id);
            }
        }

        LLUUID add(const char* pattern, LLHighlightEntry::EConditionType condition = LLHighlightEntry::CONTAINS, bool case_sensitive = false,
                   S32 categories = LLHighlightEntry::CAT_ALL, LLHighlightEntry::EHighlightType type = LLHighlightEntry::PART)
        {
            LLHighlightEntry entry;
            entry.mPattern       = pattern;
            entry.mCondition     = condition;
            entry.mCaseSensitive = case_sensitive;
            entry.mCategoryMask  = categories;
            entry.mHighlightType = type;
            parser.addHighlight(entry);
            return entry.getId();
        }

        // The pieces a line is split into, a highlighted one in brackets
        // with its keyword after a colon. A keyword that ends the line after
        // its start leaves an empty piece after it, as the parser always has.
        std::string pieces(const char* text, S32 categories = LLHighlightEntry::CAT_NEARBYCHAT)
        {
            std::string out;
            for (const auto& [piece, entry] : parser.parsePartialLineHighlights(text, categories))
            {
                out += entry ? "[" + piece + ":" + entry->mPattern + "]" : piece;
                out += "|";
            }
            return out;
        }
    };

    TEST_CASE_METHOD(lltextparser_data, "lltextparser: a line is split at each keyword in any case unless case is asked for", "[lltextparser]")
    {
        CHECK(pieces("hi alice and bob") == std::string("hi alice and bob|"));
        add("alice");
        UNSCOPED_INFO("a keyword in any case");
        CHECK(pieces("Hi ALICE and bob") == std::string("Hi |[ALICE:alice]| and bob|"));
        add("bob");
        CHECK(pieces("Hi ALICE and bob") == std::string("Hi |[ALICE:alice]| and |[bob:bob]||"));
        CHECK(pieces("bob, alice, bob") == std::string("[bob:bob]|, |[alice:alice]|, |[bob:bob]||"));
        add("Carol", LLHighlightEntry::CONTAINS, true);
        UNSCOPED_INFO("a keyword whose case is asked for");
        CHECK(pieces("carol and Carol") == std::string("carol and |[Carol:Carol]||"));
        CHECK(pieces("nobody here") == std::string("nobody here|"));
        add("jos\xC3\xA9");
        CHECK(pieces("hi jos\xC3\xA9!") == std::string("hi |[jos\xC3\xA9:jos\xC3\xA9]|!|"));
    }

    TEST_CASE_METHOD(lltextparser_data, "lltextparser: keywords match only where their condition and category allow", "[lltextparser]")
    {
        add("hey", LLHighlightEntry::STARTS_WITH);
        add("bye", LLHighlightEntry::ENDS_WITH);
        add("im only", LLHighlightEntry::CONTAINS, false, LLHighlightEntry::CAT_IM);
        CHECK(pieces("hey there") == std::string("[hey:hey]| there|"));
        UNSCOPED_INFO("a STARTS_WITH keyword not at the start");
        CHECK(pieces("they said hey") == std::string("they said hey|"));
        CHECK(pieces("ok bye") == std::string("ok |[bye:bye]||"));
        UNSCOPED_INFO("an ENDS_WITH keyword not at the end");
        CHECK(pieces("bye now") == std::string("bye now|"));
        UNSCOPED_INFO("an IM keyword in nearby chat");
        CHECK(pieces("this is im only") == std::string("this is im only|"));
        CHECK(pieces("this is im only", LLHighlightEntry::CAT_IM) == std::string("this is |[im only:im only]||"));

        add("urgent", LLHighlightEntry::CONTAINS, false, LLHighlightEntry::CAT_ALL, LLHighlightEntry::ALL);
        add("exactly this", LLHighlightEntry::MATCHES);
        const LLHighlightEntry* entry = nullptr;
        CHECK(parser.parseFullLineHighlights("this is URGENT now", LLHighlightEntry::CAT_NEARBYCHAT, &entry));
        CHECK((entry ? entry->mPattern : std::string()) == std::string("urgent"));
        REQUIRE(parser.parseFullLineHighlights("Exactly This", LLHighlightEntry::CAT_NEARBYCHAT, &entry));
        REQUIRE(entry);
        CHECK(entry->mPattern == "exactly this");
        CHECK_FALSE(parser.parseFullLineHighlights("not exactly this", LLHighlightEntry::CAT_NEARBYCHAT, &entry));
    }

    TEST_CASE_METHOD(lltextparser_data, "lltextparser: an entry changed in place or removed is found as it now is", "[lltextparser]")
    {
        const LLUUID id = add("alice");
        CHECK(pieces("carol and alice") == std::string("carol and |[alice:alice]||"));
        LLHighlightEntry* entry = parser.getHighlightById(id);
        REQUIRE(entry != nullptr);
        entry->mPattern = "carol";
        CHECK(pieces("carol and alice") == std::string("[carol:carol]| and alice|"));
        entry = parser.getHighlightById(id);
        REQUIRE(entry != nullptr);
        entry->mCaseSensitive = true;
        CHECK(pieces("Carol and carol") == std::string("Carol and |[carol:carol]||"));
        parser.removeHighlight(id);
        CHECK(pieces("carol and alice") == std::string("carol and alice|"));
    }
}
