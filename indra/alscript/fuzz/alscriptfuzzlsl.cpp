/**
 * @file alscriptfuzzlsl.cpp
 * @brief libFuzzer on Script Studio's LSL analysis: the input is a script
 * pasted into the editor
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

#include "alscriptformatter.h"
#include "alscriptstack.h"
#include "alscriptweight.h"
#include "allslservice.h"
#include "allsltoslua.h"
#include "alluauservice.h"
#include "alpreprocessor.h"
#include "llerrorcontrol.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace
{
    // Once for the process, as the viewer has it before it checks a script:
    // Luau's flags set, the grid's builtins loaded, warnings quiet.
    ALLSLService& service()
    {
        static ALLSLService* made = []
        {
            LLError::initForApplication("", "", true);
            LLError::setDefaultLevel(LLError::LEVEL_ERROR);
            ALLuauService::setUpProcess();
            ALLSLService* lsl = new ALLSLService();
            std::string   error;
            lsl->loadBuiltins(std::string(AL_LSL_DEFINITIONS_DIR) + "/builtins.txt", error);
            return lsl;
        }();
        return *made;
    }

    // A position in the text for the questions asked at one: the line and
    // column of the middle byte.
    void middleOf(const std::string& text, S32& line, S32& column)
    {
        line   = 0;
        column = 0;
        for (size_t i = 0; i < text.size() / 2; ++i)
        {
            if (text[i] == '\n')
            {
                ++line;
                column = 0;
            }
            else
            {
                ++column;
            }
        }
    }
}

// The bytes as an LSL script, run through what the editor does with one as
// it is pasted in and checked, and what its commands do with it, on the
// stack the editor's analysis has (alscriptanalysislane.cpp). Longer than
// the about 256 KiB of source the grid takes is no script.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 262144)
    {
        return -1;
    }
    ALLSLService&     lsl = service();
    const std::string text(reinterpret_cast<const char*>(data), size);
    S32               line, column;
    middleOf(text, line, column);
    // The stages run, all of them unless ALSCRIPT_FUZZ_STAGES names some,
    // as a mask in the order below: for finding which a finding is in.
    static const unsigned stages = []
    {
        const char* asked = std::getenv("ALSCRIPT_FUZZ_STAGES");
        return asked ? static_cast<unsigned>(std::strtoul(asked, nullptr, 0)) : ~0u;
    }();
    alScriptOnLargeStack(
        [&]
        {
            if (stages & 1)
            {
                ALPreprocessor::run(text, ALPreprocessor::Options());
            }
            if (stages & 2)
            {
                lsl.check(text, true);
                lsl.check(text, false);
                lsl.outline(text);
                lsl.semanticTokens(text);
                lsl.inlayHints(text, true);
            }
            if (stages & 4)
            {
                lsl.symbols(text, line, column);
                lsl.hover(text, line, column);
                lsl.signature(text, line, column);
                lsl.references(text, line, column);
                lsl.actions(text, line, column, line, column);
            }
            if (stages & 8)
            {
                ALScriptWeigh::lso(text);
                ALScriptWeigh::mono(text);
            }
            if (stages & 16)
            {
                ALScriptWeigh::lslLuau(text);
            }
            if (stages & 32)
            {
                // As the studio formats, at its width (ALScriptFormatWidth).
                ALScriptFormatter::Options formatting;
                formatting.width = 120;
                ALScriptFormatter::format(text, formatting);
            }
            if (stages & 64)
            {
                ALLSLToSLua::convert(text);
            }
        });
    return 0;
}
