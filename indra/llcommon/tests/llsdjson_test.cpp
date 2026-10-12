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
        LLSD require_parse(const std::string& json)
        {
            LLSD result;
            std::string errmsg;
            bool ok = LlsdFromJsonString(json, result, &errmsg);
            CAPTURE(json, errmsg);
            REQUIRE(ok);
            return result;
        }
    };

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: scalars parse to the right type and value", "[LlsdJson]")
    {
        LLSD val = require_parse("null");
        CHECK(val.type() == LLSD::TypeUndefined);

        val = require_parse("true");
        CHECK(val.type() == LLSD::TypeBoolean);
        CHECK(val.asBoolean());

        val = require_parse("false");
        CHECK_FALSE(val.asBoolean());

        val = require_parse("42");
        CHECK(val.type() == LLSD::TypeInteger);
        CHECK(val.asInteger() == 42);

        val = require_parse("-7");
        CHECK(val.asInteger() == -7);

        val = require_parse("3.25");
        CHECK(val.type() == LLSD::TypeReal);
        CHECK(val.asReal() == 3.25);

        val = require_parse("\"hello world\"");
        CHECK(val.type() == LLSD::TypeString);
        CHECK(val.asString() == "hello world");
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: integer vs real distinction is preserved by the parse", "[LlsdJson]")
    {
        CHECK(require_parse("1").type() == LLSD::TypeInteger);
        CHECK(require_parse("1.0").type() == LLSD::TypeReal);
        CHECK(require_parse("1e0").type() == LLSD::TypeReal);
        CHECK(require_parse("0").type() == LLSD::TypeInteger);
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: arrays parse in order with nesting and empty arrays", "[LlsdJson]")
    {
        LLSD val = require_parse("[1, \"two\", 3.0, null, [true]]");
        CHECK(val.type() == LLSD::TypeArray);
        REQUIRE(val.size() == 5);
        CHECK(val[0].asInteger() == 1);
        CHECK(val[1].asString() == "two");
        CHECK(val[2].asReal() == 3.0);
        CHECK(val[3].type() == LLSD::TypeUndefined);
        CHECK(val[4][0].asBoolean());

        val = require_parse("[]");
        CHECK(val.type() == LLSD::TypeArray);
        CHECK(val.size() == 0);
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: objects parse by key with nesting and empty objects", "[LlsdJson]")
    {
        LLSD val = require_parse("{\"a\": 1, \"b\": {\"c\": [2, 3]}}");
        CHECK(val.type() == LLSD::TypeMap);
        REQUIRE(val.size() == 2);
        CHECK(val["a"].asInteger() == 1);
        CHECK(val["b"]["c"][1].asInteger() == 3);

        val = require_parse("{}");
        CHECK(val.type() == LLSD::TypeMap);
        CHECK(val.size() == 0);
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: serialization writes the golden strings", "[LlsdJson]")
    {
        CHECK(LlsdToJson(LLSD()) == "null");
        CHECK(LlsdToJson(LLSD(true)) == "true");
        CHECK(LlsdToJson(LLSD(false)) == "false");
        CHECK(LlsdToJson(LLSD(17)) == "17");
        CHECK(LlsdToJson(LLSD(-17)) == "-17");
        CHECK(LlsdToJson(LLSD(2.5)) == "2.5");
        CHECK(LlsdToJson(LLSD("simple")) == "\"simple\"");
        CHECK(LlsdToJson(LLSD("")) == "\"\"");
        CHECK(LlsdToJson(LLSD::emptyMap()) == "{}");
        CHECK(LlsdToJson(LLSD::emptyArray()) == "[]");

        LLSD map;
        map["b"] = 2;
        map["a"] = 1;
        // LLSD maps iterate in sorted key order
        CHECK(LlsdToJson(map) == "{\"a\":1,\"b\":2}");

        LLSD array;
        array.append(1);
        array.append("x");
        CHECK(LlsdToJson(array) == "[1,\"x\"]");
    }

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
        REQUIRE(LlsdFromJsonString(LlsdToJson(LLSD(std::string("a\x01z"))), ctrl));
        CHECK(ctrl.asString() == std::string("a\x01z"));

        // UTF-8 passes through unescaped or escaped, but must round-trip
        std::string utf8("\xE3\x81\x93\xE3\x82\x93 caf\xC3\xA9");
        LLSD uni;
        REQUIRE(LlsdFromJsonString(LlsdToJson(LLSD(utf8)), uni));
        CHECK(uni.asString() == utf8);
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: special scalar types serialize as strings", "[LlsdJson]")
    {
        LLUUID id("c96f9b8e-f5ad-4b72-b6f7-a2b30d1444d2");
        CHECK(LlsdToJson(LLSD(id)) ==
                      "\"c96f9b8e-f5ad-4b72-b6f7-a2b30d1444d2\"");

        LLDate date("2026-06-11T12:34:56Z");
        CHECK(LlsdToJson(LLSD(date)) == "\"2026-06-11T12:34:56Z\"");

        LLURI uri("http://example.com/path");
        CHECK(LlsdToJson(LLSD(uri)) == "\"http://example.com/path\"");
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: non-finite reals degrade to null (JSON has no NaN/Infinity)", "[LlsdJson]")
    {
        CHECK(LlsdToJson(LLSD(std::numeric_limits<F64>::quiet_NaN())) == "null");
        CHECK(LlsdToJson(LLSD(std::numeric_limits<F64>::infinity())) == "null");
        CHECK(LlsdToJson(LLSD(-std::numeric_limits<F64>::infinity())) == "null");

        // and stay valid inside containers
        LLSD map;
        map["bad"] = std::numeric_limits<F64>::quiet_NaN();
        CHECK(LlsdToJson(map) == "{\"bad\":null}");
    }

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
        CAPTURE(json);
        REQUIRE(LlsdFromJsonString(json, dst));
        CHECK(llsd_equals(src, dst));
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: full-precision reals survive the round trip", "[LlsdJson]")
    {
        F64 vals[] = { 0.1, 1.0/3.0, 2.718281828459045, 1e-300, 1.7976931348623157e308 };
        for (F64 v : vals)
        {
            LLSD parsed;
            CAPTURE(v);
            REQUIRE(LlsdFromJsonString(LlsdToJson(LLSD(v)), parsed));
            REQUIRE(parsed.asReal() == v);
        }
    }

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
            CAPTURE(json);
            REQUIRE_FALSE(ok);
            REQUIRE_FALSE(errmsg.empty());
            REQUIRE(out.type() == LLSD::TypeUndefined);
        }

        // errmsg pointer is optional
        LLSD out;
        UNSCOPED_INFO("without an errmsg pointer");
        CHECK_FALSE(LlsdFromJsonString("{", out));
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: unicode object keys round-trip", "[LlsdJson]")
    {
        LLSD src;
        src["caf\xC3\xA9"] = 1;
        LLSD dst;
        REQUIRE(LlsdFromJsonString(LlsdToJson(src), dst));
        CHECK(dst["caf\xC3\xA9"].asInteger() == 1);
    }

    TEST_CASE_METHOD(llsdjson_data, "LlsdJson: padded_string overload parses in place", "[LlsdJson]")
    {
        LLSD out;
        REQUIRE(LlsdFromJsonString(simdjson::padded_string(std::string("{\"a\": [1, 2.5, \"three\"]}")), out));
        CHECK(out["a"][0].asInteger() == 1);
        CHECK(out["a"][1].asReal() == 2.5);
        CHECK(out["a"][2].asString() == "three");

        std::string errmsg;
        REQUIRE_FALSE(LlsdFromJsonString(simdjson::padded_string(std::string("{")), out, &errmsg));
        CHECK_FALSE(errmsg.empty());
    }
}
