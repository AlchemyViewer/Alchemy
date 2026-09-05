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

    template<> template<>
    void vocacheTestObject::test<8>()
    {
        set_test_name("scene load memory factor: bounds from RAM, under the heap cap, always ordered");

        auto factor = [](F32 allocated, F32 physical, F32 cap, F32 low, F32 high)
        {
            return LLVOCacheEntry::memoryAdjustFactor(allocated, physical, cap, low, high);
        };
        auto close_to = [](F32 a, F32 b) { return fabsf(a - b) < 1e-4f; };
        const F32 no_cap = 4194303.f; // the cap before it is set: U32_MAX kilobytes

        // 8 GB with the default settings: the bounds are the RAM fractions,
        // 2048 and 4915, and the factor runs from 1 to 0 between them
        ensure("at the low bound nothing is pulled in", close_to(factor(2048.f, 8192.f, no_cap, 750.f, 2048.f), 1.f));
        ensure("at the high bound everything is", close_to(factor(4915.2f, 8192.f, no_cap, 750.f, 2048.f), 0.f));
        ensure("halfway is half", close_to(factor(3481.6f, 8192.f, no_cap, 750.f, 2048.f), 0.5f));
        ensure("well below the low bound is still 1", close_to(factor(100.f, 8192.f, no_cap, 750.f, 2048.f), 1.f));

        // no RAM figure: the settings alone
        ensure("without a RAM figure the settings are the bounds", close_to(factor(1399.f, 0.f, no_cap, 750.f, 2048.f), 0.5f));

        // 64 GB behind a 16 GB heap cap: the tightest point moves under the
        // cap and the loose point keeps its ratio to it
        const F32 cap = 16384.f;
        ensure("the tightest point sits under the cap", close_to(factor(cap * 0.9f, 65536.f, cap, 750.f, 2048.f), 0.f));
        ensure("the loose point scaled with it", close_to(factor(6144.f, 65536.f, cap, 750.f, 2048.f), 1.f));
        ensure("between them is between", close_to(factor(10444.8f, 65536.f, cap, 750.f, 2048.f), 0.5f));
        ensure("past the cap is still a factor", factor(2.f * cap, 65536.f, cap, 750.f, 2048.f) == 0.f);

        // settings the wrong way round
        for (F32 allocated : { 0.f, 2000.f, 2500.f, 3000.f, 4000.f })
        {
            const F32 f = factor(allocated, 0.f, no_cap, 3000.f, 2048.f);
            ensure("inverted settings still give a factor in [0, 1]", f >= 0.f && f <= 1.f);
        }
        ensure("inverted settings: below the low setting nothing is pulled in", close_to(factor(2000.f, 0.f, no_cap, 3000.f, 2048.f), 1.f));
    }

    template<> template<>
    void vocacheTestObject::test<9>()
    {
        set_test_name("hostile extras records: a depth cap, a budget for unreadable ones, and no face out of range");

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
        ensure("a file with one bad record is still a cache", LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, objects));
        ensure_equals("the shallow record loaded, the deep one did not", overrides.size(), 1u);
        ensure("it is the shallow one", overrides.find(1) != overrides.end());

        // up to the budget, unreadable records are skipped; past it the file is refused
        std::vector<std::string> garbage(16, std::string("\xff\xfe\xfd\xfc", 4));
        garbage.push_back(binaryRecord(makeOverrideLLSD(3, handle, LLUUID::generateNewID(), 1)));
        writeExtrasFile(handle, id, garbage);
        overrides.clear();
        ensure("sixteen unreadable records are within the budget", LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, objects));
        ensure_equals("and the good record after them loaded", overrides.size(), 1u);

        garbage.insert(garbage.begin(), std::string("\xff\xfe\xfd\xfc", 4));
        writeExtrasFile(handle, id, garbage);
        overrides.clear();
        ensure("seventeen is a bad file", !LLVOCache::instance().readGenericExtrasFromCache(handle, id, overrides, objects));

        // a face index no object has is dropped by the record parser
        LLSD faces = makeOverrideLLSD(4, handle, LLUUID::generateNewID(), 2);
        faces["sides"][1] = 300;
        LLGLTFOverrideCacheEntry entry;
        ensure("the record still parses", entry.fromLLSD(faces));
        ensure_equals("only the face in range is kept", entry.mSides.size(), 1u);
        ensure("and it is face 0", entry.mSides.find(0) != entry.mSides.end());
        faces["sides"][1] = -1;
        LLGLTFOverrideCacheEntry negative;
        ensure("a negative face parses", negative.fromLLSD(faces));
        ensure_equals("and is dropped too", negative.mSides.size(), 1u);
    }

    template<> template<>
    void vocacheTestObject::test<10>()
    {
        set_test_name("header slots survive a removal followed by a crash");

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
        ensure_equals("two regions come back", LLVOCache::instance().getCacheEntries(), 2u);

        // a new region has to take the hole, not the slot of the one behind it
        LLVOCache::instance().writeToCache(fourth, id, makeEntries(3), true, false);
        restartAsAfterCrash();
        ensure_equals("three regions come back", LLVOCache::instance().getCacheEntries(), 3u);
        LLVOCacheEntry::vocache_entry_map_t read;
        ensure("the first region is still there", LLVOCache::instance().readFromCache(first, id, read));
        read.clear();
        ensure("the third region was not overwritten", LLVOCache::instance().readFromCache(third, id, read));
        read.clear();
        ensure("the new region is there", LLVOCache::instance().readFromCache(fourth, id, read));
    }

    template<> template<>
    void vocacheTestObject::test<11>()
    {
        set_test_name("extras are only rewritten when the overrides changed");

        const U64 handle = to_region_handle(1020 * 256, 1000 * 256);
        const LLUUID id = LLUUID::generateNewID();
        LLVOCacheEntry::vocache_entry_map_t objects = makeEntries(4);
        LLVOCache::instance().writeToCache(handle, id, objects, true, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t overrides;
        overrides[1] = makeOverride(1, handle, 2);
        overrides[2] = makeOverride(2, handle, 3);
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, overrides, true, false);

        // a save with nothing dirty leaves the file as it was, whatever it is handed
        LLVOCacheEntry::vocache_gltf_overrides_map_t nothing;
        LLVOCache::instance().writeGenericExtrasToCache(handle, id, nothing, false, false);

        LLVOCacheEntry::vocache_gltf_overrides_map_t read;
        ensure("the file still reads", LLVOCache::instance().readGenericExtrasFromCache(handle, id, read, objects));
        ensure_equals("and still holds both overrides", read.size(), 2u);
        ensureSameOverride(overrides[1], read[1]);
        ensureSameOverride(overrides[2], read[2]);
    }
}
