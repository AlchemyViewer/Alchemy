/**
 * @file common.cpp
 * @author Phoenix
 * @date 2005-10-12
 * @brief Common templates for test framework
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

/**
 *
 * THOROUGH_DESCRIPTION of common.cpp
 *
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
/*
    template<> template<>
    void sd_object::test<16>()
    {
    }
*/
}

#if 0
'{\'task_id\':u1fd77b79-a8e7-25a5-9454-02a4d948ba1c}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t00082000\n\t\tcreator_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t00000000-0000-0000-0000-000000000000\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t10284\n\ttotal_crc\t35\n\ttype\t1\n\ttask_valid\t2\n\ttravel_access\t21\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t0\t0\t0\n\toldpos\t0\t0\t0\n\trotation\t4.371139183945160766597837e-08\t1\t4.371139183945160766597837e-08\t0\n\tvelocity\t0\t0\t0\n\tangvel\t0\t0\t0\n\tscale\t0.2816932\t0.2816932\t0.2816932\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t80\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t16\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\tscale_x\t1\n\t\t\tscale_y\t1\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t1\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t6\n\t{\n\t\timageid\t89556747-24cb-43ed-920b-47caed15465f\n\t\tcolors\t1 1 1 1\n\t\tscales\t0.56\n\t\tscalet\t0.56\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\t89556747-24cb-43ed-920b-47caed15465f\n\t\tcolors\t1 1 1 1\n\t\tscales\t0.56\n\t\tscalet\t0.56\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\t89556747-24cb-43ed-920b-47caed15465f\n\t\tcolors\t1 1 1 1\n\t\tscales\t0.56\n\t\tscalet\t0.56\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\t89556747-24cb-43ed-920b-47caed15465f\n\t\tcolors\t1 1 1 1\n\t\tscales\t0.56\n\t\tscalet\t0.56\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\t89556747-24cb-43ed-920b-47caed15465f\n\t\tcolors\t1 1 1 1\n\t\tscales\t0.56\n\t\tscalet\t0.56\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\t89556747-24cb-43ed-920b-47caed15465f\n\t\tcolors\t1 1 1 1\n\t\tscales\t0.56\n\t\tscalet\t0.56\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t1132625972249870\n\tbirthtime\t1132625953120694\n\treztime\t1132625953120694\n\tparceltime\t1132625953120694\n\ttax_rate\t1.01615\n\tnamevalue\tAttachmentOrientation VEC3 RW DS -3.141593, 0.000000, -3.141593\n\tnamevalue\tAttachmentOffset VEC3 RW DS 0.000000, 0.000000, 0.000000\n\tnamevalue\tAttachPt U32 RW S 5\n\tnamevalue\tAttachItemID STRING RW SV 1f9975c0-2951-1b93-dd83-46e2b932fcc8\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\torig_asset_id\t52019cdd-b464-ba19-e66d-3da751fef9da\n\torig_item_id\t1f9975c0-2951-1b93-dd83-46e2b932fcc8\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n'
#endif

namespace
{
}

namespace
{
    // llstrtou64
    // seems to be deprecated - could not find it being used
    // anywhere in the tarball - skipping unit tests for now
}


namespace
{
    TEST_CASE("LLHash: llhash depends on the string and not on its pointer", "[LLHash]")
    {
        const char * str1 = "test string one";
        const char * same_as_str1 = "test string one";

        size_t hash1 = llhash(str1);
        size_t same_as_hash1 = llhash(same_as_str1);


        UNSCOPED_INFO("Hashes from identical strings should be equal");
        CHECK(hash1 == same_as_hash1);

        char str[100];
        strcpy( str, "Another test" );

        size_t hash2 = llhash(str);

        strcpy( str, "Different string, same pointer" );

        size_t hash3 = llhash(str);

        UNSCOPED_INFO("Hashes from same pointer but different string should not be equal");
        CHECK(hash2 != hash3);
    }

    TEST_CASE("LLMemoryStream: operator>> reads whitespace-separated words", "[LLMemoryStream]")
    {
        const char HELLO_WORLD[] = "hello world";
        LLMemoryStream mem((U8*)&HELLO_WORLD[0], static_cast<S32>(strlen(HELLO_WORLD)));      /* Flawfinder: ignore */
        std::string hello;
        std::string world;
        mem >> hello >> world;
        UNSCOPED_INFO("first word");
        CHECK(hello == std::string("hello"));
        UNSCOPED_INFO("second word");
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
        UNSCOPED_INFO("stream parsed");
        REQUIRE(response.isDefined());
        UNSCOPED_INFO("stream parse count");
        CHECK(count == 13);
        UNSCOPED_INFO("sd type");
        CHECK(response.type() == LLSD::TypeMap);
        UNSCOPED_INFO("map element count");
        REQUIRE(response.size() == 6);
        UNSCOPED_INFO("value connect");
        CHECK(response["connect"].asBoolean() == true);
        UNSCOPED_INFO("value region_x");
        CHECK(response["region_x"].asInteger() == 8192);
        UNSCOPED_INFO("value region_y");
        CHECK(response["region_y"].asInteger() == 8192);
    }

    TEST_CASE("LLSD: binary writes to notation as b(6) and parses back", "[LLSD]")
    {
        const std::string decoded("random");
        //const std::string encoded("cmFuZG9t\n");
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
        UNSCOPED_INFO("output count");
        CHECK(count == 1);
        std::string actual(str.str());
        UNSCOPED_INFO("formatted binary encoding");
        CHECK(actual == streamed);
        sd.clear();
        LLSDSerialize::fromNotation(sd, str, str.str().size());
        std::vector<U8> after;
        after = sd.asBinary();
        UNSCOPED_INFO("binary decoded size");
        REQUIRE(after.size() == decoded.size());
        UNSCOPED_INFO("binary decoding");
        REQUIRE(memcmp(
                                       &after[0],
                                       decoded.c_str(),
                                       decoded.size()) == 0);
    }

    TEST_CASE("LLSD: random binary round-trips through notation", "[LLSD]")
    {
        for(S32 i = 0; i < 100; ++i)
        {
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
            UNSCOPED_INFO("format count");
            REQUIRE(count == 1);
            LLSD sd2;
            count = LLSDSerialize::fromNotation(sd2, str, str.str().size());
            UNSCOPED_INFO("parse count");
            REQUIRE(count == 1);
            buf_t dest = sd2.asBinary();
            str.str("");
            str << "binary encoding size " << i;
            UNSCOPED_INFO(str.str().c_str());
            REQUIRE(dest.size() == source.size());
            str.str("");
            str << "binary encoding " << i;
            UNSCOPED_INFO(str.str().c_str());
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
        UNSCOPED_INFO("parse count");
        CHECK(count == 1);
        UNSCOPED_INFO("String streaming");
        CHECK(sd.asString() == expected);
    }

    TEST_CASE("LLSD: random raw binary in b(size) notation parses", "[LLSD]")
    {
        for(S32 i = 0; i < 100; ++i)
        {
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
            UNSCOPED_INFO("binary parse");
            REQUIRE(count == 1);
            buf_t actual = sd.asBinary();
            UNSCOPED_INFO("binary size");
            REQUIRE(actual.size() == (size_t)size);
            UNSCOPED_INFO("binary data");
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
        UNSCOPED_INFO("parse count");
        CHECK(count == 1);
        std::string actual = sd.asString();
        UNSCOPED_INFO("string sizes");
        REQUIRE(actual.size() == expected.size());
        UNSCOPED_INFO("string content");
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
        UNSCOPED_INFO("parse count");
        CHECK(count == 12);
        UNSCOPED_INFO("bool value");
        CHECK(sd["connect"].asBoolean() == true);
        UNSCOPED_INFO("message value");
        CHECK(sd["message"].asString() == msg);
        UNSCOPED_INFO("pos x");
        CHECK(sd["position"][0].asReal() == 45.65);
        UNSCOPED_INFO("pos y");
        CHECK(sd["position"][1].asReal() == 100.1);
        UNSCOPED_INFO("pos z");
        CHECK(sd["position"][2].asReal() == 25.5);
        UNSCOPED_INFO("look x");
        CHECK(sd["look_at"][0].asReal() == 0.0);
        UNSCOPED_INFO("look y");
        CHECK(sd["look_at"][1].asReal() == 1.0);
        UNSCOPED_INFO("look z");
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
        UNSCOPED_INFO("parse count");
        CHECK(count == 5);
        UNSCOPED_INFO("sd type");
        CHECK(response.type() == LLSD::TypeMap);
        UNSCOPED_INFO("map element count");
        REQUIRE(response.size() == 4);
        UNSCOPED_INFO("singlechar");
        CHECK(response["singlechar"].asString() == "a");
        UNSCOPED_INFO("empty");
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
        UNSCOPED_INFO("parse count");
        CHECK(count == 5);
        UNSCOPED_INFO("sd created");
        REQUIRE(sd.isDefined());
        UNSCOPED_INFO("sd type");
        CHECK(sd.type() == LLSD::TypeMap);
        UNSCOPED_INFO("map element count");
        REQUIRE(sd.size() == 4);
        UNSCOPED_INFO("label");
        CHECK(sd["label"].asString() ==
            "short binary test");
        std::vector<U8> bin =  sd["singlebinary"].asBinary();
        std::vector<U8> expected;
        expected.resize(1);
        expected[0] = 'A';
        UNSCOPED_INFO("single binary");
        CHECK(memcmp(&bin[0], &expected[0], 1) == 0);
        UNSCOPED_INFO("single string");
        CHECK(sd["singlerawstring"].asString() ==
            std::string("A"));
        UNSCOPED_INFO("end");
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
        UNSCOPED_INFO("stream contents");
        CHECK(actual_str == expected_str);
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, actual_str.size());
        UNSCOPED_INFO("parse count");
        CHECK(count == 2);
        UNSCOPED_INFO("valid parse");
        REQUIRE(sd.isDefined());
        std::string actual = sd["message"].asString();
        UNSCOPED_INFO("message contents");
        CHECK(actual == message);
    }

    TEST_CASE("LLSD: a string of single and double quotes escapes and parses back", "[LLSD]")
    {
        std::string expected("\"\"\"\"''''''\"");
        std::stringstream str;
        str << "'" << LLSDNotationFormatter::escapeString(expected) << "'";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
        UNSCOPED_INFO("parse count");
        CHECK(count == 1);
        UNSCOPED_INFO("string value");
        CHECK(sd.asString() == expected);
    }

    TEST_CASE("LLSD: a string ending in a backslash escapes and parses back", "[LLSD]")
    {
        std::string expected("mytest\\");
        std::stringstream str;
        str << "'" << LLSDNotationFormatter::escapeString(expected) << "'";
        LLSD sd;
        S32 count = LLSDSerialize::fromNotation(sd, str, str.str().size());
        UNSCOPED_INFO("parse count");
        CHECK(count == 1);
        UNSCOPED_INFO("string value");
        CHECK(sd.asString() == expected);
    }

    TEST_CASE("LLSD: random strings escape and parse back", "[LLSD]")
    {
        for(S32 i = 0; i < 1000; ++i)
        {
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
            S32 count = LLSDSerialize::fromNotation(sd, str, expected.size());
            UNSCOPED_INFO("parse count");
            REQUIRE(count == 1);
            std::string actual = sd.asString();
/*
            if(actual != expected)
            {
                LL_WARNS() << "iteration " << i << LL_ENDL;
                std::ostringstream e_str;
                std::string::iterator iter = expected.begin();
                std::string::iterator end = expected.end();
                for(; iter != end; ++iter)
                {
                    e_str << (S32)((U8)(*iter)) << " ";
                }
                e_str << std::endl;
                llsd_serialize_string(e_str, expected);
                LL_WARNS() << "expected size: " << expected.size() << LL_ENDL;
                LL_WARNS() << "expected:      " << e_str.str() << LL_ENDL;

                std::ostringstream a_str;
                iter = actual.begin();
                end = actual.end();
                for(; iter != end; ++iter)
                {
                    a_str << (S32)((U8)(*iter)) << " ";
                }
                a_str << std::endl;
                llsd_serialize_string(a_str, actual);
                LL_WARNS() << "actual size:   " << actual.size() << LL_ENDL;
                LL_WARNS() << "actual:      " << a_str.str() << LL_ENDL;
            }
*/
            UNSCOPED_INFO("string value");
            REQUIRE(actual == expected);
        }
    }

    TEST_CASE("LLSD: an instant message parameter array parses", "[LLSD]")
    {
//#if LL_WINDOWS && _MSC_VER >= 1400
//        skip_fail("Fails on VS2005 due to broken LLSDSerialize::fromNotation() parser.");
//#endif
        std::string param = "[{'version':i1},{'data':{'binary_bucket':b(0)\"\"},'from_id':u3c115e51-04f4-523c-9fa6-98aff1034730,'from_name':'Phoenix Linden','id':u004e45e5-5576-277a-fba7-859d6a4cb5c8,'message':'hey','offline':i0,'timestamp':i0,'to_id':u3c5f1bb4-5182-7546-6401-1d329b4ff2f8,'type':i0},{'agent_id':u3c115e51-04f4-523c-9fa6-98aff1034730,'god_level':i0,'limited_to_estate':i1}]";
        std::istringstream istr;
        istr.str(param);
        LLSD param_sd;
        LLSDSerialize::fromNotation(param_sd, istr, param.size());
        UNSCOPED_INFO("parsed type");
        CHECK(param_sd.type() == LLSD::TypeArray);
        LLSD version_sd = param_sd[0];
        UNSCOPED_INFO("version type");
        CHECK(version_sd.type() == LLSD::TypeMap);
        UNSCOPED_INFO("has version");
        REQUIRE(version_sd.has("version"));
        UNSCOPED_INFO("version number");
        CHECK(version_sd["version"].asInteger() == 1);
        LLSD src_sd = param_sd[1];
        UNSCOPED_INFO("src type");
        CHECK(src_sd.type() == LLSD::TypeMap);
        LLSD dst_sd = param_sd[2];
        UNSCOPED_INFO("dst type");
        CHECK(dst_sd.type() == LLSD::TypeMap);
    }

    TEST_CASE("LLSD: an undefined value parses in a map in an array", "[LLSD]")
    {
        std::string val = "[{'failures':!,'successfuls':[u3c115e51-04f4-523c-9fa6-98aff1034730]}]";
        std::istringstream istr;
        istr.str(val);
        LLSD sd;
        LLSDSerialize::fromNotation(sd, istr, val.size());
        UNSCOPED_INFO("parsed type");
        CHECK(sd.type() == LLSD::TypeArray);
        UNSCOPED_INFO("parsed size");
        REQUIRE(sd.size() == 1);
        LLSD failures = sd[0]["failures"];
        UNSCOPED_INFO("no failures.");
        REQUIRE(failures.isUndefined());
        LLSD success = sd[0]["successfuls"];
        UNSCOPED_INFO("success type");
        CHECK(success.type() == LLSD::TypeArray);
        UNSCOPED_INFO("success size");
        REQUIRE(success.size() == 1);
        UNSCOPED_INFO("success instance type");
        CHECK(success[0].type() == LLSD::TypeUUID);
    }

    TEST_CASE("LLSD: booleans parse from f t 0 and 1 in an array and a map", "[LLSD]")
    {
        std::string val = "[f,t,0,1,{'foo':t,'bar':f}]";
        std::istringstream istr;
        istr.str(val);
        LLSD sd;
        LLSDSerialize::fromNotation(sd, istr, val.size());
        UNSCOPED_INFO("parsed type");
        CHECK(sd.type() == LLSD::TypeArray);
        UNSCOPED_INFO("parsed size");
        REQUIRE(sd.size() == 5);
        UNSCOPED_INFO("element 0 false");
        CHECK(sd[0].asBoolean() == false);
        UNSCOPED_INFO("element 1 true");
        CHECK(sd[1].asBoolean() == true);
        UNSCOPED_INFO("element 2 false");
        CHECK(sd[2].asBoolean() == false);
        UNSCOPED_INFO("element 3 true");
        CHECK(sd[3].asBoolean() == true);
        LLSD map = sd[4];
        UNSCOPED_INFO("element 4 type");
        CHECK(map.type() == LLSD::TypeMap);
        UNSCOPED_INFO("map foo type");
        CHECK(map["foo"].type() == LLSD::TypeBoolean);
        UNSCOPED_INFO("map foo value");
        CHECK(map["foo"].asBoolean() == true);
        UNSCOPED_INFO("map bar type");
        CHECK(map["bar"].type() == LLSD::TypeBoolean);
        UNSCOPED_INFO("map bar value");
        CHECK(map["bar"].asBoolean() == false);
    }

    // U64_to_str
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
        UNSCOPED_INFO("U64_to_str converted 1.1");
        CHECK(val_str == result_str);

        val = 0;
        val_str = "0";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.2");
        CHECK(val_str == result_str);

        val = U64L(18446744073709551615); // 0xFFFFFFFFFFFFFFFF
        val_str = "18446744073709551615";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.3");
        CHECK(val_str == result_str);

        // overflow - will result in warning at compile time
        val = U64L(18446744073709551615) + 1; // overflow 0xFFFFFFFFFFFFFFFF + 1 == 0
        val_str = "0";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.4");
        CHECK(val_str == result_str);

        val = U64(-1); // 0xFFFFFFFFFFFFFFFF == 18446744073709551615
        val_str = "18446744073709551615";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.5");
        CHECK(val_str == result_str);

        val = U64L(10000000000000000000); // testing preserving of 0s
        val_str = "10000000000000000000";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.6");
        CHECK(val_str == result_str);

        val = 1; // testing no leading 0s
        val_str = "1";
        U64_to_str(val, result, sizeof(result));
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.7");
        CHECK(val_str == result_str);

        val = U64L(18446744073709551615); // testing exact sized buffer for result
        val_str = "18446744073709551615";
        memset(result, 'A', sizeof(result)); // initialize buffer with all 'A'
        U64_to_str(val, result, sizeof("18446744073709551615")); //pass in the exact size
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.8");
        CHECK(val_str == result_str);

        val = U64L(18446744073709551615); // testing smaller sized buffer for result
        val_str = "1844";
        memset(result, 'A', sizeof(result)); // initialize buffer with all 'A'
        U64_to_str(val, result, 5); //pass in a size of 5. should only copy first 4 integers and add a null terminator
        result_str = (const char*) result;
        UNSCOPED_INFO("U64_to_str converted 1.9");
        CHECK(val_str == result_str);
    }

    // str_to_U64
    TEST_CASE("U64_conversion: str_to_U64 reads the first run of digits and wraps on overflow", "[U64_conversion]")
    {
        U64 val;
        U64 result;

        val = U64L(18446744073709551610); // slightly less than MAX_U64
        result = str_to_U64("18446744073709551610");
        UNSCOPED_INFO("str_to_U64 converted 2.1");
        CHECK(val == result);

        val = U64L(0); // empty string
        result = str_to_U64(LLStringUtil::null);
        UNSCOPED_INFO("str_to_U64 converted 2.2");
        CHECK(val == result);

        val = U64L(0); // 0
        result = str_to_U64("0");
        UNSCOPED_INFO("str_to_U64 converted 2.3");
        CHECK(val == result);

        val = U64L(18446744073709551615); // 0xFFFFFFFFFFFFFFFF
        result = str_to_U64("18446744073709551615");
        UNSCOPED_INFO("str_to_U64 converted 2.4");
        CHECK(val == result);

        // overflow - will result in warning at compile time
        val = U64L(18446744073709551615) + 1; // overflow 0xFFFFFFFFFFFFFFFF + 1 == 0
        result = str_to_U64("18446744073709551616");
        UNSCOPED_INFO("str_to_U64 converted 2.5");
        CHECK(val == result);

        val = U64L(1234); // process till first non-integral character
        result = str_to_U64("1234A5678");
        UNSCOPED_INFO("str_to_U64 converted 2.6");
        CHECK(val == result);

        val = U64L(5678); // skip all non-integral characters
        result = str_to_U64("ABCD5678");
        UNSCOPED_INFO("str_to_U64 converted 2.7");
        CHECK(val == result);

        // should it skip negative sign and process
        // rest of string or return 0
        val = U64L(1234); // skip initial negative sign
        result = str_to_U64("-1234");
        UNSCOPED_INFO("str_to_U64 converted 2.8");
        CHECK(val == result);

        val = U64L(5678); // stop at negative sign in the middle
        result = str_to_U64("5678-1234");
        UNSCOPED_INFO("str_to_U64 converted 2.9");
        CHECK(val == result);

        val = U64L(0); // no integers
        result = str_to_U64("AaCD");
        UNSCOPED_INFO("str_to_U64 converted 2.10");
        CHECK(val == result);
    }

    // U64_to_F64
    TEST_CASE("U64_conversion: U64_to_F64 converts values from zero to the largest U64", "[U64_conversion]")
    {
        F64 val;
        F64 result;

        result = 18446744073709551610.0;
        val = U64_to_F64(U64L(18446744073709551610));
        UNSCOPED_INFO("U64_to_F64 converted 3.1");
        CHECK(val == result);

        result = 18446744073709551615.0; // 0xFFFFFFFFFFFFFFFF
        val = U64_to_F64(U64L(18446744073709551615));
        UNSCOPED_INFO("U64_to_F64 converted 3.2");
        CHECK(val == result);

        result = 0.0; // overflow 0xFFFFFFFFFFFFFFFF + 1 == 0
        // overflow - will result in warning at compile time
        val = U64_to_F64(U64L(18446744073709551615)+1);
        UNSCOPED_INFO("U64_to_F64 converted 3.3");
        CHECK(val == result);

        result = 0.0; // 0
        val = U64_to_F64(U64L(0));
        UNSCOPED_INFO("U64_to_F64 converted 3.4");
        CHECK(val == result);

        result = 1.0; // odd
        val = U64_to_F64(U64L(1));
        UNSCOPED_INFO("U64_to_F64 converted 3.5");
        CHECK(val == result);

        result = 2.0; // even
        val = U64_to_F64(U64L(2));
        UNSCOPED_INFO("U64_to_F64 converted 3.6");
        CHECK(val == result);

        result = F64(0x7FFFFFFFFFFFFFFF) * 1.0; // 0x7FFFFFFFFFFFFFFF
        val = U64_to_F64(U64L(0x7FFFFFFFFFFFFFFF));
        UNSCOPED_INFO("U64_to_F64 converted 3.7");
        CHECK(val == result);
    }
}
