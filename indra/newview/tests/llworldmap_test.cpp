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
// Tut header
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
// TUT
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
    // * The tests are executed in sequence so the test instance state may change between calls
    // * Remember that you cannot test private methods with tut
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // Test the LLItemInfo interface
    // ---------------------------------------------------------------------------------------
    TEST_CASE_METHOD(iteminfo_test, "LLItemInfo: each accessor returns what the item was made or set with", "[LLItemInfo]")
    {
        // Test 1 : setCount() / getCount()
        mItem->setCount(10);
        UNSCOPED_INFO("LLItemInfo::setCount() test failed");
        CHECK(mItem->getCount() == 10);
        // Test 2 : setTooltip() / getToolTip()
        std::string tooltip = TOOLTIP_TEST;
        mItem->setTooltip(tooltip);
        UNSCOPED_INFO("LLItemInfo::setTooltip() test failed");
        CHECK(mItem->getToolTip() == TOOLTIP_TEST);
        // Test 3 : setElevation() / getGlobalPosition()
        mItem->setElevation(Z_WORLD_TEST);
        LLVector3d pos = mItem->getGlobalPosition();
        LLVector3d ref(X_WORLD_TEST, Y_WORLD_TEST, Z_WORLD_TEST);
        UNSCOPED_INFO("LLItemInfo::getGlobalPosition() test failed");
        CHECK(pos == ref);
        // Test 4 : getName()
        std::string name = mItem->getName();
        UNSCOPED_INFO("LLItemInfo::getName() test failed");
        CHECK(name == ITEM_NAME_TEST);
        // Test 5 : isName()
        UNSCOPED_INFO("LLItemInfo::isName() test failed");
        CHECK(mItem->isName(name));
        // Test 6 : getUUID()
        LLUUID id;
        UNSCOPED_INFO("LLItemInfo::getUUID() test failed");
        CHECK(mItem->getUUID() == id);
        // Test 7 : getRegionHandle()
        U64 handle = to_region_handle_global(X_WORLD_TEST, Y_WORLD_TEST);
        UNSCOPED_INFO("LLItemInfo::getRegionHandle() test failed");
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
        UNSCOPED_INFO("LLSimInfo::setName() test failed");
        CHECK(mSim->getName() == SIM_NAME_TEST);
        // Test 2 : isName()
        UNSCOPED_INFO("LLSimInfo::isName() test failed");
        CHECK(mSim->isName(name));
        // Test 3 : getGlobalPos()
        LLVector3 local;
        LLVector3d ref(X_WORLD_TEST, Y_WORLD_TEST, 0.0f);
        LLVector3d pos = mSim->getGlobalPos(local);
        UNSCOPED_INFO("LLSimInfo::getGlobalPos() test failed");
        CHECK(pos == ref);
        // Test 4 : getGlobalOrigin()
        pos = mSim->getGlobalOrigin();
        UNSCOPED_INFO("LLSimInfo::getGlobalOrigin() test failed");
        CHECK(pos == ref);
        // Test 5 : clearImage()
        try {
            mSim->clearImage();
        } catch (...) {
            FAIL("LLSimInfo::clearImage() test failed");
        }
        // Test 6 : dropImagePriority()
        try {
            mSim->dropImagePriority();
        } catch (...) {
            FAIL("LLSimInfo::dropImagePriority() test failed");
        }
        // Test 7 : updateAgentCount()
        try {
            mSim->updateAgentCount(0.0f);
        } catch (...) {
            FAIL("LLSimInfo::updateAgentCount() test failed");
        }
        // Test 8 : getAgentCount()
        S32 agents = mSim->getAgentCount();
        UNSCOPED_INFO("LLSimInfo::getAgentCount() test failed");
        CHECK(agents == 0);
        // Test 9 : setLandForSaleImage() / getLandForSaleImage()
        LLUUID id;
        mSim->setLandForSaleImage(id);
        LLPointer<LLViewerFetchedTexture> image = mSim->getLandForSaleImage();
        UNSCOPED_INFO("LLSimInfo::getLandForSaleImage() test failed");
        REQUIRE(image.isNull());
        // Test 10 : isPG()
        mSim->setAccess(SIM_ACCESS_PG);
        UNSCOPED_INFO("LLSimInfo::isPG() test failed");
        CHECK(mSim->isPG());
        // Test 11 : isDown()
        mSim->setAccess(SIM_ACCESS_DOWN);
        UNSCOPED_INFO("LLSimInfo::isDown() test failed");
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
            FAIL("LLSimInfo::clearItems() at init test failed");
        }

        // Test 15 : Verify that all the lists are empty
        LLSimInfo::item_info_list_t list;
        list = mSim->getTeleHub();
        UNSCOPED_INFO("LLSimInfo::getTeleHub() empty at init test failed");
        REQUIRE(list.empty());
        list = mSim->getInfoHub();
        UNSCOPED_INFO("LLSimInfo::getInfoHub() empty at init test failed");
        REQUIRE(list.empty());
        list = mSim->getPGEvent();
        UNSCOPED_INFO("LLSimInfo::getPGEvent() empty at init test failed");
        REQUIRE(list.empty());
        list = mSim->getMatureEvent();
        UNSCOPED_INFO("LLSimInfo::getMatureEvent() empty at init test failed");
        REQUIRE(list.empty());
        list = mSim->getLandForSale();
        UNSCOPED_INFO("LLSimInfo::getLandForSale() empty at init test failed");
        REQUIRE(list.empty());
        list = mSim->getAgentLocation();
        UNSCOPED_INFO("LLSimInfo::getAgentLocation() empty at init test failed");
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
        UNSCOPED_INFO("LLSimInfo::insertTeleHub() test failed");
        REQUIRE(list.size() == 1);
        list = mSim->getInfoHub();
        UNSCOPED_INFO("LLSimInfo::insertInfoHub() test failed");
        REQUIRE(list.size() == 1);
        list = mSim->getPGEvent();
        UNSCOPED_INFO("LLSimInfo::insertPGEvent() test failed");
        REQUIRE(list.size() == 1);
        list = mSim->getMatureEvent();
        UNSCOPED_INFO("LLSimInfo::insertMatureEvent() test failed");
        REQUIRE(list.size() == 1);
        list = mSim->getLandForSale();
        UNSCOPED_INFO("LLSimInfo::insertLandForSale() test failed");
        REQUIRE(list.size() == 1);
        list = mSim->getAgentLocation();
        UNSCOPED_INFO("LLSimInfo::insertAgentLocation() test failed");
        REQUIRE(list.size() == 1);

        // Test 17 : clearItems()
        try {
            mSim->clearItems();
        } catch (...) {
            FAIL("LLSimInfo::clearItems() at end test failed");
        }

        // Test 18 : Verify that all the lists are empty again... *except* agent which is persisted!! (on purpose)
        list = mSim->getTeleHub();
        UNSCOPED_INFO("LLSimInfo::getTeleHub() empty after clear test failed");
        REQUIRE(list.empty());
        list = mSim->getInfoHub();
        UNSCOPED_INFO("LLSimInfo::getInfoHub() empty after clear test failed");
        REQUIRE(list.empty());
        list = mSim->getPGEvent();
        UNSCOPED_INFO("LLSimInfo::getPGEvent() empty after clear test failed");
        REQUIRE(list.empty());
        list = mSim->getMatureEvent();
        UNSCOPED_INFO("LLSimInfo::getMatureEvent() empty after clear test failed");
        REQUIRE(list.empty());
        list = mSim->getLandForSale();
        UNSCOPED_INFO("LLSimInfo::getLandForSale() empty after clear test failed");
        REQUIRE(list.empty());
        list = mSim->getAgentLocation();
        UNSCOPED_INFO("LLSimInfo::getAgentLocation() empty after clear test failed");
        REQUIRE(list.size() == 1);
    }

    // ---------------------------------------------------------------------------------------
    // Test the LLWorldMap interface
    // ---------------------------------------------------------------------------------------
    // Test Setters and Accessors methods
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: the image and region calls do not throw and no tile is loaded", "[LLWorldMap]")
    {
        // Test 1 : reset()
        try {
            mWorld->reset();
        } catch (...) {
            FAIL("LLWorldMap::reset() at init test failed");
        }
        // Test 2 : clearImageRefs()
        try {
            mWorld->clearImageRefs();
        } catch (...) {
            FAIL("LLWorldMap::clearImageRefs() test failed");
        }
        // Test 3 : dropImagePriorities()
        try {
            mWorld->dropImagePriorities();
        } catch (...) {
            FAIL("LLWorldMap::dropImagePriorities() test failed");
        }
        // Test 4 : reloadItems()
        try {
            mWorld->reloadItems(true);
        } catch (...) {
            FAIL("LLWorldMap::reloadItems() test failed");
        }
        // Test 5 : updateRegions()
        try {
            mWorld->updateRegions(1000, 1000, 1004, 1004);
        } catch (...) {
            FAIL("LLWorldMap::updateRegions() test failed");
        }
        // Test 6 : equalizeBoostLevels()
        try {
            mWorld->equalizeBoostLevels();
        } catch (...) {
            FAIL("LLWorldMap::equalizeBoostLevels() test failed");
        }
        // Test 7 : getObjectsTile()
        try {
            LLPointer<LLViewerFetchedTexture> image = mWorld->getObjectsTile((U32)(X_WORLD_TEST/REGION_WIDTH_METERS), (U32)(Y_WORLD_TEST/REGION_WIDTH_METERS), 1);
            UNSCOPED_INFO("LLWorldMap::getObjectsTile() failed");
            REQUIRE(image.isNull());
        } catch (...) {
            FAIL("LLWorldMap::getObjectsTile() test failed with exception");
        }
    }
    // Test management of LLSimInfo lists
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: inserted regions are found by position and name and handle until reset", "[LLWorldMap]")
    {
        // Test 8 : reset()
        try {
            mWorld->reset();
        } catch (...) {
            FAIL("LLWorldMap::reset() at init test failed");
        }

        // Test 9 : Verify that all the region list is empty
        LLWorldMap::sim_info_map_t list;
        list = mWorld->getRegionMap();
        UNSCOPED_INFO("LLWorldMap::getRegionMap() empty at init test failed");
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
            INFO("LLWorldMap::insertRegion() failed");
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
            INFO("LLWorldMap::insertItem() in existing region failed");
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
            INFO("LLWorldMap::insertItem() in unexisting region failed");
            CHECK(success);
            REQUIRE(list.size() == 2);
        }

        // Test 13 : simInfoFromPosGlobal() in region
        LLVector3d pos1(    X_WORLD_TEST + REGION_WIDTH_METERS*2 + REGION_WIDTH_METERS/2,
                            Y_WORLD_TEST + REGION_WIDTH_METERS*2 + REGION_WIDTH_METERS/2,
                            0.0f);
        LLSimInfo* sim;
        sim = mWorld->simInfoFromPosGlobal(pos1);
        UNSCOPED_INFO("LLWorldMap::simInfoFromPosGlobal() test on existing region failed");
        REQUIRE(sim != NULL);

        // Test 14 : simInfoFromPosGlobal() outside region
        LLVector3d pos2(    X_WORLD_TEST + REGION_WIDTH_METERS*4 + REGION_WIDTH_METERS/2,
                            Y_WORLD_TEST + REGION_WIDTH_METERS*4 + REGION_WIDTH_METERS/2,
                            0.0f);
        sim = mWorld->simInfoFromPosGlobal(pos2);
        UNSCOPED_INFO("LLWorldMap::simInfoFromPosGlobal() test outside region failed");
        REQUIRE(sim == NULL);

        // Test 15 : simInfoFromName()
        sim = mWorld->simInfoFromName(name_sim);
        UNSCOPED_INFO("LLWorldMap::simInfoFromName() test on existing region failed");
        REQUIRE(sim != NULL);

        // Test 16 : simInfoFromHandle()
        U64 handle = to_region_handle_global(X_WORLD_TEST, Y_WORLD_TEST);
        sim = mWorld->simInfoFromHandle(handle);
        UNSCOPED_INFO("LLWorldMap::simInfoFromHandle() test on existing region failed");
        REQUIRE(sim != NULL);

        // Test 17 : simNameFromPosGlobal()
        LLVector3d pos3(    X_WORLD_TEST + REGION_WIDTH_METERS/2,
                            Y_WORLD_TEST + REGION_WIDTH_METERS/2,
                            0.0f);
        success = mWorld->simNameFromPosGlobal(pos3, name_sim);
        {
            INFO("LLWorldMap::simNameFromPosGlobal() test on existing region failed");
            CHECK(success);
            CHECK(name_sim == SIM_NAME_TEST);
        }

        // Test 18 : reset()
        try {
            mWorld->reset();
        } catch (...) {
            FAIL("LLWorldMap::reset() at end test failed");
        }

        // Test 19 : Verify that all the region list is empty
        list = mWorld->getRegionMap();
        UNSCOPED_INFO("LLWorldMap::getRegionMap() empty at end test failed");
        REQUIRE(list.empty());
    }
    // Test tracking
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: tracking a position sets its flags and keeps it until cancelled", "[LLWorldMap]")
    {
        // Point to track
        LLVector3d pos( X_WORLD_TEST + REGION_WIDTH_METERS/2, Y_WORLD_TEST + REGION_WIDTH_METERS/2, Z_WORLD_TEST);

        // Test 20 : no tracking
        mWorld->cancelTracking();
        UNSCOPED_INFO("LLWorldMap::cancelTracking() at begin test failed");
        CHECK_FALSE(mWorld->isTracking());

        // Test 21 : set tracking
        mWorld->setTracking(pos);
        {
            INFO("LLWorldMap::setTracking() failed");
            REQUIRE(mWorld->isTracking());
            CHECK_FALSE(mWorld->isTrackingValidLocation());
        }

        // Test 22 : set click and commit flags
        mWorld->setTrackingDoubleClick();
        UNSCOPED_INFO("LLWorldMap::setTrackingDoubleClick() failed");
        CHECK(mWorld->isTrackingDoubleClick());
        mWorld->setTrackingCommit();
        UNSCOPED_INFO("LLWorldMap::setTrackingCommit() failed");
        CHECK(mWorld->isTrackingCommit());

        // Test 23 : in rectangle test
        bool inRect = mWorld->isTrackingInRectangle(    X_WORLD_TEST, Y_WORLD_TEST,
                                                        X_WORLD_TEST + REGION_WIDTH_METERS,
                                                        Y_WORLD_TEST + REGION_WIDTH_METERS);
        UNSCOPED_INFO("LLWorldMap::isTrackingInRectangle() in rectangle failed");
        CHECK(inRect);
        inRect = mWorld->isTrackingInRectangle(         X_WORLD_TEST + REGION_WIDTH_METERS,
                                                        Y_WORLD_TEST + REGION_WIDTH_METERS,
                                                        X_WORLD_TEST + 2 * REGION_WIDTH_METERS,
                                                        Y_WORLD_TEST + 2 * REGION_WIDTH_METERS);
        UNSCOPED_INFO("LLWorldMap::isTrackingInRectangle() outside rectangle failed");
        CHECK_FALSE(inRect);

        // Test 24 : set tracking to valid and invalid
        mWorld->setTrackingValid();
        {
            INFO("LLWorldMap::setTrackingValid() failed");
            REQUIRE(mWorld->isTrackingValidLocation());
            CHECK_FALSE(mWorld->isTrackingInvalidLocation());
        }
        mWorld->setTrackingInvalid();
        {
            INFO("LLWorldMap::setTrackingInvalid() failed");
            REQUIRE_FALSE(mWorld->isTrackingValidLocation());
            CHECK(mWorld->isTrackingInvalidLocation());
        }

        // Test 25 : getTrackedPositionGlobal()
        LLVector3d res = mWorld->getTrackedPositionGlobal();
        UNSCOPED_INFO("LLWorldMap::getTrackedPositionGlobal() failed");
        CHECK(res == pos);

        // Test 26 : reset tracking
        mWorld->cancelTracking();
        UNSCOPED_INFO("LLWorldMap::cancelTracking() at end test failed");
        CHECK_FALSE(mWorld->isTracking());
    }
    // Test updateRegions() request grouping, size limits, and bounds
    TEST_CASE_METHOD(worldmap_test, "LLWorldMap: updateRegions asks in requests of at most 64 regions within bounds", "[LLWorldMap]")
    {
        // Test 27 : 1x1 block (4x4 = 16 regions) - single small request within bounds
        mWorld->reset();
        gMapBlockRequests.clear();
        mWorld->updateRegions(0, 0, 3, 3);
        UNSCOPED_INFO("updateRegions 1x1 block: expected 1 request");
        REQUIRE(gMapBlockRequests.size() == 1);
        {
            const MapBlockRequest& req = gMapBlockRequests[0];
            UNSCOPED_INFO("updateRegions 1x1 block: min_x");
            CHECK(req.min_x == 0);
            UNSCOPED_INFO("updateRegions 1x1 block: min_y");
            CHECK(req.min_y == 0);
            UNSCOPED_INFO("updateRegions 1x1 block: max_x");
            CHECK(req.max_x == 3);
            UNSCOPED_INFO("updateRegions 1x1 block: max_y");
            CHECK(req.max_y == 3);
            S32 regions = (req.max_x - req.min_x + 1) * (req.max_y - req.min_y + 1);
            UNSCOPED_INFO("updateRegions 1x1 block: must not exceed 64 regions");
            CHECK(regions <= 64);
        }

        // Test 28 : 2x2 blocks (8x8 = 64 regions) - single request at the 64-region limit
        mWorld->reset();
        gMapBlockRequests.clear();
        mWorld->updateRegions(0, 0, 7, 7);
        UNSCOPED_INFO("updateRegions 2x2 blocks: expected 1 request");
        REQUIRE(gMapBlockRequests.size() == 1);
        {
            const MapBlockRequest& req = gMapBlockRequests[0];
            UNSCOPED_INFO("updateRegions 2x2 blocks: min_x");
            CHECK(req.min_x == 0);
            UNSCOPED_INFO("updateRegions 2x2 blocks: min_y");
            CHECK(req.min_y == 0);
            UNSCOPED_INFO("updateRegions 2x2 blocks: max_x");
            CHECK(req.max_x == 7);
            UNSCOPED_INFO("updateRegions 2x2 blocks: max_y");
            CHECK(req.max_y == 7);
            S32 regions = (req.max_x - req.min_x + 1) * (req.max_y - req.min_y + 1);
            UNSCOPED_INFO("updateRegions 2x2 blocks: must not exceed 64 regions");
            CHECK(regions <= 64);
        }

        // Test 29 : 4x4 blocks (16x16 = 256 total regions) - must split into multiple requests
        //           each spanning a 4x1 strip (64 regions), none exceeding the server limit
        mWorld->reset();
        gMapBlockRequests.clear();
        mWorld->updateRegions(0, 0, 15, 15);
        UNSCOPED_INFO("updateRegions 4x4 blocks: expected 4 requests");
        REQUIRE(gMapBlockRequests.size() == 4);
        for (size_t i = 0; i < gMapBlockRequests.size(); ++i)
        {
            const MapBlockRequest& req = gMapBlockRequests[i];
            S32 regions = (req.max_x - req.min_x + 1) * (req.max_y - req.min_y + 1);
            UNSCOPED_INFO("updateRegions 4x4 blocks: each request must not exceed 64 regions");
            REQUIRE(regions <= 64);
            // Requests must stay within the requested area (0..15)
            UNSCOPED_INFO("updateRegions 4x4 blocks: max_x in bounds");
            REQUIRE(req.max_x <= 15);
            UNSCOPED_INFO("updateRegions 4x4 blocks: max_y in bounds");
            REQUIRE(req.max_y <= 15);
        }
        // Each request covers one horizontal strip of 4 blocks (rows 0..3, 4..7, 8..11, 12..15)
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[0] min_y");
        CHECK(gMapBlockRequests[0].min_y == 0);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[0] max_y");
        CHECK(gMapBlockRequests[0].max_y == 3);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[1] min_y");
        CHECK(gMapBlockRequests[1].min_y == 4);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[1] max_y");
        CHECK(gMapBlockRequests[1].max_y == 7);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[2] min_y");
        CHECK(gMapBlockRequests[2].min_y == 8);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[2] max_y");
        CHECK(gMapBlockRequests[2].max_y == 11);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[3] min_y");
        CHECK(gMapBlockRequests[3].min_y == 12);
        UNSCOPED_INFO("updateRegions 4x4 blocks: request[3] max_y");
        CHECK(gMapBlockRequests[3].max_y == 15);
    }
}
