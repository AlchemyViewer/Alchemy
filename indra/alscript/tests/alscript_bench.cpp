/**
 * @file alscript_bench.cpp
 * @brief The script tools over big scripts: the preprocessor, the formatter,
 *        the optimizer, and one analysis of each language.
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

// What the script studio's tools cost over a big script, LSL and SLua side
// by side, with the grid's own definitions loaded: what the analysis thread
// and the preprocessor's thread do each time a script changes. A number is
// milliseconds per run: the median of five samples, each as many runs as
// fit in fifty milliseconds (and at least one).
//
// Each run is over a text that differs from the run before it -- a comment
// at its end comes and goes -- so that a tool that keeps what it made of
// the same text is measured doing the work, as it does for every edit.
//
// The output is a table, not a verdict; a number is read against the same
// row from another build, on the same quiet machine. An unoptimised build
// exits 125, which CTest reads as skipped, since its numbers would say
// nothing.

#include "linden_common.h"

#include "../allslservice.h"
#include "../allsloptimizer.h"
#include "../alluauservice.h"
#include "../almessagemap.h"
#include "../alpreprocessor.h"
#include "../alscriptformatter.h"
#include "../alscriptweight.h"

#include "albigscript.h"
#include "llfile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>

// Only an optimised build measures, and only it has a use for these: an
// unoptimised one would say they are unused.
#if defined(LL_RELEASE)
namespace
{
    using clock = std::chrono::steady_clock;

    constexpr int LINES = 5000;

    // Everything a row makes feeds this, so nothing is made for nothing.
    volatile size_t g_sink = 0;

    double ms_per_run(const std::function<void()>& run)
    {
        run();
        double samples[5];
        for (double& sample : samples)
        {
            size_t          runs  = 0;
            const auto      start = clock::now();
            clock::duration elapsed{};
            do
            {
                run();
                ++runs;
                elapsed = clock::now() - start;
            } while (elapsed < std::chrono::milliseconds(50));
            sample = std::chrono::duration<double, std::milli>(elapsed).count() / double(runs);
        }
        std::sort(samples, samples + 5);
        return samples[2];
    }

    // One language's script, in the two forms the runs go between.
    struct Script
    {
        std::string texts[2];
        size_t      next = 0;

        Script(std::string text, const char* comment)
        {
            texts[0] = text;
            texts[1] = std::move(text) + comment;
        }

        const std::string& text()
        {
            next ^= 1;
            return texts[next];
        }
    };

    void cell(double ms)
    {
        if (std::isnan(ms))
        {
            std::printf(" %10s", "-");
        }
        else
        {
            std::printf(" %10.3f", ms);
        }
    }

    void row(const char* name, double lsl, double slua)
    {
        std::printf("  %-52s", name);
        cell(lsl);
        cell(slua);
        std::printf("\n");
    }

    std::string readWhole(const std::string& path)
    {
        llifstream        in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    size_t errorsIn(const ALScriptProblems& problems)
    {
        return std::count_if(problems.begin(), problems.end(),
                             [](const ALScriptProblem& p) { return p.severity == ALScriptProblem::Severity::Error; });
    }

    // The LSL script with a macro in every helper, for the preprocessor
    // to expand: what a script written for it has.
    std::string withMacros(std::string text)
    {
        const std::string plain = "total += llStringLength(b) * 2;";
        const std::string macro = "total += TWICE(llStringLength(b));";
        for (size_t at = text.find(plain); at != std::string::npos; at = text.find(plain, at + macro.size()))
        {
            text.replace(at, plain.size(), macro);
        }
        return "#define TWICE(x) ((x) * 2)\n" + text;
    }
}
#endif // LL_RELEASE

int main(int, char**)
{
#if !defined(LL_RELEASE)
    std::printf("Skipped: an unoptimised build has no numbers worth reading\n");
    return 125;
#else
    const double NONE = std::nan("");

    // The grid's definitions: the builtins, which the optimizer reads
    // through the LSL service, and SLua's declarations.
    ALLSLService lsl;
    std::string  error;
    if (!lsl.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error))
    {
        std::printf("Skipped: the builtins did not load: %s\n", error.c_str());
        return 125;
    }
    ALLuauService luau;
    if (!luau.loadDefinitions(readWhole(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau"), error))
    {
        std::printf("Skipped: the SLua definitions did not load: %s\n", error.c_str());
        return 125;
    }

    Script lslScript(ll_test::bigLSL(LINES), "\n// again\n");
    Script luaScript(ll_test::bigSLua(LINES), "\n-- again\n");
    Script macroScript(withMacros(ll_test::bigLSL(LINES)), "\n// again\n");

    std::printf("alscript_bench: the script tools over generated scripts of %d lines (ms per run)\n", LINES);
    std::printf("\n  %-52s %10s %10s\n", "", "LSL", "SLua");
    std::printf("  %-52s %10zu %10zu\n", "bytes", lslScript.texts[0].size(), luaScript.texts[0].size());
    std::printf("  %-52s %10zu %10zu\n", "errors found (0 is right)", errorsIn(lsl.check(lslScript.texts[0], true)),
                errorsIn(luau.check(luaScript.texts[0])));
    std::printf("  %-52s %10d %10d\n", "the hover below finds its name (1 is right)",
                lsl.hover(lslScript.texts[0], ll_test::big_script_detail::linesIn(lslScript.texts[0]) - 12, 20).found,
                luau.hover(luaScript.texts[0], ll_test::big_script_detail::linesIn(luaScript.texts[0]) - 8, 16).found);

    std::printf("\nThe analysis thread's work\n");
    row("check", ms_per_run([&] { g_sink = g_sink + lsl.check(lslScript.text(), true).size(); }),
        ms_per_run([&] { g_sink = g_sink + luau.check(luaScript.text()).size(); }));
    row("a check job: check, outline, semantic tokens, hints",
        ms_per_run([&] {
            const std::string& text = lslScript.text();
            g_sink = g_sink + lsl.check(text, true).size() + lsl.outline(text).size() + lsl.semanticTokens(text).size() +
                     lsl.inlayHints(text, true).size();
        }),
        ms_per_run([&] {
            const std::string& text = luaScript.text();
            g_sink = g_sink + luau.check(text).size() + luau.outline(text).size() + luau.semanticTokens(text).size() +
                     luau.inlayHints(text, true, true).size();
        }));
    // An edit as the studio has it answered: the check job, then the
    // questions typing asks -- a completion, a hover, a completion again.
    row("an edit: the check job, then complete, hover, complete",
        ms_per_run([&] {
            const std::string& text = lslScript.text();
            const S32          last = ll_test::big_script_detail::linesIn(text) - 12;
            g_sink = g_sink + lsl.check(text, true).size() + lsl.outline(text).size() + lsl.semanticTokens(text).size() +
                     lsl.inlayHints(text, true).size() + lsl.hover(text, last, 20).label.size();
        }),
        ms_per_run([&] {
            const std::string& text = luaScript.text();
            const S32          last = ll_test::big_script_detail::linesIn(text) - 8;
            g_sink = g_sink + luau.check(text).size() + luau.outline(text).size() + luau.semanticTokens(text).size() +
                     luau.inlayHints(text, true, true).size() + luau.complete(text, last, 16).size() + luau.hover(text, last, 16).label.size() +
                     luau.complete(text, last, 16).size();
        }));
    // An expansion whose include is most of it: the names and hints of the
    // include's lines passed over, as the studio shows none of them.
    {
        std::string       big      = ll_test::bigLSL(LINES);
        const size_t      at       = big.find("default");
        const std::string library  = big.substr(0, at);
        const std::string expanded = library + big.substr(at);
        const S32         lines    = static_cast<S32>(std::count(library.begin(), library.end(), '\n'));
        lsl.check(expanded, true);
        const auto names = [&] { g_sink = g_sink + lsl.semanticTokens(expanded).size() + lsl.inlayHints(expanded, true).size(); };
        const double all = ms_per_run(names);
        lsl.setPassedOver({ { 0, lines - 1 } });
        const double own = ms_per_run(names);
        lsl.setPassedOver({});
        row("LSL names and hints of an expansion, all of it", all, NONE);
        row("  its include's lines passed over", own, NONE);
    }
    // A check with many problems, each offered its fixes: names misspelt,
    // locals nobody uses.
    {
        std::string troubled = ll_test::bigLSL(LINES);
        std::string body     = "troubled()\n{\n";
        for (int i = 0; i < 100; ++i)
        {
            body += "    integer unused" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
            body += "    llSya(0, \"x\");\n";
        }
        body += "}\n";
        troubled.insert(troubled.find("default"), body);
        size_t problems = 0;
        row("LSL check of a text with 200 problems", ms_per_run([&] { problems = lsl.check(troubled, true).size(); g_sink = g_sink + problems; }),
            NONE);
        std::printf("  %-52s %10zu\n", "  problems", problems);
    }
    // Two tabs in front in turn, neither typed in: each its own module,
    // found checked as it was left. (With one module for every tab, each
    // turn was a full check of each: twice the check row above.)
    {
        const std::string& one = luaScript.texts[0];
        const std::string& two = luaScript.texts[1];
        row("SLua: two tabs checked in turn, per turn", NONE, ms_per_run([&] {
            luau.setDocument("one");
            g_sink = g_sink + luau.check(one).size();
            luau.setDocument("two");
            g_sink = g_sink + luau.check(two).size();
        }));
        luau.setDocument("");
    }
    // Asked again of a text already checked: what the answer itself
    // costs, the check being the one in hand.
    {
        const std::string& text = luaScript.texts[0];
        luau.check(text);
        const S32 last = ll_test::big_script_detail::linesIn(text) - 8;
        row("SLua hints of a checked text", NONE, ms_per_run([&] { g_sink = g_sink + luau.inlayHints(text, true, true).size(); }));
        row("SLua refactors at a place of a checked text", NONE,
            ms_per_run([&] { g_sink = g_sink + luau.actions(text, last, 16, last, 16).size(); }));
    }
    row("hover at the last helper's call",
        ms_per_run([&] {
            const std::string& text = lslScript.text();
            g_sink = g_sink + lsl.hover(text, ll_test::big_script_detail::linesIn(text) - 12, 20).label.size();
        }),
        ms_per_run([&] {
            const std::string& text = luaScript.text();
            g_sink = g_sink + luau.hover(text, ll_test::big_script_detail::linesIn(text) - 8, 16).label.size();
        }));
    row("weigh (Mono; SLua bytecode)", ms_per_run([&] { g_sink = g_sink + ALScriptWeigh::mono(lslScript.text()).total; }),
        ms_per_run([&] { g_sink = g_sink + ALScriptWeigh::slua(luaScript.text()).total; }));

    std::printf("\nThe preprocessor's thread's work\n");
    ALPreprocessor::Options lslOptions;
    ALPreprocessor::Options luaOptions;
    luaOptions.lua = true;
    row("expand: no directives",
        ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(lslScript.text(), lslOptions).text.size(); }),
        ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(luaScript.text(), luaOptions).text.size(); }));
    row("expand: a macro in every helper",
        ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(macroScript.text(), lslOptions).text.size(); }), NONE);
    ALLSLOptimizer::Options optimizer;
    optimizer.notes = false;
    row("optimize (Mono)", ms_per_run([&] { g_sink = g_sink + ALLSLOptimizer::run(lslScript.text(), optimizer).text.size(); }),
        NONE);

    std::printf("\nTidying\n");
    ALScriptFormatter::Options lslFormat;
    ALScriptFormatter::Options luaFormat;
    luaFormat.lua = true;
    row("format the whole text", ms_per_run([&] { g_sink = g_sink + ALScriptFormatter::format(lslScript.text(), lslFormat).size(); }),
        ms_per_run([&] { g_sink = g_sink + ALScriptFormatter::format(luaScript.text(), luaFormat).size(); }));
    // One long line: a table or a list of a few thousand terms, which a
    // generated script can have.
    {
        std::string lsl_line = "default { state_entry() { integer n = 0";
        std::string lua_line = "local n = 0";
        for (int i = 0; i < 3000; ++i)
        {
            lsl_line += " + " + std::to_string(i) + "*-" + std::to_string(i);
            lua_line += " + " + std::to_string(i) + "*-" + std::to_string(i);
        }
        lsl_line += "; } }\n";
        lua_line += "\n";
        row("format one line of 6,000 operators", ms_per_run([&] { g_sink = g_sink + ALScriptFormatter::format(lsl_line, lslFormat).size(); }),
            ms_per_run([&] { g_sink = g_sink + ALScriptFormatter::format(lua_line, luaFormat).size(); }));
    }

    // A check's messages put into the viewer's words: a thousand, of the
    // kinds a script mid-edit has, most matching a row late or none.
    std::printf("\nMessages\n");
    {
        const char* said[] = { "Unknown global 'llSya'", "Type 'number' could not be converted into 'string'",
                               "Argument count mismatch. Function 'f' expects 2 arguments, but 3 are specified",
                               "Something the map has no row for at all" };
        const double ms    = ms_per_run([&] {
            ALMessageMap::Match match;
            for (int i = 0; i < 1000; ++i)
            {
                g_sink = g_sink + (ALMessageMap::luauError(said[i % 4], match) ? match.args.size() : 1);
            }
        });
        row("1,000 Luau errors keyed", NONE, ms);
    }

    std::printf("\n(checksum %zu)\n", static_cast<size_t>(g_sink));
    return 0;
#endif
}
