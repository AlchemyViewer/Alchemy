/**
 * @file alscriptanalyzers_test.cpp
 * @brief Each language's analyzer, asked as the analysis thread asks it.
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

#include "../alscriptanalyzers.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct alscriptanalyzers_data
    {
        ALScriptAnalyzer::Setup setup;

        alscriptanalyzers_data() { setup.lslPath = std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt"; }

        static ALScriptAnalysis::Request request(ALScriptAnalysis::Kind kind, const std::string& text)
        {
            ALScriptAnalysis::Request out;
            out.kind = kind;
            out.id   = "a";
            out.text = std::make_shared<const std::string>(text);
            return out;
        }
    };

    TEST_CASE_METHOD(alscriptanalyzers_data, "alscriptanalyzers: an LSL check reports its parse and a later weigh reports its own", "[alscriptanalyzers]")
    {
        ALLSLAnalyzer                     lsl;
        const ALScriptAnalysis::Request   broken = request(ALScriptAnalysis::Kind::Check, "default { state_entry() { llSay(0, \"x\" } }\n");
        ALScriptAnalysis::Result          checked;
        lsl.answer(broken, *broken.text, setup, checked);
        UNSCOPED_INFO("builtins loaded: " << checked.definitionsError);
        REQUIRE(checked.definitionsError.empty());
        CHECK_FALSE(checked.parsed);
        ALScriptAnalysis::Request weighing = request(ALScriptAnalysis::Kind::Weigh, "default { state_entry() { llSay(0, \"x\"); } }\n");
        weighing.targets                   = { ALScriptWeight::Target::Mono };
        ALScriptAnalysis::Result weighed;
        lsl.answer(weighing, *weighing.text, setup, weighed);
        REQUIRE(weighed.weights.size() == size_t(1));
        UNSCOPED_INFO("not the check's parse");
        CHECK(weighed.parsed);
    }

    TEST_CASE_METHOD(alscriptanalyzers_data, "alscriptanalyzers: a shape outlines LSL states and events and untyped SLua functions", "[alscriptanalyzers]")
    {
        ALLSLAnalyzer                   lsl;
        const ALScriptAnalysis::Request shaped = request(ALScriptAnalysis::Kind::Shape, "go() {}\ndefault {\n  state_entry() { go(); }\n}\n");
        ALScriptAnalysis::Result        lsl_shape;
        lsl.answer(shaped, *shaped.text, setup, lsl_shape);
        UNSCOPED_INFO("LSL: the function, the state, its event");
        REQUIRE(lsl_shape.outline.size() == size_t(3));
        {
            INFO("the function first");
            CHECK(lsl_shape.outline[0].kind == ALScriptSymbolKind::Function);
            CHECK(lsl_shape.outline[0].name == "go");
        }
        {
            INFO("the event in its state");
            CHECK(lsl_shape.outline[1].kind == ALScriptSymbolKind::State);
            CHECK(lsl_shape.outline[2].name == "state_entry");
            CHECK(lsl_shape.outline[2].depth == 1);
        }
        UNSCOPED_INFO("nothing else answered");
        CHECK(lsl_shape.problems.empty());

        ALLuauAnalyzer            luau;
        ALScriptAnalysis::Request slua = request(ALScriptAnalysis::Kind::Shape, "local function half(n: number)\n  return n / 2\nend\n");
        slua.lua                       = true;
        ALScriptAnalysis::Result luau_shape;
        luau.answer(slua, *slua.text, ALScriptAnalyzer::Setup(), luau_shape);
        {
            INFO("SLua: the function, untyped");
            REQUIRE(luau_shape.outline.size() == 1);
            CHECK(luau_shape.outline[0].name == "half");
            CHECK(luau_shape.outline[0].detail.empty());
        }
        UNSCOPED_INFO("no definitions asked for");
        CHECK(luau_shape.definitionsError.empty());
    }
}
