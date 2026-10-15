/**
 * @file aluploadheader_test.cpp
 * @brief The upload header writes ours, reads ours and the plugin's, and hashes what is sent as xxhsum would.
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

#include "../preprocessor/aluploadheader.h"

#include "lldate.h"

#include <catch2/catch_test_macros.hpp>

#include <xxhash.h>

#include <string>

namespace
{
    struct aluploadheader_data
    {
        // The hash as xxHash itself writes it, over the bytes given whole:
        // what hashOf must answer, worked out apart from it.
        static std::string xxhsum(const std::string& bytes)
        {
            XXH128_canonical_t canonical;
            XXH128_canonicalFromHash(&canonical, XXH3_128bits(bytes.data(), bytes.size()));
            static const char DIGITS[] = "0123456789abcdef";
            std::string       out      = "xxh128:";
            for (unsigned char byte : canonical.digest)
            {
                out += DIGITS[byte >> 4];
                out += DIGITS[byte & 15];
            }
            return out;
        }

        static ALUploadHeader full()
        {
            ALUploadHeader header;
            header.file    = "net/door.lsl";
            header.hash    = "xxh128:0123456789abcdef0123456789abcdef";
            header.date    = "2026-10-07 14:03:11";
            header.creator = "Rye Resident";
            return header;
        }
    };

    TEST_CASE_METHOD(aluploadheader_data, "aluploadheader: ours is written byte for byte in either language with only the fields it has", "[aluploadheader]")
    {
        const ALUploadHeader header = aluploadheader_data::full();
        UNSCOPED_INFO("LSL");
        CHECK(header.write(false) ==
                      std::string("// ================ alchemy meta ================\n"
                                  "// @file net/door.lsl\n"
                                  "// @hash xxh128:0123456789abcdef0123456789abcdef\n"
                                  "// @date 2026-10-07 14:03:11\n"
                                  "// @creator Rye Resident\n"
                                  "// ==============================================\n"));
        UNSCOPED_INFO("SLua");
        CHECK(header.write(true) ==
                      std::string("-- ================ alchemy meta ================\n"
                                  "-- @file net/door.lsl\n"
                                  "-- @hash xxh128:0123456789abcdef0123456789abcdef\n"
                                  "-- @date 2026-10-07 14:03:11\n"
                                  "-- @creator Rye Resident\n"
                                  "-- ==============================================\n"));

        ALUploadHeader bare;
        bare.hash = "xxh128:00";
        bare.date = "now";
        UNSCOPED_INFO("no file and no creator");
        CHECK(bare.write(false) ==
                      std::string("// ================ alchemy meta ================\n// @hash xxh128:00\n// @date now\n"
                                  "// ==============================================\n"));

        for (const char* where : { "/home/rye/net/door.lsl", "C:\\scripts\\door.lsl", "c:door.lsl", "\\\\host\\share\\door.lsl", "~/door.lsl" })
        {
            ALUploadHeader absolute = bare;
            absolute.file           = where;
            INFO("never absolute: " << where);
            REQUIRE(absolute.write(false) == bare.write(false));
        }
        ALUploadHeader aliased = bare;
        aliased.file           = "@lib/util.luau";
        UNSCOPED_INFO("an alias is no root");
        CHECK(aliased.write(true).find("-- @file @lib/util.luau\n") != std::string::npos);

        ALUploadHeader broken = bare;
        broken.creator        = "two\nlines\r";
        UNSCOPED_INFO("a line break in a field stays in its comment");
        CHECK(broken.write(false).find("// @creator two lines \n") != std::string::npos);
    }

    TEST_CASE_METHOD(aluploadheader_data, "aluploadheader: ours reads back and take() steps past it to the code", "[aluploadheader]")
    {
        for (const bool lua : { false, true })
        {
            const ALUploadHeader written = aluploadheader_data::full();
            const std::string    code    = lua ? "print('x')\n" : "default { }\n";
            const std::string    text    = written.write(lua) + code;
            std::string_view     rest    = text;
            const std::optional<ALUploadHeader> back = ALUploadHeader::take(rest, lua);
            CAPTURE(lua);
            REQUIRE(back.has_value());
            REQUIRE(back->ours);
            REQUIRE(back->file == written.file);
            REQUIRE(back->hash == written.hash);
            REQUIRE(back->date == written.date);
            REQUIRE(back->creator == written.creator);
            REQUIRE(back->creatorId == std::string());
            UNSCOPED_INFO("the code is what is left");
            REQUIRE(std::string(rest) == code);
            UNSCOPED_INFO("not in the other language's comments");
            REQUIRE_FALSE(ALUploadHeader::parse(text, !lua).has_value());
        }

        // Written on Windows, every line ends in a carriage return as well.
        std::string_view crlf = "// ================ alchemy meta ================\r\n// @hash h\r\n// ===\r\ncode";
        const std::optional<ALUploadHeader> windows = ALUploadHeader::take(crlf, false);
        UNSCOPED_INFO("carriage returns");
        REQUIRE(windows.has_value());
        UNSCOPED_INFO("and none in a field");
        CHECK(windows->hash == std::string("h"));
        CHECK(std::string(crlf) == std::string("code"));
    }

    TEST_CASE_METHOD(aluploadheader_data, "aluploadheader: the plugin's block reads as it writes it", "[aluploadheader]")
    {
        // As sl-vscode-plugin's prefixWithMetaInformation writes it, its
        // creator lines and all.
        const std::string text = "// ================ sl-vscode-plugin meta ================\n"
                                 "// @file scripts/door.lsl\n"
                                 "// @hash 9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08\n"
                                 "// @date 2026-10-07 14:03:11\n"
                                 "// @creator Rye Resident\n"
                                 "// @creatorID 0b8f2ef9-1d4a-4f4c-9b62-7d3a1c4e5f60\n"
                                 "// =======================================================\n"
                                 "default { }\n";
        std::string_view                    rest = text;
        const std::optional<ALUploadHeader> back = ALUploadHeader::take(rest, false);
        REQUIRE(back.has_value());
        CHECK_FALSE(back->ours);
        CHECK(back->file == std::string("scripts/door.lsl"));
        UNSCOPED_INFO("its sha256, as it is");
        CHECK(back->hash == std::string("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));
        CHECK(back->date == std::string("2026-10-07 14:03:11"));
        CHECK(back->creator == std::string("Rye Resident"));
        CHECK(back->creatorId == std::string("0b8f2ef9-1d4a-4f4c-9b62-7d3a1c4e5f60"));
        UNSCOPED_INFO("the code is what is left");
        CHECK(std::string(rest) == std::string("default { }\n"));

        const std::optional<ALUploadHeader> slua = ALUploadHeader::parse("-- ================ sl-vscode-plugin meta ================\n"
                                                                         "-- @file a.luau\n-- @someday more\n"
                                                                         "-- =======================================================\n",
                                                                         true);
        {
            INFO("SLua's, a field to come passed over");
            REQUIRE(slua.has_value());
            CHECK(slua->file == "a.luau");
            CHECK_FALSE(slua->ours);
        }
    }

    TEST_CASE_METHOD(aluploadheader_data, "aluploadheader: what only looks like a header is code", "[aluploadheader]")
    {
        const char* texts[] = {
            // Nothing of the kind.
            "default { }\n",
            "// just a comment\ndefault { }\n",
            // Someone else's banner.
            "// ================ other meta ================\n// @file x\n// ====\n",
            // Not at the very start.
            "\n// ================ alchemy meta ================\n// ====\n",
            " // ================ alchemy meta ================\n// ====\n",
            // A line of code before the close.
            "// ================ alchemy meta ================\n// @file x\ninteger i;\n// ====\n",
            // A comment that is no field.
            "// ================ alchemy meta ================\n// note\n// ====\n",
            // Never closed.
            "// ================ alchemy meta ================\n// @file x\n",
        };
        for (const char* text : texts)
        {
            std::string_view rest = text;
            CAPTURE(text);
            REQUIRE_FALSE(ALUploadHeader::take(rest, false).has_value());
            REQUIRE(std::string(rest) == std::string(text));
        }
        std::string runs_on = "// ================ alchemy meta ================\n";
        for (int i = 0; i < 40; ++i)
        {
            runs_on += "// @x y\n";
        }
        runs_on += "// ====\n";
        UNSCOPED_INFO("a block far longer than any header");
        CHECK_FALSE(ALUploadHeader::parse(runs_on, false).has_value());
    }

    TEST_CASE_METHOD(aluploadheader_data, "aluploadheader: the hash is XXH128 of v1 and the target and source and code split by noughts", "[aluploadheader]")
    {
        using namespace std::string_literals;
        const std::string hash = ALUploadHeader::hashOf("mono", "#define X 1\nX", "1");
        UNSCOPED_INFO("xxHash's own");
        CHECK(hash == aluploadheader_data::xxhsum("v1\0mono\0#define X 1\nX\0001"s));
        UNSCOPED_INFO("its scheme and 32 digits");
        CHECK(hash.size() == std::string("xxh128:").size() + 32);
        UNSCOPED_INFO("plain: the text alone");
        CHECK(ALUploadHeader::hashOfPlain("luau", "print(1)") ==
                      aluploadheader_data::xxhsum("v1\0luau\0print(1)"s));
        UNSCOPED_INFO("no target");
        CHECK(ALUploadHeader::hashOfPlain("", "x") == aluploadheader_data::xxhsum("v1\0\0x"s));

        UNSCOPED_INFO("the same, the same");
        CHECK(hash == ALUploadHeader::hashOf("mono", "#define X 1\nX", "1"));
        UNSCOPED_INFO("another target");
        CHECK(hash != ALUploadHeader::hashOf("lsl2", "#define X 1\nX", "1"));
        UNSCOPED_INFO("another source");
        CHECK(hash != ALUploadHeader::hashOf("mono", "#define X 2\nX", "1"));
        UNSCOPED_INFO("other code");
        CHECK(hash != ALUploadHeader::hashOf("mono", "#define X 1\nX", "2"));
        UNSCOPED_INFO("where the source ends");
        CHECK(ALUploadHeader::hashOf("mono", "ab", "c") != ALUploadHeader::hashOf("mono", "a", "bc"));
        UNSCOPED_INFO("wrapped with nothing expanded is not plain");
        CHECK(ALUploadHeader::hashOf("mono", "x", "") != ALUploadHeader::hashOfPlain("mono", "x"));
    }

    TEST_CASE_METHOD(aluploadheader_data, "aluploadheader: a date as @date writes it in UTC; and what is absolute", "[aluploadheader]")
    {
        UNSCOPED_INFO("the epoch");
        CHECK(ALUploadHeader::dateOf(LLDate(0.0)) == std::string("1970-01-01 00:00:00"));
        UNSCOPED_INFO("a day");
        CHECK(ALUploadHeader::dateOf(LLDate(1791381791.0)) == std::string("2026-10-07 14:03:11"));
        UNSCOPED_INFO("a leap day's last second");
        CHECK(ALUploadHeader::dateOf(LLDate(951868799.5)) == std::string("2000-02-29 23:59:59"));

        for (const char* path : { "/a", "\\a", "\\\\host\\a", "C:\\a", "c:/a", "z:a", "~/a", "~rye/a" })
        {
            CAPTURE(path);
            REQUIRE(ALUploadHeader::absolute(path));
        }
        for (const char* path : { "", "a", "net/door.lsl", "../shared/util.luau", "@lib/util.luau", "1:x", ":a" })
        {
            CAPTURE(path);
            REQUIRE_FALSE(ALUploadHeader::absolute(path));
        }
    }
}
