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

#include "../preprocessor/aldiskcache.h"
#include "../preprocessor/aldiskincludes.h"
#include "../lsl/allslservice.h"
#include "../lsl/optimizer/allsloptimizer.h"
#include "../luau/alluauservice.h"
#include "../core/almessagemap.h"
#include "../preprocessor/alpreprocessor.h"
#include "../core/alscriptformatter.h"
#include "../core/alscriptstack.h"
#include "../core/alscriptweight.h"

#include "albigscript.h"
#include "llfile.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

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

    // A scripter's include folder on disk, its `.lslrc` listing a library
    // folder under it, and a script three folders down that includes five
    // files: what the main thread reads each time that script is checked.
    struct IncludeTree
    {
        std::filesystem::path    root;
        std::string              folder;
        std::string              script;
        std::vector<std::string> names;

        IncludeTree()
        {
            namespace fs = std::filesystem;
            root         = fs::temp_directory_path() / ("alscript_bench_" + std::to_string(clock::now().time_since_epoch().count()));
            fs::create_directories(root / "includes" / "lib");
            fs::create_directories(root / "project" / "a" / "b");
            folder = (root / "includes").string();
            script = (root / "project" / "a" / "b" / "door.lsl").string();
            std::ofstream(root / "includes" / ".lslrc") << "{\"include\": [\"lib\"]}";
            for (int i = 0; i < 5; ++i)
            {
                const std::string name = "part" + std::to_string(i) + ".lsl";
                std::ofstream(root / "includes" / "lib" / name) << "integer part" << i << "() { return " << i << "; }\n";
                names.push_back(name);
            }
            std::ofstream(script) << "default { state_entry() { } }\n";
        }
        ~IncludeTree()
        {
            std::error_code ec;
            std::filesystem::remove_all(root, ec);
        }
    };

    // What the preprocessor's snapshot does for each include a script asks
    // for (ALScriptPreprocessor::candidatesFor, textOf): the folders
    // blessed afresh -- the scripter's, what their `.lslrc` lists, the
    // nearest `.lslrc` up from the script -- then each name looked for in
    // each folder, and the one found read.
    size_t includesUncached(const IncludeTree& tree)
    {
        size_t read = 0;
        for (const std::string& name : tree.names)
        {
            ALDiskIncludes blessed;
            blessed.bless(tree.folder);
            for (const std::string& listed : ALDiskIncludes::lslrcFolders(tree.folder))
            {
                blessed.blessFromConfig(listed, tree.folder);
            }
            const std::string from_dir = std::filesystem::path(tree.script).parent_path().string();
            std::string       config_folder;
            for (const std::string& listed : ALDiskIncludes::nearestLslrcFolders(from_dir, &config_folder))
            {
                blessed.blessFromConfig(listed, config_folder);
            }
            std::vector<std::string> dirs{ from_dir };
            for (const std::string& folder : blessed.folders())
            {
                dirs.push_back(folder);
            }
            bool found = false;
            for (const std::string& dir : dirs)
            {
                for (const std::string& file : ALDiskIncludes::namesFor(name, false, false))
                {
                    const std::optional<std::string> real = blessed.admits((std::filesystem::path(dir) / file).string());
                    std::string                      text;
                    if (!found && real && ALDiskIncludes::readOrdinary(*real, text))
                    {
                        read += text.size();
                        found = true;
                    }
                }
            }
        }
        return read;
    }

    // The same through what the disk said a moment ago, as the snapshot
    // asks it now (ALScriptPreprocessor::blessedFor, textOf): each check
    // within a couple of seconds of the last.
    size_t includesKept(const IncludeTree& tree, ALDiskCache& cache)
    {
        size_t                         read     = 0;
        const std::vector<std::string> own{ tree.folder };
        const std::string              from_dir = std::filesystem::path(tree.script).parent_path().string();
        for (const std::string& name : tree.names)
        {
            ALDiskCache::Blessed&    blessed = cache.blessed(own, true, from_dir, {}, 1, 1.0);
            std::vector<std::string> dirs{ from_dir };
            for (const std::string& folder : blessed.includes.folders())
            {
                dirs.push_back(folder);
            }
            bool found = false;
            for (const std::string& dir : dirs)
            {
                for (const std::string& file : ALDiskIncludes::namesFor(name, false, false))
                {
                    const std::optional<std::string> real = cache.admits(blessed, (std::filesystem::path(dir) / file).string());
                    std::string                      text;
                    if (!found && real && cache.read(*real, text))
                    {
                        read += text.size();
                        found = true;
                    }
                }
            }
        }
        return read;
    }

    void row(const char* name, double lsl, double slua)
    {
        std::printf("  %-52s", name);
        cell(lsl);
        cell(slua);
        std::printf("\n");
    }

    // A script being typed in: a line put into the big script's main, in
    // two forms one character apart, and the caret in each `back` bytes
    // before the line's end. Each run is over the other form, as a
    // scripter typing the character and deleting it has them, so every
    // question is about a text not checked yet.
    struct Typing
    {
        std::string texts[2];
        S32         line = 0;
        S32         columns[2]{};
        size_t      next = 0;

        Typing(const std::string& script, const std::string& one, const std::string& two, S32 back = 0)
        {
            const size_t at = script.rfind("    print(name");
            line            = static_cast<S32>(std::count(script.begin(), script.begin() + at, '\n'));
            texts[0]        = script.substr(0, at) + one + "\n" + script.substr(at);
            texts[1]        = script.substr(0, at) + two + "\n" + script.substr(at);
            columns[0]      = static_cast<S32>(one.size()) - back;
            columns[1]      = static_cast<S32>(two.size()) - back;
        }

        // The other form; column() is then the caret in it.
        const std::string& text()
        {
            next ^= 1;
            return texts[next];
        }
        S32 column() const { return columns[next]; }
    };

    // A row of SLua alone over the scripts of each size.
    void sizes(const char* name, const std::vector<double>& ms)
    {
        std::printf("  %-52s", name);
        for (double one : ms)
        {
            cell(one);
        }
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
    // A small script that requires a big module, typed in: the bundle
    // checked whole at each edit, against the script apart from the module
    // (ALPreprocessor::Options::apart), where the module stays checked.
    {
        const std::string module = luaScript.texts[0] + "\nreturn {}\n";
        ALPreprocessor::Options options;
        options.lua     = true;
        options.apart   = true;
        options.resolve = [&module](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
            if (ask.name != "big")
            {
                return ALPreprocessor::Found::No;
            }
            out.text = module;
            out.name = "big";
            out.path = "disk:/big.luau";
            return ALPreprocessor::Found::Yes;
        };
        const ALPreprocessor::Result made   = ALPreprocessor::run("local big = require(\"big\")\nprint(big)\n", options);
        const std::string            bundle = made.text;
        ALLuauService::Modules       modules;
        for (const ALPreprocessor::Result::Piece& piece : made.apart.modules)
        {
            modules.modules.push_back({ piece.key, piece.text });
        }
        for (const ALPreprocessor::Result::Resolved& resolved : made.resolved)
        {
            modules.reaches.push_back({ resolved.from, resolved.name, resolved.path });
        }
        int edits = 0;
        luau.setDocument("bundled");
        const double whole = ms_per_run([&] {
            g_sink = g_sink + luau.check(bundle + "-- " + std::to_string(++edits) + "\n").size();
        });
        luau.setDocument("apart");
        luau.setModules(modules);
        luau.check(made.apart.script.text);
        const double apart = ms_per_run([&] {
            g_sink = g_sink + luau.check(made.apart.script.text + "-- " + std::to_string(++edits) + "\n").size();
        });
        luau.setModules({});
        luau.setDocument("");
        row("SLua: an edit of a script requiring a big module, bundled", NONE, whole);
        row("  the script apart, the module kept checked", NONE, apart);
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

    // What running a job on a stack as deep as a script needs costs,
    // before the job itself: nothing to do on it.
    row("a job on the large stack, doing nothing", ms_per_run([] { alScriptOnLargeStack([] { g_sink = g_sink + 1; }); }), NONE);

    // What typing asks of SLua, under each of Luau's solvers and over
    // scripts of three sizes: an edit of one character, then the question
    // typing asks there -- a completion as a name is typed, signature help
    // in a call, a hover after either. These are what the studio waits on
    // at a keystroke, where the check job waits out its settle.
    for (const bool new_solver : { false, true })
    {
        ALLuauService typing;
        typing.setNewSolver(new_solver, error);
        typing.loadDefinitions(readWhole(std::string(AL_LSL_DEFINITIONS_DIR) + "/secondlife.d.luau"), error);
        std::vector<double> complete, again, signature, hover, expanded;
        int                 found = 0;
        for (const int lines : { 1000, 5000, 20000 })
        {
            const std::string script = ll_test::bigSLua(lines);
            Typing            word(script, "    local x = helper1", "    local x = helper12");
            Typing            call(script, "    helper1()", "    helper12()", 1);
            if (lines == 1000)
            {
                const std::vector<ALScriptCompletion> offered = typing.complete(word.texts[1], word.line, word.columns[1]);
                found += std::any_of(offered.begin(), offered.end(), [](const ALScriptCompletion& one) { return one.text == "helper12"; });
                found += typing.signature(call.texts[1], call.line, call.columns[1]).found;
                found += typing.hover(word.texts[1], word.line, word.columns[1] - 2).found;
            }
            complete.push_back(ms_per_run([&] {
                const std::string& text = word.text();
                g_sink                  = g_sink + typing.complete(text, word.line, word.column()).size();
            }));
            // The same asked again of the text it was asked of: what the
            // answer costs, with nothing to check.
            again.push_back(ms_per_run([&] { g_sink = g_sink + typing.complete(word.texts[1], word.line, word.columns[1]).size(); }));
            signature.push_back(ms_per_run([&] {
                const std::string& text = call.text();
                g_sink                  = g_sink + typing.signature(text, call.line, call.column()).parameters.size();
            }));
            hover.push_back(ms_per_run([&] {
                const std::string& text = word.text();
                g_sink                  = g_sink + typing.hover(text, word.line, word.column() - 2).label.size();
            }));
            // A script the preprocessor expands, as one with a directive is:
            // each question waits for the expansion of its text, then is
            // asked of what it made.
            {
                ALPreprocessor::Options options;
                options.lua = true;
                Typing directed("--#define LIMIT 100\n" + script, "    local x = helper1", "    local x = helper12");
                S32    made_lines[2]{};
                for (int form = 0; form < 2; ++form)
                {
                    const std::string made = ALPreprocessor::run(directed.texts[form], options).text;
                    const size_t      at   = made.find("    local x = helper1");
                    made_lines[form]       = static_cast<S32>(std::count(made.begin(), made.begin() + at, '\n'));
                }
                expanded.push_back(ms_per_run([&] {
                    const std::string&           text = directed.text();
                    const ALPreprocessor::Result made = ALPreprocessor::run(text, options);
                    g_sink = g_sink + typing.complete(made.text, made_lines[directed.next], directed.column()).size();
                }));
            }
        }
        std::printf("\nSLua typing, the %s solver: an edit of one character, then the question\n\n", new_solver ? "new" : "old");
        std::printf("  %-52s %10s %10s %10s\n", "lines", "1,000", "5,000", "20,000");
        std::printf("  %-52s %10d\n", "the questions below answer (3 is right)", found);
        sizes("complete after an edit", complete);
        sizes("  asked again of the same text", again);
        sizes("signature help in a call after an edit", signature);
        sizes("hover after an edit", hover);
        sizes("complete after an edit of a script expanded", expanded);
    }

    // What the main thread does for a check before the preprocessor's
    // thread has anything to do: the includes found on disk and read.
    std::printf("\nThe main thread's include work, a snapshot of a script with five disk includes\n");
    {
        const IncludeTree tree;
        row("find and read five disk includes, asking the disk", ms_per_run([&] { g_sink = g_sink + includesUncached(tree); }), NONE);
        ALDiskCache cache;
        row("  what it said a moment ago kept", ms_per_run([&] { g_sink = g_sink + includesKept(tree, cache); }), NONE);
    }

    std::printf("\nThe preprocessor's thread's work\n");
    ALPreprocessor::Options lslOptions;
    ALPreprocessor::Options luaOptions;
    luaOptions.lua = true;
    row("expand: no directives",
        ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(lslScript.text(), lslOptions).text.size(); }),
        ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(luaScript.text(), luaOptions).text.size(); }));
    row("expand: a macro in every helper",
        ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(macroScript.text(), lslOptions).text.size(); }), NONE);
    // A library of helpers behind classic include guards, included fifty
    // times, as headers including one another do: its body is wanted once.
    {
        std::string header = "#ifndef GUARDED_LSL\n#define GUARDED_LSL\n";
        for (int i = 0; i < 400; ++i)
        {
            header += "integer helper" + std::to_string(i) + "(integer x) { return x + " + std::to_string(i) + "; }\n";
        }
        header += "#endif\n";
        std::string script;
        for (int i = 0; i < 50; ++i)
        {
            script += "#include \"guarded.lsl\"\n";
        }
        script += "default { state_entry() { llOwnerSay((string)helper1(2)); } }\n";
        ALPreprocessor::Options guarded = lslOptions;
        guarded.resolve                 = [&header](const ALPreprocessor::Ask&, ALPreprocessor::Include& out) {
            out.path = "disk:/guarded.lsl";
            out.name = "guarded.lsl";
            out.text = header;
            return ALPreprocessor::Found::Yes;
        };
        row("expand: a guarded header included 50 times", ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(script, guarded).text.size(); }),
            NONE);
    }
    // Firestorm's transforms over a script that nests: loops in loops
    // with break and continue, a switch in each, each a level of the
    // transforms' descent.
    {
        std::string nested;
        for (int f = 0; f < 200; ++f)
        {
            const std::string n = std::to_string(f);
            nested += "integer nest" + n + "(integer n)\n{\n    integer total = 0;\n    integer i;\n    for (i = 0; i < n; ++i)\n    {\n"
                      "        integer j;\n        for (j = 0; j < i; ++j)\n        {\n            if (j == 3) continue;\n"
                      "            switch (j)\n            {\n                case 1: total = total + 1; break;\n"
                      "                case 2: { while (total > 9) { total = total - 2; if (total == 5) break; } } break;\n"
                      "                default: total = total + j;\n            }\n"
                      "            do { total = total - 1; if (total < 0) break; } while (total > 50);\n"
                      "            if (total > 100) break;\n        }\n    }\n    return total + " + n + ";\n}\n";
        }
        nested += "default { state_entry() { llOwnerSay((string)nest0(3)); } }\n";
        ALPreprocessor::Options transformed = lslOptions;
        transformed.extensions              = true;
        transformed.switches                = true;
        if (const ALPreprocessor::Result r = ALPreprocessor::run(nested, transformed); !r.problems.empty() || !r.usedSwitches || !r.usedExtensions)
        {
            std::printf("  (the nested script's transforms did not run cleanly: %zu problems)\n", r.problems.size());
        }
        row("expand, extensions and switches: 200 helpers that nest", ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(nested, transformed).text.size(); }),
            NONE);
    }
    // What a save does past the expansion: the optimizer with its notes,
    // each offered as a change, then the compression, the maps composed.
    {
        ALPreprocessor::Options saving = lslOptions;
        saving.optimize                = true;
        saving.compress                = true;
        row("expand, optimize with notes, compress (a save)",
            ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(macroScript.text(), saving).problems.size(); }), NONE);
        // One line a macro makes long -- a list of three thousand calls of
        // it, as generated data is -- saved, and places all along it read
        // back to the source, as the checker's colours and problems are.
        std::string long_line = "#define P(x) ((x) + 1)\ndefault { state_entry() { list l = [";
        for (int i = 0; i < 3000; ++i)
        {
            long_line += (i ? ", P(" : "P(") + std::to_string(i) + ")";
        }
        long_line += "]; llOwnerSay((string)llGetListLength(l)); } }\n";
        row("expand: one line of 3,000 macro calls", ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(long_line, lslOptions).text.size(); }),
            NONE);
        row("a save of one line of 3,000 macro calls", ms_per_run([&] { g_sink = g_sink + ALPreprocessor::run(long_line, saving).text.size(); }),
            NONE);
        // The optimizer alone, over what the expansion made: most of a
        // save's time.
        const std::string long_expanded = ALPreprocessor::run(long_line, lslOptions).text;
        row("  optimize it alone",
            ms_per_run([&] { g_sink = g_sink + ALLSLOptimizer::run(long_expanded, ALLSLOptimizer::Options()).text.size(); }), NONE);
        const ALPreprocessor::Result     made = ALPreprocessor::run(long_line, lslOptions);
        std::vector<std::pair<S32, S32>> places;
        S32                              line = 0, column = 0;
        for (size_t i = 0; i < made.text.size(); ++i, ++column)
        {
            if (made.text[i] == '\n')
            {
                ++line;
                column = -1;
            }
            else if (i % 64 == 0)
            {
                places.emplace_back(line, column);
            }
        }
        row("  every 64th byte of its expansion read back", ms_per_run([&] {
                for (const auto& [at_line, at_column] : places)
                {
                    g_sink = g_sink + size_t(made.map.toSource(at_line, at_column).line);
                }
            }),
            NONE);
    }
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
