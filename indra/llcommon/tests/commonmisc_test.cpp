/**
 * @file commonmisc_test.cpp
 * @author Phoenix
 * @date 2005-10-12
 * @brief Tests of llhash, LLMemoryStream, LLSD notation and the U64 conversions
 *
 * $LicenseInfo:firstyear=2005&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include <algorithm>
#include <iomanip>
#include <iterator>

#include "linden_common.h"

#include "../llmemorystream.h"
#include "../llsd.h"
#include "../llsdserialize.h"
#include "../u64.h"
#include "../llhash.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    TEST_CASE("LLHash: llhash depends on the string and not on its pointer", "[LLHash]")
    {
        const char * str1 = "test string one";
        const char * same_as_str1 = "test string one";

        size_t hash1 = llhash(str1);
        size_t same_as_hash1 = llhash(same_as_str1);


        CHECK(hash1 == same_as_hash1);

        char str[100];
        strcpy( str, "Another test" );

        size_t hash2 = llhash(str);

        strcpy( str, "Different string, same pointer" );

        size_t hash3 = llhash(str);

        CHECK(hash2 != hash3);
    }

    TEST_CASE("LLMemoryStream: operator>> reads whitespace-separated words", "[LLMemoryStream]")
    {
        const char HELLO_WORLD[] = "hello world";
        LLMemoryStream mem((U8*)&HELLO_WORLD[0], static_cast<S32>(strlen(HELLO_WORLD)));      /* Flawfinder: ignore */
        std::string hello;
        std::string world;
        mem >> hello >> world;
        CHECK(hello == std::string("hello"));
        CHECK(world == std::string("world"));
    }

    TEST_CASE("LLSD: fromNotation parses a map from an LLMemoryStream", "[LLSD]")
    {
        std::ostringstream resp;
        resp << "{'connect':true,  'position':[r128,r128,r128], 'look_at':[r0,r1,r0], 'agent_access':'M', 'region_x':i8192, 'region_y':i8192}";
        std::string str = resp.str();
        LLMemoryStream mstr((U8*)str.c_str(), static_cast<S32>(str.size()));
        LLSD response;
        S32 count = LLSDSerialize::fromNotation(response, mstr, str.size());
        CHECK(response.isDefined());
        CHECK(count == 13);
        CHECK(response.type() == LLSD::TypeMap);
        CHECK(response.size() == 6);
        CHECK(response["connect"].asBoolean());
        CHECK(response["region_x"].asInteger() == 8192);
        CHECK(response["region_y"].asInteger() == 8192);
    }

    TEST_CASE("LLSD: binary writes to notation as b(6) and parses back", "[LLSD]")
    {
        const std::string decoded("random");
        const std::string streamed("b(6)\"random\"");
        typedef std::vector<U8> buf_t;
        buf_t buf;
        std::copy(
            decoded.begin(),
            decoded.end(),
            std::back_insert_iterator<buf_t>(buf));
        LLSD sd;
        sd = buf;
        std::stringstream str;
        S32 count = LLSDSerialize::toNotation(sd, str);
        CHECK(count == 1);
        std::string actual(str.str());
        CHECK(actual == streamed);
        sd.clear();
        LLSDSerialize::fromNotation(sd, str, str.str().size());
        std::vector<U8> after;
        after = sd.asBinary();
        REQUIRE(after.size() == decoded.size());
        CHECK(memcmp(&after[0], decoded.c_str(), decoded.size()) == 0);
    }

    TEST_CASE("LLSD: random binary round-trips through notation", "[LLSD]")
    {
        for(S32 i = 0; i < 100; ++i)
        {
            CAPTURE(i);
            // gen up a starting point
            typedef std::vector<U8> buf_t;
            buf_t source;
            srand(i);       /* Flawfinder: ignore */
            S32 size = rand() % 1000 + 10;
            std::generate_n(
                std::back_insert_iterator<buf_t>(source),
                size,
                rand);
            LLSD sd(source);
            std::stringstream str;
            S32 count = LLSDSerialize::toNotation(sd, str);
            sd.clear();
            REQUIRE(count == 1);
            LLSD sd2;
            count = LLSDSerialize::fromNotation(sd2, str, str.str().size());
            REQUIRE(count == 1);
            buf_t dest = sd2.asBinary();
            REQUIRE(dest.size() == source.size());
            REQUIRE(source == dest);
        }
    }

    TEST_CASE("LLSD: an escaped string with newlines and quotes parses back", "[LLSD]")
    {
        std::ostringstream ostr;
        ostr << "{'task_id':u1fd77b79-a8e7-25a5-9454-02a4d948ba1c}\n"
             << "{\n\tname\tObject|\n}\n";
        std::string expected = ostr.str();
        std::stringstream serialized;
        serialized << "'" << LLSDNotationFormatter::escapeString(expected)
               << "'";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(
            sd,
            serialized,
            serialized.str().size());
        CHECK(count == 1);
        CHECK(sd.asString() == expected);
    }

    TEST_CASE("LLSD: random raw binary in b(size) notation parses", "[LLSD]")
    {
        for(S32 i = 0; i < 100; ++i)
        {
            CAPTURE(i);
            // gen up a starting point
            typedef std::vector<U8> buf_t;
            buf_t source;
            srand(666 + i);     /* Flawfinder: ignore */
            S32 size = rand() % 1000 + 10;
            std::generate_n(
                std::back_insert_iterator<buf_t>(source),
                size,
                rand);
            std::stringstream str;
            str << "b(" << size << ")\"";
            str.write((const char*)&source[0], size);
            str << "\"";
            LLSD sd;
            S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
            REQUIRE(count == 1);
            buf_t actual = sd.asBinary();
            REQUIRE(actual.size() == (size_t)size);
            REQUIRE(memcmp(&source[0], &actual[0], size) == 0);
        }
    }

    TEST_CASE("LLSD: a raw string in s(size) notation keeps quotes and whitespace", "[LLSD]")
    {
        std::string expected("'{\"task_id\":u1fd77b79-a8e7-25a5-9454-02a4d948ba1c}'\t\n\t\t");
        std::stringstream str;
        str << "s(" << expected.size() << ")'";
        str.write(expected.c_str(), expected.size());
        str << "'";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
        CHECK(count == 1);
        std::string actual = sd.asString();
        CHECK(actual.size() == expected.size());
        CHECK(actual == expected);
    }

    TEST_CASE("LLSD: fromNotation parses a map of mixed scalar and array values", "[LLSD]")
    {
        std::string msg("come on in");
        std::stringstream stream;
        stream << "{'connect':1, 'message':'" << msg << "',"
               << " 'position':[r45.65,r100.1,r25.5],"
               << " 'look_at':[r0,r1,r0],"
               << " 'agent_access':'PG'}";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(
            sd,
            stream,
            stream.str().size());
        CHECK(count == 12);
        CHECK(sd["connect"].asBoolean());
        CHECK(sd["message"].asString() == msg);
        CHECK(sd["position"][0].asReal() == 45.65);
        CHECK(sd["position"][1].asReal() == 100.1);
        CHECK(sd["position"][2].asReal() == 25.5);
        CHECK(sd["look_at"][0].asReal() == 0.0);
        CHECK(sd["look_at"][1].asReal() == 1.0);
        CHECK(sd["look_at"][2].asReal() == 0.0);
    }

    TEST_CASE("LLSD: one-character and empty strings parse in a map", "[LLSD]")
    {
        std::stringstream resp;
        resp << "{'label':'short string test', 'singlechar':'a', 'empty':'', 'endoftest':'end' }";
        LLSD response;
        S32 count = LLSDSerialize::fromNotation(
            response,
            resp,
            resp.str().size());
        CHECK(count == 5);
        CHECK(response.type() == LLSD::TypeMap);
        CHECK(response.size() == 4);
        CHECK(response["singlechar"].asString() == "a");
        CHECK(response["empty"].asString() == "");
    }

    TEST_CASE("LLSD: one-byte binary and raw string values parse in a map", "[LLSD]")
    {
        std::ostringstream resp;
        resp << "{'label':'short binary test', 'singlebinary':b(1)\"A\", 'singlerawstring':s(1)\"A\", 'endoftest':'end' }";
        std::string str = resp.str();
        LLSD sd;
        LLMemoryStream mstr((U8*)str.c_str(), static_cast<S32>(str.size()));
        S32 count = LLSDSerialize::fromNotation(sd, mstr, str.size());
        CHECK(count == 5);
        REQUIRE(sd.isDefined());
        CHECK(sd.type() == LLSD::TypeMap);
        REQUIRE(sd.size() == 4);
        CHECK(sd["label"].asString() ==
            "short binary test");
        const std::vector<U8> expected{'A'};
        CHECK(sd["singlebinary"].asBinary() == expected);
        CHECK(sd["singlerawstring"].asString() ==
            std::string("A"));
        CHECK(sd["endoftest"].asString() == "end");
    }

    TEST_CASE("LLSD: escapeString escapes single quotes so a message parses back", "[LLSD]")
    {

        std::string message("parcel '' is naughty.");
        std::stringstream str;
        str << "{'message':'" << LLSDNotationFormatter::escapeString(message)
            << "'}";
        std::string expected_str("{'message':'parcel \\'\\' is naughty.'}");
        std::string actual_str = str.str();
        CHECK(actual_str == expected_str);
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, actual_str.size());
        CHECK(count == 2);
        CHECK(sd.isDefined());
        std::string actual = sd["message"].asString();
        CHECK(actual == message);
    }

    TEST_CASE("LLSD: a string of single and double quotes escapes and parses back", "[LLSD]")
    {
        std::string expected("\"\"\"\"''''''\"");
        std::stringstream str;
        str << "'" << LLSDNotationFormatter::escapeString(expected) << "'";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
        CHECK(count == 1);
        CHECK(sd.asString() == expected);
    }

    TEST_CASE("LLSD: a string ending in a backslash escapes and parses back", "[LLSD]")
    {
        std::string expected("mytest\\");
        std::stringstream str;
        str << "'" << LLSDNotationFormatter::escapeString(expected) << "'";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
        CHECK(count == 1);
        CHECK(sd.asString() == expected);
    }

    TEST_CASE("LLSD: random strings escape and parse back", "[LLSD]")
    {
        for(S32 i = 0; i < 1000; ++i)
        {
            CAPTURE(i);
            // gen up a starting point
            std::string expected;
            srand(1337 + i);        /* Flawfinder: ignore */
            S32 size = rand() % 30 + 5;
            std::generate_n(
                std::back_insert_iterator<std::string>(expected),
                size,
                rand);
            std::stringstream str;
            str << "'" << LLSDNotationFormatter::escapeString(expected) << "'";
            LLSD sd;
            S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
            REQUIRE(count == 1);
            std::string actual = sd.asString();
            REQUIRE(actual == expected);
        }
    }

    TEST_CASE("LLSD: an instant message parameter array parses", "[LLSD]")
    {
        std::string param = "[{'version':i1},{'data':{'binary_bucket':b(0)\"\"},'from_id':u3c115e51-04f4-523c-9fa6-98aff1034730,'from_name':'Phoenix Linden','id':u004e45e5-5576-277a-fba7-859d6a4cb5c8,'message':'hey','offline':i0,'timestamp':i0,'to_id':u3c5f1bb4-5182-7546-6401-1d329b4ff2f8,'type':i0},{'agent_id':u3c115e51-04f4-523c-9fa6-98aff1034730,'god_level':i0,'limited_to_estate':i1}]";
        std::istringstream istr;
        istr.str(param);
        LLSD param_sd;
        LLSDSerialize::fromNotation(param_sd, istr, param.size());
        CHECK(param_sd.type() == LLSD::TypeArray);
        LLSD version_sd = param_sd[0];
        CHECK(version_sd.type() == LLSD::TypeMap);
        CHECK(version_sd.has("version"));
        CHECK(version_sd["version"].asInteger() == 1);
        LLSD src_sd = param_sd[1];
        CHECK(src_sd.type() == LLSD::TypeMap);
        LLSD dst_sd = param_sd[2];
        CHECK(dst_sd.type() == LLSD::TypeMap);
    }

    TEST_CASE("LLSD: an undefined value parses in a map in an array", "[LLSD]")
    {
        std::string val = "[{'failures':!,'successfuls':[u3c115e51-04f4-523c-9fa6-98aff1034730]}]";
        std::istringstream istr;
        istr.str(val);
        LLSD sd;
        LLSDSerialize::fromNotation(sd, istr, val.size());
        CHECK(sd.type() == LLSD::TypeArray);
        REQUIRE(sd.size() == 1);
        LLSD failures = sd[0]["failures"];
        CHECK(failures.isUndefined());
        LLSD success = sd[0]["successfuls"];
        CHECK(success.type() == LLSD::TypeArray);
        REQUIRE(success.size() == 1);
        CHECK(success[0].type() == LLSD::TypeUUID);
    }

    TEST_CASE("LLSD: booleans parse from f t 0 and 1 in an array and a map", "[LLSD]")
    {
        std::string val = "[f,t,0,1,{'foo':t,'bar':f}]";
        std::istringstream istr;
        istr.str(val);
        LLSD sd;
        LLSDSerialize::fromNotation(sd, istr, val.size());
        CHECK(sd.type() == LLSD::TypeArray);
        REQUIRE(sd.size() == 5);
        CHECK_FALSE(sd[0].asBoolean());
        CHECK(sd[1].asBoolean());
        CHECK_FALSE(sd[2].asBoolean());
        CHECK(sd[3].asBoolean());
        LLSD map = sd[4];
        CHECK(map.type() == LLSD::TypeMap);
        CHECK(map["foo"].type() == LLSD::TypeBoolean);
        CHECK(map["foo"].asBoolean());
        CHECK(map["bar"].type() == LLSD::TypeBoolean);
        CHECK_FALSE(map["bar"].asBoolean());
    }

    TEST_CASE("U64_conversion: U64_to_str writes the digits and truncates to the buffer", "[U64_conversion]")
    {
        U64 val;
        std::string val_str;
        char result[256];
        std::string result_str;

        val = U64L(18446744073709551610); // slightly less than MAX_U64
        val_str = "18446744073709551610";

        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = 0;
        val_str = "0";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = U64L(18446744073709551615); // 0xFFFFFFFFFFFFFFFF
        val_str = "18446744073709551615";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        // overflow - will result in warning at compile time
        val = U64L(18446744073709551615) + 1; // overflow 0xFFFFFFFFFFFFFFFF + 1 == 0
        val_str = "0";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = U64(-1); // 0xFFFFFFFFFFFFFFFF == 18446744073709551615
        val_str = "18446744073709551615";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = U64L(10000000000000000000); // testing preserving of 0s
        val_str = "10000000000000000000";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = 1; // testing no leading 0s
        val_str = "1";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = U64L(18446744073709551615); // testing exact sized buffer for result
        val_str = "18446744073709551615";
        memset(result, 'A', sizeof(result)); // initialize buffer with all 'A'
        U64_to_str(val, result, sizeof("18446744073709551615")); //pass in the exact size
        result_str = (const char*) result;
        CHECK(val_str == result_str);

        val = U64L(18446744073709551615); // testing smaller sized buffer for result
        val_str = "1844";
        memset(result, 'A', sizeof(result)); // initialize buffer with all 'A'
        U64_to_str(val, result, 5); //pass in a size of 5. should only copy first 4 integers and add a null terminator
        result_str = (const char*) result;
        CHECK(val_str == result_str);
    }

    TEST_CASE("U64_conversion: str_to_U64 reads the first run of digits and wraps on overflow", "[U64_conversion]")
    {
        U64 val;
        U64 result;

        val = U64L(18446744073709551610); // slightly less than MAX_U64
        result = str_to_U64("18446744073709551610");
        CHECK(val == result);

        val = U64L(0); // empty string
        result = str_to_U64(LLStringUtil::null);
        CHECK(val == result);

        val = U64L(0); // 0
        result = str_to_U64("0");
        CHECK(val == result);

        val = U64L(18446744073709551615); // 0xFFFFFFFFFFFFFFFF
        result = str_to_U64("18446744073709551615");
        CHECK(val == result);

        // overflow - will result in warning at compile time
        val = U64L(18446744073709551615) + 1; // overflow 0xFFFFFFFFFFFFFFFF + 1 == 0
        result = str_to_U64("18446744073709551616");
        CHECK(val == result);

        val = U64L(1234); // process till first non-integral character
        result = str_to_U64("1234A5678");
        CHECK(val == result);

        val = U64L(5678); // skip all non-integral characters
        result = str_to_U64("ABCD5678");
        CHECK(val == result);

        // should it skip negative sign and process
        // rest of string or return 0
        val = U64L(1234); // skip initial negative sign
        result = str_to_U64("-1234");
        CHECK(val == result);

        val = U64L(5678); // stop at negative sign in the middle
        result = str_to_U64("5678-1234");
        CHECK(val == result);

        val = U64L(0); // no integers
        result = str_to_U64("AaCD");
        CHECK(val == result);
    }

    TEST_CASE("U64_conversion: U64_to_F64 converts values from zero to the largest U64", "[U64_conversion]")
    {
        F64 val;
        F64 result;

        result = 18446744073709551610.0;
        val = U64_to_F64(U64L(18446744073709551610));
        CHECK(val == result);

        result = 18446744073709551615.0; // 0xFFFFFFFFFFFFFFFF
        val = U64_to_F64(U64L(18446744073709551615));
        CHECK(val == result);

        result = 0.0; // overflow 0xFFFFFFFFFFFFFFFF + 1 == 0
        // overflow - will result in warning at compile time
        val = U64_to_F64(U64L(18446744073709551615)+1);
        CHECK(val == result);

        result = 0.0; // 0
        val = U64_to_F64(U64L(0));
        CHECK(val == result);

        result = 1.0; // odd
        val = U64_to_F64(U64L(1));
        CHECK(val == result);

        result = 2.0; // even
        val = U64_to_F64(U64L(2));
        CHECK(val == result);

        result = F64(0x7FFFFFFFFFFFFFFF) * 1.0; // 0x7FFFFFFFFFFFFFFF
        val = U64_to_F64(U64L(0x7FFFFFFFFFFFFFFF));
        CHECK(val == result);
    }
}
