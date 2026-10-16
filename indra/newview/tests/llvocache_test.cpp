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
}
