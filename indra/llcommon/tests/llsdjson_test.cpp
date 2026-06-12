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
#include "lltut.h"

#include "llsdjson.h"
#include "llsdutil.h"
#include "lldate.h"
#include "lluri.h"
#include "lluuid.h"

#include <cmath>
#include <limits>

namespace tut
{
    struct llsdjson_data
    {
        // parse json text expecting success
        LLSD parse(const std::string& json)
        {
            LLSD result;
            std::string errmsg;
            bool ok = LlsdFromJsonString(json, result, &errmsg);
            ensure("parse failed for " + json + ": " + errmsg, ok);
            return result;
        }
    };
    typedef test_group<llsdjson_data> llsdjson_test;
    typedef llsdjson_test::object llsdjson_object;
    tut::llsdjson_test tllsdjson("LlsdJson");

    // scalar parsing: types and values
    template<> template<>
    void llsdjson_object::test<1>()
    {
        LLSD val = parse("null");
        ensure_equals("null type", val.type(), LLSD::TypeUndefined);

        val = parse("true");
        ensure_equals("bool type", val.type(), LLSD::TypeBoolean);
        ensure("bool value", val.asBoolean());

        val = parse("false");
        ensure("false value", !val.asBoolean());

        val = parse("42");
        ensure_equals("int type", val.type(), LLSD::TypeInteger);
        ensure_equals("int value", val.asInteger(), 42);

        val = parse("-7");
        ensure_equals("negative int", val.asInteger(), -7);

        val = parse("3.25");
        ensure_equals("real type", val.type(), LLSD::TypeReal);
        ensure_equals("real value", val.asReal(), 3.25);

        val = parse("\"hello world\"");
        ensure_equals("string type", val.type(), LLSD::TypeString);
        ensure_equals("string value", val.asString(), "hello world");
    }

    // integer vs real distinction is preserved by the parse
    template<> template<>
    void llsdjson_object::test<2>()
    {
        ensure_equals("1 is integer", parse("1").type(), LLSD::TypeInteger);
        ensure_equals("1.0 is real", parse("1.0").type(), LLSD::TypeReal);
        ensure_equals("1e0 is real", parse("1e0").type(), LLSD::TypeReal);
        ensure_equals("0 is integer", parse("0").type(), LLSD::TypeInteger);
    }

    // arrays: order, nesting, empties
    template<> template<>
    void llsdjson_object::test<3>()
    {
        LLSD val = parse("[1, \"two\", 3.0, null, [true]]");
        ensure_equals("array type", val.type(), LLSD::TypeArray);
        ensure_equals("array size", val.size(), 5);
        ensure_equals("elt 0", val[0].asInteger(), 1);
        ensure_equals("elt 1", val[1].asString(), "two");
        ensure_equals("elt 2", val[2].asReal(), 3.0);
        ensure_equals("elt 3 undefined", val[3].type(), LLSD::TypeUndefined);
        ensure_equals("nested array", val[4][0].asBoolean(), true);

        val = parse("[]");
        ensure_equals("empty array type", val.type(), LLSD::TypeArray);
        ensure_equals("empty array size", val.size(), 0);
    }

    // objects: keys, nesting, empties
    template<> template<>
    void llsdjson_object::test<4>()
    {
        LLSD val = parse("{\"a\": 1, \"b\": {\"c\": [2, 3]}}");
        ensure_equals("map type", val.type(), LLSD::TypeMap);
        ensure_equals("map size", val.size(), 2);
        ensure_equals("member a", val["a"].asInteger(), 1);
        ensure_equals("nested member", val["b"]["c"][1].asInteger(), 3);

        val = parse("{}");
        ensure_equals("empty map type", val.type(), LLSD::TypeMap);
        ensure_equals("empty map size", val.size(), 0);
    }

    // serialization golden strings
    template<> template<>
    void llsdjson_object::test<5>()
    {
        ensure_equals("undefined", LlsdToJson(LLSD()), "null");
        ensure_equals("true", LlsdToJson(LLSD(true)), "true");
        ensure_equals("false", LlsdToJson(LLSD(false)), "false");
        ensure_equals("integer", LlsdToJson(LLSD(17)), "17");
        ensure_equals("negative", LlsdToJson(LLSD(-17)), "-17");
        ensure_equals("real", LlsdToJson(LLSD(2.5)), "2.5");
        ensure_equals("string", LlsdToJson(LLSD("simple")), "\"simple\"");
        ensure_equals("empty string", LlsdToJson(LLSD("")), "\"\"");
        ensure_equals("empty map", LlsdToJson(LLSD::emptyMap()), "{}");
        ensure_equals("empty array", LlsdToJson(LLSD::emptyArray()), "[]");

        LLSD map;
        map["b"] = 2;
        map["a"] = 1;
        // LLSD maps iterate in sorted key order
        ensure_equals("map", LlsdToJson(map), "{\"a\":1,\"b\":2}");

        LLSD array;
        array.append(1);
        array.append("x");
        ensure_equals("array", LlsdToJson(array), "[1,\"x\"]");
    }

    // string escaping in serialization
    template<> template<>
    void llsdjson_object::test<6>()
    {
        ensure_equals("quote", LlsdToJson(LLSD("a\"b")), "\"a\\\"b\"");
        ensure_equals("backslash", LlsdToJson(LLSD("a\\b")), "\"a\\\\b\"");
        ensure_equals("newline", LlsdToJson(LLSD("a\nb")), "\"a\\nb\"");
        ensure_equals("tab", LlsdToJson(LLSD("a\tb")), "\"a\\tb\"");

        // control character must be escaped one way or another; round-trip it
        LLSD ctrl;
        ensure("control char parses",
               LlsdFromJsonString(LlsdToJson(LLSD(std::string("a\x01z"))), ctrl));
        ensure_equals("control char round-trip", ctrl.asString(), std::string("a\x01z"));

        // UTF-8 passes through unescaped or escaped, but must round-trip
        std::string utf8("\xE3\x81\x93\xE3\x82\x93 caf\xC3\xA9");
        LLSD uni;
        ensure("utf8 parses", LlsdFromJsonString(LlsdToJson(LLSD(utf8)), uni));
        ensure_equals("utf8 round-trip", uni.asString(), utf8);
    }

    // special scalar types serialize as strings
    template<> template<>
    void llsdjson_object::test<7>()
    {
        LLUUID id("c96f9b8e-f5ad-4b72-b6f7-a2b30d1444d2");
        ensure_equals("uuid", LlsdToJson(LLSD(id)),
                      "\"c96f9b8e-f5ad-4b72-b6f7-a2b30d1444d2\"");

        LLDate date("2026-06-11T12:34:56Z");
        ensure_equals("date", LlsdToJson(LLSD(date)), "\"2026-06-11T12:34:56Z\"");

        LLURI uri("http://example.com/path");
        ensure_equals("uri", LlsdToJson(LLSD(uri)), "\"http://example.com/path\"");
    }

    // non-finite reals degrade to null (JSON has no NaN/Infinity)
    template<> template<>
    void llsdjson_object::test<8>()
    {
        ensure_equals("nan", LlsdToJson(LLSD(std::numeric_limits<F64>::quiet_NaN())), "null");
        ensure_equals("inf", LlsdToJson(LLSD(std::numeric_limits<F64>::infinity())), "null");
        ensure_equals("-inf", LlsdToJson(LLSD(-std::numeric_limits<F64>::infinity())), "null");

        // and stay valid inside containers
        LLSD map;
        map["bad"] = std::numeric_limits<F64>::quiet_NaN();
        ensure_equals("nan in map", LlsdToJson(map), "{\"bad\":null}");
    }

    // structured round trip preserves everything
    template<> template<>
    void llsdjson_object::test<9>()
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
        ensure("round-trip parses: " + json, LlsdFromJsonString(json, dst));
        ensure("round-trip equality: " + json, llsd_equals(src, dst));
    }

    // full-precision reals survive the round trip
    template<> template<>
    void llsdjson_object::test<10>()
    {
        F64 vals[] = { 0.1, 1.0/3.0, 2.718281828459045, 1e-300, 1.7976931348623157e308 };
        for (F64 v : vals)
        {
            LLSD parsed;
            ensure("real parses", LlsdFromJsonString(LlsdToJson(LLSD(v)), parsed));
            ensure_equals("real precision", parsed.asReal(), v);
        }
    }

    // parse failures report errors and clear the output
    template<> template<>
    void llsdjson_object::test<11>()
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
        };
        for (const char* json : bad)
        {
            LLSD out = LLSD::emptyMap();
            std::string errmsg;
            bool ok = LlsdFromJsonString(json, out, &errmsg);
            ensure(std::string("rejects: ") + json, !ok);
            ensure(std::string("errmsg set for: ") + json, !errmsg.empty());
            ensure_equals(std::string("output cleared for: ") + json,
                          out.type(), LLSD::TypeUndefined);
        }

        // errmsg pointer is optional
        LLSD out;
        ensure("no errmsg crash", !LlsdFromJsonString("{", out));
    }

    // unicode object keys
    template<> template<>
    void llsdjson_object::test<12>()
    {
        LLSD src;
        src["caf\xC3\xA9"] = 1;
        LLSD dst;
        ensure("unicode key parses", LlsdFromJsonString(LlsdToJson(src), dst));
        ensure_equals("unicode key value", dst["caf\xC3\xA9"].asInteger(), 1);
    }

    // padded_string overload parses in place
    template<> template<>
    void llsdjson_object::test<13>()
    {
        LLSD out;
        ensure("padded parse",
               LlsdFromJsonString(simdjson::padded_string(std::string("{\"a\": [1, 2.5, \"three\"]}")), out));
        ensure_equals("padded int", out["a"][0].asInteger(), 1);
        ensure_equals("padded real", out["a"][1].asReal(), 2.5);
        ensure_equals("padded string", out["a"][2].asString(), "three");

        std::string errmsg;
        ensure("padded parse fails",
               !LlsdFromJsonString(simdjson::padded_string(std::string("{")), out, &errmsg));
        ensure("padded errmsg set", !errmsg.empty());
    }
}
