/** 
 * @file lluuid.cpp
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

#include "linden_common.h"

// We can't use WIN32_LEAN_AND_MEAN here, needs lots of includes.
#if LL_WINDOWS
#include "llwin32headerslean.h"
// ugh, this is ugly.  We need to straighten out our linking for this library
#pragma comment(lib, "IPHLPAPI.lib")
#include <iphlpapi.h>
#endif

#include "lldefs.h"
#include "llerror.h"

#include "lluuid.h"
#include "llerror.h"
#include "llrand.h"
#include "llmd5.h"
#include "llstring.h"
#include "lltimer.h"
#include "llthread.h"
#include "llmutex.h"

const LLUUID LLUUID::null;
const LLTransactionID LLTransactionID::tnull;

// static 
LLMutex * LLUUID::mMutex = NULL;



/*

NOT DONE YET!!!

static char BASE85_TABLE[] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
	'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
	'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',
	'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
	'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',
	'y', 'z', '!', '#', '$', '%', '&', '(', ')', '*',
	'+', '-', ';', '[', '=', '>', '?', '@', '^', '_',
	'`', '{', '|', '}', '~', '\0'
};


void encode( char * fiveChars, unsigned int word ) throw( )
{
for( int ix = 0; ix < 5; ++ix ) {
fiveChars[4-ix] = encodeTable[ word % 85];
word /= 85;
}
}

To decode:
unsigned int decode( char const * fiveChars ) throw( bad_input_data )
{
unsigned int ret = 0;
for( int ix = 0; ix < 5; ++ix ) {
char * s = strchr( encodeTable, fiveChars[ ix ] );
if( s == 0 ) LLTHROW(bad_input_data());
ret = ret * 85 + (s-encodeTable);
}
return ret;
}

void LLUUID::toBase85(char* out)
{
	U32* me = (U32*)&(mData[0]);
	for(S32 i = 0; i < 4; ++i)
	{
		char* o = &out[i*i];
		for(S32 j = 0; j < 5; ++j)
		{
			o[4-j] = BASE85_TABLE[ me[i] % 85];
			word /= 85;
		}
	}
}

unsigned int decode( char const * fiveChars ) throw( bad_input_data )
{
	unsigned int ret = 0;
	for( S32 ix = 0; ix < 5; ++ix )
	{
		char * s = strchr( encodeTable, fiveChars[ ix ] );
		ret = ret * 85 + (s-encodeTable);
	}
	return ret;
} 
*/

// Common to all UUID implementations
void LLUUID::to_chars(char* out) const
{
#if defined(__SSE4_1__)
    alignas(16) char buffer[UUID_STR_SIZE-1]; // Temporary aligned output buffer for simd op
    
    __m128i lower = load_unaligned_si128(mData);
    __m128i upper = _mm_and_si128(_mm_set1_epi8(0xFF >> 4), _mm_srli_epi32(lower, 4));
    
    const __m128i a = _mm_set1_epi8(0x0F);
    lower = _mm_and_si128(lower, a);
    upper = _mm_and_si128(upper, a);
    
    const __m128i pastNine = _mm_set1_epi8(9 + 1);
    const __m128i lowerMask = _mm_cmplt_epi8(lower, pastNine);
    const __m128i upperMask = _mm_cmplt_epi8(upper, pastNine);
    
    __m128i letterMask1 = _mm_and_si128(lower, lowerMask);
    __m128i letterMask2 = _mm_and_si128(upper, upperMask);
    __m128i letterMask3 = _mm_or_si128(lower, lowerMask);
    __m128i letterMask4 = _mm_or_si128(upper, upperMask);
    
    const __m128i first = _mm_set1_epi8('0');
    const __m128i second = _mm_set1_epi8('a' - 10);
    
    letterMask1 = _mm_add_epi8(letterMask1, first);
    letterMask2 = _mm_add_epi8(letterMask2, first);
    letterMask3 = _mm_add_epi8(letterMask3, second);
    letterMask4 = _mm_add_epi8(letterMask4, second);
    
    lower = _mm_blendv_epi8(letterMask3, letterMask1, lowerMask);
    upper = _mm_blendv_epi8(letterMask4, letterMask2, upperMask);
    
    const __m128i mask1 = _mm_shuffle_epi8(lower, _mm_setr_epi8(-1, 0, -1, 1, -1, 2, -1, 3, -1, -1, 4, -1, 5, -1, -1, 6));
    const __m128i mask2 = _mm_shuffle_epi8(upper, _mm_setr_epi8(0, -1, 1, -1, 2, -1, 3, -1, -1, 4, -1, 5, -1, -1, 6, -1));
    const __m128i mask3 = _mm_shuffle_epi8(lower, _mm_setr_epi8(-1, 7, -1, -1, 8, -1, 9, -1, -1, 10, -1, 11, -1, 12, -1, 13));
    const __m128i mask4 = _mm_shuffle_epi8(upper, _mm_setr_epi8(7, -1, -1, 8, -1, 9, -1, -1, 10, -1, 11, -1, 12, -1, 13, -1));
    const __m128i hypens = _mm_set_epi8(0, 0, '-', 0, 0, 0, 0, '-', 0, 0, 0, 0, 0, 0, 0, 0);
    const __m128i hypens2 = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, '-', 0, 0, 0, 0, '-', 0, 0);
    const __m128i upperSorted = _mm_or_si128(_mm_or_si128(mask1, mask2), hypens);
    const __m128i lowerSorted = _mm_or_si128(_mm_or_si128(mask3, mask4), hypens2);
    
    _mm_store_si128(reinterpret_cast<__m128i *>(buffer), upperSorted);
    _mm_store_si128(reinterpret_cast<__m128i *>(buffer + UUID_BYTES), lowerSorted);
    
    // Did not fit the last four chars. Extract and append them.
    const int v1 = _mm_extract_epi16(upper, 7);
    const int v2 = _mm_extract_epi16(lower, 7);
    buffer[32] = (v1 & 0xff);
    buffer[33] = (v2 & 0xff);
    buffer[34] = ((v1 >> 8) & 0xff);
    buffer[35] = ((v2 >> 8) & 0xff);
    
    memcpy(out, buffer, UUID_STR_SIZE-1);
#else
    for (size_t i = 0; i < UUID_BYTES; ++i)
    {
        const auto uuid_byte = mData[i];
        const size_t hi = ((uuid_byte) >> 4) & 0x0F;
        *out++ = (i <= 9) ? static_cast<char>('0' + hi) : static_cast<char>('a' + (hi-10));;
        
        const size_t lo = (uuid_byte) & 0x0F;
        *out++ = (i <= 9) ? static_cast<char>('0' + lo) : static_cast<char>('a' + (lo-10));;
        
        if (i == 3 || i == 5 || i == 7 || i == 9)
        {
            *out++ = '-';
        }
    }
#endif
}

void LLUUID::toCompressedString(std::string& out) const
{
	char bytes[UUID_BYTES+1];
	memcpy(bytes, mData, UUID_BYTES);		/* Flawfinder: ignore */
	bytes[UUID_BYTES] = '\0';
	out.assign(bytes, UUID_BYTES);
}

// *TODO: deprecate
void LLUUID::toCompressedString(char *out) const
{
	memcpy(out, mData, UUID_BYTES);		/* Flawfinder: ignore */
	out[UUID_BYTES] = '\0';
}

BOOL LLUUID::set(const char* in_string, BOOL emit)
{
	return set(absl::NullSafeStringView(in_string),emit);
}

BOOL LLUUID::parseInternalScalar(const char* in_string, bool broken_format, bool emit)
{
    U8 cur_pos = 0;
    S32 i;
    for (i = 0; i < UUID_BYTES; i++)
    {
        if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
        {
            cur_pos++;
            if (broken_format && (i==10))
            {
                // Missing - in the broken format
                cur_pos--;
            }
        }
        
        mData[i] = 0;
        
        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
            mData[i] += (U8)(in_string[cur_pos] - '0');
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <='f'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'a');
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <='F'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'A');
        }
        else
        {
            if(emit)
            {
                LL_WARNS() << "Invalid UUID string character" << LL_ENDL;
            }
            setNull();
            return FALSE;
        }
        
        mData[i] = mData[i] << 4;
        cur_pos++;
        
        if ((in_string[cur_pos] >= '0') && (in_string[cur_pos] <= '9'))
        {
            mData[i] += (U8)(in_string[cur_pos] - '0');
        }
        else if ((in_string[cur_pos] >= 'a') && (in_string[cur_pos] <='f'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'a');
        }
        else if ((in_string[cur_pos] >= 'A') && (in_string[cur_pos] <='F'))
        {
            mData[i] += (U8)(10 + in_string[cur_pos] - 'A');
        }
        else
        {
            if(emit)
            {
                LL_WARNS() << "Invalid UUID string character" << LL_ENDL;
            }
            setNull();
            return FALSE;
        }
        cur_pos++;
    }
    return TRUE;
}

#if defined(__SSE4_2__)
BOOL LLUUID::parseInternalSIMD(const char* in_string, bool emit)
{
    __m128i mm_lower_mask_1, mm_lower_mask_2, mm_upper_mask_1, mm_upper_mask_2;
    const __m128i mm_lower = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in_string));
    const __m128i mm_upper = _mm_loadu_si128(reinterpret_cast<const __m128i *>(in_string + UUID_BYTES + 3));
    
    mm_lower_mask_1 = _mm_shuffle_epi8(mm_lower, _mm_setr_epi8(0, 2, 4, 6, 9, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1));
    mm_lower_mask_2 = _mm_shuffle_epi8(mm_lower, _mm_setr_epi8(1, 3, 5, 7, 10, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1));
    mm_upper_mask_1 = _mm_shuffle_epi8(mm_upper, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 0, 2, 5, 7, 9, 11, 13, -1));
    mm_upper_mask_2 = _mm_shuffle_epi8(mm_upper, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 1, 3, 6, 8, 10, 12, 14, -1));
    
    // Since we had hypens between the character we have 36 characters which does not fit in two 16 char loads
    // therefor we must manually insert them here
    mm_lower_mask_1 = _mm_insert_epi8(mm_lower_mask_1, in_string[16], 7);
    mm_lower_mask_2 = _mm_insert_epi8(mm_lower_mask_2, in_string[17], 7);
    mm_upper_mask_1 = _mm_insert_epi8(mm_upper_mask_1, in_string[34], 15);
    mm_upper_mask_2 = _mm_insert_epi8(mm_upper_mask_2, in_string[35], 15);
    
    // Merge [aaaaaaaa|aaaaaaaa|00000000|00000000] | [00000000|00000000|bbbbbbbb|bbbbbbbb] -> [aaaaaaaa|aaaaaaaa|bbbbbbbb|bbbbbbbb]
    __m128i mm_mask_merge_1 = _mm_or_si128(mm_lower_mask_1, mm_upper_mask_1);
    __m128i mm_mask_merge_2 = _mm_or_si128(mm_lower_mask_2, mm_upper_mask_2);
    
    // Check if all characters are between 0-9, A-Z or a-z
    const __m128i mm_allowed_char_range = _mm_setr_epi8('0', '9', 'A', 'Z', 'a', 'z', 0, -1, 0, -1, 0, -1, 0, -1, 0, -1);
    const int cmp_lower = _mm_cmpistri(mm_allowed_char_range, mm_mask_merge_1, _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES | _SIDD_NEGATIVE_POLARITY);
    const int cmp_upper = _mm_cmpistri(mm_allowed_char_range, mm_mask_merge_2, _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES | _SIDD_NEGATIVE_POLARITY);
    if (cmp_lower != UUID_BYTES || cmp_upper != UUID_BYTES)
    {
        if(emit)
        {
            LL_WARNS() << "Invalid UUID string: " << in_string << LL_ENDL;
        }
        setNull();
        return FALSE;
    }
    
    const __m128i nine = _mm_set1_epi8('9');
    const __m128i mm_above_nine_mask_1 = _mm_cmpgt_epi8(mm_mask_merge_1, nine);
    const __m128i mm_above_nine_mask_2 = _mm_cmpgt_epi8(mm_mask_merge_2, nine);
    
    __m128i mm_letter_mask_1 = _mm_and_si128(mm_mask_merge_1, mm_above_nine_mask_1);
    __m128i mm_letter_mask_2 = _mm_and_si128(mm_mask_merge_2, mm_above_nine_mask_2);
    
    // Convert all letters to to lower case first
    const __m128i toLowerCase = _mm_set1_epi8(0x20);
    mm_letter_mask_1 = _mm_or_si128(mm_letter_mask_1, toLowerCase);
    mm_letter_mask_2 = _mm_or_si128(mm_letter_mask_2, toLowerCase);
    
    // now convert to hex
    const __m128i toHex = _mm_set1_epi8('a' - 10 - '0');
    const __m128i fixedUppercase1 = _mm_sub_epi8(mm_letter_mask_1, toHex);
    const __m128i fixedUppercase2 = _mm_sub_epi8(mm_letter_mask_2, toHex);
    
    const __m128i mm_blended_high = _mm_blendv_epi8(mm_mask_merge_1, fixedUppercase1, mm_above_nine_mask_1);
    const __m128i mm_blended_low = _mm_blendv_epi8(mm_mask_merge_2, fixedUppercase2, mm_above_nine_mask_2);
    const __m128i zero = _mm_set1_epi8('0');
    __m128i lo = _mm_sub_epi8(mm_blended_low, zero);
    __m128i hi = _mm_sub_epi8(mm_blended_high, zero);
    hi = _mm_slli_epi16(hi, 4);
    
    _mm_storeu_si128(reinterpret_cast<__m128i *>(mData), _mm_xor_si128(hi, lo));
    return TRUE;
}
#endif

BOOL LLUUID::set(const std::string_view in_string, BOOL emit)
{
	// empty strings should make NULL uuid
	if (in_string.empty())
	{
		setNull();
		return TRUE;
	}

    BOOL broken_format = FALSE;
    
	if (in_string.length() != (UUID_STR_LENGTH - 1))		/* Flawfinder: ignore */
	{
		// I'm a moron.  First implementation didn't have the right UUID format.
		// Shouldn't see any of these any more
		if (in_string.length() == (UUID_STR_LENGTH - 2))	/* Flawfinder: ignore */
		{
			if(emit)
			{
				LL_WARNS() << "Warning! Using broken UUID string format" << LL_ENDL;
			}
			broken_format = TRUE;
		}
		else
		{
			// Bad UUID string.  Spam as INFO, as most cases we don't care.
			if(emit)
			{
				//don't spam the logs because a resident can't spell.
				LL_WARNS() << "Bad UUID string: " << in_string << LL_ENDL;
			}
			setNull();
			return FALSE;
		}
	}

#if defined(__SSE4_2__)
    if(broken_format)
    {
        return parseInternalScalar(in_string.data(), broken_format, emit);
    }
    else
    {
        return parseInternalSIMD(in_string.data(), emit);
    }
#else
    return parseInternalScalar(in_string.data(), broken_format, emit);
#endif

	return TRUE;
}

BOOL validate_internal_scalar(const char* str_ptr, bool broken_format)
{
    U8 cur_pos = 0;
    for (U32 i = 0; i < 16; i++)
    {
        if ((i == 4) || (i == 6) || (i == 8) || (i == 10))
        {
            cur_pos++;
            if (broken_format && (i==10))
            {
                // Missing - in the broken format
                cur_pos--;
            }
        }
        
        if (((str_ptr[cur_pos] >= '0') && (str_ptr[cur_pos] <= '9'))
            || ((str_ptr[cur_pos] >= 'a') && (str_ptr[cur_pos] <='f'))
            || ((str_ptr[cur_pos] >= 'A') && (str_ptr[cur_pos] <='F')))
        {
        }
        else
        {
            return FALSE;
        }
        
        cur_pos++;
        
        if (((str_ptr[cur_pos] >= '0') && (str_ptr[cur_pos] <= '9'))
            || ((str_ptr[cur_pos] >= 'a') && (str_ptr[cur_pos] <='f'))
            || ((str_ptr[cur_pos] >= 'A') && (str_ptr[cur_pos] <='F')))
        {
        }
        else
        {
            return FALSE;
        }
        cur_pos++;
    }
    return TRUE;
}

#if defined(__SSE4_2__)
BOOL validate_internal_simd(const char* str_ptr)
{
    __m128i mm_lower_mask_1, mm_lower_mask_2, mm_upper_mask_1, mm_upper_mask_2;
    const __m128i mm_lower = _mm_loadu_si128(reinterpret_cast<const __m128i *>(str_ptr));
    const __m128i mm_upper = _mm_loadu_si128(reinterpret_cast<const __m128i *>(str_ptr + UUID_BYTES + 3));
    
    mm_lower_mask_1 = _mm_shuffle_epi8(mm_lower, _mm_setr_epi8(0, 2, 4, 6, 9, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1));
    mm_lower_mask_2 = _mm_shuffle_epi8(mm_lower, _mm_setr_epi8(1, 3, 5, 7, 10, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1));
    mm_upper_mask_1 = _mm_shuffle_epi8(mm_upper, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 0, 2, 5, 7, 9, 11, 13, -1));
    mm_upper_mask_2 = _mm_shuffle_epi8(mm_upper, _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 1, 3, 6, 8, 10, 12, 14, -1));
    
    // Since we had hypens between the character we have 36 characters which does not fit in two 16 char loads
    // therefor we must manually insert them here
    mm_lower_mask_1 = _mm_insert_epi8(mm_lower_mask_1, str_ptr[16], 7);
    mm_lower_mask_2 = _mm_insert_epi8(mm_lower_mask_2, str_ptr[17], 7);
    mm_upper_mask_1 = _mm_insert_epi8(mm_upper_mask_1, str_ptr[34], 15);
    mm_upper_mask_2 = _mm_insert_epi8(mm_upper_mask_2, str_ptr[35], 15);
    
    // Merge [aaaaaaaa|aaaaaaaa|00000000|00000000] | [00000000|00000000|bbbbbbbb|bbbbbbbb] -> [aaaaaaaa|aaaaaaaa|bbbbbbbb|bbbbbbbb]
    __m128i mm_mask_merge_1 = _mm_or_si128(mm_lower_mask_1, mm_upper_mask_1);
    __m128i mm_mask_merge_2 = _mm_or_si128(mm_lower_mask_2, mm_upper_mask_2);
    
    // Check if all characters are between 0-9, A-Z or a-z
    const __m128i mm_allowed_char_range = _mm_setr_epi8('0', '9', 'A', 'Z', 'a', 'z', 0, -1, 0, -1, 0, -1, 0, -1, 0, -1);
    const int cmp_lower = _mm_cmpistri(mm_allowed_char_range, mm_mask_merge_1, _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES | _SIDD_NEGATIVE_POLARITY);
    const int cmp_upper = _mm_cmpistri(mm_allowed_char_range, mm_mask_merge_2, _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES | _SIDD_NEGATIVE_POLARITY);
    if (cmp_lower != UUID_BYTES || cmp_upper != UUID_BYTES)
    {
        return FALSE;
    }

    return TRUE;
}
#endif

BOOL LLUUID::validate(std::string_view in_string)
{
    if (in_string.empty())
    {
        return FALSE;
    }
    
    static constexpr auto HYPEN_UUID = 36;
    static constexpr auto BROKEN_UUID = 35;
    
    size_t in_str_size = in_string.size();
    if(in_str_size == HYPEN_UUID)
    {
#if defined(__SSE4_2__)
        return validate_internal_simd(in_string.data());
#else
        return validate_internal_scalar(in_string.data(), false);
#endif
    }
    else if (in_str_size == BROKEN_UUID)
    {
        return validate_internal_scalar(in_string.data(), true);
    }
    return FALSE;
}

const LLUUID& LLUUID::operator^=(const LLUUID& rhs)
{
	U32* me = (U32*)&(mData[0]);
	const U32* other = (U32*)&(rhs.mData[0]);
	for(S32 i = 0; i < 4; ++i)
	{
		me[i] = me[i] ^ other[i];
	}
	return *this;
}

LLUUID LLUUID::operator^(const LLUUID& rhs) const
{
	LLUUID id(*this);
	id ^= rhs;
	return id;
}

void LLUUID::combine(const LLUUID& other, LLUUID& result) const
{
	LLMD5 md5_uuid;
	md5_uuid.update((unsigned char*)mData, 16);
	md5_uuid.update((unsigned char*)other.mData, 16);
	md5_uuid.finalize();
	md5_uuid.raw_digest(result.mData);
}

LLUUID LLUUID::combine(const LLUUID &other) const
{
	LLUUID combination;
	combine(other, combination);
	return combination;
}

std::ostream& operator<<(std::ostream& s, const LLUUID &uuid)
{
    char uuid_str[37] = {}; // will be null-terminated
    uuid.to_chars(uuid_str);
	s << uuid_str;
	return s;
}

std::istream& operator>>(std::istream &s, LLUUID &uuid)
{
	U32 i;
	char uuid_str[UUID_STR_LENGTH];		/* Flawfinder: ignore */
	for (i = 0; i < UUID_STR_LENGTH-1; i++)
	{
		s >> uuid_str[i];
	}
	uuid_str[i] = '\0';
	uuid.set(std::string(uuid_str));
	return s;
}

static void get_random_bytes(void *buf, int nbytes)
{
	int i;
	char *cp = (char *) buf;

	// *NOTE: If we are not using the janky generator ll_rand()
	// generates at least 3 good bytes of data since it is 0 to
	// RAND_MAX. This could be made more efficient by copying all the
	// bytes.
	for (i=0; i < nbytes; i++)
		*cp++ = ll_rand() & 0xFF;

	return;	
}

#if	LL_WINDOWS

// static
S32	LLUUID::getNodeID(unsigned char	*node_id)
{
	static bool got_node_id = false;
	static unsigned char local_node_id[6];
	if (got_node_id)
	{
		memcpy(node_id, local_node_id, sizeof(local_node_id));
		return 1;
	}

	S32 retval = 0;
	PIP_ADAPTER_ADDRESSES pAddresses = nullptr;
	ULONG outBufLen = 0U;
	DWORD dwRetVal = 0U;

	ULONG family = AF_INET;
	ULONG flags = GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_INCLUDE_GATEWAYS;

	GetAdaptersAddresses(
		AF_INET,
		flags,
		nullptr,
		nullptr,
		&outBufLen);

	constexpr U32 MAX_TRIES = 3U;
	U32 iteration = 0U;
	do {

		pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(malloc(outBufLen));
		if (pAddresses == nullptr) {
			return 0;
		}

		dwRetVal =
			GetAdaptersAddresses(family, flags, nullptr, pAddresses, &outBufLen);

		if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
			free(pAddresses);
			pAddresses = nullptr;
		}
		else {
			break;
		}

		++iteration;

	} while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (iteration < MAX_TRIES));

	if (dwRetVal == NO_ERROR)
	{
		PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
		PIP_ADAPTER_GATEWAY_ADDRESS pFirstGateway = nullptr;
		do {
			pFirstGateway = pCurrAddresses->FirstGatewayAddress;
			if (pFirstGateway)
			{
				if ((pCurrAddresses->IfType == IF_TYPE_ETHERNET_CSMACD || pCurrAddresses->IfType == IF_TYPE_IEEE80211) && pCurrAddresses->ConnectionType == NET_IF_CONNECTION_DEDICATED
					&& pCurrAddresses->OperStatus == IfOperStatusUp)
				{
					if (pCurrAddresses->PhysicalAddressLength == 6) 
					{
						for (size_t i = 0; i < 5; ++i)
						{
							node_id[i] = pCurrAddresses->PhysicalAddress[i];
							local_node_id[i] = pCurrAddresses->PhysicalAddress[i];
						}
						retval = 1;
						got_node_id = true;
						break;
					}
				}
			}
			pCurrAddresses = pCurrAddresses->Next;
		} while (pCurrAddresses);                    // Terminate if last adapter
	}
	
	if(pAddresses)
		free(pAddresses);
	pAddresses = nullptr;

	return retval;
}

#elif LL_DARWIN
// Mac OS X version of the UUID generation code...
/*
 * Get an ethernet hardware address, if we can find it...
 */
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <ifaddrs.h>

// static
S32 LLUUID::getNodeID(unsigned char *node_id)
{
	int i;
	unsigned char 	*a = NULL;
	struct ifaddrs *ifap, *ifa;
	int rv;
	S32 result = 0;

	if ((rv=getifaddrs(&ifap))==-1)
	{       
		return -1;
	}
	if (ifap == NULL)
	{
		return -1;
	}

	for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next)
	{       
//		printf("Interface %s, address family %d, ", ifa->ifa_name, ifa->ifa_addr->sa_family);
		for(i=0; i< ifa->ifa_addr->sa_len; i++)
		{
//			printf("%02X ", (unsigned char)ifa->ifa_addr->sa_data[i]);
		}
//		printf("\n");
		
		if(ifa->ifa_addr->sa_family == AF_LINK)
		{
			// This is a link-level address
			struct sockaddr_dl *lla = (struct sockaddr_dl *)ifa->ifa_addr;
			
//			printf("\tLink level address, type %02X\n", lla->sdl_type);

			if(lla->sdl_type == IFT_ETHER)
			{
				// Use the first ethernet MAC in the list.
				// For some reason, the macro LLADDR() defined in net/if_dl.h doesn't expand correctly.  This is what it would do.
				a = (unsigned char *)&((lla)->sdl_data);
				a += (lla)->sdl_nlen;
				
				if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
				{
					continue;
				}

				if (node_id) 
				{
					memcpy(node_id, a, 6);
					result = 1;
				}
				
				// We found one.
				break;
			}
		}
	}
	freeifaddrs(ifap);

	return result;
}

#else

// Linux version of the UUID generation code...
/*
 * Get the ethernet hardware address, if we can find it...
 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#define HAVE_NETINET_IN_H
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#if !LL_DARWIN
#include <linux/sockios.h>
#endif
#endif

// static
S32 LLUUID::getNodeID(unsigned char *node_id)
{
	int 		sd;
	struct ifreq 	ifr, *ifrp;
	struct ifconf 	ifc;
	char buf[1024];
	int		n, i;
	unsigned char 	*a;
	
/*
 * BSD 4.4 defines the size of an ifreq to be
 * max(sizeof(ifreq), sizeof(ifreq.ifr_name)+ifreq.ifr_addr.sa_len
 * However, under earlier systems, sa_len isn't present, so the size is 
 * just sizeof(struct ifreq)
 */
#ifdef HAVE_SA_LEN
#ifndef max
#define max(a,b) ((a) > (b) ? (a) : (b))
#endif
#define ifreq_size(i) max(sizeof(struct ifreq),\
     sizeof((i).ifr_name)+(i).ifr_addr.sa_len)
#else
#define ifreq_size(i) sizeof(struct ifreq)
#endif /* HAVE_SA_LEN*/

	sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sd < 0) {
		return -1;
	}
	memset(buf, 0, sizeof(buf));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl (sd, SIOCGIFCONF, (char *)&ifc) < 0) {
		close(sd);
		return -1;
	}
	n = ifc.ifc_len;
	for (i = 0; i < n; i+= ifreq_size(*ifr) ) {
		ifrp = (struct ifreq *)((char *) ifc.ifc_buf+i);
		strncpy(ifr.ifr_name, ifrp->ifr_name, IFNAMSIZ);		/* Flawfinder: ignore */
#ifdef SIOCGIFHWADDR
		if (ioctl(sd, SIOCGIFHWADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) &ifr.ifr_hwaddr.sa_data;
#else
#ifdef SIOCGENADDR
		if (ioctl(sd, SIOCGENADDR, &ifr) < 0)
			continue;
		a = (unsigned char *) ifr.ifr_enaddr;
#else
		/*
		 * XXX we don't have a way of getting the hardware
		 * address
		 */
		close(sd);
		return 0;
#endif /* SIOCGENADDR */
#endif /* SIOCGIFHWADDR */
		if (!a[0] && !a[1] && !a[2] && !a[3] && !a[4] && !a[5])
			continue;
		if (node_id) {
			memcpy(node_id, a, 6);		/* Flawfinder: ignore */
			close(sd);
			return 1;
		}
	}
	close(sd);
	return 0;
}

#endif

S32 LLUUID::cmpTime(uuid_time_t *t1, uuid_time_t *t2)
{
   // Compare two time values.

   if (t1->high < t2->high) return -1;
   if (t1->high > t2->high) return 1;
   if (t1->low  < t2->low)  return -1;
   if (t1->low  > t2->low)  return 1;
   return 0;
}

void LLUUID::getSystemTime(uuid_time_t *timestamp)
{
   // Get system time with 100ns precision. Time is since Oct 15, 1582.
#if LL_WINDOWS
   ULARGE_INTEGER time;
   GetSystemTimeAsFileTime((FILETIME *)&time);
   // NT keeps time in FILETIME format which is 100ns ticks since
   // Jan 1, 1601. UUIDs use time in 100ns ticks since Oct 15, 1582.
   // The difference is 17 Days in Oct + 30 (Nov) + 31 (Dec)
   // + 18 years and 5 leap days.
   time.QuadPart +=
            (unsigned __int64) (1000*1000*10)       // seconds
          * (unsigned __int64) (60 * 60 * 24)       // days
          * (unsigned __int64) (17+30+31+365*18+5); // # of days

   timestamp->high = time.HighPart;
   timestamp->low  = time.LowPart;
#else
   struct timeval tp;
   gettimeofday(&tp, 0);

   // Offset between UUID formatted times and Unix formatted times.
   // UUID UTC base time is October 15, 1582.
   // Unix base time is January 1, 1970.
   U64 uuid_time = ((U64)tp.tv_sec * 10000000) + (tp.tv_usec * 10) +
                           U64L(0x01B21DD213814000);
   timestamp->high = (U32) (uuid_time >> 32);
   timestamp->low  = (U32) (uuid_time & 0xFFFFFFFF);
#endif
}

void LLUUID::getCurrentTime(uuid_time_t *timestamp)
{
   // Get current time as 60 bit 100ns ticks since whenever.
   // Compensate for the fact that real clock resolution is less
   // than 100ns.

   const U32 uuids_per_tick = 1024;

   static uuid_time_t time_last;
   static U32    uuids_this_tick;
   static BOOL     init = FALSE;

   if (!init) {
      getSystemTime(&time_last);
      uuids_this_tick = uuids_per_tick;
      init = TRUE;
      mMutex = new LLMutex();
   }

   uuid_time_t time_now = {0,0};

   while (1) {
      getSystemTime(&time_now);

      // if clock reading changed since last UUID generated
      if (cmpTime(&time_last, &time_now))  {
         // reset count of uuid's generated with this clock reading
         uuids_this_tick = 0;
         break;
      }
      if (uuids_this_tick < uuids_per_tick) {
         uuids_this_tick++;
         break;
      }
      // going too fast for our clock; spin
   }

   time_last = time_now;

   if (uuids_this_tick != 0) {
      if (time_now.low & 0x80000000) {
         time_now.low += uuids_this_tick;
         if (!(time_now.low & 0x80000000))
            time_now.high++;
      } else
         time_now.low += uuids_this_tick;
   }

   timestamp->high = time_now.high;
   timestamp->low  = time_now.low;
}

void LLUUID::generate()
{
	// Create a UUID.
	uuid_time_t timestamp;

	static unsigned char node_id[6];	/* Flawfinder: ignore */
	static int has_init = 0;
   
	// Create a UUID.
	static uuid_time_t time_last = {0,0};
	static U16 clock_seq = 0;

	if (!has_init) 
	{
		has_init = 1;
		if (getNodeID(node_id) <= 0) 
		{
			get_random_bytes(node_id, 6);
			/*
			 * Set multicast bit, to prevent conflicts
			 * with IEEE 802 addresses obtained from
			 * network cards
			 */
			node_id[0] |= 0x80;
		}

		getCurrentTime(&time_last);

		clock_seq = (U16)ll_rand(65536);
	}

	// get current time
	getCurrentTime(&timestamp);
	U16 our_clock_seq = clock_seq;

	// if clock hasn't changed or went backward, change clockseq
	if (cmpTime(&timestamp, &time_last) != 1) 
	{
		LLMutexLock	lock(mMutex);
		clock_seq = (clock_seq + 1) & 0x3FFF;
		if (clock_seq == 0) 
			clock_seq++;
		our_clock_seq = clock_seq;	// Ensure we're using a different clock_seq value from previous time
	}

    time_last = timestamp;

	memcpy(mData+10, node_id, 6);		/* Flawfinder: ignore */
	U32 tmp;
	tmp = timestamp.low;
	mData[3] = (unsigned char) tmp;
	tmp >>= 8;
	mData[2] = (unsigned char) tmp;
	tmp >>= 8;
	mData[1] = (unsigned char) tmp;
	tmp >>= 8;
	mData[0] = (unsigned char) tmp;
	
	tmp = (U16) timestamp.high;
	mData[5] = (unsigned char) tmp;
	tmp >>= 8;
	mData[4] = (unsigned char) tmp;

	tmp = (timestamp.high >> 16) | 0x1000;
	mData[7] = (unsigned char) tmp;
	tmp >>= 8;
	mData[6] = (unsigned char) tmp;

	tmp = our_clock_seq;

	mData[9] = (unsigned char) tmp;
	tmp >>= 8;
	mData[8] = (unsigned char) tmp;

	LLMD5 md5_uuid;
	
	md5_uuid.update(mData,16);
	md5_uuid.finalize();
	md5_uuid.raw_digest(mData);
}

void LLUUID::generate(const std::string& hash_string)
{
	LLMD5 md5_uuid((U8*)hash_string.c_str());
	md5_uuid.raw_digest(mData);
}

U32 LLUUID::getRandomSeed()
{
   static unsigned char seed[16];		/* Flawfinder: ignore */
   
   getNodeID(&seed[0]);

   // Incorporate the pid into the seed to prevent
   // processes that start on the same host at the same
   // time from generating the same seed.
   pid_t pid = LLApp::getPid();

   seed[6]=(unsigned char)(pid >> 8);
   seed[7]=(unsigned char)(pid);
   getSystemTime((uuid_time_t *)(&seed[8]));

   LLMD5 md5_seed;
	
   md5_seed.update(seed,16);
   md5_seed.finalize();
   md5_seed.raw_digest(seed);
   
   U32 out;
   memcpy(&out, seed, sizeof(out));
   return out;
}

BOOL LLUUID::parseUUID(const std::string& buf, LLUUID* value)
{
	if( buf.empty() || value == NULL)
	{
		return FALSE;
	}

	std::string temp( buf );
	LLStringUtil::trim(temp);
	if( LLUUID::validate( temp ) )
	{
		value->set( temp );
		return TRUE;
	}
	return FALSE;
}

//static
LLUUID LLUUID::generateNewID()
{
	LLUUID new_id;
	new_id.generate();
	return new_id;
}

//static
LLUUID LLUUID::generateNewID(const std::string& hash_string)
{
	LLUUID new_id;
	if (hash_string.empty())
	{
		new_id.generate();
	}
	else
	{
		new_id.generate(hash_string);
	}
	return new_id;
}

LLAssetID LLTransactionID::makeAssetID(const LLUUID& session) const
{
	LLAssetID result;
	if (isNull())
	{
		result.setNull();
	}
	else
	{
		combine(session, result);
	}
	return result;
}

// Faster than copying from memory
 void LLUUID::setNull()
{
	 memset(mData, 0, sizeof(mData));
}

/*
// JC: This is dangerous.  It allows UUIDs to be cast automatically
// to integers, among other things.  Use isNull() or notNull().
 LLUUID::operator bool() const
{
	U32 *word = (U32 *)mData;
	return (word[0] | word[1] | word[2] | word[3]) > 0;
}
*/

 LLUUID::LLUUID(const char *in_string)
{
	set(in_string);
}

 LLUUID::LLUUID(const std::string_view in_string)
{
	set(in_string);
}

 U16 LLUUID::getCRC16() const
{
	// A UUID is 16 bytes, or 8 shorts.
	U16 *short_data = (U16*)mData;
	U16 out = 0;
	out += short_data[0];
	out += short_data[1];
	out += short_data[2];
	out += short_data[3];
	out += short_data[4];
	out += short_data[5];
	out += short_data[6];
	out += short_data[7];
	return out;
}

 U32 LLUUID::getCRC32() const
{
	U32 ret = 0;
	for(U32 i = 0;i < 4;++i)
	{
		ret += (mData[i*4]) | (mData[i*4+1]) << 8 | (mData[i*4+2]) << 16 | (mData[i*4+3]) << 24;
	}
	return ret;
}
