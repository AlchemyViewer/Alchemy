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

#include <charconv>
#include <cmath>
#include <fast_float/fast_float.h>
#include <simdutf.h>

#include <array>

#include <unicode/uchar.h>
#include <unicode/ubrk.h>
#include <unicode/ucasemap.h>
#include <unicode/ucol.h>
#include <unicode/ustring.h>
#include <unicode/utext.h>
#include <unicode/utf8.h>


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


std::string rawstr_to_utf8(std::string_view raw)
{
    // Converting up and back is what this used to do, and what it was for:
    // the conversion in replaces anything malformed. utf8str_sanitize is that
    // same repair with simdutf asked first, so text that was already valid --
    // which is nearly all of it -- skips both conversions.
    return utf8str_sanitize(raw);
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

S32 utf8str_utf16_length(std::string_view utf8str, const S32 byte_offset, const S32 byte_len)
{
    const S32 size  = (S32)utf8str.size();
    const S32 begin = llclamp(byte_offset, 0, size);
    // The budget is clamped before it is added, not after: callers pass S32_MAX
    // for "the rest of it", and begin + S32_MAX overflows to a negative.
    const S32 end   = llmin(begin + llmin(S32_MAX - begin, llmax(byte_len, 0)), size);

    // One UTF-16 code unit per character, two for anything above the BMP --
    // which in UTF-8 is exactly the four-byte forms.
    S32 units = 0;
    for (S32 i = begin; i < end; )
    {
        const LLCodepointAt at = utf8str_decode_at(utf8str, (size_t)i);
        units += (at.cp >= 0x10000) ? 2 : 1;
        i = (S32)at.next;
    }
    return units;
}

S32 utf8str_length_from_utf16_length(std::string_view utf8str, const S32 byte_offset,
                                     const S32 utf16_length, bool *unaligned)
{
    const S32 size  = (S32)utf8str.size();
    const S32 begin = llclamp(byte_offset, 0, size);

    bool u = false;
    S32  units = 0;
    S32  i = begin;
    while (i < size && units < utf16_length)
    {
        const LLCodepointAt at = utf8str_decode_at(utf8str, (size_t)i);
        const S32 cost = (at.cp >= 0x10000) ? 2 : 1;
        if (units + cost > utf16_length)
        {
            // The budget ends between the two halves of a surrogate pair.
            u = true;
            break;
        }
        units += cost;
        i = (S32)at.next;
    }
    if (unaligned)
    {
        *unaligned = u;
    }
    return i - begin;
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

namespace
{

// The one place UTF-32 survives: repairing UTF-8 means decoding it, and simdutf
// is the arbiter of what is malformed. Going out through UTF-32 and back keeps
// that judgement in simdutf's hands rather than a second decoder's, which would
// have to agree with it exactly and has no way to prove that it does.
std::u32string utf8_to_u32_lossy(std::string_view utf8str)
{
    return utf_convert_with_replacement<char, char32_t>(
        utf8str.data(), utf8str.size(),
        &simdutf::validate_utf8_with_errors,
        &simdutf::utf32_length_from_utf8,
        &simdutf::convert_valid_utf8_to_utf32,
        static_cast<char32_t>(LL_UNKNOWN_CHAR));
}

std::string u32_to_utf8_lossy(std::u32string_view utf32str)
{
    return utf_convert_with_replacement<char32_t, char>(
        utf32str.data(), utf32str.size(),
        &simdutf::validate_utf32_with_errors,
        &simdutf::utf8_length_from_utf32,
        &simdutf::convert_valid_utf32_to_utf8,
        static_cast<char>(LL_UNKNOWN_CHAR));
}

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

namespace
{

// Where the leading whitespace stops. The decode is the part that cannot be
// skipped: LLStringOps::isSpace on a char answers for ASCII only, so trimming
// bytes leaves a no-break space or an ideographic space behind. ASCII settles
// without decoding, which is the whole of the common case.
size_t utf8str_trim_head_offset(std::string_view utf8str)
{
    size_t i = 0;
    while (i < utf8str.size())
    {
        const unsigned char byte = static_cast<unsigned char>(utf8str[i]);
        if (byte < 0x80)
        {
            if (!LLStringOps::isSpace(static_cast<char>(byte)))
            {
                break;
            }
            ++i;
            continue;
        }

        const LLCodepointAt at = utf8str_decode_at(utf8str, i);
        if (!LLStringOps::isSpace(at.cp))
        {
            break;
        }
        i = at.next;
    }
    return i;
}

// Where the trailing whitespace starts. Walks backward over continuation
// bytes to find the character start, bounded by the longest encoding, and
// stops on anything that does not decode back to exactly where it began --
// malformed bytes are content, and leaving them is the safe direction.
size_t utf8str_trim_tail_offset(std::string_view utf8str)
{
    size_t i = utf8str.size();
    while (i > 0)
    {
        size_t start = i - 1;
        while (start > 0
               && (i - start) < 4
               && (static_cast<unsigned char>(utf8str[start]) & 0xC0) == 0x80)
        {
            --start;
        }

        const unsigned char byte = static_cast<unsigned char>(utf8str[start]);
        if (byte < 0x80)
        {
            if (start + 1 != i || !LLStringOps::isSpace(static_cast<char>(byte)))
            {
                break;
            }
        }
        else
        {
            const LLCodepointAt at = utf8str_decode_at(utf8str, start);
            if (at.next != i || !LLStringOps::isSpace(at.cp))
            {
                break;
            }
        }
        i = start;
    }
    return i;
}

}

template<> void LLStringUtilBase<char>::trimHead(std::string& string)
{
    string.erase(0, utf8str_trim_head_offset(string));
}

template<> void LLStringUtilBase<char>::trimTail(std::string& string)
{
    string.erase(utf8str_trim_tail_offset(string));
}

template<> void LLStringUtilBase<char>::trimHead(std::string_view& string)
{
    string = string.substr(utf8str_trim_head_offset(string));
}

template<> void LLStringUtilBase<char>::trimTail(std::string_view& string)
{
    string = string.substr(0, utf8str_trim_tail_offset(string));
}

std::string utf8str_trim(std::string_view utf8str)
{
    utf8str = utf8str.substr(utf8str_trim_head_offset(utf8str));
    return std::string(utf8str.substr(0, utf8str_trim_tail_offset(utf8str)));
}


std::string utf8str_tolower(std::string_view utf8str)
{
    std::string out_str(utf8str);
    LLStringUtilBase<char>::toLower(out_str);
    return out_str;
}


S32 utf8str_compare_insensitive(const std::string& lhs, const std::string& rhs)
{
    return LLStringUtilBase<char>::compareInsensitive(lhs, rhs);
}

std::string utf8str_truncate(std::string_view utf8str, const S32 max_len)
{
    // A negative bound would sail past the zero test and reach trim_partial_utf8
    // as a size_t near its maximum, which reads until it faults. Callers get
    // theirs from a settings file or a database constant, so it is worth not
    // trusting the sign.
    if (max_len <= 0) return std::string();
    if ((S32)utf8str.length() <= max_len) return std::string(utf8str);
    return std::string(utf8str.substr(0,
        simdutf::trim_partial_utf8(utf8str.data(), (size_t)max_len)));
}

// [RLVa:KB] - Checked: RLVa-2.1.0
std::string utf8str_substr(std::string_view utf8str, const S32 index, const S32 max_len)
{
    if (max_len <= 0 || index < 0) return std::string();
    // An index past the end makes the subtraction below wrap to an enormous
    // size_t, so the fits-entirely test fails and the pointer arithmetic that
    // follows walks off the buffer -- before the substr that would have thrown.
    if ((size_t)index >= utf8str.length()) return std::string();
    if (utf8str.length() - index <= (size_t)max_len)
    {
        return std::string(utf8str.substr(index, max_len));
    }
    return std::string(utf8str.substr(index,
        simdutf::trim_partial_utf8(utf8str.data() + index, (size_t)max_len)));
}

void utf8str_split(std::list<std::string>& split_list, std::string_view utf8str, size_t maxlen, char split_token)
{
    split_list.clear();

    std::string::size_type lenMsg = utf8str.length(), lenIt = 0;

    const char* pstrIt = utf8str.data(); std::string strTemp;
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

        // A budget smaller than the character sitting at lenIt leaves nothing
        // to cut, and an empty piece advances nothing -- the loop would push
        // empty strings until it ran out of memory. Take one whole character
        // instead: it overshoots the budget by less than a character, and it
        // finishes.
        if (strTemp.empty())
        {
            const size_t next = utf8str_decode_at(utf8str, lenIt).next;
            if (next <= lenIt)
                break;
            strTemp = std::string(utf8str.substr(lenIt, next - lenIt));
        }

        split_list.push_back(strTemp);

        lenIt += strTemp.length();
        pstrIt = utf8str.data() + lenIt;
        // A view carries no terminator, so the end has to be checked before
        // the byte is read. c_str() used to make the one-past-the-end read
        // land on the NUL.
        if (lenIt < lenMsg && *pstrIt == split_token)
            lenIt++;
    }
}
// [/RLVa:KB]

std::string utf8str_symbol_truncate(std::string_view utf8str, const S32 symbol_len)
{
    if (0 == symbol_len)
    {
        return std::string();
    }
    if ((S32)utf8str.length() <= symbol_len)
    {
        return std::string(utf8str);
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
    // Counting codepoints can stop in the middle of what the reader sees as
    // one character -- between a letter and its accent, or inside a flag or a
    // family. Give back the last whole one instead.
    size_t cut = utf8str_grapheme_align_backward(utf8str, byteIndex);
    if (0 == cut && byteIndex > 0)
    {
        // The very first character spends more codepoints than the budget
        // allows, so no whole one fits inside it. Returning nothing would
        // erase the text rather than shorten it -- a name that opens with a
        // family emoji would render as blank -- so overshoot by that one
        // character and let the caller's own width clip it.
        cut = utf8str_grapheme_align_forward(utf8str, byteIndex);
    }
    return std::string(utf8str.substr(0, cut));
}

std::string utf8str_substChar(
    std::string_view utf8str,
    const llwchar target_char,
    const llwchar replace_char)
{
    // Decode and rebuild in one pass rather than converting to UTF-32 and
    // back. It cannot be done in place either: the two characters need not
    // occupy the same number of bytes.
    std::string out_str;
    out_str.reserve(utf8str.size());
    for (size_t i = 0; i < utf8str.size(); )
    {
        const LLCodepointAt at = utf8str_decode_at(utf8str, i);
        utf8str_append_cp(out_str, at.cp == target_char ? replace_char : at.cp);
        i = at.next;
    }
    return out_str;
}

std::string utf8str_makeASCII(std::string_view utf8str)
{
    // One character in, one byte out, in a single pass -- the conversion to
    // UTF-32 and back that this replaces produced exactly the same thing.
    std::string out_str;
    out_str.reserve(utf8str.size());
    for (size_t i = 0; i < utf8str.size(); )
    {
        const LLCodepointAt at = utf8str_decode_at(utf8str, i);
        out_str.push_back(at.cp > 0x7f ? LL_UNKNOWN_CHAR : (char)at.cp);
        i = at.next;
    }
    return out_str;
}

std::string mbcsstring_makeASCII(std::string_view wstr)
{
    // Replace non-ASCII chars with replace_char
    std::string out_str(wstr);
    for (S32 i = 0; i < (S32)out_str.length(); i++)
    {
        if ((U8)out_str[i] > 0x7f)
        {
            out_str[i] = LL_UNKNOWN_CHAR;
        }
    }
    return out_str;
}

std::string utf8str_removeCRLF(std::string_view utf8str)
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
static llwchar utf8str_to_wchar(std::string_view utf8str, size_t offset, size_t length)
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

std::string utf8str_showBytesUTF8(std::string_view utf8str)
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

// Cut emoji symbols if exist
bool utf8str_remove_emojis(std::string& utf8str)
{
    // Removal only ever shortens, so this compacts in place the way the wide
    // form does. Converting to UTF-32 and back to drop a few characters cost
    // two allocations of the whole string whether or not anything was found.
    const auto clusters = utf8str_find_emoji_clusters(utf8str);
    bool found = false;
    size_t read = 0, write = 0;
    auto cluster_it = clusters.begin();
    while (read < utf8str.size())
    {
        if (cluster_it != clusters.end() && read == cluster_it->first)
        {
            read = cluster_it->second;
            ++cluster_it;
            found = true;
            continue;
        }

        const LLCodepointAt at = utf8str_decode_at(utf8str, read);
        if (LLStringOps::isEmoji(at.cp))
        {
            read = at.next;
            found = true;
            continue;
        }

        const size_t span = at.next - read;
        if (write != read)
        {
            std::copy_n(utf8str.begin() + read, span, utf8str.begin() + write);
        }
        write += span;
        read = at.next;
    }
    if (found)
        utf8str.resize(write);
    return found;
}

// Codepoints that can act as a ZWJ/VS emoji-sequence base. Broader than
// LLStringOps::isEmoji, which asks only how a codepoint renders on its own —
// BMP pictographs like ❤ (U+2764), © and ® are eligible sequence bases per
// UAX #51, and compositions like ❤️‍🔥 (U+2764 U+FE0F U+200D U+1F525) need them
// recognised here even though none of them renders as colour unaided.
// (Defined as LLStringOps::isPictographBase so the same predicate is
// available to llrender's shape-itemizer and font-fallback walkers.)

LLCodepointAt utf8str_decode_at(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos >= n)
        return { 0, byte_pos };

    const auto lead = (unsigned char)utf8str[byte_pos];
    if (lead < 0x80)
        return { (llwchar)lead, byte_pos + 1 };

    size_t  len = 0;
    llwchar cp  = 0;
    if ((lead & 0xE0) == 0xC0)      { len = 2; cp = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { len = 3; cp = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { len = 4; cp = lead & 0x07; }
    else
        return { 0xFFFD, byte_pos + 1 };

    if (byte_pos + len > n)
        return { 0xFFFD, byte_pos + 1 };
    for (size_t k = 1; k < len; ++k)
    {
        const auto cont = (unsigned char)utf8str[byte_pos + k];
        if ((cont & 0xC0) != 0x80)
            return { 0xFFFD, byte_pos + 1 };
        cp = (cp << 6) | (cont & 0x3F);
    }
    // A well-formed sequence is the shortest one that spells its codepoint, is
    // not half of a surrogate pair, and does not reach past the last codepoint
    // there is. Taking the others at face value lets an overlong form carry a
    // character past a filter that already looked for it -- an overlong slash
    // still reads as a slash to everything downstream -- and puts values into
    // llwchar that no encoder further on can represent.
    static constexpr llwchar sMinForLength[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < sMinForLength[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
    {
        return { 0xFFFD, byte_pos + 1 };
    }
    return { cp, byte_pos + len };
}

namespace
{

using CodepointAt = LLCodepointAt;

CodepointAt decode_at(std::string_view utf8str, size_t pos)
{
    return utf8str_decode_at(utf8str, pos);
}

// True if position i begins an emoji sequence that the 1:1 codepoint->glyph
// path cannot render correctly — i.e., the next codepoint transforms the base
// (ZWJ, VS15/16, skin-tone, keycap combiner, tag character, or regional
// indicator pair), or we're sitting on a keycap starter (digit/#/* + FE0F +
// 20E3). Isolated emoji are excluded: they render fine through FreeType alone.
template <typename VIEW>
bool is_shaping_starter(VIEW text, size_t i)
{
    const CodepointAt at = decode_at(text, i);
    const llwchar     c  = at.cp;
    // Keycap sequence: digit/#/* + VS16 + COMBINING ENCLOSING KEYCAP.
    // shapeRun itemises these into per-face sub-runs (digit on the text
    // font, combining mark on the emoji font) so we can treat keycap as
    // one cluster for cursor/grapheme purposes without losing the mark's
    // natural overlay on the base.
    if (c == '#' || c == '*' || (c >= '0' && c <= '9'))
    {
        const CodepointAt vs = decode_at(text, at.next);
        if (vs.cp == 0xFE0F && decode_at(text, vs.next).cp == 0x20E3)
            return true;
    }
    if (!LLStringOps::isPictographBase(c))
        return false;
    const llwchar next = decode_at(text, at.next).cp;
    if (next == 0x200D || LLStringOps::isEmojiClusterExtender(next))
        return true;
    // Regional indicator pair (flag).
    return c >= 0x1F1E6 && c <= 0x1F1FF
        && next >= 0x1F1E6 && next <= 0x1F1FF;
}

// Greedy forward walk from a confirmed shaping-starter position, returning the
// one-past-end position of the sequence.
template <typename VIEW>
size_t advance_shaping_run(VIEW text, size_t start)
{
    const CodepointAt first = decode_at(text, start);
    const llwchar     base  = first.cp;
    size_t            r     = first.next;

    // Keycap: always exactly 3 codepoints.
    if (base == '#' || base == '*' || (base >= '0' && base <= '9'))
    {
        const CodepointAt vs = decode_at(text, r);
        if (vs.cp == 0xFE0F)
        {
            const CodepointAt keycap = decode_at(text, vs.next);
            if (keycap.cp == 0x20E3)
                return keycap.next;
        }
    }

    // Regional indicator pair: exactly one trailing RI.
    if (base >= 0x1F1E6 && base <= 0x1F1FF)
    {
        const CodepointAt second = decode_at(text, r);
        if (second.cp >= 0x1F1E6 && second.cp <= 0x1F1FF)
            return second.next;
    }

    // General case: ZWJ joins another base, then any number of plain
    // extenders (VS, skin-tone, keycap mark, tag chars).
    for (;;)
    {
        const CodepointAt at = decode_at(text, r);
        if (at.next == r)
            break; // end of text
        if (at.cp == 0x200D)
        {
            // Accept any pictograph base after the joiner, including BMP
            // pictographs like 🔥's partner heart in ❤️‍🔥 where the base
            // sits outside the astral emoji range.
            const CodepointAt joined = decode_at(text, at.next);
            if (joined.next != at.next && LLStringOps::isPictographBase(joined.cp))
            {
                r = joined.next;
                continue;
            }
            break; // orphan ZWJ
        }
        if (LLStringOps::isEmojiClusterExtender(at.cp))
        {
            r = at.next;
            continue;
        }
        break;
    }
    return r;
}

template <typename VIEW>
EmojiClusterList find_emoji_clusters(VIEW text)
{
    EmojiClusterList runs;
    const size_t n = text.size();
    size_t i = 0;
    while (i < n)
    {
        const CodepointAt at = decode_at(text, i);
        if (is_shaping_starter(text, i))
        {
            const size_t end = advance_shaping_run(text, i);
            // is_shaping_starter accepts a base when the *next* codepoint is
            // an extender, but advance_shaping_run can still bail (e.g. a
            // ZWJ at end of string with nothing after it, or a ZWJ followed
            // by something that isn't a pictograph base). That leaves us
            // with a degenerate single-character "cluster" that's really just
            // the base alone with a dangling extender. Skip those — isolated
            // single-codepoint emoji shape correctly via the 1:1 path and
            // are intentionally absent from the cluster list.
            if (end > at.next)
                runs.emplace_back(i, end);
            i = end;
        }
        else
        {
            i = at.next;
        }
    }
    return runs;
}

}

EmojiClusterList utf8str_find_emoji_clusters(std::string_view utf8str)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    LL_PROFILE_ZONE_NUM(utf8str.size());
    return find_emoji_clusters(utf8str);
}

namespace
{

// ubrk_open parses ICU's break rules into a state machine, so an iterator is
// kept and pointed at fresh text rather than opened per query. They carry scan
// state, so each thread keeps its own set.
//
// Breaking runs in the root locale rather than the user's: where the cursor
// lands is a property of the text, and two people reading the same string
// should see it land in the same place.
const char* break_iterator_kind(UBreakIteratorType type)
{
    switch (type)
    {
    case UBRK_CHARACTER: return "character";
    case UBRK_WORD:      return "word";
    case UBRK_LINE:      return "line";
    case UBRK_SENTENCE:  return "sentence";
    default:             return "unknown";
    }
}

// Segmentation failing is not a condition that comes and goes -- ICU either has
// the data or it does not -- so it is said once and the callers get on with the
// crude walk they fall back to. Saying it at all is the point: without it the
// only symptom is a caret that moves a byte at a time, which reads as a
// rendering bug rather than as missing data. The data filter that trims icudt
// before shipping is exactly how this would start happening.
class BreakIterators
{
public:
    BreakIterators() = default;
    BreakIterators(const BreakIterators&) = delete;
    BreakIterators& operator=(const BreakIterators&) = delete;

    ~BreakIterators()
    {
        for (UBreakIterator* iter : mIters)
        {
            if (iter)
            {
                ubrk_close(iter);
            }
        }
    }

    UBreakIterator* get(UBreakIteratorType type)
    {
        UBreakIterator*& slot = mIters[(size_t)type];
        if (!slot)
        {
            UErrorCode status = U_ZERO_ERROR;
            slot = ubrk_open(type, "", nullptr, 0, &status);
            if (U_FAILURE(status))
            {
                slot = nullptr;
                LL_WARNS_ONCE("Unicode") << "ICU has no " << break_iterator_kind(type)
                    << " break iterator (" << u_errorName(status)
                    << "); cursor movement, word selection and wrapping fall back"
                       " to a crude walk" << LL_ENDL;
            }
        }
        return slot;
    }

    // Hands out the shared iterator for `type`, unless one is already checked
    // out on this thread -- then the caller gets one of its own to close. Two
    // live queries of the same kind would otherwise be holding one iterator,
    // and setting the text on the second silently re-points the first at a
    // different string, which reads as a wrong answer rather than a crash.
    // Nothing nests today; the cost of saying so here is a bool.
    UBreakIterator* acquire(UBreakIteratorType type, bool& owned)
    {
        const size_t slot = (size_t)type;
        if (slot >= mInUse.size())
        {
            owned = false;
            return nullptr;
        }
        if (mInUse[slot])
        {
            UErrorCode status = U_ZERO_ERROR;
            UBreakIterator* iter = ubrk_open(type, "", nullptr, 0, &status);
            if (U_FAILURE(status))
            {
                LL_WARNS_ONCE("Unicode") << "ICU could not open a nested "
                    << break_iterator_kind(type) << " break iterator ("
                    << u_errorName(status) << ")" << LL_ENDL;
                return nullptr;
            }
            owned = true;
            return iter;
        }
        UBreakIterator* iter = get(type);
        if (iter)
        {
            mInUse[slot] = true;
            owned = false;
        }
        return iter;
    }

    void release(UBreakIteratorType type)
    {
        const size_t slot = (size_t)type;
        if (slot < mInUse.size())
        {
            mInUse[slot] = false;
        }
    }

private:
    // UBRK_CHARACTER, UBRK_WORD, UBRK_LINE, UBRK_SENTENCE.
    std::array<UBreakIterator*, 4> mIters {};
    std::array<bool, 4>            mInUse {};
};

// The per-thread holder, reached by both Utf8Breaks and its destructor.
inline BreakIterators& break_iterators()
{
    thread_local BreakIterators iters;
    return iters;
}

// An ICU break iterator pointed at a span of UTF-8. ICU reads those bytes where
// they lie -- no conversion, no index map, and the offsets it takes and reports
// are the caller's own. The UText is held alongside because ICU keeps a pointer
// to it rather than adopting it, so the two have to live and die together.
class Utf8Breaks
{
public:
    Utf8Breaks(std::string_view utf8str, UBreakIteratorType type)
    {
        if (utf8str.empty())
            return;

        UBreakIterator* iter = break_iterators().acquire(type, mOwned);
        if (!iter)
            return;
        mHeld = iter;
        mType = type;

        UErrorCode status = U_ZERO_ERROR;
        utext_openUTF8(&mText, utf8str.data(), (int64_t)utf8str.size(), &status);
        if (U_FAILURE(status))
        {
            LL_WARNS_ONCE("Unicode") << "ICU could not read a " << utf8str.size()
                << "-byte span as UTF-8 (" << u_errorName(status)
                << "); segmentation of it falls back to a crude walk" << LL_ENDL;
            return;
        }
        mOpen = true;

        // A fresh status: the open above reports success through warnings as
        // well, and ICU does nothing at all when handed a code already set.
        status = U_ZERO_ERROR;
        ubrk_setUText(iter, &mText, &status);
        if (U_SUCCESS(status))
        {
            mIter = iter;
        }
        else
        {
            LL_WARNS_ONCE("Unicode") << "ICU could not point its "
                << break_iterator_kind(type) << " break iterator at a span ("
                << u_errorName(status) << "); segmentation of it falls back to"
                   " a crude walk" << LL_ENDL;
        }
    }

    ~Utf8Breaks()
    {
        if (mHeld)
        {
            if (mOwned)
            {
                ubrk_close(mHeld);
            }
            else
            {
                break_iterators().release(mType);
            }
        }
        if (mOpen)
        {
            utext_close(&mText);
        }
    }

    Utf8Breaks(const Utf8Breaks&) = delete;
    Utf8Breaks& operator=(const Utf8Breaks&) = delete;

    // Null when ICU could not supply an iterator, or the span was empty. The
    // caller then has no segmentation to work from and falls back to something
    // crude but bounded.
    UBreakIterator* get() const { return mIter; }
    explicit operator bool() const { return mIter != nullptr; }

private:
    UText mText = UTEXT_INITIALIZER;
    bool mOpen = false;
    // What get() hands out, and so only set once the text is attached. mHeld is
    // the same iterator from the moment it is checked out, since the release
    // owes the holder an answer whether the attach worked or not.
    UBreakIterator* mIter = nullptr;
    UBreakIterator* mHeld = nullptr;
    UBreakIteratorType mType = UBRK_CHARACTER;
    bool mOwned = false;
};

} // anonymous namespace

void utf8str_append_cp(std::string& out, llwchar cp)
{
    // A lone surrogate or an out-of-range value has no UTF-8 form.
    // Substituting rather than dropping keeps every codepoint contributing
    // bytes, which is what an offset map built alongside this depends on.
    const UChar32 c = (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                    ? (UChar32)0xFFFD : (UChar32)cp;
    char encoded[4];
    int32_t written = 0;
    U8_APPEND_UNSAFE(encoded, written, c);
    out.append(encoded, (size_t)written);
}

std::string utf8str_from_cp(llwchar cp)
{
    std::string out;
    utf8str_append_cp(out, cp);
    return out;
}

size_t utf8str_codepoint_count(std::string_view utf8str)
{
    size_t count = 0;
    for (size_t i = 0; i < utf8str.size(); ++count)
    {
        i = utf8str_decode_at(utf8str, i).next;
    }
    return count;
}

size_t utf8str_offset_from_codepoint_index(std::string_view utf8str, size_t index)
{
    size_t i = 0;
    for (size_t seen = 0; seen < index && i < utf8str.size(); ++seen)
    {
        i = utf8str_decode_at(utf8str, i).next;
    }
    return i;
}

bool utf8str_is_valid(std::string_view utf8str)
{
    return utf8str.empty() || simdutf::validate_utf8(utf8str.data(), utf8str.size());
}

std::string utf8str_sanitize(std::string_view utf8str)
{
    if (utf8str_is_valid(utf8str))
    {
        return std::string(utf8str);
    }

    return u32_to_utf8_lossy(utf8_to_u32_lossy(utf8str));
}

// Bounds [begin, end) of the line containing byte_pos, excluding its newline.
// UAX #29 breaks words either side of a newline (WB3a, WB3b), so one line is a
// self-contained scan. Scanning for the byte is safe: 0x0A cannot appear inside
// a multi-byte UTF-8 sequence, only as itself.
static std::pair<size_t, size_t> utf8str_line_bounds(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    const size_t at = llmin(byte_pos, n);

    size_t begin = 0;
    if (at > 0)
    {
        const size_t newline = utf8str.rfind('\n', at - 1);
        if (newline != std::string_view::npos)
        {
            begin = newline + 1;
        }
    }

    size_t end = utf8str.find('\n', at);
    if (end == std::string_view::npos)
    {
        end = n;
    }
    return { begin, end };
}

// A run the cursor should step over rather than stop in. Empty runs do not
// count -- they are the zero-width segment at the end of a line.
static bool utf8str_run_is_space(std::string_view utf8str, size_t begin, size_t end)
{
    if (begin >= end)
        return false;

    const uint8_t* bytes = (const uint8_t*)utf8str.data();
    int32_t at = (int32_t)begin;
    const int32_t limit = (int32_t)end;
    while (at < limit)
    {
        UChar32 cp = 0;
        U8_NEXT(bytes, at, limit, cp);
        if (cp < 0 || !LLStringOps::isSpace((llwchar)cp))
            return false;
    }
    return true;
}

// Where the character containing byte_pos begins. Only for the paths that run
// without ICU: they still owe their caller an offset it can cut a string at,
// and stepping a raw byte would hand back somewhere inside a character.
static size_t utf8str_prev_char_start(std::string_view utf8str, size_t byte_pos)
{
    size_t at = llmin(byte_pos, utf8str.size());
    if (at > 0)
    {
        --at;
    }
    while (at > 0 && ((unsigned char)utf8str[at] & 0xC0) == 0x80)
    {
        --at;
    }
    return at;
}

// The other direction: the first character start at or after byte_pos.
static size_t utf8str_next_char_start(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    size_t at = llmin(byte_pos, n);
    while (at < n && ((unsigned char)utf8str[at] & 0xC0) == 0x80)
    {
        ++at;
    }
    return at;
}

size_t utf8str_step_grapheme_forward(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos >= n)
        return n;

    // No window here, unlike the wide form this replaced: reading UTF-8 costs
    // nothing to set up, and ICU's own safe-backward rules keep a query local
    // without one being drawn for it.
    const Utf8Breaks breaks(utf8str, UBRK_CHARACTER);
    if (!breaks)
        return (size_t)utf8str_decode_at(utf8str, byte_pos).next;

    const int32_t next = ubrk_following(breaks.get(), (int32_t)byte_pos);
    return next == UBRK_DONE ? n : (size_t)next;
}

size_t utf8str_step_grapheme_backward(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos == 0)
        return 0;
    // A position past the end clamps rather than steps. The caller is holding
    // an index its own string cannot account for, and the end is the nearest
    // answer that is certainly inside it.
    if (byte_pos > n)
        return n;

    const Utf8Breaks breaks(utf8str, UBRK_CHARACTER);
    if (!breaks)
        return utf8str_prev_char_start(utf8str, byte_pos);

    const int32_t prev = ubrk_preceding(breaks.get(), (int32_t)byte_pos);
    return prev == UBRK_DONE ? 0 : (size_t)prev;
}

size_t utf8str_grapheme_align_backward(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos == 0 || n == 0)
        return 0;
    if (byte_pos >= n)
        return n;

    // An offset landing inside a codepoint has to come back to that
    // codepoint's own start first. ICU normalises such an index down and then
    // steps strictly back from there, which lands a whole character too early.
    while (byte_pos > 0 && ((unsigned char)utf8str[byte_pos] & 0xC0) == 0x80)
    {
        --byte_pos;
    }

    const Utf8Breaks breaks(utf8str, UBRK_CHARACTER);
    if (!breaks)
        return byte_pos;

    const int32_t at = (int32_t)byte_pos;
    if (ubrk_isBoundary(breaks.get(), at))
        return byte_pos;

    const int32_t prev = ubrk_preceding(breaks.get(), at);
    return prev == UBRK_DONE ? 0 : (size_t)prev;
}

size_t utf8str_grapheme_align_forward(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos >= n)
        return n;
    if (byte_pos == 0)
        return 0;

    const Utf8Breaks breaks(utf8str, UBRK_CHARACTER);
    if (!breaks)
        return utf8str_next_char_start(utf8str, byte_pos);

    const int32_t at = (int32_t)byte_pos;
    if (ubrk_isBoundary(breaks.get(), at))
        return byte_pos;

    const int32_t next = ubrk_following(breaks.get(), at);
    return next == UBRK_DONE ? n : (size_t)next;
}

size_t utf8str_step_word_forward(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos >= n)
        return n;

    const auto bounds = utf8str_line_bounds(utf8str, byte_pos);
    if (byte_pos >= bounds.second)
        return byte_pos;

    const std::string_view line = utf8str.substr(bounds.first, bounds.second - bounds.first);
    const Utf8Breaks breaks(line, UBRK_WORD);
    if (!breaks)
        return bounds.second;

    UBreakIterator* iter = breaks.get();
    int32_t prev = ubrk_first(iter);
    for (int32_t next = ubrk_next(iter); next != UBRK_DONE; prev = next, next = ubrk_next(iter))
    {
        const size_t begin = bounds.first + (size_t)prev;
        const size_t end = bounds.first + (size_t)next;
        if (begin > byte_pos && !utf8str_run_is_space(utf8str, begin, end))
            return begin;
    }
    return bounds.second;
}

// ---------------------------------------------------------------------------
// The wide entry points, as adapters over the narrow ones above. Each converts
// once, delegates, and brings the answer back through the offset map. Stage B
// deletes this half and leaves the callers holding bytes of their own.
// ---------------------------------------------------------------------------

namespace
{
    // Casing runs in the root locale rather than the user's: Turkish would
    // case an ASCII i into a dotless one, and these strings are matched,
    // sorted and compared far more often than they are read.
    const UCaseMap* case_map()
    {
        struct Holder
        {
            UCaseMap* map = nullptr;

            Holder()
            {
                UErrorCode status = U_ZERO_ERROR;
                map = ucasemap_open("", 0, &status);
                if (U_FAILURE(status))
                {
                    map = nullptr;
                }
            }
            ~Holder()
            {
                if (map)
                {
                    ucasemap_close(map);
                }
            }
            Holder(const Holder&) = delete;
            Holder& operator=(const Holder&) = delete;
        };

        thread_local Holder holder;
        return holder.map;
    }

    using icu_case_utf8_fn = int32_t (*)(const UCaseMap*, char*, int32_t,
                                         const char*, int32_t, UErrorCode*);

    // A null destination asks for the length, which arrives alongside a buffer
    // overflow that is not an error here. A buffer of exactly the reported size
    // is enough -- ICU reports a warning about the terminator it could not
    // write, and writes every byte that matters.
    void utf8str_convert_case(std::string& string, icu_case_utf8_fn convert)
    {
        if (string.empty())
            return;

        const UCaseMap* csm = case_map();
        if (!csm)
            return;

        UErrorCode status = U_ZERO_ERROR;
        const int32_t needed = convert(csm, nullptr, 0, string.data(), (int32_t)string.size(), &status);
        // Asking for the length always reports the overflow, since there is no
        // buffer to write into; that one status is the expected answer and not
        // a failure. Any other is ICU declining to measure the string at all,
        // and it reports zero when it does. Case mapping never removes
        // characters, so a zero length for input that is not empty can only
        // mean failure -- leave the text as it stands rather than erasing it.
        if (U_FAILURE(status) && U_BUFFER_OVERFLOW_ERROR != status)
        {
            return;
        }
        if (needed <= 0)
        {
            return;
        }

        std::string out((size_t)needed, '\0');
        status = U_ZERO_ERROR;
        const int32_t written = convert(csm, out.data(), needed, string.data(), (int32_t)string.size(), &status);
        if (U_FAILURE(status) || written <= 0)
            return;

        out.resize((size_t)llmin(written, needed));
        string.swap(out);
    }
}

template<>
void LLStringUtilBase<char>::toUpper(std::string& string)
{
    utf8str_convert_case(string, &ucasemap_utf8ToUpper);
}

template<>
void LLStringUtilBase<char>::toLower(std::string& string)
{
    utf8str_convert_case(string, &ucasemap_utf8ToLower);
}

size_t utf8str_bytes_from_cased_bytes(std::string_view utf8str, size_t cased_bytes, bool to_upper)
{
    const UCaseMap* csm = case_map();
    if (!csm)
        return 0;

    const icu_case_utf8_fn convert = to_upper ? &ucasemap_utf8ToUpper
                                              : &ucasemap_utf8ToLower;
    const uint8_t* bytes = (const uint8_t*)utf8str.data();
    const int32_t length = (int32_t)utf8str.size();

    size_t spent = 0;
    int32_t at = 0;

    while (at < length && spent < cased_bytes)
    {
        const int32_t begin = at;
        UChar32 cp = 0;
        U8_NEXT(bytes, at, length, cp);
        if (cp < 0)
        {
            // Ill-formed. Stop at its start rather than partway through it, so
            // the offset returned is always one a caller can slice on.
            at = begin;
            break;
        }

        // Ask for the cased length of this one codepoint without writing it.
        UErrorCode status = U_ZERO_ERROR;
        const int32_t cased = convert(csm, nullptr, 0, utf8str.data() + begin, at - begin, &status);
        if (cased < 0 || spent + (size_t)cased > cased_bytes)
        {
            // The offset lands inside this codepoint's cased form; stop before
            // it, which leaves `at` at this codepoint's own start.
            at = begin;
            break;
        }
        spent += (size_t)cased;
    }
    return (size_t)at;
}

void utf8str_line_break_opportunities(std::string_view utf8str, std::vector<size_t>& out)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    LL_PROFILE_ZONE_NUM(utf8str.size());
    out.clear();
    if (utf8str.empty())
        return;

    const Utf8Breaks breaks(utf8str, UBRK_LINE);
    if (!breaks)
    {
        out.push_back(utf8str.size());
        return;
    }

    // The first boundary is 0, which is never an opportunity. The last is the
    // string's own end, which always is.
    UBreakIterator* iter = breaks.get();
    ubrk_first(iter);
    for (int32_t b = ubrk_next(iter); b != UBRK_DONE; b = ubrk_next(iter))
    {
        out.push_back((size_t)b);
    }
}

// UAX #29 says where the segments are, not which of them a human would call a
// word -- runs of spaces and of punctuation are segments in their own right.
// ICU tags every break with what kind of run preceded it, which is exactly
// that distinction, and it knows about scripts a test for alphanumerics cannot
// speak for.
static bool utf8str_status_is_word(int32_t rule_status)
{
    return rule_status >= UBRK_WORD_NONE_LIMIT;
}

std::pair<size_t, size_t> utf8str_word_range_at(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    if (byte_pos >= n)
        return { n, n };

    const auto bounds = utf8str_line_bounds(utf8str, byte_pos);
    if (byte_pos >= bounds.second)
        return { byte_pos, byte_pos };

    const std::string_view line = utf8str.substr(bounds.first, bounds.second - bounds.first);
    const Utf8Breaks breaks(line, UBRK_WORD);
    if (!breaks)
        return { byte_pos, byte_pos };

    // The segment holding byte_pos is the one ending at the first boundary
    // past it.
    UBreakIterator* iter = breaks.get();
    const int32_t at = (int32_t)(byte_pos - bounds.first);
    const int32_t end = ubrk_isBoundary(iter, at) ? ubrk_next(iter) : ubrk_current(iter);
    if (end == UBRK_DONE)
        return { byte_pos, byte_pos };

    // The status describes the run behind the break the iterator sits on, so
    // it has to be read before stepping back across it.
    const bool is_word = utf8str_status_is_word(ubrk_getRuleStatus(iter));
    const int32_t begin = ubrk_previous(iter);
    if (!is_word || begin == UBRK_DONE)
        return { byte_pos, byte_pos };

    return { bounds.first + (size_t)begin, bounds.first + (size_t)end };
}

std::pair<size_t, size_t> utf8str_next_word_range(std::string_view utf8str, size_t byte_pos)
{
    const size_t n = utf8str.size();
    size_t at = llmin(byte_pos, n);

    while (at < n)
    {
        const auto bounds = utf8str_line_bounds(utf8str, at);

        if (at < bounds.second)
        {
            const std::string_view line = utf8str.substr(bounds.first, bounds.second - bounds.first);
            const Utf8Breaks breaks(line, UBRK_WORD);
            if (breaks)
            {
                UBreakIterator* iter = breaks.get();
                int32_t begin = ubrk_first(iter);
                for (int32_t end = ubrk_next(iter); end != UBRK_DONE;
                     begin = end, end = ubrk_next(iter))
                {
                    if (bounds.first + (size_t)end > at
                        && utf8str_status_is_word(ubrk_getRuleStatus(iter)))
                    {
                        return { bounds.first + (size_t)begin, bounds.first + (size_t)end };
                    }
                }
            }
        }
        // Nothing left on this line; resume past its newline.
        at = bounds.second + 1;
    }
    return { n, n };
}

size_t utf8str_step_word_backward(std::string_view utf8str, size_t byte_pos)
{
    if (byte_pos == 0)
        return 0;
    const size_t at = llmin(byte_pos, utf8str.size());

    const auto bounds = utf8str_line_bounds(utf8str, at);
    if (at <= bounds.first)
        return at;

    const std::string_view line = utf8str.substr(bounds.first, bounds.second - bounds.first);
    const Utf8Breaks breaks(line, UBRK_WORD);
    if (!breaks)
        return bounds.first;

    const size_t scan_end = llmin(at, bounds.second);
    size_t best = bounds.first;

    UBreakIterator* iter = breaks.get();
    int32_t prev = ubrk_first(iter);
    for (int32_t next = ubrk_next(iter); next != UBRK_DONE; prev = next, next = ubrk_next(iter))
    {
        const size_t begin = bounds.first + (size_t)prev;
        if (begin >= scan_end)
            break;
        if (!utf8str_run_is_space(utf8str, begin, bounds.first + (size_t)next))
            best = begin;
    }
    return best;
}

size_t utf8str_caret_word_forward(std::string_view utf8str, size_t byte_pos)
{
    const size_t at = llmin(byte_pos, utf8str.size());
    const size_t next = utf8str_step_word_forward(utf8str, at);
    return next != at ? next : utf8str_step_grapheme_forward(utf8str, at);
}

size_t utf8str_caret_word_backward(std::string_view utf8str, size_t byte_pos)
{
    const size_t at = llmin(byte_pos, utf8str.size());
    const size_t previous = utf8str_step_word_backward(utf8str, at);
    if (previous != at)
        return previous;

    return utf8str_step_word_backward(utf8str,
                                      utf8str_step_grapheme_backward(utf8str, at));
}

// --- the wide adapters -----------------------------------------------------

namespace
{

template <typename VIEW>
std::pair<size_t, size_t> emoji_range_at(VIEW text, size_t pos,
                                         const EmojiClusterList& clusters)
{
    if (pos >= text.size())
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
    // Over UTF-8 a position inside a character decodes to the replacement
    // character, which is no pictograph, so it reports empty as it should.
    const CodepointAt at = decode_at(text, pos);
    return LLStringOps::isPictographBase(at.cp)
        ? std::make_pair(pos, at.next)
        : std::make_pair(pos, pos);
}

}

std::pair<size_t, size_t> utf8str_emoji_range_at(std::string_view utf8str, size_t byte_pos,
                                                 const EmojiClusterList& clusters)
{
    return emoji_range_at(utf8str, byte_pos, clusters);
}

std::pair<size_t, size_t> utf8str_emoji_range_at(std::string_view utf8str, size_t byte_pos)
{
    // Scans the whole string, which the drag-select and tooltip callers pay
    // per mouse-move against a document that can be a chat history. Windowing
    // it needs care this has not had: a window that opens inside a cluster can
    // report no cluster at all rather than a cut one, so an empty result is not
    // evidence that a wider window would agree -- and deciding where it is safe
    // to cut means knowing what a cluster is made of, which is knowledge that
    // lives in the walker and should stay there. The callers that already hold
    // a list should pass it; see the overload above.
    return utf8str_emoji_range_at(utf8str, byte_pos, utf8str_find_emoji_clusters(utf8str));
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
std::map<std::string, std::string, std::less<>> LLStringOps::datetimeToCodes;

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
    // Emoji_Presentation is the property that means "renders in colour unless
    // asked otherwise", which is the question. Nothing below U+231A carries it,
    // so ordinary text never reaches the table.
    return a >= 0x231A && u_hasBinaryProperty((UChar32)a, UCHAR_EMOJI_PRESENTATION);
}

// static
bool LLStringOps::isPictographBase(llwchar a)
{
    // Extended_Pictographic is what UAX #51 builds its sequences out of, and it
    // already excludes every extender: ZWJ, the variation selectors, the keycap
    // mark, the skin tones and the tag characters are none of them pictographic.
    //
    // Regional indicators are the one thing it leaves out that belongs here. A
    // flag is a pair of them and nothing else, and Unicode does not count them
    // as pictographic.
    if (a < 0xA9)
        return false;
    if (a >= 0x1F1E6 && a <= 0x1F1FF)
        return true;
    return u_hasBinaryProperty((UChar32)a, UCHAR_EXTENDED_PICTOGRAPHIC);
}

bool LLStringOps::isEmojiClusterExtender(llwchar a)
{
    if (a < 0x20E3)
        return false;
    return a == 0xFE0E || a == 0xFE0F                             // VS-15 / VS-16
        || a == 0x20E3                                            // keycap combiner
        || (a >= 0xE0020 && a <= 0xE007F)                         // tag chars + CANCEL TAG
        || u_hasBinaryProperty((UChar32)a, UCHAR_EMOJI_MODIFIER); // skin tones
}

// The simple, one-to-one case mappings. A codepoint whose full mapping is
// longer than itself -- sharp s uppercasing to SS, U+0130 lowercasing to i
// plus a combining dot -- keeps its simple mapping here, because one llwchar
// in cannot give two out. LLStringUtilBase<llwchar>::toUpper/toLower case
// whole strings and are free to change their length, so those are the ones
// that get such a character right.
llwchar LLStringOps::toUpperAboveAscii(llwchar elem)
{
    return (llwchar)u_toupper((UChar32)elem);
}

llwchar LLStringOps::toLowerAboveAscii(llwchar elem)
{
    return (llwchar)u_tolower((UChar32)elem);
}

bool LLStringOps::isNonprintable(llwchar a)
{
    // The ASCII printables are most of most text and never qualify.
    if (a >= 0x20 && a < 0x7F)
        return false;

    switch (u_charType((UChar32)a))
    {
    case U_CONTROL_CHAR:
    case U_LINE_SEPARATOR:
    case U_PARAGRAPH_SEPARATOR:
        return true;
    default:
        return false;
    }
}

bool LLStringOps::isSpaceAboveAscii(llwchar elem)
{
    return u_hasBinaryProperty((UChar32)elem, UCHAR_WHITE_SPACE);
}

bool LLStringOps::isUpperAboveAscii(llwchar elem)
{
    return u_charType((UChar32)elem) == U_UPPERCASE_LETTER;
}

bool LLStringOps::isLowerAboveAscii(llwchar elem)
{
    return u_charType((UChar32)elem) == U_LOWERCASE_LETTER;
}

bool LLStringOps::isAlphaAboveAscii(llwchar elem)
{
    return u_isalpha((UChar32)elem) != 0;
}

bool LLStringOps::isAlnumAboveAscii(llwchar elem)
{
    return u_isalnum((UChar32)elem) != 0;
}

// Symbols count, as they do for the ASCII half: ispunct('+') is true, so
// isPunct(U+00B1 PLUS-MINUS SIGN) had better be too. That is wider than
// Unicode's own punctuation categories, which hold no symbols at all.
bool LLStringOps::isPunctAboveAscii(llwchar elem)
{
    switch (u_charType((UChar32)elem))
    {
    case U_CONNECTOR_PUNCTUATION:
    case U_DASH_PUNCTUATION:
    case U_START_PUNCTUATION:
    case U_END_PUNCTUATION:
    case U_INITIAL_PUNCTUATION:
    case U_FINAL_PUNCTUATION:
    case U_OTHER_PUNCTUATION:
    case U_MATH_SYMBOL:
    case U_CURRENCY_SYMBOL:
    case U_MODIFIER_SYMBOL:
    case U_OTHER_SYMBOL:
        return true;
    default:
        return false;
    }
}

namespace
{
    // Root-locale collation, so a list sorts the same way for everyone. What
    // this replaces did not: wcscmp on Windows is codepoint order, in which
    // every accented letter sorts past z, while wcscoll elsewhere follows
    // whatever locale the process happens to be in.
    enum class CollatorKind
    {
        Plain,                // Unicode default order
        Caseless,             // + case ignored
        Dictionary,           // + digit runs compared as numbers
        DictionaryCaseless,   // + digit runs, and case ignored
        Count
    };

    UCollator* collator(CollatorKind kind)
    {
        struct Holder
        {
            std::array<UCollator*, (size_t)CollatorKind::Count> colls {};

            Holder() = default;
            Holder(const Holder&) = delete;
            Holder& operator=(const Holder&) = delete;
            ~Holder()
            {
                for (UCollator* coll : colls)
                {
                    if (coll)
                    {
                        ucol_close(coll);
                    }
                }
            }
        };

        thread_local Holder holder;
        UCollator*& slot = holder.colls[(size_t)kind];
        if (!slot)
        {
            UErrorCode status = U_ZERO_ERROR;
            UCollator* coll = ucol_open("", &status);
            if (U_SUCCESS(status))
            {
                // Uppercase before lowercase, which is the order the viewer's
                // lists have always had; Unicode's own default is the other way
                // round. Case is a tertiary difference and accents are a
                // secondary one, so this reorders the former and leaves the
                // latter where collation puts it.
                ucol_setAttribute(coll, UCOL_CASE_FIRST, UCOL_UPPER_FIRST, &status);
            }
            const bool numeric = (kind == CollatorKind::Dictionary
                               || kind == CollatorKind::DictionaryCaseless);
            const bool caseless = (kind == CollatorKind::Caseless
                                || kind == CollatorKind::DictionaryCaseless);

            if (U_SUCCESS(status) && numeric)
            {
                // A run of digits compares as the number it spells, so item2
                // comes before item10.
                ucol_setAttribute(coll, UCOL_NUMERIC_COLLATION, UCOL_ON, &status);
            }
            if (U_SUCCESS(status) && caseless)
            {
                // Case is a tertiary difference, so dropping to secondary
                // strength folds it away. Accents are secondary and survive.
                ucol_setStrength(coll, UCOL_SECONDARY);
            }
            if (U_FAILURE(status))
            {
                if (coll)
                {
                    ucol_close(coll);
                }
                return nullptr;
            }
            slot = coll;
        }
        return slot;
    }

    // With no collator to be had, order by codepoint. It is the wrong order,
    // but it is still a total order, which is what a sort needs before it
    // needs anything else.
    // Whether a comparison exists to put things in order, as against to answer
    // whether two strings are the same. Collation deliberately overlooks
    // differences the reader is not meant to see -- the leading zeros in a
    // numeric run, a variation selector, a soft hyphen -- and an ordering that
    // calls two distinct names equal lets an unstable sort shuffle them between
    // one refresh and the next. Breaking that tie by bytes is arbitrary, but it
    // is fixed, which is all a sort needs.
    //
    // Only the case-sensitive dictionary order gets that treatment. Every other
    // kind is asked whether two strings are the same and has callers relying on
    // the answer: the caseless forms are equivalences on purpose, so a tie there
    // is the result rather than a gap in it.
    constexpr bool orders_rather_than_equates(CollatorKind kind)
    {
        return CollatorKind::Dictionary == kind;
    }

    S32 collate_utf8(std::string_view a, std::string_view b, CollatorKind kind)
    {
        UCollator* coll = collator(kind);
        if (!coll)
            return (S32)a.compare(b);

        UErrorCode status = U_ZERO_ERROR;
        const S32 result = (S32)ucol_strcollUTF8(coll, a.data(), (int32_t)a.size(),
                                                 b.data(), (int32_t)b.size(), &status);
        if (U_FAILURE(status))
            return (S32)a.compare(b);
        if (0 != result || !orders_rather_than_equates(kind))
            return result;
        return (S32)a.compare(b);
    }
}

S32 LLStringOps::collate(const char* a, const char* b)
{
    return collate_utf8(a, b, CollatorKind::Plain);
}

// Malformed UTF-8 counts as unprintable, which is what it is.
template<>
bool LLStringUtilBase<char>::containsNonprintable(std::string_view string)
{
    const uint8_t* bytes = (const uint8_t*)string.data();
    const int32_t length = (int32_t)string.size();
    int32_t at = 0;
    while (at < length)
    {
        UChar32 cp = 0;
        U8_NEXT(bytes, at, length, cp);
        if (cp < 0 || LLStringOps::isNonprintable((llwchar)cp))
            return true;
    }
    return false;
}

template<>
void LLStringUtilBase<char>::stripNonprintable(std::string& string)
{
    if (string.empty())
        return;

    const uint8_t* bytes = (const uint8_t*)string.data();
    const int32_t length = (int32_t)string.size();
    std::string kept;
    kept.reserve(string.size());

    int32_t at = 0;
    while (at < length)
    {
        const int32_t begin = at;
        UChar32 cp = 0;
        U8_NEXT(bytes, at, length, cp);
        if (cp >= 0 && !LLStringOps::isNonprintable((llwchar)cp))
        {
            kept.append(string, (size_t)begin, (size_t)(at - begin));
        }
    }
    string.swap(kept);
}

// Capitalising a byte can only ever reach ASCII, so the narrow form goes
// through codepoints. The word rule -- a capital after a space, a hyphen or an
// underscore -- is the caller's and stays as it is.
namespace
{
    // The stream extraction these replace skipped leading space and took a
    // leading '+'; from_chars does neither. trim() has dealt with the space,
    // so only the sign is left to handle by hand.
    std::string_view without_plus(std::string_view s)
    {
        return (!s.empty() && s.front() == '+') ? s.substr(1) : s;
    }
}

// A parse that consumes a prefix still succeeds, as extraction did: "12abc"
// reads 12. What changes is that an out-of-range value now says so through
// from_chars rather than through a stream failbit nobody could interpret,
// which is what the TODOs these carried were asking for -- and that a
// negative fed to the unsigned form is rejected instead of wrapping, which
// is what num_get did with it.
template<>
bool LLStringUtilBase<char>::convertToU32(std::string_view string, U32& value)
{
    trim(string);
    const std::string_view s = without_plus(string);
    if (s.empty())
        return false;

    U32 v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{})
        return false;

    value = v;
    return true;
}

template<>
bool LLStringUtilBase<char>::convertToS32(std::string_view string, S32& value)
{
    trim(string);
    const std::string_view s = without_plus(string);
    if (s.empty())
        return false;

    S32 v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{})
        return false;

    value = v;
    return true;
}

template<>
bool LLStringUtilBase<char>::convertToF64(std::string_view string, F64& value)
{
    trim(string);
    const std::string_view s = without_plus(string);
    if (s.empty())
        return false;

    // fast_float rather than std::from_chars: libc++ marks the floating-point
    // overloads unavailable below macOS 26, and llcommon already depends on it.
    F64 v = 0.0;
    const auto [ptr, ec] = fast_float::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{})
        return false;

    // from_chars spells the same infinities and NaNs strtod does, so "inf" and
    // "nan" parse where the stream this replaced set failbit on them. Settings,
    // XML attributes and typed fields all reach here, and none of them means a
    // non-finite number by writing a word.
    if (!std::isfinite(v))
        return false;

    value = v;
    return true;
}

template<>
void LLStringUtilBase<char>::capitalize(std::string& str)
{
    if (str.empty())
        return;

    // Rebuilt rather than converted through UTF-32 and back. It cannot be done
    // in place either: uppercasing one codepoint stays one codepoint, but not
    // necessarily the same number of bytes -- dotless i is two and becomes I,
    // which is one. `last` is the character that was WRITTEN, matching the
    // wide form, so a capitalised separator would start the next word too.
    std::string out_str;
    out_str.reserve(str.size());
    llwchar last = 0;
    bool at_start = true;
    for (size_t i = 0; i < str.size(); )
    {
        const LLCodepointAt at = utf8str_decode_at(str, i);
        i = at.next;

        const bool starts_word = at_start || last == ' ' || last == '-' || last == '_';
        last = starts_word ? LLStringOps::toUpper(at.cp) : at.cp;
        at_start = false;
        utf8str_append_cp(out_str, last);
    }
    str.swap(out_str);
}

template<>
S32 LLStringUtilBase<char>::compareInsensitive(const char* lhs, const char* rhs)
{
    if (lhs == rhs)
        return 0;
    if (!lhs || !lhs[0])
        return (!rhs || !rhs[0]) ? 0 : 1;
    if (!rhs || !rhs[0])
        return -1;
    return collate_utf8(lhs, rhs, CollatorKind::Caseless);
}

template<>
S32 LLStringUtilBase<char>::compareInsensitive(std::string_view lhs, std::string_view rhs)
{
    return collate_utf8(lhs, rhs, CollatorKind::Caseless);
}

template<>
S32 LLStringUtilBase<char>::compareDict(std::string_view a, std::string_view b)
{
    return collate_utf8(a, b, CollatorKind::Dictionary);
}

template<>
S32 LLStringUtilBase<char>::compareDictInsensitive(std::string_view a, std::string_view b)
{
    return collate_utf8(a, b, CollatorKind::DictionaryCaseless);
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


std::string LLStringOps::getDatetimeCode (std::string_view key)
{
    // datetimeToCodes is keyed on std::string; the transparent comparator lets
    // the token probe it without a copy being made to ask.
    auto iter = datetimeToCodes.find (key);
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
    static const std::string delims (",");

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
bool LLStringUtil::simpleReplacement(std::string &replacement, std::string_view token, const format_map_t& substitutions)
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
    // if not, see if there's one WITH brackets. Built into a buffer that is
    // kept between calls: the first probe misses for every map that stores its
    // keys bracketed, so this ran on the common path.
    static thread_local std::string bracketed;
    bracketed.assign(1, '[').append(token).append(1, ']');
    iter = substitutions.find(std::string_view(bracketed));
    if (iter != substitutions.end())
    {
        replacement = iter->second;
        return true;
    }

    return false;
}

// static
template<>
bool LLStringUtil::simpleReplacement(std::string &replacement, std::string_view token, const LLSD& substitutions)
{
    // see if we have a replacement for the bracketed string (without the brackets)
    // test first using has() because if we just look up with operator[] we get back an
    // empty string even if the value is missing. We want to distinguish between
    // missing replacements and deliberately empty replacement strings.
    // LLSD keys are std::string, so these do build one -- but only the two the
    // lookups actually need, rather than a copy of the token as well.
    const std::string key(token);
    if (substitutions.has(key))
    {
        replacement = substitutions[key].asString();
        return true;
    }
    // if not, see if there's one WITH brackets
    const std::string bracketed = "[" + key + "]";
    if (substitutions.has(bracketed))
    {
        replacement = substitutions[bracketed].asString();
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
void LLStringUtil::formatNumber(std::string& numStr, std::string_view decimals)
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
bool LLStringUtil::formatDatetime(std::string& replacement, std::string_view token,
                                  std::string_view param, S32 secFromEpoch)
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
