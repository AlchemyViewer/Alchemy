/** 
* @file   llmaterialid.h
* @brief  Header file for llmaterialid
* @author Stinson@lindenlab.com
*
* $LicenseInfo:firstyear=2012&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2012, Linden Research, Inc.
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
#ifndef LL_LLMATERIALID_H
#define LL_LLMATERIALID_H

#define MATERIAL_ID_SIZE 16

#include <string>
#include <immintrin.h>
#include "llsd.h"

#include <robin_hood.h>

class LLMaterialID
{
public:
	LLMaterialID() = default;
	LLMaterialID(const LLSD& pMaterialID);
	LLMaterialID(const LLSD::Binary& pMaterialID);
	LLMaterialID(const void* pMemory);
	LLMaterialID(const LLUUID& lluid);

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

	bool operator==(const LLMaterialID& rhs) const
	{
		__m128i mm_left = load_unaligned_si128(mID);
		__m128i mm_right = load_unaligned_si128(rhs.mID);

		__m128i mm_cmp = _mm_cmpeq_epi32(mm_left, mm_right);
#if defined(AL_AVX)
		return _mm_test_all_ones(mm_cmp);
#else
		return _mm_movemask_epi8(mm_cmp) == 0xFFFF;
#endif
	}

	bool operator!=(const LLMaterialID& rhs) const
	{
		return !((*this) == rhs);
	}

	bool operator<(const LLMaterialID& rhs) const
	{
		__m128i mm_left = load_unaligned_si128(mID);
		__m128i mm_right = load_unaligned_si128(rhs.mID);

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

	bool operator>(const LLMaterialID& rhs) const
	{
		return rhs < (*this);
	}

	bool operator<=(const LLMaterialID& rhs) const
	{
		return !(rhs < (*this));
	}

	bool operator>=(const LLMaterialID& rhs) const
	{
		return !((*this) < rhs);
	}

	bool isNull() const
	{
		__m128i mm = load_unaligned_si128(mID);
#if defined(AL_AVX)
		return _mm_test_all_zeros(mm, mm) != 0;
#else
		mm = _mm_cmpeq_epi8(mm, _mm_setzero_si128());
		return _mm_movemask_epi8(mm) == 0xFFFF;
#endif
	}

	inline size_t hash() const
	{
		size_t seed = 0;
		for (U8 i = 0; i < 4; ++i)
		{
			seed ^= static_cast<size_t>(mID[i * 4]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= static_cast<size_t>(mID[i * 4 + 1]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= static_cast<size_t>(mID[i * 4 + 2]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
			seed ^= static_cast<size_t>(mID[i * 4 + 3]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
		}
		return robin_hood::hash_bytes(mID, MATERIAL_ID_SIZE);
	}
// END BOOST

	const U8*     get() const;
	void          set(const void* pMemory);
	void          clear();

	LLSD          asLLSD() const;
	std::string   asString() const;

	friend std::ostream& operator<<(std::ostream& s, const LLMaterialID &material_id);

	static const LLMaterialID null;

private:
	void parseFromBinary(const LLSD::Binary& pMaterialID);

	U8 mID[MATERIAL_ID_SIZE] = {};
} ;

namespace std {
	template <> struct hash<LLMaterialID>
	{
		size_t operator()(const LLMaterialID& id) const
		{
			return id.hash();
		}
	};
}

namespace boost {
	template<> struct hash<LLMaterialID>
	{
		size_t operator()(const LLMaterialID& id) const
		{
			return id.hash();
		}
	};
}

namespace robin_hood {
	template<> struct hash<LLMaterialID>
	{
		size_t operator()(const LLMaterialID& id) const
		{
			return id.hash();
		}
	};
}

#endif // LL_LLMATERIALID_H

