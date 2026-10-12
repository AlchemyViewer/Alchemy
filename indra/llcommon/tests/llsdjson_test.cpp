/**
 * @file llsdjson_test.cpp
 * @brief LLSD <-> JSON conversion test cases.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>

#include "llsdjson.h"
#include "llsdutil.h"
#include "lldate.h"
#include "lluri.h"
#include "lluuid.h"

#include <cmath>
#include <limits>

namespace
{
    struct llsdjson_data
    {
        // parse json text expecting success
        LLSD parse(const std::string& json)
        {
            LLSD result;
            std::string errmsg;
            bool ok = LlsdFromJsonString(json, result, &errmsg);
            UNSCOPED_INFO("parse failed for " + json + ": " + errmsg);
            REQUIRE(ok);
            return result;
        }
    };

    // scalar parsing: types and values
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: scalars parse to the right type and value", "[LlsdJson]")
    {
        LLSD val = parse("null");
        UNSCOPED_INFO("null type");
        CHECK(val.type() == LLSD::TypeUndefined);

        val = parse("true");
        UNSCOPED_INFO("bool type");
        CHECK(val.type() == LLSD::TypeBoolean);
        UNSCOPED_INFO("bool value");
        CHECK(val.asBoolean());

        val = parse("false");
        UNSCOPED_INFO("false value");
        CHECK_FALSE(val.asBoolean());

        val = parse("42");
        UNSCOPED_INFO("int type");
        CHECK(val.type() == LLSD::TypeInteger);
        UNSCOPED_INFO("int value");
        CHECK(val.asInteger() == 42);

        val = parse("-7");
        UNSCOPED_INFO("negative int");
        CHECK(val.asInteger() == -7);

        val = parse("3.25");
        UNSCOPED_INFO("real type");
        CHECK(val.type() == LLSD::TypeReal);
        UNSCOPED_INFO("real value");
        CHECK(val.asReal() == 3.25);

        val = parse("\"hello world\"");
        UNSCOPED_INFO("string type");
        CHECK(val.type() == LLSD::TypeString);
        UNSCOPED_INFO("string value");
        CHECK(val.asString() == "hello world");
    }

    // integer vs real distinction is preserved by the parse
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: integer vs real distinction is preserved by the parse", "[LlsdJson]")
    {
        UNSCOPED_INFO("1 is integer");
        CHECK(parse("1").type() == LLSD::TypeInteger);
        UNSCOPED_INFO("1.0 is real");
        CHECK(parse("1.0").type() == LLSD::TypeReal);
        UNSCOPED_INFO("1e0 is real");
        CHECK(parse("1e0").type() == LLSD::TypeReal);
        UNSCOPED_INFO("0 is integer");
        CHECK(parse("0").type() == LLSD::TypeInteger);
    }

    // arrays: order, nesting, empties
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: arrays parse in order with nesting and empty arrays", "[LlsdJson]")
    {
        LLSD val = parse("[1, \"two\", 3.0, null, [true]]");
        UNSCOPED_INFO("array type");
        CHECK(val.type() == LLSD::TypeArray);
        UNSCOPED_INFO("array size");
        REQUIRE(val.size() == 5);
        UNSCOPED_INFO("elt 0");
        CHECK(val[0].asInteger() == 1);
        UNSCOPED_INFO("elt 1");
        CHECK(val[1].asString() == "two");
        UNSCOPED_INFO("elt 2");
        CHECK(val[2].asReal() == 3.0);
        UNSCOPED_INFO("elt 3 undefined");
        CHECK(val[3].type() == LLSD::TypeUndefined);
        UNSCOPED_INFO("nested array");
        CHECK(val[4][0].asBoolean() == true);

        val = parse("[]");
        UNSCOPED_INFO("empty array type");
        CHECK(val.type() == LLSD::TypeArray);
        UNSCOPED_INFO("empty array size");
        REQUIRE(val.size() == 0);
    }

    // objects: keys, nesting, empties
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: objects parse by key with nesting and empty objects", "[LlsdJson]")
    {
        LLSD val = parse("{\"a\": 1, \"b\": {\"c\": [2, 3]}}");
        UNSCOPED_INFO("map type");
        CHECK(val.type() == LLSD::TypeMap);
        UNSCOPED_INFO("map size");
        REQUIRE(val.size() == 2);
        UNSCOPED_INFO("member a");
        CHECK(val["a"].asInteger() == 1);
        UNSCOPED_INFO("nested member");
        CHECK(val["b"]["c"][1].asInteger() == 3);

        val = parse("{}");
        UNSCOPED_INFO("empty map type");
        CHECK(val.type() == LLSD::TypeMap);
        UNSCOPED_INFO("empty map size");
        REQUIRE(val.size() == 0);
    }

    // serialization golden strings
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: serialization writes the golden strings", "[LlsdJson]")
    {
        UNSCOPED_INFO("undefined");
        CHECK(LlsdToJson(LLSD()) == "null");
        UNSCOPED_INFO("true");
        CHECK(LlsdToJson(LLSD(true)) == "true");
        UNSCOPED_INFO("false");
        CHECK(LlsdToJson(LLSD(false)) == "false");
        UNSCOPED_INFO("integer");
        CHECK(LlsdToJson(LLSD(17)) == "17");
        UNSCOPED_INFO("negative");
        CHECK(LlsdToJson(LLSD(-17)) == "-17");
        UNSCOPED_INFO("real");
        CHECK(LlsdToJson(LLSD(2.5)) == "2.5");
        UNSCOPED_INFO("string");
        CHECK(LlsdToJson(LLSD("simple")) == "\"simple\"");
        UNSCOPED_INFO("empty string");
        CHECK(LlsdToJson(LLSD("")) == "\"\"");
        UNSCOPED_INFO("empty map");
        CHECK(LlsdToJson(LLSD::emptyMap()) == "{}");
        UNSCOPED_INFO("empty array");
        CHECK(LlsdToJson(LLSD::emptyArray()) == "[]");

        LLSD map;
        map["b"] = 2;
        map["a"] = 1;
        // LLSD maps iterate in sorted key order
        UNSCOPED_INFO("map");
        CHECK(LlsdToJson(map) == "{\"a\":1,\"b\":2}");

        LLSD array;
        array.append(1);
        array.append("x");
        UNSCOPED_INFO("array");
        CHECK(LlsdToJson(array) == "[1,\"x\"]");
    }

    // string escaping in serialization
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: string escaping in serialization", "[LlsdJson]")
    {
        UNSCOPED_INFO("quote");
        CHECK(LlsdToJson(LLSD("a\"b")) == "\"a\\\"b\"");
        UNSCOPED_INFO("backslash");
        CHECK(LlsdToJson(LLSD("a\\b")) == "\"a\\\\b\"");
        UNSCOPED_INFO("newline");
        CHECK(LlsdToJson(LLSD("a\nb")) == "\"a\\nb\"");
        UNSCOPED_INFO("tab");
        CHECK(LlsdToJson(LLSD("a\tb")) == "\"a\\tb\"");

        // control character must be escaped one way or another; round-trip it
        LLSD ctrl;
        UNSCOPED_INFO("control char parses");
        REQUIRE(LlsdFromJsonString(LlsdToJson(LLSD(std::string("a\x01z"))), ctrl));
        UNSCOPED_INFO("control char round-trip");
        CHECK(ctrl.asString() == std::string("a\x01z"));

        // UTF-8 passes through unescaped or escaped, but must round-trip
        std::string utf8("\xE3\x81\x93\xE3\x82\x93 caf\xC3\xA9");
        LLSD uni;
        UNSCOPED_INFO("utf8 parses");
        REQUIRE(LlsdFromJsonString(LlsdToJson(LLSD(utf8)), uni));
        UNSCOPED_INFO("utf8 round-trip");
        CHECK(uni.asString() == utf8);
    }

    // special scalar types serialize as strings
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: special scalar types serialize as strings", "[LlsdJson]")
    {
        LLUUID id("c96f9b8e-f5ad-4b72-b6f7-a2b30d1444d2");
        UNSCOPED_INFO("uuid");
        CHECK(LlsdToJson(LLSD(id)) ==
                      "\"c96f9b8e-f5ad-4b72-b6f7-a2b30d1444d2\"");

        LLDate date("2026-06-11T12:34:56Z");
        UNSCOPED_INFO("date");
        CHECK(LlsdToJson(LLSD(date)) == "\"2026-06-11T12:34:56Z\"");

        LLURI uri("http://example.com/path");
        UNSCOPED_INFO("uri");
        CHECK(LlsdToJson(LLSD(uri)) == "\"http://example.com/path\"");
    }

    // non-finite reals degrade to null (JSON has no NaN/Infinity)
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: non-finite reals degrade to null (JSON has no NaN/Infinity)", "[LlsdJson]")
    {
        UNSCOPED_INFO("nan");
        CHECK(LlsdToJson(LLSD(std::numeric_limits<F64>::quiet_NaN())) == "null");
        UNSCOPED_INFO("inf");
        CHECK(LlsdToJson(LLSD(std::numeric_limits<F64>::infinity())) == "null");
        UNSCOPED_INFO("-inf");
        CHECK(LlsdToJson(LLSD(-std::numeric_limits<F64>::infinity())) == "null");

        // and stay valid inside containers
        LLSD map;
        map["bad"] = std::numeric_limits<F64>::quiet_NaN();
        UNSCOPED_INFO("nan in map");
        CHECK(LlsdToJson(map) == "{\"bad\":null}");
    }

    // structured round trip preserves everything
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: structured round trip preserves everything", "[LlsdJson]")
    {
        LLSD src;
        src["int"] = 123;
        src["real"] = 0.1;
        src["string"] = "round trip";
        src["flag"] = true;
        src["empty"] = LLSD();
        src["list"][0] = 1;
        src["list"][1] = "two";
        src["list"][2]["deep"] = 3.5;

        LLSD dst;
        std::string json = LlsdToJson(src);
        UNSCOPED_INFO("round-trip parses: " + json);
        REQUIRE(LlsdFromJsonString(json, dst));
        UNSCOPED_INFO("round-trip equality: " + json);
        CHECK(llsd_equals(src, dst));
    }

    // full-precision reals survive the round trip
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: full-precision reals survive the round trip", "[LlsdJson]")
    {
        F64 vals[] = { 0.1, 1.0/3.0, 2.718281828459045, 1e-300, 1.7976931348623157e308 };
        for (F64 v : vals)
        {
            LLSD parsed;
            UNSCOPED_INFO("real parses");
            REQUIRE(LlsdFromJsonString(LlsdToJson(LLSD(v)), parsed));
            UNSCOPED_INFO("real precision");
            REQUIRE(parsed.asReal() == v);
        }
    }

    // parse failures report errors and clear the output
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: parse failures report errors and clear the output", "[LlsdJson]")
    {
        const char* bad[] =
        {
            "",
            "   ",
            "{",
            "[1, 2",
            "{\"a\": }",
            "{} trailing",
            "'single quotes'",
            "{\"a\": 1,}",
            "\"unterminated",
            // valid syntax but exceeds uint64: rejected with BIGINT_ERROR
            // under the default parser configuration
            "18446744073709551616",
        };
        for (const char* json : bad)
        {
            LLSD out = LLSD::emptyMap();
            std::string errmsg;
            bool ok = LlsdFromJsonString(json, out, &errmsg);
            UNSCOPED_INFO(std::string("rejects: ") + json);
            REQUIRE_FALSE(ok);
            UNSCOPED_INFO(std::string("errmsg set for: ") + json);
            REQUIRE_FALSE(errmsg.empty());
            UNSCOPED_INFO(std::string("output cleared for: ") + json);
            REQUIRE(out.type() == LLSD::TypeUndefined);
        }

        // errmsg pointer is optional
        LLSD out;
        UNSCOPED_INFO("no errmsg crash");
        CHECK_FALSE(LlsdFromJsonString("{", out));
    }

    // unicode object keys
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: unicode object keys round-trip", "[LlsdJson]")
    {
        LLSD src;
        src["caf\xC3\xA9"] = 1;
        LLSD dst;
        UNSCOPED_INFO("unicode key parses");
        REQUIRE(LlsdFromJsonString(LlsdToJson(src), dst));
        UNSCOPED_INFO("unicode key value");
        CHECK(dst["caf\xC3\xA9"].asInteger() == 1);
    }

    // padded_string overload parses in place
    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: padded_string overload parses in place", "[LlsdJson]")
    {
        LLSD out;
        UNSCOPED_INFO("padded parse");
        REQUIRE(LlsdFromJsonString(simdjson::padded_string(std::string("{\"a\": [1, 2.5, \"three\"]}")), out));
        UNSCOPED_INFO("padded int");
        CHECK(out["a"][0].asInteger() == 1);
        UNSCOPED_INFO("padded real");
        CHECK(out["a"][1].asReal() == 2.5);
        UNSCOPED_INFO("padded string");
        CHECK(out["a"][2].asString() == "three");

        std::string errmsg;
        UNSCOPED_INFO("padded parse fails");
        REQUIRE_FALSE(LlsdFromJsonString(simdjson::padded_string(std::string("{")), out, &errmsg));
        UNSCOPED_INFO("padded errmsg set");
        REQUIRE_FALSE(errmsg.empty());
    }
}
