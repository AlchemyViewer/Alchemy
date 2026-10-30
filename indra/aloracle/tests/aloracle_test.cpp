/**
 * @file aloracle_test.cpp
 * @brief aloracle's C functions, called through aloracle.h alone.
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

#include "../aloracle.h"

#include <catch2/catch_test_macros.hpp>

#include <future>
#include <string>
#include <vector>

using namespace std::string_literals;

namespace
{
    // A call's status and the bytes it gave, which the library has freed.
    struct Result
    {
        int32_t status;
        std::string bytes;
    };

    const uint8_t* in(const std::string& text)
    {
        return reinterpret_cast<const uint8_t*>(text.data());
    }

    std::string taken(aloracle_bytes& bytes)
    {
        std::string text;
        if (bytes.size)
        {
            text.assign(reinterpret_cast<const char*>(bytes.data), bytes.size);
        }
        aloracle_bytes_free(&bytes);
        return text;
    }

    Result parse(int32_t format, const std::string& text, int64_t max_bytes = -1, int32_t max_depth = -1)
    {
        aloracle_bytes value{};
        const int32_t status =
            aloracle_llsd_parse(format, in(text), text.size(), max_bytes, max_depth, &value, nullptr);
        return { status, taken(value) };
    }

    Result deserialize(const std::string& text, int64_t max_bytes = -1)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_llsd_deserialize(in(text), text.size(), max_bytes, &value);
        return { status, taken(value) };
    }

    Result format(int32_t format, int32_t options, const std::string& value)
    {
        aloracle_bytes text{};
        const int32_t status = aloracle_llsd_format(format, options, in(value), value.size(), &text);
        return { status, taken(text) };
    }

    Result serialize(int32_t format, int32_t options, const std::string& value)
    {
        aloracle_bytes text{};
        const int32_t status = aloracle_llsd_serialize(format, options, in(value), value.size(), &text);
        return { status, taken(text) };
    }

    // The value notation text holds, in the binary values cross in.
    std::string valueOf(const std::string& notation)
    {
        const Result value = parse(ALORACLE_LLSD_NOTATION, notation);
        REQUIRE(value.status == ALORACLE_OK);
        return value.bytes;
    }

    Result escape(int32_t set, const std::string& text)
    {
        aloracle_bytes escaped{};
        const int32_t status = aloracle_lluri_escape(set, in(text), text.size(), &escaped);
        return { status, taken(escaped) };
    }

    Result unescape(const std::string& text)
    {
        aloracle_bytes unescaped{};
        const int32_t status = aloracle_lluri_unescape(in(text), text.size(), &unescaped);
        return { status, taken(unescaped) };
    }

    Result queryMap(const std::string& query)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_lluri_query_map(in(query), query.size(), &value);
        return { status, taken(value) };
    }

    Result mapToQueryString(const std::string& value)
    {
        aloracle_bytes query{};
        const int32_t status = aloracle_lluri_map_to_query_string(in(value), value.size(), &query);
        return { status, taken(query) };
    }

    Result dateFormat(double seconds)
    {
        aloracle_bytes text{};
        const int32_t status = aloracle_lldate_format(seconds, &text);
        return { status, taken(text) };
    }

    // A nest of arrays depth levels deep around one integer, which is a level.
    std::string nested(size_t depth)
    {
        return std::string(depth - 1, '[') + "i1" + std::string(depth - 1, ']');
    }

    TEST_CASE("aloracle: an LLSD value crosses in LLSD binary with no header", "[aloracle]")
    {
        CHECK(valueOf("{'a':i1}") == "{\0\0\0\x01" "k\0\0\0\x01" "a" "i\0\0\0\x01" "}"s);
        CHECK(valueOf("'x'") == "s\0\0\0\x01" "x"s);
        CHECK(valueOf("!") == "!");
    }

    TEST_CASE("aloracle: each LLSD format reads a value and writes it back as it was", "[aloracle]")
    {
        const std::string notation = "{'a':i1,'b':[r2.5,'x',!]}";
        const std::string xml =
            "<llsd><map><key>a</key><integer>1</integer><key>b</key>"
            "<array><real>2.5</real><string>x</string><undef /></array></map></llsd>\n";
        const std::string value = valueOf(notation);

        CHECK(format(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_NONE, value).bytes == notation);

        const Result from_xml = parse(ALORACLE_LLSD_XML, xml);
        CHECK(from_xml.status == ALORACLE_OK);
        CHECK(from_xml.bytes == value);
        CHECK(format(ALORACLE_LLSD_XML, ALORACLE_LLSD_OPTIONS_NONE, value).bytes == xml);

        const Result from_binary = parse(ALORACLE_LLSD_BINARY, value);
        CHECK(from_binary.status == ALORACLE_OK);
        CHECK(from_binary.bytes == value);
        const Result to_binary = format(ALORACLE_LLSD_BINARY, ALORACLE_LLSD_OPTIONS_NONE, value);
        CHECK(to_binary.status == ALORACLE_OK);
        CHECK(to_binary.bytes == value);
    }

    TEST_CASE("aloracle: the notation formatter writes binary raw or in base 16 as its options say", "[aloracle]")
    {
        const std::string value = valueOf("b16\"48656C6C6F\"");
        CHECK(format(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_NONE, value).bytes == "b(5)\"Hello\"");
        CHECK(format(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_PRETTY_BINARY, value).bytes
              == "b16\"48656C6C6F\"");
    }

    TEST_CASE("aloracle: a parse gives the count its parser returns", "[aloracle]")
    {
        const std::string text = "i42";
        aloracle_bytes value{};
        int32_t count = 0;
        CHECK(aloracle_llsd_parse(ALORACLE_LLSD_NOTATION, in(text), text.size(), -1, -1, &value, &count)
              == ALORACLE_OK);
        CHECK(count == 1);
        CHECK(taken(value) == "i\0\0\0\x2a"s);

        const std::string broken = "{'a':";
        CHECK(aloracle_llsd_parse(ALORACLE_LLSD_NOTATION, in(broken), broken.size(), -1, -1, &value, &count)
              == ALORACLE_FAILED);
        CHECK(count == -1);
        CHECK(value.data == nullptr);
        CHECK(value.size == 0);
    }

    TEST_CASE("aloracle: a parse fails past the depth it is given", "[aloracle]")
    {
        CHECK(parse(ALORACLE_LLSD_NOTATION, nested(4), -1, 4).status == ALORACLE_OK);
        CHECK(parse(ALORACLE_LLSD_NOTATION, nested(4), -1, 3).status == ALORACLE_FAILED);
        CHECK(parse(ALORACLE_LLSD_BINARY, valueOf(nested(4)), -1, 3).status == ALORACLE_FAILED);

        // A negative depth is the parsers' default.
        CHECK(parse(ALORACLE_LLSD_NOTATION, nested(256)).status == ALORACLE_OK);
        CHECK(parse(ALORACLE_LLSD_NOTATION, nested(257)).status == ALORACLE_FAILED);
    }

    TEST_CASE("aloracle: a parse fails past the bytes it is given", "[aloracle]")
    {
        const std::string hello = "s\0\0\0\x05" "hello"s;
        CHECK(parse(ALORACLE_LLSD_BINARY, hello, (int64_t)hello.size()).status == ALORACLE_OK);
        CHECK(parse(ALORACLE_LLSD_BINARY, hello, (int64_t)hello.size() - 1).status == ALORACLE_FAILED);
    }

    TEST_CASE("aloracle: deserialize reads the format the bytes hold", "[aloracle]")
    {
        const std::string value = valueOf("{'a':i1}");
        const std::string xml = "<llsd><map><key>a</key><integer>1</integer></map></llsd>";

        CHECK(deserialize("<? LLSD/Binary ?>\n" + value).bytes == value);
        CHECK(deserialize("<? llsd/notation ?>\n{'a':i1}").bytes == value);
        CHECK(deserialize("<? LLSD/XML ?>\n" + xml).bytes == value);

        // With no header line, '<' first is XML and anything else notation.
        CHECK(deserialize(xml).bytes == value);
        CHECK(deserialize("<?xml version=\"1.0\" ?>" + xml).bytes == value);
        CHECK(deserialize("{'a':i1}\n").bytes == value);
        // Binary with no header is notation, whose map skips what is no key.
        CHECK(deserialize(value).bytes == valueOf("{}"));
        CHECK(deserialize("").status == ALORACLE_FAILED);
    }

    TEST_CASE("aloracle: deserialize reads headerless text under twenty bytes with no newline", "[aloracle]")
    {
        // It reads up to twenty bytes for a header line first, and a first
        // line that reaches the end is the first line, with a budget or not.
        const std::string nineteen = "[i1,i2,i3,i4,i5,i6]";
        const std::string twenty = "[i1,i2,i3,i4,i5,i66]";
        REQUIRE(nineteen.size() == 19);
        REQUIRE(twenty.size() == 20);

        CHECK(deserialize(nineteen).bytes == valueOf(nineteen));
        CHECK(deserialize(nineteen + "\n").bytes == valueOf(nineteen));
        CHECK(deserialize(nineteen, (int64_t)nineteen.size()).bytes == valueOf(nineteen));
        CHECK(deserialize(twenty).bytes == valueOf(twenty));
    }

    TEST_CASE("aloracle: deserialize holds to the parsers' default depth", "[aloracle]")
    {
        CHECK(deserialize(nested(256)).status == ALORACLE_OK);
        CHECK(deserialize(nested(257)).status == ALORACLE_FAILED);
    }

    TEST_CASE("aloracle: serialize writes the format's header line first", "[aloracle]")
    {
        const std::string value = valueOf("{'a':i1}");
        CHECK(serialize(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_NONE, value).bytes
              == "<? llsd/notation ?>\n{'a':i1}");
        CHECK(serialize(ALORACLE_LLSD_XML, ALORACLE_LLSD_OPTIONS_NONE, value).bytes
              == "<? LLSD/XML ?>\n<llsd><map><key>a</key><integer>1</integer></map></llsd>\n");
        CHECK(serialize(ALORACLE_LLSD_BINARY, ALORACLE_LLSD_OPTIONS_NONE, value).bytes
              == "<? LLSD/Binary ?>\n" + value);

        for (int32_t type : { ALORACLE_LLSD_BINARY, ALORACLE_LLSD_XML, ALORACLE_LLSD_NOTATION })
        {
            CAPTURE(type);
            const Result text =
                serialize(type, ALORACLE_LLSD_OPTIONS_PRETTY | ALORACLE_LLSD_OPTIONS_PRETTY_BINARY, value);
            CHECK(text.status == ALORACLE_OK);
            CHECK(deserialize(text.bytes).bytes == value);
        }
    }

    TEST_CASE("aloracle: the LLSD functions refuse what is not theirs to read", "[aloracle]")
    {
        const std::string value = valueOf("{'a':i1}");
        const std::string text = "i1";
        aloracle_bytes out{};

        CHECK(parse(3, text).status == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_llsd_parse(ALORACLE_LLSD_NOTATION, in(text), text.size(), -1, -1, nullptr, nullptr)
              == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_llsd_parse(ALORACLE_LLSD_NOTATION, nullptr, 2, -1, -1, &out, nullptr)
              == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_llsd_deserialize(nullptr, 2, -1, &out) == ALORACLE_BAD_ARGUMENT);

        CHECK(format(3, ALORACLE_LLSD_OPTIONS_NONE, value).status == ALORACLE_BAD_ARGUMENT);
        CHECK(format(ALORACLE_LLSD_NOTATION, 4, value).status == ALORACLE_BAD_ARGUMENT);
        CHECK(serialize(3, ALORACLE_LLSD_OPTIONS_NONE, value).status == ALORACLE_BAD_ARGUMENT);

        // A value passed in is one LLSD binary value and nothing more.
        CHECK(format(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_NONE, "{'a':i1}").status
              == ALORACLE_BAD_ARGUMENT);
        CHECK(format(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_NONE, value + "!").status
              == ALORACLE_BAD_ARGUMENT);
        CHECK(format(ALORACLE_LLSD_NOTATION, ALORACLE_LLSD_OPTIONS_NONE, "").status
              == ALORACLE_BAD_ARGUMENT);

        // What it is given is emptied whatever the status.
        uint8_t held = 0;
        out = aloracle_bytes{ &held, 1 };
        CHECK(aloracle_llsd_format(3, ALORACLE_LLSD_OPTIONS_NONE, in(value), value.size(), &out)
              == ALORACLE_BAD_ARGUMENT);
        CHECK(out.data == nullptr);
        CHECK(out.size == 0);
    }

    TEST_CASE("aloracle: escape leaves the set it is given as it is", "[aloracle]")
    {
        CHECK(escape(ALORACLE_LLURI_UNRESERVED, "a b/c~").bytes == "a%20b%2Fc~");
        CHECK(escape(ALORACLE_LLURI_PATH_COMPONENT, "a b/c@d").bytes == "a%20b%2Fc@d");
        CHECK(escape(ALORACLE_LLURI_QUERY_VARIABLE, "a=b&c").bytes == "a%3Db%26c");
        CHECK(escape(ALORACLE_LLURI_QUERY_VALUE, "a=b&c").bytes == "a=b%26c");
        CHECK(escape(ALORACLE_LLURI_URI_PATH, "a b/c?d").bytes == "a%20b/c?d");

        // The bytes in are not NUL-terminated, and every one is escaped.
        CHECK(escape(ALORACLE_LLURI_UNRESERVED, "a\0b\xFF"s).bytes == "a%00b%FF");
        CHECK(escape(ALORACLE_LLURI_UNRESERVED, "").status == ALORACLE_OK);

        CHECK(escape(-1, "a").status == ALORACLE_BAD_ARGUMENT);
        CHECK(escape(ALORACLE_LLURI_URI_PATH + 1, "a").status == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: unescape decodes each escape and leaves a plus alone", "[aloracle]")
    {
        CHECK(unescape("a%20b%2fc+d").bytes == "a b/c+d");
        CHECK(unescape("%00").bytes == "\0"s);
    }

    TEST_CASE("aloracle: unescape leaves a malformed escape as it is", "[aloracle]")
    {
        CHECK(unescape("%").bytes == "%");
        CHECK(unescape("%4g").bytes == "%4g");
        CHECK(unescape("100%").bytes == "100%");
        CHECK(unescape("%%41").bytes == "%A");
    }

    TEST_CASE("aloracle: queryMap reads a query into a map", "[aloracle]")
    {
        const Result value = queryMap("a=1&b=x%20y&c");
        CHECK(value.status == ALORACLE_OK);
        CHECK(value.bytes == valueOf("{'a':'1','b':'x y','c':true}"));
    }

    TEST_CASE("aloracle: queryMap reads a query with no keys as an empty map", "[aloracle]")
    {
        CHECK(queryMap("").bytes == valueOf("{}"));
        CHECK(queryMap("&&").bytes == valueOf("{}"));
    }

    TEST_CASE("aloracle: mapToQueryString writes a map as a query in key order", "[aloracle]")
    {
        CHECK(mapToQueryString(valueOf("{'c':!,'b':'x y','a':i1}")).bytes == "?a=1&b=x%20y&c");
        CHECK(mapToQueryString("{'a':i1}").status == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a date is written in ISO-8601 to the hundredth", "[aloracle]")
    {
        CHECK(dateFormat(0.0).bytes == "1970-01-01T00:00:00Z");
        CHECK(dateFormat(1791955800.0).bytes == "2026-10-14T05:30:00Z");
        CHECK(dateFormat(1.5).bytes == "1970-01-01T00:00:01.50Z");
        CHECK(dateFormat(-1.5).bytes == "1969-12-31T23:59:58.50Z");
    }

    TEST_CASE("aloracle: a date is read from ISO-8601 with its zone", "[aloracle]")
    {
        const auto read = [](const std::string& text, double& seconds)
        {
            return aloracle_lldate_parse(in(text), text.size(), &seconds);
        };

        double seconds = -1.0;
        CHECK(read("2026-10-14T05:30:00Z", seconds) == ALORACLE_OK);
        CHECK(seconds == 1791955800.0);
        CHECK(read("2026-10-14T06:30:00+01:00", seconds) == ALORACLE_OK);
        CHECK(seconds == 1791955800.0);
        CHECK(read("1970-01-01T00:00:01.25Z", seconds) == ALORACLE_OK);
        CHECK(seconds == 1.25);

        // A date that does not parse is the epoch.
        CHECK(read("2026-10-14", seconds) == ALORACLE_FAILED);
        CHECK(seconds == 0.0);
        CHECK(aloracle_lldate_parse(nullptr, 0, nullptr) == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a float quantized to eight bits floors or rounds as asked", "[aloracle]")
    {
        CHECK(aloracle_f32_to_u8(0.5f, 0.f, 1.f) == 127);
        CHECK(aloracle_f32_to_u8_round(0.5f, 0.f, 1.f) == 128);
        CHECK(aloracle_f32_to_u8(2.f, 0.f, 1.f) == 255);
        CHECK(aloracle_f32_to_u8(-1.f, 0.f, 1.f) == 0);

        CHECK(aloracle_u8_to_f32(0, -1.f, 1.f) == -1.f);
        // Within one step of zero comes back as zero.
        CHECK(aloracle_u8_to_f32(127, -1.f, 1.f) == 0.f);
    }

    TEST_CASE("aloracle: a float quantized to sixteen bits floors or rounds as asked", "[aloracle]")
    {
        CHECK(aloracle_f32_to_u16(0.f, -1.f, 1.f) == 32767);
        CHECK(aloracle_f32_to_u16_round(0.f, -1.f, 1.f) == 32768);
        CHECK(aloracle_f32_to_u16(5.f, -1.f, 1.f) == 65535);

        CHECK(aloracle_u16_to_f32(0, -128.f, 128.f) == -128.f);
        CHECK(aloracle_u16_to_f32(32767, -1.f, 1.f) == 0.f);
    }

    TEST_CASE("aloracle: freeing empty bytes or none does nothing", "[aloracle]")
    {
        aloracle_bytes empty{};
        aloracle_bytes_free(&empty);
        CHECK(empty.data == nullptr);
        CHECK(empty.size == 0);
        aloracle_bytes_free(nullptr);
    }

    TEST_CASE("aloracle: calls from several threads at once give what one thread gets", "[aloracle]")
    {
        const std::string xml = "<llsd><map><key>a</key><array><real>2.5</real><string>x y</string></array></map></llsd>";
        const std::string value = parse(ALORACLE_LLSD_XML, xml).bytes;
        const std::string escaped = escape(ALORACLE_LLURI_UNRESERVED, "x y/z").bytes;
        const std::string date = dateFormat(1791955800.5).bytes;

        std::vector<std::future<int>> workers;
        for (int i = 0; i < 8; ++i)
        {
            workers.push_back(std::async(std::launch::async, [&]
            {
                int disagreements = 0;
                for (int j = 0; j < 200; ++j)
                {
                    if (parse(ALORACLE_LLSD_XML, xml).bytes != value)
                    {
                        ++disagreements;
                    }
                    if (escape(ALORACLE_LLURI_UNRESERVED, "x y/z").bytes != escaped)
                    {
                        ++disagreements;
                    }
                    if (dateFormat(1791955800.5).bytes != date)
                    {
                        ++disagreements;
                    }
                }
                return disagreements;
            }));
        }
        for (std::future<int>& worker : workers)
        {
            CHECK(worker.get() == 0);
        }
    }
}
