/**
 * @file llstring.cpp
 * @brief String utility functions and the std::string class.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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

#include "llstring.h"
#include "llerror.h"
#include "llfasttimer.h"
#include "llsd.h"
#include <vector>

#include <simdutf.h>

#if LL_WINDOWS
#include "llwin32headers.h"
#endif

namespace
{

// Tolerantly convert src (SrcCh* of length len) using simdutf's fast path on
// valid runs, substituting `replacement` for each rejected code unit. Mirrors
// the "best-effort with LL_UNKNOWN_CHAR" semantics of the previous hand-rolled
// decoders while letting valid segments ride the SIMD path.
template<typename SrcCh, typename DstCh>
std::basic_string<DstCh> utf_convert_with_replacement(
    const SrcCh* src, size_t len,
    simdutf::result (*validate)(const SrcCh*, size_t),
    size_t (*out_len_from)(const SrcCh*, size_t),
    size_t (*convert_valid)(const SrcCh*, size_t, DstCh*),
    DstCh replacement)
{
    std::basic_string<DstCh> out;
    if (len == 0 || !src) return out;

    while (len > 0)
    {
        const auto r = validate(src, len);
        const size_t valid = (r.error == simdutf::error_code::SUCCESS) ? len : r.count;
        if (valid > 0)
        {
            const size_t pos = out.size();
            out.resize(pos + out_len_from(src, valid));
            convert_valid(src, valid, out.data() + pos);
            src += valid;
            len -= valid;
        }
        if (r.error != simdutf::error_code::SUCCESS)
        {
            out.push_back(replacement);
            ++src;
            --len;
        }
    }
    return out;
}

} // anonymous namespace

std::string ll_safe_string(const char* in)
{
    if(in) return std::string(in);
    return std::string();
}

std::string ll_safe_string(const char* in, S32 maxlen)
{
    if(in && maxlen > 0 ) return std::string(in, maxlen);

    return std::string();
}

bool is_char_hex(char hex)
{
    if((hex >= '0') && (hex <= '9'))
    {
        return true;
    }
    else if((hex >= 'a') && (hex <='f'))
    {
        return true;
    }
    else if((hex >= 'A') && (hex <='F'))
    {
        return true;
    }
    return false; // uh - oh, not hex any more...
}

U8 hex_as_nybble(char hex)
{
    if((hex >= '0') && (hex <= '9'))
    {
        return (U8)(hex - '0');
    }
    else if((hex >= 'a') && (hex <='f'))
    {
        return (U8)(10 + hex - 'a');
    }
    else if((hex >= 'A') && (hex <='F'))
    {
        return (U8)(10 + hex - 'A');
    }
    return 0; // uh - oh, not hex any more...
}

bool iswindividual(llwchar elem)
{
    U32 cur_char = (U32)elem;
    bool result = false;
    if (0x2E80<= cur_char && cur_char <= 0x9FFF)
    {
        result = true;
    }
    else if (0xAC00<= cur_char && cur_char <= 0xD7A0 )
    {
        result = true;
    }
    else if (0xF900<= cur_char && cur_char <= 0xFA60 )
    {
        result = true;
    }
    return result;
}

bool _read_file_into_string(std::string& str, const std::string& filename)
{
    llifstream ifs(filename.c_str(), llifstream::binary);
    if (!ifs.is_open())
    {
        LL_INFOS() << "Unable to open file " << filename << LL_ENDL;
        return false;
    }

    std::ostringstream oss;

    oss << ifs.rdbuf();
    str = oss.str();
    ifs.close();
    return true;
}




// See http://www.unicode.org/Public/BETA/CVTUTF-1-2/ConvertUTF.c
// for the Unicode implementation - this doesn't match because it was written before finding
// it.


std::ostream& operator<<(std::ostream &s, const LLWString &wstr)
{
    std::string utf8_str = wstring_to_utf8str(wstr);
    s << utf8_str;
    return s;
}

std::string rawstr_to_utf8(const std::string& raw)
{
    LLWString wstr(utf8str_to_wstring(raw));
    return wstring_to_utf8str(wstr);
}

std::ptrdiff_t wchar_to_utf8chars(llwchar in_char, char* outchars)
{
    U32 cur_char = (U32)in_char;
    char* base = outchars;
    if (cur_char < 0x80)
    {
        *outchars++ = (U8)cur_char;
    }
    else if (cur_char < 0x800)
    {
        *outchars++ = 0xC0 | (cur_char >> 6);
        *outchars++ = 0x80 | (cur_char & 0x3F);
    }
    else if (cur_char < 0x10000)
    {
        *outchars++ = 0xE0 | (cur_char >> 12);
        *outchars++ = 0x80 | ((cur_char >> 6) & 0x3F);
        *outchars++ = 0x80 | (cur_char & 0x3F);
    }
    else if (cur_char < 0x200000)
    {
        *outchars++ = 0xF0 | (cur_char >> 18);
        *outchars++ = 0x80 | ((cur_char >> 12) & 0x3F);
        *outchars++ = 0x80 | ((cur_char >> 6) & 0x3F);
        *outchars++ = 0x80 | (cur_char & 0x3F);
    }
    else if (cur_char < 0x4000000)
    {
        *outchars++ = 0xF8 | (cur_char >> 24);
        *outchars++ = 0x80 | ((cur_char >> 18) & 0x3F);
        *outchars++ = 0x80 | ((cur_char >> 12) & 0x3F);
        *outchars++ = 0x80 | ((cur_char >> 6) & 0x3F);
        *outchars++ = 0x80 | (cur_char & 0x3F);
    }
    else if (cur_char < 0x80000000)
    {
        *outchars++ = 0xFC | (cur_char >> 30);
        *outchars++ = 0x80 | ((cur_char >> 24) & 0x3F);
        *outchars++ = 0x80 | ((cur_char >> 18) & 0x3F);
        *outchars++ = 0x80 | ((cur_char >> 12) & 0x3F);
        *outchars++ = 0x80 | ((cur_char >> 6) & 0x3F);
        *outchars++ = 0x80 | (cur_char & 0x3F);
    }
    else
    {
        LL_WARNS() << "Invalid Unicode character " << cur_char << "!" << LL_ENDL;
        *outchars++ = LL_UNKNOWN_CHAR;
    }
    return outchars - base;
}

llutf16string wstring_to_utf16str(const llwchar* utf32str, size_t len)
{
    return utf_convert_with_replacement<char32_t, char16_t>(
        utf32str, len,
        &simdutf::validate_utf32_with_errors,
        &simdutf::utf16_length_from_utf32,
        &simdutf::convert_valid_utf32_to_utf16le,
        static_cast<char16_t>(LL_UNKNOWN_CHAR));
}

LLWString utf16str_to_wstring(const char16_t* utf16str, size_t len)
{
    return utf_convert_with_replacement<char16_t, char32_t>(
        utf16str, len,
        &simdutf::validate_utf16le_with_errors,
        &simdutf::utf32_length_from_utf16le,
        &simdutf::convert_valid_utf16le_to_utf32,
        static_cast<char32_t>(LL_UNKNOWN_CHAR));
}

// Length in utf16string (UTF-16) of wlen wchars beginning at woffset.
S32 wstring_utf16_length(const LLWString &wstr, const S32 woffset, const S32 wlen)
{
    const S32 end = llmin((S32)wstr.length(), woffset + wlen);
    if (end <= woffset) return 0;
    return (S32)simdutf::utf16_length_from_utf32(wstr.data() + woffset, end - woffset);
}

// Given a wstring and an offset in it, returns the length as wstring (i.e.,
// number of llwchars) of the longest substring that starts at the offset
// and whose equivalent utf-16 string does not exceeds the given utf16_length.
S32 wstring_wstring_length_from_utf16_length(const LLWString & wstr, const S32 woffset, const S32 utf16_length, bool *unaligned)
{
    const auto end = wstr.length();
    bool u{ false };
    S32 n = woffset + utf16_length;
    S32 i = woffset;
    while (i < end)
    {
        if (wstr[i] >= 0x10000)
        {
            --n;
        }
        if (i >= n)
        {
            u = (i > n);
            break;
        }
        i++;
    }
    if (unaligned)
    {
        *unaligned = u;
    }
    return i - woffset;
}

// Given a wstring and an offset in it, returns the length as wstring (i.e.,
// number of llwchars) of the longest substring that starts at the offset
// and whose equivalent utf-8 string does not exceed the given utf8_length.
S32 wstring_wstring_length_from_utf8_length(LLWStringView wstr, const S32 woffset, const S32 utf8_length, bool *unaligned)
{
    const S32 end = (S32)wstr.length();
    const S32 start = llclamp(woffset, 0, end);

    S32 i = start;
    S32 bytes = 0;
    while (i < end && bytes < utf8_length)
    {
        const S32 n = wchar_utf8_length(wstr[i]);
        if (bytes + n > utf8_length)
        {
            // utf8_length falls inside this codepoint's encoding; stop before it.
            break;
        }
        bytes += n;
        i++;
    }

    if (unaligned)
    {
        *unaligned = (i < end) && (bytes < utf8_length);
    }
    return i - start;
}

S32 wchar_utf8_length(const llwchar wc)
{
    if (wc < 0x80)
    {
        return 1;
    }
    else if (wc < 0x800)
    {
        return 2;
    }
    else if (wc < 0x10000)
    {
        return 3;
    }
    else if (wc < 0x200000)
    {
        return 4;
    }
    else if (wc < 0x4000000)
    {
        return 5;
    }
    else
    {
        return 6;
    }
}

std::string wchar_utf8_preview(const llwchar wc)
{
    std::ostringstream oss;
    oss << std::hex << std::uppercase << (U32)wc;

    U8 out_bytes[8];
    U32 size = (U32)wchar_to_utf8chars(wc, (char*)out_bytes);

    if (size > 1)
    {
        oss << " [";
        for (U32 i = 0; i < size; ++i)
        {
            if (i)
            {
                oss << ", ";
            }
            oss << (int)out_bytes[i];
        }
        oss << "]";
    }

    return oss.str();
}

S32 wstring_utf8_length(const LLWString& wstr)
{
    return (S32)simdutf::utf8_length_from_utf32(wstr.data(), wstr.size());
}

LLWString utf8str_to_wstring(const char* utf8str, size_t len)
{
    return utf_convert_with_replacement<char, char32_t>(
        utf8str, len,
        &simdutf::validate_utf8_with_errors,
        &simdutf::utf32_length_from_utf8,
        &simdutf::convert_valid_utf8_to_utf32,
        static_cast<char32_t>(LL_UNKNOWN_CHAR));
}

std::string wstring_to_utf8str(const llwchar* utf32str, size_t len)
{
    return utf_convert_with_replacement<char32_t, char>(
        utf32str, len,
        &simdutf::validate_utf32_with_errors,
        &simdutf::utf8_length_from_utf32,
        &simdutf::convert_valid_utf32_to_utf8,
        static_cast<char>(LL_UNKNOWN_CHAR));
}

std::string utf16str_to_utf8str(const char16_t* utf16str, size_t len)
{
    return utf_convert_with_replacement<char16_t, char>(
        utf16str, len,
        &simdutf::validate_utf16le_with_errors,
        &simdutf::utf8_length_from_utf16le,
        &simdutf::convert_valid_utf16le_to_utf8,
        static_cast<char>(LL_UNKNOWN_CHAR));
}

std::u8string str_to_u8str(const char* str, size_t len)
{
    if (!str || len == 0) return {};

    // We treat std::string as utf8 in this codebase so pass through
    std::string_view str_view(str, len);
    return std::u8string(str_view.begin(), str_view.end());
}

std::string u8str_to_str(const char8_t* u8str, size_t len)
{
    if (!u8str || len == 0) return {};

    // We treat std::string as utf8 in this codebase so pass through
    std::u8string_view u8str_view(u8str, len);
    return std::string(u8str_view.begin(), u8str_view.end());
}

std::string utf8str_trim(const std::string& utf8str)
{
    LLWString wstr = utf8str_to_wstring(utf8str);
    LLWStringUtil::trim(wstr);
    return wstring_to_utf8str(wstr);
}


std::string utf8str_tolower(const std::string& utf8str)
{
    LLWString out_str = utf8str_to_wstring(utf8str);
    LLWStringUtil::toLower(out_str);
    return wstring_to_utf8str(out_str);
}


S32 utf8str_compare_insensitive(const std::string& lhs, const std::string& rhs)
{
    LLWString wlhs = utf8str_to_wstring(lhs);
    LLWString wrhs = utf8str_to_wstring(rhs);
    return LLWStringUtil::compareInsensitive(wlhs, wrhs);
}

std::string utf8str_truncate(const std::string& utf8str, const S32 max_len)
{
    if (0 == max_len) return std::string();
    if ((S32)utf8str.length() <= max_len) return utf8str;
    return utf8str.substr(0,
        simdutf::trim_partial_utf8(utf8str.data(), (size_t)max_len));
}

// [RLVa:KB] - Checked: RLVa-2.1.0
std::string utf8str_substr(const std::string& utf8str, const S32 index, const S32 max_len)
{
    if (0 == max_len) return std::string();
    if (utf8str.length() - index <= (size_t)max_len)
    {
        return utf8str.substr(index, max_len);
    }
    return utf8str.substr(index,
        simdutf::trim_partial_utf8(utf8str.data() + index, (size_t)max_len));
}

void utf8str_split(std::list<std::string>& split_list, const std::string& utf8str, size_t maxlen, char split_token)
{
    split_list.clear();

    std::string::size_type lenMsg = utf8str.length(), lenIt = 0;

    const char* pstrIt = utf8str.c_str(); std::string strTemp;
    while (lenIt < lenMsg)
    {
        if (lenIt + maxlen < lenMsg)
        {
            // Find the last split character
            const char* pstrTemp = pstrIt + maxlen;
            while ( (pstrTemp > pstrIt) && (*pstrTemp != split_token) )
                pstrTemp--;

            if (pstrTemp > pstrIt)
                strTemp = utf8str.substr(lenIt, pstrTemp - pstrIt);
            else
                strTemp = utf8str_substr(utf8str, narrow(lenIt), narrow(maxlen));
        }
        else
        {
            strTemp = utf8str.substr(lenIt, std::string::npos);
        }

        split_list.push_back(strTemp);

        lenIt += strTemp.length();
        pstrIt = utf8str.c_str() + lenIt;
        if (*pstrIt == split_token)
            lenIt++;
    }
}
// [/RLVa:KB]

std::string utf8str_symbol_truncate(const std::string& utf8str, const S32 symbol_len)
{
    if (0 == symbol_len)
    {
        return std::string();
    }
    if ((S32)utf8str.length() <= symbol_len)
    {
        return utf8str;
    }

    int symbols = 0;
    size_t byteIndex = 0;
    const size_t origSize = utf8str.size();
    while (byteIndex < origSize)
    {
        if ((utf8str[byteIndex] & 0xc0) != 0x80)
        {
            if (symbols == symbol_len)
                break;
            ++symbols;
        }
        ++byteIndex;
    }
    return utf8str.substr(0, byteIndex);
}

std::string utf8str_substChar(
    const std::string& utf8str,
    const llwchar target_char,
    const llwchar replace_char)
{
    LLWString wstr = utf8str_to_wstring(utf8str);
    LLWStringUtil::replaceChar(wstr, target_char, replace_char);
    //wstr = wstring_substChar(wstr, target_char, replace_char);
    return wstring_to_utf8str(wstr);
}

std::string utf8str_makeASCII(const std::string& utf8str)
{
    LLWString wstr = utf8str_to_wstring(utf8str);
    LLWStringUtil::_makeASCII(wstr);
    return wstring_to_utf8str(wstr);
}

std::string mbcsstring_makeASCII(const std::string& wstr)
{
    // Replace non-ASCII chars with replace_char
    std::string out_str = wstr;
    for (S32 i = 0; i < (S32)out_str.length(); i++)
    {
        if ((U8)out_str[i] > 0x7f)
        {
            out_str[i] = LL_UNKNOWN_CHAR;
        }
    }
    return out_str;
}

std::string utf8str_removeCRLF(const std::string& utf8str)
{
    if (0 == utf8str.length())
    {
        return std::string();
    }
    const char CR = 13;

    std::string out;
    out.reserve(utf8str.length());
    const S32 len = (S32)utf8str.length();
    for( S32 i = 0; i < len; i++ )
    {
        if( utf8str[i] != CR )
        {
            out.push_back(utf8str[i]);
        }
    }
    return out;
}

// Only used by utf8str_showBytesUTF8 below. Kept file-local after the simdutf
// migration (no external callers).
static llwchar utf8str_to_wchar(const std::string& utf8str, size_t offset, size_t length)
{
    switch (length)
    {
    case 2:
        return ((utf8str[offset] & 0x1F) << 6) +
                (utf8str[offset + 1] & 0x3F);
    case 3:
        return ((utf8str[offset] & 0x0F) << 12) +
                ((utf8str[offset + 1] & 0x3F) << 6) +
                (utf8str[offset + 2] & 0x3F);
    case 4:
        return ((utf8str[offset] & 0x07) << 18) +
                ((utf8str[offset + 1] & 0x3F) << 12) +
                ((utf8str[offset + 2] & 0x3F) << 6) +
                (utf8str[offset + 3] & 0x3F);
    case 5:
        return ((utf8str[offset] & 0x03) << 24) +
                ((utf8str[offset + 1] & 0x3F) << 18) +
                ((utf8str[offset + 2] & 0x3F) << 12) +
                ((utf8str[offset + 3] & 0x3F) << 6) +
                (utf8str[offset + 4] & 0x3F);
    case 6:
        return ((utf8str[offset] & 0x01) << 30) +
                ((utf8str[offset + 1] & 0x3F) << 24) +
                ((utf8str[offset + 2] & 0x3F) << 18) +
                ((utf8str[offset + 3] & 0x3F) << 12) +
                ((utf8str[offset + 4] & 0x3F) << 6) +
                (utf8str[offset + 5] & 0x3F);
    case 7:
        return ((utf8str[offset + 1] & 0x03) << 30) +
                ((utf8str[offset + 2] & 0x3F) << 24) +
                ((utf8str[offset + 3] & 0x3F) << 18) +
                ((utf8str[offset + 4] & 0x3F) << 12) +
                ((utf8str[offset + 5] & 0x3F) << 6) +
                (utf8str[offset + 6] & 0x3F);
    }
    return LL_UNKNOWN_CHAR;
}

std::string utf8str_showBytesUTF8(const std::string& utf8str)
{
    std::string result;

    bool in_sequence = false;
    size_t sequence_size = 0;
    size_t byte_index = 0;
    size_t source_length = utf8str.size();

    auto open_sequence = [&]()
        {
            if (!result.empty() && result.back() != '\n')
                result += '\n'; // Use LF as a separator before new UTF-8 sequence
            result += '[';
            in_sequence = true;
        };

    auto close_sequence = [&]()
        {
            llwchar unicode = utf8str_to_wchar(utf8str, byte_index - sequence_size, sequence_size);
            if (unicode != LL_UNKNOWN_CHAR)
            {
                result += llformat("+%04X", unicode);
            }
            result += ']';
            in_sequence = false;
            sequence_size = 0;
        };

    while (byte_index < source_length)
    {
        U8 byte = utf8str[byte_index];
        if (byte >= 0x80) // Part of an UTF-8 sequence
        {
            if (!in_sequence) // Start new UTF-8 sequence
            {
                open_sequence();
            }
            else if (byte >= 0xC0) // Start another UTF-8 sequence
            {
                close_sequence();
                open_sequence();
            }
            else // Continue the same UTF-8 sequence
            {
                result += '.';
            }
            result += llformat("%02X", byte); // The byte is represented in hexadecimal form
            ++sequence_size;
        }
        else // ASCII symbol is represented as a character
        {
            if (in_sequence) // End of UTF-8 sequence
            {
                close_sequence();
                if (byte != '\n')
                {
                    result += '\n'; // Use LF as a separator between UTF-8 and ASCII
                }
            }
            result += byte;
        }
        ++byte_index;
    }

    if (in_sequence) // End of UTF-8 sequence
    {
        close_sequence();
    }

    return result;
}

// Search for any emoji symbol, return true if found
bool wstring_has_emoji(LLWStringView wstr)
{
    for (const llwchar& wch : wstr)
    {
        if (LLStringOps::isEmoji(wch))
            return true;
    }

    return false;
}

// Strip every emoji-cluster the cluster walker identifies, plus any isolated
// astral-emoji codepoint (LLStringOps::isEmoji-true) that the walker excludes
// because it shapes correctly via the 1:1 path. Sharing the cluster walker
// here means this function and wstring_find_emoji_clusters can never disagree
// on cluster bounds — historical ad-hoc state machines diverged on tag chars,
// VS-15, BMP-base ZWJ sequences, and keycaps and produced visibly broken
// output (orphan ZWJ, leftover tag bytes) in those cases.
//
// "Cluster" here means anything HarfBuzz would itemize as a single emoji
// glyph — keycaps, BMP-base sequences (heart-on-fire, isolated heart+VS16),
// subdivision flags. Earlier the function used the narrower
// LLStringOps::isEmoji predicate which let composed glyphs survive partial
// stripping; the broader contract matches the visual intent of "remove
// emojis" for input fields like usernames and search.
bool wstring_remove_emojis(LLWString& wstr)
{
    const auto clusters = wstring_find_emoji_clusters(wstr);
    bool found = false;
    size_t read = 0, write = 0;
    auto cluster_it = clusters.begin();
    while (read < wstr.size())
    {
        if (cluster_it != clusters.end() && read == cluster_it->first)
        {
            read = cluster_it->second;
            ++cluster_it;
            found = true;
            continue;
        }
        if (LLStringOps::isEmoji(wstr[read]))
        {
            ++read;
            found = true;
            continue;
        }
        wstr[write++] = wstr[read++];
    }
    if (found)
        wstr.resize(write);
    return found;
}

// Cut emoji symbols if exist
bool utf8str_remove_emojis(std::string& utf8str)
{
    LLWString wstr = utf8str_to_wstring(utf8str);
    if (!wstring_remove_emojis(wstr))
        return false;
    utf8str = wstring_to_utf8str(wstr);
    return true;
}

// Codepoints that can act as a ZWJ/VS emoji-sequence base. Broader than
// LLStringOps::isEmoji (which is restricted to the "genuine" astral emoji
// range so font fallback only routes genuine emoji to the colour face) —
// BMP pictographs like ❤ (U+2764), ©, ®, and the various symbol blocks in
// U+2000..U+32FF are eligible sequence bases per UAX #51, and HarfBuzz
// compositions like ❤️‍🔥 (U+2764 U+FE0F U+200D U+1F525) require they be
// detected here even though they are not "genuine" emoji.
// (Defined as LLStringOps::isPictographBase so the same predicate is
// available to llrender's shape-itemizer and font-fallback walkers without
// duplicating the range list.)

// True if position i begins an emoji sequence that the 1:1 codepoint->glyph
// path cannot render correctly — i.e., the next codepoint transforms the base
// (ZWJ, VS15/16, skin-tone, keycap combiner, tag character, or regional
// indicator pair), or we're sitting on a keycap starter (digit/#/* + FE0F +
// 20E3). Isolated emoji are excluded: they render fine through FreeType alone.
static bool is_shaping_starter(const llwchar* p, size_t n, size_t i)
{
    const llwchar c = p[i];
    // Keycap sequence: digit/#/* + VS16 + COMBINING ENCLOSING KEYCAP.
    // shapeRun itemises these into per-face sub-runs (digit on the text
    // font, combining mark on the emoji font) so we can treat keycap as
    // one cluster for cursor/grapheme purposes without losing the mark's
    // natural overlay on the base.
    if ((c == '#' || c == '*' || (c >= '0' && c <= '9'))
        && i + 2 < n && p[i + 1] == 0xFE0F && p[i + 2] == 0x20E3)
    {
        return true;
    }
    if (!LLStringOps::isPictographBase(c) || i + 1 >= n)
        return false;
    const llwchar next = p[i + 1];
    if (next == 0x200D || LLStringOps::isEmojiClusterExtender(next))
        return true;
    // Regional indicator pair (flag).
    return c >= 0x1F1E6 && c <= 0x1F1FF
        && next >= 0x1F1E6 && next <= 0x1F1FF;
}

// Greedy forward walk from a confirmed shaping-starter position, returning the
// one-past-end index of the sequence.
static size_t advance_shaping_run(const llwchar* p, size_t n, size_t start)
{
    const llwchar base = p[start];
    size_t r = start + 1;

    // Keycap: always exactly 3 codepoints.
    if ((base == '#' || base == '*' || (base >= '0' && base <= '9'))
        && r + 1 < n && p[r] == 0xFE0F && p[r + 1] == 0x20E3)
    {
        return r + 2;
    }

    // Regional indicator pair: exactly one trailing RI.
    if (base >= 0x1F1E6 && base <= 0x1F1FF
        && r < n && p[r] >= 0x1F1E6 && p[r] <= 0x1F1FF)
    {
        return r + 1;
    }

    // General case: ZWJ joins another base, then any number of plain
    // extenders (VS, skin-tone, keycap mark, tag chars).
    while (r < n)
    {
        const llwchar c = p[r];
        if (c == 0x200D)
        {
            // Accept any pictograph base after the joiner, including BMP
            // pictographs like 🔥's partner heart in ❤️‍🔥 where the base
            // sits outside the astral emoji range.
            if (r + 1 < n && LLStringOps::isPictographBase(p[r + 1]))
            {
                r += 2;
                continue;
            }
            break; // orphan ZWJ
        }
        if (LLStringOps::isEmojiClusterExtender(c))
        {
            ++r;
            continue;
        }
        break;
    }
    return r;
}

EmojiClusterList wstring_find_emoji_clusters(LLWStringView wstr)
{
    EmojiClusterList runs;
    const llwchar* p = wstr.data();
    const size_t n = wstr.size();
    size_t i = 0;
    while (i < n)
    {
        if (is_shaping_starter(p, n, i))
        {
            const size_t end = advance_shaping_run(p, n, i);
            // is_shaping_starter accepts a base when the *next* codepoint is
            // an extender, but advance_shaping_run can still bail (e.g. a
            // ZWJ at end of string with nothing after it, or a ZWJ followed
            // by something that isn't a pictograph base). That leaves us
            // with a degenerate length-1 "cluster" that's really just the
            // base alone with a dangling extender. Skip those — isolated
            // single-codepoint emoji shape correctly via the 1:1 path and
            // are intentionally absent from the cluster list.
            if (end > i + 1)
                runs.emplace_back(i, end);
            i = end;
        }
        else
        {
            ++i;
        }
    }
    return runs;
}

size_t wstring_step_grapheme_forward(LLWStringView wstr, size_t pos,
                                     const EmojiClusterList& clusters)
{
    const size_t n = wstr.size();
    if (pos >= n)
        return n;
    const size_t next = pos + 1;
    // Clusters are sorted by start, so stop scanning once we pass `next`.
    for (const auto& run : clusters)
    {
        if (next <= run.first)
            break;
        if (run.first < next && next < run.second)
            return run.second;
    }
    return next;
}

size_t wstring_step_grapheme_forward(LLWStringView wstr, size_t pos)
{
    return wstring_step_grapheme_forward(wstr, pos, wstring_find_emoji_clusters(wstr));
}

size_t wstring_step_grapheme_backward(LLWStringView wstr, size_t pos,
                                      const EmojiClusterList& clusters)
{
    if (pos == 0)
        return 0;
    const size_t prev = pos - 1;
    for (const auto& run : clusters)
    {
        if (prev < run.first)
            break;
        if (run.first < prev && prev < run.second)
            return run.first;
    }
    return prev;
}

size_t wstring_step_grapheme_backward(LLWStringView wstr, size_t pos)
{
    return wstring_step_grapheme_backward(wstr, pos, wstring_find_emoji_clusters(wstr));
}

size_t wstring_grapheme_align_backward(LLWStringView wstr, size_t pos,
                                       const EmojiClusterList& clusters)
{
    if (pos == 0 || pos >= wstr.size())
        return pos;
    for (const auto& run : clusters)
    {
        if (pos <= run.first)
            break;
        if (run.first < pos && pos < run.second)
            return run.first;
    }
    return pos;
}

size_t wstring_grapheme_align_backward(LLWStringView wstr, size_t pos)
{
    return wstring_grapheme_align_backward(wstr, pos, wstring_find_emoji_clusters(wstr));
}

size_t wstring_grapheme_align_forward(LLWStringView wstr, size_t pos,
                                      const EmojiClusterList& clusters)
{
    if (pos >= wstr.size())
        return wstr.size();
    for (const auto& run : clusters)
    {
        if (pos <= run.first)
            break;
        if (run.first < pos && pos < run.second)
            return run.second;
    }
    return pos;
}

size_t wstring_grapheme_align_forward(LLWStringView wstr, size_t pos)
{
    return wstring_grapheme_align_forward(wstr, pos, wstring_find_emoji_clusters(wstr));
}

std::pair<size_t, size_t> wstring_emoji_range_at(LLWStringView wstr, size_t pos,
                                                 const EmojiClusterList& clusters)
{
    if (pos >= wstr.size())
        return { pos, pos };
    for (const auto& run : clusters)
    {
        if (pos < run.first)
            break;
        if (pos < run.second)
            return run;
    }
    // Single-codepoint pictographs are intentionally absent from the cluster
    // list (they shape via the 1:1 path), but tooltip lookup still wants
    // their bounds. Use isPictographBase rather than isEmoji so BMP
    // pictographs (©, ®, ☦, ⚓, ❤, …) get a range; isPictographBase already
    // excludes extenders (ZWJ, VS-15/16, keycap combiner, skin-tone mods,
    // tag chars) which have no business being a standalone tooltip target.
    return LLStringOps::isPictographBase(wstr[pos])
        ? std::make_pair(pos, pos + 1)
        : std::make_pair(pos, pos);
}

std::pair<size_t, size_t> wstring_emoji_range_at(LLWStringView wstr, size_t pos)
{
    return wstring_emoji_range_at(wstr, pos, wstring_find_emoji_clusters(wstr));
}

#if LL_WINDOWS
unsigned int ll_wstring_default_code_page()
{
    return CP_UTF8;
}

std::string ll_convert_wide_to_string(const wchar_t* in, size_t len_in, unsigned int code_page)
{
    std::string out;
    if(in)
    {
        int len_out = WideCharToMultiByte(
            code_page,
            0,
            in,
            static_cast<int>(len_in),
            NULL,
            0,
            0,
            0);
        // We will need two more bytes for the double NULL ending
        // created in WideCharToMultiByte().
        char* pout = new char [len_out + 2];
        memset(pout, 0, len_out + 2);
        if(pout)
        {
            WideCharToMultiByte(
                code_page,
                0,
                in,
                static_cast<int>(len_in),
                pout,
                len_out,
                0,
                0);
            out.assign(pout);
            delete[] pout;
        }
    }
    return out;
}

std::wstring ll_convert_string_to_wide(const char* in, size_t len, unsigned int code_page)
{
    // From review:
    // We can preallocate a wide char buffer that is the same length (in wchar_t elements) as the utf8 input,
    // plus one for a null terminator, and be guaranteed to not overflow.

    //  Normally, I'd call that sort of thing premature optimization,
    // but we *are* seeing string operations taking a bunch of time, especially when constructing widgets.
//  int output_str_len = MultiByteToWideChar(code_page, 0, in.c_str(), in.length(), NULL, 0);

    // reserve an output buffer that will be destroyed on exit, with a place
    // to put NULL terminator
    std::vector<wchar_t> w_out(len + 1);

    memset(&w_out[0], 0, w_out.size());
    int real_output_str_len = MultiByteToWideChar(code_page, 0, in, static_cast<int>(len),
                                                  &w_out[0], static_cast<int>(w_out.size() - 1));

    //looks like MultiByteToWideChar didn't add null terminator to converted string, see EXT-4858.
    w_out[real_output_str_len] = 0;

    // construct string<wchar_t> from our temporary output buffer
    return {&w_out[0]};
}

LLWString ll_convert_wide_to_wstring(const wchar_t* in, size_t len)
{
    // Windows wchar_t is 16-bit UTF-16LE — same layout as char16_t.
    return utf_convert_with_replacement<char16_t, char32_t>(
        reinterpret_cast<const char16_t*>(in), len,
        &simdutf::validate_utf16le_with_errors,
        &simdutf::utf32_length_from_utf16le,
        &simdutf::convert_valid_utf16le_to_utf32,
        static_cast<char32_t>(LL_UNKNOWN_CHAR));
}

std::wstring ll_convert_wstring_to_wide(const llwchar* in, size_t len)
{
    const auto utf16 = utf_convert_with_replacement<char32_t, char16_t>(
        in, len,
        &simdutf::validate_utf32_with_errors,
        &simdutf::utf16_length_from_utf32,
        &simdutf::convert_valid_utf32_to_utf16le,
        static_cast<char16_t>(LL_UNKNOWN_CHAR));
    return { reinterpret_cast<const wchar_t*>(utf16.data()), utf16.size() };
}

std::string ll_convert_string_to_utf8_string(const std::string& in)
{
    // If you pass code_page, you must also pass length, otherwise the code
    // page parameter will be mistaken for length.
    auto w_mesg = ll_convert_string_to_wide(in, in.length(), CP_ACP);
    // CP_UTF8 is default -- see ll_wstring_default_code_page() above.
    return ll_convert_wide_to_string(w_mesg);
}

namespace
{

void HeapFree_deleter(void* ptr)
{
    // instead of LocalFree(), per https://stackoverflow.com/a/31541205
    HeapFree(GetProcessHeap(), NULL, ptr);
}

} // anonymous namespace

unsigned long windows_get_last_error()
{
    return GetLastError();
}

template<>
LL_COMMON_API std::wstring windows_message<std::wstring>(DWORD error)
{
    // derived from https://stackoverflow.com/a/455533
    wchar_t* rawptr = nullptr;
    auto okay = FormatMessageW(
        // use system message tables for GetLastError() codes
        FORMAT_MESSAGE_FROM_SYSTEM |
        // internally allocate buffer and return its pointer
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        // you cannot pass insertion parameters (thanks Gandalf)
        FORMAT_MESSAGE_IGNORE_INSERTS |
        // ignore line breaks in message definition text
        FORMAT_MESSAGE_MAX_WIDTH_MASK,
        NULL,                       // lpSource, unused with FORMAT_MESSAGE_FROM_SYSTEM
        error,                      // dwMessageId
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // dwLanguageId
        (LPWSTR)&rawptr,         // lpBuffer: force-cast wchar_t** to wchar_t*
        0,                // nSize, unused with FORMAT_MESSAGE_ALLOCATE_BUFFER
        NULL);            // Arguments, unused

    // make a unique_ptr from rawptr so it gets cleaned up properly
    std::unique_ptr<wchar_t, void(*)(void*)> bufferptr(rawptr, HeapFree_deleter);

    if (okay && bufferptr)
    {
        // got the message, return it ('okay' is length in characters)
        return { bufferptr.get(), okay };
    }

    // did not get the message, synthesize one
    auto format_message_error = GetLastError();
    std::wostringstream out;
    out << L"GetLastError() " << error << L" (FormatMessageW() failed with "
        << format_message_error << L")";
    return out.str();
}

std::optional<std::wstring> llstring_getoptenv(const std::string& key)
{
    auto wkey = ll_convert_string_to_wide(key);
    // Take a wild guess as to how big the buffer should be.
    std::vector<wchar_t> buffer(1024);
    auto n = GetEnvironmentVariableW(wkey.c_str(), &buffer[0], static_cast<DWORD>(buffer.size()));
    // If our initial guess was too short, n will indicate the size (in
    // wchar_t's) that buffer should have been, including the terminating nul.
    if (n > (buffer.size() - 1))
    {
        // make it big enough
        buffer.resize(n);
        // and try again
        n = GetEnvironmentVariableW(wkey.c_str(), &buffer[0], static_cast<DWORD>(buffer.size()));
    }
    // did that (ultimately) succeed?
    if (n)
    {
        // great, return populated std::optional
        return std::make_optional<std::wstring>(&buffer[0]);
    }

    // not successful
    auto last_error = GetLastError();
    // Don't bother warning for NOT_FOUND; that's an expected case
    if (last_error != ERROR_ENVVAR_NOT_FOUND)
    {
        LL_WARNS() << "GetEnvironmentVariableW('" << key << "') failed: "
                   << windows_message<std::string>(last_error) << LL_ENDL;
    }
    // return empty std::optional
    return {};
}

// Length in llwchar (UTF-32) of the first len units (16 bits) of the given UTF-16 string.
S32 wide_wstring_length(const std::wstring& utf16str, const S32 utf16_len)
{
    if (utf16_len <= 0) return 0;
    return (S32)simdutf::utf32_length_from_utf16le(
        reinterpret_cast<const char16_t*>(utf16str.data()),
        (size_t)utf16_len);
}

#else  // ! LL_WINDOWS

std::optional<std::string> llstring_getoptenv(const std::string& key)
{
    auto found = getenv(key.c_str());
    if (found)
    {
        // return populated std::optional
        return std::make_optional<std::string>(found);
    }
    else
    {
        // return empty std::optional
        return {};
    }
}

#endif // ! LL_WINDOWS

long LLStringOps::sPacificTimeOffset = 0;
long LLStringOps::sLocalTimeOffset = 0;
bool LLStringOps::sPacificDaylightTime = 0;
std::map<std::string, std::string> LLStringOps::datetimeToCodes;

std::vector<std::string> LLStringOps::sWeekDayList;
std::vector<std::string> LLStringOps::sWeekDayShortList;
std::vector<std::string> LLStringOps::sMonthList;
std::vector<std::string> LLStringOps::sMonthShortList;


std::string LLStringOps::sDayFormat;
std::string LLStringOps::sAM;
std::string LLStringOps::sPM;

// static
bool LLStringOps::isEmoji(llwchar a)
{
#if 0   // Do not consider special characters that might have a corresponding
        // glyph in the monochorme fallback fonts as a "genuine" emoji. HB
    return a == 0xa9 || a == 0xae || (a >= 0x2000 && a < 0x3300) ||
           (a >= 0x1f000 && a < 0x20000);
#else
    // These are indeed "genuine" emojis, we *do want* rendered as such. HB
    return a >= 0x1f000 && a < 0x20000;
#endif
    }

// static
bool LLStringOps::isPictographBase(llwchar a)
{
    // Emoji-sequence extenders sit inside the broad pictograph ranges below
    // (notably ZWJ at U+200D in the General Punctuation block, and the
    // skin-tone modifiers in U+1F3FB..U+1F3FF). Exclude them up front so
    // callers that ask "is this codepoint a base of an emoji cluster" get
    // a no for these — the cluster walker handles them as extenders.
    if (a == 0x200C || a == 0x200D)            // ZWNJ, ZWJ
        return false;
    if (a == 0xFE0E || a == 0xFE0F)            // VS-15 / VS-16
        return false;
    if (a == 0x20E3)                           // combining enclosing keycap
        return false;
    if (a >= 0x1F3FB && a <= 0x1F3FF)          // skin-tone modifiers
        return false;
    if (a >= 0xE0020 && a <= 0xE007F)          // tag characters (SP-CANCEL)
        return false;

    return a == 0xA9 || a == 0xAE
        || (a >= 0x2000 && a < 0x3300)
        || (a >= 0x1F000 && a < 0x20000);
}

bool LLStringOps::isEmojiClusterExtender(llwchar a)
{
    return a == 0xFE0E || a == 0xFE0F            // VS-15 / VS-16
        || a == 0x20E3                           // keycap combiner
        || (a >= 0x1F3FB && a <= 0x1F3FF)        // skin-tone modifiers
        || (a >= 0xE0020 && a <= 0xE007F);       // tag chars + CANCEL TAG
}

S32 LLStringOps::collate(const llwchar* a, const llwchar* b)
{
#if LL_WINDOWS
    // in Windows, wide string functions operator on 16-bit strings,
    // not the proper 32 bit wide string
    return wcscmp(ll_convert<std::wstring>(a).c_str(), ll_convert<std::wstring>(b).c_str());
#else
    return wcscoll((const wchar_t*)a, (const wchar_t*)b);
#endif
}

void LLStringOps::setupDatetimeInfo (bool daylight)
{
    time_t nowT, localT, gmtT;
    struct tm * tmpT;

    nowT = time (NULL);

    tmpT = gmtime (&nowT);
    gmtT = mktime (tmpT);

    tmpT = localtime (&nowT);
    localT = mktime (tmpT);

    sLocalTimeOffset = (long) (gmtT - localT);
    if (tmpT->tm_isdst)
    {
        sLocalTimeOffset -= 60 * 60;    // 1 hour
    }

    sPacificDaylightTime = daylight;
    sPacificTimeOffset = (sPacificDaylightTime? 7 : 8 ) * 60 * 60;

    datetimeToCodes["wkday"]    = "%a";     // Thu
    datetimeToCodes["weekday"]  = "%A";     // Thursday
    datetimeToCodes["year4"]    = "%Y";     // 2009
    datetimeToCodes["year"]     = "%Y";     // 2009
    datetimeToCodes["year2"]    = "%y";     // 09
    datetimeToCodes["mth"]      = "%b";     // Aug
    datetimeToCodes["month"]    = "%B";     // August
    datetimeToCodes["mthnum"]   = "%m";     // 08
    datetimeToCodes["day"]      = "%d";     // 31
    datetimeToCodes["sday"]     = "%-d";    // 9
    datetimeToCodes["hour24"]   = "%H";     // 14
    datetimeToCodes["hour"]     = "%H";     // 14
    datetimeToCodes["hour12"]   = "%I";     // 02
    datetimeToCodes["min"]      = "%M";     // 59
    datetimeToCodes["ampm"]     = "%p";     // AM
    datetimeToCodes["second"]   = "%S";     // 59
    datetimeToCodes["timezone"] = "%Z";     // PST
}

void tokenizeStringToArray(const std::string& data, std::vector<std::string>& output)
{
    output.clear();
    size_t length = data.size();

    // tokenize it and put it in the array
    std::string cur_word;
    for(size_t i = 0; i < length; ++i)
    {
        if(data[i] == ':')
        {
            output.push_back(cur_word);
            cur_word.clear();
        }
        else
        {
            cur_word.append(1, data[i]);
        }
    }
    output.push_back(cur_word);
}

void LLStringOps::setupWeekDaysNames(const std::string& data)
{
    tokenizeStringToArray(data,sWeekDayList);
}
void LLStringOps::setupWeekDaysShortNames(const std::string& data)
{
    tokenizeStringToArray(data,sWeekDayShortList);
}
void LLStringOps::setupMonthNames(const std::string& data)
{
    tokenizeStringToArray(data,sMonthList);
}
void LLStringOps::setupMonthShortNames(const std::string& data)
{
    tokenizeStringToArray(data,sMonthShortList);
}
void LLStringOps::setupDayFormat(const std::string& data)
{
    sDayFormat = data;
}


std::string LLStringOps::getDatetimeCode (std::string key)
{
    std::map<std::string, std::string>::iterator iter;

    iter = datetimeToCodes.find (key);
    if (iter != datetimeToCodes.end())
    {
        return iter->second;
    }
    else
    {
        return std::string("");
    }
}

std::string LLStringOps::getReadableNumber(F64 num)
{
    if (fabs(num)>=1e9)
    {
        return llformat("%.2lfB", num / 1e9);
    }
    else if (fabs(num)>=1e6)
    {
        return llformat("%.2lfM", num / 1e6);
    }
    else if (fabs(num)>=1e3)
    {
        return llformat("%.2lfK", num / 1e3);
    }
    else
    {
        return llformat("%.2lf", num);
    }
}

namespace LLStringFn
{
    // NOTE - this restricts output to ascii
    void replace_nonprintable_in_ascii(std::basic_string<char>& string, char replacement)
    {
        const char MIN = 0x20;
        std::basic_string<char>::size_type len = string.size();
        for(std::basic_string<char>::size_type ii = 0; ii < len; ++ii)
        {
            if(string[ii] < MIN)
            {
                string[ii] = replacement;
            }
        }
    }


    // NOTE - this restricts output to ascii
    void replace_nonprintable_and_pipe_in_ascii(std::basic_string<char>& str,
                                       char replacement)
    {
        const char MIN  = 0x20;
        const char PIPE = 0x7c;
        std::basic_string<char>::size_type len = str.size();
        for(std::basic_string<char>::size_type ii = 0; ii < len; ++ii)
        {
            if( (str[ii] < MIN) || (str[ii] == PIPE) )
            {
                str[ii] = replacement;
            }
        }
    }

    // https://wiki.lindenlab.com/wiki/Unicode_Guidelines has details on
    // allowable code points for XML. Specifically, they are:
    // 0x09, 0x0a, 0x0d, and 0x20 on up.  JC
    std::string strip_invalid_xml(const std::string& instr)
    {
        std::string output;
        output.reserve( instr.size() );
        std::string::const_iterator it = instr.begin();
        while (it != instr.end())
        {
            // Must compare as unsigned for >=
            // Test most likely match first
            const unsigned char c = (unsigned char)*it;
            if (   c >= (unsigned char)0x20   // SPACE
                || c == (unsigned char)0x09   // TAB
                || c == (unsigned char)0x0a   // LINE_FEED
                || c == (unsigned char)0x0d ) // CARRIAGE_RETURN
            {
                output.push_back(c);
            }
            ++it;
        }
        return output;
    }

    /**
     * @brief Replace all characters that are not allowed in XML 1.0
     * with corresponding literals: [ < > & ] => [ &lt; &gt; &amp; ]
     * (plus [ " ' ] => [ &quot; &apos; ] when encoding for an attribute)
     */
    std::string xml_encode(const std::string& input, bool for_attribute)
    {
        std::string result;
        result.reserve(input.size());
        const char* const end = input.data() + input.size();
        const char* run = input.data();
        for (const char* p = run; p < end; ++p)
        {
            const char* entity;
            switch (*p)
            {
            case '<': entity = "&lt;"; break;
            case '>': entity = "&gt;"; break;
            case '&': entity = "&amp;"; break;
            case '"': if (!for_attribute) continue; entity = "&quot;"; break;
            case '\'': if (!for_attribute) continue; entity = "&apos;"; break;
            default: continue;
            }
            result.append(run, p);
            result.append(entity);
            run = p + 1;
        }
        result.append(run, end);
        return result;
    }

    /**
     * @brief Replace some of XML literals that are defined in XML 1.0
     * with corresponding characters: [ &lt; &gt; &amp; ] => [ < > & ]
     * (plus [ &quot; &apos; ] => [ " ' ] when decoding an attribute)
     *
     * Each literal is decoded once, left to right, so text like &amp;lt;
     * correctly becomes &lt; rather than cascading down to <.
     */
    std::string xml_decode(const std::string& input, bool for_attribute)
    {
        std::string result;
        result.reserve(input.size());
        const std::string_view text(input);
        std::string_view::size_type run = 0; // start of the undecoded run
        for (std::string_view::size_type pos = 0;
             (pos = text.find('&', pos)) != std::string_view::npos; )
        {
            const std::string_view rest = text.substr(pos);
            char decoded;
            std::string_view::size_type len;
            if (rest.starts_with("&lt;"))       { decoded = '<';  len = 4; }
            else if (rest.starts_with("&gt;"))  { decoded = '>';  len = 4; }
            else if (rest.starts_with("&amp;")) { decoded = '&';  len = 5; }
            else if (for_attribute && rest.starts_with("&quot;")) { decoded = '"';  len = 6; }
            else if (for_attribute && rest.starts_with("&apos;")) { decoded = '\''; len = 6; }
            else { ++pos; continue; }
            result.append(text, run, pos - run);
            result.push_back(decoded);
            pos += len;
            run = pos;
        }
        result.append(text, run, text.size() - run);
        return result;
    }

    /**
     * @brief Replace all control characters (c < 0x20) with replacement in
     * string.
     */
    void replace_ascii_controlchars(std::basic_string<char>& string, char replacement)
    {
        const unsigned char MIN = 0x20;
        std::basic_string<char>::size_type len = string.size();
        for(std::basic_string<char>::size_type ii = 0; ii < len; ++ii)
        {
            const unsigned char c = (unsigned char) string[ii];
            if(c < MIN)
            {
                string[ii] = replacement;
            }
        }
    }
}

////////////////////////////////////////////////////////////

// Forward specialization of LLStringUtil::format before use in LLStringUtil::formatDatetime.
template<>
S32 LLStringUtil::format(std::string& s, const format_map_t& substitutions);

//static
template<>
void LLStringUtil::getTokens(const std::string& instr, std::vector<std::string >& tokens, const std::string& delims)
{
    // Starting at offset 0, scan forward for the next non-delimiter. We're
    // done when the only characters left in 'instr' are delimiters.
    for (std::string::size_type begIdx, endIdx = 0;
         (begIdx = instr.find_first_not_of (delims, endIdx)) != std::string::npos; )
    {
        // Found a non-delimiter. After that, find the next delimiter.
        endIdx = instr.find_first_of (delims, begIdx);
        if (endIdx == std::string::npos)
        {
            // No more delimiters: this token extends to the end of the string.
            endIdx = instr.length();
        }

        // extract the token between begIdx and endIdx; substr() needs length
        std::string currToken(instr.substr(begIdx, endIdx - begIdx));
        LLStringUtil::trim (currToken);
        tokens.push_back(currToken);
        // next scan past delimiters starts at endIdx
    }
}

template<>
LLStringUtil::size_type LLStringUtil::getSubstitution(const std::string& instr, size_type& start, std::vector<std::string>& tokens)
{
    const std::string delims (",");

    // Find the first [
    size_type pos1 = instr.find('[', start);
    if (pos1 == std::string::npos)
        return std::string::npos;

    //Find the first ] after the initial [
    size_type pos2 = instr.find(']', pos1);
    if (pos2 == std::string::npos)
        return std::string::npos;

    // Find the last [ before ] in case of nested [[]]
    pos1 = instr.find_last_of('[', pos2-1);
    if (pos1 == std::string::npos || pos1 < start)
        return std::string::npos;

    getTokens(std::string(instr,pos1+1,pos2-pos1-1), tokens, delims);
    start = pos2+1;

    return pos1;
}

// static
template<>
bool LLStringUtil::simpleReplacement(std::string &replacement, std::string token, const format_map_t& substitutions)
{
    // see if we have a replacement for the bracketed string (without the brackets)
    // test first using has() because if we just look up with operator[] we get back an
    // empty string even if the value is missing. We want to distinguish between
    // missing replacements and deliberately empty replacement strings.
    format_map_t::const_iterator iter = substitutions.find(token);
    if (iter != substitutions.end())
    {
        replacement = iter->second;
        return true;
    }
    // if not, see if there's one WITH brackets
    iter = substitutions.find(std::string("[" + token + "]"));
    if (iter != substitutions.end())
    {
        replacement = iter->second;
        return true;
    }

    return false;
}

// static
template<>
bool LLStringUtil::simpleReplacement(std::string &replacement, std::string token, const LLSD& substitutions)
{
    // see if we have a replacement for the bracketed string (without the brackets)
    // test first using has() because if we just look up with operator[] we get back an
    // empty string even if the value is missing. We want to distinguish between
    // missing replacements and deliberately empty replacement strings.
    if (substitutions.has(token))
    {
        replacement = substitutions[token].asString();
        return true;
    }
    // if not, see if there's one WITH brackets
    else if (substitutions.has(std::string("[" + token + "]")))
    {
        replacement = substitutions[std::string("[" + token + "]")].asString();
        return true;
    }

    return false;
}

//static
template<>
void LLStringUtil::setLocale(std::string inLocale)
{
    if(startsWith(inLocale, "MissingString"))
    {
        // it seems this hasn't been working for some time, and I'm not sure how it is intentded to
        // properly discover the correct locale.  early out now to avoid failures later in
        // formatNumber()
        LL_WARNS() << "Failed attempting to set invalid locale: " << inLocale << LL_ENDL;
        return;
    }
    sLocale = inLocale;
};

//static
template<>
std::string LLStringUtil::getLocale(void)
{
    return sLocale;
};

// static
template<>
void LLStringUtil::formatNumber(std::string& numStr, std::string decimals)
{
    std::stringstream strStream;
    S32 intDecimals = 0;

    convertToS32 (decimals, intDecimals);
    if (!sLocale.empty())
    {
        // std::locale() throws if the locale is unknown! (EXT-7926)
        try
        {
            strStream.imbue(std::locale(sLocale.c_str()));
        } catch (const std::exception &)
        {
            LL_WARNS_ONCE("Locale") << "Cannot set locale to " << sLocale << LL_ENDL;
        }
    }

    if (!intDecimals)
    {
        S32 intStr;

        if (convertToS32(numStr, intStr))
        {
            strStream << intStr;
            numStr = strStream.str();
        }
    }
    else
    {
        F32 floatStr;

        if (convertToF32(numStr, floatStr))
        {
            strStream << std::fixed << std::showpoint << std::setprecision(intDecimals) << floatStr;
            numStr = strStream.str();
        }
    }
}

// static
template<>
bool LLStringUtil::formatDatetime(std::string& replacement, std::string token,
                                  std::string param, S32 secFromEpoch)
{
    if (param == "local")   // local
    {
        secFromEpoch -= LLStringOps::getLocalTimeOffset();
    }
    else if (param != "utc") // slt
    {
        secFromEpoch -= LLStringOps::getPacificTimeOffset();
    }

    // if never fell into those two ifs above, param must be utc
    if (secFromEpoch < 0) secFromEpoch = 0;

    LLDate datetime((F64)secFromEpoch);
    std::string code = LLStringOps::getDatetimeCode (token);

    // special case to handle timezone
    if (code == "%Z") {
        if (param == "utc")
        {
            replacement = "GMT";
        }
        else if (param == "local")
        {
            replacement = "";       // user knows their own timezone
        }
        else
        {
#if 0
            // EXT-1565 : Zai Lynch, James Linden : 15/Oct/09
            // [BSI] Feedback: Viewer clock mentions SLT, but would prefer it to show PST/PDT
            // "slt" = Second Life Time, which is deprecated.
            // If not utc or user local time, fallback to Pacific time
            replacement = LLStringOps::getPacificDaylightTime() ? "PDT" : "PST";
#else
            // SL-20370 : Steeltoe Linden : 29/Sep/23
            // Change "PDT" to "SLT" on menu bar
            replacement = "SLT";
#endif
        }
        return true;
    }

    //EXT-7013
    //few codes are not suppotred by strtime function (example - weekdays for Japanise)
    //so use predefined ones

    //if sWeekDayList is not empty than current locale doesn't support
        //weekday name.
    time_t loc_seconds = (time_t) secFromEpoch;
    if(LLStringOps::sWeekDayList.size() == 7 && code == "%A")
    {
        struct tm * gmt = gmtime (&loc_seconds);
        replacement = LLStringOps::sWeekDayList[gmt->tm_wday];
    }
    else if(LLStringOps::sWeekDayShortList.size() == 7 && code == "%a")
    {
        struct tm * gmt = gmtime (&loc_seconds);
        replacement = LLStringOps::sWeekDayShortList[gmt->tm_wday];
    }
    else if(LLStringOps::sMonthList.size() == 12 && code == "%B")
    {
        struct tm * gmt = gmtime (&loc_seconds);
        replacement = LLStringOps::sMonthList[gmt->tm_mon];
    }
    else if( !LLStringOps::sDayFormat.empty() && code == "%d" )
    {
        struct tm * gmt = gmtime (&loc_seconds);
        LLStringUtil::format_map_t args;
        args["[MDAY]"] = llformat ("%d", gmt->tm_mday);
        replacement = LLStringOps::sDayFormat;
        LLStringUtil::format(replacement, args);
    }
    else if (code == "%-d")
    {
        struct tm * gmt = gmtime (&loc_seconds);
        replacement = llformat ("%d", gmt->tm_mday); // day of the month without leading zero
    }
    else if( !LLStringOps::sAM.empty() && !LLStringOps::sPM.empty() && code == "%p" )
    {
        struct tm * gmt = gmtime (&loc_seconds);
        if(gmt->tm_hour<12)
        {
            replacement = LLStringOps::sAM;
        }
        else
        {
            replacement = LLStringOps::sPM;
        }
    }
    else
    {
        replacement = datetime.toHTTPDateString(code);
    }

    // *HACK: delete leading zero from hour string in case 'hour12' (code = %I) time format
    // to show time without leading zero, e.g. 08:16 -> 8:16 (EXT-2738).
    // We could have used '%l' format instead, but it's not supported by Windows.
    if(code == "%I" && token == "hour12" && replacement.at(0) == '0')
    {
        replacement = replacement.at(1);
    }

    return !code.empty();
}

// LLStringUtil::format recogizes the following patterns.
// All substitutions *must* be encased in []'s in the input string.
// The []'s are optional in the substitution map.
// [FOO_123]
// [FOO,number,precision]
// [FOO,datetime,format]


// static
template<>
S32 LLStringUtil::format(std::string& s, const format_map_t& substitutions)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_STRING;
    S32 res = 0;

    // Every substitution is bracketed, so a string with no '[' cannot produce
    // one. Bail before building `output`: the loop below would copy the whole
    // string into it and assign back, and most strings that reach here (button
    // labels, menu entries, tooltips) have nothing to substitute.
    if (s.find('[') == std::string::npos)
    {
        return res;
    }

    std::string output;
    std::vector<std::string> tokens;

    std::string::size_type start = 0;
    std::string::size_type prev_start = 0;
    std::string::size_type key_start = 0;
    while ((key_start = getSubstitution(s, start, tokens)) != std::string::npos)
    {
        output += std::string(s, prev_start, key_start-prev_start);
        prev_start = start;

        bool found_replacement = false;
        std::string replacement;

        if (tokens.size() == 0)
        {
            found_replacement = false;
        }
        else if (tokens.size() == 1)
        {
            found_replacement = simpleReplacement (replacement, tokens[0], substitutions);
        }
        else if (tokens[1] == "number")
        {
            std::string param = "0";

            if (tokens.size() > 2) param = tokens[2];
            found_replacement = simpleReplacement (replacement, tokens[0], substitutions);
            if (found_replacement) formatNumber (replacement, param);
        }
        else if (tokens[1] == "datetime")
        {
            std::string param;
            if (tokens.size() > 2) param = tokens[2];

            format_map_t::const_iterator iter = substitutions.find("datetime");
            if (iter != substitutions.end())
            {
                S32 secFromEpoch = 0;
                bool r = LLStringUtil::convertToS32(iter->second(), secFromEpoch);
                if (r)
                {
                    found_replacement = formatDatetime(replacement, tokens[0], param, secFromEpoch);
                }
            }
        }

        if (found_replacement)
        {
            output += replacement;
            res++;
        }
        else
        {
            // we had no replacement, use the string as is
            // e.g. "hello [MISSING_REPLACEMENT]" or "-=[Stylized Name]=-"
            output += std::string(s, key_start, start-key_start);
        }
        tokens.clear();
    }
    // send the remainder of the string (with no further matches for bracketed names)
    output += std::string(s, start);
    s = output;
    return res;
}

//static
template<>
S32 LLStringUtil::format(std::string& s, const LLSD& substitutions)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_STRING;
    S32 res = 0;

    if (!substitutions.isMap())
    {
        return res;
    }

    // See the format_map_t overload: no '[' means no substitution is possible.
    if (s.find('[') == std::string::npos)
    {
        return res;
    }

    std::string output;
    std::vector<std::string> tokens;

    std::string::size_type start = 0;
    std::string::size_type prev_start = 0;
    std::string::size_type key_start = 0;
    while ((key_start = getSubstitution(s, start, tokens)) != std::string::npos)
    {
        output += std::string(s, prev_start, key_start-prev_start);
        prev_start = start;

        bool found_replacement = false;
        std::string replacement;

        if (tokens.size() == 0)
        {
            found_replacement = false;
        }
        else if (tokens.size() == 1)
        {
            found_replacement = simpleReplacement (replacement, tokens[0], substitutions);
        }
        else if (tokens[1] == "number")
        {
            std::string param = "0";

            if (tokens.size() > 2) param = tokens[2];
            found_replacement = simpleReplacement (replacement, tokens[0], substitutions);
            if (found_replacement) formatNumber (replacement, param);
        }
        else if (tokens[1] == "datetime")
        {
            std::string param;
            if (tokens.size() > 2) param = tokens[2];

            S32 secFromEpoch = (S32) substitutions["datetime"].asInteger();
            found_replacement = formatDatetime (replacement, tokens[0], param, secFromEpoch);
        }

        if (found_replacement)
        {
            output += replacement;
            res++;
        }
        else
        {
            // we had no replacement, use the string as is
            // e.g. "hello [MISSING_REPLACEMENT]" or "-=[Stylized Name]=-"
            output += std::string(s, key_start, start-key_start);
        }
        tokens.clear();
    }
    // send the remainder of the string (with no further matches for bracketed names)
    output += std::string(s, start);
    s = output;
    return res;
}

////////////////////////////////////////////////////////////
// Testing

#ifdef _DEBUG

template<class T>
void LLStringUtilBase<T>::testHarness()
{
    std::string s1;

    llassert( s1.c_str() == NULL );
    llassert( s1.size() == 0 );
    llassert( s1.empty() );

    std::string s2( "hello");
    llassert( !strcmp( s2.c_str(), "hello" ) );
    llassert( s2.size() == 5 );
    llassert( !s2.empty() );
    std::string s3( s2 );

    llassert( "hello" == s2 );
    llassert( s2 == "hello" );
    llassert( s2 > "gello" );
    llassert( "gello" < s2 );
    llassert( "gello" != s2 );
    llassert( s2 != "gello" );

    std::string s4 = s2;
    llassert( !s4.empty() );
    s4.clear();
    llassert( s4.empty() );

    std::string s5("");
    llassert( s5.empty() );

    llassert( isValidIndex(s5, 0) );
    llassert( !isValidIndex(s5, 1) );

    s3 = s2;
    s4 = "hello again";

    s4 += "!";
    s4 += s4;
    llassert( s4 == "hello again!hello again!" );


    std::string s6 = s2 + " " + s2;
    std::string s7 = s6;
    llassert( s6 == s7 );
    llassert( !( s6 != s7) );
    llassert( !(s6 < s7) );
    llassert( !(s6 > s7) );

    llassert( !(s6 == "hi"));
    llassert( s6 == "hello hello");
    llassert( s6 < "hi");

    llassert( s6[1] == 'e' );
    s6[1] = 'f';
    llassert( s6[1] == 'f' );

    s2.erase( 4, 1 );
    llassert( s2 == "hell");
    s2.insert( 0, "y" );
    llassert( s2 == "yhell");
    s2.erase( 1, 3 );
    llassert( s2 == "yl");
    s2.insert( 1, "awn, don't yel");
    llassert( s2 == "yawn, don't yell");

    std::string s8 = s2.substr( 6, 5 );
    llassert( s8 == "don't"  );

    std::string s9 = "   \t\ntest  \t\t\n  ";
    trim(s9);
    llassert( s9 == "test"  );

    s8 = "abc123&*(ABC";

    s9 = s8;
    toUpper(s9);
    llassert( s9 == "ABC123&*(ABC"  );

    s9 = s8;
    toLower(s9);
    llassert( s9 == "abc123&*(abc"  );


    std::string s10( 10, 'x' );
    llassert( s10 == "xxxxxxxxxx" );

    std::string s11( "monkey in the middle", 7, 2 );
    llassert( s11 == "in" );

    std::string s12;  //empty
    s12 += "foo";
    llassert( s12 == "foo" );

    std::string s13;  //empty
    s13 += 'f';
    llassert( s13 == "f" );
}


#endif  // _DEBUG
