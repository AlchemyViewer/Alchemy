/**
 * @file alscriptfuzzformat.cpp
 * @brief libFuzzer on Script Studio's formatter: the input is a script and
 * the options it is formatted with
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

#include <fuzzer/FuzzedDataProvider.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    // What the studio needs of the formatter, broken as a crash, so that
    // libFuzzer keeps the input: it takes formatEach's lines one for one
    // (alfloaterscriptstudio.cpp), and a text written out much longer than
    // the text was came from something that grows faster than it does.
    void require(bool held, const char* what)
    {
        if (!held)
        {
            std::fprintf(stderr, "alscript_fuzz_format: %s\n", what);
            std::abort();
        }
    }
}

// The input's last bytes choose the options and the lines asked for, the
// rest is the script: formatted whole, line for line, a few lines alone,
// and its strings' breaks found, as the studio's Format does on the main
// thread. Longer than the about 256 KiB of source the grid takes is no
// script.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 262144 + 16)
    {
        return -1;
    }
    FuzzedDataProvider         in(data, size);
    ALScriptFormatter::Options options;
    options.lua           = in.ConsumeBool();
    options.indent        = in.ConsumeIntegralInRange<S32>(-4, 20);
    options.tabs          = in.ConsumeBool();
    options.maxBlankLines = in.ConsumeIntegralInRange<S32>(-1, 4);
    options.spacing       = in.ConsumeBool();
    options.braceSpaces   = in.ConsumeBool();
    options.width         = in.ConsumeBool() ? 0 : in.ConsumeIntegralInRange<S32>(-1, 200);
    const S32         from = in.ConsumeIntegralInRange<S32>(-2, 64);
    const S32         to   = in.ConsumeIntegralInRange<S32>(-2, 64);
    const std::string text = in.ConsumeRemainingBytesAsString();
    const size_t      lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1;

    const std::string whole = ALScriptFormatter::format(text, options);
    require(whole.size() <= 600 * text.size() + 4096, "format wrote out far more than it was given");
    const std::vector<std::string> each = ALScriptFormatter::formatEach(text, options, 0, static_cast<S32>(lines) - 1);
    require(each.size() == lines, "formatEach gave a line count other than the text's");
    const std::vector<std::string> some = ALScriptFormatter::formatEach(text, options, from, to);
    require(some.size() == lines, "formatEach of some lines gave a line count other than the text's");
    ALScriptFormatter::formatLines(text, options, from, to);
    require(ALScriptFormatter::breaksInStrings(text, options.lua).size() == lines, "breaksInStrings gave a line count other than the text's");
    return 0;
}
