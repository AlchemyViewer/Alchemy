/**
 * @file lluuid.h
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#ifndef LL_LLUUID_H
#define LL_LLUUID_H

#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <vector>
#include "stdtypes.h"
#include "llpreprocessor.h"
#include <boost/container_hash/hash_is_avalanching.hpp>
#include <boost/functional/hash.hpp>

#if defined(_MSC_VER) && !defined(__SIZEOF_INT128__)
#include <intrin.h>     // __umulh
#endif

class LLMutex;

const S32 UUID_BYTES = 16;
const S32 UUID_WORDS = 4;
const S32 UUID_STR_LENGTH = 37; // number of bytes needed to store a UUID as a string
const S32 UUID_STR_SIZE = 36; // .size() of a UUID in a std::string
const S32 UUID_BASE85_LENGTH = 21; // including the trailing NULL.

struct uuid_time_t {
    U32 high;
    U32 low;
        };

class LL_COMMON_API LLUUID
{
public:
    //
    // CREATORS
    //
    constexpr LLUUID() noexcept = default;
    explicit constexpr LLUUID(const U8 (&bytes)[UUID_BYTES]) noexcept
    {
        for (S32 i = 0; i < UUID_BYTES; ++i) mData[i] = bytes[i];
    }
    explicit LLUUID(const char *in_string); // Convert from string.
    explicit LLUUID(const std::string& in_string); // Convert from string.
    ~LLUUID() = default;

    //
    // MANIPULATORS
    //
    void    generate();                 // Generate a new UUID
    void    generate(const std::string& stream); //Generate a new UUID based on hash of input stream

    //static versions of above for use in initializer expressions such as constructor params, etc.
    static LLUUID generateNewID();
    static LLUUID generateNewID(const std::string& stream);

    bool    set(const char *in_string, bool emit = true);   // Convert from string, if emit is false, do not emit warnings
    bool    set(const std::string& in_string, bool emit = true);    // Convert from string, if emit is false, do not emit warnings
    void    setNull();                  // Faster than setting to LLUUID::null.

    S32     cmpTime(uuid_time_t *t1, uuid_time_t *t2);
    static void    getSystemTime(uuid_time_t *timestamp);
    void    getCurrentTime(uuid_time_t *timestamp);

    //
    // ACCESSORS
    //
    bool    isNull() const;         // Faster than comparing to LLUUID::null.
    bool    notNull() const;        // Faster than comparing to LLUUID::null.
    // JC: This is dangerous.  It allows UUIDs to be cast automatically
    // to integers, among other things.  Use isNull() or notNull().
    //      operator bool() const;

    bool    operator==(const LLUUID &rhs) const;
    bool    operator!=(const LLUUID &rhs) const;
    bool    operator<(const LLUUID &rhs) const;
    bool    operator>(const LLUUID &rhs) const;

    // xor functions. Useful since any two random uuids xored together
    // will yield a determinate third random unique id that can be
    // used as a key in a single uuid that represents 2.
    const LLUUID& operator^=(const LLUUID& rhs);
    LLUUID operator^(const LLUUID& rhs) const;

    // similar to functions above, but not invertible
    // yields a third random UUID that can be reproduced from the two inputs
    // but which, given the result and one of the inputs can't be used to
    // deduce the other input
    LLUUID combine(const LLUUID& other) const;
    void combine(const LLUUID& other, LLUUID& result) const;

    friend LL_COMMON_API std::ostream&   operator<<(std::ostream& s, const LLUUID &uuid);
    friend LL_COMMON_API std::istream&   operator>>(std::istream& s, LLUUID &uuid);

    void to_chars(char* out) const;
    void to_wchars(wchar_t* out) const;
    void toString(std::string& out) const;
    void toCompressedString(std::string& out) const;

    std::string asString() const;
    std::string getString() const;

    U16 getCRC16() const;
    U32 getCRC32() const;

    static bool validate(const std::string& in_string); // Validate that the UUID string is legal.

    static const LLUUID null;
    static LLMutex * mMutex;

    static S32 getNodeID(unsigned char * node_id);

    static bool parseUUID(const std::string& buf, LLUUID* value);

    U8 mData[UUID_BYTES] {};
};
static_assert(std::is_trivially_copyable<LLUUID>::value, "LLUUID must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLUUID>::value, "LLUUID must be trivial move");
static_assert(std::is_standard_layout<LLUUID>::value, "LLUUID must be a standard layout type");

// LLUUID::null is defined out-of-line in lluuid.cpp, NOT as an inline constexpr
// variable here. LLUUID is exported (class LL_COMMON_API), which makes this
// static member dllimport in consumer modules, and a dllimport member may not
// have an in-header (inline) definition. So it is an ordinary exported constant
// defined once in the DLL. It is still constant-initialized (LLUUID has a
// constexpr default ctor), so there is no static-init-order hazard.

typedef std::vector<LLUUID> uuid_vec_t;
typedef std::set<LLUUID> uuid_set_t;

// Helper structure for ordering lluuids in stl containers.  eg:
// std::map<LLUUID, LLWidget*, lluuid_less> widget_map;
//
// (isn't this the default behavior anyway? I think we could
// everywhere replace these with uuid_set_t, but someone should
// verify.)
struct lluuid_less
{
    bool operator()(const LLUUID& lhs, const LLUUID& rhs) const
    {
        return lhs < rhs;
    }
};

typedef std::set<LLUUID, lluuid_less> uuid_list_t;
/*
 * Sub-classes for keeping transaction IDs and asset IDs
 * straight.
 */
typedef LLUUID LLAssetID;

class LL_COMMON_API LLTransactionID : public LLUUID
{
public:
    constexpr LLTransactionID() noexcept = default;

    static const LLTransactionID tnull;
    LLAssetID makeAssetID(const LLUUID& session) const;
};

// As with LLUUID::null above: defined out-of-line in lluuid.cpp because an
// exported (dllimport-in-consumers) static member can't have an inline
// definition.

namespace LL
{
namespace hash_detail
{
    // 64x64 -> 128 widening multiply. On return a holds the low half of the
    // product and b the high half. One instruction on every target we ship.
    LL_FORCE_INLINE void widening_mul(U64& a, U64& b) noexcept
    {
#if defined(__SIZEOF_INT128__)
        const unsigned __int128 r = (unsigned __int128)a * (unsigned __int128)b;
        a = (U64)r;
        b = (U64)(r >> 64);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
        const U64 lo = a * b;
        const U64 hi = __umulh(a, b);
        a = lo;
        b = hi;
#else
        // 32x32 -> 64 decomposition for targets without a widening 64-bit
        // multiply. Same result, several times the instruction count.
        const U64 al = a & 0xffffffffULL, ah = a >> 32;
        const U64 bl = b & 0xffffffffULL, bh = b >> 32;
        const U64 ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
        const U64 mid = (ll >> 32) + (lh & 0xffffffffULL) + (hl & 0xffffffffULL);
        a = (ll & 0xffffffffULL) | (mid << 32);
        b = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
#endif
    }

    // Fold a widening multiply down to 64 bits.
    LL_FORCE_INLINE U64 mul_mix(U64 a, U64 b) noexcept
    {
        widening_mul(a, b);
        return a ^ b;
    }

    // Three of the four entries of wyhash's default secret, keeping wyhash's
    // numbering so the constants can be checked against it; the 16-byte path
    // has no use for secret[2]. Each is odd with a popcount of 32, which is
    // what stops the multiply collapsing a structured operand.
    inline constexpr U64 UUID_SECRET_0 = 0xa0761d6478bd642fULL;
    inline constexpr U64 UUID_SECRET_1 = 0xe7037ed1a0b428dbULL;
    inline constexpr U64 UUID_SECRET_3 = 0x589965cc75374cc3ULL;
} // namespace hash_detail
} // namespace LL

// Canonical hash for LLUUID (also used by boost::container_hash via ADL).
//
// An LLUUID is sixteen bytes, and only sometimes sixteen *random* bytes: the
// type is also the viewer's general-purpose 16-byte key, holding tracking ids,
// composed asset keys and other structured payloads whose entropy sits in one
// lane and whose remaining bytes are constant or zero. The hash has to spread
// those as well as it spreads the 122 random bits of a real v4 UUID.
//
// This is the 16-byte case of wyhash: two dependent widening multiplies with
// the full 128-bit product carried between them. Every input bit reaches every
// output bit, so callers may take the index from either end of the value --
// a mask (`h & (n-1)`, which is what MSVC's std::unordered_map and every
// hand-rolled table do) or a shift (which is what boost's open-addressing
// tables do) both see a fully mixed result.
//
// Do not replace this with a fold of the two halves. XOR mixes nothing, and a
// constant multiply propagates carries upward only, so either one collapses
// whole families of these inputs onto a single bucket. The hash group in
// lluuid_test.cpp measures the properties that rule those out.
inline size_t hash_value(const LLUUID& id) noexcept
{
    U64 lo, hi;
    memcpy(&lo, id.mData,     sizeof(lo));
    memcpy(&hi, id.mData + 8, sizeof(hi));

    U64 a = lo ^ LL::hash_detail::UUID_SECRET_1;
    U64 b = hi ^ LL::hash_detail::UUID_SECRET_3;
    LL::hash_detail::widening_mul(a, b);
    return (size_t)LL::hash_detail::mul_mix(
        a ^ LL::hash_detail::UUID_SECRET_0 ^ (U64)UUID_BYTES,
        b ^ LL::hash_detail::UUID_SECRET_1);
}

namespace std
{
    template<> struct hash<LLUUID>
    {
        // Tells boost::unordered's open-addressing tables that the value is
        // already mixed, so they skip their own mulx pass.
        using is_avalanching = std::true_type;

        inline size_t operator()(const LLUUID& id) const noexcept
        {
            return hash_value(id);
        }
    };
}

namespace boost
{
    // Same claim for boost::hash, which is the default hasher for every
    // boost::unordered_* container keyed by an LLUUID.
    template<> struct hash_is_avalanching<boost::hash<LLUUID>> : std::true_type {};
    template<> struct hash_is_avalanching<boost::hash<LLTransactionID>> : std::true_type {};
}

#endif // LL_LLUUID_H
