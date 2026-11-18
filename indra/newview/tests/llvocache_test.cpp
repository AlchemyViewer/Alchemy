/**
 * @file llvocache_test.cpp
 * @author Brad
 * @date 2023-03-01
 * @brief Test Viewer Object Cache functionality
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2014, Linden Research, Inc.
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
#include "../llviewerprecompiledheaders.h"
#include <catch2/catch_test_macros.hpp>

#include "../llvocache.h"

#include "lldir.h"
#include "../llhudobject.h"
#include "llregionhandle.h"
#include "llsdutil.h"
#include "llsdserialize.h"

#include "../llviewerobjectlist.h"
#include "../llviewerregion.h"
#include "../llworld.h"

#include "llvieweroctree_stub.cpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

// LLGLTFMaterial, which the override entries build, reaches tinygltf, whose
// single-header implementation the viewer compiles into its GLTF loader. The
// test carries its own copy the same way llgltfmaterial_test does.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_USE_CPP14
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE 1
#include <tiny_gltf.h>

//----------------------------------------------------------------------------
// Mock objects for the dependencies of the code we're testing
S32 LLVOCachePartition::cull(LLCamera &camera, bool do_occlusion) { return 0; }

LLViewerObjectList::LLViewerObjectList() = default;
LLViewerObjectList::~LLViewerObjectList() = default;
LLDebugBeacon::~LLDebugBeacon() = default;
LLViewerObjectList gObjectList{};
void LLViewerObjectList::getUUIDFromLocal(LLUUID&, const U32, const U32, const U32) {}
LLViewerCamera::eCameraID LLViewerCamera::sCurCameraID{};
void LLViewerObject::unpackUUID(LLDataPackerBinaryBuffer *dp, LLUUID &value, std::string name) {}

bool LLViewerRegion::addVisibleGroup(LLViewerOctreeGroup*) { return false; }
U32 LLViewerRegion::getNumOfVisibleGroups() const { return 0; }
LLVector3 LLViewerRegion::getOriginAgent() const { return LLVector3::zero; }
S32 LLViewerRegion::sLastCameraUpdated{};
void LLViewerRegion::clearVOCacheFromMemory() {}
const LLHost& LLViewerRegion::getHost() const { static const LLHost host; return host; }

// No world exists in the test; the cache checks for one before asking it.
LLViewerRegion* LLWorld::getRegionFromHandle(const U64&) { return nullptr; }

// -------------------------------------------------------------------------------------------
// Tests
// -------------------------------------------------------------------------------------------
namespace
{
    // Each test gets a fresh cache singleton with the real LLDir's cache
    // directory pointed at a temporary directory of its own, so tests neither
    // see each other's files nor the user's.
    struct vocacheTest
    {
        static constexpr U32 CACHE_VERSION = 20;          // LLAppViewer::getObjectCacheVersion()
        static constexpr U32 CACHE_NUMBER_OF_REGIONS = 128;

        std::filesystem::path mRoot;

        vocacheTest()
        {
            mRoot = std::filesystem::temp_directory_path() / ("llvocache_test_" + LLUUID::generateNewID().asString());
            std::filesystem::create_directories(mRoot);
            UNSCOPED_INFO("cache dir redirected");
            REQUIRE(gDirUtilp->setCacheDir(mRoot.string()));

            LLVOCache::initParamSingleton(/*read_only=*/false);
            LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);
        }

        ~vocacheTest()
        {
            LLVOCache::deleteSingleton();
            gDirUtilp->setCacheDir("");
            std::error_code ec;
            std::filesystem::remove_all(mRoot, ec);
        }

        // Where the cache put a region's files, derived the way the cache does.
        std::filesystem::path objectFile(U64 handle, const char* suffix) const
        {
            U32 x, y;
            grid_from_region_handle(handle, &x, &y);
            return mRoot / "objectcache" / ("objects_" + std::to_string(x) + "_" + std::to_string(y) + suffix);
        }

        static LLPointer<LLVOCacheEntry> makeEntry(U32 local_id, U32 crc, S32 body_size)
        {
            std::vector<U8> body((size_t)body_size);
            for (S32 i = 0; i < body_size; ++i)
            {
                body[(size_t)i] = (U8)(local_id * 31 + i);
            }
            LLDataPackerBinaryBuffer dp(body.data(), body_size);
            return new LLVOCacheEntry(local_id, crc, dp);
        }

        static LLVOCacheEntry::vocache_entry_map_t makeEntries(U32 count)
        {
            LLVOCacheEntry::vocache_entry_map_t entries;
            for (U32 i = 1; i <= count; ++i)
            {
                LLPointer<LLVOCacheEntry> entry = makeEntry(i, i * 7919u, 1 + (S32)(i % 300));
                if (i % 5 == 0)
                {
                    entry->recordHit();
                }
                entries[i] = entry;
            }
            return entries;
        }

        static void requireSameEntry(const LLVOCacheEntry& expected, const LLVOCacheEntry& actual)
        {
            REQUIRE(actual.getCRC() == expected.getCRC());
            REQUIRE(actual.getHitCount() == expected.getHitCount());
            const LLDataPackerBinaryBuffer* a = expected.getDP();
            const LLDataPackerBinaryBuffer* b = actual.getDP();
            {
                INFO("body present");
                REQUIRE(a);
                REQUIRE(b);
            }
            REQUIRE(b->getBufferSize() == a->getBufferSize());
            UNSCOPED_INFO("body bytes");
            REQUIRE(memcmp(a->getBuffer(), b->getBuffer(), (size_t)a->getBufferSize()) == 0);
        }

        // An override message in the shape LLGLTFOverrideCacheEntry::fromLLSD
        // expects, with `sides` faces each carrying a roughness and textures.
        static LLSD makeOverrideLLSD(U32 local_id, U64 handle, const LLUUID& object_id, int sides)
        {
            U32 x, y;
            from_region_handle(handle, &x, &y);
            LLSD data;
            data["local_id"] = (LLSD::Integer)local_id;
            data["object_id"] = object_id;
            data["region_handle_x"] = (LLSD::Integer)x;
            data["region_handle_y"] = (LLSD::Integer)y;
            for (int side = 0; side < sides; ++side)
            {
                LLSD over;
                over["rf"] = 0.25 * side;
                LLSD tex;
                tex.append(LLUUID::generateNewID());
                tex.append(LLUUID::null);
                tex.append(LLUUID::null);
                over["tex"] = tex;
                data["sides"].append(side);
                data["gltf_llsd"].append(over);
            }
            return data;
        }

        static LLGLTFOverrideCacheEntry requireOverride(U32 local_id, U64 handle, int sides)
        {
            LLGLTFOverrideCacheEntry entry;
            UNSCOPED_INFO("override parses");
            REQUIRE(entry.fromLLSD(makeOverrideLLSD(local_id, handle, LLUUID::generateNewID(), sides)));
            return entry;
        }

        // An extras file written by hand: the header the reader expects, then
        // each record as its length and its bytes, whatever they are.
        void writeExtrasFile(U64 handle, const LLUUID& id, const std::vector<std::string>& records) const
        {
            std::string data;
            data += LLGLTFOverrideCacheEntry::VERSION_LABEL;
            data += ':';
            data += std::to_string(LLGLTFOverrideCacheEntry::VERSION);
            data += '\n';
            data.append(reinterpret_cast<const char*>(id.mData), UUID_BYTES);
            const U32 count = (U32)records.size();
            data.append(reinterpret_cast<const char*>(&count), sizeof(U32));
            for (const std::string& record : records)
            {
                const U32 length = (U32)record.size();
                data.append(reinterpret_cast<const char*>(&length), sizeof(U32));
                data += record;
            }
            std::ofstream out(objectFile(handle, "_extras.slec"), std::ios::binary | std::ios::trunc);
            out.write(data.data(), (std::streamsize)data.size());
        }

        static std::string binaryRecord(const LLSD& llsd)
        {
            std::ostringstream out;
            LLSDSerialize::toBinary(llsd, out);
            return out.str();
        }

        static LLSD nestedArrays(int depth)
        {
            LLSD inner = 1;
            for (int i = 0; i < depth; ++i)
            {
                LLSD outer;
                outer.append(inner);
                inner = outer;
            }
            return inner;
        }

        std::filesystem::path headerFile() const { return mRoot / "objectcache" / "object.cache"; }

        static std::string slurp(const std::filesystem::path& file)
        {
            std::ifstream in(file, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        static void spit(const std::filesystem::path& file, const std::string& bytes)
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), (std::streamsize)bytes.size());
        }

        // Restarts the cache the way a crash would have left it: the header the
        // last in-place update wrote, not the compacted one a clean shutdown
        // writes over it.
        void restartAsAfterCrash()
        {
            const std::string header = slurp(headerFile());
            LLVOCache::deleteSingleton();
            spit(headerFile(), header);
            LLVOCache::initParamSingleton(false);
            LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);
        }

        static void requireSameOverride(const LLGLTFOverrideCacheEntry& expected, const LLGLTFOverrideCacheEntry& actual)
        {
            REQUIRE(actual.mObjectId == expected.mObjectId);
            REQUIRE(actual.mLocalId == expected.mLocalId);
            REQUIRE(actual.mRegionHandle == expected.mRegionHandle);
            REQUIRE(actual.mSides.size() == expected.mSides.size());
            for (const auto& [side, llsd] : expected.mSides)
            {
                CAPTURE(side);
                auto found = actual.mSides.find(side);
                UNSCOPED_INFO("side present");
                REQUIRE(found != actual.mSides.end());
                REQUIRE(llsd_equals(found->second, llsd));
            }
        }
    };

    // ---------------------------------------------------------------------------------------
    // Test functions
    // ---------------------------------------------------------------------------------------

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: an override entry round-trips LLSD and builds materials on demand", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1000 * 256, 1000 * 256);
        const LLUUID object_id = LLUUID::generateNewID();
        LLSD data = makeOverrideLLSD(42, handle, object_id, 3);

        LLGLTFOverrideCacheEntry entry;
        REQUIRE(entry.fromLLSD(data));
        CHECK(entry.mObjectId == object_id);
        CHECK(entry.mLocalId == 42u);
        CHECK(entry.mRegionHandle == handle);
        REQUIRE(entry.mSides.size() == (size_t)3);
        UNSCOPED_INFO("materials not built by loading");
        CHECK(entry.mGLTFMaterial.empty());

        // toLLSD walks a hash map, so compare per side rather than as arrays.
        LLSD back = entry.toLLSD();
        UNSCOPED_INFO("local id survives");
        CHECK(back["local_id"].asInteger() == 42);
        UNSCOPED_INFO("object id survives");
        CHECK(back["object_id"].asUUID() == object_id);
        UNSCOPED_INFO("region x survives");
        CHECK(back["region_handle_x"].asInteger() == data["region_handle_x"].asInteger());
        UNSCOPED_INFO("region y survives");
        CHECK(back["region_handle_y"].asInteger() == data["region_handle_y"].asInteger());
        UNSCOPED_INFO("side count survives");
        REQUIRE(back["sides"].size() == (size_t)3);
        for (size_t i = 0; i < back["sides"].size(); ++i)
        {
            S32 side = back["sides"][i].asInteger();
            CAPTURE(side);
            UNSCOPED_INFO("side llsd survives");
            REQUIRE(llsd_equals(back["gltf_llsd"][i], data["gltf_llsd"][side]));
        }

        entry.materialize();
        UNSCOPED_INFO("one material per side");
        REQUIRE(entry.mGLTFMaterial.size() == entry.mSides.size());
        for (const auto& [side, llsd] : entry.mSides)
        {
            CAPTURE(side);
            UNSCOPED_INFO("material for side");
            REQUIRE(entry.mGLTFMaterial.find(side) != entry.mGLTFMaterial.end());
            UNSCOPED_INFO("material built");
            REQUIRE(entry.mGLTFMaterial.at(side).notNull());
        }
        LLGLTFMaterial* first = entry.mGLTFMaterial.at(0).get();
        entry.materialize();
        UNSCOPED_INFO("second materialize keeps the materials");
        CHECK(entry.mGLTFMaterial.at(0).get() == first);

        LLGLTFOverrideCacheEntry rejected;
        LLSD headless = data;
        headless.erase("region_handle_x");
        UNSCOPED_INFO("no region handle is rejected");
        CHECK_FALSE(rejected.fromLLSD(headless));
        headless = data;
        headless.erase("local_id");
        UNSCOPED_INFO("no local id is rejected");
        CHECK_FALSE(rejected.fromLLSD(headless));
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: an object cache file round-trips every entry", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1000 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(500);

        LLVOCache::instance().writeToCache(handle, id, written, /*dirty=*/true, /*removal_enabled=*/false);
        UNSCOPED_INFO("cache file written");
        CHECK(std::filesystem::exists(objectFile(handle, ".slc")));

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("read succeeds");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read));
        UNSCOPED_INFO("entry count");
        REQUIRE(read.size() == written.size());
        for (const auto& [local_id, entry] : written)
        {
            CAPTURE(local_id);
            auto found = read.find(local_id);
            UNSCOPED_INFO("entry present");
            REQUIRE(found != read.end());
            requireSameEntry(*entry, *found->second);
        }

        LLVOCacheEntry::vocache_entry_map_t other;
        UNSCOPED_INFO("another region's id is refused");
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, LLUUID::generateNewID(), other));
        UNSCOPED_INFO("nothing loaded for it");
        CHECK(other.empty());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a save with removal enabled counts only what it wrote", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1001 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(200);
        size_t valid = 0;
        for (auto& [local_id, entry] : written)
        {
            entry->setValid(local_id % 3 != 0);
            valid += entry->isValid() ? 1 : 0;
        }
        UNSCOPED_INFO("some entries were invalidated");
        REQUIRE(valid < written.size());

        LLVOCache::instance().writeToCache(handle, id, written, true, /*removal_enabled=*/true);

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("read succeeds against the written count");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read));
        UNSCOPED_INFO("only valid entries were written");
        REQUIRE(read.size() == valid);
        for (const auto& [local_id, entry] : read)
        {
            UNSCOPED_INFO("no invalid entry came back");
            REQUIRE(written.at(local_id)->isValid());
        }
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a truncated object cache is refused", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1002 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(100);
        LLVOCache::instance().writeToCache(handle, id, written, true, false);

        const std::filesystem::path file = objectFile(handle, ".slc");
        const uintmax_t size = std::filesystem::file_size(file);
        std::filesystem::resize_file(file, size - 100);

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("truncated file fails the read");
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, id, read));
        UNSCOPED_INFO("fewer entries than written");
        CHECK(read.size() < written.size());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: the header survives a restart", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1003 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(50);
        LLVOCache::instance().writeToCache(handle, id, written, true, false);

        LLVOCache::deleteSingleton();
        LLVOCache::initParamSingleton(false);
        LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("region known after restart");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read));
        UNSCOPED_INFO("entries after restart");
        CHECK(read.size() == written.size());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: the override cache round-trips and drops overrides with no object", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1004 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(20);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t written;
        for (U32 local_id = 1; local_id <= 20; local_id += 3)
        {
            written[local_id] = requireOverride(local_id, handle, 1 + (int)(local_id % 4));
        }
        // An override for an object the primary cache does not hold is dropped on load.
        written[999] = requireOverride(999, handle, 2);

        LLVOCache::instance().writeGenericExtrasToCache(handle, id, written, true, false);
        UNSCOPED_INFO("extras file written");
        CHECK(std::filesystem::exists(objectFile(handle, "_extras.slec")));

        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        UNSCOPED_INFO("read succeeds");
        REQUIRE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, read, objects));
        UNSCOPED_INFO("all overrides with an object came back");
        CHECK(read.size() == written.size() - 1);
        UNSCOPED_INFO("orphan override dropped");
        CHECK(read.find(999) == read.end());
        for (const auto& [local_id, entry] : read)
        {
            CAPTURE(local_id);
            requireSameOverride(written.at(local_id), entry);
            UNSCOPED_INFO("materials not built by loading");
            REQUIRE(entry.mGLTFMaterial.empty());
        }

        LLVOCacheEntry::vocache_gltf_overrides_map_t none;
        LLVOCacheEntry::vocache_entry_map_t no_objects;
        UNSCOPED_INFO("empty primary cache is not a failure");
        CHECK(LLVOCache::instance().readGenericExtrasFromCache(handle, id, none, no_objects));
        UNSCOPED_INFO("and loads nothing");
        CHECK(none.empty());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a corrupt override record is skipped and a truncated file refused", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1005 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(10);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t written;
        for (U32 local_id = 1; local_id <= 10; ++local_id)
        {
            written[local_id] = requireOverride(local_id, handle, 2);
        }
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, written, true, false);

        const std::filesystem::path file = objectFile(handle, "_extras.slec");
        std::string bytes;
        {
            std::ifstream in(file, std::ios::binary);
            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        // version line, cache id, count, then records of [length][llsd]
        const size_t line_end = bytes.find('\n');
        UNSCOPED_INFO("version line present");
        REQUIRE(line_end != std::string::npos);
        UNSCOPED_INFO("version line");
        CHECK(bytes.substr(0, line_end) == LLGLTFOverrideCacheEntry::VERSION_LABEL + ":" + std::to_string(LLGLTFOverrideCacheEntry::VERSION));
        size_t cursor = line_end + 1 + UUID_BYTES;
        UNSCOPED_INFO("the cache id, the count and the first record's length inside the file");
        REQUIRE(cursor + 2 * sizeof(U32) <= bytes.size());
        U32 count = 0;
        memcpy(&count, bytes.data() + cursor, sizeof(U32));
        cursor += sizeof(U32);
        UNSCOPED_INFO("record count");
        REQUIRE((size_t)count == written.size());

        // Overwrite the second record's first byte with a type tag LLSD binary
        // does not have, so that one record and only that one fails to parse.
        U32 first_length = 0;
        memcpy(&first_length, bytes.data() + cursor, sizeof(U32));
        const size_t second_record = cursor + sizeof(U32) + first_length + sizeof(U32);
        UNSCOPED_INFO("second record inside file");
        REQUIRE(second_record < bytes.size());
        std::string corrupted = bytes;
        corrupted[second_record] = (char)0xFF;
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(corrupted.data(), (std::streamsize)corrupted.size());
        }

        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        UNSCOPED_INFO("corrupt record does not fail the file");
        REQUIRE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, read, objects));
        UNSCOPED_INFO("one record skipped");
        CHECK(read.size() == written.size() - 1);
        for (const auto& [local_id, entry] : read)
        {
            CAPTURE(local_id);
            requireSameOverride(written.at(local_id), entry);
        }

        // Cut the last record short.
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), (std::streamsize)(bytes.size() - 3));
        }
        LLVOCacheEntry::vocache_gltf_overrides_map_t partial;
        UNSCOPED_INFO("truncated file is refused");
        CHECK_FALSE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, partial, objects));

        // An earlier format is refused by its version line alone.
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << LLGLTFOverrideCacheEntry::VERSION_LABEL << ":1\n" << id << "\n0000000000\n";
        }
        LLVOCacheEntry::vocache_gltf_overrides_map_t old;
        UNSCOPED_INFO("old version is refused");
        CHECK_FALSE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, old, objects));
        UNSCOPED_INFO("old version loads nothing");
        CHECK(old.empty());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: memoryAdjustFactor bounds follow RAM and stay under the heap cap", "[LLVOCache]")
    {
        auto factor = [](F32 allocated, F32 physical, F32 cap, F32 low, F32 high)
        {
            return LLVOCacheEntry::memoryAdjustFactor(allocated, physical, cap, low, high);
        };
        auto close_to = [](F32 a, F32 b) { return fabsf(a - b) < 1e-4f; };
        const F32 no_cap = 4194303.f; // the cap before it is set: U32_MAX kilobytes

        // 8 GB with the default settings: the bounds are the RAM fractions,
        // 2048 and 4915, and the factor runs from 1 to 0 between them
        UNSCOPED_INFO("at the low bound nothing is pulled in");
        CHECK(close_to(factor(2048.f, 8192.f, no_cap, 750.f, 2048.f), 1.f));
        UNSCOPED_INFO("at the high bound everything is");
        CHECK(close_to(factor(4915.2f, 8192.f, no_cap, 750.f, 2048.f), 0.f));
        UNSCOPED_INFO("halfway is half");
        CHECK(close_to(factor(3481.6f, 8192.f, no_cap, 750.f, 2048.f), 0.5f));
        UNSCOPED_INFO("well below the low bound is still 1");
        CHECK(close_to(factor(100.f, 8192.f, no_cap, 750.f, 2048.f), 1.f));

        // no RAM figure: the settings alone
        UNSCOPED_INFO("without a RAM figure the settings are the bounds");
        CHECK(close_to(factor(1399.f, 0.f, no_cap, 750.f, 2048.f), 0.5f));

        // 64 GB behind a 16 GB heap cap: the tightest point moves under the
        // cap and the loose point keeps its ratio to it
        const F32 cap = 16384.f;
        UNSCOPED_INFO("the tightest point sits under the cap");
        CHECK(close_to(factor(cap * 0.9f, 65536.f, cap, 750.f, 2048.f), 0.f));
        UNSCOPED_INFO("the loose point scaled with it");
        CHECK(close_to(factor(6144.f, 65536.f, cap, 750.f, 2048.f), 1.f));
        UNSCOPED_INFO("between them is between");
        CHECK(close_to(factor(10444.8f, 65536.f, cap, 750.f, 2048.f), 0.5f));
        UNSCOPED_INFO("past the cap is still a factor");
        CHECK(factor(2.f * cap, 65536.f, cap, 750.f, 2048.f) == 0.f);

        // settings the wrong way round: the low one is the bound, and the high
        // one is taken as just past it
        for (F32 allocated : { 0.f, 2000.f, 2500.f, 3000.f, 4000.f })
        {
            const F32 f = factor(allocated, 0.f, no_cap, 3000.f, 2048.f);
            CAPTURE(allocated, f);
            INFO("inverted settings: up to the low setting nothing is pulled in, past it everything is");
            CHECK(close_to(f, allocated <= 3000.f ? 1.f : 0.f));
        }

        // a bound set by hand is the bound, tighter than RAM would make it too
        auto explicit_factor = [](F32 allocated, F32 physical, F32 cap, F32 low, F32 high)
        {
            return LLVOCacheEntry::memoryAdjustFactor(allocated, physical, cap, low, high, true, true);
        };
        UNSCOPED_INFO("explicit bounds on a big machine: at the low one nothing is pulled in");
        CHECK(close_to(explicit_factor(1000.f, 65536.f, no_cap, 1000.f, 2000.f), 1.f));
        UNSCOPED_INFO("explicit bounds on a big machine: at the high one everything is");
        CHECK(close_to(explicit_factor(2000.f, 65536.f, no_cap, 1000.f, 2000.f), 0.f));
        UNSCOPED_INFO("explicit bounds on a big machine: halfway is half");
        CHECK(close_to(explicit_factor(1500.f, 65536.f, no_cap, 1000.f, 2000.f), 0.5f));
        // The default low follows RAM to 16384, past the explicit high of
        // 2048, so the high is taken as just past the low.
        UNSCOPED_INFO("an explicit high with a default low still comes out ordered: below the low one nothing is pulled in");
        CHECK(close_to(LLVOCacheEntry::memoryAdjustFactor(16000.f, 65536.f, no_cap, 750.f, 2048.f, false, true), 1.f));
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: hostile extras records meet a depth cap and a budget and lose bad faces", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1006 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(4);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        // a legitimate record carrying something nested a little loads; one
        // nested past the cap is refused rather than parsed
        LLSD shallow = makeOverrideLLSD(1, handle, LLUUID::generateNewID(), 1);
        shallow["deep"] = nestedArrays(8);
        LLSD deep = makeOverrideLLSD(2, handle, LLUUID::generateNewID(), 1);
        deep["deep"] = nestedArrays(24);
        writeExtrasFile(handle, id, { binaryRecord(shallow), binaryRecord(deep) });

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        UNSCOPED_INFO("a file with one bad record is still a cache");
        REQUIRE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, objects));
        UNSCOPED_INFO("the shallow record loaded, the deep one did not");
        CHECK(overrides.size() == 1u);
        UNSCOPED_INFO("it is the shallow one");
        CHECK(overrides.find(1) != overrides.end());

        // up to the budget, unreadable records are skipped; past it the file is refused
        std::vector<std::string> garbage(16, std::string("\xff\xfe\xfd\xfc", 4));
        garbage.push_back(binaryRecord(makeOverrideLLSD(3, handle, LLUUID::generateNewID(), 1)));
        writeExtrasFile(handle, id, garbage);
        overrides.clear();
        UNSCOPED_INFO("sixteen unreadable records are within the budget");
        CHECK(LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, objects));
        UNSCOPED_INFO("and the good record after them loaded");
        CHECK(overrides.size() == 1u);

        garbage.insert(garbage.begin(), std::string("\xff\xfe\xfd\xfc", 4));
        writeExtrasFile(handle, id, garbage);
        overrides.clear();
        UNSCOPED_INFO("seventeen is a bad file");
        CHECK_FALSE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, objects));

        // a face index no object has is dropped by the record parser
        LLSD faces = makeOverrideLLSD(4, handle, LLUUID::generateNewID(), 2);
        faces["sides"][1] = 300;
        LLGLTFOverrideCacheEntry entry;
        UNSCOPED_INFO("the record still parses");
        REQUIRE(entry.fromLLSD(faces));
        UNSCOPED_INFO("only the face in range is kept");
        CHECK(entry.mSides.size() == 1u);
        UNSCOPED_INFO("and it is face 0");
        CHECK(entry.mSides.find(0) != entry.mSides.end());
        faces["sides"][1] = -1;
        LLGLTFOverrideCacheEntry negative;
        UNSCOPED_INFO("a negative face parses");
        REQUIRE(negative.fromLLSD(faces));
        UNSCOPED_INFO("and is dropped too");
        CHECK(negative.mSides.size() == 1u);
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: header slots survive a removal followed by a crash", "[LLVOCache]")
    {
        const LLUUID id = LLUUID::generateNewID();
        const U64 first = to_region_handle(1010 * 256, 1000 * 256);
        const U64 second = to_region_handle(1011 * 256, 1000 * 256);
        const U64 third = to_region_handle(1012 * 256, 1000 * 256);
        const U64 fourth = to_region_handle(1013 * 256, 1000 * 256);
        LLVOCache::instance().writeToCache(first, id, makeEntries(3), true, false);
        LLVOCache::instance().writeToCache(second, id, makeEntries(3), true, false);
        LLVOCache::instance().writeToCache(third, id, makeEntries(3), true, false);

        // removing the middle region leaves a hole in the header on disk; a
        // crash means nobody closes it up
        LLVOCache::instance().removeEntry(second);
        restartAsAfterCrash();
        UNSCOPED_INFO("two regions come back");
        CHECK(LLVOCache::instance().getCacheEntries() == 2u);

        // a new region has to take the hole, not the slot of the one behind it
        LLVOCache::instance().writeToCache(fourth, id, makeEntries(3), true, false);
        restartAsAfterCrash();
        UNSCOPED_INFO("three regions come back");
        CHECK(LLVOCache::instance().getCacheEntries() == 3u);
        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("the first region is still there");
        CHECK(LLVOCache::instance().readFromCache(first, id, read));
        read.clear();
        UNSCOPED_INFO("the third region was not overwritten");
        CHECK(LLVOCache::instance().readFromCache(third, id, read));
        read.clear();
        UNSCOPED_INFO("the new region is there");
        CHECK(LLVOCache::instance().readFromCache(fourth, id, read));
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: extras are only rewritten when the overrides changed", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1020 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(4);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1] = requireOverride(1, handle, 2);
        overrides[2] = requireOverride(2, handle, 3);
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, overrides, true, false);

        // a save with nothing dirty leaves the file as it was, whatever it is handed
        LLVOCacheEntry::vocache_gltf_overrides_map_t nothing;
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, nothing, false, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        UNSCOPED_INFO("the file still reads");
        REQUIRE(LLVOCache::instance().readGenericExtrasFromCache(handle, id, read, objects));
        UNSCOPED_INFO("and still holds both overrides");
        REQUIRE(read.size() == 2u);
        requireSameOverride(overrides[1], read[1]);
        requireSameOverride(overrides[2], read[2]);
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a region that never had an override keeps its objects into the next login", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1021 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(30);

        // The region's save: its objects missed the cache, so they are dirty;
        // no override ever arrived, so there are none and nothing marked the
        // overrides dirty.
        LLVOCache::instance().writeToCache(handle, id, written, /*dirty=*/true, false);
        LLVOCacheEntry::vocache_gltf_overrides_map_t nothing;
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, nothing, /*dirty=*/false, false);
        UNSCOPED_INFO("the extras file is written beside the objects");
        CHECK(std::filesystem::exists(objectFile(handle, "_extras.slec")));

        LLVOCache::deleteSingleton();
        LLVOCache::initParamSingleton(false);
        LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);

        // The next login loads as LLViewerRegion::loadObjectCache does: extras
        // it cannot use take the objects with them.
        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("the objects read");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read));
        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        const bool extras_read = LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, read);
        if (!extras_read)
        {
            LLVOCache::instance().removeGenericExtrasForHandle(handle);
        }
        UNSCOPED_INFO("the extras read");
        CHECK(extras_read);
        UNSCOPED_INFO("and hold no override");
        CHECK(overrides.empty());

        LLVOCacheEntry::vocache_entry_map_t kept;
        UNSCOPED_INFO("the object cache is still there after the load");
        CHECK(LLVOCache::instance().readFromCache(handle, id, kept));
        UNSCOPED_INFO("with every object in it");
        CHECK(kept.size() == written.size());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: an object write that fails leaves no extras file behind", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1022 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();

        // A body past the cache's ten-thousand-byte cap on one entry fails the
        // object write, which drops the region from the header. The save goes
        // on to the extras, dirty since the objects were.
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(3);
        objects[4] = makeEntry(4, 4, 10001);
        LLVOCache::instance().writeToCache(handle, id, objects, /*dirty=*/true, false);
        UNSCOPED_INFO("the failed write dropped the region");
        REQUIRE(LLVOCache::instance().getCacheEntries() == 0u);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1] = requireOverride(1, handle, 2);
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, overrides, /*dirty=*/true, false);
        UNSCOPED_INFO("no object file");
        CHECK_FALSE(std::filesystem::exists(objectFile(handle, ".slc")));
        UNSCOPED_INFO("and no extras file for a region the header does not hold");
        CHECK_FALSE(std::filesystem::exists(objectFile(handle, "_extras.slec")));
    }

    // Removing a region's entry lets go of its objects in memory, and an
    // object update that finds the cache bogus is read from one of them.
    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a removal asked for waits for the removals asked for to be carried out", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1023 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCache::instance().writeToCache(handle, id, makeEntries(3), true, false);
        REQUIRE(LLVOCache::instance().getCacheEntries() == 1u);

        CHECK_FALSE(LLVOCache::instance().isRemovalRequested(handle));
        LLVOCache::instance().requestRemoval(handle);
        LLVOCache::instance().requestRemoval(handle);
        UNSCOPED_INFO("the region is still cached once its removal is asked for");
        CHECK(LLVOCache::instance().isRemovalRequested(handle));
        CHECK(LLVOCache::instance().getCacheEntries() == 1u);
        CHECK(std::filesystem::exists(objectFile(handle, ".slc")));

        LLVOCache::instance().removeRequestedEntries();
        UNSCOPED_INFO("and gone once the removals are carried out");
        CHECK_FALSE(LLVOCache::instance().isRemovalRequested(handle));
        CHECK(LLVOCache::instance().getCacheEntries() == 0u);
        CHECK_FALSE(std::filesystem::exists(objectFile(handle, ".slc")));

        UNSCOPED_INFO("which leaves none asked for");
        LLVOCache::instance().writeToCache(handle, id, makeEntries(3), true, false);
        LLVOCache::instance().removeRequestedEntries();
        CHECK(LLVOCache::instance().getCacheEntries() == 1u);
    }

    TEST_CASE("LLVOCacheEntry: an entry is the object of the UUID its data opens with", "[LLVOCache]")
    {
        const LLUUID object_id("44444444-4444-4444-4444-444444444444");
        const LLUUID other_id("55555555-5555-5555-5555-555555555555");

        std::vector<U8> body(object_id.mData, object_id.mData + UUID_BYTES);
        body.resize(UUID_BYTES + 24, 0xAB);
        LLDataPackerBinaryBuffer dp(body.data(), (S32)body.size());
        LLPointer<LLVOCacheEntry> entry = new LLVOCacheEntry(7, 1234, dp);
        CHECK(entry->isObject(object_id));
        CHECK_FALSE(entry->isObject(other_id));
        CHECK_FALSE(entry->isObject(LLUUID::null));

        UNSCOPED_INFO("and reading it moves nothing that reads the entry after");
        LLDataPackerBinaryBuffer* read_back = entry->getDP();
        REQUIRE(read_back);
        read_back->reset();
        LLUUID read_id;
        read_back->unpackUUID(read_id, "ID");
        CHECK(read_id == object_id);

        UNSCOPED_INFO("data too short to hold one is no object's");
        LLDataPackerBinaryBuffer short_dp(body.data(), UUID_BYTES - 1);
        LLPointer<LLVOCacheEntry> short_entry = new LLVOCacheEntry(8, 1234, short_dp);
        CHECK_FALSE(short_entry->isObject(object_id));
        LLPointer<LLVOCacheEntry> empty_entry = new LLVOCacheEntry();
        CHECK_FALSE(empty_entry->isObject(LLUUID::null));
    }

    TEST_CASE("LLVOCacheEntry: a kill takes a local ID's overrides whether or not it has a valid entry", "[LLVOCache]")
    {
        constexpr U32 VALID = 1;
        constexpr U32 INVALID = 2;
        constexpr U32 NO_ENTRY = 3;
        constexpr U32 NO_OVERRIDES = 4;

        LLVOCacheEntry::vocache_entry_map_t entries;
        entries[VALID] = vocacheTest::makeEntry(VALID, 1, 16);
        entries[INVALID] = vocacheTest::makeEntry(INVALID, 2, 16);
        entries[INVALID]->setValid(false);
        entries[NO_OVERRIDES] = vocacheTest::makeEntry(NO_OVERRIDES, 4, 16);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        for (U32 local_id : { VALID, INVALID, NO_ENTRY })
        {
            overrides[local_id].mLocalId = local_id;
        }

        std::vector<LLVOCacheEntry*> killed;
        const auto kill_entry = [&](LLVOCacheEntry* entry) { killed.push_back(entry); };

        CHECK(LLVOCacheEntry::killLocalID(VALID, entries, overrides, kill_entry));
        CHECK(overrides.count(VALID) == 0);
        REQUIRE(killed.size() == 1u);
        CHECK(killed[0] == entries[VALID].get());

        killed.clear();
        UNSCOPED_INFO("an entry already invalid is not killed twice");
        CHECK(LLVOCacheEntry::killLocalID(INVALID, entries, overrides, kill_entry));
        CHECK(overrides.count(INVALID) == 0);
        CHECK(killed.empty());

        UNSCOPED_INFO("a local ID with no entry at all");
        CHECK(LLVOCacheEntry::killLocalID(NO_ENTRY, entries, overrides, kill_entry));
        CHECK(overrides.empty());
        CHECK(killed.empty());

        UNSCOPED_INFO("an entry with no overrides is killed and says it took none");
        CHECK_FALSE(LLVOCacheEntry::killLocalID(NO_OVERRIDES, entries, overrides, kill_entry));
        REQUIRE(killed.size() == 1u);
        CHECK(killed[0] == entries[NO_OVERRIDES].get());

        UNSCOPED_INFO("and a local ID with neither takes nothing");
        killed.clear();
        CHECK_FALSE(LLVOCacheEntry::killLocalID(99, entries, overrides, kill_entry));
        CHECK(killed.empty());
    }

    TEST_CASE("LLVOCacheEntry: releaseAll leaves no set holding an entry it let go", "[LLVOCache]")
    {
        LLVOCacheEntry::vocache_entry_map_t entries = vocacheTest::makeEntries(6);
        const LLPointer<LLVOCacheEntry> watched = entries[2];
        LLVOCacheEntry::vocache_entry_set_t active;
        LLVOCacheEntry::vocache_entry_set_t visible;
        active.insert(entries[1].get());
        active.insert(watched.get());
        visible.insert(entries[5].get());

        std::vector<LLVOCacheEntry*> left_tree;
        std::vector<LLPointer<LLVOCacheEntry>> keep;
        sRemovedOctreeEntries.clear();
        LLVOCacheEntry::releaseAll(entries, { &active, &visible }, [&](LLVOCacheEntry* entry) { left_tree.push_back(entry); }, keep);

        CHECK(entries.empty());
        CHECK(active.empty());
        CHECK(visible.empty());
        UNSCOPED_INFO("every entry left the tree");
        CHECK(left_tree.size() == 6u);
        CHECK(std::find(left_tree.begin(), left_tree.end(), watched.get()) != left_tree.end());
        UNSCOPED_INFO("and its octree entry");
        CHECK(sRemovedOctreeEntries.size() == 6u);
        const LLViewerOctreeEntryData* watched_data = watched.get();
        CHECK(std::find(sRemovedOctreeEntries.begin(), sRemovedOctreeEntries.end(), watched_data) != sRemovedOctreeEntries.end());
        UNSCOPED_INFO("and is held by keep alone besides the test");
        CHECK(keep.size() == 6u);
        CHECK(watched->getNumRefs() == 2);
        CHECK_FALSE(watched->isValid());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: staysInMemory keeps entries seen lately or inside the rear sphere", "[LLVOCache]")
    {
        // A stand-in for an entry that nothing has looked at in a while: its
        // group was last visible well outside the window, and it sits far
        // enough back that the sphere cannot reach it either.
        const S32 NOW = 5000;
        const U32 RANGE = 64;
        auto forgotten = [&]()
        {
            LLVOCacheEntry::VisibilityFacts facts;
            facts.mHasGroup = true;
            facts.mGroupAnyVisibleFrame = NOW - 1000;
            facts.mEntryVisibleFrame = NOW - 1000;
            facts.mCurrentFrame = NOW;
            facts.mMinFrameRange = RANGE;
            facts.mDistanceSquared = 10000.f; // 100m
            facts.mRadius = 1.f;
            facts.mDistThreshold = 32.f;
            return facts;
        };

        UNSCOPED_INFO("an entry nothing has seen, far behind the camera, is dropped");
        CHECK_FALSE(LLVOCacheEntry::staysInMemory(forgotten()));

        // the group is the first answer, and it outranks distance entirely
        {
            LLVOCacheEntry::VisibilityFacts facts = forgotten();
            facts.mGroupRecentlyVisible = true;
            facts.mDistanceSquared = 1e12f;
            UNSCOPED_INFO("a recently visible group keeps an entry at any distance");
            CHECK(LLVOCacheEntry::staysInMemory(facts));
        }

        // the frame window, with no group to ask
        {
            LLVOCacheEntry::VisibilityFacts facts = forgotten();
            facts.mHasGroup = false;
            facts.mGroupAnyVisibleFrame = 0;
            facts.mEntryVisibleFrame = NOW - ((S32)RANGE - 1);
            UNSCOPED_INFO("inside the frame window a group-less entry is kept");
            CHECK(LLVOCacheEntry::staysInMemory(facts));

            facts.mEntryVisibleFrame = NOW - ((S32)RANGE + 1);
            UNSCOPED_INFO("past the frame window and past the sphere it is dropped");
            CHECK_FALSE(LLVOCacheEntry::staysInMemory(facts));
        }

        // a group that saw something more recently than the entry did wins
        {
            LLVOCacheEntry::VisibilityFacts facts = forgotten();
            facts.mEntryVisibleFrame = NOW - 1000;
            facts.mGroupAnyVisibleFrame = NOW - ((S32)RANGE - 1);
            UNSCOPED_INFO("the more recent of group and entry is the one used");
            CHECK(LLVOCacheEntry::staysInMemory(facts));
        }

        // the sphere behind the camera, and the two things that switch it off
        {
            LLVOCacheEntry::VisibilityFacts facts = forgotten();
            facts.mDistanceSquared = 32.f * 32.f; // inside 32 + 1
            UNSCOPED_INFO("inside the rear sphere it is kept");
            CHECK(LLVOCacheEntry::staysInMemory(facts));

            facts.mIsChild = true;
            UNSCOPED_INFO("a child is not judged by the sphere; it goes with its parent");
            CHECK_FALSE(LLVOCacheEntry::staysInMemory(facts));

            facts.mIsChild = false;
            facts.mGroupOccluded = true;
            UNSCOPED_INFO("a group known to be occluded does not get the sphere either");
            CHECK_FALSE(LLVOCacheEntry::staysInMemory(facts));
        }

        // the sphere's edge is exclusive, and the entry's own radius widens it
        {
            LLVOCacheEntry::VisibilityFacts facts = forgotten();
            const F32 edge = facts.mDistThreshold + facts.mRadius;
            facts.mDistanceSquared = edge * edge;
            UNSCOPED_INFO("exactly on the sphere is outside it");
            CHECK_FALSE(LLVOCacheEntry::staysInMemory(facts));

            facts.mRadius = 80.f;
            UNSCOPED_INFO("a large entry reaches into the sphere from further out");
            CHECK(LLVOCacheEntry::staysInMemory(facts));
        }

        // A zero window evicts an object that was visible this very frame. The
        // settings are read once a second, which is why sMinFrameRange starts at
        // the widest window they can produce rather than at zero.
        {
            LLVOCacheEntry::VisibilityFacts facts = forgotten();
            facts.mMinFrameRange = 0;
            facts.mEntryVisibleFrame = NOW;
            facts.mGroupAnyVisibleFrame = NOW;
            UNSCOPED_INFO("a zero frame window drops an entry seen this frame");
            CHECK_FALSE(LLVOCacheEntry::staysInMemory(facts));

            facts.mMinFrameRange = 1;
            UNSCOPED_INFO("a window of one keeps it");
            CHECK(LLVOCacheEntry::staysInMemory(facts));
        }
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: the radius that evicts an entry is wider than the one that loads it", "[LLVOCache]")
    {
        const F32 MIN_RADIUS = 32.f;   // SceneLoadMinRadius
        const F32 DRAW = 128.f;
        auto evict = [&](F32 rear_far) { return LLVOCacheEntry::evictRadius(rear_far, MIN_RADIUS, DRAW); };

        // sRearFarRadius runs from about two metres under memory pressure to
        // three quarters of draw distance with none.
        const F32 rear_radii[] = { 2.f, 8.f, 32.f, 96.f };
        F32 previous = -1.f;
        for (F32 rear_far : rear_radii)
        {
            const F32 radius = evict(rear_far);
            CAPTURE(rear_far);
            UNSCOPED_INFO("the eviction radius clears the load radius");
            REQUIRE(radius >= rear_far * 1.25f);
            UNSCOPED_INFO("and never reaches inside SceneLoadMinRadius");
            REQUIRE(radius >= llmin(MIN_RADIUS, DRAW));
            UNSCOPED_INFO("and does not shrink as the load radius grows");
            REQUIRE(radius >= previous);
            previous = radius;
        }

        // a draw distance under the setting is the floor instead
        UNSCOPED_INFO("a short draw distance floors the radius, not the setting");
        CHECK(LLVOCacheEntry::evictRadius(2.f, MIN_RADIUS, 16.f) == 16.f);

        // The sphere is the whole sphere, not just the part behind the camera,
        // and nothing beyond the draw distance is drawn, so nothing beyond it is
        // worth keeping for the turn back. Above four fifths of draw distance the
        // band between the two radii narrows to what is left.
        UNSCOPED_INFO("the draw distance caps the radius");
        CHECK(LLVOCacheEntry::evictRadius(120.f, MIN_RADIUS, DRAW) == DRAW);

        // The invariant the two radii exist for: an entry sitting exactly at the
        // distance the back cull preloads it from is not evictable, however long
        // it has been out of sight. Turning around cannot cross both at once.
        for (F32 rear_far : rear_radii)
        {
            CAPTURE(rear_far);
            LLVOCacheEntry::VisibilityFacts facts;
            facts.mCurrentFrame = 5000;
            facts.mEntryVisibleFrame = 0;
            facts.mMinFrameRange = 64;
            facts.mDistanceSquared = rear_far * rear_far;
            facts.mDistThreshold = evict(rear_far);
            UNSCOPED_INFO("an entry at the load radius is never evicted at the eviction radius");
            REQUIRE(LLVOCacheEntry::staysInMemory(facts));
        }
    }

    // A cache group as the object cache's front cull sees it, played through
    // the partition's own cadence and decision with occlusion on: what earlyFail
    // and doOcclusion do around them is modelled here, with every query answered
    // RESULT_AGE frames after it was asked.
    struct CacheGroupModel
    {
        static constexpr U32 PERIOD     = 16; // LLVOCachePartition's mLODPeriod
        static constexpr S32 RESULT_AGE = 3;  // AlchemyRenderQueryResultAge's default

        U32  mHash         = 0;     // the frame of the period it is asked again on
        bool mHoldsCamera  = false; // its box holds the camera, so a query of it fails early
        bool mHidden       = false; // what a query of it answers
        bool mRefuseRetest = false; // the pool refuses its first retest once hidden, as it does while its target is held

        S32  mSeen      = 0;  // the group's last frame in view
        S32  mAnswerAt  = -1; // the frame its query's answer is read from, -1 with none out
        S32  mAsked     = 0;  // the frame its last query went out
        S32  mLastAsk   = -1; // the last frame a query of it was asked for, gone out or refused
        S32  mRefusedAt = -1; // the frame the pool refused its retest, -1 for none
        bool mOccluded  = false;
        S32  mMadeAt    = -1; // the first front cull that added it, -1 for none

        // One front cull at `frame`, `last_front_cull` the one before it.
        void cull(S32 frame, S32 last_front_cull)
        {
            const bool needs_update = (U32)frame % PERIOD == mHash;

            // earlyFail reads an answer come back, then culls a hidden group
            // that is not due to be asked again and does not hold the camera
            if (mAnswerAt >= 0 && frame >= mAnswerAt)
            {
                mAnswerAt = -1;
                mOccluded = mHidden;
                if (!mHidden)
                {
                    mSeen = frame;
                }
            }
            const bool retest_due = mAnswerAt < 0 && frame - mAsked >= (S32)PERIOD;
            if (!needs_update && mOccluded && LLVOCachePartition::culledAsHidden(retest_due, mHoldsCamera))
            {
                return;
            }

            LLVOCachePartition::GroupCullFacts facts;
            facts.mCurrentFrame   = frame;
            facts.mLastFrontCull  = last_front_cull;
            facts.mGroupSeenFrame = mSeen;
            facts.mPeriod         = PERIOD;
            facts.mNeedsUpdate    = needs_update;
            facts.mHoldsCamera    = mHoldsCamera;
            facts.mAnswerAwaited  = mAnswerAt >= 0;
            facts.mOccluded       = mOccluded;
            facts.mRetestDue      = retest_due;
            const LLVOCachePartition::GroupCullAction action = LLVOCachePartition::cullGroup(facts);

            if (action.mAsk)
            {   // doOcclusion, later in the frame
                if (mHoldsCamera)
                {
                    mAnswerAt = -1;
                    mOccluded = false;
                }
                else if (mAnswerAt < 0)
                {
                    mLastAsk = frame;
                    if (mRefuseRetest && mOccluded && retest_due && mRefusedAt < 0)
                    {   // nothing is out, and the group keeps its state
                        mRefusedAt = frame;
                    }
                    else
                    {
                        mAnswerAt = frame + RESULT_AGE;
                        mAsked    = frame;
                    }
                }
            }
            mSeen = frame;
            if (action.mVisible && mMadeAt < 0)
            {
                mMadeAt = frame;
            }
        }
    };

    // The front culls of a quiet partition under a resting camera, from `first`
    // for `frames` frames.
    std::vector<S32> quiet_front_culls(U32 first, U32 frames, U32 idle_hash)
    {
        std::vector<S32> culls;
        for (U32 frame = first; frame < first + frames; ++frame)
        {
            if (LLVOCachePartition::quietFrontCullDue(frame, CacheGroupModel::PERIOD, idle_hash))
            {
                culls.push_back((S32)frame);
            }
        }
        return culls;
    }

    TEST_CASE("LLVOCachePartition: a quiet partition culls one frame in nine and two together as its hash wraps", "[LLVOCache]")
    {
        const std::vector<S32> culls = quiet_front_culls(6400, 128, 0);
        REQUIRE(culls.size() == 16u);
        size_t together = 0;
        for (size_t i = 1; i < culls.size(); ++i)
        {
            const S32 gap = culls[i] - culls[i - 1];
            CAPTURE(culls[i]);
            UNSCOPED_INFO("the culls fall a block and a frame apart, or together");
            CHECK((gap == 9 || gap == 1));
            together += gap == 1;
        }
        UNSCOPED_INFO("and together once in each cycle of the hash, 64 frames");
        CHECK(together == 2u);
    }

    TEST_CASE("LLVOCachePartition: a cache group in view is made while the camera rests whatever its hash", "[LLVOCache]")
    {
        // A resting camera over a group new to the view, for each frame of the
        // period the group can be asked again on and each phase of the cadence:
        // a group whose box holds the camera, which no query asks, and one
        // outside it that its query finds in view. Seen only within the last two
        // frames, each was asked again at every cull, nine frames apart, and made
        // only at the culls a frame apart as the hash wraps: the one holding the
        // camera waited for those, and never came in where its needsUpdate()
        // frame fell on the second of them.
        for (U32 phase = 0; phase < 64; ++phase)
        {
            const std::vector<S32> culls = quiet_front_culls(6400 + phase, 128, 0);
            for (U32 hash = 0; hash < CacheGroupModel::PERIOD; ++hash)
            {
                for (bool holds_camera : { true, false })
                {
                    CAPTURE(phase, hash, holds_camera);
                    CacheGroupModel group;
                    group.mHash        = hash;
                    group.mHoldsCamera = holds_camera;

                    S32 last = culls.front() - 9; // the cull before, which left the group out of view
                    for (S32 frame : culls)
                    {
                        group.cull(frame, last);
                        last = frame;
                    }

                    REQUIRE(group.mMadeAt >= 0);
                    if (holds_camera)
                    {
                        UNSCOPED_INFO("a group whose box holds the camera is made at the first cull that sees it");
                        CHECK(group.mMadeAt == culls[0]);
                    }
                    else
                    {
                        // asked at the first cull, its answer read at the first
                        // cull a result's age after it
                        const S32 answered = *std::find_if(culls.begin(), culls.end(), [&](S32 frame)
                                                           { return frame >= culls[0] + CacheGroupModel::RESULT_AGE; });
                        UNSCOPED_INFO("a group outside it is made at the first cull its answer is back by");
                        CHECK(group.mMadeAt == answered);
                    }
                }
            }
        }
    }

    TEST_CASE("LLVOCachePartition: a moving camera makes a cache group once its answer is read as before", "[LLVOCache]")
    {
        for (U32 hash = 0; hash < CacheGroupModel::PERIOD; ++hash)
        {
            CAPTURE(hash);
            CacheGroupModel group;
            group.mHash = hash;
            for (S32 frame = 6401; frame < 6401 + 64; ++frame) // a front cull every frame
            {
                group.cull(frame, frame - 1);
            }
            UNSCOPED_INFO("asked at the first cull and made as its answer is read");
            CHECK(group.mMadeAt == 6401 + CacheGroupModel::RESULT_AGE);
        }

        CacheGroupModel hidden;
        hidden.mHidden = true;
        for (S32 frame = 6401; frame < 6401 + 256; ++frame)
        {
            hidden.cull(frame, frame - 1);
        }
        UNSCOPED_INFO("a group its queries find hidden is never made");
        CHECK(hidden.mMadeAt < 0);
    }

    TEST_CASE("LLVOCachePartition: a hidden cache group is made at the first cull after the camera steps into its box", "[LLVOCache]")
    {
        // A query asked from outside found the group hidden, and the camera then
        // stepped into its box at one of the culls that followed: with that
        // answer read or still out, the group was culled as hidden until its
        // needsUpdate() frame or its retest, up to a period on, before
        // processGroup saw that its box holds the camera.
        auto made_at = [](const std::vector<S32>& culls, S32 cull_before, U32 hash, size_t step_in)
        {
            CacheGroupModel group;
            group.mHash   = hash;
            group.mHidden = true;
            S32 last      = cull_before;
            for (size_t i = 0; i < culls.size(); ++i)
            {
                if (i == step_in)
                {
                    REQUIRE(group.mMadeAt < 0);
                    group.mHoldsCamera = true;
                }
                group.cull(culls[i], last);
                last = culls[i];
            }
            return group.mMadeAt;
        };

        for (U32 phase = 0; phase < 64; ++phase)
        {
            const std::vector<S32> culls = quiet_front_culls(6400 + phase, 128, 0);
            for (U32 hash = 0; hash < CacheGroupModel::PERIOD; ++hash)
            {
                for (size_t step_in = 1; step_in < 8; ++step_in)
                {
                    CAPTURE(phase, hash, step_in);
                    UNSCOPED_INFO("while the camera rests");
                    CHECK(made_at(culls, culls.front() - 9, hash, step_in) == culls[step_in]);
                }
            }
        }

        std::vector<S32> every_frame;
        for (S32 frame = 6401; frame < 6401 + 64; ++frame)
        {
            every_frame.push_back(frame);
        }
        for (U32 hash = 0; hash < CacheGroupModel::PERIOD; ++hash)
        {
            for (size_t step_in = 1; step_in < 48; ++step_in)
            {
                CAPTURE(hash, step_in);
                UNSCOPED_INFO("and while it moves");
                CHECK(made_at(every_frame, every_frame.front() - 1, hash, step_in) == every_frame[step_in]);
            }
        }
    }

    TEST_CASE("LLVOCachePartition: a hidden cache group whose retest the query pool refuses is asked at the next cull", "[LLVOCache]")
    {
        // The pool refuses a query while its target is held, by the shader
        // profiler: the group keeps its state, hidden and due its retest, and
        // the cull that asked it saw it. Asked again then only on its
        // needsUpdate() frame, it stayed hidden while the camera rested wherever
        // that frame never falls on a front cull, half the hashes.
        for (U32 phase = 0; phase < 64; ++phase)
        {
            const std::vector<S32> culls = quiet_front_culls(6400 + phase, 256, 0);
            for (U32 hash = 0; hash < CacheGroupModel::PERIOD; ++hash)
            {
                CAPTURE(phase, hash);
                CacheGroupModel group;
                group.mHash         = hash;
                group.mHidden       = true;
                group.mRefuseRetest = true;

                S32 last          = culls.front() - 9;
                S32 after_refusal = -1;
                for (S32 frame : culls)
                {
                    const bool refused = group.mRefusedAt >= 0;
                    group.cull(frame, last);
                    last = frame;
                    if (refused && after_refusal < 0)
                    {
                        after_refusal = frame;
                        UNSCOPED_INFO("the cull after the refusal asks it again");
                        CHECK(group.mLastAsk == frame);
                    }
                }
                REQUIRE(after_refusal >= 0);
                UNSCOPED_INFO("and it is still never made");
                CHECK(group.mMadeAt < 0);
            }
        }
    }

    TEST_CASE("LLVOCachePartition: cullGroup asks a group new to the view and makes one seen at the last cull", "[LLVOCache]")
    {
        auto seen_last_cull = []()
        {
            LLVOCachePartition::GroupCullFacts facts;
            facts.mCurrentFrame   = 6409;
            facts.mLastFrontCull  = 6400;
            facts.mGroupSeenFrame = 6400;
            facts.mPeriod         = 16;
            return facts;
        };

        {
            const LLVOCachePartition::GroupCullAction action = LLVOCachePartition::cullGroup(seen_last_cull());
            UNSCOPED_INFO("a group seen at the last cull, nine frames back, is made and not asked");
            CHECK(action.mVisible);
            CHECK_FALSE(action.mAsk);
        }
        {
            LLVOCachePartition::GroupCullFacts facts = seen_last_cull();
            facts.mNeedsUpdate = true;
            const LLVOCachePartition::GroupCullAction action = LLVOCachePartition::cullGroup(facts);
            UNSCOPED_INFO("its needsUpdate() frame asks it again and still makes it");
            CHECK(action.mAsk);
            CHECK(action.mVisible);
        }
        {
            LLVOCachePartition::GroupCullFacts facts = seen_last_cull();
            facts.mAnswerAwaited = true;
            UNSCOPED_INFO("an answer awaited holds it back");
            CHECK_FALSE(LLVOCachePartition::cullGroup(facts).mVisible);
            facts.mHoldsCamera = true;
            UNSCOPED_INFO("unless its box holds the camera");
            CHECK(LLVOCachePartition::cullGroup(facts).mVisible);
        }
        {
            LLVOCachePartition::GroupCullFacts facts = seen_last_cull();
            facts.mOccluded = true;
            UNSCOPED_INFO("a group found hidden is not made");
            CHECK_FALSE(LLVOCachePartition::cullGroup(facts).mVisible);
            facts.mRetestDue = true;
            UNSCOPED_INFO("and one due its retest is asked again though it was seen");
            CHECK(LLVOCachePartition::cullGroup(facts).mAsk);
            facts.mOccluded = false;
            UNSCOPED_INFO("which a group in view is not");
            CHECK_FALSE(LLVOCachePartition::cullGroup(facts).mAsk);
        }
        {
            LLVOCachePartition::GroupCullFacts facts = seen_last_cull();
            facts.mGroupSeenFrame = 6399;
            const LLVOCachePartition::GroupCullAction action = LLVOCachePartition::cullGroup(facts);
            UNSCOPED_INFO("a group not seen at the last cull is new to the view: asked, not made");
            CHECK(action.mAsk);
            CHECK_FALSE(action.mVisible);
        }
        {
            LLVOCachePartition::GroupCullFacts facts = seen_last_cull();
            facts.mCurrentFrame = facts.mLastFrontCull + 17;
            const LLVOCachePartition::GroupCullAction action = LLVOCachePartition::cullGroup(facts);
            UNSCOPED_INFO("a last cull more than a period back counts for nothing");
            CHECK(action.mAsk);
            CHECK_FALSE(action.mVisible);
        }
        {
            LLVOCachePartition::GroupCullFacts facts = seen_last_cull();
            facts.mLastFrontCull  = 0;
            facts.mGroupSeenFrame = 0;
            const LLVOCachePartition::GroupCullAction action = LLVOCachePartition::cullGroup(facts);
            UNSCOPED_INFO("nor does the want of one before the first cull");
            CHECK(action.mAsk);
            CHECK_FALSE(action.mVisible);
        }
    }

    TEST_CASE("LLVOCachePartition: culledAsHidden passes over a hidden group until its retest unless it holds the camera", "[LLVOCache]")
    {
        CHECK(LLVOCachePartition::culledAsHidden(false, false));
        CHECK_FALSE(LLVOCachePartition::culledAsHidden(true, false));
        CHECK_FALSE(LLVOCachePartition::culledAsHidden(false, true));
        CHECK_FALSE(LLVOCachePartition::culledAsHidden(true, true));
    }
}
