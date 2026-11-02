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

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
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

    Result llsdAsXmlrpc(const std::string& value)
    {
        aloracle_bytes text{};
        const int32_t status = aloracle_llsd_as_xmlrpc(in(value), value.size(), &text);
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

    Result messageFromLLSD(const MessageTemplate& message_template, const std::string& name, const std::string& body)
    {
        aloracle_bytes written{};
        const int32_t status = aloracle_message_from_llsd(message_template.handle, in(name), name.size(), in(body),
                                                          body.size(), &written);
        return { status, taken(written) };
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

    TEST_CASE("aloracle: an LLSD value writes as an XML-RPC value", "[aloracle]")
    {
        CHECK(llsdAsXmlrpc(valueOf("{'b':[r2.5,'x&y',!],'a':i1}")).bytes
              == "<value><struct><member><name>a</name><value><int>1</int></value></member>"
                 "<member><name>b</name><value><array><data><value><double>2.5</double></value>"
                 "<value><string>x&amp;y</string></value><value><nil/></value></data></array></value>"
                 "</member></struct></value>");
        CHECK(llsdAsXmlrpc(valueOf("[true,b16\"0102\",u11111111-1111-1111-1111-111111111111]")).bytes
              == "<value><array><data><value><boolean>1</boolean></value><value><base64>AQI=</base64></value>"
                 "<value><string>11111111-1111-1111-1111-111111111111</string></value></data></array></value>");
        CHECK(llsdAsXmlrpc("{"s).status == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_llsd_as_xmlrpc(nullptr, 1, nullptr) == ALORACLE_BAD_ARGUMENT);
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

    TEST_CASE("aloracle: a message body reads from LLSD with the reader's getter for each field", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        // A U32 reads from binary in network order, so an integer reads as 0;
        // a Variable field from a string with its NUL, or from binary as it is.
        const Result high = messageFromLLSD(messages, "TestHigh",
                                            valueOf("{'AgentData':[{'AgentID':u11111111-1111-1111-1111-111111111111,"
                                                    "'Flags':i7}],'Items':[{'Value':b16\"00000001\",'Name':'ab'},"
                                                    "{'Value':i2,'Name':b16\"0102\"}]}"));
        CHECK(high.status == ALORACLE_OK);
        CHECK(high.bytes
              == "\x01"s + std::string(16, '\x11') + "\x07" + "\x02" + "\x01\x00\x00\x00"s + "\x03" "ab\x00"s
                     + "\x00\x00\x00\x00"s + "\x02\x01\x02");

        // The fields an event queue's TeleportFinish carries.
        const MessageTemplate info("version 2.0\n{ TestInfo Low 20 Trusted Unencoded\n{ Info Single\n"
                                   "{ SimIP IPADDR }\n{ SimPort IPPORT }\n{ Handle U64 }\n{ Seed Variable 2 }\n"
                                   "{ Flags U32 }\n}\n}\n");
        REQUIRE(info.status == ALORACLE_OK);
        const Result finish = messageFromLLSD(info, "TestInfo",
                                              valueOf("{'Info':[{'SimIP':b16\"7F000001\",'SimPort':i13000,"
                                                      "'Handle':b16\"0000000100000002\",'Seed':'http://x/',"
                                                      "'Flags':b16\"00000010\"}]}"));
        CHECK(finish.status == ALORACLE_OK);
        CHECK(finish.bytes
              == "\xFF\xFF\x00\x14"s + "\x7F\x00\x00\x01"s + "\x32\xC8" + "\x02\x00\x00\x00\x01\x00\x00\x00"s
                     + "\x0A\x00" "http://x/\x00"s + "\x10\x00\x00\x00"s);
    }

    TEST_CASE("aloracle: a message body from LLSD reads what it lacks as the getters' defaults", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        // A Single block reads once, and a Variable block as often as it comes.
        const Result empty = messageFromLLSD(messages, "TestHigh", valueOf("{}"));
        CHECK(empty.status == ALORACLE_OK);
        CHECK(empty.bytes == "\x01"s + std::string(16, '\x00') + "\x00\x00"s);

        // A Multiple block reads the template's number, and a Fixed field
        // whose value is binary of another size reads as zeros.
        const Result low = messageFromLLSD(messages, "TestLow",
                                           valueOf("{'Pairs':[{'Key':i1,'Blob':b16\"616263\",'Text':'hi'},"
                                                   "{'Key':i2,'Blob':b16\"6162\"}]}"));
        CHECK(low.status == ALORACLE_OK);
        CHECK(low.bytes
              == "\xFF\xFF\x01\x2C"s + "\x01\x00" "abc" "\x03\x00" "hi\x00"s + "\x02\x00"s + std::string(3, '\x00')
                     + "\x00\x00"s);
    }

    TEST_CASE("aloracle: a message body from LLSD refuses a name its template lacks", "[aloracle]")
    {
        const MessageTemplate messages;
        REQUIRE(messages.status == ALORACLE_OK);

        const std::string body = valueOf("{}");
        CHECK(messageFromLLSD(messages, "TestNone", body).status == ALORACLE_BAD_ARGUMENT);
        CHECK(messageFromLLSD(messages, "TestHigh\0"s, body).status == ALORACLE_BAD_ARGUMENT);
        CHECK(messageFromLLSD(messages, "TestHigh", "{"s).status == ALORACLE_BAD_ARGUMENT);
        aloracle_bytes written{};
        CHECK(aloracle_message_from_llsd(nullptr, in("TestHigh"s), 8, in(body), body.size(), &written)
              == ALORACLE_BAD_ARGUMENT);

        // A field of a type the reader has no getter for.
        const MessageTemplate wide("version 2.0\n{ TestWide Low 21 NotTrusted Unencoded\n{ Data Single\n"
                                   "{ Big S64 }\n}\n}\n");
        REQUIRE(wide.status == ALORACLE_OK);
        CHECK(messageFromLLSD(wide, "TestWide", body).status == ALORACLE_BAD_ARGUMENT);

        // More repeats of a Variable block than the builder packs.
        std::string many = "{'Items':[";
        for (int item = 0; item < 256; ++item)
        {
            many += (item ? "," : "") + "{'Value':b16\"00000001\",'Name':'a'}"s;
        }
        CHECK(messageFromLLSD(messages, "TestHigh", valueOf(many + "]}")).status == ALORACLE_FATAL);
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

    // A float's four bytes, an unsigned's two and a signed's four, as the
    // wire sends them, least significant first.
    std::string f32Bytes(float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        return { char(bits), char(bits >> 8), char(bits >> 16), char(bits >> 24) };
    }

    std::string u16Bytes(uint16_t value)
    {
        return { char(value), char(value >> 8) };
    }

    std::string s32Bytes(int32_t value)
    {
        return f32Bytes(std::bit_cast<float>(value));
    }

    Result textureEntryUnpack(const std::string& entry, uint32_t face_count)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_texture_entry_unpack(in(entry), entry.size(), face_count, &value);
        return { status, taken(value) };
    }

    Result textureEntryPack(const std::string& faces)
    {
        aloracle_bytes entry{};
        const int32_t status = aloracle_texture_entry_pack(in(faces), faces.size(), &entry);
        return { status, taken(entry) };
    }

    Result volumeParamsUnpack(const std::string& shape)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_volume_params_unpack(in(shape), shape.size(), &value);
        return { status, taken(value) };
    }

    Result volumeParamsPack(const std::string& params)
    {
        aloracle_bytes shape{};
        const int32_t status = aloracle_volume_params_pack(in(params), params.size(), &shape);
        return { status, taken(shape) };
    }

    Result extraParamUnpack(uint16_t type, const std::string& data)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_extra_param_unpack(type, in(data), data.size(), &value);
        return { status, taken(value) };
    }

    Result extraParamPack(uint16_t type, const std::string& value)
    {
        aloracle_bytes data{};
        const int32_t status = aloracle_extra_param_pack(type, in(value), value.size(), &data);
        return { status, taken(data) };
    }

    Result textureAnimUnpack(const std::string& data)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_texture_anim_unpack(in(data), data.size(), &value);
        return { status, taken(value) };
    }

    Result textureAnimPack(const std::string& value)
    {
        aloracle_bytes data{};
        const int32_t status = aloracle_texture_anim_pack(in(value), value.size(), &data);
        return { status, taken(data) };
    }

    Result particlesUnpack(int32_t form, const std::string& data)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_particles_unpack(form, in(data), data.size(), &value);
        return { status, taken(value) };
    }

    Result nameValueParse(const std::string& line)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_name_value_parse(in(line), line.size(), &value);
        return { status, taken(value) };
    }

    const std::string U1 = "u11111111-1111-1111-1111-111111111111";
    const std::string U2 = "u22222222-2222-2222-2222-222222222222";

    // Two faces that differ in their image alone, each field but the last
    // followed by a 0x00, and no material field, as entries were written
    // before materials.
    const std::string TWO_FACES = std::string(16, '\x22') + "\x01" + std::string(16, '\x11') + "\x00"s
        + "\x00\x00\x00\x00\x00"s + f32Bytes(1.f) + "\x00"s + f32Bytes(1.f) + "\x00"s
        + "\x00\x00\x00"s + "\x00\x00\x00"s + "\x00\x00\x00"s + "\x00\x00"s + "\x00\x00"s + "\x00"s;

    std::string faceNotation(const std::string& image)
    {
        return "{'image':" + image + ",'color':[r1,r1,r1,r1],'scale_s':r1,'scale_t':r1,'offset_s':r0,'offset_t':r0,"
            "'rotation':r0,'bump':i0,'media_tex_gen':i0,'glow':r0,"
            "'material_id':u00000000-0000-0000-0000-000000000000}";
    }

    TEST_CASE("aloracle: a texture entry gives each face its field's default then its exceptions", "[aloracle]")
    {
        const Result read = textureEntryUnpack(TWO_FACES, 2);
        REQUIRE(read.status == ALORACLE_OK);
        // The map's faces, whatever result unpackTEMessage gave beside them.
        const std::string faces = valueOf("[" + faceNotation(U1) + "," + faceNotation(U2) + "]");
        CHECK(read.bytes.find(faces) != std::string::npos);
    }

    TEST_CASE("aloracle: a texture entry missing a field writes no face", "[aloracle]")
    {
        const Result untouched = textureEntryUnpack("", 3);
        REQUIRE(untouched.status == ALORACLE_OK);
        const Result short_entry = textureEntryUnpack(std::string(16, '\x22') + "\x00"s, 3);
        REQUIRE(short_entry.status == ALORACLE_OK);
        CHECK(short_entry.bytes == untouched.bytes);
    }

    TEST_CASE("aloracle: a texture entry packs each field with its material and reads back", "[aloracle]")
    {
        const std::string faces = valueOf("[" + faceNotation(U1) + "," + faceNotation(U2) + "]");
        const Result packed = textureEntryPack(faces);
        REQUIRE(packed.status == ALORACLE_OK);
        CHECK(packed.bytes == TWO_FACES + "\x00"s + std::string(16, '\0'));
        CHECK(textureEntryUnpack(packed.bytes, 2).bytes == textureEntryUnpack(TWO_FACES, 2).bytes);
    }

    // A box: a line path, a square profile, uncut, unscaled and unhollowed.
    const std::string BOX = "\x10"s + "\x00\x00\x00\x00"s + "\x64\x64"s + std::string(9, '\0') + "\x01"s
        + std::string(6, '\0');

    const std::string BOX_PARAMS =
        "'profile':{'curve':i1,'begin':r0,'end':r1,'hollow':r0},"
        "'path':{'curve':i16,'begin':r0,'end':r1,'scale_x':r1,'scale_y':r1,'shear_x':r0,'shear_y':r0,"
        "'twist_begin':r0,'twist_end':r0,'radius_offset':r0,'taper_x':r0,'taper_y':r0,'revolutions':r1,"
        "'skew':r0}";

    TEST_CASE("aloracle: a shape reads path then profile", "[aloracle]")
    {
        const Result read = volumeParamsUnpack(BOX);
        REQUIRE(read.status == ALORACLE_OK);
        CHECK(read.bytes == valueOf("{'ok':1," + BOX_PARAMS + "}"));
    }

    TEST_CASE("aloracle: a shape held to its limits reads as not ok", "[aloracle]")
    {
        // A hollow of 50000 steps is 1, past the 0.95 a hollow may be.
        std::string hollowed = BOX;
        hollowed.replace(21, 2, u16Bytes(50000));
        const Result read = volumeParamsUnpack(hollowed);
        REQUIRE(read.status == ALORACLE_OK);
        // The map's ok first, its keys in order: the key, then false.
        CHECK(read.bytes.find(valueOf("{'ok':0}").substr(5, 8)) == 5);
    }

    TEST_CASE("aloracle: a shape packs to its 23 bytes", "[aloracle]")
    {
        const Result packed = volumeParamsPack(valueOf("{" + BOX_PARAMS + "}"));
        REQUIRE(packed.status == ALORACLE_OK);
        CHECK(packed.bytes == BOX);
    }

    TEST_CASE("aloracle: an extra parameter reads as its type's LLSD", "[aloracle]")
    {
        const std::string light = "\xFF\x00\xFF\xFF"s + f32Bytes(10.f) + f32Bytes(0.f) + f32Bytes(0.5f);
        CHECK(extraParamUnpack(0x20, light).bytes
              == valueOf("{'color':[r1,r0,r1,r1],'radius':r10,'falloff':r0.5,'cutoff':r0}"));

        const std::string flexible = "\x8A\x94\x64\x0A"s + std::string(12, '\0');
        CHECK(extraParamUnpack(0x10, flexible).bytes
              == valueOf("{'air_friction':r2,'gravity':r0,'simulate_lod':i3,'tension':r1,"
                         "'user_force':[r0,r0,r0],'wind_sensitivity':r1}"));

        // A mesh reads as a sculpt; a type past glTF falls back to a sphere.
        const std::string mesh = std::string(16, '\x33') + "\x45";
        CHECK(extraParamUnpack(0x60, mesh).bytes
              == valueOf("{'texture':u33333333-3333-3333-3333-333333333333,'type':i69}"));
        CHECK(extraParamUnpack(0x30, std::string(16, '\x33') + "\x07").bytes
              == valueOf("{'texture':ube293869-d0d9-0a69-5989-ad27f1946fd4,'type':i1}"));

        const std::string materials = "\x02\x00"s + std::string(16, '\x44') + "\x03"s + std::string(16, '\x55');
        CHECK(extraParamUnpack(0x80, materials).bytes
              == valueOf("{'entries':[{'te_idx':i0,'id':u44444444-4444-4444-4444-444444444444},"
                         "{'te_idx':i3,'id':u55555555-5555-5555-5555-555555555555}]}"));
    }

    TEST_CASE("aloracle: an extra parameter packs from its LLSD", "[aloracle]")
    {
        const std::string light = "\xFF\x00\xFF\xFF"s + f32Bytes(10.f) + f32Bytes(0.f) + f32Bytes(0.5f);
        CHECK(extraParamPack(0x20, extraParamUnpack(0x20, light).bytes).bytes == light);

        const std::string flexible = "\x8A\x94\x64\x0A"s + std::string(12, '\0');
        CHECK(extraParamPack(0x10, extraParamUnpack(0x10, flexible).bytes).bytes == flexible);

        const std::string sculpt = std::string(16, '\x33') + "\x45";
        CHECK(extraParamPack(0x30, extraParamUnpack(0x30, sculpt).bytes).bytes == sculpt);

        const std::string materials = "\x02\x00"s + std::string(16, '\x44') + "\x03"s + std::string(16, '\x55');
        CHECK(extraParamPack(0x80, extraParamUnpack(0x80, materials).bytes).bytes == materials);
    }

    // A texture animation that loops on every face of a 4 by 2 grid.
    const std::string ANIMATION = "\x03\xFF\x04\x02"s + f32Bytes(0.f) + f32Bytes(4.f) + f32Bytes(2.f);

    TEST_CASE("aloracle: a texture animation reads and packs its 16 bytes", "[aloracle]")
    {
        const Result read = textureAnimUnpack(ANIMATION);
        REQUIRE(read.status == ALORACLE_OK);
        CHECK(read.bytes
              == valueOf("{'mode':i3,'face':i-1,'sizeX':i4,'sizeY':i2,'start':r0,'length':r4,'rate':r2}"));
        CHECK(textureAnimPack(read.bytes).bytes == ANIMATION);
    }

    TEST_CASE("aloracle: a texture animation of another size is off and a zero size is kept", "[aloracle]")
    {
        CHECK(textureAnimUnpack(ANIMATION.substr(0, 15)).bytes
              == valueOf("{'mode':i0,'face':i-1,'sizeX':i4,'sizeY':i4,'start':r0,'length':r0,'rate':r1}"));
        // The data packer's reading keeps a zero size that the message's
        // reading raises to 1.
        std::string zero = ANIMATION;
        zero[2] = '\0';
        CHECK(textureAnimUnpack(zero).bytes
              == valueOf("{'mode':i3,'face':i-1,'sizeX':i0,'sizeY':i2,'start':r0,'length':r4,'rate':r2}"));
    }

    // A legacy particle system: the 68-byte system block, then the 18-byte
    // particle block.
    const std::string SYSTEM = s32Bytes(1) + s32Bytes(0) + "\x01"s + u16Bytes(256) + u16Bytes(0) + "\x20\x00"s
        + u16Bytes(128) + u16Bytes(0) + u16Bytes(256) + u16Bytes(512) + "\x05"s + u16Bytes(32768)
        + u16Bytes(32768) + u16Bytes(32768) + u16Bytes(32896) + u16Bytes(32896) + u16Bytes(32896)
        + std::string(16, '\x66') + std::string(16, '\0');

    std::string particleBlock(int32_t flags)
    {
        return s32Bytes(flags) + u16Bytes(2560) + "\xFF\xFF\xFF\xFF"s + "\x00\x00\x00\x00"s + "\x20\x20\x40\x40"s;
    }

    std::string particlesNotation(bool ok, int32_t flags, int start_glow)
    {
        return "{'ok':" + std::string(ok ? "1" : "0")
            + ",'crc':i1,'flags':i0,'pattern':i1,'max_age':r1,'start_age':r0,'inner_angle':r1,"
              "'outer_angle':r0,'burst_rate':r0.5,'burst_radius':r0,'burst_speed_min':r1,'burst_speed_max':r2,"
              "'burst_part_count':i5,'angular_velocity':[r0,r0,r0],'accel':[r1,r1,r1],"
              "'image':u66666666-6666-6666-6666-666666666666,'target':u00000000-0000-0000-0000-000000000000,"
              "'part':{'flags':i"
            + std::to_string(flags)
            + ",'max_age':r10,'start_color':[r1,r1,r1,r1],'end_color':[r0,r0,r0,r0],"
              "'start_scale':[r1,r1],'end_scale':[r2,r2],'start_glow':r"
            + std::to_string(start_glow) + ",'end_glow':r0,'blend_source':i7,'blend_dest':i9}}";
    }

    TEST_CASE("aloracle: a particle system reads in the legacy form", "[aloracle]")
    {
        const Result read = particlesUnpack(ALORACLE_PARTICLES_LEGACY, SYSTEM + particleBlock(0));
        REQUIRE(read.status == ALORACLE_OK);
        CHECK(read.bytes == valueOf(particlesNotation(true, 0, 0)));
    }

    TEST_CASE("aloracle: a particle system reads in the extended form with its glow", "[aloracle]")
    {
        const std::string glowing = s32Bytes(68) + SYSTEM + s32Bytes(20) + particleBlock(0x10000) + "\xFF\x00"s;
        const Result read = particlesUnpack(ALORACLE_PARTICLES_EXTENDED, glowing);
        REQUIRE(read.status == ALORACLE_OK);
        CHECK(read.bytes == valueOf(particlesNotation(true, 0x10000, 1)));

        // Bytes the particle block's size holds past what its flags read
        // fail it.
        const std::string longer = s32Bytes(68) + SYSTEM + s32Bytes(20) + particleBlock(0) + "\x00\x00"s;
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, longer).bytes == valueOf(particlesNotation(false, 0, 0)));
    }

    TEST_CASE("aloracle: a name-value line reads its name type class and sendto", "[aloracle]")
    {
        CHECK(nameValueParse("FirstName STRING RW SV Ruth").bytes
              == valueOf("{'name':'FirstName','type':'STRING','class':'RW','sendto':'SV','type_id':i1,"
                         "'class_id':i2,'sendto_id':i3,'value':'Ruth','printed':'FirstName STRING RW SV Ruth'}"));
        CHECK(nameValueParse("Pos VEC3 1, 2, 3").bytes
              == valueOf("{'name':'Pos','type':'VEC3','class':'RW','sendto':'S','type_id':i4,'class_id':i2,"
                         "'sendto_id':i1,'value':[r1,r2,r3],"
                         "'printed':'Pos VEC3 RW S 1.000000, 2.000000, 3.000000'}"));
    }

    TEST_CASE("aloracle: a name-value word beginning with R reads as its class", "[aloracle]")
    {
        CHECK(nameValueParse("Title STRING Rabbit").bytes
              == valueOf("{'name':'Title','type':'STRING','class':'Rabbit','sendto':'S','type_id':i1,"
                         "'class_id':i0,'sendto_id':i1,'value':'','printed':'Title STRING Rabbit S '}"));
    }

    TEST_CASE("aloracle: the object functions refuse what is not theirs to read", "[aloracle]")
    {
        CHECK(textureEntryUnpack(TWO_FACES, 0).status == ALORACLE_BAD_ARGUMENT);
        CHECK(textureEntryUnpack(TWO_FACES, 46).status == ALORACLE_BAD_ARGUMENT);
        std::string many = "[";
        for (int face = 0; face < 45; ++face)
        {
            many += (face ? "," : "") + faceNotation(U1);
        }
        CHECK(textureEntryPack(valueOf(many + "]")).status == ALORACLE_BAD_ARGUMENT);
        CHECK(volumeParamsUnpack(BOX.substr(0, 22)).status == ALORACLE_BAD_ARGUMENT);
        CHECK(volumeParamsPack(valueOf("{'profile':{'hollow':r1e9},'path':{}}")).status == ALORACLE_BAD_ARGUMENT);
        CHECK(volumeParamsPack(valueOf("{'profile':{'begin':r-1e9},'path':{}}")).status == ALORACLE_BAD_ARGUMENT);
        CHECK(volumeParamsPack(valueOf("{'profile':{'begin':r1e9,'end':r-1e9},'path':{}}")).status
              == ALORACLE_OK);
        CHECK(extraParamUnpack(0x50, std::string(16, '\0')).status == ALORACLE_BAD_ARGUMENT);
        CHECK(extraParamUnpack(0x20, std::string(15, '\0')).status == ALORACLE_BAD_ARGUMENT);
        CHECK(extraParamPack(0x20, valueOf("{'radius':r1}")).status == ALORACLE_BAD_ARGUMENT);
        CHECK(textureAnimPack(valueOf("{'mode':i1}")).status == ALORACLE_BAD_ARGUMENT);
        CHECK(particlesUnpack(2, SYSTEM + particleBlock(0)).status == ALORACLE_BAD_ARGUMENT);
        CHECK(particlesUnpack(ALORACLE_PARTICLES_LEGACY, SYSTEM + particleBlock(0).substr(1)).status
              == ALORACLE_BAD_ARGUMENT);
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(68) + SYSTEM + s32Bytes(18)
                                                               + particleBlock(0).substr(1))
                  .status
              == ALORACLE_BAD_ARGUMENT);
        // Sizes unpack would pass over a byte at a time, far past the data.
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(10) + std::string(10, '\0')
                                                               + s32Bytes(0x7FFFFFFF) + SYSTEM)
                  .status
              == ALORACLE_BAD_ARGUMENT);
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(68) + SYSTEM + s32Bytes(0x7FFFFFFF)
                                                               + particleBlock(0))
                  .status
              == ALORACLE_BAD_ARGUMENT);
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(1000) + SYSTEM + particleBlock(0)).status
              == ALORACLE_BAD_ARGUMENT);
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(0x7FFFFFFF) + SYSTEM + particleBlock(0)).status
              == ALORACLE_BAD_ARGUMENT);
        // A particle block's size that taking 18 from wraps.
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(68) + SYSTEM + s32Bytes(-0x7FFFFFFF - 1)
                                                               + particleBlock(0))
                  .status
              == ALORACLE_BAD_ARGUMENT);
        // One that doesn't reads, the leftover below 0 passed over.
        CHECK(particlesUnpack(ALORACLE_PARTICLES_EXTENDED, s32Bytes(68) + SYSTEM + s32Bytes(-0x7FFFFFFF + 17)
                                                               + particleBlock(0))
                  .status
              == ALORACLE_OK);
        CHECK(nameValueParse("Name STRING " + std::string(2036, 'x')).status == ALORACLE_BAD_ARGUMENT);
    }

    // Bits as LLBitPack packs them: a value's bytes least significant first,
    // only the low bits of its last, each byte's bits from the most
    // significant down. The last byte's bits past them are zeros.
    class Bits
    {
    public:
        Bits& put(uint32_t value, int count)
        {
            for (int shift = 0; count > 0; shift += 8)
            {
                const int bits = std::min(count, 8);
                const uint32_t byte = (value >> shift) & 0xFF;
                for (int bit = bits - 1; bit >= 0; --bit)
                {
                    if (mCount % 8 == 0)
                    {
                        mBytes.push_back('\0');
                    }
                    if ((byte >> bit) & 1)
                    {
                        mBytes.back() = char(mBytes.back() | (0x80 >> (mCount % 8)));
                    }
                    ++mCount;
                }
                count -= bits;
            }
            return *this;
        }

        const std::string& bytes() const { return mBytes; }

    private:
        std::string mBytes;
        size_t mCount = 0;
    };

    constexpr uint8_t END_OF_PATCHES = 97;

    // A group header: its stride, patch size and layer type.
    void group(Bits& bits, uint16_t stride, uint8_t patch_size, uint8_t layer_type)
    {
        bits.put(stride, 16).put(patch_size, 8).put(layer_type, 8);
    }

    // A patch: its header, then its coefficients in their zigzag order, each
    // a 0 bit for 0, or 110 for one above 0 and 111 for one below then its
    // magnitude in the header's word bits, then 10, which zeros the rest.
    void patch(Bits& bits, uint8_t quant_wbits, float dc_offset, uint16_t range, uint16_t patchids,
               const std::vector<int32_t>& coefficients = {})
    {
        bits.put(quant_wbits, 8).put(std::bit_cast<uint32_t>(dc_offset), 32).put(range, 16).put(patchids, 10);
        const int word_bits = (quant_wbits & 0xF) + 2;
        for (const int32_t coefficient : coefficients)
        {
            if (coefficient)
            {
                bits.put(coefficient < 0 ? 7 : 6, 3).put(uint32_t(std::abs(coefficient)), word_bits);
            }
            else
            {
                bits.put(0, 1);
            }
        }
        bits.put(2, 2);
    }

    Result layerDecode(int32_t type, const std::string& data)
    {
        aloracle_bytes value{};
        const int32_t status = aloracle_layer_decode(type, in(data), data.size(), &value);
        return { status, taken(value) };
    }

    // A patch of one height throughout, as a layer gives it.
    std::string flatPatchNotation(int quant_wbits, const std::string& dc_offset, int range, int x, int y,
                                  float height, int size)
    {
        std::string heights;
        for (int at = 0; at < size * size; ++at)
        {
            heights += f32Bytes(height);
        }
        return "{'quant_wbits':i" + std::to_string(quant_wbits) + ",'dc_offset':r" + dc_offset
            + ",'range':i" + std::to_string(range) + ",'patchids':i" + std::to_string((x << 5) | y)
            + ",'x':i" + std::to_string(x) + ",'y':i" + std::to_string(y) + ",'heights':b("
            + std::to_string(heights.size()) + ")\"" + heights + "\"}";
    }

    // The heights of a layer's nth patch: the binary after its nth key
    // "heights".
    std::vector<float> heightsOf(const std::string& layer, size_t nth)
    {
        const std::string key = "k\0\0\0\x07heights"s;
        size_t at = 0;
        for (size_t found = 0; found <= nth; ++found)
        {
            at = layer.find(key, at);
            REQUIRE(at != std::string::npos);
            at += key.size();
        }
        REQUIRE(layer.size() >= at + 5);
        REQUIRE(layer[at] == 'b');
        const auto byte = [&](size_t offset) { return uint32_t(uint8_t(layer[offset])); };
        const size_t size = (byte(at + 1) << 24) | (byte(at + 2) << 16) | (byte(at + 3) << 8) | byte(at + 4);
        at += 5;
        REQUIRE(layer.size() >= at + size);
        std::vector<float> heights(size / 4);
        for (size_t height = 0; height < heights.size(); ++height)
        {
            const size_t offset = at + height * 4;
            heights[height] = std::bit_cast<float>(byte(offset) | (byte(offset + 1) << 8)
                                                   | (byte(offset + 2) << 16) | (byte(offset + 3) << 24));
        }
        return heights;
    }

    TEST_CASE("aloracle: land patches with no coefficients lie flat at their offset and half their range", "[aloracle]")
    {
        // Prequant 2 heights 10 / 4 * 2 + 20; prequant 4 heights 1 / 16 * 8 - 4.
        Bits bits;
        group(bits, 264, 16, 'L');
        patch(bits, 0x0D, 20.f, 10, (3 << 5) | 7);
        patch(bits, 0x2D, -4.f, 1, 15 << 5);
        bits.put(END_OF_PATCHES, 8);
        CHECK(layerDecode(ALORACLE_LAYER_LAND, bits.bytes()).bytes
              == valueOf("{'group':{'stride':i264,'patch_size':i16,'layer_type':i76},'patches':["
                         + flatPatchNotation(0x0D, "20", 10, 3, 7, 25.f, 16) + ","
                         + flatPatchNotation(0x2D, "-4", 1, 15, 0, -3.5f, 16) + "]}"));

        Bits none;
        group(none, 264, 32, 'L');
        none.put(END_OF_PATCHES, 8);
        CHECK(layerDecode(ALORACLE_LAYER_LAND, none.bytes()).bytes
              == valueOf("{'group':{'stride':i264,'patch_size':i32,'layer_type':i76},'patches':[]}"));
    }

    TEST_CASE("aloracle: a land coefficient decompresses to its cosine along x or along y", "[aloracle]")
    {
        const double pi = 3.14159265358979323846;
        for (const int size : { 16, 32 })
        {
            // The second coefficient in zigzag order is the first along x and
            // the third the first along y, each dequantized by 3. At prequant
            // 2 a range of 64 multiplies by 16 and adds 32 to the offset.
            Bits bits;
            group(bits, 264, uint8_t(size), 'L');
            patch(bits, 0x0D, 20.f, 64, 0, { 0, 10 });
            patch(bits, 0x0D, 20.f, 64, 1, { 0, 0, 10 });
            bits.put(END_OF_PATCHES, 8);
            const Result read = layerDecode(ALORACLE_LAYER_LAND, bits.bytes());
            REQUIRE(read.status == ALORACLE_OK);
            const std::vector<float> along_x = heightsOf(read.bytes, 0);
            const std::vector<float> along_y = heightsOf(read.bytes, 1);
            REQUIRE(along_x.size() == size_t(size * size));
            REQUIRE(along_y.size() == size_t(size * size));

            const auto height = [&](int at)
            {
                return 52. + 16. * 30. * std::cos((2. * at + 1.) * pi / (2. * size)) * (2. / size) / std::sqrt(2.);
            };
            for (int y = 0; y < size; ++y)
            {
                for (int x = 0; x < size; ++x)
                {
                    CHECK(std::fabs(along_x[y * size + x] - height(x)) < 1e-3);
                    CHECK(std::fabs(along_y[y * size + x] - height(y)) < 1e-3);
                }
            }
        }
    }

    TEST_CASE("aloracle: a wind layer reads its two patches and no more", "[aloracle]")
    {
        // Prequant 2 heights 2 / 4 * 2 plus the offset.
        Bits bits;
        group(bits, 16, 16, '7');
        patch(bits, 0x0D, 1.f, 2, 0);
        patch(bits, 0x0D, -1.f, 2, 0);
        // A byte past them, which land would read as a patch cut short.
        bits.put(0xFF, 8);
        CHECK(layerDecode(ALORACLE_LAYER_WIND, bits.bytes()).bytes
              == valueOf("{'group':{'stride':i16,'patch_size':i16,'layer_type':i55},'patches':["
                         + flatPatchNotation(0x0D, "1", 2, 0, 0, 2.f, 16) + ","
                         + flatPatchNotation(0x0D, "-1", 2, 0, 0, 0.f, 16) + "]}"));
        CHECK(layerDecode(ALORACLE_LAYER_LAND, bits.bytes()).status == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a cloud layer gives its group header alone", "[aloracle]")
    {
        Bits bits;
        group(bits, 0, 255, '8');
        CHECK(layerDecode(ALORACLE_LAYER_CLOUD, bits.bytes()).bytes
              == valueOf("{'group':{'stride':i0,'patch_size':i255,'layer_type':i56},'patches':[]}"));
    }

    TEST_CASE("aloracle: layer data a step would read past the end of is refused", "[aloracle]")
    {
        Bits land;
        group(land, 264, 16, 'L');
        patch(land, 0x0D, 20.f, 10, 0, { 5, -5 });
        land.put(END_OF_PATCHES, 8);
        REQUIRE(layerDecode(ALORACLE_LAYER_LAND, land.bytes()).status == ALORACLE_OK);
        // Every byte holds a bit a step reads, up to the end's last.
        for (size_t cut = 0; cut < land.bytes().size(); ++cut)
        {
            CHECK(layerDecode(ALORACLE_LAYER_LAND, land.bytes().substr(0, cut)).status == ALORACLE_BAD_ARGUMENT);
        }

        Bits wind;
        group(wind, 16, 16, '7');
        patch(wind, 0x0D, 0.f, 2, 0, { 5, -5 });
        patch(wind, 0x0D, 0.f, 2, 0, { -5, 5 });
        REQUIRE(layerDecode(ALORACLE_LAYER_WIND, wind.bytes()).status == ALORACLE_OK);
        for (size_t cut = 0; cut < wind.bytes().size(); ++cut)
        {
            CHECK(layerDecode(ALORACLE_LAYER_WIND, wind.bytes().substr(0, cut)).status == ALORACLE_BAD_ARGUMENT);
        }
        CHECK(layerDecode(ALORACLE_LAYER_CLOUD, "\x10\x00\x10"s).status == ALORACLE_BAD_ARGUMENT);
    }

    TEST_CASE("aloracle: a land or wind patch size but 16 or 32 is refused", "[aloracle]")
    {
        for (const uint8_t size : { 0, 8, 15, 17, 31, 33, 255 })
        {
            Bits bits;
            group(bits, size, size, 'L');
            patch(bits, 0x0D, 0.f, 2, 0);
            patch(bits, 0x0D, 0.f, 2, 0);
            bits.put(END_OF_PATCHES, 8);
            CHECK(layerDecode(ALORACLE_LAYER_LAND, bits.bytes()).status == ALORACLE_BAD_ARGUMENT);
            CHECK(layerDecode(ALORACLE_LAYER_WIND, bits.bytes()).status == ALORACLE_BAD_ARGUMENT);
            CHECK(layerDecode(ALORACLE_LAYER_CLOUD, bits.bytes()).status == ALORACLE_OK);
        }
    }

    TEST_CASE("aloracle: a wind patch header that ends the patches is refused", "[aloracle]")
    {
        // The end, then a 10 that would end its coefficients at once.
        Bits bits;
        group(bits, 16, 16, '7');
        patch(bits, 0x0D, 0.f, 2, 0);
        bits.put(END_OF_PATCHES, 8).put(2, 2);
        CHECK(layerDecode(ALORACLE_LAYER_WIND, bits.bytes()).status == ALORACLE_BAD_ARGUMENT);
        CHECK(layerDecode(ALORACLE_LAYER_LAND, bits.bytes()).status == ALORACLE_OK);
    }

    TEST_CASE("aloracle: a layer of a type the viewer has none for is refused", "[aloracle]")
    {
        // Data any of the three would read.
        Bits bits;
        group(bits, 264, 16, 'L');
        patch(bits, 0x0D, 0.f, 2, 0);
        patch(bits, 0x0D, 0.f, 2, 0);
        bits.put(END_OF_PATCHES, 8);
        REQUIRE(layerDecode(ALORACLE_LAYER_LAND, bits.bytes()).status == ALORACLE_OK);
        REQUIRE(layerDecode(ALORACLE_LAYER_WIND, bits.bytes()).status == ALORACLE_OK);
        CHECK(layerDecode('M', bits.bytes()).status == ALORACLE_BAD_ARGUMENT);
        CHECK(layerDecode(0, bits.bytes()).status == ALORACLE_BAD_ARGUMENT);
        aloracle_bytes value{};
        CHECK(aloracle_layer_decode(ALORACLE_LAYER_LAND, in(bits.bytes()), bits.bytes().size(), nullptr)
              == ALORACLE_BAD_ARGUMENT);
        CHECK(aloracle_layer_decode(ALORACLE_LAYER_LAND, nullptr, 5, &value) == ALORACLE_BAD_ARGUMENT);
        CHECK(value.data == nullptr);
    }
}
