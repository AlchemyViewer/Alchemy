/**
 * @file lluuid_test.cpp
 * @brief Unit tests for LLUUID
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

#include <cstring>
#include <functional>
#include <set>

#include "../lluuid.h"

#include "../test/lltut.h"

namespace tut
{
    struct lluuid_data { };
    typedef test_group<lluuid_data> lluuid_t;
    typedef lluuid_t::object lluuid_object_t;
    tut::lluuid_t tut_lluuid("LLUUID");

    static const std::string kSampleStr = "01020304-0506-0708-090a-0b0c0d0e0f10";
    static void fill_sample(LLUUID& id)
    {
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            id.mData[i] = (U8)(i + 1);
        }
    }

    // Default construction is null.
    template<> template<>
    void lluuid_object_t::test<1>()
    {
        LLUUID id;
        ensure("default-constructed isNull", id.isNull());
        ensure("default-constructed !notNull", !id.notNull());
        ensure("default-constructed == LLUUID::null", id == LLUUID::null);
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            ensure_equals("default byte zero", (int)id.mData[i], 0);
        }
    }

    // setNull zeroes every byte.
    template<> template<>
    void lluuid_object_t::test<2>()
    {
        LLUUID id;
        fill_sample(id);
        ensure("filled UUID is not null", id.notNull());
        id.setNull();
        ensure("setNull -> isNull", id.isNull());
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            ensure_equals("setNull byte zero", (int)id.mData[i], 0);
        }
    }

    // String parsing round-trip via set() / asString().
    template<> template<>
    void lluuid_object_t::test<3>()
    {
        LLUUID id;
        ensure("set valid string", id.set(kSampleStr));
        ensure_equals("asString round-trips", id.asString(), kSampleStr);
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            ensure_equals("byte parsed correctly", (int)id.mData[i], i + 1);
        }
    }

    // set() with an empty string clears to null; invalid input also nulls.
    template<> template<>
    void lluuid_object_t::test<4>()
    {
        LLUUID id;
        fill_sample(id);

        ensure("empty string set returns true", id.set(std::string()));
        ensure("empty string -> null", id.isNull());

        fill_sample(id);
        ensure("bogus string set returns false",
            !id.set(std::string("not-a-uuid"), false));
        ensure("bogus string -> null", id.isNull());
    }

    // Equality / inequality for matching and one-byte-different UUIDs.
    template<> template<>
    void lluuid_object_t::test<5>()
    {
        LLUUID a, b, c;
        fill_sample(a);
        fill_sample(b);
        fill_sample(c);
        c.mData[UUID_BYTES - 1] ^= 0x01;

        ensure("a == b", a == b);
        ensure("!(a != b)", !(a != b));
        ensure("a != c", a != c);
        ensure("!(a == c)", !(a == c));
    }

    // Byte-lexicographic sort order. IW's comment in lluuid.cpp warns against
    // optimizing operator< with word-wide reads precisely because the word
    // interpretation reverses bytes on LE. This test pins the byte-lex behavior.
    template<> template<>
    void lluuid_object_t::test<6>()
    {
        LLUUID low, high;
        low.setNull();
        high.setNull();
        low.mData[1]  = 0x01; // word value 0x100 on LE
        high.mData[0] = 0x01; // word value 0x001 on LE — but first byte wins

        ensure("byte-lex: low < high",  low <  high);
        ensure("byte-lex: high > low",  high >  low);
        ensure("irreflexive: !(low < low)", !(low < low));
        ensure("irreflexive: !(low > low)", !(low > low));
    }

    // operator^= is its own inverse (XOR twice cancels) and commutative.
    template<> template<>
    void lluuid_object_t::test<7>()
    {
        LLUUID a, b, original;
        fill_sample(a);
        a.set(kSampleStr);
        original = a;

        b.set("11223344-5566-7788-99aa-bbccddeeff00");

        LLUUID xored = a ^ b;
        ensure("XOR changes value", xored != a);

        LLUUID xored_twice = xored ^ b;
        ensure_equals("double XOR cancels", xored_twice, original);

        LLUUID xored_self = a ^ a;
        ensure("self-XOR yields null", xored_self.isNull());
    }

    // Bit-level regression for the digest/CRC functions. These values are
    // computed assuming the host is little-endian (effectively all targets);
    // they pin the pre-UB-fix numerical output so callers that persist or
    // compare these checksums are unaffected.
    template<> template<>
    void lluuid_object_t::test<8>()
    {
        ensure_equals("null CRC32 == 0",      LLUUID::null.getCRC32(),    (U32)0);
        ensure_equals("null CRC16 == 0",      LLUUID::null.getCRC16(),    (U16)0);
        ensure_equals("null Digest64 == 0",   LLUUID::null.getDigest64(), (U64)0);

        LLUUID id;
        fill_sample(id);
        // mData = {0x01, 0x02, ..., 0x10}
        // LE U32s: 0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d — sum 0x2824201C
        ensure_equals("sample CRC32", id.getCRC32(), (U32)0x2824201C);
        // LE U16s sum: high bytes (2+4+...+16)=72=0x48; low bytes (1+3+...+15)=64=0x40
        ensure_equals("sample CRC16", id.getCRC16(), (U16)0x4840);
        // LE U64s: 0x0807060504030201 ^ 0x100f0e0d0c0b0a09 = 0x1808080808080808
        ensure_equals("sample Digest64", id.getDigest64(), (U64)0x1808080808080808ULL);
    }

    // Hash: equal IDs hash equally, std::hash matches hash_value, null hashes to 0.
    template<> template<>
    void lluuid_object_t::test<9>()
    {
        LLUUID a, b;
        fill_sample(a);
        fill_sample(b);
        ensure_equals("equal IDs equal std::hash",
            std::hash<LLUUID>{}(a), std::hash<LLUUID>{}(b));
        ensure_equals("std::hash matches hash_value",
            std::hash<LLUUID>{}(a), hash_value(b));
        ensure_equals("null hashes to 0",
            std::hash<LLUUID>{}(LLUUID::null), (size_t)0);

        // A different UUID should (overwhelmingly likely) hash differently.
        LLUUID c;
        c.set("ffffffff-ffff-ffff-ffff-ffffffffffff");
        ensure_not_equals("distinct IDs typically hash differently",
            std::hash<LLUUID>{}(a), std::hash<LLUUID>{}(c));
    }

    // generateNewID produces distinct, non-null UUIDs.
    template<> template<>
    void lluuid_object_t::test<10>()
    {
        LLUUID a = LLUUID::generateNewID();
        LLUUID b = LLUUID::generateNewID();
        ensure("generated id is not null", a.notNull());
        ensure("generated id is not null (2)", b.notNull());
        ensure("two generated ids differ", a != b);
        // validate() should accept the generated string form
        ensure("generated id validates", LLUUID::validate(a.asString()));
    }

    // to_chars emits exactly UUID_STR_LENGTH-1 chars in canonical 8-4-4-4-12 form.
    template<> template<>
    void lluuid_object_t::test<11>()
    {
        LLUUID id;
        id.set(kSampleStr);
        char buf[UUID_STR_LENGTH] = {};
        id.to_chars(buf);
        ensure_equals("to_chars matches set string",
            std::string(buf), kSampleStr);
        ensure_equals("to_chars length", strlen(buf), (size_t)(UUID_STR_LENGTH - 1));
    }

    // ostream insertion delegates to the canonical 36-char form.
    template<> template<>
    void lluuid_object_t::test<12>()
    {
        LLUUID id;
        id.set(kSampleStr);
        std::ostringstream os;
        os << id;
        ensure_equals("operator<< matches asString", os.str(), id.asString());
    }

    // validate() accepts canonical forms and rejects garbage.
    template<> template<>
    void lluuid_object_t::test<13>()
    {
        ensure("validate canonical", LLUUID::validate(kSampleStr));
        ensure("reject short string", !LLUUID::validate("abc"));
        ensure("reject non-hex characters",
            !LLUUID::validate("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz"));
    }

    // std::set<LLUUID> via the default operator< yields byte-lex iteration.
    template<> template<>
    void lluuid_object_t::test<14>()
    {
        LLUUID one, two;
        one.setNull();
        two.setNull();
        one.mData[0] = 0x01;
        two.mData[0] = 0x02;

        std::set<LLUUID> s;
        s.insert(two);
        s.insert(one);

        auto it = s.begin();
        ensure_equals("first sorted entry has lowest first byte",
            (int)it->mData[0], 1);
        ++it;
        ensure_equals("second sorted entry has next first byte",
            (int)it->mData[0], 2);
    }
}
