/**
 * @file llworldmipmap_test.cpp
 * @author Merov Linden
 * @date 2009-02-03
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
#include "../llviewertexture.h"
#include "../llviewercontrol.h"
// Class to test
#include "../llworldmipmap.h"
#include <catch2/catch_test_macros.hpp>

// -------------------------------------------------------------------------------------------
// Stubbing: Declarations required to link and run the class being tested
// Notes:
// * Add here stubbed implementation of the few classes and methods used in the class to be tested
// * Add as little as possible (let the link errors guide you)
// * Do not make any assumption as to how those classes or methods work (i.e. don't copy/paste code)
// * A simulator for a class can be implemented here. Please comment and document thoroughly.

void LLGLTexture::setBoostLevel(S32 ) { }
LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromUrl(const std::string&, FTType, bool, LLGLTexture::EBoostLevel, S8,
                                                                         LLGLint, LLGLenum, const LLUUID& ) { return NULL; }

LLControlGroup::LLControlGroup(const std::string& name) : LLInstanceTracker<LLControlGroup, std::string>(name) { }
LLControlGroup::~LLControlGroup() { }
std::string LLControlGroup::getString(std::string_view) { return std::string("test_url"); }
LLControlGroup gSavedSettings("llworldmipmap");

// End Stubbing
// -------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------------------------
namespace
{
    // Test wrapper declaration
    struct worldmipmap_test
    {
        // Derived test class
        class LLTestWorldMipmap : public LLWorldMipmap
        {
            // Put here stubbs of virtual methods we shouldn't call all the way down
        };
        // Instance to be tested
        LLTestWorldMipmap* mMap;

        // Constructor and destructor of the test wrapper
        worldmipmap_test()
        {
            mMap = new LLTestWorldMipmap;
        }
        ~worldmipmap_test()
        {
            delete mMap;
        }
    };

    // ---------------------------------------------------------------------------------------
    // Test functions
    // Notes:
    // * Test as many as you possibly can without requiring a full blown simulation of everything
    // ---------------------------------------------------------------------------------------
    // Test static methods
    TEST_CASE_METHOD(worldmipmap_test, "LLWorldMipmap: scaleToLevel is MAP_LEVELS at zero and 1 from a tile's size up", "[LLWorldMipmap]")
    {
        S32 level = mMap->scaleToLevel(0.0);
        CHECK(level == LLWorldMipmap::MAP_LEVELS);
        level = mMap->scaleToLevel((F32)LLWorldMipmap::MAP_TILE_SIZE);
        CHECK(level == 1);
        level = mMap->scaleToLevel(10.f * LLWorldMipmap::MAP_TILE_SIZE);
        CHECK(level == 1);
    }
    TEST_CASE_METHOD(worldmipmap_test, "LLWorldMipmap: globalToMipmap gives a position's region grid cell", "[LLWorldMipmap]")
    {
        U32 grid_x, grid_y;
        mMap->globalToMipmap(1000.f*REGION_WIDTH_METERS, 1000.f*REGION_WIDTH_METERS, 1, &grid_x, &grid_y);
        CHECK(grid_x == 1000);
        CHECK(grid_y == 1000);
        mMap->globalToMipmap(0.0, 0.0, LLWorldMipmap::MAP_LEVELS, &grid_x, &grid_y);
        CHECK(grid_x == 0);
        CHECK(grid_y == 0);
    }
    TEST_CASE_METHOD(worldmipmap_test, "LLWorldMipmap: getObjectsTile without loading gives nothing for a tile not loaded", "[LLWorldMipmap]")
    {
        // Loading is not tested: it boosts the texture the texture manager
        // fetches, and this binary's stub fetches none.
        LLPointer<LLViewerFetchedTexture> img = mMap->getObjectsTile(0, 0, 1, false);
        CHECK(img.isNull());
    }
    TEST_CASE_METHOD(worldmipmap_test, "LLWorldMipmap: equalizeBoostLevels does not throw", "[LLWorldMipmap]")
    {
        try
        {
            mMap->equalizeBoostLevels();
        }
        catch (...)
        {
            FAIL("equalizeBoostLevels() threw");
        }
    }
    TEST_CASE_METHOD(worldmipmap_test, "LLWorldMipmap: dropBoostLevels does not throw", "[LLWorldMipmap]")
    {
        try
        {
            mMap->dropBoostLevels();
        }
        catch (...)
        {
            FAIL("dropBoostLevels() threw");
        }
    }
    TEST_CASE_METHOD(worldmipmap_test, "LLWorldMipmap: reset does not throw", "[LLWorldMipmap]")
    {
        try
        {
            mMap->reset();
        }
        catch (...)
        {
            FAIL("reset() threw");
        }
    }
}
