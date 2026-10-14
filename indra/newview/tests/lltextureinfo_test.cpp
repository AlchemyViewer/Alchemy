/**
 * @file llwtextureinfo_test.cpp
 * @author Si & Gabriel
 * @date 2009-03-30
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

// Precompiled header: almost always required for newview cpp files
#include "../llviewerprecompiledheaders.h"
// Class to test
#include "../lltextureinfo.h"
// Dependencies
#include "../lltextureinfodetails.cpp"

#include "../llagent.h"
#include "../llappviewer.h"
#include "../llmeshrepository.h"
#include "../lltexturecache.h"
#include "../lltexturefetch.h"
#include "../llvocache.h"

// Tut header
#include <catch2/catch_test_macros.hpp>

// -------------------------------------------------------------------------------------------
// Stubbing: Declarations required to link and run the class being tested
// Notes:
// * Add here stubbed implementation of the few classes and methods used in the class to be tested
// * Add as little as possible (let the link errors guide you)
// * Do not make any assumption as to how those classes or methods work (i.e. don't copy/paste code)
// * A simulator for a class can be implemented here. Please comment and document thoroughly.

void send_texture_stats_to_sim(const LLSD& texture_stats) {}

LLAgent gAgent;
LLAgent::LLAgent() : mAgentAccess(nullptr) { }
LLAgent::~LLAgent() { }
LLViewerRegion* LLAgent::getRegion() const { return nullptr; }

LLTextureCache*      LLAppViewer::sTextureCache      = nullptr;
LLTextureFetch*      LLAppViewer::sTextureFetch      = nullptr;

std::atomic<U32> LLMeshRepository::sCacheReads  = 0;
std::atomic<U32> LLMeshRepository::sCacheWrites = 0;

S64 LLTextureCache::sCacheMaxTexturesSize = 0; // no limit

void LLTextureFetch::getStateStats(U32* cache_read, U32* cache_write, U32* res_wait)
{
    *cache_read  = 0;
    *cache_write = 0;
    *res_wait    = 0;
}

LLVOCache::LLVOCache(bool read_only) :
    mInitialized(false),
    mReadOnly(read_only),
    mNumEntries(0),
    mCacheSize(1),
    mEnabled(true)
{}

LLVOCache::~LLVOCache() {}

// End Stubbing
// -------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------
// TUT
// -------------------------------------------------------------------------------------------

namespace
{
    // Test wrapper declarations
    struct textureinfo_test
    {
        // Constructor and destructor of the test wrapper
        textureinfo_test()
        {
        }
        ~textureinfo_test()
        {
        }
    };

    // ---------------------------------------------------------------------------------------
    // Test functions
    // Notes:
    // * Test as many as you possibly can without requiring a full blown simulation of everything
    // * The tests are executed in sequence so the test instance state may change between calls
    // * Remember that you cannot test private methods with tut
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // Test the LLTextureInfo
    // ---------------------------------------------------------------------------------------

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: an LLTextureInfo is made with logging on", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);
        CHECK(true);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: has() is false for a UUID never added", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID nonExistant("3a0efa3b-84dc-4e17-9b8c-79ea028850c1");
        CHECK_FALSE(tex_info.has(nonExistant));
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: a request start time set is read back", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID id("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestStartTime(id, 200);

        CHECK(tex_info.getRequestStartTime(id) == 200);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: an unknown texture's request start time is 0", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID nonExistant("3a0efa3b-84dc-4e17-9b8c-79ea028850c1");
        CHECK(tex_info.getRequestStartTime(nonExistant) == 0);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: an unknown texture's request complete time is 0", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID nonExistant("3a0efa3b-84dc-4e17-9b8c-79ea028850c1");
        CHECK(tex_info.getRequestCompleteTime(nonExistant) == 0);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: a request size set is read back", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID id("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestSize(id, 600);

        CHECK(tex_info.getRequestSize(id) == 600);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: an HTTP request type is recorded", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID id("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestType(id, LLTextureInfoDetails::REQUEST_TYPE_HTTP);

        CHECK(tex_info.getRequestType(id) == LLTextureInfoDetails::REQUEST_TYPE_HTTP);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: a UDP request type is recorded", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID id("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestType(id, LLTextureInfoDetails::REQUEST_TYPE_UDP);

        CHECK(tex_info.getRequestType(id) == LLTextureInfoDetails::REQUEST_TYPE_UDP);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: a request offset set is read back", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        LLUUID id("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestOffset(id, 1234);

        CHECK(tex_info.getRequestOffset(id) == 1234);
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: getAverages gives the bit rate and bytes and transport", "[LLTectureInfo]")
    {
        SKIP("Test skipped due to use of LLTrace stats failing in test");

        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        S32 requestStartTimeOne = 200;
        U64Microseconds requestEndTimeOne(400);
        S32 requestSizeOne = 1024;
        S32 requestSizeOneBits = requestSizeOne * 8;
        LLUUID id1("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestStartTime(id1, requestStartTimeOne);
        tex_info.setRequestSize(id1, requestSizeOne);
        tex_info.setRequestType(id1, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        tex_info.setRequestCompleteTimeAndLog(id1, requestEndTimeOne);

        U32 requestStartTimeTwo = 100;
        U64Microseconds requestEndTimeTwo(500);
        U32 requestSizeTwo = 2048;
        S32 requestSizeTwoBits = requestSizeTwo * 8;
        LLUUID id2("10e65d70-46fd-429f-841a-bf698e9424d4");
        tex_info.setRequestStartTime(id2, requestStartTimeTwo);
        tex_info.setRequestSize(id2, requestSizeTwo);
        tex_info.setRequestType(id2, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        tex_info.setRequestCompleteTimeAndLog(id2, requestEndTimeTwo);

        S32 averageBitRate = ((requestSizeOneBits / (S32(requestEndTimeOne.value()) - requestStartTimeOne)) +
                              (requestSizeTwoBits / (S32(requestEndTimeTwo.value()) - requestStartTimeTwo))) /
                             2;

        S32 totalBytes = requestSizeOne + requestSizeTwo;

        LLSD results = tex_info.getAverages();
        CHECK(results["bits_per_second"].asInteger() == averageBitRate);
        CHECK(results["bytes_downloaded"].asInteger() == totalBytes);
        CHECK(results["transport"].asString() == std::string("HTTP"));
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: resetTextureStatistics clears the averages", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        S32 requestStartTimeOne = 200;
        U64Microseconds requestEndTimeOne(400);
        S32 requestSizeOne = 1024;
        LLUUID id1("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestStartTime(id1, requestStartTimeOne);
        tex_info.setRequestSize(id1, requestSizeOne);
        tex_info.setRequestType(id1, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        tex_info.setRequestCompleteTimeAndLog(id1, requestEndTimeOne);

        tex_info.getAverages();
        tex_info.resetTextureStatistics();
        LLSD results = tex_info.getAverages();
        CHECK(results["bits_per_second"].asInteger() == 0);
        CHECK(results["bytes_downloaded"].asInteger() == 0);
        CHECK(results["transport"].asString() == std::string("NONE"));
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: a request's map entry goes once it completes", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);

        S32 requestStartTimeOne = 200;
        U64Microseconds requestEndTimeOne(400);
        S32 requestSizeOne = 1024;
        LLUUID id1("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestStartTime(id1, requestStartTimeOne);
        tex_info.setRequestSize(id1, requestSizeOne);
        tex_info.setRequestType(id1, LLTextureInfoDetails::REQUEST_TYPE_HTTP);

        CHECK(tex_info.getTextureInfoMapSize() == 1);

        tex_info.setRequestCompleteTimeAndLog(id1, requestEndTimeOne);

        CHECK(tex_info.getTextureInfoMapSize() == 0);
    }
}

