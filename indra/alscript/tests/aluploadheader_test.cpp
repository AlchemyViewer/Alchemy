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

#include "../test/lltut.h"

#include <xxhash.h>

#include <string>

namespace tut
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

    typedef test_group<aluploadheader_data> aluploadheader_group;
    typedef aluploadheader_group::object    aluploadheader_object;
    aluploadheader_group                    aluploadheader_instance("aluploadheader");

    template<> template<>
    void aluploadheader_object::test<1>()
    {
        set_test_name("ours is written byte for byte, a field only where there is one, in each language's comments");
        const ALUploadHeader header = aluploadheader_data::full();
        ensure_equals("LSL",
                      header.write(false),
                      std::string("// ================ alchemy meta ================\n"
                                  "// @file net/door.lsl\n"
                                  "// @hash xxh128:0123456789abcdef0123456789abcdef\n"
                                  "// @date 2026-10-07 14:03:11\n"
                                  "// @creator Rye Resident\n"
                                  "// ==============================================\n"));
        ensure_equals("SLua",
                      header.write(true),
                      std::string("-- ================ alchemy meta ================\n"
                                  "-- @file net/door.lsl\n"
                                  "-- @hash xxh128:0123456789abcdef0123456789abcdef\n"
                                  "-- @date 2026-10-07 14:03:11\n"
                                  "-- @creator Rye Resident\n"
                                  "-- ==============================================\n"));

        ALUploadHeader bare;
        bare.hash = "xxh128:00";
        bare.date = "now";
        ensure_equals("no file and no creator",
                      bare.write(false),
                      std::string("// ================ alchemy meta ================\n// @hash xxh128:00\n// @date now\n"
                                  "// ==============================================\n"));

        for (const char* where : { "/home/rye/net/door.lsl", "C:\\scripts\\door.lsl", "c:door.lsl", "\\\\host\\share\\door.lsl", "~/door.lsl" })
        {
            ALUploadHeader absolute = bare;
            absolute.file           = where;
            ensure_equals(std::string("never absolute: ") + where, absolute.write(false), bare.write(false));
        }
        ALUploadHeader aliased = bare;
        aliased.file           = "@lib/util.luau";
        ensure("an alias is no root", aliased.write(true).find("-- @file @lib/util.luau\n") != std::string::npos);

        ALUploadHeader broken = bare;
        broken.creator        = "two\nlines\r";
        ensure("a line break in a field stays in its comment", broken.write(false).find("// @creator two lines \n") != std::string::npos);
    }

    template<> template<>
    void aluploadheader_object::test<2>()
    {
        set_test_name("ours reads back, and take() steps past it to the code");
        for (const bool lua : { false, true })
        {
            const ALUploadHeader written = aluploadheader_data::full();
            const std::string    code    = lua ? "print('x')\n" : "default { }\n";
            const std::string    text    = written.write(lua) + code;
            std::string_view     rest    = text;
            const std::optional<ALUploadHeader> back = ALUploadHeader::take(rest, lua);
            const std::string    name    = lua ? "SLua: " : "LSL: ";
            ensure(name + "read", back.has_value());
            ensure(name + "ours", back->ours);
            ensure_equals(name + "file", back->file, written.file);
            ensure_equals(name + "hash", back->hash, written.hash);
            ensure_equals(name + "date", back->date, written.date);
            ensure_equals(name + "creator", back->creator, written.creator);
            ensure_equals(name + "no key", back->creatorId, std::string());
            ensure_equals(name + "the code is what is left", std::string(rest), code);
            ensure(name + "not in the other language's comments", !ALUploadHeader::parse(text, !lua).has_value());
        }

        // Written on Windows, every line ends in a carriage return as well.
        std::string_view crlf = "// ================ alchemy meta ================\r\n// @hash h\r\n// ===\r\ncode";
        const std::optional<ALUploadHeader> windows = ALUploadHeader::take(crlf, false);
        ensure("carriage returns", windows.has_value());
        ensure_equals("and none in a field", windows->hash, std::string("h"));
        ensure_equals("past it", std::string(crlf), std::string("code"));
    }

    template<> template<>
    void aluploadheader_object::test<3>()
    {
        set_test_name("the plugin's block reads as it writes it");
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
        ensure("read", back.has_value());
        ensure("not ours", !back->ours);
        ensure_equals("file", back->file, std::string("scripts/door.lsl"));
        ensure_equals("its sha256, as it is", back->hash, std::string("9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"));
        ensure_equals("date", back->date, std::string("2026-10-07 14:03:11"));
        ensure_equals("creator", back->creator, std::string("Rye Resident"));
        ensure_equals("key", back->creatorId, std::string("0b8f2ef9-1d4a-4f4c-9b62-7d3a1c4e5f60"));
        ensure_equals("the code is what is left", std::string(rest), std::string("default { }\n"));

        const std::optional<ALUploadHeader> slua = ALUploadHeader::parse("-- ================ sl-vscode-plugin meta ================\n"
                                                                         "-- @file a.luau\n-- @someday more\n"
                                                                         "-- =======================================================\n",
                                                                         true);
        ensure("SLua's, a field to come passed over", slua.has_value() && slua->file == "a.luau" && !slua->ours);
    }

    template<> template<>
    void aluploadheader_object::test<4>()
    {
        set_test_name("what only looks like a header is code");
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
            ensure(std::string("not a header: ") + text, !ALUploadHeader::take(rest, false).has_value());
            ensure_equals(std::string("and nothing taken: ") + text, std::string(rest), std::string(text));
        }
        std::string runs_on = "// ================ alchemy meta ================\n";
        for (int i = 0; i < 40; ++i)
        {
            runs_on += "// @x y\n";
        }
        runs_on += "// ====\n";
        ensure("a block far longer than any header", !ALUploadHeader::parse(runs_on, false).has_value());
    }

    template<> template<>
    void aluploadheader_object::test<5>()
    {
        set_test_name("the hash is XXH128 over v1, the target, the source and the code, each after a nought, as xxhsum writes it");
        using namespace std::string_literals;
        const std::string hash = ALUploadHeader::hashOf("mono", "#define X 1\nX", "1");
        ensure_equals("xxHash's own", hash, aluploadheader_data::xxhsum("v1\0mono\0#define X 1\nX\0001"s));
        ensure_equals("its scheme and 32 digits", hash.size(), std::string("xxh128:").size() + 32);
        ensure_equals("plain: the text alone", ALUploadHeader::hashOfPlain("luau", "print(1)"),
                      aluploadheader_data::xxhsum("v1\0luau\0print(1)"s));
        ensure_equals("no target", ALUploadHeader::hashOfPlain("", "x"), aluploadheader_data::xxhsum("v1\0\0x"s));

        ensure("the same, the same", hash == ALUploadHeader::hashOf("mono", "#define X 1\nX", "1"));
        ensure("another target", hash != ALUploadHeader::hashOf("lsl2", "#define X 1\nX", "1"));
        ensure("another source", hash != ALUploadHeader::hashOf("mono", "#define X 2\nX", "1"));
        ensure("other code", hash != ALUploadHeader::hashOf("mono", "#define X 1\nX", "2"));
        ensure("where the source ends", ALUploadHeader::hashOf("mono", "ab", "c") != ALUploadHeader::hashOf("mono", "a", "bc"));
        ensure("wrapped with nothing expanded is not plain", ALUploadHeader::hashOf("mono", "x", "") != ALUploadHeader::hashOfPlain("mono", "x"));
    }

    template<> template<>
    void aluploadheader_object::test<6>()
    {
        set_test_name("a date as @date writes it, in UTC; and what is absolute");
        ensure_equals("the epoch", ALUploadHeader::dateOf(LLDate(0.0)), std::string("1970-01-01 00:00:00"));
        ensure_equals("a day", ALUploadHeader::dateOf(LLDate(1791381791.0)), std::string("2026-10-07 14:03:11"));
        ensure_equals("a leap day's last second", ALUploadHeader::dateOf(LLDate(951868799.5)), std::string("2000-02-29 23:59:59"));

        for (const char* path : { "/a", "\\a", "\\\\host\\a", "C:\\a", "c:/a", "z:a", "~/a", "~rye/a" })
        {
            ensure(std::string("absolute: ") + path, ALUploadHeader::absolute(path));
        }
        for (const char* path : { "", "a", "net/door.lsl", "../shared/util.luau", "@lib/util.luau", "1:x", ":a" })
        {
            ensure(std::string("not: ") + path, !ALUploadHeader::absolute(path));
        }
    }
}
