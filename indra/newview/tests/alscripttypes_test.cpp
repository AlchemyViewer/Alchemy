/**
 * @file alscripttypes_test.cpp
 * @brief Tests for what a compile's result says, read back to the source.
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

#include "../alscripttypes.h"
#include "alpreprocessor.h"
#include "alsourcemap.h"
#include <catch2/catch_test_macros.hpp>

namespace
{
struct alscripttypes_data
{
    static ALScriptDiagnostic said(S32 line, S32 column, const std::string& message)
    {
        ALScriptDiagnostic out;
        out.line      = line;
        out.column    = column;
        out.hasColumn = true;
        out.level     = "ERROR";
        out.message   = message;
        return out;
    }
};

TEST_CASE_METHOD(alscripttypes_data, "alscripttypes: inSource maps diagnostics back through the envelope and the source map", "[alscripttypes]")
{
    ALPreprocessor::Options options;
    options.fileName = "main.lsl";
    options.resolve  = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
        out.name = ask.name;
        out.text = "integer a;\ninteger b;\n";
        return ALPreprocessor::Found::Yes;
    };
    const ALPreprocessor::Result expanded = ALPreprocessor::run("#include \"lib.lsl\"\ndefault\n{\nstate_entry() { x; }\n}\n", options);
    REQUIRE(expanded.problems.empty());
    // The lines of the expansion that hold each, and the column of `x`.
    S32 entry = -1, second = -1, column = -1, line = 0;
    for (size_t at = 0, next; at < expanded.text.size(); at = next + 1, ++line)
    {
        next                    = expanded.text.find('\n', at);
        const std::string piece = expanded.text.substr(at, next == std::string::npos ? std::string::npos : next - at);
        if (piece.find("state_entry") != std::string::npos)
        {
            entry  = line;
            column = static_cast<S32>(piece.find("x;"));
        }
        second = piece.find("integer b") != std::string::npos ? line : second;
        if (next == std::string::npos)
        {
            break;
        }
    }
    {
        INFO("found in the expansion");
        CHECK(entry >= 0);
        CHECK(second >= 0);
        CHECK(column >= 0);
    }

    ALScriptCompileResult result;
    result.codeLine  = 7;
    result.sourceMap = std::make_shared<const ALSourceMap>(expanded.map);
    result.diagnostics.push_back(alscripttypes_data::said(entry + 7, column, "undefined x"));
    result.diagnostics.push_back(alscripttypes_data::said(second + 7, 8, "b again"));
    result.diagnostics.push_back(alscripttypes_data::said(5000, 0, "somewhere made"));
    ALScriptDiagnostic unplaced = alscripttypes_data::said(0, 0, "Math Error");
    unplaced.hasLine            = false;
    unplaced.hasColumn          = false;
    result.diagnostics.push_back(unplaced);

    const std::vector<ALScriptDiagnostic> source = result.inSource();
    REQUIRE(source.size() == size_t(4));
    {
        INFO("the script's own line and column");
        CHECK(source[0].hasLine);
        CHECK(source[0].line == 3);
        CHECK(source[0].hasColumn);
        CHECK(source[0].column == 16);
        CHECK(source[0].message == "undefined x");
    }
    {
        INFO("the include's at none, named with its line");
        CHECK_FALSE(source[1].hasLine);
        CHECK(source[1].message == "lib.lsl:2: b again");
    }
    {
        INFO("the preprocessor's own at none");
        CHECK_FALSE(source[2].hasLine);
        CHECK(source[2].message == "somewhere made");
    }
    {
        INFO("none stays none");
        CHECK_FALSE(source[3].hasLine);
        CHECK(source[3].message == "Math Error");
    }

    const std::vector<ALScriptDiagnostic> wrapped = result.inSource(1);
    {
        INFO("a line down in an envelope, no column");
        REQUIRE(wrapped.size() == size_t(4));
        CHECK(wrapped[0].hasLine);
        CHECK(wrapped[0].line == 4);
        CHECK_FALSE(wrapped[0].hasColumn);
    }

    // Nothing expanded: as the compiler said it, the text having gone as it was.
    ALScriptCompileResult plain;
    plain.diagnostics.push_back(alscripttypes_data::said(4, 2, "as is"));
    const std::vector<ALScriptDiagnostic> as_said = plain.inSource(1);
    {
        INFO("as said");
        REQUIRE(as_said.size() == 1);
        CHECK(as_said[0].hasLine);
        CHECK(as_said[0].line == 4);
        CHECK(as_said[0].hasColumn);
        CHECK(as_said[0].column == 2);
    }
}
} // anonymous namespace
