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

#include "../preprocessor/alscriptenvelope.h"
#include "../preprocessor/aluploadheader.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    struct alscriptenvelope_data
    {
        // Lexes the long comment an SLua envelope opens with as Luau does:
        // the level from the opener, then the body up to the first closer of
        // that level, wherever that falls. Answers where that closer starts.
        static size_t luaCommentEnd(const std::string& asset, size_t& level, size_t& body)
        {
            body  = std::string_view("--start_unprocessed_text\n--[").size();
            level = 0;
            while (body < asset.size() && asset[body] == '=')
            {
                ++level;
                ++body;
            }
            ++body;
            return asset.find("]" + std::string(level, '=') + "]", body);
        }

        // An envelope as a save makes one, with or without a header.
        static ALScriptEnvelope made(bool lua, bool header)
        {
            ALScriptEnvelope envelope;
            envelope.lua            = lua;
            envelope.source         = lua ? "local n = 1\nprint(n)" : "integer n = 1;\ndefault { state_entry() { llSay(0, (string)n); } }";
            envelope.expanded       = lua ? "local n = 1\nprint(n)\n" : "integer n = 1;\ndefault { state_entry() { llSay(0, (string)n); } }\n";
            envelope.compileTarget  = lua ? "luau" : "mono";
            envelope.programVersion = "Alchemy";
            envelope.lastCompiled   = "today";
            if (header)
            {
                ALUploadHeader fields;
                fields.file     = lua ? "net/door.luau" : "net/door.lsl";
                fields.hash     = ALUploadHeader::hashOf(envelope.compileTarget, envelope.source, envelope.expanded);
                fields.date     = "2026-10-07 14:03:11";
                envelope.header = fields.write(lua);
            }
            return envelope;
        }

        // The header a save writes for an envelope as it stands: its hash
        // is of the target, the source and the code, so one made before a
        // field changed is no longer the envelope's.
        static std::string hashedHeader(const ALScriptEnvelope& envelope)
        {
            ALUploadHeader fields;
            fields.file = envelope.lua ? "net/door.luau" : "net/door.lsl";
            fields.hash = ALUploadHeader::hashOf(envelope.compileTarget, envelope.source, envelope.expanded);
            fields.date = "2026-10-07 14:03:11";
            return fields.write(envelope.lua);
        }

        static std::vector<std::string> linesOf(const std::string& asset)
        {
            std::vector<std::string> lines;
            for (size_t from = 0;;)
            {
                const size_t nl = asset.find('\n', from);
                lines.push_back(asset.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
                if (nl == std::string::npos)
                {
                    return lines;
                }
                from = nl + 1;
            }
        }
    };

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: Firestorm's escaping puts a bar after each / or * before a / or * or |", "[alscriptenvelope]")
    {
        UNSCOPED_INFO("comments");
        CHECK(ALScriptEnvelope::encodeSource("/* a */ // b") == std::string("/|* a *|/ /|/ b"));
        UNSCOPED_INFO("a bar of its own");
        CHECK(ALScriptEnvelope::encodeSource("x /| y") == std::string("x /|| y"));
        UNSCOPED_INFO("a run of stars");
        CHECK(ALScriptEnvelope::encodeSource("***") == std::string("*|*|*"));
        UNSCOPED_INFO("nothing to escape");
        CHECK(ALScriptEnvelope::encodeSource("a / b * c") == std::string("a / b * c"));
        UNSCOPED_INFO("and back");
        CHECK(ALScriptEnvelope::decodeSource("/|* a *|/ /|/ b") == std::string("/* a */ // b"));
        UNSCOPED_INFO("back from stars");
        CHECK(ALScriptEnvelope::decodeSource("*|*|*") == std::string("***"));
        const std::string source = "default\n{\n    state_entry()\n    {\n        /* hi */ llSay(0, \"a // b\");\n    }\n}\n";
        UNSCOPED_INFO("round trip");
        CHECK(ALScriptEnvelope::decodeSource(ALScriptEnvelope::encodeSource(source)) == source);
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: an LSL asset the preprocessor wrote reads back as its parts", "[alscriptenvelope]")
    {
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
        CHECK(ALScriptEnvelope::looksWrapped(asset));
        std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(asset);
        REQUIRE(envelope.has_value());
        CHECK_FALSE(envelope->lua);
        CHECK(envelope->source ==
                      std::string("#define X 1\n/* a comment */\ndefault { state_entry() { llSay(0, (string)X); } }"));
        CHECK(envelope->compileTarget == std::string("mono"));
        CHECK(envelope->programVersion == std::string("Firestorm-Releasex64 7.1.11 (76496)"));
        CHECK(envelope->lastCompiled == std::string("09/20/2026 10:11:12"));
        CHECK(envelope->expanded == std::string("default { state_entry() { llSay(0, (string)1); } }\n"));

        UNSCOPED_INFO("plain source is not an envelope");
        REQUIRE_FALSE(ALScriptEnvelope::parse("default { }").has_value());
        UNSCOPED_INFO("a start without an end is not one");
        REQUIRE_FALSE(ALScriptEnvelope::parse("//start_unprocessed_text\n/* x").has_value());
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: what is written is what Firestorm writes and reads back", "[alscriptenvelope]")
    {
        ALScriptEnvelope envelope;
        envelope.source         = "// src\ndefault { }";
        envelope.expanded       = "default { }\n";
        envelope.compileTarget  = "lsl2";
        envelope.programVersion = "Alchemy 9.0.0";
        envelope.lastCompiled   = "now";
        const std::string asset = envelope.wrap();
        UNSCOPED_INFO("byte for byte");
        CHECK(asset ==
                      std::string("//start_unprocessed_text\n/*/|/ src\ndefault { }*/\n//end_unprocessed_text\n"
                                  "//nfo_preprocessor_version 0\n//program_version Alchemy 9.0.0\n//last_compiled now\n//lsl2\ndefault { }\n"));
        std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(asset);
        REQUIRE(back.has_value());
        CHECK(back->source == envelope.source);
        CHECK(back->expanded == envelope.expanded);
        CHECK(back->compileTarget == envelope.compileTarget);
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: SLua gets a long comment at a level the source has none of", "[alscriptenvelope]")
    {
        ALScriptEnvelope envelope;
        envelope.lua            = true;
        envelope.source         = "local t = {[[a]], [=[b]=]}\nprint(t)";
        envelope.expanded       = "print('x')\n";
        envelope.compileTarget  = "luau";
        envelope.programVersion = "Alchemy";
        envelope.lastCompiled   = "now";
        const std::string asset = envelope.wrap();
        {
            INFO("level two, since the source closes levels zero and one");
            REQUIRE(asset.find("--[==[local t") != std::string::npos);
            REQUIRE(asset.find("]==]\n--end_unprocessed_text\n") != std::string::npos);
        }
        CHECK(ALScriptEnvelope::looksWrapped(asset));
        std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(asset);
        REQUIRE(back.has_value());
        CHECK(back->lua);
        CHECK(back->source == envelope.source);
        CHECK(back->expanded == envelope.expanded);
        CHECK(back->compileTarget == std::string("luau"));
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: a directive is a line of its own", "[alscriptenvelope]")
    {
        CHECK(ALScriptEnvelope::directiveOf("// x\n//mono\ndefault{}", false) == std::string("mono"));
        // As Firestorm has it: mono anywhere wins over lsl2 anywhere.
        UNSCOPED_INFO("mono over lsl2");
        CHECK(ALScriptEnvelope::directiveOf("//lsl2\n//mono\n", false) == std::string("mono"));
        CHECK(ALScriptEnvelope::directiveOf("//lsl2\n", false) == std::string("lsl2"));
        CHECK(ALScriptEnvelope::directiveOf("//monolith\n", false) == std::string());
        UNSCOPED_INFO("not mid-line");
        CHECK(ALScriptEnvelope::directiveOf("x //mono\n", false) == std::string());
        CHECK(ALScriptEnvelope::directiveOf("--luau\n", true) == std::string("luau"));
        UNSCOPED_INFO("not lua's in lsl");
        CHECK(ALScriptEnvelope::directiveOf("--luau\n", false) == std::string());
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: SLua's long comment level avoids a closer formed with the source's tail", "[alscriptenvelope]")
    {
        struct Case
        {
            std::string name;
            std::string source;
            size_t      level;
        };
        const Case cases[] = {
            // At level zero, t[1]]] would close at the source's own ].
            { "ending in ]", "return t[1]", 1 },
            // ]=]] is safe at level zero; only ]=]=] at level one closes early.
            { "ending in ]=", "-- t[1]=", 0 },
            // Level zero is out for the ]] inside; ]]]=] is safe at level one.
            { "ending in ]]", "local s = [[a]]", 1 },
            // Level zero is out for the ]], level one for the ]=]=] at the join.
            { "holding ]] and ending in ]=", "local s = [[a]]\n-- t[1]=", 2 },
        };
        for (const Case& c : cases)
        {
            ALScriptEnvelope envelope;
            envelope.lua            = true;
            envelope.source         = c.source;
            envelope.expanded       = "print('x')\n";
            envelope.compileTarget  = "luau";
            envelope.programVersion = "Alchemy";
            envelope.lastCompiled   = "now";
            const std::string asset = envelope.wrap();

            size_t       level = 0;
            size_t       body  = 0;
            const size_t end   = luaCommentEnd(asset, level, body);
            CAPTURE(c.name);
            REQUIRE(level == c.level);
            UNSCOPED_INFO("the comment closes where the source ends");
            REQUIRE(end == body + c.source.size());
            UNSCOPED_INFO("and the end line follows the closer");
            REQUIRE(asset.substr(end + level + 2, 24) ==
                          std::string("\n--end_unprocessed_text\n"));

            std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(asset);
            REQUIRE(back.has_value());
            REQUIRE(back->source == c.source);
            REQUIRE(back->expanded == envelope.expanded);
        }
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: compiledFrom sees past spacing and comments and dropped parts but not edits", "[alscriptenvelope]")
    {
        const std::string made = "integer g = 3;\nfloat unused = 1.0;\nhelper() { llSay(0, \"a\"); }\n"
                                 "default\n{\n    state_entry()\n    {\n        g = 4;\n        llSay(0, (string)g);\n    }\n}\n";
        UNSCOPED_INFO("itself");
        CHECK(ALScriptEnvelope::compiledFrom(made, made, false));
        UNSCOPED_INFO("spaced and commented otherwise");
        CHECK(ALScriptEnvelope::compiledFrom(made, "// made here\ninteger g=3;float unused=1.0;helper(){llSay(0,\"a\");}"
                                                    "default{state_entry(){g=4;llSay(0,(string)g);}}", false));
        UNSCOPED_INFO("an unused global and function left out");
        CHECK(ALScriptEnvelope::compiledFrom(made, "integer g = 3;\ndefault { state_entry() { g = 4; llSay(0, (string)g); } }", false));
        // 3 is a token the script has elsewhere: changed here, it is
        // still not what the source makes.
        UNSCOPED_INFO("a number changed");
        CHECK_FALSE(ALScriptEnvelope::compiledFrom(made, "integer g = 3;\ndefault { state_entry() { g = 3; llSay(0, (string)g); } }", false));
        UNSCOPED_INFO("a line added");
        CHECK_FALSE(ALScriptEnvelope::compiledFrom(made, "integer g = 3;\ndefault { state_entry() { g = 4; llSay(0, (string)g); llDie(); } }", false));
        UNSCOPED_INFO("a part added");
        CHECK_FALSE(ALScriptEnvelope::compiledFrom(made, made + "extra() { }\n", false));
        UNSCOPED_INFO("in another order");
        CHECK_FALSE(ALScriptEnvelope::compiledFrom(made, "default { state_entry() { g = 4; llSay(0, (string)g); } }\ninteger g = 3;", false));

        const std::string lua = "local n = 1\nprint(n)\n";
        UNSCOPED_INFO("SLua: the same tokens");
        CHECK(ALScriptEnvelope::compiledFrom(lua, "local n=1 -- one\nprint(n)", true));
        UNSCOPED_INFO("SLua: not");
        CHECK_FALSE(ALScriptEnvelope::compiledFrom(lua, "local n = 2\nprint(n)\n", true));
        UNSCOPED_INFO("SLua: nothing left out");
        CHECK_FALSE(ALScriptEnvelope::compiledFrom(lua, "local n = 1\n", true));

        UNSCOPED_INFO("compiled here: told, transformed or not");
        CHECK(ALScriptEnvelope::comparable(made, false, true, true));
        UNSCOPED_INFO("elsewhere, untransformed: told");
        CHECK(ALScriptEnvelope::comparable(made, false, false, false));
        UNSCOPED_INFO("elsewhere, transformed: not");
        CHECK_FALSE(ALScriptEnvelope::comparable(made, false, false, true));
        UNSCOPED_INFO("the time asked for: not");
        CHECK_FALSE(ALScriptEnvelope::comparable("string t = __TIME__;", false, true, false));
        UNSCOPED_INFO("who compiled it: not");
        CHECK_FALSE(ALScriptEnvelope::comparable("local a = __AGENTNAME__", true, true, false));
        UNSCOPED_INFO("named in a comment only: told");
        CHECK(ALScriptEnvelope::comparable("// built __DATE__\ninteger x;", false, true, false));
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: codeLine counts the envelope's own lines before the code", "[alscriptenvelope]")
    {
        for (const bool lua : { true, false })
        {
            ALScriptEnvelope envelope;
            envelope.lua            = lua;
            envelope.source         = "a\nb\nc";
            envelope.expanded       = "first\nsecond\n";
            envelope.compileTarget  = lua ? "luau" : "mono";
            envelope.programVersion = "Alchemy";
            envelope.lastCompiled   = "today";
            const std::string asset = envelope.wrap();
            std::vector<std::string> lines;
            for (size_t from = 0;;)
            {
                const size_t nl = asset.find('\n', from);
                lines.push_back(asset.substr(from, nl == std::string::npos ? std::string::npos : nl - from));
                if (nl == std::string::npos)
                {
                    break;
                }
                from = nl + 1;
            }
            const int at = envelope.codeLine();
            CAPTURE(lua);
            REQUIRE(static_cast<size_t>(at) < lines.size());
            REQUIRE(lines[static_cast<size_t>(at)] == std::string("first"));
            UNSCOPED_INFO("after the source's lines and the envelope's own");
            REQUIRE(at == 9);
        }
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: wrappedSize is the size of the asset wrap makes", "[alscriptenvelope]")
    {
        const char* sources[] = { "", "default { }\n", "a /* b */ c // d\n/*|*/ **// x", "local s = [[x]] .. [==[y]==]\n", "]]=]==]" };
        for (const bool lua : { false, true })
        {
            for (const char* source : sources)
            {
                for (const char* target : { "", "mono", "luau" })
                {
                    ALScriptEnvelope envelope;
                    envelope.lua            = lua;
                    envelope.source         = source;
                    envelope.expanded       = "expanded\ncode /* here */\n";
                    envelope.compileTarget  = target;
                    envelope.programVersion = "Alchemy Test 1.2.3";
                    envelope.lastCompiled   = "2026-09-27T12:00:00Z";
                    CAPTURE(lua, source, target);
                    REQUIRE(envelope.wrappedSize() ==
                                  envelope.wrap().size());
                }
            }
        }
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: a header goes between the target line and the code and parses back out", "[alscriptenvelope]")
    {
        const ALScriptEnvelope lsl   = alscriptenvelope_data::made(false, true);
        const std::string      asset = lsl.wrap();
        UNSCOPED_INFO("byte for byte");
        CHECK(asset ==
                      std::string("//start_unprocessed_text\n/*") + ALScriptEnvelope::encodeSource(lsl.source) +
                          "*/\n//end_unprocessed_text\n//nfo_preprocessor_version 0\n//program_version Alchemy\n//last_compiled today\n//mono\n" +
                          lsl.header + lsl.expanded);
        for (const bool lua : { false, true })
        {
            CAPTURE(lua);
            const ALScriptEnvelope          written = alscriptenvelope_data::made(lua, true);
            std::optional<ALScriptEnvelope> back    = ALScriptEnvelope::parse(written.wrap());
            REQUIRE(back.has_value());
            REQUIRE(back->header == written.header);
            UNSCOPED_INFO("the code alone");
            REQUIRE(back->expanded == written.expanded);
            REQUIRE(back->source == written.source);
            REQUIRE(back->compileTarget == written.compileTarget);
            UNSCOPED_INFO("written again, the same");
            REQUIRE(back->wrap() == written.wrap());
            UNSCOPED_INFO("its size worked out");
            REQUIRE(written.wrappedSize() == written.wrap().size());
            UNSCOPED_INFO("and from the fields");
            REQUIRE(ALScriptEnvelope::wrappedSize(lua, written.source, written.expanded, written.compileTarget, written.programVersion,
                                                        written.lastCompiled, written.header) ==
                          written.wrap().size());

            // Without one, as it was: nothing taken, and Firestorm's bytes
            // with nothing added.
            const ALScriptEnvelope plain = alscriptenvelope_data::made(lua, false);
            back                         = ALScriptEnvelope::parse(plain.wrap());
            REQUIRE(back.has_value());
            REQUIRE(back->header == std::string());
            REQUIRE(back->expanded == plain.expanded);
            UNSCOPED_INFO("without one: the header's bytes and no more");
            REQUIRE(plain.wrap().size() + written.header.size() == written.wrap().size());
        }

        // A header that does not end its line gets a newline of its own.
        ALScriptEnvelope unended = alscriptenvelope_data::made(false, true);
        unended.header.pop_back();
        UNSCOPED_INFO("a newline after it");
        CHECK(unended.wrap() == alscriptenvelope_data::made(false, true).wrap());
        UNSCOPED_INFO("and counted");
        REQUIRE(unended.wrappedSize() == unended.wrap().size());

        // With no target line, it follows the date.
        ALScriptEnvelope untargeted = alscriptenvelope_data::made(false, true);
        untargeted.compileTarget.clear();
        untargeted.header = hashedHeader(untargeted);
        std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(untargeted.wrap());
        {
            INFO("untargeted: the header taken");
            REQUIRE(back);
            CHECK(back->header == untargeted.header);
            CHECK(back->expanded == untargeted.expanded);
        }

        // The plugin's banner, should one stand there, is taken out too.
        ALScriptEnvelope theirs = alscriptenvelope_data::made(false, false);
        theirs.header           = "// ================ sl-vscode-plugin meta ================\n// @file a.lsl\n"
                                  "// =======================================================\n";
        back = ALScriptEnvelope::parse(theirs.wrap());
        {
            INFO("the plugin's taken");
            REQUIRE(back);
            CHECK(back->header == theirs.header);
            CHECK(back->expanded == theirs.expanded);
        }

        // Only the first: a second, which the author's code begins with, is
        // the code's.
        ALScriptEnvelope twice = alscriptenvelope_data::made(false, true);
        twice.expanded         = twice.header + twice.expanded;
        twice.header           = hashedHeader(twice);
        back                   = ALScriptEnvelope::parse(twice.wrap());
        {
            INFO("one header taken");
            REQUIRE(back);
            CHECK(back->header == twice.header);
            CHECK(back->expanded == twice.expanded);
        }
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: a header moves the code's first line by its own lines and nothing else", "[alscriptenvelope]")
    {
        for (const bool lua : { false, true })
        {
            CAPTURE(lua);
            const ALScriptEnvelope without = alscriptenvelope_data::made(lua, false);
            const ALScriptEnvelope with    = alscriptenvelope_data::made(lua, true);
            const int              lines   = static_cast<int>(std::count(with.header.begin(), with.header.end(), '\n'));
            REQUIRE(lines == 5);
            UNSCOPED_INFO("the code begins that much later");
            REQUIRE(with.codeLine() == without.codeLine() + lines);

            // The asset's line codeLine() + n is the code's line n either
            // way, and the code's lines are the ones the source map is of.
            const std::vector<std::string> plain  = alscriptenvelope_data::linesOf(without.wrap());
            const std::vector<std::string> headed = alscriptenvelope_data::linesOf(with.wrap());
            const std::vector<std::string> code   = alscriptenvelope_data::linesOf(with.expanded);
            REQUIRE(static_cast<size_t>(without.codeLine()) + code.size() <= plain.size());
            REQUIRE(static_cast<size_t>(with.codeLine()) + code.size() <= headed.size());
            for (size_t i = 0; i < code.size(); ++i)
            {
                CAPTURE(i);
                UNSCOPED_INFO("without a header");
                REQUIRE(plain[static_cast<size_t>(without.codeLine()) + i] == code[i]);
                UNSCOPED_INFO("with a header");
                REQUIRE(headed[static_cast<size_t>(with.codeLine()) + i] == code[i]);
            }
            // And read back from the asset, as a save hears of it.
            const std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(with.wrap());
            REQUIRE(back.has_value());
            UNSCOPED_INFO("read back, the same line");
            REQUIRE(back->codeLine() == with.codeLine());
        }
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: a header fools neither compiledFrom nor the check that the code was edited", "[alscriptenvelope]")
    {
        for (const bool lua : { false, true })
        {
            CAPTURE(lua);
            const ALScriptEnvelope              sent   = alscriptenvelope_data::made(lua, true);
            ALScriptEnvelope                    later  = alscriptenvelope_data::made(lua, true);
            const std::optional<ALUploadHeader> parsed = ALUploadHeader::parse(later.header, lua);
            REQUIRE(parsed.has_value());
            ALUploadHeader again = *parsed;
            again.date           = "2026-10-08 09:00:00";
            later.header         = again.write(lua);

            const std::optional<ALScriptEnvelope> held = ALScriptEnvelope::parse(sent.wrap());
            const std::optional<ALScriptEnvelope> came = ALScriptEnvelope::parse(later.wrap());
            REQUIRE(held);
            REQUIRE(came);
            UNSCOPED_INFO("the headers apart");
            REQUIRE(held->header != came->header);
            // What the bridge holds up to tell an edit of the compiled half:
            // the code the same, whenever it went up.
            UNSCOPED_INFO("the code the same");
            REQUIRE(held->expanded == came->expanded);
            UNSCOPED_INFO("compiled from the expansion");
            REQUIRE(ALScriptEnvelope::compiledFrom(sent.expanded, came->expanded, lua));
            // Even left in, a header is comments, which it passes over.
            UNSCOPED_INFO("a header in the code passed over");
            REQUIRE(ALScriptEnvelope::compiledFrom(sent.expanded, sent.header + sent.expanded, lua));
            UNSCOPED_INFO("the source told as it was");
            REQUIRE(ALScriptEnvelope::comparable(came->source, lua, true, false));
            // The hash is of what was sent, the header aside: worked out
            // again from the halves read back, it is what the header says.
            const std::optional<ALUploadHeader> says = ALUploadHeader::parse(came->header, lua);
            REQUIRE(says.has_value());
            UNSCOPED_INFO("the hash again");
            REQUIRE(ALUploadHeader::hashOf(came->compileTarget, came->source, came->expanded) == says->hash);
        }
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: a compiled half with nothing in it did not come of an expansion with something", "[alscriptenvelope]")
    {
        CHECK_FALSE(ALScriptEnvelope::compiledFrom("integer g;", "// x", false));
        CHECK_FALSE(ALScriptEnvelope::compiledFrom("integer g;", "", false));
        CHECK(ALScriptEnvelope::compiledFrom("// only this", "", false));
        CHECK_FALSE(ALScriptEnvelope::compiledFrom("local n = 1", "-- x", true));
    }

    TEST_CASE_METHOD(alscriptenvelope_data, "alscriptenvelope: an author's banner shaped like a header is the code's, and ours is taken out", "[alscriptenvelope]")
    {
        for (const bool lua : { false, true })
        {
            CAPTURE(lua);
            // A banner naming us at the top of the author's script, which
            // the expansion begins with as the source does: not a header,
            // since its hash is not of this envelope, or it has none.
            for (const bool hashed : { true, false })
            {
                CAPTURE(hashed);
                ALUploadHeader banner;
                banner.file = "theirs/own.lsl";
                banner.hash = hashed ? "xxh128:00000000000000000000000000000000" : "";
                banner.date = "2026-01-01 00:00:00";
                ALScriptEnvelope envelope = made(lua, false);
                envelope.expanded         = banner.write(lua) + envelope.expanded;
                const std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(envelope.wrap());
                REQUIRE(back.has_value());
                CHECK(back->header.empty());
                CHECK(back->expanded == envelope.expanded);
            }
            // Ours, whose hash is of the envelope, comes out as before.
            const ALScriptEnvelope                ours = made(lua, true);
            const std::optional<ALScriptEnvelope> back = ALScriptEnvelope::parse(ours.wrap());
            REQUIRE(back.has_value());
            CHECK(back->header == ours.header);
            CHECK(back->expanded == ours.expanded);
        }
    }
}
