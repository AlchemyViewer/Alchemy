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
#include "lltut.h"

#include <cstring>
#include <functional>

#include "../llmaterialid.h"
#include "lluuid.cpp"

namespace tut
{
    struct llmaterialid_data { };
    typedef test_group<llmaterialid_data> llmaterialid_t;
    typedef llmaterialid_t::object llmaterialid_object_t;
    tut::llmaterialid_t tut_llmaterialid("llmaterialid");

    static const U8 kSampleBytes[MATERIAL_ID_SIZE] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    // Default construction yields a zeroed, null-equal ID.
    template<> template<>
    void llmaterialid_object_t::test<1>()
    {
        LLMaterialID id;
        ensure("default-constructed isNull", id.isNull());
        ensure("default-constructed == LLMaterialID::null", id == LLMaterialID::null);
        for (int i = 0; i < MATERIAL_ID_SIZE; ++i)
        {
            ensure_equals("default byte zero", (int)id.get()[i], 0);
        }
    }

    // Raw-memory constructor copies the full 16 bytes.
    template<> template<>
    void llmaterialid_object_t::test<2>()
    {
        LLMaterialID id(kSampleBytes);
        ensure("bytes preserved", std::memcmp(id.get(), kSampleBytes, MATERIAL_ID_SIZE) == 0);
        ensure("non-null sample is not null", !id.isNull());
    }

    // LLUUID round-trip preserves the byte pattern in both directions.
    template<> template<>
    void llmaterialid_object_t::test<3>()
    {
        LLUUID uuid;
        std::memcpy(uuid.mData, kSampleBytes, UUID_BYTES);

        LLMaterialID id(uuid);
        ensure("LLUUID -> LLMaterialID bytes preserved",
            std::memcmp(id.get(), uuid.mData, MATERIAL_ID_SIZE) == 0);

        LLUUID back = id.asUUID();
        ensure_equals("asUUID round-trips", back, uuid);
    }

    // LLSD::Binary constructor copies a well-formed 16-byte payload.
    template<> template<>
    void llmaterialid_object_t::test<4>()
    {
        LLSD::Binary bin(kSampleBytes, kSampleBytes + MATERIAL_ID_SIZE);
        LLMaterialID id(bin);
        ensure("LLSD::Binary bytes preserved",
            std::memcmp(id.get(), bin.data(), MATERIAL_ID_SIZE) == 0);
    }

    // LLSD round-trip: asLLSD() produces a Binary that reconstructs the same ID.
    template<> template<>
    void llmaterialid_object_t::test<5>()
    {
        LLMaterialID original(kSampleBytes);
        LLSD sd = original.asLLSD();
        ensure("asLLSD is binary", sd.isBinary());
        ensure_equals("asLLSD size is MATERIAL_ID_SIZE",
            (int)sd.asBinary().size(), MATERIAL_ID_SIZE);
        LLMaterialID round_trip(sd);
        ensure("LLSD round-trip equals original", round_trip == original);
    }

    // Equality and inequality across copies and one-byte differences.
    template<> template<>
    void llmaterialid_object_t::test<6>()
    {
        U8 a[MATERIAL_ID_SIZE];
        U8 b[MATERIAL_ID_SIZE];
        std::memcpy(a, kSampleBytes, MATERIAL_ID_SIZE);
        std::memcpy(b, kSampleBytes, MATERIAL_ID_SIZE);
        b[MATERIAL_ID_SIZE - 1] ^= 0x01;

        LLMaterialID id_a(a), id_a_copy(a), id_b(b);
        ensure("a == a_copy", id_a == id_a_copy);
        ensure("!(a != a_copy)", !(id_a != id_a_copy));
        ensure("a != b (one bit differs)", id_a != id_b);
        ensure("!(a == b)", !(id_a == id_b));
    }

    // Ordering is byte-lexicographic (not word-endian).
    //
    // With the old reinterpret_cast<U32*> comparison on a little-endian host,
    // {0x00, 0x01, 0,...} read as the word 0x00000100 (256) would compare
    // greater than {0x01, 0x00, 0,...} read as 0x00000001 (1) — reversing
    // the byte-lexicographic intuition. memcmp gets it right: the first
    // differing byte wins.
    template<> template<>
    void llmaterialid_object_t::test<7>()
    {
        U8 low[MATERIAL_ID_SIZE]  = {0};
        U8 high[MATERIAL_ID_SIZE] = {0};
        low[1]  = 0x01;
        high[0] = 0x01;

        LLMaterialID id_low(low), id_high(high);
        ensure("byte-lex: id_low < id_high",  id_low <  id_high);
        ensure("byte-lex: id_high > id_low",  id_high >  id_low);
        ensure("byte-lex: id_low <= id_high", id_low <= id_high);
        ensure("byte-lex: id_high >= id_low", id_high >= id_low);
        ensure("byte-lex: !(id_low > id_high)", !(id_low > id_high));

        ensure("reflexive: id <= id", id_low <= id_low);
        ensure("reflexive: id >= id", id_low >= id_low);
        ensure("irreflexive: !(id < id)", !(id_low < id_low));
        ensure("irreflexive: !(id > id)", !(id_low > id_low));
    }

    // asString uses little-endian word formatting (preserved for log greppability).
    template<> template<>
    void llmaterialid_object_t::test<8>()
    {
        const U8 bytes[MATERIAL_ID_SIZE] = {
            0xaa, 0xbb, 0xcc, 0xdd,
            0x11, 0x22, 0x33, 0x44,
            0x00, 0x00, 0x00, 0x01,
            0xff, 0xff, 0xff, 0xff
        };
        LLMaterialID id(bytes);
        ensure_equals("asString little-endian word format",
            id.asString(),
            std::string("ddccbbaa-44332211-01000000-ffffffff"));

        ensure_equals("null asString",
            LLMaterialID::null.asString(),
            std::string("00000000-00000000-00000000-00000000"));
    }

    // set() then clear() round-trip.
    template<> template<>
    void llmaterialid_object_t::test<9>()
    {
        LLMaterialID id;
        id.set(kSampleBytes);
        ensure("set copies bytes",
            std::memcmp(id.get(), kSampleBytes, MATERIAL_ID_SIZE) == 0);
        ensure("post-set is not null", !id.isNull());

        id.clear();
        ensure("clear yields null", id.isNull());
        ensure("clear == LLMaterialID::null", id == LLMaterialID::null);
    }

    // Hash consistency: equal IDs hash the same and std::hash == boost hash_value.
    template<> template<>
    void llmaterialid_object_t::test<10>()
    {
        LLMaterialID a(kSampleBytes), b(kSampleBytes);
        ensure_equals("equal IDs -> equal std::hash",
            std::hash<LLMaterialID>{}(a), std::hash<LLMaterialID>{}(b));
        ensure_equals("std::hash matches hash_value",
            std::hash<LLMaterialID>{}(a), hash_value(b));
        ensure_equals("null hashes to 0",
            std::hash<LLMaterialID>{}(LLMaterialID::null), (size_t)0);
    }

    // Stream insertion delegates to asString.
    template<> template<>
    void llmaterialid_object_t::test<11>()
    {
        LLMaterialID id(kSampleBytes);
        std::ostringstream os;
        os << id;
        ensure_equals("operator<< matches asString", os.str(), id.asString());
    }

    // The remaining tests exercise defensive guards against malformed input.
    // In debug / RelWithDebInfo builds the underlying llassert fires and would
    // abort the test runner, so we only run these where SHOW_ASSERT is stripped.
#ifndef SHOW_ASSERT
    // parseFromBinary (via LLSD::Binary ctor) clears on wrong-size input
    // instead of overreading.
    template<> template<>
    void llmaterialid_object_t::test<12>()
    {
        LLSD::Binary too_short(MATERIAL_ID_SIZE - 1, 0xff);
        ensure("too-short binary -> null", LLMaterialID(too_short).isNull());

        LLSD::Binary too_long(MATERIAL_ID_SIZE + 1, 0xff);
        ensure("too-long binary -> null", LLMaterialID(too_long).isNull());

        LLSD::Binary empty;
        ensure("empty binary -> null", LLMaterialID(empty).isNull());
    }

    // set(nullptr) does not invoke memcpy on a null source.
    template<> template<>
    void llmaterialid_object_t::test<13>()
    {
        LLMaterialID id(kSampleBytes);
        id.set(nullptr);
        ensure("set(nullptr) clears", id.isNull());
    }

    // Non-binary LLSD (LLSD::asBinary() returns empty) clears instead of UB.
    template<> template<>
    void llmaterialid_object_t::test<14>()
    {
        LLSD not_binary("hello");
        ensure("non-binary LLSD -> null", LLMaterialID(not_binary).isNull());
    }
#endif
}
