/**
 * @file altextdiff_bench.cpp
 * @brief Comparing big scripts: by lines, by words, laid out, and what it
 *        says of each.
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

// What comparing costs over generated LSL scripts of 5,000 and 50,000
// lines, each against itself edited ten times and a thousand: the lines
// alone (ALTextDiff), and the comparison laid out as a view shows it --
// lines, each change's lines paired and their words -- (ALDiffModel). Each
// benchmark is one operation, named for its text's length; Catch2 reports
// the mean of its samples and the interval around it.
//
// After the times, what each comparison says of some edits a person
// makes -- a line changed, a block moved, a function reformatted, lines
// changed all over -- as counts: changes, lines marked, words marked in
// lines paired, and lines taken out that were moved. Those are not better
// or worse by being lower; they are read against the same rows from
// before a change to how texts are compared, to see that it did what it
// meant to and nothing else.

#include "linden_common.h"

#include "aldifflexer.h"
#include "aldiffmodel.h"
#include "altextdiff.h"

#include "albigscript.h"
#include "altestbenchmark.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
    std::string joined(const std::vector<std::string>& lines)
    {
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            out += (i ? "\n" : "") + lines[i];
        }
        return out;
    }

    // A text edited `count` times, the same way every time: a line changed
    // (a number in it, or a word put at its end), a line put in, or one
    // taken out, each somewhere at random.
    std::vector<std::string> edited(std::vector<std::string> lines, S32 count, U32 seed)
    {
        std::mt19937 random(seed);
        for (S32 n = 0; n < count && lines.size() > 2; ++n)
        {
            const size_t at = random() % lines.size();
            switch (random() % 4)
            {
                case 0:
                    lines[at] += " // edited";
                    break;
                case 1:
                    if (const size_t digit = lines[at].find_first_of("0123456789"); digit != std::string::npos)
                    {
                        lines[at][digit] = lines[at][digit] == '9' ? '0' : static_cast<char>(lines[at][digit] + 1);
                    }
                    else
                    {
                        lines[at] = "    total += " + std::to_string(n) + ";";
                    }
                    break;
                case 2:
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), "    llOwnerSay(\"put in " + std::to_string(n) + "\");");
                    break;
                default:
                    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(at));
                    break;
            }
        }
        return lines;
    }

    // A text's helper function moved from near its start to near its end.
    std::vector<std::string> moved(std::vector<std::string> lines)
    {
        const auto first = std::find_if(lines.begin() + static_cast<std::ptrdiff_t>(lines.size() / 10), lines.end(),
                                        [](const std::string& line) { return line.rfind("integer helper", 0) == 0; });
        const auto end   = std::find(first, lines.end(), "}");
        if (first == lines.end() || end == lines.end())
        {
            return lines;
        }
        std::vector<std::string> block(first, end + 1);
        lines.erase(first, end + 1);
        const auto to = std::find_if(lines.begin() + static_cast<std::ptrdiff_t>(lines.size() * 8 / 10), lines.end(),
                                     [](const std::string& line) { return line.rfind("integer helper", 0) == 0; });
        lines.insert(to, block.begin(), block.end());
        return lines;
    }

    // Each call's arguments put one to a line and each block's brace on
    // the line before it, over a stretch of a text: the same code.
    std::vector<std::string> reformatted(const std::vector<std::string>& lines, size_t from, size_t count)
    {
        std::vector<std::string> out(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(from));
        for (size_t i = from; i < lines.size(); ++i)
        {
            const std::string& line = lines[i];
            if (i >= from + count)
            {
                out.push_back(line);
                continue;
            }
            if (line.find_first_not_of(' ') != std::string::npos && line.substr(line.find_first_not_of(' ')) == "{" && !out.empty())
            {
                out.back() += " {";
                continue;
            }
            const size_t open = line.find("llParseString2List(");
            if (open != std::string::npos)
            {
                const std::string indent(line.find_first_not_of(' ') + 4, ' ');
                out.push_back(line.substr(0, open) + "llParseString2List(");
                out.push_back(indent + "b,");
                out.push_back(indent + "[\",\", \";\"],");
                out.push_back(indent + "[\"\\\\\"]");
                out.push_back(std::string(indent.size() - 4, ' ') + ");");
                continue;
            }
            out.push_back(line);
        }
        return out;
    }

    // What a comparison says, laid out: its changes, the lines marked on
    // each side, the lines paired as changed, and the words marked in them.
    void says(const char* name, const std::string& left, const std::string& right, const ALTextDiff::Options& options = ALTextDiff::Options())
    {
        ALDiffModel model;
        model.setLikeness(options.like);
        model.setLexer(options.lexer);
        model.setAlgorithm(options.algorithm);
        model.setTexts(left, right);
        S32 removed = 0, added = 0, paired = 0, marks = 0, moved = 0;
        for (S32 line = 0; line < model.lineCount(ALDiffModel::Column::Left); ++line)
        {
            const ALDiffModel::Line& one = model.line(ALDiffModel::Column::Left, line);
            removed += one.kind == ALDiffModel::Kind::Removed;
            paired += one.sign == '~';
            moved += one.move >= 0;
            marks += static_cast<S32>(one.words.size());
        }
        for (S32 line = 0; line < model.lineCount(ALDiffModel::Column::Right); ++line)
        {
            const ALDiffModel::Line& one = model.line(ALDiffModel::Column::Right, line);
            added += one.kind == ALDiffModel::Kind::Added;
            marks += static_cast<S32>(one.words.size());
        }
        std::printf("  %-44s %8d %8d %8d %8d %8d %8d\n", name, model.changeCount(), removed, added, paired, marks, moved);
    }

    // The texts compared, made once for every case of a run: each script,
    // edited ten times and a thousand, as lines and as text.
    struct Texts
    {
        std::vector<std::string> small   = ALTextDiff::split(ll_test::bigLSL(5000));
        std::vector<std::string> big     = ALTextDiff::split(ll_test::bigLSL(50000));
        std::vector<std::string> small10 = edited(small, 10, 1);
        std::vector<std::string> small1k = edited(small, 1000, 2);
        std::vector<std::string> big10   = edited(big, 10, 3);
        std::vector<std::string> big1k   = edited(big, 1000, 4);
        std::string              small_t  = joined(small);
        std::string              big_t    = joined(big);
        std::string              small10t = joined(small10);
        std::string              small1kt = joined(small1k);
        std::string              big10t   = joined(big10);
        std::string              big1kt   = joined(big1k);
    };

    const Texts& texts()
    {
        static const Texts made;
        return made;
    }

    // LSL's grammar, where the source tree has it, for words cut as code.
    std::shared_ptr<const ALSyntaxGrammar> lslGrammar()
    {
        std::string error;
        return ALSyntaxGrammar::fromFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error);
    }

    // A model's lexer as a view gives it: saying what it read again, and
    // told what an edit knows; and a merge's of its own, likewise.
    void lexing(ALDiffModel& model, const std::shared_ptr<const ALSyntaxGrammar>& lsl)
    {
        const std::shared_ptr<ALDiffLexer> compared = lsl ? std::make_shared<ALDiffLexer>(lsl) : nullptr;
        const std::shared_ptr<ALDiffLexer> merging  = lsl ? std::make_shared<ALDiffLexer>(lsl) : nullptr;
        model.setLexer(compared ? ALDiffLexer::lexerOf(compared) : ALTextDiff::lexer_t(), merging ? ALDiffLexer::lexerOf(merging) : ALTextDiff::lexer_t(),
                       compared ? ALDiffLexer::rereadOf(compared) : ALTextDiff::reread_t(),
                       compared ? ALDiffLexer::toldOf(compared) : ALTextDiff::told_t(),
                       merging ? ALDiffLexer::rereadOf(merging) : ALTextDiff::reread_t(),
                       merging ? ALDiffLexer::toldOf(merging) : ALTextDiff::told_t());
    }

    // A live comparison, the right typed in: a character put in a line near
    // the middle, and taken out again, each call one keystroke compared
    // again. Blanks let go of where asked, which a grammar's strings keep,
    // or comments, which a grammar says where they are.
    struct Typing
    {
        bool                  grammar   = false;
        ALTextDiff::Algorithm algorithm = ALTextDiff::Algorithm::Histogram;
        ALTextDiff::ranges_t  ranges;
        bool                  blanks   = false;
        bool                  comments = false;
    };

    void typed(const char* name, const std::string& left, const std::string& right, const Typing& typing = Typing())
    {
        ALDiffModel model;
        if (typing.grammar)
        {
            lexing(model, lslGrammar());
        }
        ALTextDiff::Likeness like;
        like.ignoreWhitespace = typing.blanks;
        like.ignoreComments   = typing.comments;
        model.setLikeness(like);
        model.setAlgorithm(typing.algorithm);
        model.setTexts(left, right, typing.ranges);
        const size_t at   = right.find('\n', right.size() / 2);
        std::string  with = right;
        with.insert(at, "x");
        size_t keystrokes = 0;
        BENCHMARK(name)
        {
            model.setRightText(++keystrokes % 2 ? with : right);
            return model.changeCount();
        };
    }

    // The same keystroke in a merge: the text before both versions the
    // base, theirs the left with ten edits, ours the right with a thousand.
    void merged(const char* name, const std::string& base, const std::string& left, const std::string& right, bool comments)
    {
        ALDiffModel model;
        lexing(model, lslGrammar());
        ALTextDiff::Likeness like;
        like.ignoreComments = comments;
        model.setLikeness(like);
        model.setTexts(left, right);
        model.setMergeBase(base);
        const size_t at   = right.find('\n', right.size() / 2);
        std::string  with = right;
        with.insert(at, "x");
        size_t keystrokes = 0;
        BENCHMARK(name)
        {
            model.setRightText(++keystrokes % 2 ? with : right);
            return model.conflictCount();
        };
    }

    // Another version on the left, as a slider over a script's saves steps:
    // a line apart from the one before.
    void stepped(const char* name, const std::string& left, const std::string& right)
    {
        ALDiffModel model;
        model.setTexts(left, right);
        const size_t at   = left.find('\n', left.size() / 3);
        std::string  with = left;
        with.insert(at, " // older");
        size_t steps = 0;
        BENCHMARK(name)
        {
            model.setLeftText(++steps % 2 ? with : left);
            return model.changeCount();
        };
    }

    // A script beside what it was converted to: every line written
    // otherwise, each anchored to its own, as the converter lines them up.
    std::string converted(const std::vector<std::string>& lines, ALTextDiff::ranges_t& ranges)
    {
        std::vector<std::string> out;
        for (size_t n = 0; n < lines.size(); ++n)
        {
            out.push_back(lines[n] + " -- as SLua");
            ranges.push_back({ static_cast<S32>(n), static_cast<S32>(n), static_cast<S32>(n), static_cast<S32>(n) });
        }
        return joined(out);
    }


    TEST_CASE("altextdiff_bench: the lines alone by each algorithm", "[altextdiff_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const Texts& t = texts();
        const auto lines = [](const char* name, const std::vector<std::string>& left, const std::vector<std::string>& right,
                              ALTextDiff::Algorithm algorithm) {
            ALTextDiff::Options options;
            options.algorithm = algorithm;
            BENCHMARK(name)
            {
                return ALTextDiff::lines(left, right, options).size();
            };
        };
        lines("histogram, ten edits, 5,000 lines", t.small, t.small10, ALTextDiff::Algorithm::Histogram);
        lines("histogram, ten edits, 50,000 lines", t.big, t.big10, ALTextDiff::Algorithm::Histogram);
        lines("histogram, a thousand edits, 5,000 lines", t.small, t.small1k, ALTextDiff::Algorithm::Histogram);
        lines("histogram, a thousand edits, 50,000 lines", t.big, t.big1k, ALTextDiff::Algorithm::Histogram);
        lines("patience, ten edits, 5,000 lines", t.small, t.small10, ALTextDiff::Algorithm::Patience);
        lines("patience, ten edits, 50,000 lines", t.big, t.big10, ALTextDiff::Algorithm::Patience);
        lines("patience, a thousand edits, 5,000 lines", t.small, t.small1k, ALTextDiff::Algorithm::Patience);
        lines("patience, a thousand edits, 50,000 lines", t.big, t.big1k, ALTextDiff::Algorithm::Patience);
        lines("minimal, ten edits, 5,000 lines", t.small, t.small10, ALTextDiff::Algorithm::Minimal);
        lines("minimal, ten edits, 50,000 lines", t.big, t.big10, ALTextDiff::Algorithm::Minimal);
        lines("minimal, a thousand edits, 5,000 lines", t.small, t.small1k, ALTextDiff::Algorithm::Minimal);
        lines("minimal, a thousand edits, 50,000 lines", t.big, t.big1k, ALTextDiff::Algorithm::Minimal);
        lines("structural, ten edits, 5,000 lines", t.small, t.small10, ALTextDiff::Algorithm::Structural);
        lines("structural, ten edits, 50,000 lines", t.big, t.big10, ALTextDiff::Algorithm::Structural);
        lines("structural, a thousand edits, 5,000 lines", t.small, t.small1k, ALTextDiff::Algorithm::Structural);
        lines("structural, a thousand edits, 50,000 lines", t.big, t.big1k, ALTextDiff::Algorithm::Structural);
    }

    TEST_CASE("altextdiff_bench: laid out and made whole", "[altextdiff_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const Texts&                                 t   = texts();
        const std::shared_ptr<const ALSyntaxGrammar> lsl = lslGrammar();
        const auto laid = [&lsl](const char* name, const std::string& left, const std::string& right, bool grammar) {
            BENCHMARK(name)
            {
                ALDiffModel model;
                if (grammar)
                {
                    lexing(model, lsl);
                }
                model.setTexts(left, right);
                return model.rowCount(ALDiffModel::Layout::Sides);
            };
        };
        laid("ten edits, 5,000 lines", t.small_t, t.small10t, false);
        laid("ten edits, 50,000 lines", t.big_t, t.big10t, false);
        laid("a thousand edits, 5,000 lines", t.small_t, t.small1kt, false);
        laid("a thousand edits, 50,000 lines", t.big_t, t.big1kt, false);
        laid("ten edits, words by LSL's grammar, 5,000 lines", t.small_t, t.small10t, true);
        laid("ten edits, words by LSL's grammar, 50,000 lines", t.big_t, t.big10t, true);
        laid("a thousand edits, words by LSL's grammar, 5,000 lines", t.small_t, t.small1kt, true);
        laid("a thousand edits, words by LSL's grammar, 50,000 lines", t.big_t, t.big1kt, true);
    }

    TEST_CASE("altextdiff_bench: laid out with a keystroke compared again", "[altextdiff_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const Texts& t = texts();
        typed("a keystroke, ten edits, 5,000 lines", t.small_t, t.small10t);
        typed("a keystroke, ten edits, 50,000 lines", t.big_t, t.big10t);
        typed("a keystroke, a thousand edits, 5,000 lines", t.small_t, t.small1kt);
        typed("a keystroke, a thousand edits, 50,000 lines", t.big_t, t.big1kt);
        Typing grammar;
        grammar.grammar = true;
        typed("a keystroke, a thousand edits, LSL's grammar, 5,000 lines", t.small_t, t.small1kt, grammar);
        typed("a keystroke, a thousand edits, LSL's grammar, 50,000 lines", t.big_t, t.big1kt, grammar);
        Typing blanks = grammar;
        blanks.blanks = true;
        typed("a keystroke, a thousand edits, LSL's, blanks let go, 5,000 lines", t.small_t, t.small1kt, blanks);
        typed("a keystroke, a thousand edits, LSL's, blanks let go, 50,000 lines", t.big_t, t.big1kt, blanks);
        Typing comments   = grammar;
        comments.comments = true;
        typed("a keystroke, a thousand edits, LSL's, comments let go, 5,000 lines", t.small_t, t.small1kt, comments);
        typed("a keystroke, a thousand edits, LSL's, comments let go, 50,000 lines", t.big_t, t.big1kt, comments);
        Typing structure    = grammar;
        structure.algorithm = ALTextDiff::Algorithm::Structural;
        typed("a keystroke, a thousand edits, by structure, 5,000 lines", t.small_t, t.small1kt, structure);
        typed("a keystroke, a thousand edits, by structure, 50,000 lines", t.big_t, t.big1kt, structure);
        merged("a keystroke in a merge, LSL's grammar, 5,000 lines", t.small_t, t.small10t, t.small1kt, false);
        merged("a keystroke in a merge, LSL's grammar, 50,000 lines", t.big_t, t.big10t, t.big1kt, false);
        merged("a keystroke in a merge, LSL's, comments let go, 5,000 lines", t.small_t, t.small10t, t.small1kt, true);
        merged("a keystroke in a merge, LSL's, comments let go, 50,000 lines", t.big_t, t.big10t, t.big1kt, true);
        stepped("another version on the left, ten edits, 5,000 lines", t.small_t, t.small10t);
        stepped("another version on the left, ten edits, 50,000 lines", t.big_t, t.big10t);

        ALTextDiff::ranges_t small_ranges;
        ALTextDiff::ranges_t big_ranges;
        const std::string    small_c = converted(t.small, small_ranges);
        const std::string    big_c   = converted(t.big, big_ranges);
        Typing               anchored_small;
        anchored_small.ranges = small_ranges;
        Typing anchored_big;
        anchored_big.ranges = big_ranges;
        typed("a keystroke, every line converted, anchored, 5,000 lines", t.small_t, small_c, anchored_small);
        typed("a keystroke, every line converted, anchored, 50,000 lines", t.big_t, big_c, anchored_big);
        anchored_small.grammar = anchored_big.grammar = true;
        anchored_small.blanks = anchored_big.blanks = true;
        typed("a keystroke, converted, LSL's, blanks let go, 5,000 lines", t.small_t, small_c, anchored_small);
        typed("a keystroke, converted, LSL's, blanks let go, 50,000 lines", t.big_t, big_c, anchored_big);
        anchored_small.blanks = anchored_big.blanks = false;
        anchored_small.comments = anchored_big.comments = true;
        typed("a keystroke, converted, LSL's, comments let go, 5,000 lines", t.small_t, small_c, anchored_small);
        typed("a keystroke, converted, LSL's, comments let go, 50,000 lines", t.big_t, big_c, anchored_big);
    }

    TEST_CASE("altextdiff_bench: what each comparison says of some edits", "[altextdiff_bench][!benchmark]")
    {
        ll_test::skipUnlessOptimised();
        const Texts& t = texts();
        std::printf("\nWhat it says (5,000 lines)\n");
        std::printf("  %-44s %8s %8s %8s %8s %8s %8s\n", "", "changes", "out", "in", "paired", "words", "moved");
        ALTextDiff::Options by_grammar;
        if (const std::shared_ptr<const ALSyntaxGrammar> lsl = lslGrammar())
        {
            by_grammar.lexer = ALDiffLexer::lexerOf(std::make_shared<ALDiffLexer>(lsl));
        }
        says("ten edits", t.small_t, t.small10t);
        says("a thousand edits", t.small_t, t.small1kt);
        says("a function moved", t.small_t, joined(moved(t.small)));
        says("calls and braces reformatted", t.small_t, joined(reformatted(t.small, 200, 200)));
        {
            // A line taken out of the middle of lines changed: what pairing
            // by place gets wrong.
            std::vector<std::string> right = t.small;
            for (size_t i = 300; i < 306; ++i)
            {
                right[i] += " // edited";
            }
            right.erase(right.begin() + 302);
            says("one taken out among six changed", t.small_t, joined(right));
        }
        says("a thousand edits, LSL's grammar", t.small_t, t.small1kt, by_grammar);
        for (const auto& [algorithm, name] : { std::make_pair(ALTextDiff::Algorithm::Patience, "patience"), std::make_pair(ALTextDiff::Algorithm::Minimal, "minimal"),
                                               std::make_pair(ALTextDiff::Algorithm::Structural, "structural") })
        {
            ALTextDiff::Options by;
            by.algorithm = algorithm;
            says((std::string("a thousand edits, ") + name).c_str(), t.small_t, t.small1kt, by);
            says((std::string("a function moved, ") + name).c_str(), t.small_t, joined(moved(t.small)), by);
            says((std::string("calls and braces reformatted, ") + name).c_str(), t.small_t, joined(reformatted(t.small, 200, 200)), by);
        }
        std::fflush(stdout);
    }
}
