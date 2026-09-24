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

#include "../test/lltut.h"

namespace tut
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

    typedef test_group<lltextparser_data> lltextparser_group;
    typedef lltextparser_group::object    lltextparser_object;
    lltextparser_group                    lltextparser_instance("lltextparser");

    template<> template<>
    void lltextparser_object::test<1>()
    {
        set_test_name("a line is split at each keyword it holds, in any case unless the keyword's case is asked for");
        ensure_equals("no keywords, the line whole", pieces("hi alice and bob"), std::string("hi alice and bob|"));
        add("alice");
        ensure_equals("a keyword in any case", pieces("Hi ALICE and bob"), std::string("Hi |[ALICE:alice]| and bob|"));
        add("bob");
        ensure_equals("two", pieces("Hi ALICE and bob"), std::string("Hi |[ALICE:alice]| and |[bob:bob]||"));
        ensure_equals("one twice", pieces("bob, alice, bob"), std::string("[bob:bob]|, |[alice:alice]|, |[bob:bob]||"));
        add("Carol", LLHighlightEntry::CONTAINS, true);
        ensure_equals("a keyword whose case is asked for", pieces("carol and Carol"), std::string("carol and |[Carol:Carol]||"));
        ensure_equals("none held", pieces("nobody here"), std::string("nobody here|"));
        add("jos\xC3\xA9");
        ensure_equals("a keyword beyond ASCII", pieces("hi jos\xC3\xA9!"), std::string("hi |[jos\xC3\xA9:jos\xC3\xA9]|!|"));
    }

    template<> template<>
    void lltextparser_object::test<2>()
    {
        set_test_name("a keyword that starts or ends the line only there, one for other chat not in this, and a whole-line keyword only whole");
        add("hey", LLHighlightEntry::STARTS_WITH);
        add("bye", LLHighlightEntry::ENDS_WITH);
        add("im only", LLHighlightEntry::CONTAINS, false, LLHighlightEntry::CAT_IM);
        ensure_equals("at the start", pieces("hey there"), std::string("[hey:hey]| there|"));
        ensure_equals("not after it", pieces("they said hey"), std::string("they said hey|"));
        ensure_equals("at the end", pieces("ok bye"), std::string("ok |[bye:bye]||"));
        ensure_equals("not before it", pieces("bye now"), std::string("bye now|"));
        ensure_equals("not in nearby chat", pieces("this is im only"), std::string("this is im only|"));
        ensure_equals("in an IM", pieces("this is im only", LLHighlightEntry::CAT_IM), std::string("this is |[im only:im only]||"));

        add("urgent", LLHighlightEntry::CONTAINS, false, LLHighlightEntry::CAT_ALL, LLHighlightEntry::ALL);
        add("exactly this", LLHighlightEntry::MATCHES);
        const LLHighlightEntry* entry = nullptr;
        ensure("a whole-line keyword held", parser.parseFullLineHighlights("this is URGENT now", LLHighlightEntry::CAT_NEARBYCHAT, &entry));
        ensure_equals("and which", entry ? entry->mPattern : std::string(), std::string("urgent"));
        ensure("the whole line alone", parser.parseFullLineHighlights("Exactly This", LLHighlightEntry::CAT_NEARBYCHAT, &entry) && entry &&
                                          entry->mPattern == "exactly this");
        ensure("not a part of one", !parser.parseFullLineHighlights("not exactly this", LLHighlightEntry::CAT_NEARBYCHAT, &entry));
    }

    template<> template<>
    void lltextparser_object::test<3>()
    {
        set_test_name("an entry changed where it is, or removed, is found as it now is");
        const LLUUID id = add("alice");
        ensure_equals("as it was", pieces("carol and alice"), std::string("carol and |[alice:alice]||"));
        LLHighlightEntry* entry = parser.getHighlightById(id);
        ensure("there", entry != nullptr);
        entry->mPattern = "carol";
        ensure_equals("as it is", pieces("carol and alice"), std::string("[carol:carol]| and alice|"));
        entry = parser.getHighlightById(id);
        entry->mCaseSensitive = true;
        ensure_equals("its case asked for", pieces("Carol and carol"), std::string("Carol and |[carol:carol]||"));
        parser.removeHighlight(id);
        ensure_equals("removed", pieces("carol and alice"), std::string("carol and alice|"));
    }
}
