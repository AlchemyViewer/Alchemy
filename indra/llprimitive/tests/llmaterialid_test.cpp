/**
 * @file llmaterialid_test.cpp
 * @brief Unit tests for LLMaterialID
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <functional>

#include "../llmaterialid.h"

namespace
{
    struct llmaterialid_data { };

    static const U8 kSampleBytes[MATERIAL_ID_SIZE] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: default construction yields a zeroed null ID", "[llmaterialid]")
    {
        LLMaterialID id;
        CHECK(id.isNull());
        CHECK(id == LLMaterialID::null);
        for (int i = 0; i < MATERIAL_ID_SIZE; ++i)
        {
            CAPTURE(i);
            REQUIRE((int)id.get()[i] == 0);
        }
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: the raw-memory constructor copies the full 16 bytes", "[llmaterialid]")
    {
        LLMaterialID id(kSampleBytes);
        CHECK(std::memcmp(id.get(), kSampleBytes, MATERIAL_ID_SIZE) == 0);
        CHECK_FALSE(id.isNull());
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: LLUUID round-trip preserves the byte pattern in both directions", "[llmaterialid]")
    {
        LLUUID uuid;
        std::memcpy(uuid.mData, kSampleBytes, UUID_BYTES);

        LLMaterialID id(uuid);
        CHECK(std::memcmp(id.get(), uuid.mData, MATERIAL_ID_SIZE) == 0);

        LLUUID back = id.asUUID();
        CHECK(back == uuid);
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: LLSD::Binary constructor copies a well-formed 16-byte payload", "[llmaterialid]")
    {
        LLSD::Binary bin(kSampleBytes, kSampleBytes + MATERIAL_ID_SIZE);
        LLMaterialID id(bin);
        CHECK(std::memcmp(id.get(), bin.data(), MATERIAL_ID_SIZE) == 0);
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: asLLSD produces a Binary that reconstructs the same ID", "[llmaterialid]")
    {
        LLMaterialID original(kSampleBytes);
        LLSD sd = original.asLLSD();
        CHECK(sd.isBinary());
        REQUIRE((int)sd.asBinary().size() == MATERIAL_ID_SIZE);
        LLMaterialID round_trip(sd);
        CHECK(round_trip == original);
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: equality and inequality across copies and a one-byte difference", "[llmaterialid]")
    {
        U8 a[MATERIAL_ID_SIZE];
        U8 b[MATERIAL_ID_SIZE];
        std::memcpy(a, kSampleBytes, MATERIAL_ID_SIZE);
        std::memcpy(b, kSampleBytes, MATERIAL_ID_SIZE);
        b[MATERIAL_ID_SIZE - 1] ^= 0x01;

        LLMaterialID id_a(a), id_a_copy(a), id_b(b);
        CHECK(id_a == id_a_copy);
        CHECK_FALSE(id_a != id_a_copy);
        CHECK(id_a != id_b);
        CHECK_FALSE(id_a == id_b);
    }

    // With the old reinterpret_cast<U32*> comparison on a little-endian host,
    // {0x00, 0x01, 0,...} read as the word 0x00000100 (256) would compare
    // greater than {0x01, 0x00, 0,...} read as 0x00000001 (1) — reversing
    // the byte-lexicographic intuition. memcmp gets it right: the first
    // differing byte wins.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: ordering is byte-lexicographic (not word-endian)", "[llmaterialid]")
    {
        U8 low[MATERIAL_ID_SIZE]  = {0};
        U8 high[MATERIAL_ID_SIZE] = {0};
        low[1]  = 0x01;
        high[0] = 0x01;

        LLMaterialID id_low(low), id_high(high);
        CHECK(id_low <  id_high);
        CHECK(id_high >  id_low);
        CHECK(id_low <= id_high);
        CHECK(id_high >= id_low);
        CHECK_FALSE(id_low > id_high);

        CHECK(id_low <= id_low);
        CHECK(id_low >= id_low);
        CHECK_FALSE(id_low < id_low);
        CHECK_FALSE(id_low > id_low);
    }

    // asString uses little-endian word formatting (preserved for log greppability).
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: asString formats in little-endian words", "[llmaterialid]")
    {
        const U8 bytes[MATERIAL_ID_SIZE] = {
            0xaa, 0xbb, 0xcc, 0xdd,
            0x11, 0x22, 0x33, 0x44,
            0x00, 0x00, 0x00, 0x01,
            0xff, 0xff, 0xff, 0xff
        };
        LLMaterialID id(bytes);
        CHECK(id.asString() == std::string("ddccbbaa-44332211-01000000-ffffffff"));
        CHECK(LLMaterialID::null.asString() == std::string("00000000-00000000-00000000-00000000"));
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: set() copies the bytes and clear() makes the ID null", "[llmaterialid]")
    {
        LLMaterialID id;
        id.set(kSampleBytes);
        CHECK(std::memcmp(id.get(), kSampleBytes, MATERIAL_ID_SIZE) == 0);
        CHECK_FALSE(id.isNull());

        id.clear();
        CHECK(id.isNull());
        CHECK(id == LLMaterialID::null);
    }

    // Hash consistency: equal IDs hash the same and std::hash == boost hash_value.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: std::hash agrees for equal IDs and with hash_value", "[llmaterialid]")
    {
        LLMaterialID a(kSampleBytes), b(kSampleBytes);
        CHECK(std::hash<LLMaterialID>{}(a) == std::hash<LLMaterialID>{}(b));
        CHECK(std::hash<LLMaterialID>{}(a) == hash_value(b));
        CHECK(std::hash<LLMaterialID>{}(LLMaterialID::null) == (size_t)0);
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: stream insertion delegates to asString", "[llmaterialid]")
    {
        LLMaterialID id(kSampleBytes);
        std::ostringstream os;
        os << id;
        CHECK(os.str() == id.asString());
    }

    // The remaining tests exercise defensive guards against malformed input.
    // In debug / RelWithDebInfo builds the underlying llassert fires and would
    // abort the test runner, so we only run these where SHOW_ASSERT is stripped.
#ifndef SHOW_ASSERT
    // parseFromBinary (via LLSD::Binary ctor) clears on wrong-size input
    // instead of overreading.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: parseFromBinary clears on wrong-size input instead of overreading", "[llmaterialid]")
    {
        LLSD::Binary too_short(MATERIAL_ID_SIZE - 1, 0xff);
        CHECK(LLMaterialID(too_short).isNull());

        LLSD::Binary too_long(MATERIAL_ID_SIZE + 1, 0xff);
        CHECK(LLMaterialID(too_long).isNull());

        LLSD::Binary empty;
        CHECK(LLMaterialID(empty).isNull());
    }

    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: set(nullptr) does not invoke memcpy on a null source", "[llmaterialid]")
    {
        LLMaterialID id(kSampleBytes);
        id.set(nullptr);
        CHECK(id.isNull());
    }

    // Non-binary LLSD (LLSD::asBinary() returns empty) clears instead of UB.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: a non-binary LLSD clears the ID instead of reading it", "[llmaterialid]")
    {
        LLSD not_binary("hello");
        CHECK(LLMaterialID(not_binary).isNull());
    }
#endif
}
