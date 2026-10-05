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
// lines, each change's lines paired and their words -- (ALDiffModel). A
// number is milliseconds per operation: the median of five samples, each
// as many passes as fit in fifty milliseconds (and at least one).
//
// After the times, what each comparison says of some edits a person
// makes -- a line changed, a block moved, a function reformatted, lines
// changed all over -- as counts: changes, lines marked, and words marked
// in lines paired. Those are not better or worse by being lower; they are
// read against the same rows from before a change to how texts are
// compared, to see that it did what it meant to and nothing else.
//
// An unoptimised build exits 125, which CTest reads as skipped.

#include "linden_common.h"

#include "aldifflexer.h"
#include "aldiffmodel.h"
#include "altextdiff.h"

#include "albigscript.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#ifndef LLUI_TEST_APP_DIR
#  define LLUI_TEST_APP_DIR ""
#endif

#if defined(LL_RELEASE)
namespace
{
    using clock = std::chrono::steady_clock;

    volatile size_t g_sink = 0;

    double ms_per_item(size_t count, const std::function<void()>& pass)
    {
        pass();
        double samples[5];
        for (double& sample : samples)
        {
            size_t          passes = 0;
            const auto      start  = clock::now();
            clock::duration elapsed{};
            do
            {
                pass();
                ++passes;
                elapsed = clock::now() - start;
            } while (elapsed < std::chrono::milliseconds(50));
            sample = std::chrono::duration<double, std::milli>(elapsed).count() / (double(passes) * double(count));
        }
        std::sort(samples, samples + 5);
        return samples[2];
    }

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

    void row(const char* name, double small, double big)
    {
        std::printf("  %-52s %10.3f %10.3f\n", name, small, big);
    }

    // What a comparison says, laid out: its changes, the lines marked on
    // each side, the lines paired as changed, and the words marked in them.
    void says(const char* name, const std::string& left, const std::string& right, const ALTextDiff::Options& options = ALTextDiff::Options())
    {
        ALDiffModel model;
        model.setLikeness(options.like);
        model.setLexer(options.lexer);
        model.setTexts(left, right);
        S32 removed = 0, added = 0, paired = 0, marks = 0;
        for (S32 line = 0; line < model.lineCount(ALDiffModel::Column::Left); ++line)
        {
            const ALDiffModel::Line& one = model.line(ALDiffModel::Column::Left, line);
            removed += one.kind == ALDiffModel::Kind::Removed;
            paired += one.sign == '~';
            marks += static_cast<S32>(one.words.size());
        }
        for (S32 line = 0; line < model.lineCount(ALDiffModel::Column::Right); ++line)
        {
            const ALDiffModel::Line& one = model.line(ALDiffModel::Column::Right, line);
            added += one.kind == ALDiffModel::Kind::Added;
            marks += static_cast<S32>(one.words.size());
        }
        std::printf("  %-36s %8d %8d %8d %8d %8d\n", name, model.changeCount(), removed, added, paired, marks);
    }
}
#endif // LL_RELEASE

int main(int, char**)
{
#if !defined(LL_RELEASE)
    std::printf("Skipped: an unoptimised build has no numbers worth reading\n");
    return 125;
#else
    const std::vector<std::string> small    = ALTextDiff::split(ll_test::bigLSL(5000));
    const std::vector<std::string> big      = ALTextDiff::split(ll_test::bigLSL(50000));
    const std::vector<std::string> small10  = edited(small, 10, 1);
    const std::vector<std::string> small1k  = edited(small, 1000, 2);
    const std::vector<std::string> big10    = edited(big, 10, 3);
    const std::vector<std::string> big1k    = edited(big, 1000, 4);
    const std::string              small_t  = joined(small);
    const std::string              big_t    = joined(big);
    const std::string              small10t = joined(small10);
    const std::string              small1kt = joined(small1k);
    const std::string              big10t   = joined(big10);
    const std::string              big1kt   = joined(big1k);

    std::printf("altextdiff_bench: comparing generated LSL scripts (ms per operation)\n");
    std::printf("\n  %-52s %10s %10s\n", "", "5,000", "50,000");

    std::printf("\nLines (ALTextDiff::lines)\n");
    const auto lines = [&](const std::vector<std::string>& left, const std::vector<std::string>& right, const ALTextDiff::Options& options) {
        return ms_per_item(1, [&] { g_sink = g_sink + ALTextDiff::lines(left, right, options).size(); });
    };
    {
        const ALTextDiff::Options histogram;
        row("histogram, ten edits", lines(small, small10, histogram), lines(big, big10, histogram));
        row("histogram, a thousand edits", lines(small, small1k, histogram), lines(big, big1k, histogram));
    }

    std::printf("\nLaid out (ALDiffModel: lines, pairs, words, rows)\n");
    // LSL's grammar, where the source tree has it, for words cut as code.
    std::string                            error;
    std::shared_ptr<const ALSyntaxGrammar> lsl = ALSyntaxGrammar::fromFile(std::string(LLUI_TEST_APP_DIR) + "/app_settings/syntax/lsl.xml", error);
    const auto lexer = [&lsl]() { return lsl ? ALDiffLexer::lexerOf(std::make_shared<ALDiffLexer>(lsl)) : ALTextDiff::lexer_t(); };
    const auto laid  = [&](const std::string& left, const std::string& right, bool grammar) {
        return ms_per_item(1, [&] {
            ALDiffModel model;
            if (grammar)
            {
                model.setLexer(lexer());
            }
            model.setTexts(left, right);
            g_sink = g_sink + static_cast<size_t>(model.rowCount(ALDiffModel::Layout::Sides));
        });
    };
    row("ten edits", laid(small_t, small10t, false), laid(big_t, big10t, false));
    row("a thousand edits", laid(small_t, small1kt, false), laid(big_t, big1kt, false));
    row("ten edits, words by LSL's grammar", laid(small_t, small10t, true), laid(big_t, big10t, true));
    row("a thousand edits, words by LSL's grammar", laid(small_t, small1kt, true), laid(big_t, big1kt, true));
    // A live comparison, the right typed in: a character put in a line
    // near the middle and compared again, then taken out and compared
    // again; per keystroke.
    const auto typed = [&](const std::string& left, const std::string& right, bool grammar = false) {
        ALDiffModel model;
        if (grammar)
        {
            model.setLexer(lexer());
        }
        model.setTexts(left, right);
        const size_t at   = right.find('\n', right.size() / 2);
        std::string  with = right;
        with.insert(at, "x");
        return ms_per_item(2, [&] {
            model.setRightText(with);
            model.setRightText(right);
            g_sink = g_sink + static_cast<size_t>(model.changeCount());
        });
    };
    row("a keystroke, ten edits", typed(small_t, small10t), typed(big_t, big10t));
    row("a keystroke, a thousand edits", typed(small_t, small1kt), typed(big_t, big1kt));
    row("a keystroke, a thousand edits, LSL's grammar", typed(small_t, small1kt, true), typed(big_t, big1kt, true));

    std::printf("\nWhat it says (5,000 lines)\n");
    std::printf("  %-36s %8s %8s %8s %8s %8s\n", "", "changes", "out", "in", "paired", "words");
    ALTextDiff::Options by_grammar;
    by_grammar.lexer = lexer();
    says("ten edits", small_t, small10t);
    says("a thousand edits", small_t, small1kt);
    says("a function moved", small_t, joined(moved(small)));
    says("calls and braces reformatted", small_t, joined(reformatted(small, 200, 200)));
    {
        // A line taken out of the middle of lines changed: what pairing by
        // place gets wrong.
        std::vector<std::string> right = small;
        for (size_t i = 300; i < 306; ++i)
        {
            right[i] += " // edited";
        }
        right.erase(right.begin() + 302);
        says("one taken out among six changed", small_t, joined(right));
    }
    says("a thousand edits, LSL's grammar", small_t, small1kt, by_grammar);

    std::printf("\n(checksum %zu)\n", static_cast<size_t>(g_sink));
    return 0;
#endif
}
