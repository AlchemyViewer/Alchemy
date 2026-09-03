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
#include "../test/lltut.h"

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
// TUT
// -------------------------------------------------------------------------------------------
namespace tut
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
            ensure("cache dir redirected", gDirUtilp->setCacheDir(mRoot.string()));

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

        static void ensureSameEntry(const LLVOCacheEntry& expected, const LLVOCacheEntry& actual)
        {
            ensure_equals("crc", actual.getCRC(), expected.getCRC());
            ensure_equals("hit count", actual.getHitCount(), expected.getHitCount());
            const LLDataPackerBinaryBuffer* a = expected.getDP();
            const LLDataPackerBinaryBuffer* b = actual.getDP();
            ensure("body present", a && b);
            ensure_equals("body size", b->getBufferSize(), a->getBufferSize());
            ensure("body bytes", memcmp(a->getBuffer(), b->getBuffer(), (size_t)a->getBufferSize()) == 0);
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

        static LLGLTFOverrideCacheEntry makeOverride(U32 local_id, U64 handle, int sides)
        {
            LLGLTFOverrideCacheEntry entry;
            ensure("override parses", entry.fromLLSD(makeOverrideLLSD(local_id, handle, LLUUID::generateNewID(), sides)));
            return entry;
        }

        static void ensureSameOverride(const LLGLTFOverrideCacheEntry& expected, const LLGLTFOverrideCacheEntry& actual)
        {
            ensure_equals("object id", actual.mObjectId, expected.mObjectId);
            ensure_equals("local id", actual.mLocalId, expected.mLocalId);
            ensure_equals("region handle", actual.mRegionHandle, expected.mRegionHandle);
            ensure_equals("side count", actual.mSides.size(), expected.mSides.size());
            for (const auto& [side, llsd] : expected.mSides)
            {
                auto found = actual.mSides.find(side);
                ensure("side present", found != actual.mSides.end());
                ensure("side llsd", llsd_equals(found->second, llsd));
            }
        }
    };

    // The harness's main() owns APR for the run; a group factory is a static
    // and must not tear it down a second time on the way out.
    struct vocacheTestFactory : public test_group<vocacheTest>
    {
        vocacheTestFactory() : test_group<vocacheTest>("LLVOCache") {}
    };

    typedef vocacheTestFactory::object vocacheTestObject;
    tut::vocacheTestFactory tut_test;

    // ---------------------------------------------------------------------------------------
    // Test functions
    // ---------------------------------------------------------------------------------------

    template<> template<>
    void vocacheTestObject::test<1>()
    {
        set_test_name("override entry round trip, materials built on demand");

        const U64 handle = to_region_handle(1000 * 256, 1000 * 256);
        const LLUUID object_id = LLUUID::generateNewID();
        LLSD data = makeOverrideLLSD(42, handle, object_id, 3);

        LLGLTFOverrideCacheEntry entry;
        ensure("fromLLSD", entry.fromLLSD(data));
        ensure_equals("object id", entry.mObjectId, object_id);
        ensure_equals("local id", entry.mLocalId, 42u);
        ensure_equals("region handle", entry.mRegionHandle, handle);
        ensure_equals("sides", entry.mSides.size(), (size_t)3);
        ensure("materials not built by loading", entry.mGLTFMaterial.empty());

        // toLLSD walks a hash map, so compare per side rather than as arrays.
        LLSD back = entry.toLLSD();
        ensure_equals("local id survives", back["local_id"].asInteger(), 42);
        ensure_equals("object id survives", back["object_id"].asUUID(), object_id);
        ensure_equals("region x survives", back["region_handle_x"].asInteger(), data["region_handle_x"].asInteger());
        ensure_equals("region y survives", back["region_handle_y"].asInteger(), data["region_handle_y"].asInteger());
        ensure_equals("side count survives", back["sides"].size(), (size_t)3);
        for (size_t i = 0; i < back["sides"].size(); ++i)
        {
            S32 side = back["sides"][i].asInteger();
            ensure("side llsd survives", llsd_equals(back["gltf_llsd"][i], data["gltf_llsd"][side]));
        }

        entry.materialize();
        ensure_equals("one material per side", entry.mGLTFMaterial.size(), entry.mSides.size());
        for (const auto& [side, llsd] : entry.mSides)
        {
            ensure("material for side", entry.mGLTFMaterial.find(side) != entry.mGLTFMaterial.end());
            ensure("material built", entry.mGLTFMaterial.at(side).notNull());
        }
        LLGLTFMaterial* first = entry.mGLTFMaterial.at(0).get();
        entry.materialize();
        ensure("second materialize keeps the materials", entry.mGLTFMaterial.at(0).get() == first);

        LLGLTFOverrideCacheEntry rejected;
        LLSD headless = data;
        headless.erase("region_handle_x");
        ensure("no region handle is rejected", !rejected.fromLLSD(headless));
        headless = data;
        headless.erase("local_id");
        ensure("no local id is rejected", !rejected.fromLLSD(headless));
    }

    template<> template<>
    void vocacheTestObject::test<2>()
    {
        set_test_name("object cache round trip");

        const U64 handle = to_region_handle(1000 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(500);

        LLVOCache::instance().writeToCache(handle, id, written, /*dirty=*/true, /*removal_enabled=*/false);
        ensure("cache file written", std::filesystem::exists(objectFile(handle, ".slc")));

        LLVOCacheEntry::vocache_entry_map_t read;
        ensure("read succeeds", LLVOCache::instance().readFromCache(handle, id, read));
        ensure_equals("entry count", read.size(), written.size());
        for (const auto& [local_id, entry] : written)
        {
            auto found = read.find(local_id);
            ensure("entry present", found != read.end());
            ensureSameEntry(*entry, *found->second);
        }

        LLVOCacheEntry::vocache_entry_map_t other;
        ensure("another region's id is refused", !LLVOCache::instance().readFromCache(handle, LLUUID::generateNewID(), other));
        ensure("nothing loaded for it", other.empty());
    }

    template<> template<>
    void vocacheTestObject::test<3>()
    {
        set_test_name("a save with removal enabled counts only what it wrote");

        const U64 handle = to_region_handle(1001 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(200);
        size_t valid = 0;
        for (auto& [local_id, entry] : written)
        {
            entry->setValid(local_id % 3 != 0);
            valid += entry->isValid() ? 1 : 0;
        }
        ensure("some entries were invalidated", valid < written.size());

        LLVOCache::instance().writeToCache(handle, id, written, true, /*removal_enabled=*/true);

        LLVOCacheEntry::vocache_entry_map_t read;
        ensure("read succeeds against the written count", LLVOCache::instance().readFromCache(handle, id, read));
        ensure_equals("only valid entries were written", read.size(), valid);
        for (const auto& [local_id, entry] : read)
        {
            ensure("no invalid entry came back", written.at(local_id)->isValid());
        }
    }

    template<> template<>
    void vocacheTestObject::test<4>()
    {
        set_test_name("a truncated object cache is refused");

        const U64 handle = to_region_handle(1002 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(100);
        LLVOCache::instance().writeToCache(handle, id, written, true, false);

        const std::filesystem::path file = objectFile(handle, ".slc");
        const uintmax_t size = std::filesystem::file_size(file);
        std::filesystem::resize_file(file, size - 100);

        LLVOCacheEntry::vocache_entry_map_t read;
        ensure("truncated file fails the read", !LLVOCache::instance().readFromCache(handle, id, read));
        ensure("fewer entries than written", read.size() < written.size());
    }

    template<> template<>
    void vocacheTestObject::test<5>()
    {
        set_test_name("the header survives a restart");

        const U64 handle = to_region_handle(1003 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t written = makeEntries(50);
        LLVOCache::instance().writeToCache(handle, id, written, true, false);

        LLVOCache::deleteSingleton();
        LLVOCache::initParamSingleton(false);
        LLVOCache::instance().initCache(LL_PATH_CACHE, CACHE_NUMBER_OF_REGIONS, CACHE_VERSION);

        LLVOCacheEntry::vocache_entry_map_t read;
        ensure("region known after restart", LLVOCache::instance().readFromCache(handle, id, read));
        ensure_equals("entries after restart", read.size(), written.size());
    }

    template<> template<>
    void vocacheTestObject::test<6>()
    {
        set_test_name("override cache round trip");

        const U64 handle = to_region_handle(1004 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(20);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t written;
        for (U32 local_id = 1; local_id <= 20; local_id += 3)
        {
            written[local_id] = makeOverride(local_id, handle, 1 + (int)(local_id % 4));
        }
        // An override for an object the primary cache does not hold is dropped on load.
        written[999] = makeOverride(999, handle, 2);

        LLVOCache::instance().writeGenericExtrasToCache(handle, id, written, true, false);
        ensure("extras file written", std::filesystem::exists(objectFile(handle, "_extras.slec")));

        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        ensure("read succeeds", LLVOCache::instance().readGenericExtrasFromCache(handle, id, read, objects));
        ensure_equals("all overrides with an object came back", read.size(), written.size() - 1);
        ensure("orphan override dropped", read.find(999) == read.end());
        for (const auto& [local_id, entry] : read)
        {
            ensureSameOverride(written.at(local_id), entry);
            ensure("materials not built by loading", entry.mGLTFMaterial.empty());
        }

        LLVOCacheEntry::vocache_gltf_overrides_map_t none;
        LLVOCacheEntry::vocache_entry_map_t no_objects;
        ensure("empty primary cache is not a failure", LLVOCache::instance().readGenericExtrasFromCache(handle, id, none, no_objects));
        ensure("and loads nothing", none.empty());
    }

    template<> template<>
    void vocacheTestObject::test<7>()
    {
        set_test_name("a corrupt override record is skipped, a truncated file refused");

        const U64 handle = to_region_handle(1005 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(10);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t written;
        for (U32 local_id = 1; local_id <= 10; ++local_id)
        {
            written[local_id] = makeOverride(local_id, handle, 2);
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
        ensure("version line present", line_end != std::string::npos);
        ensure_equals("version line", bytes.substr(0, line_end), LLGLTFOverrideCacheEntry::VERSION_LABEL + ":" + std::to_string(LLGLTFOverrideCacheEntry::VERSION));
        size_t cursor = line_end + 1 + UUID_BYTES;
        U32 count = 0;
        memcpy(&count, bytes.data() + cursor, sizeof(U32));
        cursor += sizeof(U32);
        ensure_equals("record count", (size_t)count, written.size());

        // Overwrite the second record's first byte with a type tag LLSD binary
        // does not have, so that one record and only that one fails to parse.
        U32 first_length = 0;
        memcpy(&first_length, bytes.data() + cursor, sizeof(U32));
        const size_t second_record = cursor + sizeof(U32) + first_length + sizeof(U32);
        ensure("second record inside file", second_record < bytes.size());
        std::string corrupted = bytes;
        corrupted[second_record] = (char)0xFF;
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(corrupted.data(), (std::streamsize)corrupted.size());
        }

        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        ensure("corrupt record does not fail the file", LLVOCache::instance().readGenericExtrasFromCache(handle, id, read, objects));
        ensure_equals("one record skipped", read.size(), written.size() - 1);
        for (const auto& [local_id, entry] : read)
        {
            ensureSameOverride(written.at(local_id), entry);
        }

        // Cut the last record short.
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), (std::streamsize)(bytes.size() - 3));
        }
        LLVOCacheEntry::vocache_gltf_overrides_map_t partial;
        ensure("truncated file is refused", !LLVOCache::instance().readGenericExtrasFromCache(handle, id, partial, objects));

        // An earlier format is refused by its version line alone.
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << LLGLTFOverrideCacheEntry::VERSION_LABEL << ":1\n" << id << "\n0000000000\n";
        }
        LLVOCacheEntry::vocache_gltf_overrides_map_t old;
        ensure("old version is refused", !LLVOCache::instance().readGenericExtrasFromCache(handle, id, old, objects));
        ensure("old version loads nothing", old.empty());
    }
}
