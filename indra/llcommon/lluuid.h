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

#include <iostream>
#include <set>
#include <vector>
#include <functional>
#include <boost/functional/hash.hpp>
#include "stdtypes.h"
#include "llpreprocessor.h"
#include <immintrin.h>

#include <robin_hood.h>

class LLMutex;

const S32 UUID_BYTES = 16;
const S32 UUID_WORDS = 4;
const S32 UUID_STR_LENGTH = 37;	// actually wrong, should be 36 and use size below
const S32 UUID_STR_SIZE = 37;
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
	LLUUID() = default;
	explicit LLUUID(const char *in_string); // Convert from string.
	explicit LLUUID(const std::string& in_string); // Convert from string.

	//
	// MANIPULATORS
	//
	void	generate();					// Generate a new UUID
	void	generate(const std::string& stream); //Generate a new UUID based on hash of input stream

	//static versions of above for use in initializer expressions such as constructor params, etc. 
	static LLUUID generateNewID();	
	static LLUUID generateNewID(const std::string& stream);	//static version of above for use in initializer expressions such as constructor params, etc. 

	BOOL	set(const char *in_string, BOOL emit = TRUE);	// Convert from string, if emit is FALSE, do not emit warnings
	BOOL	set(const std::string& in_string, BOOL emit = TRUE);	// Convert from string, if emit is FALSE, do not emit warnings
	void	setNull();					// Faster than setting to LLUUID::null.

    S32     cmpTime(uuid_time_t *t1, uuid_time_t *t2);
	static void    getSystemTime(uuid_time_t *timestamp);
	void    getCurrentTime(uuid_time_t *timestamp);

	//
	// ACCESSORS
	//

	// BEGIN BOOST
	// Contains code from the Boost Library with license below.
	/*
	 *            Copyright Andrey Semashev 2013.
	 * Distributed under the Boost Software License, Version 1.0.
	 *    (See accompanying file LICENSE_1_0.txt or copy at
	 *          http://www.boost.org/LICENSE_1_0.txt)
	 */
	LL_FORCE_INLINE __m128i load_unaligned_si128(const U8* p) const
	{
#if defined(AL_AVX)
		return _mm_lddqu_si128(reinterpret_cast<const __m128i*>(p));
#else
		return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
#endif
	}

	BOOL isNull() const // Faster than comparing to LLUUID::null.
	{
		__m128i mm = load_unaligned_si128(mData);
#if defined(AL_AVX)
		return _mm_test_all_zeros(mm, mm) != 0;
#else
		mm = _mm_cmpeq_epi8(mm, _mm_setzero_si128());
		return _mm_movemask_epi8(mm) == 0xFFFF;
#endif
	}

	BOOL notNull() const // Faster than comparing to LLUUID::null.
	{
		return !isNull();
	}
	// JC: This is dangerous.  It allows UUIDs to be cast automatically
	// to integers, among other things.  Use isNull() or notNull().
	//		operator bool() const;

	// JC: These must return real bool's (not BOOLs) or else use of the STL
	// will generate bool-to-int performance warnings.
	bool operator==(const LLUUID& rhs) const
	{
		__m128i mm_left = load_unaligned_si128(mData);
		__m128i mm_right = load_unaligned_si128(rhs.mData);

		__m128i mm_cmp = _mm_cmpeq_epi32(mm_left, mm_right);
#if defined(AL_AVX)
		return _mm_test_all_ones(mm_cmp);
#else
		return _mm_movemask_epi8(mm_cmp) == 0xFFFF;
#endif
	}

	bool operator!=(const LLUUID& rhs) const
	{
		return !((*this) == rhs);
	}

	bool operator<(const LLUUID& rhs) const
	{
		__m128i mm_left = load_unaligned_si128(mData);
		__m128i mm_right = load_unaligned_si128(rhs.mData);

		// To emulate lexicographical_compare behavior we have to perform two comparisons - the forward and reverse one.
		// Then we know which bytes are equivalent and which ones are different, and for those different the comparison results
		// will be opposite. Then we'll be able to find the first differing comparison result (for both forward and reverse ways),
		// and depending on which way it is for, this will be the result of the operation. There are a few notes to consider:
		//
		// 1. Due to little endian byte order the first bytes go into the lower part of the xmm registers,
		//    so the comparison results in the least significant bits will actually be the most signigicant for the final operation result.
		//    This means we have to determine which of the comparison results have the least significant bit on, and this is achieved with
		//    the "(x - 1) ^ x" trick.
		// 2. Because there is only signed comparison in SSE/AVX, we have to invert byte comparison results whenever signs of the corresponding
		//    bytes are different. I.e. in signed comparison it's -1 < 1, but in unsigned it is the opposite (255 > 1). To do that we XOR left and right,
		//    making the most significant bit of each byte 1 if the signs are different, and later apply this mask with another XOR to the comparison results.
		// 3. pcmpgtw compares for "greater" relation, so we swap the arguments to get what we need.

		const __m128i mm_signs_mask = _mm_xor_si128(mm_left, mm_right);

		__m128i mm_cmp = _mm_cmpgt_epi8(mm_right, mm_left), mm_rcmp = _mm_cmpgt_epi8(mm_left, mm_right);

		mm_cmp = _mm_xor_si128(mm_signs_mask, mm_cmp);
		mm_rcmp = _mm_xor_si128(mm_signs_mask, mm_rcmp);

		uint32_t cmp = static_cast<uint32_t>(_mm_movemask_epi8(mm_cmp)), rcmp = static_cast<uint32_t>(_mm_movemask_epi8(mm_rcmp));

		cmp = (cmp - 1u) ^ cmp;
		rcmp = (rcmp - 1u) ^ rcmp;

		return static_cast<uint16_t>(cmp) < static_cast<uint16_t>(rcmp);
	}

	bool operator>(const LLUUID& rhs) const
	{
		return rhs < (*this);
	}

	inline size_t hash() const
	{
		return robin_hood::hash_bytes(mData, UUID_BYTES);
	}
	// END BOOST

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

	friend LL_COMMON_API std::ostream&	 operator<<(std::ostream& s, const LLUUID &uuid);
	friend LL_COMMON_API std::istream&	 operator>>(std::istream& s, LLUUID &uuid);

	void toString(char *out) const;		// Does not allocate memory, needs 36 characters (including \0)
	void toString(std::string& out) const;
	void toCompressedString(char *out) const;	// Does not allocate memory, needs 17 characters (including \0)
	void toCompressedString(std::string& out) const;

	std::string asString() const;
	std::string getString() const;

	U16 getCRC16() const;
	U32 getCRC32() const;

	static BOOL validate(const std::string& in_string); // Validate that the UUID string is legal.

	static const LLUUID null;
	static LLMutex * mMutex;

	static U32 getRandomSeed();
	static S32 getNodeID(unsigned char * node_id);

	static BOOL parseUUID(const std::string& buf, LLUUID* value);

	U8 mData[UUID_BYTES] = {};
};
static_assert(std::is_trivially_copyable<LLUUID>::value, "LLUUID must be trivial copy");
static_assert(std::is_trivially_move_assignable<LLUUID>::value, "LLUUID must be trivial move");
static_assert(std::is_standard_layout<LLUUID>::value, "LLUUID must be a standard layout type");

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
		return (lhs < rhs) ? true : false;
	}
};

typedef std::set<LLUUID, lluuid_less> uuid_list_t;

namespace std {
	template <> struct hash<LLUUID>
	{
		size_t operator()(const LLUUID & id) const
		{
			return id.hash();
		}
	};
}

namespace boost {
	template<> struct hash<LLUUID>
	{
		size_t operator()(const LLUUID& id) const
		{
			return id.hash();
		}
	};
}

namespace robin_hood {
	template<> struct hash<LLUUID>
	{
		size_t operator()(const LLUUID& id) const
		{
			return id.hash();
		}
	};
}

/*
 * Sub-classes for keeping transaction IDs and asset IDs
 * straight.
 */
typedef LLUUID LLAssetID;

class LL_COMMON_API LLTransactionID : public LLUUID
{
public:
	LLTransactionID() : LLUUID() { }
	
	static const LLTransactionID tnull;
	LLAssetID makeAssetID(const LLUUID& session) const;
};

#endif


