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
    // Tests are numbered by category:
    //   1x  conversion       (utf8 <-> wstring/utf16, basic round-trips)
    //   2x  validation       (well-formed input pins)
    //   3x  case             (ICU-backed casing)
    //   4x  malformed UTF    (replacement-char behavior on bad bytes)
    //   5x  compose+classifiers (round-trip helpers, isUpper/isDigit/etc)
    //   6x  emoji            (narrow-contract: isEmoji-driven detection/strip)
    //   7x  emoji broad      (cluster-driven strip: keycaps, subdivision flags, BMP-base ZWJ)
    //   8x  byte-helpers
    //   9x  shaping          (cluster walker: wstring_find_emoji_clusters)
    //   10x grapheme         (step + align + range_at)
    //   11x utf8str helpers  (substChar/makeASCII/removeCRLF/split/preview)
    // The TUT default registers only test<1>..test<50>, but the explicit
    // test_group<..., 128> below raises that ceiling. Keep this index in
    // sync with categories used below.
    typedef test_group<llstring_utf_data, 128> llstring_utf_t;
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
    //                         emoji helpers
    // ---------------------------------------------------------------

    // LLStringOps::isEmoji is Unicode's Emoji_Presentation: does this render
    // in colour on its own. Symbols that only become emoji when a VS-16 asks
    // them to (©, ®, ❤) are not, and neither is a codepoint that merely sits
    // in an emoji block.
    template<> template<>
    void llstring_utf_object_t::test<60>()
    {
        ensure("isEmoji rocket",       LLStringOps::isEmoji((llwchar)0x1F680));
        // Both mahjong tiles, one of which Unicode calls an emoji and one of
        // which it does not. A block-shaped range test cannot tell them apart.
        ensure("isEmoji mahjong red dragon", LLStringOps::isEmoji((llwchar)0x1F004));
        ensure("not isEmoji mahjong east wind", !LLStringOps::isEmoji((llwchar)0x1F000));
        // A BMP emoji, which the old astral-only range could never see.
        ensure("isEmoji watch",        LLStringOps::isEmoji((llwchar)0x231A));
        ensure("not isEmoji 'A'",     !LLStringOps::isEmoji((llwchar)'A'));
        ensure("not isEmoji ©",       !LLStringOps::isEmoji((llwchar)0x00A9));
        ensure("not isEmoji ❤",       !LLStringOps::isEmoji((llwchar)0x2764));
        ensure("not isEmoji CJK-Ext", !LLStringOps::isEmoji((llwchar)0x20000));
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

        // Heart-on-fire ❤️‍🔥 round-trips through utf8 encoding without
        // the wstring layer disagreeing with the byte layer about cluster
        // bounds. UTF-8 byte sequence: U+2764 (E2 9D A4) U+FE0F (EF B8 8F)
        // U+200D (E2 80 8D) U+1F525 (F0 9F 94 A5).
        std::string fire = "A" "\xE2\x9D\xA4" "\xEF\xB8\x8F" "\xE2\x80\x8D" "\xF0\x9F\x94\xA5" "B";
        ensure("utf8 heart-on-fire found", utf8str_remove_emojis(fire));
        ensure_equals("utf8 heart-on-fire stripped", fire, std::string("AB"));

        // Subdivision flag 🏴󠁧󠁢󠁥󠁮󠁧󠁿 — base + 5 tag chars + CANCEL TAG, all
        // astral. UTF-8 bytes: U+1F3F4 (F0 9F 8F B4) followed by 6 four-byte
        // tag-char sequences.
        std::string flag = "[" "\xF0\x9F\x8F\xB4"
                                "\xF3\xA0\x81\xA7" "\xF3\xA0\x81\xA2" "\xF3\xA0\x81\xB3"
                                "\xF3\xA0\x81\xA3" "\xF3\xA0\x81\xB4" "\xF3\xA0\x81\xBF"
                            "]";
        ensure("utf8 subdiv flag found", utf8str_remove_emojis(flag));
        ensure_equals("utf8 subdiv flag stripped", flag, std::string("[]"));
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

    // Broad-contract rewrite: wstring_remove_emojis now drives off the
    // cluster walker, so anything HarfBuzz would itemize as one emoji glyph
    // is removed — including BMP-base sequences like the heart + VS-16
    // pair (which the walker treats as a 2-codepoint cluster). Earlier
    // builds left this pair intact under a narrower isEmoji-only contract;
    // the change unifies the strip behavior with cluster-aware tooltip and
    // cursor logic so they can't disagree on cluster bounds.
    template<> template<>
    void llstring_utf_object_t::test<69>()
    {
        LLWString ws = { (llwchar)0x2764, (llwchar)0xFE0F };
        ensure("BMP heart+VS16 stripped", wstring_remove_emojis(ws));
        ensure_wstring_equals("BMP heart+VS16 cleared", ws, LLWString());

        // Bare BMP heart (no VS-16) is not a cluster — single-codepoint
        // pictograph, shaped via the 1:1 path. Outside isEmoji's narrow
        // range too. Must pass through.
        LLWString bare_heart = { (llwchar)0x2764 };
        const LLWString bare_expected = bare_heart;
        ensure("bare heart not stripped", !wstring_remove_emojis(bare_heart));
        ensure_wstring_equals("bare heart preserved", bare_heart, bare_expected);
    }

    // ---------------------------------------------------------------
    //          7x emoji broad-strip (cluster-driven)
    // ---------------------------------------------------------------

    // Heart-on-fire ❤️‍🔥 (U+2764 U+FE0F U+200D U+1F525): full ZWJ sequence
    // with a BMP base. Pre-rewrite the walker would write through the
    // heart/VS-16/ZWJ then strip the lone fire, leaving `❤️‍` (heart +
    // VS-16 + dangling ZWJ) on screen. Cluster-driven removal strips
    // the whole sequence as one unit.
    template<> template<>
    void llstring_utf_object_t::test<70>()
    {
        LLWString ws = { (llwchar)'A',
                         (llwchar)0x2764, (llwchar)0xFE0F,
                         (llwchar)0x200D, (llwchar)0x1F525,
                         (llwchar)'B' };
        const LLWString expected = { (llwchar)'A', (llwchar)'B' };
        ensure("heart-on-fire found", wstring_remove_emojis(ws));
        ensure_wstring_equals("heart-on-fire cleared", ws, expected);
    }

    // Keycap `1️⃣` (`'1' + VS-16 + U+20E3`): no codepoint inside the cluster
    // is isEmoji-true (digit is ASCII, VS-16 / keycap combiner are BMP), so
    // the old narrow walker left the whole thing intact. The cluster walker
    // identifies it as a single 3-codepoint cluster, and the broad contract
    // strips it.
    template<> template<>
    void llstring_utf_object_t::test<71>()
    {
        LLWString digit_kc = { (llwchar)'1', (llwchar)0xFE0F, (llwchar)0x20E3, (llwchar)'!' };
        const LLWString expected = { (llwchar)'!' };
        ensure("digit keycap found", wstring_remove_emojis(digit_kc));
        ensure_wstring_equals("digit keycap cleared", digit_kc, expected);

        LLWString hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure("hash keycap found", wstring_remove_emojis(hash_kc));
        ensure_wstring_equals("hash keycap cleared", hash_kc, LLWString());

        LLWString star_kc = { (llwchar)'*', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure("star keycap found", wstring_remove_emojis(star_kc));
        ensure_wstring_equals("star keycap cleared", star_kc, LLWString());

        // Bare digit (no VS-16 + combiner) must pass through. The cluster
        // walker rejects it as a starter; nothing to strip.
        LLWString bare_digit = { (llwchar)'9' };
        const LLWString bare_expected = bare_digit;
        ensure("bare digit not stripped", !wstring_remove_emojis(bare_digit));
        ensure_wstring_equals("bare digit preserved", bare_digit, bare_expected);
    }

    // Subdivision flag 🏴󠁧󠁢󠁥󠁮󠁧󠁿 (U+1F3F4 + tag bytes + U+E007F CANCEL TAG).
    // Pre-rewrite the walker stripped the astral 🏴 base via isEmoji but
    // left tag chars and the U+E007F terminator behind as garbage code
    // points. Cluster-driven removal sweeps the whole sequence.
    template<> template<>
    void llstring_utf_object_t::test<72>()
    {
        LLWString ws = { (llwchar)'<',
                         (llwchar)0x1F3F4,
                         (llwchar)0xE0067, (llwchar)0xE0062, (llwchar)0xE0073,
                         (llwchar)0xE0063, (llwchar)0xE0074,
                         (llwchar)0xE007F,
                         (llwchar)'>' };
        const LLWString expected = { (llwchar)'<', (llwchar)'>' };
        ensure("subdivision flag found", wstring_remove_emojis(ws));
        ensure_wstring_equals("subdivision flag cleared", ws, expected);
    }

    // Astral emoji + VS-15 (U+FE0E text-presentation selector) — VS-15 is
    // an extender for the cluster walker, so it's part of the cluster and
    // gets stripped together with its base. Pre-rewrite VS-15 was not in
    // is_emoji_sequence_extender's narrow set and would have been left as
    // an orphan after the base was stripped.
    template<> template<>
    void llstring_utf_object_t::test<73>()
    {
        LLWString ws = { (llwchar)0x1F680, (llwchar)0xFE0E, (llwchar)'!' };
        const LLWString expected = { (llwchar)'!' };
        ensure("VS-15 cluster found", wstring_remove_emojis(ws));
        ensure_wstring_equals("VS-15 cluster cleared", ws, expected);
    }

    // Two flags back-to-back 🇺🇸🇯🇵 — distinct clusters [0,2) and [2,4).
    // Both must strip cleanly; no leftover RI codepoints.
    template<> template<>
    void llstring_utf_object_t::test<74>()
    {
        LLWString ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                         (llwchar)0x1F1EF, (llwchar)0x1F1F5,
                         (llwchar)'!' };
        const LLWString expected = { (llwchar)'!' };
        ensure("two flags found", wstring_remove_emojis(ws));
        ensure_wstring_equals("two flags cleared", ws, expected);
    }

    // ZWSP (U+200B) is not an emoji extender — it must pass through even
    // when adjacent to an astral emoji. Pre-rewrite this happened to work
    // (ZWSP isn't in the extender set), but pin it now that the path uses
    // the cluster walker, which also treats ZWSP as inert.
    template<> template<>
    void llstring_utf_object_t::test<75>()
    {
        LLWString ws = { (llwchar)0x1F680, (llwchar)0x200B, (llwchar)'X' };
        const LLWString expected = { (llwchar)0x200B, (llwchar)'X' };
        ensure("rocket+ZWSP found", wstring_remove_emojis(ws));
        ensure_wstring_equals("rocket stripped, ZWSP kept", ws, expected);
    }

    // ---------------------------------------------------------------
    //                  11x utf8str helper smoke pins
    // ---------------------------------------------------------------

    // utf8str_substChar replaces every occurrence of a code point (incl.
    // astrals) with another code point, routing through LLWString so the
    // byte-length change is handled for us.
    template<> template<>
    void llstring_utf_object_t::test<110>()
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
    void llstring_utf_object_t::test<111>()
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
    void llstring_utf_object_t::test<112>()
    {
        const std::string mixed = "A\xE6\x97\xA5" "B";
        const std::string expected = "A???B";
        ensure_equals("mbcsstring_makeASCII", mbcsstring_makeASCII(mixed), expected);
    }

    // utf8str_removeCRLF strips CR (0x0D) only — LF is preserved.
    template<> template<>
    void llstring_utf_object_t::test<113>()
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
    void llstring_utf_object_t::test<114>()
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
    void llstring_utf_object_t::test<115>()
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
        ensure_equals("empty",      wstring_find_emoji_clusters(LLWString()).size(), size_t(0));
        LLWString ascii = { (llwchar)'H', (llwchar)'i', (llwchar)'!' };
        ensure_equals("ascii",      wstring_find_emoji_clusters(ascii).size(),       size_t(0));
        LLWString cjk   = { (llwchar)0x65E5, (llwchar)0x672C };
        ensure_equals("cjk",        wstring_find_emoji_clusters(cjk).size(),         size_t(0));
        LLWString lone  = { (llwchar)0x1F680 };
        ensure_equals("lone emoji", wstring_find_emoji_clusters(lone).size(),        size_t(0));
        LLWString pair  = { (llwchar)0x1F680, (llwchar)0x1F681 };
        ensure_equals("two adj em", wstring_find_emoji_clusters(pair).size(),        size_t(0));
    }

    // ZWJ family 👨‍👩‍👧 = U+1F468 U+200D U+1F469 U+200D U+1F467 — must emerge
    // as a single 5-codepoint run.
    template<> template<>
    void llstring_utf_object_t::test<91>()
    {
        LLWString ws = { (llwchar)0x1F468, (llwchar)0x200D,
                         (llwchar)0x1F469, (llwchar)0x200D,
                         (llwchar)0x1F467 };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("one run",   runs.size(),    size_t(1));
        ensure_equals("run begin", runs[0].first,  size_t(0));
        ensure_equals("run end",   runs[0].second, size_t(5));
    }

    // Skin-tone modifier (U+1F3FB..FF) combines with the preceding emoji.
    template<> template<>
    void llstring_utf_object_t::test<92>()
    {
        LLWString ws = { (llwchar)0x1F468, (llwchar)0x1F3FB };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("skintone runs", runs.size(),    size_t(1));
        ensure_equals("skintone end",  runs[0].second, size_t(2));
    }

    // Trailing VS16 (U+FE0F) forces emoji presentation and must be part of
    // the same shaped run as its base.
    template<> template<>
    void llstring_utf_object_t::test<93>()
    {
        LLWString ws = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("vs16 runs", runs.size(),    size_t(1));
        ensure_equals("vs16 end",  runs[0].second, size_t(2));
    }

    // Regional indicator pair — 🇺🇸 = U+1F1FA U+1F1F8 — becomes a flag glyph
    // only under shaping. Two consecutive RIs form exactly one pair.
    template<> template<>
    void llstring_utf_object_t::test<94>()
    {
        LLWString ws = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("flag runs", runs.size(),    size_t(1));
        ensure_equals("flag end",  runs[0].second, size_t(2));
        // Four RIs in a row form two separate flags.
        LLWString four = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                           (llwchar)0x1F1EB, (llwchar)0x1F1F7 };
        auto more = wstring_find_emoji_clusters(four);
        ensure_equals("two flags", more.size(),    size_t(2));
        ensure_equals("flag0 end", more[0].second, size_t(2));
        ensure_equals("flag1 begin", more[1].first, size_t(2));
        ensure_equals("flag1 end",   more[1].second, size_t(4));
    }

    // Keycap: digit/#/* + U+FE0F + U+20E3 is a shaped 3-codepoint sequence.
    // shapeRun itemises this into per-face sub-runs (the ASCII digit on
    // the text font, the combining mark on the emoji font), so the
    // cluster is still detected here for cursor-snapping purposes.
    template<> template<>
    void llstring_utf_object_t::test<95>()
    {
        LLWString digit_kc = { (llwchar)'9', (llwchar)0xFE0F, (llwchar)0x20E3 };
        auto runs = wstring_find_emoji_clusters(digit_kc);
        ensure_equals("keycap runs", runs.size(),    size_t(1));
        ensure_equals("keycap end",  runs[0].second, size_t(3));
        LLWString hash_kc = { (llwchar)'#', (llwchar)0xFE0F, (llwchar)0x20E3 };
        ensure_equals("hash keycap", wstring_find_emoji_clusters(hash_kc).size(), size_t(1));
        LLWString bare = { (llwchar)'5' };
        ensure_equals("bare digit",  wstring_find_emoji_clusters(bare).size(),    size_t(0));
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
        auto runs = wstring_find_emoji_clusters(ws);
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
        auto runs = wstring_find_emoji_clusters(ws);
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
        ensure_equals("bare zwj",  wstring_find_emoji_clusters(zwj).size(),  size_t(0));
        LLWString vs16 = { (llwchar)'a', (llwchar)0xFE0F, (llwchar)'b' };
        ensure_equals("bare vs16", wstring_find_emoji_clusters(vs16).size(), size_t(0));

        // Malformed inputs: leading/trailing extenders, lone modifiers, and
        // singletons that need a partner. Pin current behavior so future
        // refactors don't silently change cluster boundaries on broken input.
        LLWString lead_zwj  = { (llwchar)0x200D, (llwchar)0x1F468 };
        ensure_equals("leading zwj", wstring_find_emoji_clusters(lead_zwj).size(), size_t(0));
        LLWString trail_zwj = { (llwchar)0x1F468, (llwchar)0x200D };
        // Trailing ZWJ: is_shaping_starter accepts the base because the
        // next codepoint is a ZWJ, but advance_shaping_run hits the
        // orphan-ZWJ break and stops at r=1. The cluster walker discards
        // the resulting length-1 "run" so the result matches the doc
        // contract (isolated single-codepoint emoji are intentionally
        // absent from the cluster list).
        ensure_equals("trailing zwj", wstring_find_emoji_clusters(trail_zwj).size(), size_t(0));
        // Same shape but with VS-16 instead of ZWJ: 'is_shaping_starter'
        // accepts because VS-16 is a valid extender, advance consumes it,
        // run length is 2 → real 2-codepoint cluster. Confirm we didn't
        // over-trim and accidentally drop legitimate 2-codepoint clusters.
        LLWString base_vs16 = { (llwchar)0x1F680, (llwchar)0xFE0F };
        auto vs_runs = wstring_find_emoji_clusters(base_vs16);
        ensure_equals("base+VS16 count", vs_runs.size(), size_t(1));
        ensure_equals("base+VS16 begin", vs_runs[0].first,  size_t(0));
        ensure_equals("base+VS16 end",   vs_runs[0].second, size_t(2));
        LLWString lone_skin = { (llwchar)0x1F3FB };
        ensure_equals("lone skintone", wstring_find_emoji_clusters(lone_skin).size(), size_t(0));
        LLWString lone_ri = { (llwchar)0x1F1FA };
        ensure_equals("lone RI", wstring_find_emoji_clusters(lone_ri).size(), size_t(0));

        // Range boundaries on isPictographBase's astral / BMP windows.
        LLWString just_below_astral = { (llwchar)0x1FFF };
        ensure_equals("U+1FFF no run", wstring_find_emoji_clusters(just_below_astral).size(), size_t(0));
        LLWString just_above_bmp    = { (llwchar)0x3300 };
        ensure_equals("U+3300 no run", wstring_find_emoji_clusters(just_above_bmp).size(), size_t(0));

        // Back-to-back ZWJ families with no separator. Pin current behavior:
        // each MAN-ZWJ-WOMAN sub-sequence registers as its own cluster, so we
        // expect two runs of length 3 each.
        LLWString two_fam = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                              (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469 };
        auto two_fam_runs = wstring_find_emoji_clusters(two_fam);
        ensure_equals("two_fam count",   two_fam_runs.size(),     size_t(2));
        ensure_equals("two_fam[0].first", two_fam_runs[0].first,  size_t(0));
        ensure_equals("two_fam[0].second", two_fam_runs[0].second, size_t(3));
        ensure_equals("two_fam[1].first", two_fam_runs[1].first,  size_t(3));
        ensure_equals("two_fam[1].second", two_fam_runs[1].second, size_t(6));

        // ZWJ followed by a non-pictograph: starter accepts the base
        // (next is ZWJ), advance_shaping_run reaches the ZWJ, sees the
        // non-pictograph 'A' afterwards, breaks orphan-ZWJ. Length-1 run
        // gets discarded by the cluster walker. No cluster emitted.
        LLWString zwj_plus_nonemoji = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)'A' };
        ensure_equals("ZWJ+non-pictograph", wstring_find_emoji_clusters(zwj_plus_nonemoji).size(), size_t(0));

        // Double ZWJ (ZWJ + ZWJ between bases): advance hits the first
        // ZWJ, looks at the next codepoint, finds another ZWJ which is
        // not a pictograph base. Orphan-ZWJ break. Length 1, discarded.
        LLWString double_zwj = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x200D, (llwchar)0x1F469 };
        ensure_equals("double ZWJ", wstring_find_emoji_clusters(double_zwj).size(), size_t(0));

        // Truncated subdivision flag — base 🏴 + a couple of tag bytes,
        // no U+E007F terminator. Pin: walker greedily eats every tag
        // codepoint to end-of-string. Real implementation behavior.
        LLWString trunc_tag = { (llwchar)0x1F3F4, (llwchar)0xE0067, (llwchar)0xE0062 };
        auto trunc_runs = wstring_find_emoji_clusters(trunc_tag);
        ensure_equals("trunc tag count", trunc_runs.size(),    size_t(1));
        ensure_equals("trunc tag begin", trunc_runs[0].first,  size_t(0));
        ensure_equals("trunc tag end",   trunc_runs[0].second, size_t(3));

        // Three RIs — first two form a flag at [0,2), third RI alone is
        // not a starter (no partner). Pin: one cluster, length 2.
        LLWString three_ri = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)0x1F1F8 };
        auto three_ri_runs = wstring_find_emoji_clusters(three_ri);
        ensure_equals("three RI count", three_ri_runs.size(),    size_t(1));
        ensure_equals("three RI begin", three_ri_runs[0].first,  size_t(0));
        ensure_equals("three RI end",   three_ri_runs[0].second, size_t(2));

        // Star keycap '*' + VS-16 + U+20E3. Pin parity with digit/hash.
        LLWString star_kc = { (llwchar)'*', (llwchar)0xFE0F, (llwchar)0x20E3 };
        auto star_runs = wstring_find_emoji_clusters(star_kc);
        ensure_equals("star kc count", star_runs.size(),    size_t(1));
        ensure_equals("star kc end",   star_runs[0].second, size_t(3));
    }

    // BMP pictograph + VS16 + ZWJ + astral emoji (❤️‍🔥 = U+2764 U+FE0F
    // U+200D U+1F525). The base ❤ is outside LLStringOps::isEmoji's astral
    // range, but the shaping detector must still pick up the whole sequence
    // so HarfBuzz gets to compose the "heart on fire" glyph instead of
    // leaving ❤ and 🔥 as disjoint codepoints.
    template<> template<>
    void llstring_utf_object_t::test<99>()
    {
        LLWString ws = { (llwchar)0x2764, (llwchar)0xFE0F,
                         (llwchar)0x200D, (llwchar)0x1F525 };
        auto runs = wstring_find_emoji_clusters(ws);
        ensure_equals("bmp zwj runs", runs.size(),    size_t(1));
        ensure_equals("bmp zwj begin", runs[0].first,  size_t(0));
        ensure_equals("bmp zwj end",   runs[0].second, size_t(4));
    }

    // ---------------------------------------------------------------
    //                 grapheme-step cursor walker
    // ---------------------------------------------------------------

    // Forward step must move exactly one codepoint through ASCII and skip
    // past entire emoji clusters in one move so the caret never lands mid-
    // ZWJ-family.
    template<> template<>
    void llstring_utf_object_t::test<100>()
    {
        // Pure ASCII — one codepoint per step, clamped at size.
        LLWString ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        ensure_equals("ascii 0->1", wstring_step_grapheme_forward(ascii, 0), size_t(1));
        ensure_equals("ascii 2->3", wstring_step_grapheme_forward(ascii, 2), size_t(3));
        ensure_equals("ascii at end stays", wstring_step_grapheme_forward(ascii, 3), size_t(3));

        // ZWJ family in the middle: H, 👨, ZWJ, 👩, ZWJ, 👧, i
        //                           0  1    2    3    4    5    6
        LLWString fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };
        ensure_equals("H->fam start", wstring_step_grapheme_forward(fam, 0), size_t(1));
        ensure_equals("fam start jumps past cluster",
                      wstring_step_grapheme_forward(fam, 1), size_t(6));
        ensure_equals("from mid-cluster snaps past",
                      wstring_step_grapheme_forward(fam, 3), size_t(6));
        ensure_equals("fam end -> 'i'", wstring_step_grapheme_forward(fam, 6), size_t(7));

        // Regional indicator flag: US = U+1F1FA U+1F1F8. One step covers both.
        LLWString flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        ensure_equals("flag 0->past both",
                      wstring_step_grapheme_forward(flag, 0), size_t(2));
    }

    // Backward step is the mirror: one codepoint through ASCII, snap to the
    // start of any emoji cluster we'd otherwise land inside.
    template<> template<>
    void llstring_utf_object_t::test<101>()
    {
        LLWString ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        ensure_equals("ascii 3->2", wstring_step_grapheme_backward(ascii, 3), size_t(2));
        ensure_equals("ascii 1->0", wstring_step_grapheme_backward(ascii, 1), size_t(0));
        ensure_equals("ascii at 0 stays", wstring_step_grapheme_backward(ascii, 0), size_t(0));

        LLWString fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };
        ensure_equals("end of string -> 'i' start",
                      wstring_step_grapheme_backward(fam, 7), size_t(6));
        ensure_equals("'i' start snaps back to cluster start",
                      wstring_step_grapheme_backward(fam, 6), size_t(1));
        ensure_equals("mid-cluster snaps to cluster start",
                      wstring_step_grapheme_backward(fam, 4), size_t(1));
        ensure_equals("cluster start -> 'H'",
                      wstring_step_grapheme_backward(fam, 1), size_t(0));

        LLWString flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        ensure_equals("flag end -> 0 (whole flag is one cluster)",
                      wstring_step_grapheme_backward(flag, 2), size_t(0));
    }

    // ---------------------------------------------------------------
    // LLStringOps::isPictographBase: Extended_Pictographic plus the regional
    // indicators, so heart-on-fire (U+2764 + ZWJ + U+1F525) registers as an
    // emoji-cluster start and flags still pair up.
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<102>()
    {
        // Astral emoji: both predicates accept.
        ensure("isPictographBase rocket",
                LLStringOps::isPictographBase((llwchar)0x1F680));
        ensure("isPictographBase fire",
                LLStringOps::isPictographBase((llwchar)0x1F525));
        // BMP pictographs that isEmoji rejects but isPictographBase accepts.
        ensure("isPictographBase heart",
                LLStringOps::isPictographBase((llwchar)0x2764));
        ensure("isPictographBase copyright",
                LLStringOps::isPictographBase((llwchar)0x00A9));
        ensure("isPictographBase registered",
                LLStringOps::isPictographBase((llwchar)0x00AE));
        // Plain ASCII / digits never qualify.
        ensure("not isPictographBase 'A'",
               !LLStringOps::isPictographBase((llwchar)'A'));
        ensure("not isPictographBase '0'",
               !LLStringOps::isPictographBase((llwchar)'0'));
        // ZWJ and VS-16 are extenders, not bases.
        ensure("not isPictographBase ZWJ",
               !LLStringOps::isPictographBase((llwchar)0x200D));
        ensure("not isPictographBase VS-16",
               !LLStringOps::isPictographBase((llwchar)0xFE0F));
        ensure("not isPictographBase U+20000",
               !LLStringOps::isPictographBase((llwchar)0x20000));
        ensure("not isPictographBase U+1FFF",
               !LLStringOps::isPictographBase((llwchar)0x1FFF));
        ensure("not isPictographBase U+3300 boundary",
               !LLStringOps::isPictographBase((llwchar)0x3300));

        // A flag is two regional indicators and nothing else. Unicode does
        // not call them pictographic, so they are named here separately;
        // reading Extended_Pictographic alone would stop flags clustering.
        ensure("isPictographBase regional indicator",
                LLStringOps::isPictographBase((llwchar)0x1F1FA));

        // The old range ran from U+2000 to U+3300, which swept up punctuation,
        // maths, and every kana and Hangul jamo along the way.
        ensure("not isPictographBase EN QUAD",
               !LLStringOps::isPictographBase((llwchar)0x2000));
        ensure("not isPictographBase summation",
               !LLStringOps::isPictographBase((llwchar)0x2211));
        ensure("not isPictographBase hiragana",
               !LLStringOps::isPictographBase((llwchar)0x3042));
        ensure("not isPictographBase katakana",
               !LLStringOps::isPictographBase((llwchar)0x30A2));
        ensure("not isPictographBase Hangul jamo",
               !LLStringOps::isPictographBase((llwchar)0x3131));
    }

    // ---------------------------------------------------------------
    // wstring_emoji_range_at: cluster bounds at a hit-test position.
    // Inclusive of cluster boundaries (unlike grapheme_align_*) and
    // synthesises a [pos, pos+1) range for single-codepoint emoji that
    // wstring_find_emoji_clusters skips.
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<103>()
    {
        // ZWJ family "H 👨 ZWJ 👩 ZWJ 👧 i" — cluster spans [1, 6).
        LLWString fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };
        auto on_lead   = wstring_emoji_range_at(fam, 1);
        auto inside    = wstring_emoji_range_at(fam, 3);
        auto on_end    = wstring_emoji_range_at(fam, 6);
        ensure_equals("fam pos==first.first", on_lead.first,  size_t(1));
        ensure_equals("fam pos==first.second", on_lead.second, size_t(6));
        ensure_equals("fam mid.first", inside.first,  size_t(1));
        ensure_equals("fam mid.second", inside.second, size_t(6));
        // pos == run.second is past the cluster — falls through to the
        // codepoint there ('i', non-emoji), so empty range.
        ensure_equals("fam past-end empty", on_end.first, on_end.second);

        // Plain ASCII: empty range everywhere.
        LLWString ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        auto a0 = wstring_emoji_range_at(ascii, 0);
        auto a2 = wstring_emoji_range_at(ascii, 2);
        ensure_equals("ascii 0 empty", a0.first, a0.second);
        ensure_equals("ascii 2 empty", a2.first, a2.second);

        // Single-codepoint emoji 😀 (U+1F600): not in cluster list, but
        // the helper synthesises [pos, pos+1).
        LLWString lone = { (llwchar)0x1F600 };
        auto le = wstring_emoji_range_at(lone, 0);
        ensure_equals("lone emoji.first", le.first,  size_t(0));
        ensure_equals("lone emoji.second", le.second, size_t(1));

        // Out-of-bounds and exact-end positions are empty.
        auto at_end   = wstring_emoji_range_at(ascii, 3);
        auto past_end = wstring_emoji_range_at(ascii, 99);
        ensure_equals("ascii at-end empty", at_end.first,   at_end.second);
        ensure_equals("ascii oob empty",    past_end.first, past_end.second);

        // Empty wstring: empty range.
        auto empty = wstring_emoji_range_at(LLWString(), 0);
        ensure_equals("empty wstr empty", empty.first, empty.second);

        // Flag pair 🇺🇸 — one cluster spanning [0, 2).
        LLWString flag = { (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        auto f0 = wstring_emoji_range_at(flag, 0);
        auto f1 = wstring_emoji_range_at(flag, 1);
        ensure_equals("flag at lead.first",  f0.first,  size_t(0));
        ensure_equals("flag at lead.second", f0.second, size_t(2));
        ensure_equals("flag mid.first",  f1.first,  size_t(0));
        ensure_equals("flag mid.second", f1.second, size_t(2));

        // BMP pictographs — the orthodox cross bug. isPictographBase accepts
        // U+2000..U+33FF plus © U+00A9 and ® U+00AE; isEmoji rejects them.
        // Tooltip lookup must still find them.
        auto check_bmp = [](llwchar cp, const char* tag) {
            LLWString ws { cp };
            auto r = wstring_emoji_range_at(ws, 0);
            ensure_equals(std::string(tag) + ".first",  r.first,  size_t(0));
            ensure_equals(std::string(tag) + ".second", r.second, size_t(1));
        };
        check_bmp((llwchar)0x2626, "orthodox cross");
        check_bmp((llwchar)0x00A9, "copyright");
        check_bmp((llwchar)0x00AE, "registered");
        check_bmp((llwchar)0x2764, "heart");
        check_bmp((llwchar)0x2693, "anchor");

        // Bare extenders / out-of-range codepoints get an empty range —
        // isPictographBase explicitly rejects these so they don't trigger
        // a tooltip lookup on their own.
        auto check_no_range = [](llwchar cp, const char* tag) {
            LLWString ws { cp };
            auto r = wstring_emoji_range_at(ws, 0);
            ensure_equals(std::string(tag) + " empty", r.first, r.second);
        };
        check_no_range((llwchar)0x200D, "bare ZWJ");
        check_no_range((llwchar)0xFE0F, "bare VS-16");
        check_no_range((llwchar)0xFE0E, "bare VS-15");
        check_no_range((llwchar)0x20E3, "bare keycap combiner");
        check_no_range((llwchar)0x1F3FB, "bare skintone");
        check_no_range((llwchar)0xE0067, "bare tag char");
        check_no_range((llwchar)0x1FFF, "U+1FFF below astral");
        check_no_range((llwchar)0x3300, "U+3300 above BMP");
        check_no_range((llwchar)0x20000, "U+20000 above astral");

        // Back-to-back flags 🇺🇸🇯🇵 — pos lookup must find the *second* run
        // when pos has passed the first. Exercises the ordered-iteration
        // early-out (`if (pos < run.first) break`) — must not stop early.
        LLWString two_flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                                (llwchar)0x1F1EF, (llwchar)0x1F1F5 };
        auto tf0 = wstring_emoji_range_at(two_flags, 0);
        auto tf2 = wstring_emoji_range_at(two_flags, 2);
        auto tf3 = wstring_emoji_range_at(two_flags, 3);
        ensure_equals("two_flags[0].first",  tf0.first,  size_t(0));
        ensure_equals("two_flags[0].second", tf0.second, size_t(2));
        ensure_equals("two_flags[2].first",  tf2.first,  size_t(2));
        ensure_equals("two_flags[2].second", tf2.second, size_t(4));
        ensure_equals("two_flags mid 2nd flag",  tf3.first,  size_t(2));
        ensure_equals("two_flags mid 2nd second", tf3.second, size_t(4));

        // Mixed "A 🚀 B": ASCII positions return empty, the rocket position
        // returns its [pos, pos+1) range via the single-codepoint fallback.
        LLWString mixed = { (llwchar)'A', (llwchar)0x1F680, (llwchar)'B' };
        auto m0 = wstring_emoji_range_at(mixed, 0);
        auto m1 = wstring_emoji_range_at(mixed, 1);
        auto m2 = wstring_emoji_range_at(mixed, 2);
        ensure_equals("mixed A empty", m0.first, m0.second);
        ensure_equals("mixed rocket.first",  m1.first,  size_t(1));
        ensure_equals("mixed rocket.second", m1.second, size_t(2));
        ensure_equals("mixed B empty", m2.first, m2.second);
    }

    // ---------------------------------------------------------------
    // wstring_grapheme_align_backward / _forward: snap onto cluster
    // boundary only when pos is *strictly inside* a cluster. Existing
    // word-walk callers (LLTextEditor::prevWordPos, LLLineEditor::*WordPos)
    // depend on positions already on a boundary being returned unchanged;
    // setCursorPos newly depends on mid-cluster snap. Pin both halves of
    // the contract so neither group regresses.
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<104>()
    {
        // ASCII: every position is already on a boundary, so both halves
        // are identity functions across the entire range.
        LLWString ascii = { (llwchar)'a', (llwchar)'b', (llwchar)'c' };
        for (size_t p = 0; p <= ascii.size(); ++p)
        {
            ensure_equals("ascii align_backward",
                          wstring_grapheme_align_backward(ascii, p), p);
            ensure_equals("ascii align_forward",
                          wstring_grapheme_align_forward(ascii, p), p);
        }

        // ZWJ family "H 👨 ZWJ 👩 ZWJ 👧 i" — cluster spans [1, 6).
        LLWString fam = { (llwchar)'H',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'i' };

        // Endpoint short-circuits: pos == 0 stays 0 (backward); pos >= size
        // stays size (forward) regardless of trailing emoji.
        ensure_equals("fam align_backward(0)",
                      wstring_grapheme_align_backward(fam, 0), size_t(0));
        ensure_equals("fam align_backward(size)",
                      wstring_grapheme_align_backward(fam, fam.size()), fam.size());
        ensure_equals("fam align_forward(size)",
                      wstring_grapheme_align_forward(fam, fam.size()), fam.size());
        ensure_equals("fam align_forward(99)",
                      wstring_grapheme_align_forward(fam, 99), fam.size());

        // pos == run.first — strictly outside, returned unchanged. This is
        // the contract LLTextEditor::prevWordPos relies on: a word-walk
        // that already landed on a cluster start is fine and must not be
        // pulled backward into something else.
        ensure_equals("fam align_backward(1)",
                      wstring_grapheme_align_backward(fam, 1), size_t(1));
        ensure_equals("fam align_forward(1)",
                      wstring_grapheme_align_forward(fam, 1), size_t(1));
        // pos == run.second — also strictly outside, unchanged.
        ensure_equals("fam align_backward(6)",
                      wstring_grapheme_align_backward(fam, 6), size_t(6));
        ensure_equals("fam align_forward(6)",
                      wstring_grapheme_align_forward(fam, 6), size_t(6));

        // pos strictly inside [2..5] — backward snaps to 1, forward to 6.
        for (size_t p = 2; p <= 5; ++p)
        {
            ensure_equals("fam mid align_backward",
                          wstring_grapheme_align_backward(fam, p), size_t(1));
            ensure_equals("fam mid align_forward",
                          wstring_grapheme_align_forward(fam, p), size_t(6));
        }

        // Outside the cluster — surrounding ASCII positions unchanged.
        ensure_equals("fam align_backward(7)",
                      wstring_grapheme_align_backward(fam, 7), size_t(7));
        ensure_equals("fam align_forward(7)",
                      wstring_grapheme_align_forward(fam, 7), size_t(7));

        // Two disjoint clusters in one string — flag at [1,3), family at
        // [4,9). Inside-the-second-cluster lookups must reach the second
        // run despite the first run being earlier in the iteration. Pins
        // that the early-out (`if (pos <= run.first) break`) doesn't
        // terminate prematurely.
        LLWString two = { (llwchar)'H',
                          (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                          (llwchar)' ',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'!' };
        // Flag interior (pos == 2) → flag.first 1.
        ensure_equals("two flag mid backward",
                      wstring_grapheme_align_backward(two, 2), size_t(1));
        ensure_equals("two flag mid forward",
                      wstring_grapheme_align_forward(two, 2), size_t(3));
        // Family interior (pos == 6) → family bounds [4, 9).
        ensure_equals("two fam mid backward",
                      wstring_grapheme_align_backward(two, 6), size_t(4));
        ensure_equals("two fam mid forward",
                      wstring_grapheme_align_forward(two, 6), size_t(9));
        // Between the two clusters (the space at pos 3) — unchanged.
        ensure_equals("two between backward",
                      wstring_grapheme_align_backward(two, 3), size_t(3));
        ensure_equals("two between forward",
                      wstring_grapheme_align_forward(two, 3), size_t(3));
    }

    // ---------------------------------------------------------------
    // step_grapheme + emoji_range_at: malformed-input pins. The simple
    // ZWJ-family / flag cases are covered above (tests 100/101/103/104);
    // here we cover the rougher edges that show up in user-pasted text:
    // empty strings, out-of-bounds positions, clusters at the very ends
    // of the buffer, two clusters back-to-back with no separator, and
    // hit-test positions sitting on tag chars / extenders inside a
    // cluster (where the caller wants the whole cluster's range, not a
    // synthetic singleton range).
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<105>()
    {
        // Empty wstring — no grapheme moves possible.
        LLWString empty;
        ensure_equals("empty step_forward",  wstring_step_grapheme_forward(empty, 0),  size_t(0));
        ensure_equals("empty step_backward", wstring_step_grapheme_backward(empty, 0), size_t(0));

        // Out-of-bounds position — both directions clamp into [0, size], as the
        // header says. The old walker returned pos-1 here, handing a caller an
        // index past the end of its own string.
        LLWString ascii = { (llwchar)'a', (llwchar)'b' };
        ensure_equals("oob step_forward",  wstring_step_grapheme_forward(ascii, 99),  size_t(2));
        ensure_equals("oob step_backward", wstring_step_grapheme_backward(ascii, 99), size_t(2));

        // Cluster at start of string — backward from inside or just-past
        // snaps to 0 (cluster.first).
        LLWString lead = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)'X' };
        ensure_equals("lead flag step_forward(0)",
                      wstring_step_grapheme_forward(lead, 0), size_t(2));
        ensure_equals("lead flag step_backward(2)",
                      wstring_step_grapheme_backward(lead, 2), size_t(0));

        // Cluster at end of string — forward from inside snaps to size.
        LLWString trail = { (llwchar)'X', (llwchar)0x1F1FA, (llwchar)0x1F1F8 };
        ensure_equals("trail flag step_forward(1)",
                      wstring_step_grapheme_forward(trail, 1), size_t(3));
        ensure_equals("trail flag step_backward(size)",
                      wstring_step_grapheme_backward(trail, trail.size()), size_t(1));

        // Two clusters back-to-back with no separator: step_forward from
        // inside cluster A must land at A.end (which is also B.first), not
        // skip into cluster B.
        LLWString two_flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                                (llwchar)0x1F1EF, (llwchar)0x1F1F5 };
        ensure_equals("two flags forward",
                      wstring_step_grapheme_forward(two_flags, 0), size_t(2));
        ensure_equals("two flags forward 2nd",
                      wstring_step_grapheme_forward(two_flags, 2), size_t(4));
        ensure_equals("two flags backward",
                      wstring_step_grapheme_backward(two_flags, 4), size_t(2));
        ensure_equals("two flags backward 1st",
                      wstring_step_grapheme_backward(two_flags, 2), size_t(0));

        // emoji_range_at on a tag char inside a subdivision flag — the
        // hit-test caller wants the whole flag's range so the tooltip key
        // covers the composed glyph, not the raw tag byte.
        LLWString subdiv = { (llwchar)'<',
                             (llwchar)0x1F3F4,
                             (llwchar)0xE0067, (llwchar)0xE0062, (llwchar)0xE0073,
                             (llwchar)0xE0063, (llwchar)0xE0074,
                             (llwchar)0xE007F,
                             (llwchar)'>' };
        // pos on the base, on a tag interior char, and on the CANCEL TAG
        // terminator must all return the same [1, 8) cluster range.
        for (size_t p = 1; p < 8; ++p)
        {
            auto r = wstring_emoji_range_at(subdiv, p);
            ensure_equals("subdiv begin", r.first,  size_t(1));
            ensure_equals("subdiv end",   r.second, size_t(8));
        }
        // pos on the surrounding ASCII falls outside the cluster.
        auto pre  = wstring_emoji_range_at(subdiv, 0);
        auto post = wstring_emoji_range_at(subdiv, 8);
        ensure_equals("subdiv pre empty",  pre.first,  pre.second);
        ensure_equals("subdiv post empty", post.first, post.second);
    }

    // ---------------------------------------------------------------
    // Pin overload-equivalence: the no-clusters and with-clusters forms
    // of wstring_emoji_range_at must produce identical results for the
    // same input. The with-clusters overload exists to let callers
    // amortise the wstring_find_emoji_clusters scan across many lookups,
    // so any future change that updates one body without the other
    // (e.g. a bug fix in the loop) needs to fail loudly here.
    // ---------------------------------------------------------------

    template<> template<>
    void llstring_utf_object_t::test<106>()
    {
        // Same mixed-content string used in test<104>: ASCII + flag +
        // space + ZWJ family + ASCII. Two disjoint clusters at [1,3) and
        // [4,9), so the loop bodies exercise both early-out and middle-
        // of-vector iteration.
        LLWString two = { (llwchar)'H',
                          (llwchar)0x1F1FA, (llwchar)0x1F1F8,
                          (llwchar)' ',
                          (llwchar)0x1F468, (llwchar)0x200D,
                          (llwchar)0x1F469, (llwchar)0x200D,
                          (llwchar)0x1F467,
                          (llwchar)'!' };
        const auto clusters = wstring_find_emoji_clusters(two);
        for (size_t p = 0; p <= two.size(); ++p)
        {
            auto r1 = wstring_emoji_range_at(two, p);
            auto r2 = wstring_emoji_range_at(two, p, clusters);
            ensure_equals("range_at first",  r1.first,  r2.first);
            ensure_equals("range_at second", r1.second, r2.second);
        }

        // Empty wstring — pinning that the with-clusters overload handles a
        // degenerate empty cluster vector identically to the no-clusters form.
        LLWString empty;
        const EmojiClusterList no_clusters;
        ensure_equals("empty range_at first overload",
                      wstring_emoji_range_at(empty, 0).first,
                      wstring_emoji_range_at(empty, 0, no_clusters).first);
    }

    // wstring_wstring_length_from_utf8_length walks a wstring spending a byte
    // budget, stopping before any codepoint whose encoding would overrun it and
    // reporting unaligned when the budget landed inside a multi-byte sequence.
    template<> template<>
    void llstring_utf_object_t::test<116>()
    {
        // 'A' (1 byte), U+00E9 (2), U+1F600 (4), 'B' (1). Cumulative: 0,1,3,7,8.
        const LLWString w = { (llwchar)'A', (llwchar)0x00E9, (llwchar)0x1F600, (llwchar)'B' };
        bool unaligned = false;

        ensure_equals("budget 0", wstring_wstring_length_from_utf8_length(w, 0, 0, &unaligned), S32(0));
        ensure("budget 0 aligned", !unaligned);

        unaligned = true;
        ensure_equals("budget 1", wstring_wstring_length_from_utf8_length(w, 0, 1, &unaligned), S32(1));
        ensure("budget 1 aligned", !unaligned);

        // 2 lands inside U+00E9's two bytes: 'A' only, and say so.
        unaligned = false;
        ensure_equals("budget 2", wstring_wstring_length_from_utf8_length(w, 0, 2, &unaligned), S32(1));
        ensure("budget 2 unaligned", unaligned);

        unaligned = true;
        ensure_equals("budget 3", wstring_wstring_length_from_utf8_length(w, 0, 3, &unaligned), S32(2));
        ensure("budget 3 aligned", !unaligned);

        // 4, 5 and 6 all land inside the astral codepoint's four bytes.
        for (S32 budget = 4; budget <= 6; ++budget)
        {
            unaligned = false;
            ensure_equals("astral straddle", wstring_wstring_length_from_utf8_length(w, 0, budget, &unaligned), S32(2));
            ensure("astral straddle unaligned", unaligned);
        }

        unaligned = true;
        ensure_equals("budget 7", wstring_wstring_length_from_utf8_length(w, 0, 7, &unaligned), S32(3));
        ensure("budget 7 aligned", !unaligned);

        unaligned = true;
        ensure_equals("budget 8", wstring_wstring_length_from_utf8_length(w, 0, 8, &unaligned), S32(4));
        ensure("budget 8 aligned", !unaligned);

        // Running out of string is not the same as stopping mid-codepoint.
        unaligned = true;
        ensure_equals("budget past end", wstring_wstring_length_from_utf8_length(w, 0, 100, &unaligned), S32(4));
        ensure("budget past end aligned", !unaligned);

        // woffset is respected, and the count returned is relative to it.
        ensure_equals("offset 1 budget 2", wstring_wstring_length_from_utf8_length(w, 1, 2), S32(1));
        ensure_equals("offset 2 budget 4", wstring_wstring_length_from_utf8_length(w, 2, 4), S32(1));
        ensure_equals("offset 2 budget 3", wstring_wstring_length_from_utf8_length(w, 2, 3), S32(0));

        // Offset past the end clamps rather than running off.
        ensure_equals("offset oob", wstring_wstring_length_from_utf8_length(w, 10, 5), S32(0));

        // Empty and null-budget degenerate cases.
        const LLWString empty_w;
        ensure_equals("empty wstring", wstring_wstring_length_from_utf8_length(empty_w, 0, 4), S32(0));

        // The shape this exists for: a std::string::find offset against the UTF-8
        // form of the same text becomes the matching codepoint index.
        const std::string utf8 = wstring_to_utf8str(w);
        const auto byte_offset = utf8.find("B");
        ensure("needle found", byte_offset != std::string::npos);
        ensure_equals("byte offset maps to codepoint index",
                      wstring_wstring_length_from_utf8_length(w, 0, (S32)byte_offset), S32(3));
    }

    // The grapheme walkers moved from an emoji-only cluster list to full
    // UAX #29 via ICU. These are the cases the old walker stepped
    // through one codepoint at a time.
    template<> template<>
    void llstring_utf_object_t::test<117>()
    {
        // "e" + COMBINING ACUTE, then "x". The base and its mark are one
        // cluster; the old walker split them.
        const LLWString combining = { (llwchar)'e', (llwchar)0x0301, (llwchar)'x' };
        ensure_equals("combining fwd from 0", wstring_step_grapheme_forward(combining, 0), size_t(2));
        ensure_equals("combining back from 2", wstring_step_grapheme_backward(combining, 2), size_t(0));
        ensure_equals("combining align back mid", wstring_grapheme_align_backward(combining, 1), size_t(0));
        ensure_equals("combining align fwd mid", wstring_grapheme_align_forward(combining, 1), size_t(2));

        // Hangul LVT: HANGUL SYLLABLE inputs as jamo L + V + T are one cluster.
        const LLWString hangul = { (llwchar)0x1100, (llwchar)0x1161, (llwchar)0x11A8, (llwchar)'!' };
        ensure_equals("hangul fwd from 0", wstring_step_grapheme_forward(hangul, 0), size_t(3));
        ensure_equals("hangul back from 3", wstring_step_grapheme_backward(hangul, 3), size_t(0));

        // Regional indicator pairs still pair up, and a third RI starts a new
        // cluster rather than joining the first two.
        const LLWString flags = { (llwchar)0x1F1FA, (llwchar)0x1F1F8, (llwchar)0x1F1FA };
        ensure_equals("flag pair fwd", wstring_step_grapheme_forward(flags, 0), size_t(2));
        ensure_equals("third RI is its own", wstring_step_grapheme_forward(flags, 2), size_t(3));

        // ZWJ family stays one cluster, as before.
        const LLWString family = { (llwchar)0x1F468, (llwchar)0x200D, (llwchar)0x1F469,
                                   (llwchar)0x200D, (llwchar)0x1F467, (llwchar)'!' };
        ensure_equals("zwj family fwd", wstring_step_grapheme_forward(family, 0), size_t(5));
        ensure_equals("zwj family back", wstring_step_grapheme_backward(family, 5), size_t(0));

        // GB4 breaks after LF, which is also what bounds the backward scan.
        const LLWString lines = { (llwchar)'a', (llwchar)'\n', (llwchar)'e', (llwchar)0x0301 };
        ensure_equals("break after lf", wstring_step_grapheme_backward(lines, 2), size_t(1));
        ensure_equals("cluster after lf", wstring_step_grapheme_backward(lines, 4), size_t(2));

        // Degenerate input: empty, and positions past the end clamp.
        const LLWString empty;
        ensure_equals("empty fwd", wstring_step_grapheme_forward(empty, 0), size_t(0));
        ensure_equals("empty back", wstring_step_grapheme_backward(empty, 0), size_t(0));
        ensure_equals("past end fwd", wstring_step_grapheme_forward(combining, 99), combining.size());
        ensure_equals("past end align", wstring_grapheme_align_forward(combining, 99), combining.size());

        // Stepping forward from every position must strictly advance, and
        // backward must strictly retreat, or a caret can wedge.
        for (size_t p = 0; p < family.size(); ++p)
        {
            ensure("fwd advances", wstring_step_grapheme_forward(family, p) > p);
        }
        for (size_t p = 1; p <= family.size(); ++p)
        {
            ensure("back retreats", wstring_step_grapheme_backward(family, p) < p);
        }
    }

    // Word stepping lands on the start of a word, stepping over the whitespace
    // between. UAX #29 via ICU, replacing an alnum-or-underscore test.
    template<> template<>
    void llstring_utf_object_t::test<118>()
    {
        const LLWString two_words = utf8str_to_wstring("foo bar");
        ensure_equals("fwd to next word", wstring_step_word_forward(two_words, 0), size_t(4));
        ensure_equals("fwd from next word", wstring_step_word_forward(two_words, 4), size_t(7));
        ensure_equals("back from end", wstring_step_word_backward(two_words, 7), size_t(4));
        ensure_equals("back over the space", wstring_step_word_backward(two_words, 4), size_t(0));
        ensure_equals("back from mid-word", wstring_step_word_backward(two_words, 5), size_t(4));

        // A full stop between letters does not split the word -- UAX #29 wants
        // example.com and 3.14 to hold together (WB6/WB7, FULL STOP is
        // MidNumLet). So the word here is "foo.bar" and the next one is "baz".
        const LLWString dotted = utf8str_to_wstring("foo.bar baz");
        ensure_equals("dot does not split", wstring_step_word_forward(dotted, 0), size_t(8));
        ensure_equals("fwd from inside the dot word",
                      wstring_step_word_forward(dotted, 3), size_t(8));

        // A comma does separate, and standing on one used to wedge the cursor:
        // it is neither alnum nor a space, so both of the old loops declined to
        // move and the caller had to retry from pos+1 (LLLineEditor::removeWord
        // still carries that workaround).
        const LLWString comma = utf8str_to_wstring("foo, bar");
        ensure_equals("fwd stops on the comma", wstring_step_word_forward(comma, 0), size_t(3));
        ensure_equals("fwd off the comma", wstring_step_word_forward(comma, 3), size_t(5));
        ensure("fwd never stands still", wstring_step_word_forward(comma, 3) > size_t(3));

        // An apostrophe is inside the word, not a break in it. The old walk
        // stopped at index 3, mid-word.
        const LLWString contraction = utf8str_to_wstring("don't stop");
        ensure_equals("contraction is one word",
                      wstring_step_word_forward(contraction, 0), size_t(6));
        ensure_equals("back over a contraction",
                      wstring_step_word_backward(contraction, 10), size_t(6));

        // Tabs are whitespace to step over; the old test was == ' ' only.
        const LLWString tabbed = utf8str_to_wstring("foo\tbar");
        ensure_equals("fwd over a tab", wstring_step_word_forward(tabbed, 0), size_t(4));

        // Neither direction crosses a newline, matching what the old walk did
        // by accident (a newline is neither a word char nor a space).
        const LLWString lines = utf8str_to_wstring("ab\ncd");
        ensure_equals("fwd stops at eol", wstring_step_word_forward(lines, 0), size_t(2));
        ensure_equals("fwd parked on eol", wstring_step_word_forward(lines, 2), size_t(2));
        ensure_equals("back stops at bol", wstring_step_word_backward(lines, 5), size_t(3));
        ensure_equals("back parked on bol", wstring_step_word_backward(lines, 3), size_t(3));

        // Trailing whitespace has no word after it, so the line's end is where
        // forward stops.
        const LLWString trailing = utf8str_to_wstring("foo   ");
        ensure_equals("fwd into trailing space",
                      wstring_step_word_forward(trailing, 0), size_t(6));

        // Degenerate input.
        const LLWString empty;
        ensure_equals("empty fwd", wstring_step_word_forward(empty, 0), size_t(0));
        ensure_equals("empty back", wstring_step_word_backward(empty, 0), size_t(0));
        ensure_equals("past end fwd", wstring_step_word_forward(two_words, 99), two_words.size());
        ensure_equals("past end back", wstring_step_word_backward(two_words, 99), size_t(4));

        // Scripts without spaces still segment, rather than swallowing the
        // whole run the way an alnum test would.
        const LLWString cjk = utf8str_to_wstring("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E ok");
        ensure("cjk advances", wstring_step_word_forward(cjk, 0) > size_t(0));
        ensure("cjk terminates", wstring_step_word_forward(cjk, 0) <= cjk.size());

        // Adjacent emoji are separate words, so a word step crosses exactly
        // one. Callers must not pre-step a grapheme and then ask for the next
        // word -- that lands two emoji away.
        const LLWString two_emoji = { (llwchar)0x1F436, (llwchar)0x1F431, (llwchar)'x' };
        ensure_equals("one emoji forward", wstring_step_word_forward(two_emoji, 0), size_t(1));
        ensure_equals("one emoji back", wstring_step_word_backward(two_emoji, 2), size_t(1));

        // A ZWJ sequence is a single word, however many codepoints it spans.
        const LLWString flag_then_dog = { (llwchar)0x1F3F3, (llwchar)0xFE0F, (llwchar)0x200D,
                                          (llwchar)0x26A7, (llwchar)0xFE0F, (llwchar)0x1F436 };
        ensure_equals("zwj sequence is one word",
                      wstring_step_word_forward(flag_then_dog, 0), size_t(5));
        ensure_equals("back over the zwj sequence",
                      wstring_step_word_backward(flag_then_dog, 6), size_t(5));
    }

    // UAX #14 line break opportunities, which replaced a spaces-plus-CJK-range
    // heuristic in LLFontGL::maxDrawableChars.
    template<> template<>
    void llstring_utf_object_t::test<119>()
    {
        std::vector<size_t> breaks;

        // A break is offered after the space, so the line keeps its trailing
        // space and the next one starts on the word. This is what the old
        // start_of_last_word tracking computed, and it has to stay that way.
        wstring_line_break_opportunities(utf8str_to_wstring("hello world"), breaks);
        ensure_equals("two opportunities", breaks.size(), size_t(2));
        ensure_equals("after the space", breaks[0], size_t(6));
        ensure_equals("and the end", breaks[1], size_t(11));

        // The string's end is always an opportunity; 0 never is.
        wstring_line_break_opportunities(utf8str_to_wstring("unbroken"), breaks);
        ensure_equals("single word, one opportunity", breaks.size(), size_t(1));
        ensure_equals("at the end", breaks[0], size_t(8));

        // A non-breaking space is glue. The old code special-cased U+00A0 by
        // hand; here it simply produces no opportunity.
        wstring_line_break_opportunities(utf8str_to_wstring("a\xC2\xA0" "b"), breaks);
        ensure_equals("nbsp does not break", breaks.size(), size_t(1));
        ensure_equals("only the end", breaks[0], size_t(3));

        // CJK ideographs may be split between characters...
        const LLWString cjk = utf8str_to_wstring("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        wstring_line_break_opportunities(cjk, breaks);
        ensure("cjk splits between characters", breaks.size() > size_t(1));

        // ...but a line may not begin with closing punctuation, so there is no
        // opportunity immediately before U+3002 IDEOGRAPHIC FULL STOP. The old
        // ideograph-range test would have broken there happily.
        const LLWString cjk_stop = utf8str_to_wstring("\xE6\x97\xA5\xE6\x9C\xAC\xE3\x80\x82");
        wstring_line_break_opportunities(cjk_stop, breaks);
        ensure("no break before the full stop",
               std::find(breaks.begin(), breaks.end(), size_t(2)) == breaks.end());

        // Every opportunity is in range and strictly ascending, since the
        // consumer walks them with a single forward cursor.
        const LLWString mixed = utf8str_to_wstring("one two\xC2\xA0three, four");
        wstring_line_break_opportunities(mixed, breaks);
        ensure("mixed has opportunities", !breaks.empty());
        for (size_t k = 0; k < breaks.size(); ++k)
        {
            ensure("in range", breaks[k] > 0 && breaks[k] <= mixed.size());
            if (k > 0)
            {
                ensure("ascending", breaks[k] > breaks[k - 1]);
            }
        }
        ensure_equals("ends at the end", breaks.back(), mixed.size());

        // Empty input clears the caller's buffer rather than leaving it alone,
        // since the buffer is reused across lines.
        wstring_line_break_opportunities(LLWString(), breaks);
        ensure("empty clears", breaks.empty());
    }

    // Case conversion, and the index map that lets a search fold its haystack
    // without losing track of where a match sits in the original.
    template<> template<>
    void llstring_utf_object_t::test<120>()
    {
        // Uppercasing sharp s produces two characters. towupper could not do
        // this at all -- it maps one codepoint to one codepoint.
        LLWString sharp_s = utf8str_to_wstring("stra\xC3\x9F" "e");
        ensure_equals("sharp s starts at 6", sharp_s.size(), size_t(6));
        LLWStringUtil::toUpper(sharp_s);
        ensure_equals("uppercased grows", wstring_to_utf8str(sharp_s), std::string("STRASSE"));

        LLWString plain = utf8str_to_wstring("Hello");
        LLWStringUtil::toLower(plain);
        ensure_equals("ordinary lowercase", wstring_to_utf8str(plain), std::string("hello"));

        // U+0130 LATIN CAPITAL LETTER I WITH DOT ABOVE lowercases to two
        // codepoints, so every index after it shifts. This is the case that
        // breaks an offset taken from a folded copy.
        const LLWString dotted_i = utf8str_to_wstring("a\xC4\xB0" "b");
        ensure_equals("three codepoints in", dotted_i.size(), size_t(3));

        LLWString folded;
        std::vector<size_t> map;
        wstring_tolower_indexed(dotted_i, folded, &map);
        ensure("fold grew", folded.size() > dotted_i.size());
        ensure_equals("map matches fold", map.size(), folded.size());

        // Every output character points at the input it came from, and the
        // two codepoints from the dotted I both point at index 1.
        ensure_equals("a maps to 0", map[0], size_t(0));
        ensure_equals("b maps to 2", map[map.size() - 1], size_t(2));
        for (size_t k = 1; k + 1 < map.size(); ++k)
        {
            ensure_equals("expansion maps to its source", map[k], size_t(1));
        }

        // The map is non-decreasing, which is what lets a caller invert it with
        // a lower_bound.
        for (size_t k = 1; k < map.size(); ++k)
        {
            ensure("map is non-decreasing", map[k] >= map[k - 1]);
        }

        // The point of all this: find 'b' in the folded copy and land on the
        // right character in the original. Taken raw, the offset would be one
        // past where b actually is.
        const size_t folded_at = folded.find((llwchar)'b');
        ensure("found in fold", folded_at != LLWString::npos);
        ensure("raw offset would be wrong", folded_at != size_t(2));
        ensure_equals("mapped offset is right", map[folded_at], size_t(2));

        // ASCII stays one-to-one, so the map is an identity and existing
        // offsets are unaffected.
        wstring_tolower_indexed(utf8str_to_wstring("ABC"), folded, &map);
        ensure_equals("ascii keeps length", folded.size(), size_t(3));
        for (size_t k = 0; k < map.size(); ++k)
        {
            ensure_equals("ascii map is identity", map[k], k);
        }

        // The map is optional, and empty input clears both outputs.
        wstring_tolower_indexed(utf8str_to_wstring("XY"), folded);
        ensure_equals("no map requested", wstring_to_utf8str(folded), std::string("xy"));
        wstring_tolower_indexed(LLWString(), folded, &map);
        ensure("empty clears fold", folded.empty());
        ensure("empty clears map", map.empty());
    }

    // The narrow forms case UTF-8 now, and the helper that brings an offset
    // taken against a cased copy back onto the text it was built from.
    template<> template<>
    void llstring_utf_object_t::test<121>()
    {
        std::string s = "stra\xC3\x9F" "e";
        LLStringUtil::toUpper(s);
        ensure_equals("narrow toUpper is unicode", s, std::string("STRASSE"));

        std::string acc = "\xC3\xA9" "cole";
        LLStringUtil::toUpper(acc);
        ensure_equals("narrow toUpper accents", acc, std::string("\xC3\x89" "COLE"));

        std::string upper = "\xC3\x89" "COLE";
        LLStringUtil::toLower(upper);
        ensure_equals("narrow toLower accents", upper, std::string("\xC3\xA9" "cole"));

        // ASCII is untouched, which is what the identifier and protocol callers
        // rely on.
        std::string ascii = "Image.PNG";
        LLStringUtil::toLower(ascii);
        ensure_equals("ascii unchanged", ascii, std::string("image.png"));

        // "straße" uppercases to "STRASSE": the search key is one byte and one
        // codepoint longer than the label it came from. An offset past the
        // sharp s must come back to the label's own indexing.
        const std::string label = "stra\xC3\x9F" "e xyz";
        std::string key = label;
        LLStringUtil::toUpper(key);
        const size_t at = key.find("XYZ");
        ensure("found in the key", at != std::string::npos);

        // Raw, that offset is past where xyz sits in the label.
        const size_t mapped = utf8str_length_from_cased_utf8_length(label, at, true);
        ensure_equals("mapped to the label's codepoint index", mapped, size_t(7));

        // And the matched span's own length, which is not the key's: here they
        // agree because xyz is ASCII, but the sharp s shows the difference.
        const size_t span_begin = utf8str_length_from_cased_utf8_length(label, 0, true);
        const size_t span_end = utf8str_length_from_cased_utf8_length(label, key.find(' '), true);
        ensure_equals("word before the space is 6 codepoints", span_end - span_begin, size_t(6));

        // Offset 0 and a whole-string offset are the degenerate ends.
        ensure_equals("zero maps to zero",
                      utf8str_length_from_cased_utf8_length(label, 0, true), size_t(0));
        ensure_equals("whole string maps to its codepoint count",
                      utf8str_length_from_cased_utf8_length(label, key.size(), true), size_t(10));

        // ASCII text maps one-to-one, so existing offsets are unaffected.
        const std::string plain = "hello world";
        ensure_equals("ascii maps straight through",
                      utf8str_length_from_cased_utf8_length(plain, 6, true), size_t(6));

        ensure_equals("empty", utf8str_length_from_cased_utf8_length(std::string(), 4, true), size_t(0));
    }

    // "Which word is here" and "where is the next one", the questions
    // double-click selection, spell check and autoreplace ask.
    template<> template<>
    void llstring_utf_object_t::test<122>()
    {
        const LLWString text = utf8str_to_wstring("don't stop, ok");

        // A contraction is one word. The alnum-or-underscore walk this replaced
        // stopped at the apostrophe and offered "don" or "t".
        auto word = wstring_word_range_at(text, 0);
        ensure_equals("contraction begin", word.first, size_t(0));
        ensure_equals("contraction end", word.second, size_t(5));

        // Anywhere inside it gives the same word.
        for (size_t p = 0; p < 5; ++p)
        {
            const auto at = wstring_word_range_at(text, p);
            ensure_equals("same word from inside", at.first, size_t(0));
            ensure_equals("same word end", at.second, size_t(5));
        }

        // Whitespace and punctuation are segments too, and neither is a word.
        const auto space = wstring_word_range_at(text, 5);
        ensure("space is not a word", space.first == space.second);
        const auto comma = wstring_word_range_at(text, 10);
        ensure("comma is not a word", comma.first == comma.second);

        // Iterating: each call yields the next word, skipping what is between.
        std::vector<std::string> found;
        size_t at = 0;
        while (at < text.size())
        {
            const auto next = wstring_next_word_range(text, at);
            if (next.first >= next.second)
                break;
            found.push_back(wstring_to_utf8str(text.substr(next.first, next.second - next.first)));
            at = next.second;
        }
        ensure_equals("three words", found.size(), size_t(3));
        ensure_equals("first", found[0], std::string("don't"));
        ensure_equals("second", found[1], std::string("stop"));
        ensure_equals("third", found[2], std::string("ok"));

        // Iteration crosses lines, which the spell checker relies on.
        const LLWString lines = utf8str_to_wstring("one\ntwo");
        const auto first = wstring_next_word_range(lines, 0);
        ensure_equals("first line word end", first.second, size_t(3));
        const auto second = wstring_next_word_range(lines, first.second);
        ensure_equals("second line word begin", second.first, size_t(4));
        ensure_equals("second line word end", second.second, size_t(7));

        // Past the end, and past the last word, both terminate.
        const auto none = wstring_next_word_range(lines, 7);
        ensure("no more words", none.first == none.second);
        const auto oob = wstring_word_range_at(lines, 99);
        ensure("past end is empty", oob.first == oob.second);

        const LLWString empty;
        const auto on_empty = wstring_word_range_at(empty, 0);
        ensure("empty string", on_empty.first == on_empty.second);

        // A whole string that is one word -- what autoreplace validates a
        // keyword with.
        const LLWString keyword = utf8str_to_wstring("don't");
        const auto whole = wstring_word_range_at(keyword, 0);
        ensure("keyword is one word",
               whole.first == size_t(0) && whole.second == keyword.size());
        const LLWString two_words = utf8str_to_wstring("not one");
        const auto partial = wstring_word_range_at(two_words, 0);
        ensure("two words is not one", partial.second != two_words.size());
    }

    // Classification above the BMP. Every one of these codepoints used to be
    // handed to <cwctype>, whose wint_t is 16 bits on Windows -- so each
    // arrived with its top half cut off and was answered for by whatever
    // happened to live at the remaining address.
    template<> template<>
    void llstring_utf_object_t::test<123>()
    {
        // The low half of each is given as the answer it used to produce.
        const llwchar CJK_EXT_B_A   = 0x2000A;  // -> U+000A, a line feed
        const llwchar CJK_EXT_B_ONE = 0x20031;  // -> U+0031, digit one
        const llwchar LINEAR_B      = 0x10020;  // -> U+0020, a space
        const llwchar DESERET_CAP   = 0x10401;  // -> U+0401, Cyrillic Io
        const llwchar DESERET_SMALL = 0x10429;
        const llwchar MATH_BOLD_A   = 0x1D400;  // -> U+D400, a lone surrogate
        const llwchar GRINNING_FACE = 0x1F600;

        ensure("CJK ideograph is not whitespace", !LLStringOps::isSpace(CJK_EXT_B_A));
        ensure("CJK ideograph is alphanumeric",   LLStringOps::isAlnum(CJK_EXT_B_A));
        ensure("CJK ideograph is not a digit",   !LLStringOps::isDigit(CJK_EXT_B_ONE));
        ensure("CJK ideograph is alphanumeric",   LLStringOps::isAlnum(CJK_EXT_B_ONE));
        ensure("Linear B is not whitespace",     !LLStringOps::isSpace(LINEAR_B));
        ensure("math capital is a letter",        LLStringOps::isAlpha(MATH_BOLD_A));
        ensure("emoji is not alphanumeric",      !LLStringOps::isAlnum(GRINNING_FACE));
        ensure("emoji is not whitespace",        !LLStringOps::isSpace(GRINNING_FACE));

        // Case above the BMP: this used to return an unrelated codepoint
        // rather than fail to convert.
        // char32_t has no operator<<, so the comparands are widened for the
        // failure message rather than compared as they stand.
        const auto cased = [](llwchar c) { return (U32)c; };

        ensure("Deseret capital is upper", LLStringOps::isUpper(DESERET_CAP));
        ensure_equals("Deseret capital lowercases within Deseret",
                      cased(LLStringOps::toLower(DESERET_CAP)), cased(DESERET_SMALL));
        ensure_equals("Deseret small uppercases back",
                      cased(LLStringOps::toUpper(DESERET_SMALL)), cased(DESERET_CAP));

        // Case inside the BMP, which towlower left alone entirely.
        ensure_equals("Greek sigma lowercases",   cased(LLStringOps::toLower(llwchar(0x03A3))), cased(0x03C3));
        ensure_equals("Cyrillic Io lowercases",   cased(LLStringOps::toLower(llwchar(0x0401))), cased(0x0451));
        ensure_equals("Cyrillic io uppercases",   cased(LLStringOps::toUpper(llwchar(0x0451))), cased(0x0401));

        // Whitespace is the Unicode property, not a guess.
        ensure("no-break space is whitespace",     LLStringOps::isSpace(llwchar(0x00A0)));
        ensure("ideographic space is whitespace",  LLStringOps::isSpace(llwchar(0x3000)));
        ensure("zero width space is not",         !LLStringOps::isSpace(llwchar(0x200B)));

        // Digits are ASCII on purpose -- every caller reads the run as a number.
        ensure("Arabic-Indic zero is not a digit", !LLStringOps::isDigit(llwchar(0x0660)));
        ensure("Arabic-Indic zero is alphanumeric", LLStringOps::isAlnum(llwchar(0x0660)));

        // ASCII keeps answering as it always did.
        ensure("A is upper",     LLStringOps::isUpper(llwchar('A')));
        ensure("a is lower",     LLStringOps::isLower(llwchar('a')));
        ensure("5 is a digit",   LLStringOps::isDigit(llwchar('5')));
        ensure("space is space", LLStringOps::isSpace(llwchar(' ')));
        ensure("dot is punct",   LLStringOps::isPunct(llwchar('.')));
        ensure_equals("A lowercases", cased(LLStringOps::toLower(llwchar('A'))), cased(llwchar('a')));
    }

    // The consequence of the above for the cursor: a CJK ideograph whose low
    // half is a line feed must not read as a run of whitespace to step over.
    template<> template<>
    void llstring_utf_object_t::test<124>()
    {
        LLWString text = utf8str_to_wstring("ab");
        text.push_back(0x2000A);
        text.push_back(0x2000A);
        text += utf8str_to_wstring(" cd");

        // Each ideograph is its own segment, and none of them is whitespace,
        // so the cursor stops at the first. Reading their low halves as line
        // feeds made all three of the ideographs and the space look like one
        // gap, and the cursor jumped clear to "cd" at 5.
        ensure_equals("cursor stops at the ideograph",
                      wstring_step_word_forward(text, 0), size_t(2));

        const auto range = wstring_word_range_at(text, 2);
        ensure("an ideograph is a word", range.first != range.second);
    }

    // utf8str_grapheme_align_backward: the byte offsets are the caller's own,
    // and a cut backs off to where a whole character starts. Everything that
    // trims UTF-8 to a length leans on this.
    template<> template<>
    void llstring_utf_object_t::test<125>()
    {
        // ASCII: every offset is already a boundary.
        const std::string ascii = "abcd";
        for (size_t i = 0; i <= ascii.size(); ++i)
        {
            ensure_equals("ascii is all boundaries",
                          utf8str_grapheme_align_backward(ascii, i), i);
        }

        // Inside a multi-byte codepoint, back to where it starts. "A" + U+65E5
        // (3 bytes) + "B".
        const std::string cjk = "A\xE6\x97\xA5" "B";
        ensure_equals("before the codepoint", utf8str_grapheme_align_backward(cjk, 1), size_t(1));
        ensure_equals("inside it, byte 2",    utf8str_grapheme_align_backward(cjk, 2), size_t(1));
        ensure_equals("inside it, byte 3",    utf8str_grapheme_align_backward(cjk, 3), size_t(1));
        ensure_equals("after it",             utf8str_grapheme_align_backward(cjk, 4), size_t(4));

        // A base and its combining mark are one character: "e" + U+0301.
        const std::string combining = "e\xCC\x81" "x";
        ensure_equals("between base and mark",
                      utf8str_grapheme_align_backward(combining, 1), size_t(0));
        ensure_equals("inside the mark",
                      utf8str_grapheme_align_backward(combining, 2), size_t(0));
        ensure_equals("after the whole cluster",
                      utf8str_grapheme_align_backward(combining, 3), size_t(3));

        // A flag is two codepoints and eight bytes, and one character. Cutting
        // anywhere inside gives nothing rather than half a flag.
        const std::string flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
        for (size_t i = 1; i < flag.size(); ++i)
        {
            ensure_equals("no half flags", utf8str_grapheme_align_backward(flag, i), size_t(0));
        }
        ensure_equals("whole flag survives",
                      utf8str_grapheme_align_backward(flag, flag.size()), flag.size());

        // Degenerate input.
        ensure_equals("zero",  utf8str_grapheme_align_backward(cjk, 0), size_t(0));
        ensure_equals("past end", utf8str_grapheme_align_backward(cjk, 99), cjk.size());
        ensure_equals("empty", utf8str_grapheme_align_backward(std::string(), 0), size_t(0));
    }

    // The walkers work in UTF-8 and the wide entry points are adapters over
    // them, so walking the same text both ways has to land in the same places.
    // Text with a byte per codepoint, three, two, and a two-codepoint cluster,
    // so the two index spaces disagree everywhere after the first space.
    template<> template<>
    void llstring_utf_object_t::test<126>()
    {
        const std::string utf8 =
            "ab "                                   // ascii
            "\xE6\x97\xA5\xE6\x9C\xAC"              // CJK, three bytes each
            " e\xCC\x81 "                           // e + combining acute
            "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"      // flag: two codepoints, one cluster
            " end";
        const LLWString wide = utf8str_to_wstring(utf8);

        // Grapheme stepping, all the way across, comparing the text each step
        // covered rather than the indices -- the indices are meant to differ.
        size_t byte_pos = 0;
        size_t cp_pos = 0;
        while (byte_pos < utf8.size())
        {
            const size_t next_byte = utf8str_step_grapheme_forward(utf8, byte_pos);
            const size_t next_cp = wstring_step_grapheme_forward(wide, cp_pos);
            ensure("byte walk advances", next_byte > byte_pos);
            ensure("codepoint walk advances", next_cp > cp_pos);
            ensure_equals("same cluster both ways",
                          wstring_to_utf8str(wide.substr(cp_pos, next_cp - cp_pos)),
                          utf8.substr(byte_pos, next_byte - byte_pos));
            byte_pos = next_byte;
            cp_pos = next_cp;
        }
        ensure_equals("both walks ended together", cp_pos, wide.size());

        // And backwards.
        while (byte_pos > 0)
        {
            const size_t prev_byte = utf8str_step_grapheme_backward(utf8, byte_pos);
            const size_t prev_cp = wstring_step_grapheme_backward(wide, cp_pos);
            ensure_equals("same cluster stepping back",
                          wstring_to_utf8str(wide.substr(prev_cp, cp_pos - prev_cp)),
                          utf8.substr(prev_byte, byte_pos - prev_byte));
            byte_pos = prev_byte;
            cp_pos = prev_cp;
        }
        ensure_equals("both walks came back to nothing", cp_pos, size_t(0));

        // Word stepping likewise.
        byte_pos = 0;
        cp_pos = 0;
        for (int step = 0; step < 8; ++step)
        {
            byte_pos = utf8str_step_word_forward(utf8, byte_pos);
            cp_pos = wstring_step_word_forward(wide, cp_pos);
            ensure_equals("word steps agree",
                          wstring_to_utf8str(wide.substr(0, cp_pos)),
                          utf8.substr(0, byte_pos));
        }

        // The word under a position, asked of both.
        const size_t cjk_byte = utf8.find("\xE6\x97\xA5");
        const size_t cjk_cp = wstring_wstring_length_from_utf8_length(wide, 0, (S32)cjk_byte);
        const auto byte_range = utf8str_word_range_at(utf8, cjk_byte);
        const auto cp_range = wstring_word_range_at(wide, cjk_cp);
        ensure_equals("same word both ways",
                      wstring_to_utf8str(wide.substr(cp_range.first, cp_range.second - cp_range.first)),
                      utf8.substr(byte_range.first, byte_range.second - byte_range.first));

        // Line break opportunities, as byte offsets against codepoint indices.
        std::vector<size_t> byte_breaks;
        std::vector<size_t> cp_breaks;
        utf8str_line_break_opportunities(utf8, byte_breaks);
        wstring_line_break_opportunities(wide, cp_breaks);
        ensure_equals("same number of opportunities", byte_breaks.size(), cp_breaks.size());
        for (size_t i = 0; i < byte_breaks.size(); ++i)
        {
            ensure_equals("opportunity " + std::to_string(i) + " is the same place",
                          wstring_to_utf8str(wide.substr(0, cp_breaks[i])),
                          utf8.substr(0, byte_breaks[i]));
        }

        // A flag is one cluster of eight bytes, and nothing cuts it in half.
        const size_t flag = utf8.find("\xF0\x9F\x87\xBA");
        ensure_equals("flag steps whole",
                      utf8str_step_grapheme_forward(utf8, flag), flag + 8);
        for (size_t inside = 1; inside < 8; ++inside)
        {
            ensure_equals("inside the flag aligns back to its start",
                          utf8str_grapheme_align_backward(utf8, flag + inside), flag);
            ensure_equals("and forward to its end",
                          utf8str_grapheme_align_forward(utf8, flag + inside), flag + 8);
        }
    }
}
