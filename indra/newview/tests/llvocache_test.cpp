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
        static constexpr U32 CACHE_VERSION = 21;          // LLAppViewer::getObjectCacheVersion()
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

        static LLGLTFOverrideCacheEntry requireOverride(U32 local_id, U64 handle, int sides, const LLUUID& object_id)
        {
            LLGLTFOverrideCacheEntry entry;
            UNSCOPED_INFO("override parses");
            REQUIRE(entry.fromLLSD(makeOverrideLLSD(local_id, handle, object_id, sides)));
            return entry;
        }

        // An override for some object the cache does not hold.
        static LLGLTFOverrideCacheEntry requireOverride(U32 local_id, U64 handle, int sides)
        {
            return requireOverride(local_id, handle, sides, LLUUID::generateNewID());
        }

        // An override for the object an entry holds, named as the region names it.
        static LLGLTFOverrideCacheEntry requireOverrideOf(const LLVOCacheEntry& object, U64 handle, int sides)
        {
            return requireOverride(object.getLocalID(), handle, sides, object.getObjectID());
        }

        // An entry whose data opens with object_id, as an object update's does.
        static LLPointer<LLVOCacheEntry> makeObjectEntry(U32 local_id, U32 crc, const LLUUID& object_id)
        {
            std::vector<U8> body(object_id.mData, object_id.mData + UUID_BYTES);
            body.resize(UUID_BYTES + 24, (U8)local_id);
            LLDataPackerBinaryBuffer dp(body.data(), (S32)body.size());
            return new LLVOCacheEntry(local_id, crc, dp);
        }

        // No override, for the saves of the tests about the objects alone.
        static inline const LLVOCacheEntry::vocache_gltf_overrides_map_t NO_OVERRIDES{};
        // Where the reads of the tests about the objects alone put the overrides.
        LLVOCacheEntry::vocache_gltf_overrides_map_t mOverridesRead;

        // An entry's header in the file: its local ID, CRC, hit count and body size.
        static constexpr size_t ENTRY_HEADER_BYTES = 4 * sizeof(S32);

        // The bytes a file's cache ID, entry count and objects take, as writeToCache lays them out.
        static size_t objectBytes(const LLVOCacheEntry::vocache_entry_map_t& objects)
        {
            size_t size = UUID_BYTES + sizeof(S32);
            for (const auto& [local_id, entry] : objects)
            {
                size += ENTRY_HEADER_BYTES + (size_t)entry->getDP()->getBufferSize();
            }
            return size;
        }

        // An object cache file written by hand: the cache ID and the objects as writeToCache lays them out, then the
        // overrides' count and each override as its length and its bytes, whatever they are.
        void writeObjectFile(U64 handle, const LLUUID& id, const LLVOCacheEntry::vocache_entry_map_t& objects,
                             const std::vector<std::string>& records) const
        {
            std::string data(reinterpret_cast<const char*>(id.mData), UUID_BYTES);
            const S32 num_entries = (S32)objects.size();
            data.append(reinterpret_cast<const char*>(&num_entries), sizeof(S32));
            for (const auto& [local_id, entry] : objects)
            {
                std::vector<U8> bytes(ENTRY_HEADER_BYTES + (size_t)entry->getDP()->getBufferSize());
                entry->writeToBuffer(bytes.data());
                data.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            }
            const U32 count = (U32)records.size();
            data.append(reinterpret_cast<const char*>(&count), sizeof(U32));
            for (const std::string& record : records)
            {
                const U32 length = (U32)record.size();
                data.append(reinterpret_cast<const char*>(&length), sizeof(U32));
                data += record;
            }
            spit(objectFile(handle, ".slc"), data);
        }

        // How many overrides a region's file holds after its objects, read the way the file lays them out.
        U32 overrideRecordCount(U64 handle) const
        {
            const std::string bytes = slurp(objectFile(handle, ".slc"));
            size_t cursor = UUID_BYTES;
            S32 num_entries = 0;
            REQUIRE(cursor + sizeof(S32) <= bytes.size());
            memcpy(&num_entries, bytes.data() + cursor, sizeof(S32));
            cursor += sizeof(S32);
            for (S32 i = 0; i < num_entries; ++i)
            {
                S32 size = 0;
                REQUIRE(cursor + ENTRY_HEADER_BYTES <= bytes.size());
                memcpy(&size, bytes.data() + cursor + 3 * sizeof(S32), sizeof(S32));
                cursor += ENTRY_HEADER_BYTES + (size_t)size;
            }
            U32 count = 0;
            REQUIRE(cursor + sizeof(U32) <= bytes.size());
            memcpy(&count, bytes.data() + cursor, sizeof(U32));
            return count;
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

        LLVOCache::instance().writeToCache(handle, id, written, NO_OVERRIDES, /*dirty=*/true, /*removal_enabled=*/false);
        UNSCOPED_INFO("cache file written");
        CHECK(std::filesystem::exists(objectFile(handle, ".slc")));

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("read succeeds");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read, mOverridesRead));
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
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, LLUUID::generateNewID(), other, mOverridesRead));
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

        LLVOCache::instance().writeToCache(handle, id, written, NO_OVERRIDES, true, /*removal_enabled=*/true);

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("read succeeds against the written count");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read, mOverridesRead));
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
        LLVOCache::instance().writeToCache(handle, id, written, NO_OVERRIDES, true, false);

        const std::filesystem::path file = objectFile(handle, ".slc");
        const uintmax_t size = std::filesystem::file_size(file);
        std::filesystem::resize_file(file, size - 100);

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("truncated file fails the read");
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, id, read, mOverridesRead));
        UNSCOPED_INFO("and keeps none of its objects, as the overrides after them are lost");
        CHECK(read.empty());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: the header survives a restart", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1003 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(50);
        LLVOCache::instance().writeToCache(handle, id, written, NO_OVERRIDES, true, false);

        LLVOCache::deleteSingleton();
        LLVOCache::initParamSingleton(false);
        LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);

        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("region known after restart");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read, mOverridesRead));
        UNSCOPED_INFO("entries after restart");
        CHECK(read.size() == written.size());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: overrides round-trip in the object file and one with no object is dropped", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1004 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(20);

        LLVOCacheEntry::vocache_gltf_overrides_map_t written;
        for (U32 local_id = 1; local_id <= 20; local_id += 3)
        {
            written[local_id] = requireOverrideOf(*objects[local_id], handle, 1 + (int)(local_id % 4));
        }
        // An override for an object the file does not hold is neither written nor loaded.
        written[999] = requireOverride(999, handle, 2);

        LLVOCache::instance().writeToCache(handle, id, objects, written, true, false);
        UNSCOPED_INFO("one file holds the objects and their overrides");
        CHECK(std::filesystem::exists(objectFile(handle, ".slc")));
        CHECK_FALSE(std::filesystem::exists(objectFile(handle, "_extras.slec")));
        CHECK(overrideRecordCount(handle) == written.size() - 1);

        LLVOCacheEntry::vocache_entry_map_t read_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read_objects.size() == objects.size());
        UNSCOPED_INFO("all overrides with an object came back");
        CHECK(read.size() == written.size() - 1);
        CHECK(read.find(999) == read.end());
        for (const auto& [local_id, entry] : read)
        {
            CAPTURE(local_id);
            requireSameOverride(written.at(local_id), entry);
            UNSCOPED_INFO("materials not built by loading");
            REQUIRE(entry.mGLTFMaterial.empty());
        }

        UNSCOPED_INFO("another cache ID loads neither objects nor overrides");
        LLVOCacheEntry::vocache_entry_map_t other_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t other;
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, LLUUID::generateNewID(), other_objects, other));
        CHECK(other_objects.empty());
        CHECK(other.empty());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a corrupt override is skipped and a file cut short loads nothing", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1005 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(10);
        LLVOCache::instance().writeToCache(handle, id, objects, NO_OVERRIDES, true, false);

        std::vector<std::string> records;
        for (U32 local_id = 1; local_id <= 10; ++local_id)
        {
            records.push_back(binaryRecord(makeOverrideLLSD(local_id, handle, objects[local_id]->getObjectID(), 2)));
        }
        // A type tag LLSD binary does not have, so that the second record and only it fails to parse.
        records[1][0] = (char)0xFF;
        writeObjectFile(handle, id, objects, records);

        LLVOCacheEntry::vocache_entry_map_t read_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        UNSCOPED_INFO("a corrupt override does not fail the file");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read_objects.size() == objects.size());
        CHECK(read.size() == records.size() - 1);
        CHECK(read.count(2) == 0u);

        const std::string bytes = slurp(objectFile(handle, ".slc"));
        UNSCOPED_INFO("a file cut short in its overrides loads neither its objects nor its overrides");
        spit(objectFile(handle, ".slc"), bytes.substr(0, bytes.size() - 3));
        read_objects.clear();
        read.clear();
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read_objects.empty());
        CHECK(read.empty());

        UNSCOPED_INFO("nor one with bytes past its overrides");
        spit(objectFile(handle, ".slc"), bytes + std::string("\x01\x02", 2));
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read_objects.empty());

        UNSCOPED_INFO("nor one that ends at its objects, as a file did before the overrides were kept with them");
        spit(objectFile(handle, ".slc"), bytes.substr(0, objectBytes(objects)));
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read_objects.empty());
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

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: hostile override records meet a depth cap and a budget and lose bad faces", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1006 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(4);
        LLVOCache::instance().writeToCache(handle, id, objects, NO_OVERRIDES, true, false);

        // a legitimate record carrying something nested a little loads; one
        // nested past the cap is refused rather than parsed
        LLSD shallow = makeOverrideLLSD(1, handle, objects[1]->getObjectID(), 1);
        shallow["deep"] = nestedArrays(8);
        LLSD deep = makeOverrideLLSD(2, handle, objects[2]->getObjectID(), 1);
        deep["deep"] = nestedArrays(24);
        writeObjectFile(handle, id, objects, { binaryRecord(shallow), binaryRecord(deep) });

        LLVOCacheEntry::vocache_entry_map_t read_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        UNSCOPED_INFO("a file with one bad record is still a cache");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, overrides));
        UNSCOPED_INFO("the shallow record loaded, the deep one did not");
        CHECK(overrides.size() == 1u);
        UNSCOPED_INFO("it is the shallow one");
        CHECK(overrides.find(1) != overrides.end());

        // up to the budget, unreadable records are skipped; past it the file is refused
        std::vector<std::string> garbage(16, std::string("\xff\xfe\xfd\xfc", 4));
        garbage.push_back(binaryRecord(makeOverrideLLSD(3, handle, objects[3]->getObjectID(), 1)));
        writeObjectFile(handle, id, objects, garbage);
        read_objects.clear();
        overrides.clear();
        UNSCOPED_INFO("sixteen unreadable records are within the budget");
        CHECK(LLVOCache::instance().readFromCache(handle, id, read_objects, overrides));
        UNSCOPED_INFO("and the good record after them loaded");
        CHECK(overrides.size() == 1u);

        garbage.insert(garbage.begin(), std::string("\xff\xfe\xfd\xfc", 4));
        writeObjectFile(handle, id, objects, garbage);
        read_objects.clear();
        overrides.clear();
        UNSCOPED_INFO("seventeen is a bad file, and its objects go with it");
        CHECK_FALSE(LLVOCache::instance().readFromCache(handle, id, read_objects, overrides));
        CHECK(read_objects.empty());

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
        LLVOCache::instance().writeToCache(first, id, makeEntries(3), NO_OVERRIDES, true, false);
        LLVOCache::instance().writeToCache(second, id, makeEntries(3), NO_OVERRIDES, true, false);
        LLVOCache::instance().writeToCache(third, id, makeEntries(3), NO_OVERRIDES, true, false);

        // removing the middle region leaves a hole in the header on disk; a
        // crash means nobody closes it up
        LLVOCache::instance().removeEntry(second);
        restartAsAfterCrash();
        UNSCOPED_INFO("two regions come back");
        CHECK(LLVOCache::instance().getCacheEntries() == 2u);

        // a new region has to take the hole, not the slot of the one behind it
        LLVOCache::instance().writeToCache(fourth, id, makeEntries(3), NO_OVERRIDES, true, false);
        restartAsAfterCrash();
        UNSCOPED_INFO("three regions come back");
        CHECK(LLVOCache::instance().getCacheEntries() == 3u);
        LLVOCacheEntry::vocache_entry_map_t read;
        UNSCOPED_INFO("the first region is still there");
        CHECK(LLVOCache::instance().readFromCache(first, id, read, mOverridesRead));
        read.clear();
        UNSCOPED_INFO("the third region was not overwritten");
        CHECK(LLVOCache::instance().readFromCache(third, id, read, mOverridesRead));
        read.clear();
        UNSCOPED_INFO("the new region is there");
        CHECK(LLVOCache::instance().readFromCache(fourth, id, read, mOverridesRead));
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a save with nothing dirty leaves the file as it was", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1020 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(4);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1] = requireOverrideOf(*objects[1], handle, 2);
        overrides[2] = requireOverrideOf(*objects[2], handle, 3);
        LLVOCache::instance().writeToCache(handle, id, objects, overrides, true, false);

        // whatever it is handed
        LLVOCacheEntry::vocache_gltf_overrides_map_t nothing;
        LLVOCache::instance().writeToCache(handle, id, makeEntries(2), nothing, /*dirty=*/false, false);

        LLVOCacheEntry::vocache_entry_map_t read_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        UNSCOPED_INFO("the file still reads");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        UNSCOPED_INFO("and still holds every object and both overrides");
        CHECK(read_objects.size() == objects.size());
        REQUIRE(read.size() == 2u);
        requireSameOverride(overrides[1], read[1]);
        requireSameOverride(overrides[2], read[2]);
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a region that never had an override keeps its objects into the next login", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1021 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(30);
        LLVOCache::instance().writeToCache(handle, id, written, NO_OVERRIDES, /*dirty=*/true, false);
        UNSCOPED_INFO("the file holds no override");
        CHECK(overrideRecordCount(handle) == 0u);

        LLVOCache::deleteSingleton();
        LLVOCache::initParamSingleton(false);
        LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);

        LLVOCacheEntry::vocache_entry_map_t read;
        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        UNSCOPED_INFO("the next login reads the file");
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read, overrides));
        UNSCOPED_INFO("with every object in it and no override");
        CHECK(read.size() == written.size());
        CHECK(overrides.empty());
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: an object write that fails leaves no file behind", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1022 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();

        // A body past the cache's ten-thousand-byte cap on one entry fails the write, which drops the region from the
        // header, overrides and all.
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(3);
        objects[4] = makeEntry(4, 4, 10001);
        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1] = requireOverrideOf(*objects[1], handle, 2);
        LLVOCache::instance().writeToCache(handle, id, objects, overrides, /*dirty=*/true, false);
        UNSCOPED_INFO("the failed write dropped the region");
        CHECK(LLVOCache::instance().getCacheEntries() == 0u);
        UNSCOPED_INFO("and left no file");
        CHECK_FALSE(std::filesystem::exists(objectFile(handle, ".slc")));
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: an override is written only beside the object it belongs to", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1024 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(4);
        // Not confirmed by the region this session: the file keeps it only while removal is off.
        objects[3]->setValid(false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        for (U32 local_id : { 1u, 3u })
        {
            overrides[local_id] = requireOverrideOf(*objects[local_id], handle, 2);
        }
        overrides[9] = requireOverride(9, handle, 2);
        // the region removed this object's overrides
        overrides[2].mLocalId = 2;

        LLVOCache::instance().writeToCache(handle, id, objects, overrides, true, /*removal_enabled=*/true);
        UNSCOPED_INFO("with removal on, the override of the confirmed object alone");
        CHECK(overrideRecordCount(handle) == 1u);
        LLVOCacheEntry::vocache_entry_map_t read_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read.size() == 1u);
        CHECK(read.count(1) == 1u);

        LLVOCache::instance().writeToCache(handle, id, objects, overrides, true, /*removal_enabled=*/false);
        UNSCOPED_INFO("with removal off, every override whose object is written");
        CHECK(overrideRecordCount(handle) == 2u);
        read_objects.clear();
        read.clear();
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read.size() == 2u);
        CHECK(read.count(1) == 1u);
        CHECK(read.count(3) == 1u);
        UNSCOPED_INFO("never one without its object, nor a removal");
        CHECK(read.count(9) == 0u);
        CHECK(read.count(2) == 0u);
    }

    TEST_CASE("LLGLTFOverrideCacheEntry: a face takes the region's override for it or none", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1025 * 256, 1000 * 256);
        LLSD data = vocacheTest::makeOverrideLLSD(7, handle, LLUUID::generateNewID(), 2);
        data["sides"][1] = 2; // faces 0 and 2
        LLGLTFOverrideCacheEntry entry;
        REQUIRE(entry.fromLLSD(data));
        entry.materialize();

        CHECK(entry.overrideFor(0, LLTEContents::MAX_TES) != nullptr);
        CHECK(entry.overrideFor(2, LLTEContents::MAX_TES) != nullptr);
        UNSCOPED_INFO("each face its own");
        CHECK(entry.overrideFor(0, LLTEContents::MAX_TES) != entry.overrideFor(2, LLTEContents::MAX_TES));
        CHECK(entry.overrideFor(2, LLTEContents::MAX_TES) == entry.mGLTFMaterial.at(2).get());
        UNSCOPED_INFO("a face the region left out takes none");
        CHECK(entry.overrideFor(1, LLTEContents::MAX_TES) == nullptr);
        CHECK(entry.overrideFor(LLTEContents::MAX_TES - 1, LLTEContents::MAX_TES) == nullptr);

        UNSCOPED_INFO("an entry with no sides gives every face none");
        LLGLTFOverrideCacheEntry removed;
        removed.mLocalId = 7;
        removed.materialize();
        CHECK(removed.overrideFor(0, LLTEContents::MAX_TES) == nullptr);
    }

    TEST_CASE("LLGLTFOverrideCacheEntry: a face past the faces the region counts takes the last counted face's", "[LLVOCache]")
    {
        // A mesh whose asset split one material into three faces, on an object the region counts one face on
        const U64 handle = to_region_handle(1026 * 256, 1000 * 256);
        LLGLTFOverrideCacheEntry one;
        REQUIRE(one.fromLLSD(vocacheTest::makeOverrideLLSD(7, handle, LLUUID::generateNewID(), 1))); // face 0
        one.materialize();
        REQUIRE(one.overrideFor(0, 1) != nullptr);
        CHECK(one.overrideFor(1, 1) == one.overrideFor(0, 1));
        CHECK(one.overrideFor(2, 1) == one.overrideFor(0, 1));
        UNSCOPED_INFO("a face the region counts and left out still takes none");
        CHECK(one.overrideFor(1, 3) == nullptr);
        UNSCOPED_INFO("an object the region counts no faces on takes nothing past them");
        CHECK(one.overrideFor(1, 0) == nullptr);

        LLSD data = vocacheTest::makeOverrideLLSD(8, handle, LLUUID::generateNewID(), 2);
        data["sides"][1] = 2; // faces 0 and 2
        LLGLTFOverrideCacheEntry two;
        REQUIRE(two.fromLLSD(data));
        two.materialize();
        UNSCOPED_INFO("the last counted face's, whether or not the region sent one for the face before it");
        CHECK(two.overrideFor(3, 3) == two.overrideFor(2, 3));
        CHECK(two.overrideFor(1, 3) == nullptr);
        UNSCOPED_INFO("a face past them the region sent one for keeps its own");
        CHECK(two.overrideFor(2, 1) == two.mGLTFMaterial.at(2).get());
        CHECK(two.overrideFor(1, 1) == two.overrideFor(0, 1));
        UNSCOPED_INFO("the entry names the faces the region sent an override for, and no face that takes another's");
        CHECK(two.names(0));
        CHECK(two.names(2));
        CHECK_FALSE(two.names(1));
        CHECK_FALSE(two.names(3));

        using EFaceState = ALGLTFOverrideFaceState;
        LLPointer<LLGLTFMaterial> same = new LLGLTFMaterial(*one.overrideFor(0, 1));
        UNSCOPED_INFO("a face past them showing the last counted face's override matches, and none differs");
        CHECK(LLGLTFOverrideCacheEntry::faceState(&one, 2, 1, same, 0.0) == EFaceState::MATCHES);
        CHECK(LLGLTFOverrideCacheEntry::faceState(&one, 2, 1, nullptr, 0.0) == EFaceState::DIFFERS);
    }

    TEST_CASE("LLGLTFOverrideCacheEntry: a face showing the override by value takes no change", "[LLVOCache]")
    {
        LLPointer<LLGLTFMaterial> held = new LLGLTFMaterial();
        held->mRoughnessFactor = 0.5f;
        LLPointer<LLGLTFMaterial> copy = new LLGLTFMaterial(*held);
        LLPointer<LLGLTFMaterial> other = new LLGLTFMaterial(*held);
        other->mRoughnessFactor = 0.25f;

        CHECK(LLGLTFOverrideCacheEntry::sameOverride(nullptr, nullptr));
        CHECK(LLGLTFOverrideCacheEntry::sameOverride(held, held));
        UNSCOPED_INFO("a copy is the same override");
        CHECK(LLGLTFOverrideCacheEntry::sameOverride(held, copy));
        CHECK_FALSE(LLGLTFOverrideCacheEntry::sameOverride(held, other));
        UNSCOPED_INFO("an override and none differ either way");
        CHECK_FALSE(LLGLTFOverrideCacheEntry::sameOverride(held, nullptr));
        CHECK_FALSE(LLGLTFOverrideCacheEntry::sameOverride(nullptr, held));
    }

    TEST_CASE("LLGLTFOverrideCacheEntry: a hold keeps the edited face until the region answers or its time is up", "[LLVOCache]")
    {
        constexpr F64 HOLD = LLGLTFOverrideCacheEntry::HOLD_SECONDS;
        LLGLTFOverrideCacheEntry entry;
        CHECK_FALSE(entry.isHeld(2, 0.0));
        entry.hold(2, 100.0);
        CHECK(entry.isHeld(2, 100.0));
        CHECK(entry.isHeld(2, 100.0 + HOLD - 0.5));
        CHECK_FALSE(entry.isHeld(2, 100.0 + HOLD));
        UNSCOPED_INFO("only the edited face");
        CHECK_FALSE(entry.isHeld(0, 100.0));
        CHECK_FALSE(entry.isHeld(3, 100.0));

        UNSCOPED_INFO("a face edited while another is held is held with it for as long");
        entry.hold(0, 105.0);
        CHECK(entry.isHeld(0, 105.0 + HOLD - 0.5));
        CHECK(entry.isHeld(2, 105.0 + HOLD - 0.5));

        UNSCOPED_INFO("a hold that ran out lets its faces go at the next one");
        entry.hold(5, 300.0);
        CHECK(entry.isHeld(5, 300.0));
        CHECK_FALSE(entry.isHeld(0, 300.0));
        CHECK_FALSE(entry.isHeld(2, 300.0));

        UNSCOPED_INFO("no face outside an object's range is held");
        entry.hold(-1, 300.0);
        entry.hold(LLTEContents::MAX_TES, 300.0);
        CHECK_FALSE(entry.isHeld(-1, 300.0));
        CHECK_FALSE(entry.isHeld(LLTEContents::MAX_TES, 300.0));

        UNSCOPED_INFO("the region's answer replaces the entry and the hold with it");
        entry = LLGLTFOverrideCacheEntry();
        CHECK_FALSE(entry.isHeld(5, 300.0));
    }

    TEST_CASE("LLGLTFOverrideCacheEntry: a face stands as unsent or held or matching or differing", "[LLVOCache]")
    {
        using EFaceState = ALGLTFOverrideFaceState;
        const U64 handle = to_region_handle(1027 * 256, 1000 * 256);
        LLGLTFOverrideCacheEntry entry;
        REQUIRE(entry.fromLLSD(vocacheTest::makeOverrideLLSD(7, handle, LLUUID::generateNewID(), 1))); // face 0
        entry.materialize();
        LLPointer<LLGLTFMaterial> same = new LLGLTFMaterial(*entry.overrideFor(0, LLTEContents::MAX_TES));
        LLPointer<LLGLTFMaterial> other = new LLGLTFMaterial(*same);
        other->mRoughnessFactor = 0.75f;

        UNSCOPED_INFO("the region sent nothing for the object");
        CHECK(LLGLTFOverrideCacheEntry::faceState(nullptr, 0, LLTEContents::MAX_TES, same, 0.0) == EFaceState::UNSENT);
        CHECK(LLGLTFOverrideCacheEntry::faceState(nullptr, 0, LLTEContents::MAX_TES, nullptr, 0.0) == EFaceState::UNSENT);

        UNSCOPED_INFO("a face showing the region's override, or none where it sent none");
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 0, LLTEContents::MAX_TES, same, 0.0) == EFaceState::MATCHES);
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 1, LLTEContents::MAX_TES, nullptr, 0.0) == EFaceState::MATCHES);

        UNSCOPED_INFO("a face showing another override, none in place of the region's, or one where it sent none");
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 0, LLTEContents::MAX_TES, other, 0.0) == EFaceState::DIFFERS);
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 0, LLTEContents::MAX_TES, nullptr, 0.0) == EFaceState::DIFFERS);
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 1, LLTEContents::MAX_TES, same, 0.0) == EFaceState::DIFFERS);

        UNSCOPED_INFO("a face waiting on the region's answer to an edit, whatever it shows");
        entry.hold(0, 100.0);
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 0, LLTEContents::MAX_TES, other, 100.0) == EFaceState::HELD);
        CHECK(LLGLTFOverrideCacheEntry::faceState(&entry, 1, LLTEContents::MAX_TES, same, 100.0) == EFaceState::DIFFERS);
    }

    TEST_CASE("ALGLTFOverrideCounts: a region with nothing counted logs nothing", "[LLVOCache]")
    {
        ALGLTFOverrideCounts counts;
        CHECK_FALSE(counts.any());
        CHECK(counts.summary().find("0 messages") == 0u);

        for (U32 ALGLTFOverrideCounts::*count : { &ALGLTFOverrideCounts::mMessages, &ALGLTFOverrideCounts::mFacesSet,
                                                  &ALGLTFOverrideCounts::mFacesDeferred, &ALGLTFOverrideCounts::mFacesReconciled,
                                                  &ALGLTFOverrideCounts::mKeptOnUpdate, &ALGLTFOverrideCounts::mTakenWithObject,
                                                  &ALGLTFOverrideCounts::mDroppedAsOther, &ALGLTFOverrideCounts::mAskedAgain })
        {
            ALGLTFOverrideCounts one;
            ++(one.*count);
            UNSCOPED_INFO("any one count is something to log");
            CHECK(one.any());
        }
    }

    TEST_CASE_METHOD(vocacheTest, "LLVOCache: an override is kept only for the object its local ID holds", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1026 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        const LLUUID first("11111111-1111-1111-1111-111111111111");
        const LLUUID second("22222222-2222-2222-2222-222222222222");
        const LLUUID third("33333333-3333-3333-3333-333333333333");
        const LLUUID gone("44444444-4444-4444-4444-444444444444");

        LLVOCacheEntry::vocache_entry_map_t objects;
        objects[1] = makeObjectEntry(1, 1, first);
        objects[2] = makeObjectEntry(2, 2, second);
        objects[3] = makeObjectEntry(3, 3, third);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1] = requireOverride(1, handle, 1, first);
        // for an object that had local ID 2 before the one the cache holds
        overrides[2] = requireOverride(2, handle, 1, gone);
        // sent before the object's update came to light
        overrides[3] = requireOverride(3, handle, 1, LLUUID::null);

        LLVOCache::instance().writeToCache(handle, id, objects, overrides, true, false);
        UNSCOPED_INFO("the writer leaves out the other object's override");
        CHECK(overrideRecordCount(handle) == 2u);
        LLVOCacheEntry::vocache_entry_map_t read_objects;
        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        CHECK(read.size() == 2u);
        CHECK(read.count(2) == 0u);
        REQUIRE(read.count(1) == 1u);
        CHECK(read[1].mObjectId == first);
        UNSCOPED_INFO("and names the unnamed one for the object it is written beside");
        REQUIRE(read.count(3) == 1u);
        CHECK(read[3].mObjectId == third);

        // a file not written that way: the reader holds each record to its object itself
        writeObjectFile(handle, id, objects, { binaryRecord(makeOverrideLLSD(1, handle, first, 1)),
                                               binaryRecord(makeOverrideLLSD(2, handle, gone, 1)),
                                               binaryRecord(makeOverrideLLSD(3, handle, LLUUID::null, 1)) });
        read_objects.clear();
        read.clear();
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, read));
        UNSCOPED_INFO("the reader drops the other object's override");
        CHECK(read.size() == 2u);
        CHECK(read.count(2) == 0u);
        UNSCOPED_INFO("and names the unnamed one for the object the file holds");
        REQUIRE(read.count(3) == 1u);
        CHECK(read[3].mObjectId == third);

        UNSCOPED_INFO("an override the region sent since the load began is the newer and stays");
        read_objects.clear();
        LLVOCacheEntry::vocache_gltf_overrides_map_t newer;
        newer[1] = requireOverride(1, handle, 3, first);
        REQUIRE(LLVOCache::instance().readFromCache(handle, id, read_objects, newer));
        CHECK(newer[1].mSides.size() == 3u);
    }

    TEST_CASE("LLVOCacheEntry: an entry killed for an update keeps its object's overrides and one killed as gone takes them", "[LLVOCache]")
    {
        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1].mLocalId = 1;

        UNSCOPED_INFO("the object stays in the world, made from the region's update");
        CHECK_FALSE(LLVOCacheEntry::killEntryOverrides(1, /*object_stays=*/true, overrides));
        CHECK(overrides.count(1) == 1u);

        UNSCOPED_INFO("the object is gone");
        CHECK(LLVOCacheEntry::killEntryOverrides(1, /*object_stays=*/false, overrides));
        CHECK(overrides.empty());

        UNSCOPED_INFO("a local ID with none has none to take");
        CHECK_FALSE(LLVOCacheEntry::killEntryOverrides(1, /*object_stays=*/false, overrides));
    }

    TEST_CASE("LLVOCacheEntry: overrides kept under a local ID go once it holds another object", "[LLVOCache]")
    {
        const LLUUID object_id("55555555-5555-5555-5555-555555555555");
        const LLUUID other_id("66666666-6666-6666-6666-666666666666");
        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1].mObjectId = object_id;
        overrides[2].mObjectId.setNull();

        UNSCOPED_INFO("the same object keeps them");
        CHECK_FALSE(LLVOCacheEntry::dropOverridesOfAnother(1, object_id, overrides));
        CHECK(overrides.count(1) == 1u);
        UNSCOPED_INFO("so does an override naming no object");
        CHECK_FALSE(LLVOCacheEntry::dropOverridesOfAnother(2, other_id, overrides));
        CHECK(overrides.count(2) == 1u);
        UNSCOPED_INFO("another object does not");
        CHECK(LLVOCacheEntry::dropOverridesOfAnother(1, other_id, overrides));
        CHECK(overrides.count(1) == 0u);
        UNSCOPED_INFO("and a local ID with none drops nothing");
        CHECK_FALSE(LLVOCacheEntry::dropOverridesOfAnother(9, other_id, overrides));
    }

    // An update that names its object drops the overrides another object left under its local ID, whether the cache
    // takes it or not (LLViewerObjectList::processObjectUpdate).
    TEST_CASE("LLViewerObjectList: a full update names its object by UUID and a terse or cached one does not", "[LLVOCache]")
    {
        UNSCOPED_INFO("an update sent uncompressed");
        CHECK(LLViewerObjectList::updateNamesObject(OUT_FULL));
        UNSCOPED_INFO("a compressed one, a temporary object's among them, which the cache does not take");
        CHECK(LLViewerObjectList::updateNamesObject(OUT_FULL_COMPRESSED));
        UNSCOPED_INFO("a terse update finds its object from the local ID");
        CHECK_FALSE(LLViewerObjectList::updateNamesObject(OUT_TERSE_IMPROVED));
        UNSCOPED_INFO("as does the region's word that it holds an object cached");
        CHECK_FALSE(LLViewerObjectList::updateNamesObject(OUT_FULL_CACHED));
        CHECK_FALSE(LLViewerObjectList::updateNamesObject(OUT_UNKNOWN));
    }

    // Removing a region's entry lets go of its objects in memory, and an
    // object update that finds the cache bogus is read from one of them.
    TEST_CASE_METHOD(vocacheTest, "LLVOCache: a removal asked for waits for the removals asked for to be carried out", "[LLVOCache]")
    {
        const U64 handle = to_region_handle(1023 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCache::instance().writeToCache(handle, id, makeEntries(3), NO_OVERRIDES, true, false);
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
        LLVOCache::instance().writeToCache(handle, id, makeEntries(3), NO_OVERRIDES, true, false);
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
        CHECK(entry->getObjectID() == object_id);

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
        CHECK(short_entry->getObjectID().isNull());
        LLPointer<LLVOCacheEntry> empty_entry = new LLVOCacheEntry();
        CHECK_FALSE(empty_entry->isObject(LLUUID::null));
        CHECK(empty_entry->getObjectID().isNull());
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

        constexpr U32 REFUSED = 5;
        entries[REFUSED] = vocacheTest::makeEntry(REFUSED, 5, 16);
        entries[REFUSED]->refuseUpdate();
        entries[REFUSED]->setValid(false);
        UNSCOPED_INFO("an entry invalid for a refused update holds its children, and is killed with them");
        CHECK_FALSE(LLVOCacheEntry::killLocalID(REFUSED, entries, overrides, kill_entry));
        REQUIRE(killed.size() == 1u);
        CHECK(killed[0] == entries[REFUSED].get());
    }

    TEST_CASE("LLVOCacheEntry: a refused update lets the region's replace the bytes once and then waits for a change", "[LLVOCache]")
    {
        constexpr U32 CRC = 0x1234u;
        LLPointer<LLVOCacheEntry> entry = vocacheTest::makeEntry(7, CRC, 16);
        CHECK_FALSE(entry->isRefused(CRC));
        CHECK_FALSE(entry->wasRefused());

        entry->refuseUpdate();
        UNSCOPED_INFO("the first refusal sets the entry's CRC apart from the one the region sends it under");
        CHECK(entry->getCRC() != CRC);
        CHECK_FALSE(entry->isRefused(CRC));
        CHECK(entry->wasRefused());
        entry->setValid(false);
        UNSCOPED_INFO("and a kill still reaches the entry, invalid until the region sends its object again");
        CHECK(entry->isKillable());
        entry->setValid();

        std::vector<U8> body(32, 0xCD);
        LLDataPackerBinaryBuffer sent(body.data(), (S32)body.size());
        entry->updateEntry(CRC, sent);
        UNSCOPED_INFO("so the region's update replaces the bytes refused");
        CHECK(entry->getCRC() == CRC);
        REQUIRE(entry->getDP());
        CHECK(entry->getDP()->getBufferSize() == 32);

        entry->refuseUpdate();
        UNSCOPED_INFO("a refusal of the bytes the region sent leaves the entry refused at their CRC");
        CHECK(entry->getCRC() == CRC);
        CHECK(entry->isRefused(CRC));
        CHECK_FALSE(entry->isRefused(CRC + 1));

        entry->refuseUpdate();
        UNSCOPED_INFO("and so does another");
        CHECK(entry->getCRC() == CRC);
        CHECK(entry->isRefused(CRC));

        entry->updateEntry(CRC + 1, sent);
        UNSCOPED_INFO("an object changed starts over");
        CHECK_FALSE(entry->isRefused(CRC));
        CHECK_FALSE(entry->isRefused(CRC + 1));
        entry->refuseUpdate();
        CHECK(entry->getCRC() != CRC + 1);
        CHECK_FALSE(entry->isRefused(CRC + 1));

        entry->refuseUpdate();
        entry->setValid(false);
        entry->forgetRefusal();
        UNSCOPED_INFO("a kill lets the refusal go, and the entry with it");
        CHECK_FALSE(entry->wasRefused());
        CHECK_FALSE(entry->isKillable());
        CHECK_FALSE(entry->isRefused(entry->getCRC()));
    }

    TEST_CASE("LLVOCacheEntry: a child whose parent has no valid entry waits among the orphans unless its parent is made", "[LLVOCache]")
    {
        using EChildPlace = LLVOCacheEntry::EChildPlace;
        constexpr U32 PARENT = 10;
        constexpr U32 OTHER_PARENT = 11;

        LLPointer<LLVOCacheEntry> child = vocacheTest::makeEntry(20, 1, 16);
        CHECK(child->takeParent(PARENT, nullptr, false) == EChildPlace::ORPHAN);
        CHECK(child->getParentID() == PARENT);

        UNSCOPED_INFO("decoded again naming the same parent, as an entry an update or a probe makes valid again is");
        CHECK(child->takeParent(PARENT, nullptr, false) == EChildPlace::ORPHAN);
        CHECK(child->takeParent(PARENT, nullptr, true) == EChildPlace::MADE);

        UNSCOPED_INFO("naming another parent, which is made");
        CHECK(child->takeParent(OTHER_PARENT, nullptr, true) == EChildPlace::MADE);
        CHECK(child->getParentID() == OTHER_PARENT);

        UNSCOPED_INFO("and naming none");
        CHECK(child->takeParent(0, nullptr, false) == EChildPlace::ROOT);
        CHECK_FALSE(child->isChild());
    }

    TEST_CASE("LLVOCacheEntry: a child waits on its parent's entry until the parent is made from it", "[LLVOCache]")
    {
        using EChildPlace = LLVOCacheEntry::EChildPlace;
        constexpr U32 PARENT = 10;

        LLPointer<LLVOCacheEntry> parent = vocacheTest::makeEntry(PARENT, 1, 16);
        LLPointer<LLVOCacheEntry> child = vocacheTest::makeEntry(20, 2, 16);
        CHECK(child->takeParent(PARENT, parent, false) == EChildPlace::WITH_PARENT);

        UNSCOPED_INFO("queued to be made");
        parent->setState(LLVOCacheEntry::IN_QUEUE);
        CHECK(child->takeParent(PARENT, parent, false) == EChildPlace::WITH_PARENT);

        UNSCOPED_INFO("its object asked for, its drawable not yet made");
        parent->setState(LLVOCacheEntry::WAITING);
        CHECK(child->takeParent(PARENT, parent, false) == EChildPlace::WITH_PARENT);
        CHECK(child->takeParent(PARENT, parent, true) == EChildPlace::WITH_PARENT);

        UNSCOPED_INFO("and made");
        parent->setState(LLVOCacheEntry::ACTIVE);
        CHECK(child->takeParent(PARENT, parent, false) == EChildPlace::MADE);
    }

    TEST_CASE("LLVOCacheEntry: a child leaves its parent's entry where it holds its children, valid or refused", "[LLVOCache]")
    {
        constexpr U32 PARENT = 10;
        constexpr U32 NO_ENTRY = 11;

        LLVOCacheEntry::vocache_entry_map_t entries;
        entries[PARENT] = vocacheTest::makeEntry(PARENT, 1, 16);
        LLVOCacheEntry* const parent = entries[PARENT].get();
        LLPointer<LLVOCacheEntry> child = vocacheTest::makeEntry(20, 2, 16);

        child->setParentID(PARENT);
        CHECK(child->leaveParent(entries) == parent);
        CHECK_FALSE(child->isChild());

        UNSCOPED_INFO("a parent refused, invalid until the region sends its object again, holds its children");
        parent->refuseUpdate();
        parent->setValid(false);
        child->setParentID(PARENT);
        CHECK(child->leaveParent(entries) == parent);
        CHECK_FALSE(child->isChild());

        UNSCOPED_INFO("a killed one holds none, and the child goes on naming it");
        parent->forgetRefusal();
        child->setParentID(PARENT);
        CHECK(child->leaveParent(entries) == nullptr);
        CHECK(child->getParentID() == PARENT);

        UNSCOPED_INFO("nor does a parent with no entry");
        child->setParentID(NO_ENTRY);
        CHECK(child->leaveParent(entries) == nullptr);
        CHECK(child->getParentID() == NO_ENTRY);

        UNSCOPED_INFO("and a root has no parent to leave");
        child->setParentID(0);
        CHECK(child->leaveParent(entries) == nullptr);
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
            // that is not due to be asked again and does not hold the camera,
            // and takes one it lets through for hidden no longer
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
            const bool hidden_retest = mOccluded && retest_due;
            if (!needs_update && mOccluded)
            {
                if (LLVOCachePartition::culledAsHidden(retest_due, mHoldsCamera))
                {
                    return;
                }
                mOccluded = false;
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
                    if (mRefuseRetest && hidden_retest && mRefusedAt < 0)
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

    TEST_CASE("LLVOCachePartition: a hidden cache group whose retest the query pool refuses is made at the next cull", "[LLVOCache]")
    {
        // The pool refuses a query while its target is held, by the shader
        // profiler: nothing is out, and the group earlyFail let through for its
        // retest is hidden no longer, so the next cull makes it, as an answer
        // that expired counts for one in view, rather than wait for an answer
        // that is not coming.
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
                        UNSCOPED_INFO("the cull after the refusal makes it");
                        CHECK(group.mMadeAt == frame);
                    }
                }
                REQUIRE(after_refusal >= 0);
            }
        }
    }

    TEST_CASE("LLVOCachePartition: a hidden cache group taken for hidden no longer at its retest is still not made", "[LLVOCache]")
    {
        // earlyFail clears a hidden group's OCCLUDED as it lets it through to
        // be asked again, so that a group it never asks does not hide the
        // answers below it. Until its answer is back the group is not seen at
        // the last cull, so it is asked, and not made, whichever cadence the
        // cull keeps and wherever its needsUpdate() frame falls.
        for (U32 hash = 0; hash < CacheGroupModel::PERIOD; ++hash)
        {
            CAPTURE(hash);
            CacheGroupModel moving;
            moving.mHash   = hash;
            moving.mHidden = true;
            for (S32 frame = 6401; frame < 6401 + 256; ++frame)
            {
                moving.cull(frame, frame - 1);
            }
            UNSCOPED_INFO("under a moving camera");
            CHECK(moving.mMadeAt < 0);

            for (U32 phase = 0; phase < 64; ++phase)
            {
                CAPTURE(phase);
                const std::vector<S32> culls = quiet_front_culls(6400 + phase, 256, 0);
                CacheGroupModel resting;
                resting.mHash   = hash;
                resting.mHidden = true;
                S32 last = culls.front() - 9;
                for (S32 frame : culls)
                {
                    resting.cull(frame, last);
                    last = frame;
                }
                UNSCOPED_INFO("under a resting one");
                CHECK(resting.mMadeAt < 0);
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
