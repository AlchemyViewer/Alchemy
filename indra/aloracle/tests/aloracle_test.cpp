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

    Result convert(int32_t type, const std::string& value)
    {
        aloracle_bytes converted{};
        const int32_t status = aloracle_llsd_convert(type, in(value), value.size(), &converted);
        return { status, taken(converted) };
    }

    Result zip(const std::string& value)
    {
        aloracle_bytes zipped{};
        const int32_t status = aloracle_llsd_zip(in(value), value.size(), &zipped);
        return { status, taken(zipped) };
    }

    Result unzip(const std::string& data, int32_t* result = nullptr)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_llsd_unzip(in(data), data.size(), &value, result);
        return { status, taken(value) };
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

    Result zerocodeEncode(const std::string& packet, uint32_t header_size = 6)
    {
        aloracle_bytes encoded{};
        const int32_t status = aloracle_zerocode_encode(in(packet), packet.size(), header_size, &encoded);
        return { status, taken(encoded) };
    }

    Result zerocodeDecode(const std::string& packet, uint32_t capacity = 4096, uint32_t header_size = 6)
    {
        aloracle_bytes expanded{};
        const int32_t status =
            aloracle_zerocode_decode(in(packet), packet.size(), capacity, header_size, &expanded);
        return { status, taken(expanded) };
    }

    // A template the message tests share, with one message of each frequency,
    // each block type, and a message the parser drops.
    const std::string MESSAGE_TEMPLATE = R"(version 2.0

// The tokenizer skips a line from its slash.
{
    TestHigh High 1 NotTrusted Unencoded
    {
        AgentData Single
        { AgentID LLUUID }
        { Flags U8 }
    }
    {
        Items Variable
        { Value U32 }
        { Name Variable 1 }
    }
}
{
    TestMedium Medium 3 Trusted Zerocoded
    {
        Data Single
        { Position LLVector3 }
    }
}
{
    TestLow Low 300 NotTrusted Unencoded
    {
        Pairs Multiple 2
        { Key U16 }
        { Blob Fixed 3 }
        { Text Variable 2 }
    }
}
{
    TestFixed Fixed 0xFFFFFFFB NotTrusted Unencoded UDPDeprecated
}
{
    TestGone Low 5 NotTrusted Unencoded Deprecated
}
{
    TestAllVariable Low 6 NotTrusted Unencoded
    {
        First Variable
        { A U8 }
    }
    {
        Second Variable
        { B U8 }
    }
}
{
    TestBlocked Low 7 NotTrusted Unencoded UDPBlackListed
}
)";

    // A template read from text, freed with the scope.
    struct MessageTemplate
    {
        explicit MessageTemplate(const std::string& text = MESSAGE_TEMPLATE)
        {
            status = aloracle_message_template_new(in(text), text.size(), &handle);
        }

        ~MessageTemplate()
        {
            aloracle_message_template_free(handle);
        }

        MessageTemplate(const MessageTemplate&) = delete;
        MessageTemplate& operator=(const MessageTemplate&) = delete;

        aloracle_message_template* handle = nullptr;
        int32_t status = -1;
    };

    // A message read and written back: the call's status, what was written
    // back, and what the read did.
    struct MessageRead
    {
        int32_t status;
        std::string written;
        int32_t result;
    };

    MessageRead readMessage(const MessageTemplate& message_template, const std::string& packet)
    {
        aloracle_bytes written{};
        int32_t result = -1;
        const int32_t status =
            aloracle_message_read(message_template.handle, in(packet), packet.size(), &written, &result);
        return { status, taken(written), result };
    }

    // A packet's six header bytes, flags, sequence number 1 and offset, then
    // the body from the message number on.
    std::string packetOf(const std::string& body, char offset = 0)
    {
        return "\x00\x00\x00\x00\x01"s + offset + body;
    }

    TEST_CASE("aloracle: the library gives the interface version it was built with", "[aloracle]")
    {
        CHECK(aloracle_abi_version() == ALORACLE_ABI_VERSION);
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

    TEST_CASE("aloracle: convert reads a value as another type with LLSD's own conversions", "[aloracle]")
    {
        CHECK(convert(ALORACLE_LLSD_TYPE_STRING, valueOf("r2.5")).bytes == valueOf("'2.5'"));
        CHECK(convert(ALORACLE_LLSD_TYPE_STRING, valueOf("r0.1234567")).bytes == valueOf("'0.123457'"));
        CHECK(convert(ALORACLE_LLSD_TYPE_INTEGER, valueOf("'1.9'")).bytes == valueOf("i1"));
        CHECK(convert(ALORACLE_LLSD_TYPE_BOOLEAN, valueOf("'false'")).bytes == valueOf("true"));
        CHECK(convert(ALORACLE_LLSD_TYPE_BOOLEAN, valueOf("i0")).bytes == valueOf("false"));
        CHECK(convert(ALORACLE_LLSD_TYPE_STRING, valueOf("d\"2006-04-24T01:02:03Z\"")).bytes
              == valueOf("'2006-04-24T01:02:03Z'"));
        CHECK(convert(ALORACLE_LLSD_TYPE_UUID, valueOf("'not a uuid'")).bytes
              == valueOf("u00000000-0000-0000-0000-000000000000"));
        CHECK(convert(ALORACLE_LLSD_TYPE_BINARY, valueOf("'text'")).bytes == valueOf("b(0)\"\""));
    }

    TEST_CASE("aloracle: convert refuses a type that has no conversion", "[aloracle]")
    {
        const std::string value = valueOf("i1");
        CHECK(convert(ALORACLE_LLSD_TYPE_UNDEFINED, value).status == ALORACLE_BAD_ARGUMENT);
        CHECK(convert(ALORACLE_LLSD_TYPE_MAP, value).status == ALORACLE_BAD_ARGUMENT);
        CHECK(convert(ALORACLE_LLSD_TYPE_ARRAY, value).status == ALORACLE_BAD_ARGUMENT);
        CHECK(convert(11, value).status == ALORACLE_BAD_ARGUMENT);
        CHECK(convert(ALORACLE_LLSD_TYPE_STRING, "i1").status == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a zipped value unzips as it was", "[aloracle]")
    {
        const std::string value = valueOf("{'a':i1,'b':[r2.5,'x',!]}");
        const Result zipped = zip(value);
        REQUIRE(zipped.status == ALORACLE_OK);
        // A zlib header, not gzip's.
        CHECK(zipped.bytes.substr(0, 1) == "\x78");

        int32_t result = -1;
        const Result unzipped = unzip(zipped.bytes, &result);
        CHECK(unzipped.status == ALORACLE_OK);
        CHECK(result == 0);
        CHECK(unzipped.bytes == value);
    }

    TEST_CASE("aloracle: unzip fails what is not zlib with unzip_llsd's result", "[aloracle]")
    {
        int32_t result = -1;
        CHECK(unzip("not zlib", &result).status == ALORACLE_FAILED);
        CHECK(result == 3); // ZR_DATA_ERROR

        const Result zipped = zip(valueOf("'some text to zip'"));
        REQUIRE(zipped.status == ALORACLE_OK);
        result = -1;
        CHECK(unzip(zipped.bytes.substr(0, zipped.bytes.size() / 2), &result).status == ALORACLE_FAILED);
        CHECK(result == 5); // ZR_BUFFER_ERROR

        CHECK(unzip("").status == ALORACLE_FAILED);
        CHECK(aloracle_llsd_unzip(nullptr, 2, nullptr, nullptr) == ALORACLE_BAD_ARGUMENT);
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

    TEST_CASE("aloracle: zerocoding replaces each run of zeros with a count", "[aloracle]")
    {
        const std::string packet = packetOf("\x01\x00\x00\x00\x02"s);
        const Result encoded = zerocodeEncode(packet);
        REQUIRE(encoded.status == ALORACLE_OK);
        CHECK(encoded.bytes == "\x80\x00\x00\x00\x01\x00" "\x01\x00\x03\x02"s);

        const Result decoded = zerocodeDecode(encoded.bytes);
        CHECK(decoded.status == ALORACLE_OK);
        CHECK(decoded.bytes == packet);
    }

    TEST_CASE("aloracle: zerocoding splits a run of zeros every 255", "[aloracle]")
    {
        const std::string packet = packetOf(std::string(300, '\0'));
        const Result encoded = zerocodeEncode(packet);
        REQUIRE(encoded.status == ALORACLE_OK);
        CHECK(encoded.bytes == "\x80\x00\x00\x00\x01\x00" "\x00\xFF\x00\x2D"s);

        const Result decoded = zerocodeDecode(encoded.bytes);
        CHECK(decoded.status == ALORACLE_OK);
        CHECK(decoded.bytes == packet);
    }

    TEST_CASE("aloracle: zerocoding fails a packet it would not shrink", "[aloracle]")
    {
        CHECK(zerocodeEncode(packetOf("\x01\x02\x03")).status == ALORACLE_FAILED);
        // A zero on its own takes two bytes.
        CHECK(zerocodeEncode(packetOf("\x01\x00\x02"s)).status == ALORACLE_FAILED);
        CHECK(zerocodeEncode("\x00\x00"s).status == ALORACLE_FAILED);
        CHECK(zerocodeEncode("").status == ALORACLE_FAILED);
    }

    TEST_CASE("aloracle: zero decoding leaves a packet with no flag or no body unwritten", "[aloracle]")
    {
        const Result plain = zerocodeDecode(packetOf("\x01\x00\x03"s));
        CHECK(plain.status == ALORACLE_OK);
        CHECK(plain.bytes.empty());

        const Result short_packet = zerocodeDecode("\x80\x00"s);
        CHECK(short_packet.status == ALORACLE_OK);
        CHECK(short_packet.bytes.empty());

        const Result nothing = zerocodeDecode("", 0, 0);
        CHECK(nothing.status == ALORACLE_OK);
        CHECK(nothing.bytes.empty());
    }

    TEST_CASE("aloracle: zero decoding reads a zero count as 256 more", "[aloracle]")
    {
        const Result decoded = zerocodeDecode("\x80\x00\x00\x00\x01\x00" "\x00\x00\x05\x07"s);
        CHECK(decoded.status == ALORACLE_OK);
        CHECK(decoded.bytes == packetOf(std::string(261, '\0') + "\x07"));
    }

    TEST_CASE("aloracle: zero decoding fails a run past its capacity", "[aloracle]")
    {
        const std::string encoded = "\x80\x00\x00\x00\x01\x00" "\x00\x0A"s;
        // decode wants a byte of room past the last run.
        CHECK(zerocodeDecode(encoded, 16).status == ALORACLE_FAILED);
        const Result room = zerocodeDecode(encoded, 17);
        CHECK(room.status == ALORACLE_OK);
        CHECK(room.bytes == packetOf(std::string(10, '\0')));

        CHECK(zerocodeDecode("\x80\x00\x00\x00\x01\x00" "\x01\x02\x03"s, 8).status == ALORACLE_FAILED);
    }

    TEST_CASE("aloracle: zerocoding refuses what its C++ cannot be given", "[aloracle]")
    {
        CHECK(zerocodeDecode(packetOf("\x01"), 5).status == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_zerocode_encode(nullptr, 3, 0, nullptr) == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_zerocode_decode(nullptr, 3, 64, 0, nullptr) == ALORACLE_BAD_ARGUMENT);
        aloracle_bytes out{};
        CHECK(aloracle_zerocode_encode(nullptr, 3, 0, &out) == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_zerocode_decode(nullptr, 3, 64, 0, &out) == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a template is described as its parser made it", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        aloracle_bytes value{};
        REQUIRE(aloracle_message_template_describe(messages.handle, &value) == ALORACLE_OK);
        const std::string described = taken(value);

        // TestGone is Deprecated, which the parser drops; Low and Fixed
        // numbers keep their 0xFFFF and 0xFFFFFFFF tops.
        const std::string expected = valueOf(
            "{'version':r2,'messages':["
            "{'name':'TestHigh','number':i1,'frequency':i1,'trust':i1,'encoding':i0,'deprecation':i0,'blocks':["
            "{'name':'AgentData','type':i1,'number':i1,'variables':["
            "{'name':'AgentID','type':i17,'size':i16},{'name':'Flags','type':i3,'size':i1}]},"
            "{'name':'Items','type':i3,'number':i1,'variables':["
            "{'name':'Value','type':i5,'size':i4},{'name':'Name','type':i2,'size':i1}]}]},"
            "{'name':'TestMedium','number':i65283,'frequency':i2,'trust':i0,'encoding':i1,'deprecation':i0,'blocks':["
            "{'name':'Data','type':i1,'number':i1,'variables':[{'name':'Position','type':i13,'size':i12}]}]},"
            "{'name':'TestLow','number':i-65236,'frequency':i4,'trust':i1,'encoding':i0,'deprecation':i0,'blocks':["
            "{'name':'Pairs','type':i2,'number':i2,'variables':["
            "{'name':'Key','type':i4,'size':i2},{'name':'Blob','type':i1,'size':i3},"
            "{'name':'Text','type':i2,'size':i2}]}]},"
            "{'name':'TestFixed','number':i-5,'frequency':i4,'trust':i1,'encoding':i0,'deprecation':i1,'blocks':[]},"
            "{'name':'TestAllVariable','number':i-65530,'frequency':i4,'trust':i1,'encoding':i0,'deprecation':i0,"
            "'blocks':["
            "{'name':'First','type':i3,'number':i1,'variables':[{'name':'A','type':i3,'size':i1}]},"
            "{'name':'Second','type':i3,'number':i1,'variables':[{'name':'B','type':i3,'size':i1}]}]},"
            "{'name':'TestBlocked','number':i-65529,'frequency':i4,'trust':i1,'encoding':i0,'deprecation':i2,"
            "'blocks':[]}"
            "]}");
        CHECK(described == expected);
    }

    TEST_CASE("aloracle: a message reads and writes back to its bytes", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        const std::string high = "\x01"s + std::string(16, '\x11') + "\x07"
            + "\x02" + "\x01\x00\x00\x00"s + "\x02" "ab" + "\x02\x00\x00\x00"s + "\x00"s;
        const std::string low = "\xFF\xFF\x01\x2C"s + "\x01\x00" "abc" "\x02\x00" "hi"s + "\x02\x00" "def" "\x00\x00"s;
        const std::string fixed = "\xFF\xFF\xFF\xFB";
        const std::string medium = "\xFF\x03"s + std::string(12, '\x42');
        for (const std::string& body : { high, low, fixed, medium })
        {
            const MessageRead read = readMessage(messages, packetOf(body));
            CHECK(read.status == ALORACLE_OK);
            CHECK(read.result == ALORACLE_MESSAGE_READ);
            CHECK(read.written == body);
        }
    }

    TEST_CASE("aloracle: a message writes back without the bytes its offset skips", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        const std::string position(12, '\x42');
        const MessageRead read = readMessage(messages, packetOf("\xFF\x03\xEE" + position, 1));
        CHECK(read.result == ALORACLE_MESSAGE_READ);
        CHECK(read.written == "\xFF\x03" + position);
    }

    TEST_CASE("aloracle: a body cut short writes back with zeros", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        // A fixed field the packet ends inside is all zeros, not what came of it.
        const MessageRead medium = readMessage(messages, packetOf("\xFF\x03\x01\x02\x03\x04\x05"));
        CHECK(medium.status == ALORACLE_OK);
        CHECK(medium.result == ALORACLE_MESSAGE_READ);
        CHECK(medium.written == "\xFF\x03"s + std::string(12, '\0'));

        // A Variable block the packet ends before has no repeats.
        const MessageRead high = readMessage(messages, packetOf("\x01"s + std::string(16, '\x11') + "\x07"));
        CHECK(high.result == ALORACLE_MESSAGE_READ);
        CHECK(high.written == "\x01"s + std::string(16, '\x11') + "\x07" + "\x00"s);
    }

    TEST_CASE("aloracle: a variable field whose length runs past the end writes back empty", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        const std::string first = "\x01\x00" "abc" "\x02\x00" "hi"s;
        const MessageRead read =
            readMessage(messages, packetOf("\xFF\xFF\x01\x2C"s + first + "\x02\x00" "def" "\x00\x01" "zz"s));
        CHECK(read.status == ALORACLE_OK);
        CHECK(read.result == ALORACLE_MESSAGE_READ);
        CHECK(read.written == "\xFF\xFF\x01\x2C"s + first + "\x02\x00" "def" "\x00\x00"s);
    }

    TEST_CASE("aloracle: validateMessage refuses an unknown number or one too short for its frequency", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        for (const std::string& body : { "\x09"s, "\xFF\xFF\x00\x63"s, "\xFF"s, "\xFF\xFF\x01"s, "\xFF\xFF\x00\x07"s })
        {
            const MessageRead read = readMessage(messages, packetOf(body));
            CHECK(read.status == ALORACLE_OK);
            CHECK(read.result == ALORACLE_MESSAGE_REFUSED);
            CHECK(read.written.empty());
        }
    }

    TEST_CASE("aloracle: decodeData drops a message whose Variable blocks all have none", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        for (const std::string& body : { "\xFF\xFF\x00\x06"s, "\xFF\xFF\x00\x06\x00\x00"s })
        {
            const MessageRead read = readMessage(messages, packetOf(body));
            CHECK(read.status == ALORACLE_OK);
            CHECK(read.result == ALORACLE_MESSAGE_DROPPED);
            CHECK(read.written.empty());
        }

        const MessageRead one = readMessage(messages, packetOf("\xFF\xFF\x00\x06\x01\x05"s));
        CHECK(one.result == ALORACLE_MESSAGE_READ);
        CHECK(one.written == "\xFF\xFF\x00\x06\x01\x05\x00"s);
    }

    TEST_CASE("aloracle: a message read refuses what the message log does not read", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        CHECK(readMessage(messages, "\x00\x00\x00\x00\x01\x00"s).status == ALORACLE_BAD_ARGUMENT);
        const std::string packet = packetOf("\x09"s);
        aloracle_bytes written{};
        CHECK(aloracle_message_read(nullptr, in(packet), packet.size(), &written, nullptr) == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_message_read(messages.handle, in(packet), packet.size(), nullptr, nullptr)
              == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_message_read(messages.handle, nullptr, 7, &written, nullptr) == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a template that names a message twice or will not parse is fatal", "[aloracle]")
    {
        const MessageTemplate twice("version 2.0\n{ TestTwice Low 10 NotTrusted Unencoded }\n"
                                    "{ TestTwice Low 11 NotTrusted Unencoded }\n");
        CHECK(twice.status == ALORACLE_FATAL);
        CHECK(twice.handle == nullptr);

        const MessageTemplate frequency("version 2.0\n{ TestOften Often 1 NotTrusted Unencoded }\n");
        CHECK(frequency.status == ALORACLE_FATAL);
        CHECK(frequency.handle == nullptr);

        const MessageTemplate unversioned("{ TestUnversioned Low 12 NotTrusted Unencoded }\n");
        CHECK(unversioned.status == ALORACLE_FATAL);

        const MessageTemplate unclosed("version 2.0\n{ TestUnclosed Low 13 NotTrusted Unencoded\n");
        CHECK(unclosed.status == ALORACLE_FATAL);

        CHECK(aloracle_message_template_new(nullptr, 0, nullptr) == ALORACLE_BAD_ARGUMENT);
        aloracle_bytes value{};
        CHECK(aloracle_message_template_describe(nullptr, &value) == ALORACLE_BAD_ARGUMENT);
        aloracle_message_template_free(nullptr);
    }

    TEST_CASE("aloracle: a packet over 2^31 - 1 bytes is refused before it is read", "[aloracle]")
    {
        // Seven bytes, passed as more than the C++ can be given: a read of the
        // size claimed would run far past them.
        const std::string packet = packetOf("\x09"s);
        const size_t oversized = size_t(INT32_MAX) + 1;
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        // What each is given is emptied though it refuses.
        uint8_t held = 0;
        aloracle_bytes out{ &held, 1 };
        int32_t result = -1;
        CHECK(aloracle_zerocode_encode(in(packet), oversized, 6, &out) == ALORACLE_BAD_ARGUMENT);
        CHECK(out.data == nullptr);
        CHECK(out.size == 0);
        out = aloracle_bytes{ &held, 1 };
        CHECK(aloracle_zerocode_decode(in(packet), oversized, 64, 6, &out) == ALORACLE_BAD_ARGUMENT);
        CHECK(out.data == nullptr);
        CHECK(out.size == 0);
        out = aloracle_bytes{ &held, 1 };
        CHECK(aloracle_message_read(messages.handle, in(packet), oversized, &out, &result) == ALORACLE_BAD_ARGUMENT);
        CHECK(out.data == nullptr);
        CHECK(out.size == 0);
        CHECK(result == -1);
    }

    TEST_CASE("aloracle: a template that could fill the string table is refused before it is parsed", "[aloracle]")
    {
        // More distinct tokens than the table has room for. A parse would
        // intern TestCrowded and then come back fatal at the words after it,
        // so a refusal shows nothing was interned.
        std::string crowded = "version 2.0\n{ TestCrowded Low 14 NotTrusted Unencoded }\n";
        for (int i = 0; i < 9000; ++i)
        {
            crowded += "word" + std::to_string(i) + (i % 16 == 15 ? "\n" : " ");
        }
        const MessageTemplate refused(crowded);
        CHECK(refused.status == ALORACLE_BAD_ARGUMENT);
        CHECK(refused.handle == nullptr);

        const MessageTemplate messages;
        CHECK(messages.status == ALORACLE_OK);
    }

    TEST_CASE("aloracle: an LL_ERRS comes back fatal while other threads log", "[aloracle]")
    {
        // The notation parser logs each character it does not know, and the
        // template's frequency is one the parser calls LL_ERRS on.
        const std::string unknown = "?";
        const std::string often = "version 2.0\n{ TestOften Often 1 NotTrusted Unencoded }\n";

        std::vector<std::future<int>> workers;
        for (int i = 0; i < 8; ++i)
        {
            workers.push_back(std::async(std::launch::async, [&, i]
            {
                int disagreements = 0;
                for (int j = 0; j < 500; ++j)
                {
                    if (i % 2)
                    {
                        if (parse(ALORACLE_LLSD_NOTATION, unknown).status != ALORACLE_FAILED)
                        {
                            ++disagreements;
                        }
                    }
                    else
                    {
                        const MessageTemplate messages(often);
                        if (messages.status != ALORACLE_FATAL)
                        {
                            ++disagreements;
                        }
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

    TEST_CASE("aloracle: templates read and messages read from several threads at once agree", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);
        aloracle_bytes value{};
        REQUIRE(aloracle_message_template_describe(messages.handle, &value) == ALORACLE_OK);
        const std::string described = taken(value);
        const std::string body = "\xFF\xFF\x01\x2C"s + "\x01\x00" "abc" "\x02\x00" "hi"s + "\x02\x00" "def" "\x00\x00"s;

        std::vector<std::future<int>> workers;
        for (int i = 0; i < 8; ++i)
        {
            workers.push_back(std::async(std::launch::async, [&]
            {
                int disagreements = 0;
                for (int j = 0; j < 50; ++j)
                {
                    const MessageTemplate mine;
                    aloracle_bytes mine_value{};
                    if (mine.status != ALORACLE_OK
                        || aloracle_message_template_describe(mine.handle, &mine_value) != ALORACLE_OK
                        || taken(mine_value) != described)
                    {
                        ++disagreements;
                    }
                    if (readMessage(messages, packetOf(body)).written != body
                        || readMessage(mine, packetOf(body)).written != body)
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
