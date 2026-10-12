/**
 * @file   lluri_test.cpp
 * @brief  LLURI unit tests
 * @date   September 2006
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

#include "linden_common.h"

#include "../llsd.h"
#include "../lluri.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    struct URITestData {
        void require_parts(const LLURI& u,
                const char* expectedScheme,
                const char* expectedOpaque,
                const char* expectedAuthority,
                const char* expectedPath,
                const char* expectedQuery = "")
        {
            INFO("uri " << u.asString());
            REQUIRE(u.scheme() == expectedScheme);
            REQUIRE(u.opaque() == expectedOpaque);
            REQUIRE(u.authority() == expectedAuthority);
            REQUIRE(u.path() == expectedPath);
            REQUIRE(u.query() == expectedQuery);
        }

        void require_escape_round_trip(const std::string& uri_raw_1)
        {
            std::string uri_esc_1(LLURI::escape(uri_raw_1));
            std::string uri_raw_2(LLURI::unescape(uri_esc_1));
            REQUIRE(uri_raw_2 == uri_raw_1);
            std::string uri_esc_2(LLURI::escape(uri_raw_2));
            REQUIRE(uri_esc_2 == uri_esc_1);
        }
    };

    TEST_CASE_METHOD(URITestData, "LLURI: parts and query map of an http URI", "[LLURI]")
    {
        LLURI u("http://abc.com/def/ghi?x=37&y=hello");

        CHECK(u.scheme() == "http");
        CHECK(u.authority() == "abc.com");
        CHECK(u.path() == "/def/ghi");
        CHECK(u.query() == "x=37&y=hello");

        CHECK(u.hostName() == "abc.com");
        CHECK(u.hostPort() == 80);

        LLSD query = u.queryMap();
        CHECK(query["x"].asInteger() == 37);
        CHECK(query["y"].asString() == "hello");

        query = LLURI::queryMap("x=22.23&y=https://lindenlab.com/");
        CHECK(query["x"].asReal() == 22.23);
        CHECK(query["y"].asURI().asString() == "https://lindenlab.com/");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: an empty string has no parts", "[LLURI]")
    {
        require_parts(LLURI(""), "", "", "", "");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: without a scheme the whole string is opaque", "[LLURI]")
    {
        require_parts(LLURI("foo"), "", "foo", "", "");
        require_parts(LLURI("foo%3A"), "", "foo:", "", "");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: a scheme without paths leaves no authority or path", "[LLURI]")
    {
        require_parts(LLURI("mailto:zero@ll.com"),
            "mailto", "zero@ll.com", "", "");
        require_parts(LLURI("silly://abc/def?foo"),
            "silly", "//abc/def?foo", "", "");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: the authority ends at the first unescaped slash or question mark", "[LLURI]")
    {
        require_parts(LLURI("http:///"),
            "http", "///", "", "/");

        require_parts(LLURI("http://abc"),
            "http", "//abc", "abc", "");

        require_parts(LLURI("http://a%2Fb/cd"),
            "http", "//a/b/cd", "a/b", "/cd");

        require_parts(LLURI("http://host?"),
            "http", "//host?", "host", "");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: the path is unescaped and keeps colons and trailing slashes", "[LLURI]")
    {
        require_parts(LLURI("http://host/a/b/"),
                "http", "//host/a/b/", "host", "/a/b/");

        require_parts(LLURI("http://host/a%3Fb/"),
                "http", "//host/a?b/", "host", "/a?b/");

        require_parts(LLURI("http://host/a:b/"),
                "http", "//host/a:b/", "host", "/a:b/");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: the query follows the first question mark and is unescaped", "[LLURI]")
    {
        require_parts(LLURI("http://host/?"),
                "http", "//host/?", "host", "/", "");

        require_parts(LLURI("http://host/?x"),
                "http", "//host/?x", "host", "/", "x");

        require_parts(LLURI("http://host/??"),
                "http", "//host/??", "host", "/", "?");

        require_parts(LLURI("http://host/?%3F"),
                "http", "//host/??", "host", "/", "?");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: buildHTTP joins and escapes path parts and a query map", "[LLURI]")
    {
        LLSD path;
        path.append("x");
        path.append("123");
        require_parts(LLURI::buildHTTP("host", path),
            "http", "//host/x/123", "host", "/x/123");

        LLSD query;
        query["123"] = "12";
        query["abcd"] = "abc";
        require_parts(LLURI::buildHTTP("host", path, query),
            "http", "//host/x/123?123=12&abcd=abc",
            "host", "/x/123", "123=12&abcd=abc");

        CHECK(LLURI::buildHTTP("host", "").asString() ==
                      "http://host");
        CHECK(LLURI::buildHTTP("host", "/").asString() ==
                      "http://host/");
        CHECK(LLURI::buildHTTP("host", "//").asString() ==
                      "http://host/");
        CHECK(LLURI::buildHTTP("host", "dir name").asString() ==
                      "http://host/dir%20name");
        CHECK(LLURI::buildHTTP("host", "dir name/").asString() ==
                      "http://host/dir%20name/");
        CHECK(LLURI::buildHTTP("host", "/dir name").asString() ==
                      "http://host/dir%20name");
        CHECK(LLURI::buildHTTP("host", "/dir name/").asString() ==
                      "http://host/dir%20name/");
        CHECK(LLURI::buildHTTP("host", "dir name/subdir name").asString() ==
                      "http://host/dir%20name/subdir%20name");
        CHECK(LLURI::buildHTTP("host", "dir name/subdir name/").asString() ==
                      "http://host/dir%20name/subdir%20name/");
        CHECK(LLURI::buildHTTP("host", "/dir name/subdir name").asString() ==
                      "http://host/dir%20name/subdir%20name");
        CHECK(LLURI::buildHTTP("host", "/dir name/subdir name/").asString() ==
                      "http://host/dir%20name/subdir%20name/");
        CHECK(LLURI::buildHTTP("host", "//dir name//subdir name//").asString() ==
                      "http://host/dir%20name/subdir%20name/");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: buildHTTP takes path parts with unescaped special characters", "[LLURI]")
    {
        LLSD path;
        path.append("x@*//*$&^");
        path.append("123");
        require_parts(LLURI::buildHTTP("host", path),
            "http", "//host/x@*//*$&^/123", "host", "/x@*//*$&^/123");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: buildHTTP takes a query with unescaped special characters", "[LLURI]")
    {
        LLSD path;
        path.append("x");
        path.append("123");
        LLSD query;
        query["123"] = "?&*#//";
        query["**@&?//"] = "abc";
        require_parts(LLURI::buildHTTP("host", path, query),
            "http", "//host/x/123?**@&?//=abc&123=?&*#//",
            "host", "/x/123", "**@&?//=abc&123=?&*#//");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: buildHTTP takes a host with unescaped special characters", "[LLURI]")
    {
        LLSD path;
        path.append("x");
        path.append("123");
        LLSD query;
        query["123"] = "12";
        query["abcd"] = "abc";
        require_parts(LLURI::buildHTTP("hi123*33--}{:portstuffs", path, query),
            "http", "//hi123*33--}{:portstuffs/x/123?123=12&abcd=abc",
            "hi123*33--}{:portstuffs", "/x/123", "123=12&abcd=abc");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: buildHTTP takes a host_port that is really a URI prefix", "[LLURI]")
    {
        require_parts(LLURI::buildHTTP("http://example.com:8080", LLSD()),
            "http", "//example.com:8080",
            "example.com:8080", "");

        require_parts(LLURI::buildHTTP("http://example.com:8080/", LLSD()),
            "http", "//example.com:8080/",
            "example.com:8080", "/");

        require_parts(LLURI::buildHTTP("http://example.com:8080/a/b", LLSD()),
            "http", "//example.com:8080/a/b",
            "example.com:8080", "/a/b");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: escape percent-encodes all but the allowed characters", "[LLURI]")
    {
        const std::string unreserved =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
            "0123456789"
            "-._~";
        CHECK(LLURI::escape("abcdefg", "abcdef") == "abcdef%67");
        CHECK(LLURI::escape("|/&\\+-_!@", "") == "%7C%2F%26%5C%2B%2D%5F%21%40");
        UNSCOPED_INFO("escaping as query variable");
        CHECK(LLURI::escape("http://10.0.1.4:12032/agent/god/agent-id/map/layer/?resume=http://station3.ll.com:12032/agent/203ad6df-b522-491d-ba48-4e24eb57aeff/send-postcard", unreserved + ":@!$'()*+,=") ==
                      "http:%2F%2F10.0.1.4:12032%2Fagent%2Fgod%2Fagent-id%2Fmap%2Flayer%2F%3Fresume=http:%2F%2Fstation3.ll.com:12032%2Fagent%2F203ad6df-b522-491d-ba48-4e24eb57aeff%2Fsend-postcard");
        // French cedilla (C with squiggle, like in the word Francais) is UTF-8 C3 A7

#if LL_WINDOWS
#pragma warning(disable: 4309)
#endif

        std::string cedilla;
        cedilla.push_back( (char)0xC3 );
        cedilla.push_back( (char)0xA7 );
        CHECK(LLURI::escape( cedilla, unreserved) == "%C3%A7");
    }


    TEST_CASE_METHOD(URITestData, "LLURI: escape and unescape of an empty string are empty", "[LLURI]")
    {
        std::string uri_esc(LLURI::escape(""));
        CHECK(uri_esc.empty());
        std::string uri_raw(LLURI::unescape(""));
        CHECK(uri_raw.empty());
    }

    TEST_CASE_METHOD(URITestData, "LLURI: escape and unescape round-trip URIs", "[LLURI]")
    {
        require_escape_round_trip("http://secondlife.com");
        require_escape_round_trip("http://secondlife.com/url with spaces");
        require_escape_round_trip("http://bad[domain]name.com/");
        require_escape_round_trip("ftp://bill.gates@ms/micro$oft.com/c:\\autoexec.bat");
        require_escape_round_trip("");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: escape by default escapes colons and slashes too", "[LLURI]")
    {
        // yes -- this mangles the url. This is expected behavior
        std::string simple("http://secondlife.com");
        CHECK(LLURI::escape(simple) ==
            "http%3A%2F%2Fsecondlife.com");
        CHECK(LLURI::escape("http://get.secondlife.com/windows viewer") ==
            "http%3A%2F%2Fget.secondlife.com%2Fwindows%20viewer");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: escape and unescape round-trip very long strings", "[LLURI]")
    {
        require_escape_round_trip("Welcome to Second Life.We hope you'll have a richly rewarding experience, filled with creativity, self expression and fun.The goals of the Community Standards are simple: treat each other with respect and without harassment, adhere to local standards as indicated by simulator ratings, and refrain from any hate activity which slurs a real-world individual or real-world community. Behavioral Guidelines - The Big Six");
        require_escape_round_trip(
            "'asset_data':b(12100){'task_id':ucc706f2d-0b68-68f8-11a4-f1043ff35ca0}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444921\n\ttotal_crc\t323\n\ttype\t2\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.368634403\t0.00781063363\t-0.569040775\n\toldpos\t150.117996\t25.8658009\t8.19664001\n\trotation\t-0.06293071806430816650390625\t-0.6995697021484375\t-0.7002241611480712890625\t0.1277817934751510620117188\n\tchildpos\t-0.00499999989\t-0.0359999985\t0.307999998\n\tchildrot\t-0.515492737293243408203125\t-0.46601200103759765625\t0.529055416584014892578125\t0.4870323240756988525390625\n\tscale"
            "\t0.074629\t0.289956\t0.01\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t16\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\tscale_x\t1\n\t\t\tscale_y\t1\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t1\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tf"
            "aces\t6\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204"
            "\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t-1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061088050622956\n\treztime\t1094866329019785\n\tparceltime\t1133568981980596\n\ttax_rate\t1.00084\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tchild\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n{'task_id':u61fa7364-e151-0597-774c-523312dae31b}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffff"
            "ff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444922\n\ttotal_crc\t324\n\ttype\t2\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.367110789\t0.00780026987\t-0.566269755\n\toldpos\t150.115005\t25.8479004\t8.18669987\n\trotation\t0.47332942485809326171875\t-0.380102097988128662109375\t-0.5734078884124755859375\t0.550168216228485107421875\n\tchildpos\t-0.00499999989\t-0.0370000005\t0.305000007\n\tchildrot\t-0.736649334430694580078125\t-0.03042060509324073791503906\t-0.02784589119255542755126953\t0.67501628398895263671875\n\tscale\t0.074629\t0.289956\t0.01\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t"
            "0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t16\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\tscale_x\t1\n\t\t\tscale_y\t1\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t1\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t6\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t"
            "\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tf54a0c32-3cd1-d49a-5b4f-7b792bebc204\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t"
            "\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\tddde1ffc-678b-3cda-1748-513086bdf01b\n\t\tcolors\t0.937255 0.796078 0.494118 1\n\t\tscales\t1\n\t\tscalet\t-1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t0\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061087839248891\n\treztime\t1094866329020800\n\tparceltime\t1133568981981983\n\ttax_rate\t1.00084\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tchild\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n{'task_id':ub8d68643-7dd8-57af-0d24-8790032aed0c}\n{\n\tname\tObject|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreat"
            "or_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444923\n\ttotal_crc\t235\n\ttype\t2\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.120029509\t-0.00284469454\t-0.0302077383\n\toldpos\t150.710999\t25.8584995\t8.19172001\n\trotation\t0.145459949970245361328125\t-0.1646589934825897216796875\t0.659558117389678955078125\t-0.718826770782470703125\n\tchildpos\t0\t-0.182999998\t-0.26699999\n\tchildrot\t0.991444766521453857421875\t3.271923924330621957778931e-05\t-0.0002416197530692443251609802\t0.1305266767740249633789062\n\tscale\t0.0382982\t0.205957\t0.368276\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundra"
            "dius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t32\n\t\t\tbegin\t0.3\n\t\t\tend\t0.65\n\t\t\tscale_x\t1\n\t\t\tscale_y\t0.05\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t0\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t3\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0"
            "\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061087534454174\n\treztime\t1094866329021741\n\tparceltime\t1133568981982889\n\ttax_rate\t1.00326\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tchild\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n{'task_id':ue4b19200-9d33-962f-c8c5-6f"
            "25be3a3fd0}\n{\n\tname\tApotheosis_Immolaine_tail|\n\tpermissions 0\n\t{\n\t\tbase_mask\t7fffffff\n\t\towner_mask\t7fffffff\n\t\tgroup_mask\t00000000\n\t\teveryone_mask\t00000000\n\t\tnext_owner_mask\t7fffffff\n\t\tcreator_id\t13fd9595-a47b-4d64-a5fb-6da645f038e0\n\t\towner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tlast_owner_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\t\tgroup_id\t00000000-0000-0000-0000-000000000000\n\t}\n\tlocal_id\t217444924\n\ttotal_crc\t675\n\ttype\t1\n\ttask_valid\t2\n\ttravel_access\t13\n\tdisplayopts\t2\n\tdisplaytype\tv\n\tpos\t-0.34780401\t-0.00968400016\t-0.260098994\n\toldpos\t0\t0\t0\n\trotation\t0.73164522647857666015625\t-0.67541944980621337890625\t-0.07733880728483200073242188\t0.05022468417882919311523438\n\tvelocity\t0\t0\t0\n\tangvel\t0\t0\t0\n\tscale\t0.0382982\t0.32228\t0.383834\n\tsit_offset\t0\t0\t0\n\tcamera_eye_offset\t0\t0\t0\n\tcamera_at_offset\t0\t0\t0\n\tsit_quat\t0\t0\t0\t1\n\tsit_hint\t0\n\tstate\t160\n\tmaterial\t3\n\tsoundid\t00000"
            "000-0000-0000-0000-000000000000\n\tsoundgain\t0\n\tsoundradius\t0\n\tsoundflags\t0\n\ttextcolor\t0 0 0 1\n\tselected\t0\n\tselector\t00000000-0000-0000-0000-000000000000\n\tusephysics\t0\n\trotate_x\t1\n\trotate_y\t1\n\trotate_z\t1\n\tphantom\t0\n\tremote_script_access_pin\t0\n\tvolume_detect\t0\n\tblock_grabs\t0\n\tdie_at_edge\t0\n\treturn_at_edge\t0\n\ttemporary\t0\n\tsandbox\t0\n\tsandboxhome\t0\t0\t0\n\tshape 0\n\t{\n\t\tpath 0\n\t\t{\n\t\t\tcurve\t32\n\t\t\tbegin\t0.3\n\t\t\tend\t0.65\n\t\t\tscale_x\t1\n\t\t\tscale_y\t0.05\n\t\t\tshear_x\t0\n\t\t\tshear_y\t0\n\t\t\ttwist\t0\n\t\t\ttwist_begin\t0\n\t\t\tradius_offset\t0\n\t\t\ttaper_x\t0\n\t\t\ttaper_y\t0\n\t\t\trevolutions\t1\n\t\t\tskew\t0\n\t\t}\n\t\tprofile 0\n\t\t{\n\t\t\tcurve\t0\n\t\t\tbegin\t0\n\t\t\tend\t1\n\t\t\thollow\t0\n\t\t}\n\t}\n\tfaces\t3\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1"
            ".57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\t{\n\t\timageid\te7150bed-3e3e-c698-eb15-d17b178148af\n\t\tcolors\t0.843137 0.156863 0.156863 1\n\t\tscales\t15\n\t\tscalet\t1\n\t\toffsets\t0\n\t\toffsett\t0\n\t\timagerot\t-1.57084\n\t\tbump\t0\n\t\tfullbright\t0\n\t\tmedia_flags\t0\n\t}\n\tps_next_crc\t1\n\tgpw_bias\t1\n\tip\t0\n\tcomplete\tTRUE\n\tdelay\t50000\n\tnextstart\t0\n\tbirthtime\t1061087463950186\n\treztime\t1094866329022555\n\tparceltime\t1133568981984359\n\tdescription\t(No Description)|\n\ttax_rate\t1.01736\n\tnamevalue\tAttachPt U32 RW S 10\n\tnamevalue\tAttachmentOrientation VEC3 RW DS -3.110088, -0.182018, 1.493795\n\tnamevalue\tAttachmentOffset VEC3 RW DS -0.347804, -0.009684, -0.260099\n\tnamevalue\tAttachItemI"
            "D STRING RW SV 20f36c3a-b44b-9bc7-87f3-018bfdfc8cda\n\tscratchpad\t0\n\t{\n\t\n\t}\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t10\n\t}\n\torig_asset_id\t8747acbc-d391-1e59-69f1-41d06830e6c0\n\torig_item_id\t20f36c3a-b44b-9bc7-87f3-018bfdfc8cda\n\tfrom_task_id\t3c115e51-04f4-523c-9fa6-98aff1034730\n\tcorrect_family_id\t00000000-0000-0000-0000-000000000000\n\thas_rezzed\t0\n\tpre_link_base_mask\t7fffffff\n\tlinked \tlinked\n\tdefault_pay_price\t-2\t1\t5\t10\t20\n}\n");
    }


    TEST_CASE_METHOD(URITestData, "LLURI: a secondlife URI splits with or without an authority", "[LLURI]")
    {
        LLURI u("secondlife:///app/login?first_name=Testert4&last_name=Tester&web_login_key=test");
        // if secondlife is the scheme, LLURI should parse /app/login as path, with no authority
        CHECK(u.scheme() == "secondlife");
        CHECK(u.authority() == "");
        CHECK(u.path() == "/app/login");
        CHECK(u.pathArray()[0].asString() == "app");
        CHECK(u.pathArray()[1].asString() == "login");
        CHECK(u.query() == "first_name=Testert4&last_name=Tester&web_login_key=test");
        CHECK(u.queryMap()["last_name"].asString() == "Tester");

        u = LLURI("secondlife://Da Boom/128/128/128");
        // if secondlife is the scheme, LLURI should parse /128/128/128 as path, with Da Boom as authority
        CHECK(u.scheme() == "secondlife");
        CHECK(u.authority() == "Da Boom");
        CHECK(u.path() == "/128/128/128");
        CHECK(u.pathArray()[0].asString() == "128");
        CHECK(u.pathArray()[1].asString() == "128");
        CHECK(u.pathArray()[2].asString() == "128");
        CHECK(u.query() == "");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: an about: URI has a path and a query", "[LLURI]")
    {
        LLURI u("about:blank?redirect-http-hack=secondlife%3A%2F%2F%2Fapp%2Flogin%3Ffirst_name%3DCallum%26last_name%3DLinden%26location%3Dspecify%26grid%3Dvaak%26region%3D%2FMorris%2F128%2F128%26web_login_key%3Defaa4795-c2aa-4c58-8966-763c27931e78");
        CHECK(u.scheme() == "about");
        CHECK(u.authority() == "");
        CHECK(u.path() == "blank");
        CHECK(u.pathArray()[0].asString() == "blank");
        CHECK(u.query() == "redirect-http-hack=secondlife:///app/login?first_name=Callum&last_name=Linden&location=specify&grid=vaak&region=/Morris/128/128&web_login_key=efaa4795-c2aa-4c58-8966-763c27931e78");
        CHECK(u.queryMap()["redirect-http-hack"].asString() == "secondlife:///app/login?first_name=Callum&last_name=Linden&location=specify&grid=vaak&region=/Morris/128/128&web_login_key=efaa4795-c2aa-4c58-8966-763c27931e78");
    }

    TEST_CASE_METHOD(URITestData, "LLURI: escapePathAndData escapes paths and data and leaves escapes alone", "[LLURI]")
    {
        // Basics scheme:[//authority]path[?query][#fragment]
        CHECK(LLURI::escapePathAndData("dirname?query") ==
            "dirname?query");
        CHECK(LLURI::escapePathAndData("dirname?query=data") ==
            "dirname?query=data");
        CHECK(LLURI::escapePathAndData("host://dirname/subdir name?query#fragment") ==
            "host://dirname/subdir%20name?query#fragment");
        CHECK(LLURI::escapePathAndData("host://dirname/subdir name?query=some@>data#fragment") ==
            "host://dirname/subdir%20name?query=some@%3Edata#fragment");
        CHECK(LLURI::escapePathAndData("host://dir[name/subdir name?query=some[data#fra[gment") ==
            "host://dir[name/subdir%20name?query=some%5Bdata#fra[gment");
        CHECK(LLURI::escapePathAndData("mailto:zero@ll.com") ==
            "mailto:zero@ll.com");
        // pre-escaped
        CHECK(LLURI::escapePathAndData("host://dirname/subdir%20name") ==
            "host://dirname/subdir%20name");

        // data:[<mediatype>][;base64],<data>
        CHECK(LLURI::escapePathAndData("data:,Hello, World!") ==
            "data:,Hello%2C%20World%21");
        CHECK(LLURI::escapePathAndData("data:text/html,<h1>Hello, World!</h1>") ==
            "data:text/html,%3Ch1%3EHello%2C%20World%21%3C%2Fh1%3E");
        // pre-escaped
        CHECK(LLURI::escapePathAndData("data:text/html,%3Ch1%3EHello%2C%20World!</h1>") ==
            "data:text/html,%3Ch1%3EHello%2C%20World%21%3C%2Fh1%3E");
        // assume that base64 does not need escaping
        CHECK(LLURI::escapePathAndData("data:image;base64,SGVs/bG8sIFd/vcmxkIQ%3D%3D!-&*?=") ==
            "data:image;base64,SGVs/bG8sIFd/vcmxkIQ%3D%3D!-&*?=");
    }
}


