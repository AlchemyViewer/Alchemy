/**
 * @file llworldmap_test.cpp
 * @author Merov Linden
 * @date 2009-03-09
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

// Dependencies
#include "linden_common.h"
#include "llapr.h"
#include "llsingleton.h"
#include "lltrans.h"
#include "lluistring.h"
#include "../llviewertexture.h"
#include "../llviewercontrol.h"
#include "../llworldmapmessage.h"
// Class to test
#include "../llworldmap.h"
#include <catch2/catch_test_macros.hpp>

// -------------------------------------------------------------------------------------------
// Stubbing: Declarations required to link and run the class being tested
// Notes:
// * Add here stubbed implementation of the few classes and methods used in the class to be tested
// * Add as little as possible (let the link errors guide you)
// * Do not make any assumption as to how those classes or methods work (i.e. don't copy/paste code)
// * A simulator for a class can be implemented here. Please comment and document thoroughly.

// Stub image calls
void LLGLTexture::setBoostLevel(S32 ) { }
LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTexture(const LLUUID&, FTType, bool, LLGLTexture::EBoostLevel, S8,
                                                                  LLGLint, LLGLenum, LLHost ) { return NULL; }

// Stub related map calls

namespace
{
// Records each sendMapBlockRequest call for inspection in tests
struct MapBlockRequest
{
    U16 min_x, min_y, max_x, max_y;
};
std::vector<MapBlockRequest> gMapBlockRequests;
} // anonymous namespace

LLWorldMapMessage::LLWorldMapMessage() { }
LLWorldMapMessage::~LLWorldMapMessage() { }
void LLWorldMapMessage::sendItemRequest(U32 type, U64 handle) { }
void LLWorldMapMessage::sendMapBlockRequest(U16 min_x, U16 min_y, U16 max_x, U16 max_y, bool return_nonexistent)
{
    MapBlockRequest req = { min_x, min_y, max_x, max_y };
    gMapBlockRequests.push_back(req);
}

LLWorldMipmap::LLWorldMipmap() { }
LLWorldMipmap::~LLWorldMipmap() { }
void LLWorldMipmap::reset() { }
void LLWorldMipmap::dropBoostLevels() { }
void LLWorldMipmap::equalizeBoostLevels() { }
LLPointer<LLViewerFetchedTexture> LLWorldMipmap::getObjectsTile(U32 grid_x, U32 grid_y, S32 level, bool load) { return NULL; }

// Stub other stuff
std::string LLTrans::getString(std::string_view, const LLStringUtil::format_map_t&, bool def_string) { return std::string("test_trans"); }
void LLUIString::updateResult() const { }
void LLUIString::setArg(const std::string& , const std::string& ) { }
void LLUIString::assign(std::string_view ) { }

LLControlGroup::LLControlGroup(const std::string& name) : LLInstanceTracker<LLControlGroup, std::string>(name) {}
LLControlGroup::~LLControlGroup() {}
bool LLControlGroup::getBOOL(std::string_view) { return true; }
LLControlGroup gSavedSettings("llworldmap");

// End Stubbing
// -------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------------------------

const F32 X_WORLD_TEST = 1000.0f * REGION_WIDTH_METERS;
const F32 Y_WORLD_TEST = 2000.0f * REGION_WIDTH_METERS;
const F32 Z_WORLD_TEST = 240.0f;
const std::string ITEM_NAME_TEST = "Item Foo";
const std::string TOOLTIP_TEST = "Tooltip Foo";

const std::string SIM_NAME_TEST = "Sim Foo";

namespace
{
    // Test wrapper declarations
    struct iteminfo_test
    {
        // Instance to be tested
        LLItemInfo* mItem;

        // Constructor and destructor of the test wrapper
        iteminfo_test()
        {
            LLUUID id;
            mItem = new LLItemInfo(X_WORLD_TEST, Y_WORLD_TEST, ITEM_NAME_TEST, id);
        }
        ~iteminfo_test()
        {
            delete mItem;
        }
    };

    struct siminfo_test
    {
        // Instance to be tested
        LLSimInfo* mSim;

        // Constructor and destructor of the test wrapper
        siminfo_test()
        {
            U64 handle = to_region_handle_global(X_WORLD_TEST, Y_WORLD_TEST);
            mSim = new LLSimInfo(handle);
        }
        ~siminfo_test()
        {
            delete mSim;
        }
    };

    struct worldmap_test
    {
        // Instance to be tested
        LLWorldMap* mWorld;

        // Constructor and destructor of the test wrapper
        worldmap_test()
        {
            mWorld = LLWorldMap::getInstance();
        }
        ~worldmap_test()
        {
            mWorld = NULL;
        }
    };

    // ---------------------------------------------------------------------------------------
    // Test functions
    // Notes:
    // * Test as many as you possibly can without requiring a full blown simulation of everything
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // Test the LLItemInfo interface
    // ---------------------------------------------------------------------------------------
    TEST_CASE_METHOD(iteminfo_test, "LLItemInfo: each accessor returns what the item was made or set with", "[LLItemInfo]")
    {
        // Test 1 : setCount() / getCount()
        mItem->setCount(10);
        CHECK(mItem->getCount() == 10);
        // Test 2 : setTooltip() / getToolTip()
        std::string tooltip = TOOLTIP_TEST;
        mItem->setTooltip(tooltip);
        CHECK(mItem->getToolTip() == TOOLTIP_TEST);
        // Test 3 : setElevation() / getGlobalPosition()
        mItem->setElevation(Z_WORLD_TEST);
        LLVector3d pos = mItem->getGlobalPosition();
        LLVector3d ref(X_WORLD_TEST, Y_WORLD_TEST, Z_WORLD_TEST);
        CHECK(pos == ref);
        // Test 4 : getName()
        std::string name = mItem->getName();
        CHECK(name == ITEM_NAME_TEST);
        // Test 5 : isName()
        CHECK(mItem->isName(name));
        // Test 6 : getUUID()
        LLUUID id;
        CHECK(mItem->getUUID() == id);
        // Test 7 : getRegionHandle()
        U64 handle = to_region_handle_global(X_WORLD_TEST, Y_WORLD_TEST);
        CHECK(mItem->getRegionHandle() == handle);
    }
    // ---------------------------------------------------------------------------------------
    // Test the LLSimInfo interface
    // ---------------------------------------------------------------------------------------
    // Test Setters and Accessors methods
    TEST_CASE_METHOD(siminfo_test, "LLSimInfo: each accessor returns what was set and image calls do not throw", "[LLSimInfo]")
    {
        // Test 1 : setName() / getName()
        std::string name = SIM_NAME_TEST;
        mSim->setName(name);
        CHECK(mSim->getName() == SIM_NAME_TEST);
        // Test 2 : isName()
        CHECK(mSim->isName(name));
        // Test 3 : getGlobalPos()
        LLVector3 local;
        LLVector3d ref(X_WORLD_TEST, Y_WORLD_TEST, 0.0f);
        LLVector3d pos = mSim->getGlobalPos(local);
        CHECK(pos == ref);
        // Test 4 : getGlobalOrigin()
        pos = mSim->getGlobalOrigin();
        CHECK(pos == ref);
        // Test 5 : clearImage()
        try {
            mSim->clearImage();
        } catch (...) {
            FAIL("LLSimInfo::clearImage() threw");
        }
        // Test 6 : dropImagePriority()
        try {
            mSim->dropImagePriority();
        } catch (...) {
            FAIL("LLSimInfo::dropImagePriority() threw");
        }
        // Test 7 : updateAgentCount()
        try {
            mSim->updateAgentCount(0.0f);
        } catch (...) {
            FAIL("LLSimInfo::updateAgentCount() threw");
        }
        // Test 8 : getAgentCount()
        S32 agents = mSim->getAgentCount();
        CHECK(agents == 0);
        // Test 9 : setLandForSaleImage() / getLandForSaleImage()
        LLUUID id;
        mSim->setLandForSaleImage(id);
        LLPointer<LLViewerFetchedTexture> image = mSim->getLandForSaleImage();
        REQUIRE(image.isNull());
        // Test 10 : isPG()
        mSim->setAccess(SIM_ACCESS_PG);
        CHECK(mSim->isPG());
        // Test 11 : isDown()
        mSim->setAccess(SIM_ACCESS_DOWN);
        CHECK(mSim->isDown());
        // Test 12 : Access strings can't be accessed from unit test...
        //ensure("LLSimInfo::getAccessString() test failed", mSim->getAccessString() == "Offline");
        // Test 13 : Region strings can't be accessed from unit test...
        //mSim->setRegionFlags(REGION_FLAGS_SANDBOX);
        //ensure("LLSimInfo::setRegionFlags() test failed", mSim->getFlagsString() == "Sandbox");
    }
    // Test management of LLInfoItem lists
    TEST_CASE_METHOD(siminfo_test, "LLSimInfo: clearItems empties every item list but agent locations", "[LLSimInfo]")
    {
        // Test 14 : clearItems()
        try {
            mSim->clearItems();
        } catch (...) {
            FAIL("LLSimInfo::clearItems() threw at init");
        }

        // Test 15 : Verify that all the lists are empty
        LLSimInfo::item_info_list_t list;
        list = mSim->getTeleHub();
        UNSCOPED_INFO("telehubs");
        REQUIRE(list.empty());
        list = mSim->getInfoHub();
        UNSCOPED_INFO("infohubs");
        REQUIRE(list.empty());
        list = mSim->getPGEvent();
        UNSCOPED_INFO("PG events");
        REQUIRE(list.empty());
        list = mSim->getMatureEvent();
        UNSCOPED_INFO("mature events");
        REQUIRE(list.empty());
        list = mSim->getLandForSale();
        UNSCOPED_INFO("land for sale");
        REQUIRE(list.empty());
        list = mSim->getAgentLocation();
        UNSCOPED_INFO("agent locations");
        REQUIRE(list.empty());

        // Create an item to be inserted
        LLUUID id;
        LLItemInfo item(X_WORLD_TEST, Y_WORLD_TEST, ITEM_NAME_TEST, id);

        // Insert the item in each list
        mSim->insertTeleHub(item);
        mSim->insertInfoHub(item);
        mSim->insertPGEvent(item);
        mSim->insertMatureEvent(item);
        mSim->insertLandForSale(item);
        mSim->insertAgentLocation(item);

        // Test 16 : Verify that the lists contain 1 item each
        list = mSim->getTeleHub();
        UNSCOPED_INFO("telehubs");
        REQUIRE(list.size() == 1);
        list = mSim->getInfoHub();
        UNSCOPED_INFO("infohubs");
        REQUIRE(list.size() == 1);
        list = mSim->getPGEvent();
        UNSCOPED_INFO("PG events");
        REQUIRE(list.size() == 1);
        list = mSim->getMatureEvent();
        UNSCOPED_INFO("mature events");
        REQUIRE(list.size() == 1);
        list = mSim->getLandForSale();
        UNSCOPED_INFO("land for sale");
        REQUIRE(list.size() == 1);
        list = mSim->getAgentLocation();
        UNSCOPED_INFO("agent locations");
        REQUIRE(list.size() == 1);

        // Test 17 : clearItems()
        try {
            mSim->clearItems();
        } catch (...) {
            FAIL("LLSimInfo::clearItems() threw at end");
        }

        // Test 18 : Verify that all the lists are empty again... *except* agent which is persisted!! (on purpose)
        list = mSim->getTeleHub();
        UNSCOPED_INFO("telehubs");
        REQUIRE(list.empty());
        list = mSim->getInfoHub();
        UNSCOPED_INFO("infohubs");
        REQUIRE(list.empty());
        list = mSim->getPGEvent();
        UNSCOPED_INFO("PG events");
        REQUIRE(list.empty());
        list = mSim->getMatureEvent();
        UNSCOPED_INFO("mature events");
        REQUIRE(list.empty());
        list = mSim->getLandForSale();
        UNSCOPED_INFO("land for sale");
        REQUIRE(list.empty());
        list = mSim->getAgentLocation();
        UNSCOPED_INFO("agent locations, which clearItems() keeps");
        REQUIRE(list.size() == 1);
    }

    // ---------------------------------------------------------------------------------------
    // Test the LLWorldMap interface
    // ---------------------------------------------------------------------------------------
    // Test Setters and Accessors methods
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: the image and region calls do not throw", "[LLWorldMap]")
    {
        // Test 1 : reset()
        try {
            mWorld->reset();
        } catch (...) {
            FAIL("LLWorldMap::reset() threw at init");
        }
        // Test 2 : clearImageRefs()
        try {
            mWorld->clearImageRefs();
        } catch (...) {
            FAIL("LLWorldMap::clearImageRefs() threw");
        }
        // Test 3 : dropImagePriorities()
        try {
            mWorld->dropImagePriorities();
        } catch (...) {
            FAIL("LLWorldMap::dropImagePriorities() threw");
        }
        // Test 4 : reloadItems()
        try {
            mWorld->reloadItems(true);
        } catch (...) {
            FAIL("LLWorldMap::reloadItems() threw");
        }
        // Test 5 : updateRegions()
        try {
            mWorld->updateRegions(1000, 1000, 1004, 1004);
        } catch (...) {
            FAIL("LLWorldMap::updateRegions() threw");
        }
        // Test 6 : equalizeBoostLevels()
        try {
            mWorld->equalizeBoostLevels();
        } catch (...) {
            FAIL("LLWorldMap::equalizeBoostLevels() threw");
        }
        // Test 7 : getObjectsTile(), which the mipmap answers: a stub here,
        // whose tile says nothing of the map's.
        try {
            mWorld->getObjectsTile((U32)(X_WORLD_TEST/REGION_WIDTH_METERS), (U32)(Y_WORLD_TEST/REGION_WIDTH_METERS), 1);
        } catch (...) {
            FAIL("LLWorldMap::getObjectsTile() threw");
        }
    }
    // Test management of LLSimInfo lists
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: inserted regions are found by position and name and handle until reset", "[LLWorldMap]")
    {
        // Test 8 : reset()
        try {
            mWorld->reset();
        } catch (...) {
            FAIL("LLWorldMap::reset() threw at init");
        }

        // Test 9 : Verify that all the region list is empty
        LLWorldMap::sim_info_map_t list;
        list = mWorld->getRegionMap();
        REQUIRE(list.empty());

        // Test 10 : Insert a region
        bool success;
        LLUUID id;
        std::string name_sim = SIM_NAME_TEST;
        success = mWorld->insertRegion( U32(X_WORLD_TEST),
                        U32(Y_WORLD_TEST),
                                        name_sim,
                                        id,
                                        SIM_ACCESS_PG,
                                        REGION_FLAGS_SANDBOX);
        list = mWorld->getRegionMap();
        {
            INFO("insertRegion()");
            CHECK(success);
            REQUIRE(list.size() == 1);
        }

        // Test 11 : Insert an item in the same region -> number of regions doesn't increase
        std::string name_item = ITEM_NAME_TEST;
        success = mWorld->insertItem(   U32(X_WORLD_TEST + REGION_WIDTH_METERS/2),
                        U32(Y_WORLD_TEST + REGION_WIDTH_METERS/2),
                                        name_item,
                                        id,
                                        MAP_ITEM_LAND_FOR_SALE,
                                        0, 0);
        list = mWorld->getRegionMap();
        {
            INFO("insertItem() in the same region");
            CHECK(success);
            REQUIRE(list.size() == 1);
        }

        // Test 12 : Insert an item in another region -> number of regions increases
        success = mWorld->insertItem(   U32(X_WORLD_TEST + REGION_WIDTH_METERS*2),
                        U32(Y_WORLD_TEST + REGION_WIDTH_METERS*2),
                                        name_item,
                                        id,
                                        MAP_ITEM_LAND_FOR_SALE,
                                        0, 0);
        list = mWorld->getRegionMap();
        {
            INFO("insertItem() in another region");
            CHECK(success);
            REQUIRE(list.size() == 2);
        }

        // Test 13 : simInfoFromPosGlobal() in region
        LLVector3d pos1(    X_WORLD_TEST + REGION_WIDTH_METERS*2 + REGION_WIDTH_METERS/2,
                            Y_WORLD_TEST + REGION_WIDTH_METERS*2 + REGION_WIDTH_METERS/2,
                            0.0f);
        LLSimInfo* sim;
        sim = mWorld->simInfoFromPosGlobal(pos1);
        UNSCOPED_INFO("simInfoFromPosGlobal() in a region");
        REQUIRE(sim != NULL);

        // Test 14 : simInfoFromPosGlobal() outside region
        LLVector3d pos2(    X_WORLD_TEST + REGION_WIDTH_METERS*4 + REGION_WIDTH_METERS/2,
                            Y_WORLD_TEST + REGION_WIDTH_METERS*4 + REGION_WIDTH_METERS/2,
                            0.0f);
        sim = mWorld->simInfoFromPosGlobal(pos2);
        UNSCOPED_INFO("simInfoFromPosGlobal() outside any region");
        REQUIRE(sim == NULL);

        // Test 15 : simInfoFromName()
        sim = mWorld->simInfoFromName(name_sim);
        UNSCOPED_INFO("simInfoFromName()");
        REQUIRE(sim != NULL);

        // Test 16 : simInfoFromHandle()
        U64 handle = to_region_handle_global(X_WORLD_TEST, Y_WORLD_TEST);
        sim = mWorld->simInfoFromHandle(handle);
        UNSCOPED_INFO("simInfoFromHandle()");
        REQUIRE(sim != NULL);

        // Test 17 : simNameFromPosGlobal()
        LLVector3d pos3(    X_WORLD_TEST + REGION_WIDTH_METERS/2,
                            Y_WORLD_TEST + REGION_WIDTH_METERS/2,
                            0.0f);
        success = mWorld->simNameFromPosGlobal(pos3, name_sim);
        {
            INFO("simNameFromPosGlobal() in a region");
            CHECK(success);
            CHECK(name_sim == SIM_NAME_TEST);
        }

        // Test 18 : reset()
        try {
            mWorld->reset();
        } catch (...) {
            FAIL("LLWorldMap::reset() threw at end");
        }

        // Test 19 : Verify that all the region list is empty
        list = mWorld->getRegionMap();
        REQUIRE(list.empty());
    }
    // Test tracking
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: tracking a position sets its flags and keeps it until cancelled", "[LLWorldMap]")
    {
        // Point to track
        LLVector3d pos( X_WORLD_TEST + REGION_WIDTH_METERS/2, Y_WORLD_TEST + REGION_WIDTH_METERS/2, Z_WORLD_TEST);

        // Test 20 : no tracking
        mWorld->cancelTracking();
        CHECK_FALSE(mWorld->isTracking());

        // Test 21 : set tracking
        mWorld->setTracking(pos);
        REQUIRE(mWorld->isTracking());
        CHECK_FALSE(mWorld->isTrackingValidLocation());

        // Test 22 : set click and commit flags
        mWorld->setTrackingDoubleClick();
        CHECK(mWorld->isTrackingDoubleClick());
        mWorld->setTrackingCommit();
        CHECK(mWorld->isTrackingCommit());

        // Test 23 : in rectangle test
        bool inRect = mWorld->isTrackingInRectangle(    X_WORLD_TEST, Y_WORLD_TEST,
                                                        X_WORLD_TEST + REGION_WIDTH_METERS,
                                                        Y_WORLD_TEST + REGION_WIDTH_METERS);
        UNSCOPED_INFO("inside the rectangle");
        CHECK(inRect);
        inRect = mWorld->isTrackingInRectangle(         X_WORLD_TEST + REGION_WIDTH_METERS,
                                                        Y_WORLD_TEST + REGION_WIDTH_METERS,
                                                        X_WORLD_TEST + 2 * REGION_WIDTH_METERS,
                                                        Y_WORLD_TEST + 2 * REGION_WIDTH_METERS);
        UNSCOPED_INFO("outside the rectangle");
        CHECK_FALSE(inRect);

        // Test 24 : set tracking to valid and invalid
        mWorld->setTrackingValid();
        REQUIRE(mWorld->isTrackingValidLocation());
        CHECK_FALSE(mWorld->isTrackingInvalidLocation());
        mWorld->setTrackingInvalid();
        REQUIRE_FALSE(mWorld->isTrackingValidLocation());
        CHECK(mWorld->isTrackingInvalidLocation());

        // Test 25 : getTrackedPositionGlobal()
        LLVector3d res = mWorld->getTrackedPositionGlobal();
        CHECK(res == pos);

        // Test 26 : reset tracking
        mWorld->cancelTracking();
        CHECK_FALSE(mWorld->isTracking());
    }
    // Test updateRegions() request grouping, size limits, and bounds
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: updateRegions asks in requests of at most 64 regions within bounds", "[LLWorldMap]")
    {
        // Test 27 : 1x1 block (4x4 = 16 regions) - single small request within bounds
        mWorld->reset();
        gMapBlockRequests.clear();
        mWorld->updateRegions(0, 0, 3, 3);
        UNSCOPED_INFO("1x1 block: one request");
        REQUIRE(gMapBlockRequests.size() == 1);
        {
            INFO("1x1 block");
            const MapBlockRequest& req = gMapBlockRequests[0];
            CHECK(req.min_x == 0);
            CHECK(req.min_y == 0);
            CHECK(req.max_x == 3);
            CHECK(req.max_y == 3);
            S32 regions = (req.max_x - req.min_x + 1) * (req.max_y - req.min_y + 1);
            CHECK(regions <= 64);
        }

        // Test 28 : 2x2 blocks (8x8 = 64 regions) - single request at the 64-region limit
        mWorld->reset();
        gMapBlockRequests.clear();
        mWorld->updateRegions(0, 0, 7, 7);
        UNSCOPED_INFO("2x2 blocks: one request");
        REQUIRE(gMapBlockRequests.size() == 1);
        {
            INFO("2x2 blocks");
            const MapBlockRequest& req = gMapBlockRequests[0];
            CHECK(req.min_x == 0);
            CHECK(req.min_y == 0);
            CHECK(req.max_x == 7);
            CHECK(req.max_y == 7);
            S32 regions = (req.max_x - req.min_x + 1) * (req.max_y - req.min_y + 1);
            CHECK(regions <= 64);
        }

        // Test 29 : 4x4 blocks (16x16 = 256 total regions) - must split into multiple requests
        //           each spanning a 4x1 strip (64 regions), none exceeding the server limit
        mWorld->reset();
        gMapBlockRequests.clear();
        mWorld->updateRegions(0, 0, 15, 15);
        UNSCOPED_INFO("4x4 blocks: split into four strips");
        REQUIRE(gMapBlockRequests.size() == 4);
        for (size_t i = 0; i < gMapBlockRequests.size(); ++i)
        {
            CAPTURE(i);
            const MapBlockRequest& req = gMapBlockRequests[i];
            S32 regions = (req.max_x - req.min_x + 1) * (req.max_y - req.min_y + 1);
            REQUIRE(regions <= 64);
            // Requests must stay within the requested area (0..15)
            REQUIRE(req.max_x <= 15);
            REQUIRE(req.max_y <= 15);
        }
        // Each request covers one horizontal strip of 4 blocks (rows 0..3, 4..7, 8..11, 12..15)
        CHECK(gMapBlockRequests[0].min_y == 0);
        CHECK(gMapBlockRequests[0].max_y == 3);
        CHECK(gMapBlockRequests[1].min_y == 4);
        CHECK(gMapBlockRequests[1].max_y == 7);
        CHECK(gMapBlockRequests[2].min_y == 8);
        CHECK(gMapBlockRequests[2].max_y == 11);
        CHECK(gMapBlockRequests[3].min_y == 12);
        CHECK(gMapBlockRequests[3].max_y == 15);
    }
}
