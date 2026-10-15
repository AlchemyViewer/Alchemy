/**
 * @file altextspelling_test.cpp
 * @brief A text view's spell check over a document, with a checker of the test's own and nothing drawn.
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

#include "altextspelling.h"

#include "alsyntaxgrammar.h"
#include "alsyntaxhighlighter.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>

namespace
{
    struct altextspelling_data
    {
        ALSyntaxLibrary       library;
        ALTextSpelling        spelling;
        // The words the test's dictionary has, and every word it was asked.
        std::set<std::string> known{ "the", "cat", "sat", "mat", "and", "dog" };
        std::vector<std::string> asked;

        altextspelling_data()
        {
            std::string error;
            const bool  loaded = library.loadFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error);
            INFO("lsl loads: " << error);
            REQUIRE(loaded);
            spelling.setChecker(
                [this](const std::string& word) {
                    asked.push_back(word);
                    return known.count(word) > 0;
                },
                [](const std::string& word, std::vector<std::string>& out) {
                    out.push_back(word + "?");
                    out.push_back("cat");
                });
        }

        // The misspellings of a line, as the words they stand on.
        static std::string words(const ALTextDocument& doc, S32 line, const ALTextSpelling::words_t& found)
        {
            std::string out;
            for (const auto& [begin, end] : found)
            {
                out += (out.empty() ? "" : "|") + doc.line(line).substr(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
            }
            return out;
        }
    };

    TEST_CASE_METHOD(altextspelling_data, "altextspelling: prose is checked word by word but code-like and very short words are not", "[altextspelling]")
    {
        ALTextDocument      doc("the catt sat on_it 2nd myVar Xyzzy zz");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        UNSCOPED_INFO("someone to ask");
        CHECK(spelling.available());
        UNSCOPED_INFO("the words it lacks");
        CHECK(words(doc, 0, spelling.misspellings(doc, highlighter, 0, true)) == std::string("catt|Xyzzy"));
        UNSCOPED_INFO("none off");
        CHECK(ALTextSpelling().misspellings(doc, highlighter, 0, false).empty());
        UNSCOPED_INFO("none past the text");
        CHECK(spelling.misspellings(doc, highlighter, 5, true).empty());
    }

    TEST_CASE_METHOD(altextspelling_data, "altextspelling: in code only comments and strings are prose even one opened above", "[altextspelling]")
    {
        ALTextDocument      doc("string mesage = \"a speling\"; // the dogg\nintegre x;");
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(library.find("lsl"));
        highlighter.attach(&doc);
        UNSCOPED_INFO("the comment and the string");
        CHECK(words(doc, 0, spelling.misspellings(doc, highlighter, 0, true)) == std::string("speling|dogg"));
        UNSCOPED_INFO("the code under them, never");
        CHECK(spelling.misspellings(doc, highlighter, 1, true).empty());
        // A comment opened above: the line under it is prose now, and
        // checked again though its own text did not change.
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "/*\n");
        spelling.edited(ALTextDocument::Edit{ ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), std::string(), "/*\n" }, doc.lineCount());
        UNSCOPED_INFO("in the comment");
        CHECK(words(doc, 2, spelling.misspellings(doc, highlighter, 2, true)) == std::string("integre"));
    }

    TEST_CASE_METHOD(altextspelling_data, "altextspelling: a line's misspellings are kept until it changes and slide with edits", "[altextspelling]")
    {
        ALTextDocument      doc("the catt\nthe dogg\nand matt");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        for (S32 line = 0; line < 3; ++line)
        {
            spelling.misspellings(doc, highlighter, line, true);
        }
        const size_t checked = asked.size();
        spelling.misspellings(doc, highlighter, 1, true);
        UNSCOPED_INFO("asked again, nothing checked again");
        CHECK(asked.size() == checked);

        // A line put in above: the two below slide, and only the new one is
        // checked.
        const ALTextDocument::Edit made = doc.replace(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 8)), "\nthe ratt");
        spelling.edited(made, doc.lineCount());
        UNSCOPED_INFO("the line after, slid");
        CHECK(words(doc, 2, spelling.misspellings(doc, highlighter, 2, true)) == std::string("dogg"));
        UNSCOPED_INFO("and the last");
        CHECK(words(doc, 3, spelling.misspellings(doc, highlighter, 3, true)) == std::string("matt"));
        UNSCOPED_INFO("still nothing checked again");
        CHECK(asked.size() == checked);
        UNSCOPED_INFO("the line put in");
        CHECK(words(doc, 1, spelling.misspellings(doc, highlighter, 1, true)) == std::string("ratt"));
        UNSCOPED_INFO("checked now");
        CHECK(asked.size() > checked);

        spelling.recheck();
        const size_t before = asked.size();
        spelling.misspellings(doc, highlighter, 3, true);
        UNSCOPED_INFO("all of it checked again once the dictionary changed");
        CHECK(asked.size() > before);
    }

    TEST_CASE_METHOD(altextspelling_data, "altextspelling: the misspelling at a place gets suggestions and one can be taken", "[altextspelling]")
    {
        ALTextDocument      doc("the catt sat");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        ALTextRange word;
        UNSCOPED_INFO("in one");
        REQUIRE(spelling.misspelledAt(doc, highlighter, ALTextPos(0, 6), true, &word));
        UNSCOPED_INFO("the word");
        CHECK(word == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 8)));
        UNSCOPED_INFO("its end counts");
        CHECK(spelling.misspelledAt(doc, highlighter, ALTextPos(0, 8), true));
        UNSCOPED_INFO("not in a word spelled right");
        CHECK_FALSE(spelling.misspelledAt(doc, highlighter, ALTextPos(0, 10), true));

        spelling.suggestAt(doc, highlighter, ALTextPos(0, 5), true);
        UNSCOPED_INFO("suggested for");
        CHECK(spelling.suggestedFor() == word);
        UNSCOPED_INFO("what the suggester said");
        CHECK(spelling.suggestions() == std::vector<std::string>{ "catt?", "cat" });
        UNSCOPED_INFO("the test's dictionary takes nothing in");
        CHECK_FALSE(spelling.canTeach(true));
        const std::optional<std::pair<ALTextRange, std::string>> taken = spelling.take(1);
        {
            INFO("taken");
            REQUIRE(taken.has_value());
            CHECK(taken->first == word);
            CHECK(taken->second == "cat");
        }
        {
            INFO("and let go of");
            CHECK(spelling.suggestions().empty());
            CHECK(spelling.suggestedFor().empty());
        }
        UNSCOPED_INFO("none past the end");
        CHECK_FALSE(spelling.take(5));

        spelling.suggestAt(doc, highlighter, ALTextPos(0, 10), true);
        {
            INFO("nothing for a word spelled right");
            CHECK(spelling.suggestions().empty());
            CHECK(spelling.suggestedFor().empty());
        }
        spelling.suggestAt(doc, highlighter, ALTextPos(0, 5), false);
        UNSCOPED_INFO("nor while it is off");
        CHECK(spelling.suggestedFor().empty());
        spelling.suggestAt(doc, highlighter, ALTextPos(0, 5), true);
        spelling.edited(ALTextDocument::Edit{ ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), std::string(), "x" }, doc.lineCount());
        UNSCOPED_INFO("an edit lets them go");
        CHECK(spelling.suggestions().empty());
    }
}
