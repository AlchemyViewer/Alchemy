/**
 * @file alscriptenvelope_test.cpp
 * @brief The envelope reads what Firestorm writes and writes what it reads.
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

#include "../alscriptenvelope.h"

#include "../test/lltut.h"

namespace tut
{
    struct alscriptenvelope_data
    {
    };

    typedef test_group<alscriptenvelope_data> alscriptenvelope_group;
    typedef alscriptenvelope_group::object    alscriptenvelope_object;
    alscriptenvelope_group                    alscriptenvelope_instance("alscriptenvelope");

    template<> template<>
    void alscriptenvelope_object::test<1>()
    {
        set_test_name("the escaping is Firestorm's: a bar after every / or * that a /, * or | follows");
        ensure_equals("comments", ALScriptEnvelope::encodeSource("/* a */ // b"), std::string("/|* a *|/ /|/ b"));
        ensure_equals("a bar of its own", ALScriptEnvelope::encodeSource("x /| y"), std::string("x /|| y"));
        ensure_equals("a run of stars", ALScriptEnvelope::encodeSource("***"), std::string("*|*|*"));
        ensure_equals("nothing to escape", ALScriptEnvelope::encodeSource("a / b * c"), std::string("a / b * c"));
        ensure_equals("and back", ALScriptEnvelope::decodeSource("/|* a *|/ /|/ b"), std::string("/* a */ // b"));
        ensure_equals("back from stars", ALScriptEnvelope::decodeSource("*|*|*"), std::string("***"));
        const std::string source = "default\n{\n    state_entry()\n    {\n        /* hi */ llSay(0, \"a // b\");\n    }\n}\n";
        ensure_equals("round trip", ALScriptEnvelope::decodeSource(ALScriptEnvelope::encodeSource(source)), source);
    }

    template<> template<>
    void alscriptenvelope_object::test<2>()
    {
        set_test_name("an LSL asset the preprocessor wrote reads back as its parts");
        const std::string asset =
            "//start_unprocessed_text\n"
            "/*#define X 1\n"
            "/|* a comment *|/\n"
            "default { state_entry() { llSay(0, (string)X); } }*/\n"
            "//end_unprocessed_text\n"
            "//nfo_preprocessor_version 0\n"
            "//program_version Firestorm-Releasex64 7.1.11 (76496)\n"
            "//last_compiled 09/20/2026 10:11:12\n"
            "//mono\n"
            "default { state_entry() { llSay(0, (string)1); } }\n";
        ensure("looks wrapped", ALScriptEnvelope::looksWrapped(asset));
        std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(asset);
        ensure("parsed", envelope.has_value());
        ensure("lsl", !envelope->lua);
        ensure_equals("the source", envelope->source,
                      std::string("#define X 1\n/* a comment */\ndefault { state_entry() { llSay(0, (string)X); } }"));
        ensure_equals("the target", envelope->compileTarget, std::string("mono"));
        ensure_equals("the program", envelope->programVersion, std::string("Firestorm-Releasex64 7.1.11 (76496)"));
        ensure_equals("the date", envelope->lastCompiled, std::string("09/20/2026 10:11:12"));
        ensure_equals("the expanded code", envelope->expanded, std::string("default { state_entry() { llSay(0, (string)1); } }\n"));

        ensure("plain source is not an envelope", !ALScriptEnvelope::parse("default { }").has_value());
        ensure("a start without an end is not one", !ALScriptEnvelope::parse("//start_unprocessed_text\n/* x").has_value());
    }

    template<> template<>
    void alscriptenvelope_object::test<3>()
    {
        set_test_name("what is written is what Firestorm writes, and reads back");
        ALScriptEnvelope envelope;
        envelope.source         = "// src\ndefault { }";
        envelope.expanded       = "default { }\n";
        envelope.compileTarget  = "lsl2";
        envelope.programVersion = "Alchemy 9.0.0";
        envelope.lastCompiled   = "now";
        const std::string asset = envelope.wrap();
        ensure_equals("byte for byte",
                      asset,
                      std::string("//start_unprocessed_text\n/*/|/ src\ndefault { }*/\n//end_unprocessed_text\n"
                                  "//nfo_preprocessor_version 0\n//program_version Alchemy 9.0.0\n//last_compiled now\n//lsl2\ndefault { }\n"));
        std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(asset);
        ensure("reads back", back.has_value());
        ensure_equals("the source", back->source, envelope.source);
        ensure_equals("the expanded code", back->expanded, envelope.expanded);
        ensure_equals("the target", back->compileTarget, envelope.compileTarget);
    }

    template<> template<>
    void alscriptenvelope_object::test<4>()
    {
        set_test_name("SLua gets a long comment at a level the source has none of");
        ALScriptEnvelope envelope;
        envelope.lua            = true;
        envelope.source         = "local t = {[[a]], [=[b]=]}\nprint(t)";
        envelope.expanded       = "print('x')\n";
        envelope.compileTarget  = "luau";
        envelope.programVersion = "Alchemy";
        envelope.lastCompiled   = "now";
        const std::string asset = envelope.wrap();
        ensure("level two, since the source closes levels zero and one",
               asset.find("--[==[local t") != std::string::npos && asset.find("]==]\n--end_unprocessed_text\n") != std::string::npos);
        ensure("looks wrapped", ALScriptEnvelope::looksWrapped(asset));
        std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(asset);
        ensure("reads back", back.has_value());
        ensure("lua", back->lua);
        ensure_equals("the source", back->source, envelope.source);
        ensure_equals("the expanded code", back->expanded, envelope.expanded);
        ensure_equals("the target", back->compileTarget, std::string("luau"));
    }

    template<> template<>
    void alscriptenvelope_object::test<5>()
    {
        set_test_name("a directive is a line of its own");
        ensure_equals("mono", ALScriptEnvelope::directiveOf("// x\n//mono\ndefault{}", false), std::string("mono"));
        // As Firestorm has it: mono anywhere wins over lsl2 anywhere.
        ensure_equals("mono over lsl2", ALScriptEnvelope::directiveOf("//lsl2\n//mono\n", false), std::string("mono"));
        ensure_equals("lsl2", ALScriptEnvelope::directiveOf("//lsl2\n", false), std::string("lsl2"));
        ensure_equals("none", ALScriptEnvelope::directiveOf("//monolith\n", false), std::string());
        ensure_equals("not mid-line", ALScriptEnvelope::directiveOf("x //mono\n", false), std::string());
        ensure_equals("lua", ALScriptEnvelope::directiveOf("--luau\n", true), std::string("luau"));
        ensure_equals("not lua's in lsl", ALScriptEnvelope::directiveOf("--luau\n", false), std::string());
    }
}
