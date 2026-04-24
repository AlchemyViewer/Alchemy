/**
 * @file llstring_utf_test.cpp
 * @brief Pin current behaviour of llstring UTF conversion routines.
 *
 * These tests are intentionally written against today's hand-rolled
 * implementation. They will flag every observable behaviour change when the
 * conversions are re-backed by simdutf. Tests that encode *deliberate*
 * pre-RFC-3629 behaviour (overlong replacement with LL_UNKNOWN_CHAR, 5/6-byte
 * legacy encodings, lone-surrogate pass-through) are expected to change and
 * require sign-off before the migration proceeds.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2026, Linden Research, Inc.
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

#include "../llstring.h"
#include "../test/lltut.h"

#include <cstring>
#include <sstream>

namespace
{
    // LLWString / llutf16string element types have no std::ostream operator<<,
    // so ensure_equals fails to instantiate its failure formatter. Compare by
    // value and format diagnostics ourselves.
    void ensure_wstring_equals(const std::string& msg,
                               const LLWString& actual,
                               const LLWString& expected)
    {
        if (actual == expected) return;

        std::ostringstream oss;
        oss << msg << ": LLWString mismatch (actual.size=" << actual.size()
            << " expected.size=" << expected.size() << ")\n  actual: ";
        oss << std::hex << std::uppercase;
        for (llwchar c : actual) oss << "U+" << (U32)c << ' ';
        oss << "\n  expect: ";
        for (llwchar c : expected) oss << "U+" << (U32)c << ' ';
        throw tut::failure(oss.str());
    }

    void ensure_u16string_equals(const std::string& msg,
                                 const llutf16string& actual,
                                 const llutf16string& expected)
    {
        if (actual == expected) return;

        std::ostringstream oss;
        oss << msg << ": llutf16string mismatch (actual.size=" << actual.size()
            << " expected.size=" << expected.size() << ")\n  actual: ";
        oss << std::hex << std::uppercase;
        for (char16_t c : actual) oss << (U32)c << ' ';
        oss << "\n  expect: ";
        for (char16_t c : expected) oss << (U32)c << ' ';
        throw tut::failure(oss.str());
    }
}

namespace tut
{
    struct llstring_utf_data {};
    // Tests are numbered by category (1x conversion, 2x validation, 3x case,
    // 6x emoji, 8x byte-helpers, 9x shaping). The TUT default registers only
    // test<1>..test<50>, silently dropping everything above. Keep this in sync
    // with the highest test number used below.
    typedef test_group<llstring_utf_data, 100> llstring_utf_t;
    typedef llstring_utf_t::object llstring_utf_object_t;
    tut::llstring_utf_t tut_llstring_utf("LLStringUTF");

    // ---------------------------------------------------------------
    //                      round-trip coverage
    // ---------------------------------------------------------------

    // Pure ASCII: all six conversion directions must round-trip byte-for-byte.
    template<> template<>
    void llstring_utf_object_t::test<1>()
    {
        const std::string ascii = "Hello, World!";
        const LLWString     wascii (ascii.begin(), ascii.end());
        const llutf16string u16ascii(ascii.begin(), ascii.end());

        ensure_wstring_equals  ("utf8->wstring ASCII",  utf8str_to_wstring (ascii),   wascii);
        ensure_equals          ("wstring->utf8 ASCII",  wstring_to_utf8str (wascii),  ascii);
        ensure_equals          ("utf16->utf8 ASCII",    utf16str_to_utf8str(u16ascii), ascii);
        ensure_u16string_equals("wstring->utf16 ASCII", wstring_to_utf16str(wascii),  u16ascii);
        ensure_wstring_equals  ("utf16->wstring ASCII", utf16str_to_wstring(u16ascii), wascii);
    }

    // Latin-1 BMP: 2-byte UTF-8 (é = U+00E9 → C3 A9).
    template<> template<>
    void llstring_utf_object_t::test<2>()
    {
        const std::string   utf8 = "H\xC3\xA9llo";
        const LLWString     w    = { (llwchar)'H', (llwchar)0x00E9, (llwchar)'l', (llwchar)'l', (llwchar)'o' };
        const llutf16string u16  = { (char16_t)'H', (char16_t)0x00E9, (char16_t)'l', (char16_t)'l', (char16_t)'o' };

        ensure_wstring_equals  ("utf8->wstring 2-byte",  utf8str_to_wstring (utf8), w);
        ensure_equals          ("wstring->utf8 2-byte",  wstring_to_utf8str (w),    utf8);
        ensure_equals          ("utf16->utf8 2-byte",    utf16str_to_utf8str(u16),  utf8);
        ensure_u16string_equals("wstring->utf16 2-byte", wstring_to_utf16str(w),    u16);
        ensure_wstring_equals  ("utf16->wstring 2-byte", utf16str_to_wstring(u16),  w);
    }

    // CJK BMP: 3-byte UTF-8 (日 = U+65E5 → E6 97 A5, 本 = U+672C → E6 9C AC).
    template<> template<>
    void llstring_utf_object_t::test<3>()
    {
        const std::string   utf8 = "\xE6\x97\xA5\xE6\x9C\xAC";
        const LLWString     w    = { (llwchar)0x65E5, (llwchar)0x672C };
        const llutf16string u16  = { (char16_t)0x65E5, (char16_t)0x672C };

        ensure_wstring_equals  ("utf8->wstring 3-byte",  utf8str_to_wstring (utf8), w);
        ensure_equals          ("wstring->utf8 3-byte",  wstring_to_utf8str (w),    utf8);
        ensure_equals          ("utf16->utf8 3-byte",    utf16str_to_utf8str(u16),  utf8);
        ensure_u16string_equals("wstring->utf16 3-byte", wstring_to_utf16str(w),    u16);
        ensure_wstring_equals  ("utf16->wstring 3-byte", utf16str_to_wstring(u16),  w);
    }

    // Astral: 4-byte UTF-8 (U+1F680 rocket → F0 9F 9A 80; UTF-16 surrogate pair D83D DE80).
    template<> template<>
    void llstring_utf_object_t::test<4>()
    {
        const std::string   utf8 = "\xF0\x9F\x9A\x80";
        const LLWString     w    = { (llwchar)0x1F680 };
        const llutf16string u16  = { (char16_t)0xD83D, (char16_t)0xDE80 };

        ensure_wstring_equals  ("utf8->wstring astral",  utf8str_to_wstring (utf8), w);
        ensure_equals          ("wstring->utf8 astral",  wstring_to_utf8str (w),    utf8);
        ensure_equals          ("utf16->utf8 astral",    utf16str_to_utf8str(u16),  utf8);
        ensure_u16string_equals("wstring->utf16 astral", wstring_to_utf16str(w),    u16);
        ensure_wstring_equals  ("utf16->wstring astral", utf16str_to_wstring(u16),  w);
    }

    // Mixed: ASCII + 3-byte BMP + 4-byte astral. Uses explicit string-literal
    // concatenation between astral bytes and the trailing 'B' so \xA5B and
    // \x80B don't eat an extra hex digit.
    template<> template<>
    void llstring_utf_object_t::test<5>()
    {
        const std::string   utf8 = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        const LLWString     w    = { (llwchar)'A', (llwchar)0x65E5, (llwchar)0x1F680, (llwchar)'B' };
        const llutf16string u16  = { (char16_t)'A', (char16_t)0x65E5, (char16_t)0xD83D, (char16_t)0xDE80, (char16_t)'B' };

        ensure_wstring_equals  ("utf8->wstring mixed",  utf8str_to_wstring (utf8), w);
        ensure_equals          ("wstring->utf8 mixed",  wstring_to_utf8str (w),    utf8);
        ensure_equals          ("utf16->utf8 mixed",    utf16str_to_utf8str(u16),  utf8);
        ensure_u16string_equals("wstring->utf16 mixed", wstring_to_utf16str(w),    u16);
        ensure_wstring_equals  ("utf16->wstring mixed", utf16str_to_wstring(u16),  w);
    }

    // Empty inputs produce empty outputs in every direction.
    template<> template<>
    void llstring_utf_object_t::test<6>()
    {
        ensure_equals("utf8->wstring empty",  utf8str_to_wstring (std::string()).size(),   size_t(0));
        ensure_equals("wstring->utf8 empty",  wstring_to_utf8str (LLWString()),             std::string());
        ensure_equals("utf16->utf8 empty",    utf16str_to_utf8str(llutf16string()),        std::string());
        ensure_equals("utf16->wstring empty", utf16str_to_wstring(llutf16string()).size(), size_t(0));
        ensure_equals("wstring->utf16 empty", wstring_to_utf16str(LLWString()).size(),     size_t(0));
    }

    // Round-trip via rawstr_to_utf8 (utf8 -> LLWString -> utf8) preserves valid input.
    template<> template<>
    void llstring_utf_object_t::test<7>()
    {
        const std::string mixed = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        ensure_equals("rawstr_to_utf8 preserves valid utf8", rawstr_to_utf8(mixed), mixed);
    }

    // utf8 <-> u8 reinterpret (not conversion) preserves bytes.
    template<> template<>
    void llstring_utf_object_t::test<8>()
    {
        const std::string   bytes = "\xF0\x9F\x9A\x80";
        const std::u8string u8    = str_to_u8str(bytes);
        ensure_equals("str_to_u8str size", u8.size(), bytes.size());
        ensure_equals("u8 round-trip",     u8str_to_str(u8), bytes);
    }

    // ---------------------------------------------------------------
    //                   single-codepoint helpers
    // ---------------------------------------------------------------

    // wchar_to_utf8chars covers 1, 2, 3, 4-byte canonical forms.
    template<> template<>
    void llstring_utf_object_t::test<10>()
    {
        char buf[8];
        std::memset(buf, 0xAA, sizeof(buf));

        ensure_equals("'A' len",   wchar_to_utf8chars(0x41,    buf), std::ptrdiff_t(1));
        ensure_equals("'A' b0",    (U32)(U8)buf[0], U32(0x41));

        ensure_equals("é len",     wchar_to_utf8chars(0x00E9,  buf), std::ptrdiff_t(2));
        ensure_equals("é b0",      (U32)(U8)buf[0], U32(0xC3));
        ensure_equals("é b1",      (U32)(U8)buf[1], U32(0xA9));

        ensure_equals("日 len",    wchar_to_utf8chars(0x65E5,  buf), std::ptrdiff_t(3));
        ensure_equals("日 b0",     (U32)(U8)buf[0], U32(0xE6));
        ensure_equals("日 b1",     (U32)(U8)buf[1], U32(0x97));
        ensure_equals("日 b2",     (U32)(U8)buf[2], U32(0xA5));

        ensure_equals("🚀 len",   wchar_to_utf8chars(0x1F680, buf), std::ptrdiff_t(4));
        ensure_equals("🚀 b0",    (U32)(U8)buf[0], U32(0xF0));
        ensure_equals("🚀 b1",    (U32)(U8)buf[1], U32(0x9F));
        ensure_equals("🚀 b2",    (U32)(U8)buf[2], U32(0x9A));
        ensure_equals("🚀 b3",    (U32)(U8)buf[3], U32(0x80));
    }

    // wchar_to_utf8chars current behaviour: emits 5 and 6-byte legacy
    // encodings for codepoints > U+10FFFF. simdutf rejects these — test
    // pins today's behaviour so the migration change is visible.
    template<> template<>
    void llstring_utf_object_t::test<11>()
    {
        char buf[8];
        std::memset(buf, 0xAA, sizeof(buf));

        ensure_equals("5-byte len",  wchar_to_utf8chars(0x00200000, buf), std::ptrdiff_t(5));
        ensure_equals("5-byte lead", (U32)((U8)buf[0] & 0xF8), U32(0xF8));

        std::memset(buf, 0xAA, sizeof(buf));
        ensure_equals("6-byte len",  wchar_to_utf8chars(0x04000000, buf), std::ptrdiff_t(6));
        ensure_equals("6-byte lead", (U32)((U8)buf[0] & 0xFC), U32(0xFC));
    }

    // ---------------------------------------------------------------
    //                      length / count helpers
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<20>()
    {
        ensure_equals("wchar_utf8_length ASCII",  wchar_utf8_length(0x00000041), S32(1));
        ensure_equals("wchar_utf8_length 2-byte", wchar_utf8_length(0x000000E9), S32(2));
        ensure_equals("wchar_utf8_length 3-byte", wchar_utf8_length(0x000065E5), S32(3));
        ensure_equals("wchar_utf8_length 4-byte", wchar_utf8_length(0x0001F680), S32(4));
        ensure_equals("wchar_utf8_length 5-byte", wchar_utf8_length(0x00200000), S32(5));
        ensure_equals("wchar_utf8_length 6-byte", wchar_utf8_length(0x04000000), S32(6));
    }

    template<> template<>
    void llstring_utf_object_t::test<21>()
    {
        const LLWString w = { (llwchar)'A', (llwchar)0x00E9, (llwchar)0x65E5, (llwchar)0x1F680 };
        // 1 + 2 + 3 + 4 = 10 UTF-8 bytes.
        ensure_equals("wstring_utf8_length", wstring_utf8_length(w), S32(10));
        ensure_equals("wstring_utf8_length empty", wstring_utf8_length(LLWString()), S32(0));
    }

    // wstring_utf16_length returns the number of UTF-16 code units for the
    // given LLWString sub-range (each llwchar >= 0x10000 contributes 2 units).
    template<> template<>
    void llstring_utf_object_t::test<22>()
    {
        const LLWString w = { (llwchar)'A', (llwchar)0x65E5, (llwchar)0x1F680, (llwchar)'B' };
        // Full range: 1 + 1 + 2 + 1 = 5 units.
        ensure_equals("wstring_utf16_length full",    wstring_utf16_length(w, 0, (S32)w.size()), S32(5));
        // Sub-range containing only the astral wchar: 2 units.
        ensure_equals("wstring_utf16_length astral",  wstring_utf16_length(w, 2, 1), S32(2));
        // Sub-range past the end clamps to 0.
        ensure_equals("wstring_utf16_length zero",    wstring_utf16_length(w, 0, 0), S32(0));
        // Offset past the end returns 0.
        ensure_equals("wstring_utf16_length oob",     wstring_utf16_length(w, 10, 5), S32(0));
    }

    // wstring_wstring_length_from_utf16_length walks a wstring, pre-decrementing
    // the utf16-unit budget when it sees an astral llwchar. Effectively:
    // entering an astral "costs" 2 units; the function stops at the first index
    // whose cumulative cost meets or exceeds the budget, and reports unaligned
    // when the stop landed in the middle of a surrogate pair (i > adjusted n).
    template<> template<>
    void llstring_utf_object_t::test<23>()
    {
        const LLWString w = { (llwchar)'A', (llwchar)0x1F680, (llwchar)'B' }; // UTF-16 units: 1,2,1.
        bool unaligned = false;

        // Budget 4 consumes the full string, aligned.
        ensure_equals("wwsl budget 4 cnt", wstring_wstring_length_from_utf16_length(w, 0, 4, &unaligned), S32(3));
        ensure("wwsl budget 4 aligned", !unaligned);

        // Budget 3 consumes 'A' + the astral pair, aligned.
        unaligned = true;
        ensure_equals("wwsl budget 3 cnt", wstring_wstring_length_from_utf16_length(w, 0, 3, &unaligned), S32(2));
        ensure("wwsl budget 3 aligned", !unaligned);

        // Budget 2 stops *before* entering the astral (which would cost 2 units),
        // reporting 1 wchar consumed and aligned.
        unaligned = true;
        ensure_equals("wwsl budget 2 cnt", wstring_wstring_length_from_utf16_length(w, 0, 2, &unaligned), S32(1));
        ensure("wwsl budget 2 aligned", !unaligned);

        // Budget 1 stops inside the astral (overshoots by 1 unit): 1 wchar consumed,
        // unaligned.
        unaligned = false;
        ensure_equals("wwsl budget 1 cnt", wstring_wstring_length_from_utf16_length(w, 0, 1, &unaligned), S32(1));
        ensure("wwsl budget 1 unaligned", unaligned);
    }

#if LL_WINDOWS
    // wide_wstring_length is Windows-only because it assumes std::wstring holds UTF-16.
    template<> template<>
    void llstring_utf_object_t::test<24>()
    {
        std::wstring w;
        w.push_back(L'A');
        w.push_back((wchar_t)0xD83D);
        w.push_back((wchar_t)0xDE80);
        w.push_back(L'B');
        // 4 UTF-16 units = 3 codepoints.
        ensure_equals("wide_wstring_length 4u -> 3cp", wide_wstring_length(w, 4), S32(3));
        ensure_equals("wide_wstring_length 0",         wide_wstring_length(w, 0), S32(0));
    }
#endif

    // ---------------------------------------------------------------
    //                        truncation helpers
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<30>()
    {
        const std::string s = "Hello";
        ensure_equals("truncate noop",   utf8str_truncate(s, 10), s);
        ensure_equals("truncate exact",  utf8str_truncate(s, 5),  s);
        ensure_equals("truncate to 3",   utf8str_truncate(s, 3),  std::string("Hel"));
        ensure_equals("truncate to 0",   utf8str_truncate(s, 0),  std::string());
    }

    // Truncation walks back only when max_len lands *inside* a multi-byte
    // sequence (the byte at the cut index is a continuation). "A日B" = 41 E6 97 A5 42.
    //   max_len=2 → cut at 0x97 (cont) → walk back to 1 → "A"
    //   max_len=3 → cut at 0xA5 (cont) → walk back to 1 → "A"
    //   max_len=4 → cut at 0x42 (ASCII 'B') → no walk-back → "A日"
    template<> template<>
    void llstring_utf_object_t::test<31>()
    {
        const std::string s = "A\xE6\x97\xA5" "B";
        ensure_equals("trunc 2 inside 日",     utf8str_truncate(s, 2), std::string("A"));
        ensure_equals("trunc 3 inside 日",     utf8str_truncate(s, 3), std::string("A"));
        ensure_equals("trunc 4 on 'B'",        utf8str_truncate(s, 4), std::string("A\xE6\x97\xA5"));
        ensure_equals("trunc 5 whole",         utf8str_truncate(s, 5), s);
    }

    template<> template<>
    void llstring_utf_object_t::test<32>()
    {
        const std::string s = "ABCDE";
        ensure_equals("substr 1,3", utf8str_substr(s, 1, 3), std::string("BCD"));
        ensure_equals("substr 3,2", utf8str_substr(s, 3, 2), std::string("DE"));
        ensure_equals("substr 0,0", utf8str_substr(s, 0, 0), std::string());
    }

    // utf8str_symbol_truncate returns the first N complete UTF-8 symbols.
    // "A日🚀B" = 1+3+4+1 = 9 bytes, 4 symbols.
    template<> template<>
    void llstring_utf_object_t::test<33>()
    {
        const std::string s = "A\xE6\x97\xA5\xF0\x9F\x9A\x80" "B";
        ensure_equals("sym 4 whole", utf8str_symbol_truncate(s, 4), s);
        ensure_equals("sym 3",       utf8str_symbol_truncate(s, 3), std::string("A\xE6\x97\xA5\xF0\x9F\x9A\x80"));
        ensure_equals("sym 2",       utf8str_symbol_truncate(s, 2), std::string("A\xE6\x97\xA5"));
        ensure_equals("sym 1",       utf8str_symbol_truncate(s, 1), std::string("A"));
        ensure_equals("sym 0",       utf8str_symbol_truncate(s, 0), std::string());
    }

    // ---------------------------------------------------------------
    //              invalid-input pinning (breaking-change flags)
    // ---------------------------------------------------------------

    // Lone continuation byte (0x80) and invalid leader (0xFF) each map to
    // one LL_UNKNOWN_CHAR.
    template<> template<>
    void llstring_utf_object_t::test<40>()
    {
        {
            LLWString w = utf8str_to_wstring(std::string("\x80"));
            ensure_equals("lone 0x80 size", w.size(), size_t(1));
            ensure_equals("lone 0x80 -> '?'", (U32)w[0], U32((U8)LL_UNKNOWN_CHAR));
        }
        {
            LLWString w = utf8str_to_wstring(std::string("\xFF"));
            ensure_equals("0xFF size", w.size(), size_t(1));
            ensure_equals("0xFF -> '?'", (U32)w[0], U32((U8)LL_UNKNOWN_CHAR));
        }
    }

    // Overlong 2-byte NUL "\xC0\x80" — simdutf flags both bytes as invalid
    // (0xC0 is never a legal 2-byte leader; 0x80 is a lone continuation), so
    // we get one '?' per rejected byte. The previous hand-rolled decoder
    // produced a single '?' per malformed sequence; this behavioural change
    // is intentional for the migration.
    template<> template<>
    void llstring_utf_object_t::test<41>()
    {
        LLWString w = utf8str_to_wstring(std::string("\xC0\x80"));
        ensure_equals("overlong NUL size", w.size(), size_t(2));
        ensure_equals("overlong NUL[0]", (U32)w[0], U32((U8)LL_UNKNOWN_CHAR));
        ensure_equals("overlong NUL[1]", (U32)w[1], U32((U8)LL_UNKNOWN_CHAR));
    }

    // Truncated 3-byte sequence "A\xE6\x97" — simdutf rejects both 0xE6 (leader
    // without a full body) and 0x97 (lone continuation), so the result is
    // "A??" (A + two replacement chars). Previous hand-rolled decoder
    // produced "A?".
    template<> template<>
    void llstring_utf_object_t::test<42>()
    {
        LLWString w = utf8str_to_wstring(std::string("A\xE6\x97"));
        ensure_equals("truncated size",   w.size(), size_t(3));
        ensure_equals("truncated[0]='A'", (U32)w[0], U32('A'));
        ensure_equals("truncated[1]='?'", (U32)w[1], U32((U8)LL_UNKNOWN_CHAR));
        ensure_equals("truncated[2]='?'", (U32)w[2], U32((U8)LL_UNKNOWN_CHAR));
    }

    // Legacy 5-byte UTF-8 encoding (U+00200000 → F8 88 80 80 80) is no longer
    // accepted — RFC-3629 compliance. Each byte is flagged individually, so
    // the input becomes 5 replacement chars instead of 1 wchar with the legacy
    // codepoint.
    template<> template<>
    void llstring_utf_object_t::test<43>()
    {
        LLWString w = utf8str_to_wstring(std::string("\xF8\x88\x80\x80\x80"));
        ensure_equals("5-byte legacy size", w.size(), size_t(5));
        for (size_t i = 0; i < w.size(); ++i)
        {
            ensure_equals("5-byte legacy[i]='?'", (U32)w[i], U32((U8)LL_UNKNOWN_CHAR));
        }
    }

    // UTF-16 lone high surrogate followed by a non-surrogate: simdutf rejects
    // the lone 0xD83D (one '?') and converts the following 0x0041 normally.
    // Previous hand-rolled decoder greedily consumed both units as one garbage
    // codepoint.
    template<> template<>
    void llstring_utf_object_t::test<44>()
    {
        const llutf16string bad = { (char16_t)0xD83D, (char16_t)0x0041 };
        LLWString w = utf16str_to_wstring(bad);
        ensure_equals("lone surrogate + 'A' size", w.size(), size_t(2));
        ensure_equals("lone surrogate -> '?'",     (U32)w[0], U32((U8)LL_UNKNOWN_CHAR));
        ensure_equals("'A' preserved",             (U32)w[1], U32('A'));
    }

    // ---------------------------------------------------------------
    //                    round-trip helpers (smoke)
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<50>()
    {
        // These compose utf8str_to_wstring + wstring_to_utf8str. Covered here
        // so composition regressions surface before we diagnose them through
        // callers.
        const std::string mixed = "H\xC3\xA9LLO \xE6\x97\xA5";
        ensure_equals("utf8str_trim passthrough",
                      utf8str_trim(mixed), mixed);
        ensure_equals("utf8str_tolower preserves BMP non-ASCII",
                      utf8str_tolower(mixed), std::string("h\xC3\xA9llo \xE6\x97\xA5"));
    }

    template<> template<>
    void llstring_utf_object_t::test<52>()
    {
        // LLStringOps::toLower/toUpper route through ICU u_tolower/u_toupper,
        // which operate on full UChar32 code points. Deseret U+10400..U+10427
        // (capital) <-> U+10428..U+1044F (small) is a cased astral block, so
        // it round-trips only when the cast path preserves the full 21-bit
        // code point.
        const llwchar capital = 0x10400; // DESERET CAPITAL LETTER LONG I
        const llwchar small   = 0x10428; // DESERET SMALL LETTER LONG I
        ensure_equals("u_tolower astral",
                      (U32)LLStringOps::toLower(capital), (U32)small);
        ensure_equals("u_toupper astral",
                      (U32)LLStringOps::toUpper(small), (U32)capital);

        LLWString ws;
        ws.push_back(capital);
        LLWStringUtil::toLower(ws);
        LLWString expected;
        expected.push_back(small);
        ensure_wstring_equals("LLWStringUtil::toLower astral", ws, expected);
    }

    template<> template<>
    void llstring_utf_object_t::test<51>()
    {
        ensure_equals("compare_insensitive eq",
                      utf8str_compare_insensitive("Hello", "hello"), S32(0));
        ensure("compare_insensitive lt",
               utf8str_compare_insensitive("abc", "abd") < 0);
        ensure("compare_insensitive gt",
               utf8str_compare_insensitive("abd", "abc") > 0);
    }

    // ---------------------------------------------------------------
    //              ICU-backed LLStringOps classifiers
    // ---------------------------------------------------------------

    // u_isspace is POSIX-compatible: it includes U+0020, U+00A0 NO-BREAK SPACE
    // and U+3000 IDEOGRAPHIC SPACE. Non-space code points (letters, ideographs)
    // must return false.
    template<> template<>
    void llstring_utf_object_t::test<53>()
    {
        ensure("isSpace ' '",      LLStringOps::isSpace((llwchar)0x0020));
        ensure("isSpace NBSP",     LLStringOps::isSpace((llwchar)0x00A0));
        ensure("isSpace idspace",  LLStringOps::isSpace((llwchar)0x3000));
        ensure("isSpace not 'A'", !LLStringOps::isSpace((llwchar)'A'));
        ensure("isSpace not 日",   !LLStringOps::isSpace((llwchar)0x65E5));
    }

    // isUpper / isLower: ASCII plus the Deseret astral cased block. Before the
    // ICU port these returned false for astrals (wint_t narrowed U+10400 to
    // 0x0400 which is neither upper nor lower in the C locale).
    template<> template<>
    void llstring_utf_object_t::test<54>()
    {
        ensure("isUpper 'A'",         LLStringOps::isUpper((llwchar)'A'));
        ensure("not isLower 'A'",    !LLStringOps::isLower((llwchar)'A'));
        ensure("isLower 'a'",         LLStringOps::isLower((llwchar)'a'));
        ensure("not isUpper 'a'",    !LLStringOps::isUpper((llwchar)'a'));
        ensure("isUpper Deseret cap", LLStringOps::isUpper((llwchar)0x10400));
        ensure("isLower Deseret sm",  LLStringOps::isLower((llwchar)0x10428));
        ensure("not isUpper '5'",    !LLStringOps::isUpper((llwchar)'5'));
        ensure("not isLower '5'",    !LLStringOps::isLower((llwchar)'5'));
    }

    // isDigit: Unicode General Category Nd. Includes ASCII and Arabic-Indic
    // digit blocks. Letters and symbols are rejected.
    template<> template<>
    void llstring_utf_object_t::test<55>()
    {
        ensure("isDigit '5'",        LLStringOps::isDigit((llwchar)'5'));
        ensure("isDigit ARAB-IND 0", LLStringOps::isDigit((llwchar)0x0660));
        ensure("not isDigit 'a'",   !LLStringOps::isDigit((llwchar)'a'));
        ensure("not isDigit 🚀",    !LLStringOps::isDigit((llwchar)0x1F680));
    }

    // isPunct: includes Unicode General Category P*. U+3002 IDEOGRAPHIC FULL
    // STOP (Po) was missed by the old MSVC iswpunct path.
    template<> template<>
    void llstring_utf_object_t::test<56>()
    {
        ensure("isPunct '.'",         LLStringOps::isPunct((llwchar)'.'));
        ensure("isPunct CJK stop",    LLStringOps::isPunct((llwchar)0x3002));
        ensure("not isPunct 'A'",    !LLStringOps::isPunct((llwchar)'A'));
        ensure("not isPunct '5'",    !LLStringOps::isPunct((llwchar)'5'));
    }

    // isAlpha / isAlnum: CJK ideographs and Deseret astrals are alphabetic,
    // digits are alnum-but-not-alpha, punctuation is neither.
    template<> template<>
    void llstring_utf_object_t::test<57>()
    {
        ensure("isAlpha 'A'",         LLStringOps::isAlpha((llwchar)'A'));
        ensure("isAlpha 日",          LLStringOps::isAlpha((llwchar)0x65E5));
        ensure("isAlpha Deseret",     LLStringOps::isAlpha((llwchar)0x10400));
        ensure("not isAlpha '5'",    !LLStringOps::isAlpha((llwchar)'5'));
        ensure("not isAlpha '.'",    !LLStringOps::isAlpha((llwchar)'.'));

        ensure("isAlnum 'A'",         LLStringOps::isAlnum((llwchar)'A'));
        ensure("isAlnum '5'",         LLStringOps::isAlnum((llwchar)'5'));
        ensure("isAlnum Deseret",     LLStringOps::isAlnum((llwchar)0x10400));
        ensure("not isAlnum '.'",    !LLStringOps::isAlnum((llwchar)'.'));
    }

    // ---------------------------------------------------------------
    //                         emoji helpers
    // ---------------------------------------------------------------

    // LLStringOps::isEmoji only accepts code points in the "genuine" emoji
    // range U+1F000..U+1FFFF. Non-emoji symbols (©, ®) and BMP pictographs
    // must not be classified as emoji.
    template<> template<>
    void llstring_utf_object_t::test<60>()
    {
        ensure("isEmoji rocket",       LLStringOps::isEmoji((llwchar)0x1F680));
        ensure("isEmoji mahjong",      LLStringOps::isEmoji((llwchar)0x1F000));
        ensure("not isEmoji 'A'",     !LLStringOps::isEmoji((llwchar)'A'));
        ensure("not isEmoji ©",       !LLStringOps::isEmoji((llwchar)0x00A9));
        ensure("not isEmoji CJK-Ext", !LLStringOps::isEmoji((llwchar)0x20000));
    }

    template<> template<>
    void llstring_utf_object_t::test<61>()
    {
        const LLWString with_emoji = { (llwchar)'H', (llwchar)'i', (llwchar)0x1F680 };
        const LLWString no_emoji   = { (llwchar)'H', (llwchar)'i', (llwchar)0x65E5 };
        ensure("wstring_has_emoji yes", wstring_has_emoji(with_emoji));
        ensure("wstring_has_emoji no", !wstring_has_emoji(no_emoji));
        ensure("wstring_has_emoji empty", !wstring_has_emoji(LLWString()));
    }

    // wstring_remove_emojis must strip consecutive emojis without skipping
    // the following code point (the implementation uses i-- after erase to
    // re-check the new index).
    template<> template<>
    void llstring_utf_object_t::test<62>()
    {
        LLWString ws = { (llwchar)'H', (llwchar)0x1F680, (llwchar)0x1F681, (llwchar)'i' };
        const LLWString expected = { (llwchar)'H', (llwchar)'i' };
        ensure("remove_emojis returned true", wstring_remove_emojis(ws));
        ensure_wstring_equals("consecutive emojis stripped", ws, expected);

        LLWString none = { (llwchar)'H', (llwchar)'i' };
        ensure("remove_emojis returned false", !wstring_remove_emojis(none));
        ensure_wstring_equals("unchanged when no emojis", none, expected);
    }

    template<> template<>
    void llstring_utf_object_t::test<63>()
    {
        std::string s = "Hi\xF0\x9F\x9A\x80" "!";
        ensure("utf8str_remove_emojis found", utf8str_remove_emojis(s));
        ensure_equals("utf8 stripped", s, std::string("Hi!"));

        std::string clean = "Hello";
        ensure("utf8str_remove_emojis clean", !utf8str_remove_emojis(clean));
        ensure_equals("clean unchanged", clean, std::string("Hello"));
    }

    // ZWJ family (U+1F468 U+200D U+1F469 U+200D U+1F467) must strip as a
    // single unit — the old implementation left the two ZWJ code points
    // behind, producing visible tofu.
    template<> template<>
    void llstring_utf_object_t::test<64>()
    {
        LLWString ws = { (llwchar)'H',
                         (llwchar)0x1F468, (llwchar)0x200D,
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467,
                         (llwchar)'i' };
        const LLWString expected = { (llwchar)'H', (llwchar)'i' };
        ensure("ZWJ family found", wstring_remove_emojis(ws));
        ensure_wstring_equals("ZWJ family stripped", ws, expected);
    }

    // Skin-tone modifier (U+1F3FB..FF) must strip together with its base emoji.
    template<> template<>
    void llstring_utf_object_t::test<65>()
    {
        LLWString ws = { (llwchar)'M',
                         (llwchar)0x1F468, (llwchar)0x1F3FB,
                         (llwchar)'X' };
        const LLWString expected = { (llwchar)'M', (llwchar)'X' };
        ensure("skin tone found", wstring_remove_emojis(ws));
        ensure_wstring_equals("skin tone stripped", ws, expected);
    }

    // Regional indicator flag (🇺🇸 = U+1F1FA U+1F1F8). Both code points are in
    // the astral emoji range so both strip — the output must not leave a
    // dangling half-flag indicator.
    template<> template<>
    void llstring_utf_object_t::test<66>()
    {
        LLWString ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)'!' };
        const LLWString expected = { (llwchar)'!' };
        ensure("flag found", wstring_remove_emojis(ws));
        ensure_wstring_equals("flag stripped", ws, expected);
    }

    // Trailing VS16 (U+FE0F) after an astral emoji must be consumed together
    // with the base glyph.
    template<> template<>
    void llstring_utf_object_t::test<67>()
    {
        LLWString ws = { (llwchar)0x1F680, (llwchar)0xFE0F, (llwchar)'!' };
        const LLWString expected = { (llwchar)'!' };
        ensure("VS16 found", wstring_remove_emojis(ws));
        ensure_wstring_equals("VS16 stripped with base", ws, expected);
    }

    // Bare ZWJ or VS16 (no adjacent emoji) are not emoji themselves and must
    // be left intact — critical because ZWJ is used in Arabic/Indic shaping
    // outside any emoji context.
    template<> template<>
    void llstring_utf_object_t::test<68>()
    {
        LLWString ws = { (llwchar)'a', (llwchar)0x200D, (llwchar)'b' };
        const LLWString expected = ws;
        ensure("bare ZWJ not found", !wstring_remove_emojis(ws));
        ensure_wstring_equals("bare ZWJ preserved", ws, expected);
    }

    // Documented limits of the current strip: BMP emoji like ❤ (U+2764) fall
    // outside LLStringOps::isEmoji's "genuine emoji" range (0x1F000..0x20000),
    // so the heart + VS16 pair survives unchanged. Widening isEmoji would
    // affect UI callers (font fallback, !mAllowEmoji filter) and is out of
    // scope for the ZWJ-sequence fix.
    template<> template<>
    void llstring_utf_object_t::test<69>()
    {
        LLWString ws = { (llwchar)0x2764, (llwchar)0xFE0F };
        const LLWString expected = ws;
        ensure("BMP heart not considered emoji", !wstring_remove_emojis(ws));
        ensure_wstring_equals("BMP heart preserved", ws, expected);
    }

    // ---------------------------------------------------------------
    //                     utf8str helper smoke pins
    // ---------------------------------------------------------------

    // utf8str_substChar replaces every occurrence of a code point (incl.
    // astrals) with another code point, routing through LLWString so the
    // byte-length change is handled for us.
    template<> template<>
    void llstring_utf_object_t::test<70>()
    {
        const std::string in  = "A\xF0\x9F\x9A\x80" "B\xF0\x9F\x9A\x80" "C";
        const std::string out = utf8str_substChar(in, (llwchar)0x1F680, (llwchar)'X');
        ensure_equals("substChar astral -> ASCII", out, std::string("AXBXC"));

        const std::string in2  = "A\xE6\x97\xA5" "B";
        const std::string out2 = utf8str_substChar(in2, (llwchar)0x65E5, (llwchar)'.');
        ensure_equals("substChar BMP -> ASCII", out2, std::string("A.B"));
    }

    // utf8str_makeASCII replaces every non-ASCII llwchar (>0x7F) with '?'
    // before serializing back to utf-8.
    template<> template<>
    void llstring_utf_object_t::test<71>()
    {
        const std::string mixed = "A\xE6\x97\xA5" "B\xF0\x9F\x9A\x80" "C";
        const std::string expected = "A?B?C";
        ensure_equals("utf8str_makeASCII", utf8str_makeASCII(mixed), expected);
        ensure_equals("utf8str_makeASCII ascii passthrough",
                      utf8str_makeASCII("Hello"), std::string("Hello"));
    }

    // mbcsstring_makeASCII is byte-level: every byte >0x7F becomes '?'. So a
    // 3-byte UTF-8 character yields three '?' rather than one.
    template<> template<>
    void llstring_utf_object_t::test<72>()
    {
        const std::string mixed = "A\xE6\x97\xA5" "B";
        const std::string expected = "A???B";
        ensure_equals("mbcsstring_makeASCII", mbcsstring_makeASCII(mixed), expected);
    }

    // utf8str_removeCRLF strips CR (0x0D) only — LF is preserved.
    template<> template<>
    void llstring_utf_object_t::test<73>()
    {
        ensure_equals("removeCRLF crlf", utf8str_removeCRLF("a\r\nb"), std::string("a\nb"));
        ensure_equals("removeCRLF cr",   utf8str_removeCRLF("a\rb"),   std::string("ab"));
        ensure_equals("removeCRLF lf",   utf8str_removeCRLF("a\nb"),   std::string("a\nb"));
        ensure_equals("removeCRLF empty", utf8str_removeCRLF(""),      std::string());
    }

    // utf8str_split splits at the last split-token at or before maxlen bytes,
    // falling back to a hard byte cut when no token is in range. Non-final
    // chunks retain the trailing split-token (current implementation).
    template<> template<>
    void llstring_utf_object_t::test<74>()
    {
        {
            std::list<std::string> out;
            utf8str_split(out, "Hello there world", 10, ' ');
            ensure_equals("split 3 parts", out.size(), size_t(3));
            auto it = out.begin();
            ensure_equals("split[0]", *it++, std::string("Hello"));
            ensure_equals("split[1]", *it++, std::string("there "));
            ensure_equals("split[2]", *it,   std::string("world"));
        }
        {
            std::list<std::string> out;
            utf8str_split(out, "short", 10, ' ');
            ensure_equals("split single", out.size(), size_t(1));
            ensure_equals("split single val", out.front(), std::string("short"));
        }
    }

    // wchar_utf8_preview formats the code point in hex, plus (when multi-byte)
    // the decoded UTF-8 byte sequence also in hex.
    template<> template<>
    void llstring_utf_object_t::test<75>()
    {
        ensure_equals("preview ASCII", wchar_utf8_preview((llwchar)'A'), std::string("41"));
        // é = U+00E9 -> 0xC3 0xA9
        ensure_equals("preview 2-byte", wchar_utf8_preview((llwchar)0x00E9),
                      std::string("E9 [C3, A9]"));
        // Rocket = U+1F680 -> 0xF0 0x9F 0x9A 0x80
        ensure_equals("preview astral", wchar_utf8_preview((llwchar)0x1F680),
                      std::string("1F680 [F0, 9F, 9A, 80]"));
    }

    // ---------------------------------------------------------------
    //                     byte-level helpers
    // ---------------------------------------------------------------

    // iswindividual recognises the CJK Unified / Hangul / CJK Compat ranges
    // used for per-character word breaks.
    template<> template<>
    void llstring_utf_object_t::test<80>()
    {
        ensure("iswindividual CJK 日",       iswindividual((llwchar)0x65E5));
        ensure("iswindividual Hangul",       iswindividual((llwchar)0xAC00));
        ensure("iswindividual CJK Compat",   iswindividual((llwchar)0xF900));
        ensure("not iswindividual 'A'",     !iswindividual((llwchar)'A'));
        ensure("not iswindividual emoji",   !iswindividual((llwchar)0x1F680));
    }

    template<> template<>
    void llstring_utf_object_t::test<81>()
    {
        for (char c : std::string("0123456789abcdefABCDEF"))
        {
            ensure("is_char_hex yes", is_char_hex(c));
        }
        ensure("is_char_hex 'g'", !is_char_hex('g'));
        ensure("is_char_hex 'Z'", !is_char_hex('Z'));
        ensure("is_char_hex ' '", !is_char_hex(' '));
    }

    template<> template<>
    void llstring_utf_object_t::test<82>()
    {
        ensure_equals("nybble '0'", (U32)hex_as_nybble('0'), U32(0));
        ensure_equals("nybble '9'", (U32)hex_as_nybble('9'), U32(9));
        ensure_equals("nybble 'a'", (U32)hex_as_nybble('a'), U32(10));
        ensure_equals("nybble 'f'", (U32)hex_as_nybble('f'), U32(15));
        ensure_equals("nybble 'A'", (U32)hex_as_nybble('A'), U32(10));
        ensure_equals("nybble 'F'", (U32)hex_as_nybble('F'), U32(15));
        // Non-hex inputs fall through to 0 (undefined-but-documented behavior).
        ensure_equals("nybble 'g' -> 0", (U32)hex_as_nybble('g'), U32(0));
    }

    // ---------------------------------------------------------------
    //                 shaping-run detector
    // ---------------------------------------------------------------

    // Inputs that never need shaping — empty, ASCII, CJK, a single isolated
    // emoji, and two adjacent but unrelated emoji all render correctly via
    // the 1:1 codepoint path, so the detector must return no runs.
    template<> template<>
    void llstring_utf_object_t::test<90>()
    {
        ensure_equals("empty",      wstring_find_shaping_runs(LLWString()).size(), size_t(0));
        LLWString ascii = { (llwchar)'H', (llwchar)'i', (llwchar)'!' };
        ensure_equals("ascii",      wstring_find_shaping_runs(ascii).size(),       size_t(0));
        LLWString cjk   = { (llwchar)0x65E5, (llwchar)0x672C };
        ensure_equals("cjk",        wstring_find_shaping_runs(cjk).size(),         size_t(0));
        LLWString lone  = { (llwchar)0x1F680 };
        ensure_equals("lone emoji", wstring_find_shaping_runs(lone).size(),        size_t(0));
        LLWString pair  = { (llwchar)0x1F680, (llwchar)0x1F681 };
        ensure_equals("two adj em", wstring_find_shaping_runs(pair).size(),        size_t(0));
    }

    // ZWJ family 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467 — must emerge
    // as a single 5-codepoint run.
    template<> template<>
    void llstring_utf_object_t::test<91>()
    {
        LLWString ws = { (llwchar)0x1F468, (llwchar)0x200D,
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467 };
        auto runs = wstring_find_shaping_runs(ws);
        ensure_equals("one run",   runs.size(),    size_t(1));
        ensure_equals("run begin", runs[0].first,  size_t(0));
        ensure_equals("run end",   runs[0].second, size_t(5));
    }

    // Skin-tone modifier (U+1F3FB..FF) combines with the preceding emoji.
    template<> template<>
    void llstring_utf_object_t::test<92>()
    {
        LLWString ws = { (llwchar)0x1F468, (llwchar)0x1F3FB };
        auto runs = wstring_find_shaping_runs(ws);
        ensure_equals("skintone runs", runs.size(),    size_t(1));
        ensure_equals("skintone end",  runs[0].second, size_t(2));
    }

    // Trailing VS16 (U+FE0F) forces emoji presentation and must be part of
    // the same shaped run as its base.
    template<> template<>
    void llstring_utf_object_t::test<93>()
    {
        LLWString ws = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto runs = wstring_find_shaping_runs(ws);
        ensure_equals("vs16 runs", runs.size(),    size_t(1));
        ensure_equals("vs16 end",  runs[0].second, size_t(2));
    }

    // Regional indicator pair — 🇺🇸 = U+1F1FA U+1F1F8 — becomes a flag glyph
    // only under shaping. Two consecutive RIs form exactly one pair.
    template<> template<>
    void llstring_utf_object_t::test<94>()
    {
        LLWString ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        auto runs = wstring_find_shaping_runs(ws);
        ensure_equals("flag runs", runs.size(),    size_t(1));
        ensure_equals("flag end",  runs[0].second, size_t(2));
        // Four RIs in a row form two separate flags.
        LLWString four = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                           (llwchar)0x1F1EB, (llwchar)0x1F1F7 };
        auto more = wstring_find_shaping_runs(four);
        ensure_equals("two flags", more.size(),    size_t(2));
        ensure_equals("flag0 end", more[0].second, size_t(2));
        ensure_equals("flag1 begin", more[1].first, size_t(2));
        ensure_equals("flag1 end",   more[1].second, size_t(4));
    }

    // Keycap: digit/#/* + U+FE0F + U+20E3 is a shaped 3-codepoint sequence.
    template<> template<>
    void llstring_utf_object_t::test<95>()
    {
        LLWString digit_kc = { (llwchar)'9', (llwchar)0xFE0F, (llwchar)0x20E3 };
        auto runs = wstring_find_shaping_runs(digit_kc);
        ensure_equals("keycap runs", runs.size(),    size_t(1));
        ensure_equals("keycap end",  runs[0].second, size_t(3));
        LLWString hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure_equals("hash keycap", wstring_find_shaping_runs(hash_kc).size(), size_t(1));
        LLWString bare = { (llwchar)'5' };
        ensure_equals("bare digit",  wstring_find_shaping_runs(bare).size(),    size_t(0));
    }

    // Subdivision flag — base 🏴 (U+1F3F4) + tag characters (U+E0020..U+E007F)
    // terminated by U+E007F. All tag bytes must be absorbed into the run.
    template<> template<>
    void llstring_utf_object_t::test<96>()
    {
        LLWString ws = { (llwchar)0x1F3F4,
                         (llwchar)0xE0067, (llwchar)0xE0062, (llwchar)0xE0073,
                         (llwchar)0xE0063, (llwchar)0xE0074,
                         (llwchar)0xE007F };
        auto runs = wstring_find_shaping_runs(ws);
        ensure_equals("tag runs",  runs.size(),    size_t(1));
        ensure_equals("tag begin", runs[0].first,  size_t(0));
        ensure_equals("tag end",   runs[0].second, size_t(7));
    }

    // Mixed: ASCII + flag + ASCII + ZWJ family + ASCII. Two disjoint runs
    // with correct bounds; surrounding ASCII is untouched.
    template<> template<>
    void llstring_utf_object_t::test<97>()
    {
        LLWString ws = { (llwchar)'H',
                         (llwchar)0x1F1FA, (llwchar)0x1F1F8,       // flag @ [1,3)
                         (llwchar)' ',
                         (llwchar)0x1F468, (llwchar)0x200D,        // family @ [4,9)
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467,
                         (llwchar)'!' };
        auto runs = wstring_find_shaping_runs(ws);
        ensure_equals("two runs",    runs.size(),     size_t(2));
        ensure_equals("run0 begin",  runs[0].first,   size_t(1));
        ensure_equals("run0 end",    runs[0].second,  size_t(3));
        ensure_equals("run1 begin",  runs[1].first,   size_t(4));
        ensure_equals("run1 end",    runs[1].second,  size_t(9));
    }

    // Bare ZWJ/VS16 surrounded by non-emoji (as used in Arabic/Indic shaping
    // outside any emoji context) must NOT produce a run. There is no emoji
    // face to shape with and the caller should keep the 1:1 path.
    template<> template<>
    void llstring_utf_object_t::test<98>()
    {
        LLWString zwj  = { (llwchar)'a', (llwchar)0x200D, (llwchar)'b' };
        ensure_equals("bare zwj",  wstring_find_shaping_runs(zwj).size(),  size_t(0));
        LLWString vs16 = { (llwchar)'a', (llwchar)0xFE0F, (llwchar)'b' };
        ensure_equals("bare vs16", wstring_find_shaping_runs(vs16).size(), size_t(0));
    }
}
