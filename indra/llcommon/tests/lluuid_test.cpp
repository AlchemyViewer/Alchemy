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

#include "../test/lltut.h"

namespace tut
{
    struct lluuid_data { };
    typedef test_group<lluuid_data> lluuid_t;
    typedef lluuid_t::object lluuid_object_t;
    tut::lluuid_t tut_lluuid("LLUUID");

    static const std::string kSampleStr = "01020304-0506-0708-090a-0b0c0d0e0f10";

    // Compile-time verification that LLUUID is constexpr-constructible.
    static_assert(LLUUID{}.mData[0] == 0, "default LLUUID is zero-initialised at compile time");
    // LLUUID::null / LLTransactionID::tnull are exported out-of-line constants
    // (dllimport in consumers), so they are no longer constexpr. Their all-zero
    // value is verified at runtime in test<1> instead.

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
    // Helpers for the hash group (tests 9, 15-21).
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
    // measured in tests 15-19 is what backs that claim.
    static_assert(boost::hash_is_avalanching<boost::hash<LLUUID>>::value,
        "boost::hash<LLUUID> must be declared avalanching");
    static_assert(boost::hash_is_avalanching<std::hash<LLUUID>>::value,
        "std::hash<LLUUID> must be declared avalanching");

    // Default construction is null.
    template<> template<>
    void lluuid_object_t::test<1>()
    {
        LLUUID id;
        ensure("default-constructed isNull", id.isNull());
        ensure("default-constructed !notNull", !id.notNull());
        ensure("default-constructed == LLUUID::null", id == LLUUID::null);
        // null / tnull are the all-zero UUID (verified at runtime since they are
        // exported out-of-line constants, no longer constexpr).
        ensure("LLUUID::null isNull", LLUUID::null.isNull());
        ensure("LLTransactionID::tnull isNull", LLTransactionID::tnull.isNull());
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

        LLUUID id;
        fill_sample(id);
        // mData = {0x01, 0x02, ..., 0x10}
        // LE U32s: 0x04030201, 0x08070605, 0x0c0b0a09, 0x100f0e0d — sum 0x2824201C
        ensure_equals("sample CRC32", id.getCRC32(), (U32)0x2824201C);
        // LE U16s sum: high bytes (2+4+...+16)=72=0x48; low bytes (1+3+...+15)=64=0x40
        ensure_equals("sample CRC16", id.getCRC16(), (U16)0x4840);
    }

    // Hash: the four spellings of it agree, and equal IDs hash equally.
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
        ensure_equals("boost::hash matches hash_value",
            boost::hash<LLUUID>{}(a), hash_value(b));

        // A different UUID should (overwhelmingly likely) hash differently.
        LLUUID c;
        c.set("ffffffff-ffff-ffff-ffff-ffffffffffff");
        ensure_not_equals("distinct IDs typically hash differently",
            std::hash<LLUUID>{}(a), std::hash<LLUUID>{}(c));

        // LLTransactionID and LLAssetID reach the same hash through the base.
        LLTransactionID tid;
        fill_sample(tid);
        ensure_equals("LLTransactionID hashes as its LLUUID bytes",
            boost::hash<LLTransactionID>{}(tid), hash_value(a));
        const LLAssetID aid = a;
        ensure_equals("LLAssetID hashes as its LLUUID bytes",
            std::hash<LLAssetID>{}(aid), hash_value(a));
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

    // Both halves of the value must reach the hash independently.
    //
    // This is the test that fails the moment someone replaces the hash with
    // `lo ^ hi` or any other fold of the two halves: a fold gives the same
    // answer for (lo, hi) and (lo^m, hi^m), and for (lo, hi) and (hi, lo).
    template<> template<>
    void lluuid_object_t::test<15>()
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
        ensure_equals("ids with an equal XOR of halves must not share a hash "
                      "(the hash is not a fold of the two halves)",
            xor_equivalent, 0);
        ensure_equals("swapping the two halves must change the hash",
            half_swapped, 0);
    }

    // Strict avalanche: flipping any one of the 128 input bits must flip each
    // output bit about half the time. 256 bases x 128 flips is 32768 trials
    // per output bit, so the sampling error is well under a percent and the
    // [0.35, 0.65] band is generous for a good hash while far out of reach for
    // a fold (0.016) or a single constant multiply (0.18).
    template<> template<>
    void lluuid_object_t::test<16>()
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

        ensure_equals("no single input bit flip may leave the hash unchanged",
            fixed_points, 0);
        for (int o = 0; o < kOutputBits; ++o)
        {
            const double rate = (double)flips[o] / (double)trials;
            ensure("every output bit must flip near half the time for a "
                   "single-bit input change (output bit "
                   + std::to_string(o) + " flipped "
                   + std::to_string(rate) + " of the time)",
                rate >= 0.35 && rate <= 0.65);
        }
    }

    // Entropy confined to one 32-bit lane, the rest of the value constant.
    // This is what a composed key looks like: a fixed prefix and a counter.
    // Both ends of the hash have to spread it, because containers index from
    // the low bits or the high bits depending on which one you reach for.
    template<> template<>
    void lluuid_object_t::test<17>()
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
            const std::string where = " (lane " + std::to_string(lane) + ")";

            ensure("counter in one lane must not pile up in the low bits" + where,
                spread.mLowMax <= 3 * mean);
            ensure("counter in one lane must not pile up in the high bits" + where,
                spread.mHighMax <= 3 * mean);
            ensure_equals("counter in one lane must reach every low-bit bucket" + where,
                spread.mLowEmpty, 0);
            ensure_equals("counter in one lane must reach every high-bit bucket" + where,
                spread.mHighEmpty, 0);
        }
    }

    // The mixed structured corpus: no full 64-bit collisions at all, and no
    // bucket carrying more than three times its share at either end.
    template<> template<>
    void lluuid_object_t::test<18>()
    {
        std::vector<LLUUID> corpus;
        build_structured_corpus(corpus);
        ensure("structured corpus is large enough to be meaningful",
            corpus.size() > 8000);

        std::set<size_t> seen;
        int collisions = 0;
        for (const LLUUID& id : corpus)
        {
            if (!seen.insert(hash_value(id)).second) ++collisions;
        }
        ensure_equals("structured 16-byte payloads must not collide outright",
            collisions, 0);

        const BucketSpread spread(corpus);
        const int mean = (int)(corpus.size() / BucketSpread::NUM_BUCKETS);
        ensure("structured payloads must not pile up in the low bits",
            spread.mLowMax <= 3 * mean);
        ensure("structured payloads must not pile up in the high bits",
            spread.mHighMax <= 3 * mean);
    }

    // Degenerate inputs: the null UUID must not hash to the zero sentinel, and
    // the 128 one-bit values must all land somewhere different.
    template<> template<>
    void lluuid_object_t::test<19>()
    {
        ensure_not_equals("the null UUID must not hash to zero",
            hash_value(LLUUID::null), (size_t)0);

        std::set<size_t> seen;
        for (int bit = 0; bit < UUID_BYTES * 8; ++bit)
        {
            seen.insert(hash_value(make_bit_id(bit)));
        }
        ensure_equals("each of the 128 one-bit values hashes differently",
            seen.size(), (size_t)(UUID_BYTES * 8));
    }

    // Pinned values for the hash. Nothing persists a hash of an LLUUID, so
    // these exist to catch an accidental change -- a mistyped constant, a
    // reversed half -- rather than to freeze the algorithm. Changing the hash
    // on purpose means updating these three lines, deliberately.
    //
    // The values assume a little-endian host, as test<8> does.
    template<> template<>
    void lluuid_object_t::test<20>()
    {
        if constexpr (sizeof(size_t) == 8)
        {
            LLUUID sample;
            fill_sample(sample);
            LLUUID ones;
            std::memset(ones.mData, 0xff, UUID_BYTES);

            ensure_equals("pinned hash of the null UUID",
                (U64)hash_value(LLUUID::null), 0xc64369fbab9cf4d7ULL);
            ensure_equals("pinned hash of 01020304-0506-0708-090a-0b0c0d0e0f10",
                (U64)hash_value(sample), 0x47c8de24eeda6e35ULL);
            ensure_equals("pinned hash of the all-ones UUID",
                (U64)hash_value(ones), 0x24af15be29dd63f0ULL);
        }
    }

    // The containers the viewer actually keys by LLUUID, exercised with the
    // structured corpus rather than with random UUIDs.
    template<> template<>
    void lluuid_object_t::test<21>()
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
        ensure_equals("flat map holds every distinct key", flat.size(), corpus.size());
        ensure_equals("node map holds every distinct key", node.size(), corpus.size());

        for (size_t i = 0; i < corpus.size(); ++i)
        {
            auto fit = flat.find(corpus[i]);
            ensure("flat map finds every key", fit != flat.end());
            ensure_equals("flat map returns the right value", fit->second, i);

            auto nit = node.find(corpus[i]);
            ensure("node map finds every key", nit != node.end());
            ensure_equals("node map returns the right value", nit->second, i);
        }

        // A key built from the same bytes by a different route still matches.
        LLUUID probe;
        probe.set(kSampleStr);
        LLUUID same;
        fill_sample(same);
        ensure_equals("equal bytes hash equally regardless of how they arrived",
            hash_value(probe), hash_value(same));
    }
}
