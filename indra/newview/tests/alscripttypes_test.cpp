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
#include "../test/lltut.h"

namespace tut
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
typedef test_group<alscripttypes_data> alscripttypes_group;
typedef alscripttypes_group::object    alscripttypes_object;
alscripttypes_group                    alscripttypes_test("alscripttypes");

template<>
template<>
void alscripttypes_object::test<1>()
{
    set_test_name("a compile's diagnostics read back to the source: under the envelope's lines and through the map; an include's and "
                  "the preprocessor's own at no line, the include named; a client's envelope a line down, without columns; as said "
                  "with no map");
    ALPreprocessor::Options options;
    options.fileName = "main.lsl";
    options.resolve  = [](const ALPreprocessor::Ask& ask, ALPreprocessor::Include& out) {
        out.name = ask.name;
        out.text = "integer a;\ninteger b;\n";
        return ALPreprocessor::Found::Yes;
    };
    const ALPreprocessor::Result expanded = ALPreprocessor::run("#include \"lib.lsl\"\ndefault\n{\nstate_entry() { x; }\n}\n", options);
    ensure("expanded", expanded.problems.empty());
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
    ensure("found in the expansion", entry >= 0 && second >= 0 && column >= 0);

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
    ensure_equals("four", source.size(), size_t(4));
    ensure("the script's own line and column", source[0].hasLine && source[0].line == 3 && source[0].hasColumn && source[0].column == 16 &&
                                                   source[0].message == "undefined x");
    ensure("the include's at none, named with its line", !source[1].hasLine && source[1].message == "lib.lsl:2: b again");
    ensure("the preprocessor's own at none", !source[2].hasLine && source[2].message == "somewhere made");
    ensure("none stays none", !source[3].hasLine && source[3].message == "Math Error");

    const std::vector<ALScriptDiagnostic> wrapped = result.inSource(1);
    ensure("a line down in an envelope, no column", wrapped[0].hasLine && wrapped[0].line == 4 && !wrapped[0].hasColumn);

    // Nothing expanded: as the compiler said it, the text having gone as it was.
    ALScriptCompileResult plain;
    plain.diagnostics.push_back(alscripttypes_data::said(4, 2, "as is"));
    const std::vector<ALScriptDiagnostic> as_said = plain.inSource(1);
    ensure("as said", as_said.size() == 1 && as_said[0].hasLine && as_said[0].line == 4 && as_said[0].hasColumn && as_said[0].column == 2);
}
} // namespace tut
