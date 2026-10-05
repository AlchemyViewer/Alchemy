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

#include "../test/lltut.h"

#include <set>
#include <string>

namespace tut
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
            ensure("lsl loads: " + error, library.loadFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error));
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

    typedef test_group<altextspelling_data> altextspelling_group;
    typedef altextspelling_group::object    altextspelling_object;
    altextspelling_group                    altextspelling_instance("altextspelling");

    template<> template<>
    void altextspelling_object::test<1>()
    {
        set_test_name("prose is checked word by word, and what reads as code or is too short to be wrong is not");
        ALTextDocument      doc("the catt sat on_it 2nd myVar Xyzzy zz");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        ensure("someone to ask", spelling.available());
        ensure_equals("the words it lacks", words(doc, 0, spelling.misspellings(doc, highlighter, 0, true)), std::string("catt|Xyzzy"));
        ensure("none off", ALTextSpelling().misspellings(doc, highlighter, 0, false).empty());
        ensure("none past the text", spelling.misspellings(doc, highlighter, 5, true).empty());
    }

    template<> template<>
    void altextspelling_object::test<2>()
    {
        set_test_name("in code only comments and strings are prose, and a comment opened above a line makes it prose");
        ALTextDocument      doc("string mesage = \"a speling\"; // the dogg\nintegre x;");
        ALSyntaxHighlighter highlighter;
        highlighter.setGrammar(library.find("lsl"));
        highlighter.attach(&doc);
        ensure_equals("the comment and the string", words(doc, 0, spelling.misspellings(doc, highlighter, 0, true)), std::string("speling|dogg"));
        ensure("the code under them, never", spelling.misspellings(doc, highlighter, 1, true).empty());
        // A comment opened above: the line under it is prose now, and
        // checked again though its own text did not change.
        doc.replace(ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), "/*\n");
        spelling.edited(ALTextDocument::Edit{ ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), std::string(), "/*\n" }, doc.lineCount());
        ensure_equals("in the comment", words(doc, 2, spelling.misspellings(doc, highlighter, 2, true)), std::string("integre"));
    }

    template<> template<>
    void altextspelling_object::test<3>()
    {
        set_test_name("a line's words are kept until it changes, and the lines below slide with an edit rather than being checked again");
        ALTextDocument      doc("the catt\nthe dogg\nand matt");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        for (S32 line = 0; line < 3; ++line)
        {
            spelling.misspellings(doc, highlighter, line, true);
        }
        const size_t checked = asked.size();
        spelling.misspellings(doc, highlighter, 1, true);
        ensure_equals("asked again, nothing checked again", asked.size(), checked);

        // A line put in above: the two below slide, and only the new one is
        // checked.
        const ALTextDocument::Edit made = doc.replace(ALTextRange(ALTextPos(0, 8), ALTextPos(0, 8)), "\nthe ratt");
        spelling.edited(made, doc.lineCount());
        ensure_equals("the line after, slid", words(doc, 2, spelling.misspellings(doc, highlighter, 2, true)), std::string("dogg"));
        ensure_equals("and the last", words(doc, 3, spelling.misspellings(doc, highlighter, 3, true)), std::string("matt"));
        ensure_equals("still nothing checked again", asked.size(), checked);
        ensure_equals("the line put in", words(doc, 1, spelling.misspellings(doc, highlighter, 1, true)), std::string("ratt"));
        ensure("checked now", asked.size() > checked);

        spelling.recheck();
        const size_t before = asked.size();
        spelling.misspellings(doc, highlighter, 3, true);
        ensure("all of it checked again once the dictionary changed", asked.size() > before);
    }

    template<> template<>
    void altextspelling_object::test<4>()
    {
        set_test_name("the misspelling at a place, what it might have been, and one of those taken");
        ALTextDocument      doc("the catt sat");
        ALSyntaxHighlighter highlighter;
        highlighter.attach(&doc);
        ALTextRange word;
        ensure("in one", spelling.misspelledAt(doc, highlighter, ALTextPos(0, 6), true, &word));
        ensure("the word", word == ALTextRange(ALTextPos(0, 4), ALTextPos(0, 8)));
        ensure("its end counts", spelling.misspelledAt(doc, highlighter, ALTextPos(0, 8), true));
        ensure("not in a word spelled right", !spelling.misspelledAt(doc, highlighter, ALTextPos(0, 10), true));

        spelling.suggestAt(doc, highlighter, ALTextPos(0, 5), true);
        ensure("suggested for", spelling.suggestedFor() == word);
        ensure("what the suggester said", spelling.suggestions() == std::vector<std::string>{ "catt?", "cat" });
        ensure("the test's dictionary takes nothing in", !spelling.canTeach(true));
        const std::optional<std::pair<ALTextRange, std::string>> taken = spelling.take(1);
        ensure("taken", taken.has_value() && taken->first == word && taken->second == "cat");
        ensure("and let go of", spelling.suggestions().empty() && spelling.suggestedFor().empty());
        ensure("none past the end", !spelling.take(5));

        spelling.suggestAt(doc, highlighter, ALTextPos(0, 10), true);
        ensure("nothing for a word spelled right", spelling.suggestions().empty() && spelling.suggestedFor().empty());
        spelling.suggestAt(doc, highlighter, ALTextPos(0, 5), false);
        ensure("nor while it is off", spelling.suggestedFor().empty());
        spelling.suggestAt(doc, highlighter, ALTextPos(0, 5), true);
        spelling.edited(ALTextDocument::Edit{ ALTextRange(ALTextPos(0, 0), ALTextPos(0, 0)), std::string(), "x" }, doc.lineCount());
        ensure("an edit lets them go", spelling.suggestions().empty());
    }
}
