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

#include "../test/lltut.h"

// llui reaches the viewer for this one, and on Linux the link pulls the
// object that calls it. Nothing under test goes near it.
class LLAvatarName;
const std::string gAnalyzersTestAnonName("Anon");
const std::string& rlvGetAnonym(const LLAvatarName& av_name)
{
    return gAnalyzersTestAnonName;
}

namespace tut
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

    typedef test_group<alscriptanalyzers_data> alscriptanalyzers_group;
    typedef alscriptanalyzers_group::object    alscriptanalyzers_object;
    alscriptanalyzers_group                    alscriptanalyzers_instance("alscriptanalyzers");

    template<> template<>
    void alscriptanalyzers_object::test<1>()
    {
        set_test_name("an LSL check says whether its text parsed; a weigh after it, which asks the compiler and not the service, says nothing of another text's");
        ALLSLAnalyzer                     lsl;
        const ALScriptAnalysis::Request   broken = request(ALScriptAnalysis::Kind::Check, "default { state_entry() { llSay(0, \"x\" } }\n");
        ALScriptAnalysis::Result          checked;
        lsl.answer(broken, *broken.text, setup, checked);
        ensure("builtins loaded: " + checked.definitionsError, checked.definitionsError.empty());
        ensure("the broken text did not parse", !checked.parsed);
        ALScriptAnalysis::Request weighing = request(ALScriptAnalysis::Kind::Weigh, "default { state_entry() { llSay(0, \"x\"); } }\n");
        weighing.targets                   = { ALScriptWeight::Target::Mono };
        ALScriptAnalysis::Result weighed;
        lsl.answer(weighing, *weighing.text, setup, weighed);
        ensure_equals("weighed", weighed.weights.size(), size_t(1));
        ensure("not the check's parse", weighed.parsed);
    }

    template<> template<>
    void alscriptanalyzers_object::test<2>()
    {
        set_test_name("a shape: what a text declares from its parse, LSL's states and events, SLua's functions with no types and nothing loaded for them");
        ALLSLAnalyzer                   lsl;
        const ALScriptAnalysis::Request shaped = request(ALScriptAnalysis::Kind::Shape, "go() {}\ndefault {\n  state_entry() { go(); }\n}\n");
        ALScriptAnalysis::Result        lsl_shape;
        lsl.answer(shaped, *shaped.text, setup, lsl_shape);
        ensure_equals("LSL: the function, the state, its event", lsl_shape.outline.size(), size_t(3));
        ensure("the event in its state", lsl_shape.outline[1].kind == ALScriptSymbolKind::State && lsl_shape.outline[2].name == "state_entry" &&
                                             lsl_shape.outline[2].depth == 1);
        ensure("nothing else answered", lsl_shape.problems.empty());

        ALLuauAnalyzer            luau;
        ALScriptAnalysis::Request slua = request(ALScriptAnalysis::Kind::Shape, "local function half(n: number)\n  return n / 2\nend\n");
        slua.lua                       = true;
        ALScriptAnalysis::Result luau_shape;
        luau.answer(slua, *slua.text, ALScriptAnalyzer::Setup(), luau_shape);
        ensure("SLua: the function, untyped", luau_shape.outline.size() == 1 && luau_shape.outline[0].name == "half" && luau_shape.outline[0].detail.empty());
        ensure("no definitions asked for", luau_shape.definitionsError.empty());
    }
}
