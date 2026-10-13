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
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include "../lluuid.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    static const std::string kSampleStr = "01020304-0506-0708-090a-0b0c0d0e0f10";

    // Compile-time verification that LLUUID is constexpr-constructible.
    static_assert(LLUUID{}.mData[0] == 0, "default LLUUID is zero-initialised at compile time");
    // LLUUID::null / LLTransactionID::tnull are exported out-of-line constants
    // (dllimport in consumers), so they are no longer constexpr. Their all-zero
    // value is verified at runtime in the default-construction case instead.

    constexpr U8 kConstexprBytes[UUID_BYTES] = {
        0xde, 0xad, 0xbe, 0xef, 0xfe, 0xed, 0xfa, 0xce,
        0xca, 0xfe, 0xba, 0xbe, 0x12, 0x34, 0x56, 0x78
    };
    static_assert(LLUUID(kConstexprBytes).mData[0]  == 0xde, "constexpr byte-array ctor copies bytes");
    static_assert(LLUUID(kConstexprBytes).mData[15] == 0x78, "constexpr byte-array ctor reaches last byte");
    static void fill_sample(LLUUID& id)
    {
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            id.mData[i] = (U8)(i + 1);
        }
    }

    // ---------------------------------------------------------------------
    // Helpers for the hash cases.
    //
    // An LLUUID is sixteen bytes, and the viewer uses it as its general
    // 16-byte key, not only as a random v4 UUID: tracking ids, composed asset
    // keys, identifiers with a small integer in one lane. The tests below
    // measure the hash on those structured payloads rather than on random
    // ones, since almost any hash looks good on 122 random bits.
    // ---------------------------------------------------------------------

    // Build an LLUUID out of two little-endian halves.
    static LLUUID make_id(U64 lo, U64 hi)
    {
        LLUUID id;
        std::memcpy(id.mData,     &lo, sizeof(lo));
        std::memcpy(id.mData + 8, &hi, sizeof(hi));
        return id;
    }

    static LLUUID make_bit_id(int bit)
    {
        LLUUID id;
        id.mData[bit >> 3] = (U8)(1u << (bit & 7));
        return id;
    }

    // splitmix64, so the sample sets are the same on every platform and run.
    static U64 splitmix64(U64& state)
    {
        U64 z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // Occupancy of a 256-bucket table indexed from one end of the hash or the
    // other: containers take the index from the low bits (MSVC's
    // std::unordered_map, any hand-rolled `h & (n-1)`) or from the high bits
    // (boost's open-addressing tables), so both ends have to be mixed.
    struct BucketSpread
    {
        static constexpr size_t NUM_BUCKETS = 256;
        int mLowMax = 0, mHighMax = 0, mLowEmpty = 0, mHighEmpty = 0;

        explicit BucketSpread(const std::vector<LLUUID>& ids)
        {
            std::vector<int> low(NUM_BUCKETS, 0), high(NUM_BUCKETS, 0);
            for (const LLUUID& id : ids)
            {
                const size_t h = hash_value(id);
                ++low[h & (NUM_BUCKETS - 1)];
                ++high[(h >> (sizeof(size_t) * 8 - 8)) & (NUM_BUCKETS - 1)];
            }
            for (size_t i = 0; i < NUM_BUCKETS; ++i)
            {
                if (low[i]  > mLowMax)  mLowMax  = low[i];
                if (high[i] > mHighMax) mHighMax = high[i];
                if (!low[i])  ++mLowEmpty;
                if (!high[i]) ++mHighEmpty;
            }
        }
    };

    // Structured payloads: a counter walking each 8-byte window, every
    // one-bit value, a pair of small ints in the outermost lanes, and short
    // ASCII in a space-padded field.
    static void build_structured_corpus(std::vector<LLUUID>& out)
    {
        std::set<std::string> seen;
        auto add = [&](const LLUUID& id)
        {
            if (seen.insert(std::string((const char*)id.mData, UUID_BYTES)).second)
            {
                out.push_back(id);
            }
        };

        for (int off = 0; off + 8 <= UUID_BYTES; ++off)
        {
            for (U64 i = 0; i < 512; ++i)
            {
                LLUUID id;
                std::memcpy(id.mData + off, &i, sizeof(i));
                add(id);
            }
        }
        for (int bit = 0; bit < UUID_BYTES * 8; ++bit)
        {
            add(make_bit_id(bit));
        }
        for (U32 a = 0; a < 64; ++a)
        {
            for (U32 b = 0; b < 64; ++b)
            {
                LLUUID id;
                std::memcpy(id.mData,      &a, sizeof(a));
                std::memcpy(id.mData + 12, &b, sizeof(b));
                add(id);
            }
        }
        for (int i = 0; i < 512; ++i)
        {
            LLUUID id;
            std::memset(id.mData, ' ', UUID_BYTES);
            const std::string text = "obj_" + std::to_string(i);
            std::memcpy(id.mData, text.data(),
                        text.size() < (size_t)UUID_BYTES ? text.size() : (size_t)UUID_BYTES);
            add(id);
        }
    }

    // The hash tells boost::unordered's open-addressing tables that it is
    // already mixed, which makes them skip their own mulx pass. Every property
    // measured in the cases from both halves to the degenerate inputs is what
    // backs that claim.
    static_assert(boost::hash_is_avalanching<boost::hash<LLUUID>>::value,
        "boost::hash<LLUUID> must be declared avalanching");
    static_assert(boost::hash_is_avalanching<std::hash<LLUUID>>::value,
        "std::hash<LLUUID> must be declared avalanching");

    // Default construction is null.
    TEST_CASE("LLUUID: default construction is null and so are null and tnull", "[LLUUID]")
    {
        LLUUID id;
        CHECK(id.isNull());
        CHECK_FALSE(id.notNull());
        CHECK(id == LLUUID::null);
        // null / tnull are the all-zero UUID (verified at runtime since they are
        // exported out-of-line constants, no longer constexpr).
        CHECK(LLUUID::null.isNull());
        CHECK(LLTransactionID::tnull.isNull());
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            CAPTURE(i);
            REQUIRE((int)id.mData[i] == 0);
        }
    }

    TEST_CASE("LLUUID: setNull zeroes every byte", "[LLUUID]")
    {
        LLUUID id;
        fill_sample(id);
        CHECK(id.notNull());
        id.setNull();
        CHECK(id.isNull());
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            CAPTURE(i);
            REQUIRE((int)id.mData[i] == 0);
        }
    }

    // String parsing round-trip via set() / asString().
    TEST_CASE("LLUUID: set() parses a string that asString() gives back", "[LLUUID]")
    {
        LLUUID id;
        CHECK(id.set(kSampleStr));
        CHECK(id.asString() == kSampleStr);
        for (int i = 0; i < UUID_BYTES; ++i)
        {
            CAPTURE(i);
            REQUIRE((int)id.mData[i] == i + 1);
        }
    }

    TEST_CASE("LLUUID: set() with an empty string clears to null; invalid input also nulls", "[LLUUID]")
    {
        LLUUID id;
        fill_sample(id);

        CHECK(id.set(std::string()));
        CHECK(id.isNull());

        fill_sample(id);
        CHECK_FALSE(id.set(std::string("not-a-uuid"), false));
        CHECK(id.isNull());
    }

    // Equality / inequality for matching and one-byte-different UUIDs.
    TEST_CASE("LLUUID: equality and inequality for matching and one-byte-different UUIDs", "[LLUUID]")
    {
        LLUUID a, b, c;
        fill_sample(a);
        fill_sample(b);
        fill_sample(c);
        c.mData[UUID_BYTES - 1] ^= 0x01;

        CHECK(a == b);
        CHECK_FALSE(a != b);
        CHECK(a != c);
        CHECK_FALSE(a == c);
    }

    // Byte-lexicographic sort order. IW's comment in lluuid.cpp warns against
    // optimizing operator< with word-wide reads precisely because the word
    // interpretation reverses bytes on LE. This test pins the byte-lex behavior.
    TEST_CASE("LLUUID: operator< orders bytes lexicographically", "[LLUUID]")
    {
        LLUUID low, high;
        low.setNull();
        high.setNull();
        low.mData[1]  = 0x01; // word value 0x100 on LE
        high.mData[0] = 0x01; // word value 0x001 on LE — but first byte wins

        CHECK(low <  high);
        CHECK(high >  low);
        CHECK_FALSE(low < low);
        CHECK_FALSE(low > low);
    }

    // XOR, by ^ or in place by ^=, is its own inverse (XOR twice cancels)
    // and commutative.
    TEST_CASE("LLUUID: XOR by an id twice cancels either way round and XOR with itself is null", "[LLUUID]")
    {
        LLUUID a, b;
        fill_sample(a);
        const LLUUID original = a;

        b.set("11223344-5566-7788-99aa-bbccddeeff00");

        LLUUID xored = a ^ b;
        CHECK(xored != a);
        UNSCOPED_INFO("commutative");
        CHECK((b ^ a) == xored);

        LLUUID xored_twice = xored ^ b;
        CHECK(xored_twice == original);

        a ^= b;
        UNSCOPED_INFO("^= is ^ in place");
        CHECK(a == xored);
        a ^= b;
        CHECK(a == original);

        LLUUID xored_self = a ^ a;
        CHECK(xored_self.isNull());
    }

    // Bit-level regression for the digest/CRC functions. These values are
    // computed assuming the host is little-endian (effectively all targets);
    // they pin the pre-UB-fix numerical output so callers that persist or
    // compare these checksums are unaffected.
    TEST_CASE("LLUUID: getCRC32 and getCRC16 give their pinned values", "[LLUUID]")
    {
        CHECK(LLUUID::null.getCRC32() == (U32)0);
        CHECK(LLUUID::null.getCRC16() == (U16)0);

        LLUUID id;
        fill_sample(id);
        // mData = {0x01, 0x02, ..., 0x10}
        // LE U32s: 0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d — sum 0x2824201C
        CHECK(id.getCRC32() == (U32)0x2824201C);
        // LE U16s sum: high bytes (2+4+...+16)=72=0x48; low bytes (1+3+...+15)=64=0x40
        CHECK(id.getCRC16() == (U16)0x4840);
    }

    // Hash: the four spellings of it agree, and equal IDs hash equally.
    TEST_CASE("LLUUID: the hash spellings agree and equal ids hash equally", "[LLUUID]")
    {
        LLUUID a, b;
        fill_sample(a);
        fill_sample(b);
        CHECK(std::hash<LLUUID>{}(a) == std::hash<LLUUID>{}(b));
        CHECK(std::hash<LLUUID>{}(a) == hash_value(b));
        CHECK(boost::hash<LLUUID>{}(a) == hash_value(b));

        // A different UUID should (overwhelmingly likely) hash differently.
        LLUUID c;
        c.set("ffffffff-ffff-ffff-ffff-ffffffffffff");
        CHECK(std::hash<LLUUID>{}(a) != std::hash<LLUUID>{}(c));

        // LLTransactionID and LLAssetID reach the same hash through the base.
        LLTransactionID tid;
        fill_sample(tid);
        CHECK(boost::hash<LLTransactionID>{}(tid) == hash_value(a));
        const LLAssetID aid = a;
        CHECK(std::hash<LLAssetID>{}(aid) == hash_value(a));
    }

    // generateNewID produces distinct, non-null UUIDs.
    TEST_CASE("LLUUID: generateNewID produces distinct non-null valid UUIDs", "[LLUUID]")
    {
        LLUUID a = LLUUID::generateNewID();
        LLUUID b = LLUUID::generateNewID();
        CHECK(a.notNull());
        CHECK(b.notNull());
        CHECK(a != b);
        // validate() should accept the generated string form
        CHECK(LLUUID::validate(a.asString()));
    }

    // to_chars emits exactly UUID_STR_LENGTH-1 chars in canonical 8-4-4-4-12 form.
    TEST_CASE("LLUUID: to_chars emits UUID_STR_LENGTH-1 chars in canonical 8-4-4-4-12 form", "[LLUUID]")
    {
        LLUUID id;
        id.set(kSampleStr);
        char buf[UUID_STR_LENGTH] = {};
        id.to_chars(buf);
        CHECK(std::string(buf) == kSampleStr);
        CHECK(strlen(buf) == (size_t)(UUID_STR_LENGTH - 1));
    }

    TEST_CASE("LLUUID: ostream insertion delegates to the canonical 36-char form", "[LLUUID]")
    {
        LLUUID id;
        id.set(kSampleStr);
        std::ostringstream os;
        os << id;
        CHECK(os.str() == id.asString());
    }

    TEST_CASE("LLUUID: validate() accepts canonical forms and rejects garbage", "[LLUUID]")
    {
        CHECK(LLUUID::validate(kSampleStr));
        CHECK_FALSE(LLUUID::validate("abc"));
        CHECK_FALSE(LLUUID::validate("zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz"));
    }

    TEST_CASE("LLUUID: std::set<LLUUID> via the default operator< yields byte-lex iteration", "[LLUUID]")
    {
        LLUUID one, two;
        one.setNull();
        two.setNull();
        one.mData[0] = 0x01;
        two.mData[0] = 0x02;

        std::set<LLUUID> s;
        s.insert(two);
        s.insert(one);
        REQUIRE(s.size() == 2);

        auto it = s.begin();
        CHECK((int)it->mData[0] == 1);
        ++it;
        CHECK((int)it->mData[0] == 2);
    }

    // Both halves of the value must reach the hash independently.
    //
    // This is the test that fails the moment someone replaces the hash with
    // `lo ^ hi` or any other fold of the two halves: a fold gives the same
    // answer for (lo, hi) and (lo^m, hi^m), and for (lo, hi) and (hi, lo).
    TEST_CASE("LLUUID: both halves of the value reach the hash independently", "[LLUUID]")
    {
        U64 state = 0x5eed01;
        int xor_equivalent = 0;
        int half_swapped = 0;
        for (int i = 0; i < 4096; ++i)
        {
            const U64 lo = splitmix64(state);
            const U64 hi = splitmix64(state);
            const U64 m  = splitmix64(state) | 1;

            const size_t h = hash_value(make_id(lo, hi));
            if (h == hash_value(make_id(lo ^ m, hi ^ m))) ++xor_equivalent;
            if (h == hash_value(make_id(hi, lo)))         ++half_swapped;
        }
        UNSCOPED_INFO("ids with an equal XOR of halves must not share a hash "
                      "(the hash is not a fold of the two halves)");
        CHECK(xor_equivalent == 0);
        UNSCOPED_INFO("swapping the two halves must change the hash");
        CHECK(half_swapped == 0);
    }

    // Strict avalanche: flipping any one of the 128 input bits must flip each
    // output bit about half the time. 256 bases x 128 flips is 32768 trials
    // per output bit, so the sampling error is well under a percent and the
    // [0.35, 0.65] band is generous for a good hash while far out of reach for
    // a fold (0.016) or a single constant multiply (0.18).
    TEST_CASE("LLUUID: flipping any input bit flips each output bit about half the time", "[LLUUID]")
    {
        const int kBases = 256;
        const int kOutputBits = (int)(sizeof(size_t) * 8);
        std::vector<int> flips(kOutputBits, 0);
        U64 state = 0x4242;
        int trials = 0;
        int fixed_points = 0;

        for (int n = 0; n < kBases; ++n)
        {
            const LLUUID base = make_id(splitmix64(state), splitmix64(state));
            const size_t hb = hash_value(base);
            for (int bit = 0; bit < UUID_BYTES * 8; ++bit)
            {
                LLUUID v = base;
                v.mData[bit >> 3] ^= (U8)(1u << (bit & 7));
                const size_t d = hash_value(v) ^ hb;
                if (!d)
                {
                    ++fixed_points;
                }
                for (int o = 0; o < kOutputBits; ++o)
                {
                    if ((d >> o) & 1) ++flips[o];
                }
                ++trials;
            }
        }

        UNSCOPED_INFO("no single input bit flip may leave the hash unchanged");
        CHECK(fixed_points == 0);
        for (int o = 0; o < kOutputBits; ++o)
        {
            const double rate = (double)flips[o] / (double)trials;
            CAPTURE(o);
            REQUIRE(rate >= 0.35);
            REQUIRE(rate <= 0.65);
        }
    }

    // Entropy confined to one 32-bit lane, the rest of the value constant.
    // This is what a composed key looks like: a fixed prefix and a counter.
    // Both ends of the hash have to spread it, because containers index from
    // the low bits or the high bits depending on which one you reach for.
    TEST_CASE("LLUUID: a counter in one 32-bit lane spreads over both ends of the hash", "[LLUUID]")
    {
        for (int lane = 0; lane < 4; ++lane)
        {
            std::vector<LLUUID> ids;
            ids.reserve(4096);
            for (U32 i = 0; i < 4096; ++i)
            {
                LLUUID id;
                std::memset(id.mData, 0xa5, UUID_BYTES);
                std::memcpy(id.mData + lane * 4, &i, sizeof(i));
                ids.push_back(id);
            }

            const BucketSpread spread(ids);
            const int mean = (int)(ids.size() / BucketSpread::NUM_BUCKETS); // 16

            CAPTURE(lane);
            REQUIRE(spread.mLowMax <= 3 * mean);
            REQUIRE(spread.mHighMax <= 3 * mean);
            REQUIRE(spread.mLowEmpty == 0);
            REQUIRE(spread.mHighEmpty == 0);
        }
    }

    // The mixed structured corpus: no full 64-bit collisions at all, and no
    // bucket carrying more than three times its share at either end.
    TEST_CASE("LLUUID: the structured corpus has no full collisions and no crowded buckets", "[LLUUID]")
    {
        std::vector<LLUUID> corpus;
        build_structured_corpus(corpus);
        UNSCOPED_INFO("structured corpus is large enough to be meaningful");
        REQUIRE(corpus.size() > 8000);

        std::set<size_t> seen;
        int collisions = 0;
        for (const LLUUID& id : corpus)
        {
            if (!seen.insert(hash_value(id)).second) ++collisions;
        }
        CHECK(collisions == 0);

        const BucketSpread spread(corpus);
        const int mean = (int)(corpus.size() / BucketSpread::NUM_BUCKETS);
        CHECK(spread.mLowMax <= 3 * mean);
        CHECK(spread.mHighMax <= 3 * mean);
    }

    // Degenerate inputs: the null UUID must not hash to the zero sentinel, and
    // the 128 one-bit values must all land somewhere different.
    TEST_CASE("LLUUID: the null UUID hashes nonzero and the one-bit values hash apart", "[LLUUID]")
    {
        CHECK(hash_value(LLUUID::null) != (size_t)0);

        std::set<size_t> seen;
        for (int bit = 0; bit < UUID_BYTES * 8; ++bit)
        {
            seen.insert(hash_value(make_bit_id(bit)));
        }
        CHECK(seen.size() == (size_t)(UUID_BYTES * 8));
    }

    // Pinned values for the hash. Nothing persists a hash of an LLUUID, so
    // these exist to catch an accidental change -- a mistyped constant, a
    // reversed half -- rather than to freeze the algorithm. Changing the hash
    // on purpose means updating these three lines, deliberately.
    //
    // The values assume a little-endian host, as the CRC regression does.
    TEST_CASE("LLUUID: hash_value gives its pinned values on 64-bit hosts", "[LLUUID]")
    {
        if constexpr (sizeof(size_t) == 8)
        {
            LLUUID sample;
            fill_sample(sample);
            LLUUID ones;
            std::memset(ones.mData, 0xff, UUID_BYTES);

            CHECK((U64)hash_value(LLUUID::null) == 0xc64369fbab9cf4d7ULL);
            UNSCOPED_INFO("the sample is 01020304-0506-0708-090a-0b0c0d0e0f10");
            CHECK((U64)hash_value(sample) == 0x47c8de24eeda6e35ULL);
            CHECK((U64)hash_value(ones) == 0x24af15be29dd63f0ULL);
        }
    }

    // The containers the viewer actually keys by LLUUID, exercised with the
    // structured corpus rather than with random UUIDs.
    TEST_CASE("LLUUID: the viewer's LLUUID-keyed maps hold and find every structured key", "[LLUUID]")
    {
        std::vector<LLUUID> corpus;
        build_structured_corpus(corpus);

        boost::unordered_flat_map<LLUUID, size_t> flat;
        std::unordered_map<LLUUID, size_t> node;
        for (size_t i = 0; i < corpus.size(); ++i)
        {
            flat.emplace(corpus[i], i);
            node.emplace(corpus[i], i);
        }
        CHECK(flat.size() == corpus.size());
        CHECK(node.size() == corpus.size());

        for (size_t i = 0; i < corpus.size(); ++i)
        {
            auto fit = flat.find(corpus[i]);
            REQUIRE(fit != flat.end());
            REQUIRE(fit->second == i);

            auto nit = node.find(corpus[i]);
            REQUIRE(nit != node.end());
            REQUIRE(nit->second == i);
        }

        // A key built from the same bytes by a different route still matches.
        LLUUID probe;
        probe.set(kSampleStr);
        LLUUID same;
        fill_sample(same);
        CHECK(hash_value(probe) == hash_value(same));
    }
}
