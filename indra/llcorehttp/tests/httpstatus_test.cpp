/**
 * @file httpstatus_test.cpp
 * @brief unit tests for HttpStatus struct
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2013, Linden Research, Inc.
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

#include "httpcommon.h"

#include <curl/curl.h>
#include <curl/multi.h>

using namespace LLCore;

namespace
{

struct HttpStatusTestData
{
    HttpStatusTestData()
        {}
};


TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: success converts to true and errors to false", "[HttpStatus_Tests]")
{
    // auto allocation fine for this
    HttpStatus status;

    status = HttpStatus(HttpStatus::EXT_CURL_EASY, 0);

    // operator! is a function of its own, so each status checks it as well as operator bool
    CHECK(bool(status));
    CHECK_FALSE(!status);

    status = HttpStatus(HttpStatus::EXT_CURL_MULTI, 0);

    CHECK(bool(status));
    CHECK_FALSE(!status);

    status = HttpStatus(HttpStatus::LLCORE, HE_SUCCESS);

    CHECK(bool(status));
    CHECK_FALSE(!status);

    status = HttpStatus(HttpStatus::EXT_CURL_MULTI, -1);

    CHECK_FALSE(bool(status));
    CHECK(!status);

    status = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_BAD_DOWNLOAD_RESUME);

    CHECK_FALSE(bool(status));
    CHECK(!status);
}


// template <> template <>
// void HttpStatusTestObjectType::test<2>()
// {
//  set_test_name("HttpStatus memory structure");
//
//  // Require that an HttpStatus object can be trivially
//  // returned as a function return value in registers.
//  // One should fit in an int on all platforms.
//
//  //ensure(sizeof(HttpStatus) <= sizeof(int));
// }


TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: toString is empty on success and says what an error is", "[HttpStatus_Tests]")
{
    HttpStatus status = HttpStatus(HttpStatus::EXT_CURL_EASY, 0);
    std::string msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK(msg.empty());

    status = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_BAD_FUNCTION_ARGUMENT);
    msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK_FALSE(msg.empty());

    status = HttpStatus(HttpStatus::EXT_CURL_MULTI, CURLM_OUT_OF_MEMORY);
    msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK_FALSE(msg.empty());

    status = HttpStatus(HttpStatus::LLCORE, HE_SHUTTING_DOWN);
    msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK_FALSE(msg.empty());
}


TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: toString says something for an unknown code", "[HttpStatus_Tests]")
{
    HttpStatus status = HttpStatus(HttpStatus::EXT_CURL_EASY, 32726);
    std::string msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK_FALSE(msg.empty());

    status = HttpStatus(HttpStatus::EXT_CURL_MULTI, -470);
    msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK_FALSE(msg.empty());

    status = HttpStatus(HttpStatus::LLCORE, 923);
    msg = status.toString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK_FALSE(msg.empty());
}

TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: distinct statuses compare unequal when both succeed or both fail", "[HttpStatus_Tests]")
{
    // Make certain equality/inequality tests do not pass
    // through the bool conversion.  Distinct successful
    // statuses, and distinct error statuses, should compare unequal.

    HttpStatus status1(HttpStatus::LLCORE, HE_SUCCESS);
    HttpStatus status2(HttpStatus::EXT_CURL_EASY, HE_SUCCESS);
    CHECK(status1 != status2);

    status1 = HttpStatus(HttpStatus::LLCORE, HE_REPLY_ERROR);
    status2 = HttpStatus(HttpStatus::LLCORE, HE_SHUTTING_DOWN);

    CHECK(status1 != status2);
}

TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: an HTTP status carries its own success or failure", "[HttpStatus_Tests]")
{
    HttpStatus status;

    status = HttpStatus(200, HE_SUCCESS);
    std::string msg = status.toString();
    CHECK(msg.empty());
    CHECK(bool(status));

    // Normally a success but application says error
    status = HttpStatus(200, HE_REPLY_ERROR);
    msg = status.toString();
    CHECK_FALSE(msg.empty());
    CHECK_FALSE(bool(status));
    CHECK(status.toULong() > 1UL);             // Biggish number, not a bool-to-ulong

    // Same statuses with distinct success/fail are distinct
    status = HttpStatus(200, HE_SUCCESS);
    HttpStatus status2(200, HE_REPLY_ERROR);
    CHECK(status != status2);

    // Normally an error but application says okay
    status = HttpStatus(406, HE_SUCCESS);
    msg = status.toString();
    CHECK(msg.empty());
    CHECK(bool(status));

    // Different statuses but both successful are distinct
    status = HttpStatus(200, HE_SUCCESS);
    status2 = HttpStatus(201, HE_SUCCESS);
    CHECK(status != status2);

    // Different statuses but both failed are distinct
    status = HttpStatus(200, HE_REPLY_ERROR);
    status2 = HttpStatus(201, HE_REPLY_ERROR);
    CHECK(status != status2);
}

TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: toString gives the HTTP reason phrase", "[HttpStatus_Tests]")
{
    HttpStatus status(100, HE_REPLY_ERROR);
    std::string msg(status.toString());
    CHECK_FALSE(msg.empty());              // Should be something
    CHECK(msg == "Continue");

    status = HttpStatus(200, HE_SUCCESS);
    msg = status.toString();
    CHECK(msg.empty());                // Success is empty

    status = HttpStatus(199, HE_REPLY_ERROR);
    msg = status.toString();
    CHECK(msg == "Unknown error");

    status = HttpStatus(505, HE_REPLY_ERROR);
    msg = status.toString();
    CHECK(msg == "HTTP Version not supported");

    status = HttpStatus(506, HE_REPLY_ERROR);
    msg = status.toString();
    CHECK(msg == "Unknown error");

    status = HttpStatus(999, HE_REPLY_ERROR);
    msg = status.toString();
    CHECK(msg == "Unknown error");
}


TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: toHex packs the type and the status", "[HttpStatus_Tests]")
{
    HttpStatus status(404);
    std::string msg = status.toHex();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK(msg == "01940001");
}


TEST_CASE_METHOD(HttpStatusTestData, "HttpStatus Tests: toTerseString names the type and the code", "[HttpStatus_Tests]")
{
    HttpStatus status(404);
    std::string msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK(msg == "Http_404");

    status = HttpStatus(200);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    CHECK(msg == "Http_200");

    status = HttpStatus(200, HE_REPLY_ERROR);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    UNSCOPED_INFO("Unsuccessful HTTP 200");
    CHECK(msg == "Http_200");         // No distinction for error

    status = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_COULDNT_CONNECT);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    UNSCOPED_INFO("Easy couldn't connect error");
    CHECK(msg == "Easy_7");

    status = HttpStatus(HttpStatus::EXT_CURL_MULTI, CURLM_OUT_OF_MEMORY);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    UNSCOPED_INFO("Multi out-of-memory error");
    CHECK(msg == "Multi_3");

    status = HttpStatus(HttpStatus::LLCORE, HE_OPT_NOT_SET);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    UNSCOPED_INFO("Core option not set error");
    CHECK(msg == "Core_7");

    status = HttpStatus(22000, 1);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    UNSCOPED_INFO("Undecodable error");
    CHECK(msg == "Unknown_1");

    status = HttpStatus(22000, -1);
    msg = status.toTerseString();
    // std::cout << "Result:  " << msg << std::endl;
    UNSCOPED_INFO("Undecodable error 65535");
    CHECK(msg == "Unknown_65535");
}

} // anonymous namespace
