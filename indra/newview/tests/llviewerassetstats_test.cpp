/**
 * @file llviewerassetstats_tut.cpp
 * @date 2010-10-28
 * @brief Test cases for some of newview/llviewerassetstats.cpp
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#include <catch2/catch_test_macros.hpp>
#include <iostream>

#include "../llviewerassetstats.h"
#include "lluuid.h"
#include "llsdutil.h"
#include "llregionhandle.h"
#include "lltracethreadrecorder.h"
#include "../llvoavatar.h"

namespace LLStatViewer
{
    LLTrace::SampleStatHandle<>     FPS_SAMPLE("fpssample");
}

void LLVOAvatar::getNearbyRezzedStats(std::vector<S32>& counts, F32& avg_cloud_time, S32& cloud_avatars, S32& pending_meshes, S32& control_avatars)
{
    counts.resize(3);
    counts[0] = 0;
    counts[1] = 0;
    counts[2] = 1;
    cloud_avatars = 0;
    pending_meshes = 0;
    control_avatars = 0;
}

// static
std::string LLVOAvatar::rezStatusToString(S32 rez_status)
{
    if (rez_status==0) return "cloud";
    if (rez_status==1) return "gray";
    if (rez_status==2) return "textured";
    return "unknown";
}

// static
LLViewerStats::StatsAccumulator& LLViewerStats::PhaseMap::getPhaseStats(const std::string& phase_name)
{
    static LLViewerStats::StatsAccumulator junk;
    return junk;
}

static const char * all_keys[] =
{
    "duration",
    "fps",
    "get_other_http",
    "get_other_udp",
    "get_texture_temp_http",
    "get_texture_temp_udp",
    "get_texture_non_temp_http",
    "get_texture_non_temp_udp",
    "get_wearable_http",
    "get_wearable_udp",
    "get_sound_http",
    "get_sound_udp",
    "get_gesture_http",
    "get_gesture_udp"
};

static const char * resp_keys[] =
{
    "get_other_http",
    "get_other_udp",
    "get_texture_temp_http",
    "get_texture_temp_udp",
    "get_texture_non_temp_http",
    "get_texture_non_temp_udp",
    "get_wearable_http",
    "get_wearable_udp",
    "get_sound_http",
    "get_sound_udp",
    "get_gesture_http",
    "get_gesture_udp"
};

static const char * sub_keys[] =
{
    "dequeued",
    "enqueued",
    "resp_count",
    "resp_max",
    "resp_min",
    "resp_mean"
};

static const char * mmm_resp_keys[] =
{
    "fps"
};

static const char * mmm_sub_keys[] =
{
    "count",
    "max",
    "min",
    "mean"
};

static const LLUUID region1("4e2d81a3-6263-6ffe-ad5c-8ce04bee07e8");
static const LLUUID region2("68762cc8-b68b-4e45-854b-e830734f2d4a");
static const U64 region1_handle(0x0000040000003f00ULL);
static const U64 region2_handle(0x0000030000004200ULL);

#if 0
static bool
is_empty_map(const LLSD & sd)
{
    return sd.isMap() && 0 == sd.size();
}
#endif

#if 0
static bool
is_single_key_map(const LLSD & sd, const std::string & key)
{
    return sd.isMap() && 1 == sd.size() && sd.has(key);
}
#endif

static bool
is_double_key_map(const LLSD & sd, const std::string & key1, const std::string & key2)
{
    return sd.isMap() && 2 == sd.size() && sd.has(key1) && sd.has(key2);
}

#if 0
static bool
is_triple_key_map(const LLSD & sd, const std::string & key1, const std::string & key2, const std::string& key3)
{
    return sd.isMap() && 3 == sd.size() && sd.has(key1) && sd.has(key2) && sd.has(key3);
}
#endif

static bool
is_no_stats_map(const LLSD & sd)
{
    return is_double_key_map(sd, "duration", "regions");
}

static bool
is_single_slot_array(const LLSD & sd, U64 region_handle)
{
    U32 grid_x(0), grid_y(0);
    grid_from_region_handle(region_handle, &grid_x, &grid_y);

    return (sd.isArray() &&
            1 == sd.size() &&
            sd[0].has("grid_x") &&
            sd[0].has("grid_y") &&
            sd[0]["grid_x"].isInteger() &&
            sd[0]["grid_y"].isInteger() &&
            grid_x == sd[0]["grid_x"].asInteger() &&
            grid_y == sd[0]["grid_y"].asInteger());
}

static bool
is_double_slot_array(const LLSD & sd, U64 region_handle1, U64 region_handle2)
{
    U32 grid_x1(0), grid_y1(0);
    U32 grid_x2(0), grid_y2(0);
    grid_from_region_handle(region_handle1, &grid_x1, &grid_y1);
    grid_from_region_handle(region_handle2, &grid_x2, &grid_y2);

    return (sd.isArray() &&
            2 == sd.size() &&
            sd[0].has("grid_x") &&
            sd[0].has("grid_y") &&
            sd[0]["grid_x"].isInteger() &&
            sd[0]["grid_y"].isInteger() &&
            sd[1].has("grid_x") &&
            sd[1].has("grid_y") &&
            sd[1]["grid_x"].isInteger() &&
            sd[1]["grid_y"].isInteger() &&
            ((grid_x1 == sd[0]["grid_x"].asInteger() &&
              grid_y1 == sd[0]["grid_y"].asInteger() &&
              grid_x2 == sd[1]["grid_x"].asInteger() &&
              grid_y2 == sd[1]["grid_y"].asInteger()) ||
             (grid_x1 == sd[1]["grid_x"].asInteger() &&
              grid_y1 == sd[1]["grid_y"].asInteger() &&
              grid_x2 == sd[0]["grid_x"].asInteger() &&
              grid_y2 == sd[0]["grid_y"].asInteger())));
}

static LLSD
get_region(const LLSD & sd, U64 region_handle1)
{
    U32 grid_x(0), grid_y(0);
    grid_from_region_handle(region_handle1, &grid_x, &grid_y);

    for (LLSD::array_const_iterator it(sd["regions"].beginArray());
         sd["regions"].endArray() != it;
         ++it)
    {
        if ((*it).has("grid_x") &&
            (*it).has("grid_y") &&
            (*it)["grid_x"].isInteger() &&
            (*it)["grid_y"].isInteger() &&
            (*it)["grid_x"].asInteger() == grid_x &&
            (*it)["grid_y"].asInteger() == grid_y)
        {
            return *it;
        }
    }
    return LLSD();
}

namespace
{
        // The fixture's ThreadRecorder makes itself this thread's recorder,
        // with its own current accumulators, and leaves all of them null
        // when it goes; the fixture makes it the master recorder too. This
        // takes the runner's as they were before the test's recorder, and
        // puts them back after it.
        struct RecorderRestore
        {
            template <typename T>
            using Current = LLThreadLocalSingletonPointer<T>;

            LLTrace::ThreadRecorder*    mMaster{ LLTrace::get_master_thread_recorder() };
            LLTrace::ThreadRecorder*    mRecorder{ LLTrace::get_thread_recorder() };
            LLTrace::CountAccumulator*  mCounts{ Current<LLTrace::CountAccumulator>::getInstance() };
            LLTrace::SampleAccumulator* mSamples{ Current<LLTrace::SampleAccumulator>::getInstance() };
            LLTrace::EventAccumulator*  mEvents{ Current<LLTrace::EventAccumulator>::getInstance() };

            ~RecorderRestore()
            {
                LLTrace::set_master_thread_recorder(mMaster);
                LLTrace::set_thread_recorder(mRecorder);
                Current<LLTrace::CountAccumulator>::setInstance(mCounts);
                Current<LLTrace::SampleAccumulator>::setInstance(mSamples);
                Current<LLTrace::EventAccumulator>::setInstance(mEvents);
            }
        };

    struct tst_viewerassetstats_index
    {
        tst_viewerassetstats_index()
        {
            LLTrace::set_master_thread_recorder(&mThreadRecorder);
        }

        ~tst_viewerassetstats_index()
        {
            // The global stats a case made, where a failed REQUIRE stopped it
            // before it deleted them, are not left for the cases after it;
            // deleted while the case's recorder is still there.
            delete gViewerAssetStats;
            gViewerAssetStats = nullptr;
            LLTrace::set_master_thread_recorder(NULL);
        }

        // before mThreadRecorder, so it is put back after mThreadRecorder is gone
        RecorderRestore         mRestore;
        LLTrace::ThreadRecorder mThreadRecorder;
    };

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: the free functions run without global stats allocated", "[tst_viewerassetstats_test]")
    {
        // Check that helpers aren't bothered by missing global stats
        CHECK(gViewerAssetStats == nullptr);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE, false, false);

        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE, false, false);

        LLViewerAssetStatsFF::record_response(LLViewerAssetType::AT_GESTURE, false, false, (U64Microseconds)12300000ULL);
    }

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: a non-global instance's LLSD has every key", "[tst_viewerassetstats_test]")
    {
        CHECK(gViewerAssetStats == nullptr);

        LLViewerAssetStats * it = new LLViewerAssetStats();

        UNSCOPED_INFO("making an instance does not make it the global");
        CHECK(gViewerAssetStats == nullptr);

        LLSD sd_full = it->asLLSD(false);

        // Default (NULL) region ID doesn't produce LLSD results so should
        // get an empty map back from output
        UNSCOPED_INFO("Stat-less LLSD initially");
        CHECK(is_no_stats_map(sd_full));

        // Once the region is set, we will get a response even with no data collection
        it->setRegion(region1_handle);
        sd_full = it->asLLSD(false);
        CHECK(is_double_key_map(sd_full, "duration", "regions"));
        CHECK(is_single_slot_array(sd_full["regions"], region1_handle));

        LLSD sd = sd_full["regions"][0];

        delete it;

        // Check the structure of the LLSD
        for (int i = 0; i < LL_ARRAY_SIZE(all_keys); ++i)
        {
            CAPTURE(all_keys[i]);
            REQUIRE(sd.has(all_keys[i]));
        }

        for (int i = 0; i < LL_ARRAY_SIZE(resp_keys); ++i)
        {
            for (int j = 0; j < LL_ARRAY_SIZE(sub_keys); ++j)
            {
                CAPTURE(resp_keys[i], sub_keys[j]);
                REQUIRE(sd[resp_keys[i]].has(sub_keys[j]));
            }
        }

        for (int i = 0; i < LL_ARRAY_SIZE(mmm_resp_keys); ++i)
        {
            for (int j = 0; j < LL_ARRAY_SIZE(mmm_sub_keys); ++j)
            {
                CAPTURE(mmm_resp_keys[i], mmm_sub_keys[j]);
                REQUIRE(sd[mmm_resp_keys[i]].has(mmm_sub_keys[j]));
            }
        }
    }

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: a new non-global instance's counts and response times start at zero", "[tst_viewerassetstats_test]")
    {
        LLViewerAssetStats * it = new LLViewerAssetStats();
        it->setRegion(region1_handle);

        LLSD sd = it->asLLSD(false);
        CHECK(is_double_key_map(sd, "regions", "duration"));
        CHECK(is_single_slot_array(sd["regions"], region1_handle));
        sd = sd["regions"][0];

        delete it;

        // Check a few points on the tree for content
        CHECK(sd["get_texture_temp_http"]["dequeued"].asInteger() == 0);
        CHECK(sd["get_sound_udp"]["resp_min"].asReal() == 0.0);
    }

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: the free functions record into a global instance and reset clears it", "[tst_viewerassetstats_test]")
    {
        gViewerAssetStats = new LLViewerAssetStats();
        LLViewerAssetStatsFF::set_region(region1_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE, false, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, false, false);

        LLSD sd = gViewerAssetStats->asLLSD(false);
        CHECK(is_double_key_map(sd, "regions", "duration"));
        CHECK(is_single_slot_array(sd["regions"], region1_handle));
        sd = sd["regions"][0];

        // Check a few points on the tree for content
        CHECK(sd["get_texture_non_temp_udp"]["enqueued"].asInteger() == 1);
        CHECK(sd["get_texture_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd["get_texture_non_temp_http"]["enqueued"].asInteger() == 0);
        CHECK(sd["get_texture_temp_http"]["enqueued"].asInteger() == 0);
        CHECK(sd["get_gesture_udp"]["dequeued"].asInteger() == 0);

        // Reset and check zeros...
        // Reset leaves current region in place
        gViewerAssetStats->reset();
        sd = gViewerAssetStats->asLLSD(false);
        CHECK(is_single_slot_array(sd["regions"], region1_handle));
        sd = sd["regions"][0];

        delete gViewerAssetStats;
        gViewerAssetStats = NULL;

        CHECK(sd["get_texture_non_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd["get_gesture_udp"]["dequeued"].asInteger() == 0);
    }

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: stats are collected per region across two regions", "[tst_viewerassetstats_test]")
    {
        gViewerAssetStats = new LLViewerAssetStats();

        LLViewerAssetStatsFF::set_region(region1_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE, false, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, false, false);

        LLViewerAssetStatsFF::set_region(region2_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);

        LLSD sd = gViewerAssetStats->asLLSD(false);

        // std::cout << sd << std::endl;

        CHECK(is_double_key_map(sd, "duration", "regions"));
        CHECK(is_double_slot_array(sd["regions"], region1_handle, region2_handle));
        LLSD sd1 = get_region(sd, region1_handle);
        LLSD sd2 = get_region(sd, region2_handle);
        UNSCOPED_INFO("Region1 is present in results");
        REQUIRE(sd1.isMap());
        UNSCOPED_INFO("Region2 is present in results");
        REQUIRE(sd2.isMap());

        // Check a few points on the tree for content
        CHECK(sd1["get_texture_non_temp_udp"]["enqueued"].asInteger() == 1);
        CHECK(sd1["get_texture_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd1["get_texture_non_temp_http"]["enqueued"].asInteger() == 0);
        CHECK(sd1["get_texture_temp_http"]["enqueued"].asInteger() == 0);
        CHECK(sd1["get_gesture_udp"]["dequeued"].asInteger() == 0);

        // Check a few points on the tree for content
        CHECK(sd2["get_gesture_udp"]["enqueued"].asInteger() == 4);
        CHECK(sd2["get_gesture_udp"]["dequeued"].asInteger() == 0);
        CHECK(sd2["get_texture_non_temp_udp"]["enqueued"].asInteger() == 0);

        // Reset and check zeros...
        // Reset leaves current region in place
        gViewerAssetStats->reset();
        sd = gViewerAssetStats->asLLSD(false);
        CHECK(is_double_key_map(sd, "regions", "duration"));
        CHECK(is_single_slot_array(sd["regions"], region2_handle));
        sd2 = sd["regions"][0];

        delete gViewerAssetStats;
        gViewerAssetStats = NULL;

        CHECK(sd2["get_texture_non_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd2["get_gesture_udp"]["enqueued"].asInteger() == 0);
    }

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: stats per region add up across moves back and forth between regions", "[tst_viewerassetstats_test]")
    {
        gViewerAssetStats = new LLViewerAssetStats();

        LLViewerAssetStatsFF::set_region(region1_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE, false, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, false, false);

        LLViewerAssetStatsFF::set_region(region2_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);

        LLViewerAssetStatsFF::set_region(region1_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE, true, true);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE, true, true);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, false, false);

        LLViewerAssetStatsFF::set_region(region2_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);
        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_GESTURE, false, false);

        LLSD sd = gViewerAssetStats->asLLSD(false);

        CHECK(is_double_key_map(sd, "duration", "regions"));
        CHECK(is_double_slot_array(sd["regions"], region1_handle, region2_handle));
        LLSD sd1 = get_region(sd, region1_handle);
        LLSD sd2 = get_region(sd, region2_handle);
        UNSCOPED_INFO("Region1 is present in results");
        REQUIRE(sd1.isMap());
        UNSCOPED_INFO("Region2 is present in results");
        REQUIRE(sd2.isMap());

        // Check a few points on the tree for content
        CHECK(sd1["get_texture_non_temp_udp"]["enqueued"].asInteger() == 1);
        CHECK(sd1["get_texture_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd1["get_texture_non_temp_http"]["enqueued"].asInteger() == 0);
        CHECK(sd1["get_texture_temp_http"]["enqueued"].asInteger() == 1);
        CHECK(sd1["get_gesture_udp"]["dequeued"].asInteger() == 0);

        // Check a few points on the tree for content
        CHECK(sd2["get_gesture_udp"]["enqueued"].asInteger() == 8);
        CHECK(sd2["get_gesture_udp"]["dequeued"].asInteger() == 0);
        CHECK(sd2["get_texture_non_temp_udp"]["enqueued"].asInteger() == 0);

        // Reset and check zeros...
        // Reset leaves current region in place
        gViewerAssetStats->reset();
        sd = gViewerAssetStats->asLLSD(false);
        CHECK(is_double_key_map(sd, "duration", "regions"));
        CHECK(is_single_slot_array(sd["regions"], region2_handle));
        sd2 = get_region(sd, region2_handle);
        UNSCOPED_INFO("Region2 is present in results");
        REQUIRE(sd2.isMap());

        delete gViewerAssetStats;
        gViewerAssetStats = NULL;

        CHECK(sd2["get_texture_non_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd2["get_gesture_udp"]["enqueued"].asInteger() == 0);
    }

    TEST_CASE_METHOD(tst_viewerassetstats_index, "tst_viewerassetstats_test: non-texture assets ignore the transport and persistence flags", "[tst_viewerassetstats_test]")
    {
        gViewerAssetStats = new LLViewerAssetStats();
        LLViewerAssetStatsFF::set_region(region1_handle);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE, false, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, false, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, false, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, false, true);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, false, true);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, true, false);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, true, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_BODYPART, true, true);
        LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_BODYPART, true, true);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_LSL_BYTECODE, false, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_LSL_BYTECODE, false, true);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_LSL_BYTECODE, true, false);

        LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_LSL_BYTECODE, true, true);

        LLSD sd = gViewerAssetStats->asLLSD(false);
        CHECK(is_double_key_map(sd, "regions", "duration"));
        CHECK(is_single_slot_array(sd["regions"], region1_handle));
        sd = get_region(sd, region1_handle);
        UNSCOPED_INFO("Region1 is present in results");
        REQUIRE(sd.isMap());

        // Check a few points on the tree for content
        CHECK(sd["get_gesture_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd["get_gesture_udp"]["dequeued"].asInteger() == 0);

        CHECK(sd["get_wearable_http"]["enqueued"].asInteger() == 2);
        CHECK(sd["get_wearable_http"]["dequeued"].asInteger() == 2);

        CHECK(sd["get_wearable_udp"]["enqueued"].asInteger() == 2);
        CHECK(sd["get_wearable_udp"]["dequeued"].asInteger() == 2);

        CHECK(sd["get_other_http"]["enqueued"].asInteger() == 2);
        CHECK(sd["get_other_http"]["dequeued"].asInteger() == 0);

        CHECK(sd["get_other_udp"]["enqueued"].asInteger() == 2);
        CHECK(sd["get_other_udp"]["dequeued"].asInteger() == 0);

        // Reset and check zeros...
        // Reset leaves current region in place
        gViewerAssetStats->reset();
        sd = get_region(gViewerAssetStats->asLLSD(false), region1_handle);
        UNSCOPED_INFO("Region1 is present in results");
        REQUIRE(sd.isMap());

        delete gViewerAssetStats;
        gViewerAssetStats = NULL;

        CHECK(sd["get_texture_non_temp_udp"]["enqueued"].asInteger() == 0);
        CHECK(sd["get_gesture_udp"]["dequeued"].asInteger() == 0);
    }
}
