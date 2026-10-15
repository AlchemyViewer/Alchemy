/**
 * @file alscripthostile_test.cpp
 * @brief Script Studio's analysis of scripts written to break a compiler
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

#include "../core/alscriptformatter.h"
#include "../core/alscriptstack.h"
#include "../core/alscriptweight.h"
#include "../lsl/allsldepth.h"
#include "../lsl/allslservice.h"
#include "../lsl/allsltoslua.h"
#include "../preprocessor/alpreprocessor.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    // The scripts that broke LL's compiler or VM (indra/lscript/tests/
    // regressions): a NUL in a literal, a constant past a float's range, a
    // block or a chain of operators as long as the grid takes; and those
    // that broke the studio's own (tests/regressions). A script is pasted
    // into the editor from anywhere, so the editor meets them all.
    std::vector<std::filesystem::path> hostileScripts(bool luau = false)
    {
        std::vector<std::filesystem::path> scripts;
        for (const char* dir : { AL_LSCRIPT_REGRESSION_DIR, AL_ALSCRIPT_TEST_DIR "/regressions" })
        {
            for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(dir))
            {
                if (item.path().extension() == ".lsl" || (luau && item.path().extension() == ".luau"))
                {
                    scripts.push_back(item.path());
                }
            }
        }
        std::sort(scripts.begin(), scripts.end());
        return scripts;
    }

    std::string bytesOf(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // What the editor runs on an LSL script as it is pasted in and checked,
    // and what its commands run on it, each on the stack the editor's
    // analysis has (alscriptanalysislane.cpp).
    TEST_CASE("alscripthostile: the editor's analysis of each script that broke LL's compiler finishes", "[alscripthostile]")
    {
        ALLSLService service;
        std::string  error;
        REQUIRE(service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error));

        const std::vector<std::filesystem::path> scripts = hostileScripts();
        REQUIRE_FALSE(scripts.empty());
        for (const std::filesystem::path& path : scripts)
        {
            INFO(path.filename().string());
            const std::string text = bytesOf(path);
            REQUIRE_FALSE(text.empty());
            bool finished = false;
            alScriptOnLargeStack(
                [&]
                {
                    ALPreprocessor::run(text, ALPreprocessor::Options());
                    service.check(text, true);
                    service.check(text, false);
                    service.outline(text);
                    service.semanticTokens(text);
                    service.inlayHints(text, true);
                    service.symbols(text, 8, 8);
                    service.hover(text, 8, 8);
                    ALScriptWeigh::lso(text);
                    ALScriptWeigh::mono(text);
                    ALScriptWeigh::lslLuau(text);
                    ALScriptFormatter::format(text, ALScriptFormatter::Options());
                    ALLSLToSLua::convert(text);
                    finished = true;
                });
            CHECK(finished);
        }
    }

    // A chain of terms as deep as it is long: past the bound, reported where
    // it goes too deep and walked by nothing; within it, analyzed as ever.
    TEST_CASE("alscripthostile: a script nested past the depth bound is reported and not walked", "[alscripthostile]")
    {
        ALLSLService service;
        std::string  error;
        REQUIRE(service.loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error));
        const auto chainOf = [](int terms)
        {
            std::string text = "default\n{\n    state_entry()\n    {\n        integer x = 1";
            for (int i = 1; i < terms; ++i)
            {
                text += "+1";
            }
            return text + ";\n        llSay(0, (string)x);\n    }\n}\n";
        };
        const auto tooDeep = [](const ALScriptProblems& problems)
        {
            for (const ALScriptProblem& problem : problems)
            {
                if (problem.key == "LSLTooDeep")
                {
                    return &problem;
                }
            }
            return static_cast<const ALScriptProblem*>(nullptr);
        };

        alScriptOnLargeStack(
            [&]
            {
                const std::string      deep     = chainOf(ALLSLDepth::LIMIT + 100);
                const ALScriptProblems problems = service.check(deep, true);
                const ALScriptProblem* said     = tooDeep(problems);
                REQUIRE(said);
                CHECK(said->severity == ALScriptProblem::Severity::Error);
                CHECK(said->args == std::vector<std::string>{ std::to_string(ALLSLDepth::LIMIT) });
                CHECK(said->line == 4);
                CHECK_FALSE(service.parsed());
                CHECK(ALScriptWeigh::lso(deep).error == "it nests too deeply");
                CHECK(ALScriptWeigh::lslLuau(deep).error == "it nests too deeply");
                CHECK_FALSE(ALLSLToSLua::convert(deep).problems.empty());

                const std::string shallow = chainOf(ALLSLDepth::LIMIT - 100);
                CHECK_FALSE(tooDeep(service.check(shallow, true)));
                CHECK(service.parsed());
                CHECK(ALScriptWeigh::lso(shallow).error.empty());
            });
    }

    // Each of them formatted as the studio formats, on the main thread
    // (alfloaterscriptstudio.cpp), with the options it may be given: its
    // own, tabs, the narrowest width with the widest indent, and an indent
    // out of range. The studio takes formatEach's lines one for one, and
    // what is written out grows with the text, not with how deep it nests.
    TEST_CASE("alscripthostile: the formatter keeps each hostile script's lines and its size in bounds", "[alscripthostile]")
    {
        std::vector<ALScriptFormatter::Options> optionSets(5);
        optionSets[1].width = 120;
        optionSets[2].width = 120;
        optionSets[2].tabs  = true;
        optionSets[3].width  = 1;
        optionSets[3].indent = 16;
        optionSets[4].indent = -5;
        optionSets[4].width  = 120;
        for (const std::filesystem::path& path : hostileScripts(true))
        {
            INFO(path.filename().string());
            const std::string text  = bytesOf(path);
            const size_t      lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
            for (size_t o = 0; o < optionSets.size(); ++o)
            {
                INFO("options " << o);
                ALScriptFormatter::Options options = optionSets[o];
                options.lua                        = path.extension() == ".luau";
                const std::string whole            = ALScriptFormatter::format(text, options);
                CHECK(whole.size() <= 600 * text.size() + 4096);
                const std::vector<std::string> each = ALScriptFormatter::formatEach(text, options, 0, static_cast<S32>(lines) - 1);
                CHECK(each.size() == lines);
                ALScriptFormatter::formatLines(text, options, static_cast<S32>(lines / 3), static_cast<S32>(lines / 2));
                CHECK(ALScriptFormatter::breaksInStrings(text, options.lua).size() == lines);
            }
        }
    }
}
