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

    // Default construction yields a zeroed, null-equal ID.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: default construction yields a zeroed null ID", "[llmaterialid]")
    {
        LLMaterialID id;
        UNSCOPED_INFO("default-constructed isNull");
        CHECK(id.isNull());
        UNSCOPED_INFO("default-constructed == LLMaterialID::null");
        CHECK(id == LLMaterialID::null);
        for (int i = 0; i < MATERIAL_ID_SIZE; ++i)
        {
            UNSCOPED_INFO("default byte zero");
            REQUIRE((int)id.get()[i] == 0);
        }
    }

    // Raw-memory constructor copies the full 16 bytes.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: the raw-memory constructor copies the full 16 bytes", "[llmaterialid]")
    {
        LLMaterialID id(kSampleBytes);
        UNSCOPED_INFO("bytes preserved");
        CHECK(std::memcmp(id.get(), kSampleBytes, MATERIAL_ID_SIZE) == 0);
        UNSCOPED_INFO("non-null sample is not null");
        CHECK_FALSE(id.isNull());
    }

    // LLUUID round-trip preserves the byte pattern in both directions.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: LLUUID round-trip preserves the byte pattern in both directions", "[llmaterialid]")
    {
        LLUUID uuid;
        std::memcpy(uuid.mData, kSampleBytes, UUID_BYTES);

        LLMaterialID id(uuid);
        UNSCOPED_INFO("LLUUID -> LLMaterialID bytes preserved");
        CHECK(std::memcmp(id.get(), uuid.mData, MATERIAL_ID_SIZE) == 0);

        LLUUID back = id.asUUID();
        UNSCOPED_INFO("asUUID round-trips");
        CHECK(back == uuid);
    }

    // LLSD::Binary constructor copies a well-formed 16-byte payload.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: LLSD::Binary constructor copies a well-formed 16-byte payload", "[llmaterialid]")
    {
        LLSD::Binary bin(kSampleBytes, kSampleBytes + MATERIAL_ID_SIZE);
        LLMaterialID id(bin);
        UNSCOPED_INFO("LLSD::Binary bytes preserved");
        CHECK(std::memcmp(id.get(), bin.data(), MATERIAL_ID_SIZE) == 0);
    }

    // LLSD round-trip: asLLSD() produces a Binary that reconstructs the same ID.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: asLLSD produces a Binary that reconstructs the same ID", "[llmaterialid]")
    {
        LLMaterialID original(kSampleBytes);
        LLSD sd = original.asLLSD();
        UNSCOPED_INFO("asLLSD is binary");
        CHECK(sd.isBinary());
        UNSCOPED_INFO("asLLSD size is MATERIAL_ID_SIZE");
        REQUIRE((int)sd.asBinary().size() == MATERIAL_ID_SIZE);
        LLMaterialID round_trip(sd);
        UNSCOPED_INFO("LLSD round-trip equals original");
        CHECK(round_trip == original);
    }

    // Equality and inequality across copies and one-byte differences.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: equality and inequality across copies and a one-byte difference", "[llmaterialid]")
    {
        U8 a[MATERIAL_ID_SIZE];
        U8 b[MATERIAL_ID_SIZE];
        std::memcpy(a, kSampleBytes, MATERIAL_ID_SIZE);
        std::memcpy(b, kSampleBytes, MATERIAL_ID_SIZE);
        b[MATERIAL_ID_SIZE - 1] ^= 0x01;

        LLMaterialID id_a(a), id_a_copy(a), id_b(b);
        UNSCOPED_INFO("a == a_copy");
        CHECK(id_a == id_a_copy);
        UNSCOPED_INFO("!(a != a_copy)");
        CHECK_FALSE(id_a != id_a_copy);
        UNSCOPED_INFO("a != b (one bit differs)");
        CHECK(id_a != id_b);
        UNSCOPED_INFO("!(a == b)");
        CHECK_FALSE(id_a == id_b);
    }

    // Ordering is byte-lexicographic (not word-endian).
    //
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
        UNSCOPED_INFO("byte-lex: id_low < id_high");
        CHECK(id_low <  id_high);
        UNSCOPED_INFO("byte-lex: id_high > id_low");
        CHECK(id_high >  id_low);
        UNSCOPED_INFO("byte-lex: id_low <= id_high");
        CHECK(id_low <= id_high);
        UNSCOPED_INFO("byte-lex: id_high >= id_low");
        CHECK(id_high >= id_low);
        UNSCOPED_INFO("byte-lex: !(id_low > id_high)");
        CHECK_FALSE(id_low > id_high);

        UNSCOPED_INFO("reflexive: id <= id");
        CHECK(id_low <= id_low);
        UNSCOPED_INFO("reflexive: id >= id");
        CHECK(id_low >= id_low);
        UNSCOPED_INFO("irreflexive: !(id < id)");
        CHECK_FALSE(id_low < id_low);
        UNSCOPED_INFO("irreflexive: !(id > id)");
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
        UNSCOPED_INFO("asString little-endian word format");
        CHECK(id.asString() ==
            std::string("ddccbbaa-44332211-01000000-ffffffff"));

        UNSCOPED_INFO("null asString");
        CHECK(LLMaterialID::null.asString() ==
            std::string("00000000-00000000-00000000-00000000"));
    }

    // set() then clear() round-trip.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: set() copies the bytes and clear() makes the ID null", "[llmaterialid]")
    {
        LLMaterialID id;
        id.set(kSampleBytes);
        UNSCOPED_INFO("set copies bytes");
        CHECK(std::memcmp(id.get(), kSampleBytes, MATERIAL_ID_SIZE) == 0);
        UNSCOPED_INFO("post-set is not null");
        CHECK_FALSE(id.isNull());

        id.clear();
        UNSCOPED_INFO("clear yields null");
        CHECK(id.isNull());
        UNSCOPED_INFO("clear == LLMaterialID::null");
        CHECK(id == LLMaterialID::null);
    }

    // Hash consistency: equal IDs hash the same and std::hash == boost hash_value.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: std::hash agrees for equal IDs and with hash_value", "[llmaterialid]")
    {
        LLMaterialID a(kSampleBytes), b(kSampleBytes);
        UNSCOPED_INFO("equal IDs -> equal std::hash");
        CHECK(std::hash<LLMaterialID>{}(a) == std::hash<LLMaterialID>{}(b));
        UNSCOPED_INFO("std::hash matches hash_value");
        CHECK(std::hash<LLMaterialID>{}(a) == hash_value(b));
        UNSCOPED_INFO("null hashes to 0");
        CHECK(std::hash<LLMaterialID>{}(LLMaterialID::null) == (size_t)0);
    }

    // Stream insertion delegates to asString.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: stream insertion delegates to asString", "[llmaterialid]")
    {
        LLMaterialID id(kSampleBytes);
        std::ostringstream os;
        os << id;
        UNSCOPED_INFO("operator<< matches asString");
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
        UNSCOPED_INFO("too-short binary -> null");
        REQUIRE(LLMaterialID(too_short).isNull());

        LLSD::Binary too_long(MATERIAL_ID_SIZE + 1, 0xff);
        UNSCOPED_INFO("too-long binary -> null");
        REQUIRE(LLMaterialID(too_long).isNull());

        LLSD::Binary empty;
        UNSCOPED_INFO("empty binary -> null");
        REQUIRE(LLMaterialID(empty).isNull());
    }

    // set(nullptr) does not invoke memcpy on a null source.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: set(nullptr) does not invoke memcpy on a null source", "[llmaterialid]")
    {
        LLMaterialID id(kSampleBytes);
        id.set(nullptr);
        UNSCOPED_INFO("set(nullptr) clears");
        REQUIRE(id.isNull());
    }

    // Non-binary LLSD (LLSD::asBinary() returns empty) clears instead of UB.
    TEST_CASE_METHOD(llmaterialid_data, "llmaterialid: a non-binary LLSD clears the ID instead of reading it", "[llmaterialid]")
    {
        LLSD not_binary("hello");
        UNSCOPED_INFO("non-binary LLSD -> null");
        REQUIRE(LLMaterialID(not_binary).isNull());
    }
#endif
}
