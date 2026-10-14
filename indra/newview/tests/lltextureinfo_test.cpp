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
#include "../llviewercontrol.h"
#include "../llvocache.h"

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
// Tests
// -------------------------------------------------------------------------------------------

namespace
{
    // Texture downloads logged to the simulator, which is what getAverages()
    // reports on, as a person would set it, and put back when the case ends,
    // a failed check included. The amount a bundle of statistics is sent at
    // is set past what any case here downloads: sending reaches for the
    // texture fetcher and cache, which this test has none of.
    struct LoggedToSimulator
    {
        LoggedToSimulator()
        {
            if (!gSavedSettings.controlExists("LogTextureDownloadsToSimulator"))
            {
                gSavedSettings.declareBOOL("LogTextureDownloadsToSimulator", false, "Declared In Code", LLControlVariable::PERSIST_NO);
            }
            if (!gSavedSettings.controlExists("TextureLoggingThreshold"))
            {
                gSavedSettings.declareU32("TextureLoggingThreshold", 1, "Declared In Code", LLControlVariable::PERSIST_NO);
            }
            mLoggedWas    = gSavedSettings.getBOOL("LogTextureDownloadsToSimulator");
            mThresholdWas = gSavedSettings.getU32("TextureLoggingThreshold");
            gSavedSettings.setU32("TextureLoggingThreshold", 1000000);
            gSavedSettings.setBOOL("LogTextureDownloadsToSimulator", true);
        }
        ~LoggedToSimulator()
        {
            gSavedSettings.setBOOL("LogTextureDownloadsToSimulator", mLoggedWas);
            gSavedSettings.setU32("TextureLoggingThreshold", mThresholdWas);
        }
        LoggedToSimulator(const LoggedToSimulator&)            = delete;
        LoggedToSimulator& operator=(const LoggedToSimulator&) = delete;

        bool mLoggedWas    = false;
        U32  mThresholdWas = 1;
    };

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
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // Test the LLTextureInfo
    // ---------------------------------------------------------------------------------------

    // Made, set to log, and destroyed: there is nothing to ask of it, and the
    // case passes by returning.
    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: an LLTextureInfo is made with logging on", "[LLTectureInfo]")
    {
        LLTextureInfo tex_info;
        tex_info.setLogging(true);
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

    // What the averages are made of is what was logged to the simulator, in
    // the texture info's own recording, as the texture fetcher keeps it.
    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: getAverages gives the bit rate and bytes and transport", "[LLTectureInfo]")
    {
        LoggedToSimulator logged;
        LLTextureInfo tex_info;
        tex_info.setLogging(true);
        tex_info.startRecording();

        // A kilobyte in a second, in microseconds.
        U64 requestStartTimeOne = 1000000;
        U64Microseconds requestEndTimeOne(2000000);
        S32 requestSizeOne = 1024;
        LLUUID id1("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestStartTime(id1, requestStartTimeOne);
        tex_info.setRequestSize(id1, requestSizeOne);
        tex_info.setRequestType(id1, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        tex_info.setRequestCompleteTimeAndLog(id1, requestEndTimeOne);

        // Two kilobytes in two seconds.
        U64 requestStartTimeTwo = 1000000;
        U64Microseconds requestEndTimeTwo(3000000);
        U32 requestSizeTwo = 2048;
        LLUUID id2("10e65d70-46fd-429f-841a-bf698e9424d4");
        tex_info.setRequestStartTime(id2, requestStartTimeTwo);
        tex_info.setRequestSize(id2, requestSizeTwo);
        tex_info.setRequestType(id2, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        tex_info.setRequestCompleteTimeAndLog(id2, requestEndTimeTwo);

        // 3072 bytes, 24576 bits, over three seconds of downloading: 8192
        // bits a second, which is each download's rate too, so the rate does
        // not hang on how the two are averaged.
        LLSD results = tex_info.getAverages();
        CHECK(results["bits_per_second"].asInteger() == 8192);
        CHECK(results["bytes_downloaded"].asInteger() == 3072);
        CHECK(results["transport"].asString() == std::string("HTTP"));
    }

    TEST_CASE_METHOD(textureinfo_test, "LLTectureInfo: resetTextureStatistics clears the averages", "[LLTectureInfo]")
    {
        LoggedToSimulator logged;
        LLTextureInfo tex_info;
        tex_info.setLogging(true);
        tex_info.startRecording();

        // A kilobyte in a second, in microseconds.
        U64 requestStartTimeOne = 1000000;
        U64Microseconds requestEndTimeOne(2000000);
        S32 requestSizeOne = 1024;
        LLUUID id1("10e65d70-46fd-429f-841a-bf698e9424d3");
        tex_info.setRequestStartTime(id1, requestStartTimeOne);
        tex_info.setRequestSize(id1, requestSizeOne);
        tex_info.setRequestType(id1, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        tex_info.setRequestCompleteTimeAndLog(id1, requestEndTimeOne);

        LLSD before = tex_info.getAverages();
        {
            INFO("something to clear");
            CHECK(before["bits_per_second"].asInteger() == 8192);
            CHECK(before["bytes_downloaded"].asInteger() == 1024);
            CHECK(before["transport"].asString() == std::string("HTTP"));
        }
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

